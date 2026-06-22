# Q3A BotLib Aim/Fire Policy Depth

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This round promotes the next M2 combat slice: staged aim/fire policy depth.
Dedicated scenario mode `66`, `aim_fire_policy_depth`, now proves the command
pipeline can withhold fire for reaction timing, aim-settle timing, and
burst-cooldown pacing, then transition into an applied rocket attack with live
projectile-leading telemetry.

No Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified for this round.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` adds the mode `66` smoke preparation and
  command path. The proof actor is forced onto the rocket launcher with stable
  ammo, a moving visible peer, and staged elapsed-time policy windows for
  reaction delay, aim settle, burst cooldown, and allowed fire.
- The live aim path now receives the active smoke slot for mode `66`, records
  policy-block reasons while attack is withheld, and forces rocket context so
  projectile leading is measured against weapon `20`.
- Action sampling bypasses normal inventory enrichment only for the
  `aim_fire_policy` smoke mode, keeping the rocket proof deterministic while
  leaving live default behavior unchanged.
- Status output now includes the policy-block counters and projectile lead
  detail fields needed to hard-gate the scenario:
  `aim_policy_blocks_reaction`, `aim_policy_blocks_aim_settle`,
  `aim_policy_blocks_burst_cooldown`, `live_aim_policy_blocks`,
  `live_aim_projectile_lead_uses`, `projectile_lead_uses`,
  `last_projectile_lead_weapon`, and `last_live_aim_reason`.
- `src/server/main.c` registers mode `66` as an FFA combat policy smoke,
  enables aim fairness for that mode, and annotates the begin marker with
  `aim_fire_policy=1`.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `aim_fire_policy_depth` scenario,
  reserved-mode mapping, marker checks, synthetic log coverage, and catalog
  documentation.

## Validation Evidence

Focused scenario validation passed from `.tmp\bot_scenarios\20260622T125826Z`:

- `frames=121`
- `commands=44`
- `route_failures=0`
- `combat_withheld_fire=35`
- `combat_fire_decisions=8`
- `action_applied_attack_buttons=8`
- `aim_policy_blocks_reaction=24`
- `aim_policy_blocks_aim_settle=24`
- `aim_policy_blocks_burst_cooldown=22`
- `aim_policy_fire_allowed=16`
- `live_aim_policy_blocks=35`
- `live_aim_projectile_lead_uses=43`
- `last_live_aim_weapon=20`
- `last_live_aim_reason=projectile_lead`
- `projectile_lead_uses=43`
- `last_projectile_lead_weapon=20`
- `last_projectile_lead_offset_sq=8984`

Commands run:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64
python tools\refresh_install.py --build-dir builddir-win
python tools\bot_scenarios\run_bot_scenarios.py --scenario aim_fire_policy_depth --install-dir .install --binary .install\worr_ded_x86_64.exe --format both
```

## Catalog Stats

- Implemented catalog rows: `72` total.
- Automated short-run rows: `71`.
- Manual-only rows: `1`.
- Default pending rows: `0`.
- Highest reserved smoke mode: `66`.
- Raw plan checklist rows: `809/809` checked.

## Next Steps

The next M2 slices should move from target memory, weapon scoring, and
aim/fire policy into survival, ammo-pressure, inventory use, and broader combat
regression loops. The highest-value follow-up is to prove live decisions can
seek ammo or defensive resources when the currently favored combat policy is
resource-starved or unsafe.
