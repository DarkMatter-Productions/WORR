# Vulkan Active-Page Moment Mip Generation

Date: 2026-07-18

Task ID: `FR-02-T14`

Status: completed active-page mip-generation slice; fixed page capacity,
budgeted allocation, resolution buckets, and alpha-tested caster coverage remain
open.

## Outcome

The native Vulkan moment-shadow path no longer transitions and generates a full
mip chain for every one of the fixed 64 shadow-array layers on every rendered
shadow frame. `VK_Shadow_Record` now gathers the unique valid pages that have a
live shadow render job, then its color-attachment barriers, transfer barriers,
linear `vkCmdBlitImage` regions, and shader-read barriers operate only on that
set.

The result remains entirely within `rend_vk`; no Vulkan shadow path redirects
to OpenGL.

## Descriptor-layout constraint and solution

The first implementation attempt left never-used layers in `UNDEFINED` while
tracking layouts per page. Vulkan validation correctly rejected receiver draws:
the `sampler2DArray` descriptor view identifies the entire 64-layer image, so
all subresources in that view must have the descriptor's declared
`SHADER_READ_ONLY_OPTIMAL` layout when the draw accesses the descriptor.

The final design preserves that requirement without restoring per-frame
worst-case mip work:

- On the first moment-shadow record after image allocation, the whole moment
  array is transitioned once from `UNDEFINED` to shader-read layout.
- At every subsequent record, only active pages transition mip 0 from
  shader-read to color attachment.
- Only those active pages move through transfer source/destination layouts and
  receive the mip blits.
- The active mip chain returns to shader-read layout before the receiver
  passes. Inactive pages remain shader-read and are never blitted.

This one-time initialization is required by descriptor validity; it does not
clear or sample inactive page contents. The shared shadow frontend continues
to select only its current valid pages for receiver sampling, as before.

## Performance impact

For a moment image with `M` mip levels and `A` active pages, recurring mip work
now uses `A` regions per mip level instead of the fixed 64. The recurring
linear blit count changes from `64 * (M - 1)` to `A * (M - 1)`; the matching
per-layer barrier coverage scales the same way.

The final staged validation workload used a 512-pixel, 10-mip moment image
with 25 live moment views. Its recurring blit regions therefore fall from 576
to 225, a 60.9% reduction. Earlier partial-frame activity with 13 live views
uses only 117 regions, a 79.7% reduction; the native cost now scales with the
actual render-job set. The one full-array layout transition is paid only after
allocation/reallocation.

## Implementation guardrail

`tools/renderer_parity/test_vulkan_shadow_active_mips_source.py` verifies that
the native backend:

- deduplicates valid render-job pages;
- initializes the descriptor-wide moment layout once;
- records active-page barriers and one-layer blit regions; and
- has no OpenGL renderer include or route.

## Verification

The focused source test and complete renderer-parity source suite passed:

```text
python -m unittest tools.renderer_parity.test_vulkan_shadow_active_mips_source
python -m unittest discover -s tools/renderer_parity -p "test_*.py"
# 328 tests passed
```

The native DLL built successfully, then the staged runtime and packaged assets
were refreshed:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The following hidden-window, input-disabled validation run completed with no
`VUID`, validation error, or error finding:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadowmapping-repro/active-shadow-capacity-validation-staged-full \
  --renderer vulkan --scene flashlight-owner --filter evsm --wait 90 \
  --vulkan-validation
```

Its `r_shadow_dump` reported native Vulkan moment materialization at 512px,
10 mips, 25 referenced/live moment pages, and a 32-layer native capacity
bucket. The launcher sets
`win_headless 1`, `in_enable 0`, `in_grab 0`, and `s_enable 0`; it did not
launch an interactive client or capture input.

## Remaining work

This mip-generation slice does not itself select allocation capacity or
resolution buckets. Later `FR-02-T14` slices add geometric capacity,
delayed shrinking, unchanged-page content selection, and alpha-tested caster
materials; resolution buckets remain open.
