# Spatial Audio Two-Identity Source Paths

Date: 2026-04-27

Task IDs: `FR-06-T01`, `FR-06-T10`

## Summary

Implemented the fifth wave from `docs-dev/proposals/spatial-audio.md`: revise OpenAL reverb routing into a two-identity model. The global EFX slot remains tied to the listener room, while every audible source resolves its own source-room path state before direct filtering and per-source reverb send routing are applied.

## Implementation

- Added `al_source_path_state_t` in `src/client/sound/al.cpp`.
  - Path classes cover local, same-space, adjacent-space, cross-space, portal, exterior-to-interior, interior-to-exterior, and unreachable states.
  - Each path state carries direct occlusion floors, direct HF limits, reverb-send scale, and reverb-send HF colour.
- Reworked source routing to use the path state.
  - Same-space sources reduce their send scale so they stay clearer and drier.
  - Adjacent/cross-space sources get mild direct damping when `s_occlusion` is enabled and stronger filtered sends.
  - Exterior-to-interior and interior-to-exterior cases now use distinct gain/HF behaviour instead of sharing one generic cross-region curve.
  - Unreachable source areas apply heavy direct damping when occlusion is enabled and darker/lower sends.
- Integrated fourth-wave portal propagation into the path state.
  - A valid one- or two-hop portal route can improve direct audibility while still applying the source path's send colour.
  - Portal state remains a one-slot EFX implementation: no second auxiliary early-reflection slot was added in this pass.
- Applied the same resolver to merged looping sounds.
  - Per-entity path states are weighted by contribution and collapsed into the merged OpenAL source's send scale and HF colour.
  - Direct occlusion remains controlled by `s_occlusion`; source-room send colouring remains part of the reverb path.

## Behavior Notes

The fifth wave makes the listener/source split explicit without changing the authored EAX override model. Authored zones can still own the global listener effect when active. Otherwise, the automatic BSP listener room selects the EFX preset, and source path states decide how each sound feeds that listener room.

The implementation deliberately keeps a single auxiliary reverb slot. A second slot for early reflections or portal slap remains possible later, but the current pass follows the roadmap's lower-risk recommendation: one slot plus path-aware send filtering.

## Verification Notes

- `meson compile -C builddir-win-bootstrap-hosted worr_engine_x86_64`
  - Rebuilt the OpenAL backend changes successfully.
- `python tools\refresh_install.py --build-dir builddir-win-bootstrap-hosted --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Refreshed and validated the local Windows `.install` staging root after the build.
