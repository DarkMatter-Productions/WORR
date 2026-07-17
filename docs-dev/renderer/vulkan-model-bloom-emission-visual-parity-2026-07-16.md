# Vulkan Model Bloom-Emission Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: opaque MD2 skin emission is now covered by native Vulkan bloom extraction and a validation-backed paired visual gate. Shell, alpha/depth-hack, and remaining material-family emission are still open.

## Native implementation

OpenGL writes an opaque model skin's paired glowmap RGB to its bloom MRT. The native Vulkan renderer now retains that contract without adding an MRT to the normal scene pass. `vk_entity.c` creates bloom-only opaque pipelines for dynamic, GPU MD2, GPU inline-BSP, and GPU MD5 vertex layouts when the swapchain's bloom-extract render pass is available. During a bloom-active frame, `VK_Entity_RecordBloomEmission` reuses submitted entity batches, model descriptors, immutable GPU geometry, and the normal depth buffer.

The replay excludes alpha/additive, weapon/depth-hack, flare/query, outline, and item-colourize stages. It selects only opaque batches carrying a paired glowmap. The `VK_ENTITY_BLOOM_EXTRACT` fragment variant writes premultiplied skin glow RGB for models and `base.rgb * glow_alpha` for lightmapped inline BSP receivers. Global and height-fog transmission are applied to the emission, depth writes remain disabled, and the existing native bloom prefilter receives it as a separate authored-emission input.

This keeps the normal renderer's single colour target when `vk_bloom 0`. The GPU model paths remain device-local: the extra work is one selective draw replay only while bloom is enabled, with no CPU skin expansion, geometry upload, or OpenGL renderer route.

## Isolated capture

The old model-glowmap scene reused the wall-glow companion fixture, so a bloom capture contained both wall and skin emitters. The model generator now writes its map with `parity/fr01_to_background`, which has no glow companion. The regular `fr01_model_glowmap.cfg` explicitly disables both bloom backends so its `FR-01-T11` direct-skin gate remains independent.

`assets/renderer_parity/fr01_model_bloom_emission.cfg` enables matched bloom controls and clamps both thresholds at their supported maximum. Scene threshold highlights are excluded; the remaining warm halo comes from the authored MD2 skin glowmap. It captures the fixed `[570, 470, 310, 110]` model crop.

The manifest enforces paired RGB error plus two masks: the bright glow skin and the low-red halo over the blue background. The latter detects the bloom result rather than only the already-lit model surface.

## Deterministic evidence

The validation-backed run at `.tmp/renderer-parity/fr01-model-bloom-emission-final/report.json` passed:

- 34,100 crop pixels;
- maximum RGB error `49 / 11 / 13`, mean absolute error `0.94170 / 0.31672 / 0.26598`, and 239 pixels (`0.70088%`) beyond RGB error 16;
- 4,427 OpenGL and 4,482 Vulkan bright-skin pixels, IoU `0.96407`;
- 1,442 OpenGL and 1,510 Vulkan halo pixels, IoU `0.95497`;
- no Vulkan validation or process errors.

The direct `FR-01-T11` capture was rerun after isolation with bloom disabled and retains its 1,864/1,940 high-emission-pixel, IoU-`0.92413` gate.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_bloom_source.py'
python -m unittest discover -s tools/renderer_parity -p 'test_model_bloom_emission_fixture.py'
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-model-bloom-emission-final --vulkan-validation --json-output .tmp/renderer-parity/fr01-model-bloom-emission-final/report.json
```

All capture commands use the repository's hidden native-surface/headless mode; no interactive client or input device is started.

## Remaining boundary

This closes opaque skin/model emission for existing glowmap families. The OpenGL shell route, alpha/depth-hack objects, other material emitters, the OpenGL mip-pyramid bloom hierarchy, HDR/tone mapping, and resolution scaling remain `FR-01-T12` work.
