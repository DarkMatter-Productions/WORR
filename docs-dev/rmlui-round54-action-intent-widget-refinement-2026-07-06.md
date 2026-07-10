# RmlUi Round 54 Action Intent and Widget Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `DV-07-T02`,
`DV-07-T04`

## Summary

Round 54 continues the staged OpenGL RmlUi menu refinement by tightening action
intent, widget semantics, and focused utility-panel layout without changing the
accepted `604px` navigation contract from Rounds 52 and 53.

The round fixes the remaining command controls that still behaved like styled
`div` elements, gives primary and destructive actions a consistent visual
language, and verifies that the higher-specificity shell/session grid rules do
not hide those action states.

## Implementation

- Converted remaining command-like `div.ui-button` controls to real
  `<button type="button">` elements in:
  - `assets/ui/rml/shell/quit_confirm.rml`
  - `assets/ui/rml/shell/downloads.rml`
  - `assets/ui/rml/shell/download_status.rml`
  - `assets/ui/rml/core/runtime_smoke.rml`
- Extended the shared base theme with action-intent states:
  - primary actions such as Resume, Apply, Start, and vote/replay Yes use a
    green-tinted fill and border treatment;
  - destructive actions such as Quit, Disconnect, Cancel, Leave Match, and
    Forfeit use a red-tinted fill and border treatment;
  - secondary back/close/pop actions keep a quieter supporting style.
- Added higher-specificity shell and session intent selectors so the fixed
  two-column command grids do not override primary/destructive treatment.
- Refined utility/player setup surfaces with slimmer panel borders, a clearer
  preview frame, compact input columns, and a primary Apply action.
- Reworked the Quit confirmation into a compact confirmation panel with a
  destructive Yes action and a secondary No action.
- Updated runtime smoke button styling so its real buttons keep the intended
  boxed treatment.

## Runtime Evidence

Focused captures were taken after staging the updated assets:

- `.tmp/rmlui/round54-final-capture/round54_final_main.png`
- `.tmp/rmlui/round54-final-capture/round54_final_game.png`
- `.tmp/rmlui/round54-final-capture/round54_final_download_status.png`
- `.tmp/rmlui/round54-final-capture/round54_open_dm_join.png`
- `.tmp/rmlui/round54-final-capture/round54_open_players.png`

The captures confirm:

- Game Resume is visibly primary while Disconnect and Quit are visibly
  destructive.
- Main Quit and Download Cancel retain the destructive treatment.
- Deathmatch Join keeps the compact two-column layout with Leave Match marked
  destructive.
- Player Setup uses typed form widgets, a large preview area, and a primary
  Apply action without list overflow.

The final staged OpenGL all-route sweep is recorded in
`.install/basew/logs/rmlui_round54_final_all_route_open.log`:

- opened documents: `59`
- unique route IDs: `58`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`
- Quake II Rerelease TTF font-source markers: `3`

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python tools\ui_smoke\check_rmlui_semantics.py`
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`

The pytest pass reported `25 passed`.

## Remaining Gaps

This round does not claim live controller/data-model parity, broad input
navigation parity, true narrow-viewport screenshot evidence, automated
route-wide pixel clipping assertions, native Vulkan/RTX-vkpt RmlUi rendering,
or full visual parity.
