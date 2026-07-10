# RmlUi Worker 5 Progress JSON Round 6

Date: 2026-07-02

Worker lane: Worker 5, progress-report automation output

Task IDs: `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 6 extends `tools/ui_smoke/report_rmlui_progress.py` with a
machine-readable `--format json` mode. The default text report and existing
`--format markdown` table are intentionally unchanged.

The JSON output is for automation that needs stable progress facts without
parsing terminal prose. It reports the manifest path, total route count,
document presence totals, required-document presence totals, and grouped counts
for `wave`, `owner`, `status`, and `migration_phase`.

## JSON Shape

The new payload uses this top-level structure:

```json
{
  "manifest_path": "tools/ui_smoke/rmlui_manifest.json",
  "total_routes": 57,
  "documents": {
    "present": 57,
    "missing": 0
  },
  "required_documents": {
    "present": 57,
    "missing": 0,
    "total": 57
  },
  "grouped_counts": {
    "wave": {},
    "owner": {},
    "status": {},
    "migration_phase": {}
  }
}
```

Grouped count ordering follows the existing report ordering. General groups
sort keys alphabetically, while `migration_phase` keeps the migration phase
progression order used by the text and markdown reports.

## Task Mapping

`FR-09-T09`: Adds migration-specific progress facts in a format that can be
consumed by route progression checks, dashboards, or coordinator scripts.

`DV-03-T07`: Extends the UI automation harness with structured output while
retaining the existing human-readable reports.

`DV-07-T04`: Improves regression/parity tracking evidence by making the current
route, document, and migration-phase counts directly machine-readable.

## Validation

Commands run:

```powershell
python -m pytest tools/ui_smoke/test_report_rmlui_progress.py
python tools/ui_smoke/report_rmlui_progress.py --format json
python tools/ui_smoke/report_rmlui_progress.py --format markdown
```

Results:

- pytest passed: `4` tests.
- The final integrated live JSON report read
  `tools/ui_smoke/rmlui_manifest.json` and reported `57` total routes, `57`
  present documents, `0` missing documents, `57` required documents present,
  and `0` required documents missing.
- Live grouped JSON counts were waves `A=21`, `B=11`, `C=25`; owners
  `agent4-shell-settings-singleplayer=23` and
  `agent5-rich-tools-session-validation=34`; statuses `starter=10`,
  `starter_round2=20`, `starter_round3=27`; and migration phases `starter=47`,
  `controller_stub=10`.
- The live markdown report emitted the same final Round 6 counts as a compact
  table.

## Caveats

This work only adds structured report output. It does not add runtime
navigation, renderer smoke, screenshot capture, live RmlUi controllers, or
parity validation.
