/* Copyright (C) 2026 André Walsh
   This file is part of bonSQL and is distributed under the GNU General Public License, version 3 or later.

   Check for new versions: see include/ngsa/NgsaUpdateCheck.h. WinHTTP is part of Windows (and of Wine), so
   nothing extra ships with the program. */

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <process.h>
#include <string>
#include <memory>

#include "ngsa/NgsaUpdateCheck.h"
#include "ngsa/NgsaBrand.h"
#include "Global.h"
#include "CommonHelper.h"
#include "FrameWindowHelper.h"
#include "GUIHelper.h"
#include "wyIni.h"
#include "Version.h"
#include "resource.h"
#include "jsoncpp.h"

#pragma comment(lib, "winhttp.lib")

extern PGLOBALS pGlobals;

#define NGSA_WM_UPDATEDONE      (WM_APP + 42)   /* worker thread -> dialog: lParam = Job* */
#define NGSA_UPDATE_DAYS        7               /* automatic check at most this often */
#define NGSA_UPDATE_MAXBODY     65536           /* the file is a few hundred bytes; anything bigger is not ours */

namespace
{
    /* wyString cannot be copied as a whole */
    void CopyInfo(NgsaUpdate::Info &dst, NgsaUpdate::Info &src)
    {
        dst.version.SetAs(src.version.GetString());
        dst.date.SetAs(src.date.GetString());
        dst.url.SetAs(src.url.GetString());
        dst.note.SetAs(src.note.GetString());
    }

    /* ---- settings ---------------------------------------------------------------------------------------- */

    void IniPath(wyString &dirstr)
    {
        wyWChar     *lpfileport = 0;
        wyWChar     directory[MAX_PATH + 1] = {0};

        SearchFilePath(BRAND_CFG_STEM_W, L".ini", MAX_PATH, directory, &lpfileport);
        dirstr.SetAs(directory);
    }

