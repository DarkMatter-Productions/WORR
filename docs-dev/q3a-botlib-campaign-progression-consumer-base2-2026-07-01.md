# Q3A BotLib Campaign Progression Consumer Base2

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes the previous base2 campaign progression-chain diagnostics
into a consumed route-interaction preference. The bot nav layer now scores
nearby route interactions for progression relevance, prefers scored
progression candidates within a small distance slack, records the selected
candidate's target-chain traits, and exposes those counters through the
scenario harness.

## Implementation

- Added progression scoring for route-interaction candidates in
  `src/game/sgame/bots/bot_nav.cpp`. The score recognizes progression target
  classes, key entities, outbound target links, named target anchors, and
  generic `target_` classes.
- Kept the chooser conservative: pure nearest-distance selection still wins
  unless a higher progression score is within the configured slack window.
- Added `BotNavRouteStatus` fields for scored candidates, scored selections,
  preference selections, selected score, selected preference flag, and the
  selected entity's target/progression/key bits.
- Extended `q3a_bot_nav_policy_status` with the new counters.
- Added `coop_campaign_progression_consumer_base2`, a mode `91` `base2`
  scenario that inherits the progression-chain row and now requires
  `nav_interaction_progression_preference_selections >= 1`, selected
  progression score evidence, and a selected outbound target link.
- Promoted the new row into release acceptance required scenario evidence.

## Validation

Commands run:

```powershell
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win sgame_x86_64 copy_sgame_dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_campaign_progression_consumer_base2 --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json --markdown-out .tmp\bot_scenarios\coop_campaign_progression_consumer_base2.md --artifact-dir .tmp\bot_scenarios\coop_campaign_progression_consumer_base2_artifacts --timeout 90
python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_progression_consumer.json
python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance
```

Focused scenario result: `1/1` passed.

Release acceptance result:
`.tmp\bot_release\bot_release_acceptance_progression_consumer.json` reports
`15/15` checks passed, `required_scenarios=14`, and
`supplemental_reports=5`.

Unit tests: `79` tests passed.

Catalog summary:
`{'total': 120, 'implemented': 120, 'pending': 0, 'manual_only': 1, 'degradation_policies': 2}`.

Key live metrics from `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`:

- `frames=121`, `commands=121`, `route_commands=61`, `route_failures=0`
- `nav_interaction_progression_candidates=14`
- `nav_interaction_progression_selections=3`
- `nav_interaction_progression_preference_selections=2`
- `last_nav_interaction_progression_score=6`
- `last_nav_interaction_progression_preferred=0`
- `last_nav_interaction_target_link=1`
- `last_nav_interaction_named_target=1`

## Follow-Up

The base2 campaign proof now shows both target-chain visibility and
target-chain consumption. The next M4 slice should widen this into a longer
multi-window or multi-map campaign progression run so bots prove stateful
post-interaction progress, not only one route-interaction preference window.
