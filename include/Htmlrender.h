/* Copyright (C) 2013 Webyog Inc
   Modified for bonSQL by André Walsh, 2024-2026; the changes are in the repository history.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA

   Changed 13/10/2026 for bonSQL: HTMLayout (closed source) replaced by HtmlView (litehtml); see
   docs/ngsa/HTMLVIEW.md.
*/

/* Link handling for the HTML views (HtmlView). The HTMLayout event plumbing that lived here (DOM event
   handlers, resource loading, form buttons for Ultimate's Form view, Schema Optimizer and Data Search) is
   gone with HTMLayout. */
#ifndef _htmlrender_
#define _htmlrender_

#include "Global.h"
#include "GUIHelper.h"

extern  PGLOBALS        pGlobals;

/* a link in an HTML view was clicked: help pages, Preferences, table links on the database page */
BOOL    HandleClickEventOnLink(HWND hwndview, const wchar_t *url);
/* a table link on the database page: select that table in the object browser */
void    HandleClickTableURL(const wchar_t *url);
/* HtmlView::LinkHandler that forwards to HandleClickEventOnLink */
void    HtmlViewLinkHandler(HWND hwnd, const wchar_t *url, void *ctx);
/* colours (and, for the Info tab, its stylesheet) of an HtmlView for the active theme; the Info tab's
   view carries the window property "bonSQLInfoView" */
void    ApplyHtmlViewTheme(HWND hwnd);
/* the Info tab's stylesheet for the active theme (ObjectInfo.cpp) */
const char* GetInfoTabCSS();

#endif
