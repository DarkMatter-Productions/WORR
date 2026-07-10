# RmlUi Worker 4 Runtime Assets JSON Round 8

Date: 2026-07-02

Worker lane: Worker 4, runtime asset validation automation output

Task IDs: `FR-09-T02`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 8 adds machine-readable JSON output to the RmlUi runtime asset validator.
The default `text` output remains unchanged for existing developer and CI usage,
while `--format json` provides stable facts for automation that should not parse
terminal prose.

The roadmap document was not edited in this worker slice because the explicit
parallel-worker write scope was limited to the runtime asset validator, its
focused tests, and this implementation log.

## Files Changed

- `tools/ui_smoke/check_rmlui_runtime_assets.py`
- `tools/ui_smoke/test_check_rmlui_runtime_assets.py`
- `docs-dev/rmlui-agent4-runtime-assets-json-round8-2026-07-02.md`

## Implementation Notes

- Added `--format text|json`, defaulting to `text`.
- Added a JSON serializer for `RuntimeAssetReport` that reports:
  - `ok`;
  - `routes_checked`;
  - `source_documents.present` and `.missing`;
  - `imported_assets.discovered`, `.present`, and `.missing`;
  - `runtime_paths.total`, `.route_documents`, and `.imported_assets`;
  - `staging_requested`;
  - `staged_loose_files.present` and `.missing`;
  - `staged_loose_imported_assets.present` and `.missing` when both import and
    staging checks are active;
  - `errors`.
- Validation failures now preserve the non-zero exit status while emitting the
  accumulated error list in JSON mode.
- Manifest load, decode, and argument-derived validation errors also emit a JSON
  error payload when `--format json` is selected.

## Test Coverage

Focused pytest coverage was added for:

- JSON success without import walking or staged loose-file validation.
- JSON success with import walking and staged loose-file validation.
- JSON failure when an imported asset exists in source but is missing from the
  staged loose-file tree.

Existing text-mode tests continue to cover route document validation, staged
route documents, recursive import discovery, duplicate import de-duplication,
missing required imports, and staged imported asset failures.

## Task Mapping

`FR-09-T02`: Keeps loose runtime asset staging verifiable by exposing route and
import runtime path totals in a structured form.

`FR-09-T09`: Extends migration-specific validation with machine-readable facts
for route documents, imported assets, and staged loose-file coverage.

`DV-03-T07`: Adds structured output to the UI smoke automation harness while
preserving the existing human-readable default report.

`DV-07-T04`: Improves the regression and parity trail by making validation
failures and coverage counts directly consumable by automation.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_assets.py`
  - Result: passed, `11 passed`.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --format json`
  - Result: passed.
  - Routes checked: 57.
  - Source documents: present=57, missing=0.
  - Imported assets: discovered=16, present=16, missing=0.
  - Runtime paths: total=73, route_documents=57, imported_assets=16.
  - Staging requested: false.

## Caveats

- JSON output is count-oriented. It does not list every route or imported asset
  path.
- The validator still follows local RML `<link href>` imports only. It does not
  parse RCSS `@import` rules, image dependencies, font dependencies, or prove
  live RmlUi document loading.
