# Native Vulkan Resolution Scaling Implementation Plan

Task: `FR-01-T12` (performance evidence: `FR-01-T15`)

## Existing OpenGL contract

OpenGL exposes `r_resolutionscale` with three modes: disabled, fixed
independent X/Y scales, and adaptive scale. The adaptive controller uses a
smoothed CPU draw-time sample plus configurable thresholds, hysteresis, and
step sizes. It renders 3D into scale-sized FBO attachments, executes
scene-relative bloom/DOF/refraction work at that size, then makes the final
post-process presentation pass fill the native output size. HUD and menu UI
remain native-resolution.

The relevant implementation is `src/rend_gl/main.c` (`GL_UpdateResolutionScale`
and `GL_BindFramebuffer`), with render dimensions consumed by the bloom, DOF,
refraction, CRT, texture-allocation, and 3D viewport paths.

## Vulkan target architecture

Vulkan already has the needed split for HDR: a frame-slot owned offscreen
scene image, a sampled scene-copy image for consumers that need it, and a
full-size swapchain presentation/UI target. The implementation must generalise
that resource family from float-HDR-only to either:

1. float `R16G16B16A16_SFLOAT` when `vk_hdr` is active; or
2. the selected swapchain format when resolution scale is below one.

Each frame-slot scene target uses the scaled `scene_extent`; depth can remain
swapchain-sized because the scene framebuffer and viewport are bounded to the
smaller extent. This avoids a second full-size depth family while still
reducing rasterised depth/color work. Presentation framebuffers continue using
the full swapchain extent and native-resolution UI.

The reusable scene-copy/mip path remains mandatory for liquid refraction,
temporal auto exposure, bloom, DOF, and LUT. The already validated direct scene
sampling path remains the fast path when none of those consumers is active.

## Required implementation phases

1. Add renderer-neutral `r_resolutionscale*` cvar registration and a Vulkan
   controller matching the OpenGL bounds, hysteresis, and fixed-scale behavior.
   Feed it the Vulkan frame CPU timer initially; add a GPU-budget policy only
   after phase timestamps share a normalised scope.
2. Store explicit `scene_extent`, `scene_format`, and float-scene state in the
   Vulkan context. Allocate the existing frame-slot scene/copy resources at
   `scene_extent` whenever HDR or scaling needs an offscreen scene.
3. Route world, entity, alpha/liquid, debug, bloom-emission, and post-process
   source passes through `scene_extent`; retain presentation, CRT output,
   screen blends, screenshots, and UI at the swapchain extent.
4. Rebuild the offscreen scene resource family only after retiring submitted
   frames when scale changes. Initially this can share the existing swapchain
   recreation safety path; factor it into an offscreen-only recreation path
   before accepting an adaptive performance claim.
5. Add fixed-scale LDR/HDR paired captures covering direct presentation,
   refraction, bloom, DOF, CRT, and native-resolution UI. Add an adaptive
   controller smoke that verifies hysteresis and resource retirement without a
   visible client.
6. Establish a provenance-bound performance budget at the target scale. It
   must report both GPU scene and GPU post time, because scaling can lower
   raster cost while the full-size presentation cost remains constant.

## Initial implementation evidence

The first implementation now registers the shared `r_resolutionscale*`
controls in Vulkan and mirrors OpenGL's disabled, fixed, and adaptive
controller behavior. It records a smoothed Vulkan frame CPU sample for the
initial adaptive policy. `scene_extent`, `scene_format`, and float-scene state
make the existing frame-slot scene/copy images generic offscreen targets:
float HDR uses the float format, while fixed LDR scaling uses the swapchain
format. World, entities, transparent liquid, debug geometry, bloom emission,
bloom, and DOF source work receive `scene_extent`; final composition, CRT,
screen blends, UI, and screenshots remain at the native swapchain extent.

When a frame is already using native offscreen scene targets, subsequent
adaptive extent steps now retire only submitted frame slots and rebuild the
scene/copy, bloom-emission, and scene-relative post-process resources. The
presentation swapchain, UI resources, command buffers, synchronization, and
format-compatible bloom render passes/pipelines remain live. Entering
offscreen LDR rendering from direct presentation (and returning to direct
native scale) still uses the established full recreation, because the scene
render-pass final-layout contract changes. The detailed retirement design and
gate are in
`docs-dev/renderer/vulkan-offscreen-resolution-scale-retirement-2026-07-16.md`.
No adaptive performance budget is accepted until a comparable GPU-timestamp
policy and scale-transition measurement land.

Material parity now includes native Vulkan mip generation for wall and skin
images. Full chains are allocated and linearly generated on registration when
the format supports transfer blits, matching OpenGL's material minification
contract and removing the high-frequency scaled-HDR aliasing gap. See
`docs-dev/renderer/vulkan-material-mipmap-parity-2026-07-16.md`.

Validation evidence (Intel Iris Xe local, 960x720 output, fixed 0.5 scale):

- LDR fixed-scale capture exact-compares all 50,000 crop pixels to OpenGL,
  including the `[24, 40, 72]` backdrop, with no Vulkan validation errors:
  `.tmp/renderer-parity/resolution-scale-fixed-packed-engine/`.
- Float-HDR fixed-scale capture exact-compares all 50,000 crop pixels to
  OpenGL, including the `[18, 38, 96]` tone-mapped backdrop, with no Vulkan
  validation errors: `.tmp/renderer-parity/hdr-resolution-scale-fixed/`.
- The headless adaptive-controller smoke forces the first downscale step
  (`r_resolutionscale=2`, zero target draw time, one sample before lowering)
  and then captures the still-native 960x720 output. It exact-compares all
  50,000 crop pixels and the 50,000-pixel `[24, 40, 72]` backdrop probe to
  OpenGL, with no Vulkan validation errors:
  `.tmp/renderer-parity/resolution-scale-adaptive/`. This demonstrates that a
  controller-driven scene-resource rebuild is safe; it is not a benchmark for
  the expensive full-swapchain recreation used by this first implementation.

The remaining coverage is deliberate: liquid/refraction, bloom, DOF, CRT,
and native-resolution UI need scale-specific paired gates before this task is
complete.

## Fixed-scale performance evidence

Task: `FR-01-T15`

A paired 120-sample headless float-HDR capture at 960x720 with fixed 0.5
scale (10 warm-up samples discarded) recorded 110 valid Vulkan GPU samples on
the local Intel Iris Xe adapter. Vulkan measured `0.268 ms` GPU scene mean and
`0.494 ms` GPU post mean (`0.783 ms` total). The preceding full-resolution
local float-HDR baseline measured `1.078 ms` scene and `0.838 ms` post, so the
native scaled scene cuts the measured scene phase by approximately 75% and
also reduces the scene/presentation total locally.

OpenGL reported `0.315 ms` total GPU time in this half-scale run, whereas the
Vulkan and OpenGL timestamp scopes still differ. This is therefore evidence
that Vulkan scaling reduces its own native work, not a cross-renderer GPU
superiority budget. Provenance and analysis:
`.tmp/renderer-perf/hdr-static-half-resolution-local/capture.json` and
`.tmp/renderer-perf/hdr-static-half-resolution-local/analysis.json`.

## Non-goals and invariants

- Vulkan must not redirect any scaled frame to OpenGL.
- Scale must not resize HUD/menu/RmlUi geometry or screenshot output.
- Float HDR must preserve scene range until its final transfer pass.
- An unavailable sampled/transfer-capable offscreen format must disable only
  the Vulkan scaling path with an explicit diagnostic; it must not silently
  render at a different format or corrupt presentation layouts.
