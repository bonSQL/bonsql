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

#include "FrameWindowHelper.h"

#define ID_ANALYZERMAIN     100
#define QUERYRIBBONHEIGHT	19

#ifndef CLAS_ANALYZER_BASE
#define CLAS_ANALYZER_BASE

//.CSS Classes
//#define CSS_WARNING ".tabcaptionstyle{font: 14px \"Trebuchet MS\", Verdana, Arial, Helvetica; text-align:left; color:grey;}"

#define QA_ULTIMATE _("")	// bonSQL: was an Ultimate upsell with a webyog.com buy link (docs/ngsa/UPSELLS.md)


class QueryAnalyzerBase : public TabQueryTypes 
{
public:	
	//constructor and destructor
	QueryAnalyzerBase(MDIWindow* wnd, HWND hwndparent);
	virtual ~QueryAnalyzerBase();

	///Create the new tab and its controls
	/**
	@param query : IN Query to be analyzed
	@param index : IN Index of the Analyzer tab
	@return wyTrue on succcess else return wyFalse
	*/
	virtual wyBool			CreateControls(wyChar *query, wyInt32 index, wyBool istunnel, wyBool isdefaulttab, wyBool islimitadded) = 0;

	//Initialize the HTML buffer
	/**
	@return void
	*/
	virtual wyBool			InitHtmlBuffer(wyBool istunnel, wyBool isdeftab) = 0;

	///Create the new tab 'Analyzer' and inserts into custom tab
	/**
	@param index	 : IN Index of the Analyzer tab
	@param istoalert : IN flag tells query alerted, this flag decides which icon to display
	@return wyTrue on succcess else return wyFalse
	*/
	wyBool			CreateAnalyzerTab(wyInt32 index);

	//Main window for analyzer controls
	/**
	@return wyTrue on success else return wyFalse
	*/
	wyBool			CreateMainWindow();

	///Creates the scintilla editor displays the query
	/**
	@return wyTrue on succcess else return wyFalse
	*/
	wyBool			CreateHtmlEditor(wyString *htmlbuffer);

	///Window Procedure for parent of editor and toolbar
    /**
    @param hwnd                 : IN Window Handler.
    @param message              : IN Messages.
    @param wparam               : IN WPARAM message parameter.
    @param lparam               : LPARAM parameter.
    */
	
	static LRESULT	CALLBACK	PQAHtmlWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    static LRESULT	CALLBACK	QueryWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    static LRESULT	CALLBACK	MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	///Wrapper resizes Analyzer tab controls
	/**
	@return void
	*/
	void			Resize();

	///Show/hide windows when switching between tabs
	/**
	@param state : IN sets wyTrue to show all tab controls and hide other tab's controls, 
	               sets wyFalse to hide this tab's controls
	@return void
	*/
	void        OnTabSelChange(wyBool isselected);

    void        SetBufferedDrawing(wyBool isset);

	//Query to be processed
	wyString	m_querytext;

	//Declarions
	HWND			m_hwnd;

	//Scintilla editor
	HWND			m_hwndscieditor;
	
	//Ribbon displays the query executed
	HWND			m_hwndquerystatic;	

	//Font for controls
	HFONT			m_hfont;

	//Flag sets wyTrue for default qa ta, else sets wyFalse
	wyBool			m_isdefaultpqatab;

	//HTML buffer
	wyString		m_htmlformatstr;
	wyString		m_htmlprofilerresultstr;

    wyBool          m_isbuffereddrawing;

    WNDPROC         m_origmainwndproc;
};

#endif 
