/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * Autocomplete glue. See include/ngsa/NgsaAutoComplete.h.
 */
#include "ngsa/NgsaAutoComplete.h"
#include "MDIWindow.h"
#include "ObjectBrowser.h"
#include "GUIHelper.h"
#include "CommonHelper.h"
#include "SQLMaker.h"
#include "Scintilla.h"
#include "ngsa/NgsaServerOps.h"
#include "ngsa/NgsaBrand.h"
#include "ngsa/NgsaTrace.h"
#include "ngsa/NgsaDark.h"
#include "resource.h"

using namespace NgsaAc;

extern PGLOBALS pGlobals;

/* ------------------------------------------------------------------------------------------------ */
/* look: icons and colours                                                                          */

/* a 16 px icon resource as straight-alpha RGBA bytes (what SCI_REGISTERRGBAIMAGE takes) */
static bool IconRgba(int resid, std::vector<unsigned char>& rgba)
{
    const int n = 16;
    HICON icon = (HICON)LoadImage(pGlobals->m_hinstance, MAKEINTRESOURCE(resid), IMAGE_ICON, n, n, LR_DEFAULTCOLOR);
    if(!icon) return false;
    ICONINFO ii = {0};
    if(!GetIconInfo(icon, &ii)) { DestroyIcon(icon); return false; }
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = n; bi.bmiHeader.biHeight = -n;   /* top-down */
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> bgra(n * n * 4), mask(n * n * 4);
    HDC dc = GetDC(NULL);
    bool ok = ii.hbmColor && GetDIBits(dc, ii.hbmColor, 0, n, &bgra[0], &bi, DIB_RGB_COLORS) == n;
    bool hasAlpha = false;
    if(ok) for(int i = 3; i < n * n * 4 && !hasAlpha; i += 4) hasAlpha = bgra[i] != 0;
    if(ok && !hasAlpha && ii.hbmMask)
    {
        /* a 24-bit icon: the AND mask says what is transparent */
        if(GetDIBits(dc, ii.hbmMask, 0, n, &mask[0], &bi, DIB_RGB_COLORS) == n)
            for(int i = 0; i < n * n; i++) bgra[i * 4 + 3] = mask[i * 4] ? 0 : 255;
    }
    ReleaseDC(NULL, dc);
    if(ii.hbmColor) DeleteObject(ii.hbmColor);
    if(ii.hbmMask) DeleteObject(ii.hbmMask);
    DestroyIcon(icon);
    if(!ok) return false;
    rgba.resize(n * n * 4);
    for(int i = 0; i < n * n; i++)
    {
        rgba[i * 4 + 0] = bgra[i * 4 + 2]; rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0]; rgba[i * 4 + 3] = bgra[i * 4 + 3];
    }
    return true;
}

void NgsaAutoComplete::RegisterImages(HWND sci)
{
    static const struct { int kind, resid; } kImages[] = {
        { KIND_KEYWORD, IDI_AC_KEYWORD }, { KIND_FUNCTION, IDI_AC_FUNCTION }, { KIND_DATABASE, IDI_DATABASE },
        { KIND_TABLE, IDI_TABLE }, { KIND_COLUMN, IDI_COLUMN }, { KIND_PROCEDURE, IDI_PROCEDURE },
        { KIND_SFUNCTION, IDI_FUNCTION }, { KIND_ALIAS, IDI_AC_ALIAS }, { KIND_VIEW, IDI_VIEW } };
    SendMessage(sci, SCI_RGBAIMAGESETWIDTH, 16, 0);
    SendMessage(sci, SCI_RGBAIMAGESETHEIGHT, 16, 0);
    SendMessage(sci, SCI_RGBAIMAGESETSCALE, 100, 0);
    for(size_t i = 0; i < sizeof(kImages) / sizeof(kImages[0]); i++)
    {
        std::vector<unsigned char> rgba;
        if(IconRgba(kImages[i].resid, rgba))
            SendMessage(sci, SCI_REGISTERRGBAIMAGE, kImages[i].kind, (LPARAM)&rgba[0]);
    }
}

