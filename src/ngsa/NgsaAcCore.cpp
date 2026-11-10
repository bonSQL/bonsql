/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * Autocomplete core. See include/ngsa/NgsaAcCore.h and docs/ngsa/AUTOCOMPLETE-DESIGN.md.
 */
#include "ngsa/NgsaAcCore.h"
#include <algorithm>
#include <cstring>
#include <cctype>

namespace NgsaAc {

/* ------------------------------------------------------------------------------------------------ */
/* helpers                                                                                          */

std::string Lower(const std::string& s)
{
    std::string r(s);
    for(size_t i = 0; i < r.size(); i++) r[i] = (char)tolower((unsigned char)r[i]);
    return r;
}

bool EqualCI(const std::string& a, const std::string& b)
{
    if(a.size() != b.size()) return false;
    for(size_t i = 0; i < a.size(); i++)
        if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

bool IsIdentChar(unsigned char c)
{
    return isalnum(c) || c == '_' || c == '$' || c >= 0x80;
}

static bool IsSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

/* MySQL 8.4 and MariaDB 11 reserved words (union); a name that is one of these is written in backquotes */
static const char* kReserved[] = {
    "accessible", "add", "all", "alter", "analyze", "and", "as", "asc", "asensitive", "before", "between",
    "bigint", "binary", "blob", "both", "by", "call", "cascade", "case", "change", "char", "character", "check",
    "collate", "column", "condition", "constraint", "continue", "convert", "create", "cross", "cube",
    "cume_dist", "current_date", "current_role", "current_time", "current_timestamp", "current_user",
    "cursor", "database", "databases", "day_hour", "day_microsecond", "day_minute", "day_second", "dec",
    "decimal", "declare", "default", "delayed", "delete", "delete_domain_id", "dense_rank", "desc",
    "describe", "deterministic", "distinct", "distinctrow", "div", "do_domain_ids", "double", "drop", "dual",
    "each", "else", "elseif", "empty", "enclosed", "escaped", "except", "exists", "exit", "explain", "false",
    "fetch", "first_value", "float", "float4", "float8", "for", "force", "foreign", "from", "fulltext",
    "function", "general", "generated", "get", "grant", "group", "grouping", "groups", "having",
    "high_priority", "hour_microsecond", "hour_minute", "hour_second", "if", "ignore", "ignore_domain_ids",
    "ignore_server_ids", "in", "index", "infile", "inner", "inout", "insensitive", "insert", "int", "int1",
    "int2", "int3", "int4", "int8", "integer", "intersect", "interval", "into", "io_after_gtids",
    "io_before_gtids", "is", "iterate", "join", "json_table", "key", "keys", "kill", "lag", "last_value",
    "lateral", "lead", "leading", "leave", "left", "like", "limit", "linear", "lines", "load", "localtime",
    "localtimestamp", "lock", "long", "longblob", "longtext", "loop", "low_priority", "master_bind",
    "master_heartbeat_period", "master_ssl_verify_server_cert", "match", "maxvalue", "mediumblob",
    "mediumint", "mediumtext", "middleint", "minute_microsecond", "minute_second", "mod", "modifies",
    "natural", "not", "no_write_to_binlog", "nth_value", "ntile", "null", "numeric", "of", "offset", "on",
    "optimize", "optimizer_costs", "option", "optionally", "or", "order", "out", "outer", "outfile", "over",
    "page_checksum", "parse_vcol_expr", "partition", "percent_rank", "position", "precision", "primary",
    "procedure", "purge", "range", "rank", "read", "reads", "read_write", "real", "recursive", "ref_system_id",
    "references", "regexp", "release", "rename", "repeat", "replace", "require", "resignal", "restrict",
    "return", "returning", "revoke", "right", "rlike", "row", "rows", "row_number", "schema", "schemas",
    "second_microsecond", "select", "sensitive", "separator", "set", "show", "signal", "slow", "smallint",
    "spatial", "specific", "sql", "sqlexception", "sqlstate", "sqlwarning", "sql_big_result",
    "sql_calc_found_rows", "sql_small_result", "ssl", "starting", "stats_auto_recalc", "stats_persistent",
    "stats_sample_pages", "stored", "straight_join", "system", "table", "terminated", "then", "tinyblob",
    "tinyint", "tinytext", "to", "trailing", "trigger", "true", "undo", "union", "unique", "unlock", "unsigned",
    "update", "usage", "use", "using", "utc_date", "utc_time", "utc_timestamp", "values", "varbinary",
    "varchar", "varcharacter", "varying", "virtual", "when", "where", "while", "window", "with", "write",
    "xor", "year_month", "zerofill", 0 };

bool IsReserved(const std::string& word)
{
    static std::set<std::string> s;
    if(s.empty()) for(int i = 0; kReserved[i]; i++) s.insert(kReserved[i]);
    return s.count(Lower(word)) != 0;
}

bool NeedsQuote(const std::string& name)
{
    if(name.empty()) return true;
    bool alldigits = true;
    for(size_t i = 0; i < name.size(); i++)
    {
        unsigned char c = (unsigned char)name[i];
        if(!IsIdentChar(c)) return true;
        if(!isdigit(c)) alldigits = false;
    }
    if(alldigits) return true;
    return IsReserved(name);
}

std::string ApplyCase(const std::string& word, CaseMode mode, const std::string& typed)
{
    bool upper;
    if(mode == CASE_UPPER) upper = true;
    else if(mode == CASE_LOWER) upper = false;
    else
    {
        /* unchanged: follow what the user has typed so far */
        bool hasUpper = false, hasLower = false;
        for(size_t i = 0; i < typed.size(); i++)
        {
            if(isupper((unsigned char)typed[i])) hasUpper = true;
            else if(islower((unsigned char)typed[i])) hasLower = true;
        }
        upper = hasUpper && !hasLower;
    }
    std::string r(word);
    for(size_t i = 0; i < r.size(); i++) r[i] = (char)(upper ? toupper((unsigned char)r[i]) : tolower((unsigned char)r[i]));
    return r;
}

/* ------------------------------------------------------------------------------------------------ */
/* catalog                                                                                          */

Catalog::Catalog(Source& src) : maxAgeMs(5 * 60 * 1000), slowMs(500), fetches(0), m_src(src), m_dbsLoaded(false), m_dbsAt(0) {}

void Catalog::InvalidateAll()       { m_db.clear(); m_dbsLoaded = false; }
void Catalog::InvalidateDatabases() { m_dbsLoaded = false; }
void Catalog::Invalidate(const std::string& db)
{
    std::map<std::string, Database>::iterator it = m_db.find(Lower(db));
    if(it != m_db.end())
    {
        it->second.tablesLoaded = it->second.routinesLoaded = false;
        it->second.tables.clear(); it->second.routines.clear();
    }
}

bool Catalog::Allowed(Budget& b, Database* db)
{
    if(b.queries <= 0 || b.ms == 0) return false;
    if(db && db->slow) return false;
    if(m_src.Busy()) return false;
    return true;
}

void Catalog::Spent(Budget& b, Database* db, unsigned long t0)
{
    unsigned long dt = m_src.Now() - t0;
    fetches++;
    b.queries--;
    b.ms = (dt >= b.ms) ? 0 : b.ms - dt;
    if(dt > slowMs && db) db->slow = true;
}

const std::vector<std::string>* Catalog::Databases(Budget& b)
{
    bool stale = m_dbsLoaded && (m_src.Now() - m_dbsAt > maxAgeMs);
    if(!m_dbsLoaded || stale)
    {
        if(!Allowed(b, 0)) return m_dbsLoaded ? &m_dbs : 0;
        std::vector<std::string> out;
        unsigned long t0 = m_src.Now();
        bool ok = m_src.Databases(out);
        Spent(b, 0, t0);
        if(ok) { m_dbs.swap(out); m_dbsLoaded = true; m_dbsAt = m_src.Now(); }
        else if(!m_dbsLoaded) return 0;
    }
    return &m_dbs;
}

Database* Catalog::Known(const std::string& name)
{
    std::map<std::string, Database>::iterator it = m_db.find(Lower(name));
    return it == m_db.end() ? 0 : &it->second;
}

bool Catalog::IsDatabase(const std::string& name, Budget& b)
{
    if(Known(name)) return true;
    const std::vector<std::string>* dbs = Databases(b);
    if(!dbs) return false;
    for(size_t i = 0; i < dbs->size(); i++) if(EqualCI((*dbs)[i], name)) return true;
    return false;
}

Database* Catalog::Db(const std::string& name, Budget& b)
{
    if(name.empty()) return 0;
    std::string key = Lower(name);
    std::map<std::string, Database>::iterator it = m_db.find(key);
    if(it == m_db.end())
    {
        /* only databases the server lists (avoids a SHOW TABLES on every typed word) */
        if(!IsDatabase(name, b)) return 0;
        Database d; d.name = name;
        it = m_db.insert(std::make_pair(key, d)).first;
    }
    Database& d = it->second;
    bool stale = d.tablesLoaded && (m_src.Now() - d.tablesAt > maxAgeMs);
    if(!d.tablesLoaded || stale)
    {
        if(!Allowed(b, &d)) return d.tablesLoaded ? &d : 0;
        std::vector<Table> out;
        unsigned long t0 = m_src.Now();
        bool ok = m_src.Tables(d.name, out);
        Spent(b, &d, t0);
        if(ok) { d.tables.swap(out); d.tablesLoaded = true; d.tablesAt = m_src.Now(); }
        else if(!d.tablesLoaded) return 0;
    }
    return &d;
}

Table* Catalog::TableOf(Database& db, const std::string& table, Budget& b)
{
    Table* t = 0;
    for(size_t i = 0; i < db.tables.size() && !t; i++) if(EqualCI(db.tables[i].name, table)) t = &db.tables[i];
    if(!t) return 0;
    if(!t->colsLoaded)
    {
        if(!Allowed(b, &db)) return 0;
        std::vector<Column> out;
        unsigned long t0 = m_src.Now();
        bool ok = m_src.Columns(db.name, t->name, out);
        Spent(b, &db, t0);
        if(!ok) return 0;
        t->cols.swap(out); t->colsLoaded = true;
    }
    return t;
}

bool Catalog::Routines(Database& db, Budget& b)
{
    bool stale = db.routinesLoaded && (m_src.Now() - db.routinesAt > maxAgeMs);
    if(db.routinesLoaded && !stale) return true;
    if(!Allowed(b, &db)) return db.routinesLoaded;
    std::vector<Routine> out;
    unsigned long t0 = m_src.Now();
    bool ok = m_src.Routines(db.name, out);
    Spent(b, &db, t0);
    if(ok) { db.routines.swap(out); db.routinesLoaded = true; db.routinesAt = m_src.Now(); }
    return db.routinesLoaded;
}

/* ------------------------------------------------------------------------------------------------ */
/* tokenizer and statement bounds                                                                   */

/* Walks text[from, to) and calls out for each token. Strings: '...' and "..." with backslash and doubled
   quote escapes; identifiers: bare or `...`; comments: -- (with a space), #, and / * ... * /. */
void Tokenize(const std::string& text, size_t from, size_t to, std::vector<Token>& out)
{
    size_t i = from;
    if(to > text.size()) to = text.size();
    while(i < to)
    {
        unsigned char c = (unsigned char)text[i];
        if(IsSpace(c)) { i++; continue; }
        Token t; t.start = i; t.quoted = false;
        if(c == '\'' || c == '"' || c == '`')
        {
            char q = (char)c; size_t j = i + 1;
            while(j < to)
            {
                if(text[j] == '\\' && q != '`' && j + 1 < to) { j += 2; continue; }
                if(text[j] == q) { if(j + 1 < to && text[j + 1] == q) { j += 2; continue; } break; }
                j++;
            }
            t.end = (j < to) ? j + 1 : to;
            if(q == '`' || q == '"')
            {
                /* an identifier; "..." is a string in MySQL unless ANSI_QUOTES, but as a qualifier before a dot it
                   can only be a name, and the scanner only looks at names in table positions */
                t.type = (q == '`') ? Token::IDENT : Token::STRING;
                std::string raw = text.substr(i + 1, (j < to ? j : to) - i - 1);
                std::string un; un.reserve(raw.size());
                for(size_t k = 0; k < raw.size(); k++) { un += raw[k]; if(raw[k] == q && k + 1 < raw.size() && raw[k + 1] == q) k++; }
                t.text = un; t.quoted = true;
            }
            else { t.type = Token::STRING; t.text = text.substr(i, t.end - i); }
            out.push_back(t); i = t.end; continue;
        }
        if(c == '#' || (c == '-' && i + 1 < to && text[i + 1] == '-' && (i + 2 >= to || IsSpace((unsigned char)text[i + 2]))))
        {
            size_t j = i; while(j < to && text[j] != '\n') j++;
            t.type = Token::COMMENT; t.end = j; t.text = text.substr(i, j - i); out.push_back(t); i = j; continue;
        }
        if(c == '/' && i + 1 < to && text[i + 1] == '*')
        {
            size_t j = text.find("*/", i + 2); if(j == std::string::npos || j + 2 > to) j = to; else j += 2;
            t.type = Token::COMMENT; t.end = j; t.text = text.substr(i, j - i); out.push_back(t); i = j; continue;
        }
        if(IsIdentChar(c))
        {
            size_t j = i; while(j < to && IsIdentChar((unsigned char)text[j])) j++;
            t.end = j; t.text = text.substr(i, j - i);
            bool num = isdigit(c) != 0;
            for(size_t k = i; k < j && num; k++) if(!isdigit((unsigned char)text[k]) && text[k] != '.') num = false;
            t.type = num ? Token::NUMBER : Token::IDENT;
            out.push_back(t); i = j; continue;
        }
        t.end = i + 1; t.text = text.substr(i, 1);
        t.type = (c == ',' || c == '.' || c == '(' || c == ')' || c == ';') ? Token::PUNCT : Token::OTHER;
        out.push_back(t); i++;
    }
}

/* The statement containing the caret: between the ';' before and the ';' after, outside strings and
   comments. Very large texts are looked at only around the caret. */
void StatementAt(const std::string& text, size_t caret, size_t& start, size_t& end)
{
    const size_t kWindow = 256 * 1024;
    size_t from = 0, to = text.size();
    if(caret > from + kWindow) from = caret - kWindow;
    if(to > caret + kWindow) to = caret + kWindow;
    if(caret > text.size()) caret = text.size();
    start = from; end = to;
    size_t i = from;
    while(i < to)
    {
        char c = text[i];
        if(c == '\'' || c == '"' || c == '`')
        {
            size_t j = i + 1;
            while(j < to) { if(text[j] == '\\' && c != '`') { j += 2; continue; } if(text[j] == c) { if(j + 1 < to && text[j + 1] == c) { j += 2; continue; } break; } j++; }
            i = (j < to) ? j + 1 : to; continue;
        }
        if(c == '#' || (c == '-' && i + 1 < to && text[i + 1] == '-' && (i + 2 >= to || IsSpace((unsigned char)text[i + 2]))))
        { while(i < to && text[i] != '\n') i++; continue; }
        if(c == '/' && i + 1 < to && text[i + 1] == '*')
        { size_t j = text.find("*/", i + 2); i = (j == std::string::npos) ? to : j + 2; continue; }
        if(c == ';')
        {
            if(i < caret) start = i + 1;
            else { end = i; break; }
        }
        i++;
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* tables and aliases of the statement                                                              */

static bool IsWord(const Token& t, const char* w) { return t.type == Token::IDENT && !t.quoted && EqualCI(t.text, w); }

static const char* kAliasStops[] = {
    "where", "on", "join", "left", "right", "inner", "outer", "cross", "natural", "straight_join", "set", "group",
    "order", "limit", "having", "union", "using", "values", "value", "select", "partition", "force", "use", "ignore",
    "into", "from", "and", "or", "not", "when", "then", "else", "end", "case", "lock", "for", "window", "returning",
    "duplicate", "as", "with", "except", "intersect", "procedure", "update", "delete", "insert", "replace", "like",
    "where", "in", "is", "between", "exists", "qualify", "fetch", "offset", 0 };

static bool IsAliasStop(const Token& t)
{
    if(t.type != Token::IDENT || t.quoted) return false;
    for(int i = 0; kAliasStops[i]; i++) if(EqualCI(t.text, kAliasStops[i])) return true;
    return false;
}

/* reads name[.name] at tokens[i], returns the next index; false if no name there */
static bool ReadName(const std::vector<Token>& tk, size_t& i, std::string& db, std::string& name)
{
    if(i >= tk.size() || tk[i].type != Token::IDENT) return false;
    if(!tk[i].quoted && IsAliasStop(tk[i])) return false;
    name = tk[i].text; db.clear(); i++;
    if(i + 1 < tk.size() && tk[i].type == Token::PUNCT && tk[i].text == "." && tk[i + 1].type == Token::IDENT)
    { db = name; name = tk[i + 1].text; i += 2; }
    return true;
}

static void SkipParens(const std::vector<Token>& tk, size_t& i)
{
    int depth = 0;
    for(; i < tk.size(); i++)
    {
        if(tk[i].type != Token::PUNCT) continue;
        if(tk[i].text == "(") depth++;
        else if(tk[i].text == ")") { depth--; if(depth <= 0) { i++; return; } }
    }
}

void ScanTables(const std::string& text, size_t caret, std::vector<StmtTable>& out)
{
    size_t s, e; StatementAt(text, caret, s, e);
    std::vector<Token> tk; Tokenize(text, s, e, tk);
    for(size_t i = 0; i < tk.size(); i++)
    {
        const Token& t = tk[i];
        if(t.type != Token::IDENT || t.quoted) continue;
        bool list = false;   /* comma-separated list follows (FROM a, b) */
        size_t j = i + 1;
        if(IsWord(t, "from") || IsWord(t, "join") || IsWord(t, "update") || IsWord(t, "table")) list = IsWord(t, "from") || IsWord(t, "update");
        else if(IsWord(t, "insert") || IsWord(t, "replace"))
        {
            while(j < tk.size() && (IsWord(tk[j], "low_priority") || IsWord(tk[j], "delayed") || IsWord(tk[j], "high_priority") || IsWord(tk[j], "ignore") || IsWord(tk[j], "into"))) j++;
        }
        else continue;
        if(IsWord(t, "update")) while(j < tk.size() && (IsWord(tk[j], "low_priority") || IsWord(tk[j], "ignore"))) j++;
        for(;;)
        {
            if(j < tk.size() && tk[j].type == Token::PUNCT && tk[j].text == "(") { SkipParens(tk, j); /* derived table: alias follows, ignore it */ if(j < tk.size() && IsWord(tk[j], "as")) j++; if(j < tk.size() && tk[j].type == Token::IDENT && !IsAliasStop(tk[j])) j++; }
            else
            {
                StmtTable st;
                if(!ReadName(tk, j, st.db, st.name)) break;
                if(j < tk.size() && tk[j].type == Token::PUNCT && tk[j].text == "(") { out.push_back(st); break; }  /* INSERT INTO t (cols) */
                if(j < tk.size() && IsWord(tk[j], "as")) j++;
                if(j < tk.size() && tk[j].type == Token::IDENT && (tk[j].quoted || !IsAliasStop(tk[j]))) { st.alias = tk[j].text; j++; }
                out.push_back(st);
            }
            if(list && j < tk.size() && tk[j].type == Token::PUNCT && tk[j].text == ",") { j++; continue; }
            break;
        }
        i = j > i ? j - 1 : i;
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* where the caret is                                                                               */

/* reads an identifier ending at pos (exclusive), bare or backquoted; returns its start or npos */
static size_t IdentBefore(const std::string& text, size_t pos, std::string& name, bool& quoted)
{
    quoted = false;
    if(pos == 0) return std::string::npos;
    if(text[pos - 1] == '`')
    {
        size_t q = text.rfind('`', pos - 2);
        if(q == std::string::npos) return std::string::npos;
        name = text.substr(q + 1, pos - 2 - q); quoted = true;
        return q;
    }
    size_t s = pos;
    while(s > 0 && IsIdentChar((unsigned char)text[s - 1])) s--;
    if(s == pos) return std::string::npos;
    name = text.substr(s, pos - s);
    return s;
}

Context ContextAt(const std::string& text, size_t caret)
{
    Context ctx;
    if(caret > text.size()) caret = text.size();
    /* inside a string or comment? tokens of the statement up to the caret tell */
    size_t s, e; StatementAt(text, caret, s, e);
    std::vector<Token> tk; Tokenize(text, s, caret, tk);
    if(!tk.empty())
    {
        const Token& last = tk.back();
        if((last.type == Token::STRING || last.type == Token::COMMENT) && last.end >= caret)
        {
            /* a closed literal ending exactly at the caret is fine, an open one is not */
            bool closed = false;
            if(last.type == Token::STRING && last.end > last.start + 1 && text[last.end - 1] == text[last.start] && last.end == caret) closed = true;
            if(last.type == Token::COMMENT && last.text.size() >= 2 && last.text[1] == '*' && last.end >= last.start + 4 && text.compare(last.end - 2, 2, "*/") == 0) closed = true;
            if(!closed) { ctx.where = Context::LITERAL; return ctx; }
        }
        if(last.type == Token::IDENT && last.quoted && last.end > caret) { ctx.where = Context::LITERAL; return ctx; }
        if(last.type == Token::IDENT && last.quoted && last.end == caret)
        {
            /* a closed `name` ending right at the caret: nothing to complete; an open one (no closing
               backquote yet) is a word being typed inside backquotes */
            bool closed = last.end - last.start >= 2 && text[caret - 1] == '`';
            if(closed) { ctx.where = Context::NONE; return ctx; }
        }
    }
    /* the word */
    size_t ws = caret;
    while(ws > 0 && IsIdentChar((unsigned char)text[ws - 1])) ws--;
    ctx.word = text.substr(ws, caret - ws);
    ctx.wordStart = ws;
    size_t before = ws;
    if(before > 0 && text[before - 1] == '`') { ctx.inBackquote = true; before--; }
    ctx.where = Context::BARE;
    if(before > 0 && text[before - 1] == '.')
    {
        bool q; size_t p = IdentBefore(text, before - 1, ctx.q1, q);
        if(p == std::string::npos) { ctx.where = Context::NONE; return ctx; }
        ctx.where = Context::QUALIFIED1;
        if(p > 0 && text[p - 1] == '.')
        {
            size_t p2 = IdentBefore(text, p - 1, ctx.q2, q);
            if(p2 != std::string::npos) ctx.where = Context::QUALIFIED2;
        }
    }
    return ctx;
}

CallContext CallAt(const std::string& text, size_t caret)
{
    CallContext cc;
    if(caret > text.size()) caret = text.size();
    size_t s, e; StatementAt(text, caret, s, e);
    std::vector<Token> tk; Tokenize(text, s, caret, tk);
    int depth = 0, commas = 0;
    size_t open = tk.size();
    for(size_t i = tk.size(); i > 0; i--)
    {
        const Token& t = tk[i - 1];
        if(t.type != Token::PUNCT) continue;
        if(t.text == ")") depth++;
        else if(t.text == "(") { if(depth == 0) { open = i - 1; break; } depth--; }
        else if(t.text == "," && depth == 0) commas++;
    }
    if(open == tk.size() || open == 0) return cc;
    const Token& nm = tk[open - 1];
    if(nm.type != Token::IDENT) return cc;
    cc.name = nm.text; cc.nameStart = nm.start; cc.parenPos = tk[open].start; cc.argIndex = commas;
    if(open >= 3 && tk[open - 2].type == Token::PUNCT && tk[open - 2].text == "." && tk[open - 3].type == Token::IDENT)
    { cc.q1 = tk[open - 3].text; cc.nameStart = tk[open - 3].start; }
    cc.found = true;
    return cc;
}

/* ------------------------------------------------------------------------------------------------ */
/* the list                                                                                         */

static void Add(ListResult& r, std::set<std::string>& seen, const std::string& text, int kind, bool quote)
{
    if(text.empty() || text.find('?') != std::string::npos || text.find('\n') != std::string::npos) return;
    std::string key = Lower(text) + "\x01" + (char)('0' + kind);
    if(!seen.insert(key).second) return;
    Item it; it.text = text; it.kind = kind; it.quote = quote;
    r.items.push_back(it);
}

static void AddColumns(ListResult& r, std::set<std::string>& seen, Table* t)
{
    if(!t) return;
    for(size_t i = 0; i < t->cols.size(); i++) Add(r, seen, t->cols[i].name, KIND_COLUMN, NeedsQuote(t->cols[i].name));
}

static void AddTables(ListResult& r, std::set<std::string>& seen, Database* d)
{
    if(!d) return;
    for(size_t i = 0; i < d->tables.size(); i++) Add(r, seen, d->tables[i].name, d->tables[i].view ? KIND_VIEW : KIND_TABLE, NeedsQuote(d->tables[i].name));
}

static void AddRoutines(ListResult& r, std::set<std::string>& seen, Database* d, Catalog& cat, Budget& b)
{
    if(!d || !cat.Routines(*d, b)) return;
    for(size_t i = 0; i < d->routines.size(); i++)
        Add(r, seen, d->routines[i].name, d->routines[i].func ? KIND_SFUNCTION : KIND_PROCEDURE, NeedsQuote(d->routines[i].name));
}

/* the table a qualifier names: an alias of the statement, a table of the statement, a table of the current database */
static Table* ResolveTable(const std::string& q, const std::string& currentDb, const std::vector<StmtTable>& st, Catalog& cat, Budget& b)
{
    for(size_t i = 0; i < st.size(); i++)
    {
        if(!st[i].alias.empty() && EqualCI(st[i].alias, q))
        {
            Database* d = cat.Db(st[i].db.empty() ? currentDb : st[i].db, b);
            return d ? cat.TableOf(*d, st[i].name, b) : 0;
        }
    }
    for(size_t i = 0; i < st.size(); i++)
    {
        if(EqualCI(st[i].name, q))
        {
            Database* d = cat.Db(st[i].db.empty() ? currentDb : st[i].db, b);
            Table* t = d ? cat.TableOf(*d, st[i].name, b) : 0;
            if(t) return t;
        }
    }
    Database* d = cat.Known(currentDb);
    if(!d && !currentDb.empty()) d = cat.Db(currentDb, b);
    return d ? cat.TableOf(*d, q, b) : 0;
}

ListResult BuildList(const std::string& text, size_t caret, const std::string& currentDb, Catalog& cat,
                     const Keywords& kw, const Prefs& prefs, Budget budget)
{
    ListResult r;
    Context ctx = ContextAt(text, caret);
    if(ctx.where == Context::NONE || ctx.where == Context::LITERAL) return r;
    r.valid = true;
    r.lenEntered = (int)ctx.word.size();
    r.inBackquote = ctx.inBackquote;
    std::set<std::string> seen;
    std::vector<StmtTable> st; ScanTables(text, caret, st);

    if(ctx.where == Context::QUALIFIED2)
    {
        Database* d = cat.Db(ctx.q2, budget);
        if(d) AddColumns(r, seen, cat.TableOf(*d, ctx.q1, budget));
        return r;
    }
    if(ctx.where == Context::QUALIFIED1)
    {
        Table* t = ResolveTable(ctx.q1, currentDb, st, cat, budget);
        if(t) { AddColumns(r, seen, t); return r; }
        Database* d = cat.Db(ctx.q1, budget);
        if(d) { AddTables(r, seen, d); AddRoutines(r, seen, d, cat, budget); }
        return r;
    }
    /* bare: everything that can stand here */
    for(size_t i = 0; i < st.size(); i++) if(!st[i].alias.empty()) Add(r, seen, st[i].alias, KIND_ALIAS, false);
    for(size_t i = 0; i < st.size(); i++)
    {
        Database* d = cat.Db(st[i].db.empty() ? currentDb : st[i].db, budget);
        if(d) AddColumns(r, seen, cat.TableOf(*d, st[i].name, budget));
    }
    if(!currentDb.empty())
    {
        Database* d = cat.Db(currentDb, budget);
        AddTables(r, seen, d);
        AddRoutines(r, seen, d, cat, budget);
    }
    const std::vector<std::string>* dbs = cat.Databases(budget);
    if(dbs) for(size_t i = 0; i < dbs->size(); i++) Add(r, seen, (*dbs)[i], KIND_DATABASE, NeedsQuote((*dbs)[i]));
    for(size_t i = 0; i < kw.keywords.size(); i++) Add(r, seen, ApplyCase(kw.keywords[i], prefs.keywordCase, ctx.word), KIND_KEYWORD, false);
    for(std::map<std::string, std::vector<std::string> >::const_iterator f = kw.functions.begin(); f != kw.functions.end(); ++f)
        Add(r, seen, ApplyCase(f->first, prefs.functionCase, ctx.word), KIND_FUNCTION, false);
    return r;
}

/* Scintilla's own order for a presorted list (CompareNCaseInsensitive: upper-cased bytes), so that its
   binary search for the typed prefix lands: '_' sorts before letters in upper case, after them in lower */
static bool LessCI(const Item& a, const Item& b)
{
    size_t n = a.text.size() < b.text.size() ? a.text.size() : b.text.size();
    for(size_t i = 0; i < n; i++)
    {
        int x = toupper((unsigned char)a.text[i]), y = toupper((unsigned char)b.text[i]);
        if(x != y) return x < y;
    }
    if(a.text.size() != b.text.size()) return a.text.size() < b.text.size();
    /* the same text as a keyword and as an object (a table called order): the object first, so that Enter on
       the typed prefix picks it (Scintilla takes the first entry that matches) */
    return a.kind > b.kind;
}

std::string JoinList(const ListResult& r, char separator, char typeSeparator)
{
    std::vector<Item> items(r.items);
    std::sort(items.begin(), items.end(), LessCI);
    std::string out;
    for(size_t i = 0; i < items.size(); i++)
    {
        if(i) out += separator;
        out += items[i].text; out += typeSeparator; out += (char)('0' + items[i].kind);
    }
    return out;
}

/* ------------------------------------------------------------------------------------------------ */
/* calltips                                                                                         */

/* [start, end) of argument n in "a, b, c" (depth-0 commas); the last argument when n is beyond */
static void ArgSpan(const std::string& args, int n, int& start, int& end)
{
    int depth = 0, idx = 0; size_t s = 0;
    start = 0; end = (int)args.size();
    for(size_t i = 0; i <= args.size(); i++)
    {
        char c = i < args.size() ? args[i] : ',';
        if(c == '(') depth++;
        else if(c == ')') depth--;
        else if(c == ',' && depth == 0)
        {
            if(idx == n || i == args.size())
            {
                start = (int)s; end = (int)i;
                while(start < end && IsSpace((unsigned char)args[start])) start++;
                while(end > start && IsSpace((unsigned char)args[end - 1])) end--;
                return;
            }
            idx++; s = i + 1;
        }
    }
}

CallTip BuildCallTip(const std::string& text, size_t caret, const std::string& currentDb, Catalog& cat,
                     const Keywords& kw, Budget budget)
{
    CallTip tip;
    CallContext cc = CallAt(text, caret);
    if(!cc.found) return tip;
    std::vector<std::string> forms;
    std::string shown = cc.name;
    if(cc.q1.empty())
    {
        std::map<std::string, std::vector<std::string> >::const_iterator f = kw.functions.find(Lower(cc.name));
        if(f != kw.functions.end()) forms = f->second;
    }
    if(forms.empty())
    {
        Database* d = cat.Db(cc.q1.empty() ? currentDb : cc.q1, budget);
        if(d && cat.Routines(*d, budget))
        {
            for(size_t i = 0; i < d->routines.size(); i++)
            {
                if(!EqualCI(d->routines[i].name, cc.name)) continue;
                forms.push_back(d->routines[i].params);
                if(d->routines[i].func && !d->routines[i].returns.empty()) forms.back() += ") RETURNS " + d->routines[i].returns + " (";
                shown = d->routines[i].name;
                break;
            }
        }
    }
    if(forms.empty()) return tip;
    for(size_t i = 0; i < forms.size(); i++)
    {
        std::string args = forms[i];
        while(!args.empty() && IsSpace((unsigned char)args[args.size() - 1])) args.erase(args.size() - 1);
        while(!args.empty() && IsSpace((unsigned char)args[0])) args.erase(0, 1);
        std::string line = shown + "(" + args + ")";
        if(i == 0)
        {
            int s, e; ArgSpan(args, cc.argIndex, s, e);
            tip.hlStart = (int)shown.size() + 1 + s; tip.hlEnd = (int)shown.size() + 1 + e;
        }
        if(i) tip.text += '\n';
        tip.text += line;
    }
    tip.pos = cc.nameStart;
    tip.valid = true;
    return tip;
}

/* ------------------------------------------------------------------------------------------------ */
/* DDL seen on the connection                                                                       */

bool DdlEffect(const std::string& sql, bool& databases, std::vector<std::string>& dbs)
{
    databases = false; dbs.clear();
    std::vector<Token> tk; Tokenize(sql, 0, sql.size(), tk);
    size_t i = 0;
    while(i < tk.size() && tk[i].type == Token::COMMENT) i++;
    if(i >= tk.size() || tk[i].type != Token::IDENT) return false;
    const Token& v = tk[i];
    bool ddl = IsWord(v, "create") || IsWord(v, "alter") || IsWord(v, "drop") || IsWord(v, "rename");
    if(!ddl) return false;
    for(size_t j = i + 1; j < tk.size() && j < i + 6; j++)
        if(IsWord(tk[j], "database") || IsWord(tk[j], "schema")) { databases = true; break; }
    /* every db.name qualifier in the statement */
    for(size_t j = 0; j + 2 < tk.size(); j++)
        if(tk[j].type == Token::IDENT && tk[j + 1].type == Token::PUNCT && tk[j + 1].text == "." && tk[j + 2].type == Token::IDENT)
            dbs.push_back(tk[j].text);
    if(databases && dbs.empty())
    {
        /* CREATE DATABASE x / DROP SCHEMA IF EXISTS x: the name is the last identifier */
        for(size_t j = tk.size(); j > 0; j--) if(tk[j - 1].type == Token::IDENT) { dbs.push_back(tk[j - 1].text); break; }
    }
    return true;
}

}   /* namespace NgsaAc */
