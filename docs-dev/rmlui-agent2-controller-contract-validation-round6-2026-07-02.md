# RmlUi Agent 2 Controller Contract Validation Round 6

Date: 2026-07-02

Worker lane: Worker 2, route contract validation

Task IDs: `FR-09-T05`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Roadmap: `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`

## Summary

Round 6 strengthens the existing route contract audit so route manifests can
declare optional per-route `controller_contracts` arrays without relying on
manual review alone.

The checker still keeps the validation lightweight. It does not require a full
controller fixture schema, but it now confirms that each referenced contract
entry has safe token metadata, points at a JSON object fixture below
`assets/ui/rml/contracts`, and reports how many controller contract references
each manifest contains.

## Changed Files

- `tools/ui_smoke/check_rmlui_route_contracts.py`
  - Adds controller contract reference validation for optional route-level
    `controller_contracts` arrays.
  - Validates each reference as an object with lowercase token fields for
    `category`, `contract`, `model`, and `status`.
  - Validates fixture references as safe slash-separated `.json` paths rooted
    under `assets/ui/rml/contracts`; filename-only references remain supported.
  - Loads each referenced fixture and requires the JSON root to be an object.
  - Reports per-manifest controller contract reference counts in CLI output.
- `tools/ui_smoke/test_check_rmlui_route_contracts.py`
  - Adds focused pytest coverage for a valid controller contract reference,
    missing fixture, unsafe fixture path, and invalid entry shape/token data.
- `docs-dev/rmlui-agent2-controller-contract-validation-round6-2026-07-02.md`
  - Records this validation pass and task mapping.

## Task Mapping

`FR-09-T05`: Guards the mock controller/data-model contract references added
to route manifests.

`FR-09-T09`: Extends route progression validation so `controller_stub` metadata
has auditable controller contract evidence.

`DV-03-T07`: Keeps the smoke/checker toolchain aligned with route manifest
metadata used by automation.

`DV-07-T04`: Preserves a lightweight parity trail by ensuring static
controller contract claims point at concrete fixtures before runtime parity is
claimed.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_route_contracts.py`
  - Result: passed, `7 passed`.
- `python tools/ui_smoke/check_rmlui_route_contracts.py`
  - Result: passed.
  - Controller contract references: `core=0`, `shell=28`, `smoke=0`.
  - Required smoke documents present: `57/57`.

## Notes

- No `q2proto/` files were touched.
- No renderer files or Vulkan/OpenGL paths were touched.
- The route contract audit remains schema-light by design; fixture content is
  only checked for JSON object shape in this round.
