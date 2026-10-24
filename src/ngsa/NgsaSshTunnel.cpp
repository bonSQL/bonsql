/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: SSH port-forward tunnel via stock PuTTY plink.exe.
 *
 * Command line used (all values from ConnectionInfo):
 *   plink -ssh -batch -N -v -l <user> -P <sshport> -L <localport>:<mysqlhost>:<mysqlport>
 *         [-pwfile <tmp>] [-i <key.ppk>] [-hostkey <SHA256:...>] <sshhost>
 *
 * Readiness: the forward is up when 127.0.0.1:<localport> accepts a TCP connection.
 * Errors: plink's stderr (-v) is captured and turned into a readable message.
 * Host keys: unknown hosts are offered to the user once; accepted fingerprints are kept in
 *   bonsql.ini [SshHostKeys] "<host>:<port>" = SHA256:... and passed back with -hostkey.
 * Lifetime: plink lives in a kill-on-close job object, so it dies with SQLyog; normal
 *   teardown is TerminateProcess(m_hprocess) by upstream MDIWindow code or NgsaSsh::Stop.
 */
#ifdef NGSA_SSH_SSL

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "ngsa/NgsaSshTunnel.h"
#include "ngsa/NgsaConfig.h"
#include "CommonHelper.h"
#include "FrameWindowHelper.h"
#include "wyIni.h"

extern PGLOBALS pGlobals;

namespace
{
    /* stored user data: [SshHostKeys] in bonsql.ini; the SQLyog import renames the old [IctSshHostKeys], so trusted keys carry over (NgsaConfig.cpp) */
    const wyChar* const HOSTKEY_SECTION  = NGSA_HOSTKEY_SECTION;
    const wyChar* const LOOPBACK         = "127.0.0.1";
    const DWORD         START_TIMEOUT_MS = 30000;
    const DWORD         POLL_MS          = 200;

    HANDLE s_job = NULL;

    HANDLE KillOnCloseJob()
    {
        if(!s_job)
        {
            s_job = CreateJobObject(NULL, NULL);
            if(s_job)
            {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
                memset(&li, 0, sizeof(li));
                li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(s_job, JobObjectExtendedLimitInformation, &li, sizeof(li));
            }
        }
        return s_job;
    }

    void EnsureWinsock()
    {
        static bool done = false;
        if(!done)
        {
            WSADATA d;
            WSAStartup(MAKEWORD(2, 2), &d);
            done = true;
        }
    }

    wyBool IniPath(wyString& path)
    {
        wyWChar directory[MAX_PATH + 1] = {0}, *lpfileport = 0;
        if(SearchFilePath(BRAND_CFG_STEM_W, L".ini", MAX_PATH, directory, &lpfileport) == 0)
            return wyFalse;
        path.SetAs(directory);
        return wyTrue;
    }

