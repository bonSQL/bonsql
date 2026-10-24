/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build (M1): MariaDB JSON column detection, see include/ngsa/NgsaJson.h.
 */
#include "ngsa/NgsaJson.h"
#include "MDIWindow.h"
#include "SQLMaker.h"           /* ExecuteAndGetResult */
#include "ngsa/NgsaTrace.h"

namespace
{
    struct Entry
    {
        wyString    key;        /* db\0table in one string: "db`table" */
        wyString    cols;       /* "|col1|col2|" */
        DWORD       tick;
        MDIWindow*  wnd;
    };

    const wyInt32   CACHE_SIZE = 16;
    const DWORD     CACHE_TTL  = 10000;     /* ms */
    Entry           s_cache[CACHE_SIZE];
    wyInt32         s_next = 0;

    void Escape(const wyChar* in, wyString& out)
    {
        out.SetAs(in ? in : "");
        out.FindAndReplace("\\", "\\\\");
        out.FindAndReplace("'", "''");
    }
}

wyBool
NgsaJson::IsCheckConstraintJsonColumn(MDIWindow* wnd, const wyChar* db, const wyChar* table, const wyChar* column)
{
    wyString    key, query, edb, etable, probe;
    MYSQL_RES*  res;
    MYSQL_ROW   row;
    DWORD       now = GetTickCount();
    wyInt32     i, slot = -1;

    if(!wnd || !db || !table || !column || !*table || !*column)
        return wyFalse;

    key.Sprintf("%s`%s", db, table);

    for(i = 0; i < CACHE_SIZE; i++)
    {
        if(s_cache[i].wnd == wnd && s_cache[i].key.Compare(key) == 0)
        {
            if(now - s_cache[i].tick < CACHE_TTL)
            {
                probe.Sprintf("|%s|", column);
                return s_cache[i].cols.Find(probe.GetString(), 0) != -1 ? wyTrue : wyFalse;
            }
            slot = i;
            break;
        }
    }

    if(slot < 0)
    {
        slot = s_next;
        s_next = (s_next + 1) % CACHE_SIZE;
    }

    Escape(db, edb);
    Escape(table, etable);
    query.Sprintf("SELECT CONSTRAINT_NAME FROM information_schema.CHECK_CONSTRAINTS "
                  "WHERE CONSTRAINT_SCHEMA = '%s' AND TABLE_NAME = '%s' AND LEVEL = 'Column' "
                  "AND CHECK_CLAUSE LIKE 'json_valid(%%'", edb.GetString(), etable.GetString());

    s_cache[slot].wnd = wnd;
    s_cache[slot].key.SetAs(key);
    s_cache[slot].cols.SetAs("|");
    s_cache[slot].tick = now;

    res = ExecuteAndGetResult(wnd, wnd->m_tunnel, &wnd->m_mysql, query, wyFalse);
    if(res)
    {
        while((row = wnd->m_tunnel->mysql_fetch_row(res)))
        {
            if(row[0])
                s_cache[slot].cols.AddSprintf("%s|", row[0]);
        }
        wnd->m_tunnel->mysql_free_result(res);
    }

    NgsaTrace("NgsaJson: %s.%s json columns %s", db, table, s_cache[slot].cols.GetString());

    probe.Sprintf("|%s|", column);
    return s_cache[slot].cols.Find(probe.GetString(), 0) != -1 ? wyTrue : wyFalse;
}

void
NgsaJson::Invalidate()
{
    wyInt32 i;

    for(i = 0; i < CACHE_SIZE; i++)
        s_cache[i].tick = 0;
}

namespace
{
    /* lower-case copy without spaces and backticks */
    void Squash(const wyChar* in, wyString& out)
    {
        out.Clear();
        for(const wyChar* p = in ? in : ""; *p; p++)
        {
            if(*p == ' ' || *p == '\t' || *p == '`' || *p == '\r' || *p == '\n')
                continue;
            wyChar c[2] = { (wyChar)tolower((unsigned char)*p), 0 };
            out.Add(c);
        }
    }
}

wyBool
NgsaJson::IsJsonValidCheck(const wyChar* check, const wyChar* column)
{
    wyString a, b, want;

    if(!check || !*check || !column || !*column)
        return wyFalse;
    Squash(check, a);
    Squash(column, b);
    /* the designer's parser hands over "(json_valid(`j`))," or "check(...)": peel those layers */
    while(a.GetLength() && a.GetCharAt(a.GetLength() - 1) == ',')
        a.Strip(1);
    if(a.GetLength() > 5 && strncmp(a.GetString(), "check", 5) == 0)
        a.Erase(0, 5);
    while(a.GetLength() > 2 && a.GetCharAt(0) == '(' && a.GetCharAt(a.GetLength() - 1) == ')')
    {
        wyString inner(a.Substr(1, a.GetLength() - 2));
        if(inner.Find("(", 0) == -1 || inner.Find("(", 0) > inner.Find(")", 0))
            break;      /* the outer pair belongs to json_valid(...) itself */
        a.SetAs(inner);
    }
    want.Sprintf("json_valid(%s)", b.GetString());
    return a.Compare(want) == 0 ? wyTrue : wyFalse;
}

void
NgsaJson::ColumnInfoQuery(wyString& query, const wyChar* db, const wyChar* table)
{
    wyString d(db), t(table);
    d.FindAndReplace("'", "''");
    t.FindAndReplace("'", "''");
    query.Sprintf(
        "select c.`COLUMN_NAME` as `Field`, "
        "if(cc.`CHECK_CLAUSE` = concat('json_valid(`', c.`COLUMN_NAME`, '`)'), 'json', c.`COLUMN_TYPE`) as `Type`, "
        "c.`COLLATION_NAME` as `Collation`, c.`IS_NULLABLE` as `Null`, c.`COLUMN_KEY` as `Key`, c.`COLUMN_DEFAULT` as `Default`, "
        "c.`EXTRA` as `Extra`, c.`PRIVILEGES` as `Privileges`, c.`COLUMN_COMMENT` as `Comment` "
        "from `information_schema`.`COLUMNS` c "
        "left join `information_schema`.`CHECK_CONSTRAINTS` cc on cc.`CONSTRAINT_SCHEMA` = c.`TABLE_SCHEMA` "
        "and cc.`TABLE_NAME` = c.`TABLE_NAME` and cc.`CONSTRAINT_NAME` = c.`COLUMN_NAME` and cc.`LEVEL` = 'Column' "
        "where c.`TABLE_SCHEMA` = '%s' and c.`TABLE_NAME` = '%s' order by c.`ORDINAL_POSITION`",
        d.GetString(), t.GetString());
}
