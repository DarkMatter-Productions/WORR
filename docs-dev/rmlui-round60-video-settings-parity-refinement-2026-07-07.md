# RmlUi Round 60 Video Settings Parity Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T06`, `FR-09-T09`, `DV-03-T07`,
`DV-07-T02`, `DV-07-T04`

## Summary

Round 60 refines the RmlUi Video Setup page against the pre-RmlUi
`src/game/cgame/ui/worr.json` definition. The page now restores the old menu's
display, anti-aliasing, texture, lightmap, and renderer controls with more
appropriate widgets and a compact three-column layout that stays inside the
active `960x720` canvas.

The pass keeps the menu sound/music metadata active and validates the staged
`pushmenu video` path through the RmlUi runtime.

## Implementation

- Rebuilt `assets/ui/rml/settings/video.rml` from a short two-section starter
  into a three-column settings page:
  - Display: video mode, three-state borderless mode, Multi-Monitor action,
    vertical sync, and anti-aliasing.
  - Textures: texture quality, filter mode including bilinear, anisotropic
    filtering, saturation, and intensity.
  - Gamma/Renderer: texture gamma, hardware gamma, lightmap saturation,
    lightmap brightness, and renderer backend.
- Changed `r_borderless` from a checkbox to the original three-value select:
  Off, Fullscreen, Always.
- Restored missing pre-RmlUi controls for `gl_multisamples`, `r_hwgamma`,
  `gl_anisotropy`, `gl_saturation`, `intensity`,
  `gl_coloredlightmaps`, and `gl_brightness`.
- Restored the legacy `pushmenu multimonitor` action as a real button with
  route-target metadata and an open-sound cue.
- Matched legacy range bounds where the starter page had drifted, including
  `r_gamma` from `0.3` to `1.3` and `gl_brightness` from `0` to `0.3`.
- Extended `assets/ui/rml/common/theme/settings.rcss` with a compact Video
  Setup layout and generic settings action-row button treatment.

## Runtime Evidence

Focused staged OpenGL probes used `.install/worr_x86_64.exe`, `.install` as
basedir, `game basew`, `r_renderer opengl`, and `ui_rml_enable 1`.

- `.install/basew/logs/round60_video_visual_compact.log`
  - `pushmenu video` routed through `ui_rml_runtime_open`.
  - `ui/rml/settings/video.rml` opened successfully.
  - Active status reported `route='video'` and `availability='ready'`.
  - RmlUi loaded Quake II Rerelease TTF faces for display, UI, and mono text.
  - The route consumed `data-menu-sound-open="open"` and
    `data-menu-music="menu"`.
  - Runtime rendered `60` frames at `960x720` with no missing-model, parser,
    or failure hits.
- `.tmp/rmlui/round60-screens/round60_video_compact.png`
  - Visual evidence for the refined three-column Video Setup page at
    `960x720`; all restored controls are visible above the footer.
- `.tmp/rmlui/round60-screens/round60_video_compact.tga`
  - Original staged TGA capture copied from the runtime screenshot path.

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m pytest tools\ui_smoke`

The final full UI smoke run reported `224 passed`.

## Remaining Gaps

- Video settings still depend on the existing cvar binding path; this pass does
  not claim complete live settings persistence/navigation parity.
- Full route-wide keyboard/controller navigation, localization stress,
  automated pixel-clipping assertions, and native Vulkan/RTX-vkpt RmlUi
  rendering remain later parity gates.
