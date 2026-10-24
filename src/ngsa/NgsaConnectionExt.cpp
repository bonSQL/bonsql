/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build extension: SSH tunnelling and SSL for the Community connection dialog.
 * Dialog glue + persistence + connect-path entry points. The tunnel itself is in
 * NgsaSshTunnel.cpp.
 */
#ifdef NGSA_SSH_SSL

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include "ngsa/NgsaConnectionExt.h"
#include "ngsa/NgsaSshTunnel.h"
#include "resource.h"
#include "wyIni.h"
#include "CommonHelper.h"       /* SetSslAuthentication, EncodePassword, DecodePassword */
#include "FrameWindowHelper.h"  /* yog_message, PPKFILE */
#include "SQLTokenizer.h"       /* ReplayInitCommands */
#include "Tunnel.h"
#include "ngsa/NgsaTrace.h"

extern PGLOBALS pGlobals;

namespace
{
    /* ---- SSL tab ---- */
    const wyInt32 s_sslDependents[] = {
        IDC_SSLAUTH, IDC_SSLCACER, IDC_CACER, IDC_SSLCAFILE, IDC_SSLCIPHER, IDC_CIPHER
    };
    const wyInt32 s_sslAuthDependents[] = {
        IDC_SSLCLIENTKEY, IDC_CLIENTKEY, IDC_SSLKEYFILE,
        IDC_SSLCLIENTCER, IDC_CLIENTCER, IDC_SSLCERFILE
    };

    /* ---- SSH tab ---- */
    const wyInt32 s_sshDependents[] = {
        IDC_SSHGRP, IDC_SSHPASSGRP, IDC_SSHHOSTST, IDC_SSHHOST, IDC_SSHUSERST, IDC_SSHUSER,
        IDC_SSHPORTST, IDC_SSHPORT, IDC_SSHPASSWORDRADIO, IDC_SSHPRIVATERADIO,
        IDC_SSHPWDST, IDC_SSHPWD, IDC_SSHSAVEPHRASE
    };
    const wyInt32 s_sshKeyDependents[] = {
        IDC_SSHPRIVATEKEYTITLE, IDC_SSHPRIVATEKEY, IDC_SSHPRIVATEKEYFILE
    };

    const wyWChar* const s_certFilter =
        L"Certificate / key files (*.pem;*.crt;*.cer;*.key)\0*.pem;*.crt;*.cer;*.key\0All Files (*.*)\0*.*\0";

    void EnableIds(HWND hdlg, const wyInt32* ids, size_t count, wyBool enable)
    {
        for(size_t i = 0; i < count; ++i)
            EnableWindow(GetDlgItem(hdlg, ids[i]), enable == wyTrue ? TRUE : FALSE);
    }

    wyBool IsChecked(HWND hdlg, wyInt32 id)
    {
        return Button_GetCheck(GetDlgItem(hdlg, id)) == BST_CHECKED ? wyTrue : wyFalse;
    }

    void SetChecked(HWND hdlg, wyInt32 id, wyBool on)
    {
        Button_SetCheck(GetDlgItem(hdlg, id), on == wyTrue ? BST_CHECKED : BST_UNCHECKED);
    }

    void GetText(HWND hdlg, wyInt32 id, wyString& out)
    {
        wyWChar buf[SIZE_2048] = {0};
        GetDlgItemText(hdlg, id, buf, SIZE_2048 - 1);
        out.SetAs(buf);
        out.RTrim();
    }

    void SetText(HWND hdlg, wyInt32 id, wyString& value)
    {
        SetDlgItemText(hdlg, id, value.GetAsWideChar());
    }

    void UpdateSslControls(HWND hdlg)
    {
        wyBool ssl  = IsChecked(hdlg, IDC_SSL);
        wyBool auth = (ssl == wyTrue && IsChecked(hdlg, IDC_SSLAUTH) == wyTrue) ? wyTrue : wyFalse;

        EnableWindow(GetDlgItem(hdlg, IDC_SSL), TRUE);
        EnableWindow(GetDlgItem(hdlg, IDC_SSLHELP), TRUE);
        EnableIds(hdlg, s_sslDependents, _countof(s_sslDependents), ssl);
        EnableIds(hdlg, s_sslAuthDependents, _countof(s_sslAuthDependents), auth);
    }

