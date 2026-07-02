# Q3A BotLib Campaign Key Carry Train Bridge Approach

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round removes the `train` key-carry proof's bridge-start warp. After the
proof bot picks up the red key, it now routes naturally to the live
`func_train` bridge-start route endpoint before the bridge interaction is
activated.

The remaining proof aid is the post-mover arrival warp. That is still explicit
in the scenario because the route graph does not yet carry a full off-mesh
mover ride/arrival state from train boarding to train exit.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` adds bridge-approach telemetry for
  route requests, approach attempts, approach readiness, and the last approach
  distance.
- The key-carry bridge phase now treats a matched one-point route endpoint as
  bridge-start arrival. This avoids depending on raw entity-origin distance
  when AAS resolves a nearby routeable endpoint for the train bridge.
- Once the approach is proven, the proof bot clears only its own stale nav slot
  and installs a one-shot train interaction request. This prevents the generic
  coop door/elevator retry owner from keeping a door interaction active while
  the key-carry proof needs the specific `func_train` entity.
- `tools/bot_scenarios/run_bot_scenarios.py` now requires
  `key_carry_bridge_approach_ready=1` and `key_carry_bridge_warps=0` for
  `coop_campaign_key_carry_train`.
- `tools/bot_scenarios/test_run_bot_scenarios.py` updates the synthetic marker
  fixture and catalog assertions for the stricter natural bridge-start
  approach contract.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py tools/bot_release/run_bot_acceptance.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_coop_campaign_key_carry_train_catalog_and_marker_checks tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_policy_trace_and_readiness_promotions_use_expected_smoke_rows`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python -m unittest tools.bot_release.test_run_bot_acceptance`
- `git diff --check -- src/game/sgame/bots/bot_brain.cpp tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_campaign_key_carry_train --timeout 90 --artifact-dir .tmp/bot_scenarios --json-out .tmp/bot_scenarios/coop_campaign_key_carry_train.json`
- `python tools/bot_release/run_bot_acceptance.py --scenario-report .tmp/bot_scenarios/implemented_hazard_context.json --allow-missing-scenario-report`

The focused scenario passed from `.tmp\bot_scenarios\20260701T142637Z` with:

- `pass=1`
- `frames=121`
- `route_commands=61`
- `route_failures=0`
- `interaction_goal_requests=16`
- `interaction_goal_candidates=48`
- `interaction_goal_resolved=16`
- `last_interaction_goal_entity=60`
- `last_interaction_goal_kind=4`
- `last_interaction_goal_area=2338`
- `interaction_arrival_goal_requests=1`
- `interaction_arrival_goal_candidates=60`
- `interaction_arrival_goal_resolved=1`
- `last_interaction_arrival_goal_entity=60`
- `last_interaction_arrival_goal_kind=4`
- `last_interaction_arrival_goal_area=1058`
- `last_interaction_arrival_goal_destination_distance_sq=18411`
- `key_carry_bridge_route_requests=16`
- `key_carry_bridge_approach_requests=16`
- `key_carry_bridge_approach_ready=1`
- `last_key_carry_bridge_approach_distance_sq=98`
- `key_carry_bridge_warps=0`
- `key_carry_bridge_interactions=1`
- `key_carry_bridge_interaction_commands=4`
- `key_carry_bridge_arrival_requests=1`
- `key_carry_bridge_arrival_resolved=1`
- superseded bridge-arrival warp counter present in this earlier slice
- `key_carry_lock_route_requests=44`
- `key_carry_lock_warps=0`
- `last_key_carry_bridge_entity=60`
- `last_key_carry_bridge_kind=4`
- `last_key_carry_bridge_travel_type=11`
- `last_nav_interaction_progression_key_path_entity=444`
- `last_nav_interaction_progression_key_path_key_lock=1`
- `last_nav_interaction_progression_key_path_required_item=70`

Release acceptance passed 15/15 checks after the rebuilt `.install` refresh,
with scenario evidence still reporting 114/114 passing historical rows and 18
required scenario evidence rows satisfied through supplemental reports.

## Follow-Up

This slice is superseded by
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-route-2026-07-01.md`,
which replaces the post-mover bridge-arrival proof warp with an arrival route
and reach latch. Broader reusable off-mesh mover graph and ride/arrival state
support remains the next generalization target.

No upstream Q3A, Quake3e, baseq3a, BSPC, Gladiator, or `q2proto/` source files
were imported or modified for this slice.
