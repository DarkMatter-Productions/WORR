# RmlUi Round 61 Settings Audio And Action-Row Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T06`, `FR-09-T09`, `DV-03-T07`,
`DV-07-T02`, `DV-07-T04`

## Summary

Round 61 makes the settings menu family more consistent with the refined
RmlUi shell. Every settings page now requests the established menu music and
open-sound cues when opened, and the remaining settings-to-settings actions
inside Screen Setup and Effects Setup now render as typed action rows instead
of loose buttons.

The pass also fixes the visual containment issues that appeared during QA:
Screen Setup and Effects Setup now use compact two-column layouts so their
lower rows no longer slip behind the footer at `960x720`.

## Implementation

- Added `data-menu-music="menu"` and `data-menu-sound-open="open"` to the
  remaining settings pages:
  - `accessibility`
  - `crosshair`
  - `effects`
  - `input`
  - `language`
  - `multimonitor`
  - `performance`
  - `railtrail`
  - `screen`
- Converted Screen Setup's Crosshair navigation into a `data-control="action"`
  row with an explicit open-sound cue.
- Converted Effects Setup's Railgun Trail navigation into a
  `data-control="action"` row with an explicit open-sound cue.
- Reworked Screen Setup into two columns:
  - HUD controls on the left.
  - Console opacity and scale controls on the right.
- Reworked Effects Setup into two columns:
  - Rendering effects on the left.
  - Gameplay effects and Railgun Trail action on the right.
- Extended `assets/ui/rml/common/theme/settings.rcss` with shared compact
  two-column rules for Screen Setup and Effects Setup.

## Runtime Evidence

Focused staged OpenGL probes used `.install/worr_x86_64.exe`, `.install` as
basedir, `game basew`, `r_renderer opengl`, and `ui_rml_enable 1`.

- `.install/basew/logs/round61_settings_audio_actionrows_final.log`
  - `pushmenu screen` routed through `ui_rml_runtime_open`.
  - `ui/rml/settings/screen.rml` opened successfully.
  - `screen` requested menu open sound `open` and menu music cue `menu`.
  - `pushmenu effects` routed through `ui_rml_runtime_open`.
  - `ui/rml/settings/effects.rml` opened successfully.
  - `effects` requested menu open sound `open` and menu music cue `menu`.
  - RmlUi loaded Quake II Rerelease TTF faces for display, UI, and mono text.
  - Both routes reported active runtime status and rendered at `960x720`.
- `.tmp/rmlui/round61-screens/round61_screen_actionrow_final.png`
  - Visual evidence for the two-column Screen Setup layout with all rows above
    Back/Close at `960x720`.
- `.tmp/rmlui/round61-screens/round61_effects_actionrow_final.png`
  - Visual evidence for the two-column Effects Setup layout with all rows above
    Back/Close at `960x720`.

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m pytest tools\ui_smoke`

The final full UI smoke run reported `224 passed`.

## Remaining Gaps

- This pass does not claim live settings persistence, full keyboard/controller
  navigation parity, localization stress coverage, or automated route-wide
  pixel clipping assertions.
- Native Vulkan/RTX-vkpt RmlUi rendering remains pending and must be completed
  natively.
