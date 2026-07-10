# RmlUi Round 28 Viewport Matrix Capture

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

Round 28 broadens the guarded OpenGL `core.runtime_smoke` evidence from a
single default screenshot into a two-viewport capture matrix. The capture
harness can now set an explicit windowed `r_geometry`, require that the
resulting screenshot dimensions match that geometry, and aggregate multiple
per-viewport reports into one manifest.

The accepted matrix covers:

- `default_960x720` using `r_geometry=960x720`
- `large_1280x960` using `r_geometry=1280x960`

Both viewports retain the Round 27 glyph, layout, synthetic input, back-close,
route teardown, and inactive-status assertions. This is still guarded sample
evidence: it does not claim normal route ownership, responsive widescreen
parity, live controllers, Vulkan/RTX-vkpt renderer bridges, full font/text
services, or legacy JSON removal.

## Implementation

- `tools/ui_smoke/check_rmlui_runtime_capture.py` now accepts
  `--geometry WIDTHxHEIGHT` for single captures and validates exact screenshot
  dimensions when a geometry is supplied.
- `--matrix` runs the default viewport matrix, appending viewport names to the
  evidence ID stem and writing an aggregate manifest with per-viewport command,
  path, fact, copied-evidence, and error payloads.
- Matrix entries still use the same guarded command path:
  `ui_rml_runtime_capture`, screenshot, `ui_rml_runtime_synthetic_input`, and
  final `ui_rml_runtime_status`.
- Focused tests now cover dry-run matrix commands, successful existing
  evidence for both viewports, exact geometry mismatch failure, and the
  previous glyph/layout/input failure modes.

## Accepted Evidence

The accepted live matrix used the RmlUi-enabled scratch engine at
`.tmp/rmlui/round17-rmlui-enabled3/worr_x86_64.exe` against the refreshed
`.install/basew` staging tree.

- `.install/basew/screenshots/rmlui_runtime_smoke_round28_default_960x720.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round28_default_960x720.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_default_960x720.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_default_960x720.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_default_960x720.preview.png`
- `.install/basew/screenshots/rmlui_runtime_smoke_round28_large_1280x960.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round28_large_1280x960.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_large_1280x960.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_large_1280x960.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round28_large_1280x960.preview.png`
- `.tmp/rmlui/runtime-capture/manifest.json`

Aggregate manifest facts:

- `ok=true`, `viewports=2`, `passed=2`, `failed=0`, `errors=[]`
- `default_960x720`: `ran_engine=true`, `exit_code=0`,
  `screenshot_fresh=true`, `screenshot_dimensions=[960, 720]`,
  `expected_dimensions=[960, 720]`, `layout_ok=true`,
  `font_geometry_marker_seen=true`, `synthetic_input_marker_seen=true`,
  `inactive_status_seen=true`, `route_closes=1`, and
  `route_close_requests=1`.
- `large_1280x960`: `ran_engine=true`, `exit_code=0`,
  `screenshot_fresh=true`, `screenshot_dimensions=[1280, 960]`,
  `expected_dimensions=[1280, 960]`, `layout_ok=true`,
  `font_geometry_marker_seen=true`, `synthetic_input_marker_seen=true`,
  `inactive_status_seen=true`, `route_closes=1`, and
  `route_close_requests=1`.

Accepted layout facts:

- `default_960x720`: panel background bbox `[66, 221, 865, 424]`, panel
  border bbox `[64, 219, 867, 426]`, button fill bbox `[66, 247, 433, 498]`,
  body text bbox `[64, 130, 775, 390]`, and
  `layout_button_fill_below_panel_count=15640`.
- `large_1280x960`: panel background bbox `[132, 442, 1279, 849]`, panel
  border bbox `[128, 438, 1279, 853]`, button fill bbox
  `[132, 493, 867, 959]`, body text bbox `[128, 259, 1279, 780]`, and
  `layout_button_fill_below_panel_count=35940`.
- All `12` layout assertions were true in both matrix viewports.

## Validation

Commands run:

```text
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_runtime_assets.py
python tools\ui_smoke\check_rmlui_runtime_capture.py --dry-run --matrix
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python tools\ui_smoke\check_rmlui_dependency_decision.py --format json
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python tools\ui_smoke\check_rmlui_cvar_inventory.py --format json
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew --format json
ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe worr_opengl_x86_64.dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --matrix --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
```

The combined focused RmlUi pytest matrix passed `55/55`. The normal Windows
build and the RmlUi-enabled scratch build both had no additional work after
the Python/docs change. `.install/` was refreshed before the final live matrix
capture.

## Remaining Work

The matrix intentionally stops short of responsive layout parity. A
`1280x720` experiment exposed fixed smoke-layout clipping in the current
sample route, so responsive widescreen behavior remains a future route/theme
task rather than an accepted Round 28 claim. The next slices should replace
the temporary smoke bitmap font path with a reviewed font/text service and
then move toward normal route ownership and renderer breadth.
