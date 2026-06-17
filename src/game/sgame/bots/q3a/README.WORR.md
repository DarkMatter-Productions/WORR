# Q3A BotLib Boundary

This directory is reserved for the Quake III Arena BotLib runtime rehost.

Current state:

- `q3a_botlib_boundary.*` is WORR-native boundary scaffolding.
- `game/q_shared.h`, `game/surfaceflags.h`, `game/botlib.h`,
  `game/be_aas.h`, `botlib/aasfile.h`, the `botlib/be_aas_*.h` declarations,
  `botlib/be_aas_file.c`, `botlib/be_aas_sample.c`, `botlib/be_aas_reach.c`,
  `botlib/be_aas_cluster.c`, `botlib/be_aas_route.c`,
  `botlib/be_aas_routealt.c`, `botlib/be_aas_optimize.c`,
  `botlib/be_aas_main.c`, `botlib/be_aas_entity.c`, `botlib/be_aas_move.c`,
  `botlib/be_aas_debug.c`, `botlib/be_interface.h`, `botlib/l_crc.*`,
  `botlib/l_log.h`, `botlib/l_memory.*`, `botlib/l_libvar.*`, and the parser
  utility headers are direct imports from the pinned Q3A commit used to prove
  the import/build and AAS file loading, loaded-area sampling,
  area-reachability, clustering, route-query, alternative-route query, AAS
  start-frame, movement-prediction, and opt-in optimization paths, plus AAS
  debug-area helper rendering.
- `q3a_botlib_import.*` is a WORR-native smoke bridge that provides temporary
  tracked bot-owned memory, read-only WORR filesystem callbacks with an
  active-AAS memory fallback, callback-backed logging, debug draw, and shared-utility
  callbacks for the imported utility/AAS loader, sampling, and reachability
  subsets. It now also
  provides bridge-backed Q3A runtime milliseconds, real `AngleVectors` support,
  active-map Q2 BSP entity-lump parsing for Q3A BSP entity/epair helpers, and
  active-map Q2 BSP model-lump parsing for inline model bounds. Static-world
  `AAS_PointContents` and `AAS_Trace` now use active-map Q2 BSP collision lumps,
  `AAS_inPVS` / `AAS_inPHS` now use active-map Q2 BSP visibility clusters, and
  imported Q3A route cache/travel-time prediction is smoke-tested against the
  loaded active-map AAS. Imported Q3A `AAS_Setup`, `AAS_SetInitialized`,
  `AAS_StartFrame`, and `AAS_Shutdown` now own the loaded AAS frame lifecycle
  while imported Q3A entity-cache code owns reset/invalidation during setup and
  start-frame. WORR bot-facing entity snapshots are pushed into imported
  `AAS_UpdateEntity` after each start-frame, with SOLID_BSP server model
  config indices translated to Q3A inline BSP model numbers. Q3A
  `AAS_EntityCollision` now calls a registered WORR entity trace callback
  backed by `gi.clip` for linked BBOX/BSP entities. Dynamic BSP leaf entity
  links and `AAS_BoxEntities` now use the active-map Q2 BSP node/leaf data. The
  bridge seeds WORR/Q2-oriented movement LibVars before imported Q3A AAS setup
  and smoke-tests imported floor-drop, jump-velocity, and client-movement
  prediction helpers. Imported Q3A `AAS_InitClustering` now replaces the
  temporary clustering no-op, and `sg_bot_debug_aas 2` reports the sampled
  cluster, cluster area count, and reachability-area count through
  `q3a_cluster`. Imported Q3A alternative routing now replaces the temporary
  `AAS_InitAlternativeRouting` / `AAS_ShutdownAlternativeRouting` lifecycle
  stubs, initializes after loaded-AAS route-cache setup, and reports
  `AAS_AlternativeRouteGoals` status through `q3a_alt_route`. Imported Q3A
  `AAS_Optimize` now replaces the final temporary optimization no-op. The
  default loaded-AAS smoke keeps `aasoptimize=0` because the Q3A optimization
  path mutates AAS geometry/index arrays for save/forcewrite flows. Imported Q3A
  `botimport.Print` now forwards warnings/errors/fatals into WORR logging and
  exposes message-level chatter when `sg_bot_debug_aas >= 3`. Imported Q3A
  `botimport.BotClientCommand` now crosses the adapter into a WORR runtime
  safety gate that validates bot clients and rejects command execution until a
  dedicated bot command dispatcher exists. Q3A `botimport.GetMemory`,
  `FreeMemory`, and `HunkAlloc` now use tracked zone/hunk allocation lists with
  grouped hunk release after AAS shutdown. Q3A `botimport.FS_FOpenFile`,
  `FS_Read`, `FS_Seek`, and `FS_FCloseFile` now use a tracked read-only
  file-handle table that loads through WORR's filesystem extension and reports
  callback/read/fallback counters through `sg_bot_debug_aas 2`; optional `.rcd`
  route-cache read probes are counted separately from filesystem open failures.
  The bridge also reports BotLib lifecycle counters for init/load/unload/shutdown
  and separates persistent LibVar zone bytes from transient AAS unload residue.
  Imported Q3A
  debug line/cross/arrow helpers now call WORR debug-line create/show/delete
  callbacks gated by `sg_bot_debug_aas >= 3`,
  `sg_bot_debug_route`, or `sg_bot_debug_goal`. `sg_bot_debug_route` /
  `sg_bot_debug_goal` now draw native cached `bot_nav` route/goal state once a
  bot route exists, including current-area labels, next-reachability labels, and
  a bounded sampled route polyline. `sg_bot_debug_client` filters the native
  cached route/goal overlay by zero-based client slot, with `-1` retaining the
  all-bots mode. The imported Q3A route-overlay smoke
  is retained as an early fallback. Q3A debug polygon
  create/delete callbacks route through WORR debug-line outline rendering, and
  imported `AAS_ShowArea` / `AAS_ShowAreaPolygons` now have a `sg_bot_debug_aas
  3` smoke path.
- The full Quake III Arena BotLib runtime has not been copied into this
  directory yet.
- The planned upstream baseline is `id-Software/Quake-III-Arena` commit
  `dbe4ddb10315479fc00086f08e25d968b4b43c49`.

Import rules:

- Add or update `docs-dev/q3a-botlib-aas-credits.md` before copying any Q3A file.
- Retain original id Software GPL headers on direct imports.
- Add a clear `Modified for WORR` note to imported files that are edited locally.
- Keep imported C code behind `botlib_adapter.*`; do not call Q3A globals from
  unrelated `sgame` systems.
- Build imported C files as an internal server-game object group unless a later
  implementation note records why a static library is safer.
