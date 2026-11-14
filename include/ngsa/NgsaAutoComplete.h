/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * Autocomplete glue: one object per connection window (MDIWindow::m_acinterface owns it). Ties the core
 * (include/ngsa/NgsaAcCore.h) to the Scintilla editor, the window's connection and the preferences.
 */
#ifndef NGSA_AUTOCOMPLETE_H
#define NGSA_AUTOCOMPLETE_H

#include <windows.h>
#include <string>
#include <vector>
#include "ngsa/NgsaAcCore.h"

class MDIWindow;
struct SCNotification;

class NgsaAutoComplete
{
public:
    /* preferences (ini [GENERALPREF]: Autocomplete, AutocompleteAsYouType, AutocompleteHelp, KeywordCase, FunctionCase) */
    static void ReloadPrefs();
    static bool Enabled();
    static bool AsYouType();
    static bool FunctionHelp();

    /* The list icons (RGBA from the icon set: database, table, view, column, procedure, function, and the three
       glyph icons for keywords, built-in functions and aliases), registered on an editor; EditorBase calls it
       where the Ultimate-era XPMs were registered. */
    static void RegisterImages(HWND sci);
    /* List and calltip colours for the editor's theme: the editor's own default background and text, the
       Dark palette's selection; Scintilla's defaults when the Dark theme is off. */
    static void ApplyColours(HWND sci);

    explicit NgsaAutoComplete(MDIWindow* wnd);
    ~NgsaAutoComplete();

    void OnConnected();                          /* after a (re)connect: fresh catalog, keywords for the engine */
    void InvalidateAll();                        /* the object browser was refreshed */
    void OnStatement(const char* sql);           /* a statement ran on this connection: DDL invalidates */

    bool OnChar(HWND sci, WPARAM ch);            /* WM_CHAR before Scintilla; true = consumed */
    bool OnKeyDown(HWND sci, WPARAM vk);         /* WM_KEYDOWN before Scintilla; true = consumed */
    bool OnNotify(SCNotification* scn);          /* SCN_* from the active editor; true = handled, stop */

private:
    class MySqlSource : public NgsaAc::Source
    {
    public:
        explicit MySqlSource(MDIWindow* wnd) : m_wnd(wnd) {}
        bool Databases(std::vector<std::string>& out);
        bool Tables(const std::string& db, std::vector<NgsaAc::Table>& out);
        bool Columns(const std::string& db, const std::string& table, std::vector<NgsaAc::Column>& out);
        bool Routines(const std::string& db, std::vector<NgsaAc::Routine>& out);
        unsigned long Now();
        bool Busy();
    private:
        bool Rows(const std::string& sql, std::vector<std::vector<std::string> >& rows);
        MDIWindow* m_wnd;
    };

    void        ApplyPending();                 /* invalidations recorded by OnStatement (any thread), on the UI thread */
    void        Show(HWND sci, bool forced);
    void        ShowCallTip(HWND sci, bool forced);
    void        UpdateCallTip(HWND sci);
    void        Accept(HWND sci, SCNotification* scn);
    std::string Text(HWND sci, size_t& caret, size_t& offset);
    std::string CurrentDb();
    void        LoadKeywords();
    NgsaAc::Prefs CasePrefs();

    MDIWindow*                  m_wnd;
    MySqlSource                 m_src;
    NgsaAc::Catalog             m_cat;
    NgsaAc::Keywords            m_kw;
    std::string                 m_kwFor;        /* the engine filter the keywords were loaded for */
    std::vector<NgsaAc::Item>   m_items;        /* the list on screen */
    bool                        m_inBackquote;
    bool                        m_swallowReturn;/* the WM_CHAR of the Enter that accepted an item */
    bool                        m_tip;          /* a calltip of ours is up */
    size_t                      m_tipParen;     /* its opening parenthesis */
    /* DDL seen by OnStatement arrives on the query thread; it is only recorded here and applied to the
       catalog on the UI thread by the next Show */
    CRITICAL_SECTION            m_cs;
    bool                        m_pendingDbs;       /* the database list */
    bool                        m_pendingCurrent;   /* the current database */
    std::vector<std::string>    m_pending;          /* named databases */
};

#endif
