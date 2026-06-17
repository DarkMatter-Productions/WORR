# Q2 AAS Generator Archive Manifest Guardrails

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice hardens the archive-backed q2aas manifest path introduced in the
packaged-map smoke round. Archive member names are now rejected when they are
absolute paths, contain traversal components, or resemble platform drive-root
paths. The malformed-manifest expected-failure smoke now covers these
archive-specific cases so future package-map manifest changes cannot silently
weaken the guardrails.

## Implementation

Updated files:

- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/README.WORR.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

Archive member validation now rejects:

- absolute archive members such as `/maps/mm-rage.bsp`
- traversal members such as `../maps/mm-rage.bsp`
- path components that look like drive roots, such as `C:`
- empty, `.`, or `..` path components

The malformed manifest smoke also now includes:

- a map entry that mixes loose `path` with `archive`/`archive_member`
- a map entry with `archive` but no `archive_member`
- an archive-backed entry with an absolute member path
- an archive-backed entry with a traversal member path

Malformed archive entries stop after reporting their manifest error instead of
falling through into a secondary missing-extracted-map message.

## Report Shape

`.tmp/q2aas/validation-report.json` now records the expanded
`manifest_schema_smoke.expected_error_fragments` set:

```json
[
  "minimum_metrics.not_a_metric",
  "minimum_travel_counts.walk",
  "must use either path or archive/archive_member",
  "must define path or archive plus archive_member",
  "archive_member must be a relative path inside the archive",
  "archive_member has an unsafe path component"
]
```

The smoke still reports `manifest_schema_smoke.status = passed` only when every
expected fragment is present in the rejected manifest errors.

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py tools\q2aas\audit_worr_q2aas_stage.py
python -m json.tool tools\q2aas\validation_manifest.json
meson compile -C builddir-win q2aas-staged-smoke
meson compile -C builddir-win q2aas-package-map-smoke
```

Observed results:

- `q2aas-staged-smoke` still passes the strict `mm-rage.bsp` validation.
- Invalid BSP smoke still fails as expected with `unknown BSP format`.
- Manifest schema smoke now rejects baseline threshold errors plus archive
  path/archive-member guardrail errors and reports `status = passed`.
- `q2aas-package-map-smoke` still passes with a valid `maps/mm-rage.bsp`
  archive member after the stricter normalization change.

## Credits and Provenance

No imported upstream BSPC source files were modified in this slice. The upstream
`TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

This work changes WORR-native validation and documentation around the imported
generator. The credits ledger now records archive-member guardrails under the
WORR-native q2aas validation helper and vendor note rows.
