/* Copyright (C) 2013 Webyog Inc.

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

/* bonSQL: the Ultimate implementation behind this interface was never published; since 2026.11 the
   handlers drive bonSQL's own autocomplete (include/ngsa/NgsaAutoComplete.h).
   The names stay so that the call sites in the editor, the window
   and the query helpers are untouched. */

#ifndef _AUTO_COMPLETE_INTERFACE_H_
#define _AUTO_COMPLETE_INTERFACE_H_

#include "MDIWindow.h"
#include "DataType.h"

class NgsaAutoComplete;

class AutoCompleteInterface
{
public:
    AutoCompleteInterface();
    ~AutoCompleteInterface();

    /// Creates the autocomplete object for this connection window (from MDIWindow::OnWmCreate)
    wyBool              HandlerInitAutoComplete(MDIWindow * wnd);

    /// Kept for the old call sites: the catalog is built lazily, nothing to store
    void	            HandlerStoreObjects(MDIWindow *wnd, wyBool brebuild);

    /// After a successful connect: a fresh catalog and the keywords of this engine
    void	            HandlerBuildTags(MDIWindow *wnd);

    /// The object browser was refreshed: forget the catalog
    void                HandlerInvalidate();

    /// WM_CHAR before Scintilla; 1 = consumed. Otherwise keeps the editor's auto-indentation.
    wyInt32             HandlerOnWMChar(HWND hwnd, EditorBase *eb, WPARAM wparam);

    /// WM_KEYDOWN before Scintilla; 1 = consumed (Ctrl+Space, Ctrl+Shift+Space)
    wyInt32             HandlerOnWMKeyDown(HWND hwnd, EditorBase *eb, WPARAM wparam);

    /// WM_KEYUP before Scintilla; always 0
    wyInt32             HandlerOnWMKeyUp(HWND hwnd, EditorBase *eb, WPARAM wparam);

    /// A statement ran on this window's connection (FrameWindowHelper): DDL invalidates the catalog
    wyBool              HandlerAddToMessageQueue(MDIWindow*	wnd, wyChar *query);

    void                HandlerProcessMessageQueue(MDIWindow *wnd);
    wyBool              HandlerShowToolTip(MDIWindow *wnd);
    void                HandlerOnAutoThreadExit(MDIWindow *wnd, LPARAM lparam);
    VOID				UpdateTagBuildFlag();

    /// SCN_* notifications of the active editor (TabModule); wyTrue = handled, stop
    wyBool              OnACNotification(WPARAM wparam, LPARAM lparam);

    /// The autocomplete object, NULL until HandlerInitAutoComplete
    NgsaAutoComplete    *m_autocomplete;
};

#endif
