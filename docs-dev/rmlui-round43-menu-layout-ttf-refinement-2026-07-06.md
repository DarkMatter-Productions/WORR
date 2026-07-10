# RmlUi Round 43 Menu Layout and TTF Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`,
`DV-07-T02`, and `DV-07-T04`.

## Summary

Round 43 moves the guarded OpenGL RmlUi menu path from "loads routes" toward
"looks like usable menus." It adds a SDL3_ttf-backed RmlUi font interface,
keeps the existing bitmap smoke font path only as a compile fallback, tightens
the shared RmlUi menu layout themes so long lists stay inside the active menu
canvas, and replaces visible migration/debug fallback copy with player-facing
menu text on utility and session surfaces.

This round does not complete live C++ data-model/controller bindings, native
Vulkan/RTX RmlUi rendering, localization flow, audio affordances, or final
legacy JSON removal.

## Implementation Notes

- `src/client/ui_rml/ui_rml_runtime.cpp` now installs
  `UI_Rml_TtfFontEngineInterface` when SDL3_ttf is enabled. The interface
  resolves packaged UI and monospace fonts first, falls back to platform font
  paths where needed, measures text through SDL_ttf, and emits RmlUi callback
  textures for rendered text geometry.
- The runtime logs the TTF evidence markers:
  - `RmlUi TTF font face 'WORR UI' loaded ...`
  - `RmlUi TTF font face 'WORR Mono' loaded ...`
  - `RmlUi TTF font engine generated text texture ...`
- `assets/ui/rml/common/theme/base.rcss`, `settings.rcss`, `shell.rcss`,
  `singleplayer.rcss`, `session.rcss`, and `utility.rcss` now use bounded
  flex/scroll ownership for menu bodies, action rows, save slots, session
  lists, utility panels, and keybind-style long lists.
- Utility keybind pages now wrap grouped buttons into readable columns at
  `960x720`, preventing the primary key list from drawing past the vertical
  canvas.
- Utility/session visible fallback copy was refined from migration-oriented
  wording into player-facing menu text while keeping the existing `data-*`
  hooks for future controller work.
- Component styles that previously used unsupported browser-like spacing were
  normalized to RmlUi-compatible margins.

## Validation

- `meson compile -C builddir-win`
  - Passed with no work pending after the final asset-only edits.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
  - Passed after the final asset refresh.
  - Staged `198` packaged assets.
  - Validated `31` botfile package/loose files.
  - Validated `104` RmlUi package/loose files.
- `python tools/ui_smoke/check_rmlui_runtime_adapter.py`
  - Passed with the TTF font engine path accepted.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
  - Passed with `57` routes, `57` source documents, `7` imported assets, and
    `64` staged loose runtime paths present.
- `python tools/ui_smoke/check_rmlui_runtime_registry.py`
  - Passed with `57` manifest routes and `58` registered routes including
    `core.runtime_smoke`.
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py`
  - Passed: `25` tests.
- Staged runtime file probe:
  - `.install/basew/logs/rmlui_round43_refine_file_probe.log`
  - Recorded `58` `RmlUi runtime file probe OK` entries.
  - Logged both TTF font faces and the TTF text texture marker.
- Staged final all-route open pass:
  - `.install/basew/logs/rmlui_round43_final_all_route_open.log`
  - Recorded `59` opened documents including startup `main` plus all `58`
    registered routes.
  - No `failed`, `error`, `Exception`, `Unhandled`, or parser hits.
  - Logged both TTF font faces and the TTF text texture marker.
- Visual evidence:
  - `.tmp/rmlui/round43-screens/rmlui_round43_keys_final_960x720.png`
  - Shows `keys` rendered at `960x720` with readable TTF text, wrapped
    keybind columns, visible cursor, and no list row drawing past the vertical
    screen extent.

## Follow-Up

RmlUi still logs expected "Could not locate data model" notices on controller
stub routes because live data-model/controller registration remains pending in
`FR-09-T05`, `FR-09-T07`, and `FR-09-T08`. Those notices did not block route
loading, rendering, TTF text generation, or the final screenshot evidence, but
they remain the next functional parity gap.
