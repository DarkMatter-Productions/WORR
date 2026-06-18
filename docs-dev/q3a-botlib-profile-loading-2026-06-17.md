# Q3A BotLib Bot Profile Loading

Date: 2026-06-17

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round adds the first deterministic server-owned bot profile loader. `sg_bot_reload_profiles` scans the game filesystem for Q3A-style character files and WORR-local `.bot` profile files, and `sg_bot_add [profile] [team]` now resolves the first argument as a profile before falling back to the older display-name behavior.

No profile assets were added in this slice. The loader and smoke coverage are WORR-owned code; future imported or curated profile assets still need source ownership and credits review before landing under `assets/`. Current packaged WORR botfiles use Q3-style `*_c/_w/_i/_t.c` script families; see `docs-dev/q3a-botlib-q3-style-botfiles-2026-06-18.md`.

## Implementation Notes

- Added a bounded in-memory server profile table.
- Added `sg_bot_reload_profiles`, which reloads profiles from:
  - `botfiles/bots/*.c` for Q3A-style character/profile files.
  - `bots/profiles/*.bot` and `bots/*.bot` for WORR-local key/value profile files.
- Added lazy first-use profile scanning so `sg_bot_add <profile>` works without requiring an operator to manually reload first.
- Profile parsing recognizes `name`, `skin`, `team`, and `skill`. Unknown keys are ignored for forward compatibility with richer Q3A character files.
- `sg_bot_add [profile] [team]` now uses profile `name`/`skin`/`skill`; the explicit command team overrides the profile team when supplied.
- Added `sg_bot_profile` as the optional default profile token for min-player autofill. If the cvar does not resolve to a loaded profile, autofill falls back to generated `botN` names.
- Profile display names are uniqued deterministically so repeated adds of the same profile do not create duplicate visible bot names.
- Added `sv_bot_profile_smoke 2`, which reloads profiles, resolves a `smoke` profile, adds it, verifies userinfo fields, removes all bots, and exits.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Temporary validation-only profile staged at `.install/basew/botfiles/bots/smoke.c`, then removed and followed by another refresh-install pass. This was a historical smoke fixture; current packaged profiles load from `*_c.c` entry points such as `smoke_c.c`.
- Dedicated profile smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_profile_self_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_profile_smoke 2 +map mm-rage`
- Dedicated min-player regression smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_minplayers_after_profiles +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_min_players_smoke 2 +map mm-rage`
- Dedicated multi-slot regression smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_multislot_after_profiles +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_slot_smoke 2 +map mm-rage`

Key profile log evidence:

- `q3a_bot_profile_smoke=begin profiles=1 found=1`
- `Added bot B|Smoke in slot 0.`
- `q3a_bot_profile_smoke_after_add count=1 name=B|Smoke profile=smoke skin=male/grunt skill=4`
- `q3a_bot_profile_smoke_after_remove_all count=0`
- `q3a_bot_profile_smoke=end final_count=0`

Key regression evidence:

- `q3a_bot_min_players_smoke=end final_count=0`
- `q3a_bot_slot_smoke_after_deferred_pair count=2`
- `q3a_bot_slot_smoke=end final_count=0`

## Outstanding Work

- Richer profile behavior fields were added in `docs-dev/q3a-botlib-profile-behavior-fields-2026-06-17.md`; they still need game-side policy consumers.
- Follow-up completed: first-party WORR profile assets now live under
  `assets/botfiles/bots/`; see
  `docs-dev/q3a-botlib-native-botfiles-assets-2026-06-18.md`.
- Team-limit and mode-change cleanup have a first pass in `docs-dev/q3a-botlib-team-policy-cleanup-2026-06-17.md`; direct team-count smoke coverage is still pending.
- Connect spawned bot slots to AAS-backed movement and the BotLib command dispatcher.
