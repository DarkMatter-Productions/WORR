# RmlUi Round 46 Single-Player And Utility List Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T06`, `FR-09-T07`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 46 follows the broad Round 45 visual pass with two targeted menu layout
fixes found in the representative capture set:

- The `singleplayer` hub now uses explicit selector/action widths so episode,
  level, load, save, Back, and Close controls remain visually aligned under the
  Quake II Rerelease TTF metrics.
- The generic `ui_list` route now gives its extra-action toolbar and list body
  explicit bounded dimensions so long generated lists scroll inside the active
  menu area instead of pushing the footer controls below the `960x720` canvas.

This round does not claim live controller behavior, final list scroll/focus
parity, native Vulkan/RTX-vkpt RmlUi rendering, or end-user documentation.

## Implementation

- Updated `assets/ui/rml/common/theme/singleplayer.rcss`:
  - set a stable `.selector-section` width;
  - aligned selector inputs, selector buttons, and single-player save/load
    action buttons to the same control width.
- Updated `assets/ui/rml/common/theme/utility.rcss`:
  - bounded `#ui-list-extra-actions`;
  - bounded `#ui-list-content` with `overflow: auto`;
  - compacted repeated `#ui-list-content button` rows.

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

- `meson compile -C builddir-win`: passed with no work to do.
- Runtime adapter boundary check: passed.
- Runtime registry check: passed with `57` manifest routes, `58` registered
  routes, `0` missing, `0` unexpected, and `0` duplicates.
- Runtime asset path check: passed with `57` source documents, `7` imported
  assets, and `64` staged loose runtime paths present.
- Focused pytest slice: `25 passed`.
- RCSS browser-feature scan: only the expected comments mention `var()`/`gap:`.

Runtime evidence:

- `.install\basew\logs\rmlui_round46_representative_capture.log`: opened `30`
  representative route IDs with `0` failure/error/exception/unhandled/parser
  hits and Quake II Rerelease font-source markers.
- `.tmp\rmlui\round46-screens\rmlui_round46_contact_sheet_1.png` through
  `.tmp\rmlui\round46-screens\rmlui_round46_contact_sheet_4.png`: representative
  visual pass used to identify the `singleplayer` and `ui_list` follow-ups.
- `.install\basew\logs\rmlui_round46_hub_list_capture.log`: focused recapture
  of `singleplayer` and `ui_list` after the CSS changes with `0`
  failure/error/exception/unhandled/parser hits.
- `.tmp\rmlui\round46-screens\rmlui_round46_refined_singleplayer_960x720.png`:
  visual inspection shows the Single Player hub controls aligned at stable
  widths with visible Back/Close actions.
- `.tmp\rmlui\round46-screens\rmlui_round46_refined_ui_list_960x720.png`:
  visual inspection shows the Session List toolbar, bounded list body, and
  Previous/Next/Return footer controls all visible inside `960x720`.
- `.install\basew\logs\rmlui_round46_final_all_route_open.log`: final staged
  OpenGL route pass recorded `59` opened documents, `58` unique route IDs, and
  `0` failure/error/exception/unhandled/parser hits.

## Remaining Work

- Replace controller-stub placeholder list content with live data-model-backed
  list providers, scroll state, and focus behavior.
- Expand automated screenshot assertions beyond the smoke route so these
  bounded-list regressions are caught without manual contact-sheet review.
- Implement native Vulkan and RTX/vkpt RmlUi render bridges before claiming
  renderer parity.
