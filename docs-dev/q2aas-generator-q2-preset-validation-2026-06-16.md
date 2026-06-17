# Q2 AAS Generator Q2 Preset and Validation Harness

Date: 2026-06-16

Task IDs: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice adds the first WORR/Quake II AAS generation preset and a repeatable validation harness for local Q2 BSP conversion tests. The generator can now consume a staged Q2 BSP and emit a structural `.aas` file into `.tmp/q2aas/`.

The generated `.aas` output is not bot-ready yet. The staged `mm-rage.bsp` smoke run produced valid AAS areas, but reachability and clusters are still zero because the imported BSPC control path only runs `AAS_CalcReachAndClusters` for `MAPTYPE_QUAKE3` maps.

Update: the zero-reachability limitation documented here was the baseline for this slice. The follow-up implementation log `docs-dev/q2aas-generator-q2-reachability-bridge-2026-06-16.md` records the first Q2 trace bridge that makes strict reachability validation pass for the same staged map.

No imported upstream source files are modified in this slice. WORR-specific work is limited to new local config, validation, build-target, README, and documentation files.

## Source and Credits

- Upstream generator baseline: `https://github.com/TTimo/bspc`
- Imported commit: `10d23c5ebd042ddc5d03e17de0f560f5076649dc`
- Fork lineage: `https://github.com/bnoordhuis/bspc`, audited at `6c11357e6d79a89e88cda2fe0e67c99a8923e116`
- License: GPL-2.0-or-later, with upstream `LICENSE` retained under `tools/q2aas/LICENSE`
- Contributor baseline: Ben Noordhuis, Chris Brooke, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset, Victor Luchits, plus original id Software source headers where present

WORR-native files added or extended in this slice:

- `tools/q2aas/cfg/worr_q2.cfg`
- `tools/q2aas/validate_worr_q2aas.py`
- `tools/q2aas/meson.build`
- `tools/q2aas/README.WORR.md`

The active credit ledger is `docs-dev/q3a-botlib-aas-credits.md`.

## Implementation

- Added `tools/q2aas/cfg/worr_q2.cfg` with the first WORR/Q2 movement preset:
  - standing player hull: `{-16, -16, -24}` to `{16, 16, 32}`;
  - crouched player hull: `{-16, -16, -24}` to `{16, 16, 4}`;
  - gravity, friction, acceleration, step height, water speed, crouch speed, and jump velocity matched to current WORR/Q2 movement constants.
- Added `tools/q2aas/validate_worr_q2aas.py`:
  - loads the WORR cfg by default;
  - keeps generated `.aas` files and logs under `.tmp/q2aas/`;
  - supports repeated `--map` inputs;
  - validates that generated maps produce at least one AAS area;
  - prints concise map summaries instead of dumping the full BSPC progress log;
  - warns on zero reachability/clusters;
  - supports `--require-reachability` for the future strict bot-ready gate.
- Added the Meson run target `q2aas-config-smoke`, which verifies that the built tool can load `cfg/worr_q2.cfg`.
- Updated `tools/q2aas/README.WORR.md` with the local preset and validation commands.

## Validation

Commands run:

```powershell
meson compile -C builddir-win worr_q2aas
meson compile -C builddir-win q2aas-config-smoke
python tools\q2aas\validate_worr_q2aas.py --map .install\basew\maps\mm-rage.bsp
python tools\q2aas\validate_worr_q2aas.py --map .install\basew\maps\mm-rage.bsp --require-reachability
```

Results:

- `worr_q2aas` builds successfully.
- `q2aas-config-smoke` loads `tools/q2aas/cfg/worr_q2.cfg` and exits through the inherited BSPC no-work path.
- `mm-rage.bsp` converts to `.tmp/q2aas/mm-rage.aas`.
- The structural AAS result for `mm-rage.bsp` reports:
  - `numareas = 428`
  - `numareasettings = 428`
  - `reachabilitysize = 0`
  - `numclusters = 0`
- `--require-reachability` fails as expected until Q2 reachability and clustering are enabled.

The Meson smoke target also emitted a local `ninja: warning: premature end of file; recovering` message after the successful target run. The warning did not prevent the target from completing, and it is not currently tied to the `q2aas` source changes.

## Known Limitation

The imported `tools/q2aas/bspc.c` path currently calls `AAS_CalcReachAndClusters(qf)` only when `loadedmaptype == MAPTYPE_QUAKE3`. Quake II maps therefore pass through `AAS_Create` and file writing, but skip the pass that populates reachability and clusters.

Before changing that gate, inspect the collision-map dependency in `tools/q2aas/be_aas_bspc.c`: `AAS_CalcReachAndClusters` uses `CM_LoadMap`, `CM_BoxTrace`, and `CM_PointContents` from the imported Q3 collision layer. The next tailoring task must prove or adapt this collision path for Q2 BSP data instead of simply removing the Q3-only guard.

## Follow-Ups

- Tailor reachability and clustering for `MAPTYPE_QUAKE2`.
- Add Q2 reference-map smoke coverage beyond the staged `mm-rage.bsp`.
- Add BSP checksum/config metadata checks once generated AAS is bot-runtime usable.
- Decide `.install/` staging policy for generated AAS files and/or the generator binary under `FR-04-T16`.
- Add per-file credit ledger rows and "Modified for WORR" notes before editing imported BSPC source files.
