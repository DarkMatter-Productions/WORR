# Q3A BotLib Bot Chat Live Item Taken

Date: 2026-06-23

Task IDs: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds live `item_taken` bot chat coverage to the default-off
`sg_bot_chat_live_events` pipeline. Bots now translate real health/armor pickup
observations into the live chat event id `4` / `item_taken`, using the same
safe dispatch, personality phrase selection, cooldown, duplicate, and bot-client
broadcast protections as the earlier spawn, route-ready, enemy-sighted, and
low-health live events.

## Implementation

- Added `item_taken` live/reply accounting in
  `src/game/sgame/bots/bot_brain.cpp`, including personality-specific phrases,
  a per-spawn live event guard, and a live event dispatcher that keys off
  `BotItems_GetStatus()` pickup observations.
- Extended `q3a_bot_chat_policy_status` through
  `src/game/sgame/gameplay/g_svcmds.cpp` and `src/game/sgame/g_local.hpp` with
  `reply_chat_item_taken` and `live_chat_item_taken` counters.
- Reserved server smoke mode `85` in `src/server/main.c`. The mode reuses the
  health/armor pickup proof, runs as a one-bot FFA case, enables
  `sg_bot_chat_live_events`, and prints `bot_chat_live_item_taken=1` in the
  begin marker.
- Added `bot_chat_live_item_taken` to the scenario catalog, reserved-mode
  registry, parser fixtures, synthetic marker tests, command-builder coverage,
  and scenario README.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.

## Validation

- `python tools\bot_scenarios\test_run_bot_scenarios.py`: 53 passed.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll`: passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`: passed.
- Focused `bot_chat_live_item_taken`: `.tmp\bot_scenarios\20260623T051126Z`, 1 passed.
- Full `implemented`: `.tmp\bot_scenarios\20260623T051133Z`, 93 passed, 0 failed, 0 timeout, 0 error, 0 pending.

Focused evidence recorded health and armor pickup observations, positive health
and armor pickup deltas, `reply_chat_item_taken=1`, `live_chat_item_taken=1`,
`last_reply_chat_event=4`, and `last_live_chat_event_name=item_taken` with zero
chat dispatch failures.

## Notes

Mode `85` intentionally stages a health/armor pickup proof. That proof starts
from a low-health state, so the live chat status can legitimately count a
low-health precursor before `item_taken`. The validation therefore gates
`bot_chat_live_low_health=0` in the begin marker while allowing the status
counter to record the precursor, then requires `item_taken` to be the latest
reply/live event.
