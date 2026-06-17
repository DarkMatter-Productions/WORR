# Q3A BotLib Nav Polyline Debug Slice

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice turns the cached live bot route debug overlay from a single route-step arrow into a bounded route polyline. The route-steer bridge now samples up to eight Q3A `AAS_PredictRoute()` endpoints for the selected route, carries them through the C++ adapter, caches them in `bot_nav`, and reports polyline counters through the dedicated frame-command smoke.

## Implementation Notes

- `Q3ABotLibImportRouteSteerResult` now carries `routePointCount` and up to `Q3A_BOTLIB_IMPORT_MAX_ROUTE_POINTS` sampled route endpoints.
- `Q3A_BotLibImport_BuildRouteSteer()` validates the full route as before, then samples one through eight predicted route depths. The original first-step `moveTarget`, `routeEndArea`, and stop-event behavior remain intact for movement.
- `BotLibAdapterRouteSteer` mirrors the bounded route-point payload and clamps the imported count at the C/C++ boundary.
- `BotNav_DrawDebugOverlay()` now draws the cached route as a sampled polyline: first segment as the existing route arrow, intermediate sampled segments as cyan lines, and the final link to the goal marker as green.
- `BotNavRouteStatus` and `Bot_FrameCommandPrintStatus()` now report `route_debug_polyline_points`, `route_debug_polyline_segments`, and `last_route_point_count` for headless validation.

No new upstream source files were imported for this slice. The work uses already-imported Q3A route prediction APIs behind WORR-owned bridge and adapter code.

## Validation

Commands run:

```text
meson compile -C builddir-win
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set logfile 1 +set logfile_name q3a_bot_nav_polyline_debug_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 2 +map mm-rage
```

Results:

- Build passed. Ninja still reports the pre-existing `premature end of file; recovering` warning.
- Refresh install passed and packaged `maps/mm-rage.aas` into `.install/basew/pak0.pkz`.
- Dedicated smoke passed with `q3a_bot_frame_command_status frames=8 commands=8 route_requests=8 route_queries=2 route_refreshes=2 route_reuses=6 route_commands=8 route_failures=0 route_debug_routes=8 route_debug_goals=8 route_debug_lines=16 route_debug_arrows=8 route_debug_labels=8 route_debug_polyline_points=16 route_debug_polyline_segments=24 last_current_area=224 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_route_point_count=2 last_reachability=218 last_reachability_type=2 pass=1`.

## Follow-Up

- Add selected-bot filtering before multi-bot route debug becomes noisy.
- Use the sampled route points for look-ahead steering once movement-state handling begins.
- Add stuck and failed-goal reason strings to the same smoke/debug status channel.
- Replace the temporary deterministic preferred goal with persistent item and position goals.
