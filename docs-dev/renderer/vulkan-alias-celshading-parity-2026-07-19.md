# Native Vulkan Alias-Model Cel-Shading Parity

Date: 2026-07-19

Task IDs: `FR-01-T04`, `FR-01-T07`, `FR-01-T15`

## Outcome

Native Vulkan now implements the OpenGL alias-model cel-shading pass. The
renderer exposes `vk_celshading`, defaulting to zero like `gl_celshading`.
When enabled, eligible MD2 and MD5 meshes replay their already-submitted
geometry through a Vulkan-only black wireframe pipeline. No Vulkan path is
redirected to OpenGL.

The replay preserves OpenGL's eligibility rules: translucent, shell, and
tracker entities do not participate. It runs after opaque alias geometry and
before general translucent work; depth-hacked/view-weapon alias batches use
the same replay under their existing reduced-depth viewport. Item colourize
uses the untinted base mesh once rather than replaying its separate tint
overlay.

## Native implementation

`vk_entity.frag` now has a compact `VK_ENTITY_CELSHADING` entry point that
outputs black with the original 700-unit camera-distance alpha fade and does
not sample skins, lightmaps, glowmaps, or shadow receivers. The embedded SPIR-V
generator emits this fragment module alongside the ordinary entity modules.

The entity pipeline creator adds a dedicated alpha-blended line raster variant
for dynamic CPU meshes, GPU MD2 interpolation, and GPU MD5 skinning. It uses
dynamic line width and each batch retains its own faded width, so GPU instances
with different distance fades split only when required. Normal vertex,
descriptor, and static model buffers are reused; the pass allocates no
per-model geometry and does not duplicate model transforms.

Vulkan non-solid fill and wide lines are optional device features. Device
selection records `fillModeNonSolid`, `wideLines`, and the line-width range;
the logical device enables them only when both are available. Unsupported
devices safely leave `vk_celshading` inactive rather than creating an invalid
pipeline. Width is clamped to the negotiated Vulkan range before command
recording.

The scene uses a negative-height Vulkan viewport. That reverses the practical
winding relative to OpenGL's `GL_FRONT` cel cull, so the equivalent Vulkan
pipeline uses `VK_CULL_MODE_BACK_BIT`. The initial `FR-01` capture exposed the
opposite choice as visible internal triangle diagonals; switching the native
cull restores the OpenGL silhouette-only replay.

## Regression coverage

`tools/renderer_parity/test_vulkan_celshading_source.py` checks the optional
feature negotiation, untextured black shader specialization, native line
raster state, per-batch width replay, and placement between opaque and alpha
entity phases.

The new `fr01_model_celshading` headless fixture forces the stock MD2 path and
uses one-pixel cel settings, the portable common line width on the validation
adapter. Its crop covers 144,000 pixels and requires an exact black contour
mask: both backends produced 280 black pixels, zero count delta, and IoU `1.0`.
The full crop had mean absolute RGB error `0.00335 / 0.00470 / 0.01086`, with
two pixels (0.00139%) over RGB threshold 20. Vulkan validation reported no
VUID or validation error.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python -m unittest tools.renderer_parity.test_vulkan_celshading_source
meson compile -C builddir-win worr_vulkan_x86_64
python tools/refresh_install.py --build-dir builddir-win --assets-dir assets --install-dir .install --base-game basew --archive-name pak0.pkz
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_celshading_manifest.json --run-root .tmp/renderer-parity/fr01-model-celshading-cull-fix --vulkan-validation --timeout 180
```