    /* days since 1601, from the system clock; only differences matter */
    wyUInt32 Today()
    {
        SYSTEMTIME      st;
        FILETIME        ft;
        ULARGE_INTEGER  u;

        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &ft);
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return (wyUInt32)(u.QuadPart / (10000000ULL * 86400ULL));
    }

    void IgnoredVersion(wyString &out)
    {
        wyString dirstr;

        IniPath(dirstr);
        wyIni::IniGetString(GENERALPREFA, "VersionToBeIgnored", "", &out, dirstr.GetString());
    }

    void SetIgnoredVersion(const wyChar *version)
    {
        wyString dirstr;

        IniPath(dirstr);
        wyIni::IniWriteString(GENERALPREFA, "VersionToBeIgnored", version, dirstr.GetString());
    }

    /* ---- the fetch ---------------------------------------------------------------------------------------- */

    void WinHttpError(DWORD code, wyString &error)
    {
        wyWChar     buffer[512] = {0};
        HMODULE     hmod = GetModuleHandle(L"winhttp.dll");
        DWORD       flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | (hmod ? FORMAT_MESSAGE_FROM_HMODULE : 0);

        if(FormatMessage(flags, hmod, code, 0, buffer, 511, NULL) == 0)
        {
            error.Sprintf("error %lu", code);
            return;
        }

        error.SetAs(buffer);
        error.RTrim();
    }

    bool ValidVersion(const std::string &v)
    {
        if(v.empty() || v.size() > 31 || v[0] == '.' || v[v.size() - 1] == '.')
            return false;

        for(size_t i = 0; i < v.size(); i++)
        {
            if(!((v[i] >= '0' && v[i] <= '9') || v[i] == '.'))
                return false;
        }

        return true;
    }

    /* GET the file; the body comes back as bytes (the file is UTF-8) */
    bool Get(const wyChar *url, std::string &body, wyString &error)
    {
        wyString            urlstr(url);
        URL_COMPONENTS      uc = {0};
        wyWChar             host[256] = {0}, path[1024] = {0};
        HINTERNET           hsession = NULL, hconnect = NULL, hrequest = NULL;
        DWORD               status = 0, size = sizeof(status), avail = 0, got = 0;
        bool                ok = false;

        uc.dwStructSize = sizeof(uc);
        uc.lpszHostName = host;     uc.dwHostNameLength = 255;
        uc.lpszUrlPath = path;      uc.dwUrlPathLength = 1023;

        if(!WinHttpCrackUrl(urlstr.GetAsWideChar(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
        {
            error.SetAs("the update address is not an https:// address");
            return false;
        }

        /* automatic proxy detection needs Windows 8.1; older systems and Wine get the default proxy */
        hsession = WinHttpOpen(L"bonSQL", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if(!hsession)
            hsession = WinHttpOpen(L"bonSQL", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

        if(!hsession)
        {
            WinHttpError(GetLastError(), error);
            return false;
        }

        WinHttpSetTimeouts(hsession, 5000, 5000, 5000, 10000);

        hconnect = WinHttpConnect(hsession, host, uc.nPort, 0);
        if(hconnect)
            hrequest = WinHttpOpenRequest(hconnect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

        if(hrequest &&
           WinHttpSendRequest(hrequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
           WinHttpReceiveResponse(hrequest, NULL) &&
           WinHttpQueryHeaders(hrequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        {
            if(status != 200)
            {
                error.Sprintf("the server answered %lu", status);
            }
            else
            {
                ok = true;

                while(WinHttpQueryDataAvailable(hrequest, &avail) && avail > 0)
                {
                    if(body.size() + avail > NGSA_UPDATE_MAXBODY)
                    {
                        error.SetAs("the answer is too large to be the version file");
                        ok = false;
                        break;
                    }

                    std::string chunk(avail, '\0');

                    if(!WinHttpReadData(hrequest, &chunk[0], avail, &got))
                    {
                        WinHttpError(GetLastError(), error);
                        ok = false;
                        break;
                    }

                    body.append(chunk, 0, got);
                }
            }
        }
        else
        {
            WinHttpError(GetLastError(), error);
        }

        if(hrequest)
            WinHttpCloseHandle(hrequest);
        if(hconnect)
            WinHttpCloseHandle(hconnect);
        WinHttpCloseHandle(hsession);

        return ok;
    }

    /* ---- the dialog --------------------------------------------------------------------------------------- */

    struct Job                      /* one fetch on a worker thread; owned by the thread until posted */
    {
        HWND                hwnddlg;
        NgsaUpdate::Info    info;
        wyBool              ok;
        wyString            error;
    };

    struct DlgState
    {
        wyBool              found;      /* opened by the start-up check with a result already in hand */
        NgsaUpdate::Info    info;
        wyBool              newer;
        wyBool              busy;
    };

    unsigned __stdcall FetchThread(void *param)
    {
        Job *job = (Job*)param;

        job->ok = NgsaUpdate::Fetch(job->info, job->error);

        if(!PostMessage(job->hwnddlg, NGSA_WM_UPDATEDONE, 0, (LPARAM)job))
            delete job;             /* the dialog has gone */

        return 0;
    }

    void ShowResult(HWND hwnd, DlgState *state)
    {
        wyString    current, text, ignored;

        NgsaUpdate::CurrentVersion(current);
        state->newer = (NgsaUpdate::CompareVersions(state->info.version.GetString(), current.GetString()) > 0) ? wyTrue : wyFalse;

        if(state->newer == wyTrue)
        {
            text.Sprintf(_("bonSQL %s is available, released %s.\nYou have %s."),
                         state->info.version.GetString(), state->info.date.GetString(), current.GetString());
        }
        else if(NgsaUpdate::CompareVersions(state->info.version.GetString(), current.GetString()) == 0)
        {
            text.Sprintf(_("bonSQL %s is up to date."), current.GetString());
        }
        else
        {
            text.Sprintf(_("You have bonSQL %s, which is newer than the released %s."), current.GetString(), state->info.version.GetString());
        }

        SetDlgItemText(hwnd, IDC_NGSA_UPD_TEXT, text.GetAsWideChar());
        SetDlgItemText(hwnd, IDC_NGSA_UPD_NOTE, state->newer == wyTrue ? state->info.note.GetAsWideChar() : L"");
        EnableWindow(GetDlgItem(hwnd, IDC_NGSA_UPD_DOWNLOAD), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_NGSA_UPD_SKIP), state->newer == wyTrue ? TRUE : FALSE);
        state->busy = wyFalse;
    }

    void ShowError(HWND hwnd, DlgState *state, const wyChar *error)
    {
        wyString text;

        text.Sprintf(_("Could not check for updates: %s"), error);
        SetDlgItemText(hwnd, IDC_NGSA_UPD_TEXT, text.GetAsWideChar());
        SetDlgItemText(hwnd, IDC_NGSA_UPD_NOTE, _(L"You can look at the download page instead."));
        EnableWindow(GetDlgItem(hwnd, IDC_NGSA_UPD_DOWNLOAD), TRUE);
        state->busy = wyFalse;
    }

    INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        DlgState    *state = (DlgState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        Job         *job;
        wyString    text;

        switch(message)
        {
        case WM_INITDIALOG:
            state = (DlgState*)lparam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);
            SendDlgItemMessage(hwnd, IDC_NGSA_UPD_AUTO, BM_SETCHECK, NgsaUpdate::IsEnabled() == wyTrue ? BST_CHECKED : BST_UNCHECKED, 0);

            if(state->found == wyTrue)
            {
                ShowResult(hwnd, state);
            }
            else
            {
                text.Sprintf(_("Checking %s..."), BRAND_UPDATE_HOST_A);
                SetDlgItemText(hwnd, IDC_NGSA_UPD_TEXT, text.GetAsWideChar());
                EnableWindow(GetDlgItem(hwnd, IDC_NGSA_UPD_DOWNLOAD), FALSE);
                EnableWindow(GetDlgItem(hwnd, IDC_NGSA_UPD_SKIP), FALSE);
                state->busy = wyTrue;

                job = new Job;
                job->hwnddlg = hwnd;
                job->ok = wyFalse;

                HANDLE hthread = (HANDLE)_beginthreadex(NULL, 0, FetchThread, job, 0, NULL);

                if(hthread)
                    CloseHandle(hthread);
                else
                {
                    delete job;
                    ShowError(hwnd, state, "could not start the check");
                }
            }
            return TRUE;

        case NGSA_WM_UPDATEDONE:
            job = (Job*)lparam;

            if(job->ok == wyTrue)
            {
                CopyInfo(state->info, job->info);
                ShowResult(hwnd, state);
            }
            else
            {
                ShowError(hwnd, state, job->error.GetString());
            }

            delete job;
            return TRUE;

        case WM_COMMAND:
            switch(LOWORD(wparam))
            {
            case IDC_NGSA_UPD_AUTO:
                NgsaUpdate::SetEnabled(SendDlgItemMessage(hwnd, IDC_NGSA_UPD_AUTO, BM_GETCHECK, 0, 0) == BST_CHECKED ? wyTrue : wyFalse);
                return TRUE;

            case IDC_NGSA_UPD_DOWNLOAD:
                text.SetAs(state->info.url.GetLength() ? state->info.url.GetString() : BRAND_DOWNLOAD_URL_A);
                ShellExecute(NULL, L"open", text.GetAsWideChar(), NULL, NULL, SW_SHOWNORMAL);
                return TRUE;

            case IDC_NGSA_UPD_SKIP:
                SetIgnoredVersion(state->info.version.GetString());
                EndDialog(hwnd, 0);
                return TRUE;

            case IDCANCEL:
            case IDOK:
                EndDialog(hwnd, 0);
                return TRUE;
            }
            break;
        }

        return FALSE;
    }

    /* ---- the start-up check ------------------------------------------------------------------------------ */

    unsigned __stdcall StartupThread(void *param)
    {
        HWND                hwndmain = (HWND)param;
        NgsaUpdate::Info    *info = new NgsaUpdate::Info;
        wyString            error, current, ignored;

        if(NgsaUpdate::Fetch(*info, error) == wyTrue)
        {
            NgsaUpdate::CurrentVersion(current);
            IgnoredVersion(ignored);

            if(NgsaUpdate::CompareVersions(info->version.GetString(), current.GetString()) > 0 &&
               ignored.Compare(info->version.GetString()) != 0 &&
               PostMessage(hwndmain, NGSA_WM_UPDATEFOUND, 0, (LPARAM)info))
            {
                return 0;       /* the main window owns `info` now */
            }
        }

        delete info;
        return 0;
    }
}

/* ---- public ---------------------------------------------------------------------------------------------- */

wyBool
NgsaUpdate::IsEnabled()
{
    wyString dirstr;

    IniPath(dirstr);
    return wyIni::IniGetInt(GENERALPREFA, "UpdateCheck", 0, dirstr.GetString()) ? wyTrue : wyFalse;
}

void
NgsaUpdate::SetEnabled(wyBool on)
{
    wyString dirstr;

    IniPath(dirstr);
    wyIni::IniWriteInt(GENERALPREFA, "UpdateCheck", on == wyTrue ? 1 : 0, dirstr.GetString());
}

void
NgsaUpdate::CurrentVersion(wyString &out)
{
    out.Sprintf("%d.%d.%d", MAJOR_VERSION_INT, MINOR_VERSION_INT, UPDATE_VERSION_INT);
}

wyInt32
NgsaUpdate::CompareVersions(const wyChar *a, const wyChar *b)
{
    while(*a || *b)
    {
        long na = 0, nb = 0;

        while(*a >= '0' && *a <= '9') na = na * 10 + (*a++ - '0');
        while(*b >= '0' && *b <= '9') nb = nb * 10 + (*b++ - '0');

        if(na != nb)
            return na < nb ? -1 : 1;

        if(*a == '.') a++;
        if(*b == '.') b++;
    }

    return 0;
}

wyBool
NgsaUpdate::Fetch(Info &info, wyString &error)
{
    std::string             body, errs;
    Json::CharReaderBuilder builder;
    Json::Value             root;

    /* BONSQL_UPDATE_URL in the environment points a test at another copy of the file (still https only) */
    const wyChar *url = getenv("BONSQL_UPDATE_URL");

    if(!Get(url && *url ? url : BRAND_UPDATE_URL_A, body, error))
        return wyFalse;

    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

    if(!reader->parse(body.data(), body.data() + body.size(), &root, &errs) ||
       !root.isObject() || !root["version"].isString() || !ValidVersion(root["version"].asString()))
    {
        error.SetAs("the version file could not be read");
        return wyFalse;
    }

    info.version.SetAs(root["version"].asCString());
    info.date.SetAs(root["date"].isString() ? root["date"].asCString() : "");
    info.url.SetAs(root["url"].isString() ? root["url"].asCString() : BRAND_DOWNLOAD_URL_A);
    info.note.SetAs(root["note"].isString() ? root["note"].asCString() : "");

    /* the download page must be on the same site as the file; anything else is ignored */
    if(info.url.GetLength() && strncmp(info.url.GetString(), BRAND_WEBSITE_A, strlen(BRAND_WEBSITE_A)) != 0)
        info.url.SetAs(BRAND_DOWNLOAD_URL_A);

    return wyTrue;
}

void
NgsaUpdate::CheckNow(HWND hwndowner)
{
    DlgState state;

    state.found = wyFalse;
    state.newer = wyFalse;
    state.busy = wyFalse;
    DialogBoxParam(pGlobals->m_hinstance, MAKEINTRESOURCE(IDD_NGSA_UPDATE), hwndowner, DlgProc, (LPARAM)&state);
}

void
NgsaUpdate::ShowFound(HWND hwndowner, LPARAM lparam)
{
    Info        *info = (Info*)lparam;
    DlgState    state;

    if(!info)
        return;

    state.found = wyTrue;
    CopyInfo(state.info, *info);
    state.newer = wyTrue;
    state.busy = wyFalse;
    delete info;

    DialogBoxParam(pGlobals->m_hinstance, MAKEINTRESOURCE(IDD_NGSA_UPDATE), hwndowner, DlgProc, (LPARAM)&state);
}

void
NgsaUpdate::CheckAtStartup(HWND hwndmain)
{
    wyString    dirstr;
    wyUInt32    today = Today(), last;
    HANDLE      hthread;

    if(IsEnabled() == wyFalse)
        return;

    IniPath(dirstr);
    last = (wyUInt32)wyIni::IniGetInt(GENERALPREFA, "UpdatesCheckedDay", 0, dirstr.GetString());

    if(last && today >= last && today - last < NGSA_UPDATE_DAYS)
        return;

    wyIni::IniWriteInt(GENERALPREFA, "UpdatesCheckedDay", (wyInt32)today, dirstr.GetString());

    hthread = (HANDLE)_beginthreadex(NULL, 0, StartupThread, hwndmain, 0, NULL);

    if(hthread)
        CloseHandle(hthread);
}
