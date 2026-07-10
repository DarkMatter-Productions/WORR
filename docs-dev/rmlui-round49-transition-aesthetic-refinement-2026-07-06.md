# RmlUi Round 49 Transition Aesthetic Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T06`, `FR-09-T07`,
`FR-09-T08`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 49 adds a conservative aesthetic pass on top of the staged OpenGL RmlUi
menu path. Shared route chrome now has lighter header/footer framing, controls
now use short RmlUi-native color/border transitions, and settings, session, and
utility surfaces now respond to hover/focus with subtler visual feedback.

The work is intentionally RCSS-only. It does not claim live controller-backed
settings persistence, live session/list behavior, native Vulkan/RTX-vkpt RmlUi
rendering, final input/focus parity, final localization/text shaping parity, or
end-user documentation readiness.

## Implementation

- `assets/ui/rml/common/theme/base.rcss`
  - Added shared header/footer divider treatment so menu chrome reads as a
    stable framed shell without adding nested cards.
  - Added short `background-color`/`border-color`/`color` transitions to
    buttons and fields using RmlUi-supported transition syntax.
  - Added input/select/textarea hover treatment so focusable fields have a
    visible but restrained affordance before live controller wiring arrives.
- `assets/ui/rml/common/theme/settings.rcss`
  - Added section dividers for long settings forms.
  - Added hover/focus feedback for compact toggle inputs.
  - Tuned the Sound form height so the final visible row is complete at
    `960x720`.
- `assets/ui/rml/common/theme/singleplayer.rcss`
  - Tuned the Start Server settings form height so footer actions remain
    visible and the scroll viewport does not end on a partial row.
- `assets/ui/rml/common/theme/session.rcss`
  - Added subtle transitions and hover states to session panels, status cards,
    flow steps, player rows, and admin rows.
- `assets/ui/rml/common/theme/utility.rcss`
  - Added matching hover/transition treatment to utility toolbars, form panels,
    preview panels, utility panels, and keybind buttons.

## Validation

Build/stage validation:

- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `meson compile -C builddir-win`

Static validation:

- `python tools/ui_smoke/check_rmlui_runtime_adapter.py`
- `python tools/ui_smoke/check_rmlui_runtime_registry.py`
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py`
- `rg -n "var\(|gap:" assets\ui\rml -g "*.rcss"`

Runtime and screenshot evidence:

- `.install\basew\logs\rmlui_round49_aesthetic_capture.log`
  - Focused capture over `main`, `game`, `options`, `performance`, `sound`,
    `downloads`, `keys`, `players`, `servers`, `callvote_main`, `dm_join`,
    `admin_commands`, `ui_list`, and `startserver`.
  - Recorded `15` total opens, `14` unique route IDs, and `0`
    failure/error/exception/unhandled/parser/transition/animation/unsupported
    hits.
- `.tmp\rmlui\round49-screens\rmlui_round49_contact_sheet_1.png`
- `.tmp\rmlui\round49-screens\rmlui_round49_contact_sheet_2.png`
  - Visual review confirmed the shared hover/framing treatment across shell,
    settings, session, and utility surfaces.
- `.install\basew\logs\rmlui_round49_form_spacing_capture.log`
  - Focused recapture after the first settings-section spacing adjustment.
- `.tmp\rmlui\round49-screens\rmlui_round49_refined_performance_960x720.png`
  - Performance form remains contained with visible footer actions.
- `.tmp\rmlui\round49-screens\rmlui_round49_refined2_startserver_960x720.png`
  - Start Server form ends on complete rows above visible Back/Close actions.
- `.install\basew\logs\rmlui_round49_sound_final_capture.log`
  - Final Sound recapture after the route-specific height adjustment.
- `.tmp\rmlui\round49-screens\rmlui_round49_refined3_sound_960x720.png`
  - Sound form ends on a complete Music row above visible footer actions.
- `.install\basew\logs\rmlui_round49_final_all_route_open.log`
  - Final staged OpenGL all-route pass recorded `59` opened documents, `58`
    unique route IDs, `0`
    failure/error/exception/unhandled/parser/transition/animation/unsupported
    hits, and Quake II Rerelease TTF font-source markers.

## Remaining Gaps

- Live controller-backed settings, list, keybind, and session behavior.
- Focus traversal, scroll behavior, and input parity beyond the current guarded
  staged OpenGL capture path.
- Native Vulkan and RTX/vkpt RmlUi render-interface activation.
- Final localization/text shaping policy and live accessibility preference
  wiring, including user-facing reduced-motion controls.
- Automated screenshot assertions for every migrated menu route.
