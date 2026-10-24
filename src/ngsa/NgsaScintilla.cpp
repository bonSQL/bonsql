/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA (M13): Scintilla 5 + Lexilla loader, lexer selection, and the Messages-tab lexer.
 * See include/ngsa/NgsaScintilla.h.
 */
#include "ngsa/NgsaScintilla.h"
#include <string.h>
#include "Scintilla.h"
#include "ILexer.h"
#include "Lexilla.h"

namespace
{
    HMODULE             s_hscintilla = NULL;
    HMODULE             s_hlexilla   = NULL;
    Lexilla::CreateLexerFn s_createlexer = NULL;

    /* Messages tab: each message starts with a tag line "<e>", "<r>", "<n>" or "<w>" (QueryThread.cpp);
       the tag characters get the *TAG style (hidden by TabMessage) and the rest of the message, up to
       the next tag, the matching body style. The body style is kept in the line state so that lexing
       can resume from any line. TabMessage filters by the style of each line's first character. */
    class NgsaMsgLexer : public Scintilla::ILexer5
    {
    public:
        int SCI_METHOD Version() const override { return Scintilla::lvRelease5; }
        void SCI_METHOD Release() override { delete this; }
        const char * SCI_METHOD PropertyNames() override { return ""; }
        int SCI_METHOD PropertyType(const char *) override { return -1; }
        const char * SCI_METHOD DescribeProperty(const char *) override { return ""; }
        Sci_Position SCI_METHOD PropertySet(const char *, const char *) override { return -1; }
        const char * SCI_METHOD DescribeWordListSets() override { return ""; }
        Sci_Position SCI_METHOD WordListSet(int, const char *) override { return -1; }
        void SCI_METHOD Fold(Sci_PositionU, Sci_Position, int, Scintilla::IDocument *) override {}
        void * SCI_METHOD PrivateCall(int, void *) override { return nullptr; }
        int SCI_METHOD LineEndTypesSupported() override { return 0; }
        int SCI_METHOD AllocateSubStyles(int, int) override { return -1; }
        int SCI_METHOD SubStylesStart(int) override { return -1; }
        int SCI_METHOD SubStylesLength(int) override { return 0; }
        int SCI_METHOD StyleFromSubStyle(int subStyle) override { return subStyle; }
        int SCI_METHOD PrimaryStyleFromStyle(int style) override { return style; }
        void SCI_METHOD FreeSubStyles() override {}
        void SCI_METHOD SetIdentifiers(int, const char *) override {}
        int SCI_METHOD DistanceToSecondaryStyles() override { return 0; }
        const char * SCI_METHOD GetSubStyleBases() override { return ""; }
        int SCI_METHOD NamedStyles() override { return 0; }
        const char * SCI_METHOD NameOfStyle(int) override { return ""; }
        const char * SCI_METHOD TagsOfStyle(int) override { return ""; }
        const char * SCI_METHOD DescriptionOfStyle(int) override { return ""; }
        const char * SCI_METHOD GetName() override { return "SQLyogMsg"; }
        int SCI_METHOD GetIdentifier() override { return 0; }
        const char * SCI_METHOD PropertyGet(const char *) override { return ""; }

        void SCI_METHOD Lex(Sci_PositionU startPos, Sci_Position lengthDoc, int, Scintilla::IDocument *doc) override
        {
            const Sci_Position docLength = doc->Length();
            Sci_Position line = doc->LineFromPosition((Sci_Position)startPos);
            Sci_Position pos = doc->LineStart(line);
            const Sci_Position endPos = (Sci_Position)startPos + lengthDoc;
            int state = line > 0 ? doc->GetLineState(line - 1) : SCE_SQLYOGMSG_DEFAULT;

            doc->StartStyling(pos);
            while(pos < endPos && pos < docLength)
            {
                Sci_Position next = doc->LineStart(line + 1);
                if(next <= pos)
                    next = docLength;
                const Sci_Position len = next - pos;
                char head[3] = {0, 0, 0};
                int tag = 0, body = state;

                if(len >= 3)
                {
                    doc->GetCharRange(head, pos, 3);
                    if(head[0] == '<' && head[2] == '>')
                    {
                        switch(head[1])
                        {
                        case 'e': tag = SCE_SQLYOGMSG_ERRORTAG;    body = SCE_SQLYOGMSG_ERROR;    break;
                        case 'r': tag = SCE_SQLYOGMSG_RESULTTAG;   body = SCE_SQLYOGMSG_RESULT;   break;
                        case 'n': tag = SCE_SQLYOGMSG_NORESULTTAG; body = SCE_SQLYOGMSG_NORESULT; break;
                        case 'w': tag = SCE_SQLYOGMSG_WARNINGTAG;  body = SCE_SQLYOGMSG_WARNING;  break;
                        }
                    }
                }
                if(tag)
                {
                    doc->SetStyleFor(3, (char)tag);
                    doc->SetStyleFor(len - 3, (char)body);
                }
                else
                {
                    doc->SetStyleFor(len, (char)body);
                }
                state = body;
                doc->SetLineState(line, state);
                pos = next;
                line++;
            }
        }
    };
}

HMODULE
NgsaScintilla::Load()
{
    if(!s_hscintilla)
        s_hscintilla = ::LoadLibrary(L"Scintilla.dll");
    if(!s_hlexilla)
    {
        s_hlexilla = ::LoadLibrary(L"Lexilla.dll");
        if(s_hlexilla)
            s_createlexer = (Lexilla::CreateLexerFn)::GetProcAddress(s_hlexilla, LEXILLA_CREATELEXER);
    }
    return (s_hscintilla && s_createlexer) ? s_hscintilla : NULL;
}

wyBool
NgsaScintilla::SetLexer(HWND hwndedit, const wyChar* name)
{
    Scintilla::ILexer5* lexer = NULL;

    if(!hwndedit || !name)
        return wyFalse;

    if(_stricmp(name, "SQLyogMsg") == 0)
        lexer = new NgsaMsgLexer();
    else
    {
        if(!s_createlexer && !Load())
            return wyFalse;
        if(_stricmp(name, "MySQL") == 0 || _stricmp(name, "LineMySQL") == 0)
            lexer = s_createlexer("mysql");
        else
            lexer = s_createlexer(name);
    }
    if(!lexer)
        return wyFalse;

    /* Scintilla owns the lexer from here (it calls Release when replaced or on destruction) */
    SendMessage(hwndedit, SCI_SETILEXER, 0, (LPARAM)lexer);
    return wyTrue;
}
