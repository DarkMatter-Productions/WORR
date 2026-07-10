# RmlUi Round 19 Native Renderer Bridge Contract

Date: 2026-07-03

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`,
`DV-07-T04`

Status: accepted implementation slice; native renderer implementations and
live route ownership remain pending.

## Summary

Round 19 adds the first native renderer bridge contract for the RmlUi runtime
path. The client scaffold now has explicit renderer-family lanes for OpenGL,
Vulkan, and RTX/vkpt, an opaque native render-interface hook, and route
availability logic that refuses to open RmlUi routes unless a renderer family
has registered a real native interface.

The route guard remains conservative. The compiled RmlUi Core adapter still
returns `CanOpenRoutes=false`, and normal menu entry points still fall back to
the legacy UI path. No Vulkan path is redirected to OpenGL.

## Implementation

- Added `ui_rml_renderer_family_t` with explicit native families:
  `UI_RML_RENDERER_FAMILY_OPENGL`, `UI_RML_RENDERER_FAMILY_VULKAN`, and
  `UI_RML_RENDERER_FAMILY_RTX_VKPT`.
- Added `ui_rml_renderer_interface_t` with:
  - `RendererName` for diagnostics.
  - `CanRender` for renderer-side readiness.
  - `NativeRenderInterface` as an opaque future `Rml::RenderInterface` hook
    without exposing RmlUi types in the public scaffold header.
- Added renderer registration and query helpers:
  - `UI_Rml_SetRendererInterface`
  - `UI_Rml_ClearRendererInterface`
  - `UI_Rml_RendererInterface`
  - `UI_Rml_RendererFamily`
  - `UI_Rml_RendererFamilyString`
  - `UI_Rml_RendererName`
  - `UI_Rml_RendererIsAvailable`
- Updated route availability so `UI_Rml_RuntimeCanOpenRoutes` requires native
  renderer availability before consulting the runtime's route-open hook.
- Updated debug/fallback logging to report the registered renderer name and
  family when RmlUi remains unavailable.
- Kept the compiled adapter's `CanOpenRoutes=false` guard intact. Even if a
  renderer later registers, document context creation and route rendering must
  be implemented before the runtime can claim menu ownership.
- Expanded `tools/ui_smoke/check_rmlui_runtime_adapter.py` and its focused
  tests to validate:
  - renderer contract declarations;
  - OpenGL, Vulkan, and RTX/vkpt family coverage;
  - scaffolded renderer registration/query helpers;
  - route availability gated by native renderer availability;
  - native render-interface pointer required for availability;
  - no Vulkan-to-OpenGL redirection.

## Validation

Accepted validation commands:

```text
python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json
python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py
ninja -C builddir-win worr_engine_x86_64.dll
ninja -C .tmp\rmlui\round17-rmlui-enabled3 worr_engine_x86_64.dll
```

Results:

- Runtime-adapter validation passed with renderer contract declarations,
  OpenGL/Vulkan/RTX-vkpt family coverage, route availability gating, native
  interface requirements, guarded RmlUi Core/system/file interfaces, WORR
  filesystem/system symbols, conservative route-open guard, and wrap fallback
  evidence confirmed.
- Focused runtime-adapter pytest passed with `7 passed`.
- The default-disabled Windows build linked `worr_engine_x86_64.dll`.
- The RmlUi-enabled scratch build linked `worr_engine_x86_64.dll` against the
  compiled RmlUi Core adapter.

## Non-Claims

- No renderer module registers `UI_Rml_SetRendererInterface` yet.
- No `Rml::RenderInterface` implementation exists for OpenGL, Vulkan, or
  RTX/vkpt.
- No route opens, creates a RmlUi context, or draws through RmlUi from normal
  menu entry points.
- No live controller, cvar, command, input, font, cursor, accessibility, or
  localization bridge is complete.
- No screenshot/layout, renderer parity, or end-user parity evidence is
  claimed.
- No legacy JSON menu path is removed or deprecated.

## Next Required Slice

The next slice should add the first renderer-family native bridge
implementation against this contract. It should register only the renderer path
it actually implements, keep Vulkan and RTX/vkpt separate from OpenGL, and
leave route ownership guarded until a sample route can create a RmlUi context
and draw through each active native renderer family.
