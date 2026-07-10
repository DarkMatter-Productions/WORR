# RmlUi Parallel Round 3 Integration

Date: 2026-07-02

Tasks: `FR-09-T01`, `FR-09-T05`, `FR-09-T06`, `FR-09-T07`,
`FR-09-T08`, `FR-09-T09`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`,
`DV-07-T04`, `FR-07-T01`, `FR-07-T02`

## Summary

This integration pass collected the third five-agent RmlUi implementation
round. The round completed source-route scaffold coverage for every tracked
Wave A, Wave B, and Wave C migration surface and strengthened the validation
tools. It still does not implement the RmlUi dependency, C++ runtime, native
renderer bridge, live data controllers, parity cutover, or legacy JSON removal.

## Integrated Work

- Agent 1 hardened `tools/ui_smoke/check_rmlui_manifest.py` so present route
  documents are parsed as XML-ish RML and local `href` imports are resolved and
  checked.
- Agent 2 added `tools/ui_smoke/check_rmlui_route_contracts.py` and tests for
  route-manifest shape and required document presence.
- Agent 3 added the remaining Agent 4-owned settings and single-player
  starter documents: `railtrail`, `effects`, `crosshair`, `gameflags`,
  `startserver`, `skill_select`, `loadgame`, and `savegame`.
- Agent 4 added standard session, vote, and admin starter documents:
  `dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, `dm_matchinfo`,
  `callvote_timelimit`, `callvote_scorelimit`, `callvote_unlagged`,
  `callvote_random`, `callvote_map_flags`, `forfeit_confirm`, `admin_menu`,
  and `admin_commands`.
- Agent 5 added tournament, replay, map selector, and match stats starter
  documents: `tourney_info`, `tourney_mapchoices`, `tourney_veto`,
  `tourney_replay_confirm`, `map_selector`, and `match_stats`.

## Coordinator Fixes

- Marked the final `27` central smoke manifest routes as `required_now` with
  `starter_round3` status.
- Updated `docs-dev/plans/rmlui-ui-migration-roadmap.md` so task status shows
  full source-route scaffold coverage while keeping runtime, renderer, live
  controller, parity, and legacy-removal gates open.
- Updated the canonical strategic roadmap with the new full source-route
  coverage and validation evidence.

## Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Passed with `57` routes, `57` required routes, `57` present documents,
    `0` pending documents, `151` parsed RML/import files, and `213` local
    `href` imports checked.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Passed for core, shell, and smoke manifests. The central smoke manifest
    reported `57/57` required documents present.
- `python -m pytest tools/ui_smoke/test_check_rmlui_manifest.py
  tools/ui_smoke/test_check_rmlui_route_contracts.py`
  - Passed: `10` tests.

## Remaining Gate Work

- Gate G0 still needs final runtime-owner sign-off against the full active UI
  entry-point inventory.
- Gate G1 still needs RmlUi dependency integration, C++ runtime bootstrap,
  native renderer bridges, and normal build refresh validation.
- Gate G2 still needs live cvar, command, condition, keybind, list/table,
  image-grid, preview, and save/load controllers.
- Gate G3 still needs runtime route opening and parity checks for the complete
  starter document set.
- Gate G4 remains blocked until runtime automation, manual parity validation,
  and legacy JSON removal/archive decisions are complete.
