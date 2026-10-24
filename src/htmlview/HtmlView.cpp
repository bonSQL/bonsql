/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * bonSQL HtmlView: see HtmlView.h. Compile this file (and third_party/litehtml) as C++17.
 * Drawing is upstream's GDI container (third_party/litehtml/containers/windows/win32, BSD-3-Clause);
 * this file adds the window, scrolling, links, images, borders and text selection.
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "win32_container.h"
#include "HtmlView.h"
#include <windowsx.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

#ifndef WM_MOUSEHWHEEL          // hidden by the SDK when the host builds for _WIN32_WINNT 0x501 (SQLyog does)
#define WM_MOUSEHWHEEL 0x020E
#endif

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace litehtml;

namespace
{
    std::wstring Wide(const char* s)
    {
        if(!s || !*s) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if(n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
        return w;
    }

    /* one piece of text as litehtml drew it: words and spaces arrive separately, in document order */
    struct Run
    {
        std::wstring    text;
        HFONT           font;
        int             x, y, w, h;
        const void*     cell = nullptr;     /* the td/th it sits in, if any */
        const void*     row = nullptr;      /* that cell's tr */
        int             col = 0;            /* the cell's index in its row */
        bool            space = false;
    };

    struct Mark
    {
        int run = -1, ch = 0;
        bool operator<(const Mark& o) const { return run < o.run || (run == o.run && ch < o.ch); }
        bool operator==(const Mark& o) const { return run == o.run && ch == o.ch; }
        bool valid() const { return run >= 0; }
    };

    struct View;

    class Container : public win32_container
    {
    public:
        View* m_view;
        explicit Container(View* v) : m_view(v) {}

        void            draw_text(uint_ptr hdc, const char* text, uint_ptr hFont, web_color color, const position& pos) override;
        void            draw_borders(uint_ptr hdc, const borders& b, const position& pos, bool root) override;
        void            set_caption(const char*) override {}
        void            set_base_url(const char*) override {}
        void            on_anchor_click(const char* url, const element::ptr&) override;
        void            on_mouse_event(const element::ptr&, litehtml::mouse_event) override {}
        void            set_cursor(const char* cursor) override;
        void            import_css(string&, const string&, string&) override {}
        void            get_viewport(position& viewport) const override;

    protected:
        void            make_url(LPCWSTR url, LPCWSTR, std::wstring& out) override { out = url; }
        uint_ptr        get_image(LPCWSTR url_or_path, bool) override;
        void            free_image(uint_ptr img) override { if(img) DeleteObject((HBITMAP)img); }
        void            get_img_size(uint_ptr img, litehtml::size& sz) override;
        void            draw_img_bg(HDC hdc, uint_ptr img, const background_layer& layer) override;
        void            draw_ellipse(HDC hdc, pixel_t x, pixel_t y, pixel_t w, pixel_t h, web_color c, int lw) override;
        void            fill_ellipse(HDC hdc, pixel_t x, pixel_t y, pixel_t w, pixel_t h, web_color c) override;
        void            fill_rect(HDC hdc, pixel_t x, pixel_t y, pixel_t w, pixel_t h, web_color c) override;
    };

    struct View
    {
        HWND                        hwnd = nullptr;
        std::unique_ptr<Container>  cont;
        document::ptr               doc;
        std::string                 html, css;
        int                         sx = 0, sy = 0;         /* scroll position */
        int                         docw = 0, doch = 0;
        COLORREF                    bg = RGB(255, 255, 255);
        COLORREF                    selbg = GetSysColor(COLOR_HIGHLIGHT), seltext = GetSysColor(COLOR_HIGHLIGHTTEXT);
        HtmlView::LinkHandler       onlink = nullptr;  void* linkctx = nullptr;
        HtmlView::ImageLoader       loader = nullptr;  void* loaderctx = nullptr;
        std::string                 cursor;
        std::wstring                pendinglink;
        bool                        collecting = false;
        std::vector<Run>            runs;
        Mark                        anchor, active;         /* selection: anchor where it started, active where it is */
        bool                        dragging = false, moved = false;
        POINT                       downpt = { 0, 0 };
        HDC                         measure = nullptr;      /* text measurement for hit tests */
    };

    View* Get(HWND hwnd) { return (View*)GetWindowLongPtrW(hwnd, GWLP_USERDATA); }

    /* ---------------- container ---------------- */

    void Container::draw_text(uint_ptr hdc, const char* text, uint_ptr hFont, web_color color, const position& pos)
    {
        if(m_view->collecting)
        {
            Run r; r.text = Wide(text); r.font = (HFONT)hFont;
            r.x = (int)pos.x; r.y = (int)pos.y; r.w = (int)pos.width; r.h = (int)pos.height;
            if(!r.text.empty()) m_view->runs.push_back(std::move(r));
            return;
        }
        win32_container::draw_text(hdc, text, hFont, color, pos);
    }

    void Container::draw_borders(uint_ptr hdc, const borders& b, const position& p, bool)
    {
        if(m_view->collecting) return;
        HDC dc = (HDC)hdc;
        apply_clip(dc);
        auto side = [&](const border& e, pixel_t x, pixel_t y, pixel_t w, pixel_t h)
        {
            if(e.width <= 0 || e.style == border_style_none || e.style == border_style_hidden || e.color.alpha == 0) return;
            fill_rect(dc, x, y, w, h, e.color);
        };
        side(b.top, p.x, p.y, p.width, b.top.width);
        side(b.bottom, p.x, p.bottom() - b.bottom.width, p.width, b.bottom.width);
        side(b.left, p.x, p.y, b.left.width, p.height);
        side(b.right, p.right() - b.right.width, p.y, b.right.width, p.height);
        release_clip(dc);
    }

    void Container::on_anchor_click(const char* url, const element::ptr&) { m_view->pendinglink = Wide(url); }
    void Container::set_cursor(const char* cursor) { m_view->cursor = cursor ? cursor : ""; }

    void Container::get_viewport(position& v) const
    {
        RECT rc = { 0, 0, 0, 0 };
        if(m_view->hwnd) GetClientRect(m_view->hwnd, &rc);
        v.x = (pixel_t)m_view->sx; v.y = (pixel_t)m_view->sy; v.width = (pixel_t)rc.right; v.height = (pixel_t)rc.bottom;
    }

    uint_ptr Container::get_image(LPCWSTR url, bool)
    {
        std::wstring u(url ? url : L"");
        if(_wcsnicmp(u.c_str(), L"res:", 4) == 0)
        {
            std::wstring name = u.substr(4);
            size_t dot = name.rfind(L'.'); if(dot != std::wstring::npos) name.resize(dot);
            for(auto& c : name) c = (wchar_t)towupper(c);
            HMODULE mod = GetModuleHandleW(nullptr);
            HRSRC r = FindResourceW(mod, name.c_str(), L"PNG");
            if(r)
            {
                HGLOBAL g = LoadResource(mod, r);
                if(g) return (uint_ptr)HtmlView::LoadImageMemory(LockResource(g), SizeofResource(mod, r));
            }
            if(m_view->loader) return (uint_ptr)m_view->loader(url, m_view->loaderctx);
            return 0;
        }
        if(m_view->loader) return (uint_ptr)m_view->loader(url, m_view->loaderctx);
        return (uint_ptr)HtmlView::LoadImageFile(url);
    }

    void Container::get_img_size(uint_ptr img, litehtml::size& sz)
    {
        BITMAP bm = {};
        if(img && GetObjectW((HBITMAP)img, sizeof(bm), &bm)) { sz.width = (pixel_t)bm.bmWidth; sz.height = (pixel_t)abs(bm.bmHeight); }
        else { sz.width = 0; sz.height = 0; }
    }

    void Container::draw_img_bg(HDC hdc, uint_ptr img, const background_layer& layer)
    {
        if(m_view->collecting || !img) return;
        BITMAP bm = {}; GetObjectW((HBITMAP)img, sizeof(bm), &bm);
        HDC mem = CreateCompatibleDC(hdc); HGDIOBJ old = SelectObject(mem, (HBITMAP)img);
        int sv = SaveDC(hdc);
        IntersectClipRect(hdc, (int)layer.clip_box.left(), (int)layer.clip_box.top(), (int)layer.clip_box.right(), (int)layer.clip_box.bottom());
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        int w = (int)layer.origin_box.width, h = (int)layer.origin_box.height;
        if(w <= 0) w = bm.bmWidth; if(h <= 0) h = abs(bm.bmHeight);
        AlphaBlend(hdc, (int)layer.origin_box.x, (int)layer.origin_box.y, w, h, mem, 0, 0, bm.bmWidth, abs(bm.bmHeight), bf);
        RestoreDC(hdc, sv);
        SelectObject(mem, old); DeleteDC(mem);
    }

    void Container::draw_ellipse(HDC hdc, pixel_t x, pixel_t y, pixel_t w, pixel_t h, web_color c, int lw)
    {
        if(m_view->collecting) return;
        HPEN pen = CreatePen(PS_SOLID, lw, RGB(c.red, c.green, c.blue));
        HGDIOBJ op = SelectObject(hdc, pen), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, (int)x, (int)y, (int)(x + w), (int)(y + h));
        SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(pen);
    }

