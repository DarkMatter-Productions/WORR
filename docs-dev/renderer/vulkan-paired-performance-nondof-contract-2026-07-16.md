# Vulkan Paired Performance Non-DOF Contract

Date: 2026-07-16

Task ID: `FR-01-T15`

Status: implemented a deterministic non-DOF capture control, native
post-process draw telemetry, and a fresh matching-adapter budget pass for the
dense inline-BSP workload.

## Problem

The hidden screenshot runner has always launched ordinary parity scenes with
`r_dof=0`, but the paired telemetry runner omitted that latched renderer cvar.
The local distributable also executes its archived `basew/config.cfg` during a
filesystem restart. As a result, a command-line setting alone was not enough
to make a performance scenario self-describing.

That made the dense inline-BSP budget vulnerable to ambient renderer settings.
It also made a regression investigation misleading: the fixture disabled
OpenGL's `gl_bloom`, but did not disable native Vulkan's independent
`vk_bloom`; the default Vulkan path added bloom prefilter and two blur draws
to every frame.

## Contract

`run_renderer_perf_capture.py` now records and passes a common non-DOF,
no-bloom, no-colour-correction, no-CRT profile. Each existing non-DOF timed
configuration additionally begins with `set r_dof 0`, before it sets up its
map or RmlUi workload. The shared bmodel configuration also explicitly sets
both backend-specific bloom and colour-correction controls:

- `fr01_renderer_perf_bmodel.cfg`;
- `fr01_renderer_perf_bmodel_instances.cfg`; and
- `fr01_renderer_perf_rmlui.cfg`.

The first-line configuration rule and the paired backend controls are covered
by the capture-runner unit test. The scenario configuration tree and the shared
launch profile are both part of the capture manifest SHA-256, so any future
relaxation or change invalidates the provenance contract.

`VK_STATS` now counts every native fullscreen post-process submission in a
dedicated `postprocess` domain and reports the per-domain draw and upload
counters. The log analyzer aggregates those fields when present. This corrects
the former headline count, which omitted post-process draws entirely.

## Controlled audit

The temporary diagnostic run at
`.tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-debug-cvars` printed
`r_dof "0"` and `gl_bloom "0"` in both renderer logs immediately before the
map command. The new postprocess domain then exposed four Vulkan fullscreen
draws in the uncorrected profile: one final composite plus the Vulkan-only
bloom prefilter and two blur passes. This identified the missing `vk_bloom 0`
control rather than attributing the work to DOF.

The corrected clean capture, with the same Intel Iris Xe / i7-13700H / Windows
11 and Intel `31.0.101.5590` provenance used by the budget, is retained at
`.tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-matched-post-controls`.
It collected 120 valid GPU samples per backend; analysis trims 20 warm-up
samples:

| Metric | Vulkan | OpenGL | Vulkan / OpenGL |
|---|---:|---:|---:|
| CPU mean | 0.52845 ms | 0.80593 ms | 0.65570x |
| CPU p95 | 0.628 ms | 1.116 ms | 0.56272x |
| Total draws mean / p95 | 18 / 18 | 38 / 38 | — |
| Vulkan world / entity / post draws | 2 / 16 / 0 | — | — |
| Uploads mean / p95 | 4,800 / 4,800 bytes | 0 / 0 bytes | — |
| Vulkan GPU mean / p95 | 1.40689 / 1.400 ms | 0.01475 / 0.016 ms | not comparable |

The timestamp scopes still differ, so the GPU figures remain diagnostic only.
The existing 18-draw CPU budget now binds to the corrected configuration hash
and passes without relaxing any CPU, p95, draw, upload, or CPU-ratio limit.

The paired visual gate at
`.tmp/renderer-parity/fr01-bmodel-instances-matched-post-controls` also passes
under Vulkan validation: all 218,400 pixels in the authored comparison crop
are exact, and the inline-BSP mask has an IoU of `1.0` (179,742 pixels on both
backends). The performance control therefore removes unmatched work without
altering the approved visible result.

## Verification

```text
python -m unittest tools/renderer_parity/test_run_renderer_perf_capture.py \
  tools/renderer_parity/test_analyze_renderer_perf.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install \
  --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg \
  --fixture assets/maps/worr_fr01_bmodel_instances.bsp \
  --scenario-id fr01-bmodel-instance-grid-telemetry \
  --run-root .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-matched-post-controls \
  --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" \
  --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py \
  --vulkan .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-matched-post-controls/vulkan.log \
  --opengl .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-matched-post-controls/opengl.log \
  --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-bmodel-instances-matched-post-controls/capture.json \
  --warmup 20 --min-samples 100 \
  --budget assets/renderer_parity/fr01_renderer_perf_bmodel_instances_budget.json
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_bmodel_instances_manifest.json \
  --run-root .tmp/renderer-parity/fr01-bmodel-instances-matched-post-controls \
  --vulkan-validation
```

All renderer launches above use the hidden native surface (`win_headless 1`)
and disable input; no interactive client is launched.

## Remaining work

Retain the corrected visual workload and extend this provenance-bound approach
to representative maps and overlay-bearing scenes. Comparable cross-renderer
GPU timing and broader GPU-driven submission remain open under `FR-01-T15`.
