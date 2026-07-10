# RmlUi Round 24 Runtime Capture Harness

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`,
`DV-07-T04`

Status: accepted implementation slice; the guarded `core.runtime_smoke` route
now has automated OpenGL screenshot/log evidence through a local runtime
capture harness. Normal menu routes, Vulkan/RTX-vkpt renderers, full font/text
services, live controllers, runtime navigation, and parity remain pending.

## Summary

Round 24 converts the Round 23 manual capture hook into an automated guarded
runtime smoke. `tools/ui_smoke/check_rmlui_runtime_capture.py` can launch the
engine, open `core.runtime_smoke`, capture a screenshot, validate the flushed
runtime log, inspect frame/input counters, verify the screenshot dimensions,
check that the TGA payload is nonblank, copy evidence to `.tmp/rmlui/`, and
write a JSON manifest.

The slice also installs the smallest RmlUi font-engine adapter needed for
initialization under the current `RMLUI_FONT_ENGINE=none` build. This
`UI_Rml_NullFontEngineInterface` reports metrics and string widths so the
document can lay out, but it intentionally emits no glyph geometry. The visual
proof therefore covers document layout boxes, styled panels/buttons, renderer
composition, and frame counters, not real text rendering.

## Implementation

- Added `tools/ui_smoke/check_rmlui_runtime_capture.py` with `--run`,
  `--dry-run`, `--screenshot-format`, JSON/text output, evidence copying, and
  manifest writing.
- The harness defaults to TGA evidence because this OpenGL/libpng path reports
  a host write failure in the current desktop run while TGA readback and write
  are stable. PNG validation remains available through `--screenshot-format
  png`.
- Added `r_screenshot_dir` as an empty-by-default, non-archived renderer cvar
  in the OpenGL and RTX screenshot implementations. The harness sets it to
  `.install/basew/screenshots` so evidence stays in the local staged tree.
- Removed the harness `condump` step after confirming condumps still resolve
  through the rerelease home directory. The flushed console log contains the
  accepted runtime status evidence without writing new capture artifacts
  outside the repo.
- Added `tools/ui_smoke/test_check_rmlui_runtime_capture.py` for dry-run,
  valid-evidence, missing-screenshot, and missing-guarded-status coverage.
- Added `UI_Rml_NullFontEngineInterface` and installed it before
  `Rml::Initialise` so RmlUi can initialize and lay out the guarded document
  until the real WORR font/text bridge lands.
- Added `assets/ui/rml/core/runtime_smoke.rcss` and linked it from
  `runtime_smoke.rml` so the capture route produces visible styled geometry
  even without glyph output.
- Extended runtime-adapter validation so it checks the guarded font-engine
  installation tokens alongside the existing renderer/context/input checks.

## Evidence

Live guarded OpenGL capture passed with the RmlUi-enabled scratch build:

```text
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
```

Accepted evidence:

- `.install/basew/screenshots/rmlui_runtime_smoke_round24.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round24.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round24.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round24.log`
- `.tmp/rmlui/runtime-capture/manifest.json`

Accepted facts from the final manifest:

- `ok=true`, `errors=[]`, engine exit code `0`.
- Guarded route marker and OpenGL active-route status were present.
- Frame counters recorded `updates=24` and `renders=24`.
- Input counters were present; no synthetic input was injected in this capture.
- Screenshot format was `tga`, dimensions were `960x720`, size was `2073618`
  bytes, and the payload was nonblank.

## Validation

Accepted validation commands:

```text
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
python tools\ui_smoke\check_rmlui_runtime_capture.py --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py
ninja -C builddir-win worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_rtx_x86_64.dll
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_x86_64.exe worr_engine_x86_64.dll worr_opengl_x86_64.dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

Results:

- The live runtime capture harness passed and wrote the accepted evidence
  listed above.
- The validate-only harness pass accepted the freshly captured evidence.
- Focused capture-harness pytest coverage passed.
- The default-disabled Windows engine/OpenGL/RTX targets and the enabled
  RmlUi scratch engine/OpenGL targets linked successfully.
- `.install` was refreshed with current binaries and packaged/loose assets;
  RmlUi asset staging validated `104` package/loose files.

## Non-Claims

- No normal menu route opens through RmlUi.
- No route has RmlUi parity evidence.
- No Vulkan or RTX/vkpt RmlUi render bridge is implemented or redirected.
- The null font engine is not a real font/text service and does not render
  glyphs.
- The capture harness proves a guarded OpenGL TGA screenshot only; it does not
  prove PNG output, visual parity, long-string layout, localization, or font
  fallback.
- No synthetic input, controller execution, runtime navigation, gamepad
  navigation, pointer-capture policy, cursor/audio integration, or
  accessibility behavior is claimed.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

Replace the null font engine with a WORR-backed font/text bridge or a minimal
glyph path so the guarded capture includes actual text geometry. After that,
broaden the capture harness with visual layout assertions and synthetic
input/counter checks before attempting normal menu route ownership.
