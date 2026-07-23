# Native Vulkan Bloom Stage Pipeline Specialization

Date: 2026-07-19

Task IDs: `FR-01-T12`, `FR-01-T15`

Status: implemented, validation-tested, and retained. The Vulkan renderer now
uses stage-specialized native pipelines for active bloom rather than dispatching
one monolithic fragment shader that branches between prefilter, horizontal
blur, and vertical blur for every fullscreen fragment.

## Change

`vk_bloom.frag` remains the generic copy stage used by the depth-of-field
route. New native `vk_bloom_prefilter.frag` and compile-time horizontal and
vertical variants of `vk_bloom_blur.frag` exactly preserve the established
four-tap prefilter, emitted-material contribution, paired bilinear Gaussian
taps, uniform coefficient table, filter radius, attachments, and draw count.

`VK_PostProcess_RecordBloomPass` selects the copy, prefilter, X, or Y pipeline
from its already explicit mode. Active bloom therefore no longer evaluates the
generic mode or direction branches in the prefilter and blur fragments. The
existing descriptor-set and push-constant ABI is retained, so direct authored
emission, no-emission fallback descriptors, and depth-of-field continue to
use their native Vulkan resources. The SPIR-V generator emits all four modules
into `vk_bloom_spv.h`; no OpenGL renderer path is called or redirected.

## Visual and validation evidence

The active translucent-shell bloom gate was rerun at 960x720 with Vulkan
validation:

| Receiver | Result |
| --- | --- |
| `fr01_model_shell_bloom_emission_manifest.json` | 59,400-pixel crop, maximum RGB `6 / 10 / 5`, zero pixels over error 16, exact 53,091-pixel shell-core mask, halo IoU `0.983754` |
| `fr01_dof_manifest.json` (`depth_aware_dof_bmodel_focus`) | 307,200-pixel crop, maximum RGB `1 / 1 / 0`, MAE `0.002324 / 0.000557 / 0`, zero pixels over error 1 |

The runs are retained under
`.tmp/renderer-parity/bloom-stage-specialization-visual` and
`.tmp/renderer-parity/dof-stage-specialization-focus`. Neither log contains a
VUID, validation error, device loss, or fatal renderer error.

## Performance evidence

The release-style paired shell capture uses the same `960x720`, fixed-view,
one-iteration configuration, 20-frame warm-up, 100 retained samples, Intel
Iris Xe adapter, and `31.0.101.5590` driver as the immediately preceding
current-build baseline. These phase timestamps are local diagnostic evidence,
not a renderer-wide Vulkan/OpenGL performance budget.

| Native Vulkan measurement | Complete GPU mean | GPU post mean | GPU scene mean | Draws / post draws |
| --- | ---: | ---: | ---: | ---: |
| Pre-specialization current baseline | 1.67201 ms | 0.94958 ms | 0.67420 ms | 8 / 4 |
| Specialized run | 1.65675 ms | 0.93980 ms | 0.66946 ms | 8 / 4 |
| Specialized repeat | 1.65294 ms | 0.93767 ms | 0.66851 ms | 8 / 4 |

Across the two specialized runs, the active-bloom GPU post mean is `0.93874`
ms: `0.01085` ms (1.14%) below the preceding current-build baseline. Complete
GPU mean averages `1.65485` ms, 1.03% lower. The stable draw/upload contract
remains eight total draws, four post-process draws, and 400 upload bytes. CPU
times are intentionally not promoted because presentation pacing dominates
the end-to-end measure on this adapter. OpenGL remains faster in this focused
GPU lane, so no cross-renderer GPU superiority claim or budget is added.

The new paired provenance roots are
`.tmp/renderer-parity/fr01-renderer-perf-bloom-shell-stage-specialized-release`
and
`.tmp/renderer-parity/fr01-renderer-perf-bloom-shell-stage-specialized-repeat-release`.
Both retain the fixture SHA-256
`75c27ba5f45bc1e3e0b56fd55092477c08dd2c02bb15b173efba741d14d5a2f3`
and configuration/profile SHA-256
`f98517b41dc6769271ce67f087fd1055bf791654907a09faf1af8c6fde590ec7`.

## Verification

```text
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_bloom_source.py'
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_shell_bloom_emission_manifest.json --run-root .tmp/renderer-parity/bloom-stage-specialization-visual --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_dof_manifest.json --scene depth_aware_dof_bmodel_focus --run-root .tmp/renderer-parity/dof-stage-specialization-focus --vulkan-validation
python tools/renderer_parity/run_renderer_perf_capture.py --install-dir .install --config renderer_parity/fr01_renderer_perf_bloom_shell.cfg --fixture assets/maps/worr_fr01_model_shell.bsp --scenario-id fr01-model-shell-active-bloom-stage-specialized-release --run-root .tmp/renderer-parity/fr01-renderer-perf-bloom-shell-stage-specialized-release --hardware-id "GPU=Intel(R) Iris(R) Xe Graphics; CPU=13th Gen Intel Core i7-13700H; OS=Windows 11" --driver "Intel=31.0.101.5590" --min-samples 100
python tools/renderer_parity/analyze_renderer_perf.py --vulkan .tmp/renderer-parity/fr01-renderer-perf-bloom-shell-stage-specialized-release/vulkan.log --opengl .tmp/renderer-parity/fr01-renderer-perf-bloom-shell-stage-specialized-release/opengl.log --capture-manifest .tmp/renderer-parity/fr01-renderer-perf-bloom-shell-stage-specialized-release/capture.json --warmup 20 --min-samples 100
```

All runtime verification is hidden/headless (`win_headless 1`) with input and
audio disabled.
