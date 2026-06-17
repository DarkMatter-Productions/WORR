# Q3A BotLib AAS Clustering Import

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

## Summary

This slice imports Q3A `be_aas_cluster.c` and replaces WORR's temporary `AAS_InitClustering` no-op with the upstream clustering implementation. The active `mm-rage` AAS already contains generated cluster data, so the imported Q3A function does not need to rebuild clusters during normal load. The new runtime smoke still calls the imported path and verifies that the loaded cluster table is usable from WORR's adapter surface.

The new `q3a_cluster` verbose status records the sampled area, sampled cluster, total cluster count, cluster area count, reachability-area count, and failure count. This gives the route/navigation work a stronger gate before later bot movement code starts depending on cluster-scoped AAS behavior.

## Implementation

- Imported `src/game/sgame/bots/q3a/botlib/be_aas_cluster.c` from pinned id Software Q3A commit `dbe4ddb10315479fc00086f08e25d968b4b43c49`.
- Added `be_aas_cluster.c` to the `q3a_botlib_utility` object group in `meson.build`.
- Removed the temporary WORR-owned `AAS_InitClustering` no-op from `q3a_botlib_import.c`; the symbol now comes from the imported Q3A source.
- Added `Q3A_BotLibImport_RunAASClusterSmoke()`, which calls imported `AAS_InitClustering()`, validates the loaded `aasworld.clusters` and `aasworld.areasettings` tables, samples the previously selected AAS area when possible, and records cluster area/reachability-area counters.
- Mirrored the new clustering status through `q3a_botlib_import.h`, `botlib_adapter.*`, and verbose `sg_bot_debug_aas 2` output.
- Added cluster-smoke failure details to the runtime AAS-load error summary.

No local modifications were made to the imported `be_aas_cluster.c` file.

Imported file SHA-256:

```text
abcdf5913ff4120925fcf0a63aae9224dae8d88886cb344726c000311be267cd
```

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
  .\.install\worr_ded_x86_64.exe +set logfile 1 +set logfile_name q3a_aas_cluster_smoke +set logfile_flush 1 +set dedicated 1 +set game basew +set sg_bot_enable 1 +set sg_bot_debug_aas 2 +map mm-rage +wait 90 +quit *> .tmp\q3a_aas_cluster_smoke_stdout.log
  $code = $LASTEXITCODE
} finally {
  Remove-Item Env:WORR_LOG_LEVEL -ErrorAction SilentlyContinue
}
exit $code
```

Observed:

```text
q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0
q3a_cluster_area=3
q3a_cluster_cluster=1
q3a_cluster_count=4
q3a_cluster_areas=157
q3a_cluster_reachability_areas=156
q3a_cluster_failures=0
```

Existing bridge smokes remained green in the same run, including:

```text
q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1
q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0
q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes
q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024
q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18
```

Logs:

- `.install/basew/logs/q3a_aas_cluster_smoke.log`
- `.tmp/q3a_aas_cluster_smoke_stdout.log`

## Remaining Work

- Import or replace the remaining temporary Q3A optimization and alternate-routing stubs when later runtime slices need them.
- Decide whether cluster rebuild behavior should stay available for generated AAS diagnostics or remain a load-time validation path only.
- Feed cluster status into final bot navigation diagnostics once `bot_nav.*` owns route following and recovery decisions.
