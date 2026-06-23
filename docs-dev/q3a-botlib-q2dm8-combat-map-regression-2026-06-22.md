# Q3A BotLib Q2DM8 Combat Map Regression

Date: 2026-06-22

Task IDs: `FR-04-T03`, `FR-04-T11`, `FR-04-T15`, `DV-03-T05`

## Purpose

Extend the compact combat/survival regression matrix beyond `mm-rage` and
`q2dm2` by promoting equivalent `q2dm8` rows for both the mode `71`
combat/survival proof and the mode `72` low-health threat-retreat proof.

This round keeps the behavior implementation unchanged and focuses on making
the scenario harness prove that existing combat, health-item routing, recovery,
and threat-retreat contracts survive a second reference deathmatch layout with
different item placement and route pressure.

## Implementation

- Added `combat_survival_regression_q2dm8` to
  `tools/bot_scenarios/run_bot_scenarios.py`.
  - Reuses smoke mode `71`.
  - Runs on map `q2dm8` through the scenario map override.
  - Hard-gates the begin marker with `map=q2dm8`.
  - Requires route-clean command output, visible/shootable enemy telemetry,
    low-health health-item goal assignment, item arbitration ownership, and
    recovery ownership.
- Added `threat_retreat_avoidance_q2dm8` to
  `tools/bot_scenarios/run_bot_scenarios.py`.
  - Reuses smoke mode `72`.
  - Runs on map `q2dm8` through the scenario map override.
  - Hard-gates the begin marker with `map=q2dm8`.
  - Requires low-health threat selection, route requests, attack suppression,
    and combat ownership evidence after retreat handling.
- Updated `tools/bot_scenarios/test_run_bot_scenarios.py` so the catalog
  promotion, smoke-mode selection, map override, marker checks, and synthetic
  marker evaluation paths cover both `q2dm8` rows.
- Updated `tools/bot_scenarios/README.md` so the new map-matrix rows are visible
  in the scenario catalog documentation.

No C/C++ gameplay, BotLib, renderer, `q2proto/`, or upstream Q3A/BSPC source
files were modified in this round.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed 47 tests.
- Probe validation for mode `71` on `q2dm8` passed from
  `.tmp\bot_scenarios\20260622T204655Z`.
  - Key metrics: `frames=121`, `commands=121`, `route_commands=121`,
    `route_failures=0`, `item_goal_assignments=11`,
    `recovery_command_uses=51`, `combat_enemy_visible=120`,
    `combat_enemy_shootable=120`, `combat_withheld_fire=10`.
- Probe validation for mode `72` on `q2dm8` passed from
  `.tmp\bot_scenarios\20260622T204702Z`.
  - Key metrics: `frames=121`, `commands=121`, `route_commands=121`,
    `route_failures=0`, `threat_retreat_requests=121`,
    `threat_retreat_enemy_sources=1`, `threat_retreat_activations=1`,
    `threat_retreat_route_requests=28`,
    `threat_retreat_attack_suppressions=25`,
    `threat_retreat_reengages=1`.
- Focused promoted-row validation passed from
  `.tmp\bot_scenarios\20260622T204956Z`.
  - Summary: 2 passed, 0 failed, 0 timeouts, 0 errors, 0 pending.
- Full implemented-suite validation passed from
  `.tmp\bot_scenarios\20260622T205123Z`.
  - Summary: 80 passed, 0 failed, 0 timeouts, 0 errors, 0 pending.

## Follow-Up

- Promote the next work toward mode-specific live loops, starting with CTF
  objective behavior, because the immediate multi-map combat/survival and
  threat-retreat matrix now covers `mm-rage`, `q2dm2`, and `q2dm8`.
- Keep future map-matrix rows evidence-driven: add rows when they introduce a
  materially different geometry, item economy, mode contract, or movement
  feature, not only to raise the catalog count.
