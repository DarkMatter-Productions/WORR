# RmlUi Round 44 Q2R Font and Menu Refinement

Date: 2026-07-06

Tasks: `FR-09-T03`, `FR-09-T04`, `FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 44 pins the guarded RmlUi TTF path to Quake II Rerelease font assets and tightens several menu layouts that became visibly cramped once the rerelease font metrics were active.

The RmlUi runtime now prefers fonts available through the normal WORR filesystem search path from the Quake II Rerelease `Q2Game.kpf` payload:

- `WORR Display`: `fonts/RussoOne-Regular.ttf`
- `WORR UI`: `fonts/Montserrat-Regular.ttf`
- `WORR Mono`: `fonts/RobotoMono-Regular.ttf`

The runtime records whether a loaded face came from the rerelease font set. Fallback platform fonts remain available only after the Quake II Rerelease candidates fail.

## Implementation

- `src/client/ui_rml/ui_rml_runtime.cpp`
  - Added Quake II Rerelease font candidates for the default display, UI, and monospace RmlUi faces.
  - Added source labeling for generated text textures so validation can prove whether the rendered glyph atlas came from a rerelease font.
  - Updated log messages to distinguish rerelease font loads from non-rerelease fallbacks.

- `assets/ui/rml/common/theme/base.rcss`
  - Added explicit block defaults for semantic RML tags used by menu documents.
  - Shared the menu footer layout outside `shell.rcss` so single-player forms keep visible Back/Close actions.
  - Routed headings and buttons through `WORR Display`, with body/input text remaining on `WORR UI`.

- `assets/ui/rml/common/theme/shell.rcss`
  - Converted the Options menu action list into a two-column wrap at `960x720`, keeping all entries and footer actions inside the visible canvas.

- `assets/ui/rml/common/theme/session.rcss`
  - Constrained `admin_commands` rows into readable command, description, and usage columns.
  - Prevented the first row from stretching into an oversized empty list area.

- `assets/ui/rml/common/theme/singleplayer.rcss`
  - Bounded Start Server and Deathmatch Flags form regions so dense controls scroll without pushing footer actions off screen.
  - Added explicit row/control widths for those forms to avoid narrow-column collapse.

- `assets/ui/rml/singleplayer/startserver.rml` and `assets/ui/rml/singleplayer/gameflags.rml`
  - Imported the shared settings theme so their `settings-form` and `setting-row` markup uses the same layout contract as the settings menu pages.

- `assets/ui/rml/session/admin_commands.rml`
  - Removed literal asterisk styling from the visible title.

- `tools/ui_smoke/check_rmlui_runtime_adapter.py` and tests
  - Added a static guard that requires the RmlUi runtime adapter to contain Quake II Rerelease TTF candidates and source markers.

- `tools/ui_smoke/check_rmlui_runtime_capture.py` and tests
  - Added a runtime log guard that requires the Quake II Rerelease font-source marker in accepted capture evidence.

- `assets/ui/rml/common/fonts/README.md` and `assets/ui/rml/common/theme/README.md`
  - Documented the three RmlUi font roles and their Quake II Rerelease source policy.

## Validation

Build and staging:

- `meson compile -C builddir-win`: passed, no work to do.
- `python tools/refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`: passed and refreshed `.install`.

Static checks:

- `python tools/ui_smoke/check_rmlui_runtime_adapter.py`: passed with `Runtime Q2R font candidates: yes`.
- `python tools/ui_smoke/check_rmlui_runtime_assets.py --include-imports --install-dir .install --base-game basew`: passed with `64` staged runtime paths present.
- `python tools/ui_smoke/check_rmlui_runtime_registry.py`: passed with `58` registered routes and `0` missing/duplicates.
- `python -m pytest tools/ui_smoke/test_check_rmlui_runtime_adapter.py tools/ui_smoke/test_check_rmlui_runtime_capture.py`: passed `25` tests.

Runtime checks:

- `.install/basew/logs/rmlui_round44_final_q2r_font_file_probe.log`
  - `58` runtime file probes passed.
  - Loaded `WORR Display` from `fonts/RussoOne-Regular.ttf`.
  - Loaded `WORR UI` from `fonts/Montserrat-Regular.ttf`.
  - Loaded `WORR Mono` from `fonts/RobotoMono-Regular.ttf`.
  - Generated text texture source included `Quake II Rerelease: fonts/Montserrat-Regular.ttf`.

- `.install/basew/logs/rmlui_round44_final_q2r_all_route_open.log`
  - Recorded `59` document opens including startup `main`.
  - Covered `58` unique registered route IDs.
  - Had `0` failure/error/exception/unhandled/parser hits.
  - Repeated the same Quake II Rerelease font-load and generated-text source markers.

Visual captures inspected:

- `.tmp/rmlui/round44-screens/rmlui_round44_refined_options_q2r_960x720.png`
- `.tmp/rmlui/round44-screens/rmlui_round44_refined_admin_commands_q2r_960x720.png`
- `.tmp/rmlui/round44-screens/rmlui_round44_refined_keys_q2r_960x720.png`
- `.tmp/rmlui/round44-screens/rmlui_round44_section_width_startserver_q2r_960x720.png`
- `.tmp/rmlui/round44-screens/rmlui_round44_section_width_gameflags_q2r_960x720.png`

The screenshots confirm the Options list no longer extends past the viewport, Admin Commands renders as a readable bounded list, and the Start Server/Deathmatch Flags routes keep Back/Close visible while their dense form contents remain scroll-bounded.

## Remaining Gaps

- Missing data-model notices on controller-stub routes are still expected until live RmlUi C++ data controllers are registered.
- Vulkan and RTX/vkpt RmlUi render paths remain inactive native-bridge work; this round does not redirect them to OpenGL.
- The final localization/shaping policy is still pending beyond the current TTF source and generated-text texture proof.
