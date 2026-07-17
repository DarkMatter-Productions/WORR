# Vulkan Rim-Bloom Occlusion Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: native sampled-depth rim bloom extraction is implemented and paired on
both a visible receiver and a real BSP-occluded receiver. The sampled path is
used when the selected Vulkan depth format exposes a sampleable depth view.
The older depth-disabled native fallback remains for formats where that view
cannot be created, so broad all-format receiver parity is still open.

## Problem

The direct `RF_RIMLIGHT` bloom replay cannot rely solely on a second
`LESS_OR_EQUAL` depth test. Alias geometry replays at slightly different
quantised values on some Vulkan depth formats, causing equal visible
fragments to disappear. The previous native workaround disabled depth testing
for the additive replay. It restored the visible rim halo but could also add
bloom for a rim fully hidden by a nearer world surface.

## Native visibility-safe path

The renderer now keeps a depth-only sampled view alongside the normal depth or
depth/stencil attachment whenever the selected format supports sampling.
`VK_CreateBloomEmissionResources` creates a second, rim-only render pass that:

- loads the completed bloom-emission colour attachment rather than clearing it;
- uses the scene depth as a read-only attachment;
- records only an actual additive rim batch, after an explicit
  depth-attachment-to-shader-read barrier.

The dynamic, GPU MD2, and GPU MD5 rim variants have matching pipelines using
the `VK_ENTITY_BLOOM_EXTRACT_DEPTH_SAMPLE` fragment specialization. It samples
the stored scene depth at `gl_FragCoord.xy` and discards the replay only when
it is behind that depth. A `0.02` equal-depth tolerance was calibrated against
the direct real-player receiver: it retains the previously validated core and
halo while still rejecting the deliberately much-nearer BSP occluder.

The ordinary bloom extraction pass is unchanged for world glow, skin glow,
and translucent shells. It still clears the emission attachment and retains
its native attachment-depth test. The selective rim pass is recorded only
when a submitted rim batch and a usable sampled-depth pipeline exist; ordinary
bloom frames pay no extra barrier, render pass, descriptor bind, or draw.
No path routes Vulkan rendering through OpenGL or expands GPU models on the
CPU.

## Deterministic obstructed receiver

`generate_model_rim_occluded_fixture.py` produces
`maps/worr_fr01_model_rim_occluded.bsp`. With third-person angle zero, the
camera is 60 units behind the player on `-X`. The generated non-emissive BSP
wall spans `X=-45..19`, between the camera and the player at the origin. The
fixture uses the same real third-person team rim and threshold-isolated bloom
settings as the visible-rim capture.

The manifest exact-compares the 112,000-pixel `[400, 280, 280, 400]` crop,
then requires zero pixels in the green rim/halo colour range in each backend.
`max_pixels_per_backend` was added to the capture comparator for this absence
contract; it complements the existing minimum coverage probe support.

## Paired evidence

The fresh 960x720 visible-rim run remains validation-clean. It reports a
25,766/25,758 saturated-green core at `0.99922` IoU and a 4,097/3,732 soft
green halo at `0.89702` IoU. Crop mean absolute RGB error is
`0.03796 / 4.47654 / 0.03628`; `9.55536%` of pixels exceed RGB error 16,
within the retained 10% receiver threshold.

The fresh occluded-rim run is exact: maximum and mean RGB error are all zero,
and zero of 112,000 crop pixels differ. Both backends have zero qualifying
green rim/halo pixels, with mask IoU `1.0`. Vulkan validation and process
failures are absent in both final runs.

The fixture is sensitive to the implementation. A temporary native
depth-disabled control leaked the hidden rim into Vulkan bloom, producing
mean green error `63.93631`, maximum green error `223`, and `55.50893%`
changed crop pixels. The sampled-depth path restores the exact result before
the final build was retained.

## Verification

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/generate_model_rim_fixture.py --validate --json
python tools/renderer_parity/generate_model_rim_occluded_fixture.py --validate --json
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest discover -s tools/renderer_parity -p 'test_*.py'
python tools/test_package_assets.py
python tools/renderer_parity/run_capture_matrix.py --renderer opengl --renderer vulkan --manifest assets/renderer_parity/fr01_model_rim_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-model-rim-bloom-emission-depth-final --timeout 120 --vulkan-validation
python tools/renderer_parity/run_capture_matrix.py --renderer opengl --renderer vulkan --manifest assets/renderer_parity/fr01_model_rim_occluded_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-model-rim-occluded-bloom-final --timeout 120 --vulkan-validation
```

Both capture commands run with the repository's hidden native-surface,
input-free headless mode.