    void Container::fill_ellipse(HDC hdc, pixel_t x, pixel_t y, pixel_t w, pixel_t h, web_color c)
    {
        if(m_view->collecting) return;
        HBRUSH br = CreateSolidBrush(RGB(c.red, c.green, c.blue));
        HGDIOBJ op = SelectObject(hdc, GetStockObject(NULL_PEN)), ob = SelectObject(hdc, br);
        Ellipse(hdc, (int)x, (int)y, (int)(x + w), (int)(y + h));
        SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(br);
    }

    void Container::fill_rect(HDC hdc, pixel_t x, pixel_t y, pixel_t w, pixel_t h, web_color c)
    {
        if(m_view->collecting || c.alpha == 0) return;
        RECT r = { (LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h) };
        HBRUSH br = CreateSolidBrush(RGB(c.red, c.green, c.blue));
        FillRect(hdc, &r, br);
        DeleteObject(br);
    }

    /* ---------------- layout, scrolling ---------------- */

    /* the text in document order, from the element tree: words and spaces are separate text elements, each
       with its box, font and place in the table structure (a cell change in a row is a tab when copying) */
    void Walk(View* v, const element::ptr& el, const element* cell, const element* row, int col)
    {
        if(!el) return;
        if(el->is_text())
        {
            string t; el->get_text(t);
            position p = el->get_placement();
            if(t.empty() || (p.width <= 0 && p.height <= 0)) return;
            Run r; r.text = Wide(t.c_str()); r.font = (HFONT)el->css().get_font();
            r.x = (int)p.x; r.y = (int)p.y; r.w = (int)p.width; r.h = (int)p.height;
            r.cell = cell; r.row = row; r.col = col; r.space = el->is_white_space() || el->is_space();
            v->runs.push_back(std::move(r));
            return;
        }
        const char* tag = el->get_tagName();
        if(tag && (!strcmp(tag, "td") || !strcmp(tag, "th")))
        {
            cell = el.get(); row = nullptr; col = 0;
            element::ptr tr = el->parent();
            if(tr && tr->get_tagName() && !strcmp(tr->get_tagName(), "tr"))
            {
                row = tr.get();
                for(const auto& c : tr->children())
                {
                    if(c == el) break;
                    const char* ct = c->get_tagName();
                    if(ct && (!strcmp(ct, "td") || !strcmp(ct, "th"))) col++;
                }
            }
        }
        for(const auto& c : el->children()) Walk(v, c, cell, row, col);
    }

