# Q3A BotLib AAS Start Frame

Date: 2026-06-17

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

Related tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice imports the pinned Quake III Arena `be_aas_main.c` runtime AAS entry point and routes WORR's server-frame tick through Q3A `AAS_StartFrame`. The bridge still loads active-map AAS from WORR-owned bytes, but it now runs Q3A `AAS_Setup` before load, uses Q3A `AAS_SetInitialized` after routing setup, and tears down via Q3A `AAS_Shutdown`.

The start-frame call is still an AAS runtime proof, not bot movement. It verifies that the imported AAS frame path can advance over the generated active-map `.aas` after the existing area, reachability, route, BSP collision, and visibility smoke checks pass.

## Imported Source

- `src/game/sgame/bots/q3a/botlib/be_aas_main.c`

The file is an exact copy from `id-Software/Quake-III-Arena` commit `dbe4ddb10315479fc00086f08e25d968b4b43c49` with the original GPL header retained.

SHA-256:

- `ce3e5ecba4742e0853c79edd685b3d5317aeb9f9b836d437c4d2dfa872c81c0b`

## Bridge Work

- Moved ownership of Q3A `aasworld`, `AAS_Error`, `AAS_Time`, and `AAS_ProjectPointOntoVector` from the WORR bridge to imported `be_aas_main.c`.
- Added `Q3A_BotLibImport_StartFrame`, called from `BotLibAdapter_RunFrame`, to feed `level.time.milliseconds()` into imported `AAS_StartFrame`.
- Added start-frame status fields to `Q3ABotLibImportSmokeStatus` and `BotLibAdapterStatus`.
- Extended verbose `sg_bot_debug_aas 2` output with `q3a_start_frame`, `q3a_start_result`, `q3a_start_frames`, and `q3a_start_time_ms`.
- Kept temporary bridge shims for still-unimported main-path helpers: BSP load/dump, settings init, clustering, optimization, and alternate routing. The entity invalidation/link reset shims were later replaced by the imported Q3A entity cache documented in `docs-dev/q3a-botlib-aas-entity-cache-2026-06-17.md`.

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
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_start_frame_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_aas_start_frame_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Observed route and start-frame lines:

- `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`
- `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`
- `q3a_areas=428`
- `q3a_reachability=562`
- `q3a_clusters=4`

## Remaining Work

- Replace the temporary main-path clustering, alternate-route, and debug draw shims with imported or WORR-owned final adapters.
- Imported movement prediction/drop/jump helpers are covered by `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`; later bot steering still needs to consume them.
- Wire `sg_bot_debug_route` and `sg_bot_debug_goal` overlays to actual bot/nav state after `bot_nav.*` exists.
