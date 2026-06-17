# Q3A BotLib AAS Entity Sync

Date: 2026-06-17

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

Related tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice pushes WORR bot-facing entity snapshots into the imported Quake III Arena `AAS_UpdateEntity` path each server frame after the imported `AAS_StartFrame` invalidation pass.

The work keeps Q3A structs quarantined behind `botlib_adapter.*`: `bot_runtime.cpp` builds a WORR-native snapshot from `gentity_t` and `sv_entity_t`, the adapter translates that neutral snapshot into the C import bridge, and `q3a_botlib_import.c` is the only layer that writes Q3A `bot_entitystate_t`.

## Implementation

- Added `BotLibAdapterEntitySnapshot` and adapter-owned entity type constants for general, player, item, missile, and mover entities.
- Added `Q3A_BotLibImport_BeginEntitySync`, `Q3A_BotLibImport_UpdateEntity`, and `Q3A_BotLibImport_FinishEntitySync` so the bridge can count updates, unlinks, skips, failures, and imported `aasworld.maxentities`.
- Added a runtime snapshot pass that copies origin, angles, old origin, bounds, ground entity number, solid type, model indexes, animation frame, event, and weapon state from WORR entities.
- Unlinks inactive, uninitialized, unlinked, or `SVF_NOCLIENT` slots from the imported AAS entity cache.
- Caps the per-frame sync pass to the imported AAS entity-cache size so WORR's larger `game.maxEntities` range does not produce noisy skipped-slot counters.
- Moved `Bot_RuntimeRunFrame()` later in `G_RunFrame_`, after the existing `Entity_UpdateState()` loop has populated bot-facing entity metadata.
- Extended `sg_bot_debug_aas 2` verbose output with `q3a_entity_sync`, updated, unlinked, skipped, failure, and max-entity counters.

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Staging:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL='debug'
try {
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_entity_sync_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_aas_entity_sync_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed smoke lines:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`
- `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`
- `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`
- `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`

The smoke proves that the imported entity cache accepts the WORR snapshot stream after each imported start-frame pass and that inactive BotLib cache slots are explicitly unlinked.

## Remaining Work

- Q3A `EntityTrace` now has a WORR `gi.clip` bridge; see `docs-dev/q3a-botlib-aas-entity-trace-2026-06-17.md` for that follow-up slice.
- Dynamic BSP leaf links now use active-map Q2 BSP leaves; see `docs-dev/q3a-botlib-aas-bsp-leaf-link-2026-06-17.md`.
- Imported movement prediction/drop/jump helpers are covered by `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`; debug draw, route-overlay, and debug-polygon smoke are covered by `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-aas-route-overlay-2026-06-17.md`, and `docs-dev/q3a-botlib-aas-debug-polygon-bridge-2026-06-17.md`; add bot steering and later staggered scheduling for expensive perception checks.
