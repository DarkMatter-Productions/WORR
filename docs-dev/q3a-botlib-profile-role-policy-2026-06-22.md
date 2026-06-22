# Q3A BotLib Profile Role Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This slice turns existing bot profile role metadata into a live match-policy
input. Server-loaded botfiles already copy `WORR_ROLE` / `role` values into bot
userinfo as `bot_role`; `bot_objectives.*` now consumes that value when building
the match context and uses it as the requested match role whenever the caller
did not supply a stronger explicit role.

## Implementation

- `BotObjectives_BuildMatchContext()` reads `bot_role` from
  `client->pers.userInfo` after validating the entity as a bot.
- The accepted profile role labels map into `BotObjectiveRole` values:
  `attacker` / `attack` / `offense` / `offence` / `duelist`, `defender` /
  `defense` / `defence` / `anchor`, `support` / `relay`, `midfielder` /
  `midfield` / `roamer`, and `returner` / `return`.
- The match-policy result and compact objective status now expose requested
  role, profile role, honored-role counters, fallback counters, and last
  requested/profile roles. These fields make profile-role consumption visible
  without inferring it from a selected lane alone.
- Dedicated server smoke mode `53` adds staged profile ids `smoke`, `bulwark`,
  `relay`, and `vanguard` in a four-bot TDM setup. The new
  `profile_role_policy` scenario hard-gates TDM readiness, profile-role
  request/honor counters, attacker/defender/support-midfield selections, and
  zero profile-role fallbacks.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --scenario profile_role_policy --artifact-dir .tmp\bot_scenarios\profile_role_policy --format both --json-out .tmp\bot_scenarios\profile_role_policy\report.json --markdown-out .tmp\bot_scenarios\profile_role_policy\report.md`

Focused scenario artifacts:
`.tmp\bot_scenarios\profile_role_policy\20260622T052929Z`.

Catalog and checklist stats after this slice:

- Scenario catalog: 59 implemented rows total: 58 automated short-run rows,
  zero pending default rows, plus one manual high-bot degradation-policy row.
- Raw markdown checklist rows in the plan: 809 checked, 0 open, 809 total.

## Source and Provenance

No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto files were
imported or modified. The changes are WORR-native behavior/status/tooling work
above existing profile loading and match-policy helpers.
