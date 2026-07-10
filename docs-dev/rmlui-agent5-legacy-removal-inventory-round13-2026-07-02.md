# RmlUi Agent 5 Legacy Removal Inventory - Round 13

Date: 2026-07-02

Tasks: `FR-09-T10`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

Round 13 adds a planning-only guardrail for the final RmlUi legacy JSON/menu
removal cutover. The new inventory records the legacy UI surfaces that must be
revisited after parity gates pass, and the checker prevents any removal item
from being marked `ready` or `complete` while the parity manifest still has no
`parity_ready` route coverage or incomplete required evidence.

No legacy JSON removal was performed. No legacy `.menu` content was removed.
No runtime fallback was cut over. The active legacy UI bridge remains
authoritative when the guarded RmlUi path cannot open a route.

## Changed Files

- `tools/ui_smoke/rmlui_legacy_removal_manifest.json`
  - Adds schema `worr.rmlui.legacy_removal_manifest.v1`.
  - Tracks blocked/pending cleanup categories for current JSON menu surfaces,
    legacy UI bridge/runtime fallback, package and staging cleanup, developer
    and user documentation updates, and renderer/input smoke evidence.
  - Keeps all real inventory items in `blocked` or `pending` state.
- `tools/ui_smoke/check_rmlui_legacy_removal.py`
  - Validates manifest structure, allowed statuses, required task IDs, required
    removal categories, item targets, and blocker text.
  - Reads `tools/ui_smoke/rmlui_parity_manifest.json` through the existing
    parity-manifest validation path.
  - Blocks `ready` or `complete` removal statuses until parity-ready route
    coverage and all required evidence categories are complete.
  - Supports text output and `--format json`.
- `tools/ui_smoke/test_check_rmlui_legacy_removal.py`
  - Adds focused temp-manifest coverage for closed-gate blocked inventories,
    premature `ready`/`complete` failures, open-gate readiness, missing
    required categories, invalid statuses, and JSON output.

## Inventory Baseline

The manifest currently records six removal items:

- `current_json_menu_surfaces`
- `rich_legacy_widget_pages`
- `legacy_ui_bridge_runtime_fallback`
- `package_staging_cleanup`
- `user_and_developer_docs_update`
- `renderer_input_smoke_evidence`

Accepted statuses remain conservative:

- `blocked`: current JSON menu surfaces, rich legacy widget pages, legacy UI
  bridge/runtime fallback, and renderer/input smoke evidence.
- `pending`: package/staging cleanup and user/developer documentation update.
- `ready`: none.
- `complete`: none.

The gate remains closed because the parity manifest still reports zero
`parity_ready` routes and pending navigation, controller, renderer, screenshot,
input, and legacy-fallback evidence.

## Validation

Executed validation:

```text
python tools\ui_smoke\check_rmlui_legacy_removal.py: passed
python tools\ui_smoke\check_rmlui_legacy_removal.py --format json: passed
python -m pytest tools\ui_smoke\test_check_rmlui_legacy_removal.py: passed, 7 tests
git diff --check -- tools/ui_smoke/rmlui_legacy_removal_manifest.json tools/ui_smoke/check_rmlui_legacy_removal.py tools/ui_smoke/test_check_rmlui_legacy_removal.py docs-dev/rmlui-agent5-legacy-removal-inventory-round13-2026-07-02.md: passed
```

Checker baseline:

- Items checked: `6`.
- Statuses: `blocked=4`, `pending=2`, `ready=0`, `complete=0`.
- Required removal categories: `5/5`.
- Required task IDs: `4/4`.
- Parity gate: closed, with `0` `parity_ready` routes and incomplete required
  evidence.

## Caveats

This is static planning and validation only. It does not claim RmlUi parity,
renderer proof, screenshot/layout proof, input proof, final packaging cleanup,
or user-visible menu replacement.
