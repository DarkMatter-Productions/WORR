# Renderer Async Shutdown Drain (2026-04-29)

Task ID: `FR-04-T08`

## Context
During Quake Champions top-HUD screenshot validation, WORR could crash when a
test queued `screenshotpng` and then quit before the asynchronous screenshot
writer completed.

The attached dump `crashdump_134219279286467617.dmp` showed an execute access
violation on the main thread during `Com_ShutdownAsyncWork()`. The top engine
frame was `Com_ShutdownAsyncWork`, and the jump target was no longer part of a
loaded WORR module. The matching log also reported a renderer-tag leak before
the crash.

## Root Cause
The OpenGL and RTX renderers queue async screenshot work with callbacks owned by
the renderer module:

```text
work_cb = screenshot_work_cb
done_cb = screenshot_done_cb
```

On quit, the previous shutdown order unloaded the external renderer before the
global async work queue was drained. If a screenshot completed after renderer
unload, `Com_ShutdownAsyncWork()` later attempted to call the renderer-owned
`done_cb`, jumping through stale code.

## Fix
`CL_ShutdownRenderer()` now calls `Com_ShutdownAsyncWork()` before renderer
subsystems are torn down and before external renderer code can be unloaded.
This keeps screenshot callbacks live while pending work is joined and completed.
The async worker can be recreated later if a renderer restart queues new work.

## Validation
Rebuilt the engine and staged `.install/`:

```text
meson compile -C builddir-win worr_engine_x86_64 cgame_x86_64 sgame_x86_64 copy_cgame_dll copy_sgame_dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

Reproduced the previous failure path with OpenGL, `screenshotpng`, and immediate
quit. The fixed run wrote the screenshot, produced no new crash dump, and exited
without the previous renderer leak report.
