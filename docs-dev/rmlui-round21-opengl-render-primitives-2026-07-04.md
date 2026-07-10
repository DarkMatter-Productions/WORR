# RmlUi Round 21 OpenGL Render Primitives

Date: 2026-07-04

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`,
`DV-07-T04`

Status: accepted implementation slice; OpenGL has a native RmlUi primitive
bridge, while route/context rendering, Vulkan/RTX bridges, input/font services,
and live menu ownership remain pending.

## Summary

Round 21 turns the Round 20 OpenGL `Rml::RenderInterface` scaffold into a real
OpenGL-owned primitive backend. The bridge now compiles RmlUi geometry into
renderer-side caches, renders through WORR's existing OpenGL 2D tessellator,
loads renderer-managed images, uploads generated textures, releases generated
textures, and applies RmlUi scissor rectangles through OpenGL scissor state.

The bridge now reports `R_RmlUiCanRender=true` in RmlUi-enabled OpenGL builds
because its required render-interface methods are implemented. Route ownership
is still guarded separately by the compiled runtime's `CanOpenRoutes=false`, so
normal menu entry points continue to fall back to legacy UI until a RmlUi
context, frame draw loop, input path, and route ownership proof exist.

No Vulkan or RTX/vkpt renderer path is redirected to OpenGL. Non-OpenGL
renderer exports still report no RmlUi renderer family and no native render
interface until their native implementations exist.

## Implementation

- Updated `src/renderer/rmlui_bridge.cpp` so the OpenGL renderer's
  `Rml::RenderInterface` compiles immutable RmlUi geometry into
  `glVertexDesc2D_t` and `glIndex_t` buffers owned by the renderer bridge.
- Converts RmlUi premultiplied vertex colors back to straight alpha before
  handing colors to the existing OpenGL 2D tessellator, which currently uses
  the engine's normal alpha blend state.
- Implements `RenderGeometry` by flushing pending 2D work, resolving the RmlUi
  texture handle to an OpenGL texture, copying translated vertices and indices
  into `tess`, enabling blend/smooth shading flags, and flushing the batch.
- Implements generated texture upload with `qglGenTextures`,
  `GL_ForceTexture`, `qglTexImage2D`, linear filtering, clamp-to-edge where
  supported, and texture upload telemetry.
- Converts generated premultiplied RGBA texture pixels back to straight alpha
  before upload for consistency with the existing blend mode.
- Implements loaded texture handles through `IMG_Find(..., IT_PIC, IF_NONE)`.
  Those handles are tracked but not deleted by RmlUi because the image manager
  owns their lifetime.
- Implements generated texture lifetime tracking and deletes only generated
  textures on `ReleaseTexture`.
- Implements `EnableScissorRegion` and `SetScissorRegion` with
  `GL_SCISSOR_TEST`, top-left to OpenGL bottom-left coordinate conversion, and
  `draw.scissor` tracking.
- Wrapped the OpenGL renderer header include in `extern "C"` so the C++ bridge
  links against the existing C renderer globals/functions.
- Updated the compiled runtime initialization diagnostic to report that route
  context rendering is pending, rather than saying the native renderer bridge
  itself is pending.
- Updated `tools/ui_smoke/check_rmlui_runtime_adapter.py` and its focused
  tests so validation now checks geometry caching, tessellator rendering,
  texture upload/lifetime behavior, scissor state handling, OpenGL-scoped
  dependency wiring, `CanRender=true`, and no Vulkan-to-OpenGL redirection.

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

- Runtime-adapter validation passed with `errors=0`, including the new OpenGL
  primitive bridge checks.
- Dependency-integration validation passed with state `optional`; guarded
  client/OpenGL `UI_RML_HAS_RUNTIME` defines remain optional.
- Dependency-decision validation passed with the route-rendering, Vulkan, and
  legacy-removal guardrails intact.
- Focused RmlUi dependency/runtime pytest passed.
- The default-disabled Windows build linked `worr_engine_x86_64.dll` and
  `worr_opengl_x86_64.dll`.
- The RmlUi-enabled scratch build linked `worr_engine_x86_64.dll` and
  `worr_opengl_x86_64.dll` against the compiled RmlUi Core path.
- `git diff --check` passed with only existing LF-to-CRLF normalization
  warnings.

## Non-Claims

- No RmlUi context is created for a menu route.
- No route opens through RmlUi from a normal menu entry point.
- No RmlUi document is advanced, updated, or drawn from the client UI frame
  loop.
- No Vulkan or RTX/vkpt `Rml::RenderInterface` implementation exists.
- No input, font/text, cursor/audio, localization, accessibility, screenshot,
  or parity evidence is claimed.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

The next slice should create the first guarded RmlUi context path for a sample
route: establish context dimensions, load one document, wire a conservative
update/render call from the client UI frame, and keep legacy fallback active
unless the route proves it can draw. Vulkan and RTX/vkpt must still wait for
their own native render bridges and must not route through OpenGL.
