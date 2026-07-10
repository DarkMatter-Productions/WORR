# RmlUi Round 42 Resize Canvas And Cursor

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`

## Summary

Round 42 fixes RmlUi behavior when the renderer canvas changes through window
resizes, fullscreen mode changes, or other `SCR_ModeChanged()` paths. The
runtime now sizes RmlUi to the same virtual 2D canvas that OpenGL uses for menu
drawing, converts framebuffer mouse coordinates into that canvas before sending
them to RmlUi, converts RmlUi scissor rectangles back to framebuffer pixels in
the OpenGL bridge, and draws a software cursor from the RmlUi path so the menu
cursor remains visible when the platform cursor is hidden for menu/fullscreen
state.

## Implementation

- Added `UI_Rml_ModeChanged()` and call it from the client UI mode-change
  bridge so active RmlUi contexts are resized immediately after renderer mode
  changes.
- Changed active RmlUi frame updates to use the renderer virtual UI canvas
  instead of raw framebuffer dimensions.
- Changed route-open initialization to create the RmlUi context at the current
  virtual canvas dimensions rather than a temporary `640x480`.
- Converted RmlUi mouse input from framebuffer coordinates into the same canvas
  used by the RmlUi context.
- Added a RmlUi software cursor using `/gfx/cursor.png`, drawn after RmlUi
  rendering with clipping cleared.
- Converted OpenGL RmlUi scissor rectangles from virtual UI coordinates to
  framebuffer pixels using the shared renderer UI-scale helper.
- Added C++ linkage guards to `inc/renderer/ui_scale.h` so renderer C++ bridge
  code can safely use the C helper declarations.

## Validation

- Build:
  `ninja -C builddir-win worr_engine_x86_64.dll worr_opengl_x86_64.dll worr_x86_64.exe`
- Install refresh:
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Runtime capture, scaled canvas:
  `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --install-dir .install --route-id main --geometry 1280x720 --evidence-dir .tmp\rmlui\resize-canvas --evidence-id rmlui_resize_canvas_1280x720_final --screenshot-format tga --format text`
  - exit code `0`
  - screenshot dimensions `(1280, 720)`
  - runtime status `active=yes route='main'`
  - RmlUi runtime dimensions `640x360`
  - software cursor visible in
    `.tmp\rmlui\resize-canvas\rmlui_resize_canvas_1280x720_final.png`
- Runtime capture, baseline canvas:
  `python tools\ui_smoke\check_rmlui_runtime_capture.py --run --install-dir .install --route-id main --geometry 960x720 --evidence-dir .tmp\rmlui\resize-canvas --evidence-id rmlui_resize_canvas_960x720_final --screenshot-format tga --format text`
  - exit code `0`
  - screenshot dimensions `(960, 720)`
  - runtime status `active=yes route='main'`
  - RmlUi runtime dimensions `960x720`
- OS-window live resize smoke:
  `.install\basew\logs\rmlui_os_window_resize_move_800_to_1280.log`
  - exit code `0`
  - no fresh crash dump
  - one active RmlUi route stayed open while Win32 `MoveWindow()` generated
    live resize events
  - RmlUi runtime dimensions changed from `1178x844` to `949x512` during the
    same process

## Remaining Work

This round proves OpenGL RmlUi canvas resizing, scissor conversion, mouse
coordinate conversion, and software cursor visibility in staged runtime runs.
It does not implement native Vulkan/RTX RmlUi rendering, final font/text
services, full controller behavior, route layout parity, or complete input
navigation parity.
