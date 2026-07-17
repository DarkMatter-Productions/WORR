# Vulkan Show-Tris Diagnostic Parity

Date: 2026-07-16

Task ID: `FR-01-T15`

Status: native Vulkan implementation complete for the OpenGL show-tris bit
contract; paired world/inline-model and HUD/UI diagnostic gates pass. Broader
effect and model-family scene coverage remains open.

## Outcome

`rend_vk` now owns cheat-protected `vk_showtris`, with the same four bits as
OpenGL: world (`1`), mesh (`2`), UI pictures (`4`), and effects (`8`). It is
a native Vulkan path: it neither includes nor invokes `rend_gl`, and it does
not require the optional Vulkan `fillModeNonSolid` feature.

3D producers expand each triangle to a portable white line list. Static world
positions are retained at map registration, while CPU-expanded entities,
indexed MD5 fallback batches, inline BSP models, particles, beams, and flares
reuse their already-generated native geometry. The normal optimized GPU MD2,
MD5, and inline-BSP routes remain untouched while the diagnostic is disabled;
the affected model/bmodel routes intentionally select their existing CPU
fallback only while their show-tris bit is active so the renderer has exact
triangle positions to stroke.

The 3D queue grows geometrically and uses a separate per-frame host-visible
Vulkan vertex buffer, so it is not constrained by the ordinary shared debug
line capacity. It preserves OpenGL's `GL_DrawOutlines` depth behavior:
depth-tested source triangles draw through the native depth pipeline with a
near-plane depth range, while flare triangles retain a separate no-depth
stream. UI uses its own native, white line-list pipeline and replays each
draw's original scissor rectangle, matching OpenGL's depth-disabled 2D path.

## Controlled evidence

`assets/renderer_parity/fr01_showtris_debug.cfg` captures a 960x720 baseline
and all-bit overlay on the deterministic first-frame inline-BSP fixture. The
fixture explicitly clears both renderer cvars before map setup and waits after
each screenshot request; screenshots are scheduled for a later rendered frame,
so omitting that wait would capture the overlay in the alleged baseline.

| Gate | Vulkan | OpenGL | Result |
|---|---:|---:|---|
| World and inline-BSP (`bits 1 + 2`) changed pixels | 3,782 | 3,689 | Same changed bounds `(133, 105)-(827, 626)`; white outlines visually cover the background triangles and inline box. |
| HUD/UI picture (`bit 4`) changed pixels | 1,869 | 1,710 | Same changed bounds `(17, 343)-(640, 706)`; crosshair and status-bar triangle edges remain scissor-clipped. |

Both gates have a maximum per-channel delta of 255 because the diagnostic
draws white edges. Vulkan's world/inline capture ran with
`VK_LAYER_KHRONOS_validation` and had no VUID, validation-error, device-loss,
or fatal-error output. Its `vk_stats` sample reports one native debug draw,
24 diagnostic vertices, and 384 debug-upload bytes. The UI capture is also
validation-clean and reports 28 native UI draws plus the white UI edge replay.

Retained runtime artifacts are under `.tmp/renderer-parity/`:

- `fr01-showtris-vulkan-paired/` and `fr01-showtris-opengl-paired/`
- `fr01-showtris-ui-vulkan/` and `fr01-showtris-ui-opengl/`

## Verification

```powershell
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
python -m unittest tools.renderer_parity.test_vulkan_showtris_source
```

Run each fixture with `win_headless 1`, an isolated `homedir`, disabled input,
and the intended `vid_renderer`/`r_renderer` pair. Enable
`VK_LAYER_KHRONOS_validation` for the Vulkan captures. Compare the named
baseline and overlay screenshots for a non-empty white-edge delta and inspect
the log for validation diagnostics.

## Remaining boundary

The two paired fixtures prove the world/inline-model and UI picture routes,
not every material or effect family. Particle, beam, flare, sprite, transparent
mesh, depth-hack weapon, and complex MD5 routes need dedicated paired visual
scenes before show-tris can be called exhaustively validated. The implementation
does preserve their native producer ownership and keeps flares in the required
no-depth stream. This work also does not resolve the separate broken headless
OpenGL `gl_showorigins` baseline or the broader performance-budget work in
`FR-01-T15`.
