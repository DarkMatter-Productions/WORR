# RmlUi Round 25 Smoke Font Glyph Path

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `DV-03-T07`,
`DV-06-T01`, `DV-07-T04`

Status: accepted implementation slice; the guarded `core.runtime_smoke` route
now emits visible RmlUi text geometry through a minimal smoke bitmap font path.
Normal menu routes, Vulkan/RTX-vkpt renderers, final font/text services, live
controllers, runtime navigation, and parity remain pending.

## Summary

Round 25 replaces the Round 24 layout-only RmlUi font bootstrap with
`UI_Rml_SmokeFontEngineInterface`. The smoke font engine returns metrics and
widths like the previous adapter, but now also generates untextured colored
5x7 glyph quads into RmlUi's `TexturedMeshList`. This keeps the
`RMLUI_FONT_ENGINE=none` dependency path alive while proving that the guarded
OpenGL route can draw actual text meshes.

The runtime capture harness now requires the log marker
`RmlUi smoke font engine generated glyph geometry`, so passing evidence proves
RmlUi asked the font engine to generate glyph meshes before the screenshot was
accepted. The smoke RCSS was also tightened so the preview is legible with the
temporary bitmap glyphs.

## Implementation

- Added a small ASCII bitmap glyph table in
  `src/client/ui_rml/ui_rml_runtime.cpp`.
- Replaced `UI_Rml_NullFontEngineInterface` with
  `UI_Rml_SmokeFontEngineInterface`.
- Generated RmlUi mesh quads for non-space smoke text characters, using the
  existing OpenGL bridge's white texture fallback for untextured geometry.
- Logged a one-time glyph-generation marker with string/glyph/size counters.
- Updated `tools/ui_smoke/check_rmlui_runtime_adapter.py` so static adapter
  validation requires the glyph-generating font path.
- Updated `tools/ui_smoke/check_rmlui_runtime_capture.py` so runtime evidence
  requires the glyph-generation marker.
- Updated `assets/ui/rml/core/runtime_smoke.rcss` with explicit block layout
  and fixed smoke widths for readable captured text.

## Evidence

Accepted live command:

```text
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
```

Accepted artifacts:

- `.install/basew/screenshots/rmlui_runtime_smoke_round25.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round25.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round25.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round25.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round25.preview.png`
- `.tmp/rmlui/runtime-capture/manifest.json`

Accepted final manifest facts:

- `ok=true`
- `errors=[]`
- `font_geometry_marker_seen=true`
- `updates=24`
- `renders=24`
- `screenshot_format=tga`
- `screenshot_dimensions=960x720`
- `screenshot_size=2073618`
- `screenshot_fresh=true`
- `screenshot_payload_nonzero=true`

Validation commands accepted during this slice:

```text
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py
ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe worr_opengl_x86_64.dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
```

## Boundaries

- The smoke bitmap font is not the final WORR font/text service.
- No text shaping, kerning, Unicode coverage, localization fallback,
  accessibility scaling, or long-string overflow parity is claimed.
- The accepted screenshot is still guarded, sample-only, OpenGL-only, and
  developer-facing.
- No normal menu entry point opens through RmlUi yet.
- No Vulkan or RTX/vkpt RmlUi render bridge is implemented or redirected.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

Broaden the capture harness from marker and nonblank checks into visual layout
assertions for the smoke route, then add synthetic input/counter checks that
prove close/back behavior during an automated run. After that, start replacing
the smoke bitmap path with the real WORR font/text bridge before normal menu
route ownership is attempted.
