# Q3A BotLib Source-Aware Raw Diagnostics

Task IDs: DV-03-T05

## Summary

This Worker G tooling slice hardens the pending scenario promotion path for raw
reserved modes `20` through `23`. It does not change runtime code under `src/`.

The scenario harness already accepted raw reserved-mode logs for:

- `20`: `engage_enemy`
- `21`: `switch_weapons`
- `22`: `health_armor_pickup`
- `23`: `team_objective`

This update makes those diagnostics more robust when runtime status markers are
emitted in multiple passes, and makes missing promotion counters easier to
trace back to the source status line that should have produced them.

## Tooling Changes

- Raw diagnostics now preserve marker event order inside each reserved-mode run.
- If a metric appears under multiple accepted raw markers, the later log line
  now wins. Previously, the final value could depend on fixed marker category
  order.
- Diagnostics record the latest marker source and source line for each metric
  through `metric_latest_sources` and `metric_lines`.
- Pending-gap rows now include `missing_metric_sources`, mapping each absent
  promotion metric to the raw status marker expected to emit it.
- Text and Markdown pending-gap output now show the expected source marker for
  missing metrics, for example:

```text
item_health_pickups<-q3a_bot_action_status
```

## Source Hints

The missing-source hints are intentionally narrow for the reserved proof
counters:

- frame smoke health, pass/fail, and route cleanliness:
  `q3a_bot_frame_command_status`
- combat action, weapon-switch, and health/armor proof counters:
  `q3a_bot_action_status`
- team objective proof counters:
  `q3a_bot_objective_status`
- raw route/source counters:
  `q3a_bot_source_counter_status`

Combat enemy fact counters can be accepted from action, blackboard, or frame
status because prior implementation slices have emitted combat perception facts
from more than one layer.

## Validation

Commands:

```powershell
python -B tools\bot_scenarios\test_run_bot_scenarios.py
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```

Results:

- `20` offline harness tests passed.
- Both Python files compile successfully.
