# Q3A BotLib Campaign Interaction Depth On Base2

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

Added `coop_campaign_interaction_depth_base2`, a stricter mode `91` scenario on
`base2` that turns the second campaign interaction row into a deeper
progression-oriented gate. The row keeps the same coop live-loop setup as the
campaign matrix, but now explicitly requires button, trigger, mover, use/touch,
wait/use command-frame, coop intent, and live pickup timing evidence.

This closes the first part of the M4/M6 trigger/objective support gap without
claiming full key or scripted campaign completion. It proves the runtime context
is seeing the important entity families and that the existing coop owners keep
working while the map exposes richer progression geometry.

## Implementation

- Added shared depth marker helpers in `tools/bot_scenarios/run_bot_scenarios.py`.
- Added the implemented `coop_campaign_interaction_depth_base2` scenario with
  `deathmatch 0`, `coop 1`, `bot_coop_live_loop 1`, `map_name="base2"`, and
  depth/progression selection tags.
- Promoted the row into release acceptance required scenario evidence.
- Updated scenario/release docs and roadmap/provenance ledgers.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_campaign_interaction_depth_base2 --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json --markdown-out .tmp\bot_scenarios\coop_campaign_interaction_depth_base2.md --artifact-dir .tmp\bot_scenarios\coop_campaign_interaction_depth_base2_artifacts --timeout 90`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_campaign_depth.json`

Focused live result: pass. The `base2` run recorded 54 interaction entities,
19 doors, 5 buttons, 22 triggers, 7 movers, 49 use-capable entities, 49
touch-capable entities, 8 wait frames, 8 use frames, 0 interaction misses,
3 coop interaction retry commands, 3 door/elevator source commands, 60 hold
commands, follow/wait/regroup policy counts of 60 each, 1 lead decision,
120 resource-share evaluations, and 59 live pickup timing observations.

## Follow-Up

The next campaign slice should move from context/owner proof into a longer
progression flow: explicit key/objective state, chained interaction transitions,
or a headless coop play-depth pass that records a small campaign segment rather
than a single interaction window.
