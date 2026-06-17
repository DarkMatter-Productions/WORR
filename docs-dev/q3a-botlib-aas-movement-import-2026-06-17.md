# Q3A BotLib AAS Movement Import

Date: 2026-06-17

Tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice imports the pinned Quake III Arena `be_aas_move.c` implementation and removes the temporary WORR-owned movement prediction/drop/jump stubs from the Q3A bridge. The imported AAS movement helpers now compile into `sgame_x86_64` and are smoke-tested against the active packaged `maps/mm-rage.aas`.

The smoke is still a runtime import proof, not bot steering. It verifies that the loaded generated AAS can drive:

- `AAS_DropToFloor`
- `AAS_HorizontalVelocityForJump`
- `AAS_PredictClientMovement`

## Imported Source

- `src/game/sgame/bots/q3a/botlib/be_aas_move.c`

The file is an exact copy from `id-Software/Quake-III-Arena` commit `dbe4ddb10315479fc00086f08e25d968b4b43c49` with the original GPL header retained. SHA-256 is `1058490f47f3061c90afd78a03016af57a8037576de85feb154e1de679de12d8`.

## Bridge Work

- Added `be_aas_move.c` to the existing `q3a_botlib_utility` build group.
- Let imported `be_aas_move.c` own the Q3A `aassettings` global and `AAS_InitSettings` path.
- Added bridge-owned `vec3_origin` and no-op `AAS_DebugLine` / `AAS_ClearShownDebugLines` surfaces needed by the imported movement code until debug draw is bridged for real.
- Seeded Q3A movement LibVars before `AAS_Setup()` with WORR/Q2-oriented defaults, including `phys_maxwalkvelocity=300`, `phys_maxcrouchvelocity=150`, `phys_maxswimvelocity=150`, `phys_jumpvel=270`, `phys_maxstep=18`, `phys_maxbarrier=32`, and matching reachability timing values.
- Added movement smoke status fields through `Q3ABotLibImportSmokeStatus`, `BotLibAdapterStatus`, and verbose `sg_bot_debug_aas 2` output.
- Added `Q3A_BotLibImport_RunAASMovementSmoke()` after route setup. The smoke finds a normal reachable floor area, confirms floor drop and horizontal jump velocity helpers, and runs an eight-frame `AAS_PredictClientMovement` sample.

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
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_movement_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_aas_movement_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed:

```text
Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)
q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0
q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes
q3a_movement_drop=yes
q3a_movement_jump=yes
q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024
q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18
```

## Remaining Work

- Q3A debug line/cross/arrow bridging is covered by `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`.
- Build WORR-native bot navigation/steering that consumes imported route and movement helper results and emits Q2 `usercmd_t` movement.
- Decide final policy for Q3A movement LibVar defaults once more Q2 reference maps and bot movement states are online.
