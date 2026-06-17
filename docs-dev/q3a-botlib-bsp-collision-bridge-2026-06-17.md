# Q3A BotLib Q2 BSP Collision Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice replaces the temporary no-hit `AAS_Trace` and zero
`AAS_PointContents` stubs with an active-map Quake II BSP static-world collision
bridge for the imported Q3A AAS runtime.

The runtime already validates `maps/<map>.bsp` as Q2 `IBSP` version 38 for the
entity and inline-model bridges. It now also passes the full BSP buffer into the
Q3A import boundary, where the bridge decodes the Q2 planes, nodes, leafs,
leafbrushes, brushes, and brushsides needed for static world contents and
brush tracing.

## Architecture Notes

- This is a Q2 BSP collision bridge, not a Q3 BSP loader. Q3A BotLib still calls
  Q3A-shaped callbacks (`AAS_Trace`, `AAS_PointContents`), but the data and
  contents masks are Q2/Q2R `IBSP38` semantics.
- The implementation mirrors the already-validated WORR `tools/q2aas`
  trace-walker shape so runtime and generator reachability agree on static map
  collision behavior.
- The bridge is intentionally scoped to the active map static world. Dynamic
  entity clipping through `AAS_EntityCollision`, final `gi.trace` integration,
  movement prediction, jump/drop helpers, and debug drawing remain separate
  follow-up slices. Static PVS/PHS visibility landed later in
  `docs-dev/q3a-botlib-bsp-visibility-bridge-2026-06-17.md`.
- Collision data is owned by `q3a_botlib_import.*` and cleared on level end
  through `botlib_adapter.*`, keeping imported Q3A files quarantined from WORR
  `sgame` ownership details.

## Implementation

- `src/game/sgame/bots/q3a/q3a_botlib_import.*`
  - Added collision status fields and `Q3A_BotLibImport_LoadBspCollisionData`.
  - Parses Q2 BSP collision lumps from the full BSP buffer.
  - Implements static-world `AAS_PointContents`.
  - Implements point/box `AAS_Trace` against Q2 brushes.
  - Reports plane/node/leaf/brush counts and point/trace smoke status.
- `src/game/sgame/bots/botlib_adapter.*`
  - Added collision load/clear entry points and adapter status fields.
- `src/game/sgame/bots/bot_runtime.*`
  - Loads Q2 BSP collision data before the AAS handoff.
  - Prints collision bridge telemetry through `sg_bot_debug_aas 2`.
- `src/game/sgame/bots/q3a/README.WORR.md`
  - Records the static Q2 BSP collision bridge and remaining temporary stubs.
- `docs-dev/plans/q3a-botlib-aas-port.md`
  - Updated the Phase 2 checklist and implementation-log index.
- `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`
  - Updated `FR-04-T12` and `DV-07-T06` progress.
- `docs-dev/q3a-botlib-aas-credits.md`
  - Updated provenance rows for the WORR-owned runtime, adapter, and import
    bridge work.

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Install refresh and q2aas package preservation:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Dedicated runtime smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bsp_collision_bridge_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_bsp_collision_bridge_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Observed smoke lines:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `q3a_bsp_collision=Q3A BSP collision load passed: maps/mm-rage.bsp planes=1367 nodes=1863 leafs=1882 brushes=1142 point_contents=1 trace_fraction=0.347 startsolid=0 allsolid=0`.
- `q3a_bsp_point_contents_smoke=yes`.
- `q3a_bsp_trace_smoke=yes`.
- `ShutdownGame`.

## Credits

- Q3A BotLib callback shapes remain credited to id Software's Quake III Arena
  source baseline pinned in the credits ledger.
- Q2 BSP structures and collision expectations follow the credited
  `TTimo/bspc` / id Software BSPC lineage already vendored under
  `tools/q2aas/`.
- The runtime bridge implementation in this slice is WORR-native and is informed
  by the WORR-owned `tools/q2aas/worr_q2aas_q2trace.c` trace bridge so the
  generator and runtime stay aligned.

## Outstanding Work

- Replace `AAS_EntityCollision` with dynamic entity clipping.
- Decide whether final static traces should stay on this Q2 BSP walker, route
  through a WORR collision API, or share a consolidated collision service.
- Static PVS/PHS callbacks are now backed by
  `docs-dev/q3a-botlib-bsp-visibility-bridge-2026-06-17.md`; dynamic
  area-portal policy remains future work if imported runtime paths need it.
- Imported movement prediction/drop/jump helpers are covered by
  `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`.
- Q3A debug line/cross/arrow bridging is covered by
  `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`.
- Q3A AAS start-frame/runtime orchestration is covered by
  `docs-dev/q3a-botlib-aas-start-frame-2026-06-17.md`.
