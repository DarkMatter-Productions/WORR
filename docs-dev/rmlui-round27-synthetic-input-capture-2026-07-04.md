# RmlUi Round 27 Synthetic Input Capture

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Summary

Round 27 extends the guarded OpenGL `core.runtime_smoke` capture from visual
layout proof to first automated input/back-close proof. The runtime now records
route open, close, close-request, and synthetic-input counters alongside the
existing frame and input counters. A new developer-only
`ui_rml_runtime_synthetic_input` command sends pointer motion, text input,
mouse-wheel input, and mouse-button-2 back/close input into the guarded sample
route after the screenshot has been written.

This remains sample-only evidence. It proves the guarded route can receive
basic automated input and tear down through the same close/back path, but it
does not claim normal route ownership, gamepad navigation, focus management,
full input parity, live controllers, Vulkan/RTX-vkpt renderer bridges, or
legacy JSON removal.

## Implementation

- `src/client/ui_rml/ui_rml.cpp` now preserves route metrics after close and
  prints `RmlUi runtime route counters` from `ui_rml_runtime_status`.
- `ui_rml_runtime_synthetic_input` is restricted to the active
  `core.runtime_smoke` route and drives mouse move, text, wheel, and
  mouse-button-2 back-close events through the same public RmlUi input bridge
  used by live input.
- `tools/ui_smoke/check_rmlui_runtime_capture.py` now runs the synthetic input
  command after the screenshot wait, then requires the synthetic marker,
  inactive final status, input counters, and route close counters.
- Focused tests cover the new command sequence, JSON facts, and a failure case
  where visual evidence exists but synthetic input/close evidence is missing.
- `tools/ui_smoke/check_rmlui_runtime_adapter.py` now treats the synthetic
  input command and route counter status line as part of the guarded runtime
  adapter boundary.

## Accepted Evidence

The accepted live capture used the RmlUi-enabled scratch engine at
`.tmp/rmlui/round17-rmlui-enabled3/worr_x86_64.exe` against the refreshed
`.install/basew` staging tree.

- `.install/basew/screenshots/rmlui_runtime_smoke_round27.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round27.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round27.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round27.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round27.preview.png`
- `.tmp/rmlui/runtime-capture/manifest.json`

Manifest facts:

- `ok=true`, `ran_engine=true`, `exit_code=0`, `errors=[]`
- `screenshot_fresh=true`, `screenshot_format=tga`,
  `screenshot_dimensions=[960, 720]`, `screenshot_size=2073618`,
  `screenshot_payload_nonzero=true`
- `font_geometry_marker_seen=true`, `layout_checked=true`, `layout_ok=true`
- `frame_updates=24`, `frame_renders=24`
- `synthetic_input_marker_seen=true`, `inactive_status_seen=true`
- `input_keys=2`, `input_chars=1`, `input_mouse_moves=1`,
  `input_mouse_buttons=1`, `input_mouse_wheels=1`
- `route_opens=1`, `route_closes=1`, `route_close_requests=1`,
  `route_synthetic_inputs=1`

The route layout remained unchanged from Round 26: panel background bbox
`[66, 221, 865, 424]`, panel border bbox `[64, 219, 867, 426]`, button fill
bbox `[66, 247, 433, 498]`, body text bbox `[64, 130, 775, 390]`, and all
`12` layout assertions were true.

## Validation

Commands run:

```text
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py tools/ui_smoke/test_check_rmlui_cvar_inventory.py tools/ui_smoke/test_check_rmlui_runtime_assets.py
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python tools\ui_smoke\check_rmlui_dependency_decision.py --format json
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python tools\ui_smoke\check_rmlui_cvar_inventory.py --format json
python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew --format json
ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe worr_opengl_x86_64.dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
git diff --check
```

The combined focused RmlUi pytest matrix passed `52/52`. `git diff --check`
reported only existing LF-to-CRLF working-copy warnings.

## Remaining Work

Next migration slices should keep the guarded evidence narrow while broadening
coverage: add a broader visual matrix, replace the temporary smoke bitmap font
path with a reviewed font/text service, and only then move toward normal route
ownership and parity checks.
