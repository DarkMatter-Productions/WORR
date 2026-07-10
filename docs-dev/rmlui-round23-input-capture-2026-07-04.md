# RmlUi Round 23 Guarded Input And Capture Path

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`,
`DV-07-T04`

Status: accepted implementation slice; the guarded `core.runtime_smoke` route
now receives key, text, mouse-button, mouse-wheel, and pointer movement events,
and exposes runtime status/capture commands for manual OpenGL evidence capture.
Normal menu routes, Vulkan/RTX-vkpt renderers, full input/font services,
automated screenshots, live controllers, and parity remain pending.

## Summary

Round 23 turns the Round 22 guarded context route into a small interactive
runtime proof. The client UI bridge now asks the RmlUi scaffold to consume
key, char, draw, and mouse callbacks before the legacy cgame UI when the
sample route is active. The compiled runtime translates that narrow event set
into RmlUi context calls, so the sample document can receive keyboard, text,
mouse button, mouse wheel, and pointer movement delivery.

The slice also adds a repeatable manual capture path. `ui_rml_runtime_status`
prints route, renderer, frame, and input counters. `ui_rml_runtime_capture`
opens `core.runtime_smoke` if needed, prints instructions for capturing the
next rendered frame, and emits the same status counters. This is not a
screenshot automation harness yet; it is the smallest stable evidence hook for
the next visual validation pass.

## Implementation

- Extended `ui_rml_runtime_interface_t` with `KeyEvent`, `CharEvent`, and
  `MouseEvent` hooks while keeping RmlUi types inside the compiled adapter.
- Added public scaffold entry points:
  - `UI_Rml_KeyEvent(int key, bool down)`
  - `UI_Rml_CharEvent(int key)`
  - `UI_Rml_MouseEvent(int x, int y)`
- Updated `src/client/ui_bridge.cpp` so active guarded RmlUi routes receive
  key, char, draw, and mouse callbacks before the legacy cgame UI callbacks.
  Console/chat mouse notification remains preserved before RmlUi pointer
  dispatch when `KEY_MESSAGE` is active.
- Added guarded route metrics for update/render counts, last frame dimensions,
  key/text counts, mouse moves, mouse buttons, mouse wheels, and the last mouse
  position.
- Kept close/back deliberately conservative: Escape and mouse button 2 close
  the active guarded route after the runtime sees the event.
- Implemented adapter-side input translation in
  `src/client/ui_rml/ui_rml_runtime.cpp`:
  - WORR key constants map to `Rml::Input::KeyIdentifier` values for common
    letters, digits, navigation, function keys, keypad keys, modifiers, and
    punctuation.
  - Modifier state is read from WORR key state and sent as RmlUi modifier
    flags.
  - Keyboard events call `Context::ProcessKeyDown` and
    `Context::ProcessKeyUp`.
  - Text input calls `Context::ProcessTextInput` after filtering invalid
    Unicode/control ranges.
  - Mouse movement calls `Context::ProcessMouseMove`.
  - Mouse button events call `Context::ProcessMouseButtonDown` and
    `Context::ProcessMouseButtonUp`.
  - Mouse wheel events call `Context::ProcessMouseWheel`.
- Added `ui_rml_runtime_status` and `ui_rml_runtime_capture` commands to make
  manual evidence capture repeatable without enabling normal route ownership.
- Extended `tools/ui_smoke/check_rmlui_runtime_adapter.py` and its focused
  tests to validate the new input hooks, bridge ordering, adapter event
  delivery calls, route close/back tokens, and status/capture command tokens.

## Validation

Accepted validation commands:

```text
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python tools\ui_smoke\check_rmlui_dependency_decision.py --format json
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_dependency_integration.py tools/ui_smoke/test_check_rmlui_dependency_decision.py
ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_opengl_x86_64.dll
git diff --check
```

Results:

- Runtime-adapter validation passed with `errors=0`, including the new
  input-hook, adapter event-delivery, UI bridge input-ordering, and
  status/capture command checks.
- Dependency integration and dependency decision validation remained green.
- Focused `tools/ui_smoke` pytest coverage passed for the runtime adapter,
  dependency integration, and dependency decision checkers.
- The default-disabled Windows engine/OpenGL targets and the enabled RmlUi
  scratch engine/OpenGL targets linked successfully.
- `git diff --check` passed with only existing LF-to-CRLF normalization
  warnings.

## Non-Claims

- No normal menu route opens through RmlUi.
- No route has RmlUi parity evidence.
- No automated screenshot/layout harness is implemented.
- No actual OpenGL screenshot artifact is recorded by this slice.
- No Vulkan or RTX/vkpt `Rml::RenderInterface` implementation exists.
- No gamepad navigation, pointer capture policy, cursor/audio integration,
  broad font/text policy, localization, accessibility, or live controller
  bridge is claimed.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

The next slice should use `ui_rml_runtime_capture` to record actual OpenGL
visual evidence for `core.runtime_smoke`, attach the matching
`ui_rml_runtime_status` counters, and then convert that manual evidence path
into a small automated screenshot/runtime smoke harness. Normal menu route
ownership should remain guarded until the visual proof, fallback behavior, and
input counters are repeatable.
