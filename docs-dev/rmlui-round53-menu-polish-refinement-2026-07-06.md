# RmlUi Round 53 Menu Polish Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `DV-03-T07`,
`DV-07-T02`, `DV-07-T04`

## Summary

Round 53 is a visual and interaction polish pass on top of the Round 52
deterministic menu grids. The previous round fixed list overflow and button
semantics; this round makes those menus read less like tables and more like
clean command surfaces.

## Implementation

- Shell Options and Game command grids now use spaced command tiles with
  reduced content width, slimmer heights, rounded corners, darker fills, and
  a subtle left accent that brightens on hover/focus.
- Single Player selectors, Load/Save buttons, and save-slot grids now use the
  same spaced tile language, with Autosave preserved as a full-width
  highlighted action.
- Multiplayer, Call Vote, Join, and Deathmatch Join command regions now use
  the same compact command tile treatment while preserving their deterministic
  two-column positions.
- Shared settings rows are slightly denser and cleaner, with smaller section
  headings, clearer row borders, rounded row frames, and preserved typed
  widgets for toggles, ranges, selects, fields, and progress controls.

## Evidence

Build/stage validation:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`

Static validation:

- `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python tools\ui_smoke\check_rmlui_semantics.py`
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`

Runtime validation:

- `.install\basew\logs\rmlui_round53_refinement_capture.log` records
  representative `960x720` route-open captures for shell, settings,
  single-player, save/load, multiplayer, and session routes.
- `.tmp\rmlui\round53-screens\rmlui_round53_refinement_contact_sheet.png`
  visually confirms the spaced command tile treatment and denser settings
  rows across the representative route set.
- `.install\basew\logs\rmlui_round53_final_all_route_open.log` records `59`
  opened documents, `58` unique route IDs, `0`
  failure/error/exception/unhandled/parser/transition/animation/unsupported
  hits, and Quake II Rerelease font-source markers.

## Remaining Gaps

- Live controller/data-model behavior, runtime navigation parity, native
  Vulkan/RTX-vkpt RmlUi rendering, localization/text shaping parity, true
  narrow-viewport screenshot evidence, automated pixel clipping assertions,
  and final visual parity remain pending.
