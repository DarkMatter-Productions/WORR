# Q3A BotLib scenario raw reserved diagnostics

Date: 2026-06-18

Task: `DV-03-T05`

## Summary

This scenario-harness slice keeps `engage_enemy`, `switch_weapons`,
`health_armor_pickup`, and `team_objective` pending while making the mode
`20` through `23` promotion path easier to diagnose.

The harness now treats the reserved-mode begin marker as required promotion
evidence:

- `q3a_bot_frame_command_smoke_scenario=begin mode=20` for `engage_enemy`.
- `q3a_bot_frame_command_smoke_scenario=begin mode=21` for `switch_weapons`.
- `q3a_bot_frame_command_smoke_scenario=begin mode=22` for
  `health_armor_pickup`.
- `q3a_bot_frame_command_smoke_scenario=begin mode=23` for `team_objective`.

Raw logs can be supplied to `--pending-gap-report` with
`--pending-gap-raw-log <file-or-directory>`. The parser groups each raw log by
the reserved-mode begin marker and then reads the latest
`q3a_bot_frame_command_status`, `q3a_bot_blackboard_status`, and
`q3a_bot_action_status` rows for that mode. Those raw diagnostics can satisfy
marker and metric presence in the gap report, but they do not change the
scenario catalog status from pending.

## Harness behavior

- Pending rows expose marker promotion checks for the reserved-mode begin marker
  so a future promotion cannot accidentally use the wrong mode or setup cvars.
- Gap reports now distinguish normal scenario rows from raw reserved-mode
  diagnostics with `fixture_source`, `raw_diagnostic_present`, and
  `raw_diagnostic` summary fields.
- Raw diagnostics record metric provenance through `metric_sources`, for
  example `action_applied_attack_buttons<-q3a_bot_action_status`.
- Markdown and text reports split missing status metrics from missing marker
  metrics so source gaps are visible without reading JSON.

## Current raw-mode state

Existing raw logs under `.tmp/bot_scenarios/raw_modes` contain begin markers for
modes `20` through `23`, plus frame/action/blackboard status rows. The gap
report can parse those markers, but the scenarios remain blocked:

- Modes `20`, `21`, and `22` expose the planned action/combat/item marker
  fields, but the current counters are still zero and `pass=0`.
- Mode `23` exposes the reserved team-objective begin marker and frame status,
  but the dedicated team-objective metrics are still absent.

## Validation

Command:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed.

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed. `15` tests ran.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_missing_raw_modes.json --markdown-out .tmp\bot_scenarios\pending_gap_missing_raw_modes.md
```

Result: completed with an intentionally blocked gap report: `0` ready, `4`
blocked, `4` missing rows, `42` missing status metrics, `28` missing marker
metrics, `0` failed metric checks, and `0` failed marker checks.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --pending-gap-raw-log .tmp\bot_scenarios\raw_modes --format text --json-out .tmp\bot_scenarios\pending_gap_with_raw_modes.json --markdown-out .tmp\bot_scenarios\pending_gap_with_raw_modes.md
```

Result: completed with an intentionally blocked gap report: `0` ready, `4`
blocked, `5` raw diagnostics parsed, `4` pending rows backed by raw diagnostics,
`9` missing status metrics, `0` missing marker metrics, `38` failed metric
checks, and `0` failed marker checks.

## Remaining gaps

- The four scenario catalog rows are still pending.
- Source work still needs real pass counters for combat engagement, weapon
  switching, health/armor pickup completion, and team-objective progress.
- Once the source metrics pass, the harness owner can promote individual rows by
  moving their smoke mode into the implemented path and retaining the same
  marker and metric checks.
