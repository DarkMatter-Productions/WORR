# WORR Bot Perf Tools

Small local tools for analyzing Q3A BotLib frame-command smoke output without launching the game.

For implementation details, baseline numbers, and instrumentation gaps, see:

- `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`
- `docs-dev/q3a-botlib-high-bot-soak-budget-2026-06-18.md`

## Quickstart

Analyze the current ten-minute soak log:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

The analyzer reads `q3a_bot_frame_command_smoke_soak` progress/completion lines plus the final `q3a_bot_frame_command_status` line. It reports derived metrics such as commands per bot per second, route refresh/reuse ratios, debug-output pressure, recovery-command pressure, and missing CPU/visibility instrumentation.

Run the manual ten-minute high-bot soak through the scenario harness with enough timeout headroom:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario high_bot_soak_degradation --timeout 720 --base-port 28000 --format text --json-out .tmp\bot_scenarios\high_bot_soak_report.json
```

The soak itself is configured for `sv_bot_frame_command_smoke 18` with `sv_bot_frame_command_smoke_soak_ms=600000`. The `720` second timeout leaves room for startup, staged bot joins, shutdown, and slower local machines.

## Common Commands

Text output:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

JSON output:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --format json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

CSV output:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --format csv .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Analyze multiple logs:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py .tmp\q3a_bot_nav_soak_redirect_short.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Analyze scenario stdout logs with durations from the scenario runner report:

```powershell
$scenarioReport = Get-Content .tmp\bot_scenarios\latest_report.json | ConvertFrom-Json
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json "$($scenarioReport.artifact_dir)\*.stdout.txt"
```

PowerShell users should quote glob inputs so the analyzer expands them consistently. Scenario stdout logs without `--scenario-report` still produce raw status counters and route ratios, but `duration_sec` and per-second rates stay `n/a` unless the stdout itself contains soak elapsed markers.

## Budgets

Use the generous default soak budget for the manual `high_bot_soak_degradation` run:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

Analyze the stdout captured by the scenario harness:

```powershell
$soakReport = Get-Content .tmp\bot_scenarios\high_bot_soak_report.json | ConvertFrom-Json
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json "$($soakReport.scenarios[0].stdout_path)"
```

Budget files have checks under:

- `checks.metrics`: derived analyzer fields such as `commands_per_bot_sec`, `route_refresh_ratio`, and `debug_work_units_per_bot_sec`.
- `checks.status`: raw final status fields such as `route_failures`, `route_invalid_slots`, and `skipped_inactive`.

Each check supports numeric `min`, numeric `max`, optional boolean `required`, and optional `description`.

The default soak budget makes these high-bot invariants required:

- Smoke pass, ten-minute duration slack, and exactly eight detected bots.
- Sustained command and route-command throughput through both derived per-bot/sec rates and raw final counters.
- Zero route failures, zero invalid route slots, zero route-debug missing frames, and zero inactive target-bot skips.
- Regular soak progress reports.

It also keeps pressure budgets for route-query rate, route refresh/reuse ratios, debug work, and recovery command churn. It intentionally does not constrain `item_goal_active_reservations` or `item_goal_peak_active_reservations`; the long-soak policy allows item-reservation occupancy to decay as pickups are consumed, hidden, cleared, blacklisted, and reassigned.

CPU/source-counter checks such as `bot_frame_cpu_ms_per_bot_sec`, `route_query_cpu_ms_per_bot_sec`, `route_reuse_cpu_ms_per_bot_sec`, and `q3a_route_cpu_ms_per_bot_sec` are present but optional. Missing optional fields appear as budget warnings, not failures, until long-soak source-counter baselines are stable.

Exit behavior:

- `0`: all required checks passed.
- `1`: at least one required check failed or was missing.

## Comparison Reports

When more than one log is supplied, text output includes a comparison section with latest, best, and worst values for key metrics.

Comparison output also emits guard warnings when the input set is not like-for-like, such as mixed scenario names, mixed bot counts, mixed duration sources, or missing duration data. The report still prints the table, but those guards mean the best/worst values should be read as an overview rather than a strict regression ranking.

Write a Markdown report:

```powershell
python .\tools\bot_perf\analyze_bot_perf.py --budget .\tools\bot_perf\default_soak_budget.json --markdown-out .tmp\bot_perf\soak_compare.md .tmp\q3a_bot_nav_soak_10min_final.stdout.txt .tmp\q3a_bot_nav_soak_10min_final.stdout.txt
```

The Markdown report contains run, comparison, and budget tables. `.tmp/bot_perf/` is a good local scratch location for generated reports.

Scenario comparison report:

```powershell
$scenarioReport = Get-Content .tmp\bot_scenarios\latest_report.json | ConvertFrom-Json
python .\tools\bot_perf\analyze_bot_perf.py --scenario-report .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_perf\scenario_compare.md "$($scenarioReport.artifact_dir)\*.stdout.txt"
```

## Tests

Run parser/analyzer regression tests:

```powershell
python .\tools\bot_perf\test_analyze_bot_perf.py
```

Or through unittest:

```powershell
python -m unittest .\tools\bot_perf\test_analyze_bot_perf.py
```

The tests use synthetic logs for core parser and analyzer behavior. If `.tmp/q3a_bot_nav_soak_10min_final.stdout.txt` exists, an optional fixture test validates the known soak baseline; otherwise that test skips.

Compile-check the scripts:

```powershell
python -m py_compile .\tools\bot_perf\analyze_bot_perf.py .\tools\bot_perf\test_analyze_bot_perf.py
```
