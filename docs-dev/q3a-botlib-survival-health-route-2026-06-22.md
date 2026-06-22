# Q3A BotLib Survival Health Route Proof

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Purpose

This round extends the M2 combat/inventory depth work from carried survival
inventory use into natural emergency item routing. The new proof validates that
a low-health bot can choose a health pickup through normal item utility and
AAS-backed route-goal assignment without relying on `item_focus=health`.

## Implementation

- Added reserved frame-command smoke mode `69` as `survival_health_route`.
- Added `sg_bot_frame_command_smoke_survival_route` server/game wiring, begin
  marker reporting, one-bot FFA targeting, runtime cvar reset, and FFA
  gametype selection.
- Added a survival-route smoke target path in `bot_brain.cpp` that stages a
  dropped medium health item near the bot route space, forces a low-health and
  no-armor state, and resets the bot nav slot after the target is prepared.
- Reused the dropped-item smoke target helper that now serves both ammo
  pressure and survival-route staged pickups.
- Expanded action status proof output with `item_health_candidates` and
  `item_health_seek_decisions`, so health routing has the same compact proof
  surface as the ammo-pressure row.
- Added the `survival_health_route` catalog row, reserved-mode mapping, raw
  fixture coverage, catalog assertions, and synthetic marker validation.

## Scenario Contract

`survival_health_route` runs with:

- `sv_bot_frame_command_smoke 69`
- `deathmatch 1`
- `g_gametype 1`
- `target=1`
- `item_focus=0`
- `survival_route=1`

The scenario requires:

- `pass=1`
- `commands>=1`
- `route_failures=0`
- `item_goal_assignments>=1`
- `last_item_goal_area>0`
- `item_low_health_boosts>=1`
- `item_health_candidates>=1`
- `item_health_seek_decisions>=1`
- `item_health_goal_assignments>=1`
- `item_last_utility_kind_name=health`

## Validation

Passed locally:

- `python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py`
- `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py -q`
- `python tools/bot_scenarios/run_bot_scenarios.py --catalog --json-out .tmp\bot_scenarios\catalog_survival_health_route.json`
- `ninja -C builddir-win sgame_x86_64.dll`
- `ninja -C builddir-win worr_ded_x86_64.exe`
- `ninja -C builddir-win worr_ded_engine_x86_64.dll`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools/bot_scenarios/run_bot_scenarios.py --scenario survival_health_route --artifact-dir .tmp\bot_scenarios\survival_health_route --format both --json-out .tmp\bot_scenarios\survival_health_route\latest.json`

Focused validation passed from
`.tmp\bot_scenarios\survival_health_route\20260622T164109Z` with
`pass=1`, `commands=60`, `route_commands=60`, `route_failures=0`,
`item_goal_assignments=3`, `last_item_goal_area=224`,
`item_low_health_boosts>=1`, `item_health_candidates>=1`,
`item_health_seek_decisions>=1`, `item_health_goal_assignments>=1`, and
`item_last_utility_kind_name=health`.

The default catalog now reports `74` automated implemented rows and `0`
pending rows; including the manual high-bot degradation row, the implemented
catalog total is `75`.
