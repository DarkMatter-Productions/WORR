# RmlUi Agent 2 Route Contracts Round 3 (2026-07-02)

Task IDs: `FR-09-T01`, `FR-09-T05`, `FR-09-T09`, `DV-04-T02`

## Summary

This round turns the round-2 hand-authored route contract into an executable
audit for the current RmlUi migration route manifests. The checker is
dependency-free, reads the contract profile from
`assets/ui/rml/contracts/route-contract.schema.json`, and complements
`tools/ui_smoke/check_rmlui_manifest.py` by validating route metadata across
the core route manifest, shell route manifest, and smoke manifest.

## Files Changed

- `assets/ui/rml/contracts/route-contract.schema.json`
  - Adds `FR-09-T01` and `FR-09-T09` to the contract task list while retaining
    the round-2 route/component task references.
  - Adds an `audit_contract` profile listing the three audited manifests,
    their expected schema IDs, and the document base used to resolve route
    document paths.
  - Clarifies that document paths must use forward-slash relative paths and
    must not use absolute paths or parent traversal.
  - Tightens owner and status fields to non-empty lowercase token strings.

- `tools/ui_smoke/check_rmlui_route_contracts.py`
  - Loads the contract schema/audit profile and audits the listed manifests.
  - Validates required route fields from `#/$defs/route/required`.
  - Validates route IDs, effective owners, optional status strings,
    `required_now` booleans, relative `.rml` document paths, duplicate route
    IDs inside each manifest, expected manifest schema IDs, and
    `required_now` document existence.
  - Resolves feature route documents relative to `assets/ui/rml` and smoke
    manifest documents relative to the repository root.

- `tools/ui_smoke/test_check_rmlui_route_contracts.py`
  - Covers schema-provided manifest profiles, feature-vs-smoke document bases,
    manifest-level owner inheritance, duplicate IDs, missing required
    documents, invalid owner/status strings, invalid document paths, and
    invalid `required_now` types.

- `docs-dev/rmlui-agent2-route-contracts-round3-2026-07-02.md`
  - Records this implementation and validation for the requested task IDs.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_route_contracts.py`
  - Result: `3 passed in 0.04s`

- `python tools\ui_smoke\check_rmlui_route_contracts.py`
  - Result: `RmlUi route contract audit passed.`
  - `core`: 1 route, 0 `required_now`, 1 document present.
  - `shell`: 23 routes, 0 `required_now`, 23 documents present.
  - `smoke`: 57 routes, 30 `required_now`, 30/30 required documents present,
    57 documents present total, and 0 optional/pending documents missing.

- Direct trailing-whitespace scan across the four owned files.
  - Result: `No trailing whitespace in owned files.`

## Notes

- This audit intentionally does not replace `check_rmlui_manifest.py`; that
  script remains the focused smoke-manifest document-presence check.
- No `q2proto/` files were read or modified.
- No end-user documentation was added because this is engineering validation
  support only.
