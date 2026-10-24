 /* Copyright (C) 2013 Webyog Inc.
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

*/

/* bonSQL, 13/10/2026: the Announcements panel showed HTML that SQLyog downloaded from
   http://www.webyog.com/notifications/ once a day at start-up, drawn by HTMLayout. HTMLayout is gone and the
   feed is Webyog's, so the panel is switched off: the download thread is not started (FrameWindow.cpp) and
   the panel never opens. The class keeps its interface so callers compile unchanged. */

#include "Announcements.h"
#include "Global.h"

Announcements::Announcements()
{
    m_hwnd = NULL;
    m_hwndHTML = NULL;
    m_htmlw = m_htmlh = m_htmlx = m_htmly = 0;
    m_annw = m_annh = m_annx = m_anny = 0;
}

Announcements::~Announcements()
{
}

wyBool
Announcements::HandleAnnouncementsCheck(HWND)
{
    return wyFalse;
}

void
Announcements::Resize(HWND, wyBool)
{
}

LRESULT CALLBACK
Announcements::AnnounceWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK
Announcements::AnnounceWndMainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, message, wParam, lParam);
}
