# Transactional Native Vulkan Shadow Resource Replacement

Date: 2026-07-19

Task ID: `FR-02-T14`

## Outcome

Native Vulkan shadow resize, storage-family, and capacity transitions now keep
the previous valid depth/moment resource family alive until the replacement has
been fully created. Previously, `VK_Shadow_EnsureResources` waited for the
device and destroyed the active images, views, samplers, pipelines, and
framebuffers before attempting the new allocation. A later Vulkan allocation
failure could therefore leave the renderer with no valid shadow resources.

The new transaction parks all resource-owned handles in a local bundle,
constructs the replacement through the existing native creation path, and only
then retires the parked bundle. Any failure follows the centralized cleanup,
which swaps the saved family back and refreshes the descriptor sets. The
frontend sees a failed ensure for that frame, but the prior native shadow array
remains valid for subsequent receiver work and a later retry.

No Vulkan shadow path uses OpenGL.

## Transaction boundary

`vk_shadow_resources_t` owns the resource-family state: images/memory, array
and layer views, depth/compare/moment samplers, render pass, pipeline layout,
opaque/alpha pipelines, material descriptor layout, framebuffers, layouts,
formats, storage family, resolution, mips, and capacity. It deliberately does
not move frontend selection, per-frame uniforms, or CPU caster data.

On a replacement request, the backend:

1. waits for the device as the existing safe-recreation policy requires;
2. swaps the active resource family into a rollback bundle;
3. builds the replacement in the normal active slots;
4. restores the rollback bundle if any creation stage fails; or
5. destroys the parked old bundle only after the replacement is complete.

Successful replacement retains the existing full-page refresh signaling. A
rollback rebinds the old image descriptors, so descriptor state cannot point at
destroyed image views.

## Deterministic failure coverage

`vk_shadow_test_fail_recreate` is a non-archived test-only one-shot cvar. It
can fail a post-initialization replacement after the live bundle has been
parked; the hook immediately resets itself to zero and invokes the normal
cleanup/rollback. It cannot fail initial shadow-resource creation.

`tools/shadowmapping_repro_smoke.py --inject-shadow-recreate-failure` enables
that hook only for a Vulkan test process. The transactional source test guards
the resource bundle, save/restore swap, descriptor refresh, and one-shot hook.
The runner contract test guards the hidden-process control.

## Verification

The Vulkan DLL built and the staged runtime/package were refreshed:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The complete renderer source suite passed:

```text
python -m unittest discover -s tools/renderer_parity -p "test_*.py"
# 352 tests passed
```

This non-interactive Vulkan EVSM test injects one failed moment-array
replacement while the initial depth array is valid:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadow-transactional-recreate-oneshot \
  --renderer vulkan --scene flashlight-owner --filter evsm \
  --inject-shadow-recreate-failure --wait 120 --vulkan-validation
```

Its process log contains one `test-injected replacement failure; restoring
prior resources` message, no Vulkan validation/error/fatal finding, and a
later successful native moment-array dump at 512px/10 mips with 25 pages in a
32-page capacity bucket.

The paired alpha-tested caster fixture remained exact after the transaction
change:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_alpha_shadow_manifest.json \
  --run-root .tmp/renderer-parity/fr01-alpha-shadow-transactional-recreate \
  --timeout 180 --vulkan-validation
```

All 235,200 pixels in the 560x420 crop have zero RGB error between OpenGL and
Vulkan, with no Vulkan validation findings.

## Remaining FR-02-T14 work

This closes transactional allocation. The same replacement bundle now backs
the completed native resolution-pool slice; see
`vulkan-shadow-resolution-pools-2026-07-19.md`.
