# Q3A BotLib Bot Chat Live Events

Date: 2026-06-23

Tasks: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round promotes bot chat beyond the smoke-only reply-event path by adding
the first live event trigger. The new default-off `sg_bot_chat_live_events`
cvar enables live route-ready replies after a bot successfully builds a route
command, and dedicated smoke mode `79` validates that the event travels through
the conservative live chat dispatcher rather than the earlier
`sg_bot_chat_event_policy_smoke` proof gate.

The event taxonomy is now exposed through `q3a_bot_chat_policy_status` with
eleven named event ids:

- `team_ready`
- `route_ready`
- `spawn`
- `item_taken`
- `item_denied`
- `enemy_sighted`
- `objective_changed`
- `flag_state`
- `low_health`
- `blocked`
- `victory_defeat`

Mode `79` proves the first live event source, `route_ready`, while leaving the
remaining event sources for later M5 breadth work.

## Runtime Changes

- `src/game/sgame/bots/bot_runtime.cpp` registers
  `sg_bot_chat_live_events`, defaulting it to disabled.
- `src/game/sgame/bots/bot_brain.cpp` tracks live chat event counters
  separately from smoke reply-event counters. The status surface now records
  total live events, route-ready live events, submitted live events,
  rate-limited live events, live dispatch failures, the live taxonomy size, the
  last live event id, and the last live event name.
- `bot_brain.cpp` emits a live `route_ready` chat reply once per bot spawn
  after route-command ownership succeeds and the live-events cvar is enabled.
  The existing initial profile-chat line remains separate, so the focused row
  validates both initial dispatch and live reply dispatch.
- `src/game/sgame/gameplay/g_svcmds.cpp` prints the new live chat counters in
  `q3a_bot_chat_policy_status`.
- `src/server/main.c` reserves smoke mode `79`, stages a four-bot TDM run,
  enables `sg_bot_allow_chat` plus `sg_bot_chat_live_events`, and resets the
  live-events cvar after the smoke path.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round. The live chat trigger and status surface
are WORR-native.

## Scenario Harness

- `tools/bot_scenarios/run_bot_scenarios.py` adds the
  `bot_chat_live_events` catalog row for reserved mode `79`.
- The row hard-gates the begin marker, chat policy status, live-event counters,
  taxonomy size, route-ready event id, route-ready event name, dispatch
  counters, and route-clean frame-command status.
- `tools/bot_scenarios/test_run_bot_scenarios.py` adds raw reserved-mode parser
  coverage, string metric parsing for `last_live_chat_event_name`, command
  construction checks for `sg_bot_chat_live_events 1`, and broad catalog
  coverage.
- `tools/bot_scenarios/README.md` documents mode `79` and the new scenario.

## Validation

Unit coverage:

```text
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
```

Result: passed 53 tests.

Build coverage:

```text
meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll
```

Result: passed. Ninja printed the existing recoverable
`premature end of file; recovering` warning before completing the targets.

Install refresh:

```text
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Result: passed. `.install/` was refreshed with current Windows binaries,
packaged `basew` data, and all eight staged q2aas AAS archive members.

Focused scenario:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_live_events --install-dir .install --game basew --timeout 120
```

Artifact: `.tmp\bot_scenarios\20260623T003630Z`

Result: 1 passed, 0 failed, 0 timeout, 0 error, 0 pending.

Key focused metrics:

- `frames=246`
- `commands=246`
- `route_commands=246`
- `route_failures=0`
- `item_goal_assignments=13`
- `dispatch_attempts=8`
- `dispatch_submitted=8`
- `dispatch_failures=0`
- `initial_chat_selections=4`
- `reply_chat_events=4`
- `reply_chat_route_ready=4`
- `reply_chat_submitted=4`
- `live_chat_enabled=1`
- `live_chat_events=4`
- `live_chat_route_ready=4`
- `live_chat_submitted=4`
- `live_chat_rate_limited=0`
- `live_chat_failures=0`
- `live_chat_event_taxonomy=11`
- `last_live_chat_event=2`
- `last_live_chat_event_name=route_ready`

Full implemented suite:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --install-dir .install --game basew --timeout 120
```

Artifact: `.tmp\bot_scenarios\20260623T003639Z`

Result: 87 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Remaining M5 Work

This round deliberately keeps the live event surface narrow. The next useful M5
slice is to add more live event sources and cooldown behavior, then document the
supported user-facing chat behavior once the set is stable enough for server
operators.
