# Native Vulkan CRT Presentation Baseline

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: partial implementation. Vulkan now has a native CRT presentation pass
with the shared `r_crt*` control contract and a retained exact paired capture
for the unmasked fixed-scale case. Gameplay depth of field and resolution
scaling remain separate, unimplemented parts of the task.

## Contract

The Vulkan renderer registers the same public controls consumed by the OpenGL
renderer:

- `r_crtmode` enables the effect.
- `r_crt_brightboost`, `r_crt_hard_pix`, and `r_crt_hard_scan` control the
  phosphor reconstruction kernel and scanline falloff.
- `r_crt_mask_dark`, `r_crt_mask_light`, and `r_crt_shadow_mask` select and
  tune the shadow-mask pattern.
- `r_crt_scale_in_linear_gamma` enables the same sRGB-to-linear reconstruction
  and conversion back to sRGB used by the OpenGL CRT shader.

The native fragment implementation retains OpenGL's five-tap horizontal
reconstruction, three-row scanline combination, alternating scanline factor,
and all four shadow-mask layouts. Values are clamped to the equivalent ranges
before they are pushed to the shader.

## Native execution and ordering

`vk_crt.frag` is compiled to `vk_crt_spv.h` and uses only the native Vulkan
scene descriptor. Its 48-byte push block contains CRT parameters, mask
parameters, and texel dimensions; a compile-time C assertion prevents its
layout drifting from the GLSL declaration.

The post-process controller creates the CRT graphics pipeline with the
existing Vulkan post-process vertex shader and liquid render pass. No OpenGL
renderer function, texture, framebuffer, or shader is used.

At frame recording time:

1. The completed 3D scene is copied to the existing Vulkan sampled scene
   image.
2. Bloom and the base waterwarp/colour/LUT composite run when enabled.
3. When a base composite was needed, its result is copied once more into that
   sampled scene image.
4. The CRT pass writes the presentation result to the swapchain.
5. HUD, menu, and console geometry are rendered afterward through the normal
   overlay pass, so CRT filtering never softens UI text or widgets.

For CRT-only frames, steps 2 and 3 are skipped: the first native scene copy is
sampled directly by the CRT draw. With `r_crtmode 0`, the CRT pipeline is not
recorded and existing identity post-process frames still avoid both the scene
copy and fullscreen draw.

The final pass has a negative-height Vulkan viewport to preserve the engine's
top-left presentation convention. `vk_crt.frag` offsets the alternating
scanline parity by one output pixel so bright/dark rows land on the same final
screenshot rows as OpenGL; it does not change the CRT reconstruction kernel.

## Performance boundary

CRT resources are created with swapchain resources, rather than during a
frame. Enabling CRT adds one fullscreen draw and one scene copy when it is the
only effect; combining it with colour correction, bloom, or waterwarp adds
only the final copy/draw required to feed the CRT stage. It does not add a
per-frame descriptor allocation, CPU-side image conversion, or OpenGL
fallback.

This is intentionally a baseline, not a claim that the renderer has dynamic
resolution scaling or a complete cross-renderer performance budget. Those
remain `FR-01-T15` work. Strict fixed-focus, centre-depth/automatic-range,
wide explicit-range, and compact-menu rectangle-state gameplay-DOF receivers
are now covered under `FR-01-T12`; isolated rectangle clipping and broader
dynamic-scene coverage remain open. See
`vulkan-dof-visual-parity-2026-07-16.md`.

## Headless verification

The following checks pass without launching a client window:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python -m unittest tools/renderer_parity/test_vulkan_crt_source.py tools/renderer_parity/test_crt_fixture.py
```

The source test checks the shared cvar names, independent native pipeline and
push block, scanline/mask shader code, composition ordering, and UI-overlay
boundary. `fr01_crt_manifest.json` now exact-compares fixed unmasked and
shadow-mask-layout-2 CRT scenes, locking 25,000-pixel scanline and
8,400-pixel green-mask probes at IoU `1.0`; other mask layouts and UI-boundary
scenes remain separate coverage. Details are in
`vulkan-crt-visual-parity-2026-07-15.md`.
