# MariaDB Connector/C 3.4.11 (NGSA build)

Replaces the static `mariadbclient.lib` (Connector/C 3.3.8, 2023) that upstream SQLyog bundled.
Decision 09/10/2026: upgrade now rather than carry the debt (MariaDB PARSEC authentication,
MySQL 8.4/9 fixes, one OpenSSL runtime shared with libpq).

- Source: vcpkg port `libmariadb` 3.4.11, features `[openssl]`, triplet `x64-windows` (DLL,
  dynamic CRT), built with the overlay port in `C:\data\tools\vcpkg-overlays\libmariadb`, which is
  the stock port with two changes: `CLIENT_PLUGIN_PARSEC=DYNAMIC` and
  `CLIENT_PLUGIN_AUTH_GSSAPI_CLIENT=DYNAMIC` (the stock port switches both off).
- Licence: LGPL 2.1 or later (file `LICENSE` here, from the port's `copyright`). Used as a DLL,
  so the LGPL's relinking requirement is met without further action.
- OpenSSL: links the same `libssl-3-x64.dll` / `libcrypto-3-x64.dll` (3.6.5) as libpq. The old
  OpenSSL 3.0.13 import libraries that the static client needed are no longer used.

## Files in the repo

| Path | What |
|---|---|
| `include/mysql/*.h` | the connector's installed headers (replaces the old source-tree copy) |
| `lib/x64/release/libmariadb.lib` | import library (linked instead of `mariadbclient.lib`) |
| `lib/x64/release/libmariadb.dll` | the client, SHA-256 starts `3470a9b43fa84de2` |
| `lib/x64/release/z.dll` | zlib 1.3.2 from vcpkg (protocol compression), Zlib licence in `LICENSE-zlib` |
| `lib/x64/release/*.dll` auth plugins | `caching_sha2_password`, `sha256_password`, `mysql_clear_password`, `dialog`, `client_ed25519`, `auth_gssapi_client`, `pvio_shmem` (`parsec` is not shipped: its source carries a GPL v2-only header, so the installer offers it as a download of MariaDB's own package) |

The app sets `MYSQL_PLUGIN_DIR` to the exe directory (`CommonHelper.cpp`), so the plugin DLLs
must sit next to `bonSQL.exe`. A release folder holds: exe, `Keywords.db`, `libmariadb.dll`, `z.dll`, the
plugin DLLs above, `libssl-3-x64.dll`, `libcrypto-3-x64.dll`, `plink.exe`.

## Rebuilding

```
vcpkg install "libmariadb[openssl]:x64-windows" --overlay-ports=C:\data\tools\vcpkg-overlays
```

then copy `installed/x64-windows/{include/mysql, lib/libmariadb.lib, bin/libmariadb.dll,
plugins/libmariadb/*.dll}` as in the table. `pvio_npipe.dll` (named pipes) is not built by the
port; add `CLIENT_PLUGIN_PVIO_NPIPE` if named-pipe connections are ever added.
