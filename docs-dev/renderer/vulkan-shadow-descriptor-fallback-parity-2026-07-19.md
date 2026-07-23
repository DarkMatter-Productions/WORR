# Vulkan Shadow Descriptor-Safe Idle Fallback

Date: 2026-07-19

Task ID: `FR-02-T14`

## Outcome

The native Vulkan shadow backend now preserves a minimal 64px, one-layer
shadow resource when all active resolution pools become idle. This keeps the
receiver descriptor arrays valid while the 128px through 1024px pools are
fully released, eliminating the stale-image-view lifetime hazard exposed by a
zero-demand pool transition.

This is a native `rend_vk` resource-lifetime fix. It does not redirect shadow
rendering or receiver sampling through OpenGL.

## Cause and resolution

World and entity receiver descriptors statically contain five depth,
compare-depth, and moment entries, one for each 64/128/256/512/1024px shadow
pool. Before this change, after the normal hysteresis delay, a zero-demand
base pool could be destroyed. If it was the final live pool, an existing
descriptor array could still contain its just-destroyed image view.

`VK_Shadow_BeginFrame` now sends an idle base pool through the existing
transactional `VK_Shadow_EnsureResources` path with a requested capacity of
one rather than destroying it. The helper waits for the device before a
replacement and refreshes the descriptor sets only after a complete native
resource bundle exists. Higher-resolution pools retain their prior reclaim to
zero behaviour; descriptor entries for those retired pools resolve to the
base-pool image instead of a null or destroyed view.

The fallback has no frontend mapping and never receives a shadow render job.
It is merely a legal descriptor target. At 64px and one layer its idle memory
cost is deliberately negligible relative to the 128px--1024px images, moment
images, mip chains, views, framebuffers, and pipelines that can still be
retired completely.

## Regression protection

`tools/renderer_parity/test_vulkan_shadow_resolution_shrink_source.py` now
requires the base-pool `1`-page fallback and verifies that the higher pools
continue through their zero-capacity reclaim path. The focused native shadow
resource suite passed:

```text
python -m unittest \
  tools.renderer_parity.test_vulkan_shadow_resolution_shrink_source \
  tools.renderer_parity.test_vulkan_shadow_capacity_shrink_source \
  tools.renderer_parity.test_vulkan_shadow_transaction_source
# 15 tests passed
```

The Vulkan and OpenGL runtime DLLs were rebuilt and staged under `.install/`.
A headless, input-disabled, audio-disabled strict capture using
`fr01_sky_fog_manifest.json` was then run with
`VK_LAYER_KHRONOS_validation`; its log contains no `VUID`, validation error,
renderer error, or fatal finding.

At the time of this capture, a separate static-sky difference was still open.
It was subsequently resolved by the native BSP sky-portal and fog-background
work recorded in `vulkan-bsp-sky-portal-parity-2026-07-19.md`: the strict
77,000-pixel recapture now has maximum RGB `1 / 1 / 1`, zero pixels above two,
exact 75,379-pixel fog-colour coverage at IoU `1.0`, and no Vulkan validation
error.

## Remaining FR-02-T14 work

The descriptor lifetime edge case is closed. The task remains open for the
bounded-page ABI/budget and representative performance-budget evidence.
