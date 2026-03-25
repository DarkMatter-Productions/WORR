# Vendored libcurl Wrap Bootstrap Build Fix

Date: 2026-03-23

Task IDs: `DV-08-T10`, `DV-08-T09`, `DV-08-T03`

## Summary

Local desktop builds could not build the bootstrap updater path from source when
Meson fell back to the vendored libcurl wrap. The tracked patch at
`subprojects/packagefiles/curl/meson.build` still described an older curl
8.15-era source list while `subprojects/libcurl.wrap` now downloads
`curl-8.18.0.tar.xz`.

That mismatch caused the bootstrap-enabled build to fail before launcher or
worker compilation, because Meson attempted to compile source files that do not
exist in curl 8.18.0.

## Root Cause

The wrap metadata was partially updated to curl 8.18.0, but the patch directory
used by Meson still listed stale source files such as:

- `lib/vtls/mbedtls_threadlock.c`
- `lib/vquic/curl_msh3.c`
- `lib/vssh/curl_path.c`
- `lib/vssh/wolfssh.c`
- `lib/curl_des.c`
- `lib/fopen.c`
- `lib/krb5.c`
- `lib/rename.c`
- `lib/share.c`
- `lib/speedcheck.c`

The 8.18.0 tree also introduced newer source files that the patch directory did
not include, so the wrapped static library definition was incomplete even after
the missing-file errors were addressed ad hoc.

## Implementation

Updated `subprojects/packagefiles/curl/meson.build` to match the 8.18.0 source
layout used by WORR's Windows-only fallback build:

- bumped the patch metadata version to `8.18.0`
- removed deleted 8.15-era files from the build list
- added the current 8.18.0 sources required by the wrapped static library:
  `curlx/fopen.c`, `curlx/strcopy.c`, `curlx/strerr.c`, `vtls/apple.c`,
  `vtls/cipher_suite.c`, `vssh/vssh.c`, `cf-ip-happy.c`, `curl_fopen.c`,
  `curl_share.c`, `multi_ntfy.c`, and `ratelimit.c`

While validating the dedicated fallback build, the updater bootstrap also hit a
Windows-only compile error from `min`/`max` macros leaking out of
`windows.h`. `src/updater/bootstrap.cpp` now defines `NOMINMAX` before the
Windows headers and undefines `min`/`max` afterward so the C++17 updater code
can safely use `std::min` and `std::max`.

## Result

Bootstrap-enabled local Windows builds now succeed when Meson uses the vendored
libcurl wrap instead of a system-installed `libcurl`.

This removes the earlier need to silently disable the bootstrap launcher path in
developer builds purely because the fallback subproject was stale.

## Validation

Validated with:

- `python -m mesonbuild.mesonmain setup builddir-win-bootstrap-fix --wipe -Dbase-game=basew -Ddefault-game=basew -Dopenal-soft:utils=false`
- `python -m mesonbuild.mesonmain compile -C builddir-win-bootstrap-fix`
- `python tools/refresh_install.py --build-dir builddir-win-bootstrap-fix --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/release/validate_stage.py --install-dir .install --platform-id windows-x86_64 --base-game basew`

The configure step reported `Dependency libcurl from subproject subprojects/curl-8.18.0 found: YES 8.18.0`,
and the staged install validated with the launcher/runtime bootstrap layout.
