# RmlUi Round 32 Vulkan/RTX Bridge Readiness Audit

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 32 adds a static bridge-readiness audit for the native Vulkan and
RTX/vkpt RmlUi lanes. The audit does not implement either bridge and does not
promote either lane to renderer proof. It records two facts that need to stay
true until native work lands:

- Vulkan and RTX/vkpt already expose renderer-native UI/image/draw foundations
  that future RmlUi bridges should use.
- Vulkan and RTX/vkpt must remain `blocked_until_native` until each renderer
  owns a real `Rml::RenderInterface`, family export, runtime dependency, and
  non-null native interface.

This gives the next native renderer round a machine-readable checklist without
allowing Vulkan or RTX/vkpt to borrow the OpenGL bridge.

## Implementation

- Added `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py`.
- The checker validates shared non-OpenGL RmlUi guardrails:
  - Vulkan and RTX/vkpt renderer families remain declared and distinct.
  - Client renderer mappings keep the non-OpenGL families distinct.
  - Non-OpenGL renderer API exports still report `family=NONE`,
    `CanRender=false`, and `NativeRenderInterface=NULL`.
  - Meson does not enable `UI_RML_HAS_RUNTIME=1` or `rmlui_dep` for Vulkan or
    RTX/vkpt renderer builds yet.
  - Vulkan/RTX-to-OpenGL shortcut mappings fail validation.
- The checker inventories Vulkan foundations in `src/rend_vk/`:
  - `VK_UI_Draw*` and `R_Draw*` UI draw entrypoints;
  - `VK_UI_BeginFrame`, `VK_UI_EndFrame`, and `VK_UI_Record`;
  - raw image registration and RGBA update paths;
  - descriptor-set access for UI images;
  - clip/scissor state through `R_SetClipRect`, `VK_UI_SetClipRect`, and
    `vkCmdSetScissor`.
- The checker inventories RTX/vkpt foundations in `src/rend_rtx/`:
  - `R_DrawPic`, `R_DrawStretchPic`, and stretch-pic enqueueing;
  - `vkpt_draw_clear_stretch_pics` and `vkpt_draw_submit_stretch_pics`;
  - image registration and texture update paths;
  - descriptor texture updates and shader sampling;
  - clip state and stretch-pic shaders.
- Added `tools/ui_smoke/test_check_rmlui_vulkan_bridge_readiness.py` with
  positive JSON/text coverage and failures for premature Vulkan runtime
  dependency enablement, premature Vulkan family claims, Vulkan-to-OpenGL
  routing, and missing Vulkan draw primitives.

## Accepted Evidence

The accepted Round 32 checker facts are:

- `ok=true`
- `lanes=2`
- `foundation_lanes=2`
- `native_bridge_lanes=0`
- `blocked_lanes=2`
- `missing_bridge_requirements=8`
- `errors=0`

The missing bridge requirements remain intentional for this round:

- renderer-owned Vulkan `Rml::RenderInterface` class;
- Vulkan renderer-family export;
- Vulkan RmlUi runtime dependency;
- non-null Vulkan native render-interface export;
- renderer-owned RTX/vkpt `Rml::RenderInterface` class;
- RTX/vkpt renderer-family export;
- RTX/vkpt RmlUi runtime dependency;
- non-null RTX/vkpt native render-interface export.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py`:
  passed with `6` focused bridge-readiness tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `84` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py`:
  passed with `Malformed findings: 0`.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
  passed with the accepted counts above.
- `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
  passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Implement native Vulkan RmlUi rendering in `rend_vk` without routing through
  OpenGL.
- Implement native RTX/vkpt RmlUi rendering in `rend_rtx`/`vkpt` without
  routing through OpenGL.
- Replace the blocked-lane guardrails only when each native lane has a
  renderer-owned `Rml::RenderInterface`, Meson dependency wiring, renderer API
  family export, and route-visible capture evidence.
- Keep Gate G1 open until OpenGL, Vulkan, and RTX/vkpt all provide native
  route-visible renderer proof plus the required font/text, input/navigation,
  controller, fallback, and parity evidence.
