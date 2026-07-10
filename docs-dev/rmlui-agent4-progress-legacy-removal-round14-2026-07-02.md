# RmlUi Agent 4 Progress Legacy Removal Round 14

Date: 2026-07-02

Worker: Round 14 Worker 4

Task IDs: `FR-09-T09`, `FR-09-T10`, `DV-03-T07`, `DV-04-T02`, `DV-07-T04`

## Summary

This round integrates the planning-only legacy-removal inventory into `tools/ui_smoke/report_rmlui_progress.py`. The progress reporter now includes a `legacy_removal` summary in text, markdown, and JSON output when `tools/ui_smoke/check_rmlui_legacy_removal.py` is available.

The summary follows the existing optional checker pattern used by the data-model, condition, metadata, event, and a11y inventories:

- missing checker: report remains usable and marks the section `unavailable`;
- checker/API failure: report remains usable and marks the section `error`;
- `--no-inventory-summary`: skips legacy-removal output along with the other optional inventories.

## Reported Fields

The new summary surfaces:

- `items_checked`;
- `categories_checked`;
- `status_counts`;
- `category_counts`;
- `missing_task_ids`;
- `ready_or_complete_items` count and list;
- parity gate state, including open/closed, `parity_ready_routes`, pending evidence, closed reasons, and gate errors;
- checker errors.

## Current Static Baseline

The current progress report shows:

- route docs: `57/57`;
- migration phases: `starter=12`, `controller_stub=42`, `runtime_stub=3`;
- advanced routes: `45/57` (`78.9%`);
- controller contracts: `117` refs across `45` routes;
- legacy-removal inventory: `6` items, `5` categories;
- legacy-removal statuses: `blocked=4`, `pending=2`, `ready=0`, `complete=0`;
- missing legacy-removal task IDs: `0`;
- ready/complete legacy-removal items: `0`;
- parity gate: closed, `parity_ready_routes=0`.

## Validation

Commands run:

- `python tools\ui_smoke\report_rmlui_progress.py`
- `python tools\ui_smoke\report_rmlui_progress.py --format markdown`
- `python tools\ui_smoke\report_rmlui_progress.py --format json`
- `python -m pytest tools\ui_smoke\test_report_rmlui_progress.py`

Focused tests passed with `18 passed`.

## Caveats

This is static progress-reporting work only. It does not delete legacy JSON/menu surfaces, open the parity gate, add live RmlUi controllers, provide runtime navigation cutover, add renderer-specific smoke evidence, or prove screenshot/input parity.
