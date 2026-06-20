# Q3A BotLib Scenario Coverage Expansion

Date: 2026-06-18

Tasks: `DV-03-T05`, `FR-04-T03`, `FR-04-T04`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Summary

Worker E expanded the `tools/bot_scenarios` harness coverage for the current
Q3A BotLib/AAS behavior round without editing gameplay code. The update keeps
the implemented smoke suite source-backed, strengthens the contracts around
status lines that already exist, and records future behavior as pending or
optional instead of turning incomplete policy work into failing runtime gates.

## Harness Changes

- Added parser constants for the detailed action/objective/nav/team-policy
  status markers:
  - `q3a_bot_action_detail_status`
  - `q3a_bot_objective_detail_status`
  - `q3a_bot_nav_policy_status`
  - `q3a_bot_nav_natural_support_status`
  - `q3a_bot_nav_interaction_context_status`
  - `q3a_bot_team_policy_status`
- Extended optional field discovery with:
  - live combat/firing counters;
  - detailed aim/fairness counters;
  - item timer fairness signal placeholders;
  - trace-checked corner-cutting signal placeholders;
  - team-mode readiness signals.
- Added `any_*` marker-check operators for repeated marker rows. The immediate
  use is `q3a_bot_team_policy_status`, which emits a pre-cleanup row with three
  bots and a post-cleanup row with zero bots.
- Made marker parsing split known status markers into separate segments even
  when two engine status hooks are emitted on the same physical output line.
- Added an implemented `team_policy_duel_readiness` scenario using the existing
  `sv_bot_team_policy_smoke 2` path.
- Strengthened implemented scenarios:
  - `engage_enemy` now also requires detailed combat/fire/action proof.
  - `switch_weapons` now also requires detailed command-dispatch proof.
  - `health_armor_pickup` now also requires detailed health/armor item
    evaluation and seek-decision proof.
  - `team_objective` now also requires CTF role-policy, enemy-flag assignment,
    objective area, and detailed lane-policy proof.

## Pending Contracts

The following rows are intentionally pending:

- `aim_fairness_policy_integration`: waits for live firing to consume the
  aim/fairness policy counters.
- `item_timer_fairness_signals`: waits for explicit item-timer fairness
  counters such as `item_timer_evaluations`, `item_timer_allowed_uses`, and
  `item_timer_fairness_blocks`.
- `trace_checked_corner_cutting`: waits for route corner-cut trace and
  acceptance counters, backed by existing BSP trace source counters.
- `ffa_tdm_match_readiness`: waits for a dedicated
  `q3a_bot_match_readiness_status` line.
- `coop_match_readiness`: waits for a dedicated
  `q3a_bot_coop_readiness_status` line.

These pending rows preserve catalog visibility while keeping
`--scenario implemented` focused on behavior that currently exists.

## Validation

Commands run:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
python tools\bot_scenarios\test_run_bot_scenarios.py
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format text --scenario all --json-out .tmp\bot_scenarios\catalog_worker_e.json
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28500 --format text --json-out .tmp\bot_scenarios\worker_e_latest_report.json --markdown-out .tmp\bot_scenarios\worker_e_latest_report.md
```

Results:

- `py_compile`: passed.
- `test_run_bot_scenarios.py`: passed, 27 tests.
- Catalog: passed; 15 scenarios reported, with 10 implemented and 5 pending.
- Implemented smoke run: passed; 10 passed, 0 failed, 0 timed out, 0 errored.

## Follow-Up Hooks Needed

- Add live aim/firing status where `aim_policy_evaluations` and
  `aim_policy_fire_allowed` become nonzero from brain-owned firing, not only
  helper scaffolding.
- Add explicit item timer fairness counters before item timing can become a
  hard scenario gate.
- Add route corner-cut trace/accept/reject counters when trace-checked corner
  cutting lands.
- Add FFA/TDM and coop readiness status lines if those flows should promote
  from pending catalog rows into implemented scenario smokes.
