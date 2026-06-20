# Q3A BotLib and q2aas Release Policy

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T13`, `FR-04-T16`, `DV-08-T05`, `DV-07-T06`

## Summary

This slice adds a conservative release policy gate for the q2aas/BSPC toolchain
and makes imported-source notice material a required part of staged binary
packages.

The policy is:

- Generated `.aas` data may be packaged only through the existing explicit
  `refresh_install.py --package-q2aas-aas` path and its staged-output hash
  checks.
- q2aas/BSPC tool binaries such as `worr_q2aas.exe`, `q2aas.exe`, `bspc.exe`,
  and matching debug/library sidecars are not distributed by default.
- If a future release flow deliberately distributes q2aas tool binaries, that
  flow must be explicit and must validate the required GPL/credit sidecars in
  the same package.
- Current binary releases must carry the source/credit notice bundle under
  `licenses/`.

## Encoded Gates

`tools/package_assets.py` now rejects q2aas/BSPC tool binaries in the canonical
asset payload before writing `pak0.pkz`. This protects the game asset archive
from accidental tool-binary inclusion if a local build artifact is copied under
`assets/`.

`tools/refresh_install.py` now scans the staged `.install` tree after runtime
staging and fails if a q2aas/BSPC tool binary is present. It also stages and
validates this non-empty notice bundle:

- `licenses/WORR-LICENSE.txt`
- `licenses/q2aas-bspc-LICENSE.txt`
- `licenses/q3a-botlib-aas-credits.md`
- `licenses/q2aas-README.WORR.md`
- `licenses/q3a-botlib-README.WORR.md`

`tools/release/targets.py` now requires those notice files in client, server,
and update payload manifests, includes `licenses/*` in release package filters,
and marks q2aas/BSPC tool artifact names as forbidden paths.

`tools/release/verify_artifacts.py` now treats required release notice files
with missing or zero-byte manifest sizes as verification failures.

## User-Facing Impact

Published server packages now include a `licenses/` directory. Operators should
keep that directory with the package when copying or mirroring a server install.
No server launch commands or botfile override locations changed.

## Validation

- `python -m py_compile tools\package_assets.py tools\refresh_install.py tools\test_package_assets.py tools\release\targets.py tools\release\verify_artifacts.py tools\release\tests\test_target_contract.py`
  - Result: passed.
- `python tools\test_package_assets.py -v`
  - Result: passed, 11 tests.
- `python -m unittest tools.release.tests.test_target_contract -v`
  - Result: passed, 5 tests.

## Notes

No `tools/q2aas/` source files, q2aas validation manifests, `tools/aas_inventory/`
files, or `q2proto/` files were changed. This is release packaging policy and
distribution validation only.
