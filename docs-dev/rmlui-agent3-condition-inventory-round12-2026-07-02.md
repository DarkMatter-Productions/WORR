# RmlUi Round 12 Worker 3 - Condition Inventory Checker

Task IDs: FR-09-T05, FR-09-T06, FR-09-T09, DV-03-T07, DV-04-T02

Date: 2026-07-02

## Scope

Round 12 Worker 3 added a deterministic static smoke checker for condition-expression hooks in tracked RmlUi route documents.

Owned files:

- `tools/ui_smoke/check_rmlui_condition_inventory.py`
- `tools/ui_smoke/test_check_rmlui_condition_inventory.py`
- `docs-dev/rmlui-agent3-condition-inventory-round12-2026-07-02.md`

No route manifests, progress reports, roadmap/proposal documents, runtime code, or unrelated worker files were edited in this lane.

## Checker behavior

`check_rmlui_condition_inventory.py` reads `tools/ui_smoke/rmlui_manifest.json` by default and resolves each tracked route document from the repository root. It scans XML-compatible RML with `ElementTree` and inventories these attributes:

- `data-show-if`
- `data-enable-if`
- `data-visible-if`
- `data-enabled-if`
- `data-condition`

The checker reports:

- route count, documents checked, and missing documents
- total condition-attribute references
- reference counts by attribute
- routes with condition hooks
- unique condition expressions
- unique extracted condition tokens/cvars
- malformed or empty condition attributes
- unsupported non-static condition expressions
- text and JSON output with `ok` and `errors`

Supported condition syntax is intentionally conservative: bare tokens, token comparisons such as `foo=1` and `foo!=bar`, and semicolon-separated chains such as `ingame;deathmatch=0;coop=0`. Empty attributes, empty semicolon clauses, or malformed simple tokens fail the check. Dynamic/template expressions such as `{{condition}}` are reported as unsupported non-static expressions rather than silently dropped.

## Current live metrics

The live repository run at implementation time reported:

- `57` routes known
- `57` documents checked
- `0` documents missing
- `141` total condition refs
- `data-show-if`: `18`
- `data-enable-if`: `3`
- `data-visible-if`: `117`
- `data-enabled-if`: `3`
- `data-condition`: `0`
- `22` routes with condition hooks
- `114` unique condition expressions
- `111` unique condition tokens/cvars
- `0` unsupported non-static conditions
- `0` malformed or empty condition attributes

## Validation

Validation run for this worker:

- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py --format json`
- `python -m pytest tools\ui_smoke\test_check_rmlui_condition_inventory.py`
- `git diff --check -- tools/ui_smoke/check_rmlui_condition_inventory.py tools/ui_smoke/test_check_rmlui_condition_inventory.py docs-dev/rmlui-agent3-condition-inventory-round12-2026-07-02.md`

This is static inventory and validation only. It does not implement live condition evaluation, RmlUi controller behavior, renderer output, screenshot parity, runtime navigation, or legacy UI removal.
