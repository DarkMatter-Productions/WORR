# Q3A BotLib AAS Debug Area Helpers

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This slice imports Q3A `be_aas_debug.c` and runs the upstream AAS debug-area helpers through WORR's debug draw and debug polygon callbacks. The imported helpers now own `AAS_ClearShownDebugLines`, `AAS_DebugLine`, `AAS_PermanentLine`, `AAS_DrawPermanentCross`, `AAS_DrawArrow`, `AAS_ShowArea`, and `AAS_ShowAreaPolygons`; the WORR bridge supplies Q3A `botimport.DebugLineCreate`, `DebugLineShow`, `DebugLineDelete`, `DebugPolygonCreate`, and `DebugPolygonDelete`.

The result is a contained validation path for area-boundary visualization. It does not yet expose final per-bot navigation overlays, but it proves that imported Q3A area drawing can cross the native adapter boundary and render through WORR debug primitives.

## Implementation

- Imported `src/game/sgame/bots/q3a/botlib/be_aas_debug.c` from pinned id Software Q3A commit `dbe4ddb10315479fc00086f08e25d968b4b43c49`.
- Added `be_aas_debug.c` to the `q3a_botlib_utility` object group in `meson.build`.
- Removed the WORR-owned temporary definitions of the Q3A debug line/cross/arrow helper functions, leaving those symbols to the imported source.
- Added Q3A debug-line ID allocation plus `botimport.DebugLineCreate`, `DebugLineShow`, and `DebugLineDelete` callbacks in `q3a_botlib_import.c`.
- Added `Q3A_BotLibImport_RunDebugAreaSmoke()`, which selects the sampled loaded area, calls imported `AAS_ShowArea()` and `AAS_ShowAreaPolygons()`, clears the shown debug output, and records line plus polygon create/delete deltas.
- Mirrored the new debug-area status through `botlib_adapter.*` and verbose `sg_bot_debug_aas` output.
- Runs the debug-area smoke beside the debug draw and debug polygon smokes when `sg_bot_debug_aas >= 3`.

No local modifications were made to the imported `be_aas_debug.c` file.

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
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_debug_area_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 3 +map mm-rage +wait 90 +quit *> .tmp\q3a_aas_debug_area_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed:

```text
q3a_debug_draw=Q3A debug draw bridge drew primitive: callback=yes lines=23 crosses=1 arrows=1 clears=1 failures=0
q3a_debug_polygon=Q3A debug polygon bridge deleted polygon: callback=yes creates=7 deletes=7 points=28 last_id=7 failures=0
q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0
q3a_debug_area_area=3
q3a_debug_area_lines=12
q3a_debug_area_polygon_creates=6
q3a_debug_area_polygon_deletes=6
q3a_debug_area_failures=0
```

Existing bridge smokes remained green in the same run, including:

```text
q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0
q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes
q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024
q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18
```

Logs:

- `.install/basew/logs/q3a_aas_debug_area_smoke.log`
- `.tmp/q3a_aas_debug_area_smoke_stdout.log`

## Remaining Work

- Feed real per-bot route/goal/current-area state into overlays once `bot_nav.*` owns route following.
- Add route-corridor and reachability-type drawing once the navigation layer tracks more than a smoke start/goal pair.
- Keep debug-area drawing behind developer cvars so normal bot runtime smoke stays quiet.
