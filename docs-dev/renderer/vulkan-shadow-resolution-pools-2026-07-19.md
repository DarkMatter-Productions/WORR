# Native Vulkan Mixed-Resolution Shadow Pools

Date: 2026-07-19

Task ID: `FR-02-T14`

## Outcome

The native Vulkan shadow backend now owns independent 64, 128, 256, 512, and
1024 texel resource pools. A mixed frame no longer forces every shadow page
into a single high-resolution depth/moment array because one sun cascade asks
for 1024 texels.

This supersedes the earlier homogeneous-resolution shrink policy. It remains
entirely in `rend_vk`; receiver sampling, shadow rendering, allocation,
resource replacement, and lifetime handling are all native Vulkan paths.

## Routing and resource ownership

The shared frontend continues to select global shadow-page IDs, generations,
storage family, and requested resolution. `vk_shadow.c` maps each live global
page generation to a stable compact local layer within the exact-resolution
pool. A recycled frontend page or a changed resolution gets a new mapping and
is reported dirty, so it is rerendered before use.

The selected pool uses the established transactional resource bundle. Its
inactive siblings are parked and swapped into the active bundle only while
they are allocated or recorded. This reuses the hardened replacement path
without making a partial pool observable if Vulkan allocation fails. Device
format capability selection remains global to the renderer instead of being
accidentally owned by the last active pool.

Receiver descriptors now carry five depth, compare-depth, and moment sampler
entries. The world and entity receiver shaders receive a `location` value per
shadow page containing the compact local layer and pool index, then use a
static pool switch for sampler selection. This keeps the descriptor ABI
portable without requiring descriptor-indexing support. Shadow recording
groups jobs by pool; moment mip generation and image transitions retain their
per-pool, dirty-layer scope.

## Lifetime and performance policy

Each pool tracks completed local demand independently. Growth is immediate;
the existing `vk_shadow_shrink_frames` interval delays a smaller geometric
capacity replacement. A pool with no completed demand is retired after the
same interval, freeing its images, views, samplers, pipelines, and memory.
Descriptor updates use a live sibling as a legal fallback while an empty pool
is waiting to be recreated. A returning page materializes its exact pool and
forces a rerender before sampling.

In the verified sun-cascade case, four 1024px cascades and one 64px page now
materialize as `64:1` plus `1024:4`. The prior global-index capacity policy
would reserve the 64-layer geometric high-resolution bucket. At base depth
image area this reduces the high-resolution allocation from 64 layers to four
(about 16x), before the additional moment-image and mip-chain savings.

## Verification

The Vulkan DLL rebuilt successfully and the focused structural suite passed:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python -m unittest tools.renderer_parity.test_vulkan_shadow_active_capacity_source \
  tools.renderer_parity.test_vulkan_shadow_capacity_shrink_source \
  tools.renderer_parity.test_vulkan_shadow_resolution_shrink_source \
  tools.renderer_parity.test_vulkan_shadow_resolution_pools_source \
  tools.renderer_parity.test_vulkan_shadow_transaction_source \
  tools.renderer_parity.test_shadowmapping_repro_runner
# 26 tests passed
```

Headless, input-disabled, audio-disabled Vulkan-validation smoke covered both
receiver filtering families:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadow-resolution-pools-retire \
  --renderer vulkan --scene sun-cascade --filter evsm \
  --shadow-size 64 --sun-shadow-size 1024 --cache-mode 0 \
  --vk-shadow-shrink-frames 8 \
  --inject-sun-resolution-drop-after-frames 4 --wait 180 \
  --vulkan-validation

python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadow-resolution-pools-pcf \
  --renderer vulkan --scene sun-cascade --filter pcf \
  --shadow-size 64 --sun-shadow-size 1024 --cache-mode 0 \
  --vk-shadow-shrink-frames 8 --wait 180 --vulkan-validation
```

The EVSM log records the injected drop and ends with
`resolution-pools=[64:4,128:0,256:0,512:0,1024:0*]` plus
`pool-shrinks=1 last-pool-shrink=4>0`. The PCF log retains the simultaneous
`resolution-pools=[64:1,128:0,256:0,512:0,1024:4*]` materialization. Neither
log contains a VUID, validation error, renderer error, or fatal finding.

The paired alpha-tested caster capture also remains exact under Vulkan
validation: its 560x420 crop contains 235,200 pixels with zero RGB error
between OpenGL and Vulkan.

## Remaining FR-02-T14 work

The per-resolution allocation gap is closed. The task remains open for the
separate bounded-page ABI/budget work and representative performance-budget
evidence; this change does not claim whole-renderer performance parity.
