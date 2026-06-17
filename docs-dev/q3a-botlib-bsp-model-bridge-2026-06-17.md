# Q3A BotLib BSP Model Bounds Bridge

Date: 2026-06-17

Tasks: `FR-04-T10`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

## Summary

This slice replaces the temporary `AAS_BSPModelMinsMaxsOrigin` zero-bounds stub
with active-map Quake II BSP model data.

The runtime now validates the active `maps/<map>.bsp` as Quake II `IBSP`
version 38, extracts both the entity lump and model lump, and passes Q2
`dmodel_t` records into the Q3A BotLib boundary before AAS load. The bridge
copies model mins/maxs, returns them through `AAS_BSPModelMinsMaxsOrigin`, and
expands bounds conservatively when Q3A callers request a rotated model.

This slice was only model metadata. Static collision traces and point contents
landed later in `docs-dev/q3a-botlib-bsp-collision-bridge-2026-06-17.md`;
static PVS/PHS visibility landed later in
`docs-dev/q3a-botlib-bsp-visibility-bridge-2026-06-17.md`; dynamic entity
collision, movement prediction, dynamic area-portal policy, and debug draw
remain separate bridge tasks.

## Architecture Notes

Q3A BotLib asks for inline BSP model bounds through `AAS_BSPModelMinsMaxsOrigin`.
For WORR/Q2 maps, the matching data lives in Q2 BSP lump 13 as fixed 48-byte
`dmodel_t` records:

- `mins[3]`
- `maxs[3]`
- `origin[3]`
- `headnode`
- `firstface`
- `numfaces`

The bridge stores the full record for diagnostics, but the public callback
returns mins/maxs and clears `origin`. That matches Q3A's BotImport behavior and
the existing WORR q2aas generator-side Q2 trace bridge, where callers treat the
returned bounds as the useful model-space/placed bounds and do not need an
additional origin offset from this callback.

## Files Changed

- `src/game/sgame/bots/bot_runtime.*`
  - validates all Q2 BSP lump headers once
  - extracts lump 0 entity text and lump 13 model records from the same BSP load
  - records model-lump byte count and model count
  - prints verbose model bridge status through `sg_bot_debug_aas 2`
- `src/game/sgame/bots/botlib_adapter.*`
  - adds `BotLibAdapter_LoadBspModelData`
  - records model count and model bounds smoke status
  - clears map model data on level end
- `src/game/sgame/bots/q3a/q3a_botlib_import.*`
  - parses little-endian Q2 `dmodel_t` records
  - implements `AAS_BSPModelMinsMaxsOrigin`
  - records load and bounds-smoke status

## Credits

No new upstream source file was imported in this slice. The callback semantics
were matched against the pinned Quake III Arena BotLib boundary and the
WORR-native q2aas Q2 trace bridge already recorded in
`docs-dev/q3a-botlib-aas-credits.md`. The runtime implementation itself is
WORR-native and remains credited to WORR contributors.

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
$env:WORR_LOG_LEVEL = 'debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_bsp_model_bridge_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_bsp_model_bridge_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Relevant log evidence from
`.install\basew\logs\q3a_bsp_model_bridge_smoke.log`:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`.
- `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start`.
- `q3a_bsp_model=Q3A BSP model lump load passed: maps/mm-rage.bsp models=18 smoke_model=1 mins=(-328.0 -584.0 24.0) maxs=(-192.0 -440.0 256.0)`.
- `q3a_bsp_models=18`.
- `q3a_bsp_model_smoke=yes`.
- `q3a_angle_vectors=Q3A AngleVectors smoke passed`.
- `q3a_time_ms=25`.
- `ShutdownGame`.

## Outstanding Work

- Continue from the static collision bridge with dynamic entity collision,
  movement-prediction, jump/drop helpers, and final WORR collision ownership.
- Replace debug-line stubs with WORR debug draw.
- Import or bridge the remaining Q3A AAS runtime files required for route
  queries, start-frame updates, and movement steering.
