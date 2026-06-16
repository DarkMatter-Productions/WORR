# Vulkan Entity Lightmap And Shadow Receiver Repair

Date: 2026-06-11

Task IDs: `FR-02-T09`, `FR-02-T10`, `FR-02-T11`, `DV-07-T05`, `FR-01-T04`

## Summary

This pass repairs the native Vulkan raster renderer's entity receiver path so
brush entities, MD2 aliases, and MD5 replacement models participate in the same
lighting vocabulary as the world renderer. It specifically closes the
post-shadowmapping follow-up where native Vulkan world surfaces sampled shadow
pages, but entities still rendered through a texture/color-only shader.

The fix stays native to `rend_vk`; it does not redirect Vulkan rendering through
OpenGL or RTX code.

## Root Causes

1. `vk_entity.c` still used an entity shader with only position, texture
   coordinates, and vertex color. There was no normal, receiver world position,
   lightmap UV, material flag, dynamic-light loop, or shadow-page descriptor
   binding.
2. Inline BSP model faces were excluded from the static world mesh, but the
   Vulkan lightmap atlas only packed static world faces. Moving brush entities
   therefore could not sample their authored face lightmaps.
3. MD2 and MD5 entities were lit once at entity origin on the CPU, including
   dynamic lights. That made dynamic lighting flat, double-counted if a shader
   receiver was added later, and left the shader without receiver normals for
   shadowed dynamic-light evaluation.
4. The embedded SPIR-V generator only owned the world shader group, so adding a
   Vulkan entity shader source would have been easy to forget during future
   shader regeneration.

## Implementation

### Entity Shaders

- Added `src/rend_vk/shaders/vk_entity.vert`.
- Added `src/rend_vk/shaders/vk_entity.frag`.
- Entity vertex data now includes:
  - world position
  - base UV
  - lightmap UV
  - color/modulation
  - per-vertex material/lighting flags
  - world-space receiver normal
- The entity fragment shader binds the same descriptor-set roles as the world
  shader:
  - set 0: base texture
  - set 1: lightmap sampler
  - set 2: shadow page textures, moment textures, and `ShadowPages` UBO
- The shader supports:
  - fullbright/no-shadow/no-dynamic-light flags for sprites, beams, particles,
    and view weapons
  - alpha-tested entity surfaces
  - authored lightmap sampling for inline BSP models
  - static entity lighting plus per-fragment dynamic lights for MD2/MD5
  - local and sun shadow sampling through the native Vulkan shadow descriptor

### SPIR-V Generation

- Extended `tools/gen_vk_world_spv.py` to generate both embedded shader headers:
  - `src/rend_vk/vk_world_spv.h`
  - `src/rend_vk/vk_entity_spv.h`
- The tool now treats entity shaders as first-class sources and still supports
  `--validate` via `spirv-val`.

### Lightmaps And Brush Entities

- `VK_World_BuildLightmapAtlas` now packs all drawable BSP faces with authored
  lightmaps, including inline model faces.
- Static world mesh construction still uses the world-face mask, so inline
  model faces are not drawn statically.
- Added:
  - `VK_World_GetLightmapDescriptorSet`
  - `VK_World_GetFaceLightmapUV`
- `VK_Entity_AddBspModel` now computes per-face lightmap UVs for moving brush
  entities and routes lightmapped faces through the entity lightmap receiver
  path.

### MD2 And MD5 Models

- MD2 loading now keeps imported vertex normals from the MD2
  `lightnormalindex` table.
- MD2 rendering interpolates both positions and normals between frames before
  transforming to world space.
- MD5 rendering now emits receiver normals using per-triangle world-space face
  normals. This is not smooth skeletal normal reconstruction yet, but it gives
  dynamic lights and shadow receivers valid directional data instead of a flat
  origin-light color.
- MD2/MD5 CPU light sampling now requests static lighting only; dynamic lights
  are added once in the entity fragment shader.
- MD2/MD5 draw loops now skip incomplete trailing index triplets defensively.

## Verification

Commands run from `E:\Repositories\WORR`:

- `python tools/gen_vk_world_spv.py --validate`
- `ninja -C builddir-win worr_vulkan_x86_64.pdb`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/shadowmapping_repro_smoke.py --renderer vulkan --scene translated-md2 --scene moving-bmodel --wait 90`
- `python tools/shadowmapping_repro_smoke.py --renderer vulkan --scene sun-cascade --scene projectile-self-shadow --wait 90`
- `.install/worr_x86_64.exe +set basedir E:\Repositories\WORR\.install +set developer 1 +set logfile 1 +set logfile_flush 1 +set logfile_name vulkan_md5_toggle_smoke +set r_renderer vulkan +set vid_fullscreen 0 +set vk_md5_use 0 +map q2dm1 +wait 60 +set vk_md5_use 1 +map q2dm1 +wait 60 +quit`
- `python tools/check_shadowmapping_guardrails.py`
- `git diff --check`

Observed smoke results:

- Vulkan shadow dumps used the native depth-compare 2D-array backend with
  `unsupported=0`.
- `moving-bmodel` rendered dirty cone-light pages and exercised the inline BSP
  entity path.
- `sun-cascade` rendered dirty cascade pages and exercised entity/world receiver
  shadow descriptor binding with sun pages enabled.
- The MD5 toggle smoke loaded replacement MD5 assets, including player and
  weapon replacements, and exited cleanly.

## Remaining Follow-Ups

- Reconstruct smooth MD5 vertex normals from mesh bind data and skinned
  tangents/normals instead of using per-triangle face normals.
- Separate direct sun contribution from baked lightmaps before making
  `r_shadow_sun` a default-on visual path.
- Add image-based Vulkan parity captures for MD2/MD5 lighting and moving
  lightmapped brush entities once an automated capture comparator exists.
