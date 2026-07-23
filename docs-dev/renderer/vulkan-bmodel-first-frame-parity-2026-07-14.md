# Vulkan Bmodel First-Frame Parity (2026-07-14)

Task: `FR-01-T06`

## Outcome

Native Vulkan inline BSP models now have deterministic first-render-frame
parity with OpenGL. Inline-model faces are excluded from the static world mesh,
rendered only through the current entity transform, and honor the shared
`r_fullbright` entity/world-lighting contract. The follow-up audit also closes
legacy Quake II v38 lightmap-coordinate preparation in Vulkan. Repository-owned
fullbright/bmodel and authored-lightmap captures are pixel-identical between
OpenGL and Vulkan.

This work remains native Vulkan. No Vulkan path redirects to OpenGL.

## Correctness boundary

An inline BSP model has two distinct representations:

1. its authored face range in the BSP; and
2. its current network entity transform.

The authored faces must not be baked into the static world mesh. They must be
submitted only by the bmodel entity path, using the entity origin and angles
from the current frame. Otherwise the first rendered frame can contain both a
stale authored-position copy and the correctly transformed dynamic copy.

The common BSP loader deliberately does not populate a usable face range for
world model zero. The Vulkan world build therefore cannot assume that
`models[0].firstface/numfaces` is valid.

## Native Vulkan implementation

### Static-world ownership

`VK_World_BuildWorldFaceMask` in `src/rend_vk/vk_world.c` now establishes face
ownership once during map registration:

- use the world model range when it is valid;
- otherwise begin with every BSP face as a conservative world candidate; and
- clear every valid face range owned by `models[1..n]`.

World lightmap and geometry construction consume this mask before
triangulation. The final fixture registration proves the intended ownership:

```text
VK_World_BuildWorldFaceMask: total=7 world=1 inline_cleared=6 world_range_valid=0
VK_World_PrepareLightmapGeometry: mode=legacy prepared=1 rejected=0
VK_World_BuildMesh: vertices=6 batches=1 lightmapped=6
```

The static world therefore uploads one quad (six vertices), not the six-face
inline box. No per-frame filter or duplicate static draw is required.

### Current-state entity submission

`VK_Entity_AddBspModel` in `src/rend_vk/vk_entity.c` resolves the inline model
from `~ent->model`, builds the current entity transform, transforms the view
into model-local space for face rejection, and emits only the visible model
faces at their current world positions. The fixture's box is authored left of
the camera and translated to the center by its initial entity origin. The
passing capture contains only the centered copy.

### Fullbright parity

The strict fixture also exposed a separate Vulkan bmodel color mismatch.
OpenGL's `GL_LightPoint` returns white immediately when `r_fullbright` is set.
Vulkan instead used its no-lightdata fallback of `0.75`, then applied the
entity modulate, producing a visible `1.5x` color gain.

`VK_World_Fullbright` now owns the shared `r_fullbright` cvar contract.
`VK_World_LightPointEx`, `VK_Entity_LitColor`, and
`VK_Entity_LightingFlags` use it to:

- skip static and dynamic light sampling in fullbright mode;
- provide a white entity color for ordinary lit models; and
- select the shader fullbright path, bypassing entity-light modulation.

The `FR-01-T07` audit then completed the static-world half of the contract.
`ShadowPages.shadow_moment_tuning.w` now carries the global fullbright bit to
`vk_world_shadow.frag`. A fragment uses white lighting when that bit or the
authored per-surface fullbright flag is set. Runtime cvar changes therefore do
not rebuild world geometry, upload vertices, recreate descriptors, or rebuild
pipelines.

The same audit found that legacy v38 maps do not provide `lm_width`,
`lm_height`, `lm_axis`, or `lm_offset` in the shared BSP face. OpenGL derives
them from face vertices and texture axes during surface upload; Vulkan had only
worked when BSPX `DECOUPLED_LM` supplied them. Native Vulkan now derives the
same 16-unit grid in `VK_World_PrepareLightmapGeometry`, validates extents and
light-data bounds, and preserves decoupled coordinates when present.

Special entity colors retain their established ordering. This change matches
the existing visual renderer query contract; the renderer-neutral gameplay
light-query separation remains the distinct `FR-01-T09` task.

## Deterministic repository fixture

`tools/renderer_parity/generate_bmodel_first_frame_fixture.py` generates a
small Quake II IBSP v38 map and two solid-color TGA textures:

- `assets/maps/worr_fr01_bmodel_first_frame.bsp`
- `assets/textures/parity/fr01_bm_bg.tga`
- `assets/textures/parity/fr01_bm_box.tga`

The 16,224-byte BSP contains one blue world quad, one six-face green inline
box, and an authored `81 x 61` dark legacy lightmap for the world face. Model
zero intentionally exercises the normal invalid-world-range loader case. The
inline entity translates the authored left-hand box to screen center on its
initial state. Clockwise render windings satisfy the OpenGL reference renderer
while Vulkan remains natively rendered.

The generated map SHA-256 is:

```text
1B65468863A0D849A319AAF9B12EC00ABDF7CFD995FC733B6ACA99B4B70AE873
```

`assets/renderer_parity/fr01_bmodel_first_frame_manifest.json` owns three scenes:

- `bmodel_transformed_first_frame` uses `r_fullbright 1` and proves the
  transformed bmodel plus global world-lighting bypass;
- `legacy_lightmapped_world` uses `r_fullbright 0`, `gl_modulate 1`, and the
  shared `r_lightmap 1` diagnostic mode to compare a background crop containing
  only the authored legacy lightmap; and
