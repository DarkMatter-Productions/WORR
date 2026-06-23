# Q3A BotLib Coop Share Loop

Date: 2026-06-23

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes coop target/resource sharing into a combined proof row. The
new `coop_share_loop` scenario uses smoke mode `78` and default-off
`sg_bot_coop_share_loop` to prove that a support-policy bot can adopt a
teammate's hostile non-client target while the same two-bot coop run also
reserves and defers item route-goal candidates for teammate resource sharing.

## Implementation

- Added `sg_bot_coop_share_loop` as a default-off aggregate gate in the bot
  brain. It activates the existing coop target-sharing bridge and the existing
  coop resource-sharing bridge without setting the older single-purpose proof
  cvars.
- Updated `bot_nav.cpp` so the same aggregate cvar enables coop
  reserve-for-teammate scoring during live pickup-goal evaluation.
- Reserved server smoke mode `78` for `coop_share_loop`, including two-bot coop
  setup, cvar reset, begin-marker `coop_share_loop=1`, and preservation of the
  synthetic hostile non-client target used by the target-share proof.
- Added catalog, marker checks, synthetic raw-mode fixture, command-order
  assertions, README documentation, and reserved-mode diagnostics for the new
  scenario.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 53
  tests.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64
  worr_ded_x86_64 copy_sgame_dll` passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --archive-name pak0.pkz --platform-id
  windows-x86_64 --package-q2aas-aas --q2aas-stage-report
  .tmp\q2aas\stage-report.json --q2aas-package-report
  .tmp\q2aas\refresh-package-archive-report.json
  --q2aas-package-audit-report
  .tmp\q2aas\refresh-package-archive-audit-report.json` passed, refreshing
  the Windows install payload and all eight staged q2aas AAS archive members.
- Focused `coop_share_loop` validation passed from
  `.tmp\bot_scenarios\20260623T001149Z` with `frames=121`, `commands=121`,
  `route_commands=121`, `route_failures=0`, `item_goal_assignments=19`,
  `team_objective_coop_policy_resource_share=129`,
  `team_objective_resource_policy_reserve=56`,
  `item_reserved_deferrals=62`, `coop_target_share_requests=1`,
  `coop_target_share_source_candidates=1`, `coop_target_share_adoptions=1`,
  `last_coop_target_share_source_client=0`,
  `last_coop_target_share_target_entity=281`, and
  `last_coop_target_share_intent=5`.
- The full `implemented` scenario suite passed 86 rows with 0 failures,
  timeouts, errors, or pending rows from
  `.tmp\bot_scenarios\20260623T001205Z`.
