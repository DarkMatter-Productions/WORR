# RmlUi Agent 2 Route Progression Round 5

Date: 2026-07-02

Worker lane: Worker 2, route progression metadata

Task IDs: `FR-09-T05`, `FR-09-T06`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 5 promotes only the safest Agent 4 entry/settings routes from
`starter` to `controller_stub`: `main`, `game`, `options`, `video`, and
`download_status`.

The promotion is based on static RML/controller evidence only. These routes now
declare mock controller contract references in `assets/ui/rml/shell/routes.json`
and matching phase metadata in the central smoke manifest. No route claims
`runtime_stub`, `parity_pending`, or `parity_ready`.

## Selection Rationale

- `main`: static `data-command` and `data-route-target` buttons map cleanly to
  navigation and command contracts.
- `game`: static navigation and command buttons are present, with
  `data-show-if` and `data-enable-if` conditions for deathmatch/save/load
  controls.
- `options`: static settings hub route targets and command buttons provide
  narrow navigation/command evidence.
- `video`: static `data-cvar`, `data-bind-cvar`, and footer command controls
  match the cvar, command, and navigation mock contracts.
- `download_status`: static `ui_download_*` cvar labels/bindings,
  `data-show-if` conditions, and `download_cancel` command support a
  controller stub. Auto-open/auto-close runtime behavior remains pending.

`core.runtime_smoke` stays `starter` because it is a load-only smoke route and
does not declare explicit controller/data-model evidence.

## Files Changed

- `tools/ui_smoke/rmlui_manifest.json`
  - Promotes only `main`, `game`, `options`, `video`, and `download_status` to
    `migration_phase: "controller_stub"`.
  - Keeps all other central smoke routes at `starter`.
- `assets/ui/rml/shell/routes.json`
  - Promotes the same five routes to `controller_stub`.
  - Adds route-level `controller_contracts` refs using mock fixture filenames
    under `assets/ui/rml/contracts`.
  - Uses existing schema category IDs: `navigation`, `command_action`,
    `condition_state`, and `cvar_binding`.
- `assets/ui/rml/core/routes.json`
  - Reviewed only; no edits. `core.runtime_smoke` remains `starter`.

## Task Mapping

`FR-09-T05`: References the mock controller/data-model contract layer from the
first safe route metadata consumers.

`FR-09-T06`: Advances shell/settings route metadata without changing route IDs,
documents, or ownership.

`FR-09-T09`: Extends migration phase reporting beyond the all-starter baseline
while keeping the claims conservative.

`DV-03-T07`: Keeps the smoke harness manifest aligned with route progression
so automation can report controller-stub coverage.

`DV-07-T04`: Leaves an explicit parity trail and avoids any runtime or parity
claim before live validation exists.

## Validation

- `python tools\ui_smoke\check_rmlui_manifest.py`
  - Result: passed.
  - Summary: 57 routes, 57 required, 57 present, 0 pending.
  - Migration phases: `starter=52`, `controller_stub=5`.
- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Result: passed.
  - `core`: 1 route, `starter=1`.
  - `shell`: 23 routes, `starter=18`, `controller_stub=5`.
  - `smoke`: 57 routes, `starter=52`, `controller_stub=5`.
