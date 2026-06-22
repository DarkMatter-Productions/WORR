# Q3A BotLib Profile Movement Policy Implementation

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round wires staged profile movement-style metadata into the live
match-policy builder. Profile files already preserved `WORR_MOVEMENT_STYLE` as
bot userinfo; the game-side objective policy now interprets that hint as
attack, defense, roam, or evasive match behavior, reports compact telemetry, and
has a dedicated scenario proof.

No `q2proto/` files were modified, and no new Q3A, BSPC, idTech3, Quake3e,
baseq3a, Gladiator, or other upstream source was imported.

## Implementation

- `src/game/sgame/bots/bot_objectives.*` adds a native
  `BotObjectiveMovementStyle` policy enum and maps profile movement labels such
  as `strafe`, `pressure`, `rush`, and `circle strafe` to attack behavior;
  `anchor`, `camp`, `defense`, and `defence` to defensive behavior; `patrol`,
  `roam`, `flank`, and `midfield` to roam/midfield behavior; and `kite`,
  `retreat`, and `evasive` to evasive behavior.
- Match-policy construction now reads `bot_movement_style`, carries the parsed
  style through `BotObjectiveMatchContext` and `BotObjectiveMatchPolicy`, and
  applies deterministic bonuses to attack, defense, roam, collect, and selected
  role priority paths. Attack movement prefers engage/major-item pressure,
  defense movement supports defender/resource-sharing priority, roam movement
  supports midfield/roam/resource-sharing priority, and evasive movement boosts
  roam/collect pressure without pretending to be an attack role.
- `src/game/sgame/bots/bot_brain.cpp` exposes profile movement-style presence,
  attack/defense/roam/evasive buckets, applied counts, last movement style,
  style name, and movement bonus values through compact
  `q3a_bot_objective_status` rows.
- `src/server/main.c` reserves frame-command smoke mode `56` for the
  `profile_movement_policy` proof. The mode stages the `smoke`, `bulwark`,
  `relay`, and `vanguard` profile ids in TDM and flips only
  `sg_bot_profile_movement_policy_smoke`, leaving `sg_bot_behavior_enable` and
  `sg_bot_match_item_policy` disabled so the proof belongs to the profile
  movement bridge.
- `tools/bot_scenarios/run_bot_scenarios.py` adds the
  `profile_movement_policy` catalog row. The scenario gates mode identity, TDM
  readiness, TDM match-policy evaluation, movement-style bucket counters,
  applied movement bonuses, attack/defense/midfield selections, and last
  movement bonus telemetry.

## Validation

Commands run:

```powershell
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --scenario profile_movement_policy --artifact-dir .tmp\bot_scenarios\profile_movement_policy --format both --json-out .tmp\bot_scenarios\profile_movement_policy\report.json --markdown-out .tmp\bot_scenarios\profile_movement_policy\report.md
```

Results:

- Scenario harness unit tests passed: 45 tests.
- Windows `sgame_x86_64` and `worr_ded_engine_x86_64` targets compiled.
- `.install/` refreshed with current binaries, packaged assets, and q2aas AAS
  archive audit output.
- Focused `profile_movement_policy` validation passed from
  `.tmp\bot_scenarios\profile_movement_policy\20260622T070032Z` with one pass,
  zero failures, zero route failures, TDM readiness, profile movement-style
  counters, and positive movement bonus telemetry.

## Completion Impact

- Raw plan checklist remains `809/809` checked with zero raw unchecked rows.
- Scenario catalog now has 62 implemented rows total: 61 automated short-run
  rows plus one manual high-bot degradation-policy row.
- Reserved frame-command scenario modes now extend through mode `56`.

## Follow-Up

- Run the full automated implemented scenario suite after the next broader bot
  behavior slice, since the latest full-suite evidence still predates the newest
  profile policy rows.
- Keep chat personality as preserved metadata until there is a stable chat or
  action surface to consume it, and continue pushing profile hints into durable
  autonomous behavior rather than one-off smoke-only policy hooks.
