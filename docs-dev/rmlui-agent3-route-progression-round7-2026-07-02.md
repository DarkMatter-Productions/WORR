# RmlUi Agent 3 Route Progression Round 7

Date: 2026-07-02

Worker lane: Worker 3, settings route progression metadata

Task IDs: `FR-09-T04`, `FR-09-T05`, `FR-09-T06`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 7 promotes exactly five remaining low-risk Agent 4 settings routes from
`starter` to `controller_stub`: `multimonitor`, `railtrail`, `effects`,
`crosshair`, and `language`.

The promotion is based on static RML and route metadata only. The central smoke
manifest now carries matching phase metadata, and the Agent 4 shell/settings
manifest now references mock controller contracts for the promoted routes. No
route is promoted beyond `controller_stub`.

No replacement route was needed. Each requested route has stable document and
route IDs, static cvar evidence, footer command hooks, and shared navigation
evidence. `multimonitor` also has an explicit conditional-state expression, and
`effects` has a static `railtrail` route target.

## Promoted Routes

- `multimonitor`: static `r_monitor_mode`, `r_display`, and `r_borderless`
  cvar controls, footer `ui.back` and `ui.close` commands, plus
  `data-enable-if="r_monitor_mode=1"` on the display field.
- `railtrail`: static rail trail type, timing, width/radius, and color cvar
  controls, with footer navigation commands.
- `effects`: static effect, weapon bob, and explosion bitfield cvar controls,
  with footer commands and a `data-route-target="railtrail"` setup button.
- `crosshair`: static imagevalues-style `crosshair` selector metadata,
  crosshair/hit-feedback cvar controls, and footer commands.
- `language`: static `loc_language` select metadata and footer commands.

## Files Changed

- `tools/ui_smoke/rmlui_manifest.json`
  - Promotes only `multimonitor`, `railtrail`, `effects`, `crosshair`, and
    `language` to `migration_phase: "controller_stub"`.
- `assets/ui/rml/shell/routes.json`
  - Promotes the same five routes to `controller_stub`.
  - Adds route-level `controller_contracts` references using existing mock
    fixtures under `assets/ui/rml/contracts`.
  - Uses existing bridge categories: `cvar_binding`, `command_action`,
    `navigation`, and `condition_state` where the RML declares conditional
    metadata.

Route IDs and document paths were not changed.

## Task Mapping

`FR-09-T04`: Advances a small settings-only route batch under the task-tracked
RmlUi migration plan.

`FR-09-T05`: References existing mock controller/data-model contracts for the
five selected static settings routes.

`FR-09-T06`: Keeps Agent 4 shell/settings route metadata aligned with the
central smoke manifest phase metadata.

`FR-09-T09`: Extends migration phase reporting while keeping all new claims at
the `controller_stub` level.

`DV-03-T07`: Preserves smoke harness visibility into the updated route counts
used by validation tools.

`DV-07-T04`: Adds an explicit audit trail for the static route progression
without asserting live route behavior or parity.

## Validation

- `python tools/ui_smoke/check_rmlui_manifest.py`
  - Result: passed.
  - Summary: 57 routes, 57 required, 57 present, 0 pending.
  - Migration phases: `starter=42`, `controller_stub=15`.
- `python tools/ui_smoke/check_rmlui_route_contracts.py`
  - Result: passed.
  - `core`: 1 route, `starter=1`, 0 controller contract references.
  - `shell`: 23 routes, 44 controller contract references,
    `starter=8`, `controller_stub=15`.
  - `smoke`: 57 routes, 0 controller contract references,
    `starter=42`, `controller_stub=15`.
- `python tools/ui_smoke/check_rmlui_semantics.py`
  - Result: passed.
  - Summary: 57 documents checked, 52 route targets checked, 289 command
    elements checked, and 452 cvar references checked.
- `git diff --check -- tools/ui_smoke/rmlui_manifest.json assets/ui/rml/shell/routes.json docs-dev/rmlui-agent3-route-progression-round7-2026-07-02.md`
  - Result: completed with no output in this worktree.
  - Note: these owned paths are currently untracked, so a direct trailing
    whitespace scan was also run over the three files and found no issues.

## Caveats

This is a metadata-only route progression. It does not add live C++ controllers,
runtime route activation, renderer integration, screenshot/layout validation,
or parity evidence.
