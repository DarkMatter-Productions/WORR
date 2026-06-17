# Q3A BotLib AAS Route Query

Date: 2026-06-17

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

Related tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice imports the pinned Quake III Arena `be_aas_route.c` route-query implementation plus the `l_crc.*` helper it needs for route-cache validation. WORR now initializes Q3A route caches after the active map AAS buffer loads, frees those caches on unload, and runs a route smoke through the existing `botlib_adapter.*` boundary.

The route smoke remains a runtime/adaptation proof, not bot movement. It verifies that a loaded generated `.aas` can answer:

- `AAS_AreaTravelTimeToGoalArea`
- `AAS_AreaReachabilityToGoalArea`
- `AAS_PredictRoute`

## Imported Source

- `src/game/sgame/bots/q3a/botlib/be_aas_route.c`
- `src/game/sgame/bots/q3a/botlib/l_crc.c`
- `src/game/sgame/bots/q3a/botlib/l_crc.h`

All three are exact copies from `id-Software/Quake-III-Arena` commit `dbe4ddb10315479fc00086f08e25d968b4b43c49` with original GPL headers retained. Their SHA-256 hashes are recorded in `docs-dev/q3a-botlib-aas-credits.md`.

## Bridge Work

- Added route status fields to `Q3ABotLibImportSmokeStatus` and `BotLibAdapterStatus`.
- Added bridge-owned `Com_sprintf` plus temporary `AAS_Time` and `AAS_ProjectPointOntoVector` helpers required by the imported route code; the time/projection helpers later moved to imported `be_aas_main.c`.
- Initialized route caches after the Q3A AAS file loader succeeds, then marked `aasworld.initialized` for route/sample queries.
- Freed Q3A route caches before AAS unload/dump to avoid stale routing state across map reloads.
- Added a route smoke that searches for a routable start/goal area pair and requires route prediction to end at the goal area.
- Extended verbose `sg_bot_debug_aas 2` output with `q3a_route_*` fields.

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
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_route_smoke_final2 +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_aas_route_smoke_final2_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Diff hygiene:

```powershell
git diff --check
```

Observed route line:

- `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`
- `q3a_areas=428`
- `q3a_reachability=562`
- `q3a_clusters=4`

## Remaining Work

- `be_aas_main.c` start-frame import is covered by `docs-dev/q3a-botlib-aas-start-frame-2026-06-17.md`.
- Imported movement prediction/drop/jump helpers are covered by `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`; debug draw and bot steering remain future adapter work.
- Wire `sg_bot_debug_route` and `sg_bot_debug_goal` overlays to actual bot/nav state after `bot_nav.*` exists.
