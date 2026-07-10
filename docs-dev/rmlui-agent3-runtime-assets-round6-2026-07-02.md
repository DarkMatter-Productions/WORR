# RmlUi Agent 3 Runtime Assets Round 6

Date: 2026-07-02

Tasks: `FR-09-T02`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Scope

This round adds dependency-free runtime asset path validation for the RmlUi smoke
manifest. The validator bridges authored route document paths under
`assets/ui/rml/` to runtime package paths under `ui/rml/`, matching the path
contract used by the client runtime probe and the loose asset staging workflow.

The roadmap document was not edited in this worker slice because this change is
scoped to validation tooling and its task mapping is recorded here.

## Changed Files

- `tools/ui_smoke/check_rmlui_runtime_assets.py`
- `tools/ui_smoke/test_check_rmlui_runtime_assets.py`
- `docs-dev/rmlui-agent3-runtime-assets-round6-2026-07-02.md`

## Implementation Notes

- Added `check_rmlui_runtime_assets.py` with the same importable function plus
  CLI shape as the existing `tools/ui_smoke` validators.
- The checker reads `tools/ui_smoke/rmlui_manifest.json` by default and validates
  each route `document` value as a repo-relative `.rml` path under
  `assets/ui/rml/`.
- Runtime paths are derived mechanically by replacing the source root with
  `ui/rml/`; for example, `assets/ui/rml/shell/main.rml` becomes
  `ui/rml/shell/main.rml`.
- Source documents are checked as repo files. Missing `required_now` documents
  fail the run; optional routes still contribute to present/missing counts.
- `--install-dir` enables an additional loose-file check under
  `<install-dir>/<base-game>/ui/rml/...`. The default invocation does not require
  package staging to exist.
- The CLI reports routes checked, source documents present/missing, runtime
  paths derived, and staged loose files present/missing when staging is checked.

## Test Coverage

Focused pytest coverage uses temporary manifests and RML fixtures for:

- Valid source-to-runtime path mapping.
- Invalid non-`.rml` route document paths.
- Missing required source documents.
- Staged loose-file validation with a missing required staged route document.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_assets.py` passed:
  `4 passed`.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py` passed against the
  current repository manifest:
  - Routes checked: 57
  - Source documents: present=57, missing=0
  - Runtime paths derived: 57
- Staged validation passed against the existing loose-file root with:
  `python tools/ui_smoke/check_rmlui_runtime_assets.py --install-dir .tmp/rmlui/round5-package-validation --base-game basew`
  - Staged loose files: present=57, missing=0
- Owned-file whitespace check passed with no output:
  `git diff --check -- tools/ui_smoke/check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_runtime_assets.py docs-dev/rmlui-agent3-runtime-assets-round6-2026-07-02.md`

## Handoff Notes

- This validator intentionally checks only route document files, not linked
  templates, RCSS, images, or fonts. Broader dependency walking should remain a
  separate validator if the package contract grows.
- The checker is suitable for both source-tree smoke validation and post-staging
  loose-file verification after `tools/package_assets.py` refreshes `.install/`
  or a temporary package root.
