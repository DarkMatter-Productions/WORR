# RmlUi Round 65 Shell Hub/Audio Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 65 refines the shell, multiplayer hub, and single-player action surfaces
after the Round 64 session pass. The pass replaces older flat hub button walls
with grouped RmlUi sections, normalizes remaining authored-route button sound
metadata, and keeps the popup confirmation/audio/music contract intact.

## Implementation

- Reworked Options, Game, and Multiplayer into grouped hub sections that mirror
  the original menu intent: player/input, display/feel, audio/network, session,
  browse, save/exit, find, host, and profile actions.
- Added explicit `data-menu-sound` intent to the remaining authored shell,
  settings, single-player, save/load, and download-status buttons.
- Added change-sound hints to Single Player episode/level selection and Start
  Server fields/selects so those typed widgets give legacy menu feedback.
- Kept Main and Game Quit on the `ui.popup` route to `quit_confirm`; Game
  Disconnect remains a direct legacy command because the pre-RmlUi JSON menu
  exposed it directly.
- Tightened shared hub widths in `shell.rcss` after capture review showed the
  first grouped Options/Multiplayer layouts clipped the right column at
  `960x720`.

## Validation

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m pytest tools\ui_smoke -q` (`224 passed`)

Runtime capture evidence:

- `.install\basew\logs\round65_main_audio_final.log`
- `.install\basew\logs\round65_options_hub_final.log`
- `.install\basew\logs\round65_game_hub_final.log`
- `.install\basew\logs\round65_multiplayer_hub_final.log`
- `.install\basew\logs\round65_singleplayer_audio_tga.log`

These logs show active OpenGL RmlUi routes, consumed open-sound and menu-music
metadata, screenshot writes, and Quake II Rerelease TTF font markers.

Visual capture evidence:

- `.tmp\rmlui\round65-hub-capture\round65_main_audio_final.png`
- `.tmp\rmlui\round65-hub-capture\round65_options_hub_final.png`
- `.tmp\rmlui\round65-hub-capture\round65_game_hub_final.png`
- `.tmp\rmlui\round65-hub-capture\round65_multiplayer_hub_final.png`
- `.tmp\rmlui\round65-hub-capture\round65_singleplayer_audio.png`

## Remaining Gaps

- Live settings/list/keybind/player-preview/session controllers remain
  incomplete beyond static command/cvar surfaces and command-published cvars.
- True narrow-viewport capture parity, route-wide automated pixel clipping
  assertions, keyboard/controller navigation parity, localization/text shaping
  completion, and native Vulkan/RTX-vkpt RmlUi rendering remain open.
