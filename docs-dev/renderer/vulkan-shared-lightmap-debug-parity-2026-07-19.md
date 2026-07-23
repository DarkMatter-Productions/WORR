# Vulkan Shared Lightmap-Debug Parity (2026-07-19)

Task: `FR-01-T06`

## Outcome

The shared cheat cvar `r_lightmap` now has a native Vulkan world-renderer
implementation. It matches the OpenGL diagnostic view for lightmapped BSP
world surfaces: the authored, style-combined lightmap is rendered over a white
base material, while glow-map emission and scene intensity are excluded.

No Vulkan path calls, loads, or redirects to OpenGL.

## Why this was necessary

The existing first-frame map contains an `81 x 61` legacy v38 lightmap whose
authored bytes are exactly RGB `32`. The normal material view also includes
the wall texture and the shared receiver controls, so it is not an isolated
assertion of the lightmap data itself. A fresh paired capture showed that the
old `legacy_lightmapped_world` probe did not observe its intended dark range:
both backends were pixel-identical, but neither produced the probe mask.

OpenGL already exposes `r_lightmap` as the correct engine-owned diagnostic
mode. Its world draw substitutes a white base texture, keeps the lightmap
sampler, removes glow-map input, and suppresses intensity. Vulkan did not
implement that cvar, so enabling it exposed a functional renderer gap rather
than a valid parity result.

## Native Vulkan design

`src/rend_vk/vk_world.c` registers the same shared `r_lightmap` cvar with
`CVAR_CHEAT`; it is not a `vk_` substitute. Vulkan creates two optional native
pipeline variants from `vk_world_shadow.frag`:

- opaque world batches; and
- alpha world batches.

The `VK_WORLD_LIGHTMAP_DEBUG` fragment specialization replaces the sampled
base material with white, leaves the native lightmap, dynamic-light, and
brightness/modulate calculation in place, and excludes glow-map lifting plus
`r_intensity`. Opaque selection disables static-light and texture-replace fast
paths while the diagnostic cvar is active; alpha batches select the matching
native debug pipeline individually. This preserves normal optimized pipelines
when the cvar is off.

`VK_World_HasBloomEmission` also returns false for the diagnostic mode so that
the separate native bloom extraction pass cannot reintroduce OpenGL-suppressed
glow emission.

The embedded shader is generated as
`vk_world_lightmap_debug_frag_spv` by
`tools/gen_vk_world_spv.py --validate`; no runtime GLSL or renderer fallback
is involved.

## Strengthened FR-01-T06 fixture

`assets/renderer_parity/fr01_bmodel_first_frame_lightmapped.cfg` now sets:

```cfg
set r_fullbright 0
set gl_modulate 1
set r_lightmap 1
```

The one-times modulate setting keeps the output equal to the source lightmap
bytes. The manifest requires exactly RGB `32 / 32 / 32` for at least 30,000
pixels per backend, rather than accepting a broad dark range. The separate
global-fullbright scene remains an exact RGB `24 / 40 / 72` check, proving the
normal textured fullbright route remains distinct.

## Validation

All validation launches were headless (`win_headless 1`, input disabled, and
an isolated runtime directory).

```text
python tools/gen_vk_world_spv.py --validate
python tools/renderer_parity/generate_bmodel_first_frame_fixture.py --asset-root assets --validate --json
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/stage_install.py --build-dir builddir-win --install-dir .install
python tools/package_assets.py --assets-dir assets --install-dir .install
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-lightmap-debug-final --timeout 180 --vulkan-validation --json-output .tmp/renderer-parity/fr01-bmodel-lightmap-debug-final/results.json
```

The final six-process matrix passed with no process failure or Vulkan
validation failure:

```text
scene                                      pixels     maximum RGB error
bmodel_transformed_first_frame             170000     0 / 0 / 0
legacy_lightmapped_world                    34000     0 / 0 / 0
global_fullbright_lightmapped_world         34000     0 / 0 / 0

legacy lightmap mask, OpenGL / Vulkan:      34000 / 34000
legacy lightmap mask IoU:                   1.0
legacy lightmap RGB:                        32 / 32 / 32 exactly
```

Focused structural coverage in
`tools/renderer_parity/test_vulkan_world_fast_lit_source.py` checks the shared
cvar, both native pipeline variants, shader specialization, embedded SPIR-V
registration, bloom suppression, and exact fixture contract.

## Boundary

This closes the shared `r_lightmap` diagnostic world-path gap. It does not
replace the broader ordinary-lighting, brightness, saturation, intensity, or
fullbright gates; those retain their independent native Vulkan validation.
