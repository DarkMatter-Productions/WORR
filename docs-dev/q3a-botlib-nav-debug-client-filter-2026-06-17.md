# Q3A BotLib Nav Debug Client Filter Slice

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds a selected-client filter for the live native `bot_nav` route/goal debug overlay. `sg_bot_debug_client` defaults to `-1` to draw every cached bot route, while any non-negative value selects one zero-based client slot for `sg_bot_debug_route` and `sg_bot_debug_goal`.

The filter keeps multi-bot route debug readable without changing route generation, cache cadence, or command dispatch.

## Implementation Notes

- Registered `sg_bot_debug_client` beside the existing bot route/goal debug cvars.
- `RunBotLibDebugDrawIfRequested()` now passes the selected client slot into `BotNav_DrawDebugOverlay()`.
- `BotNav_DrawDebugOverlay()` skips cached route slots that do not match the selected client and keeps `-1` as the all-bots mode.
- `BotNavRouteStatus` now tracks `debugOverlayFilteredSlots`, `debugOverlayFilterMissFrames`, and `lastDebugFilterClient`.
- `q3a_bot_frame_command_status` now reports `route_debug_filtered_slots`, `route_debug_filter_miss_frames`, and `last_debug_filter_client` for headless validation.

No new upstream source files were imported for this slice. The work is WORR-owned runtime/debug plumbing around the existing imported Q3A route data.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_nav_debug_client0_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sg_bot_debug_client 0 +set sv_bot_frame_command_smoke 2 +map mm-rage
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27911 +set logfile 1 +set logfile_name q3a_bot_nav_debug_client1_filtered_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sg_bot_debug_client 1 +set sv_bot_frame_command_smoke 2 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- `sg_bot_debug_client 0` smoke passed with `route_debug_routes=8`, `route_debug_goals=8`, `route_debug_filtered_slots=0`, `route_debug_filter_miss_frames=2`, `last_route_debug_client=0`, `last_debug_filter_client=0`, and `pass=1`.
- `sg_bot_debug_client 1` smoke passed while filtering out the only active bot slot, reporting `route_debug_routes=0`, `route_debug_goals=0`, `route_debug_filtered_slots=8`, `route_debug_filter_miss_frames=10`, `last_route_debug_client=-1`, `last_debug_filter_client=1`, and `pass=1`.

## Follow-Up

- Add a small operator-facing helper for choosing the active debug bot once multi-bot route debugging is common.
- Add stuck and failed-goal reason strings to the same smoke/debug status channel.
- Replace the temporary deterministic preferred goal with persistent item and position goals.
