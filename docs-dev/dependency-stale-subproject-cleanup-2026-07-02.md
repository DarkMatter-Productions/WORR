# Dependency Stale Subproject Cleanup - 2026-07-02

Project tasks: `DV-06-T02`; supporting audit context from `DV-06-T01`.

## Summary

Removed local unpacked dependency trees and packagecache artifacts that no longer
match the active Meson wrap baselines. This reduces duplicate vendored source
sprawl under `subprojects/` without changing tracked wrap files or top-level
build dependency declarations.

## Active Baseline Kept

The cleanup kept the currently referenced wrap baselines:

- `curl-8.18.0`
- `fmt-12.0.0`
- `harfbuzz-12.3.2`
- `jsoncpp-1.9.6`
- `libjpeg-turbo-3.1.3`
- `libpng-1.6.53`
- `openal-soft-1.24.3`
- `zlib-ng-2.3.2`
- `SDL3-3.4.0`
- `SDL3_ttf-3.2.2`
- `freetype-2.14.1`
- `khr-headers`
- active wrap files, packagefiles, and current packagecache archives

`ffmpeg/` was left in place because it is the checkout used by the active
`ffmpeg.wrap` wrap-git fallback. `nasm/` was left in place because the active
tree only has `nasm.wrap.disabled`; removing the disabled local checkout is a
separate policy decision from stale version cleanup.

## Removed Local Trees

Removed ignored unpacked source directories that are superseded by current wrap
targets:

- `subprojects/cairo-1.18.2`
- `subprojects/curl-8.15.0`
- `subprojects/fmt-11.0.2`
- `subprojects/glib-2.82.2`
- `subprojects/harfbuzz-11.4.1`
- `subprojects/jsoncpp-1.9.5`
- `subprojects/libjpeg-turbo-3.1.0`
- `subprojects/libpng-1.6.50`
- `subprojects/openal-soft-1.23.1`
- `subprojects/zlib-ng-2.2.4`

Removed matching stale archives and patch zips from
`subprojects/packagecache/`, plus zero-byte temporary download leftovers. The
current-version packagecache entries remain available for offline fallback
builds where they match current wraps.

## Validation

- Compared unpacked `subprojects/` directories against current wrap
  `directory = ...` targets.
- Searched top-level build files and documentation for direct references to the
  stale version directories.
- Verified the stale directories and cache files no longer exist after removal.
- Confirmed `subprojects/wraps.zip` carries the current version pins for the
  cleaned dependencies, so it was not changed.

No Meson dependency declarations were changed in this pass.
