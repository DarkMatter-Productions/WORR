# Spatial Audio Acoustic Material Resolver

Date: 2026-04-27

Task IDs: `FR-06-T01`, `FR-06-T07`

## Summary

Implemented the second wave from `docs-dev/proposals/spatial-audio.md`: replace the old occlusion substring classifier with an explicit BSP acoustic-material resolver. The runtime now treats rerelease `.mat` IDs as the primary material key and converts them to banded acoustic coefficients before producing direct attenuation, direct high-frequency filtering, and reverb-send colour.

## Implementation

- Added an internal acoustic material table in `src/client/sound/main.cpp`.
  - Profiles store low/mid/high transmission, low/mid/high absorption, scattering, and semantic flags.
  - Exact `.mat` IDs are resolved first through `csurface_t.id` into `cl.bsp->texinfo[id - 1].c.material`.
  - `csurface_t.material` and substring hints remain as fallback paths for older or custom material strings.
- Preserved the established material family behavior as the initial coefficient table:
  - glass/window/ice, grates/fences/ladders, soft materials, foliage, wood, metal, stone/concrete/brick/tile, sky, and liquids.
  - Added small built-in coverage for common rerelease and multiplayer staples such as `ladder`, `chainlink`, `bars`, `railing`, `forcefield`, `flesh`, `asphalt`, `marble`, `water`, `slime`, and `lava`.
- Extended shared occlusion output:
  - `S_ComputeOcclusion(...)` now returns direct occlusion plus a direct cutoff target and a reverb-send HF colour.
  - Channel state now smooths `occlusion_reverb_gainhf` alongside the existing occlusion amount and cutoff.
- Updated OpenAL spatial DSP:
  - Non-looping sources now apply material-derived direct gain through `AL_LOWPASS_GAIN` and material-derived high-frequency loss through `AL_LOWPASS_GAINHF`.
  - Per-source auxiliary reverb sends now apply their own material HF colour so occluded reflections are not always full-bright.
  - Merged OpenAL loop sounds keep their existing baked gain attenuation and use the shared resolver only for cutoff/send-colour targets to avoid double attenuation.
- Updated the DMA path to consume the new resolver signature and preserve coherent channel occlusion state for merged loops.

## Behavior Notes

The resolver intentionally keeps the existing zero-authoring path. A map that only ships simple rerelease `.mat` files now gets structured acoustic coefficients automatically. A map with no material metadata still falls back to the default profile and the existing multi-ray obstruction model.

The second wave does not add the third-wave BSP region cache or fourth-wave portal graph. It prepares those systems by making each blocked path resolve to reusable acoustic coefficients instead of one-off string tests.

## Verification Notes

- `meson compile -C builddir-win-bootstrap-hosted worr_engine_x86_64`
  - Rebuilt the shared sound, DMA, and OpenAL backend changes successfully.
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Refreshed and validated the local Windows `.install` staging root after the build.
