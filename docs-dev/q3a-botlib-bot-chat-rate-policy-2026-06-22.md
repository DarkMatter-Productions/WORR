# Q3A BotLib Bot Chat Rate Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round adds a conservative global rate-control gate for the current
profile-backed bot chat proof. The new public cvar
`sg_bot_chat_min_interval_ms` is default-off. When set above zero, the first
eligible bot chat dispatch may submit normally, while later bot attempts inside
the configured interval are counted as rate-limited rather than dispatch
failures.

Dedicated smoke mode `59` now proves the policy with the
`bot_chat_rate_policy` scenario. The row stages four profile-backed TDM bots,
enables `sg_bot_allow_chat 1`, sets `sg_bot_chat_min_interval_ms 60000`, and
requires `dispatch_attempts>=4`, `dispatch_submitted>=1`,
`dispatch_rate_limited>=1`, `dispatch_failures=0`, and
`rate_limit_ms=60000`.

## Implementation

- `src/game/sgame/bots/bot_runtime.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and `src/game/sgame/g_local.hpp`
  register and expose the default-off `sg_bot_chat_min_interval_ms` cvar.
- `src/game/sgame/commands/command_client.cpp` now records rate-limited bot chat
  dispatch attempts, tracks the last submitted dispatch time, exposes
  `BotChatPolicy_DispatchRateLimited`,
  `BotChatPolicy_RateLimitMilliseconds`, and
  `BotChatPolicy_LastDispatchTimeMilliseconds`, and keeps rate-limit rejections
  out of the failure counter.
- `src/game/sgame/bots/bot_brain.cpp` treats a rate-limited chat attempt as a
  handled once-per-spawn proof so blocked bots do not repeatedly retry inside
  the same spawn.
- `src/game/sgame/gameplay/g_svcmds.cpp` extends
  `q3a_bot_chat_policy_status` with `dispatch_rate_limited`,
  `rate_limit_ms`, and `last_dispatch_time_ms`.
- `src/server/main.c` reserves mode `59`, stages
  `sg_bot_chat_min_interval_ms 60000`, prints begin-marker evidence, and resets
  the cvar with the other bot behavior proof gates.
- `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `bot_chat_rate_policy` row and
  marker gates.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_rate_policy --install-dir .install --format text`

Focused scenario artifact:
`.tmp\bot_scenarios\20260622T081428Z`.

The focused smoke reported `dispatch_attempts=4`, `dispatch_submitted=1`,
`dispatch_rate_limited=3`, `dispatch_failures=0`, `rate_limit_ms=60000`,
`team_only=0`, and `pass=1`.

## Current Status

The implemented bot scenario catalog now reports 65 rows total: 64 automated
short-run rows plus one manual high-bot degradation-policy row. The raw plan
checklist remains `809/809` checked.

No Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source files
were imported or modified for this round.
