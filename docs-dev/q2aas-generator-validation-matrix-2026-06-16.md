# Q2 AAS Generator Validation Matrix

Date: 2026-06-16

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This round makes the WORR Q2 AAS generator validation repeatable instead of
single-command local smoke testing. The validator now accepts a JSON manifest,
writes a structured report under `.tmp/q2aas/`, and includes an expected-failure
smoke check for deliberately invalid BSP input.

## Implementation

- Added `tools/q2aas/validation_manifest.json` as the staged-map matrix seed.
  It currently lists `.install/basew/maps/mm-rage.bsp` as a required strict
  reachability smoke map and records pending reference-map categories.
- Extended `tools/q2aas/validate_worr_q2aas.py` with:
  - manifest loading through `--manifest`;
  - missing-map tolerance for clean checkouts through `--skip-missing-manifest-maps`;
  - empty map-set tolerance for automation through `--allow-empty-map-set`;
  - per-map metrics and travel-count extraction;
  - JSON report output through `--report-json`;
  - invalid-input expected-failure coverage through `--invalid-input-smoke`.
- Added the Meson run target `q2aas-staged-smoke`, which runs the manifest,
  requires Quake II `IBSP` version 38 input, requires reachability/clusters,
  writes deterministic AAS metadata sidecars, runs invalid-input smoke coverage,
  and writes `.tmp/q2aas/validation-report.json`.
- Tightened `tools/q2aas/bspc.c` so conversion paths abort when `LoadMapFromBSP`
  fails. This is a local modification to an imported file and is documented in
  the credits ledger with the existing `Modified for WORR 2026-06-16` note.

## Validation Results

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
meson compile -C builddir-win worr_q2aas
meson compile -C builddir-win q2aas-staged-smoke
```

`q2aas-staged-smoke` passed with:

- `mm-rage.bsp`: `numareas = 428`, `numareasettings = 428`,
  `reachabilitysize = 562`, `numclusters = 4`.
- Travel counts: `468 walk`, `1 barrier jump`, `7 jump`, `1 ladder`,
  `81 walk off ledge`, `1 elevator`, `2 rocket jump`.
- Invalid BSP smoke: passed by returning exit code `1` with
  `ERROR: unknown BSP format BAD!, version 1`.
- Scratch report: `.tmp/q2aas/validation-report.json`.

The generated scratch output was cleaned of transient `bspc.log` and
`invalid-input-smoke.bsp` files after validation.

## Current Limits

Only `mm-rage.bsp` is staged in this workspace, so the manifest remains a seed
matrix rather than the full Quake II reference set. Next generator validation
rounds should add representative deathmatch, campaign/co-op, water/liquid, and
mover-heavy maps before treating generated `.aas` files as release assets.
