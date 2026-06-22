# Q3A BotLib Bot Chat Team Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round extends the conservative bot chat dispatch bridge with a default-off
team-only audience policy. `sg_bot_allow_chat` still gates all bot-originated
chat, while the new `sg_bot_chat_team_only` cvar makes the current profile-chat
proof dispatch through the team chat path.

The implementation remains intentionally narrow: bots still emit at most one
sanitized proof line per spawn, and the dispatch bridge still avoids bot
reliable-message queues. This proves audience routing without importing Q3A's
full initial-chat/reply system or enabling ambient chatter.

## Implementation

- Added `sg_bot_chat_team_only`, registered with a default value of `0`.
- Updated the bot-brain chat consumer to pass the team-only flag into
  `BotChatPolicy_Dispatch`.
- Extended `q3a_bot_chat_policy_status` with `team_only`, keeping the existing
  dispatch counters for attempts, submitted messages, failures, last client,
  and last team scope.
- Added dedicated smoke mode `58` / `bot_chat_team_policy`. The scenario sets
  `sg_bot_allow_chat 1` and `sg_bot_chat_team_only 1`, then requires
  `team_only=1`, `dispatch_enabled=1`, `dispatch_submitted>=1`,
  `dispatch_failures=0`, `last_dispatch_team=1`, and
  `blocked_until_consumer=0`.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed: 45 tests.
- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
  - Passed.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
  - Passed. Ninja printed its known `premature end of file; recovering`
    warning after linking.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed `.install`.
- Focused scenario:
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_team_policy --install-dir .install --format text`
  - Passed from `.tmp\bot_scenarios\20260622T080044Z`.
  - Runtime metrics included `frames=246`, `commands=246`,
    `route_commands=246`, and `route_failures=0`.

## Current Status

The catalog now reports 64 implemented rows total, made of 63 automated
short-run rows plus one manual high-bot degradation-policy row. Mode `58` is the
first team-only audience proof for the live bot chat dispatch bridge.

Remaining chat work is richer behavior: Q3A-style initial chat/reply selection,
explicit server-facing rate controls beyond the current once-per-spawn proof
guard, event-triggered utterances, and broader non-smoke validation.
