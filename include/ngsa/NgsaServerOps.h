/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: engine/version-dependent server operations as data tables
 * (docs/ngsa/FEATURE-BASELINE.md 7, docs/ngsa/MARIADB-PLAN.md M10).
 *
 * The Flush dialog (IDD_FLUSH) reads this table instead of a fixed checkbox list: an option is
 * offered only when NgsaCaps says the connected server has it. Measured on 09/10/2026 against
 * MariaDB 10.11 / 11.8 and MySQL 8.0.46 / 8.4.11 / 9.7.2.
 */
#ifndef NGSA_SERVER_OPS_H
#define NGSA_SERVER_OPS_H

#include "ngsa/NgsaCaps.h"
#include "wyString.h"

struct NgsaFlushOption
{
    wyInt32         ctrlid;         /* checkbox in IDD_FLUSH */
    const wyChar*   persistkey;     /* ini key under [FLUSH] */
    const wyChar*   sql;            /* text after FLUSH [NO_WRITE_TO_BINLOG] */
    wyBool          needsunlock;    /* TABLES WITH READ LOCK: UNLOCK TABLES afterwards */
    wyBool        (*available)(const NgsaCaps& caps);   /* NULL = every server */
};

/* The table, read-lock option last so "All" releases the lock at the end. */
const NgsaFlushOption*  NgsaFlushOptionTable(wyInt32* count);
wyBool                  NgsaFlushOptionAvailable(const NgsaFlushOption& opt, const NgsaCaps& caps);

/* ---- Designer datatypes (M5) ----------------------------------------------------------- */
#define NGSA_DESIGNER_TYPES_MAX 48

/* Fills `out` with the datatype names the Create/Alter Table designer offers for this server:
   the MySQL base set plus json / inet6 / inet4 / uuid / vector when NgsaCaps says the server has
   them. Returns the count. */
wyInt32 NgsaDesignerTypes(const NgsaCaps& caps, const wyChar** out, wyInt32 max);

/* ---- BIT values (#2152) ------------------------------------------------------------------ */
/* A BIT(n) value arrives as (n+7)/8 big-endian bytes. These render it as a number / a b'..' literal. */
void    NgsaBitToDecimal(const unsigned char* bytes, wyUInt32 nbytes, wyString& out);
void    NgsaBitToLiteral(const unsigned char* bytes, wyUInt32 nbytes, wyString& out);
#define NGSA_BIT_BYTES(field) ((wyUInt32)(((field).length + 7) / 8))

/* ---- Keywords.db (M6) ------------------------------------------------------------------- */
/* The objects table carries `engine` (bit 1 = MySQL, bit 2 = MariaDB), `object_version` (MySQL
   version, NULL = any) and `mariadb_version` (NULL = any supported MariaDB). This returns the SQL
   condition that selects what the connected server has, e.g.
     (engine & 2) AND (mariadb_version IS NULL OR mariadb_version <= 101116)                      */
void    NgsaKeywordFilter(const NgsaCaps& caps, wyString& where);

#endif /* NGSA_SERVER_OPS_H */
