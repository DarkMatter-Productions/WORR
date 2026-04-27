# Spatial Audio First-Wave Consolidation

Date: 2026-04-27

Task IDs: `FR-06-T01`, `FR-06-T06`

## Summary

Implemented the first wave from `docs-dev/proposals/spatial-audio.md`: make WORR's existing OpenAL spatial features behave as the default experience, remove the tight reverb-send dependency on `al_eax`, and provide a built-in reverb environment fallback when `sound/default.environments` is absent.

## Implementation

- Changed default spatial-audio cvars:
  - `s_occlusion` now defaults to `1`.
  - `al_reverb` now defaults to `1` and is archived.
  - `al_reverb_lerp_time` now defaults to `3.0` and is archived.
  - `al_reverb_send` now defaults to `1`; its distance/min/occlusion tuning cvars are archived.
  - `al_air_absorption` now defaults to `1`; `al_air_absorption_distance` is archived.
  - `al_hrtf` now defaults to `1`, which leaves HRTF in OpenAL's default/autodetect mode instead of explicitly forcing it off.
- Decoupled per-source reverb sends from `al_eax` and `cl.bsp`. `al_reverb` is now the master switch for auxiliary send routing; `al_eax` only determines whether authored EAX zones can override the active effect.
- Reconnected the automatic BSP reverb environment path:
  - `AL_LoadReverbEnvironments()` now runs during OpenAL init.
  - `AL_SetReverbStepIDs()` and reverb state reset run at end registration when a BSP is active.
  - The OpenAL update loop now prefers authored EAX/underwater environments when active, otherwise falls back to the automatic BSP reverb resolver.
- Added a compiled-in `sound/default.environments` fallback table with small, medium, large, and exterior room-size buckets and coarse material families for stone, metal, soft, and exterior surfaces.
- Routed EFX preset loading through the same EAX/standard-reverb fallback path used by authored EAX properties so automatic reverb works on devices with only standard `AL_EFFECT_REVERB`.

## Behavior Notes

`al_eax 1` still enables `client_env_sound` / `env_sound` authored zone overrides. When no authored zone is active, the automatic BSP resolver owns the reverb slot. This keeps old authored control points useful while making the zero-authoring path produce room-aware reverb by default.

Existing archived user configs can still override the new defaults. A user with `s_occlusion 0`, `al_reverb 0`, `al_reverb_send 0`, `al_air_absorption 0`, or `al_hrtf 0` already written in config will keep that preference until they reset or change the cvar.

## Verification Notes

- `meson compile -C builddir-win-bootstrap-hosted worr_engine_x86_64`
  - Rebuilt the OpenAL/shared sound changes successfully.
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Refreshed and validated the local Windows `.install` staging root after the build.
