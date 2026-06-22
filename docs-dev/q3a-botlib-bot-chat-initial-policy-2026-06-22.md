# Q3A BotLib Bot Chat Initial Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the first conservative Q3A-style initial chat selection above
the existing default-off bot chat dispatch gate. Profile chat personality
metadata now selects deterministic initial proof utterance buckets instead of
always emitting the same static line.

Dedicated smoke mode `60` now proves the `bot_chat_initial_policy` scenario.
The row stages four profile-backed TDM bots, enables `sg_bot_allow_chat 1`, and
requires the initial selector to recognize all staged personalities:
`quiet`, `direct`, `helpful`, and `steady`.

## Implementation

- `src/game/sgame/bots/bot_brain.cpp` maps profile `bot_chat_personality` into
  compact initial-chat personality ids and phrase ids, records bucket counters,
  and dispatches the selected initial proof line through the existing
  `BotChatPolicy_Dispatch` bridge.
- `src/game/sgame/g_local.hpp` exposes the new initial-chat status getters.
- `src/game/sgame/gameplay/g_svcmds.cpp` extends
  `q3a_bot_chat_policy_status` with `initial_chat_selections`,
  `initial_chat_known_personalities`, `initial_chat_unknown_personalities`,
  per-bucket counters, and latest selected client/personality/phrase ids.
- `src/server/main.c` reserves mode `60`, stages the same profile-backed TDM
  bot chat lane, and emits `bot_chat_initial_policy=1` in the scenario begin
  marker.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `bot_chat_initial_policy` row and
  hard-gate the new status fields.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_initial_policy --install-dir .install --format text`

Focused scenario artifact:
`.tmp\bot_scenarios\20260622T085845Z`.

The focused smoke reported `initial_chat_selections=4`,
`initial_chat_known_personalities=4`, `initial_chat_unknown_personalities=0`,
`initial_chat_quiet=1`, `initial_chat_direct=1`,
`initial_chat_helpful=1`, `initial_chat_steady=1`,
`dispatch_submitted=4`, and `dispatch_failures=0`.

## Current Status

The implemented bot scenario catalog now reports 66 rows total: 65 automated
short-run rows plus one manual high-bot degradation-policy row. The raw plan
checklist remains `809/809` checked.

No Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified for this round.
