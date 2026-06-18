# Q3A BotLib Bot Team Policy Smoke

Date: 2026-06-17

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round adds direct game-side smoke coverage for bot team-policy cleanup. The dedicated server smoke now creates a Duel-style two-player active cap, adds three bots, and asks the loaded `sgame` module to count bot session teams directly. The expected result is two active bot participants and one spectator, followed by a zero-bot cleanup assertion.

The implementation is WORR-owned. No imported source, profile assets, or user-facing documentation content were added.

## Implementation Notes

- Added a lightweight shared `BOT_TEAM_POLICY_STATUS_API_V1` extension contract in `inc/shared/bot_team_policy_status.h`.
- Included that contract from `inc/shared/gameext.h` for server-side consumers without making the game module include the broader server extension header.
- Exposed `BotTeamPolicy_PrintStatus()` through `game_export_t::GetExtension()` from the server game module.
- Added `bot_team_policy_status` as a developer server command for ad-hoc game-side team count checks.
- Added `sv_bot_team_policy_smoke`, an unattended dedicated-server smoke that:
  - clears existing bots;
  - sets Duel-style limits with `g_gametype 2`, `maxplayers 2`, and `g_allow_duel_queue 0`;
  - adds `DuelOne`, `DuelTwo`, and `DuelThree`;
  - asserts game-side bot counts of `playing=2`, `spectators=1`, `bots=3`;
  - removes all bots and asserts `playing=0`, `spectators=0`, `bots=0`.
- The game-side status print uses the preserved raw engine print sink (`base_import.Com_Print`) so dedicated smoke records are not hidden by the game logger's runtime log-level filter.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Dedicated team-policy smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_team_policy_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set g_gametype 2 +set sv_bot_team_policy_smoke 2 +map mm-rage`
- Dedicated slot regression smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_slot_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sv_bot_slot_smoke 2 +map mm-rage`
- Dedicated min-player regression smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_min_players_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sv_bot_min_players_smoke 2 +map mm-rage`
- Historical profile-smoke note for this slice: the staged install had no
  `smoke` profile asset, so it exited with `profiles=0 found=0` and did not
  exercise a profile-backed spawn. Follow-up profile assets and
  `profile_backed_spawn` coverage landed on 2026-06-18.
- `git diff --check -- inc/shared/bot_team_policy_status.h inc/shared/gameext.h src/game/sgame/g_local.hpp src/game/sgame/gameplay/g_main.cpp src/game/sgame/gameplay/g_svcmds.cpp src/server/main.c`

Key smoke evidence:

- `q3a_bot_team_policy_status bots=3 playing=2 spectators=1 queued=0 none=0 free=2 red=0 blue=0 expected_playing=2 expected_spectators=1 expected_bots=3 pass=1`
- `q3a_bot_team_policy_status bots=0 playing=0 spectators=0 queued=0 none=0 free=0 red=0 blue=0 expected_playing=0 expected_spectators=0 expected_bots=0 pass=1`
- `q3a_bot_slot_smoke_after_deferred_pair count=2`
- `q3a_bot_slot_smoke=end final_count=0`
- `q3a_bot_min_players_smoke_after_fill count=3 auto=3 humans=0 target=3`
- `q3a_bot_min_players_smoke=end final_count=0`

## Outstanding Work

- Follow-up completed: first-party WORR profile assets now live under
  `assets/botfiles/bots/`; see
  `docs-dev/q3a-botlib-native-botfiles-assets-2026-06-18.md`.
- Connect spawned bot slots to AAS-backed movement and the BotLib command dispatcher.
