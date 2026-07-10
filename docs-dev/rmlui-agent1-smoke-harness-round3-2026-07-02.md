# RmlUi Agent 1 Smoke Harness Round 3

Date: 2026-07-02

Owner lane: Agent 1, validation harness hardening

Task IDs: `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

This round strengthens the RmlUi smoke manifest checker beyond presence-only
validation. The harness now validates manifest shape, duplicate route IDs,
strict `required_now` booleans, present route document XML parsing, and local
`<link href="...">` imports. Pending routes still pass when `required_now` is
`false`.

Default output is now concise: route totals, wave counts, required-present
coverage, RML parse count, href import count, and final result. Pending route
details remain available through `--verbose`.

## Changed Files

- `tools/ui_smoke/check_rmlui_manifest.py`
  - Adds manifest-shape validation for root metadata, task lists, route
    objects, route document paths, duplicate route IDs, and boolean
    `required_now`.
  - Parses every present route document as XML-ish RML and requires route
    documents to use an `<rml>` root.
  - Validates local `<link href="...">` imports relative to the referencing
    document, rejects absolute or repo-escaping imports, and parses imported
    `.rml` templates.
  - Keeps optional missing routes as pending instead of failures.
  - Replaces verbose default pending-route output with a compact summary.
- `tools/ui_smoke/test_check_rmlui_manifest.py`
  - Adds temporary-manifest coverage for required missing route failure,
    optional pending route success, malformed RML failure, missing local import
    failure, passing present route with imports, duplicate route ID failure, and
    invalid `required_now` shape failure.

## Task Mapping

`FR-09-T09`: Extends migration-specific smoke coverage so present RmlUi routes
must be parseable and their static imports must resolve.

`DV-03-T07`: Hardens the early UI automation harness around the manifest and
document-load prerequisites needed before runtime navigation/screenshot checks.

`DV-07-T04`: Adds regression guardrails for malformed documents, missing shared
assets, duplicate route IDs, and invalid manifest metadata.

## Validation Performed

- `python -m pytest tools/ui_smoke/test_check_rmlui_manifest.py`
  - Result: `7 passed in 0.08s`
- `python tools/ui_smoke/check_rmlui_manifest.py`
  - Result: passed.
  - Summary: `57` routes, `30` required, `36` present, `21` pending.
  - RML/import coverage: `96` RML files parsed, `124` local href imports
    checked.

## Notes

No manifest route ownership or asset content was changed in this lane. The
checker treats all existing RmlUi assets as intentional and validates only the
owned smoke-harness paths for this round.
