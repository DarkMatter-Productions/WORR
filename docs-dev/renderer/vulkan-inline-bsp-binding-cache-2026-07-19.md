# Vulkan Inline-BSP Binding Cache

Date: 2026-07-19

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: implemented and measured on the native dense inline-BSP lane. This is
a scoped CPU command-recording improvement, not a renderer-wide GPU budget.

## Problem

Ordinary inline BSP models already keep their face geometry in one immutable
device-local vertex buffer and place all current-frame transforms in one
frame-local instance buffer. After opaque batch coalescing, however, the
entity recorder still rebound that identical pair before every compatible
inline-BSP batch. The representative dense fixture submits sixteen opaque
native inline-BSP batches, so it recorded sixteen redundant
`vkCmdBindVertexBuffers` calls even though the vertex bindings had not changed.

The redundancy affected command recording and driver command processing; it
did not change geometry, descriptors, pipeline selection, draw count, or
upload size.

## Native Vulkan design

`vk_bmodel_binding_cache` is an archived Vulkan-only immediate cvar, defaulting
to `1`. It provides an intentional same-binary A/B path and a driver-triage
escape hatch.

Within each entity or bloom-emission recording pass, the renderer now records
whether the immutable inline-BSP vertex buffer plus the current frame's
instance buffer are already bound. A following GPU inline-BSP batch reuses
that binding when the cvar is enabled. The cache is invalidated before any
CPU-expanded dynamic stream, GPU MD2 stream, GPU MD5 stream, or the cel-shading
replay can replace the bindings. It is local to each recording pass and never
survives a pass boundary.

The optimization applies to normal entity recording and both native bloom
emission recorders. It does not alter:

- opaque coalescing or translucent ordering;
- descriptor or pipeline binding;
- static geometry, frame-local instance data, draw parameters, or uploads;
- the existing CPU-expanded fallback for shell, outline, rim-light, and
  item-colourize inline models.

`VK_STATS` now reports `entity_bmodel_bindings`, and the performance analyzer
summarizes it. This makes the command reduction observable rather than
inferring it from unchanged draw counts.

## Visual and validation evidence

The refreshed staged runtime passed the hidden, validation-enabled
`fr01_bmodel_first_frame_manifest.json` matrix:

| Receiver | Compared pixels | Result |
| --- | ---: | --- |
| transformed first-frame inline BSP | 170,000 | RGB-exact; transformed-bmodel mask IoU `1.0` |
| authored legacy lightmap | 34,000 | RGB-exact; mask IoU `1.0` |
| global-fullbright legacy lightmap | 34,000 | RGB-exact; mask IoU `1.0` |

The Vulkan capture completed without validation, device-lost, or fatal-error
text. The hidden runner uses `win_headless 1`, disabled input, and an isolated
runtime directory; it never launches a visible client.

## Same-binary A/B measurement

The paired collector ran the existing 960x720 dense inline-BSP fixture twice
on Intel Iris Xe / i7-13700H / Windows 11 with Intel driver
`31.0.101.5590 (2024-06-10)`. Each run collected 120 samples per renderer and
the analyzer retained 100 after the 20-sample warm-up. The immutable fixture
SHA-256 was
`3c211952df975e03a8ac0b8cdf3dd162e86d88c5e52b343ea07ed9385b83404b`.

| Vulkan metric | Cache off (`0`) | Cache on (`1`) | Interpretation |
| --- | ---: | ---: | --- |
| inline-BSP binding mean / p50 / p95 | 16 / 16 / 16 | 1 / 1 / 1 | 93.75% fewer redundant binding commands |
| draw mean | 17 | 17 | unchanged submission topology |
| upload mean | 4,800 bytes | 4,800 bytes | unchanged frame data |
| CPU render mean | 0.94617 ms | 0.62051 ms | 34.42% lower |
| CPU render p50 / p95 | 0.9255 / 1.348 ms | 0.6155 / 0.758 ms | 33.50% / 43.77% lower |
| CPU frame mean | 1.17687 ms | 0.80190 ms | 31.86% lower |
| opaque-entity GPU p50 | 0.173 ms | 0.174 ms | unchanged within timestamp precision |
| completed-frame GPU p50 | 0.370 ms | 0.369 ms | unchanged within timestamp precision |

The two launch profiles are intentionally distinct because the A/B cvar is
part of the collector configuration hash:

- cache off: `4b63931d3da1c10326f116ae71b42f0a47e022fcb944a72f1f70e784eb4ed9e0`
- cache on: `8d6945f9cb1a7f1f5bb48763c15e984cfc1293d2299175025cf099549ac54e01`

GPU means and cross-renderer timings varied between independent runs, so no
GPU or Vulkan-versus-OpenGL budget is claimed. The stable command counter,
constant draw/upload contract, and same-binary CPU-render reduction support
the narrower command-recording conclusion.

Raw collector evidence is retained under:

- `.tmp/renderer-parity/fr01-bmodel-binding-cache-off/`
- `.tmp/renderer-parity/fr01-bmodel-binding-cache-on/`

## Verification

```powershell
python -m unittest tools.renderer_parity.test_vulkan_gpu_bmodel_submission_source
meson compile -C builddir-win worr_vulkan_x86_64
python tools/refresh_install.py --build-dir builddir-win --assets-dir assets --install-dir .install --base-game basew --archive-name pak0.pkz
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-binding-cache-visual --vulkan-validation --timeout 180
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg --fixture assets/maps/worr_fr01_bmodel_instances.bsp --scenario-id fr01-bmodel-binding-cache-on --run-root .tmp/renderer-parity/fr01-bmodel-binding-cache-on --hardware-id "GPU=<adapter>; CPU=<cpu>; OS=<os>" --driver "<driver>" --vulkan-validation --min-samples 100 --launch-cvar vk_bmodel_binding_cache=1
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-bmodel-binding-cache-on/vulkan.log --opengl .tmp/renderer-parity/fr01-bmodel-binding-cache-on/opengl.log --capture-manifest .tmp/renderer-parity/fr01-bmodel-binding-cache-on/capture.json --warmup 20 --min-samples 100
```

`vk_bmodel_binding_cache=0` supplies the corresponding cache-off control.

## Remaining work

The general transient-ring allocator, wider bmodel/MD5 runtime coverage, and
a representative repeatable GPU budget remain open under `FR-01-T14` and
`FR-01-T15`.
