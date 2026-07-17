# Vulkan static-world no-fog fast-light specialization

Date: 2026-07-16

Task ID: `FR-01-T15`

## Outcome

Native Vulkan now has no-fog companions for both opaque static-world
fast-light receiver classes:

- `VK_WORLD_STATIC_FAST_LIT_NO_FOG` for plain authored lightmaps; and
- `VK_WORLD_STATIC_FAST_LIT_GLOWMAP_NO_FOG` for authored lightmap/glow-map
  receivers.

They retain the existing base texture, lightmap, glow-alpha lift,
brightness/modulate, and vertex-intensity equations, but compile out
`apply_fog(..., false)`. This removes the otherwise unnecessary fragment
depth division, fog-flag read, and global/height-fog branches from the common
no-fog static-world case. The embedded SPIR-V reflects the reduction: the
plain receiver is 4,488 bytes rather than 8,660, and the glow-map receiver is
5,208 bytes rather than 9,380.

This is native Vulkan specialization only. No Vulkan path invokes OpenGL.

## Runtime safety boundary

`VK_Shadow_UpdateDlights` rebuilds the fog UBO for every refdef before world
recording. `VK_Shadow_HasActiveSurfaceFog` reads those current flags and
permits the no-fog pipeline only if neither `VK_FOG_GLOBAL` nor
`VK_FOG_HEIGHT` is active. A sky-only flag is intentionally allowed because
these opaque receivers call `apply_fog(..., false)`, whose sky branch cannot
affect them.

The selection happens per opaque batch per frame:

1. Normal static-light eligibility still rejects fullbright, active
   sun/dynamic receiver lighting, unavailable pipelines, and unsupported
   material flags.
2. Active global or height fog retains the pre-existing fog-aware fast
   pipeline.
3. An unavailable no-fog pipeline also falls back to the fog-aware fast
   pipeline.
4. `vk_world_fast_lit_no_fog 0` is an archived native Vulkan
   diagnostic/driver-workaround control that makes the same fallback
   explicit. Its default is `1`.

The complete general opaque pipeline remains the fallback for all existing
fast-light eligibility failures. Thus a runtime fog transition does not
require a world rebuild and cannot render an unfogged opaque receiver.

`VK_STATS world_fast_lit_no_fog_draws` records the selected subset of
`world_fast_lit_draws`. The paired analyzer reports its mean, p50, and p95,
making the actual shader choice auditable in timing evidence.

## Visual validation

All validation used a hidden 960x720 native surface, `win_headless 1`,
isolated runtime directories, disabled input/audio, and Vulkan validation.

| Matrix | Result |
| --- | --- |
| `fr01_bmodel_instances_lightmapped_manifest.json` | All three optimized/fallback scenes passed: 218,400 crop pixels each, zero RGB error, and 179,742-pixel material mask IoU 1.0. |
| `fr01_global_fog_manifest.json` | 235,200 pixels, zero RGB error; the 187,900-pixel authored global-fog probe retains IoU 1.0. |
| `fr01_height_fog_manifest.json` | 235,200 pixels, zero RGB error; the 106,004-pixel authored height-fog gradient probe retains IoU 1.0. |

The no-fog matrix proves the specialized path keeps ordinary lightmapped
output exact; the global/height matrices prove the runtime gate retains the
fog-aware receiver result. All three runs report no Vulkan validation errors.

## Controlled paired timing

The direct A/B uses the current staged binary, identical fixture/configuration,
adapter, driver, validation setting, 120 samples per backend, and a
20-sample warmup trim. The only intended difference is
`vk_world_fast_lit_no_fog=0/1`. Both Vulkan runs have 100 valid completed GPU
samples, one authored static-world fast-light candidate/draw, 16 inline-BSP
fast-light draws, 18 total draws, and 4,800 upload bytes per post-warmup
sample. The enabled run records one no-fog static-world draw at mean/p50/p95
`1 / 1 / 1`; the disabled control records `0 / 0 / 0`.

| Vulkan metric | No-fog disabled | No-fog enabled | Change |
| --- | ---: | ---: | ---: |
| Opaque world GPU p50 | 0.399 ms | 0.393 ms | -1.5% |
| Opaque world GPU mean | 0.39692 ms | 0.38793 ms | -2.3% |
| Opaque world GPU p95 | 0.414 ms | 0.409 ms | -1.2% |
| Scene GPU p50 | 0.644 ms | 0.638 ms | -0.9% |
| Completed GPU frame p50 | 0.6745 ms | 0.667 ms | -1.1% |

CPU frame time varied between the control captures, so it is not used for a
claim. The gain is intentionally scoped to the stable opaque-world GPU phase;
it does not establish a renderer-wide GPU superiority budget. The paired
OpenGL frame p50 remains approximately 0.403-0.407 ms in these captures, so
`FR-01-T15` remains open.

## Reproduction

```powershell
python tools/gen_vk_world_spv.py --validate
python -m unittest tools.renderer_parity.test_vulkan_world_fast_lit_source tools.renderer_parity.test_analyze_renderer_perf
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install

python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_bmodel_instances_lightmapped_manifest.json --run-root .tmp/renderer-parity/fr01-world-fast-lit-no-fog-visual-rerun --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_global_fog_manifest.json --run-root .tmp/renderer-parity/fr01-world-fast-lit-no-fog-global-fog --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --executable .install/worr_x86_64.exe --manifest assets/renderer_parity/fr01_height_fog_manifest.json --run-root .tmp/renderer-parity/fr01-world-fast-lit-no-fog-height-fog --vulkan-validation

python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_perf_bmodel_lm.cfg --fixture assets/maps/worr_fr01_bmodel_instances_lightmapped.bsp --scenario-id fr01-world-fast-lit-no-fog-control-off --run-root .tmp/renderer-parity/fr01-world-fast-lit-no-fog-control-off --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100 --launch-cvar vk_world_fast_lit_no_fog=0
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_perf_bmodel_lm.cfg --fixture assets/maps/worr_fr01_bmodel_instances_lightmapped.bsp --scenario-id fr01-world-fast-lit-no-fog-control-on --run-root .tmp/renderer-parity/fr01-world-fast-lit-no-fog-control-on --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100 --launch-cvar vk_world_fast_lit_no_fog=1
```

Run `tools/renderer_parity/analyze_renderer_perf.py` against each capture's
Vulkan/OpenGL logs and `capture.json`, with `--warmup 20 --min-samples 100`,
before comparing the Vulkan phase summaries.
