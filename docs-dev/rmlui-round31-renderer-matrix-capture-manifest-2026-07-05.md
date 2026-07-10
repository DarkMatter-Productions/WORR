# RmlUi Round 31 Renderer Matrix Capture Manifest

Date: 2026-07-05

Tasks: `FR-09-T03`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`, `DV-06-T01`

## Summary

Round 31 connects the guarded OpenGL route-capture evidence to the Round 30
renderer-family guardrail. The runtime capture harness now has a
`--renderer-matrix` mode that emits one aggregate report containing:

- the guarded OpenGL `main`, `game`, and `download_status` route matrix;
- the static renderer-family guardrail with OpenGL marked
  `native_guarded`;
- Vulkan and RTX/vkpt marked `blocked_until_native`;
- an explicit failure path if the renderer guardrail detects
  Vulkan/RTX-to-OpenGL routing or premature non-OpenGL RmlUi runtime
  dependency enablement.

This still is not Gate G1 renderer proof. It packages the current evidence
honestly: OpenGL has route-visible guarded evidence, while Vulkan and RTX/vkpt
remain native-pending until renderer-owned RmlUi bridges exist.

## Implementation

- Extended `tools/ui_smoke/check_rmlui_runtime_capture.py` with:
  - `--renderer-matrix`;
  - aggregate JSON/text output;
  - renderer-matrix manifest writing;
  - dry-run output that prints the OpenGL route-matrix commands plus the
    current blocked-lane policy;
  - integration with `check_rmlui_renderer_matrix.py`.
- Extended `tools/ui_smoke/test_check_rmlui_runtime_capture.py` with focused
  tests for:
  - renderer-matrix dry-run output;
  - positive aggregate evidence with `3` OpenGL route captures and `3`
    renderer lanes;
  - failure when route evidence passes but the static Vulkan lane guard maps
    to OpenGL.

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
- `errors=0`

The OpenGL route evidence is the accepted guarded menu-route capture evidence
from Round 29. Round 31 did not launch a new engine capture and did not
generate Vulkan or RTX/vkpt screenshots.

## Validation

- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`:
  passed with `15` focused capture tests.
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_renderer_matrix.py tools\ui_smoke\test_check_rmlui_runtime_capture.py tools\ui_smoke\test_check_rmlui_dependency_integration.py tools\ui_smoke\test_check_rmlui_dependency_decision.py tools\ui_smoke\test_check_rmlui_cvar_inventory.py tools\ui_smoke\test_check_rmlui_runtime_assets.py tools\ui_smoke\test_check_rmlui_menu_entrypoints.py tools\ui_smoke\test_check_rmlui_runtime_stub_eligibility.py`:
  passed with `78` focused RmlUi tests.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --dry-run --renderer-matrix`:
  passed and printed the OpenGL route-matrix commands plus the
  `OpenGL=native_guarded`, `Vulkan=blocked_until_native`, and
  `RTX/vkpt=blocked_until_native` lane summary.
- `python tools\ui_smoke\check_rmlui_runtime_capture.py --renderer-matrix --install-dir .install --write-manifest .tmp\rmlui\runtime-capture\renderer-matrix.json --format json`:
  passed with the accepted aggregate counts above.
- `python tools\ui_smoke\check_rmlui_renderer_matrix.py --format json`:
  passed with `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`.
- `python tools\ui_smoke\check_rmlui_dependency_decision.py --format json`:
  passed with `ok=true` and `errors=[]`.

## Remaining Work

- Implement native Vulkan RmlUi rendering in the Vulkan renderer path.
- Implement native RTX/vkpt RmlUi rendering in the path-tracing renderer path.
- Replace the blocked-lane entries in the renderer matrix only after native
  lane captures exist.
- Keep RmlUi default-disabled until Gate G1 validates default route ownership,
  native renderer coverage, input/navigation, font/text behavior, and fallback
  behavior across the supported matrix.
