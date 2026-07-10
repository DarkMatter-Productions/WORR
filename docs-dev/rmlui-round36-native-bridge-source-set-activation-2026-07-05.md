# RmlUi Round 36 Native Bridge Source-Set Activation

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 36 adds source-set wiring to the native bridge activation checklist. A
non-OpenGL bridge class is not enough: the Vulkan or RTX/vkpt renderer DLL must
also compile the native RmlUi bridge source before that lane can be promoted.

The current accepted state remains blocked:

- Vulkan: `activation_status=blocked_no_activation`
- RTX/vkpt: `activation_status=blocked_no_activation`
- `activation_requirements=10`
- `satisfied_activation_requirements=0`
- `pending_activation_requirements=10`

This round does not implement native Vulkan or RTX/vkpt rendering. It closes a
build-proof gap before the native renderer matrix proof by making renderer DLL
source-set participation an explicit activation requirement.

## Implementation

- Extended `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py` with a new
  activation requirement for each non-OpenGL lane:
  - `native_bridge_source_compiled`.
- The checker now detects whether the lane-specific Meson renderer source set
  includes `src/renderer/rmlui_bridge.cpp` through `renderer_vk_src`,
  `renderer_vk_lib_src`, `renderer_vk_rtx_src`, or
  `renderer_vk_rtx_lib_src` wiring.
- The native interface export requirement now depends on source-set wiring as
  well as the native bridge class, renderer-family export, runtime dependency,
  and non-OpenGL renderer API export state.
- The partial Vulkan bridge-class regression now reports
  `next_activation_requirement=native_bridge_source_compiled`, proving class
  text alone is not enough to advance the lane.
- Aggregate renderer-matrix counts now reflect `10` total bridge activation
  requirements and `10` pending requirements.

## Accepted Evidence

The accepted standalone bridge-readiness facts are:

- `ok=true`
- `lanes=2`
- `foundation_lanes=2`
- `native_bridge_lanes=0`
- `blocked_lanes=2`
- `activation_complete_lanes=0`
- `partial_activation_lanes=0`
- `inactive_activation_lanes=2`
- `activation_requirements=10`
- `satisfied_activation_requirements=0`
- `pending_activation_requirements=10`
- `missing_bridge_requirements=10`
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
- `bridge_partial_activation_lanes=0`
- `bridge_inactive_activation_lanes=2`
- `bridge_activation_requirements=10`
- `bridge_satisfied_activation_requirements=0`
- `bridge_pending_activation_requirements=10`
- `missing_bridge_requirements=10`
- `errors=0`

The OpenGL route evidence still reuses the accepted guarded route captures.
Round 36 did not launch a fresh engine capture and did not generate Vulkan or
RTX/vkpt screenshots.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
  passed with `23` focused bridge/capture tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `86` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
  passed with the accepted standalone source-set activation counts above.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
  passed with the accepted aggregate source-set activation counts above.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Add lane-specific native RmlUi bridge source/build wiring for Vulkan and
  RTX/vkpt without routing through OpenGL.
- Implement native Vulkan RmlUi rendering in `rend_vk`.
- Implement native RTX/vkpt RmlUi rendering in `rend_rtx`/`vkpt`.
- Keep Gate G1 open until OpenGL, Vulkan, and RTX/vkpt all provide native
  route-visible renderer proof plus the required font/text, input/navigation,
  controller, fallback, and parity evidence.
