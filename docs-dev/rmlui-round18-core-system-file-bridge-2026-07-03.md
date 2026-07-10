# RmlUi Round 18 Core System/File Bridge

Date: 2026-07-03

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`,
`DV-07-T04`

Status: accepted implementation slice; native renderer and live route
ownership remain pending.

## Summary

Round 18 adds the first WORR-owned RmlUi Core system and file interfaces. The
compiled adapter now installs RmlUi `SystemInterface` and `FileInterface`
implementations before `Rml::Initialise`, routes RmlUi file opens through
WORR's filesystem search path, and exposes an explicit `ui_rml_runtime_probe`
developer command for runtime-facing file-load validation.

Menu ownership remains guarded. `ui_rml_enable` still reports
`renderer_unavailable` with the compiled runtime because no native renderer
bridge is registered, and normal menu entry points continue to fall back to the
legacy UI path.

## Implementation

- Added a `ProbeRoute` hook to `ui_rml_runtime_interface_t` without exposing
  RmlUi types in the public scaffold header.
- Added `ui_rml_runtime_probe [route_id]`, which starts the compiled runtime
  for an explicit probe, asks the runtime to load the route document, and shuts
  the runtime back down when the probe was the only reason it started.
- Added a RmlUi `SystemInterface` implementation backed by:
  - `Sys_Milliseconds` for elapsed time.
  - `Com_EPrintf`, `Com_WPrintf`, and `Com_Printf` for RmlUi log routing.
  - WORR-style relative path joining for RML/RCSS resource references.
  - Pass-through translation as the current placeholder until the localization
    bridge lands.
- Added a RmlUi `FileInterface` implementation backed by:
  - `FS_OpenFile`
  - `FS_CloseFile`
  - `FS_Read`
  - `FS_Seek`
  - `FS_Tell`
  - `FS_Length`
- Installed both RmlUi Core interfaces before `Rml::Initialise`.
- Kept `CanOpenRoutes=false` so the compiled runtime cannot claim route
  ownership before the renderer bridge exists.
- Expanded `tools/ui_smoke/check_rmlui_runtime_adapter.py` and its focused
  tests to validate the system/file includes, interface installation order,
  WORR filesystem symbols, WORR system/log symbols, and runtime file-probe
  command.

## Validation

Accepted validation commands:

```text
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py
ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml_runtime.cpp.obj
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml_runtime.cpp.obj
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll
```

Results:

- Runtime-adapter validation passed with the RmlUi Core include guarded,
  system/file includes guarded, RmlUi Core symbols present, RmlUi interface
  symbols present, WORR filesystem symbols present, WORR system/log symbols
  present, runtime probe hook present, and interface installation before
  `Rml::Initialise` confirmed.
- Dependency-integration validation still reports state `optional`; the
  default Meson option remains disabled.
- Focused runtime-adapter pytest passed with `7 passed`.
- The default-disabled Windows build compiled the touched scaffold and adapter
  objects.
- The enabled scratch build compiled the touched objects and linked
  `worr_engine_x86_64.dll` against the RmlUi Core adapter.

## Non-Claims

- No RmlUi renderer bridge is implemented.
- No route opens or draws through RmlUi from normal menu entry points.
- No live controller, cvar, command, input, font, cursor, accessibility, or
  localization bridge is complete.
- No screenshot/layout, renderer parity, or end-user parity evidence is
  claimed.
- No Vulkan renderer path is redirected to OpenGL.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

The next slice should add the first native renderer bridge scaffold. It should
keep route ownership guarded until at least one sample route can create a RmlUi
context and draw through native OpenGL, Vulkan, and RTX/vkpt renderer paths
without falling back from Vulkan to OpenGL.
