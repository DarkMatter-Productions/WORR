# Exact Shared Shadow-Page Content Reuse

Date: 2026-07-19

Task ID: `FR-02-T14`

## Outcome

The shared shadow frontend no longer treats the broad `dynamic` and
`animated` caster categories, or a dynamic-effect light source, as automatic
per-frame redraw requests. A resident shadow page is now reused whenever its
actual raster inputs are unchanged, while every raster-affecting change still
invalidates the page before either OpenGL or native Vulkan renders it.

This is shared renderer work: both backends consume the same selected view
set and page-reuse decision. Vulkan remains fully native; no renderer path is
redirected through OpenGL.

## Content contract

`shadow_view_desc_t` and `shadow_resident_view_t` carry a `content_hash`.
The frontend computes it from the complete CPU projection contract using the
exact float bit patterns for view origin, all three axis vectors, FOV,
orthographic size, near plane, and far plane. The value is copied to the
resident entry only after the backend reports a successful page render.

Dynamic-effect lights deliberately retain their stable residency key, so a
moving effect does not churn array slots. The new content hash detects the
actual view motion instead, including sub-texel changes, and applies the
normal `light-params` invalidation to the existing page.

The selected-caster signature is also exact rather than quantized. It covers
the model/flags/owner state, animation frames and blend factor, skin number,
transform, scale, and computed bounds. This is required now that dynamic and
animated labels do not themselves force a redraw: a real pose or transform
change must always invalidate the prior shadow content.

## Steady-state probe

`tools/shadowmapping_repro_smoke.py` now accepts `--cache-mode` (`0` none,
`1` static reuse, `2` world only). This exposes the existing renderer control
to the headless smoke harness; it does not change its default behavior.

The world-only mode makes a clean steady-state page-reuse probe by excluding
moving/animated entity geometry while retaining the selected light views. It
uses the existing hidden surface, disabled input, disabled audio, and isolated
runtime directory.

`tools/renderer_parity/test_shadow_frontend_content_reuse_source.py` guards
the exact projection and caster signatures, comparison, post-render resident
update, and removal of category-only dirtying. The smoke-runner test guards
the new cache-mode control and its headless contract.

## Verification

Both renderer DLLs compiled and the install/package stage refreshed the
runtime (535 packaged assets):

```text
ninja -C builddir-win worr_vulkan_x86_64.dll worr_opengl_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
```

The complete renderer source suite passed:

```text
python -m unittest discover -s tools/renderer_parity -p "test_*.py"
# 341 tests passed
```

The following non-interactive EVSM probe completed for both backends. Vulkan
ran with `VK_LAYER_KHRONOS_validation`.

```text
python tools/shadowmapping_repro_smoke.py --install-dir .install \
  --run-root .tmp/shadow-content-reuse-world \
  --renderer opengl --renderer vulkan --scene off-pvs-light --filter evsm \
  --cache-mode 2 --wait 240 --vulkan-validation
```

At the settled final dump, each backend selected six views and reported
`dirtied=1 reused=5 rendered=1`; the one render was an expected eviction.
The Vulkan process log contains no `VUID`, validation error, or renderer
error.

Finally, the paired alpha-tested shadow fixture remained exact after the
shared decision changed:

```text
python tools/renderer_parity/run_capture_matrix.py --install-dir .install \
  --manifest assets/renderer_parity/fr01_alpha_shadow_manifest.json \
  --run-root .tmp/renderer-parity/fr01-alpha-shadow-content-reuse \
  --timeout 180 --vulkan-validation
```

All 235,200 pixels in the 560x420 crop have zero RGB error between OpenGL and
Vulkan, with no Vulkan validation findings.

## Remaining FR-02-T14 work

This closes the unchanged-page dirty-selection slice. Dynamic shrinking,
resolution buckets, transactional allocation beyond safe recreation, and
capability-correct samplers remain separate work.
