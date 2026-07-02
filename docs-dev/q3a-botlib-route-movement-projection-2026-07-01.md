# Q3A BotLib Route Movement Projection Fix

Date: 2026-07-01

Tasks: `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round investigated the remaining player-visible case where bots could
still appear to spin or jam at route nodes after the route target anti-spin
fix. The earlier work stabilized which local route point the command layer
wanted to use, but it did not fully separate where the bot was moving from
where the bot was looking.

The primary remaining cause was command generation. Bot movement in WORR is
relative to the command view angles. The bot brain could correctly select a
route target, then aim at a visible enemy or nearby combat point, and still
send `forwardMove=180`. That made the bot physically walk along its combat aim
instead of along the route yaw. In live play this looked like route-node churn:
the route stayed valid, but the command vector kept pushing the bot into a wall
or into another player while the view angle moved around.

The secondary cause was route-point matching tolerance. The command layer only
treated `route.moveTarget` as one of the returned route points when it was an
almost exact 3D match. Small BotLib endpoint offsets, especially edge and z
offsets around stairs, movers, and corners, could make the command layer miss
that relationship and lose look-ahead context.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp`
  - Added tolerant route move-target matching. Exact matches still win, but a
    route point can now match an authoritative `route.moveTarget` through a
    bounded 3D or horizontal-plus-vertical tolerance.
  - Preserved unmatched move targets that are still meaningfully ahead of the
    bot, while treating already-consumed unmatched targets as a cue to restart
    local look-ahead from the returned route points.
  - Added `Bot_CommandApplyRouteMovement`, which projects route yaw into the
    current desired view yaw and fills both `forwardMove` and `sideMove`.
    Bots can now strafe or backpedal along the route while aiming at a combat
    target instead of always walking straight through the current view angle.
  - Reused one computed route angle for both view selection and movement
    projection so look-ahead telemetry is not double-counted.
  - Added frame-command status fields for approximate move-target matches,
    unmatched targets, goal fallback use, projected route movement, strafe
    movement, backpedal movement, and the last projected movement vector.
- `tools/bot_scenarios/run_bot_scenarios.py`
  - Added the new look-ahead and route-movement projection counters to optional
    status-field discovery so focused and aggregate reports retain the new
    diagnostics without breaking older logs.

No new upstream Q3A files were imported in this round; this is WORR-native bot
command, telemetry, scenario-harness, documentation, and release-staging work.

## Validation

- `meson compile -C builddir-win`
  - Passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed `.install/`.
- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  - Passed: 66 passed.
- Focused routing/combat/movement batch passed 10/10 from
  `.tmp\bot_scenarios\route_spin_projection_focus.json`.
  - All ten focused rows reported `route_failures=0`.
  - `route_movement_projected_commands` was present on every focused row that
    emitted route commands.
  - `trace_checked_corner_cutting` recorded projected route movement plus
    strafe and backpedal samples.
  - `threat_retreat_avoidance` recorded projected route movement and 13 strafe
    commands while keeping route failures at zero.
- Full implemented bot scenario sweep passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_route_projection_fix.json`.
- Release acceptance passed 15/15 from
  `.tmp\bot_release\bot_release_acceptance_route_projection_fix.json`.

Build warnings observed during validation were the existing q2aas warnings in
the staged toolchain and were not introduced by this route movement change.