    void CollectRuns(View* v)
    {
        v->runs.clear();
        if(v->doc) Walk(v, v->doc->root(), nullptr, nullptr, 0);
        wchar_t dump[MAX_PATH];
        if(GetEnvironmentVariableW(L"HTMLVIEW_DUMP_RUNS", dump, MAX_PATH))     /* diagnostics: the text runs as collected */
        {
            FILE* f = nullptr;
            if(_wfopen_s(&f, dump, L"w, ccs=UTF-8") == 0 && f)
            {
                for(size_t i = 0; i < v->runs.size(); i++)
                {
                    const Run& r = v->runs[i];
                    fwprintf(f, L"%zu\t%d,%d %dx%d\tcell %p col %d%s\t[%s]\n", i, r.x, r.y, r.w, r.h, r.cell, r.col, r.space ? L" space" : L"", r.text.c_str());
                }
                fclose(f);
            }
        }
    }

    void UpdateScroll(View* v)
    {
        RECT rc; GetClientRect(v->hwnd, &rc);
        v->sy = std::max(0, std::min(v->sy, v->doch - (int)rc.bottom));
        v->sx = std::max(0, std::min(v->sx, v->docw - (int)rc.right));
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
        si.nMin = 0; si.nMax = std::max(v->doch - 1, 0); si.nPage = rc.bottom; si.nPos = v->sy;
        SetScrollInfo(v->hwnd, SB_VERT, &si, TRUE);
        si.nMax = std::max(v->docw - 1, 0); si.nPage = rc.right; si.nPos = v->sx;
        SetScrollInfo(v->hwnd, SB_HORZ, &si, TRUE);
    }

