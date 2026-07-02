# Q3A BotLib Route Command Trace And Sequential Look-Ahead

Date: 2026-07-02

Tasks: `FR-04-T02`, `FR-04-T05`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round scrutinized the remaining route and navigation behavior behind
reports of bots spinning or sticking near route nodes. The route cache and
watchdog already knew when a local `route.moveTarget` had been consumed, but
the command layer still treated later route points as distance-only look-ahead
candidates. That could make a bot promote a future point through level
geometry, then churn against a wall or corner while the route itself still
looked valid.

The second issue was trace strictness. AAS route points are often floor or
contact-space positions. A raw hull trace to that exact endpoint can fail even
when the route point is the correct next ordered step, so the command layer
needs to distinguish unsafe arbitrary shortcutting from ordinary sequential
route progress.

## Implementation

- `src/game/sgame/bots/bot_nav.cpp` and `bot_nav.hpp`
  - Exposed `BotNav_RouteTargetTraceClear()` so command steering can reuse the
    nav-layer shortcut safety check.
  - Split route shortcut validation into candidate and public trace helpers.
  - Normalized route-target endpoint traces by trying the raw AAS point and
    small upward offsets up to the bot hull foot offset, while preserving the
    existing ground-support check for travel types that need it.
- `src/game/sgame/bots/bot_brain.cpp`
  - Added command-layer trace telemetry:
    `lookahead_trace_checks`, `lookahead_trace_blocks`,
    `lookahead_goal_trace_blocks`, `lookahead_sequential_fallbacks`, and
    `last_lookahead_trace_blocked_index`.
  - Required ordinary far look-ahead and final-goal fallback targets to pass
    the nav trace check before becoming the command target.
  - Preserved ordered route progress when the current target is already inside
    the consumed radius: the first non-close future route point becomes a
    sequential fallback, and a blocked trace stops further promotion instead
    of skipping to a farther point through geometry.
- `tools/bot_scenarios/run_bot_scenarios.py`
  - Kept the q2dm2 survival regression focused on durable health-route proof
    instead of requiring the final sampled utility kind to still be `health`.
    After route progress improves, that scenario can legitimately end with a
    later weapon utility sample after proving the health-seeking behavior.

No new upstream Q3A files were imported in this round; this is WORR-native
bot-nav, bot-brain, scenario-harness, roadmap, documentation, build, and
release-staging work.

## Validation

- `meson compile -C builddir-win`
  - Passed. The warnings observed were existing q2aas/vendor warnings.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed `.install/`.
- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py tools/bot_release/test_run_bot_acceptance.py`
  - Passed: 83 passed.
- Focused route/navigation batch passed 12/12 from
  `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`.
  - Representative rows kept `route_failures=0`.
  - `trace_checked_corner_cutting` recorded `lookahead_uses=5`,
    `lookahead_sequential_fallbacks=5`, and
    `lookahead_close_point_skips=10`.
  - `coop_door_elevator` recorded `lookahead_uses=20`,
    `lookahead_sequential_fallbacks=20`, and no route failures.
- Focused q2dm2 survival row passed 1/1 from
  `.tmp\bot_scenarios\route_sequential_trace_q2dm2_fix.json`.
- Full implemented bot scenario sweep passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_route_sequential_trace_lookahead_fix.json`.
- Release acceptance passed 15/15 with 0 warnings from
  `.tmp\bot_release\bot_release_acceptance_route_sequential_trace_lookahead.txt`.
