# Vulkan Authored Bloom Emission Visual Parity

Date: 2026-07-16

Task ID: `FR-01-T12`

Status: scoped paired visual gate passed. This closes the previously observed
wall-glow emissive-MRT delta without redirecting Vulkan through OpenGL.

## Problem

The native Vulkan bloom baseline thresholded the complete copied scene. The
OpenGL path has an additional material-emission target: a lightmapped wall
with a glow companion writes `diffuse.rgb * glowmap.a` into that target before
scene lighting and threshold extraction. On the deterministic wall-glow scene
this produced a uniform `24 / 40 / 0` RGB discrepancy over the 250 by 200
capture crop: OpenGL was `120 / 200 / 255`; Vulkan was `96 / 160 / 255`.

The issue was functional, not a cvar mismatch. Both renderers used matched
`*_bloom` controls, zero threshold/knee, intensity one, one blur iteration,
and the same glowmap source.

## Native implementation

`vk_main.c` creates a per-frame, full-resolution device-local emission image
with `COLOR_ATTACHMENT` and `SAMPLED` usage plus a framebuffer that loads the
already-rendered world depth. Its render pass transitions the image from
shader read to color attachment before the draw and back to shader read after
it. That gives separate frame slots independent source images while retaining
the existing bounded frames-in-flight ownership model.

`vk_world.c` creates a dedicated `pipeline_bloom_extract` only when the
swapchain exposes the extract render pass. It shares immutable world geometry,
per-frame instance data, descriptors, and the normal depth convention, but
has depth writes disabled. The replay draws only opaque non-sky world batches
with a lightmapped glow companion; the `VK_WORLD_BLOOM_EXTRACT` shader variant
samples glow alpha and outputs the OpenGL wall-emission expression. Its global and height fog branches multiply
emission by the same transmission that OpenGL applies to its bloom output.

`vk_bloom.frag` now accepts a second sampled image binding. In prefilter mode,
it performs the existing four-tap scene downsample, firefly clamp, and
threshold/knee calculation, then adds the four-tap authored emission image.
The `aux.y` push constant makes the path safe on devices where the extraction
resource could not be created: the old scene-only descriptor remains valid
and no image is accidentally added twice. Blur modes ignore that binding.

This is deliberately not a permanent scene MRT. With `vk_bloom 0`, no extract
draw or attachment transition is recorded and all ordinary scene pipelines
stay single-color-target. The emission image is persistent swapchain-local
memory so it is not allocated or freed on the bloom hot path.

## Deterministic evidence

The new repository-owned scene is:

- config: `assets/renderer_parity/fr01_bloom_emission.cfg`
- manifest: `assets/renderer_parity/fr01_bloom_emission_manifest.json`
- map: `worr_fr01_glowmap`
- capture crop: `[100, 100, 250, 200]` (50,000 pixels)

The final controls keep fog/DOF/CRT/colour correction off and set matched
OpenGL/Vulkan bloom threshold and knee to zero, intensity to one, one blur
iteration, four-times downscale, and the same glowmap intensity. The stable
wall result is `[120, 200, 255]` for every crop pixel.

The validated paired run at
`.tmp/renderer-parity/fr01-bloom-emission-first/report.json` reports:

```text
maximum RGB error:                 0 / 0 / 0
mean absolute RGB error:           0.0 / 0.0 / 0.0
pixels over threshold:             0 / 50,000
authored-emission probe:           50,000 / 50,000 pixels, IoU 1.0
Vulkan validation/process errors:  none
```

## Verification

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py
python -m unittest tools.renderer_parity.test_vulkan_bloom_source tools.renderer_parity.test_bloom_emission_fixture
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bloom_emission_manifest.json --run-root .tmp/renderer-parity/fr01-bloom-emission-final --vulkan-validation --json-output .tmp/renderer-parity/fr01-bloom-emission-final/report.json
```

## Remaining boundary

The scoped pass proves opaque static-world glowmap emission only. OpenGL also
has emission paths for model skins, shells, and additional material families;
those still need native extract coverage and paired scenes. The OpenGL
mip-pyramid bloom path, HDR/tone mapping, and broader post-process scenes are
also outside this gate.
