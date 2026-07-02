# Q3A BotLib Campaign Progression Carry Base2

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes the next coop/campaign slice after the base2
post-interaction proof: a bot must carry progression-completion memory through
multiple scored campaign interactions in one live-loop run. The new
`coop_campaign_progression_carry_base2` scenario keeps the mode `91` base2
campaign contract and adds assertions that one bot completes a scored
interaction after a prior completion, including a distinct follow-up entity.

The focused run also exposed a dedicated-server crash when a bot received a
localized mission-objective notification. The crash was not in the new carry
telemetry; it was an existing bot/client boundary bug in the server game-import
print path. Bot clients can be spawned without a real q2proto network writer,
so single-client localized prints, centerprints, unicast buffers, and local
sounds must not serialize q2proto messages to bots.

## Implementation

- Added per-route-slot progression completion memory in
  `src/game/sgame/bots/bot_nav.cpp` so the nav layer can distinguish a first
  completed progression interaction from a later carried completion.
- Added aggregate route-status counters in `src/game/sgame/bots/bot_nav.hpp`
  and `src/game/sgame/bots/bot_brain.cpp`:
  `nav_interaction_progression_carry_completions`,
  `nav_interaction_progression_carry_distinct_completions`,
  `nav_interaction_progression_completed_clients`,
  `nav_interaction_progression_distinct_completed_clients`, and the
  `last_nav_interaction_progression_carry_*` fields.
- Added `coop_campaign_progression_carry_base2` to
  `tools/bot_scenarios/run_bot_scenarios.py`, parser/catalog tests, the
  scenario README, and release acceptance required scenarios.
- Guarded bot clients in `src/server/game.c` for `PF_Unicast`,
  `PF_Client_Print`, `PF_Center_Print`, and `PF_LocalSound`. This matches the
  existing `SV_ClientPrintf` behavior and prevents q2proto writes to fake bot
  clients.

## Crash Diagnosis

The failing focused run produced `.install\crashdump_134273764891460555.dmp`.
`cdb` resolved the stack to:

```text
sgame_x86_64!G_PlayerNotifyGoal
sgame_x86_64!local_game_import_t::LocClient_Print
worr_ded_engine_x86_64!PF_Loc_Print
worr_ded_engine_x86_64!PF_Client_Print
0x0
```

`PF_Client_Print` attempted to serialize a localized objective print to a bot
client. The q2proto writer callback was null for that fake client, producing an
execute access violation. After the guard, the same carry scenario completed
without producing a new crash dump.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win worr_ded_x86_64 worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario coop_campaign_progression_carry_base2 --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\coop_campaign_progression_carry_base2.json --markdown-out .tmp\bot_scenarios\coop_campaign_progression_carry_base2.md --artifact-dir .tmp\bot_scenarios\coop_campaign_progression_carry_base2_artifacts --timeout 90`
- `python tools\bot_release\run_bot_acceptance.py --install-dir .install --scenario-report .tmp\bot_scenarios\implemented_hazard_context.json --movement-reference-audit .tmp\bot_scenarios\movement_reference_gap_audit.json --format json --output .tmp\bot_release\bot_release_acceptance_progression_carry.json`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios tools.bot_release.test_run_bot_acceptance`
- `python -c "import sys,pathlib; sys.path.insert(0, str(pathlib.Path('tools/bot_scenarios').resolve())); import run_bot_scenarios as h; print(h.catalog_report(h.SCENARIOS)['summary'])"`

Focused carry metrics:

- `nav_interaction_progression_completions=3`
- `nav_interaction_progression_post_refreshes=3`
- `nav_interaction_progression_repeat_suppressions=2`
- `nav_interaction_progression_carry_completions=2`
- `nav_interaction_progression_carry_distinct_completions=2`
- `nav_interaction_progression_completed_clients=1`
- `nav_interaction_progression_distinct_completed_clients=1`
- `last_nav_interaction_progression_carry_previous_entity=42`
- `last_nav_interaction_progression_carry_entity=332`
- `last_nav_interaction_progression_carry_distinct=1`
- `last_nav_interaction_progression_carry_count=3`
- `last_nav_interaction_progression_carry_distinct_count=3`

Release acceptance passed 15/15 checks with `required_scenarios=16`. The
scenario catalog reports 122 implemented rows, 0 pending rows, 1 manual-only
row, and 2 degradation policies.
