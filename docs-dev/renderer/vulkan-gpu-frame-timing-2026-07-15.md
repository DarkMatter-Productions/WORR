# Native Vulkan GPU Frame Timing

Date: 2026-07-15

Task ID: `FR-01-T15`

Status: partial implementation. Native GPU command-buffer timing is now
available for supported Vulkan devices, including upload, shadow, scene, and
composition phases. Two bounded frame contexts, a provenance-bound dense
inline-BSP CPU budget, and native post-process draw telemetry are implemented;
representative-map GPU budgets and broader submission modernization remain
open.

## Outcome

`vk_debug.c` now creates a native `VK_QUERY_TYPE_TIMESTAMP` pool with six
queries per bounded frame context when the graphics queue reports timestamp support.
Each recorded command buffer resets its own query range, writes a timestamp at
`TOP_OF_PIPE`, and writes `BOTTOM_OF_PIPE` checkpoints after the frame-local
entity upload/copy barrier, after shadow recording, after the initial scene
work, after final composition, and after all rendering plus optional
screenshot-copy work.

The reported phase intervals are deliberately coarse and command-order based:

- `upload` spans frame start through native frame-local entity transfer work.
- `shadow` spans native shadow work only.
- `scene` spans the first scene pass and any native liquid/refraction work.
  When no final post-process pass is active, that first pass also
  includes the direct UI path.
- `post` spans depth-aware DOF blur/composition when active, final
  colour/warp/bloom/CRT work, overlay UI, and no-world entity-preview
  composition. It excludes an optional screenshot copy.

The full `gpu_ms` covers the complete command buffer, including that optional
screenshot copy. The phase values therefore target recurring rendering work
rather than pretending that a capture readback is a presentation cost.

After the existing submission fence signals, Vulkan resolves the pair with a
64-bit query result and converts ticks through the physical device's
`timestampPeriod`. The result is the elapsed GPU time, in milliseconds, for
the command buffer submitted in the preceding completed frame. It deliberately
does not include presentation/compositor latency and it does not add a new CPU
wait: result resolution occurs only after the renderer's already-required
fence wait.

Unsupported devices or query-pool creation failures remain usable. They report
GPU timing as unavailable and set a named missing-capability bit rather than
failing renderer initialization.

## Diagnostics and resource lifetime

The renderer-stat panel displays `GPU frame ms` and `GPU phases ms` when
valid. `vk_stats` adds `gpu_ms`, `gpu_frame_ms`, `gpu_upload_ms`, `gpu_shadow_ms`,
`gpu_scene_ms`, `gpu_post_ms`, `gpu_valid`, and the `gpu_timing` capability
field. Its draw counters now include a dedicated `postprocess` domain, so
fullscreen bloom, DOF, final composition, and CRT submissions cannot be
silently omitted from a performance capture. Query pools are swapchain-scoped
with the command buffers, are destroyed before the swapchain is released, and
use only native Vulkan commands. No OpenGL renderer function, timer, or
fallback path participates.

Vulkan CPU submission timing now uses microsecond-resolution QPC on Windows
(and monotonic microseconds on other platforms) before conversion to fractional
milliseconds. The former integer `Sys_Milliseconds()` measurement rounded
ordinary submit work to zero and could not be fairly aggregated beside
OpenGL's microsecond profiler. The change records time only; it does not add a
fence, queue wait, or other synchronization.

The paired log analyzer aggregates each reported GPU phase to a
warmup-trimmed mean and p95. It additionally reports the `gpu_post_ms` ratio
when both backends provide that common field. These phase values and legacy
`gpu_ms` remain diagnostic. The shared full-frame `gpu_frame_ms` contract is
documented in `vulkan-opengl-full-frame-gpu-timing-2026-07-16.md`.

The renderer has two bounded native frame contexts (capped by swapchain image
count), each with its own command buffer, fence, depth attachment/framebuffer,
and transient UI/entity/world/shadow/debug storage. Completed timestamp ranges
are still resolved only after their owning frame fence signals; telemetry adds
no ordinary-frame CPU wait. Frame-local post-process targets avoid a global
drain except during rare resource replacement.

## Headless validation

`tools/renderer_parity/test_vulkan_gpu_timing_source.py` verifies timestamp
capability gating, six-query per-image allocation, command-buffer and phase
bracketing, fence-after result resolution, stats exposure, and the absence of
an OpenGL route. The build and source test are non-interactive:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_gpu_timing_source.py
```

Runtime performance claims still require the task's future no-window capture
sequence to retain matching scene inputs, GPU timing, CPU timing, draw/batch
counts, upload bytes, and explicit GL/Vulkan thresholds. The collection gate
now verifies a paired capture manifest and exact telemetry hashes before it
accepts a budget; see
`docs-dev/renderer/vulkan-paired-performance-capture-contract-2026-07-15.md`.
The first adapter-matched fixed-view collection is recorded in
`vulkan-paired-fixed-view-telemetry-2026-07-15.md`; it establishes no budget.
