# RmlUi Round 55 Popup Audio Menu Refinement

Date: 2026-07-07

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T09`, `DV-07-T02`,
`DV-07-T04`

## Summary

Round 55 makes confirmation menus use an explicit RmlUi popup route path,
restores legacy-style menu feedback sounds through the engine UI bridge, and
polishes the sound settings page so the newly exposed menu music setting fits
cleanly at `960x720`.

The round keeps the existing RmlUi route/content strategy intact: confirmation
menus are still normal tracked route documents, but destructive entry points now
open them through `ui_rml_runtime_popup` so they behave as lightweight modal
confirmation surfaces instead of ordinary navigation pages.

## Implementation

- Added `ui_rml_runtime_popup <route_id>` as a popup-route command wrapper over
  the existing guarded runtime route opener.
- Added engine-side `UI_StartFeedbackSound()` in `src/client/ui_bridge.cpp`,
  mapping RmlUi menu feedback to the legacy menu samples:
  - open: `misc/menu1.wav`
  - move/change: `misc/menu2.wav`
  - close/back: `misc/menu3.wav`
  - alert/confirm: `misc/talk1.wav`
- Added RmlUi runtime event handling for `data-menu-sound`, popup commands,
  and `data-command-cvar` command elements.
- Converted Quit, Forfeit, Leave Match, and Tournament Replay confirmations to
  shared popup markup using `popup-screen`, `popup-dialog`, `popup-header`,
  `popup-message`, and `popup-actions`.
- Pointed destructive menu buttons at `ui.popup` plus `data-route-target` for
  the relevant confirmation route.
- Added popup styling with compact centered dialogs, red left accents, and
  side-by-side Yes/No actions.
- Added `data-menu-music="menu"` to the Sound Settings route and exposed
  `ogg_menu_track` as a numeric Menu Track field.
- Reworked Sound Settings into a two-column typed-widget layout so sound
  engine/volume controls and music/effects controls remain visible above the
  footer at `960x720`.

## Runtime Evidence

Focused final captures:

- `.tmp/rmlui/round55-screens/round55b_contact.png`
- `.tmp/rmlui/round55-screens/round55c_sound_960.png`

The captures confirm:

- Main Quit opens a compact confirmation-style route.
- Quit, Forfeit, Leave Match, and Replay confirmations use matching popup
  geometry and side-by-side Yes/No controls.
- Sound Settings uses the intended widget types, including selects, sliders,
  toggles, and the numeric menu-track field, without footer overlap.

Final staged OpenGL all-route sweep:

- log: `.install/basew/logs/rmlui_round55_final_all_route_open_b.log`
- opened documents: `59`
- unique route IDs: `58`
- runtime status samples: `58`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`
- Quake II Rerelease TTF font-source markers: `3`

Final popup command sweep:

- log: `.install/basew/logs/rmlui_round55_final_popup_command_b.log`
- popup route markers: `2`
- opened documents: `4`
- runtime status samples: `2`
- bad lines matching failure/error/exception/unhandled/parser/transition/
  animation/unsupported: `0`

## Validation

Accepted validation commands:

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python tools\ui_smoke\check_rmlui_runtime_adapter.py`
- `python tools\ui_smoke\check_rmlui_runtime_registry.py`
- `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`
- `python tools\ui_smoke\check_rmlui_semantics.py`
- `python tools\ui_smoke\check_rmlui_command_inventory.py`
- `python tools\ui_smoke\check_rmlui_condition_inventory.py`
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py`
- `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_capture.py`
- `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`

The focused pytest pass reported `9 passed` for runtime adapter coverage and
`16 passed` for runtime capture coverage.

## Remaining Gaps

This round does not claim live controller/data-model parity, route-wide
automated pixel clipping assertions, full keyboard/focus parity, native
Vulkan/RTX-vkpt RmlUi rendering, or full visual parity. Popup launch buttons
preserve their source legacy command as metadata where relevant, but live
sgame/cgame state-publication parity for those helper commands remains part of
the later controller bridge work.
