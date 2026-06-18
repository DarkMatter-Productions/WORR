# Q3-Style WORR Botfiles Correction

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-07-T06`, `DV-08-T05`

## Summary

The first native WORR bot profile seed used flat placeholder `.c` files. That
was useful for proving package discovery and profile-backed spawning, but it
did not resemble the Q3A or Gladiator botfile layout closely enough to be a good
foundation.

This correction reshapes the profile pack into a Q3/Gladiator-style script
family:

- `botfiles/chars.h`
- `botfiles/bots/<id>_c.c` for character/profile data
- `botfiles/bots/<id>_w.c` for weapon weights
- `botfiles/bots/<id>_i.c` for item weights
- `botfiles/bots/<id>_t.c` for chat metadata

Local Q3A and Gladiator botfiles were used as format references for the layout,
suffix conventions, and broad vocabulary only. The WORR botfiles are original
first-party content; no external botfile text was copied.

## Runtime Changes

- `SV_BotReloadProfiles()` still scans `botfiles/bots/*.c`.
- `*_c.c` entry points strip `_c` from the runtime profile id, so
  `smoke_c.c` becomes `smoke`.
- `*_w.c`, `*_i.c`, and `*_t.c` companion scripts are logged as
  `reason=companion_script` and skipped as profile records.
- The parser now maps Q3-style character fields into the existing profile
  bridge:
  - `CHARACTERISTIC_NAME`
  - `CHARACTERISTIC_REACTIONTIME`
  - `CHARACTERISTIC_AGGRESSION`
  - WORR extensions such as `WORR_SKIN`, `WORR_TEAM`,
    `WORR_PREFERRED_WEAPON`, and `WORR_MOVEMENT_STYLE`
- `CHARACTERISTIC_REACTIONTIME` is authored in seconds and normalized to the
  existing millisecond `bot_reaction` userinfo value.

## Tooling Changes

- `tools/bot_profiles/validate_bot_profiles.py` strips `_c`, skips companion
  scripts during directory scans, accepts Q3-style `CHARACTERISTIC_*` fields,
  and validates the WORR extension keys used by the current bridge.
- `tools/test_package_assets.py` now exercises a `smoke_c.c` asset member rather
  than the obsolete flat `smoke.c` shape.
- `tools/bot_scenarios/README.md` now identifies `smoke_c.c` as the staged
  profile file for `profile_backed_spawn`.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
  - Result: passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
  - Result: passed; `.install/basew/pak0.pkz` contains the Q3-style botfiles and
    `.install/basew/botfiles` mirrors them loose.
- `python tools\bot_profiles\validate_bot_profiles.py`
  - Result: passed with 5 profiles, 0 errors, and 0 warnings.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario profile_backed_spawn --binary .install\worr_ded_x86_64.exe --install-dir .install --artifact-dir .tmp\bot_scenarios --json-out .tmp\bot_scenarios\profile_backed_spawn_report.json --format text`
  - Result: passed; `smoke_c.c` resolved as profile `smoke` and spawned
    `B|Smoke` with the expected userinfo bridge fields.

## Outstanding Work

- The companion `_w/_i/_t.c` scripts are staged assets today. Full weapon-weight,
  item-weight, and chat grammar consumption remains future BotLib/script VM
  work.
- Current behavior still treats most profile metadata as userinfo hints. Combat,
  item, chat, and team policy consumers remain follow-up work above the existing
  profile bridge.
