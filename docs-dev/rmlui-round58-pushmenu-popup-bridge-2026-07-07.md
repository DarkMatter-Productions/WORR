# RmlUi Round 58 Pushmenu and Popup Bridge Refinement

Date: 2026-07-07

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `FR-03-T08`,
`DV-04-T02`, `DV-07-T04`

## Summary

Round 58 closes the practical menu-loading gap where legacy/cgame `pushmenu`
producers could still enter the old menu stack even when the staged OpenGL
RmlUi runtime was enabled. Both client-side and cgame-side `pushmenu <route>`
paths now prefer registered RmlUi route IDs when `ui_rml_enable` is on.

Confirmation route IDs now route through the RmlUi popup command instead of a
flat route open. This covers Quit, Forfeit, Leave Match, and Tournament Replay
confirmations, preserving the modern popup presentation and the alert-style
open sound added in the previous refinement rounds.

## Implementation

- Added public runtime helpers for route-popup detection and popup route opens:
  - `UI_Rml_RouteIsPopup()`
  - `UI_Rml_OpenPopupRoute()`
- Updated the legacy client `pushmenu` command path to open known RmlUi routes
  directly, using the popup helper for confirmation routes.
- Added a cgame `pushmenu` bridge that recognizes all registered RmlUi route
  IDs and routes them through:
  - `ui_rml_runtime_open <route_id>` for normal menus.
  - `ui_rml_runtime_popup <route_id>` for confirmation popups.
- Extended the private cgame UI import/export contract from v4 to v5 with
  `InsertCommandString`.
- Wired the cgame UI insert helper to the engine `Cbuf_InsertText()` path and
  used it for the RmlUi `pushmenu` bridge. This makes launch/config command
  streams deterministic: `pushmenu options; wait; ui_rml_runtime_status; quit`
  now opens the RmlUi route before the following status/quit commands.

## Runtime Evidence

Focused staged OpenGL probes used the same core launch shape as the VSCode
OpenGL RmlUi profile: staged executable, `.install` basedir, OpenGL renderer,
`ui_rml_enable 1`, flushed logs, and developer mode.

The deterministic `pushmenu` probes recorded:

- `pushmenu options`
  - bridge: `ui_rml_runtime_open`
  - opened document: `ui/rml/shell/options.rml`
  - active runtime status: `route='options'`
  - menu open sound: `open`
  - menu music cue: `menu`
- `pushmenu quit_confirm`
  - bridge: `ui_rml_runtime_popup`
  - popup route request: `quit_confirm`
  - opened document: `ui/rml/shell/quit_confirm.rml`
  - active runtime status: `route='quit_confirm'`
  - menu open sound: `alert`
  - menu music cue: `menu`
- `pushmenu forfeit_confirm`
  - bridge: `ui_rml_runtime_popup`
  - popup route request: `forfeit_confirm`
  - opened document: `ui/rml/session/forfeit_confirm.rml`
  - active runtime status: `route='forfeit_confirm'`
  - menu open sound: `alert`
  - menu music cue: `menu`
- `pushmenu leave_match_confirm`
  - bridge: `ui_rml_runtime_popup`
  - popup route request: `leave_match_confirm`
  - opened document: `ui/rml/session/leave_match_confirm.rml`
  - active runtime status: `route='leave_match_confirm'`
  - menu open sound: `alert`
  - menu music cue: `menu`
- `pushmenu tourney_replay_confirm`
  - bridge: `ui_rml_runtime_popup`
  - popup route request: `tourney_replay_confirm`
  - opened document: `ui/rml/session/tourney_replay_confirm.rml`
  - active runtime status: `route='tourney_replay_confirm'`
  - menu open sound: `alert`
  - menu music cue: `menu`

Probe logs:

- `.install/basew/logs/round58_insert_pushmenu_options.log`
- `.install/basew/logs/round58_insert_pushmenu_quit_confirm.log`
- `.install/basew/logs/round58_insert_pushmenu_forfeit_confirm.log`
- `.install/basew/logs/round58_insert_pushmenu_leave_match_confirm.log`
- `.install/basew/logs/round58_insert_pushmenu_tourney_replay_confirm.log`

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\ui_smoke\check_rmlui_semantics.py`
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --install-dir .install --base-game basew`
- `python -m pytest tools\ui_smoke`

The full UI smoke pytest pass reported `224 passed`.

## Remaining Gaps

- Session confirmation routes still use controller-stub data-model markers for
  future live session binding. Their fallback confirmation copy and commands
  load through RmlUi today, but live sgame/cgame data-model parity remains a
  later `FR-09-T05`/`FR-09-T08`/`FR-09-T09` gate.
- Native Vulkan/RTX-vkpt RmlUi rendering is still pending and must remain
  native when implemented.
- Broad keyboard/controller navigation and full route-wide pixel clipping
  assertions remain pending parity evidence.
