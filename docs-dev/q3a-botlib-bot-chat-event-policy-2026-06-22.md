# Q3A BotLib Bot Chat Event Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round extends the smoke-only bot chat reply proof from one fixed
team-ready event to a two-event policy pass. The new
`sg_bot_chat_event_policy_smoke` cvar stays validation-only, preserves the mode
`61` single-reply proof, and enables a second route-ready reply event for the
staged profile-backed bots.

Dedicated smoke mode `62` now proves the `bot_chat_event_policy` scenario. The
row stages four profile-backed TDM bots, enables `sg_bot_allow_chat 1`, enables
the event-policy smoke gate, and requires one initial dispatch plus two reply
dispatches per staged bot.

## Implementation

- `src/game/sgame/bots/bot_runtime.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and `src/game/sgame/g_local.hpp`
  register and expose the smoke-only `sg_bot_chat_event_policy_smoke` cvar and
  route-ready reply status getter.
- `src/game/sgame/bots/bot_brain.cpp` now treats reply events as an explicit
  dispatch input, keeps per-event spawn guards, records team-ready and
  route-ready reply counts, and maps the staged chat personalities to
  deterministic phrase ids for both proof events.
- `src/game/sgame/gameplay/g_svcmds.cpp` extends
  `q3a_bot_chat_policy_status` with `reply_chat_route_ready` so the multi-event
  proof can distinguish the new route-ready event from the existing
  team-ready event.
- `src/server/main.c` reserves mode `62`, stages the same profile-backed TDM
  bot chat lane, emits `bot_chat_event_policy=1` in the scenario begin marker,
  and resets the event-policy smoke gate outside the proof.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `bot_chat_event_policy` row and
  hard-gate the new status fields.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_event_policy --install-dir .install --format text`

Focused scenario artifact:
`.tmp\bot_scenarios\20260622T093637Z`.

The focused smoke reported `dispatch_attempts=12`, `dispatch_submitted=12`,
`dispatch_failures=0`, `initial_chat_selections=4`, `reply_chat_enabled=1`,
`reply_chat_events=8`, `reply_chat_selections=8`,
`reply_chat_known_personalities=8`, `reply_chat_unknown_personalities=0`,
`reply_chat_team_ready=4`, `reply_chat_route_ready=4`,
`reply_chat_submitted=8`, `reply_chat_rate_limited=0`,
`reply_chat_failures=0`, `last_reply_chat_phrase=1221`, and
`last_reply_chat_event=2`.

## Current Status

The implemented bot scenario catalog now reports 68 rows total: 67 automated
short-run rows plus one manual high-bot degradation-policy row. The raw plan
checklist remains `809/809` checked.

No Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified for this round.
