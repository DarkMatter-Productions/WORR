# Native Vulkan 2D LUT Coordinate Parity

Date: 2026-07-16

Task IDs: `FR-01-T12`, `FR-02-T13`

Status: implemented and validation-tested for both supported 2D LUT strip
orientations.

## Issue

Both renderers accept a 2D NxN lookup-table strip in either orientation:
horizontal `N*N x N` or vertical `N x N*N`. The Vulkan final compositor had
the required cvars, native descriptor, image validation, and trilinear slice
blend, but divided the red/green intra-slice coordinate by `N` before adding
the slice origin. OpenGL uses that value directly in texel space, then applies
the texture's inverse width/height once.

For all LUT sizes larger than one, the old Vulkan expression selected the
first texel columns/rows of each slice instead of the interpolated red/green
location. It was therefore an implementation parity bug, not an intentional
filtering difference.

## Change

`vk_postprocess.frag` now calculates:

```glsl
float u = lut_color.r * (size - 1.0) + 0.5;
float v = lut_color.g * (size - 1.0) + 0.5;
```

The existing orientation branches then apply the same formulas as the OpenGL
shader:

- Horizontal strips sample `(slice * size + u) / width, v / height`.
- Vertical strips sample `u / width, (slice * size + v) / height`.

The image remains owned and sampled entirely by Vulkan; no OpenGL data or
renderer path participates. `test_vulkan_color_correction_source.py` now
locks the texel-space expressions and rejects the premature division.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools.renderer_parity.test_vulkan_color_correction_source
python -m unittest tools.renderer_parity.test_generate_color_lut_fixture tools.renderer_parity.test_color_lut_fixture
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_color_lut_manifest.json --run-root .tmp/renderer-parity/color-lut-both --vulkan-validation
```

## Runtime evidence

`fr01_color_lut_4x16.tga` and
`fr01_color_lut_4x16_vertical.tga` are deterministic non-identity horizontal
and vertical `4^3` LUTs: each inverts red and blue per slice. The
validation-enabled native capture at `.tmp/renderer-parity/color-lut-both`
exact-compares all 50,000 pixels in each fixed crop. Both orientations produce
the locked non-identity `[24, 40, 72]` output probe with mask IoU `1.0`; no
Vulkan validation warnings or errors occur.
