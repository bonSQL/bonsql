/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA (M13): Scintilla 5 + Lexilla loader and lexer selection.
 *
 * Upstream shipped a 2011 SciLexer.dll patched with two private lexers ("SQLyogMsg" for the
 * Messages tab, "LineMySQL" for the history tabs) and selected lexers by name through
 * SCI_SETLEXERLANGUAGE. Scintilla 5 removed that message: lexers are objects created by Lexilla
 * (CreateLexer) or by the application, handed over with SCI_SETILEXER. This module keeps the old
 * call sites simple: SetLexer(hwnd, "MySQL") maps the historical names to Lexilla's lexers and
 * supplies the Messages-tab lexer from NgsaMsgLexer.cpp.
 */
#ifndef NGSA_SCINTILLA_H
#define NGSA_SCINTILLA_H

#include <windows.h>
#include "Datatype.h"

namespace NgsaScintilla
{
    /* Load Scintilla.dll (registers the "Scintilla" window class) and Lexilla.dll (CreateLexer).
       Returns the Scintilla module handle, NULL when either library is missing. */
    HMODULE Load();

    /* Select a lexer by name. Historical names are mapped: "MySQL" and "LineMySQL" -> Lexilla "mysql",
       "SQLyogMsg" -> the in-process Messages-tab lexer; anything else is passed to Lexilla as given
       (e.g. "cpp", "xml", "hypertext", "css", "json", "markdown", "null"). */
    wyBool  SetLexer(HWND hwndedit, const wyChar* name);
}

/* Styles of the Messages-tab lexer (values as in the old patched SciLexer.h; TabMessage.cpp relies on them) */
#define SCE_SQLYOGMSG_DEFAULT     0
#define SCE_SQLYOGMSG_ERRORTAG    1
#define SCE_SQLYOGMSG_ERROR       2
#define SCE_SQLYOGMSG_RESULTTAG   3
#define SCE_SQLYOGMSG_RESULT      4
#define SCE_SQLYOGMSG_NORESULTTAG 5
#define SCE_SQLYOGMSG_NORESULT    6
#define SCE_SQLYOGMSG_WARNINGTAG  7
#define SCE_SQLYOGMSG_WARNING     8

#endif /* NGSA_SCINTILLA_H */
