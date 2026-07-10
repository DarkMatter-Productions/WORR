# RmlUi Round 47 Session And Keybind Containment Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 47 continues the staged OpenGL menu-refinement pass by targeting the
remaining long session/keybind surfaces identified in a fresh `30`-route
representative capture:

- `admin_commands` now keeps the long command reference inside a bounded list
  with the Back action visible.
- `callvote_main` now keeps vote options in a bounded, readable vertical menu
  with Return visible.
- `dm_join` now bounds the session action list and keeps the community/status
  copy plus Close action visible at `960x720`.
- `keys` now uses a denser keybind grid so the Interface group and footer
  actions remain visible under the Quake II Rerelease TTF metrics.

This round remains CSS-only. It does not claim live controller behavior, final
scrollbar/focus parity, native Vulkan/RTX-vkpt RmlUi rendering, or end-user
documentation.

## Implementation

- Updated `assets/ui/rml/common/theme/session.rcss`:
  - added explicit bounded heights and widths for `#callvote-main-menu`;
  - added compact bounded sizing for `#dm-join-team-actions`,
    `#dm-join-session-menu`, and `#dm-join-community-links`;
  - tightened `#admin-commands-list` height and row padding so the footer has
    space inside the active canvas.
- Updated `assets/ui/rml/common/theme/utility.rcss`:
  - bounded `#keys-content`;
  - tightened the keybind group and keybind button dimensions for the `keys`
    route only.

## Validation

Build/staging:

- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `meson compile -C builddir-win`

Static/runtime smoke:

- `python tools/ui_smoke/check_rmlui_runtime_adapter.py`
- `python tools/ui_smoke/check_rmlui_runtime_registry.py`
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py`
- `rg -n "var\(|gap:" assets\ui\rml -g "*.rcss"`

Accepted results:

- `.install` refresh passed and staged `104` RmlUi package/loose assets.
- `meson compile -C builddir-win`: passed with no work to do.
- Runtime adapter boundary check: passed.
- Runtime registry check: passed with `57` manifest routes, `58` registered
  routes, `0` missing, `0` unexpected, and `0` duplicates.
- Runtime asset path check: passed with `57` source documents, `7` imported
  assets, and `64` staged loose runtime paths present.
- Focused pytest slice: `25 passed`.
- RCSS browser-feature scan: only the expected comments mention `var()`/`gap:`.

Runtime evidence:

- `.install\basew\logs\rmlui_round47_representative_capture.log`: opened `30`
  representative route IDs with `0` failure/error/exception/unhandled/parser
  hits.
- `.tmp\rmlui\round47-screens\rmlui_round47_contact_sheet_1.png` through
  `.tmp\rmlui\round47-screens\rmlui_round47_contact_sheet_4.png`: fresh
  representative visual pass used to identify `admin_commands`,
  `callvote_main`, `dm_join`, and `keys` follow-ups.
- `.install\basew\logs\rmlui_round47_focused_capture2.log`: focused recapture
  of `admin_commands`, `callvote_main`, `dm_join`, and `keys` after the route
  CSS changes with `0` failure/error/exception/unhandled/parser hits.
- `.install\basew\logs\rmlui_round47_dm_join_final_capture.log`: final
  `dm_join` recapture after shortening its bounded session menu with `0`
  failure/error/exception/unhandled/parser hits.
- `.tmp\rmlui\round47-screens\rmlui_round47_refined2_admin_commands_960x720.png`:
  visual inspection shows the long Admin Commands list bounded above a visible
  Back action.
- `.tmp\rmlui\round47-screens\rmlui_round47_refined2_callvote_main_960x720.png`:
  visual inspection shows the Call Vote options bounded above a visible Return
  action.
- `.tmp\rmlui\round47-screens\rmlui_round47_refined3_dm_join_960x720.png`:
  visual inspection shows the Match Lobby session list, community copy, and
  Close action visible inside `960x720`.
- `.tmp\rmlui\round47-screens\rmlui_round47_refined2_keys_960x720.png`:
  visual inspection shows the Interface keybind group and footer actions
  visible inside `960x720`.
- `.install\basew\logs\rmlui_round47_final_all_route_open.log`: final staged
  OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
  `0` failure/error/exception/unhandled/parser hits, and Quake II Rerelease
  font-source markers.

## Remaining Work

- Add automated layout assertions for non-smoke routes so footer visibility and
  bounded-list regressions are caught without manual screenshot review.
- Replace controller-stub placeholder content with live session/list/keybind
  controllers and focus/scroll behavior.
- Implement native Vulkan and RTX/vkpt RmlUi render bridges before claiming
  renderer parity.
