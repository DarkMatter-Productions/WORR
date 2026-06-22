# Q3A BotLib Profile Item Policy Implementation

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round wires profile item-policy hints from staged WORR botfiles into the
live match item/resource policy path. The profile loader already preserved the
fields as metadata; they now influence pickup scoring, expose compact telemetry,
and have a dedicated scenario proof.

No `q2proto/` files were modified, and no new Q3A, BSPC, idTech3, Quake3e,
baseq3a, or Gladiator source was imported.

## Implementation

- `src/server/main.c` parses `item_greed`, `item_denial`,
  `powerup_timing`, and `retreat_health` plus WORR aliases, carries them on the
  server profile record, and publishes them through bot userinfo as
  `bot_item_greed`, `bot_item_denial`, `bot_powerup_timing`, and
  `bot_retreat_health`.
- `src/game/sgame/bots/bot_objectives.*` reads those userinfo hints while
  building match-policy contexts. Item greed boosts self-oriented collection,
  item denial boosts deny-enemy scoring in team modes, powerup timing raises
  major item and powerup preference, and retreat health adds recovery priority
  when the bot is at or below the configured health threshold.
- Resource and item-role policies now receive the same profile-derived item
  bonuses, so the selected pickup goal telemetry can prove the hints reached the
  nav scoring layer.
- `src/game/sgame/bots/bot_brain.cpp` exposes profile item-policy counters,
  applied counts, last values, and last bonuses through compact
  `q3a_bot_objective_status` rows. The oversized compact objective line was
  split into several marker rows so newer fields are not lost to console-line
  truncation.
- `src/game/sgame/bots/bot_nav.*` carries the final profile item bonus on the
  selected FFA, CTF, team item-role, and team resource-denial goals.
- `tools/bot_scenarios/run_bot_scenarios.py` reserves mode `55` for
  `profile_item_policy`. The scenario stages the `smoke`, `bulwark`, `relay`,
  and `vanguard` profiles in TDM, enables `sg_bot_match_item_policy`, forces the
  low-health proof setup for retreat-health coverage, and gates objective/nav
  marker evidence for all four profile item hints.

## Validation

Commands run:

```powershell
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --scenario profile_item_policy --artifact-dir .tmp\bot_scenarios\profile_item_policy --format both --json-out .tmp\bot_scenarios\profile_item_policy\report.json --markdown-out .tmp\bot_scenarios\profile_item_policy\report.md
```

Results:

- Scenario harness unit tests passed: 45 tests.
- Windows `sgame_x86_64` and `worr_ded_engine_x86_64` targets compiled.
- `.install/` refreshed with current binaries, packaged assets, and q2aas AAS
  archive audit output.
- Focused `profile_item_policy` validation passed from
  `.tmp\bot_scenarios\profile_item_policy\20260622T062835Z` with one pass, zero
  failures, zero route failures, and live item-goal assignments.

## Completion Impact

- Raw plan checklist remains `809/809` checked with zero raw unchecked rows.
- Scenario catalog now has 61 implemented rows total: 60 automated short-run
  rows plus one manual high-bot degradation-policy row.
- Reserved frame-command scenario modes now extend through mode `55`.

## Follow-Up

- Run the full automated implemented scenario suite after the next broader bot
  behavior slice, since the latest full suite evidence still predates the newest
  profile policy rows.
- Continue converting preserved profile metadata into live behavior where the
  policy surface is stable, with movement style and chat personality as the
  most obvious remaining profile-depth lanes.