- `global_fullbright_lightmapped_world` uses `r_fullbright 1` to prove that
  the normal textured fullbright route remains separate from the diagnostic
  lightmap route.

The manifest enforces both pixel and semantic gates:

- mean absolute RGB at most `2/255` per channel;
- at most `0.5%` of pixels above an RGB error of `8`;
- at least 10,000 green bmodel pixels per backend;
- at most `1%` backend mask-count difference; and
- at least `0.99` backend mask intersection-over-union.

The lightmapped scene additionally requires at least 30,000 exact RGB
`32 / 32 / 32` authored-lightmap pixels, at most `1%` backend count
difference, and IoU at least `0.99`.

The capture runner disables DOF and RmlUi before renderer/UI initialization.
The new idempotent `closeconsole` command invokes the existing forced console
close path, allowing headless scene configs to enter gameplay deterministically
without relying on renderer-dependent startup timing.

The fixture map, textures, configs, and manifest are mirrored loose by
`tools/package_assets.py`. This is required because the current Windows client
does not include zlib and therefore cannot consume `pak0.pkz` by itself.

## Validation evidence

Final evidence root:

```text
.tmp/renderer-parity/fr01-bmodel-lightmap-final2/
```

The OpenGL and validation-layer Vulkan fullbright/bmodel captures are
byte-identical:

```text
742FE7EB5518E469EBD3F9FE0A66D3738C520CAE3E3C210EE7A2DF2FE5A1D992
```

The final two-scene report records:

```text
fullbright/bmodel crop pixels:        170000
legacy-lightmap crop pixels:          34000
maximum RGB error (both):             0 / 0 / 0
mean absolute RGB error (both):       0.0 / 0.0 / 0.0
pixels over threshold (both):         0 (0.0%)
OpenGL/Vulkan green-mask pixels:      47300 / 47300
OpenGL/Vulkan dark-lightmap pixels:   34000 / 34000
backend mask-count delta (both):      0.0%
mask intersection-over-union (both):  1.0
process failures:                     none
```

The Vulkan process log contains no VUID, validation error/warning, device-lost,
access-violation, assertion, or fatal diagnostic.

## Automated and build checks

Passed commands:

```text
python tools/renderer_parity/generate_bmodel_first_frame_fixture.py --asset-root assets --validate --json
python -m py_compile tools/renderer_parity/generate_bmodel_first_frame_fixture.py tools/renderer_parity/run_capture_matrix.py tools/package_assets.py tools/test_package_assets.py
python tools/renderer_parity/test_compare_captures.py
python tools/test_package_assets.py
ninja -C builddir-win worr_vulkan_x86_64.dll
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --run-root .tmp/renderer-parity/fr01-bmodel-lightmap-final2 --vulkan-validation --json-output .tmp/renderer-parity/fr01-bmodel-lightmap-final2/results-runner.json
python tools/renderer_parity/run_capture_matrix.py --install-dir .install --manifest assets/renderer_parity/fr01_bmodel_first_frame_manifest.json --scene legacy_lightmapped_world --run-root .tmp/renderer-parity/fr01-legacy-lightmap-probe --compare-only --json-output .tmp/renderer-parity/fr01-legacy-lightmap-probe/results-focused.json
```

The comparison test suite passed 4 tests, including focused-scene selection.
The packaging suite passed 14 tests. Final staging packaged 330 assets. The
engine target was temporarily blocked by unrelated concurrent networking
compile inconsistencies (`carrier_command_shape` call arity and undeclared
native-readiness pilot symbols); the native Vulkan/OpenGL targets built, and
the staged engine/OpenGL artifacts completed both paired captures.

## Staged deliverables

Final staged SHA-256 values:

```text
70909BC06A162FA7BD367A908B0BAB7D7A182CCD691ED1CB5BEDA26DC31BD9A4  .install/worr_engine_x86_64.dll
2357AB1887D2083175FADD5950DAF764E0DFAFD3991E2D32836EB7D926756DC9  .install/worr_vulkan_x86_64.dll
D242B399152B7E6E93AE80A5D832665B18C3B38AB78C41D59CEB5464E185B494  .install/worr_opengl_x86_64.dll
82C16BCAF2B5DCD5E5AED2E1B5B3CCC31054058D58BA74008387D4D61CDF10A8  .install/basew/pak0.pkz
1B65468863A0D849A319AAF9B12EC00ABDF7CFD995FC733B6ACA99B4B70AE873  .install/basew/maps/worr_fr01_bmodel_first_frame.bsp
```

## 2026-07-19 follow-up: shared `r_lightmap` parity

The legacy probe was strengthened after a fresh capture showed that a broad
dark-range check did not isolate the authored lightmap itself. The shared
OpenGL `r_lightmap` cvar is now implemented natively by Vulkan opaque and
alpha world pipelines, including OpenGL-compatible glow/intensity suppression
and bloom-extraction suppression. The current three-scene validation root is:

```text
.tmp/renderer-parity/fr01-bmodel-lightmap-debug-final/
```

It holds exact OpenGL/Vulkan equality for the 170,000-pixel transformed scene,
the 34,000-pixel exact RGB `32 / 32 / 32` legacy-lightmap mask, and the
34,000-pixel exact RGB `24 / 40 / 72` global-fullbright mask under validation.
See `vulkan-shared-lightmap-debug-parity-2026-07-19.md` for implementation and
validation detail.

## Scope conclusion

`FR-01-T06` remains complete. The first-frame stale bmodel failure has a native
Vulkan ownership fix, and the strengthened fixture now also guards legacy v38
lightmaps and global world fullbright with strict exact-pixel gates,
validation-layer evidence, and refreshed distributable staging.
