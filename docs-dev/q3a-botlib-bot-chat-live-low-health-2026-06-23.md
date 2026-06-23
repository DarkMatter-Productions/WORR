# Q3A BotLib Bot Chat Live Low Health

Date: 2026-06-23

Task IDs: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the first survival-state live chat event beyond route and
combat sighting: bots can now announce `low_health` through the default-off
`sg_bot_chat_live_events` pipeline when their live health falls to or below the
chat policy threshold. The event uses the same safe dispatch, personality
phrase selection, cooldown, duplicate, and bot-client broadcast protections as
the earlier live chat events.

## Implementation

- Added event id `9` / `low_health` handling in
  `src/game/sgame/bots/bot_brain.cpp`, including personality-specific reply
  phrases and per-spawn live event suppression so a bot announces the state
  once per spawn.
- Added `reply_chat_low_health` and `live_chat_low_health` counters to
  `q3a_bot_chat_policy_status` through `src/game/sgame/gameplay/g_svcmds.cpp`
  and `src/game/sgame/g_local.hpp`.
- Reserved server smoke mode `84` in `src/server/main.c`. The mode reuses the
  survival-health route staging so the bot is actually low on health, runs as a
  one-bot FFA proof, and prints `bot_chat_live_low_health=1` in the begin
  marker.
- Added `bot_chat_live_low_health` to the scenario catalog, parser fixtures,
  command-builder checks, synthetic marker tests, reserved-mode map, and
  scenario README.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`: 53 passed.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll`: passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --platform-id windows-x86_64`: passed.
- Focused `bot_chat_live_low_health`: `.tmp\bot_scenarios\20260623T025752Z`, 1 passed.
- Full `implemented`: `.tmp\bot_scenarios\20260623T025801Z`, 92 passed, 0 failed, 0 timeout, 0 error, 0 pending.

Focused evidence recorded `survival_route_kind=health`,
`item_low_health_boosts=66`, `item_health_goal_assignments=3`,
`reply_chat_low_health=1`, `live_chat_low_health=1`,
`last_reply_chat_event=9`, and `last_live_chat_event_name=low_health` with
zero chat dispatch failures.

## Notes

The low-health trigger is intentionally tied to the live bot entity health
instead of the smoke-only chat event gate. Mode `84` uses smoke staging only to
make the gameplay state deterministic; the actual chat event is dispatched
from the normal live command frame after the survival item route is active.
