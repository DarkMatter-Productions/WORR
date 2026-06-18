# Q3A BotLib scenario promotion diagnostics

Date: 2026-06-18

Task: `DV-03-T05`

## Summary

This pass prepares the pending scenario harness rows for source-backed promotion without faking engine results. The four planned server smoke modes remain:

| Scenario | Planned mode | Promotion target |
| --- | ---: | --- |
| `engage_enemy` | `20` | Enemy acquisition, attack intent, applied attack button, and attributed damage. |
| `switch_weapons` | `21` | Preferred weapon choice, switch request, switch completion, and expected weapon match. |
| `health_armor_pickup` | `22` | Low-health/low-armor item scoring, goal assignment, pickup completion, and pickup deltas. |
| `team_objective` | `23` | Objective assignment, objective route commands, objective reach, and flag pickup. |

The harness still reports these scenarios as `pending`. Promotion now requires more than metric-name presence: `--pending-gap-report` evaluates explicit promotion `MetricCheck` and `MarkerMetricCheck` criteria against any source-backed fixture row that appears in a future report.

## Harness behavior

`tools/bot_scenarios/run_bot_scenarios.py` now exposes promotion checks in catalog, pending, and gap-report JSON:

- `promotion_required_metrics`
- `promotion_required_marker_metrics`
- `promotion_metric_checks`
- `promotion_marker_checks`
- `promotion_metric_check_results`
- `promotion_marker_check_results`
- `failed_metric_checks`
- `failed_marker_checks`

Gap reports stay diagnostic when source work lands incrementally. A row for mode `20`, `21`, `22`, or `23` can exist and still be blocked if the row is not `passed`, the smoke mode does not match, required metrics are absent, or present metrics fail their promotion checks. Marker-backed proofs receive the same treatment through `promotion_marker_checks`.

## Current promotion gaps

The current `.tmp/bot_scenarios/latest_report.json` fixture still has no rows for:

- `engage_enemy`
- `switch_weapons`
- `health_armor_pickup`
- `team_objective`

The latest gap report therefore marks all four as blocked with `4` missing rows and `42` missing status metrics. There are no failed metric or marker checks yet because no source-backed pending rows exist in that fixture.

## Tests

`tools/bot_scenarios/test_run_bot_scenarios.py` now covers:

- Pending catalog exposure of promotion check criteria.
- Semantic promotion blocking when all metric names are present but values fail, such as `last_combat_damage=0` or `route_failures=1`.
- Marker-aware promotion diagnostics through a synthetic pending scenario with a failing marker metric.
- Existing ready-state, missing-row, parser, comparison, profile-spawn, and optional latest-report fixture coverage.

## Validation

Command:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed.

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed. `13` tests ran.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_after_promotion_checks.json --markdown-out .tmp\bot_scenarios\pending_gap_after_promotion_checks.md
```

Result: passed. Summary was `0` ready, `4` blocked, `4` missing rows, `42` missing status metrics, `0` failed metric checks, and `0` failed marker checks.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28300 --format text --json-out .tmp\bot_scenarios\implemented_after_promotion_checks.json
```

Result: passed. All five implemented scenarios passed: `spawn_route_to_item`, `recover_from_stall`, `multi_bot_reservation`, `map_change_repeat`, and `profile_backed_spawn`.

## Remaining gaps

- The four pending scenarios still need source-backed smoke rows and real engine metrics before they can be promoted.
- Partial mode `20` through `23` implementations should be expected while server-side work is in progress; the gap report is intended to show missing counters and failed checks without changing scenario status.
- `team_objective` still needs deterministic team-objective setup and real objective event counters before the harness can decide whether a map override or source-managed map transition is required.
