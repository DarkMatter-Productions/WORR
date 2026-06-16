# Vulkan Renderer Parity Pass

Date: 2026-06-12

## Summary

This pass closes the major visual parity gaps between the native Vulkan
renderer and the OpenGL renderer for lighting, lightmaps, MD2/MD5 models,
skins, and post-view blends, and adds a native Vulkan screenshot path so
future verification no longer depends on foreground window captures.

All comparisons were made with pixel captures of identical scenes
(`mm-rage` third-person shadowlight repro, `q2dm1` spawn corridor and lava
pit) on both renderers at matched positions via `+teleport` and matched
frame pacing via `cl_maxfps 62`.

## Fixes

### Lighting and lightmap gains (gl_modulate parity)

The Vulkan renderer previously ignored the GL lighting gain cvars entirely,
leaving the world roughly half as bright as GL (default `gl_modulate 2`) and
entities nearly black in dark areas.

- `vk_world.c` now registers the shared cvars: `gl_modulate`,
  `gl_modulate_world`, `gl_modulate_entities`, `gl_brightness`,
  `r_map_overbright_bits`, `r_map_overbright_cap`. The Vulkan swapchain has
  no hardware gamma ramp, so `identity_light` is always 1.0 and the full
  modulate applies.
- `VK_World_ShiftLightmapBytes` now mirrors `GL_ShiftLightmapBytes`
  (hue-preserving map overbright shift plus cap). Changing the shift/cap at
  runtime forces a full lightmap-atlas rebuild through the existing style
  update path.
- The world fragment shader applies `(lm * sun + dlights + gl_brightness) *
  modulate` exactly like the GL shader, but only for faces with authored
  lightmaps. A new `VK_WORLD_VERTEX_LIGHTMAPPED` vertex flag gates this so
  unlit surfaces (classic warp water) keep raw texture brightness like GL.
- The gains travel to both world and entity shaders in the spare components
  of the ShadowPages UBO `shadow_dlight_count` vector: `y` = lightmap
  modulate, `z` = brightness add, `w` = entity modulate. `vk_shadow.c`
  seeds and uploads them every frame (also when shadows are disabled).
- Entity static light is stored unmodulated in the UNORM vertex color and
  multiplied by the entity modulate in the fragment shader, so values above
  1.0 survive (GL passes float colors; a CPU-side multiply clamped at the
  byte boundary and desaturated brightly lit models).
- `R_LightPoint` (used by client effects) keeps the complete CPU result with
  the entity modulate applied, matching `GL_LightPoint`.

### PNG skin/texture overrides

GL's `r_override_textures 1` default replaces paletted formats (.pcx/.wal)
with 32-bit overrides in `png tga dds` order. The Vulkan image loader loaded
the literal path first, so WORR's PNG player skins (brightskins) and any
PNG/TGA texture replacements never loaded. `VK_UI_LoadImageData` now probes
the override extensions first for paletted requests.

### Sampler wrap for skins

Skins registered with `IF_NONE` previously used the clamp sampler. The
rerelease MD5 viewmodels rely on UV wrapping, and GL always uses repeat for
walls and skins. `VK_UI_UpdateDescriptorSet` now mirrors
`GL_SetFilterAndRepeat`: `IT_WALL`, `IT_SKIN`, or `IF_REPEAT` get the repeat
sampler.

### MD5 replacement models

- `vk_md5_use` default changed from `0` to `1` to match `gl_md5_use`.
- MD5 skins are now derived like GL's `MD5_LoadSkins`: the MD2's skin names
  with an `md5/` directory inserted (e.g.
  `models/weapons/v_rail/md5/skin.pcx`, resolved to `.png` by the override
  logic). The MD2 loader keeps its skin name table for this. The md5mesh
  `shader` token remains a fallback only (the rerelease files carry dummy
  names like `uv_256x256` there, which GL skips).
- Skin selection honors `ent->skin` (player skins), then the derived
  `md5->skins[skinnum]` table.

### Color shells and brightskins

- `RF_SHELL_*` entities now render through the MD2 and MD5 paths as GL does:
  shell color from the flag set, vertices expanded along normals by
  `POWERSUIT_SCALE` (or `WEAPONSHELL_SCALE` for view weapons), white texture,
  translucent, fullbright, replacing the skin pass for that entity copy.
  (Port verified against `setup_color`/`tess_*_shell`; an in-game capture is
  still pending because `give item_quad` currently wedges the server on both
  renderers — gameplay bug, not renderer.)
- `RF_BRIGHTSKIN` entities use `ent->rgba` fullbright like GL's
  `setup_color`, so WORR's force-color player overlays render.

### Screen and damage blends

`R_RenderFrame` now queues `fd->screen_blend` and `fd->damage_blend` as 2D
overlays between the 3D view and the HUD, mirroring `GL_Blend`. The damage
blend uses an 8-vertex vignette ring identical to `GL_DrawVignette`,
controlled by the shared `gl_damageblend_frac` cvar; lava/underwater/powerup
full-screen tints match GL now.

### Native Vulkan screenshots

`screenshot` / `screenshotpng [name]` commands now exist in the Vulkan
renderer. The swapchain is created with `TRANSFER_SRC` usage (when the
surface supports it); a pending request records a copy of the rendered
swapchain image into a host-visible buffer inside the same command buffer
(before present), waits the frame fence, converts BGRA/RGBA, and writes the
PNG with stb to `<gamedir>/screenshots/`. This is what all captures in this
pass used.

`meson.build` adds `src/rend_vk/refresh/stb` to the Vulkan renderer include
path for `stb_image_write.h`.

## Verification

Commands run from `E:\Repositories\WORR`:

- `python tools/gen_vk_world_spv.py --validate`
- `ninja -C builddir-win`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/shadowmapping_repro_smoke.py --renderer vulkan --scene translated-md2 --scene moving-bmodel --scene sun-cascade --wait 90`
- `python tools/check_shadowmapping_guardrails.py`
- Paired VK/GL captures (native `screenshotpng` on both renderers, matched
  `+teleport` positions, `cl_maxfps 62`):
  - `mm-rage` third person: world contrast, item visibility, green PNG
    player skin (MD2 and MD5), shadowlight pages all match GL.
  - `q2dm1 1888 464 954`: corridor lightmaps, armor shards, railgun MD5
    viewmodel with correct `md5/skin.png`.
  - `q2dm1 600 950 320` (lava): orange screen blend present on both.
- Shadow dumps stayed on the native depth-compare 2D-array backend with
  `unsupported=0`; `candidates/selected` healthy on mm-rage.

## Remaining Follow-Ups

- Smooth MD5 vertex normals (per-triangle face normals still drive dynamic
  lights and shadow receivers).
- Glowmaps (`*_glow.png`) are not rendered by the Vulkan backend (GL adds
  them emissively; mostly small emissive details on rerelease assets).
- A deterministic in-game color-shell capture once the `give item_quad`
  server hang (reproduces identically on GL) is fixed in the sgame.
- `gl_intensity`/`gl_coloredlightmaps` are not consumed (defaults are 1, so
  no visual delta with stock configs).
- Dynamic light surface marking on non-lightmapped world faces differs
  slightly from GL (VK skips them entirely now, GL marks via nolm mask).