    wyBool FileExists(const wyWChar* path)
    {
        DWORD a = GetFileAttributesW(path);
        return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) ? wyTrue : wyFalse;
    }

    /* Bind 127.0.0.1:0, read the port back, release it. Small race window accepted. */
    wyInt32 PickLocalPort()
    {
        EnsureWinsock();
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(s == INVALID_SOCKET)
            return 0;

        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port        = 0;

        wyInt32 port = 0;
        int     len  = sizeof(a);
        if(bind(s, (sockaddr*)&a, sizeof(a)) == 0 && getsockname(s, (sockaddr*)&a, &len) == 0)
            port = ntohs(a.sin_port);

        closesocket(s);
        return port;
    }

    /* Non-blocking connect to 127.0.0.1:port with a short wait. */
    wyBool PortOpen(wyInt32 port)
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(s == INVALID_SOCKET)
            return wyFalse;

        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);

        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port        = htons((u_short)port);

        wyBool ok = wyFalse;
        if(connect(s, (sockaddr*)&a, sizeof(a)) == 0)
        {
            ok = wyTrue;
        }
        else if(WSAGetLastError() == WSAEWOULDBLOCK)
        {
            fd_set wfds, efds;
            FD_ZERO(&wfds); FD_ZERO(&efds);
            FD_SET(s, &wfds); FD_SET(s, &efds);
            timeval tv = {0, 300000};
            if(select(0, NULL, &wfds, &efds, &tv) > 0 && FD_ISSET(s, &wfds))
                ok = wyTrue;
        }

        closesocket(s);
        return ok;
    }

    void DrainPipe(HANDLE hread, std::string& log)
    {
        DWORD avail = 0;
        while(hread != INVALID_HANDLE_VALUE && PeekNamedPipe(hread, NULL, 0, NULL, &avail, NULL) && avail > 0)
        {
            char  buf[2048];
            DWORD got = 0;
            if(!ReadFile(hread, buf, sizeof(buf) - 1, &got, NULL) || got == 0)
                break;
            log.append(buf, got);
        }
    }

    void CloseIfValid(HANDLE& h)
    {
        if(h != INVALID_HANDLE_VALUE && h != NULL)
            CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }

    std::string LineContaining(const std::string& log, const char* needle)
    {
        size_t p = log.find(needle);
        if(p == std::string::npos)
            return std::string();
        size_t s = log.rfind('\n', p);
        s = (s == std::string::npos) ? 0 : s + 1;
        size_t e = log.find_first_of("\r\n", p);
        if(e == std::string::npos) e = log.size();
        return log.substr(s, e - s);
    }

    std::string LastLines(const std::string& log, int n)
    {
        size_t pos = log.size();
        while(n-- > 0 && pos > 0)
        {
            size_t p = log.rfind('\n', pos - 1);
            if(p == std::string::npos) { pos = 0; break; }
            pos = p;
        }
        return log.substr(pos);
    }

    /* "SHA256:" + base64 token from plink's host-key text */
    std::string ExtractFingerprint(const std::string& log)
    {
        size_t p = log.find("SHA256:");
        if(p == std::string::npos)
            return std::string();
        size_t e = p;
        while(e < log.size() && !isspace((unsigned char)log[e]))
            ++e;
        return log.substr(p, e - p);
    }

    wyBool WritePasswordFile(wyString& pwd, wyString& path, wyString& err)
    {
        wyWChar tmpdir[MAX_PATH + 1] = {0}, tmpfile[MAX_PATH + 1] = {0};

        if(!GetTempPathW(MAX_PATH, tmpdir) || !GetTempFileNameW(tmpdir, L"ngs", 0, tmpfile))
        {
            err.SetAs("SSH: could not create a temporary file for the password");
            return wyFalse;
        }

        HANDLE h = CreateFileW(tmpfile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, NULL);
        if(h == INVALID_HANDLE_VALUE)
        {
            err.SetAs("SSH: could not write the temporary password file");
            return wyFalse;
        }

        DWORD w = 0;
        WriteFile(h, pwd.GetString(), pwd.GetLength(), &w, NULL);
        CloseHandle(h);
        path.SetAs(tmpfile);
        return wyTrue;
    }

    /* Append ` "value"`; plink gets its argv from the CRT, so a bare double quote is refused. */
    wyBool AppendQuoted(wyString& cmd, wyString& value, wyString& err)
    {
        if(strchr(value.GetString(), '"'))
        {
            err.Sprintf("SSH: a value contains a double quote, which cannot be passed to plink: %s", value.GetString());
            return wyFalse;
        }
        cmd.Add(" \"");
        cmd.Add(value.GetString());
        cmd.Add("\"");
        return wyTrue;
    }

    void HostKeyIniKey(ConnectionInfo* ci, wyString& key)
    {
        key.Sprintf("%s:%d", ci->m_sshhost.GetString(), ci->m_sshport);
    }

    /* ---------------------------------------------------------------------- */

    wyBool StartInternal(ConnectionInfo* ci, HWND owner, wyString& errmsg,
                         const wyChar* hostkey, wyBool allowprompt)
    {
        wyString    plink, cmd, pwfile, tmp, inipath, keyname, fp;
        wyInt32     localport;
        std::string log;

        errmsg.Clear();

        if(!ci->m_sshhost.GetLength())
        {
            errmsg.SetAs("SSH: the SSH host address is empty");
            return wyFalse;
        }
        if(!ci->m_sshuser.GetLength())
        {
            errmsg.SetAs("SSH: the SSH user name is empty");
            return wyFalse;
        }
        if(ci->m_sshport <= 0)
            ci->m_sshport = 22;
        if(ci->m_ispassword == wyFalse && !ci->m_privatekeypath.GetLength())
        {
            errmsg.SetAs("SSH: private key authentication is selected but no key file is given");
            return wyFalse;
        }
        if(strpbrk(ci->m_host.GetString(), " \t\"") || strpbrk(ci->m_sshhost.GetString(), " \t\""))
        {
            errmsg.SetAs("SSH: host names must not contain spaces or quotes");
            return wyFalse;
        }

        if(NgsaSsh::FindPlink(plink) == wyFalse)
        {
            errmsg.SetAs("SSH: plink.exe was not found next to bonSQL.exe, in Program Files\\PuTTY or on the PATH");
            return wyFalse;
        }

        localport = PickLocalPort();
        if(!localport)
        {
            errmsg.SetAs("SSH: could not find a free local port for the tunnel");
            return wyFalse;
        }

        /* known host key from a previous "trust this host" */
        if(!hostkey && IniPath(inipath) == wyTrue)
        {
            HostKeyIniKey(ci, keyname);
            wyIni::IniGetString(HOSTKEY_SECTION, keyname.GetString(), "", &fp, inipath.GetString());
            if(fp.GetLength())
                hostkey = fp.GetString();
        }

        /* ---- command line ---- */
        cmd.Sprintf("\"%s\" -ssh -batch -N -v -P %d", plink.GetString(), ci->m_sshport);

        cmd.Add(" -l");
        if(AppendQuoted(cmd, ci->m_sshuser, errmsg) == wyFalse) return wyFalse;

        tmp.Sprintf(" -L %d:%s:%d", localport,
                    ci->m_host.GetLength() ? ci->m_host.GetString() : "localhost", ci->m_port);
        cmd.Add(tmp.GetString());

        if(ci->m_sshpwd.GetLength())
        {
            if(WritePasswordFile(ci->m_sshpwd, pwfile, errmsg) == wyFalse)
                return wyFalse;
            cmd.Add(" -pwfile");
            AppendQuoted(cmd, pwfile, errmsg);
        }

        if(ci->m_ispassword == wyFalse)
        {
            cmd.Add(" -i");
            if(AppendQuoted(cmd, ci->m_privatekeypath, errmsg) == wyFalse)
            {
                if(pwfile.GetLength()) DeleteFileW(pwfile.GetAsWideChar());
                return wyFalse;
            }
        }

        if(hostkey && *hostkey)
        {
            tmp.SetAs(hostkey);
            cmd.Add(" -hostkey");
            AppendQuoted(cmd, tmp, errmsg);
        }

        if(AppendQuoted(cmd, ci->m_sshhost, errmsg) == wyFalse)
        {
            if(pwfile.GetLength()) DeleteFileW(pwfile.GetAsWideChar());
            return wyFalse;
        }

        /* ---- pipes ---- */
        SECURITY_ATTRIBUTES sa;
        memset(&sa, 0, sizeof(sa));
        sa.nLength        = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE hStdinRead = INVALID_HANDLE_VALUE, hStdinWrite = INVALID_HANDLE_VALUE;
        HANDLE hOutRead   = INVALID_HANDLE_VALUE, hOutWrite   = INVALID_HANDLE_VALUE;

        if(!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0) || !CreatePipe(&hOutRead, &hOutWrite, &sa, 0))
        {
            errmsg.SetAs("SSH: could not create pipes for plink");
            if(pwfile.GetLength()) DeleteFileW(pwfile.GetAsWideChar());
            CloseIfValid(hStdinRead); CloseIfValid(hStdinWrite); CloseIfValid(hOutRead); CloseIfValid(hOutWrite);
            return wyFalse;
        }
        SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hOutRead,    HANDLE_FLAG_INHERIT, 0);

        /* ---- spawn ---- */
        STARTUPINFOW        si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput   = hStdinRead;
        si.hStdOutput  = hOutWrite;
        si.hStdError   = hOutWrite;

        std::vector<wyWChar> cmdline;
        {
            wyWChar* w = cmd.GetAsWideChar();
            cmdline.assign(w, w + wcslen(w) + 1);
        }

        BOOL ok = CreateProcessW(NULL, &cmdline[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        DWORD spawnerr = ok ? 0 : GetLastError();

        /* child's ends are not ours anymore */
        CloseIfValid(hStdinRead);
        CloseIfValid(hOutWrite);

        if(!ok)
        {
            errmsg.Sprintf("SSH: could not start plink (%s), error %lu", plink.GetString(), spawnerr);
            if(pwfile.GetLength()) DeleteFileW(pwfile.GetAsWideChar());
            CloseIfValid(hStdinWrite); CloseIfValid(hOutRead);
            return wyFalse;
        }

        if(KillOnCloseJob())
            AssignProcessToJobObject(s_job, pi.hProcess);
        CloseHandle(pi.hThread);

        /* ---- wait for the forward or for plink to give up ---- */
        wyBool ready = wyFalse, exited = wyFalse;
        DWORD  t0 = GetTickCount();

        while(GetTickCount() - t0 < START_TIMEOUT_MS)
        {
            DrainPipe(hOutRead, log);

            if(WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            {
                Sleep(50);
                DrainPipe(hOutRead, log);
                exited = wyTrue;
                break;
            }
            if(PortOpen(localport) == wyTrue)
            {
                ready = wyTrue;
                break;
            }
            Sleep(POLL_MS);
        }

        if(pwfile.GetLength())
            DeleteFileW(pwfile.GetAsWideChar());

        if(ready == wyTrue)
        {
            ci->m_localhost.SetAs(LOOPBACK);
            ci->m_localport     = localport;
            ci->m_origmysqlport = ci->m_port;
            ci->m_hprocess      = pi.hProcess;
            ci->m_sshsocket     = NULL;
            ci->m_sshpipeends.m_hreadpipe   = hOutRead;       /* plink stdout+stderr */
            ci->m_sshpipeends.m_hwritepipe  = INVALID_HANDLE_VALUE;
            ci->m_sshpipeends.m_hreadpipe2  = INVALID_HANDLE_VALUE;
            ci->m_sshpipeends.m_hwritepipe2 = hStdinWrite;    /* plink stdin */
            return wyTrue;
        }

        /* ---- failure ---- */
        DWORD exitcode = 0;
        if(exited == wyFalse)
            TerminateProcess(pi.hProcess, 1);
        GetExitCodeProcess(pi.hProcess, &exitcode);
        CloseHandle(pi.hProcess);
        CloseIfValid(hStdinWrite);
        CloseIfValid(hOutRead);

        std::string line;

        if(log.find("host key is not cached") != std::string::npos)
        {
            std::string fpr = ExtractFingerprint(log);
            if(allowprompt == wyTrue && !hostkey && !fpr.empty())
            {
                wyString msg;
                msg.Sprintf("The SSH host key for %s (port %d) is not known yet.\n\n"
                            "%s\n\nIf you trust this host, bonSQL will remember the fingerprint "
                            "for this host in bonsql.ini and connect.\n\nTrust this host?",
                            ci->m_sshhost.GetString(), ci->m_sshport,
                            LineContaining(log, "SHA256:").c_str());

                if(yog_message(owner, msg.GetAsWideChar(), pGlobals->m_appname.GetAsWideChar(),
                               MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
                {
                    if(IniPath(inipath) == wyTrue)
                    {
                        HostKeyIniKey(ci, keyname);
                        wyIni::IniWriteString(HOSTKEY_SECTION, keyname.GetString(), fpr.c_str(), inipath.GetString());
                    }
                    return StartInternal(ci, owner, errmsg, fpr.c_str(), wyFalse);
                }
                errmsg.SetAs("SSH: connection cancelled, host key not trusted");
                return wyFalse;
            }
            errmsg.Sprintf("SSH: the host key of %s is not known and could not be confirmed.\n%s",
                           ci->m_sshhost.GetString(), LineContaining(log, "SHA256:").c_str());
            return wyFalse;
        }

        if(log.find("POTENTIAL SECURITY BREACH") != std::string::npos ||
           log.find("does not match") != std::string::npos)
        {
            errmsg.Sprintf("SSH: the host key of %s has CHANGED since it was trusted.\n\n"
                           "If this is expected, remove the entry \"%s:%d\" from the [" NGSA_HOSTKEY_SECTION "] "
                           "section of bonsql.ini (and PuTTY's cache) and connect again.\n\n%s",
                           ci->m_sshhost.GetString(), ci->m_sshhost.GetString(), ci->m_sshport,
                           LineContaining(log, "SHA256:").c_str());
            return wyFalse;
        }

        if(log.find("Access denied") != std::string::npos || log.find("Server refused our key") != std::string::npos)
        {
            errmsg.Sprintf("SSH: login as %s@%s failed (%s).\nCheck the SSH user name and the %s.",
                           ci->m_sshuser.GetString(), ci->m_sshhost.GetString(),
                           log.find("Access denied") != std::string::npos ? "access denied" : "server refused the key",
                           ci->m_ispassword == wyTrue ? "password" : "private key / passphrase");
            return wyFalse;
        }

        static const char* netphrases[] = { "Unable to open connection", "Network error", "Connection refused",
                                            "Connection timed out", "Unable to use key file", "Unable to load",
                                            "FATAL ERROR", "Disconnected:" };
        for(size_t i = 0; i < sizeof(netphrases) / sizeof(netphrases[0]); ++i)
        {
            line = LineContaining(log, netphrases[i]);
            if(!line.empty())
            {
                errmsg.Sprintf("SSH: %s", line.c_str());
                return wyFalse;
            }
        }

        if(exited == wyFalse)
            errmsg.Sprintf("SSH: plink did not establish the port forward within %lu seconds.\n\n%s",
                           START_TIMEOUT_MS / 1000, LastLines(log, 8).c_str());
        else
            errmsg.Sprintf("SSH: plink exited with code %lu.\n\n%s", exitcode, LastLines(log, 8).c_str());
        return wyFalse;
    }
}

/* ------------------------------------------------------------------------- */

wyBool
NgsaSsh::FindPlink(wyString& path)
{
    wyWChar buf[MAX_PATH + 1] = {0};

    if(GetModuleFileNameW(NULL, buf, MAX_PATH))
    {
        wyWChar* slash = wcsrchr(buf, L'\\');
        if(slash)
        {
            wcscpy(slash + 1, L"plink.exe");
            if(FileExists(buf) == wyTrue) { path.SetAs(buf); return wyTrue; }
        }
    }

    if(SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, buf)))
    {
        wcscat(buf, L"\\PuTTY\\plink.exe");
        if(FileExists(buf) == wyTrue) { path.SetAs(buf); return wyTrue; }
    }

    if(SearchPathW(NULL, L"plink.exe", NULL, MAX_PATH, buf, NULL))
    {
        path.SetAs(buf);
        return wyTrue;
    }

    return wyFalse;
}

wyBool
NgsaSsh::Start(ConnectionInfo* ci, HWND owner, wyString& errmsg)
{
    if(!ci)
        return wyFalse;
    if(!owner && pGlobals && pGlobals->m_pcmainwin)
        owner = pGlobals->m_pcmainwin->m_hwndmain;
    return StartInternal(ci, owner, errmsg, NULL, wyTrue);
}

void
NgsaSsh::Stop(ConnectionInfo* ci)
{
    if(!ci)
        return;

    if(ci->m_hprocess != INVALID_HANDLE_VALUE && ci->m_hprocess != NULL)
    {
        TerminateProcess(ci->m_hprocess, 1);
        CloseHandle(ci->m_hprocess);
    }
    ci->m_hprocess = INVALID_HANDLE_VALUE;

    CloseIfValid(ci->m_sshpipeends.m_hreadpipe);
    CloseIfValid(ci->m_sshpipeends.m_hwritepipe);
    CloseIfValid(ci->m_sshpipeends.m_hreadpipe2);
    CloseIfValid(ci->m_sshpipeends.m_hwritepipe2);

    if(ci->m_sshsocket)
        closesocket(ci->m_sshsocket);
    ci->m_sshsocket = NULL;
    ci->m_localport = 0;
}

wyBool
NgsaSsh::Restart(ConnectionInfo* ci, HWND owner, wyString& errmsg)
{
    Stop(ci);
    return Start(ci, owner, errmsg);
}

#endif /* NGSA_SSH_SSL */
