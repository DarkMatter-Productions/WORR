# Q3A BotLib Status Surface Integration

Date: 2026-06-18

Tasks: `FR-04-T12`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This documentation lane records the current status-surface integration for the
Q3A BotLib behavior, navigation, and objective work. The runtime now emits
optional field families through the existing status markers so scenario and
pending-gap tooling can observe newly landed counters before they become hard
pass/fail gates.

This is observability and status integration only. It does not add new
autonomous aim behavior, autonomous weapon/inventory dispatch policy, or
autonomous team-objective decision making.

## Status Markers

- `q3a_bot_action_status` now carries optional action-dispatch counters for
  weapon and inventory command-request build, validation, dispatch, submit,
  defer, and failure outcomes.
- `q3a_bot_action_status` and related status rows can expose optional aim-policy
  counters for evaluation, allow/block buckets, and last-policy metadata.
- `q3a_bot_action_status` and frame status can expose optional special item
  utility buckets for high-value pickups, focused health/armor utility, powerup
  and tech categories, mobility, protection, invisibility, and objective pickup
  interest.
- `q3a_bot_frame_command_status` now exposes optional route-target
  stabilization counters and last sampled target metadata for the route-command
  lane.
- `q3a_bot_objective_status` now exposes objective lane metadata for the team
  role policy surface, including lane selection counters, last selected lane,
  lane-priority breakdown, and reason strings.

The important contract is that these fields are optional evidence. The harness
can report them in text, JSON, Markdown, and pending-gap output when present,
but scenario success is still controlled by explicit checks and marker checks.

## Validation Status

Main-agent validation for this round was reported as:

- `bot_brain` object compile: passed.
- `meson compile -C builddir-win sgame_x86_64`: passed.
- `python tools\bot_scenarios\test_run_bot_scenarios.py`: passed, 25 tests.
- Bot profile tests: passed, 18 tests.

This docs-only lane did not re-run the full compile or scenario/profile suites;
the entries above are recorded as observed main-agent results for the
implementation round.

## Residual Work

Promote the optional fields into hard scenario gates only after the corresponding
live behavior lands and stable runtime logs prove the counters are sourced from
real bot behavior rather than helper scaffolding.
