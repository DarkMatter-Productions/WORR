# Q3A BotLib Target Memory Decay

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This slice starts M2 combat depth by making the bot blackboard remember a live
enemy briefly after visibility is lost, then decay that target cleanly. The goal
is to reduce target flicker and give later weapon, aim, and survival policies a
stable combat target instead of forcing every combat proof to depend on a
perfectly visible scripted enemy.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or q2proto source was imported or
modified. The work is WORR-native behavior glue, status telemetry, server smoke
setup, and scenario harness coverage.

## Implementation

- `src/game/sgame/bots/bot_brain.*` now records whether the current blackboard
  enemy is retained from memory, the current enemy memory age, and the active
  memory window.
- `q3a_bot_blackboard_status` now reports target-memory retain counts, smoke
  occlusion counts, decay counts, seed diagnostics, final decay entity/client
  ids, and the latest memory age/window values.
- The target-memory smoke path uses a short `1000` ms memory window and a
  deterministic smoke occlusion after the target has been visible long enough to
  seed real enemy facts.
- `src/server/main.c` reserves frame-command smoke mode `64` for
  `target_memory_decay`, stages two FFA bots, and enables only the target-memory
  smoke cvar for that proof.
- `tools/bot_scenarios/run_bot_scenarios.py` adds the `target_memory_decay`
  scenario, marker gates, mode reservation, and `target_memory_counters`
  optional field family.
- `tools/bot_scenarios/test_run_bot_scenarios.py` covers the catalog row,
  command construction, reserved mode mapping, marker evaluation, and synthetic
  status parsing.

## Scenario Contract

`target_memory_decay` is a short automated scenario:

- Smoke mode: `64`
- Game setup: `deathmatch 1`, `g_gametype 1`
- Required proof: enemy acquisition, retained unseen target memory, target
  memory smoke occlusion, memory decay, blackboard clear after decay, positive
  memory age, and `last_combat_enemy_memory_window_ms=1000`
- Focused artifact: `.tmp\bot_scenarios\20260622T120742Z`

The passing artifact reported `combat_enemy_memory_retains=160`,
`combat_enemy_memory_smoke_occlusions=161`, `combat_enemy_memory_decays=1`,
`last_combat_enemy_memory_age_ms=1025`,
`last_combat_enemy_memory_window_ms=1000`,
`last_combat_enemy_memory_decay_entity=2`, and
`last_combat_enemy_memory_decay_client=1`.

## Validation

Validated locally with:

```powershell
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64
python tools\refresh_install.py --build-dir builddir-win
python tools\bot_scenarios\run_bot_scenarios.py --scenario target_memory_decay --install-dir .install --binary .install\worr_ded_x86_64.exe --format both
git diff --check
```

## Completion Impact

- Scenario catalog: `70` implemented rows total.
- Automated short-run rows: `69`.
- Manual-only high-bot degradation row: `1`.
- Default pending rows: `0`.
- Highest frame-command smoke mode: `64`.
- M2 status: in progress, with sustained enemy target memory and decay complete.

## Next Work

The next M2 slices should build on this stable target contract:

- Weapon scoring by range, ammo, splash risk, and enemy health/armor estimate.
- Aim/fire tuning around reaction time, projectile lead, splash caution, and
  line-of-fire checks.
- Survival and ammo pressure so low-resource bots make better fight-or-retreat
  decisions.
