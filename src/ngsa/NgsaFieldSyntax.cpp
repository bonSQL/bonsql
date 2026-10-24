/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA (M12): syntax colouring for the Blob/Text/JSON field viewer, see include/ngsa/NgsaFieldSyntax.h.
 */
#include "ngsa/NgsaFieldSyntax.h"
#include "Global.h"
#include "FrameWindowHelper.h"
#include "CommonHelper.h"
#include "GUIHelper.h"
#include "MDIWindow.h"
#include "EditorFont.h"
#include "wyTheme.h"
#include "wyIni.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "ngsa/NgsaScintilla.h"
#include "jsoncpp.h"      // NGSA (M14): JSON pretty-print (also used by the JsonFormat preference in DataView)
#include "tinyxml2.h"     // NGSA (M14): XML pretty-print (already linked for the theme files)

#define NGSA_SYNTAX_SECTION "NgsaFieldSyntax"

namespace
{
    struct Palette
    {
        COLORREF bg, normal, keyword, string, number, comment, function, op;
    };

    void IniPath(wyString& path)
    {
        wyWChar* lpfileport = 0;
        wyWChar  directory[MAX_PATH + 1] = {0};
        SearchFilePath(BRAND_CFG_STEM_W, L".ini", MAX_PATH, directory, &lpfileport);
        path.SetAs(directory);
    }

    /* The user's editor colours; under the dark theme the theme's editor colours where it defines them. */
    void LoadPalette(Palette& p)
    {
        wyString ini;
        IniPath(ini);
        p.bg       = wyIni::IniGetInt(GENERALPREFA, "EditorBgColor", DEF_BKGNDEDITORCOLOR, ini.GetString());
        p.normal   = wyIni::IniGetInt(GENERALPREFA, "NormalColor",   DEF_NORMALCOLOR,      ini.GetString());
        p.keyword  = wyIni::IniGetInt(GENERALPREFA, "KeywordColor",  DEF_KEYWORDCOLOR,     ini.GetString());
        p.string   = wyIni::IniGetInt(GENERALPREFA, "StringColor",   DEF_STRINGCOLOR,      ini.GetString());
        p.number   = wyIni::IniGetInt(GENERALPREFA, "NumberColor",   RGB(128, 0, 128),     ini.GetString());
        p.comment  = wyIni::IniGetInt(GENERALPREFA, "CommentColor",  DEF_COMMENTCOLOR,     ini.GetString());
        p.function = wyIni::IniGetInt(GENERALPREFA, "FunctionColor", DEF_FUNCTIONCOLOR,    ini.GetString());
        p.op       = wyIni::IniGetInt(GENERALPREFA, "OperatorColor", RGB(128, 0, 0),       ini.GetString());

        EDITORCOLORINFO ec;
        if(wyTheme::IsDarkThemeActive() && wyTheme::GetEditorColors(&ec))
        {
            if(ec.m_mask & EDITORCF_BACKGROUND) p.bg       = ec.m_background;
            if(ec.m_mask & EDITORCF_FOREGROUND) p.normal   = ec.m_foreground;
            if(ec.m_mask & EDITORCF_NORMAL)     p.normal   = ec.m_normal;
            if(ec.m_mask & EDITORCF_KEYWORD)    p.keyword  = ec.m_keyword;
            if(ec.m_mask & EDITORCF_STRING)     p.string   = ec.m_string;
            if(ec.m_mask & EDITORCF_NUMBER)     p.number   = ec.m_number;
            if(ec.m_mask & EDITORCF_COMMENT)    p.comment  = ec.m_comment;
            if(ec.m_mask & EDITORCF_FUNCTION)   p.function = ec.m_function;
            if(ec.m_mask & EDITORCF_OPERATOR)   p.op       = ec.m_operator;
        }
    }

    inline void Fore(HWND h, int style, COLORREF c)   { SendMessage(h, SCI_STYLESETFORE, style, c); }
    inline void Bold(HWND h, int style, BOOL b = TRUE) { SendMessage(h, SCI_STYLESETBOLD, style, b); }
    inline void Italic(HWND h, int style)             { SendMessage(h, SCI_STYLESETITALIC, style, TRUE); }

