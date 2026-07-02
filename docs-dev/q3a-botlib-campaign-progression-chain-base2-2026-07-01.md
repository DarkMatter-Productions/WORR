# Q3A BotLib Campaign Progression Chain Base2

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round closes the immediate M4/M6 roadmap gap after the base2 campaign
interaction-depth row: the runtime now reports target-chain and progression
context beside the existing nav interaction census, and the scenario catalog
has a promoted `coop_campaign_progression_chain_base2` row that release
acceptance requires.

## Implementation

- Extended `BotNavRouteStatus` with progression diagnostics:
  `interaction_world_target_entities`,
  `interaction_world_progression_targets`,
  `interaction_world_target_links`,
  `interaction_world_named_targets`,
  `interaction_world_key_entities`, and
  `interaction_world_progression_entities`.
- Kept `interaction_world_entities` semantics unchanged. It still counts
  nav-recognized interactables such as doors, buttons, triggers, movers, and
  hazards; the new fields describe map scripting/progression wiring around
  those interactables.
- Added `coop_campaign_progression_chain_base2`, a mode `91` `base2` scenario
  that inherits the campaign-depth wait/use, coop intent, and live pickup
  timing gates, then additionally requires target-chain and progression fields.
- Added the new scenario to release acceptance required evidence and updated
  the bot roadmap, Q3A/AAS port plan, strategic roadmap, scenario README,
  release README, and credits ledger.

## Validation

Commands run:

```powershell
meson compile -C builddir-win sgame_x86_64 copy_sgame_dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_campaign_progression_chain_base2 --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\coop_campaign_progression_chain_base2.json --markdown-out .tmp\bot_scenarios\coop_campaign_progression_chain_base2.md --artifact-dir .tmp\bot_scenarios\coop_campaign_progression_chain_base2_artifacts --timeout 90
python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_progression_chain.json
git diff --check
```

Focused scenario result: `1/1` passed.

Release acceptance result:
`.tmp\bot_release\bot_release_acceptance_progression_chain.json` reports
`15/15` checks passed, `required_scenarios=13`, and `supplemental_reports=4`.

Unit tests: `78` tests passed.

`git diff --check` passed with only existing LF/CRLF working-copy warnings.

Key live metrics from `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`:

- `frames=121`, `commands=121`, `route_commands=61`, `route_failures=0`
- `interaction_world_entities=54`, `interaction_world_doors=19`,
  `interaction_world_buttons=5`, `interaction_world_triggers=22`,
  `interaction_world_movers=7`, `interaction_world_use_entities=49`,
  `interaction_world_touch_entities=49`
- `interaction_world_target_entities=110`,
  `interaction_world_progression_targets=10`,
  `interaction_world_target_links=84`,
  `interaction_world_named_targets=138`,
  `interaction_world_key_entities=0`,
  `interaction_world_progression_entities=259`
- `nav_interaction_wait_frames=8`, `nav_interaction_use_frames=8`,
  `nav_interaction_misses=0`, `last_nav_interaction_action=3`
- `coop_interaction_retry_commands=3`,
  `coop_door_elevator_source_commands=3`,
  `coop_door_elevator_hold_commands=60`
- `team_objective_coop_policy_follow=60`,
  `team_objective_coop_policy_wait=60`,
  `team_objective_coop_policy_regroup=60`,
  `team_objective_coop_policy_lead=1`,
  `team_objective_coop_policy_resource_share=120`
- `item_timing_consumer_live_pickups=59`,
  `item_timing_consumer_ready_or_live=59`

## Follow-Up

This row proves target-chain/progression context is visible to the bot runtime.
The next M4 slice should consume that context over a longer coop/campaign run,
for example by proving multi-window interaction progression or a second
campaign map family rather than only reporting the world census.
