# Q3A BotLib AAS Entity Cache

Date: 2026-06-17

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

Related tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice imports the pinned Quake III Arena `be_aas_entity.c` entity-cache implementation and removes WORR's temporary bridge-owned `AAS_ResetEntityLinks`, `AAS_InvalidateEntities`, and `AAS_UnlinkInvalidEntities` shims.

The imported entity cache is now the owner for AAS entity validity, area-link cleanup, entity info queries, nearest-entity lookup, and the `AAS_NextEntity` iterator. This slice was the cache ownership step; the follow-up `docs-dev/q3a-botlib-aas-entity-sync-2026-06-17.md` wires live WORR snapshots into `AAS_UpdateEntity`. Dynamic BSP leaf membership remains a temporary no-op until final entity collision/linking is connected.

## Imported Source

- `src/game/sgame/bots/q3a/botlib/be_aas_entity.c`

The file is an exact copy from `id-Software/Quake-III-Arena` commit `dbe4ddb10315479fc00086f08e25d968b4b43c49` with the original GPL header retained.

SHA-256:

- `2176a5e3f63127c759a95318f3c4d1c9cbf47052c43ca5c0a0d1121d3729dd37`

## Bridge Work

- Added `be_aas_entity.c` to the `q3a_botlib_utility` Meson source list.
- Removed temporary WORR bridge definitions for `AAS_ResetEntityLinks`, `AAS_InvalidateEntities`, and `AAS_UnlinkInvalidEntities`.
- Kept narrow temporary no-op placeholders for `AAS_UnlinkFromBSPLeaves` and `AAS_BSPLinkEntity`, because final dynamic BSP leaf membership belongs with the later entity clipping and movement integration.
- Continued to let imported `be_aas_main.c` call the entity-cache reset/invalidation path during setup and start-frame smoke.

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
$env:WORR_LOG_LEVEL='debug'; .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_entity_cache_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_aas_entity_cache_smoke_stdout.log; Remove-Item Env:WORR_LOG_LEVEL
```

Observed smoke lines:

- `Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)`
- `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`
- `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`

The start-frame smoke exercises imported `AAS_UnlinkInvalidEntities` and `AAS_InvalidateEntities`, proving the new Q3A entity-cache ownership survives a live server frame over the generated `mm-rage` AAS.

## Remaining Work

- Keep WORR entity snapshots wired through imported `AAS_UpdateEntity`; see `docs-dev/q3a-botlib-aas-entity-sync-2026-06-17.md` for the first full-frame sync.
- Dynamic BSP leaf links now use active-map Q2 BSP leaves; see `docs-dev/q3a-botlib-aas-bsp-leaf-link-2026-06-17.md`.
- Imported movement prediction/drop/jump helpers are covered by `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`; continue debug draw, clustering, alternate routing, and real bot steering integration.
