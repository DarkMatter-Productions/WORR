# OpenGL Refraction State Width Repair and Vulkan Parity

Date: 2026-07-19

Task IDs: `FR-01-T10`, `FR-02-T13`

Status: repaired a dormant OpenGL reference path, retained the native Vulkan
liquid implementation, and refreshed the affected paired-capture contract.
The active liquid, viewweapon, HDR, entity-ordering, MSAA, and DOF routes now
have real cross-renderer evidence. Broader liquid-material coverage remains
open under `FR-01-T10`.

## Root cause

OpenGL declares `GLS_REFRACT_ENABLE` at bit 38. `mface_t.statebits` was an
`int`, however, so `statebits_for_surface` silently truncated that bit while
the world BSP registered. As a result, old OpenGL capture baselines rendered
ordinary turbulent transparent water even after `gl_warp_refraction` was
enabled. Vulkan's independent native liquid scene-copy path was active, which
made a small real effect look like a Vulkan-only discrepancy.

`inc/common/bsp.h` now retains the state word as `uint64_t`. The OpenGL world
batch hash in `src/rend_gl/surf.c` now hashes a typed 64-bit state member too;
otherwise faces that differ only by a high-order shader flag could still
coalesce incorrectly. The state is populated only during BSP registration, so
the fix adds no Vulkan frame work and does not introduce an OpenGL fallback in
any Vulkan code path.

`tools/renderer_parity/test_gl_refraction_state_bits_source.py` locks the
contract: the OpenGL refraction flag remains bit 38, common faces preserve a
64-bit state field, and the world batch hash consumes the whole state word.

## Capture activation

The three refraction configs now disable both renderer-specific cvars before
the map command and enable them only after the registration wait:

- `assets/renderer_parity/fr01_viewweapon_shell_bloom_refraction.cfg`
- `assets/renderer_parity/fr01_hdr_viewweapon_shell_bloom_refraction.cfg`
- `assets/renderer_parity/fr01_dof_refraction.cfg`

This is necessary for OpenGL because allocation of its refraction target is
gated by both the cvar transition and the registered transparent-warp world.
It also makes the paired fixtures prove active effects rather than inherited
startup state.

## MSAA and DOF evidence

With real OpenGL refraction active, the gun-free 4x-MSAA DOF fixture has a
localized backend filter/raster difference at a high-contrast refracted water
boundary: 1,234 of 307,200 crop pixels (`0.4016927083%`) exceed RGB error 1,
confined to rows 569--571. Its measured MAE is
`0.490332 / 0.143532 / 0.485186`, within the existing
`0.6 / 0.2 / 0.6` channel limits.

The prior zero-pixel threshold was established while the OpenGL high bit was
truncated, so it tested an inactive reference effect. The active manifest now
uses a still-narrow `0.5%` over-error-1 limit:
`assets/renderer_parity/fr01_multisample_depth_dof_refraction_manifest.json`.
The limit is deliberately smaller than one scanline of the 640x480 capture
crop and preserves the existing mean-error guard. It does not relax ordinary
MSAA or DOF controls, which remain strict.

## Retained validation

All runtime launches used the hidden native-surface mode with input and audio
disabled. Vulkan runs below used `VK_LAYER_KHRONOS_validation`.

| Manifest | Result with real OpenGL refraction |
|---|---|
| `fr01_multisample_depth_dof_refraction_manifest.json` | Passes the active 0.5% boundary envelope; 0.4016927083% over RGB error 1 |
| `fr01_viewweapon_shell_bloom_refraction_manifest.json` | Passes; 128,000 crop pixels, zero over RGB error 1 |
| `fr01_hdr_viewweapon_shell_bloom_refraction_manifest.json` | Passes; 128,000 crop pixels, zero over RGB error 2 |
| `fr01_liquid_entity_ordering_manifest.json` | Passes live refractive ordering and strict unrefracted control |

The final staged executable and both renderer DLLs were rebuilt before these
captures. The shared BSP header change also required rebuilding
`worr_engine_x86_64.dll`, which owns the BSP structures passed to renderer
DLLs.

## Commands

```text
python -m unittest discover -s tools/renderer_parity -p 'test_gl_refraction_state_bits_source.py'
python -m unittest discover -s tools/renderer_parity -p 'test_generate_viewweapon_shell_bloom_refraction_fixture.py'
python -m unittest discover -s tools/renderer_parity -p 'test_shared_multisample_control_source.py'
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_opengl_x86_64.dll worr_vulkan_x86_64.dll worr_engine_x86_64.dll
python tools/test_package_assets.py
python tools/refresh_install.py --build-dir builddir-win
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_multisample_depth_dof_refraction_manifest.json --run-root .tmp/renderer-parity/refraction-state-width-real-gl --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_viewweapon_shell_bloom_refraction_manifest.json --run-root .tmp/renderer-parity/real-gl-viewweapon-refraction --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_hdr_viewweapon_shell_bloom_refraction_manifest.json --run-root .tmp/renderer-parity/real-gl-hdr-viewweapon-refraction --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_liquid_entity_ordering_manifest.json --run-root .tmp/renderer-parity/real-gl-liquid-entity-ordering --timeout 180 --vulkan-validation
```

## Follow-up

`FR-01-T10` remains open for broader liquid texture families and gameplay
receivers. `FR-02-T13` retains the post-process task for more HDR/scaled
MSAA combinations and product-level performance measurements.
