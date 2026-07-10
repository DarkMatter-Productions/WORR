# RmlUi Round 20 OpenGL Render Interface Scaffold

Date: 2026-07-03

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`,
`DV-07-T04`

Status: accepted implementation slice; native OpenGL draw/upload behavior,
Vulkan/RTX bridges, route rendering, and live menu ownership remain pending.

## Summary

Round 20 adds the first renderer-family implementation against the native
renderer bridge contract from Round 19. The implemented family is OpenGL only:
`worr_opengl_x86_64.dll` now exports an RmlUi `Rml::RenderInterface` object
when WORR is built with `-Drmlui=enabled`.

The bridge is intentionally not render-ready. The OpenGL object satisfies the
RmlUi interface shape and can be installed into RmlUi Core, but its draw,
geometry, texture, and scissor methods are conservative no-ops. It reports
`R_RmlUiCanRender=false`, so route ownership still resolves to
`renderer_unavailable` and normal menu entry points keep falling back to the
legacy UI.

No Vulkan or RTX/vkpt renderer path is redirected to OpenGL. Non-OpenGL
renderer exports explicitly report no RmlUi renderer family and no native
render interface until their native implementations exist.

## Implementation

- Added renderer-side RmlUi bridge declarations to `inc/renderer/renderer.h`:
  - `renderer_rmlui_family_t`
  - `R_RmlUiRendererFamily`
  - `R_RmlUiRendererName`
  - `R_RmlUiCanRender`
  - `R_RmlUiNativeRenderInterface`
  - matching `renderer_export_t` slots for external renderer DLLs.
- Added `src/renderer/rmlui_bridge.cpp` to the OpenGL renderer source list.
  When `UI_RML_HAS_RUNTIME` is enabled, it defines an OpenGL
  `Rml::RenderInterface` scaffold and returns it through the opaque native
  interface hook.
- Kept `R_RmlUiCanRender()` returning `false` until the OpenGL bridge uploads
  geometry/textures and draws visible output.
- Updated `src/renderer/renderer_api.c` so OpenGL exports the concrete bridge
  functions and non-OpenGL renderers export an unavailable RmlUi bridge.
- Updated `src/client/renderer.cpp` so the client maps renderer-side family
  values to the UI scaffold families, registers the bridge after renderer init,
  and clears it during renderer shutdown.
- Updated `src/client/ui_rml/ui_rml_runtime.cpp` so the compiled adapter
  installs the native `Rml::RenderInterface` before `Rml::Initialise` when a
  renderer supplies one.
- Updated Meson external renderer C++ arguments so renderer-side C++ sources
  receive generated config/q2proto platform defines. The RmlUi dependency and
  `UI_RML_HAS_RUNTIME` renderer define remain scoped to the OpenGL renderer
  scaffold.
- Extended `tools/ui_smoke/check_rmlui_runtime_adapter.py` and focused tests
  to validate renderer API exports, OpenGL-only scaffold registration,
  OpenGL-scoped RmlUi renderer dependency wiring, client lifecycle
  registration/clear, `Rml::SetRenderInterface`, `CanRender=false`, and no
  Vulkan-to-OpenGL redirection.

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
  renderer API, OpenGL scaffold, Meson scoping, client lifecycle, render
  interface installation, and no-redirect guard checks.
- Dependency-integration validation passed with state `optional`; the guarded
  client and OpenGL renderer `UI_RML_HAS_RUNTIME` defines remain optional
  rather than default-enabled.
- Dependency-decision validation passed with the Round 20 active status and
  remaining no-route-rendering/no-legacy-removal guardrails intact.
- Focused RmlUi dependency/runtime pytest passed with `25 passed`.
- The default-disabled Windows build linked `worr_engine_x86_64.dll` and
  `worr_opengl_x86_64.dll`.
- The RmlUi-enabled scratch build linked `worr_engine_x86_64.dll` and
  `worr_opengl_x86_64.dll` against the compiled RmlUi Core path.
- `git diff --check` passed with only existing LF-to-CRLF normalization
  warnings.

## Non-Claims

- The OpenGL bridge does not upload geometry, generate textures, load images,
  set scissor state, or draw visible output yet.
- No Vulkan or RTX/vkpt `Rml::RenderInterface` implementation exists.
- No RmlUi context is created for a menu route.
- No route opens through RmlUi from a normal menu entry point.
- No live controller, input, font, cursor, accessibility, localization, or
  screenshot/parity evidence is claimed.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

The next slice should turn the OpenGL scaffold into the first visible native
draw path: compile/cache geometry, upload generated and loaded textures through
OpenGL-owned renderer resources, honor scissor state, and prove one sample
route can create a RmlUi context and draw while the legacy fallback remains
available. Vulkan and RTX/vkpt must still wait for their own native bridges.
