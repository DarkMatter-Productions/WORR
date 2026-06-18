# Q3A BotLib Release Packaging Hardening

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T13`, `FR-04-T16`, `DV-08-T05`, `DV-07-T06`

## Summary

This slice hardens the local `.install` and `pak0.pkz` release contract for
BotLib botfiles and generated q2aas AAS output without touching game/server C++
or q2aas generator internals.

`tools/package_assets.py` now treats `assets/botfiles` as a required release
payload. The packaging step validates the authored botfile family before
writing the archive:

- required support files: `chars.h`, `fw_items.c`, `fw_weap.c`, `inv.h`, and
  `teamplay.h`
- required per-bot profile companions:
  `botfiles/bots/<bot>_c.c`, `_i.c`, `_t.c`, and `_w.c`
- required per-bot script companion:
  `botfiles/scripts/<bot>_s.c`
- no script or profile companion may exist without a matching `<bot>_c.c`
  character profile

After writing `basew/pak0.pkz` and mirroring `botfiles` loose, the script now
hash-validates every botfile member in both locations against the source asset.
This catches missing archive members, stale loose mirrors, and accidental
payload drift during local staging.

`tools/refresh_install.py` now feeds the same botfile member/hash expectations
into `tools/release/validate_stage.py` whenever `--platform-id` validation is
requested. That turns the release-stage validation step into an explicit
archive manifest check for the current botfile scripts and profiles.

For generated AAS output, `refresh_install.py` now requires q2aas staged outputs
to provide a valid SHA-256 before emitting release archive requirements. A
staged AAS entry such as `mm-rage` becomes a validation requirement like
`maps/mm-rage.aas=<sha256>`; missing or malformed hashes fail before
`validate_stage.py` runs.

## Validation

- `python -m py_compile tools\package_assets.py tools\refresh_install.py tools\test_package_assets.py`
  - Result: passed.
- `python tools\test_package_assets.py -v`
  - Result: passed, 8 tests.
  - Coverage includes complete synthetic botfile packaging, stale loose mirror
    removal, authored repository botfiles, missing script failure, botfile
    member hash requirement generation, and q2aas AAS archive requirement
    generation.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .tmp\worker-d-refresh-install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\worker-d-refresh-install\q2aas-package-archive-report.json --q2aas-package-audit-report .tmp\worker-d-refresh-install\q2aas-package-archive-audit-report.json`
  - Result: passed.
  - Packed 93 assets.
  - Validated 30 botfile package/loose files.
  - Injected and audited `maps/mm-rage.aas` with SHA-256
    `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
  - Validated the scratch Windows staged payload under
    `.tmp\worker-d-refresh-install`.

## Integration Risks

- `tools/package_assets.py` now fails if `assets/botfiles` is absent or if a
  bot profile/script family is incomplete. That is intentional for release
  packaging, but any future minimal asset fixture needs to include a complete
  botfile fixture or use a narrower packaging tool.
- Existing q2aas stage reports without `staged_output.aas_sha256` are no longer
  accepted by `refresh_install.py --package-q2aas-aas --platform-id ...`.
  Regenerate the q2aas stage report when this trips.
- The scratch refresh consumed the current `.tmp\q2aas\stage-report.json`, whose
  staged AAS source points at the shared `.install\basew\maps\mm-rage.aas`.
  If that shared staged AAS is stale, regenerate the q2aas stage output before
  release packaging.
