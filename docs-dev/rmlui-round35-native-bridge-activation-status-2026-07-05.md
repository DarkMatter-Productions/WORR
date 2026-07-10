# RmlUi Round 35 Native Bridge Activation Status

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 35 extends the native bridge activation checklist with lane status and
next-blocker reporting. Round 34 made the Vulkan/RTX activation requirements
machine-readable; Round 35 makes their progression state explicit.

The current accepted state remains:

- Vulkan: `activation_status=blocked_no_activation`
- RTX/vkpt: `activation_status=blocked_no_activation`
- `activation_complete_lanes=0`
- `partial_activation_lanes=0`
- `inactive_activation_lanes=2`

This round does not implement native Vulkan or RTX/vkpt rendering. It makes the
next native renderer work easier to review by identifying whether a lane has no
activation work, partial activation that must stay blocked, or a complete
activation set that is ready for the next proof gate.

## Implementation

- Extended `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py` with
  per-lane activation state helpers:
  - `blocked_no_activation`;
  - `partial_activation_blocked`;
  - `activation_complete`.
- Added per-lane JSON fields:
  - `activation_status`;
  - `activation_complete`;
  - `satisfied_activation_requirement_ids`;
  - `pending_activation_requirement_ids`;
  - `next_activation_requirement`.
- Added standalone bridge-readiness counts:
  - `activation_complete_lanes`;
  - `partial_activation_lanes`;
  - `inactive_activation_lanes`.
- Added aggregate renderer-matrix counts:
  - `bridge_activation_complete_lanes`;
  - `bridge_partial_activation_lanes`;
  - `bridge_inactive_activation_lanes`.
- Extended text output to print `activation_status` and
  `next_activation_requirement` for each bridge-readiness lane.
- Tightened the partial Vulkan bridge-class regression test so one satisfied
  requirement reports `partial_activation_blocked`, keeps the next blocker as
  `native_family_export_present`, and still fails the lane.

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
- `activation_requirements=8`
- `satisfied_activation_requirements=0`
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
- `bridge_partial_activation_lanes=0`
- `bridge_inactive_activation_lanes=2`
- `bridge_activation_requirements=8`
- `bridge_satisfied_activation_requirements=0`
- `bridge_pending_activation_requirements=8`
- `missing_bridge_requirements=8`
- `errors=0`

The OpenGL route evidence still reuses the accepted guarded route captures.
Round 35 did not launch a fresh engine capture and did not generate Vulkan or
RTX/vkpt screenshots.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
  passed with `23` focused bridge/capture tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `86` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
  passed with the accepted standalone activation-status counts above.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
  passed with the accepted aggregate activation-status counts above.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Implement native Vulkan RmlUi rendering in `rend_vk` without routing through
  OpenGL.
- Implement native RTX/vkpt RmlUi rendering in `rend_rtx`/`vkpt` without
  routing through OpenGL.
- Treat `partial_activation_blocked` as a failure state until all activation
  requirements for a lane are satisfied together and native route-visible
  capture evidence exists.
- Keep Gate G1 open until OpenGL, Vulkan, and RTX/vkpt all provide native
  route-visible renderer proof plus the required font/text, input/navigation,
  controller, fallback, and parity evidence.
