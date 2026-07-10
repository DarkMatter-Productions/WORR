# RmlUi Round 48 Settings Toggle Form Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T06`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 48 follows the staged OpenGL representative capture with a focused pass
on long settings/local-host forms. The main issue was not route loading but
control presentation: binary settings on several pages were still drawn like
full-width text fields, which made the forms overly tall and caused bottom
rows to peek into the footer area.

This round makes toggle controls compact square controls in settings forms,
slightly shortens the shared settings scroll region, gives the compact
Downloads form enough height to show its final HTTP toggle, and trims the Start
Server scroll region so it ends on complete rows above Back/Close.

This round remains CSS-only. It does not claim live settings persistence,
controller parity, final focus/scrollbar behavior, native Vulkan/RTX-vkpt
RmlUi rendering, or end-user documentation.

## Implementation

- Updated `assets/ui/rml/common/theme/settings.rcss`:
  - reduced the default `.settings-form` height from `540px` to `500px`;
  - tightened `.setting-row` spacing;
  - added compact square sizing for `.setting-row[data-control="toggle"] input`;
  - gave `#downloads-settings-form` a route-specific `540px` height so the
    compact list shows the final Transport toggle cleanly.
- Updated `assets/ui/rml/common/theme/singleplayer.rcss`:
  - shortened `#startserver-settings-form` to `470px`;
  - kept `#gameflags-settings-form` at `500px` because its dense checkbox
    matrix already had a contained initial view.

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

- `.install\basew\logs\rmlui_round48_representative_capture.log`: opened `30`
  representative route IDs with `0` failure/error/exception/unhandled/parser
  hits.
- `.tmp\rmlui\round48-screens\rmlui_round48_contact_sheet_1.png` through
  `.tmp\rmlui\round48-screens\rmlui_round48_contact_sheet_4.png`: fresh
  representative visual pass used to identify `performance`, `sound`,
  `downloads`, and `startserver` form follow-ups.
- `.install\basew\logs\rmlui_round48_focused_forms_capture.log`: focused
  recapture of `performance`, `sound`, `downloads`, and `startserver` after
  the shared settings/single-player CSS changes with `0`
  failure/error/exception/unhandled/parser hits.
- `.install\basew\logs\rmlui_round48_downloads_final_capture.log`: final
  `downloads` recapture after route-specific form height adjustment with `0`
  failure/error/exception/unhandled/parser hits.
- `.tmp\rmlui\round48-screens\rmlui_round48_refined_performance_960x720.png`:
  visual inspection shows compact Client Behavior toggles with all rows and
  Back/Close visible.
- `.tmp\rmlui\round48-screens\rmlui_round48_refined_sound_960x720.png`:
  visual inspection shows compact music toggle presentation and footer actions
  visible.
- `.tmp\rmlui\round48-screens\rmlui_round48_refined2_downloads_960x720.png`:
  visual inspection shows all download toggles, the final HTTP Downloads
  toggle, and Back/Close visible.
- `.tmp\rmlui\round48-screens\rmlui_round48_refined_startserver_960x720.png`:
  visual inspection shows Start Server ending on complete rows above
  Back/Close.
- `.install\basew\logs\rmlui_round48_final_all_route_open.log`: final staged
  OpenGL route pass recorded `59` opened documents, `58` unique route IDs,
  `0` failure/error/exception/unhandled/parser hits, and Quake II Rerelease
  font-source markers.

## Remaining Work

- Add automated route screenshot assertions for footer visibility and complete
  row boundaries on non-smoke menu pages.
- Replace placeholder settings controls with live cvar/controller behavior,
  focus traversal, and final scrollbar handling.
- Implement native Vulkan and RTX/vkpt RmlUi render bridges before claiming
  renderer parity.
