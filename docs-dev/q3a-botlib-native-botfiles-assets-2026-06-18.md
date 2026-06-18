# Q3A BotLib Native Botfiles Assets

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This slice adds the first repository-owned WORR bot profile asset pack. The
pack now uses the familiar Q3A/Gladiator-style botfile layout instead of a flat
placeholder profile format:

- `assets/botfiles/chars.h`
- `assets/botfiles/bots/*_c.c` character/profile entry points
- `assets/botfiles/bots/*_w.c` weapon-weight companions
- `assets/botfiles/bots/*_i.c` item-weight companions
- `assets/botfiles/bots/*_t.c` chat metadata companions

The files package into `botfiles/...` inside `pak0.pkz`. The refresh workflow
also mirrors `botfiles` loose beside the archive so dedicated builds without
`.pkz` support can still discover the same scripts.

The profiles are original WORR-authored test and gameplay seeds. Local Q3A and
Gladiator botfiles were used only as format references for the directory shape,
`*_c/_w/_i/_t` naming, and broad script vocabulary. No Q3A, Gladiator, or other
external botfile text was copied.

## Profile Pack

- `smoke_c.c`: deterministic packaged profile for `sv_bot_profile_smoke`.
- `vanguard_c.c`: aggressive red-side attacker for manual add and team
  assignment experiments.
- `bulwark_c.c`: lower-aggression blue-side defender for team-limit and manual
  style checks.
- `relay_c.c`: support/patrol profile intended for min-player and later
  cooperative policy tests.
- `vector_c.c`: duelist/kiting profile for one-on-one and aim/weapon style
  checks.

Each profile has matching `_w.c`, `_i.c`, and `_t.c` companions. A later
Q3-style expansion pass added multi-skill character blocks, richer chat scripts,
`teamplay.h`, denser shared weight frameworks, and companion validation. The
current server profile loader still treats companions as resources, not bot
profiles.

## Recognized Runtime Fields

The current server bridge recognizes the following Q3-style fields:

- `skill N` from the character script header.
- `CHARACTERISTIC_NAME`
- `CHARACTERISTIC_REACTIONTIME`, normalized from seconds to milliseconds.
- `CHARACTERISTIC_AGGRESSION`
- `WORR_SKIN`
- `WORR_TEAM`
- `WORR_AIM_ERROR`
- `WORR_PREFERRED_WEAPON`
- `WORR_CHAT_PERSONALITY`
- `WORR_ROLE`
- `WORR_MOVEMENT_STYLE`

Additional `CHARACTERISTIC_*` fields are retained in the authored scripts for
future full BotLib character/weight/chat consumption, but the interim server
profile bridge intentionally ignores fields it does not yet consume.

## Smoke Profile Contract

The packaged `smoke` profile is loaded from `botfiles/bots/smoke_c.c`. The
loader strips the `_c` suffix, so the runtime profile id remains `smoke`.

- `CHARACTERISTIC_NAME "Smoke"`
- `skill 4`
- `CHARACTERISTIC_REACTIONTIME 0.250`
- `CHARACTERISTIC_AGGRESSION 0.65`
- `WORR_SKIN "male/grunt"`
- `WORR_TEAM "free"`
- `WORR_AIM_ERROR 2.5`
- `WORR_PREFERRED_WEAPON "rocketlauncher"`
- `WORR_CHAT_PERSONALITY "quiet"`
- `WORR_ROLE "attacker"`
- `WORR_MOVEMENT_STYLE "strafe"`

With the default game-side `bot_name_prefix` of `B|`, the dedicated profile
smoke reports `name=B|Smoke profile=smoke` after adding the profile.

## Notes

- Profile `team` is authored for forward compatibility and userinfo coverage.
  Current game-side initial bot team placement still picks the active team in
  code, so team values should not yet be treated as behavior authority.
- The pack is deliberately small to avoid filling the bounded 128-profile table
  with placeholder content before bot-brain policy consumes these fields.
- `chars.h` is a WORR-native readability shim for script symbols. Its shared
  characteristic values now follow the Q3A ordering, while `WORR_*` extensions
  stay in a high range for project-specific bridge fields.

## Validation

- `python tools\bot_profiles\validate_bot_profiles.py`
  - Result: passed with 5 character files, 5 profiles, 0 errors, and 0 warnings.
- `python tools\package_assets.py --assets-dir assets --install-dir .tmp\botfiles-assets-validation --base-game basew --archive-name pak0.pkz`
  - Result: wrote `.tmp\botfiles-assets-validation\basew\pak0.pkz`, mirrored
    loose `botfiles`, and packed 84 files from `assets`.
- Archive member inspection includes the 20 bot companion scripts plus
  `botfiles/chars.h`, including:
  - `botfiles/bots/smoke_c.c`
  - `botfiles/bots/smoke_w.c`
  - `botfiles/bots/smoke_i.c`
  - `botfiles/bots/smoke_t.c`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario profile_backed_spawn --binary .install\worr_ded_x86_64.exe --install-dir .install --artifact-dir .tmp\bot_scenarios --json-out .tmp\bot_scenarios\profile_backed_spawn_report.json --format text`
  - Result: passed. The refreshed install loaded 5 profiles, resolved `smoke`,
    spawned `B|Smoke`, verified all profile/userinfo fields, and cleaned up to
    final count 0.

## Remaining Risks

- Profile metadata is still mostly carried through userinfo for later policy;
  bot-brain consumers for roles, preferred weapons, chat style, and movement
  style remain follow-up work.
- Runtime team selection does not yet consume profile `team` as the sole source
  of truth.
- Weapon-weight, item-weight, and chat companion files are authored now but only
  staged as data. Full BotLib script VM consumption remains pending.
- Dedicated builds with `USE_ZLIB 0` still do not mount `.pkz` archives. Normal
  refreshed installs now mirror `botfiles` loose so profile discovery remains
  deterministic in those builds while the archive member is still packaged.
