# Q3A BotLib Bot Team Policy Cleanup

Date: 2026-06-17

Tasks: `FR-04-T13`, `DV-07-T06`

## Summary

This round makes fake-client bot placement respect active match limits instead of assigning every bot directly into play. The important fix is for one-on-one modes: a third bot can no longer bypass the normal two-player Duel/Gauntlet-style active-player cap through the bot-only initial team path.

The implementation remains WORR-owned. No imported source or profile assets were added.

## Implementation Notes

- Added a bot-specific initial team assignment helper in `p_client.cpp`.
- Bot initial placement now respects:
  - match lock during countdown or in-progress play;
  - the two-player active limit for `GameFlags::OneVOne` modes;
  - positive `maxplayers` limits for non-duel match modes.
- Surplus bots are moved to spectator/freecam state during initial spawn instead of being placed directly into active play.
- Added `Bot_EnforceMatchTeamPolicy(bool silent)` as a reusable cleanup hook.
- The game frame now runs the policy hook after cvar checks, so lowering `maxplayers` or switching into a tighter mode can move surplus bots out of active play.
- Cleanup preserves active humans first, then keeps bots only while the active match limit still has room.

## Validation

- `meson compile -C builddir-win worr_ded_engine_x86_64 worr_ded_x86_64 sgame_x86_64`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas`
- Dedicated multi-slot regression smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_multislot_after_team_policy +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_slot_smoke 2 +map mm-rage`
- Dedicated min-player regression smoke:
  `.\worr_ded_x86_64.exe +set game basew +set logfile 1 +set logfile_name q3a_bot_minplayers_after_team_policy +set logfile_flush 1 +set developer 1 +set deathmatch 1 +set sg_bot_enable 1 +set sv_bot_min_players_smoke 2 +map mm-rage`

Key regression evidence:

- `q3a_bot_slot_smoke_after_deferred_pair count=2`
- `q3a_bot_slot_smoke=end final_count=0`
- `q3a_bot_min_players_smoke_after_fill count=3 auto=3 humans=0 target=3`
- `q3a_bot_min_players_smoke=end final_count=0`

## Outstanding Work

- Add a direct game-side team-policy smoke that can assert `gclient_t::sess.team` counts without relying on server-only bot list data.
- Add curated profile assets only after source ownership and credits review.
- Connect spawned bot slots to AAS-backed movement and the BotLib command dispatcher.
