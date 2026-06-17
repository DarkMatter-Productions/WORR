# Q3A BotLib Multi-Bot Slot Queue

Date: 2026-06-17

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round removes the one-active-bot limitation from the first fake-client slice. WORR can now add a bot, remove it, add another bot in the reused slot, and defer a second active bot add into the next server frame so two local bot slots can coexist without hanging the game begin path.

## Implementation Notes

- Replaced the one-active-bot guard in `SV_BotAdd()` with a small add queue. If a bot has already been added this server frame and an active bot exists, the request is queued and processed one bot per following server frame.
- `SV_BotProcessAddQueue()` runs before the game frame, clears queued requests on `SV_BotRemoveAll()`, and still routes actual slot creation through the same `SV_BotAddImmediate()` path as direct adds.
- Preserved `SVF_BOT` after `ClientSpawn()` resets entity flags, so later spawn/session logic continues to recognize fake clients as bots.
- Moved bot team assignment ahead of host/owner auto-join handling in `InitPlayerTeam()`. Slot 0 bots were being treated as `host`, landing as spectators, and the first real playing bot then exposed a second-bot begin/spawn reentry hang. Bots now use the non-recursive team assignment path before host logic can intercept them.
- Extended `sv_bot_slot_smoke 2` to validate Alpha add/remove, Bravo add, Charlie deferred add, two-bot active count, and full cleanup.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated multi-slot smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_multislot_self_smoke_clean +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_slot_smoke 2 +map mm-rage`

Key log evidence:

- `Added bot B|Bravo in slot 0.`
- `Queued bot Charlie for the next server frame.`
- `Added bot B|Charlie in slot 1.`
- `q3a_bot_slot_smoke_after_deferred_pair count=2`
- `q3a_bot_slot_smoke_removed_all=2`
- `q3a_bot_slot_smoke_after_remove_all count=0`
- `q3a_bot_slot_smoke=end final_count=0`

## Outstanding Work

- Follow-up complete: `sg_bot_min_players` autofill landed in `docs-dev/q3a-botlib-min-players-autofill-2026-06-17.md`.
- Follow-up complete: `sg_bot_reload_profiles` and first deterministic profile loading landed in `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`.
- Add team-limit and mode-change cleanup once bot profile/team policy is defined.
- Connect spawned bot slots to AAS-backed movement and the BotLib command dispatcher.
