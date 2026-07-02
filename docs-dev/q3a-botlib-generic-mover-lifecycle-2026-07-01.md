# Q3A BotLib Generic Mover Lifecycle

Date: 2026-07-01

Related tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T11`, `FR-04-T14`,
`FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round broadens mover lifecycle evidence beyond the focused train
key-carry bridge proof. Generic route-recovery interactions now record
separate wait, board, and terminal leave samples for mover-like wait/use
windows, and the standalone coop door/elevator plus coop live-loop scenario
rows hard-gate that lifecycle telemetry.

The change keeps the existing train wait/board/ride/leave proof intact while
making the more common coop mover/elevator path visible to the scenario
catalog. That gives M4/M6 a reusable signal that bots are not merely firing a
use command near a mover; they are entering a bounded interaction lifecycle
with valid mover metadata and a completion phase.

## Implementation

- `BotNav_GetRecoveryMove` now records both wait and board phases for
  `WaitUse` recovery interactions instead of collapsing the whole window into
  a single board sample.
- Generic mover recovery records terminal leave when the interaction window is
  about to expire, and expired mover-like interactions preserve that leave
  sample before clearing the active slot.
- `coop_door_elevator` and `coop_live_loop` now require
  `interaction_mover_ride_checks`, wait, board, leave, positive
  `last_interaction_mover_ride_entity`, mover kind metadata, final phase `4`,
  and zero invalid skips.
- `movement_elevator_route` remains a diagnostic movement row for this slice:
  it records wait/board samples, but does not yet hard-gate leave because the
  current map route does not produce a stable terminal lifecycle sample.

## Validation

Focused mover lifecycle run:

```text
python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario coop_door_elevator,coop_live_loop,movement_elevator_route --timeout 120 --artifact-dir .tmp\bot_scenarios\mover_lifecycle_after_generic_leave --format text --json-out .tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json
```

Result: 3 passed, 0 failed.

Key focused metrics:

- `coop_door_elevator`: `interaction_mover_ride_checks=217`,
  `interaction_mover_ride_wait_states=104`,
  `interaction_mover_ride_board_states=104`,
  `interaction_mover_ride_leave_states=9`,
  `interaction_mover_ride_invalid_skips=0`,
  `last_interaction_mover_ride_phase=4`,
  `last_interaction_mover_ride_entity=18`, and
  `last_interaction_mover_ride_kind=3`.
- `coop_live_loop`: `interaction_mover_ride_checks=217`,
  `interaction_mover_ride_wait_states=104`,
  `interaction_mover_ride_board_states=104`,
  `interaction_mover_ride_leave_states=9`,
  `interaction_mover_ride_invalid_skips=0`,
  `last_interaction_mover_ride_phase=4`,
  `last_interaction_mover_ride_entity=18`, and
  `last_interaction_mover_ride_kind=3`.
- `movement_elevator_route`: `interaction_mover_ride_checks=16`,
  `interaction_mover_ride_wait_states=8`,
  `interaction_mover_ride_board_states=8`,
  `interaction_mover_ride_leave_states=0`,
  `interaction_mover_ride_invalid_skips=0`,
  `last_interaction_mover_ride_phase=2`,
  `last_interaction_mover_ride_entity=18`, and
  `last_interaction_mover_ride_kind=3`.

Full implemented suite:

```text
python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario implemented --timeout 120 --artifact-dir .tmp\bot_scenarios\implemented_after_generic_mover_lifecycle --format text --json-out .tmp\bot_scenarios\implemented_after_generic_mover_lifecycle.json
```

Result: 123 passed, 0 failed, 0 timeout, 0 error, 0 pending.

Release acceptance:

```text
python tools/bot_release/run_bot_acceptance.py --install-dir .install --base-game basew --scenario-report .tmp\bot_scenarios\implemented_after_generic_mover_lifecycle.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_generic_mover_lifecycle.json
```

Result: 15 checks passed, 0 failed, 0 warnings.

Build and staging:

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`

## Follow-Up

The next M4/M6 mover slice should find or stage a mover interaction that
produces physical moving and grounded-on-mover samples, then promote those
diagnostic counters into hard gates. The current generic coop proof is useful
because it validates lifecycle ownership, metadata, completion, and invalid
skip handling without depending on the parked train bridge proof.
