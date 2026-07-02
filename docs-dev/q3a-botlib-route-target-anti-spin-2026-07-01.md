# Q3A BotLib Route Target Anti-Spin Fix

Date: 2026-07-01

Tasks: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round investigated player-visible bot spin/stall behavior that looked like
the bot could not advance past a route node. The root problem was split between
command steering and progress accounting:

- `BotNavStabilizeRouteTarget` could preserve or promote `route.moveTarget`,
  but `Bot_CommandSelectRouteTarget` treated `routePoints[0]` as the steering
  base and could effectively steer back into an already consumed local point.
- Route look-ahead did not skip very close route points, so a bot standing on or
  beside a node could spend frames turning toward the same local target.
- Stuck detection measured progress only toward the final route goal. On corner
  routes, switchbacks, and mover/interaction paths this could mark useful local
  movement as stagnant and trigger recovery unnecessarily.

The fix makes the command layer start from the authoritative `route.moveTarget`,
matches that point back into the route-point list when possible, skips route
points inside a 24-unit consumed radius, and falls back to the route goal when
all returned points are already consumed. The nav layer now tracks progress
toward both the final route goal and the current local route target, resetting
stagnation when either measurement shows real movement or when the local target
has been reached.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp`
  - Added route-point matching for `route.moveTarget`.
  - Preserved stabilized `moveTarget` instead of resetting command steering to
    route point zero.
  - Added close route-point skips and a consumed-node fallback to the route goal.
  - Added `lookahead_preserved_move_targets`, `lookahead_close_point_skips`, and
    last-lookahead metadata to frame-command status.
- `src/game/sgame/bots/bot_nav.cpp`
  - Added local route-target progress tracking beside final-goal progress.
  - Reset target progress when the local route target shifts meaningfully.
  - Treat local-target progress or local-target reach as anti-stall evidence.
- `tools/bot_scenarios/run_bot_scenarios.py`
  - Added route look-ahead metrics to optional-field discovery.
  - Hardened `trace_checked_corner_cutting` so it must prove close-point skips.
- `tools/bot_scenarios/test_run_bot_scenarios.py`
  - Updated parser/catalog fixtures for the new look-ahead metrics.
- `src/game/sgame/gameplay/g_svcmds.cpp` and `src/game/sgame/g_local.hpp`
  - Restored the printed live match-result outcome status fields already tracked
    in bot brain state. This was exposed by the full implemented sweep while
    validating the routing work.

No new upstream Q3A files were imported in this round; the work is WORR-native
adapter, command, status, and harness code.

## Validation

- `meson compile -C builddir-win` passed.
- `.install` refreshed with:
  `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`.
- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py` passed:
  `66 passed`.
- Focused route batch passed 8/8 from
  `.tmp\bot_scenarios\route_spin_final_after_status.json`.
  - `trace_checked_corner_cutting`: `route_failures=0`,
    `stuck_detections=0`, `lookahead_close_point_skips=32`.
  - `ffa_live_pacing`: `route_failures=0`, `stuck_detections=0`,
    `lookahead_close_point_skips=29`.
  - `duel_live_pacing`: `route_failures=0`, `stuck_detections=0`,
    `lookahead_close_point_skips=21`.
  - `tdm_role_spawn_stability`: `route_failures=0`, `stuck_detections=0`,
    `lookahead_close_point_skips=18`.
- `bot_chat_live_match_result` passed from
  `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json` after the
  status formatter correction.
- Full implemented bot scenario sweep passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_route_spin_status_fix.json`.
- Release acceptance passed 15/15 from
  `.tmp\bot_release\bot_release_acceptance_route_spin_fix.json`.

Build warnings observed during validation were the existing q2aas/toolchain
warnings in `tools/q2aas` and not introduced by this routing change.
