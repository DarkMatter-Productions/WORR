# Q3A BotLib Botfiles Loose Staging

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Summary

This slice makes repository bot profile files visible to dedicated-server
profile loading in local staged installs even when the current build cannot
mount `.pkz` archives.

`tools/package_assets.py` still writes the canonical `basew/pak0.pkz`, but it
now also mirrors configured loose asset paths beside the archive. The default
loose mirror is `botfiles`, which places profile scripts under
`.install/basew/botfiles/...` during `refresh_install.py`.

## Why This Was Needed

The first native profile pack was correctly included inside
`.install/basew/pak0.pkz`, but the local Windows dedicated build has
`USE_ZLIB 0`. In that configuration the engine does not add `.pkz` files to the
filesystem search path, so `FS_ListFiles("botfiles/bots", ".c", ...)` saw zero
profiles even though Python archive inspection showed the files were present.
After the Q3-style reshape, that same scan sees `*_c.c` profile entry points and
`*_w/_i/_t.c` companions from the loose mirror.

Loose botfile staging keeps the asset package intact while making server-side
script discovery deterministic for no-zlib dedicated builds.

## Implementation Notes

- Added `--loose-dir` support to `tools/package_assets.py`.
- The default loose path is `botfiles`.
- Each configured loose path is validated as a relative asset path.
- The staged destination is removed before copying so stale bot profiles do not
  survive a later refresh.
- `refresh_install.py` picks up the behavior automatically because it already
  calls `tools/package_assets.py`.
- Added `tools/test_package_assets.py` coverage for archive membership, loose
  mirroring, and stale loose-file removal.

## Validation

- `python -m py_compile tools\package_assets.py tools\test_package_assets.py`
  - Result: passed.
- `python tools\test_package_assets.py`
  - Result: passed, 2 tests.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
  - Result: passed and reported `Mirrored loose asset paths: botfiles`; the
    current package pass packs 84 files from `assets`.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario profile_backed_spawn --binary .install\worr_ded_x86_64.exe --install-dir .install --artifact-dir .tmp\bot_scenarios --json-out .tmp\bot_scenarios\profile_backed_spawn_report.json --format text`
  - Result: passed.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --binary .install\worr_ded_x86_64.exe --install-dir .install --artifact-dir .tmp\bot_scenarios --json-out .tmp\bot_scenarios\implemented_report.json --format text`
  - Result: passed, 5 implemented scenarios.

## Remaining Risks

- This does not enable `.pkz` mounting in no-zlib builds. It deliberately keeps
  the local staged server behavior correct without changing archive support.
- If future server-only builds require more script-style assets that are loaded
  through `FS_ListFiles`, add them as explicit `--loose-dir` paths rather than
  mirroring the whole asset tree by default.
