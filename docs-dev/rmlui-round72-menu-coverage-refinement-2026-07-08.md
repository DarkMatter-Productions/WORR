# RmlUi Round 72 Menu Coverage Refinement

Date: 2026-07-08

Tasks: `FR-09-T04`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 72 is a broad menu polish and coverage-gap pass on top of the Round 71
stateful widget skin baseline. It keeps the OpenGL RmlUi path on renderer-safe
SVG assets while improving real menu readability, direct-route fallback
coverage, and scroll containment across representative shell, settings,
single-player, utility, session, and popup routes.

## Implementation

- Reworked text-box, select, and combo SVG skins so real menu text is not drawn
  over decorative placeholder lines or duplicated select arrows.
- Hid the old per-row widget pictograms in settings and utility forms so the
  stateful widget skins carry the UI affordance.
- Removed range stepper arrow children from the shared theme and widened dense
  settings slider tracks, making sliders read as sliders instead of tiny combo
  boxes.
- Added explicit vertical scrollbar dimensions, hidden horizontal scrollbars,
  and kept overflow surfaces vertically contained.
- Reduced main-menu and hub button widths so padding and borders no longer clip
  the right edge of command buttons.
- Loosened fixed-height settings, single-player, and utility surfaces so they
  flex into the active RmlUi screen and scroll vertically when needed.
- Updated reusable list-table and keybind component themes from older blue
  placeholder colors to the current WORR menu palette and widget decorators.
- Fixed direct-route coverage gaps:
  - `callvote_main` no longer opens blank when live session cvars are absent;
    explicit `0` values still hide unavailable live options.
  - `admin_commands` no longer requires a missing data model for its static
    command-reference rows.

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
- Asset/reference checks:
  - `widget_skin_svgs=55`
  - `widget_skin_refs=78`
  - All `decorator: image(...)` references under `assets/ui/rml/common` resolve
    to existing files.
  - All widget SVGs remain at or below `256x256`.
- Diff hygiene:
  - `git diff --check`
  - Result: clean apart from existing repository LF/CRLF warnings.

## Runtime Evidence

Focused staged OpenGL captures were written under
`.tmp\rmlui\round72-menu-improvements\` and logs under
`.install\basew\logs\round72_*.log`.

Representative final or refined captures:

- `round72_main_960x720.png`
- `round72_options_960x720.png`
- `round72_video_refined_960x720.png`
- `round72_sound_refined_960x720.png`
- `round72_startserver_960x720.png`
- `round72_players_960x720.png`
- `round72_addressbook_960x720.png`
- `round72_keys_960x720.png`
- `round72_callvote_main_refined_960x720.png`
- `round72_admin_commands_final_960x720.png`
- `round72_download_status_960x720.png`
- `round72_quit_popup_960x720.png`

Final staged all-route OpenGL sweep:

- log: `.install\basew\logs\round72_menu_improvements_all_route_open.log`
- registered route IDs opened: `58`
- opened documents: `59`
- runtime status samples: `58`
- bad lines matching SVG texture failure, invalid property, syntax error,
  missing texture, unsupported, fallback, failure, error, exception, unhandled,
  parser, or screenshot write failure: `0`

The runtime logs also confirm the RmlUi TTF font faces loaded from Quake II
Rerelease sources: `RussoOne-Regular.ttf`, `Montserrat-Regular.ttf`, and
`RobotoMono-Regular.ttf`.

## Remaining Work

- The staged Windows capture path still reports a `960x720` RmlUi runtime
  canvas when smaller `r_geometry` values are requested, so this pass does not
  claim true narrow-viewport parity.
- Live list/save/keybind/player-preview/session data-model controllers,
  full keyboard/controller navigation parity, automated route-wide pixel
  assertions, native Vulkan/RTX-vkpt RmlUi rendering, and full visual parity
  remain pending.