    void StylesJson(HWND h, const Palette& p)
    {
        /* Lexilla's json lexer (M13): property names, values, escapes and errors are told apart */
        NgsaScintilla::SetLexer(h, "json");
        SendMessage(h, SCI_SETKEYWORDS, 0, (LPARAM)"true false null");
        Fore(h, SCE_JSON_PROPERTYNAME, p.function);
        Fore(h, SCE_JSON_STRING, p.string);  Fore(h, SCE_JSON_STRINGEOL, p.string);
        Fore(h, SCE_JSON_ESCAPESEQUENCE, p.op);
        Fore(h, SCE_JSON_NUMBER, p.number);
        Fore(h, SCE_JSON_KEYWORD, p.keyword);  Bold(h, SCE_JSON_KEYWORD);  Fore(h, SCE_JSON_LDKEYWORD, p.keyword);
        Fore(h, SCE_JSON_OPERATOR, p.op);
        Fore(h, SCE_JSON_LINECOMMENT, p.comment);  Fore(h, SCE_JSON_BLOCKCOMMENT, p.comment);
        Fore(h, SCE_JSON_URI, p.function);
        Fore(h, SCE_JSON_ERROR, RGB(255, 0, 0));  Bold(h, SCE_JSON_ERROR);
    }

    void StylesMarkup(HWND h, const Palette& p, wyBool html)
    {
        NgsaScintilla::SetLexer(h, html ? "hypertext" : "xml");
        /* no tag/attribute keyword lists: unknown tags and attributes get the same colours as known ones */
        Fore(h, SCE_H_TAG, p.keyword);           Fore(h, SCE_H_TAGUNKNOWN, p.keyword);
        Fore(h, SCE_H_TAGEND, p.keyword);        Fore(h, SCE_H_XMLSTART, p.keyword);  Fore(h, SCE_H_XMLEND, p.keyword);
        Fore(h, SCE_H_ATTRIBUTE, p.function);    Fore(h, SCE_H_ATTRIBUTEUNKNOWN, p.function);
        Fore(h, SCE_H_DOUBLESTRING, p.string);   Fore(h, SCE_H_SINGLESTRING, p.string);  Fore(h, SCE_H_VALUE, p.string);
        Fore(h, SCE_H_NUMBER, p.number);
        Fore(h, SCE_H_COMMENT, p.comment);       Fore(h, SCE_H_CDATA, p.comment);
        Fore(h, SCE_H_ENTITY, p.op);
        if(html)
        {
            /* embedded JavaScript (SCE_HJ_*) and CSS inside <style> follow the same palette */
            Fore(h, SCE_HJ_KEYWORD, p.keyword);  Bold(h, SCE_HJ_KEYWORD);
            Fore(h, SCE_HJ_DOUBLESTRING, p.string); Fore(h, SCE_HJ_SINGLESTRING, p.string);
            Fore(h, SCE_HJ_NUMBER, p.number);
            Fore(h, SCE_HJ_COMMENT, p.comment);  Fore(h, SCE_HJ_COMMENTLINE, p.comment);
            Fore(h, SCE_HJ_SYMBOLS, p.op);
            SendMessage(h, SCI_SETKEYWORDS, 1, (LPARAM)"abstract boolean break byte case catch char class const continue debugger default delete do double else enum export extends final finally float for function goto if implements import in instanceof int interface let long native new null package private protected public return short static super switch synchronized this throw throws transient try typeof var void volatile while with true false undefined");
        }
    }

    void StylesCss(HWND h, const Palette& p)
    {
        NgsaScintilla::SetLexer(h, "css");
        Fore(h, SCE_CSS_TAG, p.keyword);         Bold(h, SCE_CSS_TAG);
        Fore(h, SCE_CSS_CLASS, p.function);      Fore(h, SCE_CSS_ID, p.function);
        Fore(h, SCE_CSS_PSEUDOCLASS, p.function); Fore(h, SCE_CSS_UNKNOWN_PSEUDOCLASS, p.function);
        Fore(h, SCE_CSS_IDENTIFIER, p.keyword);  Fore(h, SCE_CSS_UNKNOWN_IDENTIFIER, p.keyword);
        Fore(h, SCE_CSS_VALUE, p.number);
        Fore(h, SCE_CSS_DOUBLESTRING, p.string); Fore(h, SCE_CSS_SINGLESTRING, p.string);
        Fore(h, SCE_CSS_COMMENT, p.comment);
        Fore(h, SCE_CSS_OPERATOR, p.op);
        Fore(h, SCE_CSS_IMPORTANT, p.keyword);   Bold(h, SCE_CSS_IMPORTANT);
        Fore(h, SCE_CSS_ATTRIBUTE, p.function);
    }

