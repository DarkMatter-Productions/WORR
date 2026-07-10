# RmlUi Agent 1 Runtime Switch Round 4

Date: 2026-07-02

Owner lane: Agent 1, client runtime scaffold

Task IDs: `FR-09-T02`, `FR-09-T03`, `FR-03-T08`, `DV-04-T02`

## Summary

This round adds the first guarded client-owned RmlUi menu runtime switch
scaffold. It intentionally does not add the RmlUi third-party dependency, link
any RmlUi symbols, or replace the legacy JSON/menu widget path.

The new switch is off by default. When `ui_rml_enable` is `0`, it is a no-op and
legacy menu behavior continues. When `ui_rml_enable` is non-zero, menu-open
attempts can resolve a target route and emit a console log, but the scaffold
still returns `false` so the legacy UI path remains authoritative until a
future runtime is compiled and wired.

## Changed Files

- `src/client/ui_rml/ui_rml.h`
  - Adds a small C-compatible interface for client UI lifecycle and route-open
    attempts.
  - Exposes `UI_Rml_Init`, `UI_Rml_Shutdown`, `UI_Rml_IsEnabled`,
    `UI_Rml_RouteForMenu`, and `UI_Rml_OpenMenu`.
- `src/client/ui_rml/ui_rml.cpp`
  - Registers `ui_rml_enable` and `ui_rml_debug`.
  - Maps `UIMENU_DEFAULT` and `UIMENU_MAIN` to route `main`, `UIMENU_GAME` to
    route `game`, and `UIMENU_DOWNLOAD` to route `download_status`.
  - Logs requested route IDs only when the switch is enabled, then returns
    `false` because no RmlUi runtime is compiled yet.
- `src/client/ui/ui.cpp`
  - Adds guarded lifecycle calls for the legacy client menu path.
  - Gives the RmlUi scaffold a chance to consume a menu-open request after the
    old menu stack is closed and before legacy menu lookup falls back.
- `src/client/ui_bridge.cpp`
  - Coordinator integration added the same guarded lifecycle and open-request
    handoff to the active cgame UI bridge path.
- `meson.build`
  - Adds `src/client/ui_rml/ui_rml.cpp` to the client engine source list so the
    scaffold is compiled without introducing a third-party dependency.

## Runtime Contract

Registered cvars:

- `ui_rml_enable`: defaults to `0`; opt-in gate for route-open attempts.
- `ui_rml_debug`: defaults to `0`; emits lifecycle debug logs when enabled.

Route mapping:

| `uiMenu_t` value | RmlUi route ID |
|---|---|
| `UIMENU_DEFAULT` | `main` |
| `UIMENU_MAIN` | `main` |
| `UIMENU_GAME` | `game` |
| `UIMENU_DOWNLOAD` | `download_status` |
| `UIMENU_NONE` | no route |

Return behavior:

- Disabled or unmapped: return `false` with no menu ownership change.
- Enabled, no runtime compiled: log the requested route and return `false`.
- Future runtime compiled: the same `UI_Rml_OpenMenu` boundary is the intended
  place to consume the route and return `true`.

## Integration Notes

The legacy `src/client/ui/ui.cpp` lifecycle has direct guarded calls now. The
currently compiled front-end in this branch routes UI calls through
`src/client/ui_bridge.cpp` into the cgame UI export layer, so the coordinator
added the equivalent `UI_Rml_Init`/`UI_Rml_Shutdown`/`UI_Rml_OpenMenu` handoff
there after the worker lane completed.

Follow-up TODO:

- Decide whether the cgame menu system should delegate to the client-owned
  RmlUi route switch directly, or whether it should publish route/data requests
  through a narrow bridge owned by `FR-03-T08` and `DV-04-T02`.

## Task Mapping

`FR-09-T02`: Adds Meson compilation for the dependency-free runtime switch
scaffold. The actual RmlUi dependency remains intentionally absent.

`FR-09-T03`: Seeds the runtime bootstrap boundary and first route-open probe for
`main`, `game`, and `download_status`. No renderer bridge or native draw path is
implemented yet.

`FR-03-T08`: Keeps the switch client-owned and exposes a narrow interface that
legacy UI code can call before falling back to existing presentation code.

`DV-04-T02`: Avoids mixed ownership expansion by centralizing the future route
claim decision in `src/client/ui_rml/` rather than spreading RmlUi checks across
individual menus.

## Validation Performed

- `meson compile -C builddir-win worr_engine_x86_64`
  - Result: blocked before client-engine link by existing/static archive tool
    failures:
    `llvm-ar.exe: error: cannot convert a regular archive to a thin one`.
  - The failure occurred while creating existing archives including
    `subprojects/SDL3_ttf-3.2.2/libsdl3_ttf.a`,
    `subprojects/curl-8.18.0/libcurl.a`, and `libq2proto.a`; it was not caused
    by RmlUi linkage, because this scaffold has no RmlUi dependency.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`
  - Result: passed after adding the project-standard `shared/shared.h` include
    before client UI headers.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_bridge.cpp.obj -v`
  - Result: passed after coordinator bridge integration.
- `clang++ '@.tmp/rmlui-ui-syntax.rsp'`
  - Result: passed syntax-only validation for the touched legacy
    `src/client/ui/ui.cpp` integration point.
- No RmlUi headers or libraries are required by the new scaffold.
- No q2proto files were changed.
- No Vulkan renderer paths were redirected or touched.

## Remaining Work

- Add the accepted RmlUi dependency and build options under `FR-09-T02`.
- Implement native renderer bridges for OpenGL, Vulkan, and RTX/vkpt without
  routing Vulkan work through OpenGL.
- Replace the stub return path with real document loading only after the
  runtime, renderer, filesystem, font, input, and data-controller bridges are in
  place.
