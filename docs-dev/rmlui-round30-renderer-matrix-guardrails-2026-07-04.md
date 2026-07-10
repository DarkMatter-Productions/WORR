# RmlUi Round 30 Renderer Matrix Guardrails

Date: 2026-07-04

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 30 adds a focused renderer-family matrix guardrail for the guarded RmlUi
runtime path. The new validation separates the current supported proof from
the future Gate G1 requirement:

- OpenGL is the only current guarded native RmlUi renderer lane. It must keep
  the OpenGL-scoped RmlUi dependency, export the concrete
  `R_RmlUiOpenGLRenderInterface`, report `CanRender=true`, return the native
  `Rml::RenderInterface`, and remain the renderer used by the existing runtime
  capture harness.
- Vulkan and RTX/vkpt remain explicit family lanes, but they must continue to
  report unavailable exports (`family=NONE`, `CanRender=false`,
  `NativeRenderInterface=NULL`) until native renderer bridges exist.
- The checker fails if Vulkan or RTX/vkpt are mapped to OpenGL or if the Meson
  runtime dependency is enabled for non-OpenGL renderer lanes before a native
  bridge lands.

This is not Gate G1 renderer proof. It is a regression guard that makes the
current unsupported lanes visible and prevents accidental Vulkan-to-OpenGL
shortcuts while the native Vulkan and RTX/vkpt RmlUi render bridges are still
pending.

## Implementation

- Added `tools/ui_smoke/check_rmlui_renderer_matrix.py`.
- Added `tools/ui_smoke/test_check_rmlui_renderer_matrix.py` with focused
  tests for:
  - the accepted current matrix (`OpenGL=native_guarded`,
    `Vulkan=blocked_until_native`, `RTX/vkpt=blocked_until_native`);
  - JSON reporting of lane facts and counts;
  - OpenGL `CanRender=true` enforcement;
  - Vulkan-to-OpenGL mapping rejection;
  - Vulkan runtime dependency enablement rejection before a native bridge;
  - runtime capture harness rejection if it is no longer explicitly
    OpenGL-scoped.

## Current Matrix

| Lane | Round 30 status | Required next proof |
| --- | --- | --- |
| OpenGL | Guarded native lane accepted for the existing menu-route capture path. | Default route ownership, final font/text services, controllers, navigation, and parity proof. |
| Vulkan | Blocked until native bridge. The external renderer exports must stay unavailable and must not redirect to OpenGL. | Vulkan-native RmlUi render interface in the Vulkan renderer path, then route-visible capture evidence. |
| RTX/vkpt | Blocked until native bridge. The external renderer exports must stay unavailable and must not redirect to OpenGL. | RTX/vkpt-native RmlUi render interface with composition rules documented, then route-visible capture evidence. |

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_renderer_matrix.py`:
  passed with `6` focused tests.
- `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
  passed with `native_guarded_lanes=1`, `blocked_lanes=2`, `errors=0`, and
  `no_vulkan_or_rtx_to_opengl_redirect=true`.

## Remaining Work

- Implement native Vulkan RmlUi rendering in the Vulkan renderer path.
- Implement native RTX/vkpt RmlUi rendering in the path-tracing renderer path.
- Extend live runtime capture automation after those native lanes exist.
- Keep RmlUi disabled by default until Gate G1 validates route ownership,
  renderer coverage, input/navigation, font/text behavior, and fallback
  behavior across the supported matrix.
