# RmlUi Agent 5 Install Gate - Round 15

Date: 2026-07-02

Worker lane: Worker 5, package/install and docs-status support

Tasks: `FR-09-T02`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 15 reviewed the current RmlUi package and staged-runtime validation for
first-class dependency readiness. No code change was needed in this slice: the
existing package and runtime asset checks already cover the install gate facts
needed before real RmlUi dependency wiring lands.

The review intentionally did not edit
`docs-dev/plans/rmlui-ui-migration-roadmap.md` or
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`; those
documents remain coordinator-owned for this round.

## Existing Guardrails Confirmed

- `tools/package_assets.py` treats `ui/rml` as a default loose mirror beside
  `botfiles`, so package runs stage `.install/<base-game>/ui/rml/` style assets
  automatically.
- The package step still writes the full `assets/` tree into the archive and
  validates each discovered RmlUi asset against both the archive member and the
  loose mirror by SHA-256.
- `tools/ui_smoke/check_rmlui_runtime_assets.py` validates the source asset root
  `assets/ui/rml`, derives runtime paths under `ui/rml`, and can validate a
  staged install root via `--install-dir` and `--base-game`.
- `--include-imports` still discovers local `.rml` and `.rcss` imports reachable
  from route documents, including recursive local `.rml` imports.
- `--write-manifest` still emits a detailed runtime asset sidecar with route
  documents, imported assets, source presence, runtime paths, and staged loose
  presence.

Because those checks already cover asset root, loose mirror, archive payload,
manifest output, imported assets, and install-root expectations, adding another
test would have duplicated existing focused coverage rather than closing a real
dependency-era gap.

## Round 15 Live Counts

Package validation against `.tmp/rmlui/round15-package-validation` passed with:

- Packaged assets: `197`.
- Botfile package/loose assets: `31`.
- RmlUi package/loose assets: `103`.
- Mirrored loose paths: `botfiles`, `ui/rml`.

Runtime asset validation without staging passed with:

- Routes checked: `57`.
- Source documents: `57` present, `0` missing.
- Imported assets: `16` discovered, `16` present, `0` missing.
- Runtime paths: `73` total, with `57` route documents and `16` imported
  assets.
- Errors: `0`.

Staged runtime validation against `.tmp/rmlui/round15-package-validation/basew`
passed with:

- Routes checked: `57`.
- Source documents: `57` present, `0` missing.
- Imported assets: `16` discovered, `16` present, `0` missing.
- Runtime paths: `73` total, with `57` route documents and `16` imported
  assets.
- Staged loose files: `73` present, `0` missing.
- Staged loose imported assets: `16` present, `0` missing.

The emitted sidecar manifest
`.tmp/rmlui/round15-runtime-assets-staged.json` records:

- `ok=true`.
- `route_documents=57`.
- `imported_assets=16`.
- `install_dir=.tmp/rmlui/round15-package-validation`.
- `base_game=basew`.

## Validation

Executed:

```powershell
python tools\package_assets.py --assets-dir assets --install-dir .tmp\rmlui\round15-package-validation --base-game basew --archive-name pak0.pkz
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --format json
python -m pytest tools\test_package_assets.py tools\ui_smoke\test_check_rmlui_runtime_assets.py
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round15-package-validation --base-game basew
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round15-package-validation --base-game basew --format json
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .tmp\rmlui\round15-package-validation --base-game basew --write-manifest .tmp\rmlui\round15-runtime-assets-staged.json
git diff --check -- tools\package_assets.py tools\test_package_assets.py tools\ui_smoke\check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_runtime_assets.py docs-dev\rmlui-agent5-install-gate-round15-2026-07-02.md
```

Results:

- Package validation passed.
- Source/runtime JSON validation passed with `ok=true`.
- Focused pytest passed: `27 passed`.
- Staged runtime text validation passed.
- Staged runtime JSON validation passed with `ok=true`.
- Staged sidecar manifest output passed.
- Whitespace diff check passed on owned files. The final check used a temporary
  Git index so untracked owned files were included without changing the real
  index; Git emitted only the repository LF-to-CRLF normalization warning for
  this new markdown file.

## Caveats

This round proves packaging and staged loose asset coverage only. It does not
add the first-class RmlUi dependency, Meson/wrap wiring, native RmlUi document
loading, renderer bridge behavior, live controller execution, input parity,
layout parity, screenshot parity, or legacy UI removal.
