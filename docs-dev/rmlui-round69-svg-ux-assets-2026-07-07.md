# RmlUi Round 69 SVG UX Assets

Date: 2026-07-07

Task IDs: `FR-09-T03`, `FR-09-T04`, `FR-09-T06`, `FR-09-T07`,
`FR-09-T09`, `DV-07-T02`, and `DV-07-T04`.

## Summary

Round 69 develops and integrates the first first-party SVG UX asset set for
the RmlUi menu stack. The round adds a conservative OpenGL SVG texture path,
a cohesive icon library, shared icon-button styling, and visible icon
integration across the high-level menu hubs plus the Quit confirmation popup.

Round 70 supersedes the visible command-icon portion of this round: the OpenGL
SVG texture path remains, but the `common/icons/ux` command pictogram set and
`ux-icon-button` route usage were removed in favor of widget-specific SVG
markers under `common/icons/widgets`.

This is intentionally a renderer-owned subset implementation, not a claim of
full SVG specification support. Full RmlUi SVG plugin/LunaSVG integration and
native Vulkan/RTX-vkpt SVG texture upload remain future work.

## Implementation

- Added `32` first-party menu UX icons under
  `assets/ui/rml/common/icons/ux/` plus an asset README that documents the
  supported SVG subset and provenance expectations.
- Added OpenGL RmlUi SVG texture loading in `src/renderer/rmlui_bridge.cpp`.
  The loader handles `svg` `width`/`height`/`viewBox`, `line`, `polyline`,
  `polygon`, `rect`, `circle`, and simple `path` commands using `M`, `L`,
  `H`, `V`, and `Z`.
- Rasterization uses a small local supersampled software pass into
  premultiplied RGBA before handing the pixels to the existing OpenGL
  `GenerateTexture` path.
- Added runtime markers:
  - `RmlUi OpenGL SVG texture generated: source='...' size=...`
  - `RmlUi OpenGL SVG texture failed: source='...'`
- Added shared `ux-icon-button`, `ux-button-icon`, `ux-button-label`,
  `popup-title-row`, and `popup-title-icon` styling in
  `assets/ui/rml/common/theme/base.rcss`.
- Integrated icons into the visible high-level surfaces:
  - `assets/ui/rml/shell/main.rml`
  - `assets/ui/rml/shell/options.rml`
  - `assets/ui/rml/shell/game.rml`
  - `assets/ui/rml/shell/quit_confirm.rml`
  - `assets/ui/rml/multiplayer/multiplayer.rml`
  - `assets/ui/rml/singleplayer/singleplayer.rml`
- Updated reusable command-button templates in
  `assets/ui/rml/common/components/command_button.rml` and
  `assets/ui/rml/common/components/controls.rml` so future authored command
  buttons can opt into the same icon/label layout contract.

## Asset Set

The current SVG UX set contains:

`accessibility`, `addressbook`, `back`, `check`, `close`, `demos`,
`disconnect`, `downloads`, `effects`, `gameflags`, `input`, `keys`,
`language`, `load`, `monitor`, `multiplayer`, `options`, `performance`,
`player`, `quit`, `resume`, `save`, `screen`, `servers`, `singleplayer`,
`sound`, `startserver`, `stats`, `video`, `vote`, `warning`, and `weapons`.

The icons use fixed WORR menu colors for now. Dynamic tinting through
`currentColor`, full SVG paint inheritance, and broader art-direction variants
are deferred until the SVG dependency path is promoted.

## Validation

- Build validation:
  - `meson compile -C builddir-win`
- Static validation:
  - `python -m pytest tools\ui_smoke -q`
  - Result: `224 passed`
- Install refresh:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Diff hygiene:
  - `git diff --check`
  - Result: clean apart from existing repository CRLF warnings.
- Runtime visual validation:
  - `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --install-dir .install --engine-exe .install\worr_x86_64.exe --route-id main --geometry 960x720 --evidence-dir .tmp\rmlui\round69-svg-icons --evidence-id round69_main_svg_icons --screenshot-format tga --format text --timeout 120`
  - Direct staged OpenGL probes for `game`, `options`, `multiplayer`, and the
    `quit_confirm` popup.
- Evidence screenshots:
  - `.tmp\rmlui\round69-svg-icons\round69_main_svg_icons.png`
  - `.tmp\rmlui\round69-svg-icons\round69_game_svg_icons.png`
  - `.tmp\rmlui\round69-svg-icons\round69_options_svg_icons.png`
  - `.tmp\rmlui\round69-svg-icons\round69_multiplayer_svg_icons.png`
  - `.tmp\rmlui\round69-svg-icons\round69_quit_popup_svg_icons.png`
- Evidence logs:
  - `.install\basew\logs\round69_main_svg_icons.log`
  - `.install\basew\logs\round69_game_svg_icons.log`
  - `.install\basew\logs\round69_options_svg_icons.log`
  - `.install\basew\logs\round69_multiplayer_svg_icons.log`
  - `.install\basew\logs\round69_quit_popup_svg_icons.log`

The log scan recorded generated SVG texture markers for all focused captures:
`6` in Main, `16` in Game, `20` in Options, `12` in Multiplayer, and `8` in
the Quit popup capture. No SVG loader failure markers were found.

The `game` focused helper capture wrote the screenshot and SVG markers, but
its generic synthetic close/status phase failed after a route transition back
to Main. The direct staged probe and screenshot evidence were accepted for
the SVG asset/rendering purpose; this does not change the existing broader
input-navigation parity gap.

## Remaining Work

- Promote full SVG dependency support through the RmlUi SVG plugin/LunaSVG
  path once all renderer-family requirements are ready.
- Add native Vulkan and RTX-vkpt RmlUi texture upload/render paths instead of
  relying on the active OpenGL lane.
- Extend icon coverage into deeper settings, utility, save/load, keybind, and
  session surfaces after the high-level hub contract settles.
- Add hover/focus tint variants or dynamic tinting after the renderer supports
  the needed SVG paint semantics.
- Continue broad keyboard/controller navigation, live data-controller, and
  route-wide clipping validation.
