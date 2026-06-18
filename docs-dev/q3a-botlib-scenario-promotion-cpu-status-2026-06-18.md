# Q3A BotLib Scenario Promotion and CPU Status

Date: 2026-06-18

Tasks: `DV-03-T05`, `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T15`, `FR-04-T16`

## Summary

This round promoted the reserved behavior smoke modes `20` through `23` from
pending diagnostics into implemented scenario rows and completed the first
validated bot-frame and route CPU status emission path. The dedicated-server
capture path was rebuilt through the engine DLL target, so the larger line-safe
status capture now preserves all split marker families needed by the scenario
and perf parsers.

## Implementation

- Rebuilt the correct dedicated-server engine target (`worr_ded_engine_x86_64`)
  in addition to `sgame_x86_64`; the executable launcher alone was not enough
  to pick up the expanded server status capture buffer.
- Promoted `engage_enemy`, `switch_weapons`, `health_armor_pickup`, and
  `team_objective` to implemented scenario rows using smoke modes `20`, `21`,
  `22`, and `23`.
- Added scenario marker checks against the split frame-command, blackboard,
  action, objective, and source-counter status lines rather than accepting
  metric-name presence from a merged dictionary alone.
- Emitted bot-frame CPU counters from `bot_brain.*`, route query/reuse CPU
  counters from `bot_nav.*`, and Q3A route CPU counters from the BotLib import
  bridge on `q3a_bot_source_counter_status`.
- Updated the pending-gap reporter so an empty pending catalog reports
  `overall=ready` instead of staying blocked after all pending rows have been
  promoted.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28300 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`
  - Result: 9 passed, 0 failed, 0 timed out, 0 pending.
  - Artifact directory: `.tmp\bot_scenarios\20260618T133608Z`.
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_after_promotion.json --markdown-out .tmp\bot_scenarios\pending_gap_after_promotion.md`
  - Result: 0 pending rows, `overall=ready`.
- `python tools\bot_perf\analyze_bot_perf.py .tmp\bot_scenarios\20260618T133608Z\engage_enemy.stdout.txt --scenario-report .tmp\bot_scenarios\latest_report.json --format json`
  - Result: `source_counter_groups_missing=[]`.
  - Present source-counter groups: `bot_frame_cpu`, `route_query_cpu`,
    `q3a_route_cpu`, `visibility`, `static_bsp_trace`, and `entity_trace`.
  - Engage-enemy CPU samples: `bot_frame_cpu_ms_per_bot_sec=1.649`,
    `route_query_cpu_ms_per_bot_sec=0.239`, and
    `q3a_route_cpu_ms_per_bot_sec=0.167`.
- `python tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m pytest tools\bot_profiles\test_validate_bot_profiles.py tools\test_package_assets.py tools\bot_perf\test_analyze_bot_perf.py`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py tools\bot_perf\analyze_bot_perf.py tools\bot_perf\test_analyze_bot_perf.py`

## Remaining Work

The promoted modes are smoke-level behavior proofs, not final bot intelligence.
Follow-up work should deepen aim/firing policy, weapon/inventory command
ownership, team role selection, reference-map coverage, static BSP timing, and
dynamic entity clip CPU counters. Release packaging still needs full policy and
manifest coverage beyond the local `.install` refresh proof.
