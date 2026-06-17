# Q2 AAS Generator Archive Packaging

Date: 2026-06-17

Related tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice makes generated AAS packaging explicit. After strict q2aas validation
and loose `.install/basew/maps/` staging, `q2aas-package-aas` now injects the
staged `.aas` files into `.install/basew/pak0.pkz` and writes a machine-readable
archive report.

This moves the package policy from "loose-or-archive is acceptable" to a
repeatable archive-backed path for generated navigation data, while still
leaving the loose staged file in place for debugging and hash comparison.

## Implementation

Added:

- `tools/q2aas/package_worr_q2aas_archive.py`
- Meson target `q2aas-package-aas`
- Meson target `q2aas-package-archive-audit`
- `.tmp/q2aas/package-archive-report.json`
- `.tmp/q2aas/package-archive-audit-report.json`

The package helper reads `.tmp/q2aas/stage-report.json`, verifies each staged
AAS file exists and matches the staged-output SHA-256, rebuilds
`.install/basew/pak0.pkz` with the generated `maps/<map>.aas` member, and writes
schema `worr-q2aas-package-archive-v1`.

The strict archive audit reuses `audit_worr_q2aas_package.py` with
`--require-archive-member`, requiring both the stage report and the package
archive to agree on size and SHA-256.

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\package_worr_q2aas_archive.py tools\q2aas\audit_worr_q2aas_package.py
python tools\q2aas\package_worr_q2aas_archive.py --report-json .tmp\q2aas\stage-report.json --install-dir .install --base-game basew --archive-name pak0.pkz --package-report-json .tmp\q2aas\package-archive-report.json
python tools\q2aas\audit_worr_q2aas_package.py --report-json .tmp\q2aas\stage-report.json --install-dir .install --base-game basew --archive-name pak0.pkz --require-archive-member --audit-report-json .tmp\q2aas\package-archive-audit-report.json
python -m json.tool .tmp\q2aas\package-archive-report.json
python -m json.tool .tmp\q2aas\package-archive-audit-report.json
```

Current archive result:

- `maps/mm-rage.aas` is present in `.install/basew/pak0.pkz`
- staged AAS size: `277484` bytes
- staged and archived AAS SHA-256:
  `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`
- the first package run reported `added_count = 1`; later package runs are
  idempotent and report `replaced_count = 1`
- `package-archive-audit-report.json`: `status = passed`, `map_count = 1`,
  `failed_count = 0`, `policy = archive-required`

## Credits and Provenance

This is WORR-native packaging tooling around the credited `TTimo/bspc`
generator import. No imported BSPC source files were changed in this slice.
The credits ledger records the new package helper and Meson targets under the
WORR-native q2aas tooling rows.
