# RmlUi Agent 1 Runtime Probe Round 5

Date: 2026-07-02

Owner lane: Agent 1, client runtime scaffold

Task IDs: `FR-09-T02`, `FR-09-T03`, `FR-09-T09`, `FR-03-T08`, `DV-04-T02`

## Summary

This round advances the client-owned RmlUi scaffold from a route-name switch to
a dependency-free runtime asset probe. The engine can now resolve a small,
explicit route registry to runtime document paths under `ui_rml_asset_root` and
load those documents through `FS_LoadFileEx`.

The scaffold still does not link RmlUi, create a renderer bridge, parse RML, or
claim menu ownership. `UI_Rml_OpenMenu` continues to return `false` after the
probe so the legacy UI remains authoritative until a real RmlUi runtime is
available.

## Changed Files

- `src/client/ui_rml/ui_rml.h`
  - Adds `UI_Rml_DocumentForRoute` for resolving a registered route to its
    runtime document path.
  - Adds `UI_Rml_ProbeRoute` for probing a registered route document through
    the engine filesystem.
- `src/client/ui_rml/ui_rml.cpp`
  - Adds the registered route/document pairs:
    - `main` -> `ui/rml/shell/main.rml`
    - `game` -> `ui/rml/shell/game.rml`
    - `download_status` -> `ui/rml/shell/download_status.rml`
    - `core.runtime_smoke` -> `ui/rml/core/runtime_smoke.rml`
  - Registers `ui_rml_asset_root`, defaulting to `ui/rml`.
  - Registers `ui_rml_probe [route_id]` and deregisters it during scaffold
    shutdown.
  - Uses `FS_LoadFileEx` and `FS_FreeFile` to verify document availability from
    runtime search paths, including loose files or packaged assets.
  - Probes mapped documents when `ui_rml_enable` is non-zero and
    `UI_Rml_OpenMenu` receives `main`, `game`, or `download_status`.
- `docs-dev/rmlui-agent1-runtime-probe-round5-2026-07-02.md`
  - Records this implementation and validation evidence.

## Runtime Contract

Registered cvars:

- `ui_rml_enable`: defaults to `0`; opt-in gate for menu-open route probes.
- `ui_rml_debug`: defaults to `0`; emits lifecycle logging.
- `ui_rml_asset_root`: defaults to `ui/rml`; prefixes registered document
  paths before probing.

Registered command:

- `ui_rml_probe [route_id]`
  - With no argument, probes every registered route document.
  - With a route ID, probes only that route.
  - Completes known route IDs.
  - Reports success with the resolved runtime path and byte count, or reports
    filesystem/path failures with `Q_ErrorString`.

The probe intentionally uses runtime paths such as
`ui/rml/shell/main.rml`, not repository source paths such as
`assets/ui/rml/shell/main.rml`.

## Implementation Notes

- The route registry is static and intentionally small for this round. It
  covers the active menu-open bridge routes plus the core runtime smoke route.
- `UI_Rml_DocumentForRoute` returns a stable static buffer for immediate use by
  engine code. It returns `NULL` for unknown routes or paths that exceed the
  Quake runtime path buffer.
- `UI_Rml_ProbeRoute` performs a real load and immediately frees the loaded
  buffer. This verifies that the document can be found and read without adding
  RmlUi document parsing or ownership semantics.
- Command registration is guarded against duplicate initialization and
  deregistered only if this scaffold registered the command.
- No `q2proto/` files were changed.
- No renderer files were changed; Vulkan paths remain native and untouched.

## Task Mapping

`FR-09-T02`: Keeps the scaffold dependency-free while establishing the
filesystem boundary a future RmlUi integration will use for document loads.

`FR-09-T03`: Adds the first runtime bootstrap probe for shell and smoke route
documents without implementing document parsing or rendering.

`FR-09-T09`: Provides an in-engine smoke hook that can validate packaged or
loose route documents from runtime search paths.

`FR-03-T08`: Keeps route probing in the client-owned UI boundary and preserves
legacy UI fallback behavior.

`DV-04-T02`: Centralizes the early RmlUi route/document decision in
`src/client/ui_rml/` instead of spreading document probes through individual
legacy menu implementations.

## Validation Performed

- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj -v`
  - Result: passed.
  - The object rebuilt with C++20 using the existing engine APIs.
  - Ninja also emitted `warning: premature end of file; recovering` after the
    compile, but the command exited successfully.

## Remaining Work

- Add the real RmlUi dependency and runtime document loading once the dependency
  and renderer bridge tasks are accepted.
- Add native renderer support for OpenGL, Vulkan, and RTX/vkpt without routing
  Vulkan work through OpenGL.
- Expand the route registry or generate it from route manifests after ownership
  and runtime phase gates are defined.
- Promote probe success into real route ownership only after controller,
  renderer, input, and parity evidence is in place.
