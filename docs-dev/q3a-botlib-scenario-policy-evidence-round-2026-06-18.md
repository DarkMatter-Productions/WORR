# Q3A BotLib Scenario Policy Evidence Round - 2026-06-18

Task IDs: FR-04-T15, DV-07-T06

## Summary

This round strengthened the bot scenario harness evidence for the runtime policy consumers added during the Q3A BotLib/AAS port. The changes are limited to the scenario harness catalog, parser, tests, and README documentation.

## Implementation

- Extended raw reserved-mode diagnostics so modes `24` and `25` map to `aim_fairness_policy_integration` and `item_timer_fairness_signals`.
- Added optional-field recognition for live-aim consumer counters, projectile-lead counters, item timing-policy counters, and item timing-consumer counters.
- Added a derived marker metric, `item_timing_consumer_ready_or_live`, from `item_timing_consumer_ready` or `item_timing_consumer_live_pickups`.
- Kept the aim fairness gate strict on live-aim policy evidence: `live_aim_evaluations >= 1` and `live_aim_fire_allowed >= 1`.
- Promoted item timer evidence to require `item_timing_consumer_evaluations >= 1` and `item_timing_consumer_ready_or_live >= 1`.
- Added `missing_policy_consumer_fields` to pending-gap JSON, Markdown, and text output so absent live-aim or item timing-consumer evidence is called out separately from generic marker gaps.
- Moved the `engage_enemy` `combat_withheld_fire == 0` proof to `q3a_bot_action_status`, matching the compact combat status surface used when `q3a_bot_action_detail_status` reaches the engine print-length limit.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python tools\bot_scenarios\test_run_bot_scenarios.py`
- `python tools\bot_scenarios\run_bot_scenarios.py --binary .\.install\worr_ded_x86_64.exe --install-dir .\.install --game basew --map mm-rage --timeout 90 --artifact-dir .tmp\bot_scenarios\round2-engage-fix --json-out .tmp\bot_scenarios\round2-engage-fix.json --scenario engage_enemy`
- `python tools\bot_scenarios\run_bot_scenarios.py --binary .\.install\worr_ded_x86_64.exe --install-dir .\.install --game basew --map mm-rage --timeout 90 --artifact-dir .tmp\bot_scenarios --json-out .tmp\bot_scenarios\implemented-latest.json --markdown-out .tmp\bot_scenarios\latest_report.md --scenario implemented`

The final implemented suite reports 15 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.
