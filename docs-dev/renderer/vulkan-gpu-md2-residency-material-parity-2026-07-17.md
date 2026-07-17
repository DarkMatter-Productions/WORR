# Vulkan GPU MD2 Residency and Material-Sampling Parity

Date: 2026-07-17

Task IDs: FR-01-T14, FR-01-T15, and FR-01-T11

Status: complete for the native GPU-MD2 residency and stock-skin material
slice; broader MD5 and model-family performance coverage remains tracked by
FR-01-T14/T15.

## Outcome

The native Vulkan MD2 GPU-interpolation path is now functional rather than
silently falling back to CPU-expanded geometry. A forced stock dmspot MD2
capture passes its durable OpenGL/Vulkan glow-material gate under validation.
The repair also restored the OpenGL-equivalent anisotropic material-sampling
default on capable Vulkan devices; this was the source of the remaining
red-channel model-skin difference, not GPU interpolation.

This work remains fully native to rend_vk; no Vulkan path delegates rendering
or texture sampling to OpenGL.

## Correctness and lifetime fixes

VK_Entity_CreateMd2GpuResources allocated the static destination buffers but
did not assign each buffer to its upload descriptor. As a result,
VK_Entity_CopyStaticBuffers rejected every MD2 static upload as an invalid
range, and ordinary models fell back with:

~~~
Vulkan entity: GPU MD2 residency unavailable ... invalid static upload range
~~~

The upload now records uploads[i].destination before the staging copy. The
model lifetime path destroys both MD2 and MD5 device-local resource groups
before freeing their CPU allocations. GPU MD2 and MD5 batch coalescing also
requires matching vertex-effect flags, and bloom replay resolves the actual
CPU/GPU batch flags before deciding whether a draw has direct emission. This
prevents a coalesced mixed-effect group from dropping or inventing glow/rim
work.

The fallback diagnostic now includes the native allocation/copy error when a
GPU model cannot become resident. It was retained because it provides useful
release-build diagnostics without changing the fallback behavior.

## Sampling parity repair

The no-glow control isolated the residual mismatch to material sampling:

| Configuration | Mean absolute RGB error | Pixels above RGB error 16 |
|---|---:|---:|
| Vulkan 1x sampling vs. default OpenGL sampling | 1.27449 / 0.45733 / 0.43211 | 0.43109% |
| Both backends at 1x sampling | 0.29724 / 0.10595 / 0.09311 | 0.00000% |

OpenGL defaults gl_anisotropy to the device limit for wall and skin materials.
Vulkan had created all native image samplers with anisotropy disabled, so
visibly minified model skins used 1x trilinear filtering even though the same
hardware exposed a higher-quality path.

Vulkan now:

- queries samplerAnisotropy and maxSamplerAnisotropy on the selected physical
  device;
- enables only that supported core feature when it creates the logical device;
- exposes r_anisotropy as the renderer-shared video preference, defaulting to
  the supported device limit and clamping it to the legal range, with
  vk_anisotropy retained as a Vulkan-specific compatibility alias;
- creates native repeat/clamp and linear/nearest material samplers with the
  selected anisotropy; and
- safely drains submitted work, creates replacement samplers, and rebinds all
  resident material descriptors when the shared preference or Vulkan alias
  changes.

Devices without the feature retain the valid 1x path.

## Forced GPU-MD2 visual gate

assets/renderer_parity/fr01_model_glowmap_md2_gpu.cfg disables the optional
MD5 replacement before map registration. The matching manifest checks the
stock MD2 native GPU interpolation lane and does not rely on a replacement
model or an unexercised fallback.

The final hidden-native-surface run used the staged .install tree:

~~~
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_glowmap_md2_gpu_manifest.json --run-root .tmp/renderer-parity/fr01-model-glowmap-md2-gpu-anisotropy --vulkan-validation --timeout 180 --json-output .tmp/renderer-parity/fr01-model-glowmap-md2-gpu-anisotropy/result.json
~~~

Results for the 34,100-pixel model crop:

- maximum RGB error: 2 / 2 / 1;
- mean absolute RGB error: 0.00046921 / 0.00032258 / 0.00020528;
- zero pixels above RGB error 16;
- exact bright-glow mask: 2,035 pixels in both renderers, 1.0 IoU; and
- no process failures, VUIDs, validation errors, GPU-residency fallback, or
  static-upload-range diagnostics.

The Vulkan process selected Intel(R) Iris(R) Xe Graphics for this evidence.

An earlier CPU control with vk_md2_gpu_lerp 0 produced the exact same Vulkan
screenshot hash as the newly functional GPU path. That proves the
interpolation/storage implementation itself did not cause the color delta;
the anisotropic sampling correction fixed the shared material presentation.

The normal replacement-enabled companion gate also passed under validation.
It logged the dmspot MD5 replacement with default vk_md5_use and
vk_md5_gpu_skinning settings, emitted no GPU-MD5 fallback diagnostic, and
held the same 34,100-pixel crop to a maximum RGB error of 1 / 1 / 1, zero
pixels over threshold, and an exact 1,864-pixel bright-glow mask.

## Regression coverage

tools/renderer_parity/test_vulkan_gpu_md2_submission_source.py and
test_vulkan_gpu_md5_submission_source.py lock static upload destinations,
device-resource teardown, effect-safe batching, and bloom flag resolution.
test_vulkan_sampler_anisotropy_source.py locks supported-device feature
selection, 1x fallback, native sampler construction, and safe live
descriptor rebinding. test_shared_anisotropy_control_source.py locks the
shared Video preference, both renderer aliases, and the durable 1x capture
fixture.

The focused suite passed:

~~~
python -m unittest tools.renderer_parity.test_vulkan_sampler_anisotropy_source tools.renderer_parity.test_vulkan_ui_filter_parity_source tools.renderer_parity.test_vulkan_gpu_md2_submission_source tools.renderer_parity.test_vulkan_gpu_md5_submission_source tools.renderer_parity.test_vulkan_bloom_source tools.renderer_parity.test_model_gpu_md2_fixture
~~~

The native Vulkan DLL rebuilt successfully, followed by the required staged
runtime and packaged-asset refresh:

~~~
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
~~~
