/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: per-connection server capabilities (docs/ngsa/FEATURE-BASELINE.md §7.3 rule 1).
 *
 * Computed once after connecting from the server's version string. New engine-dependent code
 * reads these flags instead of adding to the IsMySQLxxx()/IsMariaDBxxx() functions in
 * MySQLVersionHelper.cpp; those are migrated here as the code that uses them is touched.
 * Version thresholds carry the server release that introduced the feature, so a wrong guess
 * is a one-line fix with a citation.
 */
#ifndef NGSA_CAPS_H
#define NGSA_CAPS_H

#include "Datatype.h"

class Tunnel;

enum NgsaEngine
{
    NGSA_ENGINE_MYSQL   = 0,    /* Oracle MySQL (also Percona Server, Aurora MySQL until told apart) */
    NGSA_ENGINE_MARIADB = 1,
    NGSA_ENGINE_POSTGRES = 2,   /* set by the PostgreSQL branch */
};

struct NgsaCaps
{
    NgsaEngine  engine;
    wyUInt32    version;            /* mysql_get_server_version(): 100611 = 10.6.11, 80046 = 8.0.46 */
    wyChar      versionText[64];    /* mysql_get_server_info() */

    /* ---- SQL features ---- */
    wyBool  cte;                    /* WITH … AS: MySQL 8.0, MariaDB 10.2.1 */
    wyBool  windowFunctions;        /* MySQL 8.0.2, MariaDB 10.2.0 */
    wyBool  checkConstraints;       /* enforced CHECK: MySQL 8.0.16, MariaDB 10.2.1 */
    wyBool  generatedColumns;       /* MySQL 5.7.6, MariaDB 5.2 (VIRTUAL) / 10.2 (STORED keyword) */
    wyBool  invisibleColumns;       /* MariaDB 10.3.3, MySQL 8.0.23 */
    wyBool  compressedColumns;      /* COMPRESSED column attribute: MariaDB 10.3.0 only (text/blob/varchar/varbinary) */
    wyBool  sequences;              /* MariaDB 10.3.0 only */
    wyBool  systemVersioning;       /* MariaDB 10.3.4 only */
    wyBool  applicationPeriods;     /* MariaDB 10.4.3 only */
    wyBool  jsonTable;              /* JSON_TABLE: MySQL 8.0.4, MariaDB 10.6.0 */

    /* ---- datatypes ---- */
    wyBool  jsonType;               /* JSON accepted in DDL: MySQL 5.7.8, MariaDB 10.2.7 */
    wyBool  jsonIsCheckConstraint;  /* MariaDB: JSON is LONGTEXT + CHECK(json_valid()) */
    wyBool  inet6Type;              /* MariaDB 10.5.0 */
    wyBool  inet4Type;              /* MariaDB 10.10.0 */
    wyBool  uuidType;               /* MariaDB 10.7.0 */
    wyBool  vectorType;             /* MySQL 9.0.0, MariaDB 11.7.1 */

    /* ---- accounts and privileges ---- */
    wyBool  roles;                  /* MariaDB 10.0.5, MySQL 8.0.0 */
    wyBool  dynamicPrivileges;      /* MySQL 8.0: BACKUP_ADMIN… in SHOW GRANTS */
    wyBool  splitSuperPrivileges;   /* MariaDB 10.5.2: BINLOG ADMIN, CONNECTION ADMIN… */
    wyBool  authCachingSha2;        /* MySQL 8.0 default plugin */
    wyBool  authEd25519;            /* MariaDB 10.1.22 */
    wyBool  authParsec;             /* MariaDB 11.6.1 */
    wyBool  authUnixSocket;         /* MariaDB (server-side); shown as an option */

    /* ---- server operations ---- */
    wyBool  processKillCommand;     /* COM_PROCESS_KILL (mysql_kill): removed in MySQL 8.3; MariaDB keeps it */
    wyBool  flushQueryCache;        /* FLUSH QUERY CACHE: removed in MySQL 8.0; MariaDB keeps it */
    wyBool  flushDesKeyFile;        /* FLUSH DES_KEY_FILE: removed in MySQL 8.0; MariaDB keeps it (measured on 11.8) */
    wyBool  flushHosts;             /* FLUSH HOSTS: removed in MySQL 8.4; MariaDB keeps it */
    wyBool  flushOptimizerCosts;    /* FLUSH OPTIMIZER_COSTS: MySQL 5.7+ only */
    wyBool  flushBinaryLogs;        /* FLUSH BINARY LOGS: MySQL 5.1, MariaDB 5.1 */
    wyBool  flushSsl;               /* FLUSH SSL: MariaDB 10.4.0 only */
    wyBool  showBinlogStatus;       /* MariaDB 10.5.2 renamed SHOW MASTER STATUS */
    wyBool  fkOmittedRuleIsRestrict; /* SHOW CREATE TABLE omits the default FK rule: RESTRICT on MariaDB
                                       (prints NO ACTION), NO ACTION on MySQL 8+ (prints RESTRICT); measured 09/10/2026 */
};

namespace NgsaCapsCompute
{
    /* Fill `caps` from a connected handle. Cheap: no round trip, reads the handshake version. */
    void    FromConnection(MYSQL* mysql, NgsaCaps& caps);
    /* Same from a version number and server info text (tests, offline use). */
    void    FromVersion(wyUInt32 version, const wyChar* serverinfo, NgsaCaps& caps);
    const wyChar* EngineName(NgsaEngine e);
    /* Convenience for helpers that only have a MYSQL** (GUIHelper, dumps, dialogs): MariaDB 10.3+. */
    wyBool  HasSequences(MYSQL** mysql);
}

#endif /* NGSA_CAPS_H */
