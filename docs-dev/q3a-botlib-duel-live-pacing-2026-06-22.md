# Q3A BotLib Duel Live Pacing

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes Duel pacing into the implemented bot scenario catalog as
mode `75`, `duel_live_pacing`. The new path adds a Duel match-policy mode,
default-off live pacing gate, Duel item-denial policy, server smoke support,
and scenario harness coverage that proves two active Duel bots can combine
route ownership, item scoring, role combat, and spawn-pressure avoidance in one
live Duel run.

No new Q3A, BSPC, or q2proto source was imported or modified. The work is
WORR-owned behavior, status, smoke, harness, and documentation code layered on
the existing BotLib/AAS runtime.

## Implementation Notes

- `BotObjectiveMatchMode::Duel` is now a first-class match-policy mode with
  stable value `5`, mapped from `GameType::Duel` and reported as `duel`.
- Duel bots use attacker/midfield-compatible FFA-style role/lane shape while
  recording a dedicated `team_objective_match_policy_duel` compact objective
  counter.
- Duel item policy treats weapons, ammo, powerups, and techs as deny-enemy
  pressure so pickup-goal scoring favors resource control over generic FFA
  self-greed.
- `sg_bot_duel_live_pacing` gates the Duel live-pacing path without enabling
  the individual FFA proof cvars. When active, Duel policy can reuse the
  existing FFA-style route, item-role, role-combat, spawn-camp route-source,
  and spawn-camp combat-veto status families while preserving mode `5`/`duel`
  marker evidence.
- Server smoke mode `75` stages `g_gametype 2`, targets two active bots, resets
  `sg_bot_duel_live_pacing` between runs, and emits a begin marker with
  `duel_live_pacing=1` while the FFA live-pacing proof cvars remain `0`.
- The `duel_live_pacing` scenario requires Duel readiness, route-clean command
  output, Duel match-policy selection, deny-enemy item-role selection, FFA-style
  route/combat status in Duel mode, validation-only pickup scoring telemetry,
  and spawn-source attack suppression evidence.
- The q2dm8 combat/survival regression marker contract was hardened during the
  full-suite pass: the scenario still requires health-candidate, low-health,
  goal-assignment, item-owner, and recovery-owner evidence, but no longer
  assumes the final action-status tail line must end on
  `item_last_utility_kind_name=health` after later live pickup scans advance the
  tail metadata.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed: 50 tests.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll`
  - Passed. Ninja reported a recoverable `premature end of file` warning while
    completing the target graph.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\duel-live-pacing-refresh-package-report.json --q2aas-package-audit-report .tmp\q2aas\duel-live-pacing-refresh-package-audit-report.json`
  - Passed. The refresh rebuilt `.install`, injected the staged q2aas AAS
    members, and passed archive-required package audit.
- Focused Duel validation:
  - `python tools\bot_scenarios\run_bot_scenarios.py --scenario duel_live_pacing --timeout 120 --base-port 29020 --format text --json-out .tmp\bot_scenarios\duel_live_pacing_report.json --markdown-out .tmp\bot_scenarios\duel_live_pacing_report.md`
  - Passed from `.tmp\bot_scenarios\20260622T222142Z` with `frames=121`,
    `commands=121`, `route_commands=121`, `route_failures=0`, and `pass=1`.
- q2dm8 regression recheck after marker hardening:
  - Passed from `.tmp\bot_scenarios\20260622T222450Z`.
- Full implemented suite:
  - `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 29300 --format text --json-out .tmp\bot_scenarios\implemented_after_duel_live_pacing_report.json --markdown-out .tmp\bot_scenarios\implemented_after_duel_live_pacing_report.md`
  - Passed from `.tmp\bot_scenarios\20260622T222457Z` with 83 passed rows, 0
    failed rows, 0 timeouts, 0 errors, and 0 pending rows.

## Files Updated

- `src/game/sgame/bots/bot_objectives.*`
- `src/game/sgame/bots/bot_brain.cpp`
- `src/game/sgame/bots/bot_nav.cpp`
- `src/server/main.c`
- `tools/bot_scenarios/run_bot_scenarios.py`
- `tools/bot_scenarios/test_run_bot_scenarios.py`
- `tools/bot_scenarios/README.md`
- `docs-dev/plans/bot-implementation-completion-roadmap.md`
- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
- `docs-dev/q3a-botlib-aas-credits.md`
