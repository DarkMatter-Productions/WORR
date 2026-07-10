# RmlUi Round 33 Bridge Readiness Renderer Manifest

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 33 connects the Round 32 Vulkan/RTX bridge-readiness audit to the
existing `--renderer-matrix` runtime capture manifest. The aggregate renderer
manifest now reports three linked evidence groups:

- guarded OpenGL `main`, `game`, and `download_status` route evidence;
- the renderer-family guardrail with OpenGL as `native_guarded` and
  Vulkan/RTX-vkpt as `blocked_until_native`;
- the Vulkan/RTX bridge-readiness audit proving native renderer foundations
  are present while native bridge requirements remain intentionally missing.

This still is not native Vulkan or RTX/vkpt renderer proof. It makes the
current blocked-lane evidence harder to misread by pairing each blocked lane
with its foundation inventory and missing native bridge checklist.

## Implementation

- Extended `tools/ui_smoke/check_rmlui_runtime_capture.py --renderer-matrix`
  to build `check_rmlui_vulkan_bridge_readiness.py` reports alongside the
  existing OpenGL route matrix and renderer-family guardrail.
- The aggregate JSON/text report now includes:
  - `bridge_readiness`;
  - `bridge_lanes`;
  - `bridge_foundation_lanes`;
  - `native_bridge_lanes`;
  - `bridge_blocked_lanes`;
  - `missing_bridge_requirements`.
- The aggregate fails if the OpenGL route matrix, renderer-family guardrail,
  or bridge-readiness audit fails.
- Dry-run output now prints both the renderer lane policy and the
  bridge-readiness lane policy.
- Extended `tools/ui_smoke/test_check_rmlui_runtime_capture.py` so renderer
  manifest tests require bridge-readiness counts and fail when a Vulkan
  foundation primitive disappears.

## Accepted Evidence

The accepted local aggregate manifest is:

- `.tmp/rmlui/runtime-capture/renderer-matrix.json`

The manifest recorded:

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
- `missing_bridge_requirements=8`
- `errors=0`

The OpenGL route evidence still reuses the accepted guarded route captures.
Round 33 did not launch a fresh engine capture and did not generate Vulkan or
RTX/vkpt screenshots.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
  passed with `16` focused capture tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_vulkan_bridge_readiness.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `85` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --dry-run --renderer-matrix`:
  passed and printed the OpenGL route commands, renderer lane policy, and
  bridge-readiness lane policy.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
  passed with the accepted aggregate counts above.
- `python tools\ui_smoke\check_rmlui_vulkan_bridge_readiness.py --format json`:
  passed with `foundation_lanes=2`, `native_bridge_lanes=0`, and `errors=0`.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Implement native Vulkan RmlUi rendering in `rend_vk`.
- Implement native RTX/vkpt RmlUi rendering in `rend_rtx`/`vkpt`.
- Replace blocked readiness lanes only after native renderer-owned
  `Rml::RenderInterface` classes, Meson runtime dependency wiring, renderer
  API family exports, non-null native interfaces, and route-visible captures
  exist.
- Keep Gate G1 open until OpenGL, Vulkan, and RTX/vkpt all provide native
  route-visible renderer proof plus the required font/text, input/navigation,
  controller, fallback, and parity evidence.
