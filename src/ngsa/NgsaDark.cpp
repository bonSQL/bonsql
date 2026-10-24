/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA (#2158): dark theme helpers for standard controls. See include/ngsa/NgsaDark.h.
 */
#include "ngsa/NgsaDark.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include "wyTheme.h"

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

namespace
{
    HBRUSH  s_fieldbrush = NULL;
    HHOOK   s_hook = NULL;
    bool    s_combo_ours = false;   /* the combo box being created belongs to us: so does its drop-down list */
    bool    s_accept_dialogs = false;   /* NgsaDark::AcceptDialogs: the Find/Replace dialog (comdlg32) is ours too */

    const UINT_PTR kControlSubclass = 0x4E475341;   /* "NGSA" */
    const UINT_PTR kParentSubclass  = 0x4E475342;
    const UINT_PTR kDialogSubclass  = 0x4E475343;
    const UINT_PTR kTopSubclass     = 0x4E475344;

    const COLORREF kBorder   = RGB(0x3F, 0x3F, 0x46);
    const COLORREF kHeader   = RGB(0x2D, 0x2D, 0x30);
    const COLORREF kTabIdle  = RGB(0x25, 0x25, 0x26);   /* idle tabs sit in the panel */
    const COLORREF kDisabled = RGB(0x8A, 0x8A, 0x8A);   /* disabled text on the panel */
    const COLORREF kFrame    = RGB(0x5A, 0x5A, 0x5A);   /* group box frame */

    enum { KIND_NONE = 0, KIND_FIELD, KIND_LIST, KIND_TREE, KIND_LISTVIEW, KIND_PUSH, KIND_CHECK, KIND_GROUP, KIND_TAB, KIND_HEADER, KIND_STATIC, KIND_SCROLL, KIND_PROGRESS };

    /* uxtheme's dark-mode switches (Windows 10 1809/1903+, exported by ordinal only): without them the
       DarkMode_Explorer theme leaves scroll bars light */
    typedef int  (WINAPI *SetPreferredAppModeFn)(int);          /* 0 default, 1 allow dark, 2 force dark */
    typedef BOOL (WINAPI *AllowDarkModeForWindowFn)(HWND, BOOL);
    typedef void (WINAPI *FlushMenuThemesFn)(void);
    typedef void (WINAPI *RefreshImmersiveColorPolicyStateFn)(void);
    int s_appmode = -1;

    void ApplyAppMode(bool on)
    {
        HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
        if(!ux)
            ux = LoadLibraryW(L"uxtheme.dll");
        if(!ux)
            return;
        SetPreferredAppModeFn setmode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        RefreshImmersiveColorPolicyStateFn refresh = (RefreshImmersiveColorPolicyStateFn)GetProcAddress(ux, MAKEINTRESOURCEA(104));
        FlushMenuThemesFn flush = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if(setmode)
            setmode(on ? 2 : 0);
        if(refresh)
            refresh();
        if(flush)
            flush();
        s_appmode = on ? 1 : 0;
    }

    void AllowDark(HWND hwnd, bool on)
    {
        HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
        AllowDarkModeForWindowFn allow = ux ? (AllowDarkModeForWindowFn)GetProcAddress(ux, MAKEINTRESOURCEA(133)) : NULL;
        if(s_appmode != (on ? 1 : 0))
            ApplyAppMode(on);
        if(allow)
            allow(hwnd, on ? TRUE : FALSE);
    }

    int ClassKind(HWND hwnd, LONG style)
    {
        wchar_t cls[64] = {0};
        GetClassNameW(hwnd, cls, 63);
        if(!_wcsicmp(cls, L"Edit") || !_wcsicmp(cls, L"ComboBox") || !_wcsicmp(cls, WC_COMBOBOXEXW))
            return KIND_FIELD;
        if(!_wcsicmp(cls, L"ListBox") || !_wcsicmp(cls, L"ComboLBox"))
            return KIND_LIST;
        if(!_wcsicmp(cls, WC_TREEVIEWW))
            return KIND_TREE;
        if(!_wcsicmp(cls, WC_LISTVIEWW))
            return KIND_LISTVIEW;
        if(!_wcsicmp(cls, WC_HEADERW))
            return KIND_HEADER;
        if(!_wcsicmp(cls, WC_TABCONTROLW))
            return KIND_TAB;
        if(!_wcsicmp(cls, UPDOWN_CLASSW))
            return KIND_PUSH;       /* spinners: the Explorer theme is the closest there is */
        if(!_wcsicmp(cls, L"Scintilla") || !_wcsicmp(cls, L"CustomGridControl") || !_wcsicmp(cls, L"ScrollBar") || !_wcsicmp(cls, L"bonSQLHtmlView"))
            return KIND_SCROLL;     /* their own painting; only the scroll bars take the theme */
        if(!_wcsicmp(cls, PROGRESS_CLASSW))
            return KIND_PROGRESS;
        if(!_wcsicmp(cls, L"Static"))
        {
            switch(style & SS_TYPEMASK)
            {
            case SS_LEFT:
            case SS_CENTER:
            case SS_RIGHT:
            case SS_SIMPLE:
            case SS_LEFTNOWORDWRAP:
                return KIND_STATIC;     /* text labels; icons, bitmaps, frames and owner-drawn stay as they are */
            }
        }
        if(!_wcsicmp(cls, L"Button"))
        {
            switch(style & BS_TYPEMASK)
            {
            case BS_PUSHBUTTON:
            case BS_DEFPUSHBUTTON:
                return KIND_PUSH;
            case BS_CHECKBOX:
            case BS_AUTOCHECKBOX:
            case BS_3STATE:
            case BS_AUTO3STATE:
            case BS_RADIOBUTTON:
            case BS_AUTORADIOBUTTON:
                return (style & BS_PUSHLIKE) ? KIND_PUSH : KIND_CHECK;
            case BS_GROUPBOX:
                return KIND_GROUP;
            }
        }
        return KIND_NONE;
    }

    LRESULT CALLBACK ControlProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT CALLBACK ParentProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT CALLBACK TopProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    bool IsMainClass(HWND hwnd)
    {
        wchar_t cls[64] = {0};
        GetClassNameW(hwnd, cls, 63);
        return _wcsicmp(cls, L"MainWindow") == 0;
    }

    /* a combo box's drop-down list: comctl32 creates it as a child of the desktop */
    bool IsDropList(HWND hwnd)
    {
        wchar_t cls[64] = {0};
        GetClassNameW(hwnd, cls, 63);
        return _wcsicmp(cls, L"ComboLBox") == 0;
    }

    bool IsOurDialog(HWND hwnd)
    {
        DWORD_PTR dummy;
        return GetWindowSubclass(hwnd, DialogProc, kDialogSubclass, &dummy) != FALSE;
    }

    /* The main window or one of our own dialogs is an ancestor. Not only the root: a Preferences page is
       our dialog inside a property-sheet frame that comctl32 built. */
    bool IsOurs(HWND hwndparent)
    {
        for(HWND h = hwndparent; h; h = GetParent(h))
            if(IsMainClass(h) || IsOurDialog(h))
                return true;
        return false;
    }

    bool IsInMain(HWND hwndparent)
    {
        HWND root = hwndparent ? GetAncestor(hwndparent, GA_ROOT) : NULL;
        return root && IsMainClass(root);
    }

    void DarkTitleBar(HWND hwnd)
    {
        BOOL on = NgsaDark::IsOn() ? TRUE : FALSE;
        if(hwnd && !(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD))
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof(on));
    }

    /* Check boxes in list views and trees are state images that comctl32 builds from the light Button theme.
       Replace them with dark ones: a list view's are unchecked, checked; a tree's start with a blank image. */
    HIMAGELIST DarkCheckImages(int cx, int cy, bool leadingblank)
    {
        const COLORREF  key = RGB(0xFF, 0x00, 0xFF);
        int             n = leadingblank ? 3 : 2, i, first = leadingblank ? 1 : 0;
        HDC             screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
        HBITMAP         bmp = CreateCompatibleBitmap(screen, cx * n, cy), old;
        HBRUSH          keybr = CreateSolidBrush(key);
        HIMAGELIST      il = ImageList_Create(cx, cy, ILC_COLOR24 | ILC_MASK, n, 0);
        RECT            all = {0, 0, cx * n, cy};

        old = (HBITMAP)SelectObject(mem, bmp);
        FillRect(mem, &all, keybr);
        for(i = first; i < n; i++)
        {
            RECT box = {i * cx + 1, 1, (i + 1) * cx - 1, cy - 1};
            NgsaDark::FrameControl(mem, &box, DFC_BUTTON, DFCS_BUTTONCHECK | (i == n - 1 ? DFCS_CHECKED : 0), wyTrue);
        }
        SelectObject(mem, old);
        if(il)
            ImageList_AddMasked(il, bmp, key);
        DeleteObject(keybr);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return il;
    }

    /* the old image list is left alone: the control may still own it */
    void DarkStateImages(HWND hwnd, int kind)
    {
        int cx = 16, cy = 16;
        if(!NgsaDark::IsOn())
            return;
        if(kind == KIND_LISTVIEW && (ListView_GetExtendedListViewStyle(hwnd) & LVS_EX_CHECKBOXES))
        {
            HIMAGELIST cur = ListView_GetImageList(hwnd, LVSIL_STATE);
            if(cur)
                ImageList_GetIconSize(cur, &cx, &cy);
            if(HIMAGELIST il = DarkCheckImages(cx, cy, false))
                ListView_SetImageList(hwnd, il, LVSIL_STATE);
        }
        else if(kind == KIND_TREE && (GetWindowLongPtr(hwnd, GWL_STYLE) & TVS_CHECKBOXES))
        {
            HIMAGELIST cur = TreeView_GetImageList(hwnd, TVSIL_STATE);
            if(cur)
                ImageList_GetIconSize(cur, &cx, &cy);
            if(HIMAGELIST il = DarkCheckImages(cx, cy, true))
                TreeView_SetImageList(hwnd, il, TVSIL_STATE);
        }
    }

    void ThemeKind(HWND hwnd, int kind)
    {
        bool on = NgsaDark::IsOn() ? true : false;

        if(!hwnd)
            return;
        switch(kind)
        {
        case KIND_FIELD:
            {
                LONG style = (LONG)GetWindowLongPtr(hwnd, GWL_STYLE);
                /* multi-line edits need the Explorer theme for their scroll bars; fields the CFD frame */
                AllowDark(hwnd, on);
                SetWindowTheme(hwnd, on ? ((style & ES_MULTILINE) ? L"DarkMode_Explorer" : L"DarkMode_CFD") : NULL, NULL);
            }
            break;
        case KIND_LIST:
        case KIND_PUSH:
        case KIND_SCROLL:
            AllowDark(hwnd, on);
            SetWindowTheme(hwnd, on ? L"DarkMode_Explorer" : NULL, NULL);     /* sends WM_THEMECHANGED itself; an extra one during WM_CREATE crashed comctl32 */
            break;
        case KIND_CHECK:
        case KIND_GROUP:
        case KIND_STATIC:
            break;      /* painted by ControlProc when the Dark theme is on */
        case KIND_TREE:
            AllowDark(hwnd, on);
            SetWindowTheme(hwnd, on ? L"DarkMode_Explorer" : NULL, NULL);
            /* colours: only ours to set in the Dark theme, and only ours to reset when we set them (a tree's
               default is (COLORREF)-1: CLR_DEFAULT reads as black); the object browser keeps its own */
            if(!GetPropW(hwnd, L"NgsaKeepColors"))
            {
                if(on)
                {
                    TreeView_SetBkColor(hwnd, NgsaDark::kBase);
                    TreeView_SetTextColor(hwnd, NgsaDark::kText);
                    SetPropW(hwnd, L"NgsaDarkColors", (HANDLE)1);
                }
                else if(RemovePropW(hwnd, L"NgsaDarkColors"))
                {
                    TreeView_SetBkColor(hwnd, (COLORREF)-1);
                    TreeView_SetTextColor(hwnd, (COLORREF)-1);
                }
            }
            TreeView_SetLineColor(hwnd, on ? RGB(0x5A, 0x5A, 0x5A) : CLR_DEFAULT);
            DarkStateImages(hwnd, kind);
            break;
        case KIND_LISTVIEW:
            AllowDark(hwnd, on);
            SetWindowTheme(hwnd, on ? L"DarkMode_Explorer" : NULL, NULL);
            if(on)
            {
                ListView_SetBkColor(hwnd, NgsaDark::kBase);
                ListView_SetTextBkColor(hwnd, NgsaDark::kBase);
                ListView_SetTextColor(hwnd, NgsaDark::kText);
                SetPropW(hwnd, L"NgsaDarkColors", (HANDLE)1);
            }
            else if(RemovePropW(hwnd, L"NgsaDarkColors"))
            {
                ListView_SetBkColor(hwnd, GetSysColor(COLOR_WINDOW));
                ListView_SetTextBkColor(hwnd, GetSysColor(COLOR_WINDOW));
                ListView_SetTextColor(hwnd, GetSysColor(COLOR_WINDOWTEXT));
            }
            DarkStateImages(hwnd, kind);
            break;
        case KIND_TAB:
        case KIND_HEADER:
            break;      /* painted by ControlProc */
        case KIND_PROGRESS:
            /* no dark window theme exists; without the visual style the bar takes its colours */
            SetWindowTheme(hwnd, on ? L"" : NULL, on ? L"" : NULL);
            SendMessage(hwnd, PBM_SETBKCOLOR, 0, on ? (LPARAM)NgsaDark::kInput : (LPARAM)CLR_DEFAULT);
            SendMessage(hwnd, PBM_SETBARCOLOR, 0, on ? (LPARAM)RGB(0x3A, 0x6E, 0xA5) : (LPARAM)CLR_DEFAULT);
            break;
        }
        InvalidateRect(hwnd, NULL, TRUE);
    }

    /* ---- custom paint for the parts no window theme covers ---- */

    void PaintHeader(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC         hdc = BeginPaint(hwnd, &ps);
        RECT        rc, item;
        HBRUSH      face = CreateSolidBrush(kHeader);
        HPEN        edge = CreatePen(PS_SOLID, 1, kBorder), oldpen;
        HFONT       font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0), oldfont = NULL;
        int         count = Header_GetItemCount(hwnd), i;
        wchar_t     text[256];
        HDITEMW     hdi;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, face);
        if(font)
            oldfont = (HFONT)SelectObject(hdc, font);
        oldpen = (HPEN)SelectObject(hdc, edge);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, NgsaDark::kText);
        for(i = 0; i < count; i++)
        {
            if(!Header_GetItemRect(hwnd, i, &item))
                continue;
            memset(&hdi, 0, sizeof(hdi));
            hdi.mask = HDI_TEXT | HDI_FORMAT;
            hdi.pszText = text;
            hdi.cchTextMax = 255;
            text[0] = 0;
            SendMessage(hwnd, HDM_GETITEMW, i, (LPARAM)&hdi);
            MoveToEx(hdc, item.right - 1, item.top, NULL);
            LineTo(hdc, item.right - 1, item.bottom);
            item.left += 6;
            item.right -= 6;
            DrawTextW(hdc, text, -1, &item, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |   /* & is a prefix, as in the native tab ("&&" titles) */
                      ((hdi.fmt & HDF_RIGHT) ? DT_RIGHT : (hdi.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT));
        }
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
        LineTo(hdc, rc.right, rc.bottom - 1);
        SelectObject(hdc, oldpen);
        if(oldfont)
            SelectObject(hdc, oldfont);
        DeleteObject(edge);
        DeleteObject(face);
        EndPaint(hwnd, &ps);
    }

    void PaintTab(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC         hdc = BeginPaint(hwnd, &ps);
        RECT        rc, item, page;
        HBRUSH      idle = CreateSolidBrush(kTabIdle), sel = CreateSolidBrush(NgsaDark::kInput), old;
        HPEN        edge = CreatePen(PS_SOLID, 1, kFrame), oldpen;
        HFONT       font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0), oldfont = NULL;
        LONG        style = (LONG)GetWindowLongPtr(hwnd, GWL_STYLE);
        bool        buttons = (style & TCS_BUTTONS) != 0;
        int         count = TabCtrl_GetItemCount(hwnd), cur = TabCtrl_GetCurSel(hwnd), i;
        wchar_t     text[256];
        TCITEMW     tci;

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, NgsaDark::PanelBrush());
        if(font)
            oldfont = (HFONT)SelectObject(hdc, font);
        oldpen = (HPEN)SelectObject(hdc, edge);
        old = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SetBkMode(hdc, TRANSPARENT);

        if(!buttons)
        {
            /* the page frame: everything below the tab row */
            page = rc;
            TabCtrl_AdjustRect(hwnd, FALSE, &page);
            page.left = rc.left; page.right = rc.right; page.bottom = rc.bottom;
            page.top -= 2;
            Rectangle(hdc, page.left, page.top, page.right, page.bottom);
        }
        for(i = 0; i < count; i++)
        {
            if(!TabCtrl_GetItemRect(hwnd, i, &item))
                continue;
            memset(&tci, 0, sizeof(tci));
            tci.mask = TCIF_TEXT;
            tci.pszText = text;
            tci.cchTextMax = 255;
            text[0] = 0;
            SendMessage(hwnd, TCM_GETITEMW, i, (LPARAM)&tci);
            if(i == cur && !buttons)
                item.bottom += 2;       /* the selected tab joins its page */
            FillRect(hdc, &item, i == cur ? sel : idle);
            if(i == cur || buttons)
                Rectangle(hdc, item.left, item.top, item.right, item.bottom + ((i == cur && !buttons) ? 1 : 0));
            SetTextColor(hdc, i == cur ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x9D, 0x9D, 0x9D));
            DrawTextW(hdc, text, -1, &item, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
        SelectObject(hdc, old);
        SelectObject(hdc, oldpen);
        if(oldfont)
            SelectObject(hdc, oldfont);
        DeleteObject(edge);
        DeleteObject(idle);
        DeleteObject(sel);
        EndPaint(hwnd, &ps);
    }


    /* ---- text controls painted here: the Button theme ignores the text colour for disabled captions and
       the Static control paints disabled text in the system grey, both unreadable on the dark panel ---- */

    HBRUSH AskParentBrush(HWND hwnd, HDC hdc)
    {
        HBRUSH hbr = (HBRUSH)SendMessage(GetParent(hwnd), WM_CTLCOLORSTATIC, (WPARAM)hdc, (LPARAM)hwnd);
        return hbr ? hbr : NgsaDark::PanelBrush();
    }

    COLORREF TextColorFor(HWND hwnd, HDC hdc)
    {
        COLORREF chosen = GetTextColor(hdc);     /* set by the parent's WM_CTLCOLORSTATIC (links, warnings) */
        if(!IsWindowEnabled(hwnd))
            return kDisabled;
        if(chosen == RGB(0, 0, 0) || chosen == GetSysColor(COLOR_WINDOWTEXT) || chosen == GetSysColor(COLOR_BTNTEXT))
            return NgsaDark::kText;
        return chosen;
    }

    void PaintStatic(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC         hdc;

        if(GetWindowTextLengthW(hwnd) == 0 && (HINSTANCE)GetWindowLongPtr(GetParent(hwnd), GWLP_HINSTANCE) != GetModuleHandle(NULL))
        {
            /* an empty static in a Windows dialog is a placeholder the dialog paints over itself (the Colour
               dialog's swatches and spectrum): leave it alone */
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return;
        }
        hdc = BeginPaint(hwnd, &ps);
        RECT        rc;
        LONG        style = (LONG)GetWindowLongPtr(hwnd, GWL_STYLE);
        HFONT       font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0), oldfont = NULL;
        HBRUSH      hbr = AskParentBrush(hwnd, hdc);
        UINT        fmt = 0;
        wchar_t     text[2048] = {0};

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, hbr);
        if(font)
            oldfont = (HFONT)SelectObject(hdc, font);
        SetTextColor(hdc, TextColorFor(hwnd, hdc));
        SetBkMode(hdc, TRANSPARENT);
        GetWindowTextW(hwnd, text, 2047);
        switch(style & SS_TYPEMASK)
        {
        case SS_CENTER:         fmt = DT_CENTER | DT_WORDBREAK; break;
        case SS_RIGHT:          fmt = DT_RIGHT | DT_WORDBREAK; break;
        case SS_SIMPLE:
        case SS_LEFTNOWORDWRAP: fmt = DT_LEFT | DT_SINGLELINE; break;
        default:                fmt = DT_LEFT | DT_WORDBREAK; break;
        }
        if(style & SS_NOPREFIX)
            fmt |= DT_NOPREFIX;
        if(style & SS_CENTERIMAGE)
            fmt |= DT_VCENTER | DT_SINGLELINE;
        if((style & SS_ELLIPSISMASK) == SS_ENDELLIPSIS)
            fmt |= DT_END_ELLIPSIS | DT_SINGLELINE;
        else if((style & SS_ELLIPSISMASK) == SS_PATHELLIPSIS)
            fmt |= DT_PATH_ELLIPSIS | DT_SINGLELINE;
        else if((style & SS_ELLIPSISMASK) == SS_WORDELLIPSIS)
            fmt |= DT_WORD_ELLIPSIS;
        if(fmt & DT_SINGLELINE)
            fmt &= ~DT_WORDBREAK;
        DrawTextW(hdc, text, -1, &rc, fmt);
        if(oldfont)
            SelectObject(hdc, oldfont);
        EndPaint(hwnd, &ps);
    }

    void PaintGroup(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC         hdc = BeginPaint(hwnd, &ps);
        RECT        rc, rt;
        HFONT       font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0), oldfont = NULL;
        HBRUSH      hbr = AskParentBrush(hwnd, hdc);
        HPEN        pen = CreatePen(PS_SOLID, 1, IsWindowEnabled(hwnd) ? kFrame : kBorder), oldpen;
        wchar_t     text[512] = {0};
        SIZE        sz = {0, 0};
        int         len, top;

        GetClientRect(hwnd, &rc);
        /* the controls inside the frame are siblings; whatever the z-order, never paint over them. A sibling
           that encloses the group (the Connect dialog's tab control, behind its pages) is not one of them. */
        for(HWND sib = GetWindow(GetParent(hwnd), GW_CHILD); sib; sib = GetWindow(sib, GW_HWNDNEXT))
        {
            RECT sr;
            if(sib == hwnd || !IsWindowVisible(sib))
                continue;
            GetWindowRect(sib, &sr);
            MapWindowPoints(NULL, hwnd, (LPPOINT)&sr, 2);
            if(sr.left <= rc.left && sr.top <= rc.top && sr.right >= rc.right && sr.bottom >= rc.bottom)
                continue;
            if(sr.right > rc.left && sr.left < rc.right && sr.bottom > rc.top && sr.top < rc.bottom)
                ExcludeClipRect(hdc, sr.left, sr.top, sr.right, sr.bottom);
        }
        if(font)
            oldfont = (HFONT)SelectObject(hdc, font);
        len = GetWindowTextW(hwnd, text, 511);
        if(len)
        {
            RECT m = {0, 0, 0, 0};
            DrawTextW(hdc, text, len, &m, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
            sz.cx = m.right; sz.cy = m.bottom;
        }
        else
        {
            TEXTMETRICW tm;
            GetTextMetricsW(hdc, &tm);
            sz.cy = tm.tmHeight;
        }
        top = sz.cy / 2;
        oldpen = (HPEN)SelectObject(hdc, pen);
        /* frame: left, bottom, right, then the top line around the caption */
        MoveToEx(hdc, rc.left, top, NULL);           LineTo(hdc, rc.left, rc.bottom - 1);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);    LineTo(hdc, rc.right - 1, top);
        if(len)
        {
            LineTo(hdc, rc.left + 9 + sz.cx + 3, top);
            MoveToEx(hdc, rc.left + 9 - 3, top, NULL);
            LineTo(hdc, rc.left, top);
            rt.left = rc.left + 9 - 2; rt.right = rt.left + sz.cx + 4; rt.top = rc.top; rt.bottom = rc.top + sz.cy;
            FillRect(hdc, &rt, hbr);
            SetTextColor(hdc, TextColorFor(hwnd, hdc));
            SetBkMode(hdc, TRANSPARENT);
            rt.left += 2;
            DrawTextW(hdc, text, len, &rt, DT_LEFT | DT_SINGLELINE | ((SendMessage(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0));   /* '&' marks the accelerator, as in every Button caption */
        }
        else
            LineTo(hdc, rc.left, top);
        SelectObject(hdc, oldpen);
        DeleteObject(pen);
        if(oldfont)
            SelectObject(hdc, oldfont);
        EndPaint(hwnd, &ps);
    }

    void PaintCheck(HWND hwnd)
    {
        PAINTSTRUCT ps;
        HDC         hdc = BeginPaint(hwnd, &ps);
        RECT        rc, box, rt;
        LONG        style = (LONG)GetWindowLongPtr(hwnd, GWL_STYLE);
        LONG        type = style & BS_TYPEMASK;
        bool        radio = (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON);
        bool        enabled = IsWindowEnabled(hwnd) ? true : false;
        bool        righttext = (style & BS_LEFTTEXT) != 0;
        int         check = (int)SendMessage(hwnd, BM_GETCHECK, 0, 0);
        LRESULT     ui = SendMessage(hwnd, WM_QUERYUISTATE, 0, 0);
        HFONT       font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0), oldfont = NULL;
        HBRUSH      hbr = AskParentBrush(hwnd, hdc);
        COLORREF    tc;
        UINT        fmt;
        wchar_t     text[1024] = {0};

        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, hbr);
        if(font)
            oldfont = (HFONT)SelectObject(hdc, font);
        tc = TextColorFor(hwnd, hdc);

        /* the 13 px glyph box */
        box.left = righttext ? rc.right - 13 : rc.left;
        box.right = box.left + 13;
        if(style & BS_TOP)
            box.top = rc.top + 1;
        else if(style & BS_BOTTOM)
            box.top = rc.bottom - 14;
        else
            box.top = rc.top + ((rc.bottom - rc.top) - 13) / 2;
        box.bottom = box.top + 13;

        if(radio)
        {
            HPEN   pen = CreatePen(PS_SOLID, 1, enabled ? RGB(0x9A, 0x9A, 0x9A) : RGB(0x5A, 0x5A, 0x5A)), oldpen;
            HBRUSH fill = CreateSolidBrush(enabled ? NgsaDark::kInput : NgsaDark::kPanel), oldbr;
            oldpen = (HPEN)SelectObject(hdc, pen);
            oldbr = (HBRUSH)SelectObject(hdc, fill);
            Ellipse(hdc, box.left, box.top, box.right, box.bottom);
            if(check == BST_CHECKED)
            {
                HBRUSH dot = CreateSolidBrush(enabled ? RGB(0xE0, 0xE0, 0xE0) : kDisabled);
                SelectObject(hdc, dot);
                SelectObject(hdc, GetStockObject(NULL_PEN));
                Ellipse(hdc, box.left + 4, box.top + 4, box.right - 3, box.bottom - 3);
                SelectObject(hdc, pen);
                DeleteObject(dot);
            }
            SelectObject(hdc, oldbr);
            SelectObject(hdc, oldpen);
            DeleteObject(fill);
            DeleteObject(pen);
        }
        else
        {
            UINT state = DFCS_BUTTONCHECK | (check == BST_CHECKED ? DFCS_CHECKED : 0) | (enabled ? 0 : DFCS_INACTIVE);
            NgsaDark::FrameControl(hdc, &box, DFC_BUTTON, state, wyTrue);
            if(check == BST_INDETERMINATE)
            {
                HBRUSH mid = CreateSolidBrush(enabled ? RGB(0xA0, 0xA0, 0xA0) : kDisabled);
                RECT   in = {box.left + 4, box.top + 4, box.right - 4, box.bottom - 4};
                FillRect(hdc, &in, mid);
                DeleteObject(mid);
            }
        }

        /* the caption */
        rt = rc;
        if(righttext)
            rt.right = box.left - 4;
        else
            rt.left = box.right + 4;
        fmt = (style & BS_MULTILINE) ? (DT_LEFT | DT_WORDBREAK | DT_TOP) : (DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if(style & BS_TOP)
            fmt = (fmt & ~DT_VCENTER) | DT_TOP;
        if(ui & UISF_HIDEACCEL)
            fmt |= DT_HIDEPREFIX;
        GetWindowTextW(hwnd, text, 1023);
        SetTextColor(hdc, tc);
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, text, -1, &rt, fmt);
        if(GetFocus() == hwnd && !(ui & UISF_HIDEFOCUS))
        {
            RECT fr = rt;
            DrawTextW(hdc, text, -1, &fr, fmt | DT_CALCRECT);
            if(!(style & BS_MULTILINE))
            {
                fr.top = rt.top + ((rt.bottom - rt.top) - (fr.bottom - fr.top)) / 2;
                fr.bottom = fr.top + (fr.bottom - fr.top);
            }
            InflateRect(&fr, 1, 1);
            DrawFocusRect(hdc, &fr);
        }
        if(oldfont)
            SelectObject(hdc, oldfont);
        EndPaint(hwnd, &ps);
    }

    /* A combo box is dropping down (CBN_DROPDOWN, lp = the combo or a wrapper around it): theme and
       subclass its list now. The subclass installed when the list was created does not take effect
       (comctl32 sets the list up after creation), so this is done here, once per list. */
    void ThemeDropList(HWND hwndcombo)
    {
        COMBOBOXINFO cbi = {sizeof(cbi)};
        wchar_t      cls[64] = {0};

        if(!hwndcombo || !NgsaDark::IsOn())
            return;
        GetClassNameW(hwndcombo, cls, 63);
        if(_wcsicmp(cls, L"ComboBox"))
            hwndcombo = FindWindowExW(hwndcombo, NULL, L"ComboBox", NULL);   /* CustomComboBox1 wraps one */
        if(!hwndcombo || !GetComboBoxInfo(hwndcombo, &cbi) || !cbi.hwndList)
            return;
        if(!GetPropW(cbi.hwndList, L"NgsaDarkList"))
        {
            SetPropW(cbi.hwndList, L"NgsaDarkList", (HANDLE)1);
            SetWindowSubclass(cbi.hwndList, ControlProc, kControlSubclass, (DWORD_PTR)KIND_LIST);
        }
        ThemeKind(cbi.hwndList, KIND_LIST);     /* every time: comctl32 re-themes the list after building the combo */
    }

    /* the controls sitting on a group box's frame (a check box in its caption) paint over it again: only
       the ones we paint ourselves, and at the next paint cycle (a synchronous paint of a themed button or
       list sends NM_CUSTOMDRAW to a dialog that may not have seen WM_INITDIALOG yet) */
    void RepaintOverlaps(HWND hwnd)
    {
        RECT rc;
        GetWindowRect(hwnd, &rc);
        for(HWND sib = GetWindow(GetParent(hwnd), GW_CHILD); sib; sib = GetWindow(sib, GW_HWNDNEXT))
        {
            RECT      sr;
            DWORD_PTR sibkind = KIND_NONE;
            if(sib == hwnd || !IsWindowVisible(sib))
                continue;
            if(!GetWindowSubclass(sib, ControlProc, kControlSubclass, &sibkind) || (sibkind != KIND_CHECK && sibkind != KIND_STATIC))
                continue;
            GetWindowRect(sib, &sr);
            if(sr.right > rc.left && sr.left < rc.right && sr.bottom > rc.top && sr.top < rc.bottom)
                RedrawWindow(sib, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
        }
    }

    bool IsComboEx(HWND hwnd)
    {
        wchar_t cls[64] = {0};
        GetClassNameW(hwnd, cls, 63);
        return _wcsicmp(cls, WC_COMBOBOXEXW) == 0;
    }

    /* A ComboBoxEx owner-draws the items of its inner combo (list and selection field) in the system
       colours, over the dark list: the toolbar's database combo dropped down white. Draw them here
       instead, the way comctl32 lays them out: indent of 10 px per level, image, text. */
    void DrawComboExItem(HWND hwndcbex, LPDRAWITEMSTRUCT dis)
    {
        HDC  hdc = dis->hDC;
        RECT rc = dis->rcItem;
        bool sel = (dis->itemState & ODS_SELECTED) && !(dis->itemState & ODS_COMBOBOXEDIT);

        HBRUSH bg = CreateSolidBrush(sel ? NgsaDark::kSelection : NgsaDark::kInput);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        if(dis->itemID == (UINT)-1)
            return;

        wchar_t          text[512] = {0};
        COMBOBOXEXITEMW  item = {0};
        item.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_INDENT;
        item.iItem = (INT_PTR)dis->itemID;
        item.pszText = text;
        item.cchTextMax = 511;
        if(!SendMessage(hwndcbex, CBEM_GETITEMW, 0, (LPARAM)&item))
            return;

        int        x = rc.left + 2 + ((dis->itemState & ODS_COMBOBOXEDIT) ? 0 : item.iIndent * 10);
        HIMAGELIST himl = (HIMAGELIST)SendMessage(hwndcbex, CBEM_GETIMAGELIST, 0, 0);
        int        image = sel ? item.iSelectedImage : item.iImage;
        if(himl)
        {
            int cx = 0, cy = 0;
            ImageList_GetIconSize(himl, &cx, &cy);
            if(image >= 0)
                ImageList_Draw(himl, image, hdc, x, rc.top + ((rc.bottom - rc.top) - cy) / 2, ILD_TRANSPARENT);
            x += cx + 4;
        }
        RECT tr = rc;
        tr.left = x;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, sel ? RGB(0xFF, 0xFF, 0xFF) : NgsaDark::kText);
        DrawTextW(hdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    LRESULT CALLBACK ControlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR kind)
    {
        switch(msg)
        {
        case WM_DRAWITEM:
            if(NgsaDark::IsOn() && kind == KIND_FIELD && IsComboEx(hwnd))
            {
                DrawComboExItem(hwnd, (LPDRAWITEMSTRUCT)lp);
                return TRUE;
            }
            break;
        case WM_CREATE:
            {
                LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
                if(NgsaDark::IsOn())
                    ThemeKind(hwnd, (int)kind);
                return r;
            }
        case WM_CTLCOLOREDIT:       /* a combo box's own edit and drop-down list */
        case WM_CTLCOLORLISTBOX:
            {
                HBRUSH hbr = NgsaDark::FieldColors((HDC)wp);
                if(msg == WM_CTLCOLORLISTBOX && lp && NgsaDark::IsOn() && !GetPropW((HWND)lp, L"NgsaDarkList"))
                {
                    /* the drop-down list: comctl32 re-themes it after creation, so theme it from here (its
                       scroll bar is light otherwise) */
                    SetPropW((HWND)lp, L"NgsaDarkList", (HANDLE)1);
                    ThemeKind((HWND)lp, KIND_LIST);
                }
                if(hbr)
                    return (LRESULT)hbr;
            }
            break;
        case WM_CTLCOLORSTATIC:     /* the Connect dialog re-parents its page controls into the tab control */
        case WM_CTLCOLORBTN:
            {
                HBRUSH hbr = NgsaDark::LabelColors((HDC)wp);
                if(hbr)
                    return (LRESULT)hbr;
            }
            break;
        case WM_THEMECHANGED:
        case LVM_SETEXTENDEDLISTVIEWSTYLE:
            if(NgsaDark::IsOn() && (kind == KIND_LISTVIEW || kind == KIND_TREE))
            {
                /* the control (re)builds its light check box images here: put the dark ones back */
                LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
                if(msg == WM_THEMECHANGED || kind == KIND_LISTVIEW)
                    DarkStateImages(hwnd, (int)kind);
                return r;
            }
            break;
        case WM_ERASEBKGND:
            if(NgsaDark::IsOn() && (kind == KIND_TAB || kind == KIND_HEADER || kind == KIND_GROUP || kind == KIND_CHECK || kind == KIND_STATIC))
                return 1;
            if(NgsaDark::IsOn() && kind == KIND_LISTVIEW && !IsWindowEnabled(hwnd))
            {
                /* a disabled list view erases in COLOR_3DFACE, not its background colour */
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect((HDC)wp, &rc, NgsaDark::PanelBrush());   /* disabled: the panel, like a disabled field */
                return 1;
            }
            if(NgsaDark::IsOn() && kind == KIND_LIST && IsDropList(hwnd))
            {
                /* a combo's drop-down list popup: its WM_CTLCOLORLISTBOX does not always reach us (the
                   CustomComboBox1 wrapper), so erase it here in the field colour */
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect((HDC)wp, &rc, NgsaDark::FieldColors((HDC)wp));
                return 1;
            }
            break;
        case WM_SHOWWINDOW:
            if(wp && NgsaDark::IsOn() && kind == KIND_LIST && IsDropList(hwnd))
            {
                /* the theme applied in WM_CREATE does not stick (comctl32 sets the list up after, and
                   shows it once while doing so): apply it whenever it drops down */
                SetPropW(hwnd, L"NgsaDarkList", (HANDLE)1);
                ThemeKind(hwnd, KIND_LIST);
            }
            break;
        case WM_PAINT:
            if(NgsaDark::IsOn())
            {
                switch(kind)
                {
                case KIND_HEADER:   PaintHeader(hwnd);  return 0;
                case KIND_TAB:      PaintTab(hwnd);     return 0;
                case KIND_GROUP:    PaintGroup(hwnd);   return 0;
                case KIND_CHECK:    PaintCheck(hwnd);   return 0;
                case KIND_STATIC:   PaintStatic(hwnd);  return 0;
                }
            }
            break;
        case BM_SETCHECK:
        case BM_SETSTATE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
        case WM_UPDATEUISTATE:
        case WM_SETTEXT:
            if(NgsaDark::IsOn() && (kind == KIND_CHECK || kind == KIND_STATIC || kind == KIND_GROUP))
            {
                /* the control paints its own (light) state change directly, over whatever overlaps it: a
                   group box's frame runs under the check box in its caption. A group box takes nothing
                   from WM_ENABLE but that repaint, so it is not passed on; after ours, the controls on the
                   frame paint themselves again on top. */
                LRESULT r = (kind == KIND_GROUP && msg == WM_ENABLE) ? 0 : DefSubclassProc(hwnd, msg, wp, lp);
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | (msg == WM_SETTEXT ? 0 : RDW_UPDATENOW));
                if(kind == KIND_GROUP)
                    RepaintOverlaps(hwnd);
                return r;
            }
            break;
        case WM_NCPAINT:
            if(NgsaDark::IsOn() && kind == KIND_LIST && IsDropList(hwnd))
            {
                /* a combo's drop-down list has a 1 px border: paint it dark */
                LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
                HDC     hdc = GetWindowDC(hwnd);
                RECT    rc;
                HBRUSH  br = CreateSolidBrush(kBorder);
                GetWindowRect(hwnd, &rc);
                OffsetRect(&rc, -rc.left, -rc.top);
                FrameRect(hdc, &rc, br);
                DeleteObject(br);
                ReleaseDC(hwnd, hdc);
                return r;
            }
            break;
        case WM_NCDESTROY:
            RemovePropW(hwnd, L"NgsaDarkList");
            RemovePropW(hwnd, L"NgsaDarkColors");
            RemovePropW(hwnd, L"NgsaKeepColors");
            RemoveWindowSubclass(hwnd, ControlProc, id);
            break;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK ParentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
    {
        switch(msg)
        {
        case WM_COMMAND:
            if(HIWORD(wp) == CBN_DROPDOWN)
                ThemeDropList((HWND)lp);
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            {
                HBRUSH hbr = NgsaDark::FieldColors((HDC)wp);
                if(hbr)
                    return (LRESULT)hbr;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ParentProc, id);
            break;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    /* Our dialogs: the subclass runs before the dialog procedure, so the dark answers win; a text
       colour the dialog chose itself (links, warnings) is kept, only the default black is replaced. */
    LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ismsgbox)
    {
        switch(msg)
        {
        case WM_PAINT:
            if(NgsaDark::IsOn() && ismsgbox)
            {
                /* user32's message box paints the button band in COLOR_3DFACE in its own WM_PAINT; paint
                   the panel over it afterwards, around the buttons */
                LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
                RECT    rc, br;
                int     top = -1;
                HDC     hdc;

                GetClientRect(hwnd, &rc);
                for(HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
                {
                    wchar_t cls[16] = {0};
                    GetClassNameW(c, cls, 15);
                    if(_wcsicmp(cls, L"Button"))
                        continue;
                    GetWindowRect(c, &br);
                    MapWindowPoints(NULL, hwnd, (LPPOINT)&br, 2);
                    if(top < 0 || br.top < top)
                        top = br.top;
                }
                if(top > 0)
                {
                    hdc = GetDC(hwnd);
                    for(HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
                    {
                        if(!IsWindowVisible(c))
                            continue;
                        GetWindowRect(c, &br);
                        MapWindowPoints(NULL, hwnd, (LPPOINT)&br, 2);
                        ExcludeClipRect(hdc, br.left, br.top, br.right, br.bottom);
                    }
                    rc.top = top - 12;
                    FillRect(hdc, &rc, NgsaDark::PanelBrush());
                    ReleaseDC(hwnd, hdc);
                }
                return r;
            }
            break;
        case WM_INITDIALOG:
            {
                LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
                DarkTitleBar(hwnd);
                return r;
            }
        case WM_COMMAND:
            if(HIWORD(wp) == CBN_DROPDOWN)
                ThemeDropList((HWND)lp);
            break;
        case WM_ERASEBKGND:
            /* DefDlgProc asks the dialog procedure for WM_CTLCOLORDLG directly (not through the window
               procedure), so the background is painted here instead */
            if(NgsaDark::IsOn())
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect((HDC)wp, &rc, NgsaDark::PanelBrush());
                return 1;
            }
            break;
        case WM_CTLCOLORDLG:
            if(NgsaDark::IsOn())
                return (LRESULT)NgsaDark::PanelBrush();
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if(NgsaDark::IsOn())
            {
                HDC         hdc = (HDC)wp;
                COLORREF    chosen;

                DefSubclassProc(hwnd, msg, wp, lp);
                chosen = GetTextColor(hdc);
                if(chosen == RGB(0, 0, 0) || chosen == GetSysColor(COLOR_WINDOWTEXT) || chosen == GetSysColor(COLOR_BTNTEXT))
                    SetTextColor(hdc, NgsaDark::kText);
                SetBkColor(hdc, NgsaDark::kPanel);
                SetBkMode(hdc, TRANSPARENT);
                return (LRESULT)NgsaDark::PanelBrush();
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            {
                HBRUSH hbr = NgsaDark::FieldColors((HDC)wp);
                if(hbr)
                    return (LRESULT)hbr;
            }
            break;
        case WM_NOTIFY:
            if(NgsaDark::IsOn() && lp && ((LPNMHDR)lp)->code == NM_CUSTOMDRAW)
            {
                wchar_t cls[64] = {0};
                GetClassNameW(((LPNMHDR)lp)->hwndFrom, cls, 63);
                if(!_wcsicmp(cls, WC_TREEVIEWW))
                    return NgsaDark::TreeCustomDraw((LPNMTVCUSTOMDRAW)lp);
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, DialogProc, id);
            break;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK TopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
    {
        switch(msg)
        {
        case WM_COMMAND:
            if(HIWORD(wp) == CBN_DROPDOWN)
                ThemeDropList((HWND)lp);
            break;
        case WM_CREATE:
            {
                LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
                DarkTitleBar(hwnd);
                return r;
            }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TopProc, id);
            break;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK CbtProc(int code, WPARAM wp, LPARAM lp)
    {
        if(code == HCBT_CREATEWND)
        {
            HWND            hwnd = (HWND)wp;
            LPCBT_CREATEWND pc = (LPCBT_CREATEWND)lp;
            LPCREATESTRUCT  cs = pc ? pc->lpcs : NULL;
            wchar_t         cls[64] = {0};

            if(cs)
            {
                GetClassNameW(hwnd, cls, 63);
                if(!_wcsicmp(cls, L"#32770"))
                {
                    bool accept = s_accept_dialogs;     /* one shot: the dialog FindText/ReplaceText is creating */
                    s_accept_dialogs = false;
                    /* only dialogs built from this module's templates: message boxes and the common
                       dialogs belong to Windows and keep its look */
                    if(accept || cs->hInstance == GetModuleHandle(NULL) || cs->hInstance == GetModuleHandleW(L"user32.dll")
                       || cs->hInstance == GetModuleHandleW(L"comctl32.dll"))   /* message boxes; property-sheet frames (Preferences) */
                        SetWindowSubclass(hwnd, DialogProc, kDialogSubclass, cs->hInstance == GetModuleHandleW(L"user32.dll") ? 1 : 0);
                }
                else if(!_wcsicmp(cls, L"MainWindow"))
                {
                    SetWindowSubclass(hwnd, TopProc, kTopSubclass, 0);
                }
                else
                {
                    int  kind = ClassKind(hwnd, cs->style);
                    bool ours = IsOurs(cs->hwndParent);
                    /* a combo box's drop-down list: comctl32 creates it as a child of the desktop while the
                       combo itself is being created, so it is ours when that combo is */
                    if(!_wcsicmp(cls, L"ComboBox") || !_wcsicmp(cls, WC_COMBOBOXEXW))
                        s_combo_ours = ours;
                    else if(!_wcsicmp(cls, L"ComboLBox"))
                        ours = ours || s_combo_ours;
                    if(kind != KIND_NONE && ((cs->style & WS_CHILD) || kind == KIND_LIST) && ours)
                    {
                        SetWindowSubclass(hwnd, ControlProc, kControlSubclass, (DWORD_PTR)kind);
                        if(kind == KIND_GROUP)
                            cs->style |= WS_CLIPSIBLINGS;    /* never paint the frame over the controls inside */
                        if(kind == KIND_FIELD && !IsOurDialog(cs->hwndParent))
                            SetWindowSubclass(cs->hwndParent, ParentProc, kParentSubclass, 0);
                    }
                }
            }
        }
        return CallNextHookEx(s_hook, code, wp, lp);
    }

    BOOL CALLBACK RethemeProc(HWND hwnd, LPARAM)
    {
        DWORD_PTR kind = 0;
        if(GetWindowSubclass(hwnd, ControlProc, kControlSubclass, &kind))
            ThemeKind(hwnd, (int)kind);
        return TRUE;
    }

    BOOL CALLBACK RethemeTopProc(HWND hwnd, LPARAM)
    {
        if(IsMainClass(hwnd) || IsOurDialog(hwnd))
        {
            DarkTitleBar(hwnd);
            EnumChildWindows(hwnd, RethemeProc, 0);
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        return TRUE;
    }
}

void
NgsaDark::AcceptDialogs(wyBool accept)
{
    s_accept_dialogs = accept ? true : false;
}

void
NgsaDark::InstallHook()
{
    if(!s_hook)
        s_hook = SetWindowsHookEx(WH_CBT, CbtProc, NULL, GetCurrentThreadId());
}

void
NgsaDark::RethemeAll(HWND hwndmain)
{
    ApplyAppMode(IsOn() ? true : false);
    EnumThreadWindows(GetCurrentThreadId(), RethemeTopProc, 0);
    if(hwndmain)
        RethemeTopProc(hwndmain, 0);
}

wyBool
NgsaDark::InMainWindow(HWND hwnd)
{
    return IsInMain(hwnd) ? wyTrue : wyFalse;
}

COLORREF
NgsaDark::Panel(COLORREF light)
{
    return IsOn() ? kPanel : light;
}

HBRUSH
NgsaDark::PanelBrush()
{
    static HBRUSH s_panel = NULL;
    if(!s_panel)
        s_panel = CreateSolidBrush(kPanel);
    return s_panel;
}

HBRUSH
NgsaDark::LabelColors(HDC hdc)
{
    if(!IsOn())
        return NULL;
    SetTextColor(hdc, kText);
    SetBkColor(hdc, kPanel);
    SetBkMode(hdc, TRANSPARENT);
    return PanelBrush();
}

BOOL
NgsaDark::FrameControl(HDC hdc, LPRECT prect, UINT type, UINT state, wyBool dark)
{
    if(dark && type == DFC_SCROLL && (state & 0x000F) == DFCS_SCROLLSIZEGRIP)
    {
        /* resize grip: three diagonal rows of dots on the panel */
        RECT    r = *prect;
        HBRUSH  dot = CreateSolidBrush(RGB(0x6E, 0x6E, 0x6E));
        int     i, j, w = r.right - r.left, h = r.bottom - r.top;

        FillRect(hdc, &r, NgsaDark::PanelBrush());
        for(i = 1; i <= 3; i++)
            for(j = 1; j <= i; j++)
            {
                RECT d;
                d.right = r.right - 2 - (j - 1) * 4;  d.left = d.right - 2;
                d.bottom = r.bottom - 2 - (i - j) * 4; d.top = d.bottom - 2;
                if(d.left >= r.left && d.top >= r.top)
                    FillRect(hdc, &d, dot);
            }
        DeleteObject(dot);
        return TRUE;
    }

    if(dark && type == DFC_SCROLL && (state & 0x000F) == DFCS_SCROLLDOWN)
    {
        /* drop-down arrow button: dark face, light chevron */
        RECT    r = *prect;
        HBRUSH  face = CreateSolidBrush(RGB(0x3E, 0x3E, 0x42));
        HPEN    edge = CreatePen(PS_SOLID, 1, RGB(0x55, 0x55, 0x5A));
        HPEN    glyph = CreatePen(PS_SOLID, 1, RGB(0xD4, 0xD4, 0xD4));
        HPEN    oldp;
        HBRUSH  oldb;
        int     cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;

        FillRect(hdc, &r, face);
        oldp = (HPEN)SelectObject(hdc, edge);
        oldb = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, glyph);
        for(int i = 0; i < 3; i++)          /* filled 5-3-1 triangle */
        {
            MoveToEx(hdc, cx - 2 + i, cy - 1 + i, NULL);
            LineTo(hdc, cx + 3 - i, cy - 1 + i);
        }
        SelectObject(hdc, oldb);
        SelectObject(hdc, oldp);
        DeleteObject(face); DeleteObject(edge); DeleteObject(glyph);
        return TRUE;
    }

    /* DFCS_BUTTONCHECK is 0: a check box is a DFC_BUTTON with no push bit and no radio/3-state bits */
    bool    ispush  = (state & DFCS_BUTTONPUSH) != 0;
    bool    ischeck = !ispush && (state & 0x000F) == 0;

    if(!dark || type != DFC_BUTTON || (!ispush && !ischeck))
        return DrawFrameControl(hdc, prect, type, state);

    RECT    r = *prect;
    HPEN    pen, oldpen;
    HBRUSH  fill;

    if(ischeck)
    {
        /* square box, centred, like the classic control */
        int size = min(r.right - r.left, r.bottom - r.top);
        if(size > 13)
            size = 13;
        r.left += ((r.right - r.left) - size) / 2;
        r.top  += ((r.bottom - r.top) - size) / 2;
        r.right = r.left + size;
        r.bottom = r.top + size;
        fill = CreateSolidBrush((state & DFCS_INACTIVE) ? kPanel : kInput);
        pen  = CreatePen(PS_SOLID, 1, (state & DFCS_INACTIVE) ? RGB(0x50, 0x50, 0x50) : RGB(0x86, 0x86, 0x86));
    }
    else
    {
        fill = CreateSolidBrush((state & DFCS_PUSHED) ? RGB(0x2D, 0x2D, 0x30) : RGB(0x3E, 0x3E, 0x42));
        pen  = CreatePen(PS_SOLID, 1, RGB(0x55, 0x55, 0x5A));
    }
    FillRect(hdc, &r, fill);
    oldpen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldbr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, oldbr);

    if(ischeck && (state & DFCS_CHECKED))
    {
        HPEN tick = CreatePen(PS_SOLID, 2, (state & DFCS_INACTIVE) ? RGB(0x80, 0x80, 0x80) : RGB(0xE0, 0xE0, 0xE0));
        int  w = r.right - r.left, h = r.bottom - r.top;
        SelectObject(hdc, tick);
        MoveToEx(hdc, r.left + w * 3 / 13, r.top + h * 7 / 13, NULL);
        LineTo(hdc, r.left + w * 5 / 13, r.top + h * 9 / 13);
        LineTo(hdc, r.left + w * 10 / 13, r.top + h * 4 / 13);
        SelectObject(hdc, pen);
        DeleteObject(tick);
    }
    SelectObject(hdc, oldpen);
    DeleteObject(pen);
    DeleteObject(fill);
    if(state & DFCS_ADJUSTRECT)
        InflateRect(prect, -2, -2);
    return TRUE;
}

wyBool
NgsaDark::IsOn()
{
    return wyTheme::IsDarkThemeActive();
}

void
NgsaDark::ThemeControl(HWND hwnd, wyBool isfield, wyBool keepcolors)
{
    if(!hwnd)
        return;
    if(keepcolors)
        SetPropW(hwnd, L"NgsaKeepColors", (HANDLE)1);
    ThemeKind(hwnd, isfield ? KIND_FIELD : ClassKind(hwnd, (LONG)GetWindowLongPtr(hwnd, GWL_STYLE)));
}

HBRUSH
NgsaDark::FieldColors(HDC hdc)
{
    if(!IsOn())
        return NULL;
    if(!s_fieldbrush)
        s_fieldbrush = CreateSolidBrush(kInput);
    SetTextColor(hdc, kText);
    SetBkColor(hdc, kInput);
    return s_fieldbrush;
}

LRESULT
NgsaDark::TreeCustomDraw(LPNMTVCUSTOMDRAW pcd)
{
    if(!IsOn())
        return CDRF_DODEFAULT;

    switch(pcd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
        {
            UINT state = TreeView_GetItemState(pcd->nmcd.hdr.hwndFrom, (HTREEITEM)pcd->nmcd.dwItemSpec, TVIS_SELECTED | TVIS_DROPHILITED);
            if(state & (TVIS_SELECTED | TVIS_DROPHILITED))
            {
                pcd->clrText   = RGB(0xFF, 0xFF, 0xFF);
                pcd->clrTextBk = kSelection;
                /* the theme would paint its own (light) selection: clear the state for this paint */
                pcd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS | CDIS_HOT);
                return CDRF_NEWFONT | CDRF_NOTIFYPOSTPAINT;
            }
        }
        return CDRF_DODEFAULT;

    case CDDS_ITEMPOSTPAINT:
        {
            /* with the tree focused, DarkMode_Explorer draws the selected text in its own (black) colour
               whatever clrText says: paint the text again */
            HWND        tree = pcd->nmcd.hdr.hwndFrom;
            HTREEITEM   item = (HTREEITEM)pcd->nmcd.dwItemSpec;
            TVITEMW     tvi = {0};
            wchar_t     text[512] = {0};
            RECT        rc;
            HFONT       font = (HFONT)SendMessage(tree, WM_GETFONT, 0, 0), bold = NULL, oldfont;
            HBRUSH      sel;

            tvi.mask = TVIF_TEXT | TVIF_STATE;
            tvi.hItem = item;
            tvi.pszText = text;
            tvi.cchTextMax = 511;
            tvi.stateMask = TVIS_BOLD | TVIS_SELECTED | TVIS_DROPHILITED;
            if(!TreeView_GetItem(tree, &tvi) || !(tvi.state & (TVIS_SELECTED | TVIS_DROPHILITED)) || !TreeView_GetItemRect(tree, item, &rc, TRUE))
                return CDRF_DODEFAULT;
            if(tvi.state & TVIS_BOLD)
            {
                LOGFONTW lf;
                if(GetObjectW(font ? font : GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf))
                {
                    lf.lfWeight = FW_BOLD;
                    bold = CreateFontIndirectW(&lf);
                }
            }
            sel = CreateSolidBrush(kSelection);
            FillRect(pcd->nmcd.hdc, &rc, sel);
            DeleteObject(sel);
            oldfont = (HFONT)SelectObject(pcd->nmcd.hdc, bold ? bold : font);
            SetTextColor(pcd->nmcd.hdc, RGB(0xFF, 0xFF, 0xFF));
            SetBkMode(pcd->nmcd.hdc, TRANSPARENT);
            rc.left += 2;
            DrawTextW(pcd->nmcd.hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(pcd->nmcd.hdc, oldfont);
            if(bold)
                DeleteObject(bold);
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}
