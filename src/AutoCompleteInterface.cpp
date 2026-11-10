/* Copyright (C) 2013 Webyog Inc

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

*/

/* bonSQL: the handlers drive bonSQL's own autocomplete (include/ngsa/NgsaAutoComplete.h). The Ultimate
   code (tag files, build thread) that stood behind them was never published; see the header. */

#include "AutoCompleteInterface.h"
#include "EditorBase.h"
#include "Scintilla.h"
#include "ngsa/NgsaAutoComplete.h"

AutoCompleteInterface::AutoCompleteInterface()
{
    m_autocomplete = NULL;
}

AutoCompleteInterface::~AutoCompleteInterface()
{
    if(m_autocomplete)
        delete m_autocomplete;

    m_autocomplete = NULL;
}

wyBool
AutoCompleteInterface::HandlerInitAutoComplete(MDIWindow * wnd)
{
    if(!m_autocomplete)
        m_autocomplete = new NgsaAutoComplete(wnd);

    return wyTrue;
}

void
AutoCompleteInterface::HandlerBuildTags(MDIWindow *wnd)
{
    if(!m_autocomplete)
        HandlerInitAutoComplete(wnd);

    m_autocomplete->OnConnected();
}

void
AutoCompleteInterface::HandlerStoreObjects(MDIWindow *wnd, wyBool rebuild)
{
}

void
AutoCompleteInterface::HandlerInvalidate()
{
    if(m_autocomplete)
        m_autocomplete->InvalidateAll();
}

wyInt32
AutoCompleteInterface::HandlerOnWMChar(HWND hwnd, EditorBase *eb, WPARAM wparam)
{
    if(m_autocomplete && m_autocomplete->OnChar(hwnd, wparam))
        return 1;

	 eb->SetAutoIndentation(hwnd, wparam);
	 return 0;
}

wyInt32
AutoCompleteInterface::HandlerOnWMKeyDown(HWND hwnd, EditorBase *eb, WPARAM wparam)
{
    if(m_autocomplete && m_autocomplete->OnKeyDown(hwnd, wparam))
        return 1;

    return 0;
}

wyInt32
AutoCompleteInterface::HandlerOnWMKeyUp(HWND hwnd, EditorBase *eb, WPARAM wparam)
{
    return 0;
}

wyBool
AutoCompleteInterface::HandlerAddToMessageQueue(MDIWindow*	wnd, wyChar *query)
{
    if(m_autocomplete && query)
        m_autocomplete->OnStatement(query);

    return wyFalse;
}

void
AutoCompleteInterface::HandlerProcessMessageQueue(MDIWindow *wnd)
{
}

wyBool
AutoCompleteInterface::HandlerShowToolTip(MDIWindow *wnd)
{
    return wyFalse;
}

void
AutoCompleteInterface::HandlerOnAutoThreadExit(MDIWindow *wnd, LPARAM lparam)
{
}

VOID
AutoCompleteInterface::UpdateTagBuildFlag()
{
}

wyBool
AutoCompleteInterface::OnACNotification(WPARAM wparam, LPARAM lparam)
{
    if(m_autocomplete && lparam && m_autocomplete->OnNotify((SCNotification*)lparam))
        return wyTrue;

    return wyFalse;
}
