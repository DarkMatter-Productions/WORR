# RmlUi Round 77 Functional Parity and Adaptive Layout

Date: 2026-07-10

Tasks: `FR-09-T08`, `FR-09-T09`, `DV-03-T07`, `DV-07-T04`

## Scope

Round 77 is a functional-correctness pass against the legacy menu system:
the new-game flow, the player setup page, video mode selection, menu
scaling/aspect adaptivity, and an item-by-item coverage diff of every RmlUi
page against its `worr.json` counterpart.

## Root-cause fix: starting a new game never worked

- `src/game/cgame/ui/ui_mapdb.cpp`
  - The cgame module compiles its own copy of `src/common/mapdb.c` (its own
    static database), but nothing inside the module ever called
    `MapDB_Init()` — only the engine's filesystem init populates the
    engine-side copy. `_mapdb_run` therefore always saw an empty database
    and rejected every episode/level ("bad _mapdb_episode") in both the
    legacy and RmlUi menus. `UI_MapDB_Init` now parses the database and
    `UI_MapDB_Shutdown` frees it.
  - Verified end-to-end in the staged install: episode 0 dispatches the
    `newgame` alias, skips the absent intro cinematic, and reaches
    `SpawnServer: base1`.
- `src/common/mapdb.c`
  - Removed the `Z_LeakTest(TAG_MAPDB)` call from `MapDB_Shutdown`: with
    the engine and cgame each holding a database in the shared zone, a
    per-tag leak test from one side false-positives on the other side's
    live allocations.

## Player Setup: models, skins, dogtags, and a real 3D preview

- `src/client/ui_rml/ui_rml_runtime.cpp`
  - Client-side port of the player model enumeration (players/* directories
    with `tris.md2` plus icon-paired skin .pcx files; male/female-first
    ordering).
  - The Model and Skin selects populate at open, initialize from the `skin`
    userinfo cvar, and write back `model/skin` composites on change; a
    model change repopulates the skin list, keeping the skin when shared.
  - New generic `data-source-dir` select population (directory file stems)
    drives the new Dogtag picker bound to the `dogtag` userinfo cvar.
  - 3D preview: a fullbright, slowly rotating player entity playing the
    classic stand cycle renders through `R_RenderFrame`
    (`RDF_NOWORLDMODEL`) into the preview panel rect, computed from the
    RmlUi element box each frame and drawn after the document renders.
    With no player data installed, the panel shows "No player models
    found." instead.
- `assets/ui/rml/utility/players.rml`: dropped the inert `data-bind`/
  `data-options` stubs, added the Dogtag row and a preview placeholder id.

## Video mode list

- The Video Mode select now expands `$$r_modelist` at document load
  (windowed stays value 0; fullscreen modes get 1-based indices matching
  `r_fullscreen` semantics), restoring legacy mode selection that the
  scaffold had reduced to a single hardcoded "Fullscreen" entry.

## Menu scaling and narrow/short aspect adaptivity

- `src/client/ui_rml/ui_rml.cpp` / `ui_rml.h` / `ui_rml_runtime.cpp`
  - The canvas/draw scale math now lives in exactly one place
    (`UI_Rml_CanvasScale` / `UI_Rml_DrawScale`), consumed by the runtime,
    mouse mapping, cursor, scissor and font/SVG rasterization scale.
  - Removed the `scale < 1` clamp: framebuffers smaller than 960x720 (e.g.
    800x600) now render the full canvas scaled down instead of clipping
    fixed-width layouts.
  - The Screen Setup "Menu Scale" (`ui_scale`) cvar is now honored as a
    magnification multiplier, clamped so the canvas never drops below the
    960-unit design width (so 2x/4x apply progressively on widescreen and
    high-DPI displays and are safe no-ops at 4:3).
- Theme: `@media (max-width: 940px)` stacks the settings column layouts
  vertically, and `@media (max-height: 640px)` tightens fixed chrome —
  defensive adaptivity for canvases outside the guaranteed envelope.

## Main menu identity (legacy parity)

- `assets/ui/rml/shell/main.rml` + `shell.rcss`: game logo (`/art/logo.png`,
  raster path through the render bridge), player name readout bound to the
  `name` cvar, and the DEVELOPMENT VERSION + `version` cvar footer,
  right-aligned per the legacy layout.

## Coverage diff results (worr.json vs RmlUi)

- All 51 legacy menus have RmlUi routes; item-level diff found the pages
  fully covered (performance, input, effects incl. dmflags/explosion bits
  with correct `data-bit`/`data-negate`, screen, sound, crosshair,
  downloads, keys/legacykeys/weapons binds, addressbook, startserver,
  gameflags) except the items fixed this round (video modes, player
  model/skin/dogtag, main-menu identity).
- Menu music needs no menu-side work: `OGG_Play()` resolves
  `ogg_menu_track` whenever the client is disconnected, and the engine
  triggers it at startup/disconnect; the RmlUi music cues layer on top.
  Missing music in the dev-staged install is absent game data, not a code
  path.
- Session team-action panels are now chrome-less containers, so a lobby
  with no published team actions collapses cleanly instead of showing an
  empty outlined strip.
- Footer/action buttons no longer wrap their labels ("Legacy Keys").
- Sound page heading normalized to "Music & Effects"; Menu Scale gained a
  hint line; the singleplayer episode/level selects keep an explicit
  placeholder ("Select an episode..." / "No campaigns found") so the
  visible selection can never desync from the `-1` sentinel.

## Validation

- `meson compile -C builddir-win` — clean.
- `python -m pytest tools/ui_smoke -q` — `225 passed`.
- `python tools/refresh_install.py ...` — staged payload validated.
- Staged OpenGL runs at 960x720:
  - `.tmp/rmlui/round77_main.png` — logo, player name, version footer;
  - `.tmp/rmlui/round77_players.png` — live animated 3D player preview
    with populated model/skin/dogtag/hand controls;
  - `.tmp/rmlui/round77_singleplayer.png` — placeholder-gated campaign
    selects with disabled start buttons until a selection is made;
  - `.tmp/rmlui/round77_options_scale2.png` — `ui_scale 2` safely no-ops
    at 4:3;
  - new-game probe log — `_mapdb_run` episode 0 reaches
    `SpawnServer: base1`.
- `git diff --check` — clean apart from pre-existing LF/CRLF warnings.

## Deferred

- Weapon-model accompaniment, staged animations, and muzzle flash in the
  player preview (single stand-cycle entity shipped this round).
- Model/skin icon strip beside the preview.
- Server/demo browser providers (tracked from round 76).
