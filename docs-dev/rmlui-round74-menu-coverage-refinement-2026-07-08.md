# RmlUi Round 74 Menu Coverage Refinement

Date: 2026-07-08

Tasks: `FR-09-T08`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 74 continues the menu coverage/refinement work from Round 73. The pass
focused on controller-stub routes that were visually usable but still produced
noisy direct-open evidence or weak fallback layouts:

- expected RmlUi "Could not locate data model" notices made all-route logs
  harder to review even when a route opened correctly;
- direct `ui_list`, `map_selector`, and `tourney_veto` route opens still hid
  useful fallback content when live cvars were absent;
- `servers` and `demos` empty table rows rendered beside the headers instead
  of below them in staged OpenGL captures;
- the server/demo utility copy still read like scaffold instructions rather
  than concise menu state.

This pass does not claim live controller parity. The missing data-model
diagnostic is now suppressed by default for controller-stub route sweeps, but
the underlying live data-model/controller work remains pending.

## Implementation

- `src/client/ui_rml/ui_rml_runtime.cpp`
  - Added `ui_rml_log_missing_data_models`, default `0`.
  - Suppressed exact RmlUi "Could not locate data model" log messages by
    default so all-route sweeps surface parser/CSS/texture/runtime faults
    without expected controller-stub noise.
  - Kept the diagnostic opt-in via `ui_rml_log_missing_data_models 1` for
    future live-controller work.
- `assets/ui/rml/utility/ui_list.rml`
  - Changed list item, extra action, and previous/next page visibility gates
    from strict `=1` to `!=0`.
  - Direct fallback now shows authored item/action labels unless a live cvar
    explicitly disables the item.
- `assets/ui/rml/session/map_selector.rml`
  - Kept the authored title and countdown bar visible for direct route probes.
  - Changed candidate map option show gates from strict `=1` to `!=0`.
- `assets/ui/rml/session/tourney_veto.rml`
  - Kept the inactive fallback panel visible unless a live cvar explicitly
    disables it.
- `assets/ui/rml/utility/servers.rml` and `assets/ui/rml/utility/demos.rml`
  - Tightened menu copy and empty states to concise production-style labels.
- `assets/ui/rml/common/theme/utility.rcss`
  - Added explicit table, row-group, row, and cell display rules for utility
    data tables.
  - Added stable server/demo column widths so empty states sit under their
    headers instead of beside them.
  - Refined `ui_list` action/list styling and empty/status table spacing.
- `assets/ui/rml/common/theme/session.rcss`
  - Added contained map-selector status/countdown styling.
  - Added bounded tournament-veto fallback panel sizing.

## Evidence

Runtime captures are under `.tmp/rmlui/round74-menu-improvements/`.

Representative captures:

- `round74_servers_960x720.png`
- `round74_demos_960x720.png`
- `round74_ui_list_960x720.png`
- `round74_map_selector_960x720.png`
- `round74_tourney_veto_960x720.png`

The all-route staged OpenGL sweep log is:

- `.install/basew/logs/round74_menu_improvements_all_route_open.log`

Results:

- registered routes: `58`
- document probe OK lines: `59`
- opened documents: `59`
- runtime status samples: `58`
- missing data-model notice lines at default settings: `0`
- parser/CSS/texture/runtime error lines: `0`

## Validation

- `meson compile -C builddir-win`
  - Final runtime build passed after the missing-data-model log gate change.
- `python -m pytest tools\ui_smoke -q`
  - `224 passed`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - staged runtime and assets refreshed;
  - RmlUi asset payload validated at `179` package/loose files.
- Focused staged OpenGL route captures:
  - `servers`, `demos`, `ui_list`, `map_selector`, and `tourney_veto` opened
    through `ui_rml_runtime_open` at `960x720`;
  - server/demo empty rows render under stable full-width headers;
  - `ui_list`, `map_selector`, and `tourney_veto` direct fallbacks remain
    visible and contained.
- `git diff --check`
  - clean exit; existing LF/CRLF warnings remain.

## Remaining Gaps

- Live list/save/keybind/player-preview/session data-model controllers are
  still pending.
- `ui_rml_log_missing_data_models 1` still exposes expected controller-stub
  notices until those data models are registered.
- Full keyboard/controller navigation parity is still pending.
- True narrow-viewport runtime capture parity remains pending because the
  staged Windows launch path still reports a `960x720` RmlUi canvas for smaller
  requested geometries.
- Native Vulkan/RTX-vkpt RmlUi rendering remains pending.
