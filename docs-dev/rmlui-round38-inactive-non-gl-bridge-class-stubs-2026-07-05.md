# RmlUi Round 38 Inactive Non-OpenGL Bridge Class Stubs

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 38 adds inactive Vulkan and RTX/vkpt RmlUi render-interface class stubs
to `src/renderer/rmlui_bridge.cpp`. The stubs advance the
`native_bridge_class_present` activation requirement for both non-OpenGL
lanes while keeping their family exports, RmlUi runtime dependencies, native
render-interface exports, and route-visible captures blocked.

The accepted state is now:

- `activation_requirements=10`
- `satisfied_activation_requirements=4`
- `pending_activation_requirements=6`
- `partial_activation_lanes=2`
- `native_bridge_lanes=0`

This round does not implement native Vulkan or RTX/vkpt rendering. It advances
one additional activation requirement per non-OpenGL lane and leaves the next
blocker as `native_family_export_present`.

## Implementation

- Added guarded inert class stubs:
  - `R_RmlUiVulkanRenderInterface`
  - `R_RmlUiRtxVkptRenderInterface`
- Kept the non-OpenGL renderer exports unavailable:
  - no Vulkan or RTX/vkpt renderer-family export;
  - no non-null native render-interface export;
  - no `UI_RML_HAS_RUNTIME` compile define or RmlUi dependency for
    `renderer_vk` or `renderer_vk_rtx`.
- Tightened `src/renderer/rmlui_bridge.cpp` so the OpenGL render-interface
  implementation and OpenGL native export remain behind the OpenGL renderer
  family guard.
- Updated the bridge-readiness checker so class stubs are accepted only while
  inactive, and still fail if paired with premature non-OpenGL family/runtime
  or interface exports.
- Updated fixture coverage so standalone bridge-readiness and aggregate
  renderer-matrix tests require:
  - `native_bridge_class_present=true`;
  - `native_bridge_source_compiled=true`;
  - `activation_status=partial_activation_blocked`;
  - `next_activation_requirement=native_family_export_present`;
  - `satisfied_activation_requirements=4`;
  - `pending_activation_requirements=6`.

## Accepted Evidence

The accepted standalone bridge-readiness facts are:

- `ok=true`
- `lanes=2`
- `foundation_lanes=2`
- `native_bridge_lanes=0`
- `blocked_lanes=2`
- `activation_complete_lanes=0`
- `partial_activation_lanes=2`
- `inactive_activation_lanes=0`
- `activation_requirements=10`
- `satisfied_activation_requirements=4`
- `pending_activation_requirements=6`
- `missing_bridge_requirements=6`
- `errors=0`

The accepted aggregate renderer manifest facts include:

- `ok=true`
- `routes=3`
- `route_passed=3`
- `route_failed=0`
- `renderer_lanes=3`
- `native_guarded_lanes=1`
- `blocked_lanes=2`
- `bridge_lanes=2`
- `bridge_foundation_lanes=2`
- `native_bridge_lanes=0`
- `bridge_blocked_lanes=2`
- `bridge_activation_complete_lanes=0`
- `bridge_partial_activation_lanes=2`
- `bridge_inactive_activation_lanes=0`
- `bridge_activation_requirements=10`
- `bridge_satisfied_activation_requirements=4`
- `bridge_pending_activation_requirements=6`
- `missing_bridge_requirements=6`
- `errors=0`

Round 38 did not launch a fresh engine capture and did not generate Vulkan or
RTX/vkpt screenshots.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_runtime_adapter.py`:
  passed with `32` focused bridge/capture/adapter tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `87` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
  passed with the accepted standalone inactive class-stub counts above.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
  passed with the accepted aggregate inactive class-stub counts above.
- `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
  passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed with `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Add lane-specific Vulkan and RTX/vkpt renderer-family exports without
  routing through OpenGL.
- Enable non-OpenGL RmlUi runtime dependencies only after native classes and
  exports can provide renderer-owned interfaces.
- Implement native Vulkan and RTX/vkpt render-interface methods.
- Keep Gate G1 open until OpenGL, Vulkan, and RTX/vkpt all provide native
  route-visible renderer proof plus the required font/text, input/navigation,
  controller, fallback, and parity evidence.
