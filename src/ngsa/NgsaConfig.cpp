/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * bonSQL settings location and the one-time import from SQLyog (docs/ngsa/REBRAND.md, decisions 2 and 3).
 *
 * bonSQL keeps its settings in %APPDATA%\bonSQL (or the -dir folder) under its own names. On the first
 * start, if that folder has no bonsql.ini and SQLyog's folder has a sqlyog.ini, the user is asked whether
 * to import it. The import copies, never moves: SQLyog keeps working with its own files.
 *
 *   %APPDATA%\SQLyog\sqlyog.ini          -> %APPDATA%\bonSQL\bonsql.ini, [SQLYOG] -> [bonSQL],
 *                                            [IctSshHostKeys] -> [SshHostKeys]
 *   sqlyog_backup.ini, .err, .log, ...   -> bonsql_backup.ini, ... (same section renames in the backup ini)
 *   other files and every sub-folder     -> copied as they are (Favorites, Tags, Themes, connrestore.db, ...)
 *   crash dumps, sqlyog.ini.* copies     -> left behind
 *
 * With -dir the source and target are the same folder: only SQLyog's named files are copied to their new
 * names next to the old ones. Saved passwords use SQLyog's own encoding, which bonSQL shares.
 */
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>

#include "CommonHelper.h"
#include "FrameWindowHelper.h"
#include "ngsa/NgsaBrand.h"
#include "ngsa/NgsaConfig.h"
#include "ngsa/NgsaTrace.h"
#include "ngsa/NgsaUpdateCheck.h"

extern PGLOBALS pGlobals;

HRESULT
NgsaGetAppData(wchar_t *path)
{
    DWORD   n = GetEnvironmentVariableW(L"BONSQL_TEST_APPDATA", path, MAX_PATH);

    if(n > 0 && n < MAX_PATH)
        return S_OK;

    path[0] = 0;
    return SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path);
}

namespace
{
    struct Rename { const wchar_t* from; const wchar_t* to; };

    /* SQLyog's own files that come across under bonSQL's names */
    const Rename RENAMES[] =
    {
        { L"sqlyog.ini",            BRAND_CFG_STEM_W L".ini" },
        { L"sqlyog_backup.ini",     BRAND_CFG_STEM_W L"_backup.ini" },
        { L"sqlyog.err",            BRAND_CFG_STEM_W L".err" },
        { L"sqlyog.log",            BRAND_CFG_STEM_W L".log" },
        { L"sqlyog_logx.log",       BRAND_CFG_STEM_W L"_logx.log" },
        { L"SQLyog_session.tmp",    BRAND_CFG_STEM_W L"_session.tmp" },
    };

    bool
    Exists(const std::wstring& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    /* The name a top-level file gets in bonSQL's folder, the same pointer if it keeps its name, or NULL to
       leave it behind */
    const wchar_t*
    NewName(const wchar_t* name)
    {
        size_t  len = wcslen(name);

        for(const Rename& r : RENAMES)
            if(!_wcsicmp(name, r.from))
                return r.to;

        if(!_wcsnicmp(name, L"sqlyog", 6))         /* sqlyog.ini.before-..., and anything else of SQLyog's */
            return NULL;

        if(len > 4 && !_wcsicmp(name + len - 4, L".dmp"))
            return NULL;

        return name;
    }

    /* Copies src into dst. At the top level the names go through NewName; below it everything is copied.
       samedir: src and dst are one folder (-dir), so only renamed files are copied. Returns the file count. */
    int
    CopyTree(const std::wstring& src, const std::wstring& dst, bool top, bool samedir)
    {
        WIN32_FIND_DATAW    fd;
        HANDLE              h;
        int                 n = 0;

        if((h = FindFirstFileW((src + L"\\*").c_str(), &fd)) == INVALID_HANDLE_VALUE)
            return 0;

        CreateDirectoryW(dst.c_str(), NULL);

        do
        {
            if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;

            if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if(!samedir)
                    n += CopyTree(src + L"\\" + fd.cFileName, dst + L"\\" + fd.cFileName, false, false);
                continue;
            }

            const wchar_t* to = top ? NewName(fd.cFileName) : fd.cFileName;

            if(!to || (samedir && to == fd.cFileName))
                continue;

            if(CopyFileW((src + L"\\" + fd.cFileName).c_str(), (dst + L"\\" + to).c_str(), TRUE))
                n++;
            else
                NgsaTrace("SQLyog import: copy of %ls failed, error %lu", fd.cFileName, GetLastError());
        }
        while(FindNextFileW(h, &fd));

        FindClose(h);
        return n;
    }