    void UpdateSshControls(HWND hdlg)
    {
        wyBool ssh = IsChecked(hdlg, IDC_SSH);
        wyBool key = (ssh == wyTrue && IsChecked(hdlg, IDC_SSHPRIVATERADIO) == wyTrue) ? wyTrue : wyFalse;

        EnableWindow(GetDlgItem(hdlg, IDC_SSH), TRUE);
        EnableWindow(GetDlgItem(hdlg, IDC_SSHHELP), TRUE);
        EnableIds(hdlg, s_sshDependents, _countof(s_sshDependents), ssh);
        EnableIds(hdlg, s_sshKeyDependents, _countof(s_sshKeyDependents), key);
        SetDlgItemText(hdlg, IDC_SSHPWDST, key == wyTrue ? L"Pass&phrase" : L"&Password");
    }

    wyBool BrowseIntoEdit(HWND hdlg, wyInt32 targetid, const wyWChar* title, const wyWChar* filter)
    {
        OPENFILENAME    ofn;
        wyWChar         filename[MAX_PATH + 1] = {0};

        GetDlgItemText(hdlg, targetid, filename, MAX_PATH);

        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize     = OPENFILENAME_SIZE_VERSION_400;
        ofn.hwndOwner       = hdlg;
        ofn.hInstance       = pGlobals ? pGlobals->m_hinstance : NULL;
        ofn.lpstrFilter     = filter;
        ofn.nFilterIndex    = 1;
        ofn.lpstrFile       = filename;
        ofn.nMaxFile        = MAX_PATH;
        ofn.lpstrTitle      = title;
        ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

        if(!GetOpenFileName(&ofn))
            return wyFalse;

        SetDlgItemText(hdlg, targetid, filename);
        return wyTrue;
    }
}

/* ------------------------------------------------------------------------- */

wyBool
NgsaConn::OnCommand(HWND hdlg, WPARAM wparam, LPARAM lparam, wyBool* pdirty)
{
    wyInt32 id   = LOWORD(wparam);
    wyInt32 code = HIWORD(wparam);
    wyBool  changed = wyFalse;

    if(pdirty)
        *pdirty = wyFalse;

    if(code != BN_CLICKED)
        return wyFalse;

    switch(id)
    {
    /* SSL */
    case IDC_SSL:
    case IDC_SSLAUTH:
        UpdateSslControls(hdlg);
        changed = wyTrue;
        break;
    case IDC_SSLCAFILE:
        changed = BrowseIntoEdit(hdlg, IDC_CACER, L"Select CA certificate", s_certFilter);
        break;
    case IDC_SSLKEYFILE:
        changed = BrowseIntoEdit(hdlg, IDC_CLIENTKEY, L"Select client key", s_certFilter);
        break;
    case IDC_SSLCERFILE:
        changed = BrowseIntoEdit(hdlg, IDC_CLIENTCER, L"Select client certificate", s_certFilter);
        break;

    /* SSH */
    case IDC_SSH:
    case IDC_SSHPASSWORDRADIO:
    case IDC_SSHPRIVATERADIO:
        UpdateSshControls(hdlg);
        changed = wyTrue;
        break;
    case IDC_SSHSAVEPHRASE:
        changed = wyTrue;
        break;
    case IDC_SSHPRIVATEKEYFILE:
        changed = BrowseIntoEdit(hdlg, IDC_SSHPRIVATEKEY, L"Select PuTTY private key (.ppk)", PPKFILE);
        break;

    default:
        return wyFalse;
    }

    if(pdirty)
        *pdirty = changed;
    return wyTrue;
}

void
NgsaConn::ClearDialog(HWND hdlg)
{
    SetChecked(hdlg, IDC_SSL, wyFalse);
    SetChecked(hdlg, IDC_SSLAUTH, wyFalse);
    SetDlgItemText(hdlg, IDC_CACER, L"");
    SetDlgItemText(hdlg, IDC_CIPHER, L"");
    SetDlgItemText(hdlg, IDC_CLIENTKEY, L"");
    SetDlgItemText(hdlg, IDC_CLIENTCER, L"");
    UpdateSslControls(hdlg);

    SetChecked(hdlg, IDC_SSH, wyFalse);
    SetDlgItemText(hdlg, IDC_SSHHOST, L"");
    SetDlgItemText(hdlg, IDC_SSHUSER, L"");
    SetDlgItemText(hdlg, IDC_SSHPORT, L"22");
    SetChecked(hdlg, IDC_SSHPASSWORDRADIO, wyTrue);
    SetChecked(hdlg, IDC_SSHPRIVATERADIO, wyFalse);
    SetDlgItemText(hdlg, IDC_SSHPWD, L"");
    SetChecked(hdlg, IDC_SSHSAVEPHRASE, wyTrue);
    SetDlgItemText(hdlg, IDC_SSHPRIVATEKEY, L"");
    UpdateSshControls(hdlg);
}

