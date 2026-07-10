# RmlUi Round 10 Agent 4 Progress/Parity Reporting

Date: 2026-07-02

Tasks: `FR-09-T09`, `FR-09-T10`, `DV-03-T07`, `DV-07-T04`

## Scope

This round extends `tools/ui_smoke/report_rmlui_progress.py` so the migration
progress report also summarizes the parity checklist manifest when
`tools/ui_smoke/rmlui_parity_manifest.json` is present.

This is progress/checklist reporting only. No renderer evidence, screenshot
evidence, input evidence, live controller behavior, runtime navigation, or
legacy-removal evidence is newly completed by this change.

## Implementation

- Added optional parity checklist loading to the progress reporter.
- Added `--parity-manifest <path>` so tests and dashboards can point at an
  explicit checklist file.
- Added `--no-parity-summary` for callers that need the previous report shape.
- Reused `check_rmlui_parity_manifest.py` validation semantics before reporting
  checklist counts.
- Added text, markdown, and JSON output for:
  - categories checked
  - `parity_ready` route count
  - pending counts by category
  - complete counts by category
- Kept missing parity manifests non-fatal; reports still run and simply omit
  the optional parity summary.

## Live Baseline

The current live progress report includes:

- Routes: `57`
- Migration phases: `starter=34`, `controller_stub=20`, `runtime_stub=3`
- Advanced routes: `23` (`40.4%`)
- Controller contracts: `65` references across `23` routes
- Parity checklist: `57` routes, `9` categories, `0` `parity_ready` routes

Current parity checklist category totals:

- Pending: `document_load=0`, `navigation=57`, `controller_bindings=34`,
  `renderer_open_gl=57`, `renderer_vulkan=57`, `renderer_rtx_vkpt=57`,
  `screenshot_layout=57`, `input_escape_back=57`, `legacy_fallback=54`
- Complete: `document_load=57`, `navigation=0`, `controller_bindings=23`,
  `renderer_open_gl=0`, `renderer_vulkan=0`, `renderer_rtx_vkpt=0`,
  `screenshot_layout=0`, `input_escape_back=0`, `legacy_fallback=3`

These complete counts reflect existing static checklist defaults and earlier
guarded fallback/controller-contract scaffolding. They are not new parity
evidence from this reporting change.

## Validation

- `python -m pytest tools/ui_smoke/test_report_rmlui_progress.py`
  - Passed: `11` tests
- `python -m py_compile tools\ui_smoke\report_rmlui_progress.py tools\ui_smoke\test_report_rmlui_progress.py`
  - Passed
- `python tools\ui_smoke\report_rmlui_progress.py`
  - Passed and emitted the parity checklist summary
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
  - Passed and emitted the parity checklist table row
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
  - Passed and emitted the `parity_checklist` object
