# RmlUi Worker 5 Progress Runtime Stub Round 8

Date: 2026-07-02

Worker lane: Worker 5, progress-report automation output

Task IDs: `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 8 extends `tools/ui_smoke/report_rmlui_progress.py` so route progression
is visible as first-class progress data, including the new `runtime_stub`
counts now appearing in the central smoke manifest.

The existing text, markdown, JSON, and controller-contract facts are retained.
The new fields are additive so current grouped-count consumers can continue to
read `grouped_counts.migration_phase` while coordinators use the more explicit
phase progression facts.

## Reporter Behavior

Text output now includes a concise `Phase progression` line after the existing
`By migration_phase` line. Markdown output includes the same information as a
table row.

The phase progression summary always reports the canonical migration phases in
order:

- `starter`
- `controller_stub`
- `runtime_stub`
- `parity_pending`
- `parity_ready`

It also reports `advanced_routes`, which counts every route whose
`migration_phase` is not `starter`, and `advanced_percent`, rounded to one
decimal place.

## JSON Shape

The JSON payload adds top-level `phase_progression` and `routes_by_phase`
fields:

```json
{
  "phase_progression": {
    "starter": 42,
    "controller_stub": 12,
    "runtime_stub": 3,
    "parity_pending": 0,
    "parity_ready": 0,
    "advanced_routes": 15,
    "advanced_percent": 26.3
  },
  "routes_by_phase": [
    {
      "phase": "starter",
      "route_ids": ["addressbook"]
    },
    {
      "phase": "controller_stub",
      "route_ids": ["accessibility"]
    },
    {
      "phase": "runtime_stub",
      "route_ids": ["download_status", "game", "main"]
    }
  ]
}
```

`phase_progression` includes zero counts for absent canonical phases, so
dashboards do not need to special-case missing keys. `routes_by_phase` is a
stable ordered list: canonical phases are emitted in migration order, future
extra phases are appended alphabetically, and route IDs are sorted inside each
phase bucket.

## Task Mapping

`FR-09-T09`: Adds explicit migration phase progression facts for coordinators
tracking route movement toward runtime-backed and parity-ready RmlUi surfaces.

`DV-03-T07`: Keeps the UI smoke/progress automation harness current with
Round 8 runtime-stub metadata.

`DV-07-T04`: Improves regression and parity tracking by making advanced route
coverage and route IDs per phase machine-readable.

## Validation

Commands run:

```powershell
python -m pytest tools/ui_smoke/test_report_rmlui_progress.py
python tools/ui_smoke/report_rmlui_progress.py --format json
python tools/ui_smoke/report_rmlui_progress.py --format markdown
git diff --check -- tools/ui_smoke/report_rmlui_progress.py tools/ui_smoke/test_report_rmlui_progress.py docs-dev/rmlui-agent5-progress-runtime-stub-round8-2026-07-02.md
```

Results:

- Pytest passed: `7` tests.
- Live JSON reported `57` total smoke routes, `42` starter routes, `12`
  controller-stub routes, `3` runtime-stub routes, `15` advanced routes, and
  `26.3` advanced percent.
- Live JSON emitted sorted runtime-stub route IDs:
  `download_status`, `game`, and `main`.
- Live markdown emitted the same phase progression row.
- `git diff --check` returned clean for the owned paths. The owned files are
  currently untracked in this worktree, so a direct trailing-whitespace scan
  was also run and found no matches.

## Caveats

This work only reports phase progression facts. It does not add new RmlUi
runtime routes, controller implementations, renderer smoke screenshots, or
parity validation.
