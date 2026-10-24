/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build extension: SSH tunnelling and SSL for the Community connection dialog.
 *
 * All new behaviour lives here and in src/ngsa/*.cpp. Upstream files only carry
 * one-line hooks marked "// NGSA:" and guarded by NGSA_SSH_SSL.
 * See docs/ngsa/SSH-SSL-FEASIBILITY.md for the design and merge notes.
 */
#ifndef NGSA_CONNECTION_EXT_H
#define NGSA_CONNECTION_EXT_H

#ifdef NGSA_SSH_SSL

#include "Global.h"

class Tunnel;

namespace NgsaConn
{
    /* ---- Connection dialog (IDD_CONNECT) ------------------------------------ */

    /// Handle WM_COMMAND for the SSH/SSL tab controls.
    /// Returns wyTrue when the command was ours; *pdirty is set when the user changed something.
    wyBool  OnCommand(HWND hdlg, WPARAM wparam, LPARAM lparam, wyBool* pdirty);

    /// Reset the SSH/SSL controls for a new connection.
    void    ClearDialog(HWND hdlg);

    /// Copy the SSH/SSL controls into the ConnectionInfo (called on Connect / Test).
    void    ReadFromDialog(HWND hdlg, ConnectionInfo* ci);

    /// Load the SSH/SSL keys of ini section `conn` into the dialog controls.
    void    LoadFromIni(HWND hdlg, const wyChar* conn, const wyChar* dir);

    /// Save the SSH/SSL controls into ini section `conn`.
    void    SaveToIni(HWND hdlg, const wyChar* conn, const wyChar* dir);

    /* ---- Connect path --------------------------------------------------------- */

    /// Apply SSL options and start the SSH tunnel (if enabled) on an initialised MYSQL
    /// handle, before mysql_real_connect. Returns wyFalse with errmsg set on failure.
    /* TLS options for ANY connection built from a ConnectionInfo (main window, Stop query's
       helper connection, reconnect, import, Copy Database). Must run before mysql_real_connect. */
    void    ApplyTlsOptions(ConnectionInfo* ci, Tunnel* tunnel, MYSQL* mysql);

    wyBool  PrepareConnection(ConnectionInfo* ci, Tunnel* tunnel, MYSQL* mysql, wyString& errmsg);

    /* Re-run Init Command(s) as plain queries on a live handle (TunnelCommunity calls this after a
       reconnect it detected; the text is the tunnel's m_ngsainitcommand). */
    void    ReplayInitCommands(wyString& initcommand, Tunnel* tunnel, MYSQL* mysql);

    /// Host/port that mysql_real_connect must use: the tunnel's loopback end when an
    /// SSH tunnel is up, otherwise the configured MySQL host/port.
    const wyChar*   EffectiveHost(ConnectionInfo* ci);
    wyInt32         EffectivePort(ConnectionInfo* ci);

    /// Tear the tunnel down when a connect attempt failed or a test connection is done.
    void    StopTunnel(ConnectionInfo* ci);
}

#endif /* NGSA_SSH_SSL */
#endif /* NGSA_CONNECTION_EXT_H */
