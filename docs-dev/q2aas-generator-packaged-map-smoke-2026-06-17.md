# Q2 AAS Generator Packaged Map Smoke

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice teaches the q2aas validator about archive-backed WORR map inputs.
Validation manifest entries can now use `archive` plus `archive_member` instead
of a loose `path`, allowing future staged `basew/pak0.pkz` map entries to be
extracted under `.tmp/q2aas/packaged-maps/` before AAS conversion.

The current staged `pak0.pkz` does not contain maps yet, so this round also
adds a self-contained package-map smoke. The smoke builds a scratch
`.tmp/q2aas/package-map-smoke.pkz` from `.install/basew/maps/mm-rage.bsp`,
extracts `maps/mm-rage.bsp` through the same archive resolver, and runs strict
Q2 AAS validation on the extracted BSP.

## Implementation

Updated files:

- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/meson.build`
- `tools/q2aas/README.WORR.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

New validator switches:

- `--package-map-smoke`
- `--package-map-smoke-source <path>`
- `--packaged-map-cache-dir <path>`

New Meson run target:

- `q2aas-package-map-smoke`

Manifest map entries now support either:

```json
{
  "id": "loose-map",
  "path": ".install/basew/maps/mm-rage.bsp"
}
```

or:

```json
{
  "id": "packaged-map",
  "archive": ".install/basew/pak0.pkz",
  "archive_member": "maps/mm-rage.bsp"
}
```

Archive member paths are normalized as relative POSIX paths and rejected if
they contain absolute paths, empty components, `.`, or `..`.

## Current Smoke Result

`q2aas-package-map-smoke` currently reports:

- Scratch archive: `.tmp/q2aas/package-map-smoke.pkz`
- Archive member: `maps/mm-rage.bsp`
- Extracted BSP:
  `.tmp/q2aas/packaged-maps/package-map-smoke-9e6651844cee/maps/mm-rage.bsp`
- AAS output: `.tmp/q2aas/package-map-smoke-mm-rage.aas`
- BSP SHA-256:
  `6d95267b839baa7ed66367ff86eb4d886e0aad316bff9d1febf350738b0aaca0`
- AAS SHA-256:
  `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`
- Archive SHA-256:
  `b686131f478fa1a3fa720cdb14f02eabadec9dcdbad35182c4c38d79cfbe96d2`

The package smoke validates the extracted BSP with the same strict Q2 gates
used by the staged map smoke: Q2 BSP header preflight, reachability, clean BSP
lumps, spawn coverage, item coverage, and high-value pickup reachability.

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\validate_worr_q2aas.py tools\q2aas\audit_worr_q2aas_stage.py
python -m json.tool tools\q2aas\validation_manifest.json
meson setup builddir-win --reconfigure
meson compile -C builddir-win q2aas-package-map-smoke
python -m json.tool .tmp\q2aas\package-map-smoke-report.json
```

Observed `q2aas-package-map-smoke` result:

- `package-map-smoke-mm-rage` passed strict Q2 validation.
- Report path: `.tmp/q2aas/package-map-smoke-report.json`
- AAS metrics: `428` areas, `562` reachability records, and `4` clusters.
- Travel counts: `468 walk`, `1 barrier jump`, `7 jump`, `1 ladder`,
  `81 walk off ledge`, `1 elevator`, and `2 rocket jump`.
- Diagnostics still report `9` spawn points, `48` pickups, `0` spawn/item
  origin orphans, and `0` unreachable high-value pickups.

## Credits and Provenance

No imported upstream BSPC source files were modified in this slice. The upstream
`TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

This work changes WORR-native validation, Meson integration, and documentation
around the imported generator. The credits ledger now records archive-backed
map validation and packaged-map smoke behavior under the WORR-native q2aas
validation helper and Meson wrapper rows.
