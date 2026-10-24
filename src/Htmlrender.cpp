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

#include "Htmlrender.h"
#include "MDIWindow.h"
#include "HtmlView.h"
#include "ngsa/NgsaDark.h"

#define PROMPT_MESSAGE _(L"Can not find selected table in Object Browser.\r\n\r\nRemove Filter applied (if any) in Object Browser and Refresh Object Browser.")

BOOL
HandleClickEventOnLink(HWND hwndview, const wchar_t *url)
{
    if(!url)
        return FALSE;

    if(!wcsicmp(url, L"optimizer_help "))
        ShowHelp("help:schema-optimiser");
    else if(!wcsicmp(url, L"redundantindexes_help "))
        ShowHelp("help:redundant-index-analyzer");
    else if(!wcsicmp(url, L"pref_others "))
    {
        PreferenceBase *pref = CreatePreferences();

        if(pref)
        {
            if(pGlobals->m_entlicense.CompareI("Professional"))
                pref->Create(4);
            else
                pref->Create(3);
            delete pref;
        }
    }
    else if(!wcsnicmp(url, L"http://", 7) || !wcsnicmp(url, L"https://", 8))
        ::ShellExecuteW(hwndview, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    else
    {
        HandleClickTableURL(url);
        SetFocus(hwndview);
    }
    return TRUE;
}

void
HtmlViewLinkHandler(HWND hwnd, const wchar_t *url, void *)
{
    HandleClickEventOnLink(hwnd, url);
}

void
ApplyHtmlViewTheme(HWND hwnd)
{
    if(!hwnd)
        return;
    if(NgsaDark::IsOn())
    {
        HtmlView::SetBackground(hwnd, NgsaDark::kBase);
        HtmlView::SetSelectionColors(hwnd, NgsaDark::kSelection, RGB(0xFF, 0xFF, 0xFF));
    }
    else
    {
        HtmlView::SetBackground(hwnd, GetSysColor(COLOR_WINDOW));
        HtmlView::SetSelectionColors(hwnd, GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT));
    }
    if(GetPropW(hwnd, L"bonSQLInfoView"))
        HtmlView::SetStyleSheet(hwnd, GetInfoTabCSS());
}

//this function is called whenever the table link is clicked from the DB level
void HandleClickTableURL(const wchar_t *url)
{
    MDIWindow   *wnd = NULL;
    HTREEITEM   hitem = NULL;
    wyWChar     buff[MAX_PATH] = {0};
    TVITEM      tvitem;

    wnd = GetActiveWin();

    if(!wnd || !wnd->m_pcqueryobject)
    {
		return;
    }

    //switch the current selection image
    switch(wnd->m_pcqueryobject->GetSelectionImage())
    {
        case NDATABASE:
            //get the selection
            hitem = TreeView_GetSelection(wnd->m_pcqueryobject->m_hwnd);

            if(hitem)
            {
                //expand and get the first child
                TreeView_Expand(wnd->m_pcqueryobject->m_hwnd, hitem, TVE_EXPAND);
                hitem = TreeView_GetChild(wnd->m_pcqueryobject->m_hwnd, hitem);
            }
            break;

        case NTABLES:
            //get the selection
            hitem = TreeView_GetSelection(wnd->m_pcqueryobject->m_hwnd);
    }

    if(!hitem)
    {
        return;
    }

    //expand the Tables folder
    TreeView_Expand(wnd->m_pcqueryobject->m_hwnd, hitem, TVE_EXPAND);
    
    //traverse through the tables
    for(hitem = TreeView_GetChild(wnd->m_pcqueryobject->m_hwnd, hitem); hitem; 
        hitem = TreeView_GetNextSibling(wnd->m_pcqueryobject->m_hwnd, hitem))
    {
        //get the item
        tvitem.hItem = hitem;
        tvitem.mask = TVIF_TEXT;
        buff[0] = 0;
        tvitem.pszText = buff;
        tvitem.cchTextMax = MAX_PATH - 1;
        TreeView_GetItem(wnd->m_pcqueryobject->m_hwnd, &tvitem);

        //compare the item with the url
        if(!wcscmp(url, tvitem.pszText))
        {
            //set the selection; this will generate  TVN_SELCHANGING and TVN_SELCHANGED messages
            TreeView_SelectItem(wnd->m_pcqueryobject->m_hwnd, hitem);
            break;
        }
    }

    if(!hitem)
    {
        MDIWindow *wnd = GetActiveWin();
        yog_message(wnd->m_hwnd, PROMPT_MESSAGE, pGlobals->m_appname.GetAsWideChar(), MB_OK | MB_ICONINFORMATION);
    }

    return;
}

//the function checks whether the given point is inside the bounding rectangle of the element identified by id
