# RmlUi Round 26 Capture Layout Assertions

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

Status: accepted implementation slice; the guarded `core.runtime_smoke`
OpenGL capture now validates visual layout regions in addition to log markers,
frame counters, glyph generation, dimensions, and nonblank payload. Normal menu
routes, Vulkan/RTX-vkpt renderers, final font/text services, live controllers,
synthetic input/back automation, runtime navigation, and parity remain pending.

## Summary

Round 26 broadens `tools/ui_smoke/check_rmlui_runtime_capture.py` from
presence-only screenshot validation into a first layout-aware smoke. The
harness now parses uncompressed true-color TGA files, counts the exact smoke
route colors, records bounding boxes for the key visual regions, and asserts
the expected relationships between them:

- panel background has a route-scale extent;
- panel border wraps the panel background;
- body text spans the summary and panel regions;
- accent/title text is above the action buttons;
- action button fill appears below the contract panel.

This remains a guarded developer smoke, not visual parity. It gives the next
round a stable foundation for automated input/back checks and later screenshot
matrix work.

## Implementation

- Added a small TGA reader to `tools/ui_smoke/check_rmlui_runtime_capture.py`.
  It handles uncompressed 24/32-bit true-color TGAs, image IDs, and top/bottom
  or left/right origin bits.
- Added layout color contracts for the guarded smoke route:
  `body_background`, `screen_border`, `panel_background`, `panel_border`,
  `button_fill`, `body_text`, and `accent_text`.
- Added per-color count thresholds plus bounding-box relationship checks.
- Added JSON facts for `layout_ok`, `layout_checked`,
  `layout_color_counts`, `layout_bounding_boxes`,
  `layout_button_fill_below_panel_count`, and `layout_assertions`.
- Updated `tools/ui_smoke/test_check_rmlui_runtime_capture.py` so valid
  synthetic evidence paints a miniature smoke layout, and added a failing
  nonblank/wrong-layout TGA test.
- Bumped the default evidence ID to `rmlui_runtime_smoke_round26`.

## Evidence

Accepted live command:

```text
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
```

Accepted artifacts:

- `.install/basew/screenshots/rmlui_runtime_smoke_round26.tga`
- `.install/basew/logs/rmlui_runtime_smoke_round26.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round26.tga`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round26.log`
- `.tmp/rmlui/runtime-capture/rmlui_runtime_smoke_round26.preview.png`
- `.tmp/rmlui/runtime-capture/manifest.json`

Accepted final manifest facts:

- `ok=true`
- `errors=[]`
- `font_geometry_marker_seen=true`
- `layout_checked=true`
- `layout_ok=true`
- `updates=24`
- `renders=24`
- `screenshot_format=tga`
- `screenshot_dimensions=960x720`
- `screenshot_size=2073618`
- `screenshot_fresh=true`
- `screenshot_payload_nonzero=true`

Accepted layout facts from the final manifest:

- `panel_background` count `155052`, bounding box `[66, 221, 865, 424]`
- `panel_border` count `4032`, bounding box `[64, 219, 867, 426]`
- `button_fill` count `16384`, bounding box `[66, 247, 433, 498]`
- `body_text` count `14980`, bounding box `[64, 130, 775, 390]`
- `accent_text` count `3164`, bounding box `[64, 78, 435, 500]`
- `layout_button_fill_below_panel_count=15640`
- all `12` layout assertions were `true`

Validation commands accepted during this slice:

```text
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_capture.py
python tools\ui_smoke\check_rmlui_runtime_capture.py --run --engine-exe .tmp\rmlui\round17-rmlui-enabled3\worr_x86_64.exe --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\manifest.json --format json --timeout 120
```

## Boundaries

- Layout assertions currently require TGA evidence; PNG output remains outside
  this accepted smoke.
- The assertion set is route-specific to `core.runtime_smoke`.
- The checks validate coarse visual structure and color placement, not full
  pixel parity, text shaping, localization, accessibility scaling, or long
  string behavior.
- The accepted screenshot is still guarded, sample-only, OpenGL-only, and
  developer-facing.
- No normal menu entry point opens through RmlUi yet.
- No Vulkan or RTX/vkpt RmlUi render bridge is implemented or redirected.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

Add synthetic input/counter checks to the capture harness so an automated run
can prove close/back behavior and route teardown counters after visual capture.
After that, broaden the visual checks into a small renderer/layout matrix and
start replacing the smoke bitmap path with the real WORR font/text bridge.
