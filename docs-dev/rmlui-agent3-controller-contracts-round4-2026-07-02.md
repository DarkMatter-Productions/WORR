# RmlUi Agent 3 Controller Contracts Round 4

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T07`, `FR-09-T08`, `DV-04-T02`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Scope

This round expands the mock-first data/controller contract layer for the first
live-bridge targets. It intentionally does not add or modify C++ runtime code.
The new fixtures describe the bridge-facing event names, ownership boundaries,
sample data fields, and component anchors that future live controllers can
consume without forcing a final ABI shape.

The work stays in the Agent 3 contract lane and avoids the manifest checker
owned by parallel integration work. The route contract schema remains
permissive; it only gains category metadata and an optional manifest hook for
routes that later want to declare which controller contracts they consume.

## Changed Files

- `assets/ui/rml/contracts/cvar-binding.mock.json`
- `assets/ui/rml/contracts/command-action.mock.json`
- `assets/ui/rml/contracts/condition-state.mock.json`
- `assets/ui/rml/contracts/navigation.mock.json`
- `assets/ui/rml/contracts/list-provider.mock.json`
- `assets/ui/rml/contracts/route-contract.schema.json`
- `docs-dev/rmlui-agent3-controller-contracts-round4-2026-07-02.md`

## Contract Coverage

- `cvar-binding.mock.json` defines read/write/change events for cvar-backed
  controls, including dirty and restart state for representative video cvars.
- `command-action.mock.json` defines command dispatch, completion, failure,
  enabled-state, selection, and confirmation metadata for shared command
  buttons.
- `condition-state.mock.json` defines conditional visibility and disabled-state
  evaluation from cvars, session state, and list selection state.
- `navigation.mock.json` defines route stack, push, back, replace, close, and
  legacy command fallback metadata for menu navigation.
- `list-provider.mock.json` defines list/table refresh, query, sort,
  pagination, row value, selection, and activation metadata for utility data
  providers.

## Schema Notes

`route-contract.schema.json` now lists the five controller categories under a
top-level `controller_categories` registry and allows route manifests to add an
optional `controller_contracts` array. The schema keeps
`additionalProperties: true` on route and controller contract objects so future
real data can add provider-specific fields without requiring a checker update.

## Validation

- JSON parse validation passed for every `assets/ui/rml/contracts/*.json` file
  using `python -m json.tool`, including the five new fixtures and
  `route-contract.schema.json`.
- Route contract audit passed with `python tools/ui_smoke/check_rmlui_route_contracts.py`.
  The audit reported 1 core route, 23 shell routes, and 57 smoke routes with
  all 57 required smoke documents present.
- Route contract checker tests passed with
  `python -m pytest tools/ui_smoke/test_check_rmlui_route_contracts.py`
  (`3 passed`).
- No checker behavior changed, so no test file updates were needed.

## Handoff Notes

- Future Agent 3 runtime work should map the fixture categories to live bridge
  controllers before wiring route documents to engine state.
- Agent 4 and Agent 5 routes can reference these contracts from future route
  metadata through `controller_contracts` without changing route validation.
- `DV-04-T02` ownership remains narrow: presentation assets own UI shape,
  client/cgame providers own data, and the client command bridge owns command
  dispatch.
