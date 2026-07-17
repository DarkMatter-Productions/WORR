# Vulkan Underwater Waterwarp Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T10`

Status: the native Vulkan full-screen underwater waterwarp now has a strict,
deterministic OpenGL/Vulkan visual gate. It corrects an oversized world-warp
constant that made the prior native full-screen path visibly diverge.

## Defect and native correction

Vulkan already activated its three-vertex compositor only for a real
`RDF_UNDERWATER` refdef and a nonzero `vk_waterwarp`; it did not use an
OpenGL fallback. The fragment shader had nevertheless used the world-material
warp constants (`0.0625` amplitude and `4` phase) in the 2D compositor.

OpenGL's `shader_setup_2d` supplies `GLS_WARP_ENABLE` with `0.0025` amplitude
and `10*pi` phase. Its 2D projection is top-origin while the Vulkan copied
scene is addressed in framebuffer coordinates. `vk_postprocess.frag` now
preserves OpenGL's source-texture convention explicitly:

```text
tc = gl_FragCoord.xy / output_size
warp_tc = (tc.x, 1 - tc.y)
warp_offset = 0.0025 * sin(warp_tc.yx * (10*pi) + time)
tc += warp_offset * (1, -1)
```

Only the waterwarp sample coordinate changes. The copied scene, UI ordering,
post-process resource lifetime, and ordinary non-waterwarp frames are
unchanged. The generated SPIR-V remains embedded in the native Vulkan DLL.

## Deterministic receiver

`generate_underwater_waterwarp_fixture.py` derives from the established
minimal flow BSP but marks only its playable leaf as `CONTENTS_WATER` (bit 5).
The player consequently reaches `RDF_UNDERWATER` through normal game and
client prediction code. The receiver uses an opaque, static high-contrast
checkerboard; it intentionally does not set `SURF_WARP`, so the comparison
measures the full-screen underwater pass rather than material animation.

`fr01_underwater_waterwarp.cfg` enables both `gl_waterwarp` and
`vk_waterwarp`, disables the screen-tint blend with `cl_add_blend 0`, freezes
the fixed-time scene with `pause`, then captures a `[160, 120, 640, 480]`
receiver. `fr01_underwater_waterwarp_disabled.cfg` is the paired native-path
control: water contents remain real but both waterwarp cvars are zero.

## Captured evidence

The final headless validation run used
`.tmp/renderer-parity/underwater-waterwarp-gl-constants` at `960x720` with
Vulkan validation enabled.

| Scene | Pixels | Maximum RGB error | MAE RGB | Pixels above error 2 |
| --- | ---: | --- | --- | ---: |
| Underwater full-screen waterwarp | 307,200 | `1 / 1 / 2` | `0.05694 / 0.51707 / 0.80232` | 0 |
| Underwater waterwarp-disabled control | 307,200 | `1 / 1 / 1` | `0.00921 / 0.53576 / 0.86227` | 0 |

The manifest requires zero pixels above RGB error 2 and caps MAE at
`0.1 / 0.6 / 0.9`. The active receiver is materially different from its
disabled control, so passing evidence proves both genuine underwater
activation and Vulkan/OpenGL presentation parity. Both processes completed
and Vulkan validation reported no error.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/generate_underwater_waterwarp_fixture.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest discover -s tools/renderer_parity -p 'test_*.py'
python tools/test_package_assets.py
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_underwater_waterwarp_manifest.json --run-root .tmp/renderer-parity/underwater-waterwarp-final --vulkan-validation
```

All capture commands use the repository's hidden native-surface mode. They do
not create an interactive client window, initialise client input, or capture
the mouse.
