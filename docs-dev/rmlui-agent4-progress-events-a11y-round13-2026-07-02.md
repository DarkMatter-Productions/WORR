# RmlUi Round 13 Worker 4 Progress Events/A11y Integration

Task IDs: `FR-09-T04`, `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Worker 4 extended the RmlUi progress reporter so Round 13 event and
accessibility inventories can be surfaced as soon as their checker files are
available.

Owned files changed:

- `tools/ui_smoke/report_rmlui_progress.py`
- `tools/ui_smoke/test_report_rmlui_progress.py`
- `docs-dev/rmlui-agent4-progress-events-a11y-round13-2026-07-02.md`

No route documents, route metadata, package staging output, runtime code,
roadmap/proposal files, commits, or git index state were changed by this
worker.

## Implementation

`report_rmlui_progress.py` now optionally collects and reports:

- `event_inventory`, backed by `check_rmlui_event_inventory.py` when present.
- `a11y_inventory`, backed by `check_rmlui_a11y_inventory.py` when present.

Both integrations mirror the existing optional inventory pattern:

- Missing checker files produce `status=unavailable` summaries instead of
  failing the report.
- Checker API exceptions, subprocess failures, invalid JSON, or checker-level
  errors produce `status=error` summaries while the progress report remains
  usable.
- Compatible in-process checker APIs are preferred, with deterministic
  `--format json` subprocess fallback.
- Text, Markdown, and JSON output include the new summaries behind the existing
  `--no-inventory-summary` switch.

The coercion layer accepts several likely field aliases so Worker 2 and Worker
3 can land the checker implementations independently without forcing a narrow
payload shape on the first pass.

## Reported Fields

Event inventory summary:

- Routes/documents checked
- Missing documents
- Total static event references
- Routes with event hooks
- Unique event names/tokens
- Malformed event attributes
- Status/errors

A11y inventory summary:

- Routes/documents checked
- Missing documents
- Total accessibility references
- Routes with accessibility hooks
- Labeled controls
- Focusable controls
- Missing labels
- Missing focus targets
- Malformed accessibility attributes
- Status/errors

## Limits

This is reporting integration only. It does not add the event inventory checker,
the accessibility inventory checker, live event dispatch, runtime accessibility
services, route promotion, or screenshot/layout parity evidence.
