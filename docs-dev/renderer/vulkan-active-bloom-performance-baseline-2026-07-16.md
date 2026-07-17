# Native Vulkan Active-Bloom Performance Baseline

Date: 2026-07-16

Task ID: `FR-01-T15`

Status: reproducible active-bloom baseline added. It is diagnostic evidence,
not a Vulkan performance budget: the current native shell-bloom workload is
not yet competitive with OpenGL on the reference adapter.

## Workload

`fr01_renderer_perf_bloom_shell.cfg` measures the existing validated
`worr_fr01_model_shell.bsp` receiver. It activates the matched one-iteration
bloom controls, threshold-isolates the translucent shell emission, disables
DOF/CRT/colour grading/LUTs, and captures 120 headless telemetry samples after
the fixed camera settles. The common runner profile still starts with both
backend bloom controls disabled, then this repository-owned config enables
both before map load.

The capture is bound to fixture SHA-256
`75c27ba5f45bc1e3e0b56fd55092477c08dd2c02bb15b173efba741d14d5a2f3` and
configuration/profile SHA-256
`f98517b41dc6769271ce67f087fd1055bf791654907a09faf1af8c6fde590ec7`.
The retained provenance root is
`.tmp/renderer-parity/fr01-renderer-perf-bloom-shell`.

## Result

The validation-enabled capture ran at 960x720 on Intel Iris Xe, 13th Gen Intel
Core i7-13700H, Windows 11 Home 10.0.26200, driver
`31.0.101.5590 (2024-06-10)`. It retains 100 samples after trimming 20.

| Metric | Vulkan | OpenGL | Vulkan / OpenGL |
| --- | ---: | ---: | ---: |
| CPU mean / p95 | 1.04998 / 1.358 ms | 0.33642 / 0.414 ms | 3.12x / 3.28x |
| Complete GPU mean / p95 | 2.18236 / 2.191 ms | 0.54073 / 0.573 ms | 4.04x / 3.82x |
| GPU post phase mean / p95 | 0.81801 / 0.823 ms | 0.52956 / 0.562 ms | 1.54x / 1.46x |
| Native draws | 9 total; 4 postprocess | — | — |
| Native uploads | 400 bytes | 0 bytes reported | — |

Vulkan's native post phase contains the bloom prefilter, two paired-tap
Gaussian blur passes, and final composition. The remaining large total gap is
in the Vulkan scene phase (1.32104 ms): this receiver exercises the selective,
native authored-emission replay needed to avoid a permanent bloom MRT. The
baseline therefore identifies scene-side emission submission and presentation
setup as the next optimization target; it does not justify weakening bloom
quality or accepting a cross-renderer budget.

`analyze_renderer_perf.py` now exposes `gpu_upload_ms`, `gpu_shadow_ms`,
`gpu_scene_ms`, and `gpu_post_ms` mean/p95 values when the backend reports
them. The shared `gpu_post_ms` name also produces a Vulkan/OpenGL diagnostic
ratio; timestamp scope differences still prohibit using this alone as a
renderer-wide performance claim.

The visual contract remains the existing strict shell-bloom manifest; the
performance config does not change the material or presentation controls used
by that visual gate.

## Active/disabled isolation

`fr01_renderer_perf_bloom_shell_disabled.cfg` retains the same fixed shell
receiver and disables bloom in both backends. Its validation-enabled Vulkan
capture (`85e7e70513e83ad6fb807ea95c069848dadb8b3a6db8b1ff56d8ba3cbf25795a`)
records `1.14888 ms` complete GPU, `1.09599 ms` scene, `0.00200 ms` post, and
four total / zero post-process draws. Relative to the active capture above,
native bloom adds five total draws (the authored-emission replay plus four
post-process draws), `0.81601 ms` post time, and `0.22505 ms` scene time.

This isolates most of the present active-bloom cost to native post processing
and confirms that the remaining scene-side target is the selective
authored-emission replay, not an always-on bloom MRT. A Gaussian-weight
recurrence candidate passed focused visual gates but raised this active lane's
post mean from `0.81801` to `0.87883 ms`; it was rejected in favour of the
retained paired bilinear-tap shader.

A later native uniform-coefficient pass preserves the paired bilinear taps but
precomputes their offsets and weights once per reusable frame slot. On the same
active shell lane it lowers Vulkan post mean to `0.76663 ms` and complete GPU
mean to `2.13732 ms`, with the same nine total / four post-process draws. Its
full design, visual evidence, and measurement provenance are in
`docs-dev/renderer/vulkan-blur-kernel-uniform-optimization-2026-07-16.md`.

## Verification

```text
python tools/package_assets.py
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_analyze_renderer_perf.py tools/renderer_parity/test_run_renderer_perf_capture.py
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bloom_shell.cfg --fixture assets/maps/worr_fr01_model_shell.bsp --scenario-id fr01-model-shell-active-bloom-telemetry --run-root .tmp/renderer-parity/fr01-renderer-perf-bloom-shell --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" --driver "<driver>" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-renderer-perf-bloom-shell/vulkan.log --opengl .tmp/renderer-parity/fr01-renderer-perf-bloom-shell/opengl.log --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-bloom-shell/capture.json --warmup 20 --min-samples 100
```

The collector is always hidden/headless and disables input.
