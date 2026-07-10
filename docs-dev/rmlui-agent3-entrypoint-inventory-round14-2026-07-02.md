# RmlUi Agent 3 Entrypoint Inventory Round 14

Date: 2026-07-02

Tasks: `FR-09-T05`, `FR-09-T08`, `FR-09-T09`, `FR-09-T10`, `FR-07-T01`, `FR-07-T02`, `DV-03-T07`, `DV-04-T02`, and `DV-07-T04`.

## Summary

Round 14 Worker 3 added a static route entry point inventory checker for the RmlUi migration metadata. The checker reads the central smoke manifest and discovered `assets/ui/rml/*/routes.json` files, then inventories `entry_points` for central migration routes and supported metadata-only routes.

Accepted live baseline:

- Central routes: 57.
- Metadata files: 5.
- Metadata routes: 58.
- Routes with entrypoints: 58.
- Routes without entrypoints: 0.
- Total entrypoint refs: 72.
- Unique entrypoints: 72.
- Duplicate entrypoints within a route: 0.
- Support metadata routes: 1 (`core.runtime_smoke`).
- Central routes without metadata: 0.
- Malformed entrypoints: 0.

## Implementation

Added `tools/ui_smoke/check_rmlui_entrypoint_inventory.py`.

The checker validates:

- Central smoke manifest route IDs.
- Discovered route metadata files under `assets/ui/rml/*/routes.json`.
- Supported metadata-only route IDs, currently `core.runtime_smoke`.
- Missing central route metadata.
- Missing `entry_points` fields.
- Non-list `entry_points` values.
- Non-string entries.
- Empty string entries after trimming.
- Duplicate entry point strings within a route.

The text and JSON reports expose deterministic counts for central routes, metadata files, metadata routes, routes with or without entry points, total refs, unique values, duplicate values, support-only routes, missing central metadata, malformed entries, and errors.

Added `tools/ui_smoke/test_check_rmlui_entrypoint_inventory.py`.

Focused coverage includes:

- Successful central plus support-route inventory.
- Missing central metadata.
- Missing, empty, and non-string entry point values.
- Duplicate entry point strings within one route.
- JSON output shape.
- Current repository stability.

## Validation

Commands run:

```powershell
python tools\ui_smoke\check_rmlui_entrypoint_inventory.py
python tools\ui_smoke\check_rmlui_entrypoint_inventory.py --format json
python -m pytest tools\ui_smoke\test_check_rmlui_entrypoint_inventory.py
```

Results:

- Entrypoint inventory check passed.
- JSON output reported `ok: true`.
- Focused pytest passed: 6 tests.

## Caveats

This is static metadata inventory only. It does not prove that any `pushmenu`, engine callback, fallback, or menu-opening path reaches RmlUi at runtime. It also does not claim renderer integration, input handling, screenshots, parity readiness, or legacy JSON removal.
