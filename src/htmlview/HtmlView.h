/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * bonSQL HtmlView: an HTML/CSS view window built on litehtml (third_party/litehtml, BSD-3-Clause, with its
 * bundled gumbo parser, Apache-2.0). Replaces the HTMLayout window of the Info tab.
 *
 * This header is plain Win32: no litehtml or C++17 headers leak into callers. One window = one document.
 * The window paints, scrolls (bars, wheel, keys), follows links (cursor + click callback), and supports text
 * selection with the mouse, double-click (word), Ctrl+A, Ctrl+C / Ctrl+Insert, and a Copy / Select All menu.
 * Copied text is plain text; table cells are separated by tabs and lines by CR LF, so it pastes into a
 * spreadsheet as cells.
 *
 * Images: "res:NAME.PNG" is loaded from the host module's resources (type "PNG", name NAME); anything else
 * goes to the image callback if one is set, else is read as a file path. PNGs are decoded with WIC.
 */
#ifndef BONSQL_HTMLVIEW_H
#define BONSQL_HTMLVIEW_H

#include <windows.h>

namespace HtmlView
{
    /* window class name; registered by Register(), which Create() also calls */
    extern const wchar_t* const ClassName;

    bool    Register(HINSTANCE hinst);
    HWND    Create(HWND parent, int id, DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP);

    /* Replace the document. html and css are UTF-8; css is applied after the page's own styles (like a user
       stylesheet), so theme colours can be swapped without rebuilding the page. Either may be NULL. */
    void    SetDocument(HWND hwnd, const char* html, const char* css);
    /* Re-apply a new stylesheet to the current page (theme switch); keeps the scroll position. */
    void    SetStyleSheet(HWND hwnd, const char* css);
    /* Window background behind the page (body background in the CSS paints over it). */
    void    SetBackground(HWND hwnd, COLORREF bg);
    /* Selection colours (defaults: COLOR_HIGHLIGHT / COLOR_HIGHLIGHTTEXT). */
    void    SetSelectionColors(HWND hwnd, COLORREF back, COLORREF text);

    /* Link clicks: url as written in the page (UTF-16). */
    typedef void (*LinkHandler)(HWND hwnd, const wchar_t* url, void* ctx);
    void    SetLinkHandler(HWND hwnd, LinkHandler handler, void* ctx);

    /* Image loading for non-"res:" sources: return a 32-bit premultiplied top-down DIB section, or NULL.
       The view owns the bitmap afterwards. */
    typedef HBITMAP (*ImageLoader)(const wchar_t* src, void* ctx);
    void    SetImageLoader(HWND hwnd, ImageLoader loader, void* ctx);

    bool    HasSelection(HWND hwnd);
    void    SelectAll(HWND hwnd);
    void    ClearSelection(HWND hwnd);
    /* Copies the selection (or nothing) to the clipboard; returns false when nothing was selected. */
    bool    Copy(HWND hwnd);
    /* The selection as text (UTF-16); empty when nothing is selected. Caller frees with LocalFree. */
    wchar_t* GetSelectedText(HWND hwnd);

    /* Helper for image loaders: decode a PNG/BMP/JPEG file or memory block to a premultiplied DIB. */
    HBITMAP LoadImageFile(const wchar_t* path);
    HBITMAP LoadImageMemory(const void* data, size_t size);
}

#endif
