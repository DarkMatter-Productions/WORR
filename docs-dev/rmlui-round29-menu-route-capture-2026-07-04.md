# RmlUi Round 29 Menu Route Capture

Date: 2026-07-04

Tasks: `FR-09-T03`, `FR-09-T09`, `FR-03-T08`, `DV-03-T07`,
`DV-07-T04`, `DV-06-T01`

## Summary

Round 29 accepts the first guarded normal-menu-entrypoint render proof for the
RmlUi runtime. It widens the runtime route allow-list from the smoke document
to the existing `runtime_stub` menu routes: `main`, `game`, and
`download_status`. The routes still require `ui_rml_enable=1`, OpenGL, and the
guarded runtime path; legacy UI remains authoritative when the switch is
disabled or the route fails to open.

This round does not claim final menu layout/theme parity, controller behavior,
runtime navigation, gamepad/focus parity, Vulkan/RTX-vkpt rendering, final
font/text services, or legacy JSON removal.

## Implementation

- `src/client/ui_rml/ui_rml.cpp` now defines a guarded menu route set for
  `main`, `game`, and `download_status`. Those route IDs are accepted by
  `UI_Rml_RuntimeRouteIsAllowed`, so the normal `UI_Rml_OpenMenu` path can open
  them when `ui_rml_enable=1`.
- Added `ui_rml_runtime_capture_menu <main|game|download_status>`. The command
  maps the route ID to the matching `UIMENU_*` entrypoint, opens it through
  the top-level `UI_OpenMenu`, and prints the same runtime status counters used
  by the smoke capture harness.
- `src/client/ui_rml/ui_rml_runtime.cpp` mirrors the same allow-list in the
  compiled RmlUi adapter, preserving the OpenGL-only guarded context path.
- `tools/ui_smoke/check_rmlui_runtime_capture.py` now supports
  `--route-matrix`. The route matrix captures `main`, `game`, and
  `download_status` at `960x720`, verifies route-specific active OpenGL status,
  font geometry, fresh screenshots, exact dimensions, synthetic input, close
  counters, and inactive final status.
- Route-matrix validation intentionally sets `layout_required=false`; the
  existing smoke-route color/layout assertions remain scoped to
  `core.runtime_smoke`.
- `tools/ui_smoke/check_rmlui_runtime_adapter.py` now validates that the
  scaffold and compiled adapter both contain the same guarded runtime route set
  and the new menu capture command.

## Accepted Evidence

- `.install/basew/screenshots/rmlui_runtime_smoke_round29_main.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round29_main.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_main.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_main.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_main.preview.png`
- `.install/basew/screenshots/rmlui_runtime_smoke_round29_game.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round29_game.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_game.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_game.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_game.preview.png`
- `.install/basew/screenshots/rmlui_runtime_smoke_round29_download_status.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round29_download_status.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_download_status.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_download_status.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round29_download_status.preview.png`
- `.tmp/rmlui/runtime-capture/manifest.json`

Final route-matrix facts:

- Aggregate: `ok=true`, `routes=3`, `passed=3`, `failed=0`, `errors=0`.
- `main`: `route_document=shell/main.rml`, `screenshot_dimensions=[960, 720]`,
  `expected_dimensions=[960, 720]`, `guarded_opengl_status_seen=true`,
  `font_geometry_marker_seen=true`, `synthetic_input_marker_seen=true`,
  `inactive_status_seen=true`, `route_opens=1`, `route_closes=1`,
  `route_close_requests=1`, `route_synthetic_inputs=1`,
  `layout_required=false`.
- `game`: `route_document=shell/game.rml`, `screenshot_dimensions=[960, 720]`,
  `expected_dimensions=[960, 720]`, `guarded_opengl_status_seen=true`,
  `font_geometry_marker_seen=true`, `synthetic_input_marker_seen=true`,
  `inactive_status_seen=true`, `route_opens=1`, `route_closes=1`,
  `route_close_requests=1`, `route_synthetic_inputs=1`,
  `layout_required=false`.
- `download_status`: `route_document=shell/download_status.rml`,
  `screenshot_dimensions=[960, 720]`, `expected_dimensions=[960, 720]`,
  `guarded_opengl_status_seen=true`, `font_geometry_marker_seen=true`,
  `synthetic_input_marker_seen=true`, `inactive_status_seen=true`,
  `route_opens=1`, `route_closes=1`, `route_close_requests=1`,
  `route_synthetic_inputs=1`, `layout_required=false`.

The PNG previews show sparse smoke-font menu text. This is expected for the
current temporary bitmap font path and is not final theme or layout parity.

## Validation

- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py`:
  passed, `12` tests.
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py`:
  passed, `8` tests.
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_runtime_assets.py tools/ui_smoke/test_check_rmlui_menu_entrypoints.py tools/ui_smoke/test_check_rmlui_runtime_stub_eligibility.py`:
  passed, `69` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed, `errors=0`, `route_open_guard_present=true`.
- `python tools\ui_smoke\check_rmlui_menu_entrypoints.py`:
  passed, `Mapped routes=4`, `Unique mapped routes=3`.
- `python tools\ui_smoke\check_rmlui_runtime_stub_eligibility.py`:
  passed, `Runtime_stub routes checked=3`.
- `python tools\ui_smoke\check_rmlui_dependency_integration.py --format json`:
  passed, `errors=0`.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed after the Round 29 doc update, `errors=0`.
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew --format json`:
  passed, `routes_checked=57`, `staged_loose_files.present=73`.
- `ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll`:
  passed.
- `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe worr_opengl_x86_64.dll`:
  passed.
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`:
  passed and refreshed `.install/`.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --route-matrix --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120`:
  passed, `routes=3`, `passed=3`, `errors=0`.

## Remaining Work

- Add native Vulkan and RTX/vkpt RmlUi render bridges rather than redirecting
  either renderer to OpenGL.
- Replace the temporary smoke bitmap font path with the final font/text,
  localization, and long-string behavior.
- Add live controllers, command dispatch, cvar bindings, condition handling,
  focus/gamepad navigation, and route-to-route navigation.
- Broaden visual/layout assertions from the smoke sample into route-specific
  parity checks once final theme/layout behavior is implemented.
