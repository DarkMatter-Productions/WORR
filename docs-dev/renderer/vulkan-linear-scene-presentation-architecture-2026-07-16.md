# Native Vulkan Linear Scene and Presentation Architecture

Date: 2026-07-16

Task ID: `FR-02-T13`

Status: native float-scene activation complete for the manual HDR path.
Automatic exposure and output-colour-space presentation remain separate
follow-up work.

The explicit scene/presentation render-pass split is implemented. With
`vk_hdr 1`, native Vulkan now renders 3D work into a per-frame float target,
copies it for sampling, and writes the final tone-mapped result only to the
UNORM presentation image. See
`vulkan-float-scene-hdr-activation-2026-07-16.md` for implementation and
validation evidence.

The native context now also queries `R16G16B16A16_SFLOAT` for colour-attachment,
sampled-image, transfer-source, and transfer-destination support at swapchain
creation and records the result as the linear-scene capability. This is the
required format gate for the frame-slot float resources; unsupported hardware
will retain the native LDR graph rather than route through OpenGL.

`vk_hdr` is a renderer-restart cvar. Enabling it allocates only the required
two device-local `R16G16B16A16_SFLOAT` images per frame slot: a
colour-attachment/transfer-source scene image and a sampled copy. Disabled HDR
therefore retains the prior LDR allocation cost. Bloom and DOF working images,
including authored bloom emission, follow the float format while this path is
active so no post-processing stage quantises HDR data before the final shader.

## Previous constraint

The primary Vulkan frame pass renders directly to a swapchain image. Its
per-frame `liquid_scene_image` is an image-copy of that output and has the
same format. Consequently, even though the final native shader now implements
the static OpenGL ACES controls, values above the display range have already
been quantised before the shader can receive them.

Changing only `liquid_scene_image` to `R16G16B16A16_SFLOAT` would be invalid:
the primary pass would still render to the swapchain, and transparent water
would be sampling and writing the same image. Changing the existing common
render pass to float would also make its world/entity/debug pipelines
incompatible with the current UI and menu-preview output passes. This is a
render-graph split, not an image-format toggle.

## Required native graph

```text
world, entities, sky, debug
            |
            v
  per-frame RGBA16F scene target + depth
            | copy after opaque scene
            v
  per-frame RGBA16F sampled scene copy --> transparent liquid/refraction
            | copy completed scene
            +--> bloom / depth-aware DOF / luminance reduction
            |
            v
  Vulkan final tone-map/composite --> SDR or HDR swapchain
            |
            +--> sharp UI and no-world menu preview output pass
```

All arrows are native Vulkan image barriers/copies or render passes. No stage
may route through OpenGL.

## Ownership and compatibility split

`vk_context_t` needs two explicitly named render-pass families rather than
overloading the current `render_pass`:

- **scene passes** use `VK_FORMAT_R16G16B16A16_SFLOAT` plus the frame slot's
  depth image. World, normal entity, sky, shadow receiver, and debug pipelines
  are created against this family.
- **presentation passes** use the chosen swapchain format. The final Vulkan
  post-process, CRT, UI, raw video, and no-world menu-preview entity paths are
  created against this family.

The no-world preview is important: it currently overlays RmlUi after the 3D
scene. It cannot silently reuse a float-scene entity pipeline on the
swapchain. The presentation family therefore requires its own compatible
entity variant (or a presentation-only dynamic-rendering replacement) before
the split is enabled.

Each frame slot owns its float scene target, sampled-copy target, and both
framebuffers. These allocations are keyed by frame slot, not acquired
swapchain image, preserving the current two-frame overlap and eliminating
the false assumption that a swapchain image is a usable HDR render target.

## Implementation sequence

1. **Completed:** explicit scene/presentation render-pass families and
   frame-slot float colour/copy targets.
2. **Completed:** opaque 3D submission, native transparent-liquid sampling,
   post-process working targets, and final presentation composition remain in
   Vulkan. The presentation image tracks its first-use `UNDEFINED` layout and
   later `PRESENT_SRC_KHR` layout independently per swapchain image.
3. **Completed:** final composition is unconditional for an active float
   scene target; HDR disabled retains the old LDR graph.
4. Add a frame-local luminance reduction chain over the completed float scene
   and an exposure-history value. Prefer mip/blit reduction when the selected
   float format supports it; otherwise use a small native reduction pass. The
   history update must never wait for GPU completion and must retain OpenGL's
   min/max/speed semantics.
5. Add output-colour-space selection and explicit SDR/HDR presentation
   transfer functions. Screenshot/capture conversion must continue to read a
   canonical SDR result for the existing paired manifests.

## Acceptance and performance gates

- The active HDR graph has exact static tone-map and authored glow-bloom
  captures, an HDR waterwarp capture with maximum RGB error `1 / 1 / 1`, and
  a refraction/view-weapon ordering capture bounded to `2 / 2 / 1` RGB code
  values. Every recorded Vulkan validation log is clean.
- HDR enabled/disabled, water refraction, bloom, depth-aware DOF, CRT, normal
  HUD, and RmlUi/no-world-preview scenes each require paired headless capture
  and a clean Vulkan validation run.
- Auto exposure requires bright-to-dark and dark-to-bright deterministic
  sequences, including min/max clamps and speed interpolation checks.
- The performance baseline must report scene, post, copy/reduction, CPU, draw,
  and allocation counts for both the current LDR graph and the float graph.
  The design goal is one scene render, at most two scene copies only when
  refraction/post-processing needs them, and no per-frame allocation or
  queue-idle wait.

The initial rollout must not claim global HDR parity until every acceptance
gate above passes. Until then, `vk_hdr_auto_exposure` remains intentionally
absent.
