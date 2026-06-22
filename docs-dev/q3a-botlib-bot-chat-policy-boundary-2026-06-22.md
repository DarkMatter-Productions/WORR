# Q3A BotLib Bot Chat Policy Boundary

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

Note: this document records the earlier boundary-only round. Current behavior is
superseded by `docs-dev/q3a-botlib-bot-chat-dispatch-2026-06-22.md`, which adds
the first conservative live dispatch proof behind default-off
`sg_bot_allow_chat`.

## Summary

This round adds a safe, default-off policy boundary for future bot chat without
enabling live bot chatter. WORR now registers `sg_bot_allow_chat` as the public
server-game cvar for this lane, exposes `BOT_CHAT_POLICY_STATUS_API_V1` through
the game extension surface, and emits a compact
`q3a_bot_chat_policy_status` marker for scenario validation.

The status boundary intentionally proves three things separately:

- staged profile chat metadata is preserved on bot userinfo;
- the public `sg_bot_allow_chat` gate can be enabled for diagnostics; and
- live chat consumer/dispatch paths remain disabled until a real chat consumer
  exists.

This keeps profile chat data honest and observable while avoiding accidental
server-visible bot speech from placeholder behavior.

## Implementation

- Added `inc/shared/bot_chat_policy_status.h` with
  `BOT_CHAT_POLICY_STATUS_API_V1` and a `PrintStatus` callback shape for the
  server smoke harness.
- Included the new status ABI from `inc/shared/gameext.h`.
- Registered `sg_bot_allow_chat` in `src/game/sgame/bots/bot_runtime.cpp` with
  a default value of `0`.
- Added `BotChatPolicy_PrintStatus` and the `bot_chat_policy_status` server
  command in `src/game/sgame/gameplay/g_svcmds.cpp`.
- Added the `BOT_CHAT_POLICY_STATUS_API_V1` extension implementation in
  `src/game/sgame/gameplay/g_main.cpp`.
- Added mode `57` / `bot_chat_policy` staging in `src/server/main.c`, including
  the scenario begin marker fields and the status extension call.
- Added the `bot_chat_policy` harness row in
  `tools/bot_scenarios/run_bot_scenarios.py`, plus parser/unit coverage in
  `tools/bot_scenarios/test_run_bot_scenarios.py`.

Mode `57` stages four profile-backed bots in TDM, enables
`sg_bot_allow_chat 1` only for the smoke, and requires:

- `bots=4`;
- `profile_chat_metadata=4`;
- `allow_chat=1`;
- `consumer_ready=0`;
- `dispatch_enabled=0`;
- `dispatch_attempts=0`;
- `blocked_until_consumer=1`;
- `pass=1`.

## Validation

- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  - Passed: 45 tests.
- `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`
  - Passed after switching the status helper to the server-game import
    `gi.Info_ValueForKey`.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed and refreshed `.install` with the current binaries and packaged
    assets.
- Focused scenario:
  `python tools\bot_scenarios\run_bot_scenarios.py --binary .install\worr_ded_x86_64.exe --install-dir .install --game basew --map mm-rage --scenario bot_chat_policy --format both`
  - Passed from `.tmp\bot_scenarios\bot_chat_policy\20260622T081452Z\20260622T071452Z`.
  - Runtime metrics included `frames=246`, `commands=246`,
    `route_commands=246`, and `route_failures=0`.

## Current Status

The scenario catalog now contains 63 implemented rows total: 62 automated
short-run rows plus one manual high-bot degradation-policy row. The default
`--catalog` command omits manual-only rows unless requested, so it reports the
62 automated short-run rows.

Live bot chat remains future work. The next implementation lane should add a
real chat consumer/utterance policy, explicit rate limiting, audience/team
routing, server operator controls, and scenario evidence before enabling any
dispatch.
