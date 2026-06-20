# Q2AAS Reference Map Validation Expansion

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This worker pass expands reference-map validation support without changing
`q2proto/` or server-game bot code.

The AAS inventory tool now separates manifest placeholders from real staged
assets, discovers BSP feature candidates from loose Quake II BSP contents, and
emits a deterministic focused validation manifest for the reference maps that
are actually present locally. In the current workspace, that focused set is
`mm-rage`, which is also runtime-ready because both
`.install/basew/maps/mm-rage.bsp` and `.install/basew/maps/mm-rage.aas` exist.

## Implementation

- `tools/aas_inventory/inventory_aas_assets.py` now records per-map
  `bsp_features` for loose BSPs by reusing the q2aas BSP/entity inspection
  helpers. The feature report covers water, slime, lava, teleport, elevator,
  and door evidence.
- Pending reference labels now only match maps with real BSP/AAS/source assets.
  Optional manifest declarations such as `q2dm1` no longer appear as found
  until an asset is actually staged.
- Inventory reports now include `available_reference_validation`, with selected
  manifest-declared BSPs, runtime-ready BSP+AAS maps, omitted declared maps,
  unmanifested BSPs, feature candidate map IDs, and friendly validation
  commands.
- `--available-reference-manifest <path>` writes a focused
  `worr-q2aas-validation-manifest-v1` file containing only currently available
  manifest-declared BSPs. It preserves the relevant map gates and keeps only
  reference coverage categories that the focused set can actually validate.
- `tools/aas_inventory/test_inventory_aas_assets.py` covers the pending-map
  false positive fix, focused manifest generation, manifest writing, and
  deterministic water-feature candidate discovery from a tiny synthetic IBSP38
  fixture.

## Current Local Result

Generated focused manifest:

```powershell
.tmp\q2aas\available-reference-validation-manifest.json
```

Current focused selection:

- selected map IDs: `mm-rage`
- runtime-ready map IDs: `mm-rage`
- discovered BSP feature candidates: `elevator=mm-rage`
- still missing manual/staged data: `q2dm1`, `q2dm2`, `q2dm8`, `q2ctf1`,
  `base1`, `base2`, `train`, and maps that exercise water, slime, lava,
  teleports, or doors

Focused validation passed with strict reference coverage enabled. The generated
report at `.tmp/q2aas/available-reference-validation-report.json` records:

- reference coverage status: `passed`
- reference feature readiness: `passed`
- validated feature category: `elevator_reference`
- `mm-rage` travel counts still include `468 walk`, `7 jump`, `1 ladder`,
  `81 walk off ledge`, `1 elevator`, and `2 rocket jump`

## Validation

Commands run:

```powershell
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m py_compile tools\aas_inventory\inventory_aas_assets.py tools\aas_inventory\test_inventory_aas_assets.py
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m unittest tools.aas_inventory.test_inventory_aas_assets
$env:PYTHONDONTWRITEBYTECODE='1'; python -B tools\aas_inventory\inventory_aas_assets.py --fail-on-missing-required-manifest --fail-on-needs-conversion --available-reference-manifest .tmp\q2aas\available-reference-validation-manifest.json --report-json .tmp\aas_inventory\asset-inventory.json
python -m json.tool .tmp\q2aas\available-reference-validation-manifest.json
$env:PYTHONDONTWRITEBYTECODE='1'; python -B tools\q2aas\validate_worr_q2aas.py --tool builddir-win\tools\q2aas\worr_q2aas.exe --cfg tools\q2aas\cfg\worr_q2.cfg --manifest .tmp\q2aas\available-reference-validation-manifest.json --require-q2-bsp --require-reachability --require-clean-bsp-lumps --require-spawn-coverage --require-item-coverage --require-high-value-reachability --write-aas-metadata --require-reference-coverage --report-json .tmp\q2aas\available-reference-validation-report.json
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m unittest tools.q2aas.test_validate_worr_q2aas
$env:PYTHONDONTWRITEBYTECODE='1'; python -B -m py_compile tools\q2aas\validate_worr_q2aas.py tools\q2aas\test_validate_worr_q2aas.py
```

Results:

- AAS inventory unit tests passed: `Ran 5 tests ... OK`.
- q2aas manifest unit tests passed: `Ran 3 tests ... OK`.
- Inventory exited `0`; `mm-rage` is ready, no discovered BSP-backed map needs
  conversion, and all pending reference map labels are correctly reported as
  not staged.
- Focused q2aas validation exited `0`, converted `mm-rage`, enforced the
  existing structural/diagnostic baselines, and passed strict reference coverage
  for the focused subset.

## Remaining Data Dependency

The broader reference checklist still needs real BSP data staged under the scan
roots, preferably `.install/basew/maps/`, before full coverage can pass:

- canonical deathmatch: `q2dm1`, `q2dm2`
- open-layout deathmatch: `q2dm8` or equivalent
- CTF/team objective: `q2ctf1` or equivalent
- campaign/coop/mover coverage: `base1`, `base2`, `train`, or equivalent
- natural movement/hazard coverage: maps with water, slime, lava, teleports,
  and doors

After staging new BSPs, rerun:

```powershell
python -B tools\aas_inventory\inventory_aas_assets.py --available-reference-manifest .tmp\q2aas\available-reference-validation-manifest.json
python -B tools\q2aas\validate_worr_q2aas.py --manifest .tmp\q2aas\available-reference-validation-manifest.json --require-q2-bsp --require-reachability --require-clean-bsp-lumps --require-spawn-coverage --require-item-coverage --require-high-value-reachability --write-aas-metadata --require-reference-coverage --report-json .tmp\q2aas\available-reference-validation-report.json
```
