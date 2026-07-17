# Vulkan Adaptive Resolution GPU Timing

Tasks: `FR-01-T12`, `FR-01-T15`

## Outcome

Vulkan's adaptive `r_resolutionscale` controller now uses the completed native
GPU timestamp result for its moving draw-time estimate. It no longer adjusts
scene resolution from CPU command-recording/submission duration when Vulkan
timestamp queries are supported.

`vk_debug` already resolves each timestamp range only after the owning
frame-slot fence signals. It now gives the scaler the total command-buffer
milliseconds plus a monotonically increasing sample identifier. The controller
accepts each completed result once, so a two-frame bounded renderer cannot
accidentally count the same previous GPU frame multiple times while another
frame is still in flight. The sample identifier remains monotonic across a
swapchain rebuild. A controller reset records the current identifier and waits
for a later result, preventing an old output-size sample from immediately
driving a resized scene target.

At startup, a timestamp-capable device waits for its first completed GPU
sample instead of seeding the controller with CPU time. Devices without native
timestamp-query support retain the existing microsecond CPU submission
measurement as a functional fallback. Neither branch adds a queue wait or
device-idle operation: queries continue to resolve during the existing
per-slot fence wait.

## Scope

This makes Vulkan's adaptive quality decision measure the work it is meant to
control, including native scene and presentation work. It does not by itself
make the existing Vulkan and OpenGL telemetry totals suitable for a
cross-renderer GPU budget: the backends still bracket different command scopes.
That broader `FR-01-T15` timing-contract work remains open.

## Validation

- `ninja -C builddir-win worr_vulkan_x86_64.dll`
- `python tools/stage_install.py --build-dir builddir-win`
- `python -m unittest discover -s tools/renderer_parity -p 'test_*.py'`
- `python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_resolution_scale_adaptive_manifest.json --run-root .tmp/renderer-parity/resolution-scale-adaptive-gpu-timing --vulkan-validation`

The capture runner uses a hidden native surface with input disabled; it does
not launch an interactive client window.
