# Q3A BotLib Combat Survival Second-Map Regression

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Purpose

Promote the next M2 roadmap slice by proving the compact combat/survival
regression away from the default `mm-rage` smoke map. The new row runs the
existing mode `71` setup on `q2dm2`, a larger staged DM reference map with its
own packaged AAS, so combat target facts, low-health item pressure, withheld
fire, item ownership, and recovery ownership are now protected across a second
map layout.

## Changes

- Added per-scenario map overrides to `tools/bot_scenarios/run_bot_scenarios.py`.
  Scenarios without an override still use the CLI `--map` value; the new
  `combat_survival_regression_q2dm2` row pins `map_name="q2dm2"`.
- Added `map_name` to scenario catalog/run result output so map-specific rows
  are self-describing in JSON reports.
- Added `map=<current map>` to the server
  `q3a_bot_frame_command_smoke_scenario=begin` marker, then hard-gated the
  q2dm2 row on `map=q2dm2`.
- Added the implemented `combat_survival_regression_q2dm2` scenario row. It
  reuses smoke mode `71` and requires route-clean commands, health goal
  assignment, visible/shootable enemy facts, withheld-fire evidence, and
  item/recovery arbitration ownership.
- Updated `tools/bot_scenarios/test_run_bot_scenarios.py` and
  `tools/bot_scenarios/README.md` for the new row and map override behavior.

## Validation

Commands run:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python -m pytest tools\bot_scenarios\test_run_bot_scenarios.py -q
meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas
python tools\bot_scenarios\run_bot_scenarios.py --scenario combat_survival_regression_q2dm2 --artifact-dir .tmp\bot_scenarios\combat_survival_regression_q2dm2 --format both --json-out .tmp\bot_scenarios\combat_survival_regression_q2dm2\latest.json --timeout 60
python tools\bot_scenarios\run_bot_scenarios.py --catalog --json-out .tmp\bot_scenarios\catalog_combat_survival_q2dm2.json --format text
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --artifact-dir .tmp\bot_scenarios\implemented_after_q2dm2_regression --format text --json-out .tmp\bot_scenarios\implemented_after_q2dm2_regression\latest.json --markdown-out .tmp\bot_scenarios\implemented_after_q2dm2_regression\latest.md
```

Focused artifact:

`.tmp\bot_scenarios\combat_survival_regression_q2dm2\20260622T194547Z`

Focused result:

- `1` passed, `0` failed, `0` timeouts, `0` errors, `0` pending.
- `map_name=q2dm2` in the scenario result and `map=q2dm2` in the begin marker.
- `frames=121`, `commands=121`, `route_failures=0`,
  `item_goal_assignments=5`, `stuck_detections=9`, and
  `recovery_command_uses=54`.
- Combat facts remained live: blackboard/action status reported visible and
  shootable enemy facts while survival pressure withheld attack input.
- Behavior arbitration reported item and recovery candidates plus item and
  recovery owners on q2dm2.

Full automated aggregate:

`.tmp\bot_scenarios\implemented_after_q2dm2_regression\20260622T194653Z`

- `77` passed, `0` failed, `0` timeouts, `0` errors, `0` pending.

Catalog state after this round:

- Default automated catalog: `77` implemented rows, `0` pending rows.
- Total implemented catalog including the manual high-bot degradation row:
  `78` rows.

## Provenance

This round is WORR-native harness, server marker, and documentation work. It
does not import or modify upstream Q3A, BSPC, or `q2proto/` source.
