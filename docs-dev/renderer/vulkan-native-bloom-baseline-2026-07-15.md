# Native Vulkan Bloom Baseline

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: partial implementation. Native Vulkan now has the OpenGL renderer's
scene threshold/knee/fallback bloom plus native authored wall-glow,
opaque-model-skin, translucent shell, and additive rim-light emission:
firefly clamping, downsampled separable Gaussian blur, and final
saturation/intensity blending. The wall, opaque-model, and direct
translucent-shell/rim emission routes are paired against OpenGL under
validation. A sampleable-depth rim path now exact-compares a real
BSP-occluded receiver; the depth-disabled native fallback remains only for
formats without sampleable depth. General alpha/depth-hack/material-family
emission and multi-level mip pyramids remain open, as do HDR and resolution
scaling.

## Outcome

`vk_postprocess.c` owns two device-local, downsampled colour attachments. Once
the current scene copy is available, Vulkan renders a four-tap prefilter into
the first target, then alternates horizontal and vertical Gaussian blur passes
between the targets. The native final post-process blends the completed bloom
image before colour correction, split toning, and LUT grading, matching the
ordering of the OpenGL postfx path.

The implementation is native Vulkan, not an OpenGL redirect. It does not
depend on GL framebuffer objects, GLSL program state, or renderer calls. The
final descriptor set has three native sampled-image bindings: scene,
LUT/fallback, and bloom/fallback. This allows bloom and LUT grading to run
together without a second scene copy or an additional full-resolution
composition pass.

When bloom is active, each frame slot also owns a full-resolution sampled
emission attachment and a depth-loading extraction framebuffer. Opaque world
batches with a lightmapped glow companion and opaque glow-bearing entity
batches are replayed with dedicated depth-tested pipelines. World/inline BSP
receivers write `base.rgb * glowmap.a`; model skins write their premultiplied
glow RGB before scene lighting/threshold extraction; translucent colour shells
replay through matching alpha extract pipelines and write their
post-intensity shell RGB. Additive `RF_RIMLIGHT` batches replay through
dynamic, GPU-MD2, and GPU-MD5 native extract pipelines and write the squared
view-rim source with matching additive blending. The rim source applies a
receiver-calibrated `0.70` scale before the native blur, compensating for the
observed blur-energy difference at matched public controls. When the selected
depth format is sampleable, a selective second rim pass loads the emitted
colour, samples retained read-only scene depth at the fragment coordinate, and
rejects the rim only when it is behind a nearer receiver; it is skipped on
ordinary no-rim bloom frames. All four apply the same
global and height fog transmission. The bloom prefilter samples the copied
scene and the emission attachment independently: it threshold-filters only
the scene, then adds authored emission before the existing downsampled blur.
The normal scene pass remains single-target and unchanged.

## Controls and identity behavior

The Vulkan-exclusive controls mirror the active OpenGL baseline:

- `vk_bloom`
- `vk_bloom_iterations` (`1..8`, each setting produces a horizontal and
  vertical blur pass)
- `vk_bloom_downscale` (`1..8`)
- `vk_bloom_firefly` (`0..1000`)
- `vk_bloom_sigma` (`1..25`)
- `vk_bloom_threshold` (`0..10`)
- `vk_bloom_knee` (`0..1`)
- `vk_bloom_intensity` (`0..10`)
- `vk_bloom_saturation` and `vk_bloom_scene_saturation` (`0..4`)

When `vk_bloom` is disabled, the bloom targets are not recorded and the final
descriptor falls back to the scene image. Existing identity fast paths for
waterwarp, colour correction, split toning, and LUTs stay intact. Resource and
descriptor changes occur only after the submitted-frame fence has signalled,
so an in-flight command buffer never references a destroyed descriptor or
intermediate image.

## Performance characteristics

Bloom runs at the configured downscaled resolution and uses a separable blur,
avoiding full-resolution multi-pass work. The full-resolution emission
attachment is written only when `vk_bloom` is active; it reuses opaque depth
and does not add an MRT target, shader output, or draw to bloom-disabled
frames. Persistent ping-pong targets and frame-local emission attachments are
rebuilt only with swapchain resources. The renderer's native phase telemetry
attributes this work to composition rather than hiding it in a single
aggregate frame time.

Depth-hacked direct entity receivers now reuse the same submitted native
dynamic/GPU-MD2/GPU-MD5 batches, depth-hack viewport, and view-weapon push
projection in the bloom extractor. No-refraction frames append those selected
draws to the existing extract pass. A compatible colour-load extract pass is
recorded after liquid only when bloom is active and an eligible direct
depth-hack receiver exists, so ordinary frames do not incur an extra pass.
See `vulkan-depthhack-viewweapon-bloom-visual-parity-2026-07-16.md`.

## Headless validation

`tools/renderer_parity/test_vulkan_bloom_source.py` verifies the native cvar
surface, ping-pong colour targets, extraction-pass wiring, separate emission
prefilter input, prefilter/blur/composite ordering, combined scene/LUT/bloom
descriptors, and absence of an OpenGL renderer route.
`test_bloom_emission_fixture.py`, `test_model_bloom_emission_fixture.py`,
`test_model_shell_bloom_emission_fixture.py`, and
`test_generate_model_rim_fixture.py`/`test_generate_model_rim_occluded_fixture.py`
lock the scoped wall, MD2 glowmap, translucent-shell, visible-rim, and
occluded-rim scenes. The
validation is non-interactive:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest discover -s tools/renderer_parity -p 'test_*.py'
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-bloom-emission-final --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_rim_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-model-rim-bloom-emission-final --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_model_rim_occluded_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-model-rim-occluded-bloom-final --vulkan-validation
```
