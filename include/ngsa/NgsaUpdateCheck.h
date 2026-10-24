/* Copyright (C) 2026 André Walsh
   This file is part of bonSQL and is distributed under the GNU General Public License, version 3 or later.

   Check for new versions (24/10/2026). bonSQL fetches one small JSON file from the bonSQL website over HTTPS
   (BRAND_UPDATE_URL_A) and compares the version in it with its own. The request carries nothing about the
   installation: no version, no identifier, a bare "bonSQL" user agent. The program never downloads or installs
   anything; it only tells the user and can open the download page in the browser.

   Two ways in:
     - Help > Check for Updates: the dialog opens at once, fetches in a worker thread, then shows the result.
     - At start-up, when "Check for new versions once a week" is ticked in Preferences (off by default) and the
       last check is seven or more days old: a worker thread fetches and, if a newer version is there and not
       skipped, posts NGSA_WM_UPDATEFOUND to the main window, which shows the same dialog.

   The file: {"version": "2026.11.1", "date": "2026-11-02", "url": "https://bonsql.org/download/", "note": "..."}
   Versions compare numerically, dot by dot. Settings in [GENERALPREF]: UpdateCheck (0/1), UpdatesCheckedDay
   (days since 1601), VersionToBeIgnored. */

#ifndef NGSA_UPDATECHECK_H
#define NGSA_UPDATECHECK_H

#include <windows.h>
#include "wyString.h"

#define NGSA_WM_UPDATEFOUND     (WM_APP + 41)   /* lParam = NgsaUpdate::Info*, the receiver deletes it */

namespace NgsaUpdate
{
    struct Info
    {
        wyString    version;    /* "2026.11.1" */
        wyString    date;       /* as published, shown as is */
        wyString    url;        /* download page; BRAND_DOWNLOAD_URL_A when the file has none */
        wyString    note;       /* one line, optional */
    };

    /* Preferences > General, "Check for new versions once a week" */
    wyBool      IsEnabled();
    void        SetEnabled(wyBool on);

    /* Help > Check for Updates */
    void        CheckNow(HWND hwndowner);

    /* Called once when the main window is up; returns at once, the work happens on a thread */
    void        CheckAtStartup(HWND hwndmain);

    /* NGSA_WM_UPDATEFOUND handler */
    void        ShowFound(HWND hwndowner, LPARAM lparam);

    /* The HTTPS fetch and parse; wyFalse with `error` set (text for the user) when anything goes wrong */
    wyBool      Fetch(Info &info, wyString &error);

    /* Negative, zero or positive like strcmp; "2026.10" == "2026.10.0" */
    wyInt32     CompareVersions(const wyChar *a, const wyChar *b);

    /* "2026.10.8" from Version.h */
    void        CurrentVersion(wyString &out);
}

#endif
