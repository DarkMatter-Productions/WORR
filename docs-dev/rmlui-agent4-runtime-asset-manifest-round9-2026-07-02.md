# RmlUi Worker 4 Runtime Asset Manifest Round 9

Date: 2026-07-02

Worker lane: Worker 4, runtime and staging asset validation automation output

Task IDs: `FR-09-T02`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 9 adds optional detailed sidecar manifest output to the RmlUi runtime
asset validator. The existing human-readable stdout report and the Round 8
`--format json` summary remain unchanged; `--write-manifest <path>` now writes a
deterministic JSON artifact for coordinator and package-validation evidence.

This is asset and staging evidence only. It does not add a RmlUi build
dependency, prove live RmlUi document loading, replace the legacy UI runtime, or
establish visual parity.

The roadmap document was not edited in this worker slice because the explicit
parallel-worker write scope was limited to the runtime asset validator, its
focused tests, and this implementation log.

## Files Changed

- `tools/ui_smoke/check_rmlui_runtime_assets.py`
- `tools/ui_smoke/test_check_rmlui_runtime_assets.py`
- `docs-dev/rmlui-agent4-runtime-asset-manifest-round9-2026-07-02.md`

## Implementation Notes

- Added `--write-manifest <path>` to emit a detailed JSON sidecar artifact.
- The sidecar manifest records:
  - route document entries;
  - imported `.rml` and `.rcss` entries when `--include-imports` is active;
  - repo-relative source paths;
  - runtime asset paths;
  - `source_present`;
  - `staged_loose_path` and `staged_loose_present` when `--install-dir` is
    supplied;
  - the existing summary payload and validation errors.
- Manifest entries are sorted deterministically by runtime path and stable
  identifiers.
- Repo-relative output paths are resolved from `--repo-root`, so `.tmp/` sidecar
  outputs can be created without changing stdout behavior.

## Test Coverage

Focused pytest coverage was added for:

- Writing a manifest without staging, including a route document and an imported
  RCSS asset.
- Writing a manifest with staged loose route and imported assets present.
- Writing a manifest when a staged imported asset is missing, preserving the
  validation failure while recording `staged_loose_present: false`.

Existing text and JSON summary tests continue to cover route document
validation, import discovery, duplicate import de-duplication, missing required
imports, and staged loose-file failures.

## Task Mapping

`FR-09-T02`: Improves asset staging traceability by listing concrete runtime
paths for route documents and imported assets.

`FR-09-T09`: Extends RmlUi migration validation with a deterministic sidecar
artifact for route, source, runtime, and staged loose-file facts.

`DV-03-T07`: Adds focused UI smoke automation coverage for the manifest writer
without changing the existing stdout contract.

`DV-07-T04`: Strengthens regression evidence by preserving per-asset source and
staging presence for package validation.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_assets.py`
  - Result: passed, `14 passed`.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --write-manifest .tmp/rmlui/round9-runtime-assets.json`
  - Result: passed.
  - Routes checked: 57.
  - Source documents: present=57, missing=0.
  - Imported assets: discovered=16, present=16, missing=0.
  - Runtime paths: total=73, route_documents=57, imported_assets=16.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --format json`
  - Result: passed.
  - `ok`: true.
  - `errors`: empty.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .tmp/rmlui/round8-package-validation --base-game basew --write-manifest .tmp/rmlui/round9-runtime-assets-staged.json`
  - Result: passed.
  - Staged loose files: present=73, missing=0.
  - Staged loose imported assets: present=16, missing=0.

## Caveats

- The manifest records source/runtime/staged loose-file evidence only.
- The validator still follows local RML `<link href>` imports only. It does not
  parse RCSS `@import` rules, image dependencies, font dependencies, or prove
  native RmlUi runtime integration.
