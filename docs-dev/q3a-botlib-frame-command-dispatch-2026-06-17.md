# Q3A BotLib Bot Frame Command Dispatch

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This round adds the first server-to-game bot command dispatch path. Spawned local bot clients are now visited before each server game frame, the loaded `sgame` module is asked to build a bot `usercmd_t`, and accepted commands are run through the same server `SV_ClientThink` path used for network client movement.

The initial command builder was intentionally narrow. It was gated on `sg_bot_enable`/loaded AAS runtime state and emitted a simple forward move with a slow yaw drift. That proved the ownership, timing, ABI, and fake-client movement plumbing before the later route-steered command slice.

No imported source files or profile assets were added in this slice.

## Implementation Notes

- Added `BOT_FRAME_COMMAND_API_V1` in `inc/shared/bot_frame_command.h`.
- Exposed the frame-command API through `game_export_t::GetExtension()` from `sgame`.
- Added `Bot_BuildFrameCommand()` and `Bot_FrameCommandPrintStatus()` in `src/game/sgame/bots/bot_think.cpp`.
- Added `SV_BotClientThink()` in `src/server/user.c` so fake-client commands run through the normal server movement bookkeeping and update `client_t::lastcmd`.
- Added `SV_BotRunFrameCommands()` before `SV_RunGameFrame()` in the server frame.
- Added `sv_bot_frame_command_smoke`, which spawns one bot on `mm-rage`, waits for command frames, prints game-side command counters, removes the bot, and exits.
- The initial command was AAS-gated but not route-driven. Route-backed first-step steering is tracked separately in `docs-dev/q3a-botlib-route-steered-frame-commands-2026-06-17.md`.

## Validation

- `meson compile -C builddir-win`
- First build attempt compiled and linked the changed targets, then stopped on stale OpenAL archive state: `llvm-ar.exe: error: cannot convert a regular archive to a thin one`.
- Removed the generated `builddir-win/subprojects/openal-soft-1.24.3/libcommon.a` archive and reran the full build successfully.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`
- Dedicated frame-command smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_frame_command_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 2 +map mm-rage`
- Dedicated slot regression smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_slot_smoke_regression_after_command +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_slot_smoke 2 +map mm-rage`
- Dedicated team-policy regression smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27911 +set logfile 1 +set logfile_name q3a_bot_team_policy_smoke_regression_after_command2 +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set g_gametype 2 +set sv_bot_team_policy_smoke 2 +map mm-rage`

Key smoke evidence:

- `q3a_bot_frame_command_status frames=8 commands=8 skipped_invalid=0 skipped_not_bot=0 skipped_runtime=0 skipped_inactive=0 expected_min_frames=1 expected_min_commands=1 pass=1`
- `q3a_bot_frame_command_smoke=end final_count=0`
- `q3a_bot_slot_smoke_after_deferred_pair count=2`
- `q3a_bot_slot_smoke=end final_count=0`
- `q3a_bot_team_policy_status bots=3 playing=2 spectators=1 queued=0 none=0 free=2 red=0 blue=0 expected_playing=2 expected_spectators=1 expected_bots=3 pass=1`
- `q3a_bot_team_policy_status bots=0 playing=0 spectators=0 queued=0 none=0 free=0 red=0 blue=0 expected_playing=0 expected_spectators=0 expected_bots=0 pass=1`

## Outstanding Work

- Move command generation out of the direct frame-command helper and into a WORR-native `bot_nav`/`bot_brain` layer.
- Add persistent AAS route state, item/position goals, route recomputation limits, and stuck recovery.
- Decide how Q3A `BotClientCommand` bridge validation should interact with this now that a safe server-owned command dispatch path exists.
