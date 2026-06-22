# Q3A BotLib Survival Inventory Use Proof

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This round promotes the next M2 combat/inventory slice by proving the carried
inventory use path under survival pressure. Dedicated scenario mode `68`,
`survival_inventory_use`, stages a one-bot FFA proof where the bot has low
health, no armor, a carried power shield, and enough cells to activate it.

The proof verifies the full live action chain: carried inventory scanning finds
a usable survival candidate, the policy selects power armor, the action layer
accepts a pending inventory intent, the command builder validates a
`use_inventory_index` request, and the brain-owned dispatch path submits the
exact `use_index_only` item use.

No Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified for this round.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` adds mode `68` preparation. The setup
  stages client `0` at `25/100` health, clears armor and active power armor,
  grants `IT_POWER_SHIELD`, grants at least `50` cells, and selects the power
  shield item before the action policy samples carried inventory.
- Action proof status now exposes cumulative inventory command evidence:
  `action_use_inventory_decisions`, `action_pending_inventory_uses`,
  `action_inventory_command_requests`, `action_inventory_command_dispatches`,
  `action_command_request_builds`, `action_command_request_accepted`,
  `action_command_request_submitted`, `action_command_request_dispatch_attempts`,
  `action_last_command_request_kind_name`, and
  `action_last_command_dispatch_outcome_name`.
- Inventory policy status now exposes the survival proof counters required by
  the scenario harness: carried inventory scans, candidates, usable candidates,
  selections, survival uses, and power-armor uses.
- `src/server/main.c` reserves mode `68` as a one-bot FFA survival-inventory
  smoke, resets `sg_bot_frame_command_smoke_survival_inventory` with the other
  scenario cvars, and annotates the begin marker with `survival_inventory=1`.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `survival_inventory_use` scenario,
  reserved-mode mapping, marker gates, optional-field coverage, catalog tests,
  and synthetic marker evaluation.

## Validation Evidence

Focused scenario validation passed from
`.tmp\bot_scenarios\survival_inventory_use\20260622T161739Z`:

- `frames=60`
- `commands=60`
- `route_commands=60`
- `route_failures=0`
- `action_inventory_policy_scans=60`
- `action_inventory_policy_usable_candidates=1`
- `action_inventory_policy_selections=1`
- `action_inventory_policy_survival_uses=1`
- `action_inventory_policy_power_armor_uses=1`
- `action_use_inventory_decisions=1`
- `action_pending_inventory_uses=1`
- `action_inventory_command_requests=1`
- `action_command_request_submitted=1`
- `action_inventory_command_dispatches=1`
- `action_last_command_request_kind_name=use_inventory_index`
- `action_last_command_dispatch_outcome_name=submitted`
- `pass=1`

The first focused live run used the previously staged `.install` binary and
failed before the new mode could be observed. After refreshing `.install` from
the rebuilt binaries, the same scenario passed with the expected mode `68`
begin marker and inventory dispatch counters.

Commands run:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q
python tools\bot_scenarios\run_bot_scenarios.py --catalog --json-out .tmp\bot_scenarios\catalog_survival_inventory_prebuild.json
ninja -C builddir-win sgame_x86_64.dll
ninja -C builddir-win worr_ded_x86_64.exe
ninja -C builddir-win worr_ded_engine_x86_64.dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools\bot_scenarios\run_bot_scenarios.py --scenario survival_inventory_use --artifact-dir .tmp\bot_scenarios\survival_inventory_use --format both --json-out .tmp\bot_scenarios\survival_inventory_use\latest.json
```

## Catalog Stats

- Implemented catalog rows: `74` total.
- Automated short-run rows: `73`.
- Manual-only rows: `1`.
- Default pending rows: `0`.
- Highest reserved bot frame-command smoke mode: `68`.
- Raw plan checklist rows: `809/809` checked.

The default `--catalog` view reports `73` implemented rows because it excludes
the manual-only high-bot degradation row unless explicitly requested.

## Next Steps

M2 now has focused proof for target memory, weapon scoring, aim/fire policy,
ammo pickup pressure, and carried power-armor use. The next practical slice is
broader survival behavior: emergency health/armor route ownership, retreat or
threat-avoidance scoring, and compact combat regression rows that prove these
choices away from one staged inventory setup.
