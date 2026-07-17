# Native Vulkan Gameplay DOF Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: fixed-focus, centre-depth, wide explicit-range, and menu-rectangle
state gameplay DOF have strict, deterministic native Vulkan/OpenGL visual
gates. Isolated rectangle clipping and broader dynamic-scene coverage remain
open.

## Receiver and activation

`worr_fr01_bmodel_first_frame.bsp` supplies a deterministic depth-separated
receiver: the inline brush model occupies the approximately 224--288 unit
range and the high-contrast world receiver is at 512 units. The fixed-focus
pair uses a 250-unit focus and a 32-unit blur range, so the inline model
remains sharp while the world receiver exercises the depth-aware blur
composite. The centre-focus pair passes zero for both controls: each backend
samples the central brush depth and derives the normal automatic
`max(64, focus * 0.25)` range.

The capture reaches real gameplay DOF through `inven`, which makes the normal
inventory layout contribute `refdef.dof_strength`. `cl_draw2d 0` removes that
layout from the screenshot without suppressing the refdef state. The new
manifest `launch_cvars` facility applies `r_dof` before renderer startup;
that is required because the shared control is latched. It validates ASCII
single-line cvar name/value pairs before they are appended to the hidden
native-surface command line.

`fr01_dof_disabled.cfg` follows the identical gameplay route with `r_dof=0`.
It is therefore a direct control for the native depth-aware path rather than a
synthetic post-process substitute.

The compact translucent `quit_confirm` cgame page supplies a real
`dof_rect` of `0,316–960,396` after the normal inventory transition.
`fr01_dof_menu_rect.cfg` opens that shipped page with RmlUi explicitly off,
so the test covers the production cgame rectangle route rather than a test
only renderer hook. OpenGL's `FBO_POST` retains the full pre-menu DOF
composite around its clipped draw. Vulkan keeps an equivalent complete native
current-frame composite, avoiding a stale-image dependency while matching the
visible OpenGL result.

## Native parity repair

The Vulkan path was native before this repair, but its shared blur setup did
not match OpenGL: it supplied a constant sigma of 1 and recorded eight
alternating passes. OpenGL uses its shared Gaussian equation even for the
quarter-size DOF buffers:

```text
sigma = clamp(bloom_sigma, 1, 25) * swapchain_height / 2160
        * 4 / clamp(bloom_downscale, 1, 8)
```

It records four alternating passes total (two horizontal and two vertical),
not four X/Y pairs. `VK_PostProcess_RecordDof` now implements that same
equation and pass count in Vulkan. No scene data crosses to OpenGL; the
existing Vulkan scene copy, sampled depth view, intermediate images, and
depth composite remain the only rendering route.

The native Gaussian blur subsequently adopted OpenGL's paired-bilinear-tap
formulation. It keeps the exact pass topology and strict visual gates while
removing approximately half of the texture fetches per blur pass. The
measured GPU evidence and the launch-safe active-DOF performance fixture are
recorded in `vulkan-dof-paired-tap-performance-2026-07-16.md`.

## Captured evidence

The final headless validation run used
`.tmp/renderer-parity/dof-final` at `960x720`, with a `[160, 120, 640, 480]`
receiver crop (307,200 pixels) and Vulkan validation enabled.

| Scene | Maximum RGB error | MAE RGB | Pixels above RGB error 1 |
| --- | --- | --- | ---: |
| Fixed-focus gameplay DOF | `1 / 1 / 1` | `0.004466 / 0.004867 / 0.002168` | 0 |
| DOF-disabled control | `0 / 0 / 0` | `0 / 0 / 0` | 0 |
| Centre-focus, automatic-range gameplay DOF | `1 / 1 / 1` | `0.001484 / 0.003161 / 0.001484` | 0 |
| Centre-focus, automatic-range disabled control | `0 / 0 / 0` | `0 / 0 / 0` | 0 |
| Compact-menu rectangle state | `1 / 1 / 1` | `0.004063 / 0.011849 / 0.004063` | 0 |
| Compact-menu rectangle disabled control | `0 / 0 / 0` | `0 / 0 / 0` | 0 |
| Wide explicit range (512 units) | `1 / 1 / 1` | `0.000938 / 0.001735 / 0.000938` | 0 |
| Wide explicit-range disabled control | `0 / 0 / 0` | `0 / 0 / 0` | 0 |

The manifest locks all eight scenes to zero pixels beyond one RGB level and an
MAE ceiling of `0.1 / 0.1 / 0.1`. Both renderer processes completed and Vulkan
validation reported no error. The active scenes differ materially from their
paired disabled images, confirming that the gates exercise DOF rather than a
no-op presentation path.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest discover -s tools/renderer_parity -p 'test_*.py'
python tools/test_package_assets.py
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_dof_manifest.json --run-root .tmp/renderer-parity/dof-final --vulkan-validation
```

The capture harness uses the repository hidden native-surface mode. It does
not create an interactive client window, initialise client input, or capture
the mouse.
