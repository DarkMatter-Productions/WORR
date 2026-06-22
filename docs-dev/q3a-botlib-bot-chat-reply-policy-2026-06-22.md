# Q3A BotLib Bot Chat Reply Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds the first proof-gated reply selector above the conservative
bot chat dispatch path. The new selector stays behind the smoke-only
`sg_bot_chat_reply_policy_smoke` cvar and runs after the existing initial
profile-chat proof line, mapping staged `bot_chat_personality` metadata to a
deterministic reply utterance for the first team-ready proof event.

Dedicated smoke mode `61` now proves the `bot_chat_reply_policy` scenario. The
row stages four profile-backed TDM bots, enables `sg_bot_allow_chat 1`, enables
the reply smoke gate, and requires one initial dispatch plus one reply dispatch
per staged bot.

## Implementation

- `src/game/sgame/bots/bot_runtime.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and `src/game/sgame/g_local.hpp`
  register and expose the smoke-only `sg_bot_chat_reply_policy_smoke` cvar.
- `src/game/sgame/bots/bot_brain.cpp` records per-spawn reply selection state,
  maps profile personalities into deterministic reply phrase ids, and submits
  selected reply lines through the existing `BotChatPolicy_Dispatch` bridge.
- `src/game/sgame/gameplay/g_svcmds.cpp` extends
  `q3a_bot_chat_policy_status` with reply enablement, event, selection,
  personality, team-ready, submitted, rate-limited, failure, and latest
  client/personality/phrase/event counters.
- `src/server/main.c` reserves mode `61`, stages the same profile-backed TDM
  bot chat lane, and emits `bot_chat_reply_policy=1` in the scenario begin
  marker while keeping the reply selector disabled outside the proof.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `bot_chat_reply_policy` row and
  hard-gate the new status fields.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_reply_policy --install-dir .install --format text`

Focused scenario artifact:
`.tmp\bot_scenarios\20260622T092009Z`.

The focused smoke reported `dispatch_attempts=8`, `dispatch_submitted=8`,
`dispatch_failures=0`, `initial_chat_selections=4`, `reply_chat_enabled=1`,
`reply_chat_events=4`, `reply_chat_selections=4`,
`reply_chat_known_personalities=4`, `reply_chat_unknown_personalities=0`,
`reply_chat_team_ready=4`, `reply_chat_submitted=4`,
`reply_chat_rate_limited=0`, `reply_chat_failures=0`,
`last_reply_chat_phrase=1121`, and `last_reply_chat_event=1`.

## Current Status

The implemented bot scenario catalog now reports 67 rows total: 66 automated
short-run rows plus one manual high-bot degradation-policy row. The raw plan
checklist remains `809/809` checked.

No Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified for this round.
