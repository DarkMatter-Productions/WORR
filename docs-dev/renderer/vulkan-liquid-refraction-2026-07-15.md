# Native Vulkan Transparent-Liquid Refraction

Date: 2026-07-15

Task ID: `FR-01-T10`

Status: transparent-liquid refraction, full-screen underwater waterwarp, and
entity alpha-phase placement are native Vulkan. Both the owned transparent-warp
receiver and a real-contents underwater receiver now have strict headless
paired evidence; broader liquid material coverage remains open.

## Outcome

The Vulkan raster renderer now implements transparent `SURF_WARP` refraction
without any OpenGL fallback or framebuffer feedback loop. `vk_world.c` records
opaque world work separately from transparent world work. When a registered
world contains a transparent warp face and `vk_warp_refraction` is above zero,
`vk_main.c` performs this native sequence:

```text
opaque world + inline/opaque entities + alpha-back entities
  -> swapchain image copied into a device-local sampled scene image
  -> load-preserving liquid render pass
  -> sorted transparent world batches + effects + alpha-front entities
```

The scene image is swapchain-sized device-local Vulkan memory with
`TRANSFER_DST | SAMPLED` usage. The swapchain is requested with transfer-source
usage when the surface supports it. All image layouts and access transitions
are explicit: the opaque swapchain color attachment becomes a transfer source,
the scene image becomes a shader-read sampler, and the swapchain returns to a
color attachment for the liquid load pass. Unsupported hardware leaves the
scene image unavailable and disables only refraction; it does not redirect to
OpenGL or fail renderer startup.

Opaque depth is stored by the main pass and loaded by the liquid pass. This
keeps depth rejection correct for the late alpha work without copying or
sampling the active color attachment. The liquid pass is render-pass compatible
with the normal world pipeline, so it reuses the same native pipeline objects.

## Shader and cvar contract

The per-frame world instance stream is 64 bytes: the first 16 bytes carry the
animated liquid controls and the remaining bytes carry the native sky axes:

```text
float time
float refraction_scale
uint32 effects_enabled
uint32 refraction_enabled
```

`vk_warp_refraction` is a Vulkan-exclusive cvar with OpenGL's `0.1` default and
is clamped to `[0, 2]`. Refraction activates only for a visible world frame
that contains a transparent `SURF_WARP` face, has Vulkan shader effects
enabled, and has a valid native scene image descriptor.

The visible fragment shader binds that descriptor at set 3. It preserves the
OpenGL distortion model: turbulent warp is evaluated per fragment, converted
from surface UV space into screen-space pixels using UV derivatives, and used
to sample both the original and displaced opaque scene. The shader then
compensates for normal source-alpha blending, so a translucent water surface
mixes the refracted background and the material color instead of darkening it
twice. The shadow shader compilation remains unanimated and does not bind the
scene sampler.

## Underwater full-screen waterwarp

`vk_postprocess.c` adds a separate native full-screen Vulkan pipeline and the
`vk_waterwarp` cvar (default `1`). A rendered `RDF_UNDERWATER` view copies its
completed 3D scene into the same sampled image, then overwrites the swapchain
with a three-vertex full-screen pass. The fragment shader preserves OpenGL's
2D `GLS_WARP_ENABLE` constants and top-origin source-texture orientation:

```text
tc = gl_FragCoord.xy / output_size
warp_tc = (tc.x, 1 - tc.y)
tc += 0.0025 * sin(warp_tc.yx * (10*pi) + time) * (1, -1)
```

The normal UI pass is deferred until after this composition pass, so the HUD
and menu text do not wobble. When transparent liquid refraction is active, the
sequence makes one scene copy for the liquid pass and a second copy after its
late alpha work, before depth-aware DOF and the full-screen warp. DOF samples
the completed scene and depth through native Vulkan off-screen targets; the
UI overlay remains separate and sharp.

## Ordering boundary

The native command path tags every entity batch during frame construction as
opaque, alpha-back, post-liquid effects, or alpha-front. Inline BSP models
retain OpenGL's early placement. General translucent entities use
`vk_draworder` (default `1`) and weapon models always use the alpha-front
phase. Before the scene copy, Vulkan records opaque/inline work plus alpha-back
entities. The second liquid pass records sorted transparent world faces, then
beams, particles, flare work, and alpha-front entities. This removes the prior
all-entity-before-liquid ordering error without creating any OpenGL path.

## Headless validation

The repository now has a static, non-windowed regression test at
`tools/renderer_parity/test_vulkan_liquid_refraction_source.py`. It verifies
the native scene-copy resource, load pass, copied-scene ordering, shader
derivative/alpha-compensation contract, split world and entity ordering APIs,
native waterwarp activation/formula, and absence of an OpenGL renderer path.

Validation completed without launching a client window:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools/renderer_parity/test_vulkan_liquid_refraction_source.py
```

The generated SPIR-V validates and the Vulkan DLL links. The source regression
passes. The staged `.install` tree must be refreshed after the final build.

`generate_viewweapon_shell_bloom_refraction_fixture.py` adds an owned
transparent `SURF_WARP | SURF_TRANS33 | SURF_FLOWING` receiver. It freezes the
flow phase with `pause` before the hidden-native-surface screenshot and uses a
water-only `[160, 120, 320, 400]` crop. The paired OpenGL/Vulkan result has
maximum RGB error `1 / 1 / 1`, MAE `0.47960 / 0.09655 / 0.43227`, and zero
pixels over error 1 under validation. Its real Quad view weapon also exercises
the late depth-hack bloom load pass after liquid; that source has a separate
strict view-weapon gate.

`generate_underwater_waterwarp_fixture.py` adds the complementary real-water
receiver. Its static checkerboard map marks the player's leaf as
`CONTENTS_WATER`, so normal player movement produces `RDF_UNDERWATER`; it does
not fabricate renderer state. The enabled `[160, 120, 640, 480]` crop has
maximum RGB error `1 / 1 / 2`, MAE `0.05694 / 0.51707 / 0.80232`, and zero
pixels over error 2 under validation. The disabled-waterwarp control retains
the real contents and also has zero pixels over error 2. Details are in
`vulkan-underwater-waterwarp-visual-parity-2026-07-16.md`. Broader
transparent-material coverage remains required before this task can be
treated as globally complete.
