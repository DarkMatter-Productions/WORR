# Q3A BotLib Bot Chat Phrase Library Expansion - 2026-06-23

Task IDs: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round expands the WORR bot chat phrase library from two phrase variants to four variants across initial, team-ready, route-ready, and enemy-sighted reply families. The implementation keeps the existing personality/event phrase ID buckets while recording low-digit phrase variants in chat policy status so scenario smokes can prove actual phrase diversity instead of only proving that a phrase was selected.

## Implementation

- Added four safe phrase variants for quiet, direct, taunting, helpful, steady, and fallback personalities in `BotChatPolicy_InitialPhrase` and `BotChatPolicy_ReplyPhrase`.
- Changed phrase selection seeding to use compact selection order rather than raw client slot numbers, avoiding duplicate variants when staged bots occupy non-contiguous client indices.
- Added chat status telemetry:
  - `initial_chat_phrase_variants`
  - `initial_chat_unique_variants`
  - `last_initial_chat_variant`
  - `reply_chat_phrase_variants`
  - `reply_chat_unique_variants`
  - `last_reply_chat_variant`
- Added reserved smoke mode `82` / `bot_chat_phrase_library`, which verifies four staged profile bots exercise all four initial and reply phrase variants through the live chat event path.
- Relaxed route/live chat smoke marker checks to allow valid extra gameplay-derived `enemy_sighted` events during longer aggregate runs while still requiring spawn and route-ready coverage.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`: 53 passed.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll`: passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --platform-id windows-x86_64`: passed.
- Focused `bot_chat_phrase_library`: `.tmp\bot_scenarios\20260623T020850Z`, 1 passed.
- Full `implemented`: `.tmp\bot_scenarios\20260623T021355Z`, 90 passed, 0 failed, 0 timeout, 0 error, 0 pending.

## Notes

The new phrase variant telemetry reads the low decimal digit from phrase IDs because IDs reserve the higher decimal bucket for event and personality identity. This avoids folding personality buckets into variant masks and keeps the status counters aligned with the actual phrase selected.
