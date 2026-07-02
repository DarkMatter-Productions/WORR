# Q3A BotLib Campaign Key Carry Train Bridge Proof

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round closes the missing middle leg in the `train` key-carry proof. The
`coop_campaign_key_carry_train` row now proves the bot can pick up the red key,
route to the key-side train bridge, activate the real `func_train` interaction,
and only then continue into the `trigger_key` lock proof.

The bridge-start and lock-side warps remain explicit proof scaffolding. Current
static AAS data does not connect the red-key side to the bridge mover start or
the final lock island well enough for a true end-to-end route. The scenario now
records that limitation instead of hiding it: `key_carry_bridge_warps=1` marks
the temporary bridge-start staging, while the real train interaction telemetry
must still pass before the lock leg is accepted.

## Implementation

- `src/game/sgame/bots/bot_nav.cpp` treats Q3A elevator-style travel as
  compatible with platforms, trains, and generic movers instead of only one
  interaction family. This lets generated mover/elevator reachability select a
  `func_train` candidate.
- `BotNav_ActivateInteractionNearPosition` adds a narrow bridge for proof rows
  that need to activate a nearby interaction of a specific kind while preserving
  the normal nav retry, wait/use command, and route-status accounting.
- `src/game/sgame/bots/bot_brain.cpp` adds key-carry bridge state, counters,
  and final status markers. The proof records the bridge entity, interaction
  kind, action, travel type, bridge route request count, command frames, and
  explicit bridge-start warp count.
- `q3a_bot_nav_policy_status` now exposes `last_nav_interaction_client`, and
  the key-carry bridge recorder requires that client to match the proof bot
  before accepting the bridge interaction.
- The train key-carry route request now commits to three phases: red-key pickup,
  train bridge interaction, then lock-side key path. The final lock-side warp is
  still explicit until an off-mesh mover graph or ride/arrival model connects
  the AAS islands.
- `tools/bot_scenarios/run_bot_scenarios.py` now gates
  `coop_campaign_key_carry_train` on bridge evidence, including
  `last_key_carry_bridge_kind=4` for the real `func_train` and
  `last_key_carry_bridge_travel_type=11` for elevator/mover travel.
- `tools/bot_scenarios/test_run_bot_scenarios.py` covers the new catalog tags
  and required marker metrics.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py tools/bot_release/run_bot_acceptance.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_coop_campaign_key_carry_train_catalog_and_marker_checks tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_policy_trace_and_readiness_promotions_use_expected_smoke_rows`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python -m unittest tools.bot_release.test_run_bot_acceptance`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_campaign_key_carry_train --timeout 90 --artifact-dir .tmp/bot_scenarios --json-out .tmp/bot_scenarios/coop_campaign_key_carry_train.json`
- `python tools/bot_release/run_bot_acceptance.py --scenario-report .tmp/bot_scenarios/implemented_hazard_context.json --allow-missing-scenario-report`

The focused scenario passed from `.tmp\bot_scenarios\20260701T130947Z` with:

- `pass=1`
- `route_commands=61`
- `route_failures=0`
- `key_carry_key_pickups=1`
- `key_carry_bridge_route_requests=1`
- `key_carry_bridge_warps=1`
- `key_carry_bridge_interactions=1`
- `key_carry_bridge_interaction_commands=4`
- `last_key_carry_bridge_entity=31`
- `last_key_carry_bridge_kind=4`
- `last_key_carry_bridge_action=3`
- `last_key_carry_bridge_travel_type=11`
- `last_nav_interaction_client=0`
- `key_carry_lock_route_requests=59`
- `key_carry_lock_warps=1`
- `last_key_carry_pickup_inventory=1`
- `last_key_carry_lock_required_item=70`
- `last_key_carry_phase=3`

Release acceptance passed `15/15` checks with `114` accepted scenario rows and
`18` required scenario evidence rows.

## Follow-Up

The next campaign movement slice should remove the bridge-start and lock-side
proof warps by teaching the route graph about train mover starts, ride state,
and arrival-side continuation. Until then, the scenario keeps those warps
observable and requires a real train interaction before the lock proof can pass.

No upstream Q3A, Quake3e, baseq3a, BSPC, Gladiator, or `q2proto/` source files
were imported or modified for this slice.
