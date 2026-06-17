# Q2 AAS Generator Manifest Schema Validation

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice makes `tools/q2aas/validation_manifest.json` self-describing and
harder to misconfigure. The validation helper now checks the manifest schema,
version, root keys, task IDs, pending reference-map list, map entry shape,
boolean gate fields, notes type, baseline metric names, baseline travel-count
names, and non-negative integer baseline values before map conversion starts.

The structured validation report now carries manifest provenance so staged
smoke output can be tied back to the task IDs and manifest schema that produced
it.

## Implementation

Updated files:

- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/validation_manifest.json`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

The manifest now declares:

```json
"schema": "worr-q2aas-validation-manifest-v1"
```

The validator requires:

- `schema == "worr-q2aas-validation-manifest-v1"`
- `version == 1`
- `task_ids` as non-empty strings
- `maps` as an array of objects
- `pending_reference_maps` as non-empty strings when present
- known root and map-entry keys only
- boolean gate fields as actual JSON booleans
- `minimum_metrics` keys matching parsed AAS metric names
- `minimum_travel_counts` keys matching parsed travel-count names
- baseline threshold values as non-negative JSON integers

## Report Shape

`.tmp/q2aas/validation-report.json` now includes a top-level `manifests` array.
The staged manifest report currently records:

- schema: `worr-q2aas-validation-manifest-v1`
- version: `1`
- task IDs: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`
- map count: `1`
- loaded map count: `1`
- pending reference categories
- errors: `[]`
- skipped maps: `[]`

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
python -m json.tool tools\q2aas\validation_manifest.json
meson compile -C builddir-win q2aas-config-smoke
meson compile -C builddir-win q2aas-staged-smoke
```

Scratch malformed-manifest smoke:

- Created `.tmp/q2aas/invalid-manifest-schema-smoke.json`.
- Verified the helper exits with code `2`.
- Confirmed errors for an unknown metric key and a string travel-count
  threshold.
- Removed the scratch manifest after the check.

The staged `mm-rage` smoke still passes all diagnostic and baseline gates and
still writes:

- `.tmp/q2aas/mm-rage.aas`
- `.tmp/q2aas/mm-rage.aas.meta.json`
- `.tmp/q2aas/validation-report.json`

## Credits and Provenance

No imported upstream BSPC source files were modified in this slice. The upstream
`TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

This work changes WORR-native validation, manifest, and documentation files
around the imported generator. The credits ledger now records the manifest
schema and provenance reporting behavior beside the existing q2aas validation
entries.
