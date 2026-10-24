/* Copyright (C) 2026 André Walsh

   This program is free software; you can redistribute it and/or modify it under the terms of the GNU
   General Public License as published by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version. See COPYING.

   bonSQL: the product's name, ownership and web addresses in one place (docs/ngsa/LINK-AUDIT.md,
   docs/ngsa/REBRAND.md). Change them here, not at the call sites. */
#ifndef NGSA_BRAND_H
#define NGSA_BRAND_H

#define BRAND_NAME_A            "bonSQL"
#define BRAND_NAME_W            L"bonSQL"
#define BRAND_TAGLINE_W         L"MySQL and MariaDB client"
#define BRAND_EXE_W             L"bonSQL.exe"

/* Settings live in %APPDATA%\bonSQL (or the -dir folder): bonsql.ini with its main section [bonSQL],
   bonsql.err, bonsql.log, Favorites, Tags, Themes. SQLyog's folder is offered for import on the first
   start (src/ngsa/NgsaConfig.cpp, docs/ngsa/REBRAND.md). */
#define BRAND_CFG_DIR_A         "bonSQL"
#define BRAND_CFG_DIR_W         L"bonSQL"
#define BRAND_CFG_STEM_A        "bonsql"
#define BRAND_CFG_STEM_W        L"bonsql"
#define BRAND_INI_SECTION_A     "bonSQL"

/* Copyright belongs to André Walsh personally; S.A.R.L. Walsh-Howard Consulting manages and promotes bonSQL
   and is authorised to act as his agent (Andre, 13/10/2026). NOTICE.md says so; the About box does not. */
#define BRAND_COPYRIGHT_W       L"Copyright \u00A9 2026 Andr\u00E9 Walsh"
#define BRAND_ORIGIN_W          L"Based on SQLyog Community 13.3.1, \u00A9 2001-2025 Webyog Inc., GNU GPL."
#define BRAND_DISCLAIMER_W      L"SQLyog and Webyog are trademarks of their owners. bonSQL is not affiliated with or endorsed by Webyog."

/* Web addresses: not decided yet (13/10/2026). While a value is empty the feature that needs it stays quiet:
   Help says the pages are not online yet, and no other link is shown. */
#define BRAND_WEBSITE_A         "https://bonsql.org/"
#define BRAND_DOWNLOAD_URL_A    "https://bonsql.org/download/"
#define BRAND_UPDATE_URL_A      "https://bonsql.org/release/latest.json"   /* Check for Updates reads this (NgsaUpdateCheck.cpp) */
#define BRAND_UPDATE_HOST_A     "bonsql.org"
#define BRAND_HELP_BASE_A       "https://bonsql.org/help/"   /* topic names are appended: ".../help/" + "create-index" (live 24/10/2026) */
#define BRAND_LICENCE_URL_A     "https://www.gnu.org/licenses/gpl-3.0.html"

#endif
