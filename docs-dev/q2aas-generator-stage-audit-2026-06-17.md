# Q2 AAS Generator Staged Artifact Audit

Date: 2026-06-17

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds the first q2aas-specific packaging audit hook. After
`q2aas-stage-aas` validates and copies generated `.aas` files into
`.install/basew/maps/`, the new `q2aas-stage-audit` target verifies that the
staged files still exist, live under the expected staging directory, have a
non-zero size, and match the SHA-256 hashes recorded in
`.tmp/q2aas/stage-report.json`.

This does not yet make AAS files part of the final release package contract.
It gives `FR-04-T16` a repeatable staged-artifact integrity check that can be
wired into broader release validation once the runtime bot path starts loading
the generated AAS files.

## Implementation

Updated files:

- `tools/q2aas/audit_worr_q2aas_stage.py`
- `tools/q2aas/meson.build`
- `tools/q2aas/README.WORR.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`

New Meson run target:

- `q2aas-stage-audit`

The audit helper reads `.tmp/q2aas/stage-report.json` by default and writes
`.tmp/q2aas/stage-audit-report.json` with schema
`worr-q2aas-stage-audit-v1`.

The audit currently checks:

- every required map entry has `staged_output.enabled = true`
- staged output status is `staged`
- staged `.aas` path is under `.install/basew/maps/`
- staged file exists and uses the `.aas` extension
- staged file size is greater than zero
- staged file hash matches the staged-output hash and generated scratch AAS hash

## Current Audit Result

The current staged map audit passes:

- Staged AAS: `.install/basew/maps/mm-rage.aas`
- Size: `277484` bytes
- SHA-256:
  `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`
- Audit report: `.tmp/q2aas/stage-audit-report.json`
- Audit report status: `passed`

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\audit_worr_q2aas_stage.py tools\q2aas\validate_worr_q2aas.py
meson setup builddir-win --reconfigure
meson compile -C builddir-win q2aas-stage-aas
meson compile -C builddir-win q2aas-stage-audit
python -m json.tool .tmp\q2aas\stage-audit-report.json
```

Observed `q2aas-stage-audit` result:

- `mm-rage` passed staged artifact verification.
- The staged hash matches both the staged-output report hash and the generated
  scratch AAS hash.
- `.tmp/q2aas/stage-audit-report.json` records `map_count = 1`,
  `failed_count = 0`, and `status = passed`.

## Credits and Provenance

No imported upstream BSPC source files were modified in this slice. The upstream
`TTimo/bspc` snapshot remains pinned at
`10d23c5ebd042ddc5d03e17de0f560f5076649dc`.

This work adds WORR-native audit tooling, Meson integration, and documentation
around the imported generator. The credits ledger now records the staged AAS
audit helper separately from the vendored upstream BSPC snapshot.
