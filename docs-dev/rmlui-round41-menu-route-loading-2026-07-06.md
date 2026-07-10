# RmlUi Round 41 Menu Route Loading

Date: 2026-07-06

Tasks: `FR-09-T02`, `FR-09-T03`, `FR-09-T04`, `FR-09-T09`

## Summary

Round 41 fixes the installed RmlUi menu-load crash that appeared as the WORR
unhandled-exception dialog during startup. The root cause was document
ownership during route open: the runtime loaded a new RmlUi document into the
context, then called the generic route-close helper before assigning
`ui_rml_document`. With no active document pointer set, that helper called
`UnloadAllDocuments()`, which destroyed the newly loaded document. Listener
registration and `Show()` then operated on freed memory.

The route-open path now closes only the known active document after the new
document has loaded successfully. It no longer unloads every context document
while a newly loaded route document is waiting to become active.

## Implementation

- Added `UI_Rml_CompiledRuntimeCloseActiveDocument()` in
  `src/client/ui_rml/ui_rml_runtime.cpp`.
- Changed `UI_Rml_CompiledRuntimeOpenRoute()` to load the replacement document
  first, close only the previous active document, then attach the click listener,
  show the new document, and update the context.
- Removed the normal close-route path's opportunistic `UnloadAllDocuments()`
  call. Shutdown still removes the RmlUi context after closing the active route.
- Restored the main menu RCSS imports that were temporarily removed during
  crash reduction.
- Kept shell route documents on RmlUi-compatible element usage by using
  `.ui-button` divs instead of browser-only button semantics where needed.
- Converted linked runtime theme declarations away from browser CSS features
  that RmlUi 6.2 rejects:
  - `common/theme/singleplayer.rcss`
  - `common/theme/session.rcss`
  - `common/theme/accessibility.rcss`
- Fixed the VSCode build/launch path so it uses the same enabled RmlUi runtime
  as the accepted staged validation:
  - `.vscode/tasks.json` now reconfigures or creates `builddir-win` with
    `-Drmlui=enabled`.
  - The default fast build task runs the setup task before compile/install, so a
    stale `rmlui=disabled` Meson coredata file cannot silently persist.
  - `.install` refreshes use the explicit `windows-x86_64` platform validation
    lane.
  - `.vscode/launch.json` now puts `WORR (OpenGL RmlUi)` first, starts in the
    menu instead of jumping straight into `q2dm1`, and forces
    `ui_rml_enable 1`. Vulkan and RTX launch profiles remain separate because
    native Vulkan/RTX RmlUi render bridges are still blocked work, not OpenGL
    redirects.

## Validation

- Built the enabled RmlUi runtime:
  `ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll worr_x86_64.exe -v`
- Refreshed the install payload:
  `python tools\refresh_install.py --build-dir .tmp\rmlui\round17-rmlui-enabled3 --install-dir .install --base-game basew --platform-id windows-x86_64`
- Startup/main-menu staged run:
  `.install\basew\logs\rmlui_main_route_fix.log`
  - exit code `0`
  - no fresh crash dump
  - `RmlUi route 'main' opened document 'ui/rml/shell/main.rml'`
  - runtime status `active=yes route='main'`
  - `updates=180 renders=180`
- Route-swap staged run:
  `.install\basew\logs\rmlui_route_swap_fix.log`
  - exit code `0`
  - no fresh crash dump
  - `main` opened, then `options` opened and became active
- Top-menu route batch:
  `.install\basew\logs\rmlui_top_menu_routes_fix2.log`
  - exit code `0`
  - no fresh crash dump
  - `main`, `options`, `singleplayer`, `multiplayer`, `video`, `downloads`,
    `quit_confirm`, `game`, `download_status`, and `core.runtime_smoke` opened
  - no `Syntax error`, `failed`, `fallback`, `No font face`, `Parser`, or
    `error` log hits after the RCSS cleanup
- Full registered-route load pass:
  `.install\basew\logs\rmlui_all_routes_fix2.log`
  - exit code `0`
  - no fresh crash dump
  - `opened_count=57`
  - runtime status `active=yes route='core.runtime_smoke'`
  - no parser/fallback/error log hits
- VSCode build/launch workflow validation:
  - `tools\meson_setup.cmd setup --native-file meson.native.ini --reconfigure builddir-win -Dbase-game=basew -Ddefault-game=basew -Drmlui=enabled`
    reported `rmlui : YES`.
  - `meson compile -C builddir-win` completed successfully.
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
    completed, validated `104` RmlUi asset payload entries, and staged
    `rmlui_core.dll`.
  - `meson introspect builddir-win --buildoptions` reports
    `rmlui=enabled`.
  - `llvm-readobj --coff-imports .install\worr_engine_x86_64.dll` shows the
    staged engine imports `rmlui_core.dll`.
  - `.install\basew\logs\vscode_opengl_rmlui_launch_verify_20260706.log`
    shows `RmlUi route 'main' opened document 'ui/rml/shell/main.rml'` and
    runtime status `active=yes route='main'`, exited with code `0`, and
    produced no fresh crash dump.

## Remaining Work

This round proves installed OpenGL RmlUi route loading and route swaps across
the full registered route inventory. It does not claim final live controllers,
settings persistence parity, final font/text shaping, broad input navigation
parity, screenshot layout parity, native Vulkan/RTX-vkpt RmlUi rendering, or
legacy JSON removal.
