# Q2 AAS Generator Diagnostic Gates

Date: 2026-06-16

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice turns the staged `q2aas` diagnostics from report-only data into
enforced validation gates. `q2aas-staged-smoke` now fails if the current staged
map has invalid Quake II BSP lump-table ranges, unmapped player spawn origins,
unmapped item origins, or high-value pickups that cannot be reached from any
generated spawn area.

The goal is to catch generator regressions at the tool boundary before runtime
BotLib loading and bot behavior work depends on bad `.aas` output.

## Implementation

Updated files:

- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/validation_manifest.json`
- `tools/q2aas/meson.build`
- `tools/q2aas/README.WORR.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

New validator switches:

- `--require-clean-bsp-lumps`
- `--require-spawn-coverage`
- `--require-item-coverage`
- `--require-high-value-reachability`

The same requirements can also be set per map in
`tools/q2aas/validation_manifest.json`, allowing stricter gates on stable maps
while future reference maps are being brought up.

`q2aas-staged-smoke` passes all four gates explicitly and the `mm-rage` manifest
entry also opts into all four requirements.

## Gate Semantics

Clean BSP lumps:

- Fails when the Quake II BSP header has any lump with negative offset/length or
  a range extending beyond the file end.
- Still records BSPX marker offsets separately; a BSPX marker after the standard
  Quake II lump range is not itself a failure.

Spawn coverage:

- Fails when there are no player spawn entities.
- Fails when any player spawn has no parseable `origin`.
- Fails when any player spawn origin does not map to generated AAS area bounds.

Item coverage:

- Fails when any item, weapon, ammo, or key entity has no parseable `origin`.
- Fails when any item origin does not map to generated AAS area bounds.
- Does not require every map to contain items.

High-value pickup reachability:

- Fails when any high-value pickup is missing an origin, is outside generated AAS
  area bounds, or is not reachable from the generated spawn-area reachability
  graph.
- The current high-value set covers quad, invulnerability, power screen, power
  shield, BFG, railgun, and rocket launcher entities.

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
meson compile -C builddir-win q2aas-config-smoke
meson compile -C builddir-win q2aas-staged-smoke
```

Observed `q2aas-staged-smoke` result for `.install/basew/maps/mm-rage.bsp`:

- AAS areas: `428`
- Reachability records: `562`
- Clusters: `4`
- Clean BSP lump issues: `0`
- Spawn coverage: `9/9` mapped to generated AAS areas
- Item coverage: `48/48` mapped to generated AAS areas
- High-value reachability: `2` high-value pickup areas, `0` unreachable
- Invalid BSP smoke: passed, failing as expected with
  `ERROR: unknown BSP format BAD!, version 1`

The structured report now includes a `diagnostic_requirements` object per map.
For `mm-rage`, all four gates are `required: true` and `status: passed`.

## Credits and Provenance

The upstream `TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc` and no imported upstream BSPC source
files were modified in this slice.

This work only changes WORR-native validation/build/documentation files around
the imported generator. The credits ledger was updated to record the stricter
validation behavior and the per-map manifest gate requirements.
