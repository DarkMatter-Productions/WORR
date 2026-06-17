# Q2 AAS Generator Package Audit

Date: 2026-06-17

Related tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds a package-readiness audit for generated q2aas output after the
validated `.aas` staging step. The new audit confirms that every staged AAS
reported by `.tmp/q2aas/stage-report.json` is represented in the local release
payload, either as a loose file under `.install/basew/` or as an archive member
inside `.install/basew/pak0.pkz`.

The current policy is intentionally `loose-or-archive`: `mm-rage.aas` is staged
as `.install/basew/maps/mm-rage.aas`, while the current `pak0.pkz` remains a
readable asset archive without generated AAS members. The audit reports that
distinction explicitly so the future packaging policy can tighten to archive
membership without changing the staged AAS validation chain.

## Implementation

Added:

- `tools/q2aas/audit_worr_q2aas_package.py`
- Meson target `q2aas-package-audit`
- `.tmp/q2aas/package-audit-report.json`

The audit reads the stage report, resolves each `staged_output.aas` path against
`.install/basew/`, verifies the loose staged file and hash, opens `pak0.pkz` as
a zip/pkz archive, and looks for the matching archive member such as
`maps/mm-rage.aas`.

The report uses schema `worr-q2aas-package-audit-v1` and records:

- install root, base game directory, package archive path, archive hash, and
  archive member count
- one entry per staged AAS map
- expected SHA-256 from the q2aas stage report
- loose-file presence, size, and hash
- archive-member presence, size, and hash when present
- the active policy, currently `loose-or-archive`

## Validation

Commands run:

```powershell
python -m py_compile tools\q2aas\audit_worr_q2aas_package.py
python tools\q2aas\audit_worr_q2aas_package.py --report-json .tmp\q2aas\stage-report.json --install-dir .install --base-game basew --archive-name pak0.pkz --audit-report-json .tmp\q2aas\package-audit-report.json
python -m json.tool .tmp\q2aas\package-audit-report.json
```

Current result:

- `status = passed`
- `map_count = 1`
- `failed_count = 0`
- `mm-rage`: `loose = present`, `archive = missing`, `represented = true`
- `.install/basew/pak0.pkz`: readable, `member_count = 63`

## Credits and Provenance

This is WORR-native validation tooling around the credited `TTimo/bspc`
generator import. No imported BSPC source files were changed in this slice.
The credits ledger records the new audit helper and Meson target under the
WORR-native q2aas tooling rows.
