# RmlUi Round 57 Open and Interaction Audio Refinement

Date: 2026-07-07

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `DV-07-T02`,
`DV-07-T04`

## Summary

Round 57 completes the next audio/navigation refinement pass for the guarded
OpenGL RmlUi menu runtime. The runtime now consumes menu open-sound metadata,
attaches RmlUi focus/change audio listeners to command and form controls, and
keeps the high-level hub routes explicitly marked for menu music plus open
feedback.

This keeps the confirmation menu path aligned with the original menu intent:
Quit and related confirmations use popup presentation with an alert-style open
cue, while ordinary menu hubs use the normal open cue and menu music cue.

## Implementation

- Added `data-menu-sound-open` handling after a document successfully parses,
  shows, and receives its first update.
- Added a short RmlUi feedback-sound de-dupe window so a button click that
  immediately opens a route or popup does not double-fire the same cue.
- Added direct focus/change listeners to interactive RmlUi descendants because
  RmlUi focus events are target-only.
- Defaulted focus/change feedback to the legacy move cue while preserving
  per-element `data-menu-sound-focus` and `data-menu-sound-change` overrides.
- Tagged the high-level hub routes with `data-menu-sound-open="open"` alongside
  their menu music hints:
  - Main
  - Game
  - Options
  - Video
  - Sound
  - Single Player
  - Multiplayer
  - Downloads
  - Download Status
- Left confirmation routes on `data-menu-sound-open="alert"` so Quit Confirm,
  Forfeit Confirm, Leave Match Confirm, and Tourney Replay Confirm keep the
  popup warning tone.

## Runtime Evidence

Focused visual captures:

- `.tmp/rmlui/round57-screens/round57_audio_route_matrix_tga_main.png`
- `.tmp/rmlui/round57-screens/round57_audio_route_matrix_tga_game.png`
- `.tmp/rmlui/round57-screens/round57_audio_route_matrix_tga_download_status.png`
- `.tmp/rmlui/round57-screens/round57_quit_popup.png`

The captures confirm:

- Main menu buttons remain fully visible at `960x720`.
- Game menu actions stay contained in a two-column grid.
- Download Status keeps the progress row and footer within the viewport.
- Quit Confirm is centered as a compact popup with clear destructive emphasis.

Final staged OpenGL all-route sweep:

- log: `.install/basew/logs/rmlui_round57_all_route_audio_open.log`
- opened documents: `59`
- unique route IDs: `58`
- runtime status samples: `58`
- menu music cue markers: `14`
- menu open-sound cue markers: `14`
- RmlUi Quake II Rerelease TTF font-source markers: `3`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`

Focused popup/audio sweep:

- log: `.install/basew/logs/rmlui_round57_popup_audio_flow.log`
- popup route requests: `2`
- opened documents: `5`
- runtime status samples: `2`
- menu music cue markers: `5`
- menu open-sound cue markers: `5`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`

Focused Quit Confirm capture:

- log: `.install/basew/logs/rmlui_round57_quit_popup_capture.log`
- popup route requests: `1`
- Quit Confirm active status samples: `1`
- opened documents: `3`
- menu music cue markers: `3`
- menu open-sound cue markers: `3`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`

## Validation

Accepted validation commands:

- `python tools\ui_smoke\check_rmlui_semantics.py`
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`

The focused pytest pass reported `25 passed`.

The PNG route-matrix capture attempt was not accepted because the engine could
not write PNG screenshots to `.install/basew/screenshots` in this environment.
The same capture pass with TGA output wrote the screenshots successfully.
`main` passed the full route-matrix checker; `game` and `download_status`
failed only the post-capture synthetic back-close expectation because the
current menu flow returned to `main` instead of becoming inactive. The route
open, active status, screenshot write, and visual evidence for those routes
were still recorded and inspected.