void
NgsaConn::ReadFromDialog(HWND hdlg, ConnectionInfo* ci)
{
    wyString tmp;

    if(!ci)
        return;

    /* SSL */
    ci->m_issslchecked     = IsChecked(hdlg, IDC_SSL);
    ci->m_issslauthchecked = (ci->m_issslchecked == wyTrue && IsChecked(hdlg, IDC_SSLAUTH) == wyTrue) ? wyTrue : wyFalse;
    GetText(hdlg, IDC_CACER,     ci->m_cacert);
    GetText(hdlg, IDC_CIPHER,    ci->m_cipher);
    GetText(hdlg, IDC_CLIENTKEY, ci->m_clikey);
    GetText(hdlg, IDC_CLIENTCER, ci->m_clicert);
    ci->m_no_ca = ci->m_cacert.GetLength() ? wyFalse : wyTrue;

    /* SSH */
    ci->m_isssh = IsChecked(hdlg, IDC_SSH);
    GetText(hdlg, IDC_SSHHOST, ci->m_sshhost);
    GetText(hdlg, IDC_SSHUSER, ci->m_sshuser);
    GetText(hdlg, IDC_SSHPORT, tmp);
    ci->m_sshport = atoi(tmp.GetString());
    if(ci->m_sshport <= 0)
        ci->m_sshport = 22;
    ci->m_ispassword = IsChecked(hdlg, IDC_SSHPRIVATERADIO) == wyTrue ? wyFalse : wyTrue;
    GetText(hdlg, IDC_SSHPWD, ci->m_sshpwd);
    ci->m_issshsavepassword = IsChecked(hdlg, IDC_SSHSAVEPHRASE);
    GetText(hdlg, IDC_SSHPRIVATEKEY, ci->m_privatekeypath);
}

/* ini key names deliberately match Ultimate's session/export format
   (see GetSessionDetails in GUIHelper.cpp): SslChecked, SshAuth (sic: the SSL
   client-auth flag), CA, Cipher, Client_Key, Client_Cert, SSH, SshHost, SshUser,
   SshPort, SshPwd, SshPasswordRadio, SSHPrivateKeyPath, SshSavePassword. */

void
NgsaConn::LoadFromIni(HWND hdlg, const wyChar* conn, const wyChar* dir)
{
    wyString value;

    /* SSL */
    SetChecked(hdlg, IDC_SSL,     wyIni::IniGetInt(conn, "SslChecked", 0, dir) ? wyTrue : wyFalse);
    SetChecked(hdlg, IDC_SSLAUTH, wyIni::IniGetInt(conn, "SshAuth", 0, dir) ? wyTrue : wyFalse);
    wyIni::IniGetString(conn, "CA", "", &value, dir);          SetText(hdlg, IDC_CACER, value);
    wyIni::IniGetString(conn, "Cipher", "", &value, dir);      SetText(hdlg, IDC_CIPHER, value);
    wyIni::IniGetString(conn, "Client_Key", "", &value, dir);  SetText(hdlg, IDC_CLIENTKEY, value);
    wyIni::IniGetString(conn, "Client_Cert", "", &value, dir); SetText(hdlg, IDC_CLIENTCER, value);
    UpdateSslControls(hdlg);

    /* SSH */
    SetChecked(hdlg, IDC_SSH, wyIni::IniGetInt(conn, "SSH", 0, dir) ? wyTrue : wyFalse);
    wyIni::IniGetString(conn, "SshHost", "", &value, dir);           SetText(hdlg, IDC_SSHHOST, value);
    wyIni::IniGetString(conn, "SshUser", "", &value, dir);           SetText(hdlg, IDC_SSHUSER, value);
    value.Sprintf("%d", wyIni::IniGetInt(conn, "SshPort", 22, dir)); SetText(hdlg, IDC_SSHPORT, value);
    wyIni::IniGetString(conn, "SSHPrivateKeyPath", "", &value, dir); SetText(hdlg, IDC_SSHPRIVATEKEY, value);

    wyBool pwdauth = wyIni::IniGetInt(conn, "SshPasswordRadio", 1, dir) ? wyTrue : wyFalse;
    SetChecked(hdlg, IDC_SSHPASSWORDRADIO, pwdauth);
    SetChecked(hdlg, IDC_SSHPRIVATERADIO, pwdauth == wyTrue ? wyFalse : wyTrue);
    SetChecked(hdlg, IDC_SSHSAVEPHRASE, wyIni::IniGetInt(conn, "SshSavePassword", 1, dir) ? wyTrue : wyFalse);

    wyIni::IniGetString(conn, "SshPwd", "", &value, dir);
    if(value.GetLength())
    {
        wyString::DecodeBase64Password(value);
        DecodePassword(value);
    }
    SetText(hdlg, IDC_SSHPWD, value);

    UpdateSshControls(hdlg);
}

