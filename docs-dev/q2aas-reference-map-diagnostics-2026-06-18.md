# Q2AAS Reference Map Diagnostics

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This pass tightens q2aas reference-map reporting around the remaining feature
coverage gaps without changing generator C internals. No `q2proto/` files were
changed.

Only `.install/basew/maps/mm-rage.bsp` is staged locally, so no new optional BSP
map entries were added. The manifest now records feature-specific readiness
categories for water, slime, lava, teleport, elevator, and door coverage. The
elevator category is backed by staged `mm-rage`; water, slime, lava, teleport,
and door coverage are explicit no-candidate-yet gaps.

## Implementation

- `tools/q2aas/validate_worr_q2aas.py` now accepts `required_features` and
  `strict_required` on `reference_coverage` entries.
- Reference coverage reports now include candidate absence counts, missing
  optional candidate counts, per-category strict gate records, and top-level
  strict gate summaries.
- Per-map diagnostics now include `diagnostics.coverage_features`, derived from
  Q2 BSP brush contents, mover/teleport entity groups, and generated AAS travel
  counts.
- The validation JSON report now includes `reference_feature_readiness`, which
  verifies whether converted candidate maps actually demonstrate their required
  feature coverage.
- `tools/aas_inventory/inventory_aas_assets.py` mirrors the static coverage
  vocabulary so asset-only reports show feature requirements, optional candidate
  absence, and strict gate status.
- `tools/q2aas/validation_manifest.json` adds granular feature coverage
  categories without claiming unavailable BSPs.

## Current Local State

The staged smoke still validates only `mm-rage`.

`mm-rage` feature diagnostics currently report:

- `elevator=present`
- `water=absent`
- `slime=absent`
- `lava=absent`
- `teleport=absent`
- `door=absent`

Strict reference coverage correctly fails today because the wider reference
set is not staged. The failed strict categories are
`id_deathmatch_reference`, `open_deathmatch_reference`, `ctf_reference`,
`campaign_reference`, `liquid_or_hazard_reference`, `water_reference`,
`slime_reference`, `lava_reference`, `teleport_reference`, and
`door_reference`.

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
- q2aas manifest tests passed: `Ran 3 tests ... OK`.
- AAS inventory tests passed: `Ran 3 tests ... OK`.
- Inventory exited `0` for the normal non-strict policy and now reports no
  optional candidate map declared for water, slime, lava, teleport, and door.
- `q2aas-staged-smoke` exited `0`, regenerated the validation report, and
  recorded `elevator` as the only ready feature on `mm-rage`.
- Strict `--require-reference-coverage` exited `2` as expected because the
  remaining reference categories are absent.

The Meson smoke again printed `ninja: warning: premature end of file;
recovering` after the target completed successfully. The target exit code was
still `0`.

## Follow-ups

- Add optional manifest map entries for water, slime, lava, teleport, and door
  only after the BSPs are actually present in `.install/basew/maps` or another
  validated source path.
- Once staged, promote feature categories from candidate absence to real
  `map_ids`, then add generated AAS travel-count or diagnostic baselines.
