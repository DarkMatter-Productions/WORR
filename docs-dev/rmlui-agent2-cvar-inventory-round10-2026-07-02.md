# RmlUi Agent 2 Cvar Inventory - Round 10

Date: 2026-07-02

Task IDs: FR-09-T05, FR-09-T09, DV-03-T07, DV-07-T04

## Scope

Added a static cvar inventory checker for the RmlUi route-document smoke set. The checker reads `tools/ui_smoke/rmlui_manifest.json`, parses each route `.rml` document, and inventories cvar hooks declared through:

- `data-cvar`
- `data-bind-cvar`
- `data-label-cvar`
- `data-command-cvar`
- `data-enable-if`
- `data-show-if`
- `data-visible-if`

Condition expressions are parsed conservatively so clauses such as `ui_list_item_show_0=1` inventory `ui_list_item_show_0` without treating literal values as cvars.

## Added Files

- `tools/ui_smoke/check_rmlui_cvar_inventory.py`
- `tools/ui_smoke/test_check_rmlui_cvar_inventory.py`

## Reported Progression

The checker reports:

- route count
- present and missing route documents checked
- direct cvar refs
- label cvar refs
- command cvar refs
- condition cvar refs
- unique cvars
- unknown/bad tokens
- routes with cvar hooks
- dynamic placeholder values skipped

The checker also supports `--format json` for dashboard and future progression reporting.

## Boundaries

This is static cvar inventory only. It does not prove live cvar read/write behavior, RmlUi data-model binding, command execution, input behavior, renderer output, screenshots, parity, or legacy UI removal.

## Validation

Passed commands:

```text
python -m pytest tools/ui_smoke/test_check_rmlui_cvar_inventory.py
python tools/ui_smoke/check_rmlui_cvar_inventory.py
python tools/ui_smoke/check_rmlui_cvar_inventory.py --format json
git diff --check -- tools/ui_smoke/check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py docs-dev/rmlui-agent2-cvar-inventory-round10-2026-07-02.md
```

Live inventory baseline:

- focused pytest passed: 6 tests
- text and JSON checker output passed with ok=true
- routes known: 57
- documents checked: 57 present, 0 missing
- direct cvar refs: 233
- label cvar refs: 54
- command cvar refs: 15
- condition cvar refs: 150
- total cvar refs: 452
- unique cvars: 272
- routes with cvar hooks: 37
- unknown/bad tokens: 0
