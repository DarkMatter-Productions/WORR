## Nightly Run 23153597827 Error and Warning Recovery

Date: 2026-03-16

Task IDs:
- `DV-02-T02`
- `DV-04-T03`

## Context
GitHub Actions run `23153597827` failed across the nightly matrix with a mix of hard build/package breaks and a large amount of first-party warning noise:

- Windows built the binaries but failed during MSI validation.
- Linux failed in the RTX renderer due to Vulkan header/API drift.
- macOS failed in `sgame` save metadata serialization because `size_t` was not handled consistently with JsonCpp-backed save metadata.
- The run also surfaced avoidable first-party warnings in `client`, `cgame`, `sgame`, `rend_gl`, and `rend_vk`, plus noisy fallback subproject warnings during forced-fallback dependency builds.

Run investigated:
- `https://github.com/DarkMatter-Productions/WORR/actions/runs/23153597827`

## Failures Observed

### Windows MSI packaging
The Windows job reached `Build MSI installer` and then failed WiX validation with an ICE80 architecture mismatch. The harvested/generated installer metadata was still describing components as 32-bit while the package was installing into a 64-bit target layout.

The failure was in the WiX invocation path rather than in the game binaries themselves.

### Linux Vulkan RTX compile failure
The Linux build stopped in `src/rend_rtx/vkpt/debug.c` because the build environment's Vulkan headers exposed the line-rasterization extension through the `EXT` names instead of the older `KHR` names used by the source.

That made the debug draw pipeline setup uncompilable on the current runner image.

### macOS save metadata compile failure
The macOS build failed in `sgame` save metadata/serialization because the save-format version constant and save-type deduction path used `size_t` in a way that was not portable across the hosted JsonCpp/libc++ combination.

The run exposed two separate but related problems:

- the metadata constant used a platform-sized type instead of an explicit JsonCpp integer type
- the serialization type-deduction layer did not cover a distinct `size_t` type cleanly when it was not an alias for `uint32_t` or `uint64_t`

## Warning Cleanup Goals
Beyond the hard failures, the run logs showed repeated first-party warnings that were worth eliminating before they get normalized:

- debug-only locals and counters compiled into release builds
- format-string warnings from localized UI print calls
- ignored-attribute warnings from `std::unique_ptr<FILE, decltype(&std::fclose)>`
- sequence-point and operator-precedence warnings in `sgame`
- unused static helper warnings in `rend_gl`
- minor macro/extern redefinition warnings in `cgame`

Forced-fallback third-party builds also emitted avoidable warning noise, especially around subproject test targets.

## Implementation

### 1. Fixed Windows MSI architecture metadata
Updated `tools/build_msi.ps1` so the WiX harvest/compile path is explicitly 64-bit:

- `heat.exe` now runs with `-platform x64`
- `candle.exe` now runs with `-arch x64`

This aligns the harvested components with the 64-bit installer layout and resolves the ICE80 failure seen in the run.

### 2. Updated Vulkan line-rasterization usage for current headers
Updated `src/rend_rtx/vkpt/debug.c` to use the currently exposed `EXT` line-rasterization symbols:

- `VkPipelineRasterizationLineStateCreateInfoEXT`
- `VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT`
- `VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT`

This restores compatibility with the Vulkan headers present on the Linux CI runner.

### 3. Made save metadata serialization portable on macOS
Updated the save metadata path so the JsonCpp-facing save version is explicit and the serializer handles `size_t` safely:

- `src/game/sgame/gameplay/g_save_metadata.hpp` now uses `Json::UInt` for `SAVE_FORMAT_VERSION`
- `src/game/sgame/gameplay/g_save.cpp` now provides a `save_type_deducer` specialization for distinct `size_t` types and maps them to `UInt32` or `UInt64` based on width

This removes the macOS compile failure while keeping serialization type handling explicit.

### 4. Removed first-party warning noise exposed by the run
Cleaned warning-producing first-party code in several subsystems:

- `client`: wrapped release-unused packet debug locals/counters in `#if USE_DEBUG`
- `cgame`: added localized print wrappers so translated keys are not passed as raw `printf` format strings with extra arguments; also cleaned a renderer macro redefinition and `vid` declaration warning
- `sgame`: replaced repeated `FILE*` deleter spellings with a shared scoped file wrapper, removed unused capture-state accessors, fixed sequence-point updates in the status bar builder, and clarified operator precedence in item-flag checks
- `rend_gl`: marked currently-unused helper functions as intentionally unused
- `rend_vk`: moved debug-only world-face counters and debug prints behind `#if USE_DEBUG`

These changes are all first-party and targeted at warnings that appeared in release-oriented CI builds.

### 5. Reduced fallback dependency warning noise
Adjusted Meson fallback dependency options so third-party fallback builds run quieter:

- added a `quiet_fallback_opt` path with `warning_level=0`
- applied it to fallback SDL3, FreeType, HarfBuzz, SDL3_ttf, and OpenAL dependency configuration
- disabled HarfBuzz subproject tests in nightly/release configure commands

This keeps nightly logs focused on WORR warnings rather than vendored dependency chatter.

### 6. Hardened local Windows validation path
While verifying the fixes locally, `worr.exe` exposed an additional Windows linkage inconsistency: project-wide link arguments were only being applied to C targets, not C++ targets.

Updated `meson.build` to apply the shared `common_link_args` to both `c` and `cpp` project link steps. That keeps the Windows client executable aligned with the same linker/toolchain assumptions already used successfully by the dedicated server and renderer binaries.

## Validation

Local verification completed:

- PowerShell parser validation for `tools/build_msi.ps1`
- `meson setup builddir-msys2-run --reconfigure -Dharfbuzz:tests=disabled`
- `meson compile -C builddir-msys2-run q2proto`
- `meson compile -C builddir-msys2-run worr`
- `meson compile -C builddir-msys2-run worr_opengl_x86_64 worr_vulkan_x86_64 worr_rtx_x86_64 worr worr.ded worr_updater sgamex86_64 cgamex86_64`

The final compile completed successfully and no first-party compiler warnings appeared in that validated build output.

## Verification Limits

- I did not rerun the hosted GitHub Actions workflow from this session.
- I did not perform a full local MSI build because this machine does not currently expose the complete WiX toolchain in `PATH`.
- The warning cleanup was validated through the local Windows/MSYS2 build path and direct inspection of the affected CI failure sources, not through fresh hosted Linux/macOS reruns.

## Outcome
Run `23153597827` was a real cross-platform CI regression set, not a platform-support gap. The blocking errors were corrected for:

- Windows MSI packaging
- Linux Vulkan RTX compilation
- macOS `sgame` save metadata compilation

The associated first-party warning backlog exposed by the same run was also reduced materially so nightly logs stay actionable instead of burying real regressions in repeated noise.
