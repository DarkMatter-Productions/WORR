# Q3A BotLib CTF Objective Transitions

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes the next CTF objective slice from staged flag target states
to a live transition proof. The new `ctf_objective_transitions` scenario uses
smoke mode `76` to verify that actual CTF pickup, death-drop, and dropped-flag
return paths feed bot objective counters before the combined CTF objective
route policy takes ownership.

## Implementation

- Added `flagDrops` and `flagReturns` to the bot objective status snapshot and
  exposed them in compact and detailed objective status output.
- Added `BotObjectives_RecordFlagDrop` and `BotObjectives_RecordFlagReturn`
  helpers, including entity overloads that preserve last-source metadata for
  flag carrier and dropped-flag events.
- Hooked `CTF_DeadDropFlag` and the same-team dropped-flag return branch in
  `g_capture.cpp` so real gameplay transitions increment the new objective
  counters.
- Added default-off `sg_bot_ctf_objective_transitions` handling and reserved
  smoke mode `76` in the server frame-command smoke dispatcher.
- Added a CTF transition setup path that stages teams, performs an enemy-flag
  pickup through the CTF pickup entry point, death-drops the carried flag, then
  returns the dropped flag through the same CTF pickup logic used by gameplay.
- Kept the existing combined CTF objective route policy active after the
  transition setup so the scenario also proves base-return, dropped-flag, and
  carrier-support route selection can still command the live objective loop.
- Added catalog, parser, marker-check, synthetic raw-mode, command-construction,
  and README coverage for `ctf_objective_transitions`.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 51
  tests.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64
  worr_ded_x86_64 copy_sgame_dll` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --archive-name pak0.pkz --platform-id
  windows-x86_64 --package-q2aas-aas --q2aas-stage-report
  .tmp\q2aas\stage-report.json --q2aas-package-report
  .tmp\q2aas\refresh-package-archive-report.json
  --q2aas-package-audit-report
  .tmp\q2aas\refresh-package-archive-audit-report.json` passed, representing
  all eight staged q2aas AAS maps as loose files and `pak0.pkz` members.
- Focused `ctf_objective_transitions` validation passed from
  `.tmp\bot_scenarios\20260622T230509Z` with `frames=246`, `commands=246`,
  `route_commands=246`, `route_failures=0`, `team_objective_flag_pickups=2`,
  `team_objective_flag_drops=1`, `team_objective_flag_returns=1`,
  `ctf_objective_route_assignments=212`,
  `ctf_objective_route_base_return_candidates=106`,
  `ctf_objective_route_dropped_flag_candidates=212`,
  `ctf_objective_route_route_commands=212`, and
  `ctf_objective_route_invalid_skips=0`.
- The full `implemented` scenario suite passed 84 rows with 0 failures,
  timeouts, errors, or pending rows from
  `.tmp\bot_scenarios\20260622T230519Z`.
