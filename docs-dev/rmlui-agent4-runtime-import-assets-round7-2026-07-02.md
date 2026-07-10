# RmlUi Worker 4 Runtime Import Assets Round 7

Date: 2026-07-02

Worker lane: Worker 4, runtime asset validation imports

Task IDs: `FR-09-T02`, `FR-09-T04`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 7 extends the RmlUi runtime asset validator so it can optionally walk local
RML/RCSS imports referenced by present route documents. The default route
document validation path remains unchanged; import validation is enabled only
with `--include-imports`.

The roadmap document was not edited in this worker slice because the explicit
write scope for the parallel task was limited to the runtime asset validator,
its focused tests, and this implementation log.

## Files Changed

- `tools/ui_smoke/check_rmlui_runtime_assets.py`
- `tools/ui_smoke/test_check_rmlui_runtime_assets.py`
- `docs-dev/rmlui-agent4-runtime-import-assets-round7-2026-07-02.md`

## Implementation Notes

- Added a `--include-imports` CLI flag and matching `include_imports` argument
  for importable validation calls.
- Import walking parses each present route document and follows local `<link
  href="...">` references ending in `.rml` or `.rcss`.
- Import resolution is intentionally conservative:
  - hash-only, external URL, scheme-based, Windows/backslash, and template
    targets are ignored;
  - relative paths are normalized under `assets/ui/rml`;
  - imports that escape `assets/ui/rml` are reported as validation errors;
  - recursive `.rml` imports are bounded by a visited map to avoid cycles.
- Imported assets are mapped to runtime paths by the same source-root to
  runtime-root transform used for route documents:
  `assets/ui/rml/...` -> `ui/rml/...`.
- Imported assets contribute to runtime path, source presence, and staged loose
  file counts. The report now prints separate imported asset counts when import
  validation is enabled.
- Duplicate recursive imports are counted once as assets. If a file was first
  walked from an optional route and later from a required route, the recursive
  pass can revisit it once so nested missing imports still become required
  errors.

## Test Coverage

Focused pytest coverage was added for:

- Inclusion of direct and recursive local `.rml`/`.rcss` imports.
- Recursive duplicate avoidance for shared imports.
- Missing required local imports.
- Staged loose-file validation for imported assets.

Existing default route document mapping, invalid document path, missing source
document, and staged route document tests continue to cover the non-import path.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_assets.py`
  - Result: passed, `8 passed`.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports`
  - Result: passed.
  - Routes checked: 57.
  - Source documents: present=57, missing=0.
  - Imported assets: discovered=16, present=16, missing=0.
  - Runtime paths derived: 73 total, with 57 route documents and 16 imported
    assets.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .tmp/rmlui/round6-package-validation --base-game basew`
  - Result: passed.
  - Staged loose files: present=73, missing=0.
  - Staged loose imported assets: present=16, missing=0.
- `git diff --check -- tools/ui_smoke/check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_assets.py docs-dev/rmlui-agent4-runtime-import-assets-round7-2026-07-02.md`
  - Result: passed with no output.

## Caveats

- This pass follows RML `<link href>` imports only. It does not parse RCSS
  `@import` rules or discover image/font dependencies referenced from styles.
- Import validation is source-tree and loose-file package validation only; it
  does not prove live RmlUi document load, layout, renderer behavior, or parity.
