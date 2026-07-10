# RmlUi Round 37 Inactive Non-OpenGL Bridge Source Wiring

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 37 wires `src/renderer/rmlui_bridge.cpp` into the Vulkan and RTX/vkpt
renderer source sets while keeping their RmlUi runtime dependency disabled.
The source compiles in inactive mode for those renderer DLLs, so the lanes can
prove source-set participation without claiming a native RmlUi bridge.

The accepted state is now:

- `activation_requirements=10`
- `satisfied_activation_requirements=2`
- `pending_activation_requirements=8`
- `partial_activation_lanes=2`
- `native_bridge_lanes=0`

This round does not implement native Vulkan or RTX/vkpt rendering. It advances
one activation requirement per non-OpenGL lane and leaves the next blocker as
`native_bridge_class_present`.

## Implementation

- Added `src/renderer/rmlui_bridge.cpp` to `renderer_vk_src`.
- Added `src/renderer/rmlui_bridge.cpp` to `renderer_vk_rtx_src`.
- Kept `renderer_vk_cpp_args`, `renderer_vk_rtx_cpp_args`,
  `renderer_vk_deps`, and `renderer_vk_rtx_deps` free of RmlUi runtime
  enablement.
- Extended the bridge-readiness checker to detect multi-line Meson source
  lists for `native_bridge_source_compiled`.
- Updated the runtime-adapter checker to accept the shared bridge source in
  the required OpenGL, Vulkan, and RTX/vkpt renderer source sets instead of
  enforcing the old single-listing assumption.
- Updated fixture coverage so the standalone bridge-readiness and aggregate
  renderer-matrix tests require:
  - `native_bridge_source_compiled=true`;
  - `activation_status=partial_activation_blocked`;
  - `next_activation_requirement=native_bridge_class_present`;
  - `satisfied_activation_requirements=2`;
  - `pending_activation_requirements=8`.
- Added runtime-adapter regression coverage that fails if an inactive
  non-OpenGL renderer source set drops `src/renderer/rmlui_bridge.cpp`.

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
- `satisfied_activation_requirements=2`
- `pending_activation_requirements=8`
- `missing_bridge_requirements=8`
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
- `bridge_satisfied_activation_requirements=2`
- `bridge_pending_activation_requirements=8`
- `missing_bridge_requirements=8`
- `errors=0`

Round 37 did not launch a fresh engine capture and did not generate Vulkan or
RTX/vkpt screenshots.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
  passed with `23` focused bridge/capture tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `87` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
  passed with the accepted standalone inactive source-set counts above.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
  passed with the accepted aggregate inactive source-set counts above.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Implement native Vulkan and RTX/vkpt `Rml::RenderInterface` classes.
- Add lane-specific renderer-family exports without routing through OpenGL.
- Enable RmlUi runtime dependencies for non-OpenGL lanes only after native
  bridge classes and exports exist.
- Keep Gate G1 open until OpenGL, Vulkan, and RTX/vkpt all provide native
  route-visible renderer proof plus the required font/text, input/navigation,
  controller, fallback, and parity evidence.
