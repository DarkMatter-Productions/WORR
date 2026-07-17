# Vulkan inline-BSP fast-light early-depth specialization

Date: 2026-07-16

Task ID: `FR-01-T15`

## Outcome

The native Vulkan opaque inline-BSP fast-light fragment shader no longer
contains an alpha discard. Its submission gate already proves that the batch
is opaque: it rejects transparent textures, `SURF_ALPHATEST`, translucent
entity alpha, and all material flags outside the authored-lightmap static
receiver contract.

Removing the unreachable discard permits normal early depth testing on
overlapping opaque inline-BSP fragments. This is a native Vulkan shader
specialization; neither the Vulkan renderer nor the performance path invokes
OpenGL.

## Safety boundary

The general Vulkan entity fragment shader keeps its alpha and alpha-test
discard behavior. The change applies only under
`VK_ENTITY_GPU_BMODEL_FAST_LIT`, which is selected after
`VK_Entity_GetBspFaceTexture` reports an opaque image and
`VK_Entity_AddGpuBspModel` rejects alpha materials.

The source regression test verifies both sides of that contract: the fast
block has no `discard;`, while the native submission code continues to
classify texture transparency and `SURF_ALPHATEST`.

## Visual validation

The validation-enabled, hidden 960x720
`fr01_bmodel_instances_lightmapped_manifest.json` matrix passed all three
scenes from the refreshed staging root:

| Scene | Result |
| --- | --- |
| Default optimized path | 218,400 crop pixels, zero RGB error, 179,742-pixel material-mask IoU 1.0 |
| Forced inline-BSP fallback | 218,400 crop pixels, zero RGB error, 179,742-pixel material-mask IoU 1.0 |
| Forced static-world fallback | 218,400 crop pixels, zero RGB error, 179,742-pixel material-mask IoU 1.0 |

Vulkan validation reports no errors.

## Paired timing

The baseline and two reruns use the same fixture, configuration, hidden
runtime profile, adapter, driver, 120 samples per backend, and 20-sample
warm-up trim. The pre-change baseline already had one active static-world
fast draw and 16 active inline-BSP fast draws per frame, so the comparison
isolates the fragment discard removal.

| Vulkan p50 metric | Baseline | Run 1 | Run 2 | Improvement range |
| --- | ---: | ---: | ---: | ---: |
| Opaque entity GPU | 0.350 ms | 0.241 ms | 0.241 ms | 31.1% |
| Opaque world GPU | 0.396 ms | 0.397 ms | 0.394 ms | unchanged |
| Scene GPU | 0.749 ms | 0.644 ms | 0.638 ms | 14.0–14.8% |
| Completed GPU frame | 0.778 ms | 0.675 ms | 0.668 ms | 13.3–14.1% |

Both optimized runs retain 18 Vulkan draws, 4,800 uploaded bytes, one
static-world fast-light draw, and 16 inline-BSP fast-light draws per sample.
The inline-BSP entity p95 is 0.243 ms in the second run, compared with the
baseline 0.352 ms.

Completed-frame p95 remains noisy in these runs due to non-entity timing
outliers, so this change claims the stable localized entity/scene median gain,
not a general Vulkan GPU-superiority budget. The updated representative
renderer-wide budget remains open under `FR-01-T15`.

The subsequent fog-free fast-light receiver companion retains the same opaque
submission boundary, adds per-frame global/height-fog fallback, and records
a separate entity no-fog counter. Its generated fogged 37-instance matrix and
controlled timing are documented in
`docs-dev/renderer/vulkan-bmodel-fast-lit-no-fog-2026-07-16.md`.

## Reproduction

```powershell
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_bmodel_instances_lightmapped_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-fast-lit-early-depth-visual --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_perf_bmodel_lm.cfg --fixture assets/maps/worr_fr01_bmodel_instances_lightmapped.bsp --scenario-id fr01-bmodel-fast-lit-early-depth --run-root .tmp/renderer-parity/fr01-bmodel-fast-lit-early-depth --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-bmodel-fast-lit-early-depth/vulkan.log --opengl .tmp/renderer-parity/fr01-bmodel-fast-lit-early-depth/opengl.log --capture-manifest .tmp/renderer-parity/fr01-bmodel-fast-lit-early-depth/capture.json --warmup 20 --min-samples 100 --json-output .tmp/renderer-parity/fr01-bmodel-fast-lit-early-depth/analysis.json
```
