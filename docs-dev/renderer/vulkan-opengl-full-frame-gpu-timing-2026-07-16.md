# Shared Vulkan/OpenGL Full-Frame GPU Timing

Date: 2026-07-16

Task ID: `FR-01-T15`

Status: implemented telemetry contract with validation-backed paired captures;
an accepted cross-renderer GPU budget remains pending Vulkan GPU parity.

## Outcome

The paired renderer collector now has one explicit full-frame GPU metric:
`gpu_frame_ms`. Vulkan emits it as an alias of its completed native
command-buffer timestamp span. OpenGL emits it from a pair of native
`GL_TIMESTAMP` query-counter values recorded from `R_BeginFrame` through the
final normal 2D flush in `R_EndFrame`, immediately before presentation.

The OpenGL frame pair uses its own four-slot ring. It is polled only after at
least two subsequent render frames and only when both query results report
available, so telemetry never blocks on a GPU result. Its existing elapsed
queries for world, lightmap, effects, transparent, and post-process phases
remain intact; the new pair does not nest `GL_TIME_ELAPSED` queries.

`gpu_ms` remains the legacy phase-sum diagnostic. It is not a budget metric:
it omits some ordinary OpenGL work. `gpu_frame_ms` is instead the only GPU
comparison field intended for a Vulkan/OpenGL threshold. Both backends emit
`gpu_frame_valid`, and the analyzer exposes mean/p95 values, Vulkan/OpenGL
ratios, and a `require_gpu_frame_valid` budget guard. A GPU budget must bind
that guard and only `gpu_frame_ms_mean` and/or `gpu_frame_ms_p95` ratios.

The hidden paired runner already suppresses screenshots, so Vulkan's
`gpu_frame_ms` does not include its optional capture-copy path in the budget
scenario. Neither timing includes compositor or presentation latency.

## Implementation

- `src/rend_gl/qgl.h` and `qgl.c` load `glQueryCounter` with
  `GL_ARB_timer_query` support.
- `src/rend_gl/profile.c` owns the non-blocking full-frame query ring and
  preserves the phase query ring.
- `src/rend_gl/main.c` brackets the complete normal GL render frame and writes
  `gpu_frame_ms` / `gpu_frame_valid` to `GL_STATS`.
- `src/rend_vk/vk_debug.c` writes the same two fields in `VK_STATS` while
  retaining its phase breakdown and native adaptive-resolution consumer.
- `tools/renderer_parity/analyze_renderer_perf.py` aggregates and can enforce
  the explicit full-frame metric.

## Verification and current evidence

Both renderer DLLs rebuild and stage successfully. The telemetry parser,
full-frame source contract, OpenGL/Vulkan source contracts, and complete
renderer-parity source suite pass (218 tests).

The hidden validation-enabled collector produced 120 samples per renderer
(100 after the standard warm-up trim) on Intel Iris Xe / i7-13700H / Windows
11 with Intel driver `31.0.101.5590`. Every retained sample reports
`gpu_frame_valid=1`. On the dense inline-BSP workload at native scale, Vulkan
remains CPU-submission efficient but is GPU-bound relative to OpenGL:

| Metric | Vulkan | OpenGL | Vulkan / OpenGL |
|---|---:|---:|---:|
| CPU mean | 0.60234 ms | 0.82167 ms | 0.733x |
| GPU frame mean | 1.42395 ms | 0.36194 ms | 3.934x |
| GPU frame p95 | 1.430 ms | 0.344 ms | 4.157x |

This is diagnostic evidence, not an accepted budget. It establishes that the
shared timing scope is working and identifies native Vulkan scene/presentation
work as an optimization target; it does not support a claim of GPU-performance
parity or superiority.

Use only the existing hidden, adapter-matched collector for comparable
follow-up measurements and require all frame samples:

```text
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install \
  --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg \
  --fixture assets/maps/worr_fr01_bmodel_instances.bsp \
  --scenario-id fr01-bmodel-instance-grid-telemetry \
  --run-root .tmp/renderer-parity/fr01-renderer-perf-full-frame-gpu \
  --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" \
  --driver "<display-driver>" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py \
  --vulkan .tmp/renderer-parity/fr01-renderer-perf-full-frame-gpu/vulkan.log \
  --opengl .tmp/renderer-parity/fr01-renderer-perf-full-frame-gpu/opengl.log \
  --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-full-frame-gpu/capture.json \
  --warmup 20 --min-samples 100
```

The runner uses `win_headless 1`, disables input, and creates no interactive
client window.
