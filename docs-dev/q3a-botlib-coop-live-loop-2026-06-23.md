# Q3A BotLib Coop Live Loop

Date: 2026-06-23

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes the first aggregate coop behavior proof. The new
`coop_live_loop` scenario uses smoke mode `77` and default-off
`sg_bot_coop_live_loop` to prove that existing coop leader route, progress
wait, anti-blocking, interaction retry, and door/elevator source-plus-hold
policies can cooperate in one two-bot run instead of only passing as isolated
proof rows.

## Implementation

- Added `sg_bot_coop_live_loop` as a behavior umbrella for the selected coop
  helpers while preserving the existing individual proof cvars.
- Reserved server smoke mode `77` for `coop_live_loop`, including two-bot coop
  setup, `coop 1`, `deathmatch 0`, travel-elevator goal support, begin-marker
  evidence, cvar reset, and target bot-count handling.
- Split coop progress-wait activation so the live-loop proof can make one bot
  wait while the other keeps leader-route/support behavior active.
- Allowed coop leader-route activation to compose with door/elevator source
  ownership when the live-loop cvar is enabled.
- Preserved progress-wait command telemetry when door/elevator hold ownership
  preempts the normal route path.
- Tuned the coop anti-blocking close-distance proof only under
  `sg_bot_coop_live_loop`, because the combined elevator travel setup places
  the two bots farther apart than the older single-purpose anti-blocking row.
- Added catalog, marker checks, synthetic raw-mode fixture, command-order
  assertions, README documentation, and scenario selection tags for
  `coop_live_loop`.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 52
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
- First focused `coop_live_loop` validation reached the aggregate behavior
  proof but failed the anti-blocking close-command gate from
  `.tmp\bot_scenarios\20260622T234149Z`. The live-loop-only distance tuning
  above corrected that without changing the existing `coop_anti_blocking`
  single-purpose threshold.
- Focused `coop_live_loop` validation passed from
  `.tmp\bot_scenarios\20260622T234315Z` with `frames=121`, `commands=121`,
  `route_commands=61`, `route_failures=0`, `coop_leader_route_activations=60`,
  `coop_progress_wait_commands=60`, `coop_anti_blocking_policy_close=60`,
  `coop_anti_blocking_commands=60`, `coop_interaction_retry_commands=36`,
  `coop_door_elevator_source_commands=36`, and
  `coop_door_elevator_hold_commands=60`.
- The full `implemented` scenario suite passed 85 rows with 0 failures,
  timeouts, errors, or pending rows from
  `.tmp\bot_scenarios\20260622T234325Z`.
