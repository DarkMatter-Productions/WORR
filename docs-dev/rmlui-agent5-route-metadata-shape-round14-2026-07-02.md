# RmlUi Agent 5 Route Metadata Shape Guardrail - Round 14

Date: 2026-07-02

Tasks: `FR-09-T08`, `FR-09-T09`, `DV-03-T07`, `DV-04-T02`.

## Summary

Round 14 Worker 5 added a stricter static shape checker for discovered
`assets/ui/rml/*/routes.json` metadata files:

- `tools/ui_smoke/check_rmlui_route_metadata_shape.py`
- `tools/ui_smoke/test_check_rmlui_route_metadata_shape.py`

The checker validates route metadata structure beyond sync/parity checks:

- root `schema`, `owner`, `tasks`, `status_values`, `migration_phase_values`,
  and `routes` shape;
- per-route identity, ownership, surface, task, entry point, data-model,
  migration-phase, and notes fields;
- safe RML document paths, treating feature-relative paths as relative to
  `assets/ui/rml`;
- `FR-09-T08`/`DV-03-T07` style task IDs;
- string-list shape for `entry_points` and `data_models`;
- non-empty controller contracts on advanced routes; and
- required controller contract reference fields: `category`, `contract`,
  `fixture`, `model`, and `status`.

## Live Baseline

Accepted live counts from `python tools\ui_smoke\check_rmlui_route_metadata_shape.py`:

- Metadata files: 5.
- Metadata routes: 58.
- Routes by phase: `starter=13`, `controller_stub=42`, `runtime_stub=3`,
  `parity_pending=0`, `parity_ready=0`.
- Routes with controller contracts: 45.
- Controller contract refs: 117.
- Malformed routes: 0.

## Local Exceptions

`assets/ui/rml/core/routes.json` is treated as a support/bootstrap metadata
file because it contains the support-only `core.runtime_smoke` route already
recognized by the existing metadata sync checker. That support route is still
validated for core document, ownership, phase, task, entry-point, and data-model
shape, but it is not required to provide the same user-facing route fields as
menu routes.

Empty `data_models` lists are allowed only for routes that already document
that pattern locally: `core.runtime_smoke`, `main`, `game`, `options`,
`quit_confirm`, and `leave_match_confirm`.

There are no advanced-route controller-contract exceptions in the accepted
baseline.

## Validation

Executed:

```powershell
python tools\ui_smoke\check_rmlui_route_metadata_shape.py
python tools\ui_smoke\check_rmlui_route_metadata_shape.py --format json
python -m pytest tools\ui_smoke\test_check_rmlui_route_metadata_shape.py
```

Results:

- Text checker passed.
- JSON checker passed with `ok=true`.
- Focused pytest passed: 7 tests.

## Caveats

This is a static metadata guardrail only. It does not prove live RmlUi
dependency loading, controller execution, renderer behavior, runtime navigation,
screenshot parity, input parity, or legacy JSON removal.
