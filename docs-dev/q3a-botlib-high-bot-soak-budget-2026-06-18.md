# Q3A BotLib High-Bot Soak Budget

Date: 2026-06-18

Task IDs: `DV-03-T05`, `DV-05-T02`, `DV-05-T05`, `FR-04-T16`

## Summary

This slice aligns `tools/bot_perf/default_soak_budget.json` and `tools/bot_perf/README.md` with the manual `high_bot_soak_degradation` policy owned by the bot scenario harness.

The default budget remains a generous ten-minute eight-bot `mm-rage` soak budget, but its descriptions now make the high-bot invariants explicit. The sidecar is still the numeric budget profile referenced by the scenario policy; the scenario harness owns launching mode `18`, marker checks, and manual-only selection.

## Budgeted Invariants

The default soak budget requires:

- Smoke pass, ten-minute duration slack, and exactly eight detected bots.
- Sustained command throughput through `commands_per_bot_sec` and raw `commands`.
- Sustained route-command throughput through `route_commands_per_bot_sec` and raw `route_commands`.
- Route cleanliness through `route_failures=0` and `route_invalid_slots=0`.
- Route-debug coverage through `route_debug_missing_frames=0`.
- Active target bots through `expected_min_commands=8` and `skipped_inactive=0`.
- Regular progress visibility through `progress_reports`.

The budget also retains pressure thresholds for route-query rate, route refresh/reuse ratios, debug work units, and recovery command churn. These are regression guardrails rather than policy assertions that the long soak must avoid all recovery behavior.

## Allowed Degradation

The budget intentionally does not constrain `item_goal_active_reservations` or `item_goal_peak_active_reservations`.

That matches the manual high-bot policy: long soaks can consume, hide, clear, blacklist, and reassign item goals, so final item-reservation occupancy is not equivalent to the short `multi_bot_reservation` pressure proof.

## Optional CPU Fields

The budget now includes optional source-counter checks:

- `bot_frame_cpu_ms_per_bot_sec`
- `route_query_cpu_ms_per_bot_sec`
- `route_reuse_cpu_ms_per_bot_sec`
- `q3a_route_cpu_ms_per_bot_sec`

Each uses `required: false`. Legacy ten-minute soak logs without source-counter timing emit budget warnings, not failures. These can be promoted later after stable long-soak CPU baselines exist.

## Operator Command

Run the manual ten-minute soak with timeout headroom:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario high_bot_soak_degradation --timeout 720 --base-port 28000 --format text --json-out .tmp\bot_scenarios\high_bot_soak_report.json
```

Analyze the captured stdout with the default budget:

```powershell
$soakReport = Get-Content .tmp\bot_scenarios\high_bot_soak_report.json | ConvertFrom-Json
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json "$($soakReport.scenarios[0].stdout_path)"
```

For preexisting soak fixtures, the direct analyzer command remains:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

## Validation

Commands run:

```powershell
python -m json.tool tools\bot_perf\default_soak_budget.json > $null
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
python .\tools\bot_perf\test_analyze_bot_perf.py
python tools\bot_scenarios\run_bot_scenarios.py --catalog --scenario high_bot_soak_degradation --format text
```

Results:

- JSON validation passed.
- Existing ten-minute fixture passed the expanded default budget: `budget: pass checks=22`.
- The fixture reported expected optional warnings for missing CPU fields: `bot_frame_cpu_ms_per_bot_sec`, `route_query_cpu_ms_per_bot_sec`, `route_reuse_cpu_ms_per_bot_sec`, and `q3a_route_cpu_ms_per_bot_sec`.
- Bot perf regression tests passed: `Ran 12 tests`, `OK`.
- Scenario catalog validation passed and reported `high_bot_soak_degradation` as manual-only mode `18`, tagged `soak,high_bot,degradation`, with budget profile `tools/bot_perf/default_soak_budget.json`.

## Integration Risks

- The current checked fixture predates long-soak CPU source-counter output, so optional CPU thresholds are shape checks until a new ten-minute source-counter soak lands.
- The budget and scenario policy must stay in sync if mode `18` changes duration, progress cadence, target bot count, or status field names.
- Strict gates should continue to use like-for-like ten-minute eight-bot `mm-rage` logs; the budget is not calibrated for short scenario runs.
