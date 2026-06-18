# Q3A BotLib Pending Scenario Promotion Tooling

Task IDs: DV-03-T05

## Summary

This Worker E slice advances the pending scenario gap tooling for reserved smoke
modes 20 through 23 without changing engine or game runtime code. The harness
can now consume the dedicated status markers that the runtime workers are
emitting as split lines instead of requiring every proof counter to fit on the
primary `q3a_bot_frame_command_status` line.

## Tooling Changes

- `tools/bot_scenarios/run_bot_scenarios.py` now treats these marker lines as
  raw reserved-mode metric sources:
  - `q3a_bot_frame_command_status`
  - `q3a_bot_blackboard_status`
  - `q3a_bot_action_status`
  - `q3a_bot_objective_status`
  - `q3a_bot_source_counter_status`
- Raw diagnostics still begin at
  `q3a_bot_frame_command_smoke_scenario=begin` and use the begin row for
  reserved-mode setup checks.
- When one marker appears more than once inside the same raw reserved-mode run,
  the latest row for that marker wins.
- When multiple raw diagnostics exist for the same reserved mode, the latest
  parsed diagnostic for that mode is used in the pending-gap report. Older
  passing diagnostics no longer mask a newer failed run.
- Promotion checks remain strict. Raw diagnostics can fill metric presence, but
  a scenario is only ready when the raw status is passed, the mode/setup marker
  checks pass, all required metrics are present, and every semantic check
  passes.

## Test Coverage

`tools/bot_scenarios/test_run_bot_scenarios.py` now covers:

- raw promotion success for all reserved modes:
  - mode 20: `engage_enemy`
  - mode 21: `switch_weapons`
  - mode 22: `health_armor_pickup`
  - mode 23: `team_objective`
- split source-counter marker parsing through `q3a_bot_source_counter_status`
- dedicated objective metric parsing through `q3a_bot_objective_status`
- latest-diagnostic selection per reserved mode
- blocked results when a dedicated marker value is present but fails a semantic
  promotion check
- existing blocked behavior for missing health/armor pickup proof

## Runtime Metrics Main Thread Must Emit

The main runtime thread still owns the real game/engine implementation for the
proof counters. The promotion gate expects these metrics by scenario, either on
`q3a_bot_frame_command_status` or on one of the dedicated marker lines above.

`engage_enemy` mode 20:

- `pass`
- `combat_enemy_acquisitions`
- `combat_enemy_visible`
- `combat_enemy_shootable`
- `combat_fire_decisions`
- `action_attack_decisions`
- `action_applied_attack_buttons`
- `combat_damage_events`
- `last_combat_enemy_client`
- `last_combat_damage`
- `route_failures`

`switch_weapons` mode 21:

- `pass`
- `combat_weapon_switch_decisions`
- `action_weapon_switch_decisions`
- `action_pending_weapon_switches`
- `weapon_switch_requests`
- `weapon_switch_completions`
- `weapon_switch_failures`
- `weapon_switch_expected_item`
- `weapon_switch_actual_item`
- `weapon_switch_expected_match`

`health_armor_pickup` mode 22:

- `pass`
- `item_low_health_boosts`
- `item_low_armor_boosts`
- `item_health_goal_assignments`
- `item_armor_goal_assignments`
- `item_health_pickups`
- `item_armor_pickups`
- `last_health_pickup_delta`
- `last_armor_pickup_delta`
- `route_failures`

`team_objective` mode 23:

- `pass`
- `team_objective_evaluations`
- `team_objective_assignments`
- `team_objective_route_requests`
- `team_objective_route_commands`
- `team_objective_reaches`
- `team_objective_flag_pickups`
- `last_team_objective_type`
- `last_team_objective_client`
- `last_team_objective_item`
- `route_failures`

All reserved modes must continue emitting
`q3a_bot_frame_command_smoke_scenario=begin` with the expected `mode`, `combat`,
`weapon_switch`, `item_focus`, `team_objective`, `target`, and `gametype` fields.

## Validation

Run:

```powershell
python -B tools\bot_scenarios\test_run_bot_scenarios.py
```
