/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * Autocomplete core: the catalog cache, the statement scanner, the list
 * and calltip builders. No Win32, no Scintilla, no MySQL in here: the server is behind the Source
 * interface and the editor glue lives in NgsaAutoComplete.cpp, so this file is also compiled into a
 * console check.
 *
 * Decisions (A1-A7): in-memory catalog per connection window, filled lazily with single bounded statements
 * on the window's own connection, invalidated by own DDL, Refresh and age; never a file on disk.
 */
#ifndef NGSA_AC_CORE_H
#define NGSA_AC_CORE_H

#include <string>
#include <vector>
#include <map>
#include <set>

namespace NgsaAc {

/* list icon numbers (NgsaAutoComplete::RegisterImages; 1-8 are the numbers the Ultimate list used, 9 is ours) */
enum Kind { KIND_KEYWORD = 1, KIND_FUNCTION = 2, KIND_DATABASE = 3, KIND_TABLE = 4, KIND_COLUMN = 5,
            KIND_PROCEDURE = 6, KIND_SFUNCTION = 7, KIND_ALIAS = 8, KIND_VIEW = 9 };

struct Column  { std::string name, type; };
struct Table   { std::string name; bool view; bool colsLoaded; std::vector<Column> cols;
                 Table() : view(false), colsLoaded(false) {} };
struct Routine { std::string name; bool func; std::string params; std::string returns;
                 Routine() : func(false) {} };
struct Database
{
    std::string          name;
    bool                 tablesLoaded, routinesLoaded, slow;
    unsigned long        tablesAt, routinesAt;
    std::vector<Table>   tables;
    std::vector<Routine> routines;
    Database() : tablesLoaded(false), routinesLoaded(false), slow(false), tablesAt(0), routinesAt(0) {}
};

/* What the catalog asks of the server. One statement per call (Routines: two). The glue implements it
   with the window's connection; the console check with a fake. */
class Source
{
public:
    virtual ~Source() {}
    virtual bool          Databases(std::vector<std::string>& out) = 0;
    virtual bool          Tables(const std::string& db, std::vector<Table>& out) = 0;       /* names + view flag */
    virtual bool          Columns(const std::string& db, const std::string& table, std::vector<Column>& out) = 0;
    virtual bool          Routines(const std::string& db, std::vector<Routine>& out) = 0;
    virtual unsigned long Now() = 0;                 /* milliseconds, monotonic */
    virtual bool          Busy() { return false; }   /* the connection is executing something else: no fetch now */
};

/* What one list build may spend on the server */
struct Budget
{
    int           queries;
    unsigned long ms;
    Budget(int q = 4, unsigned long m = 250) : queries(q), ms(m) {}
};

class Catalog
{
public:
    explicit Catalog(Source& src);

    void InvalidateAll();
    void InvalidateDatabases();                       /* the database list only */
    void Invalidate(const std::string& db);           /* one database: tables, columns, routines */

    const std::vector<std::string>* Databases(Budget& b);
    Database* Db(const std::string& name, Budget& b);                         /* with its table list, or NULL */
    Table*    TableOf(Database& db, const std::string& table, Budget& b);     /* with its columns, or NULL */
    bool      Routines(Database& db, Budget& b);
    Database* Known(const std::string& name);                                 /* no fetch */
    bool      IsDatabase(const std::string& name, Budget& b);

    unsigned long maxAgeMs;    /* a loaded list older than this is fetched again when next needed */
    unsigned long slowMs;      /* a statement slower than this marks its database slow for the session */
    int           fetches;     /* statistics (the console check reads it) */

private:
    bool Allowed(Budget& b, Database* db);
    void Spent(Budget& b, Database* db, unsigned long t0);

