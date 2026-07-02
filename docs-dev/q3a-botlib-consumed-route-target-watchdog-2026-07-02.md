# Q3A BotLib Consumed Route Target Watchdog

Date: 2026-07-02

Tasks: `FR-04-T02`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round investigated the persistent live symptom where bots could still
spin or churn in place near route nodes after the route target anti-spin and
route movement projection fixes. The prior fixes made command steering more
stable, but the route-progress watchdog still had a blind spot: a bot that was
already inside the current route target radius could receive fresh progress
credit every frame, even when final goal distance was not improving.

The second contributing issue was axis mismatch. The watchdog compared route
target changes with full 3D distance, while route progress and target-reached
checks are horizontal. On stairs, ledges, movers, or vertically noisy AAS
samples, a target could look "new" only because its z value changed, then earn
another route-target-reached progress reset without the bot actually moving
through the route.

## Implementation

- `src/game/sgame/bots/bot_nav.cpp`
  - Changed the route-progress target-shift check to use horizontal distance,
    matching the rest of the watchdog's progress model.
  - Split route-target arrival into first arrival and repeated consumed-target
    cases. Reaching a fresh target still counts as progress once; remaining
    inside the same already-reached target radius no longer resets stagnant
    progress unless the bot is also making meaningful final-goal or target
    distance progress.
  - Preserved the existing stuck escalation path, so consumed local targets can
    now flow into repath, short recovery movement, interaction retry, and goal
    blacklist logic instead of being indefinitely excused.
- `src/game/sgame/bots/bot_nav.hpp`
  - Added compact consumed-route-target counters and last-check fields.
- `src/game/sgame/bots/bot_brain.cpp`
  - Emitted the new diagnostics on `q3a_bot_nav_policy_status` instead of
    lengthening the already-large frame-command status row.
- `tools/bot_scenarios/run_bot_scenarios.py`
  - Added source hints for the new nav-policy metrics.

No new upstream Q3A files were imported in this round; this is WORR-native
navigation watchdog, status, scenario-harness, documentation, and
release-staging work.

## Validation

- `meson compile -C builddir-win`
  - Passed after splitting the new diagnostics out of the frame-command status
    formatter. The initial attempt exposed MSVC's `std::format` nesting limit
    once that very large status row crossed 256 arguments.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed `.install/`.
- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  - Passed: 66 passed.
- Focused route/combat/movement batch passed 10/10 from
  `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json`.
  - Raw scenario logs include `stuck_target_reached_progresses`,
    `stuck_consumed_target_stalls`, `last_stuck_target_distance_sq`, and
    `last_stuck_consumed_target` on `q3a_bot_nav_policy_status`.
  - `combat_survival_regression` recorded consumed-target stalls without route
    failures, confirming the new signal is live while preserving scenario pass
    behavior.
- Full implemented bot scenario sweep passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_consumed_target_watchdog.json`.
- Release acceptance passed 15/15 with 0 warnings from
  `.tmp\bot_release\bot_release_acceptance_consumed_target_watchdog.json`.

Build warnings observed during validation were the existing q2aas/vendor
warnings from the full rebuild and were not introduced by this watchdog change.
