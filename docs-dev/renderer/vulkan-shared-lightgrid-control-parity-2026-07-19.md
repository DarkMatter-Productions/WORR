# Shared Raster Lightgrid Control and Model-Light Validation (2026-07-19)

Project task: `FR-01-T14`

Status: complete for the shared raster lightgrid-control, world/inline static
receiver, and no-static-receiver model-lighting contracts. Every state is a
strict cross-renderer gate.

## Scope

OpenGL historically exposed `gl_lightgrid` for CPU model-light queries. Native
Vulkan raster sampling had no equivalent shared raster control and always used
the BSPX lightgrid when one existed. Its lightmap fallback also omitted the
OpenGL nearest-inline-BSP intersection pass.

This slice establishes native, renderer-neutral control ownership for the
raster paths only. It does not redirect any Vulkan execution to OpenGL and it
does not alter the independent RTX `rtx_lightgrid` / legacy `vk_lightgrid`
contract.

## Implementation

- `r_lightgrid` is the archived shared raster-model-lighting control.
- OpenGL retains `gl_lightgrid` as a synchronized compatibility alias. The
  registration path selects a modified legacy value during migration, clamps
  both controls to boolean values, and removes callbacks before renderer
  shutdown.
- OpenGL's existing grid sampler now reads the canonical shared cvar.
- Vulkan registers and reads `r_lightgrid` directly in `vk_world.c`; grid
  interpolation only runs when the control is enabled.
- When static grid sampling is unavailable or disabled, Vulkan now mirrors
  OpenGL's fallback ordering: trace the world, test visible inline BSP entity
  models in X/Y with the same rotated-radius or unrotated-bounds broad phase,
  retain the nearest transformed hit, then sample that face lightmap.

The fallback is native Vulkan CPU query code. It uses
`BSP_TransformedLightPoint` and the current refdef entity list, not an OpenGL
renderer call.

OpenGL's `GL_LightPoint` returns white when no receiver is found, and
deliberately bypasses `GL_AdjustColor` (including entity modulation) on that
branch. Vulkan now reports whether a static grid/lightmap receiver was found
to entity submission. CPU-lit MD2/MD5 paths, including their GPU-instanced
submissions, mark only the no-receiver vertices with
`VK_ENTITY_VERTEX_NO_ENTITY_MODULATE`. The native entity fragment shader skips
`entity_modulate` for that marker while retaining its normal shadow and
dynamic-light work. Thus normal sampled lighting stays on the optimized
modulated path while the exceptional fallback matches OpenGL even for
non-default `gl_modulate_entities` values.

## Validation fixture and harness

`tools/renderer_parity/generate_lightgrid_fixture.py` generates a three-map
family: `assets/maps/worr_fr01_lightgrid.bsp`,
`assets/maps/worr_fr01_lightgrid_no_receiver.bsp`, and
`assets/maps/worr_fr01_lightgrid_inline_receiver.bsp`. The primary map
contains:

- a stock `dmspot` MD2 at world origin `(256, 0, -22)`, which is lightgrid
  cell `(1, 0, 0)`;
- a `2 x 1 x 1` BSPX `LIGHTGRID_OCTREE` with an authored green model sample;
- an invisible-to-the-capture 128-by-128 authored world light receiver below
  the MD2, with a red lightmap sample and a real Z-plane BSP split so CPU
  light-point tracing reaches it when the grid is disabled;
- a one-cell Y/Z grid whose minima exactly match the model origin, avoiding a
  false out-of-range sample.

The no-receiver sibling omits the static receiver, exercising the white
fallback independently of red world-lightmap sampling. The inline-receiver map
has no world lightmap: its hidden `RF_VIEWERMODEL` inline BSP owns a real node,
two private terminal leaves, and a horizontal authored blue lightmap face.
`RF_VIEWERMODEL` keeps the brush model in the refdef for
`BSP_TransformedLightPoint` while both backends omit it from rendering. This
avoids treating entity alpha as an invisibility contract for bmodels, which
OpenGL deliberately schedules before its alpha entity phases. All maps are
mirrored loose by `tools/package_assets.py` so headless runtimes that cannot
read compressed packages execute the same fixture.

The four configurations use distinct screenshot names so a multi-scene capture
cannot overwrite one scene with another. The manifest has four strict backend
checks plus three functional transition checks:

1. With `r_lightgrid 1`, the 84,000-pixel model crop is strict GL/Vulkan
   parity: maximum RGB error `0 / 1 / 0`, zero pixels above error 16, and an
   exact 38,928-pixel green-model mask (IoU `1.0`).
2. With `r_lightgrid 0` on the primary map, the red static-receiver fallback
   strict-compares at maximum RGB `1 / 0 / 0`, with zero pixels above error 16.
   The green-grid to red-static transition changes 48,217 pixels above error
   16 for both OpenGL and Vulkan.
3. With `r_lightgrid 0` on the no-receiver sibling, the white fallback
   strict-compares at maximum RGB `1 / 1 / 1`, with zero pixels above error
   16. The red-static to white-no-receiver transition changes 44,679 pixels
   above error 16 for both renderers.
4. With `r_lightgrid 0` on the inline-only map, the transformed inline-BSP
   fallback is blue and strict-compares at maximum RGB `0 / 0 / 1`, with zero
   pixels above error 16. The 30,559-pixel blue mask is exact (IoU `1.0`), and
   its blue-to-white no-receiver transition changes 46,641 pixels above error
   16 for both renderers.

The generic capture comparator supports both strict per-scene backend checks
and `control_pairs`. This manifest uses the default strict backend comparison
for every state; the three pairs are additional functional evidence, not a
substitute for visual parity.

## Commands run

```text
python -m unittest test_compare_captures test_capture_matrix_launch_cvars \
  test_generate_bmodel_cull_fixture test_generate_lightgrid_fixture \
  test_shared_lightgrid_control_source
python -m unittest tools.test_package_assets
python tools/gen_vk_world_spv.py --validate
meson compile -C builddir-win worr_x86_64 worr_engine_x86_64 \
  worr_opengl_x86_64 worr_vulkan_x86_64
python tools/refresh_install.py --build-dir builddir-win \
  --install-dir .install
python tools/renderer_parity/run_capture_matrix.py \
  --install-dir .install \
  --manifest assets/renderer_parity/fr01_lightgrid_manifest.json \
  --run-root .tmp/renderer-parity/lightgrid-inline-receiver-final4 \
  --timeout 90 --vulkan-validation
```

All captures ran headlessly with isolated runtime homes and disabled input.
