# macOS Nightly Recovery and SDL/MoltenVK Vulkan Support

Date: 2026-03-16

Task IDs:
- `DV-02-T02` CI matrix renderer/toolchain coverage
- `FR-02-T07` macOS SDL/MoltenVK Vulkan support

## Summary

GitHub Actions run `23144804091` exposed two distinct problems:

1. The Linux nightly job failed during dependency installation because the
   workflow was still targeting hosted-image package names that no longer
   exist on current `ubuntu-latest`.
2. The macOS nightly job targeted the retired `macos-13` Intel runner label,
   so the build never started.

After the initial inspection, the completed run also showed a third issue:

3. The Windows nightly job failed in the compile step because the MSYS2 setup
   did not install a Vulkan toolchain (`vulkan` headers/loader plus
   `glslangValidator`), so Vulkan/RTX artifact generation was not reliable.

Separately, the codebase already supported macOS client/server builds through
SDL3, but the native `vulkan` renderer was not actually usable on macOS:

- the SDL video backend always created an OpenGL window/context
- the Vulkan backend rejected SDL-native windows instead of creating a surface
- MoltenVK portability enumeration/subset handling was missing

## Root Cause

### Nightly workflow drift

- `.github/workflows/nightly.yml` still installed `libsdl3-dev` and
  `libdecor-0-dev` on Linux even though current Ubuntu runner images moved away
  from that package set.
- `tools/release/targets.py` still mapped the macOS Intel target to
  `macos-13`, which GitHub no longer provisions for new jobs.
- macOS CI did not explicitly install the Vulkan loader/MoltenVK toolchain, so
  even after the runner label issue was corrected the job could have produced
  an OpenGL-only build depending on image state.

### macOS Vulkan path incomplete by construction

- `src/unix/video/sdl.c` created every SDL window with `SDL_WINDOW_OPENGL` and
  always attempted `SDL_GL_CreateContext(...)`.
- `src/rend_vk/vk_main.c` only supported Win32/X11/Wayland surface creation and
  explicitly rejected `VID_NATIVE_SDL`.
- MoltenVK portability enumeration/device-extension handling was absent, which
  risks zero-device enumeration or device-creation failure on macOS.

## Implementation

### CI and packaging updates

- Updated the macOS release target runner from `macos-13` to
  `macos-15-intel`.
- Updated Linux nightly dependencies to current Ubuntu 24.04-compatible names
  and added explicit Vulkan toolchain packages:
  - `libvulkan-dev`
  - `glslang-tools`
  - `libdecoration0-dev`
- Updated macOS nightly dependencies to install:
  - `vulkan-loader`
  - `molten-vk`
  - `glslang`
- Updated Windows MSYS2 dependency installation in nightly and release
  workflows to include:
  - `mingw-w64-x86_64-vulkan-headers`
  - `mingw-w64-x86_64-vulkan-loader`
  - `mingw-w64-x86_64-glslang`
- Updated `BUILDING.md` so local Linux/macOS instructions match the CI/toolchain
  expectations.

### Renderer/runtime updates

- `src/unix/video/sdl.c`
  - added renderer-aware SDL window creation
  - `opengl` keeps the existing GL-context path
  - `vulkan` and `rtx` now create `SDL_WINDOW_VULKAN` windows and skip GL
    context creation
- `src/rend_vk/vk_main.c`
  - added SDL Vulkan integration through `SDL_Vulkan_GetInstanceExtensions(...)`
    and `SDL_Vulkan_CreateSurface(...)`
  - added SDL-aware surface destruction via `SDL_Vulkan_DestroySurface(...)`
  - enabled `VK_KHR_portability_enumeration` when present
  - enabled `VK_KHR_portability_subset` device extension when present
  - fixed zero-count instance-extension / physical-device enumeration handling
    so failures are reported cleanly instead of falling through as false
    positives
- `meson.build`
  - linked the native Vulkan renderer module against SDL3 when SDL support is
    enabled, so the SDL Vulkan helper calls resolve cleanly

## Validation

- GitHub Actions API inspection of run `23144804091` confirmed:
  - Linux failed in dependency installation
  - macOS was cancelled before execution because no runner was assigned
  - Windows failed in the build step before staging/packaging
- Local build validation:
  - `meson compile -C builddir`
  - result: success
  - rebuilt `worr.exe`, `worr_vulkan_x86_64.dll`, and related targets cleanly

## Support Status After This Change

- WORR supports macOS client/server builds.
- WORR now has a real SDL-backed Vulkan initialization path for macOS via
  MoltenVK instead of failing by construction.
- `rtx` should still be considered unsupported on current macOS Vulkan stacks.

## Remaining Follow-Up

- Validate a full nightly run after merge on GitHub-hosted Linux/macOS runners.
- Validate a clean macOS machine launch and decide whether release archives
  should eventually bundle Vulkan loader/MoltenVK dylibs instead of assuming a
  developer-style environment.
