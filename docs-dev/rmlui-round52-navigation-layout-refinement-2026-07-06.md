# RmlUi Round 52 Navigation Layout Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `DV-03-T07`,
`DV-07-T02`, `DV-07-T04`

## Summary

Round 52 refines the RmlUi menu navigation surfaces after the previous
widget/layout pass. The main goal was to stop long action lists from behaving
as narrow single columns and to make shell, single-player, multiplayer,
session, save/load, and settings pages feel like one clean menu system.

The implementation uses a conservative `604px` menu slab for non-main menu
surfaces so common two-column routes fit inside narrower 4:3-safe space while
remaining balanced on the `960x720` reference canvas. RmlUi did not reliably
wrap command controls through flex or inline flow in these menu documents, so
finite command menus now use explicit positioned tiles inside bounded panels.

## Implementation

- Converted shell command controls in `shell/main.rml`, `shell/options.rml`,
  and `shell/game.rml` from generic `div.ui-button` elements to real
  `button type="button"` elements while preserving `data-command` and
  `data-route-target` behavior.
- Reworked `common/theme/shell.rcss` so Options and Game use deterministic
  two-column command grids, with the main menu retaining its distinct vertical
  composition and stable button width.
- Reworked `common/theme/singleplayer.rcss` so Single Player selector cards,
  Load/Save actions, and save-slot lists use bounded two-column layouts.
  Autosave remains a full-width highlighted row.
- Reworked `common/theme/session.rcss` so Multiplayer, Call Vote, Join, and
  Deathmatch Join use compact two-column action grids and narrower status/link
  panels.
- Rebalanced `common/theme/settings.rcss` to the same `604px` contract with
  narrower label/control/value columns while preserving typed widgets for
  toggles, ranges, selects, fields, and progress controls.

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

- `.install\basew\logs\rmlui_round52_960x720_layout_capture.log` records
  representative direct route-open screenshots for shell, single-player,
  save/load, multiplayer/session, and settings routes.
- `.tmp\rmlui\round52-screens\rmlui_round52_960x720_layout_contact_sheet.png`
  visually confirms the new two-column navigation grids and the narrowed
  settings widget layout.
- `.install\basew\logs\rmlui_round52_final_all_route_open.log` records
  `59` opened documents, `58` unique route IDs, `0`
  failure/error/exception/unhandled/parser/transition/animation/unsupported
  hits, and Quake II Rerelease font-source markers.

## Remaining Gaps

- Live controller/data-model behavior, runtime navigation parity, native
  Vulkan/RTX-vkpt RmlUi rendering, localization/text shaping parity, and final
  visual parity remain pending.
- The existing screenshot capture path accepted a `640x480` request but still
  reported `960x720` runtime dimensions, so true narrow-viewport screenshot
  evidence needs a capture-harness follow-up before claiming responsive matrix
  parity.
