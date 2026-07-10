# RmlUi Round 51 Widget Layout Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T05`, `FR-09-T09`

## Summary

Round 51 refines the staged OpenGL RmlUi menu presentation around typed
settings widgets and consistent non-main menu layout. The main menu keeps its
separate shell treatment from Round 50; settings, single-player setup,
download status, reusable controls, and utility forms now share a clearer
horizontal control rhythm.

## Implementation

- `assets/ui/rml/common/theme/settings.rcss` now defines a wider settings
  form contract with `720px` forms, `700px` sections, `676px` rows, typed
  label/control/value columns, and short RmlUi-native row transitions.
- `data-control` rows now receive control-specific treatment:
  - `toggle` rows reserve the full label lane and align compact square
    checkbox controls at the right edge.
  - `range` rows use slider-width controls with a dedicated value lane.
  - `select`, `combo`, and `imagevalues` rows share stable select widths.
  - `field` rows keep text and numeric inputs narrower than selects, with
    room for helper values.
  - `progress` rows style the download progress element through the same
    shared control contract.
- `assets/ui/rml/common/theme/base.rcss` now gives generic checkbox, range,
  progress, and `.actions` surfaces the same color, border, and transition
  language used by the newer menu panels.
- `assets/ui/rml/common/theme/singleplayer.rcss` now aligns Start Server and
  Deathmatch Flags with the shared settings widths while preserving their
  bounded scroll regions and visible footer actions at `960x720`.
- `assets/ui/rml/common/theme/utility.rcss` now gives form labels and fields
  explicit pixel widths, fixing empty Address Book text inputs that collapsed
  under RmlUi when `maxlength` was present.
- `assets/ui/rml/common/components/controls.rcss` now matches the current
  shared control palette instead of the earlier starter palette.
- `assets/ui/rml/shell/download_status.rml` now imports the shared settings
  stylesheet and opts into `settings-screen`, so its progress row uses the
  same typed-widget layout as the rest of the menu stack.

## Validation

- Build/stage:
  - `meson compile -C builddir-win`: passed; no compile work was required.
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`: passed after the final RCSS/RML changes.
- Static/runtime guardrails:
  - `python tools\ui_smoke\check_rmlui_runtime_adapter.py`: passed.
  - `python tools\ui_smoke\check_rmlui_runtime_registry.py`: passed.
  - `python tools\ui_smoke\check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`: passed.
  - `rg -n "var\(|gap:|box-shadow|filter:|calc\(|@media|:checked|::" assets\ui\rml -g "*.rcss"`: only comment hits for `var()` wording.
  - `python -m pytest tools\ui_smoke\test_check_rmlui_runtime_adapter.py tools\ui_smoke\test_check_rmlui_runtime_capture.py`: `25 passed`.
- Runtime visual evidence:
  - `.install\basew\logs\rmlui_round51_widget_layout_capture2.log`: opened
    representative typed-widget routes at `960x720` using
    `ui_rml_runtime_open`.
  - `.install\basew\logs\rmlui_round51_utility_form_recapture.log`: confirmed
    the utility form-width correction for `players` and `addressbook`.
  - `.tmp\rmlui\round51-screens\rmlui_round51_widget_layout_contact_sheet.png`:
    visual inspection covers `addressbook`, `crosshair`, `download_status`,
    `downloads`, `gameflags`, `input`, `multimonitor`, `performance`,
    `players`, `sound`, `startserver`, and `video`.
  - `.tmp\rmlui\round51-screens\rmlui_round51_addressbook_960x720_full.png`:
    visual inspection confirms Address Book text fields are full-width and no
    longer collapse into narrow boxes.
  - `.tmp\rmlui\round51-screens\rmlui_round51_players_960x720_full.png`:
    visual inspection confirms the player form and preview remain balanced.
  - `.install\basew\logs\rmlui_round51_final_all_route_open.log`: final staged
    OpenGL sweep recorded `59` opened documents, `58` unique route IDs,
    `0` failure/error/exception/unhandled/parser/transition/animation/
    unsupported hits, and Quake II Rerelease font-source markers.

## Remaining Gaps

This round is a layout and presentation refinement. It does not complete live
data-model/controller behavior, keyboard/controller navigation parity,
automated pixel assertions for every route, localization/text shaping parity,
or native Vulkan/RTX-vkpt RmlUi rendering. The expected controller-stub
warnings for missing live data models remain visible in broad runtime sweeps.