    void StylesSql(HWND h, const Palette& p)
    {
        MDIWindow* wnd = GetActiveWin();
        wyString   keys, funcs;

        NgsaScintilla::SetLexer(h, "MySQL");
        if(wnd)
        {
            MDIWindow::GetScintillaKeyWordsAndFunctions(wnd->m_tunnel, wnd->m_mysql, wyTrue, keys);
            MDIWindow::GetScintillaKeyWordsAndFunctions(wnd->m_tunnel, wnd->m_mysql, wyFalse, funcs);
            EditorFont::SetLexerWords(h, keys, funcs);
        }
        Fore(h, SCE_MYSQL_MAJORKEYWORD, p.keyword); Bold(h, SCE_MYSQL_MAJORKEYWORD);
        Fore(h, SCE_MYSQL_KEYWORD, p.keyword);      Bold(h, SCE_MYSQL_KEYWORD);
        Fore(h, SCE_MYSQL_PROCEDUREKEYWORD, p.keyword);
        Fore(h, SCE_MYSQL_FUNCTION, p.function);
        Fore(h, SCE_MYSQL_STRING, p.string);  Fore(h, SCE_MYSQL_SQSTRING, p.string);  Fore(h, SCE_MYSQL_DQSTRING, p.string);
        Fore(h, SCE_MYSQL_NUMBER, p.number);
        Fore(h, SCE_MYSQL_OPERATOR, p.op);
        Fore(h, SCE_MYSQL_COMMENT, p.comment);  Fore(h, SCE_MYSQL_COMMENTLINE, p.comment);  Fore(h, SCE_MYSQL_HIDDENCOMMAND, p.comment);
    }

    void StylesMarkdown(HWND h, const Palette& p)
    {
        NgsaScintilla::SetLexer(h, "markdown");
        for(int s = SCE_MARKDOWN_HEADER1; s <= SCE_MARKDOWN_HEADER6; s++)
        {
            Fore(h, s, p.keyword);
            Bold(h, s);
        }
        Bold(h, SCE_MARKDOWN_STRONG1);    Bold(h, SCE_MARKDOWN_STRONG2);
        Italic(h, SCE_MARKDOWN_EM1);      Italic(h, SCE_MARKDOWN_EM2);
        Fore(h, SCE_MARKDOWN_CODE, p.string);  Fore(h, SCE_MARKDOWN_CODE2, p.string);  Fore(h, SCE_MARKDOWN_CODEBK, p.string);
        Fore(h, SCE_MARKDOWN_LINK, p.function);
        Fore(h, SCE_MARKDOWN_BLOCKQUOTE, p.comment);
        Fore(h, SCE_MARKDOWN_ULIST_ITEM, p.op);   Fore(h, SCE_MARKDOWN_OLIST_ITEM, p.op);
        Fore(h, SCE_MARKDOWN_HRULE, p.op);
        Fore(h, SCE_MARKDOWN_STRIKEOUT, p.comment);
    }

    /* case-insensitive "needle in the first n bytes of hay" */
    bool Contains(const wyChar* hay, wyInt32 n, const wyChar* needle)
    {
        size_t m = strlen(needle);
        for(wyInt32 i = 0; i + (wyInt32)m <= n; i++)
            if(_strnicmp(hay + i, needle, m) == 0)
                return true;
        return false;
    }
}

const wyWChar*
NgsaFieldSyntax::Name(wyInt32 syntax)
{
    static const wyWChar* names[NGSA_SYNTAX_COUNT] = { L"None", L"JSON", L"XML", L"HTML", L"CSS", L"SQL", L"Markdown" };
    return (syntax >= 0 && syntax < NGSA_SYNTAX_COUNT) ? names[syntax] : L"None";
}

