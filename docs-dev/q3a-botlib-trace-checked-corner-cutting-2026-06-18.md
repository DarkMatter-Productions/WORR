# Q3A BotLib Trace-Checked Corner Cutting

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T05`, `FR-04-T14`

## Summary

This slice closes the trace-checked corner-cutting lane from
`docs-dev/plans/q3a-botlib-aas-port.md` line 806. The nav route refresh path now
looks for a farther sampled route point that can be reached by a direct, safe
server trace before exposing it as the current steering target.

## Implementation

- Added deterministic corner-cut candidate evaluation in `bot_nav.cpp`.
- The pass runs only after a successful route refresh and before the route is
  stored in the per-client route cache.
- Candidates are scanned in stable route-point order within the existing
  256-unit local look-ahead distance.
- A shortcut is accepted only when a player-hull `gi.trace` from the bot origin
  to the candidate reaches full fraction without starting/all-solid.
- Walk and crouch shortcuts also require fixed-fraction downward ground probes
  along the shortcut so clear air over a gap is not treated as safe ground.
- Accepted shortcuts update only the local steering payload: `moveTarget`,
  `routePoints[0]`, and the steering sample count. Goal areas, route ownership,
  item reservations, persistent goals, reachability metadata, and route cache
  ownership are unchanged.
- The older near-origin route-target stabilization now uses the same trace
  proof before promoting a farther sampled point.

## Status Counters

`BotNavRouteStatus` now carries internal corner-cut telemetry:

- `cornerCutChecks`, `cornerCutApplications`, `cornerCutSkips`
- direct trace attempts/passes/failures
- ground probe attempts/failures
- last selected point index, original/result point counts, distance, trace
  fraction, travel type, and skip reason

These fields are available through `BotNav_GetRouteStatus()`. Emitting them on
`q3a_bot_frame_command_status` still requires a brain-side status-row hook in
`BotBrain_PrintFrameCommandStatus()`; this lane did not edit `bot_brain.cpp` by
request.

## Validation

Command:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_nav.cpp.obj
meson compile -C builddir-win sgame_x86_64
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 27983 +set logfile 1 +set logfile_name q3a_bot_trace_corner_cutting_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 3 +map mm-rage
```

Result: both builds passed. Ninja emitted the pre-existing shared-build-dir
warning `premature end of file; recovering` on both builds. The install refresh
completed, staged the Windows payload, and packaged `maps/mm-rage.aas`.

The dedicated `mm-rage` frame-command smoke passed with `pass=1`,
`route_failures=0`, `route_queries=5`, `route_reuses=12`, and
`last_route_point_count=1` after the smoothed steering payload was cached.

## Follow-Up

- Add the new `BotNavRouteStatus` corner-cut fields to the brain-owned
  `q3a_bot_frame_command_status` row when a status-surface lane is allowed to
  touch `bot_brain.cpp`.
- Run a map-backed dedicated smoke once the broader shared worktree settles, so
  the new counter values can be captured in scenario tooling.
