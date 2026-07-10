# RmlUi Round 15 Worker 3 Runtime Interface Preparation

## Project Tasks

- `FR-09-T03`: Runtime bootstrap and native renderer integration.
- `DV-06-T01`: Dependency baseline audit.

## Scope

Round 15 Worker 3 prepared the dependency-free `src/client/ui_rml/`
scaffold for a future real RmlUi runtime without adding the dependency,
changing Meson wiring, or changing legacy UI fallback behavior.

## Implementation

- Added explicit runtime availability states:
  - `uninitialized`
  - `disabled`
  - `runtime_not_compiled`
  - `runtime_unavailable`
  - `ready`
- Added a small dependency-free file interface that currently routes document
  probes through `FS_LoadFileEx(..., TAG_FILESYSTEM)` and frees loaded data with
  `FS_FreeFile`.
- Added a dependency-free runtime hook interface with `Init`, `Shutdown`,
  `OpenRoute`, and `RuntimeName` callbacks. Registration is only compiled when
  `UI_RML_HAS_RUNTIME` is enabled, so the scaffold still builds without RmlUi
  headers or libraries.
- Refactored `UI_Rml_OpenMenu()` so it only hands a route to a runtime when:
  - `ui_rml_enable` is enabled,
  - the route document probe succeeds,
  - runtime hooks are compiled and registered,
  - runtime startup succeeds.

## Behavior Preserved

- `ui_rml_enable` remains registered with default `0`.
- Menu route probes still use WORR filesystem loading and still print probe
  success/failure diagnostics.
- With the current dependency-free build, `UI_Rml_OpenMenu()` always returns
  `false` and the legacy cgame UI remains authoritative.
- Defining `UI_RML_HAS_RUNTIME=1` without registering a runtime still falls
  back to legacy UI through the `runtime_unavailable` state.
- No RmlUi include path or type is required by this slice.
- No Vulkan renderer path is redirected to OpenGL.

## Validation

- `ninja -C builddir-win -t clean worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj` passed.
- `ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj` passed.
- A scratch compile of `src/client/ui_rml/ui_rml.cpp` with
  `UI_RML_HAS_RUNTIME=1` passed and wrote
  `.tmp/ui_rml_round15_runtime.obj`.
- `git diff --check -- src/client/ui_rml docs-dev/rmlui-agent3-runtime-interface-round15-2026-07-02.md` passed.
- Because the owned files are currently untracked, an additional no-index
  `git diff --check` pass against an empty `.tmp/` file was run for
  `ui_rml.h`, `ui_rml.cpp`, and this note; it produced no whitespace errors.
