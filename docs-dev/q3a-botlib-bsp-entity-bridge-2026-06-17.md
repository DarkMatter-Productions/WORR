# Q3A BotLib BSP Entity Bridge

Date: 2026-06-17

Tasks: `FR-04-T10`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice replaces the temporary Q3A BSP entity and epair stubs with an
active-map bridge backed by WORR's Quake II BSP data.

The runtime now loads `maps/<map>.bsp`, validates the Quake II `IBSP` version 38
header, extracts lump 0 entity text, and passes that text into the Q3A BotLib
boundary before loading `maps/<map>.aas`. The bridge parses the entity lump into
Q3A-style entity/epair records and implements:

- `AAS_NextBSPEntity`
- `AAS_ValueForBSPEpairKey`
- `AAS_VectorForBSPEpairKey`
- `AAS_FloatForBSPEpairKey`
- `AAS_IntForBSPEpairKey`

This is intentionally scoped to entity text and epair lookup. Inline BSP model
bounds, static-world collision traces, and static PVS/PHS visibility landed in
later bridge tasks; movement prediction, dynamic entity collision, dynamic
area-portal policy, and debug draw hooks remain separate bridge tasks.

## Architecture Notes

Q3A BotLib expects BSP entity queries through the `be_aas_bsp` surface. WORR
does not load Q3 BSP data for Q2 maps, so this bridge reads the Q2 entity lump
directly from the active `IBSP38` file and exposes only the text epair behavior
needed by imported Q3A AAS code.

Entity index semantics match Q3A BotLib's public helpers:

- entity `0` is worldspawn and is not returned by `AAS_NextBSPEntity`
- `AAS_NextBSPEntity(0)` returns the first non-world entity when present
- epair lookup rejects public entity `0`, matching Q3A behavior

The parser is WORR-owned and stores copied key/value strings with explicit
cleanup on level end and import shutdown. Missing or invalid BSP entity data is
reported through adapter status, but this slice does not make AAS load fail on
that condition. That keeps the existing generated-AAS smoke useful while later
BotLib runtime imports decide which callbacks are mandatory.

## Files Changed

- `src/game/sgame/bots/bot_runtime.*`
  - adds `maps/<map>.bsp` status
  - validates Q2 `IBSP38` entity lump bounds
  - loads entity text before the AAS buffer handoff
  - prints verbose bridge status through `sg_bot_debug_aas 2`
- `src/game/sgame/bots/botlib_adapter.*`
  - adds `BotLibAdapter_LoadBspEntityData`
  - records entity count, epair count, and value-smoke status
  - clears map entity data on level end
- `src/game/sgame/bots/q3a/q3a_botlib_import.*`
  - adds Q2 entity-lump parser storage
  - implements the Q3A BSP entity/epair helper surface
  - records load and lookup smoke status

## Credits

No new upstream source file was imported in this slice. The public callback
semantics were matched to Quake III Arena BotLib behavior from the pinned id
Software baseline already recorded in `docs-dev/q3a-botlib-aas-credits.md`.
The bridge implementation itself is WORR-native and remains credited to WORR
contributors in the ledger.

## Validation

Build validation:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- `q3a_botlib_import.c`, `botlib_adapter.cpp`, and `bot_runtime.cpp` compiled.
- `sgame_x86_64.dll` linked successfully.

Install refresh:

```powershell
python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json
```

Result:

- `.install` was refreshed with the rebuilt server-game DLL.
- `maps/mm-rage.aas` was re-injected into `.install\basew\pak0.pkz`.
- The q2aas archive audit and Windows staged payload validation passed.

Runtime smoke:

```powershell
$env:WORR_LOG_LEVEL = 'debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bsp_entity_bridge_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_bsp_entity_bridge_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Relevant log evidence from
`.install\basew\logs\q3a_bsp_entity_bridge_smoke.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start`.
- `q3a_bsp_entities=394`.
- `q3a_bsp_epairs=1704`.
- `q3a_bsp_entity_smoke=yes`.
- `q3a_angle_vectors=Q3A AngleVectors smoke passed`.
- `q3a_time_ms=25`.
- `ShutdownGame`.

## Outstanding Work

- `AAS_BSPModelMinsMaxsOrigin` is now backed by
  `docs-dev/q3a-botlib-bsp-model-bridge-2026-06-17.md`.
- Static `AAS_Trace` and `AAS_PointContents` are now backed by
  `docs-dev/q3a-botlib-bsp-collision-bridge-2026-06-17.md`.
- Continue dynamic entity collision, movement-prediction, and jump/drop helpers.
- Replace debug-line stubs with WORR debug draw.
- Import or bridge the remaining Q3A AAS runtime files required for route
  queries, start-frame updates, and movement steering.
