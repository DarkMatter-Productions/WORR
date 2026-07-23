# Native Vulkan Paletted Sprite Cutout Parity

Date: 2026-07-19

Project task: `FR-02-T05`

Status: implemented and covered by a retained validation-enabled OpenGL/Vulkan
capture. The remaining sprite matrix work is non-paletted and explicitly
translucent/replacement-source variety.

## Outcome

Vulkan now preserves the resolved source image's legacy paletted property and
uses a dedicated native cutout pipeline for opaque paletted sprites with
transparent texels. It no longer approximates that OpenGL route with alpha
blending.

The authored BFG sprite capture passes at a maximum RGB difference of
`0 / 0 / 1` over 144,000 pixels, with no pixel exceeding RGB error one. Its
exact-colour `48 / 220 / 96` visible cutout mask contains 34,650 pixels in
both renderers at IoU `1.0`. The Vulkan validation-layer run is clean.

## OpenGL contract

`GL_DrawSpriteModel` in `src/rend_gl/main.c` disables depth writes for every
sprite. For a fully opaque entity whose image has transparent texels, it:

- uses `GLS_ALPHATEST_ENABLE` when the source image is paletted;
- uses regular source-alpha blending for a non-paletted image; and
- uses blending whenever `RF_TRANSLUCENT` or entity alpha requires it.

The alpha-test cutoff is applied in the entity fragment shader at alpha
`0.666`. A cutout is not a blended sprite: surviving edge samples write their
unblended RGB while depth writes remain disabled.

## Native implementation

`src/rend_vk/vk_ui.c` now carries encoding information from the file that
actually resolved, rather than only from the requested filename. The loader
marks PCX/WAL, colour-map TGA, and indexed PNG sources with `IF_PALETTED`.
Consequently, a PNG/TGA/DDS texture override of a PCX remains truecolour, as
it does in OpenGL.

`VK_Entity_AddSprite` obtains those flags through `VK_UI_GetImageFlags`. A
non-translucent, fully opaque paletted image with transparent texels receives
`VK_ENTITY_VERTEX_ALPHATEST` and is submitted as a native cutout batch. Other
transparent and explicitly translucent sprite cases retain the existing
native alpha-blend batching route.

The new `VK_ENTITY_BLEND_CUTOUT` pipelines are created with normal swapchain
entity resources, not on a draw. They have:

- depth test enabled and depth writes disabled;
- colour blending disabled; and
- the existing native entity fragment cutoff flag.

Depth-hack sprites have an equivalent pre-created cutout pipeline. The
regular transient sprite stream still emits two billboard triangles and
coalesces only compatible adjacent batches; no OpenGL renderer code is
included or called, and the change adds no per-frame descriptor allocation or
pipeline creation.

## Retained fixture and evidence

`assets/renderer_parity/fr02_sprite_cutout.cfg` uses the existing authored
`worr_fr01_sprite_fog` map's ordinary `misc_model` BFG sprite, but disables
both `gl_fog` and `vk_fog`. Its fixed camera isolates the source-alpha/cutout
edge from fog attenuation. The paired contract is kept in
`assets/renderer_parity/fr02_sprite_cutout_manifest.json` and protected by
`tools/renderer_parity/test_vulkan_sprite_cutout_source.py`.

Final headless command:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_sprite_cutout_manifest.json --run-root .tmp/renderer-parity/fr02-sprite-cutout-staged-final --timeout 180 --vulkan-validation
```

Result:

```text
crop pixels:                 144,000
maximum RGB error:           0 / 0 / 1
mean absolute RGB error:     0.0 / 0.0 / 0.000472222
pixels above RGB error 1:    0
exact cutout mask:           34,650 / 34,650
cutout mask IoU:             1.0
Vulkan VUID/validation/fatal diagnostics: none
```

The focused source test passed four tests and `ninja -C builddir-win
worr_vulkan_x86_64.dll` completed before the staged Vulkan DLL/PDB and fixture
config were refreshed under `.install/`.

## Remaining scope

The fogged BFG receiver remains covered separately by
`vulkan-sprite-fog-parity-2026-07-15.md`. This closure specifically adds the
previously missing unfogged paletted-cutout contract. Nightly sprite expansion
should add non-paletted alpha images, explicit `RF_TRANSLUCENT` alpha values,
and replacement-source cases. Fence coverage remains separate alpha-test
world/model work under `FR-02-T05`.
