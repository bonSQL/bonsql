/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA (#2158): dark theme helpers for the controls the theme XML cannot reach.
 *
 * wyTheme colours what SQLyog draws itself (frame, tabs, grids, Scintilla editors). Standard Win32
 * controls (tree views, edits, combo boxes, buttons, scroll bars) paint with the system visual style,
 * so in the Dark theme they need:
 *   - the "DarkMode_Explorer" / "DarkMode_CFD" window themes (dark scroll bars, tree glyphs, combo
 *     drop-downs; Windows 10 1809 and later, ignored elsewhere),
 *   - a WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX answer for the field background and text,
 *   - custom draw for the tree view's selected row.
 * Every helper does nothing (and returns NULL / CDRF_DODEFAULT) when the Dark theme is not active,
 * so callers can call them unconditionally.
 */
#ifndef NGSA_DARK_H
#define NGSA_DARK_H

#include <windows.h>
#include <commctrl.h>
#include "Datatype.h"

namespace NgsaDark
{
    /* Colours shared with include/Dark.xml */
    const COLORREF  kBase      = RGB(0x1E, 0x1E, 0x1E);
    const COLORREF  kPanel     = RGB(0x25, 0x25, 0x26);
    const COLORREF  kInput     = RGB(0x3C, 0x3C, 0x3C);
    const COLORREF  kText      = RGB(0xD4, 0xD4, 0xD4);
    const COLORREF  kSelection = RGB(0x26, 0x4F, 0x78);

    wyBool      IsOn();

    /* Dark window theme for a tree view, list view or scroll bar ("DarkMode_Explorer"), or for edits and
       combo boxes ("DarkMode_CFD"); restores the default theme when the Dark theme is off. */
    /* keepcolors: the caller owns the background/text colours (the object browser: the connection's own
       colours, or the theme's) and they are never touched here */
    void        ThemeControl(HWND hwnd, wyBool isfield = wyFalse, wyBool keepcolors = wyFalse);

    /* WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX answer for a field: sets the DC colours and returns the brush,
       NULL when the Dark theme is off (caller then falls through to its own handling). */
    HBRUSH      FieldColors(HDC hdc);

    /* NM_CUSTOMDRAW for a tree view: the selected row in the selection colour, white text. */
    LRESULT     TreeCustomDraw(LPNMTVCUSTOMDRAW pcd);

    /* A WH_CBT hook on the UI thread. The main window, every dialog built from this module's own
       templates, message boxes (user32), property-sheet frames (comctl32) and the dialogs flagged with
       AcceptDialogs get a dark title bar, a dark background (painted in WM_ERASEBKGND: DefDlgProc never
       routes WM_CTLCOLORDLG through a subclass) and palette answers to WM_CTLCOLORSTATIC/BTN/EDIT/LISTBOX
       (a text colour a dialog chose itself, such as a link, is kept). Every edit, combo box, list box,
       tree view, list view, header, tab control, button, check box, radio, group box, text static,
       scroll bar, progress bar, Scintilla and custom grid created under them is subclassed: dark window
       theme (with uxtheme's dark app mode, ordinals 133/135), list/tree colours and check-box images, and
       what no dark theme covers is painted here (tab strips, list headers, labels, group boxes, check
       boxes, the message-box button band, the combo drop-down list). The whole story, with the traps,
       is docs/ngsa/DARK-THEME.md. Install once, before the main window is created. */
    void        InstallHook();
    /* the next dialog created is treated as ours (one shot): before FindText/ReplaceText (comdlg32's template) */
    void        AcceptDialogs(wyBool accept);

    /* Re-apply the window themes after the user switches themes (called from wyTheme::ApplyTheme). */
    void        RethemeAll(HWND hwndmain);

    /* True when the window's top-level ancestor is the main window (not a dialog). */
    wyBool      InMainWindow(HWND hwnd);

    /* Panel background for windows SQLyog paints itself with a fixed light colour (table designer):
       `light` when the Dark theme is off. PanelBrush is shared, do not delete it. */
    COLORREF    Panel(COLORREF light);
    HBRUSH      PanelBrush();
    /* WM_CTLCOLORSTATIC answer for labels on a dark panel (NULL when the Dark theme is off). */
    HBRUSH      LabelColors(HDC hdc);

    /* DrawFrameControl for the custom grid: when `dark`, check boxes and push buttons are painted in the
       dark palette (DrawFrameControl only knows the classic light look); otherwise the original call. */
    BOOL        FrameControl(HDC hdc, LPRECT prect, UINT type, UINT state, wyBool dark);
}

#endif /* NGSA_DARK_H */
