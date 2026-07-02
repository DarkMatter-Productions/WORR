# Q3A BotLib Match-Result Outcome Chat

Date: 2026-07-01

Tasks: `FR-04-T07`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round completes the next focused M5 chat/personality slice by making the
existing live match-result chat path outcome-aware. The mode `90`
`bot_chat_live_match_result` scenario already proved that native intermission
state can emit the reserved `victory_defeat` live event. It now also proves
that each staged bot receives a classified result phrase rather than a generic
post-match line.

The implementation remains WORR-native and does not import new upstream code.

## Implementation

- Added a match-result outcome classifier in `bot_brain.cpp` with explicit
  `unknown`, `win`, `loss`, `tie`, and `abort` outcomes.
- Classified team modes from `level.teamScores` and the bot's current team.
  Non-team modes fall back to `CalculateRanks()` and `pers.currentRank`.
- Treated known administrative or invalid-match intermission messages as
  aborted results so future match-flow exits do not appear as wins/losses.
- Encoded the outcome into the match-result phrase id for event id `11`.
  The existing phrase id layout remains intact for all other reply events.
- Replaced the generic match-over phrases with compact outcome-specific lines:
  win, loss, tie, abort, and unknown fallback phrase sets.
- Added status counters to `q3a_bot_chat_policy_status`:
  `reply_chat_match_result_unknown`, `reply_chat_match_result_win`,
  `reply_chat_match_result_loss`, `reply_chat_match_result_tie`,
  `reply_chat_match_result_abort`, `last_match_result_outcome`, and
  `last_match_result_outcome_name`.
- Seeded the mode `90` smoke proof with a deterministic Red-over-Blue score
  before dispatching live match-result chat. This keeps the proof stable while
  exercising both the winning and losing team phrase paths.

## Scenario Gate

`bot_chat_live_match_result` now requires:

- `reply_chat_match_result_win >= 1`
- `reply_chat_match_result_loss >= 1`
- `reply_chat_match_result_unknown == 0`
- `reply_chat_match_result_tie == 0`
- `reply_chat_match_result_abort == 0`
- `last_match_result_outcome in (1, 2)`
- `last_match_result_outcome_name in (win, loss)`

The last-outcome fields expose the most recently processed bot. They are
order-dependent, so the durable proof is the required split between winning
and losing result counters.

The focused run passed from:

- JSON: `.tmp\bot_scenarios\bot_chat_match_result_outcome.json`
- Markdown: `.tmp\bot_scenarios\bot_chat_match_result_outcome.md`
- Artifacts:
  `.tmp\bot_scenarios\bot_chat_match_result_outcome_artifacts\20260701T072126Z`

Key observed metrics:

- `reply_chat_match_result=4`
- `reply_chat_match_result_win=2`
- `reply_chat_match_result_loss=2`
- `reply_chat_match_result_unknown=0`
- `reply_chat_match_result_tie=0`
- `reply_chat_match_result_abort=0`
- `last_reply_chat_phrase=12223`
- `last_match_result_outcome=2`
- `last_match_result_outcome_name=loss`
- `live_chat_match_result=4`
- `live_chat_failures=0`

## Validation

- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_live_match_result --binary .install\worr_ded_x86_64.exe --install-dir .install --format both --json-out .tmp\bot_scenarios\bot_chat_match_result_outcome.json --markdown-out .tmp\bot_scenarios\bot_chat_match_result_outcome.md --artifact-dir .tmp\bot_scenarios\bot_chat_match_result_outcome_artifacts --timeout 60`

The Meson/Ninja build emitted the existing startup warning
`ninja: warning: premature end of file; recovering` and completed
successfully.
