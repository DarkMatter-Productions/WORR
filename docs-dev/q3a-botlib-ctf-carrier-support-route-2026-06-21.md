# Q3A BotLib CTF Carrier Support Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a default-off CTF carrier-support route ownership proof behind
`sg_bot_ctf_carrier_support_route`. The bridge proves that WORR-native CTF
objective role policy can select a same-team bot carrying the enemy flag, assign
support role and carrier-support lane ownership, route to that carrier, and
surface hard scenario evidence through the bot scenario harness.

## Implementation

- `bot_objectives.*` now exposes
  `BotObjectives_AssignEnemyFlagCarrierSupportObjective`, which scans alive
  same-team clients for the bot team's enemy flag item and builds an
  `EnemyFlagPickup` target sourced from `FlagCarrier`.
- The role-policy handoff requests `Support`, records carrier-support lane
  selection, preserves the carrier client id, and reuses existing route-goal
  consistency checks before command/reach evidence is accepted.
- `bot_brain.*` adds the default-off
  `sg_bot_ctf_carrier_support_route` command bridge, a short mode-38 warmup
  guard for freshly spawned smoke bots, CTF team/carrier proof seeding, and a
  route-owner path that records requests, assignments, route requests, route
  commands, invalid skips, role, lane, objective type, target source, entity,
  carrier client, item, priority, and goal distance.
- `server/main.c` reserves smoke mode `38`, requests four CTF bots, sets
  `g_gametype 5`, resets `sg_bot_ctf_carrier_support_route`, and marks the
  begin row with `ctf_carrier_support_route=1`.
- `tools/bot_scenarios/` promotes `ctf_carrier_support_route` as an implemented
  scenario and gates CTF readiness, carrier-support lane selection, flag-carrier
  target-source selection, route requests, route commands, carrier identity, and
  invalid-skip safety.

The proof is WORR-native. No Q3A, Gladiator, BSPC, idTech3, or q2proto files
were imported or modified for this update.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  passed.
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 32
  tests.
- `meson compile -C builddir-win` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  passed and refreshed `.install`.
- Focused `ctf_carrier_support_route` passed from the refreshed install:
  `frames=246`, `commands=246`, `route_commands=246`,
  `route_failures=0`, `route_invalid_slots=0`, `skipped_inactive=0`, and
  `pass=1`.
- The focused smoke stream reported mode `38` with
  `ctf_carrier_support_route=1`,
  `ctf_carrier_support_route_requests=215`,
  `ctf_carrier_support_route_assignments=215`,
  `ctf_carrier_support_route_route_requests=215`,
  `ctf_carrier_support_route_route_commands=215`,
  `ctf_carrier_support_route_invalid_skips=0`,
  `last_ctf_carrier_support_route_role=4`,
  `last_ctf_carrier_support_route_lane=4`,
  `last_ctf_carrier_support_route_source=3`,
  `last_ctf_carrier_support_route_carrier_client=1`, and
  `last_ctf_carrier_support_route_goal_distance_sq=52981`.
- The full implemented scenario suite passed 30/30 from
  `.tmp/bot_scenarios/latest_report.json`.

## Follow-Up

- Keep own-base-return CTF priority consumption as future Phase 7 work.
- Continue turning smoke-level route-owner proofs into durable autonomous role
  behavior in ordinary FFA/TDM/CTF match flow.
