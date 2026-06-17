# Q3A BotLib Nav Debug Overlay

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This round feeds real `bot_nav` route-cache state into the route/goal debug overlay. `sg_bot_debug_route` and `sg_bot_debug_goal` now draw the cached per-bot route step and goal markers when a bot has an active cached route, instead of always drawing only the imported Q3A sample overlay.

The imported sample overlay remains as a fallback before any bot route has been cached, which keeps the low-level Q3A debug bridge smoke useful while allowing live bot state to take over once movement commands run.

No new upstream source files were imported in this slice.

## Implementation Notes

- Added `BotNav_DrawDebugOverlay()` in `src/game/sgame/bots/bot_nav.*`.
- Draws the cached bot origin to route move target as an arrow when `sg_bot_debug_route` is active.
- Draws the cached move target to final route goal hint as a route line.
- Draws cached route target and goal crosses when route/goal debug is active.
- Counts native overlay frames, route draws, goal draws, missing-cache fallback frames, lines, crosses, arrows, and last debug client in `BotNavRouteStatus`.
- Changed `RunBotLibDebugDrawIfRequested()` to draw cached native route state first and run `BotLibAdapter_RunRouteOverlaySmoke()` only when no cached bot route is available yet.
- Expanded `q3a_bot_frame_command_status` with the native `route_debug_*` counters.

## Validation

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated nav debug overlay smoke:
  `.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_nav_debug_overlay_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 2 +map mm-rage`

Key smoke evidence:

- `q3a_bot_frame_command_status frames=8 commands=8 route_requests=8 route_queries=2 route_refreshes=2 route_reuses=6 route_commands=8 route_failures=0 route_invalid_slots=0 route_cadence_refreshes=1 route_target_refreshes=0 route_drift_refreshes=0 route_preferred_goal_refreshes=0 route_debug_frames=10 route_debug_routes=8 route_debug_goals=8 route_debug_missing_frames=2 route_debug_lines=8 route_debug_crosses=16 route_debug_arrows=8 last_route_client=0 last_route_debug_client=0 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_travel_time=130 last_reachability=218 last_stop_event=0 skipped_invalid=0 skipped_not_bot=0 skipped_runtime=0 skipped_inactive=0 expected_min_frames=1 expected_min_commands=1 pass=1`
- `q3a_bot_frame_command_smoke=end final_count=0`

## Outstanding Work

- Add selected-bot filtering and richer labels once the bot blackboard owns per-bot debug state.
- Draw full route polylines after look-ahead route points are available.
- Report current AAS area, next reachability type, stuck reason, and failed goal reason in the overlay.
