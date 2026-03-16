# Nightly Run 23162552895 Linux Safety Recovery

Date: 2026-03-16

Task ID: `DV-02-T02`

## Summary
- Resolved nightly run `23162552895`, where the Linux build job (`67293973244`) failed during the main engine build while Windows and macOS passed.
- Reproduced the failure on Ubuntu 24.04 with the same Meson flags used by CI, then validated the full Linux nightly flow through build, `.install` refresh, archive packaging, and artifact verification.
- Hardened a small set of GCC/Linux-specific safety issues found during that validation pass.

## Failure Analysis
- The first Linux build stop was in `src/client/effects.cpp` and `src/client/view.cpp`.
- The C++ `VEC2(...)` and `VEC3(...)` helpers expand to temporary vector literals. GCC rejects the resulting address-of-temporary-array conversions in function calls, while the Windows toolchain path had been accepting them.
- The Linux build also emitted a stream of avoidable portability warnings, including:
  - C-only `-Wmissing-prototypes` being applied to C++ engine targets through shared Meson argument plumbing.
  - a potential null `%s` print in `src/common/zone.c`
  - a maybe-uninitialized leaf-area path in `src/server/mvd/parse.c`
  - unsafe patched-PVS path construction in `src/common/bsp.c`

## Implementation

### 1. Fix GCC-incompatible temporary vector callsites
- Added scalar overloads in `src/client/effects.cpp` and `src/client/view.cpp` so the Linux build no longer depends on passing temporary array literals through `const vec*_t` parameters.
- Replaced the affected muzzle-flash and dlight fade callsites with the scalar forms.

### 2. Keep C-only warnings out of C++ Linux targets
- Split Meson engine warning arguments so `-Wmissing-prototypes` is only applied to C compilation units.
- Updated engine, renderer, q2proto, and executable C target argument wiring to use the new C-specific argument list while leaving C++ targets on the shared engine defines only.

### 3. Linux-safety hardening discovered during the validation pass
- `src/common/bsp.c`
  - Reworked patched-PVS path assembly to use bounded concatenation and explicit final-length checks instead of `strncpy`/`strncat` patterns that trigger GCC overflow diagnostics.
- `src/common/zone.c`
  - Guarded the `z_tagnames[i]` print path with a non-null fallback label before using `%s`.
- `src/server/mvd/parse.c`
  - Removed the warning-prone `leaf1` lifetime by caching the source area explicitly before client fan-out.
- `src/client/sound/qal.cpp`
  - Converted OpenAL section/driver loops to `size_t` indexing for GCC-clean array-count comparisons.
- `src/client/view.cpp`
  - Matched the low-priority entity replacement loop to the `int`-typed entity count.

## Validation
- Reproduced CI-style Linux configure on Ubuntu 24.04:
  - `meson setup builddir-linux-ci --wrap-mode=forcefallback --buildtype=release -Dbootstrapper=false -Dtests=false -Davcodec=disabled -Dlibcurl=disabled -Dharfbuzz:tests=disabled`
- Verified the full Linux build after fixes:
  - `meson compile -C builddir-linux-ci`
- Verified Linux staging and release packaging:
  - `python3 tools/refresh_install.py --build-dir builddir-linux-ci --install-dir .install-linux-ci --base-game baseq2 --platform-id linux-x86_64`
  - `python3 tools/release/package_platform.py --input-dir .install-linux-ci --output-dir release-linux-ci --platform-id linux-x86_64 --repo DarkMatter-Productions/WORR --channel nightly --version 0.0.0 --commit-sha fb3de3eb1404384f0b0a5af7447c18d7c806819a --build-id local-linux --allow-prerelease`
  - `python3 tools/release/verify_artifacts.py --artifacts-root release-linux-ci --platform-id linux-x86_64`

## Notes
- The Linux build still emits broader first-party GCC warning noise around localization-string print wrappers (`Com_Printf("$key", ...)`) and other older warning classes. Those warnings were pre-existing and are not part of the job-stopping regression fixed here.
- The resolved regression for run `23162552895` was the GCC/Linux incompatibility in client vector literal callsites, plus the related Linux safety cleanups documented above.
