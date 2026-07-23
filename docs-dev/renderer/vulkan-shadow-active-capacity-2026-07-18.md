# Vulkan Active Shadow-Array Capacity Buckets

Date: 2026-07-18

Task ID: `FR-02-T14`

Status: completed active-capacity slice; later 2026-07-19 slices close dynamic
shrinking, transactional allocation, alpha-tested casters, and native
per-resolution pools.

## Outcome

Vulkan no longer allocates a fixed 64-layer depth/moment shadow image whenever
the shadow backend is initialized. The shared frontend now determines the
largest compatible page index after it has constructed the complete frame view
set, and supplies that one-based requirement to the backend before any
individual page is ensured.

`rend_vk` rounds the requirement up through `1, 2, 4, 8, 16, 32, 64` and uses
the resulting native capacity for:

- the depth and moment image `arrayLayers`;
- descriptor image-view layer ranges;
- per-layer image views and framebuffers;
- whole-array depth barriers; and
- one-time descriptor-wide moment-layout initialization.

The uniform page table, job storage, and shader compatibility ceiling remain
64 pages. This keeps page identifiers, the shared frontend contract, legacy
server behavior, and receiver shader indexing intact while eliminating unused
native image layers below the active bucket.

## Allocation behavior

The capacity calculation happens before the backend loop calls `ensure_page`,
so the resource family is large enough for every view in the current frame.
This avoids a sequence of mid-frame reallocations that could discard an
already-recorded page. Existing resources are retained while their resolution,
storage family, and bucket satisfy the new requirement; a larger requirement
uses the next geometric bucket, bounded by `VK_SHADOW_MAX_PAGES`.

The first no-shadow depth descriptor uses one layer. A later moment/depth
allocation starts directly at the current frame's required bucket rather than
growing once per individual view.

## Observed reduction

The validation workload selected/referenced 25 pages. Its native Vulkan dump
now reports `capacity=32`, halving depth/moment image layers, image-view span,
and per-layer framebuffer/view objects versus the prior 64-layer allocation.
The same settled run has 25 live moment render jobs; paired with the
active-page mip work in `vulkan-shadow-active-moment-mips-2026-07-18.md`,
only those 25 pages receive recurring moment mip barriers and blits.

This is an allocation and recurring-work reduction, not a cross-renderer GPU
budget claim. Device memory alignment, shadow format selection, and the
current fixed maximum still vary by adapter/configuration.

## Guardrails

`tools/renderer_parity/test_vulkan_shadow_active_capacity_source.py` verifies
that the shared frontend passes a completed-view-set requirement, Vulkan uses
geometric bounded capacity, image/view/framebuffer ranges use that capacity,
and record-time page checks cannot address beyond it. The test also guards the
native-only renderer boundary.

## Verification

The complete renderer source suite passed:

```text
python -m unittest discover -s tools/renderer_parity -p "test_*.py"
# 333 tests passed
```

Both affected raster DLLs compiled successfully and the staged install was
refreshed and package-verified (533 archive entries):

```text
ninja -C builddir-win worr_vulkan_x86_64.dll worr_opengl_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The native Vulkan EVSM smoke used the non-interactive launcher settings
`win_headless 1`, `in_enable 0`, `in_grab 0`, and `s_enable 0`:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadowmapping-repro/active-shadow-capacity-validation-staged-full \
  --renderer vulkan --scene flashlight-owner --filter evsm --wait 90 \
  --vulkan-validation
```

Its frame-152 `r_shadow_dump` reports native `moment 2d-array`, `pages=25`,
`capacity=32`, `size=512`, `mips=10`, and `moment_views=25`; the captured log
has no `VUID`, validation error, or error finding. The matching headless
OpenGL EVSM smoke completed without an error and reported the same 25 shared
frontend pages.

## Remaining work

The 64-page ABI ceiling and fixed-size CPU uniform/job arrays intentionally
remain. Capacity now shrinks after a sustained low-demand interval; that
follow-up is documented in `vulkan-shadow-delayed-capacity-shrink-2026-07-19.md`.
Resolution selection is now a native five-pool resource system; see
`vulkan-shadow-resolution-pools-2026-07-19.md`. Transactional replacement,
unchanged-page dirty selection, and alpha-tested shadow casters are documented
in their respective later `FR-02-T14` slices.
