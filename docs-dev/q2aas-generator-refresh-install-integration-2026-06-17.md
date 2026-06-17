# Q2 AAS Generator Refresh Install Integration

Date: 2026-06-17

Related tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice integrates q2aas AAS archive packaging with the normal
`tools/refresh_install.py` staging workflow. The refresh workflow rebuilds
`.install/basew/pak0.pkz` from `assets/`, which would otherwise remove generated
AAS members that were injected by `q2aas-package-aas`.

`refresh_install.py` now has an explicit `--package-q2aas-aas` option. When
enabled, it runs the q2aas package helper after the asset archive is rebuilt,
then runs the archive-required q2aas package audit before optional platform
stage validation. A later hardening slice also feeds the staged AAS member names
and hashes into the generic staged-release validator when `--platform-id` is
combined with `--package-q2aas-aas`; see
`docs-dev/q2aas-generator-stage-archive-member-validation-2026-06-17.md`.

## Implementation

Added refresh options:

- `--package-q2aas-aas`
- `--q2aas-stage-report`
- `--q2aas-package-report`
- `--q2aas-package-audit-report`

The new refresh order is:

1. Stage runtime and base-game files into `.install/`.
2. Rebuild `.install/basew/pak0.pkz` from `assets/`.
3. If requested, package validated q2aas staged AAS into `pak0.pkz`.
4. If requested, audit packaged q2aas AAS archive members with
   `--require-archive-member`.
5. Run platform stage validation when `--platform-id` is supplied, including
   required packaged AAS member/hash checks when q2aas packaging is enabled.

This keeps q2aas packaging opt-in while making the release-staging workflow able
to preserve generated navigation data deliberately.

## Validation

Commands run:

```powershell
python -m py_compile tools\refresh_install.py tools\q2aas\package_worr_q2aas_archive.py tools\q2aas\audit_worr_q2aas_package.py
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Refresh result:

- assets repacked: `63` files from `assets/`
- q2aas archive packaging: `maps/mm-rage.aas` added to
  `.install/basew/pak0.pkz`
- staged and archived AAS SHA-256:
  `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`
- q2aas archive audit: `status = passed`, `map_count = 1`,
  `failed_count = 0`, `policy = archive-required`
- staged install validation: `windows-x86_64` passed

## Credits and Provenance

This is WORR-native staging integration around the credited `TTimo/bspc`
generator import. No imported BSPC source files were changed in this slice.
The credits ledger records the refresh integration under the WORR-native q2aas
tooling rows.
