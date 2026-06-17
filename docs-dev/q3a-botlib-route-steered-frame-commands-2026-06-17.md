# Q3A BotLib Route-Steered Frame Commands

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This round replaces the first bot frame-command placeholder yaw drift with an AAS-backed route steering query. Spawned bots still use the server-owned fake-client `usercmd_t` dispatch path from the previous slice, but command angles now come from a Q3A AAS route step instead of a deterministic drift.

The route logic is intentionally small. It finds a routable AAS area near the bot, chooses a deterministic reachable goal area, validates the full route through imported Q3A route prediction, then returns the first route step as the movement target for the current frame.

No new upstream source files were imported in this slice. The work is WORR-native bridge and command-generation code around the already imported Q3A AAS route APIs.

## Implementation Notes

- Added `Q3A_BotLibImport_BuildRouteSteer()` to the Q3A import boundary.
- Added `BotLibAdapter_BuildRouteSteer()` and `BotLibAdapterRouteSteer` so `sgame` code can request route steering without reaching directly into Q3A globals.
- Added robust current-area lookup that samples around the bot origin and falls back to the nearest reachable AAS area when the exact point is not inside a reachable area.
- Added deterministic reachable-goal selection, with an optional preferred goal area parameter reserved for later `bot_nav` ownership.
- Validated the full route with `AAS_PredictRoute(..., maxareas=0)` and used a one-area route prediction step for the immediate steering target.
- Updated `Bot_BuildFrameCommand()` to face the returned route target, move forward, and count route queries, route commands, route failures, and last route diagnostics.
- Extended `q3a_bot_frame_command_status` output with route counters and the last start/goal/route-end areas.

## Validation

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated route-steered frame-command smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_route_steer_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 2 +map mm-rage`

Key smoke evidence:

- `q3a_bot_frame_command_status frames=8 commands=8 route_queries=8 route_commands=8 route_failures=0 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_travel_time=130 last_reachability=218 last_stop_event=0 skipped_invalid=0 skipped_not_bot=0 skipped_runtime=0 skipped_inactive=0 expected_min_frames=1 expected_min_commands=1 pass=1`
- `q3a_bot_frame_command_smoke=end final_count=0`

## Outstanding Work

- Move persistent route/goal ownership into a dedicated `bot_nav` layer instead of recomputing the deterministic sample route every frame.
- Add item/position goal selection, route recomputation limits, stuck detection, jump/crouch/swim/door/plat handling, and per-bot route debug overlays.
- Fold profile behavior fields into movement style once higher-level bot brain scheduling exists.
