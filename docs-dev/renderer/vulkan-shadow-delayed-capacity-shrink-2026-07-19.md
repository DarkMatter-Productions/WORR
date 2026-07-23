# Delayed Native Vulkan Shadow-Array Capacity Shrinking

Date: 2026-07-19

Task ID: `FR-02-T14`

Status: superseded as the active implementation by the per-resolution policy
in `vulkan-shadow-resolution-pools-2026-07-19.md`; this document retains the
original homogeneous-array evidence.

## Outcome

The native Vulkan shadow backend now releases excess depth/moment array
capacity after a sustained reduction in the completed frontend view set.
Previously, the geometric capacity bucket could grow from one to 32 or 64
pages during a busy scene and remain at that high-water allocation for the
rest of the renderer lifetime, even when later frames required one page.

This is a native `rend_vk` allocation policy. The shared frontend continues to
own page selection and no Vulkan path is routed through OpenGL.

## Allocation policy

`vk_shadow_shrink_frames` is an archived Vulkan-only cvar with a conservative
default of 180 frames. At `VK_Shadow_BeginFrame`, the completed frontend view
set provides its one-based page requirement before any page is recorded.

If that requirement remains below a smaller power-of-two capacity bucket for
the configured interval, Vulkan recreates the shadow image family at the
smaller bucket before recording the frame. A temporary low-water frame resets
the timer whenever the demand returns to the current bucket or higher, so
alternating light sets do not repeatedly allocate and free device-local
images. Growth remains immediate.

The existing safe-recreation path performs its device-idle synchronization
before destroying old images, descriptors, views, and framebuffers. A
reallocation marks the current/next frame for a complete page refresh, so
resident frontend cache entries cannot sample shadow content from the retired
array.

`r_shadow_dump` now reports `shrinks` and `last-shrink=old>new` alongside the
realized capacity. This makes the runtime allocation transition observable
without a visible client window.

## Test support and guardrails

The headless smoke launcher accepts `--vk-shadow-shrink-frames`. Its optional
`--pre-dump-wait` adds a retained pre-settle dump for other capacity probes;
normal smoke behavior is unchanged.

`tools/renderer_parity/test_vulkan_shadow_capacity_shrink_source.py` guards
the delayed smaller-bucket calculation, begin-frame recreation ordering,
state reset, retained transition telemetry, and native-only renderer boundary.
The smoke-runner contract test covers the test cvar and dump controls.

## Verification

The affected Vulkan DLL built, and the staged runtime/package were refreshed:

```text
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The 535-file asset package was rebuilt. The complete renderer source suite
passed:

```text
python -m unittest discover -s tools/renderer_parity -p "test_*.py"
# 348 tests passed
```

This hidden, input-disabled, audio-disabled Vulkan EVSM smoke forces a short
test interval while preserving the production default:

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadow-capacity-shrink-final \
  --renderer vulkan --scene flashlight-owner --filter evsm \
  --vk-shadow-shrink-frames 30 --wait 240 --vulkan-validation
```

The final `r_shadow_dump` reports a native 512px/10-mip moment array with
`capacity=1 shrinks=1 last-shrink=32>1`. Its process log contains no `VUID`,
validation error, renderer error, or fatal finding.

The paired alpha-tested caster fixture remained exact after the reallocation
policy changed:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_alpha_shadow_manifest.json \
  --run-root .tmp/renderer-parity/fr01-alpha-shadow-capacity-shrink \
  --timeout 180 --vulkan-validation
```

All 235,200 pixels in the 560x420 crop have zero RGB error between OpenGL and
Vulkan, with no Vulkan validation findings.

## Remaining FR-02-T14 work

Dynamic shrinking now runs independently for each native resolution pool and
also retires fully idle pools. See
`vulkan-shadow-resolution-pools-2026-07-19.md`; transactional replacement
continues to be documented in
`vulkan-shadow-transactional-allocation-2026-07-19.md`.
