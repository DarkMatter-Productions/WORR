# RmlUi Worker 5 Progress Contracts Round 7

Date: 2026-07-02

Worker lane: Worker 5, progress-report automation output

Task IDs: `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 7 extends `tools/ui_smoke/report_rmlui_progress.py` so the progress
report can read shell route metadata from `assets/ui/rml/shell/routes.json`
and expose controller-contract progress facts alongside the existing manifest
document counts.

The smoke manifest remains the source of route/document progress. Shell route
metadata is only used for the controller contract summary, and the reporter
falls back to zero/empty contract facts when the shell routes file is absent.

## Reporter Behavior

The reporter now accepts:

```powershell
python tools/ui_smoke/report_rmlui_progress.py --shell-routes assets/ui/rml/shell/routes.json
```

When `--shell-routes` is omitted, the reporter checks
`assets/ui/rml/shell/routes.json` relative to the repo root and uses it when
present. Missing shell route metadata does not fail text, markdown, or JSON
report generation.

Malformed shell route metadata still fails the report, because bad
`controller_contracts` structure would otherwise produce misleading automation
facts.

## JSON Shape

The JSON payload keeps the Round 6 fields and adds a top-level
`controller_contracts` object:

```json
{
  "controller_contracts": {
    "total_references": 44,
    "routes_with_contracts": 15,
    "by_category": {
      "command_action": 15,
      "condition_state": 3,
      "cvar_binding": 12,
      "navigation": 14
    },
    "by_migration_phase": {
      "controller_stub": 15
    }
  }
}
```

`by_category` counts individual controller-contract references. Because
`migration_phase` is route-level metadata, `by_migration_phase` counts shell
routes that declare at least one controller contract.

Text and markdown output also include a concise controller-contract summary
line/table row while retaining the existing route, document, and grouped-count
fields.

## Task Mapping

`FR-09-T05`: Adds controller-contract progress visibility for shell route
metadata used by migration coordination.

`FR-09-T09`: Extends the machine-readable progress report with contract facts
that can be consumed by dashboards, route checks, and coordinator scripts.

`DV-03-T07`: Keeps the UI smoke/progress automation harness current with the
new route-controller contract metadata.

`DV-07-T04`: Improves regression and parity tracking by making controller
coverage totals visible without parsing route manifests directly.

## Validation

Commands run:

```powershell
python -m pytest tools/ui_smoke/test_report_rmlui_progress.py
python tools/ui_smoke/report_rmlui_progress.py --format json
python tools/ui_smoke/report_rmlui_progress.py --format markdown
git diff --check -- tools/ui_smoke/report_rmlui_progress.py tools/ui_smoke/test_report_rmlui_progress.py docs-dev/rmlui-agent5-progress-contracts-round7-2026-07-02.md
```

Results:

- Pytest passed: `6` tests.
- Live JSON reported `57` total smoke routes, `57` present documents, `0`
  missing documents, `57` required documents present, and `0` required
  documents missing.
- Live controller contract facts reported `44` references across `15` shell
  routes, with category counts `command_action=15`, `condition_state=3`,
  `cvar_binding=12`, and `navigation=14`.
- Live contract migration-phase coverage reported `controller_stub=15`.
- Live markdown emitted the same controller-contract summary as a compact table
  row.
- `git diff --check` passed for the owned files.

## Caveats

This work only reports controller-contract metadata. It does not add live RmlUi
controllers, runtime route dispatch, renderer smoke screenshots, or parity
validation.
