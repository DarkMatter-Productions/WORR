# Q3A BotLib Physical Elevator Mover Activation

Date: 2026-07-01

Related tasks: `FR-04-T11`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`,
`DV-03-T05`, `DV-07-T06`

## Summary

This slice promotes the `movement_elevator_route` proof from route-only
elevator reachability into a physical mover activation and observation gate.
The previous generic mover lifecycle work proved Wait/Board/Leave bookkeeping
for coop mover interactions, but the dedicated elevator movement row could
still pass while the mover never reported a moving state.

The new behavior lets bot recovery commands directly activate mover-like
interactions through the same `use` callback a player would trigger, then
observes the mover after activation long enough to prove real movement.

## Implementation

- Added a direct-use recovery interaction path in
  `src/game/sgame/bots/bot_brain.cpp`. Recovery moves that already request
  `use` can now activate platform, train, and generic mover interactions when
  the interaction entity is valid and not already moving. Activations are
  cooldown-limited per bot/entity/spawn count so a stuck bot cannot spam the
  same mover every frame.
- Added `interaction_direct_use_*` telemetry to
  `q3a_bot_nav_policy_status`, including activation counts, invalid skips, and
  last client/action/kind/entity/move-state metadata.
- Added travel-type elevator observation state to frame-command proof slots.
  When a `TRAVEL_ELEVATOR` recovery interaction is observed, the bot records
  interaction, activation-request, observation-frame, moving, grounded,
  completion, timeout, invalid-skip, and last-elevator metadata.
- Fed observed physical elevator samples back into
  `BotNav_RecordMoverRideState(... Ride)` so the shared mover lifecycle status
  reports real moving-state evidence.
- Fixed raw mode `12` proof-slot allocation. The mode is a movement smoke, not
  a scenario mode, so the new elevator observer had no slot until
  `Bot_CommandSmokeProofSlotFor` explicitly accepted raw mode `12`.
- Extended raw mode `12` settle frames in `src/server/main.c` from the default
  movement-route settle to 24 frames so the post-activation mover observation
  can see a physical state transition.
- Tightened `tools/bot_scenarios/run_bot_scenarios.py` so
  `movement_elevator_route` now requires direct-use activation, elevator
  activation request, observation request, moving observation, completion, and
  shared mover moving-state telemetry.
- Updated `tools/bot_scenarios/test_run_bot_scenarios.py` so the catalog test
  locks the new `movement_elevator_route` marker contract.

## Scope Notes

The `movement_elevator_route` row now hard-gates physical moving samples.
Grounded-on-mover samples remain diagnostic because the current route point and
brush geometry do not reliably keep the proof bot parented to the elevator
platform during the short smoke window. That remains a follow-up for a map or
interaction that can provide a reliable platform-riding sample.

This work is WORR-native bot-brain, scenario, test, documentation, and release
staging work. No new upstream Q3A or BSPC source imports are claimed.

## Validation

- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py -k movement_context_rows_catalog_and_marker_checks`
  - Passed: 1 passed, 65 deselected.
- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  - Passed: 66 passed.
- `meson compile -C builddir-win`
  - Passed.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed `.install/`.
- `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario movement_elevator_route --timeout 120 --artifact-dir .tmp\bot_scenarios\movement_elevator_physical --format text --json-out .tmp\bot_scenarios\movement_elevator_physical.json`
  - Passed. Focused artifacts were written under
    `.tmp\bot_scenarios\movement_elevator_physical\20260701T184702Z`.
- `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario movement_elevator_route,coop_interaction_retry,coop_door_elevator,coop_live_loop,coop_campaign_key_carry_train --timeout 180 --artifact-dir .tmp\bot_scenarios\mover_direct_use_regression --format text --json-out .tmp\bot_scenarios\mover_direct_use_regression.json`
  - Passed 5/5. Regression artifacts were written under
    `.tmp\bot_scenarios\mover_direct_use_regression\20260701T184713Z`.
- `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario implemented --timeout 180 --artifact-dir .tmp\bot_scenarios\implemented_after_physical_elevator_mover --format text --json-out .tmp\bot_scenarios\implemented_after_physical_elevator_mover.json`
  - Passed: 123 passed, 0 failed, 0 timeout, 0 error, 0 pending. Aggregate
    artifacts were written under
    `.tmp\bot_scenarios\implemented_after_physical_elevator_mover\20260701T185401Z`.
- `python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario movement_elevator_route --timeout 120 --artifact-dir .tmp\bot_scenarios\movement_elevator_physical_final --format text --json-out .tmp\bot_scenarios\movement_elevator_physical_final.json`
  - Passed after the final rebuild and `.install/` refresh: 1 passed, 0
    failed, 0 timeout, 0 error, 0 pending. Final focused artifacts were
    written under
    `.tmp\bot_scenarios\movement_elevator_physical_final\20260701T190445Z`.
- `python tools/bot_release/run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_after_physical_elevator_mover.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_physical_elevator_mover_final.json`
  - Passed: 15 checks passed, 0 failed, 0 warnings.
