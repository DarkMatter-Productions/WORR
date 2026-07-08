# RmlUi Round 68 Meter Widget and Crosshair Layout Refinement

Date: 2026-07-07

Primary tasks: `FR-09-T04`, `FR-09-T05`, `FR-09-T06`, `FR-09-T07`,
`FR-09-T09`, `DV-07-T02`, and `DV-07-T04`.

## Summary

Round 68 adds another focused visual/runtime refinement pass over the staged
RmlUi menu stack. Range and progress-style settings now have cvar-driven meter
badges, the Crosshair setup page is laid out as a bounded two-column form, and
the Quit confirmation route continues to use the popup route path with alert
sound and menu music metadata.

SVG widget assets were not introduced in this pass. The current RmlUi renderer
texture path loads through the engine image loader, and there is no active SVG
rasterization path for RmlUi assets yet. The safer implementation path was to
build the new affordances from RmlUi-native elements and RCSS styling so they
load consistently in the staged OpenGL runtime.

## Implementation Notes

- Added `data-meter-cvar` runtime binding support in
  `src/client/ui_rml/ui_rml_runtime.cpp`. Meter elements read
  `data-meter-min` and `data-meter-max`, clamp the live cvar into that range,
  and update their fill width during the same refresh pass as cvar-backed
  labels and controls.
- Added shared `.setting-meter`, `.setting-meter-fill`, and
  `.setting-meter-text` styling in `assets/ui/rml/common/theme/settings.rcss`.
  These keep numeric values readable while giving range/progress controls a
  modern filled badge without relying on unsupported external vector assets.
- Converted value badges on Video, Sound, Screen, Crosshair, Rail Trail, and
  Download Status range/progress rows to use live meter fills plus cvar text.
- Reworked `assets/ui/rml/settings/crosshair.rml` into two bounded columns:
  the original Crosshair controls stay together on the left, while Hit
  Feedback controls stay visible on the right above the footer at the staged
  `960x720` canvas.
- Preserved confirmation-menu routing through the existing RmlUi popup
  function for Quit confirmation. The popup route still consumes alert/open
  sound intent and menu music metadata.

## Validation

Build and staging:

- `meson compile -C builddir-win`
- `python -m pytest tools\ui_smoke -q` (`224 passed`)
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`

Runtime evidence:

- `.install\basew\logs\round68_video_meter_widgets.log`
- `.install\basew\logs\round68_sound_meter_widgets.log`
- `.install\basew\logs\round68_crosshair_meter_widgets_final.log`
- `.install\basew\logs\round68_quit_popup_route.log`

The logs show active OpenGL RmlUi route opens, Quake II Rerelease TTF
font-source markers, menu open/alert sound requests, menu music requests,
popup route dispatch for Quit confirmation, and screenshot writes without
unexpected RmlUi parser, fallback, or unknown-command errors.

Visual evidence:

- `.tmp\rmlui\round68-menu-refine\round68_video_meter_widgets.png`
- `.tmp\rmlui\round68-menu-refine\round68_sound_meter_widgets.png`
- `.tmp\rmlui\round68-menu-refine\round68_crosshair_meter_widgets_final.png`
- `.tmp\rmlui\round68-menu-refine\round68_quit_popup_route.png`

## Remaining Gaps

- Live list, save/load, keybind, player-preview, and session controllers are
  still pending.
- True SVG/vector widget asset support is still pending because the current
  RmlUi texture path does not rasterize SVG assets.
- Native slider thumb/track rendering remains RmlUi-native plus styled value
  badges rather than a full custom widget skin.
- Full keyboard/controller navigation parity, automated route-wide pixel
  clipping assertions, narrow-viewport parity, and native Vulkan/RTX-vkpt
  RmlUi rendering remain pending.
