# Q3A BotLib Nav Route Cache

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This round moves the live AAS route-steering query out of `bot_think.cpp` and into a WORR-owned `bot_nav` layer. Bot frame commands still use the server-owned fake-client `usercmd_t` path, but route steering is now cached per bot client slot and refreshed on a small cadence instead of rebuilding an imported Q3A AAS route every accepted command frame.

This is still not the full bot navigation brain. The current cache owns route reuse, refresh cadence, invalidation on map/AAS lifetime, and telemetry. Item/position goals, movement-state handling, and stuck recovery remain later `bot_nav`/`bot_brain` work.

No new upstream source files were imported in this slice.

## Implementation Notes

- Added `src/game/sgame/bots/bot_nav.hpp` and `src/game/sgame/bots/bot_nav.cpp`.
- Added per-client route slots keyed by the bot entity/client index.
- Cached `BotLibAdapterRouteSteer` results for four server frames by default.
- Refreshes a cached route when:
  - there is no valid route,
  - the cadence expires,
  - the bot reaches the cached move target,
  - the bot drifts far enough from the cached origin,
  - the preferred goal area changes.
- Resets all route cache state during `Bot_RuntimeBeginLevel()` and `Bot_RuntimeEndLevel()`.
- Changed `Bot_BuildFrameCommand()` to request route steering through `BotNav_GetRouteSteer()`.
- Expanded `q3a_bot_frame_command_status` with route requests, live AAS route queries, refreshes, reuses, failure counts, invalid slots, refresh reason counters, and last-route diagnostics.

## Validation

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated nav route-cache smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_nav_route_cache_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_frame_command_smoke 2 +map mm-rage`

Key smoke evidence:

- `q3a_bot_frame_command_status frames=8 commands=8 route_requests=8 route_queries=2 route_refreshes=2 route_reuses=6 route_commands=8 route_failures=0 route_invalid_slots=0 route_cadence_refreshes=1 route_target_refreshes=0 route_drift_refreshes=0 route_preferred_goal_refreshes=0 last_route_client=0 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_travel_time=130 last_reachability=218 last_stop_event=0 skipped_invalid=0 skipped_not_bot=0 skipped_runtime=0 skipped_inactive=0 expected_min_frames=1 expected_min_commands=1 pass=1`
- `q3a_bot_frame_command_smoke=end final_count=0`

## Outstanding Work

- Add persistent item/position goal ownership rather than deterministic sample goals.
- Add route debug drawing for the selected bot's cached route state.
- Add stuck detection and forced refresh/goal cooldown behavior.
- Add movement-state handling for jump, crouch, swim, ladders, doors, plats, and teleporters.
