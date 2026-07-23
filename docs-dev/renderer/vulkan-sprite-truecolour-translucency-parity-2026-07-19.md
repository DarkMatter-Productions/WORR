# Native Vulkan Truecolour and Entity-Alpha Sprite Parity

Date: 2026-07-19

Project task: `FR-02-T05`

Status: validation-backed parity coverage added for the two remaining normal
sprite blend modes: a truecolour source-alpha sprite and an explicitly
translucent game entity.

## Outcome

The retained `worr_fr02_sprite_blend` fixture proves that Vulkan's existing
native `VK_Entity_AddSprite` blend path selects and renders the same modes as
OpenGL's `GL_DrawSpriteModel`:

- a truecolour RGBA PNG with transparent, 50%-alpha, and opaque texels; and
- a stock paletted BFG sprite whose ordinary map entity supplies `alpha 0.5`
  and `renderFX 32` (`RF_TRANSLUCENT`).

The map uses normal `misc_model` entities and the game snapshot path. It does
not use a client cvar, renderer test hook, or any OpenGL fallback. Cgame
converts the authored alpha/renderFX state into the renderer's ordinary
`RF_TRANSLUCENT` entity state before Vulkan sees it.

## Native contract

`VK_Entity_AddSprite` reads image flags retained by `VK_UI` and selects the
native alpha pipeline whenever an image has transparent texels or the entity
is translucent. The uploaded image remains an RGBA Vulkan texture, while the
flags preserve the legacy distinction needed for the separate paletted cutout
case. These blend sprites retain depth testing with depth writes disabled,
source-alpha / one-minus-source-alpha blending, and the existing two-triangle
billboard submission.

This is complementary to the earlier paletted-cutout implementation:

- opaque paletted transparent sprites use the native cutout pipeline;
- non-paletted transparent sprites use normal source-alpha blending; and
- `RF_TRANSLUCENT` forces normal source-alpha blending even for a paletted
  sprite.

No pipelines or descriptors are created on the frame hot path, and no Vulkan
renderer path routes into OpenGL.

## Deterministic fixture

`tools/renderer_parity/generate_sprite_blend_fixture.py` generates all of the
fixture-owned inputs:

- `maps/worr_fr02_sprite_blend.bsp`;
- `sprites/worr_fr02_truecolor_alpha.sp2` pointing to the RGBA source image;
- `textures/parity/fr02_sprite_truecolor_alpha.png`; and
- the opaque generated backdrop.

The 64x64 truecolour source deliberately contains alpha `0`, `128`, and
`255` regions. The two billboards are spatially separated so each blend mode
has a stable, individually observable mask; transparent world/entity ordering
continues to have its own dedicated fixture. The map and fixture SP2 are
included in the explicit loose staging set because the headless no-zlib
runtime must resolve both directly.

The capture contract is in
`assets/renderer_parity/fr02_sprite_blend_manifest.json`, with source and
fixture regression coverage in:

- `tools/renderer_parity/test_generate_sprite_blend_fixture.py`; and
- `tools/renderer_parity/test_vulkan_sprite_blend_parity_source.py`.

## Validation evidence

Final staged command:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_sprite_blend_manifest.json --run-root .tmp/renderer-parity/fr02-sprite-blend-staged-final --timeout 180 --vulkan-validation
```

The 760x400 crop contains 304,000 pixels. Its final paired result was:

```text
maximum RGB error:           2 / 0 / 1
mean absolute RGB error:     0.002404605 / 0 / 0.001184211
pixels above RGB error 2:    0
truecolour opaque mask:      53,824 / 53,824, IoU 1.0
truecolour 50% mask:         60,296 / 60,296, IoU 1.0
explicit-translucent BFG:     1,984 / 1,984, IoU 1.0
Vulkan VUID/validation/fatal diagnostics: none
```

The exact masks validate both source-alpha coverage levels and the independent
entity-alpha route, rather than accepting a similar overall screenshot.

## Remaining scope

The retained sprite lanes now cover fogged BFG receiving, opaque paletted
cutout, truecolour source alpha, explicit entity translucency, and legacy-PCX
requests resolved through a truecolour replacement (documented separately in
`vulkan-sprite-replacement-source-parity-2026-07-19.md`). Fully opaque
truecolour sprites also retain OpenGL's no-depth-write state without enabling
blending (documented separately in
`vulkan-sprite-opaque-depth-write-parity-2026-07-19.md`). Future `FR-02-T05`
expansion should add broader gameplay-emitter varieties; it is not a reason to
redirect any Vulkan path to OpenGL.
