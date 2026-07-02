# Q3A BotLib Campaign Key Carry Train Bridge Arrival

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round advances the `train` key-carry proof from a direct lock-side warp
to an explicit post-mover arrival projection. After the proof bot picks up the
red key and records the key-side `func_train` bridge interaction, bot nav now
derives a routeable arrival goal on the lock side from the source train entity
and final lock destination.

The focused scenario now requires `key_carry_lock_warps=0`. At this stage the
remaining proof aids were the temporary bridge-start warp and the temporary
post-mover arrival warp. Later bridge-approach work removes the bridge-start
warp; the post-mover arrival warp remains tracked until real off-mesh mover
graph, ride-state, and arrival-resume support land.

## Implementation

- `src/game/sgame/bots/bot_nav.hpp` extends interaction-goal telemetry with
  destination-distance fields, arrival-goal counters, and last arrival
  entity/kind/action/area/position status.
- `src/game/sgame/bots/bot_nav.cpp` adds `BotNav_FindInteractionArrivalGoal`,
  which samples routeable points between a live interaction entity and the
  requested destination, scores them by destination distance and bot distance,
  and records the selected post-mover arrival goal.
- `src/game/sgame/bots/bot_brain.cpp` uses the bridge-arrival goal after the
  observed `func_train` interaction. It then activates a one-shot nearby
  `trigger_key` interaction with normal walk travel so the keyed-path
  progression selection and required-key telemetry remain tied to the actual
  lock instead of a coordinate-only goal.
- `tools/bot_scenarios/run_bot_scenarios.py` hardens
  `coop_campaign_key_carry_train` so the row requires arrival-goal resolution,
  a nonzero arrival-to-lock distance, bridge-arrival proof accounting, and
  `key_carry_lock_warps=0`.
- `tools/bot_scenarios/test_run_bot_scenarios.py` updates the harness fixture
  and catalog assertions for the stricter bridge-arrival contract.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py tools/bot_release/run_bot_acceptance.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_coop_campaign_key_carry_train_catalog_and_marker_checks tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_policy_trace_and_readiness_promotions_use_expected_smoke_rows`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `git diff --check -- src/game/sgame/bots/bot_brain.cpp src/game/sgame/bots/bot_nav.hpp src/game/sgame/bots/bot_nav.cpp tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_campaign_key_carry_train --timeout 90 --artifact-dir .tmp/bot_scenarios --json-out .tmp/bot_scenarios/coop_campaign_key_carry_train.json`

The focused scenario passed from `.tmp\bot_scenarios\20260701T140932Z` with:

- `pass=1`
- `route_commands=61`
- `route_failures=0`
- `interaction_goal_requests=1`
- `interaction_goal_candidates=3`
- `interaction_goal_resolved=1`
- `last_interaction_goal_entity=60`
- `last_interaction_goal_kind=4`
- `last_interaction_goal_area=2338`
- `interaction_arrival_goal_requests=1`
- `interaction_arrival_goal_candidates=60`
- `interaction_arrival_goal_resolved=1`
- `last_interaction_arrival_goal_entity=60`
- `last_interaction_arrival_goal_kind=4`
- `last_interaction_arrival_goal_area=1058`
- `last_interaction_arrival_goal_x=-1547`
- `last_interaction_arrival_goal_y=821`
- `last_interaction_arrival_goal_z=200`
- `last_interaction_arrival_goal_destination_distance_sq=18411`
- `key_carry_bridge_arrival_requests=1`
- `key_carry_bridge_arrival_resolved=1`
- superseded bridge-arrival warp counter present in this earlier slice
- `key_carry_lock_warps=0`
- `nav_interaction_progression_key_path_selections=1`
- `last_nav_interaction_progression_key_path_key_lock=1`
- `last_nav_interaction_progression_key_path_required_item=70`

## Follow-Up

This slice is superseded by
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-approach-2026-07-01.md`
for bridge-start movement and by
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-route-2026-07-01.md`
for bridge-arrival routing. The broader follow-up is to generalize this focused
train proof into reusable off-mesh mover graph edges and ride/arrival state
support.

No upstream Q3A, Quake3e, baseq3a, BSPC, Gladiator, or `q2proto/` source files
were imported or modified for this slice.
