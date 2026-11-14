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

#include "PreferenceCommunity.h"
#include "EditorFont.h"
#include "CommonHelper.h"
#include "ngsa/NgsaAutoComplete.h"	// bonSQL: the Autocomplete page

extern PGLOBALS		pGlobals;

// bonSQL: Autocomplete page defaults
#define		NGSA_AC_DEFAULT				1
#define		NGSA_AC_ASYOUTYPE_DEFAULT	1
#define		NGSA_AC_HELP_DEFAULT		1

/* Applies all the changes or current values to the .ini file */
void
PreferencesCommunity::Apply()
{
	if(m_ispreferenceapplied)
		return;
    SaveGeneralPreferences(m_hwnd, GENERALPREF_PAGE);

    SaveFontPreferences(m_hwnd, FONT_PAGE); 

	SaveOthersPreferences(m_hwnd, OTHERS_PAGE);

	SaveAcPreferences(m_hwnd, AC_PAGE);	// bonSQL

	EnumChildWindows(pGlobals->m_hwndclient, EnumChildProc, (LPARAM)this);

	m_ispreferenceapplied = wyTrue;
	return;
}

void
PreferencesCommunity::RestoreAllDefaults()
{
	m_isrestorealldefaults = wyTrue;//user clicked RestoreAllTabs button

	SetGenPrefDefaultAllTabValues(m_hwnd, GENERALPREF_PAGE); //Setting General pref dialogue with default values
	SetFontPrefDefAllTabValues(m_hwnd, FONT_PAGE);//Setting Font pref dialogue with default values
	SetOthersPrefDefaultAllTabValues(m_hwnd, OTHERS_PAGE);//Setting Others pref dialogue with default values
	SetAcPrefDefaultAllTabValues(m_hwnd, AC_PAGE);	// bonSQL

	return;
}

