# Q3A BotLib Interaction Mover Ride-State Round - 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round turns the train key-carry mover bridge proof into an explicit
wait/board/ride/leave lifecycle. The prior interaction-arrival work proved the
bot could discover a `func_train`, activate it, resolve a routeable post-mover
arrival, and route to the lock without proof warps. The new slice records the
state transitions around that movement so later map rows can reason about
mover traversal as a lifecycle instead of only as a route endpoint.

## Implementation

- Added `BotNavMoverRidePhase` with `wait`, `board`, `ride`, and `leave`
  phases.
- Added `BotNav_RecordMoverRideState`, backed by `BotNavRouteStatus`, so
  mover-like interactions can report lifecycle checks, phase counters, invalid
  skips, grounded-on-mover samples, moving-mover samples, and last entity/kind,
  action, client, mover move-state, ground entity, position, and distance.
- Wired the train key-carry proof through the lifecycle:
  wait is recorded when the bridge interaction is observed, board when the
  post-mover arrival is resolved, ride while the interaction-arrival route is
  requested, and leave when the arrival route is reached.
- Expanded `q3a_bot_frame_command_status` with the ride-state marker fields.
- Hardened `coop_campaign_key_carry_train` so the scenario requires all four
  phases and a final `leave` phase on the train mover entity.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Focused `coop_campaign_key_carry_train` validation:
  `.tmp\bot_scenarios\mover_ride_state.json`
- Full implemented-suite validation:
  `.tmp\bot_scenarios\implemented_after_mover_ride_state.json`

Focused validation passed with:
`interaction_mover_ride_checks=5`,
`interaction_mover_ride_wait_states=1`,
`interaction_mover_ride_board_states=1`,
`interaction_mover_ride_ride_states=2`,
`interaction_mover_ride_leave_states=1`,
`interaction_mover_ride_invalid_skips=0`,
`last_interaction_mover_ride_phase=4`,
`last_interaction_mover_ride_entity=60`,
`last_interaction_mover_ride_kind=4`, and
`last_interaction_mover_ride_area=1058`.

The live train proof did not sample physical grounding on the mover or a moving
`MoveState::Up` / `MoveState::Down` frame during this short smoke. Those values
are retained as diagnostics rather than hard gates. The next movement slice
should promote those diagnostics on a map/case that naturally keeps a bot on
moving geometry long enough to assert physical boarding and moving ride frames.
