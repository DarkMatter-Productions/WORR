# Q2AAS Reference Map Coverage Round

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This round advances reference-map coverage reporting in the AAS inventory tool
without touching source game code, `q2proto/`, bot scenarios, bot performance
tools, the plan, roadmap, or credits.

The current staged reference set is still only `mm-rage`, so broad reference
coverage remains intentionally incomplete. The inventory report now makes that
state more actionable: required-feature categories need actual BSP feature
evidence, missing categories include structured diagnostics, and the available
reference subset includes compact machine-readable summary counts.

## Implementation

- `tools/aas_inventory/inventory_aas_assets.py` now evaluates
  `reference_coverage.required_features` against inspected loose BSP feature
  evidence before marking a category ready.
- Feature category reports now include `validated_map_count`,
  `feature_ready_map_count`, `feature_gap_count`, and per-candidate
  `feature_status`, `missing_features`, and `observed_features`.
- Manifest-level reference coverage now includes machine-readable summary
  fields: `category_status_counts`, `required_feature_category_count`,
  `feature_ready_category_count`, `feature_incomplete_category_count`,
  `missing_feature_map_count`, `feature_coverage`, `feature_gap_maps`, and
  `missing_category_diagnostics`.
- `available_reference_validation.summary` now reports selected/runtime-ready
  map counts, omitted manifest map counts, unmanifested BSP counts, per-feature
  candidate counts, and reference feature suggestion gap counts.
- Text output now prints feature readiness and missing required feature details
  for incomplete feature-backed categories.

## Current Local State

The inventory command finds one available reference validation map:

- `mm-rage`: staged BSP+AAS, runtime-ready

Discovered BSP feature candidates:

- `elevator=mm-rage`

Still missing or not declared locally:

- canonical deathmatch: `q2dm1`, `q2dm2`
- open deathmatch: `q2dm8` or equivalent
- CTF/team objective: `q2ctf1` or equivalent
- campaign/co-op/mover references: `base1`, `base2`, `train`, or equivalent
- feature-specific references for water, slime, lava, teleports, and doors

The feature-specific categories now distinguish "candidate missing" from
"candidate present but feature absent." A staged BSP can no longer satisfy a
feature category merely by existing; it must show the required BSP/entity
feature evidence.

## Validation

Commands run:

```powershell
python -B -m py_compile tools\aas_inventory\inventory_aas_assets.py tools\aas_inventory\test_inventory_aas_assets.py
python -m json.tool tools\q2aas\validation_manifest.json
python -B -m unittest tools.aas_inventory.test_inventory_aas_assets
python -B -m unittest tools.q2aas.test_validate_worr_q2aas
python -B tools\aas_inventory\inventory_aas_assets.py --fail-on-missing-required-manifest --fail-on-needs-conversion --report-json ""
```

Results:

- Python compile check passed.
- Manifest JSON validation passed.
- AAS inventory tests passed: `Ran 6 tests ... OK`.
- q2aas manifest tests passed: `Ran 3 tests ... OK`.
- Inventory exited `0` with required manifest coverage present, no BSP-backed
  map needing conversion, `mm-rage` selected for focused reference validation,
  and explicit incomplete diagnostics for the unstaged broader reference set.
