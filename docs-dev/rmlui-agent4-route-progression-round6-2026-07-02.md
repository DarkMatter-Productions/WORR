# RmlUi Agent 4 Route Progression Round 6

Date: 2026-07-02

Worker lane: Worker 4, shell/settings route progression metadata

Task IDs: `FR-09-T04`, `FR-09-T05`, `FR-09-T06`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 6 conservatively promotes exactly five additional Agent 4 settings routes
from `starter` to `controller_stub`: `performance`, `accessibility`, `sound`,
`screen`, and `input`.

The promotion is based on static RML/controller metadata only. These routes now
declare mock controller contract references in `assets/ui/rml/shell/routes.json`
and matching phase metadata in `tools/ui_smoke/rmlui_manifest.json`. No route
claims `runtime_stub`, `parity_pending`, or `parity_ready`.

After this pass, the `controller_stub` route set is the Round 5 baseline
(`main`, `game`, `options`, `video`, `download_status`) plus the five Round 6
settings routes listed above. All other smoke and shell routes remain at
`starter`.

## Selection Rationale

- `performance`: static `data-cvar` controls cover `cl_maxfps`, `r_maxfps`,
  `cl_predict`, `cl_footsteps`, `cl_warn_on_fps_rounding`, and `cl_async`, with
  footer `ui.back` and `ui.close` commands.
- `accessibility`: static `data-cvar` controls cover `ui_high_visibility_text`
  and `ui_text_typeface`, with footer navigation commands.
- `sound`: static `data-cvar` and `data-bind-cvar` controls cover sound engine,
  latency, volume, music, ambient, underwater, and chat beep cvars.
- `screen`: static `data-cvar` and `data-bind-cvar` controls cover HUD,
  console, and scale cvars; the crosshair setup button has a stable
  `data-route-target="crosshair"` route reference while `crosshair` itself
  remains `starter`.
- `input`: static `data-cvar` controls cover mouse sensitivity, autosensitivity,
  filtering, freelook, and always-run cvars; key binding pages remain outside
  this promotion.

## Files Changed

- `tools/ui_smoke/rmlui_manifest.json`
  - Promotes only `performance`, `accessibility`, `sound`, `screen`, and
    `input` to `migration_phase: "controller_stub"`.
- `assets/ui/rml/shell/routes.json`
  - Promotes the same five routes to `controller_stub`.
  - Adds route-level `controller_contracts` refs using existing mock fixture
    filenames under `assets/ui/rml/contracts`.
  - Uses existing schema category IDs: `cvar_binding`, `command_action`, and
    `navigation`.

Route IDs and document paths were not changed.

## Task Mapping

`FR-09-T04`: Advances a second small, task-tracked shell/settings metadata
batch without expanding into higher-risk route families.

`FR-09-T05`: Adds mock controller contract references for the selected static
settings routes.

`FR-09-T06`: Keeps shell/settings route metadata aligned between the Agent 4
route manifest and the central smoke manifest.

`FR-09-T09`: Updates migration phase coverage while keeping all claims at the
`controller_stub` level.

`DV-03-T07`: Preserves smoke harness visibility into the route progression
counts used by validation tools.

`DV-07-T04`: Leaves an explicit audit trail and avoids any runtime or parity
claim before live validation exists.

## Validation

- `python tools/ui_smoke/check_rmlui_manifest.py`
  - Result: passed.
  - Summary: 57 routes, 57 required, 57 present, 0 pending.
  - Migration phases: `starter=47`, `controller_stub=10`.
- `python tools/ui_smoke/check_rmlui_route_contracts.py`
  - Result: passed.
  - `core`: 1 route, `starter=1`.
  - `shell`: 23 routes, 28 controller contract references,
    `starter=13`, `controller_stub=10`.
  - `smoke`: 57 routes, `starter=47`, `controller_stub=10`.
- `python tools/ui_smoke/check_rmlui_semantics.py`
  - Result: passed.
  - Summary: 57 documents checked, 52 route targets checked, 289 command
    elements checked, and 452 cvar references checked.
- `git diff --check -- tools/ui_smoke/rmlui_manifest.json assets/ui/rml/shell/routes.json docs-dev/rmlui-agent4-route-progression-round6-2026-07-02.md`
  - Result: passed with no output.

## Caveats

This is a metadata-only progression. It does not add live C++ controllers,
runtime route activation, renderer integration, screenshot/layout validation,
or parity evidence.
