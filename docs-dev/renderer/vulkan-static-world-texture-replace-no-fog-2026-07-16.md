# Vulkan static-world texture-replace no-fog specialization

Date: 2026-07-16

Task ID: FR-01-T15

## Outcome

The native Vulkan static-world texture-replace receiver now has a no-fog
fragment module and telemetry-proven runtime selection. During this work, a
new owned unlightmapped world fixture also exposed and corrected a pre-existing
visual defect in the fog-aware specialization: it used one-sided rasterization
where the legacy world path requires double-sided rasterization.

Both texture-replace variants now preserve the general opaque world's
double-sided raster state, while retaining a native fragment program that
does only the OpenGL-equivalent base sample, optional intensity, and applicable
fog work. No Vulkan work is redirected through OpenGL.

The existing archived default-on controls govern the specialization:

- vk_world_fast_lit enables the native static material specialization;
- vk_world_fast_lit_no_fog selects its no-fog companion only when global and
  height fog are absent.

Setting either relevant control to 0 preserves the complete native Vulkan
fallback. It is a regression and driver-workaround path, not a backend switch.

## Recovered material and raster contract

The static texture-replace fragment path applies the same material formula as
OpenGL GLS_TEXTURE_REPLACE:

1. sample the base material;
2. apply intensity only when the world vertex requests it; and
3. apply surface fog only when global or height fog is active.

The fog-aware module is 7,976 bytes. The new
VK_WORLD_TEXTURE_REPLACE_NO_FOG module is 3,756 bytes and compiles out the
fog routine. A per-frame VK_Shadow_HasActiveSurfaceFog query gates the choice;
sky-only fog does not affect opaque receiver fog and does not block the
no-fog module.

The initial native texture-replace pipeline inherited a one-sided culling
assumption from the lightmapped fast receiver. The new fixture's intentionally
unlightmapped world face exposed that assumption: the fog-aware and no-fog
pipelines both rendered a stale/incorrect background while the complete native
fallback was exact. The correction binds both texture-replace pipelines with
VK_CULL_MODE_NONE, matching the established general world pipeline. This is
why the fix improves visual parity as well as performance.

VK_STATS now records:

- world_texture_replace_draws, the selected native world material pipeline;
- world_texture_replace_no_fog_draws, its no-fog subset.

The performance analyzer reports mean, p50, and p95 for both fields.

## Owned coverage

tools/renderer_parity/generate_world_texture_replace_fixture.py produces:

- maps/worr_fr01_world_texture_replace.bsp, whose static world face has
  lightofs=-1; and
- maps/worr_fr01_world_texture_replace_fog.bsp, with the same geometry and
  authored global fog.

The common fixture generator gained an optional world_lightmap_rgb parameter.
Its default remains the prior authored-lightmap byte layout, so all existing
fixture generators retain their bytes. The new generator alone passes None to
exercise the OpenGL texture-replace material path. Both maps are packaged and
mirrored loose for no-zlib headless captures.

## Visual and functional validation

All captures use an isolated hidden 960x720 runtime, win_headless 1, disabled
input/audio, and Vulkan validation.

| Matrix | Result |
| --- | --- |
| Dry static texture-replace world | Exact OpenGL/Vulkan output over 235,200 crop pixels: zero RGB error and zero pixels over threshold. Telemetry reports one world texture-replace draw and one no-fog draw per frame. |
| Global-fog static texture-replace world | Exact OpenGL/Vulkan output over 235,200 crop pixels: zero RGB error and zero pixels over threshold. Telemetry reports one world texture-replace draw and zero no-fog draws per frame. |
| Forced complete native fallback | vk_world_fast_lit=0 remains exact over the dry 235,200-pixel crop, with zero texture-replace selections. |

The dry and fogged rows prove both sides of the runtime decision. The forced
fallback confirms the correction did not make the specialized path the only
way to obtain the correct result.

## Controlled timing

The paired current-binary capture held the fixture, configuration, adapter,
driver, Vulkan validation, and 100 post-warmup samples per backend constant.
Only vk_world_fast_lit_no_fog=0/1 changed.

| Vulkan metric | Fog-aware control | No-fog enabled | Change |
| --- | ---: | ---: | ---: |
| Opaque world GPU p50 | 0.353 ms | 0.316 ms | -10.5% |
| Opaque world GPU mean | 0.35444 ms | 0.31737 ms | -10.5% |
| Opaque world GPU p95 | 0.366 ms | 0.327 ms | -10.7% |
| Completed GPU-frame p50 | 0.406 ms | 0.369 ms | -9.1% |

Both rows report one world texture-replace draw, while the no-fog subset is
0 then 1. The adjacent inline-BSP texture-replace no-fog counter stays one in
both rows, isolating the world receiver. Draws stay at 3 and uploads at
192 bytes per frame. The test hardware was Intel Iris Xe Graphics, driver
31.0.101.5590 (2024-06-10), a 13th Gen Intel Core i7-13700H, and Windows 11
Home 10.0.26200.

This is a localized static-world improvement. OpenGL completed-frame p50 is
0.170/0.169 ms in the pair, so it does not establish a renderer-wide
Vulkan/OpenGL GPU budget or close FR-01-T15.

## Reproduction

1. Refresh and validate the generated fixtures:

   python tools/renderer_parity/generate_world_texture_replace_fixture.py --asset-root assets

   python tools/renderer_parity/generate_world_texture_replace_fixture.py --asset-root assets --validate

   python tools/renderer_parity/test_generate_world_texture_replace_fixture.py

2. Run source and package tests, regenerate SPIR-V, build, and stage:

   python -m unittest tools.renderer_parity.test_vulkan_world_fast_lit_source tools.renderer_parity.test_analyze_renderer_perf

   python tools/test_package_assets.py

   python tools/gen_vk_world_spv.py --validate

   ninja -C builddir-win worr_vulkan_x86_64.dll

   python tools/stage_install.py --build-dir builddir-win --install-dir .install

   python tools/package_assets.py --assets-dir assets --install-dir .install

3. Run assets/renderer_parity/fr01_world_texture_replace_manifest.json with
   run_capture_matrix.py and Vulkan validation. For controls, use
   fr01_perf_world_texture_replace.cfg with matching performance captures that
   set vk_world_fast_lit_no_fog=0 and =1, then analyze each capture pair with
   warmup 20 and min-samples 100.
