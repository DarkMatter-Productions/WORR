# Q3A BotLib Second Campaign Interaction Row

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

Added a second packaged campaign-map row for the coop live-loop interaction
matrix. The existing mode `91` proof still covers `base1`; the new
`coop_campaign_interaction_matrix_base2` row runs the same behavior contract on
`base2`.

This is a harness and release-gate promotion of existing runtime behavior, not a
new imported behavior path. It makes the campaign interaction evidence less
map-specialized by requiring the same route-interaction retry, campaign mover
source ownership, teammate hold command, coop readiness, and nav interaction
context counters on another staged Q2 AAS map.

## Implementation

- Factored the mode `91` coop campaign marker contract in
  `tools/bot_scenarios/run_bot_scenarios.py` so both `base1` and `base2` rows
  share the same required marker set.
- Added `coop_campaign_interaction_matrix_base2` with `deathmatch 0`, `coop 1`,
  `bot_coop_live_loop 1`, `map_name="base2"`, and the same campaign/coop/movement
  selection tags as the original `base1` row.
- Promoted the new row into the release acceptance required scenario set, relying
  on the existing supplemental focused-report discovery for post-aggregate rows.
- Updated scenario and release README coverage so operators can see that the
  campaign proof now includes both packaged base campaign maps.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_campaign_interaction_matrix_base2 --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json --markdown-out .tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.md --artifact-dir .tmp\bot_scenarios\coop_campaign_interaction_matrix_base2_artifacts --timeout 90`

Focused live result: pass. The `base2` run recorded `frames=121`,
`route_commands=61`, `route_failures=0`, `coop_interaction_retry_commands=3`,
`coop_door_elevator_source_commands=3`,
`coop_door_elevator_hold_commands=60`, and
`last_coop_door_elevator_entity=331`.

## Follow-Up

The next M4/M6 work should move beyond row duplication into deeper campaign
play-depth: trigger/key progression, longer coop loops, and live evidence that
the same command owners continue to work after multiple route/interaction
transitions.
