# Q3A BotLib Bot Chat Live Enemy Sighted

Date: 2026-06-23

Tasks: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round expands the default-off live bot chat event path from spawn and
route-ready accounting into the first combat-derived event source. When
`sg_bot_chat_live_events` is enabled, bots now dispatch a live `enemy_sighted`
reply after their blackboard records a current visible enemy. The event uses
the existing event id `6` from the eleven-entry live chat taxonomy and remains
behind the same conservative dispatch, audience, and cooldown accounting as the
earlier live chat events.

The scenario catalog adds reserved frame-command smoke mode `81`,
`bot_chat_live_enemy_sighted`. It stages a two-bot TDM combat run with live
chat enabled, verifies visible and shootable enemy facts, requires
`reply_chat_enemy_sighted` plus `live_chat_enemy_sighted`, and proves the last
live event name is `enemy_sighted`.

## Runtime Changes

- `src/game/sgame/bots/bot_brain.cpp` adds event id `6` as
  `enemy_sighted`, extends personality-aware reply phrases for quiet, direct,
  taunting, helpful, steady, and fallback personalities, and records
  enemy-sighted reply/live counters.
- The live chat path now checks the per-bot blackboard for a valid current
  visible enemy and dispatches one enemy-sighted live reply per spawn.
- `BotBrain_ResetChatPolicyState()` resets the per-bot enemy-sighted live
  dispatch state alongside the existing route-ready live state.
- `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `reply_chat_enemy_sighted` and
  `live_chat_enemy_sighted` through `q3a_bot_chat_policy_status`.
- `src/server/main.c` reserves mode `81`, treats it as both live-chat and
  engage-enemy validation, prints `bot_chat_live_enemy_sighted=1` in the begin
  marker, and limits the proof to two profile bots for stable direct combat
  contact.

No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
imported or modified for this round. The work is WORR-native game/server chat
behavior and validation wiring.

## Scenario Harness

- `tools/bot_scenarios/run_bot_scenarios.py` adds
  `bot_chat_live_enemy_sighted` for mode `81`.
- The row hard-gates the mode `81` begin marker, TDM setup, live chat cvars,
  visible/shootable enemy action status, route-clean frame-command status,
  dispatch/submission counters, enemy-sighted reply/live counters, taxonomy
  size, last live event id `6`, and `last_live_chat_event_name=enemy_sighted`.
- `tools/bot_scenarios/test_run_bot_scenarios.py` adds reserved-mode begin
  lines, parser fixtures, marker checks, catalog count updates, and command
  construction coverage for the new row.
- `tools/bot_scenarios/README.md` documents mode `81` as the first
  gameplay-derived live chat event proof beyond spawn and route-ready.

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

Focused enemy-sighted scenario:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_live_enemy_sighted --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json
```

Artifact: `.tmp\bot_scenarios\20260623T013832Z`

Result: 1 passed, 0 failed, 0 timeout, 0 error, 0 pending.

Key focused metrics:

- `frames=121`
- `commands=121`
- `route_commands=121`
- `route_failures=0`
- `item_goal_assignments=3`
- `combat_enemy_acquisitions=120`
- `combat_enemy_visible=120`
- `combat_enemy_shootable=120`
- `dispatch_attempts=5`
- `dispatch_submitted=5`
- `dispatch_rate_limited=0`
- `dispatch_failures=0`
- `initial_chat_selections=2`
- `reply_chat_events=3`
- `reply_chat_route_ready=2`
- `reply_chat_enemy_sighted=1`
- `reply_chat_submitted=3`
- `live_chat_events=5`
- `live_chat_spawn=2`
- `live_chat_route_ready=2`
- `live_chat_enemy_sighted=1`
- `live_chat_submitted=5`
- `live_chat_rate_limited=0`
- `live_chat_failures=0`
- `live_chat_event_taxonomy=11`
- `last_live_chat_event=6`
- `last_live_chat_event_name=enemy_sighted`

Full implemented suite:

```text
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json
```

Artifact: `.tmp\bot_scenarios\20260623T013843Z`

Result: 89 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Remaining M5 Work

The live chat path now proves spawn, route-ready, enemy-sighted, and global
cooldown behavior. The next M5 slice should broaden phrase libraries and
duplicate-suppression behavior before presenting chat as fully supported
public behavior, then continue adding item, low-health, objective, and
match-result event sources behind the same safety gates.
