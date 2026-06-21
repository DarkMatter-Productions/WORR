# Q3A BotLib Duel Queue Spectator Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

## Summary

This round closes the Phase 7 Duel/tournament bot queue/spectator handling row with a source-backed smoke proof. The existing `sv_bot_team_policy_smoke 2` path still validates the conservative surplus-bot spectator case with `g_allow_duel_queue 0`. A new queue-enabled mode, `sv_bot_team_policy_smoke 3`, enables `g_allow_duel_queue`, adds three bots to a two-player Duel setup, and verifies that the third bot remains spectator-owned while entering the Duel queue.

The work is WORR-native server smoke, game-status extension, scenario harness, and documentation code. No new Q3A or BSPC source files were imported.

## Implementation Notes

- Extended `BOT_TEAM_POLICY_STATUS_API_V1` so the server smoke can pass an expected queued-bot count into the game-side status callback.
- Extended `BotTeamPolicy_PrintStatus()` and the `bot_team_policy_status` developer command to parse and report `expected_queued`.
- Kept `sv_bot_team_policy_smoke 2` as the queue-disabled readiness proof and added `sv_bot_team_policy_smoke 3` as the queue-enabled proof.
- Updated bot initial team assignment so a surplus Duel bot uses the normal queue-capable `SetTeam(..., Team::None, force=false)` path when `g_allow_duel_queue` is enabled, with the previous spectator fallback retained if queueing is unavailable.
- Added the promoted `duel_queue_spectator` scenario. It gates:
  - queue-enabled smoke setup;
  - three accepted bot add requests;
  - pre-cleanup `bots=3`, `playing=2`, `spectators=1`, `queued=1`;
  - `expected_queued=1`;
  - cleanup back to zero bots and zero queued spectators.
- Tightened the existing `team_policy_duel_readiness` scenario so the queue-disabled path also asserts `queued=0`.
- Added `expected_queued` to the team-mode optional-field family so raw diagnostics preserve the new status field.

## Validation

- `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`
- `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario duel_queue_spectator --timeout 120 --base-port 30240 --format text --json-out .tmp\bot_scenarios\duel_queue_spectator.json`
- `python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 30280 --format text --json-out .tmp\bot_scenarios\latest_report.json`
- `git diff --check -- inc/shared/bot_team_policy_status.h src/game/sgame/g_local.hpp src/game/sgame/gameplay/g_svcmds.cpp src/game/sgame/player/p_client.cpp src/server/main.c tools/bot_scenarios/run_bot_scenarios.py tools/bot_scenarios/test_run_bot_scenarios.py tools/bot_scenarios/README.md docs-dev/plans/q3a-botlib-aas-port.md docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md docs-dev/q3a-botlib-aas-credits.md docs-dev/q3a-botlib-duel-queue-spectator-2026-06-21.md`

The latest implemented scenario run reports 38 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.

## Evidence

Expected queue-enabled smoke evidence:

- `q3a_bot_team_policy_smoke=begin queue_enabled=1`
- `q3a_bot_team_policy_status bots=3 playing=2 spectators=1 queued=1 ... expected_playing=2 expected_spectators=1 expected_bots=3 expected_queued=1 pass=1`
- `q3a_bot_team_policy_status bots=0 playing=0 spectators=0 queued=0 ... expected_playing=0 expected_spectators=0 expected_bots=0 expected_queued=0 pass=1`

## Remaining Work

- Duel warmup behavior and map-restart cleanup remain open Phase 7 rows.
- Votes, map queue/mymap, scoreboard classification, intermission, and reconnect cleanup remain open match-tool rows.
