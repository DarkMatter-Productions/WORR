# Q3A BotLib Botfiles Scripts Package Coverage

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Summary

This packaging lane verifies that Q3-style `botfiles/scripts` payloads are not
lost when WORR stages botfiles for the Q3A botlib/AAS port.

`tools/package_assets.py` already packs every file under `assets/` into
`.install/basew/pak0.pkz` and mirrors `assets/botfiles` loose beside the archive
by default. The first regression coverage creates a synthetic
`assets/botfiles/scripts/...` file and checks that it appears in both locations:

- `basew/pak0.pkz` as `botfiles/scripts/...`
- `.install/basew/botfiles/scripts/...` as a loose file

Follow-up coverage now also uses the real authored repository scripts under
`assets/botfiles/scripts/*_s.c`. The regression enumerates the authored script
set and verifies each script is present in the generated archive and byte-equal
in the loose `.install/basew/botfiles/scripts/` mirror. This covers the
`refresh_install.py` path too because refresh delegates the asset pack/mirror
step to `tools/package_assets.py`.

The authored script set covered by the regression is:

- `botfiles/scripts/bulwark_s.c`
- `botfiles/scripts/relay_s.c`
- `botfiles/scripts/smoke_s.c`
- `botfiles/scripts/vanguard_s.c`
- `botfiles/scripts/vector_s.c`

## Validation

- `python -m py_compile tools\package_assets.py tools\test_package_assets.py tools\bot_profiles\validate_bot_profiles.py tools\bot_profiles\test_validate_bot_profiles.py`
- `python tools\test_package_assets.py`
- `python tools\bot_profiles\test_validate_bot_profiles.py`
- `python tools\bot_profiles\validate_bot_profiles.py`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .tmp\worker-p-refresh-install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64`
  - Result: passed; packed 93 files, mirrored loose `botfiles`, and validated the scratch staged payload.
- Direct scratch refresh audit:
  - Expected authored scripts: 5.
  - Missing archive members: none.
  - Missing loose mirror files: none.
