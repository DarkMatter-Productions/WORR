# RmlUi Agent 1 Session Vote Controller Stubs Round 13

Date: 2026-07-02

Tasks: `FR-09-T08`, `FR-09-T09`, `FR-07-T01`, `FR-07-T02`, `DV-03-T07`, and `DV-04-T02`.

## Summary

Promoted the first session/vote batch from `starter` to `controller_stub`.
The promoted routes are:

- `vote_menu`
- `callvote_main`
- `callvote_ruleset`
- `callvote_timelimit`
- `callvote_scorelimit`
- `callvote_unlagged`
- `callvote_random`
- `callvote_map_flags`

Both `assets/ui/rml/session/routes.json` and
`tools/ui_smoke/rmlui_manifest.json` now agree on the promoted phase for those
eight routes. No other session routes were promoted.

## Contract Coverage

Added static `controller_contracts` metadata for the promoted routes:

- `vote_menu`: `command_action` via `session-multiplayer.mock.json`.
- `callvote_main`: `command_action` and `condition_state`.
- `callvote_ruleset`: `command_action`.
- `callvote_timelimit`: `command_action` and `condition_state`.
- `callvote_scorelimit`: `command_action`, `cvar_binding`, and
  `condition_state`.
- `callvote_unlagged`: `command_action` and `condition_state`.
- `callvote_random`: `command_action`.
- `callvote_map_flags`: `command_action` and `cvar_binding`.

The metadata covers every controller category inferred from the existing RML
attributes by `tools/ui_smoke/check_rmlui_controller_stub_coverage.py`.

Accepted Round 13 Worker 1 progression after this slice:

- Migration phases: `starter=18`, `controller_stub=36`, `runtime_stub=3`.
- Advanced routes: `39` (`68.4%`).
- Controller-contract refs: `101` refs across `39` routes.
- Session route metadata: `8` `controller_stub` routes and `17` `starter`
  routes.
- Parity-ready routes: `0`.

## Validation

Passed:

- `python tools\ui_smoke\check_rmlui_manifest.py`
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`
- `python tools\ui_smoke\check_rmlui_controller_fixtures.py`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py`
- `python tools\ui_smoke\check_rmlui_metadata_sync.py`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`

No focused pytest run was required for this slice because it only changed route
metadata and documentation, and the affected smoke validators passed directly.

## Caveat

This is static controller-stub metadata only. It does not add a live vote or
callvote controller, runtime RmlUi open path, screenshot/layout parity evidence,
or any user-visible RmlUi behavior.
