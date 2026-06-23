# Q3A BotLib Bot Chat Live Event Cooldown

Date: 2026-06-23

Tasks: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round expands the default-off live bot chat event path from a single
`route_ready` source into a two-event proof that also counts each bot's initial
spawn utterance as live `spawn` telemetry when `sg_bot_chat_live_events` is
enabled. The event taxonomy remains the same eleven-entry set introduced by
the previous live-events slice, but `q3a_bot_chat_policy_status` now separates
`live_chat_spawn` from `live_chat_route_ready` so future event sources can be
added without losing per-event evidence.

The round also adds reserved frame-command smoke mode `80`,
`bot_chat_live_event_cooldown`. It runs the same four-bot TDM live chat event
path with `sg_bot_chat_min_interval_ms 60000`, proving that live event
selection still occurs while the global chat dispatcher submits only the first
utterance and rate-limits the remaining seven live dispatch attempts without
recording failures.

## Runtime Changes

- `src/game/sgame/bots/bot_brain.cpp` adds a shared live-event selection helper
  and records initial spawn chat as event id `3` (`spawn`) when live events are
  enabled.
- The live chat status surface now includes `live_chat_spawn` alongside total
  live events, route-ready events, submitted events, rate-limited events,
  failures, taxonomy size, and last-event metadata.
- Initial live spawn events now participate in the same submitted,
  rate-limited, and failure accounting as live route-ready replies, while the
  existing initial utterance personality counters remain unchanged.
- `src/game/sgame/gameplay/g_svcmds.cpp` prints `live_chat_spawn` in
  `q3a_bot_chat_policy_status`.
- `src/server/main.c` reserves mode `80` for the cooldown proof, keeps it in
  the bot chat policy lane, enables `sg_bot_chat_live_events`, and applies the
  same `60000` ms global cooldown used by the older chat rate-policy proof.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round. The work is WORR-native server/game
behavior and validation wiring.

## Scenario Harness

- `tools/bot_scenarios/run_bot_scenarios.py` adds
  `bot_chat_live_event_cooldown` for mode `80`.
- Mode `79` now requires `live_chat_events=8`, `live_chat_spawn=4`,
  `live_chat_route_ready=4`, and `live_chat_submitted=8`.
- Mode `80` requires `dispatch_attempts=8`, `dispatch_submitted=1`,
  `dispatch_rate_limited=7`, `reply_chat_submitted=0`,
  `reply_chat_rate_limited=4`, `live_chat_submitted=1`, and
  `live_chat_rate_limited=7`.
- `tools/bot_scenarios/test_run_bot_scenarios.py` updates the parser fixtures,
  catalog checks, marker gates, command construction checks, and subset catalog
  count for the new scenario.
- `tools/bot_scenarios/README.md` documents mode `80` and updates mode `79` to
  describe spawn plus route-ready live event coverage.

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
packaged `basew` data, loose botfile mirrors, and all eight staged q2aas AAS
archive members.

Focused live-events scenario:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_live_events --install-dir .install --game basew --timeout 120
```

Artifact: `.tmp\bot_scenarios\20260623T010520Z`

Result: 1 passed, 0 failed, 0 timeout, 0 error, 0 pending.

Key focused metrics:

- `frames=246`
- `commands=246`
- `route_commands=246`
- `route_failures=0`
- `item_goal_assignments=10`
- `dispatch_attempts=8`
- `dispatch_submitted=8`
- `dispatch_rate_limited=0`
- `dispatch_failures=0`
- `initial_chat_selections=4`
- `reply_chat_events=4`
- `reply_chat_route_ready=4`
- `reply_chat_submitted=4`
- `live_chat_events=8`
- `live_chat_spawn=4`
- `live_chat_route_ready=4`
- `live_chat_submitted=8`
- `live_chat_rate_limited=0`
- `live_chat_failures=0`
- `live_chat_event_taxonomy=11`
- `last_live_chat_event=2`
- `last_live_chat_event_name=route_ready`

Focused cooldown scenario:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_live_event_cooldown --install-dir .install --game basew --timeout 120
```

Artifact: `.tmp\bot_scenarios\20260623T010530Z`

Result: 1 passed, 0 failed, 0 timeout, 0 error, 0 pending.

Key focused metrics:

- `frames=246`
- `commands=246`
- `route_commands=246`
- `route_failures=0`
- `item_goal_assignments=15`
- `dispatch_attempts=8`
- `dispatch_submitted=1`
- `dispatch_rate_limited=7`
- `dispatch_failures=0`
- `rate_limit_ms=60000`
- `initial_chat_selections=4`
- `reply_chat_events=4`
- `reply_chat_route_ready=4`
- `reply_chat_submitted=0`
- `reply_chat_rate_limited=4`
- `reply_chat_failures=0`
- `live_chat_events=8`
- `live_chat_spawn=4`
- `live_chat_route_ready=4`
- `live_chat_submitted=1`
- `live_chat_rate_limited=7`
- `live_chat_failures=0`
- `last_live_chat_event=2`
- `last_live_chat_event_name=route_ready`

Full implemented suite:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --install-dir .install --game basew --timeout 120
```

Artifact: `.tmp\bot_scenarios\20260623T010536Z`

Result: 88 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Remaining M5 Work

The live chat path now proves spawn and route-ready event handling plus global
cooldown suppression. The next M5 slice should add another gameplay-derived
event source, such as item or enemy-sighted chat, or begin broadening the
profile phrase libraries while preserving the same team/global audience and
cooldown guarantees.
