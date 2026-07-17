# Model-Skin Glowmap Visual Parity

Date: 2026-07-15

Task ID: `FR-01-T11`

Status: paired model-skin visual gate complete.

## Purpose

This is the model-side companion to the wall glowmap gate. It verifies the
native Vulkan skin-emission path in a normal game scene, rather than relying
only on the shader/source checks. It also keeps the emission assertion separate
from the known non-glow inline-BSP lighting difference in the shared fixture.

## Deterministic scene

`tools/renderer_parity/generate_model_glowmap_fixture.py` creates
`assets/maps/worr_fr01_model_glowmap.bsp`. The map uses a normal `misc_model`
entity to request the stock rerelease path
`models/objects/dmspot/tris.md2` at a fixed origin, scale, and angle. This is
the ordinary game registration route, not a renderer test hook. The final
Vulkan process log also records its available MD5 replacement being loaded;
the requested stock model path and companion skin resolution are therefore
exercised through the real replacement policy.

`assets/renderer_parity/fr01_model_glowmap.cfg` enables `r_glowmaps`, fixes
both renderer intensity controls at one, and takes a no-window screenshot from
a fixed camera. It uses `r_fullbright 1` only for this skin-emission scene: the
setting removes unrelated receiver-lighting variation while preserving the
post-lighting skin glow contribution. The crop `[570, 470, 310, 110]` contains
the visible model and excludes the shared fixture's inline BSP model.

The generated map now uses the unpaired `parity/fr01_to_background` material,
so the later bloom gate can isolate the model emitter from the wall-glow
fixture. This direct-material configuration explicitly keeps both `gl_bloom`
and `vk_bloom` off; bloom evidence is recorded in
`vulkan-model-bloom-emission-visual-parity-2026-07-16.md`.

## Gate contract

`fr01_model_glowmap_manifest.json` limits paired mean absolute error to
`[1.0, 0.3, 0.3]`, permits at most `0.8%` of crop pixels beyond RGB error 16,
and requires the bright orange-red glow-emission mask
`[160, 0, 0]` through `[255, 60, 30]`. That mask must contain at least 1,800
pixels per backend, have no more than four-percent count divergence, and have
intersection-over-union of at least `0.92`.

An exploratory control capture with `r_glowmaps 0` verified that this selected
skin mask collapses from 1,864 to 2 pixels in OpenGL and from 1,940 to 4 pixels
in Vulkan. The control is evidence only; the checked-in scene always restores
`r_glowmaps 1`.

## Evidence

The compliant no-window paired run with Vulkan validation against the
post-build staged `.install` tree at
`.tmp/renderer-parity/fr01-model-glowmap-final-staged` passed with:

- 34,100 model-region pixels compared;
- mean absolute RGB error `[0.90633, 0.25862, 0.20897]`;
- 257 pixels (`0.75367%`) above RGB error 16;
- 1,864 OpenGL and 1,940 Vulkan bright-emission pixels;
- 1,827 intersecting mask pixels, 1,977 union pixels, and `0.92413` IoU.

## Regression and validation

`test_generate_model_glowmap_fixture.py` locks the generated BSP, the normal
model entity contract, the enabled fixed-intensity capture configuration, and
the focused emission-manifest threshold. The native source test continues to
lock model glow-flag propagation and the skin fragment emission operation.

```text
python tools/renderer_parity/generate_model_glowmap_fixture.py --validate
python -m unittest tools/renderer_parity/test_generate_model_glowmap_fixture.py
python -m unittest tools/renderer_parity/test_vulkan_glowmaps_source.py
python tools/package_assets.py --assets-dir assets --install-dir .install --base-game basew
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_glowmap_manifest.json --run-root .tmp/renderer-parity/fr01-model-glowmap-final-staged --vulkan-validation
```

All launches above use the repository's hidden native-surface headless capture
mode. No interactive client is started.