// bonSQL: the Autocomplete page (the IDD_PREF3 template): the three ticks from the ini
void
PreferencesCommunity::EnterprisePrefHandleWmInitDialog(HWND hwnd)
{
	wyString	dirstr(m_directory);
	wyInt32		v;

	if(m_startpage == AC_PAGE)
        HandlerToSetWindowPos(hwnd);

	v = wyIni::IniGetInt(GENERALPREFA, "Autocomplete", NGSA_AC_DEFAULT, dirstr.GetString());
	SendMessage(GetDlgItem(hwnd, IDC_AUTOCOMPLETE), BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
	v = wyIni::IniGetInt(GENERALPREFA, "AutocompleteAsYouType", NGSA_AC_ASYOUTYPE_DEFAULT, dirstr.GetString());
	SendMessage(GetDlgItem(hwnd, IDC_AUTOCOMPLETESHOWTOOLTIP), BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
	v = wyIni::IniGetInt(GENERALPREFA, "AutocompleteHelp", NGSA_AC_HELP_DEFAULT, dirstr.GetString());
	SendMessage(GetDlgItem(hwnd, IDC_AUTOCOMPLETEHELP), BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
	AcEnableSubTicks(hwnd);
}

void
PreferencesCommunity::AcEnableSubTicks(HWND hwnd)
{
	BOOL on = SendMessage(GetDlgItem(hwnd, IDC_AUTOCOMPLETE), BM_GETCHECK, 0, 0) == BST_CHECKED;
	EnableWindow(GetDlgItem(hwnd, IDC_AUTOCOMPLETESHOWTOOLTIP), on);
	EnableWindow(GetDlgItem(hwnd, IDC_AUTOCOMPLETEHELP), on);
}

void
PreferencesCommunity::SetAcPrefDefaultValues(HWND hwnd)
{
	SendMessage(GetDlgItem(hwnd, IDC_AUTOCOMPLETE), BM_SETCHECK, NGSA_AC_DEFAULT, 0);
	SendMessage(GetDlgItem(hwnd, IDC_AUTOCOMPLETESHOWTOOLTIP), BM_SETCHECK, NGSA_AC_ASYOUTYPE_DEFAULT, 0);
	SendMessage(GetDlgItem(hwnd, IDC_AUTOCOMPLETEHELP), BM_SETCHECK, NGSA_AC_HELP_DEFAULT, 0);
	AcEnableSubTicks(hwnd);
}

void
PreferencesCommunity::SetAcPrefDefaultAllTabValues(HWND hwndbase, wyInt32 page)
{
	HWND hwnd = PropSheet_IndexToHwnd(GetParent(hwndbase), page);

	if(hwnd)
		SetAcPrefDefaultValues(hwnd);
}

wyBool
PreferencesCommunity::SaveAcPreferences(HWND hwndbase, wyInt32 page)
{
	HWND		hwnd = PropSheet_IndexToHwnd(GetParent(hwndbase), page);
	wyString	dirstr(m_directory);

	if(!hwnd)
	{
		// the page was never shown: only Restore All Defaults has something to write
		if(m_isrestorealldefaults == wyTrue)
		{
			wyIni::IniWriteInt(GENERALPREFA, "Autocomplete", NGSA_AC_DEFAULT, dirstr.GetString());
			wyIni::IniWriteInt(GENERALPREFA, "AutocompleteAsYouType", NGSA_AC_ASYOUTYPE_DEFAULT, dirstr.GetString());
			wyIni::IniWriteInt(GENERALPREFA, "AutocompleteHelp", NGSA_AC_HELP_DEFAULT, dirstr.GetString());
			NgsaAutoComplete::ReloadPrefs();
		}
		return wyFalse;
	}

	SetBoolProfileString(hwnd, GENERALPREF, L"Autocomplete", IDC_AUTOCOMPLETE);
	SetBoolProfileString(hwnd, GENERALPREF, L"AutocompleteAsYouType", IDC_AUTOCOMPLETESHOWTOOLTIP);
	SetBoolProfileString(hwnd, GENERALPREF, L"AutocompleteHelp", IDC_AUTOCOMPLETEHELP);
	NgsaAutoComplete::ReloadPrefs();
	return wyTrue;
}

void
PreferencesCommunity::EntPrefHandleWmCommand(HWND hwnd, WPARAM wParam)
{
	switch(LOWORD(wParam))
	{
	case IDC_AUTOCOMPLETE:
		AcEnableSubTicks(hwnd);
		break;

	case IDC_POWERTOOLSRESTORETAB:
		SetAcPrefDefaultValues(hwnd);
		break;

	case IDC_POWERTOOLSRESTOREALL:
		RestoreAllDefaults();
		break;
	}
}
void 
PreferencesCommunity::FormatterPrefHandleWmNotify(HWND hwnd, LPARAM lParam)
{
    LPNMHDR lpnm = (LPNMHDR)lParam;
	wyString dirstr;
    switch (lpnm->code)
    {
	case PSN_SETACTIVE:
		dirstr.SetAs(m_directory);
		m_hwnd = hwnd;
		pGlobals->m_prefpersist=FORMATTER_PAGE;
		wyIni::IniWriteInt(GENERALPREFA, "PrefPersist", FORMATTER_PAGE, dirstr.GetString());
		break;
	case PSN_APPLY: //user pressed the OK button.		
		Apply();
		break;

	}	
}
void 
PreferencesCommunity::ACPrefHandleWmNotify(HWND hwnd, LPARAM lParam)
{
    LPNMHDR lpnm = (LPNMHDR)lParam;
	wyString dirstr;
    switch (lpnm->code)
    {
	case PSN_SETACTIVE:
		dirstr.SetAs(m_directory);
		m_hwnd = hwnd;
		pGlobals->m_prefpersist=AC_PAGE;
		wyIni::IniWriteInt(GENERALPREFA, "PrefPersist", AC_PAGE, dirstr.GetString());
		break;
	case PSN_APPLY: //user pressed the OK button.		
		Apply();
		break;

	}	
}
//Disabling all Enterprise-Formtter tab controls
void
PreferencesCommunity::FormatterPrefHandleWmInitDialog(HWND hwnd)
{
	HWND		hwndacgrp = NULL;
	HWND		hwndpreview = NULL;
	wyInt32     publicid[] = { IDC_OPTIONSGROUP, IDC_TABCOLLIST, IDC_LINEBRK,
								IDC_PREVIEWGROUP, IDC_STACKED, IDC_BEFORECOMMA, 
								IDC_AFTERCOMMA, IDC_NOTSTACKED,  
								IDC_INDENT, IDC_INDENTATION, IDC_SPACES, IDC_FORMATTERPREVIEW, 
								IDC_FORMATTERRESTOREALL, IDC_FORMATTERRESTORETAB };

	wyInt32     count = sizeof(publicid)/ sizeof(publicid[0]);
	if(m_startpage == FORMATTER_PAGE)
		HandlerToSetWindowPos(hwnd);

	hwndpreview = GetDlgItem(hwnd, IDC_FORMATTERPREVIEW);

	//setting editor properties
	SetPreviewEditor(hwnd);
	//Setting a query to preview window
	SendMessage(hwndpreview, SCI_SETTEXT, 0, (LPARAM)FORMATTER_PREVIEW);

	EnableOrDisable(hwnd, publicid, count , wyFalse);

	//Formatter Groupbox	
	hwndacgrp = GetDlgItem(hwnd, IDC_OPTIONSGROUP);

	//Sets the Groupbox caption
	SendMessage(hwndacgrp, WM_SETTEXT, 0, (LPARAM)COMMUNITY_FORMATTER);
}

void        
PreferencesCommunity::FormatterPrefHandleWmCommand(HWND hwnd, WPARAM wParam)
{
}