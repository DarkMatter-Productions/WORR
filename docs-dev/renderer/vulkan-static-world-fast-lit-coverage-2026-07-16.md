# Vulkan static-world fast-light coverage telemetry

Date: 2026-07-16

Task ID: `FR-01-T15`

## Outcome

Native Vulkan `vk_stats` and the paired performance analyzer now expose why
each opaque authored-lightmap world batch does or does not select the
static-light receiver specialization. This makes the optimized path auditable
in the same validation-enabled headless capture that supplies GPU timing; it
does not redirect any Vulkan rendering through OpenGL.

The fresh dense inline-BSP capture confirms the world specialization is live:
one eligible static-world batch selected the native fast shader on every one
of the 100 post-warm-up samples. None were blocked by the cvar, global
fullbright, sun/dynamic receiver lighting, an unavailable pipeline, or an
unsupported material flag.

## Telemetry contract

The new `VK_STATS` fields are per-frame opaque world-batch counts:

| Field | Meaning |
| --- | --- |
| `world_fast_lit_candidates` | Authored-lightmapped opaque world batches considered for a static-light specialization. |
| `world_fast_lit_no_fog_draws` | Selected fast-light draws using the no-global/height-fog shader; a subset of `world_fast_lit_draws`. |
| `world_fast_lit_disabled` | Candidates held on the complete native path by `vk_world_fast_lit 0`. |
| `world_fast_lit_fullbright` | Candidates held on the complete path by global `r_fullbright`. |
| `world_fast_lit_receiver_lighting` | Candidates requiring the complete receiver because a sun page or dynamic light is active. |
| `world_fast_lit_pipeline_unavailable` | Candidates whose appropriate optional Vulkan specialization was unavailable. |
| `world_fast_lit_material_ineligible` | Candidates with flags outside the plain or glow-map static-light contracts. |

`world_fast_lit_draws` now counts only plain-lightmap and glow-map
static-light draws. It no longer labels the separate unlightmapped
texture-replace specialization as a fast-light draw.

`tools/renderer_parity/analyze_renderer_perf.py` summarizes each field as
mean, p50, and p95 so capture analysis can prove path coverage rather than
assuming it from shader source or draw counts.

The no-fog follow-up records this additional subset so the performance capture
proves the specialized fragment module was selected, not merely compiled. Its
runtime fog gate, exact global/height-fog validation, and controlled A/B
timing are in
`docs-dev/renderer/vulkan-static-world-fast-lit-no-fog-2026-07-16.md`.

## Fresh paired capture

The capture used `renderer_parity/fr01_perf_bmodel_lm.cfg`,
`worr_fr01_bmodel_instances_lightmapped.bsp`, a hidden 960x720 surface,
`win_headless 1`, an isolated home directory, disabled input/audio, and
Vulkan validation. It collected 120 samples per renderer and trimmed the
first 20.

| Metric | Vulkan | OpenGL | Vulkan/OpenGL |
| --- | ---: | ---: | ---: |
| CPU mean | 0.984 ms | 1.874 ms | 0.525x |
| CPU p95 | 1.229 ms | 2.922 ms | 0.421x |
| GPU frame p50 | 0.778 ms | 0.401 ms | 1.94x |
| GPU frame mean | 0.767 ms | 0.329 ms | 2.33x |
| Vulkan opaque-world GPU p50 | 0.396 ms | — | — |
| Vulkan opaque-entity GPU p50 | 0.350 ms | — | — |
| Draws | 18 | 38 | 0.47x |

The Vulkan coverage means/p50/p95 are all `1` for
`world_fast_lit_candidates` and `world_fast_lit_draws`, and all `0` for
the five blocker counters. Inline-BSP specialization remains active for 16
entity draws per frame. Vulkan validation reported no errors.

This replaces the misleading interpretation of a prior stale,
non-specialized local capture. It is not a representative renderer-wide GPU
budget: Vulkan remains slower than OpenGL on completed GPU frame time for
this particular scene, so `FR-01-T15` remains open.

## Visual regression validation

The fresh validation-enabled
`fr01_bmodel_instances_lightmapped_manifest.json` matrix also passed all
three scenes: default optimized, forced inline-BSP fallback, and forced
static-world fallback. Each compared 218,400 crop pixels with zero RGB error
and retained the 179,742-pixel material mask at IoU 1.0. This confirms the
coverage bookkeeping and corrected counter do not alter either renderer's
visual result.

## Reproduction

```powershell
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_perf_bmodel_lm.cfg --fixture assets/maps/worr_fr01_bmodel_instances_lightmapped.bsp --scenario-id fr01-bmodel-lightmapped-fast-lit-coverage --run-root .tmp/renderer-parity/fr01-bmodel-lightmapped-fast-lit-coverage --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-bmodel-lightmapped-fast-lit-coverage/vulkan.log --opengl .tmp/renderer-parity/fr01-bmodel-lightmapped-fast-lit-coverage/opengl.log --capture-manifest .tmp/renderer-parity/fr01-bmodel-lightmapped-fast-lit-coverage/capture.json --warmup 20 --min-samples 100 --json-output .tmp/renderer-parity/fr01-bmodel-lightmapped-fast-lit-coverage/analysis.json
```
