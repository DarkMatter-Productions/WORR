# Vulkan Shared Anisotropy Control Parity

Date: 2026-07-17

Task ID: FR-01-T14

Status: complete for the shared-control parity slice; broader model and
renderer performance work remains tracked by FR-01-T14 and FR-01-T15.

## Outcome

The Video settings control now writes r_anisotropy, a renderer-shared
preference, instead of the OpenGL-only gl_anisotropy spelling. This applies to
the classic menu declaration, cgame JSON definition, and live RmlUi Video
route. Vulkan therefore receives the same player-selected sampling quality as
OpenGL without forwarding any Vulkan work through the OpenGL renderer.

The legacy OpenGL spelling remains functional. Vulkan retains vk_anisotropy as
a native renderer-specific compatibility alias for established configs and
scripts. On renderer initialization, an explicitly configured legacy alias is
adopted only when r_anisotropy has not been configured; otherwise the shared
value wins. Subsequent interactive changes synchronize both spellings.

## Native update behavior

OpenGL applies either spelling to its wall and skin texture parameters after
the alias has synchronized. Vulkan rebuilds its native repeat/clamp and
linear/nearest samplers, waits for submitted work to complete, and rebinds all
resident native image descriptors. The valid one-times fallback remains in
place for adapters that do not expose sampler anisotropy.

## Regression coverage

tools/renderer_parity/test_shared_anisotropy_control_source.py locks all three
Video menu routes, OpenGL live texture updates, Vulkan alias registration,
native sampler refresh and teardown, plus the durable forced-1x fixture:
assets/renderer_parity/fr01_model_glowmap_shared_anisotropy.cfg and its
matching manifest. The existing test_vulkan_sampler_anisotropy_source.py also
guards the shared preference as the input to Vulkan's native sampler
construction.

The focused suite passed 38 tests:

~~~
python -m unittest tools.renderer_parity.test_shared_anisotropy_control_source tools.renderer_parity.test_vulkan_sampler_anisotropy_source tools.renderer_parity.test_vulkan_ui_filter_parity_source tools.renderer_parity.test_vulkan_gpu_md2_submission_source tools.renderer_parity.test_vulkan_gpu_md5_submission_source tools.renderer_parity.test_vulkan_bloom_source tools.renderer_parity.test_model_gpu_md2_fixture
~~~

Both native DLLs rebuilt successfully and the staged runtime was refreshed:

~~~
ninja -C builddir-win worr_opengl_x86_64.dll worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
~~~

The durable forced-1x paired capture ran from the staged tree with Vulkan
validation:

~~~
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_glowmap_shared_anisotropy_manifest.json --run-root .tmp/renderer-parity/fr01-model-glowmap-shared-anisotropy --vulkan-validation --timeout 180 --json-output .tmp/renderer-parity/fr01-model-glowmap-shared-anisotropy/result.json
~~~

The config logged r_anisotropy at 1 and the active native alias at 1 in both
renderer processes. The 34,100-pixel crop had maximum RGB error 16 / 6 / 5,
mean absolute RGB error 0.56927 / 0.15161 / 0.10783, and zero pixels above the
RGB-16 threshold. Its bright-glow masks contained 1,827 OpenGL and 1,797
Vulkan pixels with 0.95469 IoU, safely inside the fixture contract. The Vulkan
log contained no VUID or validation errors.

The standard device-limit forced-GPU-MD2 gate was rerun after the control
change. It retained maximum RGB error 2 / 2 / 1, zero over-threshold pixels,
and an exact 2,035-pixel glow mask with 1.0 IoU. Results are stored in
.tmp/renderer-parity/fr01-model-glowmap-md2-gpu-shared-anisotropy-default/
result.json.
