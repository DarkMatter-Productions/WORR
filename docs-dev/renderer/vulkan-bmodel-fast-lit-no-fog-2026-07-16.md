# Vulkan inline-BSP fast-light no-fog specialization

Date: 2026-07-16

Task ID: `FR-01-T15`

## Outcome

The native Vulkan opaque inline-BSP fast-light receiver now has a no-fog
fragment companion. It retains the existing proven opaque/lightmapped
submission contract and lightmap/intensity equation, but compiles out
`apply_fog` when the current refdef has neither global nor height fog.

The new `VK_ENTITY_GPU_BMODEL_FAST_LIT_NO_FOG` module is 4,264 bytes,
compared with 8,184 bytes for the fog-aware fast-light module. It is selected
only after the regular inline-BSP fast gate has accepted the opaque material:
transparent textures, alpha-tested faces, translucency, glow maps,
fullbright/entity material flags, sun receivers, and dynamic lights remain
on their existing native paths. The general entity fragment shader and its
alpha discard behavior are unchanged.

This is entirely native Vulkan; it does not route rendering through OpenGL.

## Runtime selection and safety

`VK_Shadow_HasActiveSurfaceFog` reads the current frame's fog UBO after
`VK_Shadow_UpdateDlights`. The entity recorder binds the no-fog pipeline
only if:

1. the batch already has `VK_ENTITY_VERTEX_GPU_BMODEL_FAST_LIT`;
2. `vk_bmodel_fast_lit_no_fog` is at its archived default `1`;
3. neither `VK_FOG_GLOBAL` nor `VK_FOG_HEIGHT` is active; and
4. the optional no-fog pipeline was created successfully.

Otherwise it selects the prior fog-aware inline-BSP fast pipeline. This
decision is made each frame, so a fog transition takes effect immediately
without mesh, instance, descriptor, or pipeline recreation. The default-on
cvar is also a native driver-workaround/regression control; `0` does not
invoke OpenGL.

`VK_STATS entity_fast_lit_no_fog_draws` records the actual selected subset
of `entity_fast_lit_draws`. The paired analyzer reports mean, p50, and p95
for that field.

## Visual validation

All runs use a hidden 960x720 native surface, `win_headless 1`, isolated
runtime directories, input/audio disabled, and Vulkan validation.

| Matrix | Result |
| --- | --- |
| `fr01_bmodel_instances_lightmapped_manifest.json` | Three optimized/fallback scenes pass with zero RGB error over 218,400 crop pixels each and a 179,742-pixel inline-BSP mask at IoU 1.0. |
| `fr01_bmodel_instances_lightmapped_fog_manifest.json` | New generated 37-instance global-fog scene passes over 218,400 pixels: maximum RGB error 1/1/1, mean absolute RGB error at most 0.029314, and zero pixels over the threshold. |

The fog fixture combines the existing authored fog worldspawn properties with
the dense lightmapped inline-BSP grid. It proves the actual entity receiver
retains the fog-aware fast pipeline under global fog, rather than only proving
the shared fog query in a world-only scene. Both matrices are
validation-clean.

## Controlled paired timing

Four current-binary captures use the same lightmapped 37-instance fixture,
configuration, adapter, driver, validation setting, 120 samples per backend,
and a 20-sample warmup trim. The only intended change is
`vk_bmodel_fast_lit_no_fog=0/1`; static-world no-fog remains enabled in
every run.

Both fog-aware controls record 16 `entity_fast_lit_draws` and zero
`entity_fast_lit_no_fog_draws` per post-warmup sample. Both enabled runs
record 16 of each, proving that every eligible inline-BSP draw selected the
new fragment module. The static-world no-fog counter remains one in all four
runs, so it does not confound the entity A/B. Draws remain 18 and uploads
remain 4,800 bytes.

| Vulkan opaque-entity GPU | No-fog disabled | No-fog enabled | Reduction |
| --- | ---: | ---: | ---: |
| p50 (control 1 vs enabled 1) | 0.241 ms | 0.0995 ms | 58.7% |
| p50 (control 2 vs enabled 2) | 0.240 ms | 0.177 ms | 26.3% |
| mean (stable control 2 vs enabled runs) | 0.24057 ms | 0.10847-0.17758 ms | 26.2-54.9% |
| p95 (stable control 2 vs enabled runs) | 0.241 ms | 0.178-0.179 ms | 25.7-26.1% |

One initial disabled tail sample is noisy (0.875 ms p95), so tail claims use
the repeated stable control. CPU timing remains approximately 1.04-1.08 ms
across the pair and is not used for a claim. The capture's adjacent world,
scene, and completed-frame phase times vary between runs, so this result is a
localized entity-phase optimization, not a renderer-wide GPU budget. The
OpenGL completed-frame p50 remains approximately 0.402-0.407 ms and
`FR-01-T15` remains open.

## Fixture and reproduction

```powershell
python tools/renderer_parity/generate_bmodel_lightmapped_instance_fog_fixture.py --asset-root assets
python tools/renderer_parity/generate_bmodel_lightmapped_instance_fog_fixture.py --asset-root assets --validate
python tools/renderer_parity/test_generate_bmodel_lightmapped_instance_fog_fixture.py
python tools/test_package_assets.py
python tools/gen_vk_world_spv.py --validate
python -m unittest tools.renderer_parity.test_vulkan_gpu_bmodel_submission_source tools.renderer_parity.test_analyze_renderer_perf
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install

python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_bmodel_instances_lightmapped_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-fast-lit-no-fog-visual --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_bmodel_instances_lightmapped_fog_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-fast-lit-no-fog-global-fog --vulkan-validation

python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_perf_bmodel_lm.cfg --fixture assets/maps/worr_fr01_bmodel_instances_lightmapped.bsp --scenario-id fr01-bmodel-fast-lit-no-fog-control-off --run-root .tmp/renderer-parity/fr01-bmodel-fast-lit-no-fog-control-off --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100 --launch-cvar vk_bmodel_fast_lit_no_fog=0
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_perf_bmodel_lm.cfg --fixture assets/maps/worr_fr01_bmodel_instances_lightmapped.bsp --scenario-id fr01-bmodel-fast-lit-no-fog-control-on --run-root .tmp/renderer-parity/fr01-bmodel-fast-lit-no-fog-control-on --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100 --launch-cvar vk_bmodel_fast_lit_no_fog=1
```

Analyze each capture with `tools/renderer_parity/analyze_renderer_perf.py`,
its paired `capture.json`, `--warmup 20`, and `--min-samples 100`.
