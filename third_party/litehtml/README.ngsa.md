# litehtml 0.10 (vendored for bonSQL's HtmlView)

- Source: litehtml v0.10 as fetched by vcpkg (`litehtml-litehtml-v0.10.tar.gz`, SHA-256 7700eced92847d34ad9846b138cf195a9c974b519be70de58797880ae9da649e), unmodified.
  Kept: `include/`, `src/` (with the bundled gumbo HTML parser in `src/gumbo`), `containers/windows/win32`,
  `LICENSE`, `README.md`.
- Licences: litehtml BSD-3-Clause (`LICENSE`); gumbo Apache-2.0 (`src/gumbo/LICENSE`); compatible with
  bonSQL's GPL v3. Both texts go into the third-party notices.
- Build notes: C++17 and `/permissive-` (litehtml uses the `and`/`not` keywords); `NOMINMAX` before
  `windows.h`; gumbo as C (`src/gumbo/visualc/include` supplies `strings.h`). See
  `tools/htmlview-demo/build.cmd`.
- Used by `src/htmlview/HtmlView.cpp` only; nothing else includes litehtml headers.
