# Q3A BotLib Profile Team Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This slice extends live profile-driven match policy beyond role labels. Server
profiles can now carry teamplay, objective, and friendly-fire-care bias values
through bot userinfo, and `bot_objectives.*` consumes those hints when building
team/CTF match policy. The result is still conservative and proof-oriented:
profile hints alter priority/avoidance metadata inside the existing match
policy helpers instead of bypassing the default-off behavior gates.

## Implementation

- `src/server/main.c` parses `teamplay_bias`, `objective_bias`, and
  `friendly_fire_care` profile keys plus WORR aliases, copies them into bot
  userinfo as `bot_teamplay_bias`, `bot_objective_bias`, and
  `bot_friendly_fire_care`, and includes them in the profile smoke summary.
- `src/game/sgame/bots/bot_objectives.*` reads those userinfo values into
  `BotObjectiveMatchContext`, clamps numeric profile biases to `0.0` through
  `1.0`, and records the accepted values as permille fields for deterministic
  status reporting.
- Match-policy evaluation now applies teamplay and friendly-fire-care bonuses
  in team modes, objective bonuses in CTF, and can enable friendly-fire
  avoidance when a profile expresses high friendly-fire care even if the match
  damage rule is not currently punishing teammates.
- `src/game/sgame/bots/bot_brain.cpp` exposes compact objective-status counters
  for profile teamplay/objective/friendly-fire-care presence, applied counts,
  last accepted values, and last priority bonuses.
- Dedicated server smoke mode `54` adds staged profile ids `smoke`, `bulwark`,
  `relay`, and `vanguard` in a four-bot CTF setup. The new
  `profile_team_policy` scenario hard-gates profile hint presence, applied
  match-policy bonuses, friendly-fire policy activation, CTF readiness, route
  commands, and zero route failures.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed: 45 tests.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
  - Passed, linking `sgame_x86_64.dll` and `worr_ded_engine_x86_64.dll`.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`
  - Passed; all eight staged AAS maps were represented in `.install/basew/pak0.pkz`.
- `python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --scenario profile_team_policy --artifact-dir .tmp\bot_scenarios\profile_team_policy --format both --json-out .tmp\bot_scenarios\profile_team_policy\report.json --markdown-out .tmp\bot_scenarios\profile_team_policy\report.md`
  - Passed from `.tmp\bot_scenarios\profile_team_policy\20260622T055119Z`.

Focused scenario metrics:

- `frames=246`
- `commands=246`
- `route_commands=246`
- `route_failures=0`
- `item_goal_assignments=10`
- `pass=1`

Catalog and checklist stats after this slice:

- Scenario catalog: 60 implemented rows total: 59 automated short-run rows,
  zero pending default rows, plus one manual high-bot degradation-policy row.
- Raw markdown checklist rows in the plan: 809 checked, 0 open, 809 total.

## Source and Provenance

No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto files were
imported or modified. The changes are WORR-native profile parsing, match-policy
consumption, status/tooling, and documentation over existing bot objective and
server smoke infrastructure.
