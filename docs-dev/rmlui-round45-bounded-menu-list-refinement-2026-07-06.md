# RmlUi Round 45 Bounded Menu List Refinement

Date: 2026-07-06

Tasks: `FR-09-T04`, `FR-09-T06`, `FR-09-T07`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 45 tightens RmlUi list and form containers that still depended on content height after the Quake II Rerelease TTF pass. The goal was to keep repeated lists and long settings forms inside the active `960x720` menu viewport while preserving visible Back/Close actions.

The broad representative capture pass covered `30` routes with no parser/error hits, then the focused refinement pass adjusted:

- in-game menu action layout;
- settings form height/width;
- save/load slot list height/width;
- compact save/load slot row styling.

## Implementation

- `assets/ui/rml/common/theme/shell.rcss`
  - Gives `#game-menu-actions` the same two-column wrap treatment used by the Options menu.
  - Keeps the in-game menu's Back/Close footer visible at `960x720`.

- `assets/ui/rml/common/theme/settings.rcss`
  - Gives settings forms explicit width and viewport-safe height.
  - Gives settings sections and rows explicit widths so section headings and controls do not collapse or wrap unexpectedly.
  - Keeps long settings pages scroll-bounded above their Back/Close footer.

- `assets/ui/rml/common/theme/singleplayer.rcss`
  - Gives save/load slot lists explicit width and viewport-safe height.
  - Compacts repeated save-slot rows so more slots are visible while Back/Close remains available.

## Validation

Build and staging:

- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`: passed and refreshed `.install`.
- `meson compile -C builddir-win`: passed, no work to do.

Static checks:

- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`: passed with `64` staged runtime paths present.
- `python tools/ui_smoke/check_rmlui_runtime_adapter.py`: passed with the Quake II Rerelease font-candidate guard intact.
- `python tools/ui_smoke/check_rmlui_runtime_registry.py`: passed with `58` registered routes and `0` missing/duplicates.
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py`: passed `25` tests.
- `rg -n "var\(|gap:" assets\ui\rml -g "*.rcss"` only found explanatory comments, not unsupported RCSS declarations.

Runtime checks:

- `.install/basew/logs/rmlui_round45_representative_capture.log`
  - Opened `30` representative routes.
  - Had `0` failure/error/exception/unhandled/parser hits.
  - Logged Quake II Rerelease font-source markers.

- `.install/basew/logs/rmlui_round45_final_all_route_open.log`
  - Recorded `59` document opens including startup `main`.
  - Covered `58` unique registered route IDs.
  - Had `0` failure/error/exception/unhandled/parser hits.
  - Logged Quake II Rerelease font-source markers.

Visual captures inspected:

- `.tmp/rmlui/round45-screens/rmlui_round45_bounded_game_960x720.png`
- `.tmp/rmlui/round45-screens/rmlui_round45_width_fix_sound_960x720.png`
- `.tmp/rmlui/round45-screens/rmlui_round45_width_fix_screen_960x720.png`
- `.tmp/rmlui/round45-screens/rmlui_round45_width_fix_performance_960x720.png`
- `.tmp/rmlui/round45-screens/rmlui_round45_width_fix_video_960x720.png`
- `.tmp/rmlui/round45-screens/rmlui_round45_compact_loadgame_960x720.png`
- `.tmp/rmlui/round45-screens/rmlui_round45_compact_savegame_960x720.png`

The screenshots confirm that long in-game, settings, and save/load surfaces keep visible Back/Close actions and no longer draw their primary lists past the bottom of the active menu viewport.

## Remaining Gaps

- Missing data-model notices remain expected on controller-stub routes until live C++ data-model/controller registration lands.
- Some repeated lists now expose clipped scroll content at the bottom of the scroll region. That is preferable to hiding footer actions, but final controller-backed list widgets should eventually add explicit scrollbar/focus behavior.
- This round does not change native Vulkan/RTX-vkpt RmlUi bridge status.