    void Layout(View* v)
    {
        if(!v->doc) { v->docw = v->doch = 0; v->runs.clear(); UpdateScroll(v); return; }
        RECT rc; GetClientRect(v->hwnd, &rc);
        int width = std::max((int)rc.right, 50);
        v->doc->render((pixel_t)width);
        v->docw = (int)v->doc->width(); v->doch = (int)v->doc->height();
        CollectRuns(v);
        UpdateScroll(v);
        /* the bars may have changed the client width: lay out once more if so */
        RECT rc2; GetClientRect(v->hwnd, &rc2);
        if(rc2.right != rc.right && rc2.right > 0)
        {
            v->doc->render((pixel_t)std::max((int)rc2.right, 50));
            v->docw = (int)v->doc->width(); v->doch = (int)v->doc->height();
            CollectRuns(v);
            UpdateScroll(v);
        }
    }

    void Build(View* v, bool keepscroll)
    {
        int sx = v->sx, sy = v->sy;
        v->anchor = v->active = Mark();
        v->doc.reset();
        if(!v->cont) v->cont.reset(new Container(v));
        if(!v->html.empty())
            v->doc = document::createFromString(v->html, v->cont.get(), litehtml::master_css, v->css);
        if(keepscroll) { v->sx = sx; v->sy = sy; } else { v->sx = v->sy = 0; }
        Layout(v);
        InvalidateRect(v->hwnd, nullptr, FALSE);
    }

    void ScrollTo(View* v, int x, int y)
    {
        RECT rc; GetClientRect(v->hwnd, &rc);
        int nx = std::max(0, std::min(x, v->docw - (int)rc.right)), ny = std::max(0, std::min(y, v->doch - (int)rc.bottom));
        if(nx == v->sx && ny == v->sy) return;
        v->sx = nx; v->sy = ny;
        UpdateScroll(v);
        InvalidateRect(v->hwnd, nullptr, FALSE);
    }

    /* ---------------- selection ---------------- */

    int Extent(View* v, const Run& r, int chars)
    {
        if(chars <= 0) return 0;
        HGDIOBJ old = SelectObject(v->measure, r.font);
        SIZE sz = {}; GetTextExtentPoint32W(v->measure, r.text.c_str(), chars, &sz);
        SelectObject(v->measure, old);
        return sz.cx;
    }

    int CharAt(View* v, const Run& r, int dx)
    {
        int n = (int)r.text.size(), prev = 0;
        for(int k = 1; k <= n; k++)
        {
            int e = Extent(v, r, k);
            if(dx < (prev + e) / 2) return k - 1;
            prev = e;
        }
        return n;
    }

