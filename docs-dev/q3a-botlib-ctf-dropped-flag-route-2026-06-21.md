# Q3A BotLib CTF Dropped Flag Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off CTF dropped-flag route ownership proof behind
`sg_bot_ctf_dropped_flag_route`. The bridge keeps the behavior out of normal
gameplay until explicitly enabled, while proving that WORR-native CTF objective
role policy can select a dropped enemy flag, route to it, and expose hard
scenario evidence through the bot scenario harness.

## Implementation

- `bot_brain.*` now recognizes smoke mode `37` through
  `sg_bot_ctf_dropped_flag_route` and seeds routeable synthetic red/blue dropped
  flags for the smoke proof only.
- The frame-command path calls the objective helper with an attacker role
  request, requires an `EnemyFlagPickup` assignment from a `DroppedFlagEntity`,
  and records a position route request to the selected dropped-flag route goal.
- The route result path records command/reach evidence only after the route goal
  stays consistent with the assigned dropped flag.
- A compact `q3a_bot_frame_command_status` row exposes
  `ctf_dropped_flag_route_*` and `last_ctf_dropped_flag_route_*` counters for
  request, assignment, route request, route command, invalid skip, role, lane,
  objective type, target source, entity, item, priority, and goal distance.
- `server/main.c` reserves smoke mode `37`, requests four CTF bots, sets
  `g_gametype 5`, resets `sg_bot_ctf_dropped_flag_route`, and marks the begin
  row with `ctf_dropped_flag_route=1`.
- `tools/bot_scenarios/` promotes `ctf_dropped_flag_route` as an implemented
  scenario and gates the mode, bot target, CTF readiness, dropped-flag response
  lane, dropped-flag target source, route requests, route commands, and invalid
  skips.

The proof reuses existing WORR-native objective helpers and route ownership
surfaces. No Q3A, Gladiator, BSPC, idTech3, or q2proto files were imported or
modified for this update.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 32
  tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  passed and refreshed `.install`.
- Focused `ctf_dropped_flag_route` passed from the refreshed install:
  `frames=246`, `commands=246`, `route_commands=246`,
  `route_failures=0`, `skipped_inactive=0`, and `pass=1`.
- The raw focused smoke stream reported mode `37` with
  `ctf_dropped_flag_route=1`,
  `ctf_dropped_flag_route_requests=246`,
  `ctf_dropped_flag_route_assignments=246`,
  `ctf_dropped_flag_route_route_requests=246`,
  `ctf_dropped_flag_route_route_commands=246`,
  `ctf_dropped_flag_route_invalid_skips=0`,
  `last_ctf_dropped_flag_route_lane=5`, and
  `last_ctf_dropped_flag_route_source=2`.
- The full implemented scenario suite passed 29/29 from
  `.tmp/bot_scenarios/latest_report.json`.

## Follow-Up

- Keep carrier-support and own-base-return CTF priority consumers as future
  Phase 7 work.
- Continue turning smoke-level role proofs into durable autonomous behavior in
  ordinary FFA/TDM/CTF match flow.
