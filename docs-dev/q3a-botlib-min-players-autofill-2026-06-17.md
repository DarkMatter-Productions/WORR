# Q3A BotLib Min-Players Autofill

Date: 2026-06-17

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round adds the first automatic local bot population policy. When `sg_bot_enable` is enabled and `sg_bot_min_players` is greater than the current human plus manual-bot population, the server creates auto-managed fake clients until the target is satisfied. When the target drops or bot support is disabled, only auto-managed bots are removed.

## Implementation Notes

- Added `client_t::bot_autofill` so the server can distinguish manually added bots from min-player bots.
- Registered server-owned `sg_bot_enable` and `sg_bot_min_players` cvar handles for dedicated-server population maintenance. The cvars remain global and shared with the existing sgame/runtime registrations.
- Added `SV_BotMaintainMinPlayers()` in the server frame after queued manual bot adds and before the game frame.
- Clamped the min-player target to public client slots, matching the fake-client allocator and avoiding repeated attempts to fill reserved/private slots.
- Counted humans with the bot-aware `SV_CountClients()` path and treated manual bots as satisfying the min-player target without making them removable by the autofill policy.
- Added prefix-aware generated-name scanning so default bots advance through `B|bot1`, `B|bot2`, `B|bot3` even though the game-side bot prefix mutates connecting userinfo names.
- Added `sv_bot_min_players_smoke 2`, which fills to three auto bots, lowers the target to one, disables bot support, and exits after the auto-managed population reaches zero.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated min-player smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_min_players_self_smoke +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_min_players_smoke 2 +map mm-rage`
- Dedicated multi-slot regression smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_multislot_after_minplayers +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_slot_smoke 2 +map mm-rage`

Key min-player log evidence:

- `q3a_bot_min_players_smoke=begin target=3`
- `Added bot B|bot1 in slot 0.`
- `Added bot B|bot2 in slot 1.`
- `Added bot B|bot3 in slot 2.`
- `q3a_bot_min_players_smoke_after_fill count=3 auto=3 humans=0 target=3`
- `q3a_bot_min_players_smoke_after_trim count=1 auto=1 target=1`
- `q3a_bot_min_players_smoke_after_disable count=0 auto=0 enabled=0`
- `q3a_bot_min_players_smoke=end final_count=0`

Key regression evidence:

- `Queued bot Charlie for the next server frame.`
- `Added bot B|Charlie in slot 1.`
- `q3a_bot_slot_smoke_after_deferred_pair count=2`
- `q3a_bot_slot_smoke_after_remove_all count=0`

## Outstanding Work

- Follow-up complete: `sg_bot_reload_profiles` and first deterministic profile loading landed in `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`.
- Add team-limit and mode-change cleanup once bot profile/team policy is defined.
- Connect spawned bot slots to AAS-backed movement and the BotLib command dispatcher.
