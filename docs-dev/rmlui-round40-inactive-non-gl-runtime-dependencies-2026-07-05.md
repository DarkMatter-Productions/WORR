# RmlUi Round 40: Inactive Non-OpenGL Runtime Dependencies

Date: 2026-07-05

Task IDs: `FR-09-T02`, `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-06-T01`, `DV-07-T04`

## Summary

Round 40 advances the native Vulkan/RTX-vkpt bridge activation checklist by
satisfying `runtime_dependency_enabled` for both non-OpenGL lanes. It does not
export non-null native render interfaces, does not make Vulkan or RTX/vkpt
route-visible, and does not redirect either lane through OpenGL.

The intended baseline after this round is:

- `activation_requirements=10`
- `satisfied_activation_requirements=8`
- `pending_activation_requirements=2`
- `missing_bridge_requirements=2`
- `native_bridge_lanes=0`
- `next_activation_requirement=native_interface_export_present`

## Implementation

- `meson.build` now adds `rmlui_dep` and `-DUI_RML_HAS_RUNTIME=1` to:
  - `renderer_vk_deps` / `renderer_vk_cpp_args`;
  - `renderer_vk_rtx_deps` / `renderer_vk_rtx_cpp_args`.
- The RTX/vkpt renderer now uses a dedicated `renderer_vk_rtx_deps` variable
  so the optional RmlUi dependency can be attached without changing unrelated
  renderer dependency wiring.
- `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py` now records the
  non-OpenGL runtime dependency as an activation requirement while keeping it
  inactive until a native interface export exists.
- `tools/ui_smoke/check_rmlui_renderer_matrix.py` now reports
  `runtime_dependency_inactive` for Vulkan and RTX/vkpt instead of requiring
  their runtime dependencies to remain disabled.
- `tools/ui_smoke/check_rmlui_runtime_adapter.py` now validates RmlUi runtime
  dependency coverage across all renderer DLL lanes.

## Validation

Accepted validation for this round:

- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`
  - Result: `38 passed`.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`
  - Result: `87 passed`.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`
  - Result: `ok=true`, `satisfied_activation_requirements=8`,
    `pending_activation_requirements=2`,
    `missing_bridge_requirements=2`, `native_bridge_lanes=0`, and
    `next_activation_requirement=native_interface_export_present`.
- `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`
  - Result: `ok=true`, `native_guarded_lanes=1`, `blocked_lanes=2`, and
    `errors=0`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`
  - Result: `ok=true`, `renderer_runtime_dependencies=true`, and
    `errors=[]`.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`
  - Result: `ok=true`, `bridge_satisfied_activation_requirements=8`,
    `bridge_pending_activation_requirements=2`,
    `missing_bridge_requirements=2`, and `errors=0`.

No C++ build was run in this round, so `.install/` was not refreshed.

## Remaining Blocker

The next activation blocker is `native_interface_export_present`. Vulkan and
RTX/vkpt must not export a non-null native RmlUi render interface until their
renderer-owned geometry, texture, clip, and frame integration can consume it
natively.
