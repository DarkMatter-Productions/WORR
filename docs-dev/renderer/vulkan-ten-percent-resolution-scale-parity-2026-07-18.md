# Vulkan Ten-Percent Resolution-Scale Parity

Date: 2026-07-18

Task ID: `FR-01-T12`

## Result

Vulkan now honors the same fixed and adaptive lower bound as OpenGL:
`r_resolutionscale_fixedscale_w` and `_h` can both reach `0.1`.

Before this change, OpenGL rendered a 960 by 720 view at 96 by 72 when the
shared controls requested 10%, while Vulkan silently clamped it to 240 by 180
(25%). That was both a functional cvar mismatch and unnecessary 3D shading
work on hardware where the user explicitly selected the lowest quality tier.

The Vulkan offscreen target remains native and frame-slot owned. The final
composite and UI stay at output resolution, so visual controls and text remain
sharp while the 3D scene at 10% covers only one percent of the native scene
pixel area.

## Validation

`fr01_resolution_scale_crt_tenth_manifest.json` runs the shared CRT scene at
fixed `0.1` scale. It exact-compares a 50,000-pixel crop and locks the 25,000
dark-pixel scanline blocks at IoU `1.0`; that distinguishes a real 96 by 72
scene from the old 240 by 180 Vulkan clamp.

`fr01_resolution_scale_adaptive_crt_tenth_manifest.json` uses the shared
zero-millisecond target, one-frame lowering cadence, and `0.5` step to force
the adaptive controller to its `0.1` floor before capture. Its same exact
pixel and scanline-block contract proves that the adaptive Vulkan path reaches
the shared lower tier rather than only accepting it for fixed scale.

The retained fixed and adaptive runs both recorded zero RGB error across all
50,000 compared pixels and a 25,000-pixel scanline-block intersection at IoU
`1.0` with Vulkan validation enabled.

## Measured native Vulkan benefit

A separate hidden, same-machine timing pair used the stock first-frame BSP
receiver at 960 by 720 with 120 collected frames per backend and 20 discarded
warmup frames. It compares Vulkan at native scene resolution to Vulkan at the
fixed 10% floor; the UI/composite stays at output resolution in both cases.
On the recorded Intel Iris Xe / Intel `31.0.101.5590` environment:

| Vulkan metric | Native scene | 10% scene | Change |
|---|---:|---:|---:|
| Completed GPU-frame mean | 0.43457 ms | 0.22956 ms | -47.2% |
| Completed GPU-frame p50 | 0.432 ms | 0.272 ms | -37.0% |
| GPU scene mean | 0.39541 ms | 0.06906 ms | -82.5% |
| GPU scene p50 | 0.4055 ms | 0.075 ms | -81.5% |

The scaled path adds a native upsample/composite phase (`gpu_post_ms` mean
`0.13189 ms` versus `0.00157 ms` at native resolution), but the saved
scene work still reduces total GPU-frame time. Both samples retain three
draws and 192 uploaded bytes, so this is scene-resolution work avoided rather
than a changed workload. These are same-device observational results, not a
cross-driver budget or a claim of general Vulkan-versus-OpenGL superiority.
The validation-enabled pixel gates above remain the correctness evidence.

The provenance-bound reports are retained under
`.tmp/renderer-perf/fr01-resolution-scale-native/analysis.json` and
`.tmp/renderer-perf/fr01-resolution-scale-tenth/analysis.json`.

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_resolution_scale_crt_tenth_manifest.json --run-root .tmp/renderer-parity/fr01-crt-tenth-final --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_resolution_scale_adaptive_crt_tenth_manifest.json --run-root .tmp/renderer-parity/fr01-crt-adaptive-tenth-final --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bmodel.cfg --fixture assets/maps/worr_fr01_bmodel_first_frame.bsp --scenario-id fr01-bmodel-tenth-resolution --run-root .tmp/renderer-perf/fr01-resolution-scale-tenth --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<version>" --driver "<driver>" --min-samples 100 --launch-cvar r_resolutionscale=1 --launch-cvar r_resolutionscale_fixedscale_w=0.1 --launch-cvar r_resolutionscale_fixedscale_h=0.1
```

The runner uses isolated homes, disabled input and sound, and a hidden native
surface.
