# Q3A BotLib Bot Profile Behavior Fields

Date: 2026-06-17

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round expands the server-owned bot profile bridge beyond display identity. Loaded profiles can now carry behavior metadata for later bot policy, brain, and movement work without requiring profile assets to be added yet.

No curated profile assets were added in this slice. The temporary profile used for validation lived only under `.install/` and was removed before the final refresh-install pass. Current packaged WORR botfiles use Q3-style `*_c/_w/_i/_t.c` script families; see `docs-dev/q3a-botlib-q3-style-botfiles-2026-06-18.md`.

## Implementation Notes

- Extended the bounded `bot_profile_t` table with behavior fields for reaction, aggression, aim error, preferred weapon, chat personality, team role, and movement style.
- Kept parsing permissive for Q3A-style and WORR-local profile files by accepting common aliases:
  - `reaction`, `reaction_time`, `reaction_ms`
  - `aggression`, `aggression_bias`
  - `aim_error`, `aimerror`, `accuracy_error`
  - `preferred_weapon`, `weapon`, `favorite_weapon`
  - `chat_personality`, `chat`, `personality`
  - `role`, `team_role`
  - `movement_style`, `movement`, `move_style`
- Exposed the parsed metadata through fake-client userinfo keys:
  - `bot_reaction`
  - `bot_aggression`
  - `bot_aim_error`
  - `bot_preferred_weapon`
  - `bot_chat_personality`
  - `bot_role`
  - `bot_movement_style`
- Added a small optional-userinfo helper so empty profile fields do not bloat userinfo strings.
- Expanded `sv_bot_profile_smoke` to copy userinfo values into local buffers before printing. `Info_ValueForKey()` uses rotating static buffers, so the smoke line now remains stable even with the longer field list.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Temporary validation-only profile staged at `.install/basew/botfiles/bots/smoke.c`, then removed before the final refresh-install pass. This was a historical smoke fixture; current packaged profiles load from `*_c.c` entry points such as `smoke_c.c`.
- Dedicated profile behavior smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_profile_behavior_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_profile_smoke 2 +map mm-rage`
- Dedicated min-player regression smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_minplayers_after_behavior_profiles +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_min_players_smoke 2 +map mm-rage`
- Dedicated multi-slot regression smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_multislot_after_behavior_profiles +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_slot_smoke 2 +map mm-rage`
- Final `refresh_install.py --package-q2aas-aas` pass verified the staged runtime and packaged `maps/mm-rage.aas` member after temporary validation content was removed.

Key profile behavior log evidence:

- `q3a_bot_profile_smoke=begin profiles=1 found=1`
- `q3a_bot_profile_smoke_after_add count=1 name=B|Smoke profile=smoke skin=male/grunt skill=4 reaction=250 aggression=0.65 aim_error=2.5 preferred_weapon=rocketlauncher chat=quiet role=attacker movement=strafe`
- `q3a_bot_profile_smoke_after_remove_all count=0`
- `q3a_bot_profile_smoke=end final_count=0`

Key regression evidence:

- `q3a_bot_min_players_smoke=end final_count=0`
- `q3a_bot_slot_smoke_after_deferred_pair count=2`
- `q3a_bot_slot_smoke=end final_count=0`

## Outstanding Work

- Follow-up completed: first-party WORR profile assets now live under
  `assets/botfiles/bots/`; see
  `docs-dev/q3a-botlib-native-botfiles-assets-2026-06-18.md`.
- Feed the behavior metadata into bot-brain policy; team-limit and mode-change cleanup now have a first pass in `docs-dev/q3a-botlib-team-policy-cleanup-2026-06-17.md`.
- Connect spawned bot slots to AAS-backed movement and the BotLib command dispatcher.