static LPARAM Alpha(COLORREF c) { return (LPARAM)(0xFF000000u | (unsigned)c); }

void NgsaAutoComplete::ApplyColours(HWND sci)
{
    if(NgsaDark::IsOn())
    {
        /* the list in the editor's own colours, the Dark selection; the calltip on the Dark input shade */
        COLORREF back = (COLORREF)SendMessage(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, 0);
        COLORREF fore = (COLORREF)SendMessage(sci, SCI_STYLEGETFORE, STYLE_DEFAULT, 0);
        SendMessage(sci, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_BACK, Alpha(back));
        SendMessage(sci, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST, Alpha(fore));
        SendMessage(sci, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED_BACK, Alpha(NgsaDark::kSelection));
        SendMessage(sci, SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED, Alpha(RGB(0xFF, 0xFF, 0xFF)));
        SendMessage(sci, SCI_CALLTIPSETBACK, NgsaDark::kInput, 0);
        SendMessage(sci, SCI_CALLTIPSETFORE, NgsaDark::kText, 0);
        SendMessage(sci, SCI_CALLTIPSETFOREHLT, RGB(0x4F, 0xC1, 0xFF), 0);
    }
    else
    {
        SendMessage(sci, SCI_RESETELEMENTCOLOUR, SC_ELEMENT_LIST_BACK, 0);
        SendMessage(sci, SCI_RESETELEMENTCOLOUR, SC_ELEMENT_LIST, 0);
        SendMessage(sci, SCI_RESETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED_BACK, 0);
        SendMessage(sci, SCI_RESETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED, 0);
        SendMessage(sci, SCI_CALLTIPSETBACK, RGB(255, 255, 225), 0);
        SendMessage(sci, SCI_CALLTIPSETFORE, RGB(0, 0, 0), 0);
        SendMessage(sci, SCI_CALLTIPSETFOREHLT, RGB(255, 0, 0), 0);
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* preferences                                                                                      */

static bool     g_prefsLoaded = false;
static int      g_enabled = 1, g_asYouType = 1, g_help = 1;
static CaseMode g_keywordCase = CASE_UPPER, g_functionCase = CASE_UPPER;

static CaseMode CaseFrom(wyString& s)
{
    if(s.CompareI("lowercase") == 0) return CASE_LOWER;
    if(s.CompareI("Unchanged") == 0) return CASE_UNCHANGED;
    return CASE_UPPER;
}

void NgsaAutoComplete::ReloadPrefs()
{
    wyWChar  directory[MAX_PATH + 1] = {0}, *lpfileport = 0;
    wyString dirstr, kc, fc;
    g_prefsLoaded = true;
    if(SearchFilePath(BRAND_CFG_STEM_W, L".ini", MAX_PATH, directory, &lpfileport) == wyFalse) return;
    dirstr.SetAs(directory);
    g_enabled   = wyIni::IniGetInt(GENERALPREFA, "Autocomplete", 1, dirstr.GetString());
    g_asYouType = wyIni::IniGetInt(GENERALPREFA, "AutocompleteAsYouType", 1, dirstr.GetString());
    g_help      = wyIni::IniGetInt(GENERALPREFA, "AutocompleteHelp", 1, dirstr.GetString());
    wyIni::IniGetString(GENERALPREFA, "KeywordCase", "UPPERCASE", &kc, dirstr.GetString());
    wyIni::IniGetString(GENERALPREFA, "FunctionCase", "UPPERCASE", &fc, dirstr.GetString());
    g_keywordCase = CaseFrom(kc); g_functionCase = CaseFrom(fc);
    NgsaTrace("[ac] prefs: enabled=%d asyoutype=%d help=%d keywordcase=%d functioncase=%d", g_enabled, g_asYouType, g_help, (int)g_keywordCase, (int)g_functionCase);
}

bool NgsaAutoComplete::Enabled()      { if(!g_prefsLoaded) ReloadPrefs(); return g_enabled != 0; }
bool NgsaAutoComplete::AsYouType()    { if(!g_prefsLoaded) ReloadPrefs(); return g_asYouType != 0; }
bool NgsaAutoComplete::FunctionHelp() { if(!g_prefsLoaded) ReloadPrefs(); return g_help != 0; }

Prefs NgsaAutoComplete::CasePrefs()
{
    Prefs p; p.keywordCase = g_keywordCase; p.functionCase = g_functionCase; return p;
}

/* ------------------------------------------------------------------------------------------------ */
/* the server                                                                                       */

static std::string Backquoted(const std::string& name)
{
    std::string r = "`";
    for(size_t i = 0; i < name.size(); i++) { r += name[i]; if(name[i] == '`') r += '`'; }
    return r + "`";
}

static std::string Literal(const std::string& s)
{
    std::string r = "'";
    for(size_t i = 0; i < s.size(); i++)
    {
        if(s[i] == '\\' || s[i] == '\'') r += '\\';
        r += s[i];
    }
    return r + "'";
}

bool NgsaAutoComplete::MySqlSource::Rows(const std::string& sql, std::vector<std::vector<std::string> >& rows)
{
    if(!m_wnd || !m_wnd->m_tunnel || !m_wnd->m_mysql) return false;
    wyString query(sql.c_str());
    /* isprofile = false: not in the History tab */
    MYSQL_RES* res = ExecuteAndGetResult(m_wnd, m_wnd->m_tunnel, &m_wnd->m_mysql, query, wyFalse);
    if(!res) { NgsaTrace("[ac] failed: %s", sql.c_str()); return false; }
    unsigned int nf = m_wnd->m_tunnel->mysql_num_fields(res);
    MYSQL_ROW row;
    while((row = m_wnd->m_tunnel->mysql_fetch_row(res)) != NULL)
    {
        std::vector<std::string> r;
        for(unsigned int i = 0; i < nf; i++) r.push_back(row[i] ? row[i] : "");
        rows.push_back(r);
    }
    m_wnd->m_tunnel->mysql_free_result(res);
    NgsaTrace("[ac] %d rows: %s", (int)rows.size(), sql.c_str());
    return true;
}

bool NgsaAutoComplete::MySqlSource::Databases(std::vector<std::string>& out)
{
    std::vector<std::vector<std::string> > rows;
    if(!Rows("SHOW DATABASES", rows)) return false;
    for(size_t i = 0; i < rows.size(); i++) if(!rows[i].empty()) out.push_back(rows[i][0]);
    return true;
}

bool NgsaAutoComplete::MySqlSource::Tables(const std::string& db, std::vector<Table>& out)
{
    std::vector<std::vector<std::string> > rows;
    if(!Rows("SHOW FULL TABLES FROM " + Backquoted(db), rows)) return false;
    for(size_t i = 0; i < rows.size(); i++)
    {
        if(rows[i].empty()) continue;
        Table t; t.name = rows[i][0]; t.view = rows[i].size() > 1 && rows[i][1] == "VIEW";
        out.push_back(t);
    }
    return true;
}

bool NgsaAutoComplete::MySqlSource::Columns(const std::string& db, const std::string& table, std::vector<Column>& out)
{
    std::vector<std::vector<std::string> > rows;
    if(!Rows("SHOW COLUMNS FROM " + Backquoted(db) + "." + Backquoted(table), rows)) return false;
    for(size_t i = 0; i < rows.size(); i++)
    {
        if(rows[i].empty()) continue;
        Column c; c.name = rows[i][0]; if(rows[i].size() > 1) c.type = rows[i][1];
        out.push_back(c);
    }
    return true;
}

bool NgsaAutoComplete::MySqlSource::Routines(const std::string& db, std::vector<Routine>& out)
{
    std::vector<std::vector<std::string> > rows, params;
    if(!Rows("SELECT ROUTINE_NAME, ROUTINE_TYPE, DTD_IDENTIFIER FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = " + Literal(db) + " ORDER BY ROUTINE_NAME", rows)) return false;
    for(size_t i = 0; i < rows.size(); i++)
    {
        if(rows[i].size() < 2) continue;
        Routine r; r.name = rows[i][0]; r.func = rows[i][1] == "FUNCTION"; if(rows[i].size() > 2) r.returns = rows[i][2];
        out.push_back(r);
    }
    if(out.empty()) return true;
    if(!Rows("SELECT SPECIFIC_NAME, PARAMETER_MODE, PARAMETER_NAME, DTD_IDENTIFIER FROM information_schema.PARAMETERS WHERE SPECIFIC_SCHEMA = " + Literal(db) + " AND ORDINAL_POSITION > 0 ORDER BY SPECIFIC_NAME, ORDINAL_POSITION", params)) return true;
    for(size_t i = 0; i < params.size(); i++)
    {
        if(params[i].size() < 4) continue;
        for(size_t j = 0; j < out.size(); j++)
        {
            if(!EqualCI(out[j].name, params[i][0])) continue;
            std::string p;
            if(!params[i][1].empty()) p += params[i][1] + " ";
            p += params[i][2] + " " + params[i][3];
            if(!out[j].params.empty()) out[j].params += ", ";
            out[j].params += p;
            break;
        }
    }
    return true;
}

unsigned long NgsaAutoComplete::MySqlSource::Now() { return (unsigned long)GetTickCount(); }

bool NgsaAutoComplete::MySqlSource::Busy()
{
    return !m_wnd || m_wnd->m_executing == wyTrue || m_wnd->m_isthreadbusy == wyTrue;
}

/* ------------------------------------------------------------------------------------------------ */
/* the object                                                                                       */

NgsaAutoComplete::NgsaAutoComplete(MDIWindow* wnd)
    : m_wnd(wnd), m_src(wnd), m_cat(m_src), m_inBackquote(false), m_swallowReturn(false), m_tip(false), m_tipParen(0),
      m_pendingDbs(false), m_pendingCurrent(false)
{
    InitializeCriticalSection(&m_cs);
}

NgsaAutoComplete::~NgsaAutoComplete()
{
    DeleteCriticalSection(&m_cs);
}

void NgsaAutoComplete::OnConnected()
{
    m_cat.InvalidateAll();
    m_kwFor.clear();
}

void NgsaAutoComplete::InvalidateAll()
{
    m_cat.InvalidateAll();
}

/* called from whichever thread ran the statement (the query thread for the editor): record only */
void NgsaAutoComplete::OnStatement(const char* sql)
{
    if(!sql || !Enabled()) return;
    bool databases; std::vector<std::string> dbs;
    if(!DdlEffect(sql, databases, dbs)) return;
    EnterCriticalSection(&m_cs);
    if(databases) m_pendingDbs = true;
    if(dbs.empty()) m_pendingCurrent = true;
    for(size_t i = 0; i < dbs.size(); i++) m_pending.push_back(dbs[i]);
    LeaveCriticalSection(&m_cs);
    NgsaTrace("[ac] ddl: %.80s", sql);
}

void NgsaAutoComplete::ApplyPending()
{
    bool dbs, current; std::vector<std::string> named;
    EnterCriticalSection(&m_cs);
    dbs = m_pendingDbs; current = m_pendingCurrent; named.swap(m_pending);
    m_pendingDbs = m_pendingCurrent = false;
    LeaveCriticalSection(&m_cs);
    if(dbs) m_cat.InvalidateDatabases();
    if(current) m_cat.Invalidate(CurrentDb());
    for(size_t i = 0; i < named.size(); i++) m_cat.Invalidate(named[i]);
    if(dbs || current || !named.empty()) NgsaTrace("[ac] catalog invalidated: databases=%d current=%d named=%d", dbs ? 1 : 0, current ? 1 : 0, (int)named.size());
}

std::string NgsaAutoComplete::CurrentDb()
{
    if(m_wnd->m_pcqueryobject && m_wnd->m_pcqueryobject->m_seldatabase.GetLength())
        return m_wnd->m_pcqueryobject->m_seldatabase.GetString();
    return m_wnd->m_database.GetString();
}

/* keywords and functions of this engine and version from Keywords.db, the same selection the colouring uses */
static int KeywordRow(void* arg, int argc, char** argv, char**)
{
    Keywords* kw = (Keywords*)arg;
    if(argc < 1 || !argv[0]) return 0;
    std::string n(argv[0]);
    for(size_t i = 0; i < n.size(); i++) if(!IsIdentChar((unsigned char)n[i])) return 0;
    if(n.size() < 2) return 0;
    kw->keywords.push_back(Lower(n));
    return 0;
}

static int FunctionRow(void* arg, int argc, char** argv, char**)
{
    Keywords* kw = (Keywords*)arg;
    if(argc < 2 || !argv[0]) return 0;
    std::string n(argv[0]);
    for(size_t i = 0; i < n.size(); i++) if(!IsIdentChar((unsigned char)n[i])) return 0;
    std::vector<std::string>& forms = kw->functions[Lower(n)];
    std::string f = argv[1] ? argv[1] : "";
    for(size_t i = 0; i < forms.size(); i++) if(forms[i] == f) return 0;
    forms.push_back(f);
    return 0;
}

void NgsaAutoComplete::LoadKeywords()
{
    wyString where;
    NgsaKeywordFilter(m_wnd->m_ngsacaps, where);
    if(m_kwFor == where.GetString()) return;
    m_kw = Keywords();
    sqlite3* hdb = NULL;
    if(OpenKeyWordsDB(&hdb) == wyFalse) return;
    wyString q;
    q.Sprintf("select distinct object_name from objects where %s and object_type = %d", where.GetString(), AC_PRE_KEYWORD);
    YogSqliteExec(hdb, q.GetString(), KeywordRow, &m_kw);
    q.Sprintf("select object_name, object_arg_list from objects where %s and object_type = %d order by object_name, rowid", where.GetString(), AC_PRE_FUNCTION);
    YogSqliteExec(hdb, q.GetString(), FunctionRow, &m_kw);
    sqlite3_close(hdb);
    m_kwFor = where.GetString();
    NgsaTrace("[ac] keywords: %d keywords, %d functions for %s", (int)m_kw.keywords.size(), (int)m_kw.functions.size(), where.GetString());
}

/* the editor text around the caret (all of it up to 1 MB, otherwise a window), with the caret's byte offset
   inside the returned text and the text's offset in the document */
std::string NgsaAutoComplete::Text(HWND sci, size_t& caret, size_t& offset)
{
    Sci_Position len = (Sci_Position)SendMessage(sci, SCI_GETLENGTH, 0, 0);
    Sci_Position cur = (Sci_Position)SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    const Sci_Position kMax = 1024 * 1024, kWindow = 256 * 1024;
    Sci_Position from = 0, to = len;
    if(len > kMax) { from = cur > kWindow ? cur - kWindow : 0; to = cur + kWindow < len ? cur + kWindow : len; }
    std::string text;
    text.resize((size_t)(to - from));
    if(to > from)
    {
        Sci_TextRange tr; tr.chrg.cpMin = (Sci_PositionCR)from; tr.chrg.cpMax = (Sci_PositionCR)to;
        std::vector<char> buf((size_t)(to - from) + 1);
        tr.lpstrText = &buf[0];
        SendMessage(sci, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);
        text.assign(&buf[0], (size_t)(to - from));
    }
    caret = (size_t)(cur - from); offset = (size_t)from;
    return text;
}

void NgsaAutoComplete::Show(HWND sci, bool forced)
{
    if(!Enabled()) return;
    LoadKeywords();
    ApplyPending();
    size_t caret, offset;
    std::string text = Text(sci, caret, offset);
    Budget budget;   /* up to 4 statements, 250 ms */
    ListResult r = BuildList(text, caret, CurrentDb(), m_cat, m_kw, CasePrefs(), budget);
    if(!r.valid || r.items.empty()) { if(SendMessage(sci, SCI_AUTOCACTIVE, 0, 0)) SendMessage(sci, SCI_AUTOCCANCEL, 0, 0); return; }
    if(!forced && r.lenEntered > 0 && isdigit((unsigned char)text[caret - r.lenEntered])) return;   /* a number */
    m_items = r.items; m_inBackquote = r.inBackquote;
    std::string list = JoinList(r);
    ApplyColours(sci);
    SendMessage(sci, SCI_AUTOCSETOPTIONS, SC_AUTOCOMPLETE_FIXED_SIZE, 0);   /* a plain border, not the sizing frame (light in the Dark theme) */
    SendMessage(sci, SCI_AUTOCSETORDER, SC_ORDER_PRESORTED, 0);   /* JoinList sorts: same text, objects before keywords */
    SendMessage(sci, SCI_AUTOCSETAUTOHIDE, TRUE, 0);
    SendMessage(sci, SCI_AUTOCSETIGNORECASE, TRUE, 0);
    SendMessage(sci, SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR, SC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE, 0);
    SendMessage(sci, SCI_AUTOCSETTYPESEPARATOR, '?', 0);
    SendMessage(sci, SCI_AUTOCSETSEPARATOR, '\n', 0);
    SendMessage(sci, SCI_AUTOCSETMAXHEIGHT, 12, 0);
    SendMessage(sci, SCI_AUTOCSHOW, (WPARAM)r.lenEntered, (LPARAM)list.c_str());
    NgsaTrace("[ac] list: %d items, %d typed, forced=%d, fetches so far %d", (int)r.items.size(), r.lenEntered, forced ? 1 : 0, m_cat.fetches);
}

void NgsaAutoComplete::ShowCallTip(HWND sci, bool forced)
{
    if(!Enabled() || (!forced && !FunctionHelp())) return;
    LoadKeywords();
    ApplyPending();
    size_t caret, offset;
    std::string text = Text(sci, caret, offset);
    CallTip tip = BuildCallTip(text, caret, CurrentDb(), m_cat, m_kw, Budget(2, 250));
    if(!tip.valid) { if(m_tip) { SendMessage(sci, SCI_CALLTIPCANCEL, 0, 0); m_tip = false; } return; }
    CallContext cc = CallAt(text, caret);
    m_tipParen = offset + cc.parenPos;
    ApplyColours(sci);
    SendMessage(sci, SCI_CALLTIPSHOW, (WPARAM)(offset + tip.pos), (LPARAM)tip.text.c_str());
    SendMessage(sci, SCI_CALLTIPSETHLT, (WPARAM)tip.hlStart, (LPARAM)tip.hlEnd);
    m_tip = true;
}

void NgsaAutoComplete::UpdateCallTip(HWND sci)
{
    if(!m_tip) return;
    if(!SendMessage(sci, SCI_CALLTIPACTIVE, 0, 0)) { m_tip = false; return; }
    size_t caret, offset;
    std::string text = Text(sci, caret, offset);
    CallContext cc = CallAt(text, caret);
    if(!cc.found || offset + cc.parenPos != m_tipParen) { SendMessage(sci, SCI_CALLTIPCANCEL, 0, 0); m_tip = false; return; }
    CallTip tip = BuildCallTip(text, caret, CurrentDb(), m_cat, m_kw, Budget(0, 0));
    if(tip.valid) SendMessage(sci, SCI_CALLTIPSETHLT, (WPARAM)tip.hlStart, (LPARAM)tip.hlEnd);
}

/* the chosen item: backquotes around a name that needs them (A6) */
void NgsaAutoComplete::Accept(HWND sci, SCNotification* scn)
{
    m_swallowReturn = true;
    if(!scn->text || m_inBackquote) return;
    const Item* it = 0;
    for(size_t i = 0; i < m_items.size() && !it; i++) if(m_items[i].text == scn->text) it = &m_items[i];
    if(!it || !it->quote) return;
    SendMessage(sci, SCI_AUTOCCANCEL, 0, 0);
    std::string q = Backquoted(it->text);
    Sci_Position cur = (Sci_Position)SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    SendMessage(sci, SCI_SETTARGETRANGE, (WPARAM)scn->position, (LPARAM)cur);
    SendMessage(sci, SCI_REPLACETARGET, (WPARAM)q.size(), (LPARAM)q.c_str());
    SendMessage(sci, SCI_GOTOPOS, (WPARAM)(scn->position + (Sci_Position)q.size()), 0);
    NgsaTrace("[ac] accepted %s as %s", scn->text, q.c_str());
}

/* ------------------------------------------------------------------------------------------------ */
/* editor events                                                                                    */

bool NgsaAutoComplete::OnChar(HWND sci, WPARAM ch)
{
    if(!Enabled()) return false;
    if(ch == ' ' && (GetKeyState(VK_CONTROL) & 0x8000)) return true;        /* the WM_CHAR of Ctrl+Space */
    if(ch == VK_RETURN && m_swallowReturn) { m_swallowReturn = false; return true; }
    m_swallowReturn = false;
    return false;
}

bool NgsaAutoComplete::OnKeyDown(HWND sci, WPARAM vk)
{
    if(!Enabled()) return false;
    if(vk == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        if(GetKeyState(VK_SHIFT) & 0x8000) ShowCallTip(sci, true);
        else Show(sci, true);
        return true;
    }
    if(vk == VK_ESCAPE && m_tip) { SendMessage(sci, SCI_CALLTIPCANCEL, 0, 0); m_tip = false; }
    return false;
}

bool NgsaAutoComplete::OnNotify(SCNotification* scn)
{
    if(!scn || !Enabled()) return false;
    HWND sci = (HWND)scn->nmhdr.hwndFrom;
    switch(scn->nmhdr.code)
    {
    case SCN_AUTOCSELECTION:
        Accept(sci, scn);
        return true;
    case SCN_AUTOCCANCELLED:
        m_items.clear();
        return false;
    case SCN_CHARADDED:
        {
            int ch = scn->ch;
            if(ch == '(') { if(FunctionHelp()) ShowCallTip(sci, false); }
            else if(ch == ')' || ch == ',') UpdateCallTip(sci);
            if(!AsYouType()) return false;
            /* Scintilla sends SCN_CHARADDED before it narrows or closes its open list for the new character: a
               '.' after a word with the word's list still up means a fresh member list; any other character is
               Scintilla's to narrow */
            if(ch == '.') { if(SendMessage(sci, SCI_AUTOCACTIVE, 0, 0)) SendMessage(sci, SCI_AUTOCCANCEL, 0, 0); Show(sci, false); }
            else if(SendMessage(sci, SCI_AUTOCACTIVE, 0, 0)) return false;
            else if(ch < 0x80 && IsIdentChar((unsigned char)ch))
            {
                /* the first character of a word only: a list closed with Esc stays closed for that word */
                Sci_Position cur = (Sci_Position)SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
                int before = cur >= 2 ? (int)SendMessage(sci, SCI_GETCHARAT, (WPARAM)(cur - 2), 0) : ' ';
                if(before < 0x80 && !IsIdentChar((unsigned char)before) && before != '`') Show(sci, false);
            }
            return false;
        }
    case SCN_UPDATEUI:
        if(m_tip) UpdateCallTip(sci);
        return false;
    }
    return false;
}
