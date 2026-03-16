# Nightly CI Cross-Platform Recovery 2026-03-16

Date: 2026-03-16

Task IDs:
- `DV-02-T02` CI and Validation Pipeline Expansion

## Context
GitHub Actions run `23148759868` failed after the earlier MSYS2 recovery work landed. The failures were spread across all three release-producing platforms:

- Windows failed in the MSI packaging step.
- Linux failed while compiling `sgame`.
- macOS failed while compiling libc++-based C++ translation units.

The run link investigated for this recovery was:
- `https://github.com/DarkMatter-Productions/WORR/actions/runs/23148759868`

## Failure Analysis

### Windows MSI packaging
The nightly build reached the `Build MSI installer` step and then failed inside WiX:

- `Product/@Version` was being seen as the literal string `$Version`
- `Product/@UpgradeCode` was being seen as the literal string `$UpgradeCode`
- `Product/@Language` was missing
- `light.exe` then failed because `Product.wixobj` was never generated

The underlying issue was the WiX template and PowerShell wrapper working against each other. The template redefined variables that were already being injected from `candle.exe`, producing self-referential values instead of concrete values.

The PowerShell wrapper also did not stop immediately on native tool failure, which is why the job continued far enough to print a misleading `Wrote ... msi` line even after WiX had already failed.

### Linux compile failure
The Linux runner stopped in `src/game/sgame/gameplay/g_func.cpp` because GCC did not accept `std::sinf`:

- `error: 'sinf' is not a member of 'std'`

The same non-portable pattern existed in multiple `sgame` translation units. The immediate run only failed in `g_func.cpp`, but additional files would have remained vulnerable to the same compiler/libstdc++ combination.

### macOS compile failure
The macOS runner produced repeated errors like:

- `../version:1:1: error: expected unqualified-id`

This was not a game-code syntax problem. The repository's version metadata file was named `VERSION`, and on the default case-insensitive macOS filesystem that file could satisfy a standard library include for `<version>`. Because Meson passed the repository root as an include directory, libc++ ended up opening the project metadata file instead of the standard header.

## Changes Made

### 1. Renamed repository version metadata
Updated version metadata handling to use `WORR_VERSION` instead of `VERSION`:

- Added `WORR_VERSION`
- Removed `VERSION`
- Updated `version.py` to read `WORR_VERSION`

This removes the `<version>` header collision on macOS while keeping the project version source explicit and simple.

### 2. Fixed Linux-portable trigonometry calls
Replaced `std::sinf(...)` with `std::sin(...)` in the affected `sgame` code paths:

- `src/game/sgame/player/p_view.cpp`
- `src/game/sgame/gameplay/g_func.cpp`
- `src/game/sgame/gameplay/g_turret.cpp`
- `src/game/sgame/monsters/m_fixbot.cpp`
- `src/game/sgame/monsters/m_move.cpp`
- `src/game/sgame/monsters/m_turret.cpp`

Using the standard overloaded `std::sin` form keeps the code valid across GCC, Clang, and MSVC.

### 3. Fixed WiX MSI generation
Updated the Windows installer tooling:

- Removed redundant/self-referential `<?define ... ?>` lines from `tools/installer/worr.wxs`
- Added `Language="1033"` to the WiX `Product` declaration
- Updated `tools/build_msi.ps1` to:
  - accept the version parameter via `ProductVersion` with `Version` kept as an alias
  - invoke `heat`, `candle`, and `light` through a shared native-command wrapper
  - fail immediately when a native tool returns a non-zero exit code
  - clean up the temporary MSI staging directory in a `finally` block

This makes MSI failures visible at the exact failing tool invocation and prevents false-positive completion messages.

## Verification

The following verification was completed locally:

- `python version.py --json`
  - confirmed `raw` now comes from `WORR_VERSION`
- `meson compile -C builddir-msys2-run`
  - completed successfully after the `sgame` and version-file changes
- `python3 tools/refresh_install.py --build-dir builddir-msys2-run --install-dir .install --base-game baseq2 --platform-id windows-x86_64`
  - completed successfully
- `python3 tools/release/package_platform.py --input-dir .install --output-dir release-ci-fix --platform-id windows-x86_64 --repo DarkMatter-Productions/WORR --channel nightly --version 0.0.0 --commit-sha 1ccbe44829cfcfe9f61ddd362c95b2b348b51dee --build-id local-ci-fix --allow-prerelease`
  - completed successfully
- `python3 tools/release/verify_artifacts.py --artifacts-root release-ci-fix --platform-id windows-x86_64`
  - completed successfully
- PowerShell parse-check for `tools/build_msi.ps1`
  - completed successfully

## Verification Limits

I did not run a full local MSI build because this environment only exposed `light.exe`; `heat.exe` and `candle.exe` were not present in `PATH`.

I also did not execute a hosted rerun of GitHub Actions from this session. The macOS confidence is based on the concrete root-cause removal (`VERSION` no longer exists, and `version.py` no longer depends on that filename) plus successful local rebuild of the Windows/MSYS2 tree after the same source changes.

## Outcome
Run `23148759868` was not a platform-support problem. It was a set of independent CI regressions:

- Windows MSI templating/tool error handling
- Linux `std::sinf` portability
- macOS case-insensitive header shadowing via repository version metadata

Those issues were corrected in source and tooling so the nightly pipeline can build and package consistently across Windows, Linux, and macOS.
