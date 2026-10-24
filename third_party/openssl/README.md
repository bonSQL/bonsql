# OpenSSL 3.x runtime (shared by the MariaDB client and libpq)

- Version: OpenSSL 3.6.5 (vcpkg port `openssl`, triplet `x64-windows`), replacing the 3.0.13 DLLs that shipped
  with the 2024 build. OpenSSL keeps ABI compatibility across 3.x, and 3.0 reached end of life in 09/2026.
- Files: `lib/x64/release/libssl-3-x64.dll`, `lib/x64/release/libcrypto-3-x64.dll`. The import libraries
  `libssl.lib` / `libcrypto.lib` in the same folder are the original ones used to link `mariadbclient.lib`;
  they resolve by symbol name against the newer DLLs.
- Used by: `mariadbclient.lib` (MySQL TLS) and `libpq.dll` (PostgreSQL TLS).
- Licence: Apache 2.0, see LICENSE.

SHA-256 of the committed binaries:
19e0683d7757061d081a4aa0bb71319af8118f8a5abad1170c192c1b55bc0100 *lib/x64/release/libssl-3-x64.dll
93b5e376b1cfab7e8371bc1b067220b31b60c4bf16fd48eb1a31f180553d9724 *lib/x64/release/libcrypto-3-x64.dll
