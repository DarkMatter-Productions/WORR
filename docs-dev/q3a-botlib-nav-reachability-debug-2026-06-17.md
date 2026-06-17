# Q3A BotLib Nav Reachability Debug Slice

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice extends the live AAS route-steering debug path so WORR can report the bot's current AAS area and the next Q3A reachability travel type. The previous route/goal debug overlay proved that `bot_nav` could draw cached live route state; this round makes that state more diagnostic by carrying the next reachability's travel metadata through the Q3A import bridge, the C++ adapter, `bot_nav`, and the dedicated frame-command smoke status.

## Implementation Notes

- `Q3A_BotLibImport_BuildRouteSteer()` now resolves the selected reachability with `AAS_ReachabilityFromNum()` and records the masked Q3A travel type, WORR/Q3A travel flag, and reachability end area in `Q3ABotLibImportRouteSteerResult`.
- `BotLibAdapterRouteSteer` carries `reachabilityTravelType`, `reachabilityTravelFlags`, and `reachabilityEndArea` into the native bot navigation layer.
- `BotNavRouteStatus` now records `lastCurrentArea`, `lastReachabilityTravelType`, `lastReachabilityTravelFlags`, `lastReachabilityEndArea`, and `debugOverlayLabels`.
- `BotNav_DrawDebugOverlay()` labels the cached route step with the current AAS area, reachability id, readable travel type, and reachability end area while `sg_bot_debug_route` is active.
- `Bot_FrameCommandPrintStatus()` includes the new route debug fields so dedicated smoke logs can validate the data path without requiring a graphical client.

No new upstream source files were imported for this slice. The new work is WORR-native bridge, adapter, navigation, and smoke-status plumbing.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_nav_reachability_debug_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 2 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Dedicated smoke passed with `q3a_bot_frame_command_status frames=8 commands=8 route_requests=8 route_queries=2 route_refreshes=2 route_reuses=6 route_commands=8 route_failures=0 route_debug_routes=8 route_debug_goals=8 route_debug_arrows=8 route_debug_labels=8 last_current_area=224 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_travel_time=130 last_reachability=218 last_reachability_type=2 last_reachability_flags=2 last_reachability_end_area=217 pass=1`.

## Follow-Up

- Add full cached route polylines once `bot_nav` can preserve more than the immediate route step.
- Add selected-bot filtering so route labels do not become noisy with multiple bots.
- Add stuck and failed-goal reasons to the same debug/status channel.
- Move from the temporary preferred AAS area target toward persistent item and position goals.
