# Main Menu Rerelease Layout Alignment (2026-03-23)

Task ID: `FR-03-T10`

## Overview
- Aligned the fixed-layout main menu against the Quake II rerelease menu framing used by the retail reference captures in `refs/Quake2Rerelease-MenuScreenshots/`.
- Fixed the split layout bug where the main-menu heading used a synthesized centered bitmap column while the button stack used explicit fixed positions.
- Updated heading rendering so the main menu header reads like a rerelease page title instead of a small generic section label.

## Reference
- Primary reference capture: `refs/Quake2Rerelease-MenuScreenshots/Screenshot 2025-09-21 163446.png`
- Additional captures in the same folder were used to keep plaque/title/player-name framing consistent with the rerelease options pages.

## Implementation
- `src/game/cgame/ui/ui_menu.cpp`
  - Fixed-layout menus now derive their bitmap-aligned left edge from the actual fixed-position bitmap widgets when present.
  - Non-fixed content in those menus is anchored relative to the fixed bitmap stack, which brings the main-menu heading back above the button group instead of leaving it pinned to the top of the screen.
  - Fixed-layout main-menu bitmaps now compute a shared horizontal offset from the combined extents of the menu buttons, visible spinner column, and fixed Quake II/id plaque assets, then apply that offset consistently at draw/layout/hit-test time so the whole group centers in the UI canvas.
  - Fixed plaque/logo draws now ignore stale fixed X values for the main menu and instead snap directly to the spinner column, with the plaque/logo stack vertically centered as a combined group in the menu canvas.
  - Fixed-layout menus now report scrollable height from the actual content span instead of a forced full-screen height, which removes the bogus right-edge scrollbar from the main menu.
  - `ui_debug 2` now outlines cgame menu widget/plaque/logo rects, matching the legacy menu system's rect-debug behavior closely enough to inspect 2D extents during layout work.
- `src/game/cgame/ui/ui_widgets.cpp`
  - `HeadingWidget` now draws as a title/section header with the underline below the text across the content width.
  - Added optional heading text sizing/color support so the main menu can match the rerelease title treatment without changing every heading in the UI.
  - Focused bitmap widgets now draw the animated cursor on the same visible spinner column used by the fixed main-menu plaque stack, so the spinner touches the selected row on the right and the Quake II/id banners on the left.
- `src/game/cgame/ui/ui_json.cpp`
  - Wired generic `textSize` and `textColor` item fields into `HeadingWidget`.
- `src/game/cgame/ui/worr.json`
  - Main menu plaque/logo rects are reduced to half of their prior draw size (`32x141` and `32x32`), while code continues to control the final grouped placement.
  - Tuned the main menu `Game` heading to use a rerelease-like title size/color while keeping the rest of the fixed button stack intact.

## Verification
- Built `cgame_x86_64` successfully with `meson compile -C builddir-win cgame_x86_64`.
