# Sgame Spawn Selection and Heatmap Second Pass (2026-06-16)

Task IDs: `FR-04-T09`, `DV-07-T02`

## Summary

This follow-up pass re-audited the player respawn selection and combat heatmap
paths after the initial hardening work. The goal was to remove remaining
edge-case inconsistencies, especially where legacy fallback spawn entities could
distort normal deathmatch selection and where non-player damage could pollute the
heat signal used by respawns.

No protocol state was changed, and `q2proto/` remains untouched.

## Issues Found

- `info_player_start`, `info_player_coop`, and `info_player_coop_lava` were
  registered into the normal FFA spawn list. That made solo/coop starts eligible
  for ordinary multiplayer selection instead of keeping them as true recovery
  fallbacks for maps without suitable deathmatch/team spawns.
- A few relaxed fallback paths filtered out unsafe spots but still picked
  randomly from the survivors. That could ignore the same heat, LOS, proximity,
  mine, and last-death scoring used by the primary path.
- Enemy line-of-sight checks traced a full player hull from the enemy eye to the
  spawn eye. For visibility, a point trace is a better fit and avoids rejecting
  spots because a player-sized swept hull grazed nearby geometry.
- Generic damage recording added heat for all deathmatch damage, including
  world, monster, and destructible interactions with no player involvement. That
  made the heatmap less representative of combat pressure around respawns.
- Heat decay was still too slow for spawn safety. A single strong event could
  remain influential for minutes, long after the firefight had moved on.

## Changes Made

- Added a dedicated `SpawnLists::fallback` vector for solo/coop spawn entities
  and kept the existing `ffa`, `red`, and `blue` lists reserved for real
  multiplayer spawn points.
- Updated spawn registration, legacy flattening, and spawn-count logging so
  fallback starts remain visible to tools without contaminating normal FFA
  selection.
- Added `SelectFallbackStartPoint`, which only considers solo/coop starts after
  real deathmatch/team lists fail, and still runs relaxed safety filtering plus
  composite danger scoring.
- Routed team, any-team, FFA, and last-ditch fallback picks through the same
  scoring logic after filtering instead of using unscored random picks.
- Changed enemy LOS testing to a point trace from enemy view height to spawn view
  height, passing the enemy as the trace skip entity while still ignoring player
  contents.
- Filtered combat heat writes to player-involved damage and added a robust event
  origin fallback: explicit damage point first, then player target origin, then
  inflictor origin.
- Increased heat decay so capped direct events age out on the order of tens of
  seconds instead of several minutes, keeping the heatmap useful for current
  spawn pressure rather than stale map history.

## Validation

- Built the focused server-game target:
  - `meson compile -C builddir-win sgame_x86_64`
- Refreshed and validated the local distributable staging root:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`

The build linked `sgame_x86_64.dll` successfully. Ninja emitted the same
recoverable `premature end of file; recovering` warning seen during the previous
pass.

## Follow-Up

- Add scenario coverage for maps with only `info_player_start`, maps with mixed
  deathmatch and coop starts, and hot-zone respawn scoring under `DV-03-T05`.
- Consider an operator-facing debug summary for current heat values and selected
  spawn penalties if `match_player_respawn_min_distance_debug` is expanded.
