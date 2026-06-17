# Q3A BotLib Bot Slot Lifecycle

Date: 2026-06-17

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round adds the first WORR-native fake-client lifecycle for bots. Server operators can add, list, and remove a bot through `sg_bot_*` commands, and the server now treats those bot slots as local game participants instead of UDP clients.

The slice deliberately stops at one active bot. A second simultaneous bot exposed a hang in the game begin/session path, so the engine now refuses the second add with a clear message until the next `FR-04-T13` round hardens multi-bot spawning.

Follow-up: `docs-dev/q3a-botlib-multibot-slot-queue-2026-06-17.md` removes this one-active-bot limitation with a deferred add queue and bot team-assignment fix.

## Implementation Notes

- Added `client_t::bot` plus engine helpers for `SV_BotAdd()`, `SV_BotRemove()`, `SV_BotRemoveAll()`, `SV_BotGetClient()`, and `SV_BotList()`.
- `SV_BotAdd()` builds bot userinfo, selects an available public slot, initializes the client/edict state, and calls `ge->ClientConnect(..., true)` followed by `ge->ClientBegin()`.
- Bot clients use a no-op message sink, are not linked as UDP clients, and are skipped by client message sending, async packet sending, target client prints, final server messages, q2proto disconnect writes, and anti-cheat disconnect calls.
- `SV_CountClients()` and server status output now ignore `client_t::bot` so fake clients do not inflate human server population counts.
- `SV_BotRemoveAll()` is called during server shutdown, and explicit remove/kick-all paths drop and free bot slots through the normal server client cleanup path.
- Added server commands:
  - `sg_bot_add [name] [team]`
  - `sg_bot_remove <name|slot|all>`
  - `sg_bot_kick_all`
  - `sg_bot_list`
- Added `sv_bot_slot_smoke` as a dedicated self-smoke:
  - `1`: run once after the first active game frame and leave the server running.
  - `2`: run once and quit after the smoke.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64`
- `meson compile -C builddir-win sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated bot slot self-smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_slot_self_smoke5 +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_slot_smoke 2 +map mm-rage`

Key log evidence:

- `q3a_bot_slot_smoke_after_alpha added=1 count=1 maxclients=8 soft=8`
- `q3a_bot_slot_smoke_after_remove_alpha count=0`
- `q3a_bot_slot_smoke_after_pair added_bravo=1 added_charlie=0 count=1`
- `q3a_bot_slot_smoke_removed_all=1`
- `q3a_bot_slot_smoke_after_remove_all count=0`

## Outstanding Work

- Follow-up complete: the one-active-bot guard was replaced by the deferred multi-bot queue in `docs-dev/q3a-botlib-multibot-slot-queue-2026-06-17.md`.
- Follow-up complete: `sg_bot_min_players` autofill landed in `docs-dev/q3a-botlib-min-players-autofill-2026-06-17.md`.
- Follow-up complete: `sg_bot_reload_profiles` and first deterministic profile loading landed in `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`.
- Add team-limit and mode-change policy cleanup once bot profiles and team assignment are real.
- Connect spawned bot slots to AAS-backed movement and the BotLib command dispatcher.
