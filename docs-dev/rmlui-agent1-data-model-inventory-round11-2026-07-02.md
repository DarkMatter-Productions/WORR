# RmlUi Round 11 Agent 1 Data-Model Inventory

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T07`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Added a static data-model inventory checker for the RmlUi migration route
documents. The checker reads `tools/ui_smoke/rmlui_manifest.json`, parses each
listed `.rml` route document, and inventories these route-level hooks:

- `data-model`
- `data-bind`
- `data-options`
- `data-component`
- `data-controller`
- `data-action-type`
- `data-slot`
- `data-bind-command`
- `data-bind-group`

The report tracks route/document coverage, total model/data-binding
references, unique model tokens, component/controller/action/slot references,
routes with data-model hooks, and malformed token evidence. It supports both
human-readable text and `--format json` for later progress dashboards.

## Non-Goals

This is static data-model inventory only. It does not prove live RmlUi data
binding, live C++ controller behavior, runtime navigation, renderer output,
screenshot/layout parity, input parity, or legacy UI removal.

## Validation Results

Passed after implementation:

- `python -m pytest tools/ui_smoke/test_check_rmlui_data_model_inventory.py`
  - `5 passed`
- `python tools/ui_smoke/check_rmlui_data_model_inventory.py`
  - `57` routes known
  - `57` documents checked, `0` missing
  - `190` total model/data-binding refs
  - `187` unique model tokens
  - `30` component refs
  - `13` controller refs
  - `33` action-type refs
  - `31` slot refs
  - `38` routes with data-model hooks
  - `0` malformed tokens
- `python tools/ui_smoke/check_rmlui_data_model_inventory.py --format json`
  - passed with `ok: true`
- `git diff --check -- tools/ui_smoke/check_rmlui_data_model_inventory.py tools/ui_smoke/test_check_rmlui_data_model_inventory.py docs-dev/rmlui-agent1-data-model-inventory-round11-2026-07-02.md`
  - clean
