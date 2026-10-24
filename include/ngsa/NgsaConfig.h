/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * bonSQL settings location and the one-time import from SQLyog (docs/ngsa/REBRAND.md, decisions 2 and 3).
 */
#ifndef NGSA_CONFIG_H
#define NGSA_CONFIG_H

#include <windows.h>

/* The roaming AppData folder, as SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path) gives it; path holds
   MAX_PATH wide characters. The environment variable BONSQL_TEST_APPDATA replaces it, so that the test
   harnesses can run the first-start import without touching the real profile. */
HRESULT NgsaGetAppData(wchar_t *path);

/* First start: when bonSQL has no bonsql.ini yet and SQLyog's sqlyog.ini exists (in %APPDATA%\SQLyog, or
   in the -dir folder), ask whether to import it. The SQLyog files are copied under bonSQL's names, never
   moved or changed. Call after the -dir option is parsed and before anything reads the ini. */
void NgsaOfferSqlyogImport();

/* Settings the installer chose for the user: it writes HKCU\Software\bonSQL\InstallUpdateCheck = 1 when the
   "Check for new versions once a week" task was ticked. Applied to bonsql.ini on the next start and deleted, so
   it never overrides a choice the user makes in Preferences later. Call after the ini exists. */
void NgsaApplyInstallDefaults();

/* The SSH host key section of bonsql.ini (SQLyog NGSA builds called it IctSshHostKeys). */
#define NGSA_HOSTKEY_SECTION        "SshHostKeys"
#define NGSA_HOSTKEY_SECTION_OLD    "IctSshHostKeys"

#endif
