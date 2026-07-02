# Q3A BotLib Chat Smoke Queue Determinism - 2026-07-01

Tasks: `FR-04-T07`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

The full implemented scenario rerun after the train interaction-arrival slice
left four chat/personality rows red. Three rows (`bot_chat_team_policy`,
`bot_chat_reply_policy`, and `bot_chat_event_policy`) were not failing because
chat behavior regressed; the smoke runner was issuing duplicate pending
`SV_BotAdd` requests while waiting for the fourth staged bot to become visible
in `SV_BotCount()`. Those duplicate queued requests matured into a fifth bot,
so the game-side chat status correctly reported five profile bots while the
scenario contract still expected four.

The fourth row (`bot_chat_live_match_result`) already proved both winning and
losing outcome phrases. Its remaining failure was an order-dependent assertion
that the last processed match-result bot must be a loss. Depending on team and
client iteration order, the last processed bot may be either a classified win
or loss while the meaningful win/loss counters remain correct.

## Implementation

- Added `bot_add_queue_count()` in `src/server/main.c` so smoke staging can
  see pending queued bot additions.
- Updated the frame-command smoke fill loop to compare spawned plus queued bot
  requests against `target_bots`. When the pending total already reaches the
  target, the smoke waits instead of enqueueing another add request.
- Extended the `q3a_bot_frame_command_smoke_after_extra_add_request` marker
  with `pending=` so future diagnostics can distinguish spawned and queued bot
  counts.
- Relaxed the mode `90` `last_match_result_outcome` and
  `last_match_result_outcome_name` marker checks to accept the latest
  classified win or loss. The scenario still requires
  `reply_chat_match_result_win >= 1`, `reply_chat_match_result_loss >= 1`,
  and `reply_chat_match_result_unknown == 0`.

## Validation

Passed:

```text
python -m py_compile tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py
python -m unittest tools.bot_scenarios.test_run_bot_scenarios
meson compile -C builddir-win
python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

The focused chat rerun passed all four previously failing rows:

```text
python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario bot_chat_team_policy,bot_chat_reply_policy,bot_chat_event_policy,bot_chat_live_match_result --timeout 120 --artifact-dir .tmp\bot_scenarios\chat_smoke_queue_determinism --format text --json-out .tmp\bot_scenarios\chat_smoke_queue_determinism.json
```

Result: `4/4` passed. The four rows each ended with `bots=4`,
`profile_chat_metadata=4`, `expected_bots=4`, and
`expected_profile_chat=4`. `bot_chat_live_match_result` also reported
`reply_chat_match_result_win=2`, `reply_chat_match_result_loss=2`,
`live_chat_match_result=4`, and zero unknown/tie/abort classifications.

The full automated implemented suite then passed:

```text
python tools/bot_scenarios/run_bot_scenarios.py --install-dir .install --game basew --scenario implemented --timeout 120 --artifact-dir .tmp\bot_scenarios\implemented_after_chat_smoke_queue_determinism --format text --json-out .tmp\bot_scenarios\implemented_after_chat_smoke_queue_determinism.json
```

Result: `123/123` passed, with `0` failed, `0` timeout, `0` error, and
`0` pending.

Release acceptance passed:

```text
python tools/bot_release/run_bot_acceptance.py --install-dir .install --base-game basew --format text --output .tmp\bot_scenarios\bot_acceptance_after_chat_smoke_queue_determinism.txt
```

Result: `15/15` checks passed, including required implemented scenario
evidence from
`.tmp\bot_scenarios\implemented_after_chat_smoke_queue_determinism.json`.
