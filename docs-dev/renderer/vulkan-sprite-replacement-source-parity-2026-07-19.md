# Native Vulkan Sprite Replacement-Source Parity

Date: 2026-07-19

Project task: `FR-02-T05`

Status: validation-backed legacy PCX-to-truecolour replacement coverage added.

## Outcome

Vulkan now has a retained parity gate for a sprite whose SP2 frame names a
legacy PCX while a same-stem truecolour PNG replacement exists. Both renderers
resolve and render the PNG. This confirms that sprite loading follows the
standard replacement-pack source-selection contract without routing any
Vulkan work through OpenGL.

The deliberately distinct source images make an incorrect resolution visible:

- `textures/parity/fr02_sprite_replace.pcx` is indexed green RGB `16 / 220 /
  64`;
- `textures/parity/fr02_sprite_replace.png` is RGBA purple RGB `188 / 40 /
  232`, with an eight-texel transparent border; and
- `sprites/worr_fr02_sprite_replacement.sp2` requests the PCX name exactly.

The observed purple core and the transparent border therefore prove that the
renderer selected and decoded the truecolour replacement, rather than merely
rendering the legacy PCX with compatible geometry.

## Native Vulkan contract

`VK_UI_LoadImageData` recognizes PCX and WAL requests as paletted and tries
same-stem `.png`, `.tga`, then `.dds` files before the original image. The
first successfully loaded candidate supplies both the RGBA upload bytes and
its resolved image metadata. `VK_Entity_AddSprite` then uses its ordinary
native source-alpha sprite path for the replacement's transparent edge.

The lookup, image upload, descriptor binding, and sprite batching all remain
inside `rend_vk`. The fixture does not change a renderer cvar or add a
test-only rendering path, and it does not use an OpenGL fallback.

## Deterministic fixture and gate

`tools/renderer_parity/generate_sprite_replacement_fixture.py` owns the map,
SP2, indexed source PCX, RGBA PNG replacement, and opaque backdrop. The
ordinary `misc_model` is loaded through the normal map/game snapshot path.
The map and SP2 are explicitly mirrored beside `pak0.pkz` so the headless
no-zlib capture runtime can load them directly.

The strict paired contract is in
`assets/renderer_parity/fr02_sprite_replacement_manifest.json`. It compares
the central 460x400 crop and requires:

- no screenshot pixel above RGB error two;
- mean absolute RGB error at most `0.0001 / 0 / 0.00005`; and
- at least 123,000 exact purple replacement-core pixels in each backend, with
  a backend intersection-over-union of `1.0`.

Focused source and fixture regressions are retained in:

- `tools/renderer_parity/test_generate_sprite_replacement_fixture.py`; and
- `tools/renderer_parity/test_vulkan_sprite_replacement_parity_source.py`.

## Validation evidence

Final staged command:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr02_sprite_replacement_manifest.json --run-root .tmp/renderer-parity/fr02-sprite-replacement-staged-final --timeout 180 --vulkan-validation
```

The 460x400 crop contains 184,000 pixels. The final paired result was:

```text
maximum RGB error:            2 / 0 / 1
mean absolute RGB error:      0.000081522 / 0 / 0.000043478
pixels above RGB error 2:     0
purple PNG replacement core: 123,904 / 123,904, IoU 1.0
green legacy-PCX pixels:      0 / 0
Vulkan VUID/validation/fatal diagnostics: none
```

The corresponding cropped Vulkan capture visibly contains the purple sprite
and its revealed blue backdrop border. Remaining `FR-02-T05` sprite work is
broader gameplay-emitter variety, not replacement-source coverage.
