/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA (M12): syntax colouring for the Blob/Text/JSON field viewer (BlobMgmt).
 *
 * The viewer's text box is a Scintilla control. The bundled SciLexer (2.27) has HTML/XML, CSS,
 * MySQL, Markdown and C++ lexers; JSON rides on the C++ lexer (keys and values both colour as
 * strings) until the Scintilla upgrade (plan M13). Colours come from the user's editor settings
 * (bonsql.ini [General] *Color keys) or, under the dark theme, from the theme's editor colours.
 * The choice is remembered per column in [NgsaFieldSyntax] db.table.column = n.
 */
#ifndef NGSA_FIELD_SYNTAX_H
#define NGSA_FIELD_SYNTAX_H

#include <windows.h>
#include "Datatype.h"
class wyString;   // NGSA (M14): Format() takes wyString&

enum NgsaSyntax
{
    NGSA_SYNTAX_NONE = 0,
    NGSA_SYNTAX_JSON,
    NGSA_SYNTAX_XML,
    NGSA_SYNTAX_HTML,
    NGSA_SYNTAX_CSS,
    NGSA_SYNTAX_SQL,
    NGSA_SYNTAX_MARKDOWN,
    NGSA_SYNTAX_COUNT
};

namespace NgsaFieldSyntax
{
    /* Display name of a syntax, for the combo. */
    const wyWChar*  Name(wyInt32 syntax);

    /* Guess from the column (JSON type) and the text: '{'/'[' -> JSON, '<html' or '<!doctype html'
       -> HTML, other '<' -> XML, else None. */
    wyInt32         Detect(const wyChar* text, wyInt32 length, wyBool isjsoncolumn);

    /* Remembered choice for a column key ("db.table.column"), -1 when none. */
    wyInt32         Load(const wyChar* colkey);
    void            Save(const wyChar* colkey, wyInt32 syntax);

    /* Set the lexer, keywords and style colours on the viewer's Scintilla and recolour it. */
    void            Apply(HWND hwndedit, wyInt32 syntax);

    /* M14: pretty-print JSON (jsoncpp) or XML (tinyxml2). wyFalse with `error` set when the text does
       not parse; other syntaxes are not formattable (wyFalse, empty error). */
    wyBool          CanFormat(wyInt32 syntax);
    wyBool          Format(wyInt32 syntax, const wyChar* text, wyInt32 length, wyString& out, wyString& error);
}

#endif /* NGSA_FIELD_SYNTAX_H */
