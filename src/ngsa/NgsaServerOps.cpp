/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: Flush option table. See include/ngsa/NgsaServerOps.h.
 */
#include "ngsa/NgsaServerOps.h"
#include "resource.h"

static wyBool CapHosts(const NgsaCaps& c)          { return c.flushHosts; }
static wyBool CapOptimizerCosts(const NgsaCaps& c) { return c.flushOptimizerCosts; }
static wyBool CapQueryCache(const NgsaCaps& c)     { return c.flushQueryCache; }
static wyBool CapDesKeyFile(const NgsaCaps& c)     { return c.flushDesKeyFile; }
static wyBool CapSsl(const NgsaCaps& c)            { return c.flushSsl; }
static wyBool CapBinaryLogs(const NgsaCaps& c)     { return c.flushBinaryLogs; }

static const NgsaFlushOption s_flushoptions[] =
{
    /* ctrlid              persist key            sql                        unlock   available */
    { IDC_FL_LOGS,         "FlushLogs",           "logs",                    wyFalse, NULL },
    { IDC_FL_BINLOGS,      "FlushBinaryLogs",     "binary logs",             wyFalse, CapBinaryLogs },
    { IDC_FL_ENGINELOGS,   "FlushEngineLogs",     "engine logs",             wyFalse, NULL },
    { IDC_FL_ERRORLOGS,    "FlushErrorLogs",      "error logs",              wyFalse, NULL },
    { IDC_FL_GENERALLOGS,  "FlushGeneralLogs",    "general logs",            wyFalse, NULL },
    { IDC_FL_SLOWLOGS,     "FlushSlowLogs",       "slow logs",               wyFalse, NULL },
    { IDC_FL_RELAYLOGS,    "FlushRelayLogs",      "relay logs",              wyFalse, NULL },
    { IDC_FL_HOSTS,        "FlushHosts",          "hosts",                   wyFalse, CapHosts },
    { IDC_FL_PRIV,         "FlushPrivs",          "privileges",              wyFalse, NULL },
    { IDC_FL_STATUS,       "FlushStatus",         "status",                  wyFalse, NULL },
    { IDC_FL_TABLES,       "FlushTables",         "tables",                  wyFalse, NULL },
    { IDC_FL_RESOURCE,     "FlushResource",       "user_resources",          wyFalse, NULL },
    { IDC_FL_OPTCOSTS,     "FlushOptimizerCosts", "optimizer_costs",         wyFalse, CapOptimizerCosts },
    { IDC_FL_CACHE,        "FlushCache",          "query cache",             wyFalse, CapQueryCache },
    { IDC_FL_DES,          "FlushDesKey",         "des_key_file",            wyFalse, CapDesKeyFile },
    { IDC_FL_SSL,          "FlushSsl",            "ssl",                     wyFalse, CapSsl },
    { IDC_FL_TABLESREAD,   "FlushRead",           "tables with read lock",   wyTrue,  NULL },
};

const NgsaFlushOption*
NgsaFlushOptionTable(wyInt32* count)
{
    if(count)
        *count = (wyInt32)(sizeof(s_flushoptions) / sizeof(s_flushoptions[0]));
    return s_flushoptions;
}

wyBool
NgsaFlushOptionAvailable(const NgsaFlushOption& opt, const NgsaCaps& caps)
{
    return opt.available ? opt.available(caps) : wyTrue;
}

/* ---- Designer datatypes (M5) ----------------------------------------------------------- */

static wyBool TypeJson(const NgsaCaps& c)   { return c.jsonType; }
static wyBool TypeInet6(const NgsaCaps& c)  { return c.inet6Type; }
static wyBool TypeInet4(const NgsaCaps& c)  { return c.inet4Type; }
static wyBool TypeUuid(const NgsaCaps& c)   { return c.uuidType; }
static wyBool TypeVector(const NgsaCaps& c) { return c.vectorType; }

struct NgsaDesignerType { const wyChar* name; wyBool (*available)(const NgsaCaps&); };

/* Order is what the designer's dropdown shows. Measured 09/10/2026: MariaDB 11.8 accepts
   vector(n), uuid, inet6, inet4 and the json alias; MySQL 9.7 accepts vector(n) and json. */
static const NgsaDesignerType s_designertypes[] =
{
    { "tinyint", NULL }, { "smallint", NULL }, { "mediumint", NULL }, { "int", NULL }, { "bigint", NULL },
    { "real", NULL }, { "bit", NULL }, { "bool", NULL }, { "boolean", NULL }, { "float", NULL },
    { "double", NULL }, { "decimal", NULL }, { "date", NULL }, { "datetime", NULL }, { "timestamp", NULL },
    { "numeric", NULL }, { "time", NULL }, { "year", NULL }, { "char", NULL }, { "varchar", NULL },
    { "tinyblob", NULL }, { "tinytext", NULL }, { "text", NULL }, { "blob", NULL }, { "mediumblob", NULL },
    { "mediumtext", NULL }, { "longblob", NULL }, { "longtext", NULL }, { "enum", NULL }, { "set", NULL },
    { "binary", NULL }, { "varbinary", NULL },
    { "json", TypeJson }, { "inet6", TypeInet6 }, { "inet4", TypeInet4 }, { "uuid", TypeUuid },
    { "vector", TypeVector },
};

wyInt32
NgsaDesignerTypes(const NgsaCaps& caps, const wyChar** out, wyInt32 max)
{
    wyInt32 i, n = 0, count = (wyInt32)(sizeof(s_designertypes) / sizeof(s_designertypes[0]));

    for(i = 0; i < count && n < max; i++)
    {
        if(!s_designertypes[i].available || s_designertypes[i].available(caps))
            out[n++] = s_designertypes[i].name;
    }

    return n;
}

/* ---- BIT values (#2152) ------------------------------------------------------------------ */

void
NgsaBitToDecimal(const unsigned char* bytes, wyUInt32 nbytes, wyString& out)
{
    unsigned __int64 v = 0;
    wyUInt32         i;

    if(!bytes || !nbytes || nbytes > 8)
    {
        out.SetAs("0");
        return;
    }

    for(i = 0; i < nbytes; i++)
        v = (v << 8) | bytes[i];

    out.Sprintf("%I64u", v);
}

void
NgsaBitToLiteral(const unsigned char* bytes, wyUInt32 nbytes, wyString& out)
{
    wyUInt32    i;
    wyInt32     b;

    out.SetAs("b'");

    if(!bytes || !nbytes)
    {
        out.Add("0'");
        return;
    }

    for(i = 0; i < nbytes; i++)
        for(b = 7; b >= 0; b--)
            out.Add((bytes[i] >> b) & 1 ? "1" : "0");

    out.Add("'");
}

/* ---- Keywords.db (M6) ------------------------------------------------------------------- */

void
NgsaKeywordFilter(const NgsaCaps& caps, wyString& where)
{
    if(caps.engine == NGSA_ENGINE_MARIADB)
        where.Sprintf("(engine & 2) AND (mariadb_version IS NULL OR mariadb_version <= %u)", caps.version);
    else
        where.Sprintf("(engine & 1) AND (object_version IS NULL OR object_version <= %u)", caps.version);
}