wyInt32
NgsaFieldSyntax::Detect(const wyChar* text, wyInt32 length, wyBool isjsoncolumn)
{
    wyInt32 i = 0;

    if(!text || length <= 0)
        return isjsoncolumn ? NGSA_SYNTAX_JSON : NGSA_SYNTAX_NONE;

    if(length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        i = 3;
    while(i < length && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n'))
        i++;
    if(i >= length)
        return isjsoncolumn ? NGSA_SYNTAX_JSON : NGSA_SYNTAX_NONE;

    if(text[i] == '{' || text[i] == '[')
        return NGSA_SYNTAX_JSON;
    if(text[i] == '<')
    {
        wyInt32 n = length - i < 512 ? length - i : 512;
        if(Contains(text + i, n, "<html") || Contains(text + i, n, "<!doctype html"))
            return NGSA_SYNTAX_HTML;
        return NGSA_SYNTAX_XML;
    }
    return isjsoncolumn ? NGSA_SYNTAX_JSON : NGSA_SYNTAX_NONE;
}

wyInt32
NgsaFieldSyntax::Load(const wyChar* colkey)
{
    wyString ini;
    wyInt32  v;

    if(!colkey || !*colkey)
        return -1;
    IniPath(ini);
    v = wyIni::IniGetInt(NGSA_SYNTAX_SECTION, colkey, -1, ini.GetString());
    return (v >= 0 && v < NGSA_SYNTAX_COUNT) ? v : -1;
}

void
NgsaFieldSyntax::Save(const wyChar* colkey, wyInt32 syntax)
{
    wyString ini;

    if(!colkey || !*colkey || syntax < 0 || syntax >= NGSA_SYNTAX_COUNT)
        return;
    IniPath(ini);
    wyIni::IniWriteInt(NGSA_SYNTAX_SECTION, colkey, syntax, ini.GetString());
}

void
NgsaFieldSyntax::Apply(HWND h, wyInt32 syntax)
{
    Palette p;

    if(!h)
        return;
    LoadPalette(p);

    /* every style starts from the default font (set by EditorFont::SetFont) and the palette's base colours */
    SendMessage(h, SCI_STYLESETFORE, STYLE_DEFAULT, p.normal);
    SendMessage(h, SCI_STYLESETBACK, STYLE_DEFAULT, p.bg);
    SendMessage(h, SCI_STYLECLEARALL, 0, 0);
    SendMessage(h, SCI_SETCARETFORE, p.bg ^ 0xFFFFFF, 0);

    switch(syntax)
    {
    case NGSA_SYNTAX_JSON:     StylesJson(h, p); break;
    case NGSA_SYNTAX_XML:      StylesMarkup(h, p, wyFalse); break;
    case NGSA_SYNTAX_HTML:     StylesMarkup(h, p, wyTrue); break;
    case NGSA_SYNTAX_CSS:      StylesCss(h, p); break;
    case NGSA_SYNTAX_SQL:      StylesSql(h, p); break;
    case NGSA_SYNTAX_MARKDOWN: StylesMarkdown(h, p); break;
    default:
        NgsaScintilla::SetLexer(h, "null");
        break;
    }

    SendMessage(h, SCI_COLOURISE, 0, -1);
}

wyBool
NgsaFieldSyntax::CanFormat(wyInt32 syntax)
{
    return (syntax == NGSA_SYNTAX_JSON || syntax == NGSA_SYNTAX_XML) ? wyTrue : wyFalse;
}

wyBool
NgsaFieldSyntax::Format(wyInt32 syntax, const wyChar* text, wyInt32 length, wyString& out, wyString& error)
{
    out.Clear();
    error.Clear();
    if(!text || length < 0)
        return wyFalse;

    if(syntax == NGSA_SYNTAX_JSON)
    {
        Json::Reader    reader;
        Json::Value     value;

        if(!reader.parse(text, text + length, value))
        {
            error.SetAs(reader.getFormattedErrorMessages().c_str());
            return wyFalse;
        }
        out.SetAs(value.toStyledString().c_str());
        return wyTrue;
    }

    if(syntax == NGSA_SYNTAX_XML)
    {
        tinyxml2::XMLDocument   doc;
        tinyxml2::XMLPrinter    printer;

        if(doc.Parse(text, (size_t)length) != tinyxml2::XML_SUCCESS)
        {
            /* this tinyxml2 has ErrorName()/GetErrorStr1(), not ErrorStr() */
            error.SetAs(doc.ErrorName() ? doc.ErrorName() : "XML parse error");
            if(doc.GetErrorStr1() && *doc.GetErrorStr1())
                error.AddSprintf(": %s", doc.GetErrorStr1());
            return wyFalse;
        }
        doc.Print(&printer);
        out.SetAs(printer.CStr());
        return wyTrue;
    }
    return wyFalse;
}
