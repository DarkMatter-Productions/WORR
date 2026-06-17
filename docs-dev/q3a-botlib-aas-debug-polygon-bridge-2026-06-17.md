# Q3A BotLib AAS Debug Polygon Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This slice adds a WORR-owned bridge for Q3A `botimport.DebugPolygonCreate` and `botimport.DebugPolygonDelete`. Future imported Q3A debug paths that create debug polygons now have a real callback surface through `botlib_adapter.*`, while the runtime renders polygons through existing WORR debug line imports.

WORR does not currently expose a filled debug polygon primitive to `sgame`, so the runtime renders polygon outlines and fan diagonals with `gi.Draw_Line`. That keeps the bridge useful for AAS area/reachability visualization without adding renderer or protocol work.

## Implementation

- Added a C-compatible debug polygon callback ABI to `q3a_botlib_import.h`.
- Added `Q3A_BotLibImport_SetDebugPolygonCallback()` and adapter/runtime registration.
- Installed `botimport.DebugPolygonCreate` and `botimport.DebugPolygonDelete` in the Q3A import table.
- Added Q3A-style polygon ID accounting, create/delete counters, point counters, failure counters, and verbose status messages.
- Added `Q3A_BotLibImport_RunDebugPolygonSmoke()`, which creates and deletes a four-point polygon above a sampled imported AAS area.
- Added `BotLibAdapter_RunDebugPolygonSmoke()` and runtime outline/fan rendering through `gi.Draw_Line`.
- Runs the polygon smoke beside the generic debug draw smoke when `sg_bot_debug_aas >= 3`.

This slice did not import `be_aas_debug.c`; the follow-up area-helper slice imports it and is recorded in `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`.

## Validation

Build:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Install refresh and package preservation:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --assets-dir assets --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas
```

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'
try {
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_debug_polygon_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 3 +map mm-rage +wait 90 +quit *> .tmp\q3a_aas_debug_polygon_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed:

```text
q3a_debug_draw=Q3A debug draw bridge passed: callback=yes lines=2 crosses=1 arrows=1 clears=1 failures=0
q3a_debug_polygon=Q3A debug polygon bridge passed: callback=yes creates=1 deletes=1 points=4 last_id=1 failures=0
q3a_debug_polygon_callback=yes
q3a_debug_polygon_creates=1
q3a_debug_polygon_deletes=1
q3a_debug_polygon_points=4
q3a_debug_polygon_last_id=1
q3a_debug_polygon_failures=0
```

Existing bridge smokes remained green in the same run, including:

```text
q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0
q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes
q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024
q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18
```

Logs:

- `.install/basew/logs/q3a_aas_debug_polygon_smoke.log`
- `.tmp/q3a_aas_debug_polygon_smoke_stdout.log`

## Remaining Work

- Imported Q3A AAS debug area helper coverage is recorded in `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`; future work should feed real bot navigation state into that path.
- Feed real per-bot movement debug state into overlays once `bot_nav.*` owns route following.
