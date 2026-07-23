# Delayed Native Vulkan Shadow-Array Resolution Shrinking

Date: 2026-07-19

Task ID: `FR-02-T14`

Status: superseded as the active implementation by
`vulkan-shadow-resolution-pools-2026-07-19.md`. This document retains the
historical homogeneous-array evidence that motivated the pool design.

## Outcome

The native Vulkan shadow backend now releases an oversized homogeneous shadow
array after the completed frontend view set has sustained a lower peak
resolution. Before this change, a temporary 1024px sun page could leave the
complete depth/moment array at 1024px for the remainder of the renderer
lifetime, even after every active page returned to 64px.

This is a `rend_vk` resource policy only. It does not redirect any Vulkan work
through OpenGL, and it retains the shared frontend as owner of shadow-view
selection and requested resolutions.

## Policy and lifetime

`VK_Shadow_EnsurePage` records the clamped requested resolution for every
accepted view. `VK_Shadow_EndFrame` publishes that frame's peak, and the next
`VK_Shadow_BeginFrame` evaluates it before recording new pages. If the peak
stays below the current array resolution for the existing archived
`vk_shadow_shrink_frames` interval (180 frames by default), Vulkan builds a
smaller native resource family through the established transactional
replacement path.

Growth remains immediate: a later `EnsurePage` request above the current
bound recreates the resource family at once. This prevents a returning
high-quality sun/light view from being downsampled or rendered into a too-small
target. Capacity and resolution reclamation can happen in the same begin-frame
transaction, avoiding two device-idle replacement passes when both bounds are
ready together.

The policy is especially material for moment filtering. Moving a per-layer
1024px depth/moment family to 64px reduces the base image area by 256x (with
the same corresponding mip-chain reduction); exact allocation size remains
format-, layer-count-, and driver-dependent.

## Diagnostics and test support

`r_shadow_dump` now retains separate resolution telemetry:

```text
res-shrinks=1 last-res-shrink=1024>64
```

The existing hidden smoke launcher accepts initial/default and sun-specific
resolution controls for repro work. Its Vulkan-only
`--inject-sun-resolution-drop-after-frames N` switch sets the no-archive
`vk_shadow_test_sun_resolution_drop_after_frames` hook. After `N` live sun
frames, the hook one-shot updates the public `r_shadow_sun_resolution` cvar to
64 and clears itself. It exists only to create a deterministic in-process
high-to-low transition in a headless test; normal rendering never enables it.

`tools/renderer_parity/test_vulkan_shadow_resolution_shrink_source.py` guards
completed-frame peak tracking, shared hysteresis, begin-frame coalescing with
capacity reclamation, descriptor-visible runtime telemetry, test-hook scope,
and the absence of an OpenGL renderer include. The smoke-runner contract test
covers the new controls.

## Verification

The native Vulkan DLL rebuilt and `.install/` was refreshed and repackaged:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The full renderer source suite passed:

```text
python -m unittest discover -s tools/renderer_parity -p "test_*.py"
# 358 tests passed
```

This hidden, input-disabled, audio-disabled EVSM run establishes a real
1024px-to-64px demand transition and runs with Vulkan validation enabled:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadow-resolution-shrink-final-5 \
  --renderer vulkan --scene sun-cascade --filter evsm \
  --shadow-size 64 --sun-shadow-size 1024 \
  --inject-sun-resolution-drop-after-frames 60 \
  --vk-shadow-shrink-frames 8 --wait 180 --vulkan-validation
```

The process log records the one-shot drop and ends with a 64px, seven-mip
moment array plus `res-shrinks=1 last-res-shrink=1024>64`. It contains no
`VUID`, validation error, renderer error, or fatal finding.

The existing paired alpha-tested shadow-caster gate remained exact after the
allocation policy change: all 235,200 pixels in the 560x420 crop have zero RGB
error between OpenGL and Vulkan, with Vulkan validation enabled.

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_alpha_shadow_manifest.json \
  --run-root .tmp/renderer-parity/fr01-alpha-shadow-resolution-shrink \
  --timeout 180 --vulkan-validation
```

## Remaining FR-02-T14 work

Per-resolution resource pools now replace the homogeneous array: page-to-pool
routing is present in the native Vulkan receiver descriptor/shader contract,
and each pool has its own delayed capacity/reclamation lifetime. See
`vulkan-shadow-resolution-pools-2026-07-19.md` for the current implementation
and verification evidence.
