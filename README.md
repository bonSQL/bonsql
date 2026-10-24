# bonSQL

bonSQL is a Windows desktop client for MySQL and MariaDB: an object browser, a SQL editor with result grids,
a table designer, user management, import and export, SSH tunnels and SSL connections. One folder of files,
no .NET or Java runtime, and it runs on Linux under Wine.

It is based on SQLyog Community 13.3.1 and continues it independently: current MariaDB and MySQL servers,
a dark theme, no adverts, no upgrade prompts and no calls home.

Website and help: **https://bonsql.org/**

## Download

The latest installer and the portable zip are on the
[Releases](https://github.com/bonSQL/bonsql/releases/latest) page, each with its SHA-256. The same links are on
[bonsql.org/download](https://bonsql.org/download/).

- **bonSQL-&lt;version&gt;-setup.exe**: installs to Program Files or per user, Start menu entries, uninstaller.
  If the Microsoft Visual C++ runtime (2022, 14.44 or newer) is missing, Setup downloads it from Microsoft.
- **bonSQL-&lt;version&gt;.zip**: the same files, unpack anywhere and run `bonSQL.exe`. Needs the same runtime,
  from [Microsoft's download page](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist).

Windows 10 or 11, 64-bit. The releases are not code-signed yet, so SmartScreen may warn on first run; the
SHA-256 on the release page is the check.

Servers: MariaDB 10.11 and later and MySQL 8.4 and later are supported and tested; MySQL 8.0 works but is
not tested for every release.

## What bonSQL adds to SQLyog Community

- MariaDB: JSON columns, roles, current privileges, authentication plugins (ed25519, GSSAPI, PARSEC),
  sequences, system-versioned tables, invisible and compressed columns, keywords and functions per engine.
- MySQL 8.4 and 9: caching_sha2_password, KILL QUERY instead of the removed COM_PROCESS_KILL, current keyword
  list.
- MariaDB Connector/C 3.4 with OpenSSL 3; enforced TLS when SSL is ticked, certificate verification with a CA.
- SSH tunnels and SSL connections in the free edition (they were Ultimate-only).
- A dark theme throughout, Fluent icons, a query running-time display, persistent Table Data filters, a
  processlist filter, Kill Query, Scintilla 5 with syntax colouring and formatting in the field viewer.
- Everything that advertised a paid edition is gone: no nag screens, no greyed-out menus, no notifications
  feed, no usage tracking. An optional, off-by-default weekly check for new versions reads one file from
  bonsql.org and sends nothing.
- Your SQLyog settings: on its first start bonSQL offers to import SQLyog's connections and settings (copied,
  never changed).

## Reporting problems

Use [Issues](https://github.com/bonSQL/bonsql/issues). Say which server (MariaDB or MySQL, version), which
bonSQL version (Help > About), and what you did. For anything security-related, see [SECURITY.md](SECURITY.md).

## Building

Visual Studio 2022 with the v143 toolset and a Windows 10 SDK. Open `build/bonSQL.sln`, pick
Release and x64, build (the Debug configuration is not maintained). The output is `bin/x64/Release/bonSQL.exe`; copy `lib/Keywords.db` and the DLLs from
`lib/x64/release` next to it. The prebuilt libraries in `lib` were produced with vcpkg; the overlay port for
MariaDB Connector/C is in `third_party/vcpkg-overlays` and the exact versions are in
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

## Licence

bonSQL is free software under the GNU General Public License, version 3 or later ([COPYING](COPYING)).
Copyright © 2026 André Walsh; published by Walsh-Howard Consulting. Copyright and attribution details are in
[NOTICE.md](NOTICE.md); the licences of the components bonSQL includes are in
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

bonSQL is based on SQLyog Community 13.3.1, copyright © 2001-2025 Webyog Inc., GNU GPL version 2 or later.
SQLyog and Webyog are trademarks of their owners; bonSQL is not affiliated with Webyog.