    /* document point -> position in the text */
    Mark HitTest(View* v, int x, int y)
    {
        Mark m;
        if(v->runs.empty()) return m;
        /* a word under the point wins over a whitespace box that overlaps it */
        for(int i = 0; i < (int)v->runs.size(); i++)
        {
            const Run& r = v->runs[i];
            if(!r.space && y >= r.y && y < r.y + r.h && x >= r.x && x <= r.x + r.w) { m.run = i; m.ch = CharAt(v, r, x - r.x); return m; }
        }
        int best = -1; long bestd = LONG_MAX;
        for(int i = 0; i < (int)v->runs.size(); i++)
        {
            const Run& r = v->runs[i];
            bool online = y >= r.y && y < r.y + r.h;
            if(online && x >= r.x && x <= r.x + r.w) { m.run = i; m.ch = CharAt(v, r, x - r.x); return m; }
            long dy = online ? 0 : (y < r.y ? r.y - y : y - (r.y + r.h) + 1);
            long dx = x < r.x ? r.x - x : (x > r.x + r.w ? x - (r.x + r.w) : 0);
            long d = dy * 100000 + dx;
            if(d < bestd) { bestd = d; best = i; }
        }
        const Run& r = v->runs[best];
        m.run = best;
        bool online = y >= r.y && y < r.y + r.h;
        if(online) m.ch = x < r.x ? 0 : (int)r.text.size();
        else m.ch = y < r.y ? 0 : (int)r.text.size();
        return m;
    }

    bool Ordered(View* v, Mark& a, Mark& b)
    {
        if(!v->anchor.valid() || !v->active.valid() || v->anchor == v->active) return false;
        a = std::min(v->anchor, v->active); b = std::max(v->anchor, v->active);
        return true;
    }

    bool IsSpace(const std::wstring& s) { for(wchar_t c : s) if(!iswspace(c) && c != 0xA0) return false; return true; }

    std::wstring SelectionText(View* v)
    {
        Mark a, b; std::wstring out;
        if(!Ordered(v, a, b)) return out;
        const Run* prev = nullptr;
        for(int i = a.run; i <= b.run; i++)
        {
            const Run& r = v->runs[i];
            int from = i == a.run ? a.ch : 0, to = i == b.run ? b.ch : (int)r.text.size();
            if(from >= to) continue;
            if(prev)
            {
                if(r.row && prev->row && r.row == prev->row && r.cell != prev->cell)
                    out.append(std::max(1, r.col - prev->col), L'\t');            /* next cell(s) in the row */
                else if(r.row && r.row != prev->row)
                {
                    out += L"\r\n";                                               /* next table row */
                    out.append(r.col, L'\t');                                     /* keep the columns aligned */
                }
                else if(r.y >= prev->y + prev->h - 1 || r.x + 1 < prev->x)
                    out += L"\r\n";                                               /* next line of text */
            }
            std::wstring piece = r.text.substr(from, to - from);
            for(auto& c : piece) if(c == 0xA0) c = L' ';
            out += piece;
            prev = &r;
        }
        return out;
    }

