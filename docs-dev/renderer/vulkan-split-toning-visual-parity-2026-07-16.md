# Vulkan/OpenGL Split-Toning Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: implemented and validation-tested.

## Issue

Vulkan already executed its native split-toning stage whenever
`vk_color_split_strength` was non-zero. OpenGL populated the equivalent
uniforms, but only allocated the final post-process target for bloom, broad
colour correction, or HDR. A split-tone-only or LUT-only configuration could
therefore silently skip its final shader in OpenGL, leaving the two renderers
visibly different despite matching tone equations.

## Change

`GL_BindFramebuffer` now enables the OpenGL final target when either:

- `gl_color_split_strength` is non-zero; or
- a valid `gl_color_lut` has non-zero intensity.

Dedicated modified-count tracking recreates the target when either activation
state changes. Vulkan needs no fallback or route change: its existing native
final stage already follows OpenGL's luminance/pivot/smoothstep/tint order.

## Runtime evidence

`fr01_split_toning.cfg` disables bloom, HDR, broader colour correction, LUT,
DOF, and CRT, then applies shadow `#402060`, highlight `#ffd080`, strength
`0.85`, and balance `-0.10` to both renderers. Its strict manifest capture at
`.tmp/renderer-parity/split-toning-fixed` exact-compares all 50,000 pixels:

| Check | Result |
| --- | ---: |
| Maximum RGB error | `0 / 0 / 0` |
| Mean absolute RGB error | `0 / 0 / 0` |
| Pixels above error 0 | `0` |
| Non-identity `[9, 10, 34]` probe IoU | `1.0` |

The validation-enabled Vulkan process contains no VUID or validation error.

## Verification

```text
python -m unittest tools.renderer_parity.test_opengl_postfx_activation_source tools.renderer_parity.test_vulkan_color_correction_source tools.renderer_parity.test_split_toning_fixture
ninja -C builddir-win worr_opengl_x86_64.dll worr_vulkan_x86_64.dll
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_split_toning_manifest.json --run-root .tmp/renderer-parity/split-toning-fixed --vulkan-validation
```

All captures use the hidden/headless runtime with input disabled.
