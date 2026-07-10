# RmlUi Agent 1 Session Lobby Controller Stubs Round 14

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `FR-07-T01`, `FR-07-T02`, `FR-03-T08`, `DV-03-T07`, and `DV-04-T02`.

## Summary

Promoted the next session/multiplayer starter batch from `starter` to
`controller_stub` metadata:

- `multiplayer`
- `dm_welcome`
- `dm_join`
- `join`
- `dm_hostinfo`
- `dm_matchinfo`

Both `tools/ui_smoke/rmlui_manifest.json` and the feature route metadata now
agree on the promoted phase. No other routes were promoted in this slice.

## Contract Coverage

Added static `controller_contracts` metadata for the promoted routes:

- `multiplayer`: `command_action` and static `navigation` route-open metadata.
- `dm_welcome`: `command_action` and `condition_state`.
- `dm_join`: `navigation`, `command_action`, `cvar_binding`, and
  `condition_state`.
- `join`: `navigation`, `command_action`, `cvar_binding`, and
  `condition_state`.
- `dm_hostinfo`: `command_action` and `condition_state`.
- `dm_matchinfo`: `command_action` and `condition_state`.

The metadata covers every controller category inferred from the existing RML
attributes by `tools/ui_smoke/check_rmlui_controller_stub_coverage.py`. The
`multiplayer` route also records a static navigation contract for its
`ui.open_route` plus `data-route` buttons, while the runtime fallback remains
the legacy menu path.

Accepted Round 14 Worker 1 progression after this slice:

- Migration phases: `starter=12`, `controller_stub=42`, `runtime_stub=3`.
- Advanced routes: `45/57` (`78.9%`).
- Controller-contract refs: `117` refs across `45` routes.
- Feature metadata: `multiplayer=1` `controller_stub`; `session=13`
  `controller_stub` routes and `12` `starter` routes.
- Parity-ready routes: `0`.

## Validation

Passed:

- `python tools\ui_smoke\check_rmlui_manifest.py`
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
- `python tools\ui_smoke\check_rmlui_controller_stub_coverage.py`
- `python tools\ui_smoke\check_rmlui_controller_fixtures.py`
- `python tools\ui_smoke\check_rmlui_metadata_sync.py`
- `python tools\ui_smoke\check_rmlui_phase_consistency.py`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
- `git diff --check -- assets/ui/rml/multiplayer/routes.json assets/ui/rml/session/routes.json tools/ui_smoke/rmlui_manifest.json docs-dev/rmlui-agent1-session-lobby-controller-stubs-round14-2026-07-02.md`

## Caveat

This is controller-stub metadata only. It does not add live multiplayer,
lobby, host-info, or match-info controllers; a runtime RmlUi open path;
screenshot/layout parity evidence; or any user-visible RmlUi behavior. The
legacy menu fallback remains authoritative.
