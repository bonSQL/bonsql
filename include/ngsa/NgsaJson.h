/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build (M1): MariaDB has no JSON column type on the wire. A column declared JSON is a
 * LONGTEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_bin with CHECK (json_valid(`col`)), which the
 * server exposes in information_schema.CHECK_CONSTRAINTS (LEVEL = 'Column'). The grid asks here
 * whether a result column maps to such a column so it gets the JSON viewer and validation.
 */
#ifndef NGSA_JSON_H
#define NGSA_JSON_H

#include "Datatype.h"

class MDIWindow;
class wyString;

namespace NgsaJson
{
    /* true when `column` of `db`.`table` carries a json_valid() column check. One catalogue query
       per table, cached for a few seconds (a grid repaints the same cells many times). */
    wyBool  IsCheckConstraintJsonColumn(MDIWindow* wnd, const wyChar* db, const wyChar* table, const wyChar* column);

    /* drop the cache (after the designer altered a table) */
    void    Invalidate();

    /* M1 part two: true when a column CHECK clause is exactly json_valid(`column`) (the clause MariaDB
       writes for the JSON alias), ignoring case, spaces and backticks */
    wyBool  IsJsonValidCheck(const wyChar* check, const wyChar* column);

    /* M1 part two: SHOW FULL COLUMNS equivalent from the catalogue with Type = 'json' for such columns
       (same column names and values as SHOW FULL COLUMNS; measured on 11.8 and 10.11, 10/10/2026) */
    void    ColumnInfoQuery(wyString& query, const wyChar* db, const wyChar* table);
}

#endif /* NGSA_JSON_H */
