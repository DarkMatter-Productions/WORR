# Native Vulkan Float-Scene HDR Activation

Date: 2026-07-16

Task IDs: `FR-01-T12`, `FR-02-T13`

## Outcome

The native Vulkan renderer now keeps manual-HDR scene data in
`VK_FORMAT_R16G16B16A16_SFLOAT` until the final Vulkan presentation composite.
It does not redirect any HDR, bloom, or liquid path through OpenGL.

`vk_hdr` now includes `CVAR_RENDERER`. Toggling it recreates renderer-owned
resources: HDR-off retains the existing LDR allocation profile, while HDR-on
creates two device-local float images per frame slot. The first is the scene
colour attachment and transfer source; the second is its sampled copy.

## Native graph

```text
opaque world/entities/debug -> RGBA16F scene + depth
                                   | copy
transparent liquid/refraction <- RGBA16F sampled copy
                                   | copy completed scene
float bloom/DOF working targets <-+
                                   |
                         final Vulkan ACES composite -> UNORM swapchain
                                                           |
                                                     UI / menu overlay
```

The final composite explicitly moves each acquired swapchain image from its
tracked initial `UNDEFINED` layout or subsequent `PRESENT_SRC_KHR` layout to
`COLOR_ATTACHMENT_OPTIMAL`. This replaces the accidental transition formerly
provided by the old LDR scene-copy route. Swapchain framebuffers are created
for the presentation pass; float scene framebuffers are separate and owned by
frame slots.

Bloom emission and bloom/DOF working images use the float working format when
the float scene is active. This prevents HDR values being quantised to the
UNORM presentation format before post-processing.

## Validation evidence

All runs are headless with isolated runtime homes and
`VK_LAYER_KHRONOS_validation` enabled.

- `fr01_hdr_static_manifest.json`: enabled and disabled controls are exact
  over 50,000 pixels each.
- `fr01_hdr_underwater_waterwarp_manifest.json`: maximum RGB difference
  `1 / 1 / 1` over 307,200 pixels; no pixel exceeds the threshold of two.
- `fr01_hdr_bloom_emission_manifest.json`: exact 50,000-pixel authored
  glow-bloom capture, proving the float bloom path.
- `fr01_hdr_viewweapon_shell_bloom_refraction_manifest.json`: maximum RGB
  difference `2 / 2 / 1` over 128,000 pixels, with no pixel exceeding two.
  The bounded two-code HDR rounding difference is explicitly encoded in the
  manifest rather than hidden by an unbounded tolerance.

The structural guard
`tools/renderer_parity/test_vulkan_scene_presentation_pass_source.py` asserts
that the float copy, presentation transition, restart cvar, per-image layout
state, and float post-process working format stay native and explicit.

## Remaining FR-02-T13 work

Automatic exposure still requires a frame-local luminance reduction chain and
history. HDR-display colour spaces and transfer functions also remain pending.
DOF, CRT, full RmlUi/menu-preview, performance-budget, and broader gameplay
coverage must be expanded before claiming complete linear-light presentation
parity.
