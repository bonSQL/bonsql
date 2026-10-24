/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: SSH port-forward tunnel for MySQL connections using a stock,
 * unmodified PuTTY plink.exe. See docs/ngsa/SSH-SSL-FEASIBILITY.md section 3a for
 * why upstream's patched plink is not used and how each of its behaviours is
 * reproduced here (readiness by polling the local port, errors from stderr,
 * host keys via -hostkey, password via -pwfile).
 */
#ifndef NGSA_SSH_TUNNEL_H
#define NGSA_SSH_TUNNEL_H

#ifdef NGSA_SSH_SSL

#include "Global.h"

namespace NgsaSsh
{
    /// Spawn plink for ci (m_sshhost/m_sshport/m_sshuser/m_sshpwd/m_ispassword/
    /// m_privatekeypath) forwarding a fresh loopback port to ci->m_host:ci->m_port.
    /// On success fills m_localhost/m_localport/m_origmysqlport/m_hprocess/m_sshpipeends.
    /// May prompt (owner window) to trust an unknown host key. Returns wyFalse + errmsg on failure.
    wyBool  Start(ConnectionInfo* ci, HWND owner, wyString& errmsg);

    /// Terminate the tunnel process (if any) and reset the runtime fields.
    void    Stop(ConnectionInfo* ci);

    /// Stop + Start, used by the reconnect path.
    wyBool  Restart(ConnectionInfo* ci, HWND owner, wyString& errmsg);

    /// Locate plink.exe: next to bonSQL.exe, then %ProgramFiles%\PuTTY, then PATH.
    wyBool  FindPlink(wyString& path);
}

#endif /* NGSA_SSH_SSL */
#endif /* NGSA_SSH_TUNNEL_H */
