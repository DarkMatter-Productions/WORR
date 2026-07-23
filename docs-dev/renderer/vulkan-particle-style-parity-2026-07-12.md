# Native Vulkan Particle Style Parity

Date: 2026-07-12

Task ID: `FR-01-T01`

Status: Complete

## Outcome

The native Vulkan raster renderer now supports the two particle compositing
styles exposed by OpenGL. `vk_particle_style 0` uses ordinary source-alpha blending,
while any nonzero value uses the saturating/additive `SRC_ALPHA, ONE` behavior
of `gl_partstyle`.

This remains a native Vulkan implementation. It does not redirect rendering
through OpenGL and does not change `q2proto/`.

## Implementation

`src/rend_vk/vk_entity.c` now carries blend mode as part of each entity batch.
Ordinary translucent entities continue to use the existing alpha pipeline.
Particle batches select the new additive pipeline only when `vk_particle_style` is
nonzero.

The additive pipeline is created once with the other swapchain-dependent entity
pipelines and destroyed through the same lifecycle. Changing `vk_particle_style` at
runtime therefore changes command recording on the next frame without pipeline
recreation. All particles in a frame remain coalesced into the existing
descriptor-compatible batch, so the feature adds no per-particle draw calls or
pipeline decisions.

The Vulkan blend factors match the OpenGL state exactly:

- blended: source alpha plus destination one-minus-source-alpha;
- saturating/additive: source alpha plus destination one.

The existing default is preserved: `vk_particle_style 0`.

## Validation

- `ninja -C builddir-win worr_vulkan_x86_64.dll`
  - passed; the native Vulkan DLL compiled and linked.
- `python tools/check_shadowmapping_guardrails.py`
  - passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64`
  - refreshed the distributable staging tree, repacked 280 assets, and passed
    Windows x86-64 staged-payload validation.
- Staged native Vulkan smoke:
  - loaded renderer `vulkan` on Intel Iris Xe;
  - loaded `q2dm1` with `vk_particle_style 0`;
  - changed to `vk_particle_style 1` in the same renderer session;
  - reported `"vk_particle_style" is "1" default: "0"`;
  - exited cleanly without Vulkan renderer errors.

The smoke log is `.install/basew/logs/vulkan_particle_style_smoke.log`. Staged
logs are local evidence and are not release artifacts.

## Remaining Renderer-Parity Work

Particle shape selection and deterministic pixel-comparison coverage were
subsequently completed in
`vulkan-particle-shape-additive-visual-parity-2026-07-19.md`. Vulkan beam
style parity (`FR-01-T02`), flare behavior (`FR-01-T03`), and broader
post-processing remain separate work.
