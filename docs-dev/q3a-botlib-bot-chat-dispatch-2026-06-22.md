# Q3A BotLib Bot Chat Dispatch

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This round turns the previous default-off bot chat policy boundary into the
first conservative live dispatch path. `sg_bot_allow_chat` remains disabled by
default, but when a server explicitly enables it, profile-backed bots with
`bot_chat_personality` metadata can submit one sanitized policy proof line per
spawn.

The initial consumer is intentionally narrow. It proves the live plumbing and
status accounting without importing the full Q3A initial-chat/reply system or
adding random chatter. The dispatch path writes to the dedicated-server log and
broadcasts only to real human clients; it deliberately skips bot client message
queues so dedicated smoke bots cannot accumulate reliable chat messages.

## Implementation

- Reused the existing player chat formatting style from
  `src/game/sgame/commands/command_client.cpp` for bot-originated policy chat,
  then added `BotChatPolicy_Dispatch` as a narrow server-game helper.
- Added dispatch status counters for attempts, submitted messages, failures,
  last dispatching client, and whether the last dispatch was team-scoped.
- Added `BotBrain_ResetChatPolicyState` and reset the per-client chat proof
  guard from `Bot_RuntimeBeginLevel` and `Bot_RuntimeEndLevel`.
- Added a bot-brain consumer that checks `sg_bot_allow_chat`, requires profile
  chat metadata, and submits one message per bot spawn.
- Extended `q3a_bot_chat_policy_status` with
  `dispatch_submitted`, `dispatch_failures`, `last_dispatch_client`, and
  `last_dispatch_team`.
- Updated mode `57` / `bot_chat_policy` to require:
  `consumer_ready=1`, `dispatch_enabled=1`, `dispatch_attempts>=1`,
  `dispatch_submitted>=1`, `dispatch_failures=0`, and
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
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_policy --install-dir .install --format text`
  - Passed from `.tmp\bot_scenarios\20260622T080531Z`.
  - Runtime metrics included `frames=246`, `commands=246`,
    `route_commands=246`, and `route_failures=0`.

## Current Status

Follow-up note: `docs-dev/q3a-botlib-bot-chat-team-policy-2026-06-22.md`
extends this dispatch bridge with the default-off `sg_bot_chat_team_only`
audience proof.

Remaining chat work is richer behavior, not the basic dispatch bridge: Q3A-style
initial chat/reply selection, explicit server-facing rate controls beyond the
current once-per-spawn proof guard, event-triggered utterances, broader audience
policy beyond the current team-only proof, and broader non-smoke validation.
