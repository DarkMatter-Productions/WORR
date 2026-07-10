# RmlUi Round 10 Agent 1 Command Inventory

Date: 2026-07-02
Tasks: FR-09-T05, FR-09-T09, DV-03-T07, DV-07-T04

## Scope

Added static command inventory validation for authored RmlUi route documents. The checker reads `tools/ui_smoke/rmlui_manifest.json`, resolves each route `.rml` document, and inventories command hooks declared through `data-command` and `data-command-cvar`.

## Implemented

- Added `tools/ui_smoke/check_rmlui_command_inventory.py`.
- Added `tools/ui_smoke/test_check_rmlui_command_inventory.py`.
- Counts manifest routes, present/missing documents, direct command attributes, command-cvar attributes, unique direct command tokens, unique command-cvar references, malformed or empty command attributes, and routes with command hooks.
- Splits legacy semicolon command chains while preserving semicolons inside quoted command strings.
- Flags empty command attributes and empty semicolon-chain segments.
- Validates `data-command-cvar` values as lowercase snake_case-style cvar tokens.
- Supports `--format json` for dashboard and CI consumers.

## Boundaries

This is static command inventory evidence only. It does not prove live command dispatch, native RmlUi controller execution, renderer output, input behavior, screenshot parity, or removal of legacy UI paths.

## Validation

Commands run:

- `python -m pytest tools/ui_smoke/test_check_rmlui_command_inventory.py`
- `python tools/ui_smoke/check_rmlui_command_inventory.py`
- `python tools/ui_smoke/check_rmlui_command_inventory.py --format json`
- `git diff --check -- tools/ui_smoke/check_rmlui_command_inventory.py tools/ui_smoke/test_check_rmlui_command_inventory.py docs-dev/rmlui-agent1-command-inventory-round10-2026-07-02.md`

Results:

- Focused pytest passed: `5` tests.
- Text and JSON command inventory checks passed with `ok=true`.
- Live inventory reports `57` routes, `57` documents checked, `0` missing,
  `289` direct command refs, `15` command-cvar refs, `70` unique command
  tokens, `15` unique command-cvar refs, `57` routes with command hooks, and
  `0` malformed command attributes.
