/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: per-connection server capabilities, see include/ngsa/NgsaCaps.h.
 */
#include <windows.h>
#include <string.h>
#include <mysql.h>
#include "ngsa/NgsaCaps.h"

namespace
{
    inline wyBool ge(wyUInt32 v, wyUInt32 threshold) { return v >= threshold ? wyTrue : wyFalse; }
}

void
NgsaCapsCompute::FromVersion(wyUInt32 version, const wyChar* serverinfo, NgsaCaps& c)
{
    memset(&c, 0, sizeof(c));
    c.version = version;
    strncpy(c.versionText, serverinfo ? serverinfo : "", sizeof(c.versionText) - 1);
    c.engine = (serverinfo && strstr(serverinfo, "MariaDB")) ? NGSA_ENGINE_MARIADB : NGSA_ENGINE_MYSQL;

    const wyBool maria = c.engine == NGSA_ENGINE_MARIADB ? wyTrue : wyFalse;
    const wyBool mysql = maria ? wyFalse : wyTrue;
    const wyUInt32 v = version;

    /* SQL features */
    c.cte                 = maria ? ge(v, 100201) : ge(v, 80000);
    c.windowFunctions     = maria ? ge(v, 100200) : ge(v, 80002);
    c.checkConstraints    = maria ? ge(v, 100201) : ge(v, 80016);
    c.generatedColumns    = maria ? ge(v, 50200)  : ge(v, 50706);
    c.invisibleColumns    = maria ? ge(v, 100303) : ge(v, 80023);
    c.compressedColumns   = maria ? ge(v, 100303) : wyFalse;   /* 10.3.0 really; tied to the Invisible column so the grid columns come as a pair */
    c.sequences           = maria ? ge(v, 100300) : wyFalse;
    c.systemVersioning    = maria ? ge(v, 100304) : wyFalse;
    c.applicationPeriods  = maria ? ge(v, 100403) : wyFalse;
    c.jsonTable           = maria ? ge(v, 100600) : ge(v, 80004);

    /* datatypes */
    c.jsonType            = maria ? ge(v, 100207) : ge(v, 50708);
    c.jsonIsCheckConstraint = maria;
    c.inet6Type           = maria ? ge(v, 100500) : wyFalse;
    c.inet4Type           = maria ? ge(v, 101000) : wyFalse;
    c.uuidType            = maria ? ge(v, 100700) : wyFalse;
    c.vectorType          = maria ? ge(v, 110701) : ge(v, 90000);

    /* accounts and privileges */
    c.roles               = maria ? ge(v, 100005) : ge(v, 80000);
    c.dynamicPrivileges   = mysql ? ge(v, 80000) : wyFalse;
    c.splitSuperPrivileges = maria ? ge(v, 100502) : wyFalse;
    c.authCachingSha2     = mysql ? ge(v, 80000) : wyFalse;
    c.authEd25519         = maria ? ge(v, 100122) : wyFalse;
    c.authParsec          = maria ? ge(v, 110601) : wyFalse;
    c.authUnixSocket      = maria;

    /* server operations */
    c.processKillCommand  = maria ? wyTrue : (ge(v, 80300) ? wyFalse : wyTrue);
    /* FLUSH options measured on 09/10/2026 against MariaDB 10.11/11.8 and MySQL 8.0.46/8.4.11/9.7.2
       (docs/ngsa/MARIADB-PLAN.md M10): MySQL 8.0 dropped QUERY CACHE, DES_KEY_FILE and SSL,
       MySQL 8.4 also dropped HOSTS; MariaDB keeps them all and has no OPTIMIZER_COSTS. */
    c.flushQueryCache     = maria ? wyTrue : (ge(v, 80000) ? wyFalse : wyTrue);
    c.flushDesKeyFile     = maria ? wyTrue : (ge(v, 80000) ? wyFalse : wyTrue);
    c.flushHosts          = maria ? wyTrue : (ge(v, 80400) ? wyFalse : wyTrue);
    c.flushOptimizerCosts = mysql ? ge(v, 50700) : wyFalse;
    c.flushBinaryLogs     = ge(v, 50100);
    c.flushSsl            = maria ? ge(v, 100400) : wyFalse;
    c.showBinlogStatus    = maria ? ge(v, 100502) : wyFalse;
    c.fkOmittedRuleIsRestrict = maria;
}

void
NgsaCapsCompute::FromConnection(MYSQL* mysql, NgsaCaps& caps)
{
    if(!mysql) { FromVersion(0, "", caps); return; }
    FromVersion((wyUInt32)mysql_get_server_version(mysql), mysql_get_server_info(mysql), caps);
}

const wyChar*
NgsaCapsCompute::EngineName(NgsaEngine e)
{
    switch(e)
    {
    case NGSA_ENGINE_MARIADB:  return "MariaDB";
    case NGSA_ENGINE_POSTGRES: return "PostgreSQL";
    default:                   return "MySQL";
    }
}

wyBool
NgsaCapsCompute::HasSequences(MYSQL** mysql)
{
    NgsaCaps c;
    if(!mysql || !*mysql)
        return wyFalse;
    FromConnection(*mysql, c);
    return c.sequences;
}
