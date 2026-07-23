# Vulkan Liquid/Entity Ordering Parity

Date: 2026-07-19

Task ID: `FR-01-T10`

Status: the native Vulkan alpha-phase schedule now matches OpenGL both with
and without transparent-liquid refraction. A deterministic map-entity fixture
proves a live refraction path and a strict no-refraction ordering control.
Broader transparent-material coverage remains open.

## Outcome

The former no-refraction Vulkan route submitted all transparent world faces
before all entity phases. That differed from OpenGL when a translucent entity
was classified as alpha-back by the renderer draw-order threshold: OpenGL
draws that entity before transparent world faces, whereas Vulkan drew it after
them.

`src/rend_vk/vk_main.c` now uses the same native phase boundary for every
world frame:

```text
opaque world
  -> opaque entities + alpha-back entities
  -> transparent world faces
  -> beams/particles/flares + alpha-front entities
```

When native liquid refraction is active, the existing device-local scene copy
is inserted after alpha-back work and before transparent world faces. When it
is disabled, no copy or extra pass is created; the same draws remain in the
same order within the ordinary scene render pass. This is a Vulkan-only
submission correction and introduces no OpenGL renderer dependency or fallback.

`VK_World_HasTransparentBatches` keeps this split conditional. Ordinary maps
with no visible transparent world batch retain the original single native
entity-recording call, avoiding duplicate batch scans in the common opaque
case.

## Deterministic fixture

`tools/renderer_parity/generate_transparent_liquid_entity_fixture.py` owns
`assets/maps/worr_fr01_liquid_entity_ordering.bsp`
(`SHA-256 3ec7430c45ba6670ca536426ae269597b0ac4cfb97a50ec4d68ab72fd7f0868b`).
It derives the existing flowing `SURF_WARP | SURF_TRANS33` receiver and adds
two ordinary `misc_model` BFG sprites through the game entity path:

- the sprite behind the liquid is authored with alpha `0.75`;
- the closer sprite is authored with alpha `0.25`;
- both configs set `gl_draworder` and `vk_draworder` to `0.5`, making the
  former alpha-back and the latter alpha-front in both renderers.

`fr01_liquid_entity_ordering.cfg` keeps both refraction cvars at zero while
the map registers, then enables them after the map wait and allows ten frames
for resource creation. This matters to OpenGL: its refraction texture is
allocated on a cvar transition and must see the registered transparent-warp
world. `fr01_liquid_entity_ordering_unrefracted.cfg` is the paired zero-cvar
control.

The generated fixture, configs, and manifest are mirrored loose by
`tools/package_assets.py` for the Windows no-zlib headless runtime as well as
being included in `pak0.pkz`.

## Retained evidence

The final hidden native-surface capture is in
`.tmp/renderer-parity/liquid-entity-ordering-final-fast-path`. It exercised
307,200 crop pixels per scene with Vulkan validation enabled and no process
or validation failures.

| Scene | Cross-renderer result | Retained manifest limit |
|---|---|---|
| Live refractive ordering | max RGB `42 / 35 / 36`; MAE `0.885107 / 0.636576 / 0.732604`; `1.067057%` over RGB error 12 | MAE `1.2 / 1.0 / 1.0`; at most `1.5%` over RGB error 12 |
| Unrefracted ordering control | max RGB `1 / 1 / 1`; MAE `0.462578 / 0.145859 / 0.430459`; zero pixels over RGB error 1 | MAE `0.5 / 0.2 / 0.5`; zero pixels over RGB error 1 |

The enabled-versus-disabled images differ materially in both renderers,
confirming that the scene activates refraction rather than merely comparing
dormant cvar settings: OpenGL has 458,964 and Vulkan has 457,738 of 691,200
full-frame pixels over RGB error 12. The paired capture was repeated before
the manifest was tightened; both scene metrics were identical across the two
runs.

## Validation

All commands were run from the repository root; runtime captures use the
hidden native-surface mode with input and audio disabled.

```text
python tools/renderer_parity/generate_transparent_liquid_entity_fixture.py --asset-root assets --validate
python -m unittest discover -s tools/renderer_parity -p 'test_generate_transparent_liquid_entity_fixture.py'
python -m unittest discover -s tools/renderer_parity -p 'test_vulkan_liquid_refraction_source.py'
python tools/gen_vk_world_spv.py --validate
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/test_package_assets.py
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_liquid_entity_ordering_manifest.json --run-root .tmp/renderer-parity/liquid-entity-ordering-final-fast-path --timeout 180 --vulkan-validation
```

## Remaining boundary

This closes the missing normal-map-entity phase coverage and the discovered
disabled-refraction ordering mismatch. It does not claim complete liquid
coverage: additional liquid texture families, moving gameplay entities, and
mixed particle/beam/material receivers remain follow-up work under
`FR-01-T10` and the broader `FR-02-T05` scene expansion.
