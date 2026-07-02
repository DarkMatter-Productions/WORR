# Q3A BotLib Campaign Key Carry Train Interaction Goal

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round removes the hard dependency on a fixed bridge-start coordinate from
the `train` key-carry bridge proof. The `coop_campaign_key_carry_train` row now
asks bot navigation to find a live map interaction entity of the required kind,
resolves that entity to a routeable AAS point, and uses that point as the
bridge-start goal before invoking the existing `func_train` interaction proof.

At this point in the same-day sequence, the explicit bridge-start and
lock-side proof warps still existed. The later bridge-arrival follow-up in
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-2026-07-01.md`
removed the direct lock-side warp from the focused row and replaced it with a
routeable post-mover arrival projection. A later bridge-approach follow-up in
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-approach-2026-07-01.md`
removed the bridge-start proof warp. The remaining M4/M6 work is the larger
off-mesh mover graph and ride/arrival state model that can remove the
post-mover arrival proof warp entirely.

## Implementation

- `src/game/sgame/bots/bot_nav.hpp` adds `BotNavInteractionGoal` and
  interaction-goal status fields for request, candidate, resolution, skip, and
  last-selected entity/area/position metadata.
- `src/game/sgame/bots/bot_nav.cpp` adds the public
  `BotNav_FindInteractionGoal` API, backed by a private entity scan that
  filters live interaction entities by kind/action, resolves their best route
  point, and selects the nearest valid candidate to the requesting bot.
- `src/game/sgame/bots/bot_brain.cpp` uses that API for the train bridge phase
  before falling back to the historical bridge-start coordinate. The existing
  interaction activation path still records the real `func_train` wait/use
  telemetry and bridge interaction result.
- `q3a_bot_frame_command_status` now prints interaction-goal telemetry so
  scenario rows can distinguish a data-driven bridge goal from a coordinate-only
  proof.
- `tools/bot_scenarios/run_bot_scenarios.py` requires the train key-carry row
  to report at least one interaction-goal request, one resolved goal, train kind
  `4`, a positive entity number, and a positive AAS area.
- `tools/bot_scenarios/test_run_bot_scenarios.py` covers the new catalog and
  marker requirements.

## Validation

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py tools/bot_release/run_bot_acceptance.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_coop_campaign_key_carry_train_catalog_and_marker_checks tools.bot_scenarios.test_run_bot_scenarios.BotScenarioHarnessTests.test_policy_trace_and_readiness_promotions_use_expected_smoke_rows`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario coop_campaign_key_carry_train --timeout 90 --artifact-dir .tmp/bot_scenarios --json-out .tmp/bot_scenarios/coop_campaign_key_carry_train.json`
- `python tools/bot_release/run_bot_acceptance.py --scenario-report .tmp/bot_scenarios/implemented_hazard_context.json --allow-missing-scenario-report`

The focused scenario passed from `.tmp\bot_scenarios\20260701T133314Z` with:

- `pass=1`
- `route_commands=61`
- `route_failures=0`
- `interaction_goal_requests=1`
- `interaction_goal_candidates=3`
- `interaction_goal_resolved=1`
- `interaction_goal_invalid_skips=0`
- `last_interaction_goal_entity=60`
- `last_interaction_goal_kind=4`
- `last_interaction_goal_action=3`
- `last_interaction_goal_area=2338`
- `last_interaction_goal_x=-1024`
- `last_interaction_goal_y=-711`
- `last_interaction_goal_z=-61`
- `last_interaction_goal_distance_sq=86882`
- `key_carry_bridge_interactions=1`
- `last_key_carry_bridge_kind=4`
- `last_key_carry_bridge_travel_type=11`
- `last_nav_interaction_client=0`

Release acceptance passed `15/15` checks against
`.tmp\bot_scenarios\implemented_hazard_context.json` plus supplemental focused
reports, with `114` accepted scenario rows and `18` required scenario evidence
rows.

## Follow-Up

This slice is superseded by the bridge-arrival and bridge-approach follow-ups.
The remaining campaign movement slice should replace the post-mover arrival
proof warp with actual off-mesh mover graph support: discover train mover
entry/exit points, commit to ride state while the mover is active, and resume
the post-ride route from the arrival side.

No upstream Q3A, Quake3e, baseq3a, BSPC, Gladiator, or `q2proto/` source files
were imported or modified for this slice.