    /* Renames SQLyog's section headers in a copied ini. The ini is 8-bit text (wyIni); lines keep their
       own line endings. */
    bool
    RenameSections(const std::wstring& ini)
    {
        static const char* const MAP[][2] =
        {
            { "[SQLYOG]",                           "[" BRAND_INI_SECTION_A "]" },
            { "[" NGSA_HOSTKEY_SECTION_OLD "]",     "[" NGSA_HOSTKEY_SECTION "]" },
        };
        std::string     s, out;
        char            buf[65536];
        size_t          k, pos = 0;
        FILE*           f;
        int             changed = 0;

        if(_wfopen_s(&f, ini.c_str(), L"rb") || !f)
            return false;
        while((k = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, k);
        fclose(f);

        if(s.size() >= 2 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE)
        {
            NgsaTrace("SQLyog import: %ls is UTF-16, sections not renamed", ini.c_str());
            return false;
        }

        while(pos < s.size())
        {
            size_t          e = s.find('\n', pos);
            size_t          end = (e == std::string::npos) ? s.size() : e + 1;
            std::string     line = s.substr(pos, end - pos);
            std::string     core = line;

            while(!core.empty() && (core.back() == '\n' || core.back() == '\r' || core.back() == ' ' || core.back() == '\t'))
                core.pop_back();

            for(const auto& m : MAP)
            {
                if(!_stricmp(core.c_str(), m[0]))
                {
                    line = std::string(m[1]) + line.substr(core.size());
                    changed++;
                    break;
                }
            }

            out += line;
            pos = end;
        }

        if(_wfopen_s(&f, ini.c_str(), L"wb") || !f)
            return false;
        k = fwrite(out.data(), 1, out.size(), f);
        fclose(f);

        NgsaTrace("SQLyog import: %ls, %d section(s) renamed", ini.c_str(), changed);
        return k == out.size();
    }
}

void
NgsaOfferSqlyogImport()
{
    std::wstring    src, dst;
    bool            samedir;
    wchar_t         msg[1024];
    int             n;

    if(pGlobals->m_configdirpath.GetLength())
    {
        src = dst = pGlobals->m_configdirpath.GetAsWideChar();
        samedir = true;
    }
    else
    {
        wchar_t     appdata[MAX_PATH + 1] = {0};

        if(FAILED(NgsaGetAppData(appdata)))
            return;

        src = std::wstring(appdata) + L"\\SQLyog";
        dst = std::wstring(appdata) + L"\\" BRAND_CFG_DIR_W;
        samedir = false;
    }

    if(Exists(dst + L"\\" BRAND_CFG_STEM_W L".ini") || !Exists(src + L"\\sqlyog.ini"))
        return;

    swprintf(msg, 1024, L"SQLyog settings were found in\r\n%s\r\n\r\n"
                        L"Import SQLyog's connections, favorites and settings into " BRAND_NAME_W L"?\r\n\r\n"
                        L"SQLyog's own files are copied, not changed.", src.c_str());

    if(MessageBoxW(NULL, msg, BRAND_NAME_W, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) != IDYES)
    {
        NgsaTrace("SQLyog import: declined (%ls)", src.c_str());
        return;
    }

    n = CopyTree(src, dst, true, samedir);

    bool ok = Exists(dst + L"\\" BRAND_CFG_STEM_W L".ini") && RenameSections(dst + L"\\" BRAND_CFG_STEM_W L".ini");

    if(Exists(dst + L"\\" BRAND_CFG_STEM_W L"_backup.ini"))
        RenameSections(dst + L"\\" BRAND_CFG_STEM_W L"_backup.ini");

    NgsaTrace("SQLyog import: %d file(s) from %ls to %ls, ini %s", n, src.c_str(), dst.c_str(), ok ? "ok" : "FAILED");

    if(!ok)
        MessageBoxW(NULL, L"The SQLyog settings could not be imported completely. " BRAND_NAME_W
                          L" starts with the files that were copied.", BRAND_NAME_W, MB_OK | MB_ICONWARNING);
}

void
NgsaApplyInstallDefaults()
{
    HKEY    hkey;
    DWORD   value = 0, size = sizeof(value), type = 0;

    if(RegOpenKeyExA(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ | KEY_SET_VALUE, &hkey) != ERROR_SUCCESS)
        return;

    if(RegQueryValueExA(hkey, "InstallUpdateCheck", NULL, &type, (LPBYTE)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
    {
        NgsaUpdate::SetEnabled(value ? wyTrue : wyFalse);
        RegDeleteValueA(hkey, "InstallUpdateCheck");
        NgsaTrace("install defaults: UpdateCheck=%lu applied", value);
    }

    RegCloseKey(hkey);
}
