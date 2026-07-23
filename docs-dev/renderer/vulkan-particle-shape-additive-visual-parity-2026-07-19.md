# Native Vulkan Particle Shape and Additive Visual Parity

Date: 2026-07-19

Task IDs: `FR-01-T01`, `FR-01-T12`, `FR-02-T05`

Status: completed for the deterministic native raster particle field: normal
and additive blending, all three legacy shapes, and the authored global-fog
receiver are now paired and validation-backed.

## Outcome

Vulkan now owns the full legacy particle-texture shape control through
`vk_particle_shape`:

- `0`: radial falloff (default);
- `1`: nearest-filtered hard square; and
- `2`: tighter soft radial falloff.

It matches the corresponding `gl_partshape` texture construction, including
the legacy byte truncation used for the alpha channel. The prior Vulkan
round-to-nearest alpha conversion was usually invisible for normal blending,
but compounded in a dense additive field and produced errors up to RGB four.
The native texture now uses the same truncation, while the existing additive
pipeline continues to use OpenGL's `SRC_ALPHA, ONE` colour factors.

`vk_particle_shape` is an archived Vulkan-specific cvar, consistent with the
existing `vk_particle_style` control. A runtime shape change retires the two
in-flight frame slots before replacing only the native particle image and its
descriptor; ordinary particle frames still reuse the pre-created alpha or
additive pipeline and descriptor-compatible batch. No per-particle allocation,
pipeline build, draw submission, or OpenGL route is added.

## Deterministic fixture correction

The debug-only `cl_testparticles` field previously reset its count but did not
initialize every particle's scale and brightness. Since the field storage is
reused, that could make the capture depend on stale values or generate
degenerate triangles. `V_TestParticles` now clears each item and explicitly
sets `scale = 1` and `brightness = 1`, making the fixture a visible,
deterministic renderer receiver rather than a background-only test.

## Headless validation

`fr02_particle_additive_manifest.json` launches the same 640 x 500 view for
the three shapes with additive mode enabled. All captures use a staged,
headless renderer, validation-enabled Vulkan, and a real initialized
8,192-particle field.

| Shape | Maximum RGB | Mean absolute RGB | Pixels above threshold | Visible-field probe |
|---|---:|---:|---:|---|
| Radial (`0`) | `2 / 2 / 2` | `0.006338 / 0.005319 / 0.006281` | 0 of 320,000 above 2 | 24,495 / 24,506 pixels, IoU `0.999551` at tolerance 2 |
| Square (`1`) | `0 / 0 / 0` | `0 / 0 / 0` | 0 of 320,000 above 2 | 32,410 pixels on both backends, IoU `1.0` at tolerance 1 |
| Soft radial (`2`) | `1 / 1 / 1` | `0.001688 / 0.001363 / 0.001666` | 0 of 320,000 above 2 | 27,377 / 27,379 pixels, IoU `0.999927` at tolerance 1 |

The pre-existing non-additive authored global-fog fixture was revalidated after
the debug-field correction. Its 320,000-pixel crop has maximum RGB
`1 / 1 / 1`, mean absolute RGB `0.021778 / 0.001706 / 0.025975`, and no pixels
above one. Its exact fogged-particle-colour mask has 24,255 OpenGL and 24,251
Vulkan pixels at IoU `0.998929`. Vulkan validation emitted no VUID or error in
either matrix.

## Regression coverage

`tools/renderer_parity/test_vulkan_particle_parity_fixture.py` guards the
native additive blend selection, shape cvar clamping/rebuild path, legacy alpha
truncation, all three fixture configs, and the visible debug-field
initialization. `tools/renderer_parity/test_vulkan_fog_source.py` guards the
non-additive fog field's explicit shape and bounded visual contract.

Reproduction:

```powershell
ninja -C builddir-win worr_engine_x86_64.dll worr_vulkan_x86_64.dll worr_opengl_x86_64.dll
python -m unittest tools.renderer_parity.test_vulkan_particle_parity_fixture tools.renderer_parity.test_vulkan_fog_source
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_particle_additive_manifest.json --run-root .tmp/renderer-parity/fr02-particle-additive-shapes --timeout 180 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_particle_fog_manifest.json --run-root .tmp/renderer-parity/fr01-particle-fog-visible-final --timeout 180 --vulkan-validation
```

## Scope boundary

This closes the deterministic raster particle field and its shape/style
controls. Gameplay-specific emitters, exotic replacement textures, and
product-level performance budgets remain broader nightly/performance work.
