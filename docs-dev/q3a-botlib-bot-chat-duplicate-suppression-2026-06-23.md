# Q3A BotLib Bot Chat Duplicate Suppression

Date: 2026-06-23

Task IDs: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds WORR-native duplicate suppression to the bot chat policy so the
same bot cannot immediately repeat the same semantic reply event. The first
submitted reply event now seeds a short duplicate window, and a later same-bot
attempt for the same event is counted as suppressed before it reaches public
chat dispatch. Suppressions are not counted as dispatch failures or global
cooldown rate limits.

## Implementation

- Added a fixed `5000` ms duplicate window for reply events in
  `src/game/sgame/bots/bot_brain.cpp`.
- Recorded the latest submitted reply event per bot and suppressed later
  same-event attempts within the window.
- Added duplicate telemetry to `q3a_bot_chat_policy_status`:
  - `reply_chat_duplicate_suppressed`
  - `live_chat_duplicate_suppressed`
  - `chat_duplicate_window_ms`
  - `last_duplicate_chat_client`
  - `last_duplicate_chat_event`
  - `last_duplicate_chat_event_name`
  - `last_duplicate_chat_phrase`
  - `last_duplicate_chat_elapsed_ms`
- Added reserved smoke mode `83` / `bot_chat_duplicate_suppression`, which
  intentionally enables both `sg_bot_chat_event_policy_smoke` and
  `sg_bot_chat_live_events`. The smoke-only `route_ready` reply is submitted
  first, and the matching live `route_ready` attempt is suppressed as a
  duplicate.
- Updated the bot scenario harness, parser fixtures, and README catalog for
  mode `83`.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`: 53 passed.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll`: passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --platform-id windows-x86_64`: passed.
- Focused `bot_chat_duplicate_suppression`: `.tmp\bot_scenarios\20260623T023211Z`, 1 passed.
- Full `implemented`: `.tmp\bot_scenarios\20260623T023230Z`, 91 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Notes

The duplicate policy is intentionally semantic instead of string-only: the
same bot repeating the same event is suppressed even if phrase rotation would
choose a different variant. That keeps live chat from spamming repeated
state announcements while still allowing different events, such as spawn,
team-ready, route-ready, and enemy-sighted, to speak independently.
