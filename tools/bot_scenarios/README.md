# Bot Scenario Smokes

Lightweight local harness for WORR Q3A BotLib scenario validation. It wraps existing dedicated-server smoke modes, parses `q3a_bot_frame_command_status`, and can emit text, JSON, Markdown, and comparison reports.

For implementation history and validation notes, see `docs-dev/q3a-botlib-scenario-smoke-harness-2026-06-18.md`.

## Requirements

- Python 3 standard library only.
- For catalog/report/test-only commands: no game launch is required.
- For implemented scenario runs: `.install/worr_ded_x86_64.exe` and packaged `basew` / `mm-rage` assets must exist, usually after a refreshed install.
- `profile_backed_spawn` also requires a staged smoke profile asset resolvable as `smoke`, normally `.install/basew/botfiles/bots/smoke_c.c`. The harness does not create profile assets.
- Reports and stdout/stderr artifacts are written under `.tmp/bot_scenarios/` by default.

## Quickstart

List known scenarios:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --list
```

Run the implemented smoke suite:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json
```

`implemented` runs the default short suite and skips manual long-running scenarios. Use an explicit scenario name or tag for long soaks.

Run one scenario:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario spawn_route_to_item --timeout 60
```

Run the manual high-bot degradation soak:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario high_bot_soak_degradation --timeout 720 --base-port 28000 --format text --json-out .tmp\bot_scenarios\high_bot_soak_report.json
```

Run only pending placeholders without launching the game:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --json-out .tmp\bot_scenarios\pending_report.json
```

## Scenarios

Implemented:

- `spawn_route_to_item`: mode `2`, verifies item-backed route commands.
- `recover_from_stall`: mode `4`, verifies stuck detection and recovery commands.
- `multi_bot_reservation`: mode `17`, verifies eight-bot route pressure and item reservation peak.
- `map_change_repeat`: mode `19`, verifies two map-repeat cycles, one map change, and final bot cleanup.
- `profile_backed_spawn`: `sv_bot_profile_smoke 2`, verifies profile-backed spawn, userinfo profile fields, and final cleanup.

Manual long-running:

- `high_bot_soak_degradation`: mode `18`, ten-minute eight-bot soak. Select it by name or with `--scenario soak`; it is omitted from `--scenario implemented` so the default suite stays fast.

Pending placeholders:

- `engage_enemy`
- `switch_weapons`
- `health_armor_pickup`
- `team_objective`

Pending rows are reported but do not fail the suite unless `--fail-on-pending` is passed.

## Catalog

Emit the declarative scenario catalog:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format json --json-out .tmp\bot_scenarios\catalog_report.json
```

Catalog entries include scenario status, task IDs, smoke mode, runtime budget, manual-only status, selection tags, required status metrics, marker metrics, degradation policy metadata, extra cvars, and pending blockers.

Pending catalog rows also include planned source smoke modes, promotion-required metrics, and promotion check criteria. Those fields describe the counters and pass/fail values a future source-backed smoke must satisfy before the placeholder can become an implemented scenario.

## High-Bot Degradation Policy

High bot count validation is split between a fast pressure proof and an opt-in soak:

- `multi_bot_reservation` is the short eight-bot pressure gate. It does not allow degradation: all eight bots must emit commands, route commands must stay clean, and item reservation pressure must reach eight active reservations.
- `high_bot_soak_degradation` is the long eight-bot degradation gate. It allows final item reservation occupancy and peak reservations to drop below the short proof because long soaks consume, hide, clear, and reassign goals over time. It still requires sustained command throughput, sustained route commands, zero route failures, zero invalid route slots, no inactive target bots, and regular soak progress reports.
- Derived per-bot/sec budget thresholds remain owned by `tools/bot_perf/default_soak_budget.json`; the scenario harness reports that budget profile instead of duplicating the perf analyzer.

## Pending Gap Reports

Analyze an existing JSON report, usually `.tmp\bot_scenarios\latest_report.json`, to see which pending scenario rows and source counters are still missing:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_report.json
```

This command does not launch the game. It compares pending placeholders against the report fixture and prints whether each scenario is ready for harness promotion or blocked by missing scenario rows, wrong smoke modes, pending fixture rows, absent status/marker metrics, or failed promotion metric checks.

Raw reserved-mode logs can be included when modes `20` through `23` have been run outside the normal scenario catalog:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --pending-gap-raw-log .tmp\bot_scenarios\raw_modes --format text --json-out .tmp\bot_scenarios\pending_gap_with_raw_modes.json
```

`--pending-gap-raw-log` accepts a file or directory and may be repeated. The parser groups logs by `q3a_bot_frame_command_smoke_scenario=begin`, then reads the latest `q3a_bot_frame_command_status`, `q3a_bot_blackboard_status`, `q3a_bot_action_status`, `q3a_bot_objective_status`, and `q3a_bot_source_counter_status` markers for that reserved run. If repeated diagnostics exist for one reserved mode, the latest parsed run for that mode is the promotion source. Raw diagnostics can satisfy marker/metric presence in the gap report, but the scenarios remain pending until the real runtime counters pass their promotion checks.

When the same metric appears on more than one raw marker inside a reserved run, the later log line wins. Gap reports also include `missing_metric_sources` so absent promotion counters identify the raw status marker that should emit them, for example `item_health_pickups<-q3a_bot_action_status`.

The planned source-backed smoke mode numbers are fixed for compatibility with in-progress server work:

- `engage_enemy`: mode `20`
- `switch_weapons`: mode `21`
- `health_armor_pickup`: mode `22`
- `team_objective`: mode `23`

If one of those modes lands before every planned metric exists, the gap report should remain useful: it will show the row, mode, present metrics, missing metrics, failed metric checks, marker-check failures, and any scenario-specific related telemetry that helps explain the block without pretending the scenario is promoted.

For `health_armor_pickup`, generic `item_goal_*`, `last_item_goal_*`, and `last_failed_goal_*` status metrics are surfaced as related telemetry when present. They do not satisfy the health/armor promotion gate by themselves; the gate still requires health/armor-specific boost, assignment, pickup, and delta counters.

## Markdown And Comparison Reports

Write JSON and Markdown for a run:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md
```

Compare a current report with a previous JSON report:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --json-out .tmp\bot_scenarios\pending_compare_report.json --markdown-out .tmp\bot_scenarios\pending_compare_report.md --compare .tmp\bot_scenarios\latest_report.json
```

The comparison is name-based and reports status changes plus selected key metric deltas. It is intended as a quick local regression aid, not a statistical trend analyzer.

## Tests

Run offline parser/reporting tests:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

The tests use only the Python standard library. If `.tmp/bot_scenarios/latest_report.json` exists, they also validate key real-report scenario outcomes, including `map_change_repeat` when present. If the fixture is missing, that fixture check is skipped.

Compile-check the harness:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```
