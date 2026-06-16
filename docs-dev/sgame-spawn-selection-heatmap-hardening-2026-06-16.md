# Sgame Spawn Selection and Heatmap Hardening (2026-06-16)

Task IDs: `FR-04-T09`, `DV-07-T02`

## Summary

This pass scrutinized the deathmatch player spawn-selection path and the combat
heatmap used by spawn scoring. It fixes several fairness and reliability issues
without changing protocol state or touching `q2proto/`.

## Issues Found

- `info_player_deathmatch` `INITIAL` handling used `0x10000`, but the entity
  comment defines `INITIAL` as the first mapper checkbox. The actual spawnflag
  bit is `1`, so initial spawns were effectively ignored unless a map used an
  unintended high bit.
- `ClientCompleteSpawn` prechecked safe initial spawns, but the actual
  `SelectSpawnPoint` call hard-coded `initial=false`. Initial mapper intent
  could be lost between the dry-run and the real spawn.
- Team-mode spawn selection only checked occupancy before scoring, while FFA
  selection applied last-death, mine/trap, nearest-player, and enemy-LOS
  filters.
- Relaxed fallback selection always treated `(0,0,0)` as a real avoid point.
  When no avoid point existed, maps with spawns near world origin could reject
  otherwise valid fallback spots.
- Spawn scoring used `1 / distance` for player and avoid-point terms, making
  those penalties nearly irrelevant at normal Quake II map distances.
- Heatmap pruning mixed unordered-map bucket counts with element indexes. This
  did not reliably walk the heat cells it intended to decay/prune.

## Implementation

- Corrected `SPAWNFLAG_INITIAL` to `1_spawnflag`.
- Kept initial-spawn selection active when either `sess.inGame` is false or
  `pers.spawned` is false, and used the same predicate in the precheck wrapper.
- Routed team spawns through the shared eligible/fallback filters used by FFA
  spawns before scoring them.
- Restored requester exclusion in nearest-player distance checks.
- Made `match_player_respawn_min_distance` drive the hard nearest-player
  filter, clamped to a safe range, instead of leaving the cvar unused.
- Added explicit no-avoid-point handling to fallback filtering.
- Cached per-candidate spawn scores within `SelectFromSpawnList` so expensive
  checks like enemy LOS are not recomputed several times per selection pass.
- Reworked composite scoring to use normalized distance bands for heat,
  nearest-player, enemy LOS, last-death proximity, and mine/trap risk.
- Capped single heatmap event magnitude, smoothed danger normalization with
  `raw / (raw + fullDangerHeat)`, and reset/fixed the heatmap pruning cursor.

## Validation

- Built the focused server-game target:
  - `meson compile -C builddir-win sgame_x86_64`
- Refreshed and validated the local distributable staging root:
  - `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`

## Follow-Up

- Add deterministic scenario coverage for `INITIAL` deathmatch spawns,
  team-spawn fallback, and heatmap-biased spawn scoring under `DV-03-T05`.
- Consider exposing controlled heatmap/spawn debug summaries through the
  existing `match_player_respawn_min_distance_debug` cvar if operator-facing
  diagnostics are needed.
