# Native Vulkan DOF Control and Depth-Path Parity

Date: 2026-07-15

Task ID: `FR-01-T12`

Status: native raster baseline with strict paired no-window fixed-focus,
centre-depth/automatic-range, wide explicit-range, and real menu-rectangle
state visual gates. Isolated rectangle-clipping semantics remain open.

## Shared controls

The Vulkan post-process owner now registers and consumes the same canonical
gameplay controls as OpenGL:

- `r_dof` (`1`, archive/latch) gates gameplay DOF;
- `r_dof_focus_distance` (`16.0`, server-info) selects the fixed focus plane;
- `r_dof_blur_range` (`0.0`, server-info) selects the falloff range, with the
  same automatic `max(64, focus * 0.25)` fallback when zero.

`refdef_t.dof_strength` is applied only for world rendering. `RDF_NOWORLDMODEL`
previews cannot retain or invoke gameplay DOF from a prior frame.

## Native Vulkan path

The previous whole-frame transfer-image focus blur was removed. The new path
never invokes OpenGL:

1. Each frame context creates a depth-only sampled image view when its selected
   depth format supports sampled linear filtering. The regular framebuffer view
   still retains depth/stencil use for scene and outline passes.
2. After opaque and optional native liquid rendering, Vulkan copies the full
   3D scene to its frame-local sampled scene image, transitions depth from
   attachment write to shader read, and restores it before the existing final
   presentation pass.
3. Frame-local off-screen resources downsample that scene to quarter
   resolution, then run four alternating Gaussian passes (two horizontal and
   two vertical). Their sigma uses the same shared OpenGL bloom-sigma,
   base-height, and bloom-downscale equation. This mirrors OpenGL's four blur
   iterations without a CPU readback or an OpenGL fallback.
4. A dedicated `vk_dof.frag` pass samples scene, blurred scene, and depth. It
   reconstructs view distance from the same projection coefficients used by
   Vulkan world/entity drawing and dynamically samples centre depth when focus
   is zero. On a menu rectangle frame it keeps a complete current-frame native
   composite, matching OpenGL's retained `FBO_POST` presentation without
   relying on stale Vulkan image contents.
5. The depth-composited scene proceeds through Vulkan's existing native
   waterwarp, colour/LUT, bloom, CRT, and sharp UI overlay stages.

The off-screen images and external descriptors are per frame slot. Any
resize/resource rebuild first retires submitted slots, preserving the bounded
frames-in-flight lifetime rule.

## Capability fallback

Vulkan continues to run normally when its selected depth format cannot be
sampled with the sampler required by this path. It reports `depth_dof=0` in
`vk_stats` capability output and leaves gameplay DOF disabled rather than
substituting an OpenGL or non-depth-aware path.

## Validation

Headless structural coverage verifies the controls, sampled-depth transition,
quarter-resolution copy plus four alternating Gaussian passes, shared sigma
scaling, focus reconstruction, `dof_rect`, and command ordering. It does not
launch a client window:

```text
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools/renderer_parity/test_vulkan_dof_control_source.py
```

The native DLL build validates shader embedding and C-side interface linkage.
`vulkan-dof-visual-parity-2026-07-16.md` records strict paired fixed-focus,
centre-depth/automatic-range, wide explicit-range, and compact-menu rectangle
state receivers. The remaining acceptance work is compliant paired
Vulkan/OpenGL coverage for isolated rectangle clipping and broader dynamic
scene geometry. HDR/tone mapping and resolution scaling remain separate
`FR-01-T12` gaps.
