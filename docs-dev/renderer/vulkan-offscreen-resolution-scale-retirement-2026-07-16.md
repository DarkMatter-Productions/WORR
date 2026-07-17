# Vulkan Offscreen Resolution-Scale Retirement

Tasks: `FR-01-T12`, `FR-01-T15`

## Problem

The initial native Vulkan resolution-scale implementation safely reused full
swapchain recreation whenever an adaptive controller step changed the scene
extent. That retired work correctly, but it also replaced presentation images,
frame command buffers, synchronization objects, native UI swapchain resources,
and post-process pipelines even though their output extent and formats had not
changed.

## Native extent-only refresh

When the renderer is already using a frame-slot offscreen scene target, it now
uses `VK_RecreateSceneTargets` instead of full swapchain recreation for later
extent changes. The refresh:

1. waits for every submitted frame-slot fence, without a device-wide idle;
2. destroys and recreates only linear scene/copy images, their scaled scene
   framebuffers, and bloom-emission images/framebuffers;
3. recalculates float-HDR auto-exposure mip count for the new scene extent;
4. invalidates post-process descriptors and scene-relative bloom/DOF working
   targets, then updates the persistent direct-scene descriptor; and
5. preserves presentation images, output framebuffers, command buffers,
   semaphores, fences, native UI resources, and post-process pipelines.

The bloom extraction render passes and their native pipelines are retained:
render-pass compatibility is determined by their unchanged attachment formats
and subpass contract, not framebuffer extent. Only bloom-emission images and
extent-specific framebuffers change.

Entering offscreen rendering from direct LDR presentation, and returning to
direct presentation at native scale, still use the established full swapchain
recreation path. Those transitions change the scene render-pass final-layout
contract. Float-HDR starts offscreen, so its adaptive changes use the
extent-only route from the first frame.

## Validation

`assets/renderer_parity/fr01_resolution_scale_offscreen_retirement_manifest.json`
launches at fixed half scale so the renderer creates offscreen scene targets,
then its config switches to a stable adaptive target before the first map
frame. This deterministically exercises the 0.5-to-1.0 offscreen refresh while
keeping the native 960x720 presentation size.

The headless paired run exact-compares all 50,000 crop pixels and the complete
50,000-pixel `[24, 40, 72]` backdrop probe to OpenGL, with no Vulkan validation
errors:
`.tmp/renderer-parity/resolution-scale-offscreen-retirement/`.

This confirms safe native resource retirement and output parity for the
covered static scene. It is not a performance-budget result: adaptive policy
still uses a CPU timing sample, and the known high-frequency scaled-scene
visual discrepancy (including liquid/post-process receiver coverage) remains
open.

An authored-bloom scale-change attempt made after a map wait did not complete
within the headless 120-second limit in either OpenGL or Vulkan. It is not a
Vulkan-specific result or a regression gate; active bloom scale-transition
runtime coverage remains open until that shared harness/runtime behavior is
separated from renderer work.
