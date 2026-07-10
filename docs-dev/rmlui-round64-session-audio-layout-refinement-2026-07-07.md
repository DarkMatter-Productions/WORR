# RmlUi Round 64 Session Audio/Layout Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T08`, `FR-09-T09`, `DV-07-T04`

## Summary

Round 64 refines the session/match RmlUi menu family on top of the Round 63
utility baseline. The pass normalizes menu music/open-sound metadata, adds
explicit action-intent sounds to session controls, restores live vote command
parity, and tightens the most visible long-list layouts so they remain bounded
above footer actions at the `960x720` reference capture size.

## Implementation

- Added `data-menu-music="menu"` and `data-menu-sound-open="open"` to all
  non-popup session pages. Existing confirmation pages keep
  `data-menu-presentation="popup"`, alert open sounds, and menu music.
- Added explicit `data-menu-sound` intent cues across session buttons:
  `open` for navigation/route-producing commands, `confirm` for join/vote
  commits, `change` for flag toggles, `cancel` for Back/Return/Close, and
  `alert` for dangerous confirmation entry points.
- Restored dynamic sgame command flow for session buttons that need cvar
  publication before opening a RmlUi route. Call Vote, MyMap, Host Info,
  Match Info, Admin, and Forfeit now run their original `worr_*` commands
  instead of being short-circuited by static route targets.
- Restored Admin Replay intent: `Replay Game` opens the tournament replay
  picker command path, leaving `tourney_replay_confirm` as the later popup
  confirmation route after a replay entry is selected.
- Converted `vote_menu.rml` from mock `session.vote_*` events to the live
  `ui_vote_*` cvars and `worr_vote_yes`, `worr_vote_no`, and
  `worr_vote_close` commands from the pre-RmlUi menu.
- Refined `session.rcss` so session hubs use explicit two-column placement,
  while callvote random/flag lists, MyMap flags, Admin Commands, Match Stats,
  and Tournament Map Choices use bounded scrollable panels.
- Removed the `ui-player-name` helper from the fixed Match Stats placeholder
  line because the global accessibility helper compressed that row under the
  current RmlUi sizing model.

## Validation

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m pytest tools\ui_smoke -q` (`224 passed`)

Runtime capture evidence:

- `.install\basew\logs\round64_session_dm_join_pushmenu_layout_final2.log`
- `.install\basew\logs\round64_session_callvote_main_pushmenu_layout_final2.log`
- `.install\basew\logs\round64_session_admin_commands_pushmenu_layout_final3.log`
- `.install\basew\logs\round64_session_match_stats_pushmenu_layout_final4.log`
- `.install\basew\logs\round64_session_forfeit_confirm_pushmenu_layout_final.log`

These logs show staged OpenGL RmlUi runtime activation, `pushmenu` bridge
routing, menu music/open-sound consumption, popup routing for
`forfeit_confirm`, and Quake II Rerelease TTF font markers.

Visual capture evidence:

- `.tmp\rmlui\round64-screens\round64_session_dm_join_pushmenu_layout_final2.png`
- `.tmp\rmlui\round64-screens\round64_session_callvote_main_pushmenu_layout_final2.png`
- `.tmp\rmlui\round64-screens\round64_session_admin_commands_pushmenu_layout_final3.png`
- `.tmp\rmlui\round64-screens\round64_session_match_stats_pushmenu_layout_final4.png`
- `.tmp\rmlui\round64-screens\round64_session_tourney_mapchoices_pushmenu_layout_final3.png`
- `.tmp\rmlui\round64-screens\round64_session_forfeit_confirm_pushmenu_layout_final.png`

## Remaining Gaps

- Live session/list/keybind/player controllers still need final data-model
  parity beyond static fallback and command-published cvars.
- True narrow-viewport capture parity, route-wide automated pixel clipping
  assertions, keyboard/controller navigation parity, localization/text shaping
  completion, and native Vulkan/RTX-vkpt RmlUi rendering remain open.
