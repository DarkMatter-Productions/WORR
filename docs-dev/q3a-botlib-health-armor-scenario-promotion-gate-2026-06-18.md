# Q3A BotLib Health/Armor Scenario Promotion Gate

Task: `DV-03-T05`

## Summary

`health_armor_pickup` was evaluated for promotion from pending to implemented against the staged `.install` dedicated server. The reserved source smoke mode `sv_bot_frame_command_smoke 22` launches, exits cleanly, reports `pass=1`, reports `route_failures=0`, and emits the expected scenario begin marker with `item_focus=health_armor`.

The scenario was not promoted because the current source telemetry does not yet satisfy the health/armor-specific promotion checks. The status line exposes generic item-goal telemetry such as `item_goal_assignments`, `last_item_goal_item`, and `last_item_goal_score`, but it does not report the required health/armor-specific boost, goal-assignment, pickup, or delta counters.

## Validation

Raw staged smoke command:

```powershell
.\.install\worr_ded_x86_64.exe +set game basew +set basedir E:\Repositories\WORR\.install +set net_port 28122 +set logfile 1 +set logfile_name worker_h_mode22_20260618T120651Z +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_route 1 +set sg_bot_debug_goal 1 +set sv_bot_frame_command_smoke 22 +map mm-rage
```

Result: exit code `0`; artifacts:

- `.tmp\bot_scenarios\worker_h_mode22_20260618T120651Z\mode22.stdout.txt`
- `.tmp\bot_scenarios\worker_h_mode22_20260618T120651Z\mode22.stderr.txt`

Promotion-gap command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario health_armor_pickup --pending-gap-report .tmp\bot_scenarios\implemented_report.json --pending-gap-raw-log .tmp\bot_scenarios\worker_h_mode22_20260618T120651Z --format both --json-out .tmp\bot_scenarios\worker_h_mode22_gap.json --markdown-out .tmp\bot_scenarios\worker_h_mode22_gap.md
```

Result: `blocked`.

Key result fields:

- `fixture_status=passed`
- `fixture_smoke_mode=22`
- `missing_marker_metrics=[]`
- `failed_marker_checks=0`
- `present_metrics=[pass, route_failures]`
- `missing_status_metrics=8`
- `failed_metric_checks=8`

Missing required status metrics:

- `item_low_health_boosts`
- `item_low_armor_boosts`
- `item_health_goal_assignments`
- `item_armor_goal_assignments`
- `item_health_pickups`
- `item_armor_pickups`
- `last_health_pickup_delta`
- `last_armor_pickup_delta`

## Harness Changes

The pending-gap diagnostics now include scenario-specific related metrics for `health_armor_pickup`. Generic item-goal metrics with `item_goal_`, `last_item_goal_`, or `last_failed_goal_` prefixes are shown as related telemetry when present, but they do not satisfy the promotion gate.

A regression test covers the exact blocked shape: raw mode 22 passes and has the correct begin marker plus generic item-goal telemetry, but remains blocked until health/armor-specific pickup proof is available.
