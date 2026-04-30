# Quake Champions-Inspired Top HUD (2026-04-28)

Task ID: `FR-04-T08`

## Goal
Recreate the readable competitive top HUD language from the supplied Quake
Champions FFA, teamplay, duel, and spectator references while keeping the
implementation native to WORR/Q2 systems.

The feature favors the top-center match information that players need during
active play:

- match timer, time limit, warmup/countdown, timeout, overtime, and
  intermission state
- FFA leader/chaser rows
- team score panels with team color accents
- duel score panels with player portraits
- spectator-only duel health and armor bars

## Q2 Concessions
Quake II does not have champion portraits, champion ability bars, or the same
round structure as Quake Champions, so the WORR implementation uses existing
runtime assets:

- player skin icons from the scoreboard data for duel/FFA player portraits
- CTF/team icons or miniscore image configstrings for team panels
- drawn translucent panels, score blocks, accents, and eliminated slashes
  instead of authored binary art
- Q2 match-state data for warmup/countdown/in-progress/timeout/intermission
  timing

This keeps the HUD shippable without adding renderer-specific paths or asset
format debt.

## Server Data
`src/game/sgame/player/p_hud_scoreboard.cpp` now refreshes the HUD blob every
server frame during deathmatch through `UpdateMultiplayerHudBlob()`.
Timer values are quantized to whole seconds before export because the top HUD
renders minute/second clocks and the HUD blob travels through configstrings.

The scoreboard blob section now starts with:

```text
match_meta <phase> <match_ms> <time_limit_ms>
```

`phase` uses the shared `hud_match_phase_t` enum in
`src/game/bgame/bg_local.hpp`:

- `HUD_MATCH_PHASE_WARMUP`
- `HUD_MATCH_PHASE_COUNTDOWN`
- `HUD_MATCH_PHASE_IN_PROGRESS`
- `HUD_MATCH_PHASE_TIMEOUT`
- `HUD_MATCH_PHASE_INTERMISSION`

Each `sb_row` also carries optional vitals after the existing skin icon token:

```text
sb_row <client> <score> <ping> <team> <flags> <skin> <health> <health_max> <armor> <armor_max> <rank> <name>
```

Older blob rows remain parseable because the cgame parser treats the vitals
and identity tokens as optional. The explicit rank and quoted net name keep the
top HUD from falling back to generic labels when the normal clientinfo name path
has not populated yet.

## Client Rendering
`src/game/cgame/cg_draw.cpp` adds `cg_draw_match_hud` as an archived cvar,
enabled by default. When enabled, the modern top HUD draws after both the
default legacy `CS_STATUSBAR` route and the opt-in cgame statusbar route, so it
is visible with the repository's current default `cg_hud_cgame 0` setting.
The legacy layout parser suppresses only the old match-state/countdown/miniscore
blocks while the modern top HUD is active, preventing duplicate top text.
When structured `match_meta` exists, the top timer intentionally ignores the
legacy `STAT_MATCH_STATE` prose fallback so server notice strings cannot spill
into the competitive HUD.
Cgame also resynchronizes the current HUD blob configstring range on demand, so
warmup HUD data that was committed before cgame initialization is still parsed
without waiting for a later configstring delta.
The parse cache is keyed from the concatenated current configstring payload
rather than only from per-segment dirty notifications, avoiding stale match
metadata if the segment cache and dirty flag get out of step.
If structured scoreboard data is present but the phase is still `NONE`, the top
timer treats a non-in-progress deathmatch as warmup so the center timer never
goes blank during the first join frames.
Player labels fall back to mode labels until client name strings are available,
so the top rows do not render with an empty name slot during early connection.

Rendering modes:

- FFA: timer centered, with compact rows to the right of the timer. Row
  selection mirrors the legacy miniscore rule: show first and second place, but
  replace the second slot with the local/chased player when that player is
  outside the top two.
- Team: blue and red score panels flank the centered timer, using team color
  score blocks and existing team imagery.
- Duel: player panels flank the timer with skin icons, score blocks, names,
  and eliminated styling.
- Spectator duel: the same duel panels add health and armor bars beneath each
  player, matching the supplied spectator-only reference behavior.

Warmup now follows the Quake Champions reference more closely: a small gold
state label sits above a large clock, with the configured timelimit below in
muted grey. FFA rows are hidden during warmup because the legacy miniscore data
does not publish FFA positions until the match is in progress.

The legacy statusbar elements remain available when `cg_draw_match_hud 0` is
set or when the cgame HUD is disabled.

## Validation
Focused local build:

```text
meson compile -C builddir-win cgame_x86_64 sgame_x86_64 copy_cgame_dll copy_sgame_dll
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64
```

Result: passed.

OpenGL smoke runs confirmed the staged HUD is visible with the default legacy
statusbar path (`cg_hud_cgame 0`), the warmup timer renders as
`WARMUP / 0:00 / 10:00`, and stale legacy top-center text/miniscore elements
are suppressed while `cg_draw_match_hud 1` is enabled.

Crash validation for screenshot-driven HUD QA is covered in
`docs-dev/renderer-async-shutdown-drain-2026-04-29.md`.

## Follow-Ups
- Add small authored WORR tournament/team crest assets once the broader match
  presentation package is ready.
- Consider mode-specific labels for custom gametypes beyond FFA/team/duel when
  the match rules expose richer presentation metadata.
