# Q2AAS Reference Map Coverage Reporting

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This pass broadens q2aas reference-map validation without touching game/server
code. No `q2proto/` files were changed.

The manifest now declares explicit reference coverage categories and optional
candidate maps. The current staged validation still converts `mm-rage`, but the
report no longer implies that the wider reference set is covered. Missing
reference candidates are enumerated as skipped maps and incomplete coverage
categories until their BSPs are staged.

## Implementation

Updated q2aas validation behavior:

- `tools/q2aas/validation_manifest.json` now assigns `coverage_categories` to
  manifest map entries and declares `reference_coverage` category checks.
- The currently validated category remains `worr_current_dm` through
  `mm-rage`.
- Optional reference candidates are now listed for `q2dm1`, `q2dm2`, `q2dm8`,
  `q2ctf1`, `base1`, `base2`, and `train`. They are `required: false`, so the
  normal staged smoke can skip them while recording the coverage gap.
- `tools/q2aas/validate_worr_q2aas.py` validates the new schema fields, records
  declared/skipped maps, writes manifest-level and top-level
  `reference_coverage` report summaries, and adds `--require-reference-coverage`
  for strict gating when the reference set is staged.
- The manifest schema smoke now checks malformed `reference_coverage` entries.
- `tools/q2aas/test_validate_worr_q2aas.py` covers skipped missing reference
  maps and reference coverage schema errors.

Updated inventory behavior:

- `tools/aas_inventory/inventory_aas_assets.py` now carries map coverage
  categories into the asset report, evaluates manifest `reference_coverage`
  against staged assets, and adds `--fail-on-incomplete-reference-coverage`.
- `tools/aas_inventory/test_inventory_aas_assets.py` verifies category
  propagation and incomplete reference coverage reporting.

## Current Coverage State

The current local reports still validate only `mm-rage`.

`.tmp/q2aas/validation-report.json` now reports:

- reference coverage status: `incomplete`
- validated maps: `mm-rage`
- incomplete categories: `id_deathmatch_reference`,
  `open_deathmatch_reference`, `ctf_reference`, `campaign_reference`,
  `liquid_or_hazard_reference`
- unique missing candidate maps: `base1`, `base2`, `q2ctf1`, `q2dm1`,
  `q2dm2`, `q2dm8`, `train`

`.tmp/aas_inventory/asset-inventory.json` agrees with the same incomplete
category list and unique missing candidate maps. Its current summary is
`maps=8`, `ready=1`, `needs_conversion=0`, `source_only=0`,
`aas_without_bsp=0`, `manifest_required=1`.

## Validation

Commands run:

```powershell
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m py_compile tools\q2aas\validate_worr_q2aas.py tools\q2aas\test_validate_worr_q2aas.py tools\aas_inventory\inventory_aas_assets.py tools\aas_inventory\test_inventory_aas_assets.py
python -m json.tool tools\q2aas\validation_manifest.json > $null
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m unittest tools.q2aas.test_validate_worr_q2aas
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m unittest tools.aas_inventory.test_inventory_aas_assets
$env:PYTHONDONTWRITEBYTECODE='1'; python -B tools\aas_inventory\inventory_aas_assets.py --fail-on-missing-required-manifest --fail-on-needs-conversion
meson compile -C builddir-win q2aas-staged-smoke
$env:PYTHONDONTWRITEBYTECODE='1'; python -B tools\q2aas\validate_worr_q2aas.py --tool builddir-win\tools\q2aas\worr_q2aas.exe --cfg tools\q2aas\cfg\worr_q2.cfg --manifest tools\q2aas\validation_manifest.json --skip-missing-manifest-maps --allow-empty-map-set --require-reference-coverage --report-json .tmp\q2aas\reference-coverage-strict-report.json
```

Results:

- Python compile checks passed.
- Manifest JSON validation passed.
- q2aas manifest unit tests passed: `Ran 2 tests ... OK`.
- AAS inventory unit tests passed: `Ran 3 tests ... OK`.
- Inventory exited `0`: `mm-rage` is ready, no discovered BSP-backed map needs
  conversion, required manifest coverage is present, and reference coverage is
  explicitly incomplete.
- `q2aas-staged-smoke` exited `0`, converted `mm-rage`, wrote
  `.tmp/q2aas/validation-report.json`, and reported the seven missing optional
  reference candidates before conversion.
- Strict `--require-reference-coverage` exited `2` as expected because the
  optional reference candidates are not staged. It exits before the normal
  conversion/report-writing phase, so no strict-mode JSON report is expected
  from that command.

The Meson smoke log also printed `ninja: warning: premature end of file;
recovering` after the target completed successfully. The target exit code was
still `0`; watch for recurrence in a quieter worktree because parallel workers
may be touching the same build tree.

## Integration Risks

- The optional candidate paths are placeholders under `.install/basew/maps/`
  until those BSPs are staged. Do not enable `--require-reference-coverage` in a
  required gate until the reference set is available.
- `base2` and `train` are intended coverage candidates for liquids/hazards and
  movers, but the exact travel-count baselines should be added only after their
  generated AAS output is inspected.
- Adding the optional manifest entries means reports count manifest placeholders
  as maps. Inventory consumers should key off `ready`, `needs_conversion`, and
  `reference_coverage`, not only `total_maps`.
