# RmlUi Agent 2 Session Route Metadata Round 12

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`,
`FR-07-T01`, `FR-07-T02`, `FR-03-T08`, `DV-03-T07`, `DV-04-T02`

Roadmap: `docs-dev/plans/rmlui-ui-migration-roadmap.md`

## Summary

This Worker 2 slice adds starter-level route ownership metadata for Agent
5-owned multiplayer/session documents that were previously covered by the
central smoke manifest but not by dedicated feature `routes.json` files.

The new metadata keeps all covered routes at `migration_phase: starter`.
It does not edit `tools/ui_smoke/rmlui_manifest.json`, does not promote any
route to `controller_stub`, and does not claim live session behavior, runtime
open-path evidence, screenshots, or parity.

## Changed Files

- `assets/ui/rml/multiplayer/routes.json`
- `assets/ui/rml/session/routes.json`
- `docs-dev/rmlui-agent2-session-route-metadata-round12-2026-07-02.md`

## Route Coverage

- `assets/ui/rml/multiplayer/routes.json`: `1` route, `multiplayer`.
- `assets/ui/rml/session/routes.json`: `25` routes:
  `dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, `dm_matchinfo`,
  `callvote_main`, `callvote_ruleset`, `callvote_timelimit`,
  `callvote_scorelimit`, `callvote_unlagged`, `callvote_random`,
  `callvote_map_flags`, `mymap_main`, `mymap_flags`, `forfeit_confirm`,
  `leave_match_confirm`, `admin_menu`, `admin_commands`, `tourney_info`,
  `tourney_mapchoices`, `tourney_veto`, `tourney_replay_confirm`,
  `vote_menu`, `map_selector`, and `match_stats`.

## Contract Notes

- The metadata mirrors the existing Agent 5 owner token
  `agent5-rich-tools-session-validation` from the central smoke manifest.
- `data_models` entries are copied only from static `data-model` attributes
  already present in the RML documents.
- `controller_contracts` are intentionally omitted. These routes still need a
  later controller-stub lane with complete static evidence and fixtures before
  they should be promoted.
- The dedicated feature manifests give the route-contract and phase-consistency
  tools clearer ownership/progression evidence while preserving the central
  starter phase baseline.

## Validation

Requested validation commands for this worker:

```powershell
python tools\ui_smoke\check_rmlui_route_contracts.py
python tools\ui_smoke\check_rmlui_phase_consistency.py
python tools\ui_smoke\check_rmlui_navigation_graph.py
python tools\ui_smoke\check_rmlui_data_model_inventory.py
python -m pytest tools\ui_smoke\test_check_rmlui_route_contracts.py tools\ui_smoke\test_check_rmlui_phase_consistency.py tools\ui_smoke\test_check_rmlui_navigation_graph.py tools\ui_smoke\test_check_rmlui_data_model_inventory.py
git diff --check -- assets/ui/rml/multiplayer/routes.json assets/ui/rml/session/routes.json docs-dev/rmlui-agent2-session-route-metadata-round12-2026-07-02.md
```

Result: passed.

- Route contracts discovered `assets/ui/rml/multiplayer/routes.json` with
  `1` starter route and `assets/ui/rml/session/routes.json` with `25` starter
  routes.
- Phase consistency checked `57` central smoke routes and `5` route metadata
  files with no missing evidence.
- Navigation graph and data-model inventory remained clean for the central
  smoke manifest.
- Focused pytest passed: `23 passed`.
- `git diff --check --` for the owned files returned clean.

## Handoff Notes

- Later Worker 5/session-controller work can use the new route metadata as the
  starter ownership source for vote, tournament, MyMap, map selector, and match
  stats routes.
- Controller-stub promotion should add route-local `controller_contracts` only
  after cvar binding, command dispatch, condition, list, and command-cvar
  evidence is complete enough for fixture-backed validation.
- No central phase promotion or live session behavior is claimed here.
