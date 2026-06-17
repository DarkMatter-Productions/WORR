# Q2 AAS Generator Validated AAS Staging

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds an explicit validated staging path for generated Q2 AAS files.
The generator validation helper can now copy a generated `.aas` into
`.install/basew/maps/` only after the map has passed the same strict manifest,
reachability, diagnostic, and baseline gates used by `q2aas-staged-smoke`.

This turns the current `mm-rage` smoke output from a scratch-only artifact into
the first controlled `.install/` AAS staging step.

## Implementation

Updated files:

- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/meson.build`
- `tools/q2aas/README.WORR.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

New validator switches:

- `--stage-aas`
- `--stage-aas-dir <path>`

New Meson run target:

- `q2aas-stage-aas`

The target runs the manifest-driven staged map validation, writes scratch output
under `.tmp/q2aas/`, and then copies the validated `.aas` to
`.install/basew/maps/`. The staged path and SHA-256 hash are recorded in
`.tmp/q2aas/stage-report.json` under each map's `staged_output` object.

## Current Staged Artifact

The first staged map remains the current strict smoke map:

- Input BSP: `.install/basew/maps/mm-rage.bsp`
- Scratch AAS: `.tmp/q2aas/mm-rage.aas`
- Staged AAS: `.install/basew/maps/mm-rage.aas`
- Staged size: `277484` bytes
- Staged SHA-256:
  `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`

The report records:

```json
"staged_output": {
  "enabled": true,
  "status": "staged",
  "aas": "E:\\Repositories\\WORR\\.install\\basew\\maps\\mm-rage.aas",
  "aas_sha256": "6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c"
}
```

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py
python -m json.tool tools\q2aas\validation_manifest.json
meson compile -C builddir-win q2aas-config-smoke
meson compile -C builddir-win q2aas-staged-smoke
meson compile -C builddir-win q2aas-stage-aas
```

Observed `q2aas-stage-aas` result:

- `mm-rage.bsp` strict validation still passes with `428` AAS areas, `562`
  reachability records, and `4` clusters.
- Travel counts remain at `468 walk`, `1 barrier jump`, `7 jump`, `1 ladder`,
  `81 walk off ledge`, `1 elevator`, and `2 rocket jump`.
- `.install/basew/maps/mm-rage.aas` is refreshed only after validation passes.
- `.tmp/q2aas/stage-report.json` records `staged_output.status = staged` and
  the staged AAS hash above.

## Credits and Provenance

No imported upstream BSPC source files were modified in this slice. The upstream
`TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

This work changes WORR-native validation, Meson integration, and documentation
around the imported generator. The credits ledger now records the validated AAS
staging target and report shape beside the existing q2aas validation entries.
