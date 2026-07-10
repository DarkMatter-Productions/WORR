# RmlUi Round 22 Guarded Context Route

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`,
`DV-07-T04`

Status: accepted implementation slice; one guarded sample RmlUi route can open,
update, and render through the OpenGL-native bridge. Normal menu routes,
Vulkan/RTX-vkpt renderers, live controllers, full input/font services,
screenshots, and parity remain pending.

## Summary

Round 22 creates the first real RmlUi context path in WORR. The route is
deliberately narrow: only `core.runtime_smoke` can open through the runtime,
and only through the explicit `ui_rml_runtime_open` command. The compiled
runtime creates a `worr_ui` context, loads one document, shows it, updates it
with renderer dimensions, and renders it through the Round 21 OpenGL primitive
bridge.

Legacy UI remains authoritative for normal menu entry points. `main`, `game`,
`download_status`, and the rest of the route table continue to fall back to the
existing cgame UI until route input, font/text, controller, screenshot, and
parity evidence exists. Vulkan and RTX/vkpt paths remain native-pending and are
not redirected to OpenGL.

## Implementation

- Extended `ui_rml_runtime_interface_t` with `CloseRoute`, `Update`, and
  `Render` hooks so the scaffold can drive a context lifecycle without exposing
  RmlUi types outside `ui_rml_runtime.cpp`.
- Added scaffold-side active route state in `src/client/ui_rml/ui_rml.cpp`.
  The active route is allow-listed to `core.runtime_smoke`, claims `KEY_MENU`,
  and releases it when the route closes.
- Added `ui_rml_runtime_open [route_id]` and `ui_rml_runtime_close`. The open
  command defaults to `core.runtime_smoke`; other routes are rejected by both
  the scaffold and compiled adapter.
- Implemented `UI_Rml_Draw(realtime)` so an active route updates with
  `r_config.width`/`r_config.height`, renders through the compiled runtime,
  and clears back to legacy fallback if update or render fails.
- Updated `src/client/ui_bridge.cpp` so an active RmlUi route draws before the
  legacy cgame UI draw callback. While the guarded sample route is active,
  legacy UI key/char/frame/mouse callbacks are suppressed, and Escape closes
  the sample route.
- Implemented the compiled runtime context lifecycle in
  `src/client/ui_rml/ui_rml_runtime.cpp`:
  - Creates `Rml::CreateContext("worr_ui", ...)`.
  - Loads the resolved document with `Context::LoadDocument`.
  - Shows one `ElementDocument`.
  - Resizes the context through `SetDimensions`.
  - Calls `Context::Update` before `Context::Render`.
  - Closes/unloads documents and removes the context during shutdown.
- Kept renderer-family guardrails intact. The sample path requires native
  renderer availability and the OpenGL family for this slice; non-OpenGL
  renderers still export no available RmlUi bridge.
- Updated `tools/ui_smoke/check_rmlui_runtime_adapter.py` and its focused
  tests to validate lifecycle hooks, context/document API usage, sample-route
  allow-listing, open/close commands, UI bridge draw ordering, and no
  Vulkan-to-OpenGL redirection.

## Validation

Accepted validation commands:

```text
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py
ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll
```

Results:

- Runtime-adapter validation passed with `errors=0`, including the new context
  lifecycle, sample-route guard, runtime command, and UI bridge draw-order
  checks.
- Focused runtime-adapter pytest passed.
- The default-disabled Windows build linked `worr_engine_x86_64.dll` and kept
  the OpenGL renderer target valid.

## Non-Claims

- No normal menu route opens through RmlUi.
- No route has RmlUi parity evidence.
- No Vulkan or RTX/vkpt `Rml::RenderInterface` implementation exists.
- No RmlUi keyboard navigation, text entry, mouse dispatch, gamepad navigation,
  cursor/audio, font/text policy, localization, accessibility, screenshot, or
  layout proof is claimed beyond Escape closing the guarded sample route.
- No live controller or data-model bridge is executed through the RmlUi
  document.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

The next slice should turn the guarded sample context into visible proof that
can be inspected repeatably: add a minimal screenshot/runtime smoke harness or
manual capture path for `ui_rml_runtime_open`, record OpenGL output evidence,
and start the smallest input bridge needed for close/back and pointer delivery.
Normal menu route ownership should remain guarded until that proof is stable.
