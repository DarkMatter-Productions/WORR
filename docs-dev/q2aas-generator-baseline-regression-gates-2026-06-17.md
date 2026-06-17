# Q2 AAS Generator Baseline Regression Gates

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds manifest-driven baseline minima to the staged `q2aas`
validation path. The previous diagnostic gates made `q2aas-staged-smoke` fail
when generated AAS coverage or high-value reachability regressed. The new
baseline gates catch another class of failures: the map still generates, but
the output loses important structural or route data.

The gate data is map-specific and lives in `tools/q2aas/validation_manifest.json`
so future reference maps can carry their own baselines without hardcoded script
logic.

## Implementation

Updated files:

- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/validation_manifest.json`
- `tools/q2aas/README.WORR.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

New manifest fields:

- `minimum_metrics`
- `minimum_travel_counts`

The validator normalizes these fields as non-negative integer threshold maps,
fails manifest loading on malformed threshold values, and records per-threshold
results under each map's `baseline_requirements` report object.

## Current Baseline

The staged `mm-rage` map now requires these minimum structural metrics:

- `numareas >= 428`
- `numareasettings >= 428`
- `reachabilitysize >= 562`
- `numclusters >= 4`

It also requires these minimum travel counts:

- `walk >= 468`
- `barrier jump >= 1`
- `jump >= 7`
- `ladder >= 1`
- `walk off ledge >= 81`
- `elevator >= 1`

Rocket-jump candidates are intentionally not part of this first baseline gate.
The generator currently emits them, but runtime policy and `sg_bot_*` control
for rocket-jump use are still pending.

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
meson compile -C builddir-win q2aas-config-smoke
meson compile -C builddir-win q2aas-staged-smoke
python -m json.tool tools\q2aas\validation_manifest.json
```

Observed `q2aas-staged-smoke` result for `.install/basew/maps/mm-rage.bsp`:

- AAS areas: `428`
- Area settings: `428`
- Reachability records: `562`
- Clusters: `4`
- Travel counts: `468 walk`, `1 barrier jump`, `7 jump`, `1 ladder`,
  `81 walk off ledge`, `1 elevator`
- `baseline_requirements.minimum_metrics`: `required: true`, `status: passed`
- `baseline_requirements.minimum_travel_counts`: `required: true`,
  `status: passed`

The staged smoke still writes:

- `.tmp/q2aas/mm-rage.aas`
- `.tmp/q2aas/mm-rage.aas.meta.json`
- `.tmp/q2aas/validation-report.json`

The invalid BSP smoke still fails as expected with
`ERROR: unknown BSP format BAD!, version 1`.

## Credits and Provenance

No imported upstream BSPC source files were modified in this slice. The upstream
`TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

This work changes WORR-native validation, manifest, and documentation files
around the imported generator. The credits ledger now records the baseline
regression gate behavior beside the existing q2aas validation entries.
