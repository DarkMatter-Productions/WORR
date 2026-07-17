# Vulkan Inline-BSP Budget Revalidation

Date: 2026-07-16

Task IDs: `FR-01-T14`, `FR-01-T15`

Status: the existing dense-inline-BSP CPU budget is rejected by current
evidence and must not be presented as a current performance pass.

## Method

Two independent hidden, validation-enabled 120-sample captures reran the
repository's `fr01-bmodel-instance-grid-telemetry` collector at 960 by 720.
They use the current `renderer_parity/fr01_renderer_perf_bmodel_instances.cfg`
and `worr_fr01_bmodel_instances.bsp` fixture on the budget's recorded Intel
Iris Xe / i7-13700H / Windows 11 / Intel 31.0.101.5590 environment. Each
backend produced 120 valid full-frame GPU samples; analysis trims the first 20.

The current capture configuration hashes to
`c3e9b06a94977b0474c166d54cf5619f1a6075ae53fe57bceb1b8175565aefc1`.
The checked-in budget instead pins
`b96cbed2ea23060db61ce8531f131749ebb03cfa718bec1dcb65671c4a421b82`,
so the strict analyzer correctly rejects it before treating the old limits as
current proof.

## Results

| Run | Vulkan CPU mean / p95 | OpenGL CPU mean / p95 | Vulkan/OpenGL CPU mean | Vulkan GPU frame mean | OpenGL GPU frame mean |
| --- | ---: | ---: | ---: | ---: | ---: |
| `fr01-bmodel-instances-current` | 1.299 / 1.776 ms | 2.519 / 3.613 ms | 0.516× | 1.200 ms | 0.322 ms |
| `fr01-bmodel-instances-rerun` | 1.452 / 1.709 ms | 2.508 / 3.200 ms | 0.579× | 1.200 ms | 0.319 ms |

Both runs retained exactly 18 Vulkan draws and 4,800 uploaded bytes for every
post-warm-up sample. They therefore still demonstrate the native submission
reduction relative to OpenGL's 38 draws and CPU superiority on this workload.
They do not satisfy the old Vulkan absolute CPU limits of `0.7 ms` mean and
`1.0 ms` p95, and their comparable `gpu_frame_ms` values are approximately
3.7 times OpenGL. No new or relaxed budget is accepted from these runs.

## Consequence

The budget needs a fresh, versioned configuration contract and a root-cause
optimization for the current scene-side GPU cost before it can again be used
as proof of the requested performance objective. Updating only the pinned
hash or increasing absolute limits would hide the regression and is therefore
not an acceptable resolution.

The retained raw evidence is under:

- `.tmp/renderer-parity/fr01-bmodel-instances-current`
- `.tmp/renderer-parity/fr01-bmodel-instances-rerun`

## Verification

```powershell
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bmodel_instances.cfg --fixture assets/maps/worr_fr01_bmodel_instances.bsp --scenario-id fr01-bmodel-instance-grid-telemetry --run-root .tmp/renderer-parity/fr01-bmodel-instances-rerun --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel(R) Core(TM) i7-13700H; OS=Windows 11 Home 10.0.26200" --driver "Intel 31.0.101.5590 (2024-06-10)" --vulkan-validation --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-bmodel-instances-rerun/vulkan.log --opengl .tmp/renderer-parity/fr01-bmodel-instances-rerun/opengl.log --capture-manifest .tmp/renderer-parity/fr01-bmodel-instances-rerun/capture.json --warmup 20 --min-samples 100
```