    Source&                         m_src;
    std::vector<std::string>        m_dbs;
    bool                            m_dbsLoaded;
    unsigned long                   m_dbsAt;
    std::map<std::string, Database> m_db;   /* key: lower-cased name */
};

/* --- the statement around the caret ------------------------------------------------------------- */

struct Token
{
    enum Type { IDENT, NUMBER, STRING, PUNCT, COMMENT, OTHER };
    Type        type;
    size_t      start, end;    /* byte offsets in the text */
    std::string text;          /* identifiers: unquoted; strings/comments: as written */
    bool        quoted;        /* identifier written in backquotes or double quotes */
};

struct StmtTable { std::string db, name, alias; };

void   Tokenize(const std::string& text, size_t from, size_t to, std::vector<Token>& out);
void   StatementAt(const std::string& text, size_t caret, size_t& start, size_t& end);
void   ScanTables(const std::string& text, size_t caret, std::vector<StmtTable>& out);

struct Context
{
    enum Where { NONE, LITERAL, BARE, QUALIFIED1, QUALIFIED2 };
    Where       where;
    std::string word;          /* the identifier being typed (may be empty) */
    std::string q1, q2;        /* the qualifiers: q1 right before the word, q2 before q1 */
    size_t      wordStart;     /* byte offset where the word starts */
    bool        inBackquote;   /* the word is being written inside backquotes */
    Context() : where(NONE), wordStart(0), inBackquote(false) {}
};
Context ContextAt(const std::string& text, size_t caret);

struct CallContext
{
    bool        found;
    std::string name, q1;      /* function or routine name, optional database qualifier */
    size_t      nameStart, parenPos;
    int         argIndex;      /* 0-based argument the caret is in */
    CallContext() : found(false), nameStart(0), parenPos(0), argIndex(0) {}
};
CallContext CallAt(const std::string& text, size_t caret);

/* --- lists and calltips ------------------------------------------------------------------------- */

struct Keywords
{
    std::vector<std::string>                        keywords;    /* lower-case, from Keywords.db for this engine */
    std::map<std::string, std::vector<std::string> > functions;   /* lower-case name -> argument forms */
};

enum CaseMode { CASE_UPPER, CASE_LOWER, CASE_UNCHANGED };
struct Prefs
{
    CaseMode keywordCase, functionCase;
    Prefs() : keywordCase(CASE_UPPER), functionCase(CASE_UPPER) {}
};

struct Item { std::string text; int kind; bool quote; };

struct ListResult
{
    bool              valid;        /* false: nothing to show here (literal, comment, no context) */
    std::vector<Item> items;
    int               lenEntered;   /* bytes of the word already typed */
    bool              inBackquote;
    ListResult() : valid(false), lenEntered(0), inBackquote(false) {}
};

ListResult  BuildList(const std::string& text, size_t caret, const std::string& currentDb, Catalog& cat,
                      const Keywords& kw, const Prefs& prefs, Budget budget);
std::string JoinList(const ListResult& r, char separator = '\n', char typeSeparator = '?');
bool        NeedsQuote(const std::string& name);
bool        IsReserved(const std::string& word);
std::string ApplyCase(const std::string& word, CaseMode mode, const std::string& typed);

struct CallTip
{
    bool        valid;
    std::string text;            /* one form per line */
    int         hlStart, hlEnd;  /* highlighted argument in the first line */
    size_t      pos;             /* where to show it: the name's start */
    CallTip() : valid(false), hlStart(0), hlEnd(0), pos(0) {}
};
CallTip BuildCallTip(const std::string& text, size_t caret, const std::string& currentDb, Catalog& cat,
                     const Keywords& kw, Budget budget);

/* --- DDL seen on the connection ----------------------------------------------------------------- */

/* Looks at a statement the user ran: returns true when it changes the catalog, with what to invalidate.
   databases = true for CREATE/DROP DATABASE; otherwise db names mentioned as qualifiers (may be empty:
   the current database is meant). */
bool DdlEffect(const std::string& sql, bool& databases, std::vector<std::string>& dbs);

/* helpers shared with the glue */
std::string Lower(const std::string& s);
bool        EqualCI(const std::string& a, const std::string& b);
bool        IsIdentChar(unsigned char c);

}   /* namespace NgsaAc */

#endif
