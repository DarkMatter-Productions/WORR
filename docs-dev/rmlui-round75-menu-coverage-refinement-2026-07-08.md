# RmlUi Round 75 Menu Coverage Refinement

Date: 2026-07-08

Tasks: `FR-09-T08`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 75 continues the controller-stub menu coverage work from Round 74. This
pass focused on direct-open report/list routes that were technically loading
but still had weak empty-state coverage:

- `match_stats` opened a contained report panel but could appear empty when
  no live `ui_matchstats_line_*` cvars were populated;
- `tourney_mapchoices` needed the same direct-open fallback treatment while
  preserving its live line-based cvar contract;
- `download_status` needed a clean idle state and explicit percent unit in the
  progress meter;
- the condition inventory checker did not yet accept the leading `!cvar`
  condition syntax already supported by the runtime condition evaluator.

This pass does not claim live session, tournament, or download controller
parity. It improves fallback coverage, log evidence quality, and validation
coverage for the existing RmlUi route documents.

## Implementation

- `assets/ui/rml/session/match_stats.rml`
  - Added a static fallback report block shown only when
    `ui_matchstats_line_0` is absent or falsey.
  - Preserved all live `ui_matchstats_line_*` bindings and visibility gates so
    sgame-published match stats still own the live report content.
- `assets/ui/rml/session/tourney_mapchoices.rml`
  - Added a static two-line fallback block shown only when
    `ui_tourney_mapchoice_line_0` is absent or falsey.
  - Preserved the existing live `ui_tourney_mapchoice_line_*` bindings and
    visibility gates for the tournament map-order stream.
- `assets/ui/rml/shell/download_status.rml`
  - Added a visible idle row for `ui_download_active=0`.
  - Changed the progress meter text so the bound numeric value always carries
    a visible `%` unit.
- `assets/ui/rml/common/theme/session.rcss`
  - Added shared report/list panel sizing for match stats and tournament map
    choices.
  - Added compact report-row styling with wrapping text and bounded widths.
- `assets/ui/rml/common/theme/shell.rcss`
  - Added idle-state styling for Download Status.
- `tools/ui_smoke/check_rmlui_condition_inventory.py`
  - Updated the static condition grammar to accept leading `!cvar` terms,
    matching the compiled runtime evaluator.
- `tools/ui_smoke/test_check_rmlui_condition_inventory.py`
  - Added explicit smoke coverage for negated static condition expressions.

## Evidence

Runtime captures are under `.tmp/rmlui/round75-menu-improvements/`.

Representative captures:

- `round75_match_stats_960x720.png`
- `round75_tourney_mapchoices_960x720.png`
- `round75_download_status_960x720.png`

The all-route staged OpenGL sweep log is:

- `.tmp/rmlui/round75-menu-improvements/round75_menu_improvements_all_route_open.log`

Results:

- registered routes: `58`
- document probe OK lines: `59`
- opened documents: `59`
- runtime status samples: `58`
- missing data-model notice lines at default settings: `0`
- parser/CSS/texture/runtime error lines: `0`

## Validation

- `meson compile -C builddir-win`
  - Final runtime build passed.
- `python -m pytest tools\ui_smoke -q`
  - `225 passed`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - staged runtime and assets refreshed;
  - RmlUi asset payload validated at `179` package/loose files.
- Focused staged OpenGL route captures:
  - `match_stats`, `tourney_mapchoices`, and `download_status` opened through
    `ui_rml_runtime_open` at `960x720`;
  - report/list fallback rows remain visible and contained;
  - Download Status shows the idle state and `0%` meter text.
- All-route staged OpenGL route sweep:
  - opened the previous 58-route registry set, including
    `core.runtime_smoke`;
  - produced no missing data-model notices at default logging settings;
  - produced no parser/CSS/texture/runtime error lines.
- `git diff --check`
  - clean exit; existing LF/CRLF warnings remain.

## Remaining Gaps

- Live list/save/keybind/player-preview/session data-model controllers are
  still pending.
- Live match stats, tournament map choices, and download progress behavior
  still need controller-backed parity evidence beyond direct-open fallbacks.
- Full keyboard/controller navigation parity is still pending.
- True narrow-viewport runtime capture parity remains pending because the
  staged Windows launch path still reports a `960x720` RmlUi canvas for smaller
  requested geometries.
- Native Vulkan/RTX-vkpt RmlUi rendering remains pending.
