# RmlUi Round 73 Menu Coverage Refinement

Date: 2026-07-08

Tasks: `FR-09-T08`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 73 is a follow-up menu polish and coverage-gap pass on top of the Round
72 menu containment baseline. The work focused on real runtime behavior that
was still visible in staged OpenGL captures:

- direct session route opens could erase authored fallback labels when
  `data-label-cvar` or `data-bind` cvars existed but were empty;
- `callvote_main`, `join`, and `dm_join` needed stable two-column command grids
  without drawing long action lists past the footer;
- `admin_commands` needed more readable command/usage separation;
- save/load slots still depended on brittle absolute slot coordinates;
- the Start Server initial map fallback still exposed the legacy
  `$$com_maplist` token instead of a usable visible value.

This pass does not claim live controller parity. Missing RmlUi data-model
notices remain expected for controller-stub routes until live C++ data models
and list/session controllers are registered.

## Implementation

- `src/client/ui_rml/ui_rml_runtime.cpp`
  - Added `UI_Rml_CvarHasDisplayText()`.
  - Changed text binding refresh for `data-bind-cvar`, `data-label-cvar`, and
    `data-bind="cvars.*"` so empty existing cvars no longer overwrite authored
    fallback text. This keeps direct route probes readable while still allowing
    non-empty live cvars to replace labels/text.
- `assets/ui/rml/common/theme/session.rcss`
  - Restored deterministic absolute two-column grids for `callvote_main`,
    `join`, and `dm_join`, with wider row pitch and bounded route height.
  - Reworked `admin_commands` rows so the command name, description, and usage
    read as a compact two-line command reference rather than crowded columns.
  - Hid the decorative lobby community card in compact lobby layouts so the
    real command surface and footer controls stay visible.
- `assets/ui/rml/session/join.rml` and `assets/ui/rml/session/dm_join.rml`
  - Switched session show gates from strict `=1` to `!=0` where direct-route
    fallback should show unless a live cvar explicitly disables the action.
  - Kept authored fallback labels readable through the runtime text-binding
    fallback.
- `assets/ui/rml/session/admin_menu.rml`
  - Cleaned the legacy title text.
  - Kept Replay Game on the popup-confirmation route via `ui.popup` with
    `tourney_replay_confirm`.
- `assets/ui/rml/session/vote_menu.rml`
  - Made direct fallback voting actions visible unless `ui_vote_can_vote` is
    explicitly `0`.
- `assets/ui/rml/common/theme/singleplayer.rcss`
  - Converted load/save slots from fixed absolute coordinates to a wrapping,
    scroll-contained list surface.
- `assets/ui/rml/singleplayer/startserver.rml`
  - Replaced the visible fallback `$$com_maplist` option with
    `q2dm1 - The Edge` while preserving `data-source-list="$$com_maplist"` for
    the future map-list bridge.

## Evidence

Runtime captures are under `.tmp/rmlui/round73-menu-improvements/`.

Representative accepted captures:

- `round73_callvote_main_final3_960x720.png`
- `round73_dm_join_final5_960x720.png`
- `round73_join_final2_960x720.png`
- `round73_admin_commands_final_960x720.png`
- `round73_startserver_final_960x720.png`
- `round73_loadgame_final_960x720.png`
- `round73_savegame_final_960x720.png`
- `round73_vote_menu_960x720.png`

The all-route staged OpenGL sweep log is:

- `.install/basew/logs/round73_menu_improvements_all_route_open.log`

Results:

- registered routes: `58`
- document probe OK lines: `59`
- opened documents: `59`
- runtime status samples: `58`
- parser/CSS/texture/runtime error lines, excluding expected missing
  data-model notices: `0`

## Validation

- `meson compile -C builddir-win`
  - Final run: no work to do after the runtime text-binding build passed.
- `python -m pytest tools\ui_smoke -q`
  - `224 passed`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - staged runtime and assets refreshed;
  - RmlUi asset payload validated at `179` package/loose files.
- Decorator asset reference check:
  - `decorator_image_refs=78`
  - `missing_refs=0`
- `git diff --check`
  - clean exit; existing LF/CRLF warnings remain.

## Remaining Gaps

- Live list/save/keybind/player-preview/session data-model controllers are
  still pending.
- The all-route sweep still reports expected missing data-model notices on
  controller-stub routes.
- Full keyboard/controller navigation parity is still pending.
- True narrow-viewport runtime capture parity remains pending because the
  staged Windows launch path still reports a `960x720` RmlUi canvas for smaller
  requested geometries.
- Native Vulkan/RTX-vkpt RmlUi rendering remains pending.