void
NgsaConn::SaveToIni(HWND hdlg, const wyChar* conn, const wyChar* dir)
{
    ConnectionInfo ci;

    ReadFromDialog(hdlg, &ci);

    /* SSL */
    wyIni::IniWriteInt(conn, "SslChecked", ci.m_issslchecked == wyTrue ? 1 : 0, dir);
    wyIni::IniWriteInt(conn, "SshAuth",    ci.m_issslauthchecked == wyTrue ? 1 : 0, dir);
    wyIni::IniWriteString(conn, "CA",          ci.m_cacert.GetString(),  dir);
    wyIni::IniWriteString(conn, "Cipher",      ci.m_cipher.GetString(),  dir);
    wyIni::IniWriteString(conn, "Client_Key",  ci.m_clikey.GetString(),  dir);
    wyIni::IniWriteString(conn, "Client_Cert", ci.m_clicert.GetString(), dir);

    /* SSH */
    wyIni::IniWriteInt(conn, "SSH", ci.m_isssh == wyTrue ? 1 : 0, dir);
    wyIni::IniWriteString(conn, "SshHost", ci.m_sshhost.GetString(), dir);
    wyIni::IniWriteString(conn, "SshUser", ci.m_sshuser.GetString(), dir);
    wyIni::IniWriteInt(conn, "SshPort", ci.m_sshport, dir);
    wyIni::IniWriteInt(conn, "SshPasswordRadio", ci.m_ispassword == wyTrue ? 1 : 0, dir);
    wyIni::IniWriteInt(conn, "SshSavePassword", ci.m_issshsavepassword == wyTrue ? 1 : 0, dir);
    wyIni::IniWriteString(conn, "SSHPrivateKeyPath", ci.m_privatekeypath.GetString(), dir);

    if(ci.m_issshsavepassword == wyTrue && ci.m_sshpwd.GetLength())
    {
        /* same scheme as the MySQL password (Isencrypted=1) */
        EncodePassword(ci.m_sshpwd);
        wyChar* encoded = ci.m_sshpwd.EncodeBase64Password();
        wyIni::IniWriteString(conn, "SshPwd", encoded ? encoded : "", dir);
        if(encoded)
            free(encoded);
    }
    else
    {
        wyIni::IniWriteString(conn, "SshPwd", "", dir);
    }
}

/* ------------------------------------------------------------------------- */

void
NgsaConn::ApplyTlsOptions(ConnectionInfo* ci, Tunnel* tunnel, MYSQL* mysql)
{
    /* MariaDB Connector/C 3.4 (verified in its source, plugins/auth/my_auth.c, 09/10/2026):
       server-certificate verification is ON by default and switches TLS on, so a bare
       mysql_init() handle fails against a server without TLS ("SSL is required, but the
       server does not support it") or with a self-signed certificate (error 2026). Turning
       verification off (MYSQL_OPT_SSL_VERIFY_SERVER_CERT = 0) leaves TLS off unless it is
       enforced or certificates are given. Hence the SSL box means:
         unticked        = plain connection (even if the server offers TLS)
         ticked, no CA   = TLS required, certificate not verified
         ticked with CA  = TLS required, certificate verified against the CA (client cert/key
                           and cipher as given)
       Every place that opens a connection from a ConnectionInfo must call this, otherwise the
       connector's defaults apply and the connection behaves differently from the main one. */
    my_bool on = 1, off = 0;

    if(!ci || !mysql)
        return;

    /* #2226: the wrapper replays these after a reconnect it detects (see Tunnel.h) */
    if(tunnel)
        tunnel->m_ngsainitcommand.SetAs(ci->m_initcommand);

    if(ci->m_issslchecked == wyTrue)
    {
        /* upstream helper: mysql_ssl_set with CA / client cert / cipher as given */
        SetSslAuthentication(mysql, ci);
        /* Decision (docs/ngsa/SSH-SSL-FEASIBILITY.md): when SSL is ticked the connection
           must be encrypted; never fall back to plain. */
        mysql_options(mysql, MYSQL_OPT_SSL_ENFORCE, &on);
    }
    mysql_options(mysql, MYSQL_OPT_SSL_VERIFY_SERVER_CERT,
                  (ci->m_issslchecked == wyTrue && ci->m_cacert.GetLength()) ? &on : &off);
}

