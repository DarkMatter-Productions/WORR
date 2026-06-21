# Q3A BotLib FFA Spawn-Camp Avoidance Route Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off FFA spawn-camp avoidance proof on top of the
existing FFA roam-route owner. The new `sg_bot_ffa_spawn_camp_avoidance` cvar
enables server smoke mode `45`, which also enables `sg_bot_ffa_roam_route` so
the same timed route-goal owner remains the route-command boundary.

When FFA match policy recommends avoiding spawn camping, the command owner now
looks for the nearest active player within the smoke-safe avoidance radius and
uses that player as the source point for a short route away from the pressure
area. If no valid source exists, the bridge falls back to the ordinary
FFA roam/collect/engage route source instead of blocking command ownership.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` adds
  `sg_bot_ffa_spawn_camp_avoidance`, mode-45 smoke preparation, nearest-player
  source selection, anti-camp request/policy/source/activation/fallback/route
  counters, and last-source metadata.
- `src/server/main.c` reserves mode `45`, resets the cvar between smoke runs,
  enables both FFA route cvars for the scenario, and prints the mode marker
  field `ffa_spawn_camp_avoidance=1`.
- `tools/bot_scenarios/run_bot_scenarios.py` promotes
  `ffa_spawn_camp_avoidance` with hard marker checks for FFA readiness,
  match-policy selection, timed route-goal kind, anti-camp source selection,
  route requests, and clean invalid-skip behavior.
- `tools/bot_scenarios/test_run_bot_scenarios.py` adds the mode-45 catalog,
  command construction, optional field discovery, and synthetic marker tests.
- `src/game/sgame/bots/bot_brain.cpp` also now emits compact
  `q3a_bot_nav_policy_status` route-corner telemetry before the longer verbose
  nav-policy line so `trace_checked_corner_cut_*` aliases remain visible to
  the existing mode-21 scenario parser.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario trace_checked_corner_cutting --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\trace_checked_corner_cutting_report.json --markdown-out .tmp\bot_scenarios\trace_checked_corner_cutting_report.md`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario ffa_spawn_camp_avoidance --timeout 120 --base-port 28100 --format text --json-out .tmp\bot_scenarios\ffa_spawn_camp_avoidance_report.json --markdown-out .tmp\bot_scenarios\ffa_spawn_camp_avoidance_report.md`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md`

The final implemented scenario run passed with 37 passed, 0 failed, 0 timed
out, 0 errored, and 0 pending from
`.tmp\bot_scenarios\20260621T111215Z`. The focused mode-45 run reported
`route_commands=187`, `route_failures=0`, `ffa_spawn_camp_avoidance_requests`
and source-selection metadata, and `pass=1`.

## Credit Notes

This is WORR-native behavior glue and scenario instrumentation. No new Q3A,
Gladiator, BSPC, idTech3, or q2proto source files were imported or modified.
