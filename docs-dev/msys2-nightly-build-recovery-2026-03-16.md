# MSYS2 Nightly Build Recovery

Date: 2026-03-16

Task IDs:
- `DV-02-T02` CI matrix renderer/toolchain coverage

## Summary

GitHub Actions run `23146195642` failed in the build matrix after the workflow
bootstrapped successfully. Local reproduction against the Windows MSYS2 CI path
showed three distinct toolchain-compatibility regressions:

1. `src/rend_rtx/refresh/debug.c` defined `R_DrawArrowCap(...)` as `static`
   even though the symbol is declared externally in the debug header set.
2. Client C++ translation units relied on legacy unqualified `min(...)` /
   `max(...)` usage that no longer compiled cleanly once the MSYS2 GCC 15 /
   C++20 path resolved those names through `<algorithm>`.
3. The Windows updater target exposed `wWinMain(...)`, but the MinGW GUI build
   still linked with the narrow startup path and failed with an undefined
   `WinMain`.

## Root Cause

### Renderer debug symbol mismatch

- `src/rend_rtx/refresh/debug.c` exported a helper that is used as a normal
  renderer debug symbol.
- The local definition still used `static`, which is rejected once the
  translation unit sees the non-static declaration from shared headers.

### C++ min/max assumptions were no longer portable

- A large portion of the client code still used unqualified `min(...)` /
  `max(...)` calls that historically depended on legacy macros.
- Under the MSYS2 GCC 15 C++20 build, those calls resolved through
  `<algorithm>` instead, which surfaced mixed signed/unsigned arguments that
  older paths tolerated.
- `src/client/cgame.cpp` also referenced `developer` in a helper that is only
  valid when `USE_DEBUG` is enabled, so non-debug builds failed to compile that
  function body.

### MinGW updater startup mismatch

- `src/updater/worr_updater.c` already exposes a Unicode GUI entry point
  (`wWinMain`).
- The Meson updater target did not pass `-municode` on the GCC/MinGW path, so
  the linker still selected the narrow GUI startup object and searched for
  `WinMain`.

## Implementation

### Renderer fix

- `src/rend_rtx/refresh/debug.c`
  - exported `R_DrawArrowCap(...)` with external linkage so the definition
    matches its shared declaration.

### Client C++ compatibility fixes

- `src/client/client.h`
  - included `<algorithm>` for C++ client translation units
  - imported `std::min` / `std::max` into the existing unqualified call style
- `src/client/cgame.cpp`
  - guarded `CG_UI_Com_DPrintf(...)` for non-debug builds
  - made string-length `min(...)` calls explicitly `size_t`-typed
- `src/client/screen.cpp`
  - made pointer-difference `min(...)` calls explicitly `size_t`-typed
  - fixed the netgraph clamp to use `30u` with the unsigned `ping` path
- `src/client/sound/al.cpp`
  - fixed stream-buffer clamping to compare `ALint` values explicitly

### Updater/MinGW fix

- `meson.build`
  - added GCC-only `-municode` handling to the `worr_updater` target so MinGW
    links the Unicode GUI startup path expected by `wWinMain`
- `src/updater/worr_updater.c`
  - changed the local `UNICODE` defines to guarded defines so the new MinGW
    command-line define path does not produce redefinition warnings

## Validation

- Reproduced the Windows CI build locally with:
  - `meson setup builddir-msys2-run --wrap-mode=forcefallback --buildtype=release -Dbootstrapper=true -Dtests=false -Davcodec=disabled -Dlibcurl=disabled`
  - `meson compile -C builddir-msys2-run`
- Result:
  - full MSYS2 build completed successfully
  - `worr.exe`, `worr.ded.exe`, `worr_updater.exe`, `worr_opengl_x86_64.dll`,
    `worr_vulkan_x86_64.dll`, `worr_rtx_x86_64.dll`, `cgamex86_64.dll`, and
    `sgamex86_64.dll` all built in the reproduced CI environment
- Additional targeted verification:
  - `meson compile -C builddir-msys2-run worr_updater`
  - result: success

## Remaining Follow-Up

- Re-run the hosted GitHub Actions workflow after merge to confirm Linux/macOS
  use the same corrected client C++ call sites cleanly under their compilers.
- Consider a later cleanup pass for the remaining signed/unsigned warnings in
  first-party code, but they are not current build blockers.
