# RmlUi Round 71 Stateful Widget SVG Skins

Date: 2026-07-08

Tasks: `FR-09-T04`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 71 turns the Round 70 widget SVG work from type markers into actual
state-aware control surfaces. The shared RmlUi theme now uses SVG decorators
for buttons, primary/destructive buttons, text boxes, combo/select boxes,
drop-down panels/options, checkboxes/toggles, range tracks/thumbs, progress
tracks/fills, scrollbar tracks/thumbs, arrow boxes, and confirmation popup
frames.

The new assets are stored under
`assets/ui/rml/common/skins/widgets/`. They intentionally stay inside the
current OpenGL bridge SVG subset: rect, line, polyline, polygon, path, circle,
flat fill/stroke colors, and opacity. This keeps them compatible with the
existing renderer-side SVG texture path without depending on browser-only SVG
features or an unregistered RmlUi SVG decorator plugin.

## Implementation

- Added `55` SVG skin assets and a README under
  `assets/ui/rml/common/skins/widgets/`.
- Added shared `decorator: image(...)` state rules in
  `assets/ui/rml/common/theme/base.rcss` for:
  - normal, hover, focus, active, disabled button states;
  - primary and destructive command states;
  - text/select/checkbox/range/progress control surfaces;
  - RmlUi internal `selectarrow`, `selectbox`, `slidertrack`,
    `sliderbar`, `sliderprogress`, `progress fill`, and scrollbar elements;
  - confirmation popup frames.
- Added settings-specific skin overrides in
  `assets/ui/rml/common/theme/settings.rcss` so `data-control="field"`,
  `data-control="combo"`, and `data-control="imagevalues"` rows use the
  richer text-box and combo-box skins.
- Updated `assets/ui/rml/common/components/controls.rcss` so reusable control
  templates inherit the new button, select, toggle, and slider skins.
- Kept existing color/border declarations as fallback styling behind the SVG
  decorators.

## Validation

- Build:
  - `meson compile -C builddir-win`
  - Result: no work to do, success.
- Static smoke:
  - `python -m pytest tools\ui_smoke -q`
  - Result: `224 passed`.
- Install refresh:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Result: staged install validated; RmlUi asset payload is `179`
    package/loose files.
- Diff hygiene:
  - `git diff --check`
  - Result: clean apart from the existing repository CRLF warnings.
- Asset/reference checks:
  - `widget_skin_svgs=55`
  - `widget_skin_refs=74`

## Runtime Evidence

Focused staged OpenGL captures:

- `.install\basew\logs\round71_video_stateful_skins.log`
- `.install\basew\logs\round71_sound_stateful_skins.log`
- `.install\basew\logs\round71_startserver_stateful_skins.log`
- `.install\basew\logs\round71_download_status_stateful_skins.log`
- `.install\basew\logs\round71_quit_popup_stateful_skins.log`

Each focused log shows active RmlUi route status, TGA screenshot writes, and
SVG texture generation from `ui/rml/common/skins/widgets/` without parser,
texture-load, or SVG loader failure markers.

Visual evidence:

- `.tmp\rmlui\round71-widget-skins\round71_video_stateful_skins.png`
- `.tmp\rmlui\round71-widget-skins\round71_sound_stateful_skins.png`
- `.tmp\rmlui\round71-widget-skins\round71_startserver_stateful_skins.png`
- `.tmp\rmlui\round71-widget-skins\round71_download_status_stateful_skins.png`
- `.tmp\rmlui\round71-widget-skins\round71_quit_popup_stateful_skins.png`

Final staged all-route OpenGL sweep:

- log: `.install\basew\logs\round71_stateful_skins_all_route_open.log`
- opened documents: `59`
- unique route IDs: `58`
- runtime status samples: `58`
- bad lines matching SVG texture failure, invalid property, syntax error,
  missing texture, unsupported, fallback, failure, error, exception, unhandled,
  parser, or screenshot write failure: `0`

## Remaining Work

- Hover/focus/disabled visuals are now styled, but route-wide automated pixel
  assertions for every state remain future work.
- Native Vulkan/RTX-vkpt RmlUi rendering still needs its own renderer-owned
  SVG/image path; this pass does not redirect those lanes to OpenGL.
- Keyboard/controller navigation parity and true narrow-viewport capture
  parity remain broader migration tasks.
