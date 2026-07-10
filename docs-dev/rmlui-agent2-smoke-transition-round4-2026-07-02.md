# RmlUi Agent 2 Smoke Transition Round 4

Date: 2026-07-02

Worker lane: Worker 2, smoke transition validation

Task IDs: `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

Round 4 adds transition-phase validation to the RmlUi smoke manifest checker so
the manifest can distinguish starter shell documents from routes with
controller/runtime/parity evidence as the migration matures.

The route-level field is `migration_phase`. It is optional for older or local
manifests, preserving existing smoke-check behavior, but a manifest can opt in
with `migration_phase_required: true`.

Allowed route phases:

- `starter`: document shell or scaffold with no claimed runtime/controller
  evidence.
- `controller_stub`: route declares controller-facing bindings or commands, but
  parity is not proven.
- `runtime_stub`: route has runtime/controller backing evidence, but parity is
  still pending.
- `parity_pending`: route is wired through the new path and awaiting migration
  parity signoff.
- `parity_ready`: route has migration parity evidence and is ready for final
  legacy fallback removal.

## Files Changed

- `tools/ui_smoke/check_rmlui_manifest.py`
  - Adds the `migration_phase` allowlist and the `migration_phase_required`
    manifest-level opt-in.
  - Validates present route transition metadata against the allowlist.
  - Requires `migration_phase` on every route only when the manifest opts in.
  - Prints `Migration phases: ...` counts when valid phase metadata is present.
- `tools/ui_smoke/test_check_rmlui_manifest.py`
  - Covers backwards-compatible omission, opted-in required phase validation,
    invalid phase failure, and valid summary counts.
- `tools/ui_smoke/rmlui_manifest.json`
  - Opts the central smoke manifest into `migration_phase_required`.
  - Adds the conservative `migration_phase: "starter"` baseline to all 57
    tracked routes.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_manifest.py`
  - Result: `11 passed in 0.17s`
- `python tools/ui_smoke/check_rmlui_manifest.py`
  - Result: passed.
  - Summary: `57` routes, `57` required, `57` present, `0` pending.
  - Transition summary: `starter=57`.
  - RML/import coverage: `151` RML files parsed, `213` local href imports
    checked.
- `python tools/ui_smoke/check_rmlui_route_contracts.py`
  - Result: `RmlUi route contract audit passed.`

## Task Mapping

`FR-09-T09`: Adds migration-specific route transition metadata so future smoke
runs can report which routes remain starter shells and which have moved toward
controller/runtime/parity readiness.

`DV-03-T07`: Extends the smoke harness metadata checks without requiring the
full runtime navigation/screenshot harness to exist first.

`DV-07-T04`: Keeps the migration parity trail explicit in development docs and
guards against untracked route status claims.
