# RmlUi Round 34 Native Bridge Activation Checklist

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 34 adds a structured activation checklist to the Vulkan/RTX bridge-
readiness audit and to the aggregate renderer manifest. The previous readiness
report listed missing bridge requirements. The new report records each
requirement as a named boolean so partial native work can be reviewed without
silently promoting a renderer lane.

The current accepted state remains intentionally blocked:

- `activation_requirements=8`
- `satisfied_activation_requirements=0`
- `pending_activation_requirements=8`
- `native_bridge_lanes=0`

This round does not implement native Vulkan or RTX/vkpt rendering. It makes the
next native renderer round measurable: a lane is still blocked until its native
bridge class, native family export, runtime dependency, and non-null native
render-interface export all exist together.

## Implementation

- Extended `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py` so each
  non-OpenGL lane reports `activation_requirements`.
- The activation requirements for each lane are:
  - `native_bridge_class_present`;
  - `native_family_export_present`;
  - `runtime_dependency_enabled`;
  - `native_interface_export_present`.
- Added aggregate counts:
  - `activation_requirements`;
  - `satisfied_activation_requirements`;
  - `pending_activation_requirements`.
- Kept `missing_bridge_requirements` wired to the same activation checklist so
  earlier blocked-lane reporting remains readable.
- Extended text output to print the activation requirement state for each lane.
- Extended `tools/ui_smoke/check_rmlui_runtime_capture.py --renderer-matrix`
  so aggregate renderer manifests include the bridge activation counts as:
  - `bridge_activation_requirements`;
  - `bridge_satisfied_activation_requirements`;
  - `bridge_pending_activation_requirements`.
- Added a partial Vulkan bridge-class claim regression test. It proves a single
  satisfied activation item still fails until the full native Vulkan bridge is
  wired and the blocked-lane guardrail is deliberately relaxed.

## Accepted Evidence

The accepted standalone bridge-readiness facts are:

- `ok=true`
- `lanes=2`
- `foundation_lanes=2`
- `native_bridge_lanes=0`
- `blocked_lanes=2`
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
- `bridge_activation_requirements=8`
- `bridge_satisfied_activation_requirements=0`
- `bridge_pending_activation_requirements=8`
- `missing_bridge_requirements=8`
- `errors=0`

The OpenGL route evidence still reuses the accepted guarded route captures.
Round 34 did not launch a fresh engine capture and did not generate Vulkan or
RTX/vkpt screenshots.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
  passed with `23` focused bridge/capture tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `86` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
  passed with the accepted standalone bridge-readiness counts above.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
  passed with the accepted aggregate counts above.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Implement native Vulkan RmlUi rendering in `rend_vk` without routing through
  OpenGL.
- Implement native RTX/vkpt RmlUi rendering in `rend_rtx`/`vkpt` without
  routing through OpenGL.
- Promote a non-OpenGL lane only when all activation requirements for that lane
  are satisfied and route-visible capture evidence exists for the native
  renderer.
- Keep Gate G1 open until OpenGL, Vulkan, and RTX/vkpt all provide native
  route-visible renderer proof plus the required font/text, input/navigation,
  controller, fallback, and parity evidence.
