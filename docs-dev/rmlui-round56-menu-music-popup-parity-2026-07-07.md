# RmlUi Round 56 Menu Music and Popup Parity

Date: 2026-07-07

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `DV-07-T02`,
`DV-07-T04`

## Summary

Round 56 turns the menu music metadata introduced in the previous refinement
round into runtime behavior and closes one remaining confirmation parity gap:
the in-game Game menu Quit action now uses the same popup confirmation route as
the Main menu Quit action.

The implementation stays inside the existing engine audio stack. RmlUi
documents can declare `data-menu-music="menu"`, and the compiled runtime now
requests `OGG_Play()` after a document successfully parses, shows, and updates.
The OGG layer remains responsible for choosing the menu track while disconnected
or the current map track while connected.

## Implementation

- Added parsed-document audio hint handling in the compiled RmlUi runtime.
- Consumed `data-menu-music="menu"` through `OGG_Play()` and logged a route
  music cue marker for validation.
- Added menu-music metadata to high-level hub routes:
  - Main
  - Game
  - Options
  - Video
  - Single Player
  - Multiplayer
  - Downloads
  - Download Status
- Changed Game menu Quit from direct `quit` execution to `ui.popup` targeting
  `quit_confirm`, matching the Main menu popup confirmation path.
- Preserved the existing legacy menu samples for click feedback from Round 55.

## Runtime Evidence

Focused final captures:

- `.tmp/rmlui/round56-screens/round56_contact.png`
- `.tmp/rmlui/round56-screens/round56_game_960.png`
- `.tmp/rmlui/round56-screens/round56_main_960.png`
- `.tmp/rmlui/round56-screens/round56_quit_confirm_960.png`
- `.tmp/rmlui/round56-screens/round56_sound_960.png`

The captures confirm:

- Game Quit remains visually destructive and now belongs to the same popup
  confirmation flow as Main Quit.
- Main menu button positioning remains stable.
- Quit Confirm keeps the compact popup layout.
- Sound Settings keeps the two-column typed-widget layout with no footer
  overlap.

Final staged OpenGL all-route sweep:

- log: `.install/basew/logs/rmlui_round56_all_route_music_open.log`
- opened documents: `59`
- unique route IDs: `58`
- runtime status samples: `58`
- menu music cue markers: `14`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`
- Quake II Rerelease TTF font-source markers: `3`

Focused Game/Main popup sweep:

- log: `.install/basew/logs/rmlui_round56_game_popup_music.log`
- popup route markers: `2`
- menu music cue markers: `5`
- opened documents: `5`
- runtime status samples: `2`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`

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
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py`
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`

The focused pytest pass reported `9 passed` for runtime adapter coverage and
`16 passed` for runtime capture coverage.

## Remaining Gaps

This round does not claim live controller/data-model parity, route-wide
automated pixel clipping assertions, full keyboard/focus parity, native
Vulkan/RTX-vkpt RmlUi rendering, or final visual parity. It also does not
attempt a broad route-wide music metadata pass; instead it wires behavior and
marks the menu hubs most likely to be opened directly.
