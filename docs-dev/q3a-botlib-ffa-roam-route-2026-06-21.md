# Q3A BotLib FFA Roam Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the first live consumer for the existing FFA match-policy
metadata. The new default-off `sg_bot_ffa_roam_route` bridge lets bots in a
free-for-all match turn the roam/collect/engage policy result into a short
timed route goal, proving that the role/lane helper can affect real command
ownership instead of only reporting status.

The implementation stays WORR-native. It does not import new Q3A, Gladiator,
BSPC, idTech3, or `q2proto/` source files.

## Runtime Changes

- `bot_brain.cpp` registers `sg_bot_ffa_roam_route`, maps the enabled bridge to
  smoke mode `42`, and adds `BotTimedRouteGoalKind::FfaRoam` as a dedicated
  timed route-goal owner.
- FFA route activation requires a valid free-for-all scoring policy with
  roam, collect, and engage intent. The owner records request, policy
  selection, activation, refresh, deferral, route request, expiration, invalid
  skip, and last-role/lane/priority/goal-distance status fields under
  `ffa_roam_route_*` and `last_ffa_roam_route_*`.
- The route direction is deterministic per client and fans bots across forward,
  right, back, and left lanes from their current view basis. That keeps the
  smoke proof compact while avoiding a single identical destination for every
  FFA participant.
- `server/main.c` reserves smoke mode `42` as a four-bot FFA setup, sets
  `deathmatch 1` / `g_gametype 1`, toggles `sg_bot_ffa_roam_route`, exposes
  `ffa_roam_route=1` in the scenario begin marker, and clears the cvar during
  smoke cleanup.

## Scenario Coverage

- `tools/bot_scenarios/run_bot_scenarios.py` promotes `ffa_roam_route` as an
  implemented row for smoke mode `42`.
- The scenario requires FFA readiness, the begin-marker cvar flag, match-policy
  FFA selections, timed route owner kind `7`, FFA route requests, route
  activations, route requests, zero invalid skips, and last FFA mode/role/lane
  plus positive goal distance.
- Optional field discovery now includes the `ffa_roam_route_counters` family so
  compact text and JSON reports surface the new counters without depending on
  verbose frame-command output.
- `tools/bot_scenarios/test_run_bot_scenarios.py` covers the scenario catalog,
  build command ordering, marker gates, fixture parsing, optional-field
  grouping, and report text.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 32
  tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  passed and refreshed `.install`.
- Focused `ffa_roam_route` scenario passed from the refreshed install with
  `route_commands=246`, `route_failures=0`, `pass=1`, and artifact root
  `.tmp\bot_scenarios\20260621T092914Z`.
- The full implemented scenario suite passed with 34 passed, 0 failed, 0
  timed out, 0 errored, and 0 pending from artifact root
  `.tmp\bot_scenarios\20260621T092925Z`.

## Remaining Work

This is still a conservative proof bridge, not the final FFA brain. Follow-up
work should make FFA roam/collect/engage select richer destinations from map,
item, enemy, spawn-safety, and timer context; coordinate it with combat and
inventory owners; and keep the proof aligned with broader TDM/CTF/coop role
consumption.
