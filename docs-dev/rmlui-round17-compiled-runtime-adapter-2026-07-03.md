# RmlUi Round 17 Compiled Runtime Adapter

Date: 2026-07-03

Task IDs: `FR-09-T02`, `FR-09-T03`, `FR-09-T09`, `DV-06-T01`,
`DV-03-T07`, `DV-07-T04`

Status: accepted implementation slice; native renderer and live route
ownership remain pending.

## Summary

Round 17 advances the RmlUi migration from a dependency-free runtime hook to a
compiled RmlUi Core adapter boundary. The default build remains guarded and
default-disabled, but an enabled scratch build now resolves the pinned RmlUi
`6.2` source, compiles RmlUi Core, compiles the WORR runtime adapter, and links
`worr_engine_x86_64.dll`.

This is intentionally not a route-rendering milestone. The compiled adapter
reports `renderer_unavailable` and refuses route opening until a native WORR
RmlUi renderer bridge exists for OpenGL, Vulkan, and RTX/vkpt paths.

## Implementation

- Added `src/client/ui_rml/ui_rml_runtime.cpp`, guarded by
  `UI_RML_HAS_RUNTIME`, to register a compiled runtime interface.
- Referenced real RmlUi Core entry points through the adapter:
  `Rml::GetVersion`, `Rml::Initialise`, and `Rml::Shutdown`.
- Added local RmlUi include guards for WORR macro collisions around
  `DotProduct` and `CrossProduct`.
- Extended `ui_rml_runtime_interface_t` with `CanOpenRoutes` so the scaffold
  can distinguish a compiled Core runtime from a renderer-capable runtime.
- Added `renderer_unavailable` availability reporting when RmlUi Core is
  compiled but no native route-opening renderer bridge is registered.
- Added `UI_Rml_RegisterCompiledRuntime` registration from `UI_Rml_Init` when
  `UI_RML_HAS_RUNTIME` is enabled.
- Added `src/client/ui_rml/ui_rml_runtime.cpp` to `meson.build`.
- Updated RmlUi dependency resolution to prefer an external `RmlUi::Core`
  CMake package, then external pkg-config `rmlui`, then the pinned CMake
  subproject fallback.
- Configured the fallback with `RMLUI_FONT_ENGINE=none`,
  `RMLUI_LUA_BINDINGS=false`, `RMLUI_SAMPLES=false`, and `RMLUI_TESTS=false`
  for this compile-only Core adapter proof.
- Added `[provide]` dependency aliases to `subprojects/rmlui.wrap`.
- Added `tools/ui_smoke/check_rmlui_runtime_adapter.py` plus focused tests for
  the guarded adapter, conservative route-open guard, renderer-unavailable
  state, Meson fallback options, and wrap provide aliases.
- Updated `tools/ui_smoke/check_rmlui_dependency_integration.py` so feature
  option variables such as `required: rmlui_opt` remain optional in the static
  dependency-state report.

## Validation

Accepted validation commands:

```text
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python tools\ui_smoke\check_rmlui_dependency_integration.py --format json
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_dependency_integration.py
meson setup builddir-win --reconfigure -Drmlui=disabled
ninja -C builddir-win worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml.cpp.obj worr_engine_x86_64.dll.p/src_client_ui_rml_ui_rml_runtime.cpp.obj
meson setup .tmp\rmlui\round17-rmlui-enabled3 -Drmlui=enabled
ninja -C .tmp\rmlui\round17-rmlui-enabled3 subprojects/RmlUi-6.2/rmlui_core.dll
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll
git diff --check -- meson.build meson_options.txt subprojects/rmlui.wrap src/client/ui_rml tools/ui_smoke
```

Results:

- The runtime-adapter checker passed with the adapter listed in Meson, the
  RmlUi Core include guarded, macro collision guards present, all three RmlUi
  Core symbols referenced, the conservative `CanOpenRoutes` false guard
  present, and no errors.
- The dependency-integration checker passed with state `optional`, optional
  external probes, the pinned wrap/source fallback, `subproject('rmlui')`,
  fallback `dependency('rmlui_core')`, and no errors.
- Focused pytest coverage passed with `15 passed`.
- The default-disabled builddir reconfigured cleanly and the scaffold/adapter
  translation units compiled in the normal Windows build tree.
- The enabled scratch build linked `subprojects/RmlUi-6.2/rmlui_core.dll` and
  `worr_engine_x86_64.dll`.
- `git diff --check` passed for touched build/runtime/smoke files; Git reported
  existing line-ending conversion warnings for some files.

## Notes From Enabled Fallback Bring-Up

- The first forced fallback configure attempted RmlUi's default FreeType font
  engine and failed because `Freetype::Freetype` was not available. The
  fallback now sets `RMLUI_FONT_ENGINE=none` until the real font/text bridge is
  designed under `FR-09-T04` and `DV-06-T01`.
- Meson's CMake subproject exposure provided `rmlui_core` rather than a generic
  `RmlUi::RmlUi` alias, so WORR uses `RmlUi::Core` for external CMake package
  discovery and `rmlui_core` for the fallback dependency.
- RmlUi's vector headers define `DotProduct` and `CrossProduct` methods, which
  collided with WORR macros. The adapter undefines those macros locally before
  including RmlUi Core.

## Non-Claims

- No native RmlUi renderer bridge is implemented.
- No route opens or draws through RmlUi at runtime.
- No live controller, cvar, command, input, font, localization, or filesystem
  bridge is complete.
- No screenshot/layout or renderer parity evidence is claimed.
- No Vulkan path is redirected to OpenGL.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

The next implementation slice should keep `ui_rml_enable` guarded while adding
the first native renderer/system bridge proof. Gate G1 still requires at least
one route to open from a normal WORR menu entry point and draw through native
OpenGL, Vulkan, and RTX/vkpt paths, without Vulkan-to-OpenGL fallback.
