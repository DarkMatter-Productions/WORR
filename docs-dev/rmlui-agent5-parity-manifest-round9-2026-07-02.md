# RmlUi Round 9 Worker 5: Parity Checklist Manifest

Date: 2026-07-02

Task IDs: FR-09-T09, FR-09-T10, DV-03-T07, DV-07-T04

## Scope

Added parity/status scaffolding for later Gate G3/G4 work:

- `tools/ui_smoke/rmlui_parity_manifest.json`
- `tools/ui_smoke/check_rmlui_parity_manifest.py`
- `tools/ui_smoke/test_check_rmlui_parity_manifest.py`

This round does not claim end-user parity, does not add live RmlUi controllers, does not prove renderer output, and does not remove the legacy UI path.

## Checklist Model

The parity manifest defines canonical checklist categories for each route through phase defaults plus route coverage entries:

- `document_load`
- `navigation`
- `controller_bindings`
- `renderer_open_gl`
- `renderer_vulkan`
- `renderer_rtx_vkpt`
- `screenshot_layout`
- `input_escape_back`
- `legacy_fallback`

Current status intentionally leaves all real parity evidence pending. Only static/source evidence is marked complete where previous smoke work supports it:

- `document_load` is complete for non-`parity_ready` phases because route documents are already guarded by the smoke manifest checks.
- `controller_bindings` is complete for `controller_stub` and `runtime_stub` phases because those phases require controller contract metadata.
- `legacy_fallback` is complete only for `runtime_stub` routes because Round 8 validated guarded `UI_Rml_OpenMenu` probing and legacy fallback.

The `parity_ready` phase defaults remain pending for every category. A route promoted to `parity_ready` must carry explicit completed evidence overrides or the checker fails.

## Checker Behavior

`check_rmlui_parity_manifest.py` cross-checks the parity checklist against `tools/ui_smoke/rmlui_manifest.json` and reports clear progression counts:

- Requires every smoke route ID to have checklist coverage.
- Rejects unknown route IDs in the parity checklist.
- Requires canonical evidence categories and phase defaults.
- Rejects `parity_ready` smoke routes unless all effective evidence categories are complete.
- Reports pending and complete counts by category.
- Supports `--format json` for machine-readable dashboards.

## Current Baseline

Against the live route manifest at validation time, the checker covers 57 routes and 9 evidence categories. No `parity_ready` routes exist yet. Real renderer, screenshot/layout, and input evidence remains pending across all routes.

Current validation counts:

- `document_load`: 0 pending
- `navigation`: 57 pending
- `controller_bindings`: 38 pending
- `renderer_open_gl`: 57 pending
- `renderer_vulkan`: 57 pending
- `renderer_rtx_vkpt`: 57 pending
- `screenshot_layout`: 57 pending
- `input_escape_back`: 57 pending
- `legacy_fallback`: 54 pending

Current phase counts from `tools/ui_smoke/rmlui_manifest.json`:

- `starter`: 38
- `controller_stub`: 16
- `runtime_stub`: 3
- `parity_pending`: 0
- `parity_ready`: 0

## Validation

Requested validation:

```text
python -m pytest tools/ui_smoke/test_check_rmlui_parity_manifest.py
python tools/ui_smoke/check_rmlui_parity_manifest.py
git diff --check -- tools/ui_smoke/rmlui_parity_manifest.json tools/ui_smoke/check_rmlui_parity_manifest.py tools/ui_smoke/test_check_rmlui_parity_manifest.py docs-dev/rmlui-agent5-parity-manifest-round9-2026-07-02.md
```
