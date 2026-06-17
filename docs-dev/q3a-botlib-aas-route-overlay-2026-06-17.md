# Q3A BotLib AAS Route Overlay Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This slice adds a focused Q3A AAS route/goal overlay smoke path on top of the debug draw bridge. When `sg_bot_debug_route` or `sg_bot_debug_goal` is enabled, the server game asks the imported Q3A AAS route code for the current smoke start and goal areas, validates travel time and reachability, predicts the route endpoint, and draws the route request through the WORR `gi.Draw_*` callback bridge.

This is still a developer overlay for the imported route-query bridge. It is not final per-bot navigation rendering yet; later `bot_nav.*` work should feed selected bot origins, goals, route segments, reachability types, and stuck reasons into this draw path.

## Implementation

- Added route-overlay status fields to `Q3ABotLibImportSmokeStatus` and mirrored them through `BotLibAdapterStatus`.
- Added `Q3A_BotLibImport_RunRouteOverlaySmoke()`, which:
  - reuses the imported route-smoke start/goal areas or re-runs the imported route smoke if needed,
  - validates `AAS_AreaTravelTimeToGoalArea`, `AAS_AreaReachabilityToGoalArea`, and `AAS_PredictRoute`,
  - draws start, goal, and predicted-end crosses,
  - draws a start-to-goal request line,
  - draws a predicted-end-to-goal line,
  - draws a start-to-predicted-end arrow.
- Added `BotLibAdapter_RunRouteOverlaySmoke()`.
- Changed the runtime frame debug gate so:
  - `sg_bot_debug_route` or `sg_bot_debug_goal` runs the route overlay every frame,
  - `sg_bot_debug_aas >= 3` keeps running the generic debug primitive smoke on its throttled cadence.
- Extended verbose adapter status with `q3a_route_overlay*` counters.

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Install refresh and package preservation:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'
try {
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_route_overlay_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +set sg_bot_debug_route 1 +map mm-rage +wait 90 +quit *> .tmp\q3a_aas_route_overlay_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed:

```text
q3a_route_overlay=Q3A route overlay passed: callback=yes start=3 goal=6 end=6 travel_time=113 reachability=1 lines=2 crosses=3 arrows=1 clears=1 failures=0
q3a_route_overlay_start=3
q3a_route_overlay_goal=6
q3a_route_overlay_end=6
q3a_route_overlay_time=113
q3a_route_overlay_reachability=1
q3a_route_overlay_lines=2
q3a_route_overlay_crosses=3
q3a_route_overlay_arrows=1
q3a_route_overlay_clears=1
q3a_route_overlay_failures=0
q3a_debug_draw=Q3A route overlay debug draw passed: callback=yes lines=2 crosses=3 arrows=1 clears=1 failures=0
```

Existing bridge smokes remained green in the same run, including:

```text
q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0
q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes
q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024
q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18
```

Logs:

- `.install/basew/logs/q3a_aas_route_overlay_smoke.log`
- `.tmp/q3a_aas_route_overlay_smoke_stdout.log`

## Remaining Work

- Feed real bot origins/goals and route state into the overlay once `bot_nav.*` owns steering.
- Draw actual route segment polylines once the navigation layer tracks a route corridor instead of a smoke start/goal pair.
- Q3A debug polygon create/delete bridging is covered by `docs-dev/q3a-botlib-aas-debug-polygon-bridge-2026-06-17.md`; imported AAS area helper coverage is covered by `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`.
