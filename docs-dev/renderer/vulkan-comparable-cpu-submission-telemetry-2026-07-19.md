# Comparable Renderer CPU Submission Telemetry

Date: 2026-07-19

Task ID: `FR-01-T15`

## Outcome

Vulkan and OpenGL now publish a like-for-like `cpu_render_ms` metric in their
machine-readable renderer statistics. OpenGL aliases its existing scoped render
profile. Vulkan retains its prior `cpu_ms` as end-to-end renderer-frame latency
and additionally reports `cpu_sync_wait_ms` for acquire, image-ownership,
resource-rebuild, present, and screenshot synchronization that is outside
command recording/submission work.

This closes a measurement defect rather than hiding latency: a compositor or
swapchain pacing stall remains visible in Vulkan `cpu_ms` and
`cpu_sync_wait_ms`, but no longer makes the OpenGL/Vulkan CPU-render ratio
compare unlike scopes. The analyzer aggregates the new fields and produces
`cpu_render_ms_mean` and `cpu_render_ms_p95` ratios while retaining legacy
`cpu_ms` results for historical captures.

## Native implementation

`R_BeginFrame` starts a high-resolution frame timer after the ordinary
per-frame fence wait. `VK_DrawFrame` measures and accumulates only the
additional synchronization sections that can block for presentation ownership:

- safe all-frame resource rebuild waits;
- swapchain image acquisition and prior-image ownership waits;
- queue presentation; and
- screenshot completion.

At end frame, Vulkan publishes total CPU time, the bounded subtraction of
those synchronization waits, and the synchronization total. The rendering
code, command buffers, synchronization, and present behavior are unchanged.
There is no OpenGL fallback or Vulkan rendering-path redirect.

## Verification and budget

Both headless, input-disabled, audio-disabled paired captures use the dense
inline-BSP fixture, 20 warm-up samples, then 100 samples per renderer with
Vulkan validation enabled:

```text
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install \
  --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg \
  --scenario-id fr01-bmodel-instance-grid-current \
  --run-root .tmp/renderer-perf/comparable-cpu-telemetry-paired \
  --min-samples 100 --hardware-id "Intel(R) Iris(R) Xe Graphics" \
  --driver local-headless --vulkan-validation
```

The paired run records Vulkan `cpu_render_ms` at `1.384` ms mean / `2.647` ms
p95 versus OpenGL `2.990` / `6.779` ms: ratios `0.463x` and `0.390x`. Its
independent revalidation records `1.209` / `2.709` ms versus `2.839` / `6.266`
ms: `0.426x` and `0.432x`. Both keep Vulkan at 18 draws and 4,800 upload bytes
per sample, compared with OpenGL's 38 draws. All Vulkan samples have valid GPU
timestamps and the logs have no VUID or renderer failure.

`assets/renderer_parity/fr01_renderer_perf_bmodel_instances_cpu_submission_budget.json`
binds the exact fixture, config, adapter, and driver. It requires 100 samples,
caps Vulkan CPU render work at 1.75 ms mean / 3.25 ms p95, preserves the
18-draw/4,800-byte contract, and requires Vulkan to remain at most `0.6x` of
OpenGL CPU render mean and p95. It deliberately contains no GPU requirement:
the same runs show Vulkan full-frame GPU time at approximately `1.58x` the
OpenGL result, which is a real open bottleneck rather than a claim of superior
GPU performance.

## Remaining FR-01-T15 work

Use the now-comparable telemetry to optimize the native GPU scene path before
adding a cross-renderer GPU budget. The full-frame GPU regression, broader
representative-map coverage, and product-level budgets remain open.
