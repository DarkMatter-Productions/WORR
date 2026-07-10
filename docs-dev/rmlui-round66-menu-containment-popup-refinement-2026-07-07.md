# RmlUi Round 66 Menu Containment/Popup Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 66 builds on the Round 65 shell-hub pass with a containment and modal
polish sweep. The pass keeps the original pre-RmlUi command surfaces intact,
improves fixed-width menu behavior under changing canvas sizes, strengthens
the shared confirmation popup treatment, and extends explicit menu audio
metadata into reusable RmlUi component templates.

## Implementation

- Refined shared popup styling in `base.rcss`: confirmation dialogs are now
  wider, centered, framed with stronger danger/primary/secondary action
  states, and keep side-by-side Yes/No action rows.
- Marked Quit, Forfeit, Leave Match, and Tournament Replay confirmation
  documents with shared popup action classes while preserving their legacy
  commands and `data-menu-presentation="popup"` route contract.
- Loosened fixed-width menu panel minimums and changed key fixed panels from
  hidden overflow to contained scroll overflow across shell, settings,
  single-player, utility, and session themes.
- Preserved the grouped hub/menu intent from previous rounds while making
  Options, Video, Key Bindings, and DM Join-style panels less likely to clip
  on non-default canvas sizes.
- Added explicit `data-menu-sound` / `data-menu-sound-change` intent to shared
  component templates (`command_button`, `controls`, `save-load`,
  `image-grid`, `list-table`, `preview`, and `keybind`) plus the core runtime
  smoke buttons so future live controllers inherit the same audio contract.

## Validation

- `meson compile -C builddir-win`
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- `python -m pytest tools\ui_smoke -q` (`224 passed`)
- `rg -n -P "<button(?![^>]*data-menu-sound)" assets/ui/rml -g "*.rml"`
  returned no missing authored/template button sound metadata.
- `git diff --check` reported only existing LF/CRLF normalization warnings for
  edited files, with no whitespace errors.

Runtime capture evidence:

- `.install\basew\logs\round66_quit_popup_final_960x720.log`
- `.install\basew\logs\round66_options_layout_final.log`
- `.install\basew\logs\round66_video_widget_layout_final.log`
- `.install\basew\logs\round66_keys_containment_final.log`
- `.install\basew\logs\round66_dm_join_containment_final.log`

These logs show active OpenGL RmlUi routes, consumed open-sound/menu-music
metadata, screenshot writes, and Quake II Rerelease TTF font-source markers.
The Key Bindings and DM Join logs still report missing live data models; those
are the expected controller-stub warnings and not new parser/layout failures.

Visual capture evidence:

- `.tmp\rmlui\round66-menu-refine\round66_quit_popup_final_960x720.png`
- `.tmp\rmlui\round66-menu-refine\round66_options_layout_final.png`
- `.tmp\rmlui\round66-menu-refine\round66_video_widget_layout_final.png`
- `.tmp\rmlui\round66-menu-refine\round66_keys_containment_final.png`
- `.tmp\rmlui\round66-menu-refine\round66_dm_join_containment_final.png`

## Remaining Gaps

- Live settings/list/keybind/player-preview/session controllers remain
  incomplete beyond static command/cvar surfaces and command-published cvars.
- The staged Windows launch path still reported a `960x720` RmlUi runtime
  canvas when a smaller `r_geometry` was requested, so true narrow-viewport
  capture parity remains pending.
- Automated route-wide pixel clipping assertions, keyboard/controller
  navigation parity, localization/text shaping completion, and native
  Vulkan/RTX-vkpt RmlUi rendering remain open.
