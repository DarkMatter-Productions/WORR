# RmlUi Round 67 Runtime Cvar/Condition Widget Refinement

Date: 2026-07-07

Tasks: `FR-09-T04`, `FR-09-T05`, `FR-09-T07`, `FR-09-T08`, `FR-09-T09`, `DV-07-T02`, `DV-07-T04`

## Summary

Round 67 moves the RmlUi menus beyond static presentation by giving authored
form controls a shared runtime cvar bridge, making cvar-backed labels and
conditionals update from live values, and refining the settings/session widget
layout around those live states. The pass keeps the pre-RmlUi command/menu
intent intact, keeps confirmation menus on popup routes, and validates the
result from the refreshed staged OpenGL install.

## Implementation

- Added a generic `data-cvar` runtime bridge for RmlUi form controls. Select,
  range, checkbox, text, numeric, and progress-style controls now initialize
  from cvars, write changes back through the normal menu cvar path, and refresh
  related value labels after changes.
- Added text refresh for `data-bind-cvar`, `data-label-cvar`, and the existing
  `data-bind="cvars.*"` convention used by session/lobby documents.
- Added a compact condition evaluator for `data-visible-if` / `data-show-if`
  and `data-enable-if` / `data-enabled-if`, including semicolon-AND terms,
  equality/inequality checks, numeric comparisons, truthy cvars, and negation.
- Suppressed duplicate focus/change sounds while runtime cvar values are being
  applied programmatically.
- Refined settings widgets with control-type accents, cleaner value badges,
  checked toggle styling, and range/progress visual treatments.
- Reworked the DM Join command area so cvar-gated session actions reflow into a
  bounded two-column grid and Leave Match remains visually destructive.

## Validation

- `meson compile -C builddir-win`
- `python -m pytest tools\ui_smoke -q` (`224 passed`)
- `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`
- Focused staged OpenGL runtime probes for Video, Sound, Start Server, DM Join,
  and Quit confirmation.

Runtime evidence:

- `.install\basew\logs\round67_video_tga_probe.log`
- `.install\basew\logs\round67_sound_cvar_binding_final.log`
- `.install\basew\logs\round67_startserver_conditions_final.log`
- `.install\basew\logs\round67_dm_join_conditions_flex_final3.log`
- `.install\basew\logs\round67_quit_popup_confirm_final.log`

The logs show RmlUi route opens from `.install`, Quake II Rerelease TTF face
loads, open/alert sound and menu-music cues, screenshot writes, and no parser,
unknown-command, or fallback failures. DM Join still reports missing
`session.dm_join*` data models; those are the expected controller-stub warnings
until live session model ownership lands.

Visual evidence:

- `.tmp\rmlui\round67-menu-refine\round67_video_cvar_binding_final.png`
- `.tmp\rmlui\round67-menu-refine\round67_sound_cvar_binding_final.png`
- `.tmp\rmlui\round67-menu-refine\round67_startserver_conditions_final.png`
- `.tmp\rmlui\round67-menu-refine\round67_dm_join_conditions_flex_final3.png`
- `.tmp\rmlui\round67-menu-refine\round67_quit_popup_confirm_final.png`

The DM Join capture verifies live `data-bind="cvars.*"` text, `data-label-cvar`
button labels, hidden cvar-gated entries, a bounded two-column session command
grid, and the popup confirmation path remains intact for Quit.

## Remaining Gaps

- Live list/save/keybind/player-preview/session data models and controllers
  remain incomplete beyond cvar/command surfaces.
- Native range thumbs/fill are still visually conservative because this pass
  stayed inside RmlUi-native form controls rather than replacing controls with
  custom scripted widgets.
- Start Server still exposes static data-source placeholders such as
  `$$com_maplist` until the relevant list/data model controller is promoted.
- Full keyboard/controller navigation parity, automated route-wide pixel
  clipping checks, true narrow-viewport runtime capture parity, and native
  Vulkan/RTX-vkpt RmlUi rendering remain open.