    void PaintSelection(View* v, HDC dc)
    {
        Mark a, b;
        if(!Ordered(v, a, b)) return;
        HBRUSH br = CreateSolidBrush(v->selbg);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, v->seltext);
        UINT oldalign = SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
        for(int i = a.run; i <= b.run; i++)
        {
            const Run& r = v->runs[i];
            int from = i == a.run ? a.ch : 0, to = i == b.run ? b.ch : (int)r.text.size();
            if(from >= to) continue;
            int x1 = r.x + Extent(v, r, from), x2 = r.x + Extent(v, r, to);
            RECT rc = { x1 - v->sx, r.y - v->sy, x2 - v->sx, r.y + r.h - v->sy };
            FillRect(dc, &rc, br);
            HGDIOBJ of = SelectObject(dc, r.font);
            TextOutW(dc, x1 - v->sx, r.y + r.h - v->sy, r.text.c_str() + from, to - from);
            SelectObject(dc, of);
        }
        SetTextAlign(dc, oldalign);
        DeleteObject(br);
    }

    /* ---------------- window ---------------- */

    void Paint(View* v)
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(v->hwnd, &ps);
        RECT rc; GetClientRect(v->hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max((int)rc.right, 1), std::max((int)rc.bottom, 1));
        HGDIOBJ ob = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(v->bg); FillRect(mem, &rc, bg); DeleteObject(bg);
        if(v->doc)
        {
            position clip((pixel_t)ps.rcPaint.left, (pixel_t)ps.rcPaint.top, (pixel_t)(ps.rcPaint.right - ps.rcPaint.left), (pixel_t)(ps.rcPaint.bottom - ps.rcPaint.top));
            v->doc->draw((uint_ptr)mem, (pixel_t)-v->sx, (pixel_t)-v->sy, &clip);
            PaintSelection(v, mem);
        }
        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(v->hwnd, &ps);
    }

    void OnScroll(View* v, int bar, WPARAM wp)
    {
        RECT rc; GetClientRect(v->hwnd, &rc);
        int page = bar == SB_VERT ? rc.bottom : rc.right, line = 20, pos = bar == SB_VERT ? v->sy : v->sx;
        SCROLLINFO si = { sizeof(si), SIF_TRACKPOS }; GetScrollInfo(v->hwnd, bar, &si);
        switch(LOWORD(wp))
        {
        case SB_LINEUP: pos -= line; break;
        case SB_LINEDOWN: pos += line; break;
        case SB_PAGEUP: pos -= page; break;
        case SB_PAGEDOWN: pos += page; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = INT_MAX / 2; break;
        default: return;
        }
        if(bar == SB_VERT) ScrollTo(v, v->sx, pos); else ScrollTo(v, pos, v->sy);
    }

    void ContextMenu(View* v, int x, int y)
    {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING | (HtmlView::HasSelection(v->hwnd) ? 0 : MF_GRAYED), 1, L"&Copy\tCtrl+C");
        AppendMenuW(m, MF_STRING, 2, L"Select &All\tCtrl+A");
        if(x == -1 && y == -1) { POINT p = { 10, 10 }; ClientToScreen(v->hwnd, &p); x = p.x; y = p.y; }
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, v->hwnd, nullptr);
        DestroyMenu(m);
        if(cmd == 1) HtmlView::Copy(v->hwnd);
        else if(cmd == 2) HtmlView::SelectAll(v->hwnd);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        View* v = Get(hwnd);
        switch(msg)
        {
        case WM_NCCREATE:
            v = new View(); v->hwnd = hwnd; v->measure = CreateCompatibleDC(nullptr);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
            break;
        case WM_NCDESTROY:
            if(v) { v->doc.reset(); v->cont.reset(); DeleteDC(v->measure); delete v; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); }
            break;
        case WM_SIZE:
            if(v && LOWORD(lp) > 0) { Layout(v); InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if(v) { Paint(v); return 0; }
            break;
        case WM_VSCROLL: if(v) OnScroll(v, SB_VERT, wp); return 0;
        case WM_HSCROLL: if(v) OnScroll(v, SB_HORZ, wp); return 0;
        case WM_MOUSEWHEEL:
            if(v) { UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0); ScrollTo(v, v->sx, v->sy - GET_WHEEL_DELTA_WPARAM(wp) * (int)lines * 20 / WHEEL_DELTA); }
            return 0;
        case WM_MOUSEHWHEEL:
            if(v) ScrollTo(v, v->sx + GET_WHEEL_DELTA_WPARAM(wp) * 40 / WHEEL_DELTA, v->sy);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN:
            if(v)
            {
                bool ctrl = GetKeyState(VK_CONTROL) < 0;
                RECT rc; GetClientRect(hwnd, &rc);
                switch(wp)
                {
                case 'A': if(ctrl) { HtmlView::SelectAll(hwnd); return 0; } break;
                case 'C': case VK_INSERT: if(ctrl) { HtmlView::Copy(hwnd); return 0; } break;
                case VK_UP: ScrollTo(v, v->sx, v->sy - 20); return 0;
                case VK_DOWN: ScrollTo(v, v->sx, v->sy + 20); return 0;
                case VK_LEFT: ScrollTo(v, v->sx - 20, v->sy); return 0;
                case VK_RIGHT: ScrollTo(v, v->sx + 20, v->sy); return 0;
                case VK_PRIOR: ScrollTo(v, v->sx, v->sy - rc.bottom); return 0;
                case VK_NEXT: ScrollTo(v, v->sx, v->sy + rc.bottom); return 0;
                case VK_HOME: ScrollTo(v, 0, ctrl ? 0 : v->sy); return 0;
                case VK_END: if(ctrl) ScrollTo(v, v->sx, INT_MAX / 2); return 0;
                }
            }
            break;
        case WM_COPY:
            if(v) HtmlView::Copy(hwnd);
            return 0;
        case WM_LBUTTONDOWN:
            if(v)
            {
                SetFocus(hwnd); SetCapture(hwnd);
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                v->downpt = { x, y }; v->dragging = true; v->moved = false;
                Mark m = HitTest(v, x + v->sx, y + v->sy);
                if(GetKeyState(VK_SHIFT) < 0 && v->anchor.valid()) v->active = m;
                else { v->anchor = v->active = m; }
                if(v->doc) { position::vector boxes; v->doc->on_lbutton_down((pixel_t)(x + v->sx), (pixel_t)(y + v->sy), (pixel_t)x, (pixel_t)y, boxes); }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if(v)
            {
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                if(v->doc) { position::vector boxes; if(v->doc->on_mouse_over((pixel_t)(x + v->sx), (pixel_t)(y + v->sy), (pixel_t)x, (pixel_t)y, boxes)) InvalidateRect(hwnd, nullptr, FALSE); }
                if(v->dragging)
                {
                    if(abs(x - v->downpt.x) > 3 || abs(y - v->downpt.y) > 3) v->moved = true;
                    if(v->moved)
                    {
                        RECT rc; GetClientRect(hwnd, &rc);
                        if(y < 0) ScrollTo(v, v->sx, v->sy - 20); else if(y > rc.bottom) ScrollTo(v, v->sx, v->sy + 20);
                        v->active = HitTest(v, x + v->sx, y + v->sy);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if(v && v->dragging)
            {
                ReleaseCapture();
                v->dragging = false;
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                position::vector boxes;
                if(!v->moved && GetKeyState(VK_SHIFT) >= 0)
                {
                    v->anchor = v->active = Mark();
                    v->pendinglink.clear();
                    if(v->doc) v->doc->on_lbutton_up((pixel_t)(x + v->sx), (pixel_t)(y + v->sy), (pixel_t)x, (pixel_t)y, boxes);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    if(!v->pendinglink.empty() && v->onlink) { std::wstring url = v->pendinglink; v->pendinglink.clear(); v->onlink(hwnd, url.c_str(), v->linkctx); }
                }
                else if(v->doc) v->doc->on_button_cancel(boxes);
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            if(v)
            {
                Mark m = HitTest(v, GET_X_LPARAM(lp) + v->sx, GET_Y_LPARAM(lp) + v->sy);
                if(m.valid() && !IsSpace(v->runs[m.run].text)) { v->anchor = { m.run, 0 }; v->active = { m.run, (int)v->runs[m.run].text.size() }; InvalidateRect(hwnd, nullptr, FALSE); }
            }
            return 0;
        case WM_SETCURSOR:
            if(v && LOWORD(lp) == HTCLIENT)
            {
                if(v->cursor == "pointer") { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
                POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
                Mark m = HitTest(v, p.x + v->sx, p.y + v->sy);
                bool overtext = false;
                if(m.valid()) { const Run& r = v->runs[m.run]; overtext = p.x + v->sx >= r.x && p.x + v->sx <= r.x + r.w && p.y + v->sy >= r.y && p.y + v->sy < r.y + r.h; }
                SetCursor(LoadCursor(nullptr, overtext ? IDC_IBEAM : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_CONTEXTMENU:
            if(v) { ContextMenu(v, (short)LOWORD(lp), (short)HIWORD(lp)); return 0; }
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    IWICImagingFactory* Wic()
    {
        static IWICImagingFactory* f = nullptr;
        if(!f)
        {
            HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
            if(hr == CO_E_NOTINITIALIZED) { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)); }
        }
        return f;
    }

    HBITMAP FromDecoder(IWICBitmapDecoder* dec)
    {
        HBITMAP result = nullptr;
        IWICBitmapFrameDecode* frame = nullptr; IWICBitmapSource* conv = nullptr;
        if(SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame, &conv)))
        {
            UINT w = 0, h = 0; conv->GetSize(&w, &h);
            BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = (LONG)w; bi.bmiHeader.biHeight = -(LONG)h;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if(bmp && SUCCEEDED(conv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits))) result = bmp;
            else if(bmp) DeleteObject(bmp);
        }
        if(conv) conv->Release();
        if(frame) frame->Release();
        return result;
    }
}

namespace HtmlView
{
    const wchar_t* const ClassName = L"bonSQLHtmlView";

    bool Register(HINSTANCE hinst)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        if(GetClassInfoExW(hinst, ClassName, &wc)) return true;
        wc.cbSize = sizeof(wc); wc.style = CS_DBLCLKS; wc.lpfnWndProc = WndProc; wc.hInstance = hinst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = ClassName;
        return RegisterClassExW(&wc) != 0;
    }

    HWND Create(HWND parent, int id, DWORD style)
    {
        HINSTANCE hinst = GetModuleHandleW(nullptr);
        Register(hinst);
        return CreateWindowExW(0, ClassName, L"", style, 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, hinst, nullptr);
    }

    void SetDocument(HWND hwnd, const char* html, const char* css)
    {
        View* v = Get(hwnd); if(!v) return;
        v->html = html ? html : ""; v->css = css ? css : "";
        Build(v, false);
    }

    void SetStyleSheet(HWND hwnd, const char* css)
    {
        View* v = Get(hwnd); if(!v) return;
        v->css = css ? css : "";
        Build(v, true);
    }

    void SetBackground(HWND hwnd, COLORREF bg) { View* v = Get(hwnd); if(v) { v->bg = bg; InvalidateRect(hwnd, nullptr, FALSE); } }
    void SetSelectionColors(HWND hwnd, COLORREF back, COLORREF text) { View* v = Get(hwnd); if(v) { v->selbg = back; v->seltext = text; InvalidateRect(hwnd, nullptr, FALSE); } }
    void SetLinkHandler(HWND hwnd, LinkHandler h, void* ctx) { View* v = Get(hwnd); if(v) { v->onlink = h; v->linkctx = ctx; } }
    void SetImageLoader(HWND hwnd, ImageLoader l, void* ctx) { View* v = Get(hwnd); if(v) { v->loader = l; v->loaderctx = ctx; } }

    bool HasSelection(HWND hwnd) { View* v = Get(hwnd); Mark a, b; return v && Ordered(v, a, b); }

    void SelectAll(HWND hwnd)
    {
        View* v = Get(hwnd); if(!v || v->runs.empty()) return;
        v->anchor = { 0, 0 }; v->active = { (int)v->runs.size() - 1, (int)v->runs.back().text.size() };
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void ClearSelection(HWND hwnd) { View* v = Get(hwnd); if(v) { v->anchor = v->active = Mark(); InvalidateRect(hwnd, nullptr, FALSE); } }

    wchar_t* GetSelectedText(HWND hwnd)
    {
        View* v = Get(hwnd);
        std::wstring s = v ? SelectionText(v) : std::wstring();
        wchar_t* out = (wchar_t*)LocalAlloc(LMEM_FIXED, (s.size() + 1) * sizeof(wchar_t));
        if(out) memcpy(out, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        return out;
    }

    bool Copy(HWND hwnd)
    {
        View* v = Get(hwnd); if(!v) return false;
        std::wstring s = SelectionText(v);
        if(s.empty()) return false;
        bool open = false;
        for(int i = 0; i < 20 && !(open = OpenClipboard(hwnd) != FALSE); i++) Sleep(10);     /* another process may hold it briefly */
        if(!open) return false;
        EmptyClipboard();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
        if(g) { memcpy(GlobalLock(g), s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
        CloseClipboard();
        return g != nullptr;
    }

    HBITMAP LoadImageFile(const wchar_t* path)
    {
        IWICImagingFactory* f = Wic(); if(!f || !path) return nullptr;
        IWICBitmapDecoder* dec = nullptr; HBITMAP r = nullptr;
        if(SUCCEEDED(f->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) { r = FromDecoder(dec); dec->Release(); }
        return r;
    }

    HBITMAP LoadImageMemory(const void* data, size_t size)
    {
        IWICImagingFactory* f = Wic(); if(!f || !data || !size) return nullptr;
        IWICStream* st = nullptr; IWICBitmapDecoder* dec = nullptr; HBITMAP r = nullptr;
        if(SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)data, (DWORD)size)) &&
           SUCCEEDED(f->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) { r = FromDecoder(dec); }
        if(dec) dec->Release();
        if(st) st->Release();
        return r;
    }
}
