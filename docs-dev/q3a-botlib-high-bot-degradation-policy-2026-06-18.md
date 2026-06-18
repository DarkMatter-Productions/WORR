# Q3A BotLib High-Bot Degradation Policy

Date: 2026-06-18

Related tasks: `DV-03-T05`, `FR-04-T16`

## Summary

This slice makes high bot count degradation explicit in the Python scenario harness without changing server/game code. The harness now distinguishes the fast eight-bot reservation pressure proof from the long eight-bot soak proof and reports the policy in JSON, Markdown, catalog, and text output.

The policy is intentionally split:

- `multi_bot_reservation` remains the short pressure gate. No degradation is allowed there: eight requested bots must emit commands, route commands must stay clean, and item reservation pressure must reach all eight bots.
- `high_bot_soak_degradation` is the manual mode `18` long soak gate. It allows final item reservation occupancy and peak reservations to fall below the short pressure proof because long runs naturally consume, hide, clear, blacklist, and reassign item goals. It still preserves command throughput, route-command throughput, active bot count, route cleanliness, valid route slots, route debug coverage, and progress reporting.

## Harness Changes

- Added `DegradationPolicy` metadata to `tools/bot_scenarios/run_bot_scenarios.py`.
- Added degradation policy metadata and checks to `multi_bot_reservation`.
- Added manual scenario `high_bot_soak_degradation` for `sv_bot_frame_command_smoke 18` with explicit `sv_bot_frame_command_smoke_soak_ms=600000`.
- Kept the long soak out of `--scenario implemented` and default `all` selection. It is selectable by name, `--scenario soak`, or `--scenario manual`.
- Added policy evaluation to runtime results. A policy check failure becomes a scenario failure, prefixed as a degradation-policy failure.
- Added policy fields to catalog/run JSON, Markdown scenario tables, text reports, and catalog text output.
- Added key metric reporting for soak status/markers such as `elapsed_ms`, `reports`, `expected_min_commands`, `route_invalid_slots`, `route_debug_missing_frames`, `item_goal_active_reservations`, and `skipped_inactive`.

The per-bot/sec numeric budget remains owned by `tools/bot_perf/default_soak_budget.json`. The scenario harness references that budget profile rather than duplicating the perf analyzer's derived metrics.

## Test Coverage

`tools/bot_scenarios/test_run_bot_scenarios.py` now covers:

- Catalog/report shape for the high-bot degradation policy.
- Manual selection behavior that prevents the ten-minute soak from entering the default implemented suite.
- Passing long-soak policy semantics where reservation occupancy decays but preserved throughput and route-clean checks pass.
- Failing long-soak policy semantics for silent command-throughput, route-failure, duration, and progress-report regressions.

## Validation

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python tools\bot_scenarios\test_run_bot_scenarios.py
python tools\bot_scenarios\run_bot_scenarios.py --catalog --scenario high_bot_soak_degradation --format text
```

Results:

- `py_compile` passed.
- Unit tests passed: 23 tests, 0 failures.
- Catalog CLI check passed and reported `high_bot_soak_degradation` as manual-only, tagged `soak,high_bot,degradation`, with the `high_bot_long_soak` degradation policy and `tools/bot_perf/default_soak_budget.json` budget profile.

## Integration Notes

- The long soak still requires an explicit high timeout, for example `--timeout 720`, because the runtime smoke is configured for a ten-minute `600000` ms window.
- The policy assumes mode `18` keeps emitting `q3a_bot_frame_command_smoke_soak=begin`, `q3a_bot_frame_command_smoke_soak=complete`, and the final `q3a_bot_frame_command_status` fields documented by the current soak output.
- If server-side mode `18` changes its duration, progress cadence, or status metric names, update the scenario policy and `tools/bot_perf/default_soak_budget.json` together.
