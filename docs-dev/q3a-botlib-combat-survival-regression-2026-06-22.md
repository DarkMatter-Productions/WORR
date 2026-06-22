# Q3A BotLib Combat Survival Regression

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Purpose

Add a compact M2 regression row that proves combat pressure and survival item
pressure can coexist in one live bot run. Earlier M2 slices proved target
memory, weapon scoring, aim/fire pacing, ammo pickup pressure, carried
survival inventory use, health routing, and armor routing independently. This
round combines a visible enemy with low-health health routing so future changes
cannot accidentally erase enemy facts while the bot is trying to survive.

The important behavior contract is deliberately not "always attack." In the
live proof, the bot sees a visible and shootable enemy, but low-health survival
pressure plus aim/weapon readiness can make item/recovery ownership win and
withhold attack input. That is the useful regression surface for this slice.

## Implementation

- Reserved frame-command smoke mode `71` as `combat_survival_regression`.
- Added a combat/survival route selector for
  `sg_bot_frame_command_smoke_survival_route=combat_health`.
- Composed the existing combat peer setup with the survival health-route setup
  so the lead bot starts low on health with a routeable medium health pickup and
  a nearby visible enemy peer.
- Updated server smoke setup so mode `71` runs as two-bot FFA with
  `deathmatch=1`, `g_gametype=1`, combat setup `engage_enemy`, and begin marker
  fields `survival_route=1` plus `survival_route_kind=combat_health`.
- Added the harness row, reserved mode map entry, README row, synthetic marker
  coverage, and catalog assertions for `combat_survival_regression`.

## Scenario Contract

Mode `71` now hard-gates:

- Source pass, command emission, zero route failures, item goal assignment, and
  resolved AAS item-goal area.
- Begin-marker setup for mode `71`, FFA gametype, two-bot target count,
  `combat=engage_enemy`, and `survival_route_kind=combat_health`.
- Blackboard enemy acquisition, visibility, and shootability.
- Action-layer enemy acquisition, visibility, shootability, and withheld-fire
  evidence.
- Low-health item utility pressure through health candidates, health seek
  decisions, health goal assignment, and selected utility kind `health`.
- Behavior arbitration pressure through item candidates, recovery candidates,
  item owners, and recovery owners.

The scenario intentionally does not require `combat_fire_decisions` or
`action_attack_decisions`. The first live run showed that, under low-health
pressure, the bot can correctly retain target facts while withholding fire and
letting survival/recovery work own the command frames.

## Validation

Passed:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q
python tools\bot_scenarios\run_bot_scenarios.py --scenario combat_survival_regression --artifact-dir .tmp\bot_scenarios\combat_survival_regression --format both --json-out .tmp\bot_scenarios\combat_survival_regression\latest.json
```

Focused artifact:

`.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z`

Focused metrics:

- `frames=121`
- `commands=121`
- `route_commands=121`
- `route_failures=0`
- `item_goal_assignments=7`
- `last_item_goal_area=224`
- `combat_enemy_visible=120`
- `combat_enemy_shootable=119`
- `combat_withheld_fire=35`
- `behavior_arbitration_item_owners=3`
- `behavior_arbitration_recovery_owners=40`
- `pass=1`

Aggregate follow-up signal:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --artifact-dir .tmp\bot_scenarios --format text --json-out .tmp\bot_scenarios\latest_report.json
```

That expanded automated catalog attempt ran from
`.tmp\bot_scenarios\20260622T171732Z` and reported `42` passing rows plus `34`
older contract failures. The failures are concentrated in previous
team/CTF/coop readiness/objective marker rows and the existing `engage_enemy`
firing contract, not in mode `71`. Full automated catalog reconciliation should
be the next maintenance slice before treating the expanded short suite as a
green release gate.

## Completion Stats

- Phase checklist: `809/809`.
- Raw markdown checklist rows: `809/809`.
- Scenario catalog: `77` implemented rows total.
- Automated short-run rows: `76`.
- Manual high-bot degradation row: `1`.
- Default pending rows: `0`.
- Highest frame-command smoke mode: `71`.

## Next Steps

1. Reconcile the expanded automated catalog failures so the full 76-row short
   suite is green again.
2. Split the failures into likely buckets: stale readiness marker expectations,
   objective status emission drift, and the older `engage_enemy` attack/damage
   contract.
3. Continue M2 survival work after the catalog is clean, with broader
   threat-retreat and avoidance behavior across more maps.
