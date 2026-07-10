# RmlUi Round 50 Scaling and Positioning Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T06`, `FR-09-T09`,
`DV-07-T02`, `DV-07-T04`

## Summary

Round 50 fixes the RmlUi menu scaling and viewport anchoring problems observed
when launching through the staged OpenGL path at different window sizes and
aspect ratios. The main menu no longer shrinks to a content-width sidebar or
clips the right edge of its buttons, and widescreen/fullscreen-style captures
now use a stable `960x720` reference canvas that scales up without crushing
the logical route layout.

The work is still OpenGL-staged RmlUi runtime work. It does not claim native
Vulkan/RTX-vkpt RmlUi activation, live controller parity, full route visual
parity, or final end-user documentation.

## Implementation

- `src/client/ui_rml/ui_rml.cpp`
  - Added a RmlUi-specific reference canvas scale based on `960x720`.
  - Converted framebuffer mouse input into that RmlUi canvas scale.
  - Drew the software cursor with the same renderer scale used for RmlUi
    route rendering.
- `src/client/ui_rml/ui_rml_runtime.cpp`
  - Matched runtime context dimensions to the same `960x720` reference-scale
    calculation.
  - Set the renderer 2D scale immediately before `Rml::Context::Render()` so
    RmlUi layout coordinates, scissor conversion, and OpenGL draw coordinates
    agree after resize/fullscreen-style mode changes.
- `assets/ui/rml/common/theme/base.rcss`
  - Anchored `body` and `.screen` to the RmlUi viewport with absolute
    top/right/bottom/left edges.
  - Kept screens as flex containers inside that full-viewport surface.
- `assets/ui/rml/common/theme/shell.rcss`
  - Gave the shared menu action stack a stable width and made the main-menu
    action column explicit so button borders are not clipped by intrinsic
    text sizing.

## Validation

Build/stage validation:

- `meson compile -C builddir-win`
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`

Static validation:

- `python tools/ui_smoke/check_rmlui_runtime_adapter.py`
- `python tools/ui_smoke/check_rmlui_runtime_registry.py`
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py`
- `rg -n "var\(|gap:" assets\ui\rml -g "*.rcss"`
- `git diff --check -- src/client/ui_rml/ui_rml.cpp src/client/ui_rml/ui_rml_runtime.cpp assets/ui/rml/common/theme/base.rcss assets/ui/rml/common/theme/shell.rcss`

Runtime and screenshot evidence:

- `.install\basew\logs\rmlui_round50_main_964x765.log`
  - Captured `main` at `964x765`; runtime dimensions reported `960x762`.
- `.install\basew\logs\rmlui_round50_main_1280x720.log`
  - Captured `main` at `1280x720`; runtime dimensions reported `1280x720`.
- `.install\basew\logs\rmlui_round50_main_1280x960.log`
  - Captured `main` at `1280x960`; smoke validation passed.
- `.install\basew\logs\rmlui_round50_main_2048x1152.log`
  - Captured `main` at `2048x1152`; runtime dimensions reported `1280x720`.
- `.tmp\rmlui\round50-screens\rmlui_round50_main_scaling_contact_sheet.png`
  - Visual review confirms the main menu fills the viewport and button right
    edges remain visible at the tested aspect ratios.
- `.install\basew\logs\rmlui_round50_final_main_2048x1152.log`
  - Final staged widescreen capture after install refresh passed with Q2R TTF
    source markers, synthetic input markers, and `1280x720` runtime
    dimensions.
- `.tmp\rmlui\round50-screens\rmlui_round50_final_main_2048x1152.png`
  - Final visual evidence for the fullscreen-style main menu fix.
- `.install\basew\logs\rmlui_round50_final_all_route_open.log`
  - Final staged all-route OpenGL sweep recorded `59` opened documents, `58`
    unique route IDs, `0`
    failure/error/exception/unhandled/parser/transition/animation/unsupported
    hits, and Quake II Rerelease TTF font-source markers.

## Remaining Gaps

- Automated visual assertions still need to understand menu-specific clipping
  and viewport-fill expectations.
- Live controller-backed menu behavior, focus traversal, and scrollbar parity
  remain pending.
- Native Vulkan and RTX/vkpt RmlUi render-interface activation remains
  pending.
