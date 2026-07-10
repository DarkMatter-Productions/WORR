# RmlUi Round 12 Worker 5 Progress Guardrails

Task IDs: FR-09-T09, DV-07-T04

## Scope

Worker 5 extended the RmlUi progress report so Round 12 static guardrails are visible alongside the existing phase, controller, parity, command, cvar, and data-model summaries.

Owned files changed:

- `tools/ui_smoke/report_rmlui_progress.py`
- `tools/ui_smoke/test_report_rmlui_progress.py`
- `docs-dev/rmlui-agent5-progress-guardrails-round12-2026-07-02.md`

No route manifests, checker ownership files, roadmap/proposal documents, staging files, commits, or git index state were changed by this worker.

## Implementation

`report_rmlui_progress.py` now optionally collects and reports:

- `condition_inventory`, backed by `check_rmlui_condition_inventory.py` when present.
- `metadata_sync`, backed by `check_rmlui_metadata_sync.py` when present.

Both integrations prefer an in-process checker API when a compatible function is exposed, then fall back to deterministic `--format json` subprocess execution. If either checker is absent or fails, the progress report remains usable and surfaces `available`, `ok`, `status`, and `errors` in JSON plus concise text/Markdown rows.

The summaries stay behind the existing `--no-inventory-summary` switch, matching the Round 11 behavior for command, cvar, and data-model inventory summaries.

Subprocess error handling was also tightened so a checker that exits non-zero while still emitting JSON does not cause the entire JSON payload to be duplicated inside the progress report error list.

## Reported Fields

Condition inventory summary:

- Routes/documents checked
- Missing documents
- Total condition references
- Routes with condition hooks
- Unique condition expressions
- Unique condition tokens
- Malformed condition count
- Status/errors

Metadata sync summary:

- Metadata files
- Metadata routes
- Matched routes
- Central routes without feature metadata
- Advanced routes missing feature metadata
- Phase mismatch count
- Document mismatch count
- Duplicate metadata route count
- Status/errors

## Validation

Focused test coverage:

```text
python -m pytest tools\ui_smoke\test_report_rmlui_progress.py
16 passed
```

Live progress report validation:

```text
python tools\ui_smoke\report_rmlui_progress.py
python tools\ui_smoke\report_rmlui_progress.py --format markdown
python tools\ui_smoke\report_rmlui_progress.py --format json
```

All three commands completed successfully.

Checker presence during validation:

- `check_rmlui_condition_inventory.py`: present, reported `status=ok`, `routes=57`, `documents_checked=57`, `total_refs=141`, `routes_with_hooks=22`, `unique_expressions=114`, `unique_tokens=111`, `malformed=0`.
- `check_rmlui_metadata_sync.py`: present, reported `status=error` because metadata route `core.runtime_smoke` has no central smoke manifest route. The progress report preserved the error as optional guardrail status and still exited successfully.

Remaining validation to run after this note:

```text
git diff --check -- tools/ui_smoke/report_rmlui_progress.py tools/ui_smoke/test_report_rmlui_progress.py docs-dev/rmlui-agent5-progress-guardrails-round12-2026-07-02.md
```

## Limits

This is reporting integration only. It does not fix metadata sync mismatches, promote routes, add live controllers, wire a first-class RmlUi dependency, or change runtime navigation behavior.
