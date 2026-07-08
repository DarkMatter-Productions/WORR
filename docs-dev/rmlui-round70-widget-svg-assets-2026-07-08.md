# RmlUi Round 70 Widget SVG Assets

Date: 2026-07-08

Task IDs: `FR-09-T03`, `FR-09-T04`, `FR-09-T06`, `FR-09-T07`,
`FR-09-T09`, `DV-07-T02`, and `DV-07-T04`.

## Summary

Round 70 replaces the previous high-level menu-command SVG pictogram pass with
a widget-focused SVG asset system. The OpenGL SVG texture path from Round 69
is retained, but the authored SVG usage now clarifies control type in settings
and utility widgets instead of decorating navigation buttons.

The old `assets/ui/rml/common/icons/ux/` command-icon directory was removed.
The new widget library lives under `assets/ui/rml/common/icons/widgets/`.

## Implementation

- Added `18` first-party widget SVG assets:
  `action`, `check`, `checkbox`, `combo`, `field`, `imagevalues`, `meter`,
  `number`, `progress`, `radio`, `range`, `select`, `stepper`, `text`,
  `toggle`, `toggle-off`, `value`, and `warning`.
- Added `assets/ui/rml/common/icons/widgets/README.md` documenting the
  supported SVG subset and the purpose of the widget asset set.
- Removed visible menu-command icon markup from Main, Options, Game,
  Multiplayer, Single Player, and Quit confirmation routes.
- Removed `ux-icon-button`, `ux-button-icon`, `ux-button-label`, and popup
  title-icon styling from `assets/ui/rml/common/theme/base.rcss`.
- Restored shared command-button templates to plain text buttons so they no
  longer require an `icon` placeholder.
- Added a compact `setting-widget-icon` marker slot to the settings theme and
  mapped authored `data-control` rows to widget SVGs:
  - `toggle` -> `toggle.svg`
  - `select` -> `select.svg`
  - `combo` -> `combo.svg`
  - `imagevalues` -> `imagevalues.svg`
  - `range` -> `range.svg`
  - `field` -> `field.svg`
  - numeric `field` rows -> `number.svg`
  - `action` -> `action.svg`
  - `progress` -> `progress.svg`
- Added utility-form marker styling and markers for Player Setup and Address
  Book fields/selects.
- Tuned compact Video and Start Server settings widths so widget markers do
  not force awkward control or label clipping at `960x720`.

The current authored integration covers `130` widget marker instances.

## Validation

- Build validation:
  - `meson compile -C builddir-win`
- Static validation:
  - `python -m pytest tools\ui_smoke -q`
  - Result: `224 passed`
- Install refresh:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Result: `Validated RmlUi asset payload: 123 package/loose file(s)`
- Diff hygiene:
  - `git diff --check`
  - Result: clean apart from existing repository CRLF warnings.
- Asset/reference checks:
  - `widget_svgs=18`
  - `widget_markers=130`
  - No remaining `common/icons/ux`, `ux-icon-button`, `ux-button-icon`, or
    `popup-title-icon` references in authored RML/RCSS.
- Runtime/visual validation:
  - `.install\basew\logs\round70_video_widget_svg_final2.log`
  - `.install\basew\logs\round70_sound_widget_svg.log`
  - `.install\basew\logs\round70_startserver_widget_svg_final.log`
  - `.install\basew\logs\round70_players_widget_svg.log`
  - `.install\basew\logs\round70_addressbook_widget_svg.log`
  - `.install\basew\logs\round70_download_status_widget_svg.log`
  - `.install\basew\logs\round70_main_plain_no_menu_icons.log`

The focused log scan recorded SVG texture generation markers without SVG
loader failure markers. The Main-menu validation recorded `0` SVG textures,
confirming that the previous command pictograms are no longer loaded there.

## Evidence

- `.tmp\rmlui\round70-widget-svg\round70_video_widget_svg_final2.png`
  confirms compact Video widgets render select/action/toggle/range markers
  without clipping the three-column layout.
- `.tmp\rmlui\round70-widget-svg\round70_sound_widget_svg.png` confirms Sound
  widgets render select/range/toggle/number markers in the two-column layout.
- `.tmp\rmlui\round70-widget-svg\round70_startserver_widget_svg_final.png`
  confirms Start Server widgets render combo/select/field/number markers in
  the three-column setup layout.
- `.tmp\rmlui\round70-widget-svg\round70_players_widget_svg.png` confirms
  utility Player Setup fields/selects use the quieter utility marker style.
- `.tmp\rmlui\round70-widget-svg\round70_addressbook_widget_svg.png` confirms
  Address Book fields use utility markers without crowding the 16-field grid.
- `.tmp\rmlui\round70-widget-svg\round70_download_status_widget_svg.png`
  confirms the progress widget marker renders on Downloading Content.
- `.tmp\rmlui\round70-widget-svg\round70_main_plain_no_menu_icons.png`
  confirms Main menu command buttons are plain text again.

## Remaining Work

- Widget SVGs are still static type markers; dynamic tinting/state-specific
  image swaps are deferred until the SVG paint/state path is promoted.
- The implementation still uses WORR's supported SVG subset, not full SVG
  specification/plugin parity.
- Native Vulkan/RTX-vkpt RmlUi SVG texture upload remains pending.
- Broader keyboard/controller navigation and route-wide clipping assertions
  remain pending.
