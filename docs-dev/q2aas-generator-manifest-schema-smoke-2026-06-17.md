# Q2 AAS Generator Manifest Schema Smoke

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice turns the manual malformed-manifest probe from the previous round
into an automated staged-smoke check. `q2aas-staged-smoke` now creates a
temporary invalid validation manifest under `.tmp/q2aas/`, confirms the
manifest loader rejects it for the expected schema errors, records the result
in the structured JSON report, and removes the temporary file.

This keeps manifest schema validation covered as the reference-map matrix grows.

## Implementation

Updated files:

- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/meson.build`
- `tools/q2aas/README.WORR.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

New validator switch:

- `--manifest-schema-smoke`

The smoke writes `.tmp/q2aas/invalid-manifest-schema-smoke.json` with:

- an unknown metric baseline key: `minimum_metrics.not_a_metric`
- a string travel-count threshold: `minimum_travel_counts.walk`

The helper passes only if the manifest loader returns failure and both expected
error fragments are present. It then deletes the temporary manifest so `.tmp`
does not accumulate stale expected-failure inputs.

## Report Shape

`.tmp/q2aas/validation-report.json` now includes:

```json
"manifest_schema_smoke": {
  "status": "passed",
  "expected_error_fragments": [
    "minimum_metrics.not_a_metric",
    "minimum_travel_counts.walk"
  ]
}
```

The embedded `manifest_report` records the rejected manifest path, schema,
version, task IDs, map count, loaded map count, manifest errors, and skipped-map
list.

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
python -m json.tool tools\q2aas\validation_manifest.json
meson compile -C builddir-win q2aas-config-smoke
meson compile -C builddir-win q2aas-staged-smoke
```

Observed staged smoke result:

- `mm-rage.bsp` AAS generation still passes all structural, diagnostic, and
  baseline gates.
- Invalid BSP smoke still fails as expected with
  `ERROR: unknown BSP format BAD!, version 1`.
- Manifest schema smoke fails as expected and reports `status: passed`.
- `.tmp/q2aas/` contains only `mm-rage.aas`, `mm-rage.aas.meta.json`, and
  `validation-report.json` after the run.

## Credits and Provenance

No imported upstream BSPC source files were modified in this slice. The upstream
`TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

This work changes WORR-native validation, Meson integration, and documentation
around the imported generator. The credits ledger now records the automated
manifest schema expected-failure smoke beside the existing q2aas validation
entries.
