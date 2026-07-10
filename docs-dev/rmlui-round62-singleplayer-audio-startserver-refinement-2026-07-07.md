# RmlUi Round 62 Single-Player Audio And Start Server Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T06`, `FR-09-T09`, `DV-03-T07`,
`DV-07-T02`, `DV-07-T04`

## Summary

Round 62 brings the single-player/local-session RmlUi pages into the same
audio contract as the refined shell and settings pages. Skill Select,
Load Game, Save Game, Deathmatch Flags, and Start Server now request menu
music and open-sound cues when opened.

The pass also refines Start Server into a three-column layout that preserves
the pre-RmlUi menu content while keeping every static row visible above the
footer at `960x720`.

## Implementation

- Added `data-menu-music="menu"` and `data-menu-sound-open="open"` to:
  - `skill_select`
  - `loadgame`
  - `savegame`
  - `gameflags`
  - `startserver`
- Added explicit `confirm` action sounds to the Skill Select difficulty
  choices.
- Added explicit action sounds to Start Server:
  - `open` for Deathmatch Flags.
  - `confirm` for Begin Game.
- Reworked `assets/ui/rml/singleplayer/startserver.rml` into three columns:
  - Server and start actions.
  - Match setup identity: format, gametype, modifier, max players.
  - Rules: match length, match type, best-of, time limit, frag limit, and coop
    players.
- Updated `assets/ui/rml/common/theme/singleplayer.rcss` with compact
  three-column Start Server sizing.

## Runtime Evidence

Focused staged OpenGL probes used `.install/worr_x86_64.exe`, `.install` as
basedir, `game basew`, `r_renderer opengl`, and `ui_rml_enable 1`.

- `.install/basew/logs/round62_singleplayer_audio_actions_final3.log`
  - `pushmenu skill_select` routed through `ui_rml_runtime_open`.
  - `ui/rml/singleplayer/skill_select.rml` opened successfully.
  - `skill_select` requested menu open sound `open` and menu music cue `menu`.
  - `pushmenu startserver` routed through `ui_rml_runtime_open`.
  - `ui/rml/singleplayer/startserver.rml` opened successfully.
  - `startserver` requested menu open sound `open` and menu music cue `menu`.
  - RmlUi loaded Quake II Rerelease TTF faces for display, UI, and mono text.
  - Both routes reported active runtime status and rendered at `960x720`.
- `.tmp/rmlui/round62-screens/round62_skill_select_audio_final3.png`
  - Visual evidence for Skill Select with contained action buttons and footer
    controls.
- `.tmp/rmlui/round62-screens/round62_startserver_audio_final3.png`
  - Visual evidence for the three-column Start Server layout with all static
    Server, Match Setup, Rules, and footer controls visible at `960x720`.

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m pytest tools\ui_smoke`

The final full UI smoke run reported `224 passed`.

## Remaining Gaps

- This pass does not claim live condition evaluation for hiding deathmatch-only
  or coop-only Start Server rows; the three-column layout intentionally remains
  readable with the static fallback content visible.
- Live settings persistence, full keyboard/controller navigation parity,
  localization stress coverage, automated route-wide pixel clipping assertions,
  and native Vulkan/RTX-vkpt RmlUi rendering remain later gates.
