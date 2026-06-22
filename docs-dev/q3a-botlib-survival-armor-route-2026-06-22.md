# Q3A BotLib Survival Armor Route Proof

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Purpose

Broaden the M2 survival behavior coverage from health-only emergency routing
to the armor side of survival item pressure. The previous mode `69`
`survival_health_route` proved a low-health bot naturally selected routeable
health without an item-focus cvar. This round adds mode `70`
`survival_armor_route`, which proves a full-health/no-armor bot naturally
selects armor for survival pressure without item focus.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` now treats
  `sg_bot_frame_command_smoke_survival_route` as a small string selector:
  numeric/string truthy values keep the existing health route mode `69`, while
  `armor` selects reserved mode `70`.
- The survival route target setup now accepts an item id, so the same staged
  route target machinery can spawn either `IT_HEALTH_MEDIUM` for mode `69` or
  `IT_ARMOR_JACKET` for mode `70`.
- Mode `70` clears armor and power-armor state, keeps the bot at full health,
  stages a routeable jacket armor target, resets client navigation after target
  creation, and lets the normal item utility layer choose the pickup.
- Compact `q3a_bot_action_status` now includes `item_armor_candidates` and
  `item_armor_seek_decisions` beside the existing health/ammo compact fields,
  so automated scenario checks can assert the armor utility path without
  depending on verbose status output.
- `src/server/main.c` reserves frame-command smoke mode `70`, sets
  `sg_bot_frame_command_smoke_survival_route armor`, keeps the one-bot FFA
  setup, and emits `survival_route_kind=armor` in the begin marker.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` now include the implemented
  `survival_armor_route` catalog row, reserved-mode mapping, marker checks,
  synthetic log coverage, and catalog documentation.

## Scenario Contract

`survival_armor_route` must pass with:

- mode `70`, `deathmatch 1`, `g_gametype 1`, `survival_route=1`, and
  `survival_route_kind=armor`;
- at least one command, at least one item goal assignment, and zero route
  failures;
- a resolved AAS goal area for the armor pickup;
- `item_low_armor_boosts >= 1`;
- `item_armor_candidates >= 1`;
- `item_armor_seek_decisions >= 1`;
- `item_armor_goal_assignments >= 1`;
- `item_last_utility_kind_name=armor`.

## Validation

Focused validation passed from:

`.tmp\bot_scenarios\survival_armor_route\20260622T165918Z`

Key focused-smoke evidence:

- `pass=1`
- `frames=60`
- `commands=60`
- `route_commands=60`
- `route_failures=0`
- `item_goal_assignments=15`
- `last_item_goal_area=188`
- `item_goal_peak_active_reservations=1`

Supporting validation run this round:

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py -q`
- `python tools/bot_scenarios/run_bot_scenarios.py --catalog --json-out .tmp\bot_scenarios\catalog_survival_armor_route.json`
- `ninja -C builddir-win sgame_x86_64.dll`
- `ninja -C builddir-win worr_ded_x86_64.exe`
- `ninja -C builddir-win worr_ded_engine_x86_64.dll`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario survival_armor_route --artifact-dir .tmp\bot_scenarios\survival_armor_route --format both --json-out .tmp\bot_scenarios\survival_armor_route\latest.json`

## Completion Stats

The implemented catalog now reports `76` rows total: `75` automated short-run
rows plus one manual high-bot degradation row. The default `--catalog` view
reports `75` automated rows and `0` pending rows.
