# Q3A BotLib Campaign Keyed Path Train Proof

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes the first keyed-path campaign proof into the bot scenario
catalog. The new `coop_campaign_keyed_path_train` row runs mode `91` on the
packaged `train` campaign map and proves that the runtime navigation context
can see key entities, `trigger_key` locks, key-path progression candidates, and
the required-key metadata needed to route toward a keyed progression segment.

The slice is intentionally narrower than a full "pick up key, carry key,
unlock transition" flow. It establishes the route-local key-lock detection and
status contract needed before the next campaign transition pass.

## Implementation

- `src/game/sgame/bots/bot_nav.cpp` now classifies key items from either
  `key_*` classnames or `IF_KEY` item flags, classifies `trigger_key` as a key
  lock, and extracts the required item id from keyed lock entities.
- Interaction context now counts key entities, key items, key locks, and
  key-path entities, while progression scoring can promote a nearby key lock as
  a keyed progression candidate.
- Position-goal routing preserves the caller's requested position as the
  candidate origin instead of replacing it with the resolved AAS route origin.
  This lets route-local interaction scans compare nearby entity bounds against
  the intended campaign objective.
- `src/game/sgame/bots/bot_brain.cpp` adds a raw mode `91` train-specific
  keyed-path preparation hook and route request. This uses the raw smoke mode
  because `Bot_CommandSmokeScenarioMode()` resolves mode `91` to the behavior
  umbrella path when `bot_behavior_enable` is active.
- `q3a_bot_nav_policy_status` now exposes stable keyed-path counters and last
  selection metadata, including `last_nav_interaction_progression_key_path_key_lock`
  and `last_nav_interaction_progression_key_path_required_item`.
- `tools/bot_scenarios/run_bot_scenarios.py` adds
  `coop_campaign_keyed_path_train`, and `tools/bot_release/run_bot_acceptance.py`
  requires it as release scenario evidence.

## Validation

- `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_campaign_keyed_path_train --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\coop_campaign_keyed_path_train.json --markdown-out .tmp\bot_scenarios\coop_campaign_keyed_path_train.md --artifact-dir .tmp\bot_scenarios\coop_campaign_keyed_path_train_artifacts --timeout 90`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_keyed_path.json`

The focused scenario passed with:

- `nav_interaction_progression_key_path_candidates=1`
- `nav_interaction_progression_key_path_selections=1`
- `nav_interaction_progression_key_path_completions=1`
- `last_nav_interaction_progression_key_path_key_lock=1`
- `last_nav_interaction_progression_key_path_required_item=70`
- `interaction_world_key_entities=2`
- `interaction_world_key_items=2`
- `interaction_world_key_locks=1`
- `interaction_world_key_path_entities=2`

The release dry run passed `15/15` checks and now requires `17` scenario
evidence rows. The scenario catalog reports `123` implemented rows and `0`
pending rows.

## Notes

No upstream Q3A, Quake3e, baseq3a, BSPC, Gladiator, or `q2proto/` source files
were imported or modified for this slice.

The next campaign progression target should move from this keyed-path proof to
a true key pickup-to-lock carry flow or a map-transition flow, preferably
without the train proof warp once the broader campaign behavior can choose and
commit to that sequence on its own.
