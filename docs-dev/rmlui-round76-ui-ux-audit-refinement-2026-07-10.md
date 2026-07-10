# RmlUi Round 76 UI/UX Audit Refinement

Date: 2026-07-10

Tasks: `FR-09-T08`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 76 is a full-surface UI/UX audit-and-fix pass over the RmlUi
implementation: all route documents, the shared theme system, the shared
components, and the C++ runtime/bridge. A multi-lens review (theme
consistency, per-group menu content, navigation/routing integrity,
accessibility, transitions/polish, runtime robustness, render bridge)
produced 166 findings; 21 were adversarially verified in detail and the
remainder hand-triaged. This round implements the confirmed defects and the
tractable improvements, and documents the deferred items.

## Theme and asset changes

- `common/theme/base.rcss`
  - Added a document enter animation (`@keyframes route-enter`, 0.18s fade +
    8px rise on `.screen`) with a reduced-motion guard.
  - Split every merged `:hover, :focus` rule so keyboard focus is visually
    distinct (gold border + focus skins) from mouse hover across the
    back/close, primary, danger, popup, checkbox, and range families; added
    `:active` pressed states (tonal, replacing the old bright-yellow
    inversion) for every button family.
  - Replaced the hard-coded per-ID primary/danger selector lists with
    reusable `.ui-button-primary` / `.ui-button-danger` classes; tagged the
    affected buttons in RML.
  - Back/close styling now also matches compound commands via
    `[data-command^="popmenu"]` / `[data-command^="forcemenuoff"]` prefix
    selectors plus a `.ui-button-back` opt-in class.
  - New shared `.status-message` chip (replaces bespoke
    `#download-status-idle`; also styles the previously unstyled
    `status-message` lines in callvote/mymap pages).
  - Popup titles normalized via `text-transform: uppercase`; confirm buttons
    normalized to `Yes`/`No` with `m_yes`/`m_no` loc hooks; removed the
    contradictory `popup-primary popup-danger` class combination.
  - Fixed disabled-state double dimming (dropped `opacity: 0.65`, raised
    disabled text to #8d8375 for ~4.7:1 contrast); dropdown options and
    scrollbar thumbs now share the standard 0.12s transition; scroll thumb
    fill brightened (#26221c -> #756a5d).
  - High-visibility mode now covers checkboxes, sliders, progress, selects,
    scrollbars, popups, and hover/focus/active states, and clears decorators.
  - Removed non-interactive container hover effects (dialogs, panels,
    toolbars, hub sections) so hover only signals interactivity.
- `common/theme/settings.rcss`
  - Rewritten: the five near-identical per-form ID override blocks (~450
    lines) collapsed into shared `.form-compact` density and `.cols-2` /
    `.cols-3` width tiers; pages opt in via classes on the form element.
  - Widget type icons re-enabled (round-70 assets were authored but
    force-hidden); label/value/meter-fill/meter-text transitions added;
    field/action rows gained the missing hover accent; toggle focus states
    are now distinct from hover; new `.setting-hint` style for descriptive
    row help text.
- `common/theme/session.rcss`
  - Replaced the absolute-position button grids (callvote hub, dm_join/join
    team + session menus) with wrapping flex layouts so conditionally hidden
    entries reflow instead of leaving holes; deleted the ~120-line
    slot-coordinate tables.
  - Added hover/focus accent parity for all session tile buttons; split the
    danger-tile hover/focus states; report panels (match_stats,
    tourney_mapchoices) now stretch to the content width and hug their
    content height; fallback report rows share the gold header treatment.
  - High-visibility panels now render true white-on-black.
- `common/theme/shell.rcss`
  - Rewritten: deleted ~250 lines of dead absolute hub-grid rules superseded
    by `.hub-actions`; hub skins rescoped to classes; primary/danger hub
    variants added; main menu action stack is now centered on the canvas;
    hub tile border drift (#6f6253) normalized to #756a5d.
- `common/theme/singleplayer.rcss`
  - Deleted ~160 lines of dead absolute save-slot coordinates; converted the
    hub and save/load actions to flex; removed duplicate blocks.
- `common/theme/utility.rcss`
  - Keybind pages restructured to natural-height flex columns (removes the
    fixed-offset overlap risk under large-text mode); bind rows are now
    flex label/key pairs with a `.bind-key` readout and `is-capturing`
    state.
  - Server/demo tables: proportional per-column widths via `:nth-child`,
    row transition, placeholder-row hover suppression, tonal sort-state
    styling, removed the min-height that stretched empty tables.
- `common/theme/accessibility.rcss`
  - Removed `transform: none` from the reduced-motion guard (would fight
    the enter animation contract).
- Components: retinted image-grid/preview/save-load from the leftover
  blue-gray palette to theme tokens; added missing hover/focus/transition
  states across component RCSS; `worr-table-grid` gained real
  `display: table` rules; removed the duplicate `worr-command-button`
  template and the stale widget skin duplicates in `controls.rcss`.
- New asset: `common/skins/widgets/range-track-focus.svg`.

## Menu document changes

- Removed stale multi-agent `data-owner` metadata from all documents; linked
  `accessibility.rcss` from every route document (22 added links).
- `shell/main.rml`: removed the duplicate top-level Video entry; Quit is
  `.ui-button-danger`; menu stack centered.
- `shell/options.rml`: hub summary line; Effects moved into Display & Feel;
  third section renamed System; `&` headings.
- `shell/game.rml`: hub summary; removed the non-functional root Back
  button; danger/primary classes; `Saves & Exit`; dropped trailing `...`.
- `shell/quit_confirm.rml` + session confirms: normalized title case,
  `Yes`/`No` labels, loc keys, single danger class.
- `shell/download_status.rml`: progress row hidden while idle; Cancel is
  danger-classed; idle line uses `.status-message`.
- `shell/downloads.rml`: `Pics` relabeled `HUD Images`; sub-toggles disable
  when `allow_download` is off.
- `settings/accessibility.rml`: new Menus section exposing High-Contrast
  Menus (`ui_rml_high_visibility`) and Reduce Motion
  (`ui_rml_reduced_motion`); hint sentences moved out of value chips.
- `settings/sound.rml`: music controls gate on `ogg_enable`; volume readouts
  show percentages (`data-display-scale`/`data-display-suffix`); latency
  label carries units; `Music & Effects`.
- `settings/input.rml`: Mouse Sensitivity is now a slider with meter
  readout; labels de-jargonized.
- `settings/multimonitor.rml`: `data-enable-if` moved onto the input where
  it actually works; help text moved to a hint row.
- `settings/crosshair.rml`: dead `data-model` attribute removed (picker is
  now populated by the runtime imagevalues pass).
- `singleplayer/singleplayer.rml`: Level select bound to `_mapdb_level`
  (was the unread `_mapdb_unit`); start buttons gated on a selection and
  primary-classed; hub summary.
- `singleplayer/startserver.rml`: mode select relabeled to avoid the double
  `Rules` heading; removed the coop-only Frag Limit row; Begin Game is
  primary; three-column tier classes.
- Session family: callvote map-flags aligned with the family conventions;
  gametype/arena picker sounds corrected; `Return` standardized on all
  escape buttons; the two lobby Settings buttons declare
  `data-route-target="options"`; forfeit tiles styled danger; admin Replay
  Game now dispatches `worr_tourney_replay_menu` (previously opened the
  confirm popup with an empty YES command); empty states added to
  vote_menu, dm_matchinfo, dm_hostinfo, map_selector, callvote_main, and
  ui_list; dead community-links card removed; `session_vote` data-model
  token fixed to `session.vote`.
- Utility: servers/demos toolbars are honestly disabled with truthful
  status copy (their commands were never registered); players name/hand now
  live-edit the real `name`/`hand` cvars and the inert Apply/Reset buttons
  are gone; addressbook's 16 redundant `data-bind` attributes removed
  (they injected text nodes into inputs); keybind rows show live bindings.

## Runtime and bridge changes

- `src/client/ui_rml/ui_rml_runtime.cpp`
  - Keybind controller: clicking a bind row enters capture (Escape cancels,
    Backspace/Delete unbinds, any other key rebinds via
    `Key_SetBinding`); rows display live key names via `Key_GetBinding`.
  - Initial focus: on document open the first enabled tabbable control is
    focused (after conditions apply, before audio listeners attach), so
    D-pad/arrow navigation works immediately.
  - Condition evaluator: `ingame` engine-state token (mirrors legacy
    `CgameIsInGame` via `cl.frame.valid`); missing cvars compare as numeric
    0 so `cvar!=0` gates fail closed like the legacy menus; added
    `>= <= > <` operators and a diagnostic for malformed terms.
  - Dynamic population passes at document load: `$$com_maplist` expansion
    for Start Server, map database episodes/levels for Single Player, and
    pics enumeration for imagevalues pickers (crosshair). Selects with
    out-of-preset cvar values get a `(custom)` entry, unless an authored
    placeholder exists.
  - `data-display-scale`/`data-display-suffix` on bound text for unit-aware
    readouts; numeric fields validate input and revert on parse failure;
    text bindings skip form controls (fixes injected text nodes).
  - Command dispatch: the route-target shortcut only replaces a plain
    `pushmenu <route>` command, so compound commands (`set _mapdb_type
    episode; pushmenu skill_select`, match_setup seeding) run their side
    effects; popup tokens no longer leak to the console on failure.
  - Escape/Mouse2 handling honors document `data-close-command` (vote,
    match stats, map selector, lobby, welcome close side effects) through a
    new optional `HandleBackKey` runtime hook; keybind capture consumes the
    key first.
  - `ui_rml_high_visibility` / `ui_rml_reduced_motion` archived cvars toggle
    the `ui-high-visibility` / `ui-reduced-motion` body classes each frame.
  - Font engine: glyphs rasterize at the physical canvas scale (sharp text
    above 960x720) with metrics mapped back to canvas units; identical
    strings share one texture via a dedupe cache; all string textures are
    released when the menu system closes (fixes unbounded GPU growth);
    numpad navigation cluster maps to navigation keys.
  - Success-path console prints gated behind `ui_rml_debug`.
- `src/client/ui_rml/ui_rml.cpp`
  - The software cursor stays hidden until real mouse motion and hides again
    on keyboard navigation; cursor uses the real image size and registers
    once per session; route opens no longer double-read documents through
    the probe path; failed in-menu navigation plays an alert cue; popup
    route ids single-sourced.
- `src/game/cgame/ui/ui_core.cpp`
  - `forcemenuoff` and `popmenu` issued from server stufftext or console now
    close/pop the active RmlUi route (fixes stuck uninteractable session
    dialogs after e.g. forfeit YES); added `MenuSystem::HasOpenMenus()`.
- `src/renderer/rmlui_bridge.cpp`
  - SVG skins rasterize at the canvas magnification (bounded by a raised
    supersample cap) so widget chrome stays sharp at high resolutions.

## Evidence

Runtime captures are under `.tmp/rmlui/round76-captures/` (960x720 staged
OpenGL): centered main menu with focus ring, regrouped Options hub,
three-column Video, unit-aware Sound readouts with gated music controls,
Accessibility with the new toggles and hints, live keybind readouts,
hole-free fail-closed Match Lobby, full-width Match Stats, honest Servers
state with proportional columns, idle Download Status without a stray
progress bar, and the normalized Quit confirm.

## Validation

- `meson compile -C builddir-win` — clean.
- `python -m pytest tools/ui_smoke -q` — `225 passed` (run after each phase).
- `python tools/refresh_install.py --build-dir builddir-win --install-dir
  .install --base-game basew --platform-id windows-x86_64` — RmlUi payload
  validated at `180` package/loose files.
- Staged OpenGL captures of 14 routes via `.install/basew/round76_capture.cfg`
  plus a follow-up pass verifying the initial-focus disabled-skip and table
  column fixes.
- `git diff --check` — clean apart from the pre-existing LF/CRLF warnings.

## Deferred (documented, not regressed)

- Live server/demo browser providers, save-slot metadata, and player
  model/skin enumeration (controller work; the menus now state their status
  honestly instead of silently doing nothing).
- Menu text scaling via density-independent pixels (requires a full px->dp
  conversion of the theme).
- Premultiplied-alpha blending, draw batching, and oversized-mesh chunking
  in the GL bridge.
- Sort indicators/focusable table headers (no sorting backend exists yet).
- Sub-960x720 canvas scaling (`scale < 1` clamp) pending mouse/scissor
  verification.