wyBool
NgsaConn::PrepareConnection(ConnectionInfo* ci, Tunnel* tunnel, MYSQL* mysql, wyString& errmsg)
{
    errmsg.Clear();

    if(!ci || !mysql)
        return wyTrue;

    ApplyTlsOptions(ci, tunnel, mysql);

    if(ci->m_isssh == wyTrue)
    {
        /* never reuse handles copied from another window; this connection gets its own tunnel */
        ci->m_hprocess  = INVALID_HANDLE_VALUE;
        ci->m_localport = 0;
        ci->m_sshpipeends.m_hreadpipe   = INVALID_HANDLE_VALUE;
        ci->m_sshpipeends.m_hwritepipe  = INVALID_HANDLE_VALUE;
        ci->m_sshpipeends.m_hreadpipe2  = INVALID_HANDLE_VALUE;
        ci->m_sshpipeends.m_hwritepipe2 = INVALID_HANDLE_VALUE;

        if(NgsaSsh::Start(ci, GetActiveWindow(), errmsg) == wyFalse)
            return wyFalse;
    }

    return wyTrue;
}

void
NgsaConn::ReplayInitCommands(wyString& initcommand, Tunnel* tunnel, MYSQL* mysql)
{
    /* Upstream #2226 (reproduced 09/10/2026 on MySQL 8.4 and MariaDB 11.8): Connector/C's
       auto-reconnect (mariadb_reconnect) re-runs the MYSQL_INIT_COMMAND list and THEN calls
       mysql_set_character_set(), i.e. sends SET NAMES <charset>, which resets collation_connection
       to the charset default. An init command such as
           SET NAMES utf8mb4 COLLATE utf8mb4_unicode_ci
       is therefore undone on every reconnect, and the reconnect is invisible to SQLyog (it happens
       inside mysql_real_query or the keep-alive mysql_ping). TunnelCommunity watches the connection
       id across those calls and runs this so the user's commands are the last thing on the new
       session. The re-run statement itself keeps the id, so there is no recursion. */
    const wyChar*   token;
    SQLTokenizer*   tok;
    wyUInt32        len = 0, linenum = 0;
    wyInt32         isdel = 0;
    wyChar          delimiter[256] = ";\0";
    wyString        query;
    MYSQL_RES*      res;

    if(!tunnel || !mysql || !initcommand.GetLength())
        return;

    tok = new SQLTokenizer(tunnel, &mysql, SRC_BUFFER, (void*)initcommand.GetString(), initcommand.GetLength());

    while((token = tok->GetQuery(&len, &linenum, &isdel, delimiter)))
    {
        query.SetAs(token);
        query.LTrim();
        query.RTrim();

        if(!query.GetLength())
            continue;

        NgsaTrace("ReplayInitCommands: %s", query.GetString());
        if(tunnel->mysql_real_query(mysql, query.GetString(), query.GetLength(), false) == 0)
        {
            if((res = tunnel->mysql_store_result(mysql, true)))
                tunnel->mysql_free_result(res);
        }
    }

    delete tok;
}

const wyChar*
NgsaConn::EffectiveHost(ConnectionInfo* ci)
{
    if(ci->m_isssh == wyTrue && ci->m_localport > 0)
        return ci->m_localhost.GetString();
    return ci->m_host.GetString();
}

wyInt32
NgsaConn::EffectivePort(ConnectionInfo* ci)
{
    if(ci->m_isssh == wyTrue && ci->m_localport > 0)
        return ci->m_localport;
    return ci->m_port;
}

void
NgsaConn::StopTunnel(ConnectionInfo* ci)
{
    if(ci && ci->m_isssh == wyTrue)
        NgsaSsh::Stop(ci);
}

#endif /* NGSA_SSH_SSL */
