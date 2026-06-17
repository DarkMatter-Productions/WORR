# Q3A BotLib AAS BSP Leaf Entity Link Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `DV-07-T06`

## Summary

This slice replaces the temporary Q3A BSP leaf entity-link no-op with a WORR-owned bridge backed by the active-map Q2 BSP collision tree. Imported Q3A `AAS_UpdateEntity` can now store dynamic entity `leaves` links, and Q3A-facing `AAS_BoxEntities` can query linked entity numbers from the leaves overlapped by a world-space bounds query.

## Implementation

- Added a dynamic `bsp_link_t` table beside the parsed Q2 BSP leaf data in `q3a_botlib_import.c`.
- Implemented `AAS_BSPLinkEntity` by recursively walking the parsed Q2 BSP node tree with the entity absolute bounds and inserting links into per-leaf and per-entity lists.
- Implemented `AAS_UnlinkFromBSPLeaves` and defensive link cleanup during AAS/BSP unload so stale entity pointers do not survive map or AAS reloads.
- Implemented `AAS_BoxEntities` as a duplicate-safe linked-entity query across overlapped BSP leaves.
- Added adapter status fields and verbose debug output for active leaf links, link failures, and box-query smoke results.

The bridge remains intentionally local to the Q3A import boundary. It does not change `q2proto`, does not alter renderer paths, and does not move ownership of final collision policy away from the WORR server-game adapter.

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
$env:WORR_LOG_LEVEL='debug'
.\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_leaf_link_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 60 +quit *> .tmp\q3a_aas_leaf_link_smoke_stdout.log
Remove-Item Env:WORR_LOG_LEVEL
```

Observed:

```text
Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)
q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024
q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18
q3a_bsp_leaf_link_failures=0
q3a_bsp_box_entities_smoke=yes
q3a_bsp_box_entities=2
```

## Remaining Work

- Imported movement prediction/drop/jump helpers are covered by `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`; final WORR-native bot steering still needs to consume those helpers.
- Q3A debug line/cross/arrow bridging is covered by `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`; imported route-query overlay smoke is covered by `docs-dev/q3a-botlib-aas-route-overlay-2026-06-17.md`.
- Build first bot movement/route-following code on top of the now-loaded AAS, route, entity, trace, visibility, and leaf-link surfaces.
