# RmlUi Round 39: Inactive Non-OpenGL Family Exports

Date: 2026-07-05

Task IDs: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`, `DV-07-T04`

## Summary

Round 39 advances the native Vulkan/RTX-vkpt bridge activation checklist by
satisfying `native_family_export_present` for both non-OpenGL lanes. It does
not enable RmlUi runtime dependencies for Vulkan or RTX/vkpt, does not export
non-null native render interfaces, and does not make either lane route-visible.

The intended baseline after this round is:

- `activation_requirements=10`
- `satisfied_activation_requirements=6`
- `pending_activation_requirements=4`
- `missing_bridge_requirements=4`
- `native_bridge_lanes=0`
- `next_activation_requirement=runtime_dependency_enabled`

## Implementation

- `src/renderer/rmlui_bridge.cpp` now reports distinct inactive renderer
  family identities for the non-OpenGL renderer builds:
  - `RENDERER_VULKAN_LEGACY` returns `R_RENDERER_RMLUI_FAMILY_VULKAN`;
  - `RENDERER_VULKAN_RTX` returns `R_RENDERER_RMLUI_FAMILY_RTX_VKPT`.
- `R_RmlUiRendererName()` now returns inactive lane names for Vulkan and
  RTX/vkpt while the OpenGL runtime-enabled path keeps its existing native
  render-interface name.
- `src/renderer/renderer_api.c` exports `R_RmlUiRendererFamily` and
  `R_RmlUiRendererName` for every renderer family, but keeps non-OpenGL
  `RmlUiCanRender=false` and `RmlUiNativeRenderInterface=NULL`.
- `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py` now treats family
  exports as accepted inactive activation only when the bridge source/class are
  present, runtime dependencies remain disabled, and native interface exports
  remain unavailable.
- `tools/ui_smoke/check_rmlui_renderer_matrix.py` now permits inactive
  non-OpenGL family exports while still failing Vulkan/RTX-to-OpenGL shortcut
  wiring or render-ready claims without native interfaces.

## Validation

Accepted validation for this round:

- `python -m pytest tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - Result: `29 passed`.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`
  - Result: `87 passed`.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`
  - Result: `ok=true`, `satisfied_activation_requirements=6`,
    `pending_activation_requirements=4`,
    `missing_bridge_requirements=4`, `native_bridge_lanes=0`, and
    `next_activation_requirement=runtime_dependency_enabled`.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`
  - Result: `ok=true`, `bridge_satisfied_activation_requirements=6`,
    `bridge_pending_activation_requirements=4`,
    `missing_bridge_requirements=4`, and `errors=0`.
- `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`
  - Result: `ok=true`, `native_guarded_lanes=1`, `blocked_lanes=2`, and
    `errors=0`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`
  - Result: `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`
  - Result: `ok=true` and `errors=[]`.

No C++ build was run in this round, so `.install/` was not refreshed.

## Remaining Blockers

- Enable the RmlUi runtime dependency for Vulkan and RTX/vkpt only when the
  native bridge can consume it without redirecting through OpenGL.
- Export non-null native Vulkan/RTX-vkpt `Rml::RenderInterface` instances only
  after renderer-owned geometry, texture, clip, and frame integration is ready.
- Preserve the existing OpenGL guarded route evidence while expanding native
  renderer coverage.
