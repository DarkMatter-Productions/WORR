# Q3A BotLib Ammo Pressure Pickup Proof

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This round promotes the next M2 combat/inventory slice by making ammo pressure
visible to item selection and route ownership. Dedicated scenario mode `67`,
`ammo_pressure_pickup`, now stages a one-bot FFA proof where the bot has a
shotgun, all non-shell ammo is stocked, shells are depleted, and a routeable
shell pickup is staged nearby.

The proof verifies that low carried ammo can steer item utility toward ammo,
that the item route owner can select that ammo goal, and that the scenario
harness can distinguish ammo pickups from weapon and generic item pressure.

No Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified for this round.

## Implementation

- `src/game/sgame/bots/bot_items.*` adds `BotItemFocus::Ammo`, parses and
  reports the `ammo` focus string, boosts ammo candidates under that focus, and
  tracks ammo/weapon goal assignment counters separately from health and armor.
- `src/game/sgame/bots/bot_nav.cpp` maps
  `sg_bot_frame_command_smoke_item_focus ammo` to ammo-only item utility so the
  smoke path proves the same route-goal machinery used by other item focus
  modes.
- `src/game/sgame/bots/bot_brain.cpp` adds mode `67` preparation. The setup
  forces shotgun ownership, depletes shells, stocks other ammo, places an active
  dropped `IT_AMMO_SHELLS` target near the bot, and resets nav once that target
  is available.
- Action proof status now exposes `item_ammo_candidates`,
  `item_ammo_seek_decisions`, `item_ammo_goal_assignments`,
  `item_weapon_goal_assignments`, `item_focus_ammo_boosts`,
  `item_last_item`, and `item_last_utility_kind_name` so the harness can
  hard-gate ammo-specific pressure instead of only generic item assignments.
- `src/server/main.c` reserves mode `67` as a one-bot FFA ammo-pressure smoke,
  sets `item_focus=ammo`, and annotates the begin marker with
  `ammo_pressure=1`.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `ammo_pressure_pickup` scenario,
  reserved-mode mapping, optional-field metadata, marker gates, catalog tests,
  and synthetic marker evaluation.

## Validation Evidence

Focused scenario validation passed from
`.tmp\bot_scenarios\ammo_pressure_pickup\20260622T132231Z`:

- `frames=60`
- `commands=60`
- `route_commands=60`
- `route_failures=0`
- `item_goal_assignments=10`
- `item_goal_peak_active_reservations=1`
- `item_focus_ammo_boosts=17`
- `item_ammo_goal_assignments=10`
- `item_last_utility_kind_name=ammo`
- `pass=1`

The first focused run exposed a proof-surface gap: the runtime was already
assigning ammo goals, but the action proof marker did not yet publish the ammo
candidate, seek-decision, last-item, or utility-kind fields required by the new
scenario. The second run passed after those status fields were added.

Commands run:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q
python tools\bot_scenarios\run_bot_scenarios.py --catalog --json-out .tmp\bot_scenarios\catalog_ammo_pressure_check.json
ninja -C builddir-win sgame_x86_64.dll
ninja -C builddir-win worr_ded_x86_64.exe
ninja -C builddir-win worr_ded_engine_x86_64.dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools\bot_scenarios\run_bot_scenarios.py --scenario ammo_pressure_pickup --artifact-dir .tmp\bot_scenarios\ammo_pressure_pickup --json-out .tmp\bot_scenarios\ammo_pressure_pickup_report.json --markdown-out .tmp\bot_scenarios\ammo_pressure_pickup_report.md --timeout 60
```

## Catalog Stats

- Implemented catalog rows: `73` total.
- Automated short-run rows: `72`.
- Manual-only rows: `1`.
- Default pending rows: `0`.
- Highest reserved bot frame-command smoke mode: `67`.
- Raw plan checklist rows: `809/809` checked.

The default `--catalog` view reports `72` implemented rows because it excludes
the manual-only high-bot degradation row unless explicitly requested.

## Next Steps

M2 has now proven target memory, weapon scoring, aim/fire policy, and ammo
pickup pressure. The next practical slices are live inventory use and survival
behavior: powerup/inventory activation, emergency health/armor retreat, and
combat regression rows that reuse these item-pressure counters away from one
staged layout.
