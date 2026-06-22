# WORR SWOT and Task-Based Feature + Development Roadmaps

Date: 2026-02-27

## Purpose
Create a repository-grounded SWOT and convert it into actionable, task-based project roadmaps that can guide coordinated team execution.

## Status Updates
- `FR-04-T02` / `FR-04-T03` / `FR-04-T04` / `FR-04-T05` /
  `FR-04-T06` / `FR-04-T07` / `FR-04-T15` Bot completion roadmap:
  - Added `docs-dev/plans/bot-implementation-completion-roadmap.md` as the
    go-forward roadmap for turning the completed Q3A BotLib/AAS port and
    77-row scenario catalog into full live bot behavior.
  - The roadmap defines completion gates for live behavior, combat, items,
    team play, coop, movement, chat/personality, performance, packaging, and
    release readiness; M1 live behavior arbitration is now complete, and the
    first M2 slices, sustained enemy target memory/decay, weapon-scoring
    arsenal proof, aim/fire policy depth, ammo-pressure pickup routing,
    survival inventory use, survival health routing, survival armor routing,
    the compact combat/survival regression proof, and the expanded smoke
    contract reconciliation are complete. The latest full automated
    `implemented` catalog now passes 76/76 rows from
    `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`;
    the recommended next implementation slices are broader survival
    retreat/avoidance, second-map combat/item regression, and mode-specific
    live behavior promotion.
- `FR-04-T02` / `FR-04-T11` / `FR-04-T12` / `FR-04-T14` / `FR-04-T16` /
  `DV-07-T06` Final checklist closeout:
  - `docs-dev/plans/q3a-botlib-aas-port.md` now has all 809 non-template phase
    checklist rows complete. The reusable checklist gate template has been
    converted to plain guidance so it no longer appears as raw unchecked task
    debt.
  - Added final implementation rollups:
    `docs-dev/q3a-botlib-runtime-implementation-2026-06-21.md` and
    `docs-dev/q2-aas-generator-implementation-2026-06-21.md`.
  - Linux/macOS build coverage is recorded from the release matrix:
    `.github/workflows/nightly.yml` and `.github/workflows/release.yml` both
    consume `tools/release/targets.py`, whose matrix includes `linux-x86_64` on
    `ubuntu-latest` and `macos-x86_64` on `macos-15-intel`; both workflows run
    `meson compile -C builddir` for non-Windows builds.
  - The q2aas reference-map row is closed by the staged eight-map set:
    `mm-rage`, `q2dm1`, `q2dm2`, `q2dm8`, `q2ctf1`, `base1`, `base2`, and
    `train`, covering current DM, canonical/open DM, CTF/team objectives,
    campaign/coop, water/liquid, teleport entity diagnostics, door diagnostics,
    and elevator/platform routes.
  - No FR-04 strategic task was marked complete solely because the checklist
    closed; longer-term behavior depth and future reference-map candidates
    remain roadmap work.
- `FR-04-T11` / `FR-04-T16` / `DV-07-T06` In Progress:
  - Vendored the pinned `TTimo/bspc` snapshot at `10d23c5ebd042ddc5d03e17de0f560f5076649dc` under `tools/q2aas/`.
  - Added the Meson `q2aas` option and standalone `worr_q2aas` target, keeping upstream source files unmodified in this bootstrap slice.
  - Added a WORR-local compatibility shim and vendor note beside the snapshot, and recorded the import in the credits ledger.
  - Added `tools/q2aas/cfg/worr_q2.cfg` with first WORR/Q2 player hull and movement settings, plus `tools/q2aas/validate_worr_q2aas.py` for cfg/map smoke checks under `.tmp/q2aas/`.
  - Added the Meson `q2aas-config-smoke` target for the WORR/Q2 preset.
  - Local validation built `builddir-win\tools\q2aas\worr_q2aas.exe`, smoke-ran the preset, and converted `.install\basew\maps\mm-rage.bsp` to `.tmp\q2aas\mm-rage.aas`.
  - Added a WORR-native Q2 BSP trace bridge for BotLib reachability generation and modified the imported BSPC control path with explicit `Modified for WORR` notes.
  - Strict staged-map validation now passes for `mm-rage.bsp`:
umareas = 428`, `reachabilitysize = 562`,
umclusters = 4`, with travel counts including walk, jump, ladder, walk-off-ledge, elevator, and runtime-gated inherited rocket-jump candidates.
  - Added `tools/q2aas/validation_manifest.json`, the Meson `q2aas-staged-smoke` target, JSON report output under `.tmp/q2aas/`, and invalid-BSP expected-failure coverage.
  - Invalid BSP smoke now fails clearly with `ERROR: unknown BSP format BAD!, version 1` instead of continuing toward AAS generation with missing map data.
  - Added deterministic sidecar metadata for generated AAS validation artifacts. `q2aas-staged-smoke` now requires Quake II `IBSP` version 38 input, writes `.tmp/q2aas/mm-rage.aas.meta.json`, records tool/config/BSP/AAS hashes, decodes AAS header checksum metadata, and detects the staged map's BSPX marker at offset `766956`.
  - Added first-pass entity/content diagnostics to the staged smoke report: spawn/item/mover/trigger counts, Q2 brush-content counts, AAS area-bounds origin coverage, high-value pickup reachability from spawn areas, and door/elevator/teleport inventory.
  - `mm-rage.bsp` diagnostics currently report `9` spawn points, `2` intermission cameras, `48` pickups, `2` high-value pickups, `2` movers, `1` trigger, `0` spawn/item origin orphans, `0` unreachable high-value pickups, `16` ladder brushes, and no water/slime/lava brushes.
  - `q2aas-staged-smoke` now enforces diagnostic gates for clean BSP lump tables, spawn origin coverage, item origin coverage, and high-value pickup reachability. The current `mm-rage.bsp` report records all four gates as `required: true` and `status: passed`.
  - `q2aas-staged-smoke` now also enforces manifest baseline minima for `mm-rage.bsp`: `428` areas, `428` area settings, `562` reachability records, `4` clusters, `468` walk routes, `1` barrier jump, `7` jumps, `1` ladder route, `81` walk-off-ledge routes, and `1` elevator route.
  - `tools/q2aas/validation_manifest.json` now declares schema `worr-q2aas-validation-manifest-v1`; the validator rejects malformed schema/version/task IDs, unknown manifest keys, wrong gate types, unknown baseline metric/travel names, and non-integer baseline values before conversion. The JSON report now records manifest provenance and pending reference-map categories.
  - `q2aas-staged-smoke` now runs `--manifest-schema-smoke`, creating and removing a malformed scratch manifest and passing only when the schema validator reports the expected unknown-metric and non-integer-threshold errors.
  - Added `--stage-aas` validation output staging and the Meson `q2aas-stage-aas` target. The target runs strict manifest validation before copying `.tmp\q2aas\mm-rage.aas` to `.install\basew\maps\mm-rage.aas`; `.tmp\q2aas\stage-report.json` records the staged path and SHA-256 `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
  - Added `tools/q2aas/audit_worr_q2aas_stage.py` and the Meson `q2aas-stage-audit` target. The audit verifies staged `.aas` files live under `.install\basew\maps\`, are non-empty, and match the hashes recorded in `.tmp\q2aas\stage-report.json`; `.tmp\q2aas\stage-audit-report.json` currently reports `map_count = 1`, `failed_count = 0`, and `status = passed`.
  - Added archive-backed manifest map support using `archive` plus `archive_member`, with extraction into `.tmp\q2aas\packaged-maps\` and `map_source` provenance in reports/metadata. The Meson `q2aas-package-map-smoke` target creates `.tmp\q2aas\package-map-smoke.pkz` from the staged `mm-rage.bsp`, extracts `maps/mm-rage.bsp`, converts it, and writes `.tmp\q2aas\package-map-smoke-report.json`; the current smoke passes with `428` areas, `562` reachability records, and `4` clusters.
  - Hardened archive-backed manifest validation: archive members must be relative in-archive paths, path/archive conflicts and missing archive members fail schema smoke, and absolute/traversal archive members are rejected before extraction. `q2aas-staged-smoke` now covers those expected-failure archive manifest cases.
  - Added the Meson `q2aas-package-audit` target to verify staged AAS release-payload representation and write `.tmp\q2aas\package-audit-report.json`.
  - Added the Meson `q2aas-package-aas` and `q2aas-package-archive-audit` targets. The package step injects `maps/mm-rage.aas` into `.install\basew\pak0.pkz`, and the archive-required audit verifies the packaged member hash against `.tmp\q2aas\stage-report.json`.
  - Added `refresh_install.py --package-q2aas-aas` so a normal `.install` refresh can rebuild `pak0.pkz` from assets, re-inject staged q2aas AAS, run the archive-required q2aas audit, and then validate the staged payload.
  - Generic staged release validation can now require named members inside `pak0.pkz` with SHA-256 checks; `refresh_install.py --package-q2aas-aas --platform-id ...` derives required packaged AAS members from `.tmp\q2aas\stage-report.json`.
  - Release packaging now defaults to generated AAS data only and rejects q2aas/BSPC tool binaries from asset packs and staged binary releases. `refresh_install.py` stages a required `licenses/` notice bundle, release target manifests require it, and artifact verification rejects missing or empty notice sidecars.
  - Added reference-map feature readiness diagnostics for water, slime, lava, teleport, elevator, and door coverage. The current staged report proves elevator coverage through `mm-rage`; the other feature categories now report explicit no-candidate-yet gaps and strict-gate failures until local BSP candidates are validated and staged. The follow-up inventory pass now records required-feature evidence, `missing_category_diagnostics`, `feature_coverage`, and `feature_gap_maps` so missing reference coverage can be reviewed without reading raw manifest placeholders.
  - Added q2aas generator scope and semantic-policy report fields. `.tmp\q2aas\validation-report.json` now records `generator_scope`, parsed `presence_policy` from `cfg/worr_q2.cfg`, per-map `aas_semantic_policy` for water, slime, lava, `trigger_hurt`, slick, sky, nodraw, detail, translucent handling, and trailing BSPX tolerance without editing imported BSPC source.
  - Added q2aas reachability-policy and metadata-policy diagnostics. The staged report now records water entry/exit, mover, teleport, and rocket-jump route ownership; sidecars stay scratch-only while package reports/archive-member validation carry packaged AAS identity. A local staged `q2dm1.bsp` now passes optional baselines with `1245` areas, `3066` reachability records, `254` swim routes, `19` water-jump routes, and `10` elevator routes.
  - Expanded the local staged q2aas reference set to eight maps: `mm-rage`, `q2dm1`, `q2dm2`, `q2dm8`, `q2ctf1`, `base1`, `base2`, and `train`. The new manifest rows add structural/travel baselines, CTF team objective reachability via `team_objective_report`, campaign trigger/door/progression diagnostics via `campaign_progression_report`, and water-backed liquid coverage while keeping slime/lava as explicit future feature-candidate gaps.
  - `q2aas-stage-aas` now stages all eight generated `.aas` files and `refresh_install.py --package-q2aas-aas` packages/audits all eight archive members in `.install\basew\pak0.pkz`.
  - Implementation logs: `docs-dev/q2aas-generator-vendor-bootstrap-2026-06-16.md`, `docs-dev/q2aas-generator-q2-preset-validation-2026-06-16.md`, `docs-dev/q2aas-generator-q2-reachability-bridge-2026-06-16.md`, `docs-dev/q2aas-generator-validation-matrix-2026-06-16.md`, `docs-dev/q2aas-generator-deterministic-metadata-2026-06-16.md`, `docs-dev/q2aas-generator-entity-diagnostics-2026-06-16.md`, `docs-dev/q2aas-generator-diagnostic-gates-2026-06-16.md`, `docs-dev/q2aas-generator-baseline-regression-gates-2026-06-17.md`, `docs-dev/q2aas-generator-manifest-schema-validation-2026-06-17.md`, `docs-dev/q2aas-generator-manifest-schema-smoke-2026-06-17.md`, `docs-dev/q2aas-generator-aas-staging-2026-06-17.md`, `docs-dev/q2aas-generator-stage-audit-2026-06-17.md`, `docs-dev/q2aas-generator-packaged-map-smoke-2026-06-17.md`, `docs-dev/q2aas-generator-archive-manifest-guardrails-2026-06-17.md`, `docs-dev/q2aas-generator-package-audit-2026-06-17.md`, `docs-dev/q2aas-generator-archive-packaging-2026-06-17.md`, `docs-dev/q2aas-generator-refresh-install-integration-2026-06-17.md`, `docs-dev/q2aas-generator-stage-archive-member-validation-2026-06-17.md`, `docs-dev/q3a-botlib-release-policy-2026-06-18.md`, `docs-dev/q2aas-reference-map-diagnostics-2026-06-18.md`, `docs-dev/q2aas-reference-map-coverage-round-2026-06-18.md`, `docs-dev/q2aas-generator-policy-semantics-closeout-2026-06-21.md`, `docs-dev/q2aas-generator-reachability-metadata-round-2026-06-21.md`, `docs-dev/q3a-botlib-reference-map-runtime-adapter-round-2026-06-21.md`.
- `FR-04-T12` / `DV-07-T06` In Progress:
  - Added the WORR-native `src/game/sgame/bots/bot_runtime.*` AAS runtime shell as the first Phase 2 foothold.
  - Registered disabled-by-default `sg_bot_enable`, `sg_bot_debug`, `sg_bot_debug_aas`, `sg_bot_debug_route`, `sg_bot_debug_goal`, `sg_bot_debug_client`, and `sg_bot_cpu_budget_ms` cvars.
  - Map start and entity reload now probe `maps/<current-map>.aas` through WORR's filesystem extension when `sg_bot_enable` is set, decode the Q3A/BSPC AAS v5 header transform, validate the `EAAS` version 5 lump table, and record area/reachability/cluster counts for debug status.
  - Runtime smoke against the refreshed `.install` payload loads packaged `maps/mm-rage.aas` with `areas=428`, `reachability=562`, and `clusters=4`.
  - Added the compiled Q3A BotLib import boundary: `src/game/sgame/bots/q3a/` is reserved for commit-pinned imports, `q3a_botlib_boundary.*` records the planned AAS/runtime file inventory, and `botlib_adapter.*` now owns the future setup/shutdown/map/frame bridge.
  - Imported the first commit-pinned Q3A BotLib utility subset from `id-Software/Quake-III-Arena` commit `dbe4ddb10315479fc00086f08e25d968b4b43c49`: `q_shared.h`, `surfaceflags.h`, `botlib.h`, `be_interface.h`, `l_log.h`, `l_memory.*`, and `l_libvar.*`.
  - Added the `q3a_botlib_utility` build group and WORR-native `q3a_botlib_import.*` smoke bridge. The adapter now runs a Q3A LibVar smoke during initialization and `sg_bot_debug_aas 2` prints the result.
  - Imported the next commit-pinned Q3A AAS file-loader subset: `be_aas_file.c`, `aasfile.h`, `be_aas*.h`, and parser utility headers now compile in the same boundary without editing imported Q3A source.
  - Added a temporary read-only in-memory filesystem bridge, logging shims, and `Q3A_BotLibImport_LoadAASBuffer`/`Q3A_BotLibImport_UnloadAAS` so WORR can pass already-loaded `maps/<map>.aas` bytes into Q3A's native loader; `be_aas_main.c` now owns Q3A `aasworld` and `AAS_Error`.
  - Imported Q3A `be_aas_sample.c` as the first read-only AAS query layer and added temporary WORR-owned shims for later reachability/entity-collision/vector runtime hooks.
  - Imported Q3A `be_aas_reach.c`, removed the temporary WORR-owned `AAS_AreaReachability` shim, initialized conservative bridge-owned `aassettings`, and initially kept then-pending entity-collision, movement, and debug-line hooks quarantined behind temporary stubs.
  - Replaced the temporary `AngleVectors` and `Sys_MilliSeconds` bridge shims with real Q3A-style angle-vector math and server-frame `level.time` milliseconds fed through `botlib_adapter.*`.
  - The runtime now validates the active `maps/<map>.bsp` as Quake II `IBSP` version 38, feeds lump 0 entity text into the Q3A bridge before AAS load, and replaces the temporary Q3A BSP entity/epair callbacks with active-map lookup data.
  - The runtime now also feeds Q2 BSP lump 13 model records into the Q3A bridge and replaces the temporary `AAS_BSPModelMinsMaxsOrigin` callback with active-map inline model bounds.
  - The runtime now feeds the active Q2 BSP collision lumps into the Q3A bridge and replaces the temporary static `AAS_PointContents`/`AAS_Trace` stubs with a Q2 BSP static-world point/box trace walker.
  - The runtime now also feeds the active Q2 BSP visibility lump into the Q3A bridge and implements `AAS_inPVS` / `AAS_inPHS` through Q2 leaf clusters and compressed PVS/PHS rows.
  - Imported Q3A `be_aas_route.c` and `l_crc.*`, initializes/frees route caches around active-map AAS load/unload, and added a bridge smoke for `AAS_AreaTravelTimeToGoalArea`, `AAS_AreaReachabilityToGoalArea`, and `AAS_PredictRoute`.
  - Imported Q3A `be_aas_main.c`, moved `aasworld`/`AAS_Time`/`AAS_Error` ownership to the imported runtime, runs Q3A `AAS_Setup`/`AAS_SetInitialized`/`AAS_Shutdown` around the active-map AAS handoff, and calls imported `AAS_StartFrame` each server frame.
  - Imported Q3A `be_aas_entity.c` so the Q3A entity cache now owns `AAS_ResetEntityLinks`, `AAS_InvalidateEntities`, and `AAS_UnlinkInvalidEntities`; dynamic BSP leaf entity links now use active-map Q2 BSP node/leaf data.
  - `Bot_RuntimeRunFrame` now runs after the server entity update pass and pushes WORR bot-facing snapshots into imported Q3A `AAS_UpdateEntity` each frame, with inactive imported cache slots explicitly unlinked.
  - The Q3A AAS `AAS_EntityCollision` path now calls a registered WORR `gi.clip` entity trace bridge; the snapshot sync also translates WORR SOLID_BSP server model config indices to Q3A inline BSP model numbers before `AAS_UpdateEntity`.
  - Dynamic entity BSP leaf links and Q3A `AAS_BoxEntities` now use the parsed active-map Q2 BSP tree, with unload cleanup for link lists.
  - Imported Q3A `be_aas_move.c`, removed the temporary movement prediction/drop/jump bridge stubs, seeded WORR/Q2-oriented movement LibVars before imported AAS setup, and added verbose smoke for `AAS_DropToFloor`, `AAS_HorizontalVelocityForJump`, and `AAS_PredictClientMovement`.
  - Added a callback-backed Q3A debug draw bridge for `AAS_DebugLine`, `AAS_PermanentLine`, `AAS_ClearShownDebugLines`, `AAS_DrawPermanentCross`, and `AAS_DrawArrow`, gated by `sg_bot_debug_aas >= 3`, `sg_bot_debug_route`, or `sg_bot_debug_goal`.
  - Added a route/goal overlay smoke path that uses imported Q3A route prediction and the debug draw bridge under `sg_bot_debug_route` / `sg_bot_debug_goal`.
  - Added a callback-backed Q3A debug polygon bridge for `botimport.DebugPolygonCreate` / `DebugPolygonDelete`; `sg_bot_debug_aas 3` now renders a sampled polygon outline/fan through WORR `gi.Draw_Line` and reports create/delete counters.
  - Imported Q3A `be_aas_debug.c`, replaced the remaining WORR-owned debug-line helper definitions with Q3A `botimport.DebugLineCreate` / `DebugLineShow` / `DebugLineDelete` callbacks, and added a `sg_bot_debug_aas 3` smoke for imported `AAS_ShowArea` / `AAS_ShowAreaPolygons`.
  - Imported Q3A `be_aas_cluster.c`, removed the temporary WORR-owned `AAS_InitClustering` no-op, and added `q3a_cluster` verbose smoke for the loaded AAS cluster table.
  - Imported Q3A `be_aas_routealt.c`, removed the temporary WORR-owned `AAS_InitAlternativeRouting` / `AAS_ShutdownAlternativeRouting` stubs, now initializes alternative routing after route-cache setup, and added `q3a_alt_route` verbose smoke for `AAS_AlternativeRouteGoals`.
  - Imported Q3A `be_aas_optimize.c`, removed the temporary WORR-owned `AAS_Optimize` no-op, and kept the mutating optimization path opt-in behind Q3A `aasoptimize` behavior.
  - The current imported Q3A AAS runtime C set now rebuilds as part of `sgame_x86_64`, including AAS file load/setup/start-frame, sampling, reachability, route cache/query, clustering, alternative routes, optimization hooks, entity cache, movement prediction, debug helpers, LibVars, memory, and CRC utilities. This closes the current AAS runtime compile surface without claiming the separate Q3A arena AI, EA command layer, or goal/weight system.
  - `AAS_Trace` and `AAS_PointContents` are now final-owned by the active-map Q2 BSP static-world collision bridge, `AAS_inPVS` / `AAS_inPHS` are final-owned by the active-map Q2 BSP visibility bridge, and `AAS_EntityCollision` reaches WORR's dynamic entity collision path through `botlib_adapter.*`, `BotRuntimeEntityTrace`, and server-game `gi.clip`.
  - BotLib adapter initialization is now idempotent for the game-module lifetime, and `ShutdownGame()` calls `Bot_RuntimeShutdown()` after level unload/lifecycle status so the imported BotLib memory/filesystem layer receives an explicit module-level shutdown.
  - Q3A `bot_*` LibVars stay internal to the imported AAS runtime; WORR public policy remains in `sg_bot_*`, with only Q2/WORR `phys_*` and `rs_*` movement/reachability inputs seeded for imported `be_aas_move.c`.
  - `BotRuntimeBuildEntitySnapshot()` now splits BotLib entity snapshots for players, bots, spectators, and monsters/NPCs, and `sg_bot_debug_aas` loaded-status output reports the current counts for those categories.
  - Follow-up entity scheduling now also separates regular pickups, dropped items, traps/projectiles/hazards, doors/plats/movers, and objective/flag entities; item desirability refreshes are staggered per bot with cache-reuse status, route recomputation cadence exposes rate-limit counters, and the live aim path is documented as blackboard-visible-target gated.
  - Replaced the quiet Q3A `botimport.Print` capture with a WORR-owned print callback bridge; warnings/errors/fatals now forward to `gi.Com_PrintFmt`, message-level chatter is gated behind `sg_bot_debug_aas >= 3`, and verbose status reports `q3a_print_*` counters.
  - Added a Q3A `botimport.BotClientCommand` safety bridge that reaches WORR runtime validation, requires a bot client, and safely rejects command execution until a dedicated bot command dispatcher exists.
  - Replaced raw Q3A `botimport.GetMemory`, `FreeMemory`, and `HunkAlloc` callbacks with a tracked bot-owned allocator that reports zone/hunk active and peak bytes, grouped hunk releases, and allocation failures.
  - Replaced the singleton active-AAS memory file shim with a read-only Q3A `botimport.FS_*` bridge backed by WORR filesystem load/free callbacks, while keeping the memory buffer as a fallback path.
  - Runtime smoke against the refreshed `.install` payload loads packaged `maps/mm-rage.aas` and reports `utility=Q3A LibVar smoke passed`, `q3a_aas=Q3A AAS file load passed`, `q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1`, `q3a_sample_reachability=1`, `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`, `q3a_route_overlay=Q3A route overlay passed: callback=yes start=3 goal=6 end=6 travel_time=113 reachability=1 lines=2 crosses=3 arrows=1 clears=1 failures=0`, `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`, `q3a_movement_drop=yes`, `q3a_movement_jump=yes`, `q3a_debug_draw=Q3A route overlay debug draw passed: callback=yes lines=2 crosses=3 arrows=1 clears=0`, `q3a_debug_draw_callback=yes`, `q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0`, `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`, `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`, `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`, `q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18`, `q3a_bsp_leaf_link_failures=0`, `q3a_bsp_box_entities_smoke=yes`, `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start`, `q3a_bsp_entity_smoke=yes`, `q3a_bsp_model=Q3A BSP model lump load passed: maps/mm-rage.bsp models=18 smoke_model=1 mins=(-328.0 -584.0 24.0) maxs=(-192.0 -440.0 256.0)`, `q3a_bsp_model_smoke=yes`, `q3a_bsp_collision=Q3A BSP collision load passed: maps/mm-rage.bsp planes=1367 nodes=1863 leafs=1882 brushes=1142 point_contents=1 trace_fraction=0.347 startsolid=0 allsolid=0`, `q3a_bsp_point_contents_smoke=yes`, `q3a_bsp_trace_smoke=yes`, `q3a_bsp_visibility=Q3A BSP visibility load passed: maps/mm-rage.bsp clusters=303 smoke_cluster=0 pvs_visible=142 phs_visible=289`, `q3a_bsp_pvs_smoke=yes`, `q3a_bsp_phs_smoke=yes`, `q3a_angle_vectors=Q3A AngleVectors smoke passed`, `q3a_time_ms=25`, `q3a_areas=428`, `q3a_reachability=562`, `q3a_clusters=4`, and `planned_files=48`; active item route-goal selection, exact-origin position route goals, basic item reservation, route-point look-ahead steering, velocity-aware command yaw, stuck-progress repath, short stuck recovery commands, item-goal blacklist cooldowns, failed-goal reason diagnostics, reachability-aware movement-state command intent, natural jump/ladder/barrier-jump/walk-off-ledge/elevator travel-type validation, interaction wait/use retry telemetry, and a `bot_brain.*` command/status ownership split now exist, while natural map-backed crouch/swim/waterjump runtime proof waits on reference maps and higher-level behavior integration remains pending.
  - Rocket-jump route policy smoke now keeps inherited `TRAVEL_ROCKETJUMP` reachability default-blocked unless `sg_bot_allow_rocketjump 1` is set; mode `14` reports `last_reachability_type=12`, `route_failures=0`, and `pass=1`, while mode `15` reports `travel_type_goal_expect_blocked=1`, `commands=0`, `route_commands=0`, `route_failures=8`, and `pass=1`.
  - Four-bot frame-command smoke now validates the staged fake-client command path above the two-bot item-reservation baseline; mode `16` adds four bots, reports `commands=38`, `route_commands=38`, `route_failures=0`, `item_goal_active_reservations=4`, and `pass=1`.
  - Eight-bot frame-command smoke now extends the same staged fake-client command validation to eight active bots; mode `17` adds `B|Mover` through `B|MoverEight`, reports `commands=92`, `route_commands=92`, `route_failures=0`, `item_goal_active_reservations=8`, and `pass=1`.
  - Ten-minute eight-bot frame-command soak now validates the long-running route-command path; mode `18` reports `elapsed_ms=600001`, `frames=192036`, `commands=192036`, `route_commands=192036`, `route_failures=0`, `skipped_inactive=0`, and `pass=1` after refresh-install packaging.
  - Same-map reload repeat smoke now validates the eight-bot route-command path across both default `gamemap` reloads and an opt-in forced `map "<map>" force` restart path; mode `19` now reports command/restart markers, restart-time `realtime_reset`, cleanup status gates with zero active reservations, three-cycle forced restart validation, and default two-cycle regression coverage.
  - Natural movement support diagnostics now report packaged `mm-rage.aas` lacks natural crouch/swim/waterjump routes while the elevator/platform interaction proof reports wait/use activation counters through the route-command status path. Follow-up diagnostics add unsupported masks, per-type reason codes, AAS area/goal-area fields, route-start origins for future reference maps, and interaction context counts by world entity type.
  - First behavior/action dispatcher units now compile as WORR-native `bot_actions.*`, `bot_items.*`, and `bot_combat.*` boundaries; `bot_brain.*` samples the dispatcher every accepted command frame, applies validated attack/use decisions through `BotActions_ApplyDecisionDetailed()`, records pending weapon/inventory intents, and emits `q3a_bot_action_status`. Follow-up Phase 4/6 support now adds a per-bot perception blackboard, Q2/WORR weapon metadata, item utility scoring hooks including special-item buckets, health/armor focused routing, combat enemy-fact/damage-attribution proof helpers, opt-in aim/fairness plus live aim-profile/projectile-leading consumption, live pickup and observed-respawn timing consumers, item timer disable/fuzz helper policy, weapon-switch request/observation proof state, deterministic health/armor pickup proof helpers, exact `use_index_only` weapon/inventory dispatch for accepted pending intents, per-bot enemy health/armor estimates from visible observations and split bot-attributed damage deltas, estimate-aware finisher/armor-pressure weapon scoring, carried-weapon inventory scanning, conservative carried non-weapon inventory/powerup use policy, environment utility and sphere deployable policy, placement-checked doppelganger use, last-resort personal teleporter escape policy, safety-gated nuke policy, command-owned nuke retreat routing, generic timed route-goal owner telemetry with nuke retreat, personal teleporter escape, coop leader-route, coop LeadAdvance, team role-route, CTF role-route, and FFA roam-route consumers, target-source-aware team-objective proof helpers, deterministic role/lane-depth team-policy helpers, FFA/TDM/CTF objective-side match/item/friendly-fire helper policy, profile-derived match-role selection, profile-derived teamplay/objective/friendly-fire-care match-policy hints, profile-derived item-greed/item-denial/powerup-timing/retreat-health match item-policy hints, profile-derived movement-style match-policy hints, default-off bot chat-policy live-dispatch, team-only audience, global rate-limit gating, profile chat-personality initial utterance selection, smoke-gated reply selection, and smoke-gated multi-event reply selection, coop/resource policy helper metadata, default-off FFA roam-route ownership, default-off FFA spawn-camp avoidance, default-off FFA item-role pickup scoring, default-off FFA role-combat attack ownership, default-off FFA spawn-camp combat avoidance, default-off team role-route ownership, default-off team item-role route selection, default-off team resource-denial pickup scoring, default-off match item-policy umbrella pickup scoring, default-off team friendly-fire attack suppression, default-off team role-combat attack ownership, default-off CTF role-route ownership, default-off CTF role-combat attack ownership, default-off CTF dropped-flag route ownership, default-off CTF carrier-support route ownership, default-off CTF base-return route ownership, default-off CTF objective-route policy ownership, default-off CTF objective-route precedence over generic role routing, default-off CTF item-role pickup scoring, default-off coop WaitForLeader and interaction-retry command owners, a default-off coop resource-share route-selection gate, default-off coop anti-blocking command ownership, default-off coop monster target-sharing blackboard adoption, default-off coop door/elevator source-owner plus teammate hold commands, and central behavior owner arbitration with cvar classification and handoff telemetry. `sg_bot_behavior_enable` now groups the current default-off behavior proof family behind one opt-in switch, mode `52` proves the umbrella activates TDM role-route, role-combat, friendly-fire, and match item-policy gates without setting the individual proof cvars, mode `53` proves staged profile roles feed match-policy requested-role selection, mode `54` proves staged profile teamplay/objective/friendly-fire-care hints feed CTF match policy, mode `55` proves staged profile item-greed/item-denial/powerup-timing/retreat-health hints feed TDM match item/resource policy, mode `56` proves staged profile movement-style hints feed TDM match policy, mode `57` proves profile chat metadata and `sg_bot_allow_chat` while submitting a conservative live dispatch, mode `58` proves the `sg_bot_chat_team_only` audience path, mode `59` proves `sg_bot_chat_min_interval_ms` rate limiting without dispatch failures, mode `60` proves profile chat-personality initial utterance selection, mode `61` proves profile chat-personality reply selection for the first team-ready event, mode `62` proves profile chat-personality reply selection across team-ready and route-ready proof events, mode `63` proves central behavior arbitration with route/item/combat candidates, combat ownership, handoff evidence, and live/smoke/debug/deprecated cvar classification, modes `20` through `63` now use those hooks as implemented smoke scenarios, and `sv_bot_mapvote_smoke 2` covers the native map-vote transition proof, while broader autonomous team behavior, deeper trigger-aware/campaign-specific coop command ownership, broader objective intelligence, and richer Q3A-style bot chat live triggers and phrase libraries remain future work.
  - Scenario and performance validation tooling now exists under `tools/bot_scenarios/` and `tools/bot_perf/`, covering 77 implemented catalog rows total: 76 automated short-run scenario smokes plus one manual high-bot soak/degradation policy row, the now-empty default pending scenario set, raw reserved-mode diagnostics, split-marker raw metric merging from frame/blackboard/action/objective/source-counter status lines, embedded proof-marker parsing, field-wise marker metric lookup for split status rows, reserved-mode runtime warmup waiting, hard marker gates for action dispatch, weapon scoring arsenal telemetry, aim/fire policy depth telemetry, ammo-pressure pickup telemetry, survival inventory-use telemetry, survival health-route telemetry, survival armor-route telemetry, combat/survival regression telemetry, live aim policy, item timer proof, trace-checked corner cutting, map-change repeat, map-restart cleanup, warmup bot-start readiness, vote bot-exclusion, admin bot privilege audit, tournament bot veto-exclusion, tournament replay reset, match logging schema/versioning, MyMap queue/consume, queued nextmap transition, map-vote bot-exclusion transition, scoreboard bot classification, intermission bot cleanup, duel queue/spectator handling, FFA/TDM readiness, target-memory decay, behavior arbitration owner/cvar classification, profile-derived match-role selection, profile-derived team-policy hint selection, profile-derived item-policy hint selection, profile-derived movement-policy hint selection, bot chat-policy live-dispatch, team-only audience gating, global rate-limit gating, initial personality selection, smoke-gated reply selection, and smoke-gated multi-event reply selection, FFA roam-route ownership, FFA spawn-camp avoidance, FFA item-role pickup scoring, FFA role-combat attack ownership, FFA spawn-camp combat avoidance, team role-route ownership, team item-role pickup scoring, team resource-denial pickup scoring, umbrella match item-policy pickup scoring, integrated behavior umbrella activation, team friendly-fire attack suppression, team role-combat attack ownership, CTF role-route ownership, CTF role-combat ownership, CTF dropped-flag route ownership, CTF carrier-support route ownership, CTF base-return route ownership, CTF objective-route policy ownership, CTF objective-route precedence, CTF item-role pickup scoring, coop readiness, coop leader routing, coop LeadAdvance, coop progress-wait command ownership, coop interaction retry, coop resource-share route selection, coop anti-blocking command ownership, coop target-sharing adoption, and coop door/elevator cooperation, plus optional discovery for other emerging fields, derived perf budgets, guarded comparison reports, fixture-aware parser tests, analyzer support for optional source-counter fields, import/adapter source-counter getters, split source-counter status emission, source-counter completeness diagnostics, validated bot-frame/route/Q3A-route/static-BSP/entity-clip CPU timing fields, and Q3A memory source-counter reporting. The follow-up full-suite reconciliation now passes the expanded 76-row automated catalog from `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z` with 76 passing rows and no failures/timeouts/errors/pending rows, so the next work is live behavior depth rather than harness reconciliation.
  - Botfile profile work now includes Q3/Gladiator-style companion families, idTech3-style `botfiles/scripts/*_s.c` companions, multi-skill character validation, deeper behavior metadata validation, shared teamplay event-name parity, utility/chat/weapon/script parity polish, script-package coverage, script parity validation, loose `botfiles` mirroring for no-zlib dedicated builds, user-facing profile docs, and first live use of `WORR_ROLE`, `WORR_TEAMPLAY_BIAS`, `WORR_OBJECTIVE_BIAS`, `WORR_FRIENDLY_FIRE_CARE`, `WORR_ITEM_GREED`, `WORR_ITEM_DENIAL`, `WORR_POWERUP_TIMING`, `WORR_RETREAT_HEALTH`, and `WORR_MOVEMENT_STYLE` as match-policy hints. Profile chat metadata now has a default-off `sg_bot_allow_chat` status boundary, conservative live dispatch proof, team-only audience proof, global rate-limit proof, initial personality selection proof, smoke-only reply selection proof, and smoke-only multi-event reply proof, while richer conversation and broader live event-trigger behavior remains future work.
  - The latest promotion waves connect bot-attributed damage to the real damage path, health/armor pickup observation to successful item touches, CTF objective pickup/return/capture observations to authoritative gameplay branches, live-aim firing proof, deterministic item-timer policy proof, trace-backed corner-cutting proof, FFA/TDM/coop readiness proof, behavior arbitration owner/cvar proof, profile-role match-policy proof, profile team-policy match proof, profile item-policy match proof, profile movement-policy match proof, bot chat-policy live-dispatch proof, bot chat team-only proof, bot chat rate-limit proof, bot chat initial-personality proof, bot chat reply-policy proof, bot chat event-policy proof, duel queue/spectator proof, map-restart cleanup proof, warmup bot-start readiness proof, vote bot-exclusion proof, MyMap queue/consume proof, scoreboard bot classification proof, intermission bot cleanup proof, queued nextmap transition proof, map-vote bot-exclusion transition proof, target-memory decay proof, weapon-scoring proof, aim/fire policy depth proof, ammo-pressure pickup proof, survival inventory-use proof, survival health-route proof, survival armor-route proof, combat/survival regression proof, default-off FFA roam-route ownership, default-off FFA spawn-camp avoidance, default-off FFA item-role pickup scoring, default-off FFA role-combat attack ownership, default-off FFA spawn-camp combat avoidance, default-off team role-route ownership, default-off team item-role pickup scoring, default-off team resource-denial pickup scoring, default-off match item-policy umbrella pickup scoring, default-off behavior policy umbrella activation, default-off team friendly-fire attack suppression, default-off team role-combat attack ownership, default-off CTF role-route ownership, default-off CTF role-combat ownership, default-off CTF dropped-flag route ownership, default-off CTF carrier-support route ownership, default-off CTF base-return route ownership, default-off CTF objective-route policy ownership, default-off CTF objective-route precedence over generic role routing, default-off CTF item-role pickup scoring, default-off coop progress-wait, interaction-retry, LeadAdvance, resource-share, anti-blocking, target-sharing, and door/elevator source-hold proofs, and stricter scenario marker checks for live aim plus match-policy evidence. Modes `20` through `71`, `sv_bot_team_policy_smoke` modes `2` and `3`, `sv_bot_warmup_smoke 2`, `sv_bot_vote_smoke 2`, `sv_bot_mymap_smoke 2`, `sv_bot_nextmap_smoke 2`, `sv_bot_mapvote_smoke 2`, `sv_bot_scoreboard_smoke 2`, `sv_bot_intermission_smoke 2`, mode `19` map-change/map-restart rows, plus the coop reuse rows are implemented smoke scenarios; the expanded automated catalog now has a green 76/76 reconciliation baseline after the latest status-surface growth.
  - Raw reserved-mode diagnostics preserve latest marker source lines and missing-metric source hints. The earlier blocked mode `22` route-only evaluation is retained as a diagnostic artifact, and the later promotion pass supersedes it with health/armor-specific counters in the implemented `health_armor_pickup` scenario.
  - The focused clustering smoke reports `q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0`.
  - The focused alternative-route smoke reports `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`.
  - The optimize import smoke keeps `aasoptimize=0` on the default loaded-AAS path while the linked imported `AAS_Optimize` implementation replaces the final no-op.
  - The focused print-bridge smoke with `sg_bot_debug_aas 3` reports `Q3A BotLib message: trying to load maps/mm-rage.aas`, `q3a_print_callback=yes`, `q3a_print_messages=2`, `q3a_print_warnings=0`, and `q3a_print_errors=0`.
  - The focused BotClientCommand smoke with `sg_bot_debug_aas 3` reports the bridge passed with `callback=yes`, `client=0`, `accepted=0`, `rejected=1`, and `failures=0`.
  - The focused memory-allocator smoke with `sg_bot_debug_aas 3` reports `q3a_memory_zone_active=239894`, `q3a_memory_hunk_active=691078`, `q3a_memory_hunk_allocs=17`, and `q3a_memory_failures=0`; implemented scenario source-counter output now also reports Q3A memory active/peak/available fields.
  - The focused filesystem-bridge smoke with `sg_bot_debug_aas 3` reports `q3a_fs_passed=yes`, `q3a_fs_files=1`, `q3a_fs_memory_files=0`, `q3a_fs_read_bytes=277484`, and `q3a_fs_writes_rejected=0`.
  - The dedicated bot slot smoke now reports `Queued bot Charlie for the next server frame`, `Added bot B|Charlie in slot 1`, `q3a_bot_slot_smoke_after_deferred_pair count=2`, and `q3a_bot_slot_smoke_after_remove_all count=0`.
  - The dedicated min-player autofill smoke now reports `q3a_bot_min_players_smoke=begin target=3`, `Added bot B|bot1 in slot 0`, `Added bot B|bot2 in slot 1`, `Added bot B|bot3 in slot 2`, `q3a_bot_min_players_smoke_after_fill count=3 auto=3 humans=0 target=3`, target trim to `count=1 auto=1`, and disable cleanup to `q3a_bot_min_players_smoke=end final_count=0`.
  - The dedicated profile smoke now reloads the WORR-authored staged `smoke` profile, resolves `sg_bot_add smoke`, and reports `q3a_bot_profile_smoke_after_add count=1 name=B|Smoke profile=smoke skin=male/grunt skill=4 reaction=250 aggression=0.65 aim_error=2.5 preferred_weapon=rocketlauncher chat=quiet role=attacker movement=strafe` followed by cleanup to `q3a_bot_profile_smoke=end final_count=0`; the implemented scenario suite now includes the profile-backed spawn proof.
  - The dedicated bot frame-command smoke now asks `sgame` for cached AAS route-steered bot `usercmd_t` data, runs it through the server fake-client movement path, and reports `q3a_bot_frame_command_status frames=8 commands=8 route_requests=8 route_queries=2 route_refreshes=2 route_reuses=6 route_commands=8 route_failures=0 route_invalid_slots=0 route_cadence_refreshes=1 route_target_refreshes=0 route_drift_refreshes=0 route_preferred_goal_refreshes=0 last_route_client=0 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_travel_time=130 last_reachability=218 last_stop_event=0 skipped_invalid=0 skipped_not_bot=0 skipped_runtime=0 skipped_inactive=0 expected_min_frames=1 expected_min_commands=1 pass=1`. With `sg_bot_debug_route 1` and `sg_bot_debug_goal 1`, the same path reports native cached overlay counters, reachability fields, and bounded polyline counters `route_debug_routes=8`, `route_debug_goals=8`, `route_debug_arrows=8`, `route_debug_labels=8`, `route_debug_polyline_points=16`, `route_debug_polyline_segments=24`, `route_debug_missing_frames=2`, `last_route_debug_client=0`, `last_current_area=224`, `last_route_point_count=2`, `last_reachability_type=2`, `last_reachability_flags=2`, and `last_reachability_end_area=217`; selected-client debug smoke with `sg_bot_debug_client 1` suppresses the slot-0 overlay and reports `route_debug_routes=0`, `route_debug_filtered_slots=8`, `route_debug_filter_miss_frames=10`, `last_debug_filter_client=1`, and `pass=1`.
  - Persistent route-goal smoke now reports native goal ownership with `route_goal_requests=1`, `route_goal_assignments=1`, `route_goal_cache_reuses=6`, `route_goal_clears=0`, `route_goal_fallbacks=0`, `last_persistent_goal_area=227`, `last_goal_clear_reason=0`, and `pass=1`; active-pickup item goals now select a live item entity and report `last_item_goal_area=415`; the two-bot reservation smoke reports `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, and `pass=1`.
  - Removed the inherited Q2R `src/game/sgame/bots/bot_debug.*`, `bot_exports.*`, and `bot_utils.*` surface from the active server-game bot implementation; the active bot surface is now the WORR/Q3A BotLib path through `bot_runtime.*`, `botlib_adapter.*`, `bot_nav.*`, `bot_think.*`, and `q3a/`.
  - Reserved scenario smoke modes now wait for runtime-backed frame status before final capture when early mode `20` through `23` frames only report skipped runtime work, and print `q3a_bot_frame_command_smoke_runtime_wait` while deferring evaluation.
  - Health/armor item-focus routing first passed the reserved mode `22` routing proof with `pass=1`, `route_failures=0`, `item_focus=health_armor`, `item_goal_assignments=15`, and final `last_item_goal_item=4`; the later promoted `health_armor_pickup` scenario now adds deterministic low-health/low-armor setup and pickup-delta checks.
  - Team-objective helper scaffolding now exposes deterministic flag/objective assignment helpers plus compact `q3a_bot_frame_command_status` fields and a dedicated `q3a_bot_objective_status` line. The latest helper lanes add role/lane-depth policy metadata, real gameplay hooks now feed objective pickup/return/capture counters for the smoke proof, and a default-off TDM route-owner proof consumes match role/lane policy; broader autonomous role consumption across live CTF/TDM flows remains pending.
  - `Bot_BeginFrame` and `Bot_EndFrame` remain lifecycle stubs unless the runtime is enabled and AAS is loaded; per-bot route cache/query cadence plus native route/goal debug markers, current-area labels, next-reachability status, bounded route polylines, selected-client route debug filtering, active item route-goal selection, exact-origin position route goals, basic item reservation, route-point look-ahead steering, velocity-aware command yaw, route-target stabilization for near-origin route steps, trace-checked corner cutting in the nav refresh path, stuck-progress repath, short stuck recovery commands, item-goal blacklist cooldowns, failed-goal reason diagnostics, reachability-aware movement-state command intent, natural jump/ladder/barrier-jump/walk-off-ledge/elevator travel-type validation, default-off rocket-jump route policy, interaction wait/use retry telemetry, per-bot blackboard status, detailed action application, team-objective status, implemented smoke coverage for modes `20` through `23`, and `bot_brain.*` command/status ownership exist, but natural map-backed crouch/swim/waterjump runtime proof, accepted-corner-cut hard-gate/budget coverage, deeper combat/weapon-switch/pickup/objective behavior, and higher-level behavior integration remain pending.
  - Implementation logs: `docs-dev/q3a-botlib-runtime-aas-shell-2026-06-17.md`, `docs-dev/q3a-botlib-import-boundary-2026-06-17.md`, `docs-dev/q3a-botlib-utility-import-2026-06-17.md`, `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`, `docs-dev/q3a-botlib-aas-sample-query-2026-06-17.md`, `docs-dev/q3a-botlib-aas-reach-query-2026-06-17.md`, `docs-dev/q3a-botlib-aas-route-query-2026-06-17.md`, `docs-dev/q3a-botlib-aas-start-frame-2026-06-17.md`, `docs-dev/q3a-botlib-aas-entity-cache-2026-06-17.md`, `docs-dev/q3a-botlib-aas-entity-sync-2026-06-17.md`, `docs-dev/q3a-botlib-aas-entity-trace-2026-06-17.md`, `docs-dev/q3a-botlib-aas-bsp-leaf-link-2026-06-17.md`, `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`, `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-aas-route-overlay-2026-06-17.md`, `docs-dev/q3a-botlib-bridge-time-vector-2026-06-17.md`, `docs-dev/q3a-botlib-bsp-entity-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-bsp-model-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-bsp-collision-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-bsp-visibility-bridge-2026-06-17.md`.
  - Additional implementation logs: `docs-dev/q3a-botlib-aas-debug-polygon-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`, `docs-dev/q3a-botlib-aas-cluster-import-2026-06-17.md`, `docs-dev/q3a-botlib-aas-alternative-route-import-2026-06-17.md`, `docs-dev/q3a-botlib-aas-optimize-import-2026-06-17.md`, `docs-dev/q3a-botlib-print-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-bot-client-command-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-memory-allocator-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-filesystem-bridge-2026-06-17.md`, `docs-dev/q3a-botlib-bot-slot-lifecycle-2026-06-17.md`, `docs-dev/q3a-botlib-multibot-slot-queue-2026-06-17.md`, `docs-dev/q3a-botlib-min-players-autofill-2026-06-17.md`, `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`, `docs-dev/q3a-botlib-profile-behavior-fields-2026-06-17.md`, `docs-dev/q3a-botlib-team-policy-cleanup-2026-06-17.md`, `docs-dev/q3a-botlib-team-policy-smoke-2026-06-17.md`, `docs-dev/q3a-botlib-frame-command-dispatch-2026-06-17.md`, `docs-dev/q3a-botlib-route-steered-frame-commands-2026-06-17.md`, `docs-dev/q3a-botlib-nav-route-cache-2026-06-17.md`, `docs-dev/q3a-botlib-nav-debug-overlay-2026-06-17.md`, `docs-dev/q3a-botlib-nav-reachability-debug-2026-06-17.md`, `docs-dev/q3a-botlib-nav-polyline-debug-2026-06-17.md`, `docs-dev/q3a-botlib-nav-debug-client-filter-2026-06-17.md`.
  - Latest implementation logs: `docs-dev/q3a-botlib-nav-persistent-goal-2026-06-18.md`, `docs-dev/q3a-botlib-legacy-bot-surface-removal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-item-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-item-reservation-2026-06-18.md`, `docs-dev/q3a-botlib-nav-lookahead-steering-2026-06-18.md`, `docs-dev/q3a-botlib-nav-velocity-steering-2026-06-18.md`, `docs-dev/q3a-botlib-route-target-stabilization-2026-06-18.md`, `docs-dev/q3a-botlib-nav-stuck-repath-2026-06-18.md`, `docs-dev/q3a-botlib-nav-stuck-recovery-command-2026-06-18.md`, `docs-dev/q3a-botlib-nav-goal-blacklist-cooldown-2026-06-18.md`, `docs-dev/q3a-botlib-nav-failed-goal-reason-2026-06-18.md`, `docs-dev/q3a-botlib-nav-movement-state-commands-2026-06-18.md`, `docs-dev/q3a-botlib-bot-brain-command-ownership-2026-06-18.md`, `docs-dev/q3a-botlib-nav-position-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-ladder-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-walkoffledge-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-elevator-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-barrierjump-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-rocketjump-policy-2026-06-18.md`, `docs-dev/q3a-botlib-nav-four-bot-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-eight-bot-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-soak-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-map-change-repeat-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-map-restart-lifecycle-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-movement-door-retry-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-interaction-diagnostics-2026-06-18.md`, `docs-dev/q3a-botlib-perception-blackboard-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-dispatcher-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-brain-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-action-item-utility-2026-06-18.md`, `docs-dev/q3a-botlib-combat-weapon-metadata-2026-06-18.md`, `docs-dev/q3a-botlib-action-application-helpers-2026-06-18.md`, `docs-dev/q3a-botlib-nav-health-armor-focus-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-helper-scaffold-2026-06-18.md`, `docs-dev/q3a-botlib-smoke-scenario-modes-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-smoke-harness-2026-06-18.md`, `docs-dev/q3a-botlib-pending-scenario-counters-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-pending-gap-report-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotions-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-raw-reserved-diagnostics-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`, `docs-dev/q3a-botlib-source-counter-plumbing-2026-06-18.md`, `docs-dev/q3a-botlib-implementation-round-summary-2026-06-18.md`, `docs-dev/q3a-botlib-engage-enemy-proof-2026-06-18.md`, `docs-dev/q3a-botlib-weapon-switch-proof-2026-06-18.md`, `docs-dev/q3a-botlib-health-armor-pickup-proof-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-proof-2026-06-18.md`, `docs-dev/q3a-botlib-pending-scenario-promotion-tooling-2026-06-18.md`, `docs-dev/q3a-botlib-worker-i-status-2026-06-18.md`.
  - Additional latest implementation logs: `docs-dev/q3a-botlib-match-logging-schema-2026-06-21.md`, `docs-dev/q3a-botlib-match-logging-catalog-2026-06-21.md`, `docs-dev/q3a-botlib-competitive-server-tools-docs-2026-06-21.md`, `docs-dev/q3a-botlib-team-resource-denial-2026-06-21.md`, `docs-dev/q3a-botlib-reference-map-runtime-adapter-round-2026-06-21.md`, `docs-dev/q3a-botlib-runtime-entity-lifecycle-closeout-2026-06-21.md`, `docs-dev/q3a-botlib-entity-scheduling-fairness-closeout-2026-06-21.md`, `docs-dev/q3a-botlib-movement-recovery-inventory-closeout-2026-06-21.md`, `docs-dev/q3a-botlib-runtime-implementation-2026-06-21.md`, and `docs-dev/q2-aas-generator-implementation-2026-06-21.md`.
  - Current-wave status logs: `docs-dev/q3a-botlib-botfiles-q3a-style-expansion-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-scripts-package-coverage-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-scripts-support-2026-06-18.md`, `docs-dev/q3a-botlib-botfile-script-parity-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-worker-i-validation-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-tooling-source-aware-raw-diagnostics-2026-06-18.md`, `docs-dev/q3a-botlib-combat-damage-event-hook-2026-06-18.md`, `docs-dev/q3a-botlib-gameplay-item-hooks-2026-06-18.md`, `docs-dev/q3a-botlib-ctf-objective-gameplay-hooks-2026-06-18.md`, `docs-dev/q3a-botlib-health-armor-scenario-promotion-gate-2026-06-18.md`, `docs-dev/q3a-botlib-route-cpu-timing-2026-06-18.md`, `docs-dev/q3a-botlib-worker-n-status-2026-06-18.md`, and `docs-dev/q3a-botlib-worker-u-status-2026-06-18.md`.
  - Final current-wave helper logs: `docs-dev/q3a-botlib-weapon-inventory-dispatch-2026-06-18.md`, `docs-dev/q3a-botlib-aim-fairness-policy-2026-06-18.md`, `docs-dev/q3a-botlib-special-item-utility-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-depth-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`, and `docs-dev/q3a-botlib-status-harness-expansion-2026-06-18.md`.
  - Extensive closeout logs: `docs-dev/q3a-botlib-live-combat-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-live-item-timing-consumers-2026-06-18.md`, `docs-dev/q3a-botlib-team-coop-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-long-soak-source-counter-round-2026-06-18.md`, `docs-dev/q3a-botlib-profile-behavior-depth-round-2026-06-18.md`, `docs-dev/q2aas-reference-map-coverage-round-2026-06-18.md`, and `docs-dev/q3a-botlib-extensive-round-closeout-2026-06-18.md`.
- `FR-04-T10` / `DV-07-T06` In Progress:
  - Created the Q3A BotLib/Q2 AAS port plan, credits ledger, and first source audit.
  - Pinned the public baseline refs for `TTimo/bspc`, `bnoordhuis/bspc`, and the id Software Quake III Arena mirror.
  - Recorded that the local Q3A tree is not a Git checkout, so future imports must come from a commit-pinned source or an approved local snapshot manifest.
  - Added contributor attribution for the first BSPC/Q2 AAS generator candidates and made source provenance a hard import gate.
  - Implementation logs: `docs-dev/plans/q3a-botlib-aas-port.md`, `docs-dev/q3a-botlib-aas-credits.md`, `docs-dev/q3a-botlib-aas-source-audit-2026-06-16.md`.
- `FR-04-T09` Done:
  - Corrected deathmatch `INITIAL` spawn flag handling and kept initial-spawn
    intent aligned between the safe-spawn precheck and the actual spawn.
  - Routed team-mode spawns through the same safety filters and composite
    scoring used by FFA spawns, including last-death, mine/trap, nearest-player,
    and enemy-LOS checks.
  - Split solo/coop starts into a fallback-only spawn registry so ordinary
    deathmatch selection does not consume them unless real multiplayer spawn
    lists fail.
  - Kept relaxed fallback paths scored by heat, player proximity, LOS,
    last-death distance, and mine risk instead of reverting to random picks.
  - Hardened combat heatmap pruning, event capping, and danger normalization so
    spawn scoring receives a stable recent-combat signal.
  - Filtered heat writes to player-involved damage, tightened heat event origin
    fallback, and accelerated decay so stale combat zones stop biasing respawns.
  - Implementation logs: `docs-dev/sgame-spawn-selection-heatmap-hardening-2026-06-16.md`, `docs-dev/sgame-spawn-selection-heatmap-second-pass-2026-06-16.md`.
- `DV-04-T02` / `FR-03-T06` / `DV-07-T04` In Progress:
  - Added `cg_view.cpp` as the cgame-local home for first-person viewweapon pose logic and introduced archived `cg_weapon_bob` modes for disabled, Quake 3-style, and Doom 3-style weapon bobbing.
  - Promoted `cg_weapon_bob` as the snake_case primary cvar while keeping `cg_weaponBob` as a non-archived compatibility alias.
  - Fixed Quake 3 mode to use the active interpolated playerstate weapon pose instead of a parallel cgame-only bob cycle.
  - Wired the cgame Effects menu and client cvar docs to expose the new setting.
  - Implementation logs: `docs-dev/cgame-viewweapon-bob-options-2026-05-06.md`, `docs-dev/cgame-weapon-bob-cvar-snake-case-2026-06-12.md`, `docs-dev/cgame-weapon-bob-active-pose-fix-2026-06-12.md`.
- `FR-02-T09` / `FR-02-T10` / `FR-02-T11` / `DV-02-T06` / `DV-07-T05` Done:
  - Added a renderer-neutral shadow frontend contract (`shadow_light_desc_t`, `shadow_view_desc_t`, `shadow_caster_t`, `shadow_cache_key_t`, `shadow_page_id_t`, `shadow_backend_ops_t`) shared by GL, native Vulkan, and RTX builds.
  - Wired GL and native Vulkan frame paths into the shared frontend for deterministic candidate light selection, backend-resolved caster bounds, per-view caster index spans, light-influence cluster dirtying, page residency keys, dirty reasons, freeze modes, optional sun cascade descriptors, and main-view visibility mutation guardrails.
  - Implemented native OpenGL depth and moment array page allocation, per-layer shadow rendering, moment mip generation, `ShadowPages` UBO upload, and hard/PCF/PCSS/VSM/EVSM receiver sampling in the dynamic shader path.
  - Implemented native Vulkan depth and moment array page allocation, per-layer render pass/framebuffer setup, explicit depth/moment image barriers, optional moment mip generation, shadow descriptor binding, and world receiver sampling in the embedded Vulkan world shader.
    - Replaced interim caster-box rendering with actual brush, MD2/alias, and MD5 skeletal caster geometry for both non-RTX backends; GL keeps CPU shadow copies of uploaded model buffers, while Vulkan emits caster triangles through a native entity callback.
    - Fixed first-person entity caster visibility by marking the local body clone as `RF_CASTSHADOW`, excluding `RF_WEAPONMODEL` view weapons from the caster list, and emitting model-less `RF_CASTSHADOW` bounds proxies in both non-RTX backends.
    - Hardened transient visual no-cast behavior so particles, projectile models, and explosion models are marked or rejected before entering shared shadow caster collection.
    - Completed authored entity shadowlight handling for point and spot `light`/`dynamic_light` records across cgame, client, server setup, shared frontend area/PVS2 culling, wide-spot caster influence testing, and 64-bit receiver light masks.
    - Fitted sun cascades to camera frustum splits with texel-snapped light-space origins, and aligned Vulkan receiver normal-offset bias behavior with OpenGL.
  - Added focused shadow dumps, materialization reports, live debug overlays, model-path caster exclusion, tracked/configstring shadowlight metadata preservation, world-occluder view culling, and scripted repro smoke launch coverage.
  - Added `sv_shadow_strict_replication` for multiplayer servers that prefer strict normal-PVS shadow owner replication over the default PVS2 shadow relevance expansion.
  - Added a CI/source guardrail script that blocks the removed no-slot fallback and sticky slot-churn shadow cvars/paths from returning.
  - Completed the native Vulkan entity receiver follow-up so inline BSP models sample authored lightmaps and MD2/MD5 entities sample dynamic shadow pages through the Vulkan shadow descriptor set.
  - Stabilized dynamic effect dlight shadow residency for both OpenGL and Vulkan by preserving stable cdlight/entity/explosion keys through client submission and by removing moving dynamic-light projection/origin drift from shadow cache residency keys.
  - Restored first-person viewweapon shadow receiving in both OpenGL and native Vulkan so the hidden local-player body caster can shadow the held weapon without making `RF_WEAPONMODEL` entities cast shadows.
  - Implementation logs: `docs-dev/renderer/shadowmapping-replacement-baseline.md`, `docs-dev/renderer/shadowmapping-native-backends-2026-04-30.md`, `docs-dev/renderer/shadowmapping-full-plan-2026-04-30.md`, `docs-dev/renderer/vulkan-entity-lightmap-shadow-receiver-repair-2026-06-11.md`, `docs-dev/renderer/vulkan-viewweapon-dlight-glow-fixes-2026-06-12.md`, `docs-dev/renderer/viewweapon-shadow-receiver-2026-06-13.md`.
- `FR-03-T09` Done:
  - Added shared archived `r_borderless` tri-state window behavior for renderer/video backends (`0` exclusive where supported, `1` borderless fullscreen, `2` always borderless in windowed mode too).
  - Updated the Video and Multi-Monitor menu selectors to expose `r_borderless` instead of the legacy `r_fullscreen_exclusive` toggle, while keeping the legacy cvar as a no-archive runtime mirror.
  - Aligned the bootstrap session shell with `r_borderless` so startup window mode resolution matches the engine's renderer window policy.
  - Implementation log: `docs-dev/shared-borderless-cvar-2026-04-29.md`.
- `FR-04-T08` Done:
  - Added a Quake Champions-inspired top HUD for cgame multiplayer modes, covering FFA leader/chaser rows, team score panels, duel player panels, match timer, time limit, warmup/countdown, timeout, overtime, and intermission states.
  - Extended the sgame HUD blob with match metadata and optional scoreboard-row health/armor vitals so spectator duel panels can show player resources without changing legacy layout compatibility.
  - Refined the warmup timer to match the QC state/clock/timelimit stack, made FFA row selection mirror the existing minihud's top-two-or-viewed-player behavior, and serialized row rank/name data so top rows do not fall back to generic labels.
  - Fixed a screenshot-validation crash by draining renderer-owned async callbacks before external renderer shutdown/unload.
  - Implementation logs: `docs-dev/quake-champions-top-hud-2026-04-28.md`, `docs-dev/renderer-async-shutdown-drain-2026-04-29.md`.
- `DV-02-T02` In Progress:
  - Nightly Linux/macOS jobs now install explicit Vulkan toolchain dependencies so renderer artifacts do not depend on hosted-image luck.
  - Windows MSYS2 nightlies/releases now install Vulkan headers/loader plus `glslangValidator` so Vulkan/RTX renderer artifacts build in CI.
  - The macOS Intel target now uses the supported `macos-15-intel` runner label instead of the retired `macos-13` label.
  - Linux nightly dependency names were updated for current Ubuntu runner images (`libdecoration0-dev`, no `libsdl3-dev` requirement under Meson fallback).
  - Implementation log: `docs-dev/macos-nightly-vulkan-support-2026-03-16.md`.
  - Recovered run `23146195642` by fixing MSYS2/C++20 client compatibility issues (`min`/`max` typing, non-debug `developer` usage), the RTX debug symbol linkage mismatch, and the MinGW Unicode updater entry-point path.
  - Implementation log: `docs-dev/msys2-nightly-build-recovery-2026-03-16.md`.
  - Recovered run `23148759868` by fixing three cross-platform CI regressions:
    - renamed repository version metadata from `VERSION` to `WORR_VERSION` so macOS case-insensitive filesystems no longer shadow libc++ `<version>`
    - replaced non-portable `std::sinf` usage in `sgame` with standard `std::sin` overloads so GCC/Linux builds complete
    - fixed WiX MSI generation by removing self-referential preprocessor defines, adding the required Product language, and hard-failing on `heat`/`candle`/`light` native command errors
  - Implementation log: `docs-dev/nightly-ci-cross-platform-recovery-2026-03-16.md`.
  - Recovered run `23152001787` by fixing three follow-up CI regressions:
    - changed the WiX PowerShell wrapper to pass explicit native argument arrays so switches like `-out` are not consumed as PowerShell parameters
    - restored the Linux nightly OpenGL header dependency with `libgl1-mesa-dev`
    - replaced `std::from_chars(float)` with portable strict `std::strtof` parsing in `sgame` command and sky-rotation paths for macOS libc++
  - Implementation log: `docs-dev/nightly-run-23152001787-recovery-2026-03-16.md`.
  - Recovered run `23153597827` by fixing another three-platform nightly regression set:
    - emitted explicit x64 WiX harvest/compile metadata so Windows MSI validation no longer trips `ICE80`
    - updated RTX debug line-rasterization setup to the current Vulkan `EXT` symbols used by hosted Linux headers
    - made `sgame` save metadata/serialization handle `size_t` cleanly with JsonCpp-facing explicit widths for macOS builds
  - Implementation log: `docs-dev/nightly-run-23153597827-error-warning-recovery-2026-03-16.md`.
  - Recovered run `23156641291` by fixing the Unix release-pack staging collision with the root `worr` executable and restoring GCC-compatible `q_unused` placement in `rend_gl`.
  - Implementation log: `docs-dev/nightly-run-23156641291-recovery-2026-03-16.md`.
  - Recovered run `23159773990` / job `67284227517` by converting the remaining RTX line-rasterization negotiation path in `src/rend_rtx/vkpt/main.c` from `KHR` to `EXT`, matching the Vulkan headers used on current Linux CI runners.
  - Implementation log: `docs-dev/nightly-run-23159773990-recovery-2026-03-16.md`.
  - Recovered run `23162552895` / job `67293973244` by fixing GCC/Linux-incompatible temporary `VEC2`/`VEC3` client callsites, keeping the C-only `-Wmissing-prototypes` warning path out of C++ engine targets, and hardening Linux validation-path issues in BSP patched-PVS path construction, zone stats printing, MVD multicast leaf handling, and OpenAL loader loop indexing.
  - Implementation log: `docs-dev/nightly-run-23162552895-linux-safety-recovery-2026-03-16.md`.
- `DV-04-T03` In Progress:
  - Recovered run `23153597827` by fixing three additional cross-platform failures:
    - emitted explicit x64 WiX harvest/compile metadata so Windows MSI validation no longer trips `ICE80`
    - updated RTX debug line-rasterization setup to the current Vulkan `EXT` symbols used by hosted Linux headers
    - made `sgame` save metadata/serialization handle `size_t` cleanly with JsonCpp-facing explicit widths for macOS builds
  - Removed first-party warning categories surfaced by the same run across `client`, `cgame`, `sgame`, `rend_gl`, and `rend_vk`, and reduced fallback dependency warning noise by quieting forced-fallback third-party builds and disabling HarfBuzz subproject tests in CI configure steps.
  - Aligned project-wide linker arguments for both C and C++ Meson targets so local Windows validation of the client executable uses the same linker configuration as the other binaries.
  - Implementation log: `docs-dev/nightly-run-23153597827-error-warning-recovery-2026-03-16.md`.
  - Recovered additional warning noise from run `23156641291` by removing `entity_iterable_t` constructor template-id syntax in `sgame` and extending quiet fallback warning suppression for third-party fallback builds.
  - Implementation log: `docs-dev/nightly-run-23156641291-recovery-2026-03-16.md`.
- `DV-05-T03` In Progress:
  - Added OpenGL renderer baseline instrumentation for GPU-offload work: opt-in CPU scope timers, delayed GPU timer queries, `KHR_debug` phase groups, stable per-frame telemetry, streamed buffer byte counters, texture-upload byte counters, and fast-path flags for shader/GPU-lerp/static-world-VBO/per-pixel-lighting selection.
  - Implementation log: `docs-dev/renderer/opengl-gpu-offload-instrumentation-2026-05-04.md`.
- `FR-02-T08` Done:
  - Hardened the OpenGL `q2dm1` launch path so unsupported postfx framebuffer combinations retry safer bloom/DOF/depth variants instead of failing the whole postfx chain on `GL_FRAMEBUFFER_UNSUPPORTED`.
  - Removed two false-positive startup warnings from the same smoke path by scoping map-fix validation to entities that actually need model data and suppressing `PF_Client_Print` free/zombie noise during `ss_loading`.
  - Added a client-side guard so sound-only frames do not call `CL_CalcViewValues` before cgame entity extensions are available.
  - Implementation log: `docs-dev/renderer-startup-log-cleanup-2026-03-27.md`.
- `FR-03-T08` In Progress:
  - Tightened multiplayer menu routing so the match menu is only selected during an active multiplayer game session, instead of any `cl.maxclients > 1` state.
  - Split the session-only menu definitions (`dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, `dm_matchinfo`) out of `src/game/cgame/ui/worr.json` into a dedicated embedded `src/game/cgame/ui/worr-multiplayer.json` asset loaded by cgame UI init.
  - Added a cgame-side session helper exposed through the UI access boundary so the menu module no longer depends on broad engine-state assumptions for multiplayer routing.
  - Implementation log: `docs-dev/match-menu-session-split-2026-03-23.md`.
  - Converted the multiplayer `MyMap` entry into a dedicated submenu flow with explicit availability/status messaging, preserved flag state across navigation, and successful queue cleanup/close behavior.
  - Implementation log: `docs-dev/match-menu-mymap-submenu-2026-03-23.md`.
- `FR-02-T07` Done:
  - SDL video backend now creates Vulkan-capable windows for `r_renderer vulkan`/`rtx` instead of always forcing an OpenGL context.
  - Native Vulkan renderer now uses SDL Vulkan instance/surface helpers and enables portability enumeration/subset support required by MoltenVK-backed macOS devices.
  - Implementation log: `docs-dev/macos-nightly-vulkan-support-2026-03-16.md`.
- `FR-02-T03` Done:
  - User-facing launch/debug presets stay on `worr_x86_64(.exe)` and `worr_ded_x86_64(.exe)`, which are now bootstrap hosts that load `worr_engine_*` and `worr_ded_engine_*` in-process.
  - The short-lived explicit `worr_launcher_*` split was removed so the published binary names match the actual user launch path again.
  - Implementation logs: `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`, `docs-dev/vscode-bootstrap-debug-presets-2026-03-24.md`.
- `DV-08-T05` Done:
  - Nightly/stable staging now packages the canonical repo `assets/` tree directly into `.install/basew/pak0.pkz`.
  - Loose staged asset duplication between `assets/` and `.install/` was removed so runtime assets now have a single authored source and a single packaged staging form.
  - Client/server release archives now use explicit payload filters instead of packaging identical full `.install/` trees.
  - Artifact verification now validates manifest contents so role-specific payload regressions are caught before publish.
  - Stable release packaging now reuses the same platform-packaging path as nightlies.
  - Implementation log: `docs-dev/basew-gamedir-and-arch-runtime-layout-2026-03-16.md`.
- `DV-08-T06` Done:
  - Local staging, release archives, and the Windows MSI now use a single `basew/` gamedir instead of the earlier `baseq2/` + `worr/` split.
  - Release CI now builds with `-Dbase-game=basew -Ddefault-game=basew`, and gameplay/runtime defaults now resolve the WORR payload through `basew`.
  - `WORR_VERSION` remains `0.1.0`, and the stable release workflow still publishes cross-platform GitHub releases from that semver source of truth.
  - Implementation log: `docs-dev/basew-gamedir-and-arch-runtime-layout-2026-03-16.md`.
- `DV-08-T07` Done:
  - User-facing bootstrap executables now ship with explicit arch suffixes such as `worr_x86_64(.exe)`, `worr_ded_x86_64(.exe)`, and `worr_updater_x86_64.exe`.
  - Hosted engine libraries now follow the same arch-suffixed rule (`worr_engine_x86_64`, `worr_ded_engine_x86_64`), while game DLLs continue using `cgame_x86_64` and `sgame_x86_64`.
  - Release manifests and updater metadata now publish the launcher/engine pairing through `launch_exe` and `engine_library`.
  - Implementation logs: `docs-dev/basew-gamedir-and-arch-runtime-layout-2026-03-16.md`, `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`.
- `DV-08-T08` Done:
  - Nightly GitHub releases now publish as standard releases instead of GitHub prereleases.
  - Updater release discovery now filters the full GitHub release list by channel/tag and `allow_prerelease` policy, so stable installs do not drift onto nightly releases after that publishing change.
  - Nightly packaging no longer emits updater configs with `allow_prerelease=true`.
  - Implementation log: `docs-dev/nightly-release-non-prerelease-channel-selection-2026-03-16.md`.
- `FR-08-T04` Done:
  - Release metadata now publishes an explicit role-level updater contract through the release index instead of inferring package names inside the updater.
  - `worr_update.json` now points at a channel-specific release index asset, and updater discovery resolves role payloads from that index before fetching the remote manifest.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-03-T06` Done:
  - Added release-tool SemVer ordering tests covering stable, prerelease, and nightly version strings.
  - Added release-index parser tests for missing role metadata and malformed payload fields.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-08-T01` Done:
  - Added release-index parser fixtures for missing role payloads and missing required updater metadata.
  - Added target-contract tests that keep full-install updater payload coverage separate from split manual archive coverage.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-08-T03` In Progress:
  - The new bootstrap apply worker now verifies staged file hashes, writes the local install manifest last, and restores backed-up files on failure.
  - Local build/package validation passed, but explicit live fault-injection coverage for failed extraction/apply paths is still pending.
  - Implementation log: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`.
- `DV-08-T09` Done:
  - Desktop updater startup remains bootstrap-based on Windows, Linux, and macOS, with the client using a branded splash and the dedicated server using a console-first updater flow.
  - Normal startup now stays inside the user-facing bootstrap executables, which host the engine shared libraries in-process; the temp updater worker is reserved for approved file-replacement/update paths and relaunches the public bootstrap after a successful update.
  - Update prompts can now be deferred without forcing an exit, and `autolaunch` is respected after worker-applied installs.
  - Implementation logs: `docs-dev/desktop-bootstrap-updater-2026-03-23.md`, `docs-dev/bootstrap-hosted-engine-libraries-2026-03-24.md`.
- `DV-08-T11` Done:
  - Re-audited the updater pipeline against packaged artifacts, role-scoped staged installs, and local pending-update payloads.
  - Kept install-root normalization and real apply-time permission handling hardening in the bootstrap worker, added trace instrumentation plus a native Windows dedicated-server temp-worker/relaunch handoff path, closed the local Windows public-bootstrap approved-update gap, and added `tools/release/server_bootstrap_update_smoke.py` for deterministic local validation.
  - Implementation log: `docs-dev/updater-pipeline-audit-2026-03-25.md`.
- `DV-08-T12` In Progress:
  - Analyzed the local Steam-distributed Quake Champions client install and confirmed that Steam owns build/depot updates while the game behaves like a branded session shell with embedded web/session components.
  - Refactored the desktop bootstrapper toward a session-shell model by introducing explicit install-sync planning, enabling same-version repair/synchronization decisions, and constraining removals to managed files tracked by the local install manifest.
  - The worker/apply path can now complete metadata-only syncs without downloading a package, and local validation repaired a deliberately missing dedicated engine DLL through the public bootstrap update flow.
  - Added a Windows client shared-window handoff slice so the hosted engine can adopt the bootstrap-owned splash window instead of creating a second native client window on that path.
  - Added an in-process client sync/apply path so approved client synchronization can stay in the launcher, restore managed files, and enter the hosted engine in the same bootstrap-owned window when the running bootstrap executable itself is not being replaced.
  - Replaced the old fixed-size placeholder splash with a display-profile-driven SDL3 session shell that resolves `r_geometry`, `r_fullscreen`, `r_borderless`, legacy `r_fullscreen_exclusive`, `r_monitor_mode`, `r_display`, `autoexec.cfg`, and forwarded `+set` overrides before creating the client window.
  - The bootstrap now creates the client shell through SDL3's hidden property-based window path, applies the real fullscreen/window mode before showing it, and logs the resolved session-shell mode for validation.
  - Extended `tools/release/client_bootstrap_sync_smoke.py` so local validation can assert both the in-process repair/sync handoff and the expected bootstrap session-shell window mode.
  - The bootstrap splash now renders text through SDL3_ttf first, uses readable 12-16 px legal fine print, shortens the no-update splash dwell, disables Windows shared-HWND handoff so Win11 thumbnails/PrintScreen sample the renderer-owned engine window, keeps the transient splash out of the taskbar preview surface, routes fullscreen through `r_borderless 1` by default, clears the renderer-owned backbuffer under non-transparent menus, and consumes the startup transition marker as a one-shot so stale bootstrap frames cannot reappear behind the main menu.
  - Implementation logs: `docs-dev/client-bootstrap-session-shell-architecture-2026-03-25.md`, `docs-dev/bootstrap-session-shell-sync-refactor-2026-03-25.md`, `docs-dev/bootstrap-windows-client-shared-window-adoption-2026-03-25.md`, `docs-dev/bootstrap-client-in-process-sync-handoff-2026-03-25.md`, `docs-dev/bootstrap-session-shell-display-profile-window-creation-2026-03-25.md`, `docs-dev/ui-bootstrap-font-handoff-2026-04-27.md`, `docs-dev/shared-borderless-cvar-2026-04-29.md`.
- `DV-08-T10` Done:
  - Repaired the tracked vendored libcurl wrap patch so bootstrap-enabled local Windows builds now succeed against the `curl-8.18.0` fallback instead of failing on stale 8.15-era source paths.
  - Fixed the bootstrap updater's Windows `min`/`max` macro collision so the launcher/worker binaries compile cleanly with the vendored fallback enabled.
  - Implementation log: `docs-dev/libcurl-wrap-bootstrap-build-fix-2026-03-23.md`.
- `FR-01-T04` In Progress:
  - Completed Vulkan MD5 parity follow-up for frame resolve semantics and MD2/MD5 opaque-vs-alpha routing in `src/rend_vk/vk_entity.c`.
  - Implementation log: `docs-dev/vulkan-md5-mesh-frame-alpha-parity-fix-2026-02-27.md`.
  - Completed Vulkan MD5 `.md5scale` + scale-position parity and MD5 skin-routing correction in `src/rend_vk/vk_entity.c`.
  - Implementation log: `docs-dev/vulkan-md5-mesh-parity-revision-2026-02-27.md`.
  - Fixed Vulkan sky re-registration crash on same-map reload (`VK_World_SetSky` handle invalidation ordering) in `src/rend_vk/vk_world.c`.
  - Implementation log: `docs-dev/vulkan-sky-reregister-crash-fix-2026-02-27.md`.
  - Aligned Vulkan MD2 mesh decode topology handling with RTX/GL remap behavior in `src/rend_vk/vk_entity.c`:
    - Skip invalid triangles instead of hard-failing the whole model.
    - Remap/deduplicate MD2 vertices by `(index_xyz, st.s, st.t)` and emit compact index/vertex streams.
    - Added MD2 header/frame-size/skin-dimension bounds checks matching RTX-style validation.
  - Implementation log: `docs-dev/vulkan-md2-mesh-remap-parity-fix-2026-02-27.md`.
  - Added Vulkan entity receiver lighting for MD2/MD5 and inline BSP models:
    - MD2 now interpolates imported frame normals for dynamic-light/shadow receiver evaluation.
    - MD5 now emits world-space per-triangle receiver normals while a smoother skinned-normal reconstruction remains a follow-up.
    - Inline BSP models now sample authored Vulkan lightmaps while remaining excluded from the static world mesh.
  - Implementation log: `docs-dev/renderer/vulkan-entity-lightmap-shadow-receiver-repair-2026-06-11.md`.
  - Fixed native Vulkan view weapon rendering by splitting depthhack entity rendering into opaque and alpha pipelines with a compressed near depth range, and restored the classic `RF_GLOW` item pulse in the Vulkan entity light path.
  - Implementation log: `docs-dev/renderer/vulkan-viewweapon-dlight-glow-fixes-2026-06-12.md`.
- `FR-06-T01` In Progress:
  - Fixed OpenAL loop-merge channel reuse so merged loops cannot reuse
o_merge` Doppler channels in `src/client/sound/al.cpp`.
  - This preserves projectile world-origin tracking for Doppler-marked loop sounds when mixed with non-Doppler loops using the same sample.
  - Implementation log: `docs-dev/audio-projectile-doppler-origin-merge-fix-2026-02-27.md`.
  - Fixed q2proto entity delta application so loop extension fields are applied in `src/client/parse.cpp`:
    - `Q2P_ESD_LOOP_VOLUME`
    - `Q2P_ESD_LOOP_ATTENUATION`
  - This prevents stale `loop_attenuation` states (for example `ATTN_LOOP_NONE`) from producing level-wide/full-volume projectile loops after entity reuse.
  - Implementation log: `docs-dev/audio-projectile-loop-attenuation-parse-fix-2026-02-27.md`.
  - Fixed OpenAL EAX spatial routing consistency in `src/client/sound/al.cpp`:
    - Non-merged channels now run the same per-source spatial effect update path used by merged loops (direct filter, air absorption, and auxiliary send updates).
    - EAX zone selection now uses uncapped nearest-zone matching (`FLT_MAX` + squared-distance checks), eliminating the hard 8192-unit selection ceiling and reducing unnecessary LOS traces.
    - EAX effect application now clears stale AL error state before property updates for reliable success/failure reporting.
  - Implementation log: `docs-dev/audio-eax-spatial-awareness-fixes-2026-02-27.md`.
  - Closed remaining EAX spatial-awareness gaps:
    - Restored real occlusion behavior in `src/client/sound/main.cpp` (`S_ComputeOcclusion`, `S_GetOcclusion`, `S_SmoothOcclusion`, `S_MapOcclusion`) with multi-ray tracing, material/transparency weighting, query rate limiting, and smoothing.
    - Replaced center-only LOS zone validation with multi-probe reachability checks in `src/client/sound/al.cpp` to reduce false zone misses in occluded/non-convex spaces.
  - Implementation log: `docs-dev/audio-eax-spatial-gap-closure-2026-02-27.md`.
  - Fixed excessive projectile loop attenuation fallback in shared client sound:
    - Added `S_GetEntityLoopDistMult(const entity_state_t *ent)` in `src/client/sound/main.cpp`.
    - For projectile-like/doppler loops with unset `loop_attenuation` (`== 0`), fallback now uses `ATTN_NORM` before conversion to distance multiplier.
    - This applies to both OpenAL and DMA loop paths through the shared `S_GetEntityLoopDistMult(...)` call sites.
  - Implementation log: `docs-dev/audio-projectile-loop-attenuation-fallback-fix-2026-02-27.md`.
  - Stabilized dense OpenAL Doppler loop mixes in `src/client/sound/al.cpp` for Issue #761:
    - Doppler-preserved same-sample loop groups now apply `1 / sqrt(count)` gain normalization instead of stacking full gain linearly.
    - Unmerged projectile/autosound loops now use a stable per-entity phase offset so identical loop samples do not all start in lockstep.
    - This reduces crackle/noise when many projectile loop emitters are active simultaneously while preserving per-entity Doppler spatialization.
  - Implementation log: `docs-dev/audio-eax-loop-doppler-mix-stability-2026-03-22.md`.
  - Fixed clear-path explosion and held hand grenade tick regressions:
    - OpenAL source-path damping now skips occlusion floors/HF ceilings when the direct multi-ray path is still inside `S_OCCLUSION_CLEAR_MARGIN`, so visible midair explosions are not muffled by room-path classification alone.
    - Held throwable loop setup now mirrors primed sounds onto the player entity loop state immediately, and hand grenades emit a first tick cue while the loop carries the continuing fuse sound for the owner and nearby players.
  - Implementation log: `docs-dev/audio-clear-path-explosions-and-grenade-tick-2026-04-27.md`.
- `FR-06-T06` Done:
  - Implemented the first spatial-audio roadmap wave from `docs-dev/proposals/spatial-audio.md`:
    - Defaulted occlusion, EFX reverb, per-source reverb sends, air absorption, and HRTF default/autodetect toward the modern spatial path.
    - Decoupled OpenAL auxiliary reverb sends from `al_eax` so `al_reverb` is the routing master and authored EAX zones are optional overrides.
    - Reconnected automatic BSP reverb environment selection and added a compiled-in fallback table for missing `sound/default.environments`.
  - Implementation log: `docs-dev/spatial-audio-first-wave-consolidation-2026-04-27.md`.
- `FR-06-T07` Done:
  - Implemented the second spatial-audio roadmap wave from `docs-dev/proposals/spatial-audio.md`:
    - Replaced the direct substring occlusion classifier with a `.mat` ID keyed acoustic material resolver.
    - Converted the existing material families into low/mid/high transmission, absorption, scattering, and semantic flag profiles.
    - Routed resolved acoustic coefficients through direct gain, per-source HF filtering, and material-coloured reverb sends for OpenAL; kept DMA loop/channel state coherent with the shared resolver.
  - Implementation log: `docs-dev/spatial-audio-acoustic-material-resolver-2026-04-27.md`.
- `FR-06-T08` Done:
  - Implemented the third spatial-audio roadmap wave from `docs-dev/proposals/spatial-audio.md`:
    - Added an OpenAL BSP acoustic region cache keyed by BSP area, built from leaf bounds, leaf faces, `.mat` material groups, sky exposure, and areaportal neighbours.
    - Replaced floor-first automatic reverb preset selection with a weighted listener-space resolver using region material composition, live dimension probes, sky ratio, vertical openness, portal openness, and floor material as a secondary signal.
    - Added source/listener region classification to per-source and merged-loop reverb sends so interior/exterior and cross-area sources get region-aware send gain and HF colour.
  - Implementation log: `docs-dev/spatial-audio-bsp-acoustic-regions-2026-04-27.md`.
- `FR-06-T09` Done:
  - Implemented the fourth spatial-audio roadmap wave from `docs-dev/proposals/spatial-audio.md`:
    - Added a lightweight OpenAL portal propagation fallback that searches one- and two-hop BSP areaportal neighbour routes after direct multi-ray occlusion.
    - Evaluates route distance, bend penalty, aperture/openness penalty, and acoustic material transmission to estimate indirect direct-path audibility.
    - Applies valid portal routes to reduce over-occlusion, raise direct HF cutoff where appropriate, and colour/boost reverb sends for sources heard through neighbouring spaces.
  - Implementation log: `docs-dev/spatial-audio-portal-propagation-2026-04-27.md`.
- `FR-06-T10` Done:
  - Implemented the fifth spatial-audio roadmap wave from `docs-dev/proposals/spatial-audio.md`:
    - Reworked OpenAL source routing around an explicit two-identity source path state: listener room identity remains the global EFX slot, while each source resolves its own source room and path class.
    - Added same-space, adjacent-space, cross-space, portal, exterior-to-interior, interior-to-exterior, and unreachable source path classes.
    - Applied path classes consistently to direct attenuation/HF limits when occlusion is enabled and to per-source reverb send gain/HF colour for normal and merged looping sources.
  - Implementation log: `docs-dev/spatial-audio-two-identity-source-paths-2026-04-27.md`.
- `FR-06-T11` Done:
  - Implemented the sixth spatial-audio roadmap wave from `docs-dev/proposals/spatial-audio.md`:
    - Preserved authored `client_env_sound` / `env_sound` overrides and continued loading EAX JSON effect profiles, with named authored-zone profile keys added alongside numeric `reverb_effect_id`.
    - Added optional `.aud` sidecars from `maps/<mapname>.aud` or `sound/acoustics/<mapname>.aud` for authored region, portal/opening, and EAX zone refinements.
    - Routed valid sidecar portal hints through the existing one- and two-hop BSP source path resolver so authored data refines route aperture, transmission, and HF colour without replacing automatic spatial audio.
  - Implementation log: `docs-dev/spatial-audio-authored-sidecar-overrides-2026-04-27.md`.
- `FR-06-T03` Completed:
  - Hardened the SDL3_ttf/HarfBuzz text path in `src/client/font.cpp` so failed `TTF_CreateText(...)` or `TTF_GetStringSize(...)` calls no longer silently drop render/measure segments.
  - Updated TTF glyph cache generation to keep HarfBuzz-shaped glyphs renderable when metrics lookup fails but glyph image extraction succeeds (`TTF_GetGlyphImageForIndex(...)`).
  - Added SDL3_ttf surface text-engine startup validation so TTF mode only stays active when both library init and text engine creation succeed.
  - Finalized accessibility defaults and fallback controls:
    - Consolidated high-visibility text under archived `ui_high_visibility_text 1` so black text backgrounds and related accessibility text behavior have one authoritative cvar.
    - Added archived `ui_text_typeface 2` as the default TrueType selector, with legacy and KEX/kfont options available from settings.
    - Added archived fallback font cvars (`cl_font_fallback_kfont`, `cl_font_fallback_legacy`) so fallback chains remain configurable without code edits.
  - Repaired the console/UI/screen font chain so fixed-width TTF fonts render through a direct per-codepoint TTF path again, and readable client fallbacks now use `fonts/qconfont.kfont` instead of `fonts/qfont.kfont`.
  - Implementation log: `docs-dev/ttf-sdl3-harfbuzz-render-path-hardening-2026-03-27.md`.
  - Implementation log: `docs-dev/fr-06-t03-accessibility-defaults-and-fallback-controls-2026-03-27.md`.
  - Implementation log: `docs-dev/console-font-ttf-kfont-fallback-repair-2026-03-28.md`.
  - Implementation log: `docs-dev/font-ttf-kexfont-alignment-2026-04-27.md`.
  - Implementation log: `docs-dev/font-ttf-test-screen-visual-alignment-2026-04-27.md`.
  - Implementation log: `docs-dev/font-horizontal-alignment-and-menu-footer-2026-04-27.md`.
  - Extended the TTF-first policy to the actual cgame in-game weapon bar and the bootstrapper splash/legal footer text; client font loading now falls back to platform TTFs when staged project font files are unavailable, with TTF menu measurement kept on the renderer glyph-advance path for stable center/right alignment.
  - Expanded high-visibility black text backgrounds from centerprint-specific contrast bars to shared HUD/menu font wrappers under the single `ui_high_visibility_text` cvar. Added Options -> Accessibility controls and `ui_text_typeface` (`legacy`, `KEX`, `TrueType`, default TrueType), with high-visibility text forcing the effective typeface to TrueType.
  - Implementation log: `docs-dev/ui-bootstrap-font-handoff-2026-04-27.md`.
  - Stabilized fullscreen/resizable TTF handling so framebuffer size changes refresh font raster pixel height without changing the assigned font kind, expanded shared font-generation invalidation to console and cached weapon-bar paths, and fixed multiline screen/cgame TTF row stepping to use real font line heights.
  - Fixed a HUD font-role mix-up in the cgame SCR bridge so HUD helpers no longer swap between different TTF families (`scr.font` vs. the readable screen/UI font path) as layout or mode paths change.
  - Fixed cgame menu font bootstrap so JSON-selected menu fonts bind immediately through canonical `cl_font` resolution instead of lingering on the startup default until a fullscreen/windowed renderer restart.
  - Implementation log: `docs-dev/ttf-fullscreen-font-pixel-scale-refresh-2026-04-28.md`.

## Baseline Snapshot (Repository-Derived)
- Codebase scale is substantial: approximately 733 `*.c`/`*.cpp`/`*.h`/`*.hpp` files and approximately 426k lines across `src/` and `inc/`.
- Workload concentration is heavily in gameplay and rendering:
  - `src/game/sgame`: 259 files, approximately 128k lines
  - `src/rend_gl`: 39 files, approximately 49k lines
  - `src/client`: 48 files, approximately 41k lines
  - `src/rend_rtx`: 65 files, approximately 38k lines
  - `src/rend_vk`: 15 files, approximately 26k lines
- cgame UI is already a large system:
  - `src/game/cgame/ui/worr.json`: 1869 lines
  - `src/game/cgame/ui/ui_widgets.cpp`: 114k+ bytes
- Build and release automation is mature:
  - Local staging standard: `.install/` via `tools/refresh_install.py`
  - Automated release paths: `.github/workflows/nightly.yml`, `.github/workflows/release.yml`
  - Platform packaging and metadata tooling under `tools/release/`
- Protocol compatibility has dedicated boundaries and tests:
  - `q2proto/` integrated as read-only in policy
  - `q2proto/tests/` contains protocol flavor build tests
- Existing technical debt is visible:
  - 200+ `TODO/FIXME/HACK/XXX` markers in first-party `src/` and `inc/` (excluding legacy and third-party trees)
  - Bots have an active Q3A BotLib/AAS-backed fake-client command path with initial movement-state button intent, natural jump/ladder/barrier-jump validation, route-only walk-off-ledge/elevator validation, and default-off rocket-jump route gating, but higher-level perception, combat, item utility, actual rocket-jump execution, remaining natural crouch/swim/waterjump movement-state validation, and door/trigger recovery remain incomplete.
  - Local TODOs still include major items (`TODO.md`)
- Documentation volume is high and active:
  - `docs-dev/` carries extensive subsystem writeups (renderer, cgame, font, build, release)
  - Some docs show drift against current code paths, signaling curation needs

## SWOT

## Strengths
- Broad gameplay foundation already exists in `sgame`, with many game modes and systems wired (`src/game/sgame/g_local.hpp`, `src/game/sgame/gameplay/*`, `src/game/sgame/match/*`).
- Strong multi-renderer strategy is already implemented: OpenGL (`src/rend_gl`), native Vulkan raster (`src/rend_vk`), and RTX/vkpt (`src/rend_rtx`).
- Release and staging discipline is materially stronger than typical forks (`tools/refresh_install.py`, `tools/release/*`, nightly workflow automation).
- cgame and JSON UI architecture is robust and extensible (`src/game/cgame/*`, `src/game/cgame/ui/*`).
- Documentation culture is real and ongoing (`docs-dev/` has high-frequency change logs and design analyses).
- Clear policy boundaries already exist for critical risks:
  - no Vulkan-to-OpenGL fallback policy
  - q2proto compatibility guardrails

## Weaknesses
- Scope breadth is high enough to fragment focus without enforced project governance.
- Automated quality gates are underdeveloped for core engine/gameplay paths (CI is release-centric, not full PR validation-centric).
- Vulkan parity is still incomplete in multiple high-visibility areas (particle styles, beam styles, flare behavior, post-process parity).
- Bot behavior is not yet production-capable despite plumbing being present.
- Technical debt markers are spread across gameplay, client, renderer, and server paths.
- Cvar namespace modernization is only partially applied (`g_` still dominates many new sgame controls despite `sg_` preference).
- Dependency lifecycle complexity is high (multiple vendored versions of several libraries under `subprojects/`).
- Documentation freshness is uneven (some architecture docs lag current filenames/wiring).

## Opportunities
- Differentiate WORR through native Vulkan parity plus predictable performance improvements.
- Leverage already-rich game mode set into a clear competitive and cooperative offering roadmap.
- Exploit nightly + updater tooling to move toward rapid, measurable, low-friction iteration.
- Convert documentation volume into execution strength by binding work to project IDs and status workflows.
- Add targeted automated tests and smoke harnesses to reduce regression risk as refactors continue.
- Complete C++ migration with module boundaries that reduce long-term maintenance cost.
- Consolidate dependency versions and improve reproducibility/security posture.
- Use analytics/observability for performance baselines and release quality gates.

## Threats
- Regression risk is high due to surface area and currently limited automated gameplay/renderer test coverage.
- Cross-platform support can drift if changes are validated mainly on one host/toolchain.
- Feature creep can outrun finishing work unless work-in-progress limits are enforced.
- Upstream divergence from Q2REPRO, KEX, and reference idTech3 patterns can increase integration cost over time.
- Team coordination cost will rise with parallel renderer/gameplay/UI tracks unless roadmap ownership is explicit.
- Dependency sprawl increases update effort and potential security exposure windows.
- Documentation debt can lead to incorrect decisions when implementation and docs disagree.
- Public expectations around compatibility and re-release parity can be missed without milestone-level acceptance criteria.

## Project Backbone Model (Mandatory Operating Approach)

## Portfolio Structure
- Portfolio: `WORR 2026 Execution Portfolio`
- Projects:
  - `P-FEATURE`: player/admin-visible outcomes
  - `P-DEVELOPMENT`: engineering quality, architecture, and delivery capability

## Task Metadata Schema
Each task must include:
- `ID`: stable identifier (`FR-xx-Tyy` or `DV-xx-Tyy`)
- `Epic`: roadmap epic ID
- `Area`: subsystem (`rend_vk`, `cgame`, `sgame`, `tools/release`, etc.)
- `Priority`: `P0`, `P1`, `P2`
- `Dependencies`: IDs that must be completed first
- `Definition of Done`: explicit acceptance criteria

## Workflow States
- `Backlog`
- `Ready`
- `In Progress`
- `In Review`
- `Blocked`
- `Done`

## Cadence
- Weekly: backlog grooming and dependency resolution
- Biweekly: milestone review against exit criteria
- Per release train: roadmap delta review and reprioritization

## Definition of Ready
- Scope and subsystem boundaries are explicit.
- Dependencies are known and linked.
- Validation strategy is defined (build/test/runtime checks).

## Definition of Done
- Code merged and documented.
- Staging/packaging impact validated if applicable.
- Corresponding roadmap task marked complete.

## Feature Roadmap (Task-Based Project)

## Timeline
- Phase F1 (2026-03-01 to 2026-04-30): parity blockers and UI completion groundwork
- Phase F2 (2026-05-01 to 2026-08-31): major gameplay and renderer differentiation
- Phase F3 (2026-09-01 to 2026-12-31): feature hardening, polish, and release readiness

## Epic FR-01: Native Vulkan Gameplay Parity
Objective: close gameplay-visible parity gaps versus OpenGL while preserving native Vulkan policy.

Primary Areas: `src/rend_vk/*`, `src/client/renderer.cpp`, `docs-dev/vulkan-*.md`

Exit Criteria:
- Vulkan supports all essential gameplay rendering paths used in core multiplayer and campaign flows.
- Known parity blockers from Vulkan audits are closed or explicitly deferred with owner/date.

Tasks:
- [ ] `FR-01-T01` Implement Vulkan equivalents for particle style controls (`gl_partstyle` parity map to `vk_/r_` cvars).
  Dependency: none. Priority: P0.
- [ ] `FR-01-T02` Implement Vulkan beam style parity (`gl_beamstyle` behavior equivalents).
  Dependency: `FR-01-T01`. Priority: P0.
- [ ] `FR-01-T03` Add `RF_FLARE` behavior parity in Vulkan entity path.
  Dependency: none. Priority: P1.
- [ ] `FR-01-T04` Complete MD2 and MD5 visual parity pass with map-driven validation scenes.
  Dependency: none. Priority: P0.
  Progress: Native Vulkan now renders MD2/MD5 entity receivers with dynamic shadows, keeps MD5 skin selection aligned with GL, and fixes first-person view weapon depthhack rendering with separate opaque/alpha depthhack pipelines. `RF_GLOW` item pulse parity is restored in the Vulkan entity light path.
  Implementation logs: `docs-dev/renderer/vulkan-entity-lightmap-shadow-receiver-repair-2026-06-11.md`, `docs-dev/renderer/vulkan-viewweapon-dlight-glow-fixes-2026-06-12.md`.
- [ ] `FR-01-T05` Resolve remaining sky seam/artifact issues for all six faces and transitions.
  Dependency: none. Priority: P0.
- [ ] `FR-01-T06` Finalize bmodel initial-state correctness on first render frame.
  Dependency: `FR-01-T04`. Priority: P0.
- [ ] `FR-01-T07` Add Vulkan parity checklist doc and per-feature status table in `docs-dev/renderer/`.
  Dependency: `FR-01-T01..T06`. Priority: P1.
- [ ] `FR-01-T08` Add Vulkan runtime debug overlays/counters for missing-feature detection.
  Dependency: none. Priority: P1.

## Epic FR-02: Renderer Role Clarity (OpenGL vs Vulkan vs RTX)
Objective: ensure each renderer has a clearly defined role and quality target.

Primary Areas: `meson.build`, `src/client/renderer.cpp`, `src/rend_vk/*`, `src/rend_rtx/*`

Exit Criteria:
- Renderer selection behavior is explicit and documented.
- Vulkan raster and RTX path-tracing are clearly differentiated in functionality and messaging.

Tasks:
- [ ] `FR-02-T01` Produce renderer capability matrix (`opengl`, `vulkan`, `rtx`) and include cvar mapping.
  Dependency: none. Priority: P0.
- [ ] `FR-02-T02` Add runtime command to dump active renderer capabilities to log/console.
  Dependency: `FR-02-T01`. Priority: P1.
- [x] `FR-02-T03` Align launch/debug presets with current renderer names and expected modes.
  Dependency: none. Priority: P1.
- [ ] `FR-02-T04` Validate and document fallback/error behavior for missing renderer DLLs.
  Dependency: none. Priority: P1.
- [ ] `FR-02-T05` Add parity smoke map sequence for each renderer in nightly validation.
  Dependency: `DV-02-T03`. Priority: P1.
- [ ] `FR-02-T06` Publish renderer support policy page under `docs-user/` for end users.
  Dependency: `FR-02-T01`. Priority: P2.
- [x] `FR-02-T07` Add SDL/MoltenVK Vulkan window/surface support for macOS and other SDL-backed platforms.
  Dependency: none. Priority: P0.
- [x] `FR-02-T08` Harden OpenGL startup fallback and clean local `q2dm1` launch log noise.
  Dependency: none. Priority: P1.
- [x] `FR-02-T09` Implement renderer-neutral shadowmapping frontend, deterministic page residency, and no-fallback guardrails.
  Dependency: `FR-02-T01`. Priority: P0.
- [x] `FR-02-T10` Implement native OpenGL shadow page allocation/render/sample backend under the shared frontend.
  Dependency: `FR-02-T09`. Priority: P0.
  Progress: Dynamic effect dlights now preserve stable cdlight/entity/explosion identities before they reach shared shadow selection, reducing page churn and flicker in the OpenGL backend.
- [x] `FR-02-T11` Implement native Vulkan raster shadow page allocation/render/sample backend under the shared frontend.
  Dependency: `FR-02-T09`, `FR-01-T07`. Priority: P0.
  Progress: The same dynamic effect dlight identity/cache-key stabilization applies to native Vulkan, while dynamic pages remain rerendered when light parameters move or fade.

## Epic FR-03: JSON UI Rework Completion
Objective: complete modern menu coverage and remove remaining UX gaps for core settings and flows.

Primary Areas: `src/game/cgame/ui/*`, `src/game/cgame/ui/worr.json`, menu proposal docs

Exit Criteria:
- Main menu, in-game menu, and settings hierarchy are complete and stable.
- High-value missing widgets are implemented or replaced by approved alternatives.

Tasks:
- [ ] `FR-03-T01` Convert current menu proposal into implementation backlog with explicit widget tickets.
  Dependency: none. Priority: P0.
- [ ] `FR-03-T02` Implement dropdown overlay behavior (no legacy spin-style fallback for new pages).
  Dependency: `FR-03-T01`. Priority: P0.
- [ ] `FR-03-T03` Implement palette picker widget for color-centric settings.
  Dependency: `FR-03-T01`. Priority: P1.
- [ ] `FR-03-T04` Implement crosshair tile/grid selector with live preview.
  Dependency: `FR-03-T01`. Priority: P1.
- [ ] `FR-03-T05` Implement model preview widget for player visuals pages.
  Dependency: `FR-03-T01`. Priority: P1.
- [ ] `FR-03-T06` Audit and complete settings page cvar wiring for Video/Audio/Input/HUD/Downloads.
  Dependency: `FR-03-T02..T05`. Priority: P0.
  Progress: The cgame Effects menu now exposes `cg_weapon_bob` as a 0/1/2 selector for disabled, Quake 3, and Doom 3 viewweapon bob modes.
- [ ] `FR-03-T07` Add menu regression checklist (navigation, conditionals, scaling, localization).
  Dependency: `FR-03-T06`. Priority: P1.
- [ ] `FR-03-T08` Complete split between engine-side and cgame-side UI ownership where still mixed.
  Dependency: `FR-03-T06`. Priority: P1.
- [x] `FR-03-T09` Complete multi-monitor settings hierarchy and monitor targeting behavior for fullscreen modes.
  Dependency: `FR-03-T06`. Priority: P1.
- [x] `FR-03-T10` Align the fixed-layout main menu framing with Quake II rerelease reference captures.
  Dependency: none. Priority: P1.

## Epic FR-04: Bots and Match Experience
Objective: evolve bot and match systems from structural presence to reliable gameplay experience.

Primary Areas: `src/game/sgame/bots/*`, `src/game/sgame/match/*`, `src/game/sgame/gameplay/*`, `tools/q2aas/*`

Exit Criteria:
- Bots can join, navigate, fight, and participate in primary supported modes without obvious dead behavior.
- Match flow automation remains stable with bots in common scenarios.
- Quake II / Quake II Rerelease maps have a maintained AAS generation path based on credited upstream BSPC work.
- Imported bot/AAS code and algorithms have complete source provenance and credits.

Tasks:
- [x] `FR-04-T01` Define bot MVP behavior set (spawn, roam, engage, objective awareness).
  Dependency: none. Priority: P0.
  Progress: MVP behavior scope is accepted and mapped to current WORR/Q3A ownership boundaries. Spawn/leave, profile loading, AAS area lookup, route-to-item/roam, basic visible-enemy engagement, simple stuck recovery, and FFA/TDM match-flow participation are covered by promoted profile, frame-command, combat, item, stuck-recovery, and match-readiness scenario rows.
  Implementation log: `docs-dev/q3a-botlib-phase0-mvp-closeout-2026-06-21.md`.
- [ ] `FR-04-T02` Implement frame logic in `Bot_BeginFrame` and `Bot_EndFrame`.
  Dependency: `FR-04-T01`. Priority: P0.
  Progress: The first AAS route-steered bot command dispatch path is in place. Before each server game frame, spawned local bot clients call `BOT_FRAME_COMMAND_API_V1` in `sgame`; the game asks `bot_nav` for cached route state, builds a `usercmd_t`, and the server runs accepted commands through `SV_BotClientThink()` so fake-client movement uses normal server `ClientThink` bookkeeping. Dedicated smoke on `mm-rage` reports `frames=8`, `commands=8`, `route_requests=8`, `route_queries=2`, `route_reuses=6`, `route_commands=8`, `route_failures=0`, and `pass=1`; route/goal debug smoke reports native cached overlay counters, reachability fields, route polyline counters, and selected-client filter counters including `route_debug_routes=8`, `route_debug_goals=8`, `route_debug_labels=8`, `route_debug_polyline_points=16`, `route_debug_polyline_segments=24`, `route_debug_filtered_slots=0`, `last_current_area=224`, `last_route_point_count=2`, `last_reachability_type=2`, `last_route_debug_client=0`, and `last_debug_filter_client=0`. Current high-level frame command/status ownership now lives in `bot_brain.*`, while full `Bot_BeginFrame`/`Bot_EndFrame` scheduling, perception, richer debug-state ownership, and deeper behavior policy remain pending.
  Persistent route-goal update: `bot_nav` now owns a persistent route goal area across cache reuses and cadence refreshes, and the dedicated smoke reports `route_goal_assignments=1`, `route_goal_requests=1`, `route_goal_cache_reuses=6`, `last_persistent_goal_area=227`, and `pass=1`.
  Legacy surface update: the old Q2R `sgame/bots` debug/export/entity-state files are removed; frame logic should continue through the WORR/Q3A BotLib replacement path rather than the old engine bot callbacks.
  Item route-goal update: `bot_nav` now scans active pickup entities and resolves the selected pickup to a persistent AAS route goal. The dedicated smoke reports `item_goal_scans=1`, `item_goal_candidates=45`, `item_goal_assignments=1`, `item_goal_reuses=7`, `last_item_goal_entity=32`, `last_item_goal_area=415`, `last_item_goal_item=53`, `last_persistent_goal_area=415`, and `pass=1`.
- [ ] `FR-04-T03` Add weapon selection heuristics and situational item use.
  Dependency: `FR-04-T02`. Priority: P1.
  Progress: Combat now exposes an opt-in aim/fairness policy helper that gates
  aim/fire permission on skill, reaction delay, FOV, yaw/pitch turn limits,
  aim-settle time, and burst cooldown/limits while returning deterministic
  aim-error and tracking-noise metadata for future brain integration. The
  live-aim helper combines that policy with direct-projectile lead points and
  is now consumed by the brain-owned known-enemy aim path, with richer status
  fields for reaction, settle, burst, turn overage, and lead scaling.
  Item utility now distinguishes damage boosts, protection, invisibility,
  mobility, utility powerups, techs, and CTF objective buckets, and item timer
  knowledge can be disabled or deterministically fuzzed through
  `sg_bot_allow_item_timers` / `sg_bot_item_timer_fuzz_ms` where `bot_items`
  already owns timing facts. Live pickup and observed-respawn timing consumers
  now conservatively gate normal item selection and expose status-friendly
  consumer counters, coop leader follow/regroup/support policy now consumes
  the timed route owner for short leader-route commands, and default-off
  WaitForLeader policy can own a stop-and-face command for progression waits.
  Door/elevator cooperation now has a default-off source-owner plus teammate
  hold proof; broader trigger-detected campaign coordination remains follow-up.
  Accepted weapon/inventory action intents can now dispatch validated exact
  `use_index_only` requests through the brain-owned frame path and the item
  `use` callback boundary.
  Enemy health/armor estimates now flow through the combat blackboard: visible
  observations snap per-bot estimates to current vitals, bot-attributed damage
  records split health/armor deltas with a sequence guard, and the weapon scorer
  now consumes them for finisher, armor-pressure, and underpowered-choice
  adjustments with status-friendly counters. The action layer now scans carried
  weapons after enemy-fact enrichment and feeds the best scorer-approved weapon
  back as the preferred switch target. Carried inventory policy now covers
  combat/survival powerups, power armor, environment utility, spheres,
  placement-checked doppelganger, last-resort teleporter escape, and
  safety-gated nuke use. Submitted safe nuke inventory use now also arms a
  short-lived brain-owned retreat route goal so the bot begins moving away from
  the area-denial source after use, and submitted personal teleporter escape use
  now arms a short timed route goal away from remembered enemy pressure, recent
  damage, or a view-direction fallback source. Coop follow/regroup/support
  policy now also arms a short `coop_leader` timed route owner toward or around
  the selected leader without overriding emergency item route owners, and that
  route owner now has a compact-status `coop_leader_route` scenario gate. Coop
  no-leader LeadAdvance policy now has a default-off
  `sg_bot_coop_lead_advance` timed route owner and dedicated mode `27` proof.
  Coop progress waiting now has a default-off `sg_bot_coop_progress_wait` command
  owner that consumes WaitForLeader policy, and route-detected coop
  interactions now have a default-off `sg_bot_coop_interaction_retry` wait/use
  retry owner.
  Implementation logs: `docs-dev/q3a-botlib-combat-weapon-metadata-2026-06-18.md`,
  `docs-dev/q3a-botlib-weapon-inventory-command-api-2026-06-18.md`,
  `docs-dev/q3a-botlib-weapon-inventory-dispatch-2026-06-18.md`,
  `docs-dev/q3a-botlib-aim-fairness-policy-2026-06-18.md`,
  `docs-dev/q3a-botlib-live-aim-policy-integration-2026-06-18.md`,
  `docs-dev/q3a-botlib-live-combat-policy-round-2026-06-18.md`,
  `docs-dev/q3a-botlib-enemy-health-armor-estimates-2026-06-20.md`,
  `docs-dev/q3a-botlib-estimate-aware-weapon-selection-2026-06-20.md`,
  `docs-dev/q3a-botlib-carried-arsenal-selection-2026-06-20.md`,
  `docs-dev/q3a-botlib-nonweapon-inventory-policy-2026-06-20.md`,
  `docs-dev/q3a-botlib-utility-deployable-inventory-policy-2026-06-20.md`,
  `docs-dev/q3a-botlib-escape-deployable-inventory-policy-2026-06-20.md`,
  `docs-dev/q3a-botlib-safe-nuke-inventory-policy-2026-06-20.md`,
  `docs-dev/q3a-botlib-nuke-retreat-route-ownership-2026-06-21.md`,
  `docs-dev/q3a-botlib-timed-route-goal-owner-2026-06-21.md`,
  `docs-dev/q3a-botlib-teleporter-escape-route-owner-2026-06-21.md`,
  `docs-dev/q3a-botlib-coop-leader-route-owner-2026-06-21.md`,
  `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`,
  `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`,
  `docs-dev/q3a-botlib-coop-progress-wait-command-2026-06-21.md`,
  `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`,
  `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`,
  `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`,
  `docs-dev/q3a-botlib-item-timer-fairness-2026-06-18.md`,
  `docs-dev/q3a-botlib-live-item-timing-consumers-2026-06-18.md`,
  `docs-dev/q3a-botlib-special-item-utility-2026-06-18.md`.
- [ ] `FR-04-T04` Add team mode awareness (CTF/TDM/etc.) to bot utility state updates.
  Dependency: `FR-04-T02`. Priority: P1.
  Progress: Team-objective helper/status vocabulary now exists for enemy flag, own flag return, neutral flag, and base-defense assignments. The current proof-hook wave wires authoritative CTF flag pickup, return, and capture observations from the real gameplay branches into objective counters, and runtime mode `23` now passes as the `team_objective` smoke scenario. Deterministic role policy and lane/depth helpers now expose attacker, defender, returner, support, attack, defense, midfield, carrier-support, dropped-flag-response, and own-base-return metadata. Objective-side FFA/TDM/CTF match helpers now also report scoring participation, roam/collect/engage intent, item-role splits, friendly-fire recommendations, resource-denial intent, and target-engagement recommendations; default-off `sg_bot_ffa_roam_route` consumes selected FFA roam/collect/engage policy as a timed route-goal owner in mode `42`, default-off `sg_bot_ffa_spawn_camp_avoidance` composes with that owner to source FFA anti-camp routes away from nearby live opponents in mode `45`, default-off `sg_bot_ffa_role_combat` consumes selected FFA role/lane/engage policy as a live attack-decision owner in mode `48`, default-off `sg_bot_team_role_route` consumes selected match role/lane policy as a timed route-goal owner in mode `32`, default-off `sg_bot_team_item_roles` consumes selected match item-role policy as a live pickup-candidate scoring bridge in mode `33`, default-off `sg_bot_team_resource_denial` consumes deny-enemy resource policy as a live pickup-candidate scoring bridge in mode `50`, default-off `sg_bot_team_fire_avoidance` consumes friendly-fire policy as live attack-input suppression in mode `34`, default-off `sg_bot_team_role_combat` consumes selected TDM match role/lane policy as a live attack-decision owner in mode `43`, and mode `44` proves that TDM role-combat attack decisions compose with `sg_bot_team_fire_avoidance` so blocked friendly-line shots are vetoed. Default-off `sg_bot_ctf_role_route` consumes CTF role/lane policy as a timed route-goal owner in mode `35`, default-off `sg_bot_ctf_role_combat` consumes CTF role/lane policy as a live attack-decision owner in mode `36`, default-off `sg_bot_ctf_dropped_flag_route` consumes dropped enemy flag response policy as route ownership in mode `37`, default-off `sg_bot_ctf_carrier_support_route` consumes same-team enemy flag-carrier support policy as route ownership in mode `38`, default-off `sg_bot_ctf_base_return_route` consumes enemy own-flag carrier return policy as route ownership in mode `39`, default-off `sg_bot_ctf_objective_route` composes base-return, carrier-support, and enemy-flag fallback candidates into one priority route owner in mode `40`, and mode `41` proves the objective-route owner takes precedence over the generic CTF role-route owner while `ctf_role_route_objective_deferrals` records the handoff. Coop/resource helpers now report follow, wait, regroup, lead, support, team-share, teammate-reserve, enemy-deny, and objective-resource policy results; follow/regroup/support now feed a short `coop_leader` timed route owner in the brain command path with a compact `coop_leader_route` scenario gate, no-leader LeadAdvance can own a short timed route behind `sg_bot_coop_lead_advance`, default-off progression waiting can consume WaitForLeader policy as stop-and-face command ownership, default-off route interaction retry can consume detected mover/trigger route evidence as wait/use command ownership, default-off resource sharing can defer item route-goal candidates for another coop bot through reserve-for-teammate policy, default-off anti-blocking can consume close-to-leader policy as a short backpedal/sidestep command while facing the leader, default-off target sharing can let support-policy bots adopt a teammate's current hostile monster target from the blackboard, and default-off door/elevator cooperation can split mover wait/use source ownership from teammate hold commands. Durable route/reach policy, broader autonomous role ownership across real team flows, and broader campaign-specific trigger/key/objective coordination remain pending.
  Latest CTF item-role update: default-off `sg_bot_ctf_item_roles` now consumes selected CTF match item-role policy as a live pickup-candidate scoring bridge in mode `47`, with separate `ctf_item_role_*` and `last_ctf_item_role_*` nav-policy status from the FFA and TDM item-role bridges.
  Previous FFA item-role update: default-off `sg_bot_ffa_item_roles` consumes selected FFA match item-role policy as a live pickup-candidate scoring bridge in mode `46`, with separate `ffa_item_role_*` and `last_ffa_item_role_*` nav-policy status from the existing TDM `sg_bot_team_item_roles` bridge.
  Latest FFA role-combat update: default-off `sg_bot_ffa_role_combat` consumes selected FFA match role/lane/engage policy as a live attack-decision bridge in mode `48`, with separate `ffa_role_combat_*` and `last_ffa_role_combat_*` frame-command status from the TDM and CTF combat bridges.
  Implementation logs: `docs-dev/q3a-botlib-team-objective-helper-scaffold-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-proof-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-depth-2026-06-18.md`, `docs-dev/q3a-botlib-ffa-tdm-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-ffa-roam-route-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-dropped-flag-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-carrier-support-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-base-return-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-policy-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-precedence-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-coop-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-coop-leader-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`, `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-progress-wait-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`, `docs-dev/q3a-botlib-coop-anti-blocking-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-target-share-2026-06-21.md`, `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-gameplay-hooks-2026-06-18.md`.
  Latest FFA item-role implementation log: `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`.
  Latest FFA role-combat implementation log: `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`.
  Latest CTF item-role implementation log: `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`.
- [ ] `FR-04-T05` Add map-level nav validation pass and bot spawn diagnostics.
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T06` Add bot participation checks to match/tournament/map-vote flows.
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T07` Provide bot tuning cvars in preferred naming convention (`sg_` for new controls).
  Dependency: `FR-04-T01`. Priority: P2.
  Progress: Public bot cvar docs now cover the practical setup/debug cvars plus item timer fairness controls, including `sg_bot_allow_item_timers` and `sg_bot_item_timer_fuzz_ms`, in user-facing operator language. The item-timer cvars are helper/policy controls; broad respawn timing consumers remain future behavior work.
- [x] `FR-04-T08` Recreate a modern competitive top HUD for FFA/team/duel, including match timer, time limit, warmup/countdown state, player/team assets, and spectator duel vitals.
  Dependency: none. Priority: P1.
- [x] `FR-04-T09` Harden player spawn selection and combat heatmap danger scoring for multiplayer respawns.
  Dependency: none. Priority: P1.
  Progress: Completed a second-pass audit that separates solo/coop fallback starts from normal FFA/team pools, scores relaxed fallbacks, uses point-trace enemy visibility, filters heat writes to player-involved damage, and shortens stale heat influence.
  Implementation logs: `docs-dev/sgame-spawn-selection-heatmap-hardening-2026-06-16.md`, `docs-dev/sgame-spawn-selection-heatmap-second-pass-2026-06-16.md`.
- [ ] `FR-04-T10` Complete the Q3A BotLib/BSPC source audit, license review, and credits ledger before importing code.
  Dependency: none. Priority: P0.
  Progress: First audit and credits ledger are in place. `TTimo/bspc`, `bnoordhuis/bspc`, and id Software public mirror refs are pinned; unpinned local Q3A source is reference-only until matched to a pinned source or approved manifest.
- [ ] `FR-04-T11` Tailor `TTimo/bspc` into a WORR Q2/Q2R AAS generator with reproducible map validation.
  Dependency: `FR-04-T10`. Priority: P0.
  Progress: Pinned `TTimo/bspc` is vendored under `tools/q2aas/`; `worr_q2aas` builds through Meson; `tools/q2aas/cfg/worr_q2.cfg` loads through `q2aas-config-smoke`; the WORR Q2 trace bridge now lets `MAPTYPE_QUAKE2` run BotLib reachability. `.install\basew\maps\mm-rage.bsp` strict validation writes `.tmp\q2aas\mm-rage.aas` with 428 AAS areas, 562 reachability records, and 4 clusters. `tools/q2aas/validation_manifest.json` and `q2aas-staged-smoke` now run the staged map matrix, require Q2 `IBSP` version 38 input, emit `.tmp\q2aas\validation-report.json`, write deterministic `.aas.meta.json` sidecars with tool/config/BSP/AAS hashes, record AAS source checksum metadata, detect BSPX marker offsets, parse entity and brush-content diagnostics, count spawn/item origin coverage, report high-value pickup reachability from spawn areas, fail on clean BSP lump/spawn/item/high-value reachability regressions, fail when AAS metrics or travel counts drop below manifest baselines, validate/report manifest schema/task provenance before conversion, and run an automated malformed-manifest expected-failure smoke including archive-backed manifest guardrails. `q2aas-stage-aas` now validates and stages `.install\basew\maps\mm-rage.aas` with a staged-output hash report, `q2aas-stage-audit` verifies the staged file path/size/hashes, `q2aas-package-map-smoke` verifies pkz archive extraction plus conversion through a scratch packaged map, `q2aas-package-audit` verifies staged AAS release-payload representation, `q2aas-package-aas` injects `maps/mm-rage.aas` into `.install\basew\pak0.pkz` with an archive-required audit, and `refresh_install.py --package-q2aas-aas` preserves the generated AAS member after rebuilding `pak0.pkz` from assets while generic staged release validation can require the packaged member and hash. Release binary policy now keeps q2aas/BSPC tool binaries out of default packages and requires notice sidecars for imported Q3A/BSPC work. Available-reference validation now covers the current eight-map staged set (`mm-rage`, `q2dm1`, `q2dm2`, `q2dm8`, `q2ctf1`, `base1`, `base2`, and `train`), including CTF team-objective reachability, campaign progression diagnostics, and water-backed liquid coverage. Slime/lava-specific candidates, broader release manifest automation, and deeper runtime map-behavior proof remain pending.
- [ ] `FR-04-T12` Rehost the Quake III Arena BotLib runtime behind a WORR sgame adapter.
  Dependency: `FR-04-T10`. Priority: P0.
  Progress: First WORR-native runtime shell is in place at `src/game/sgame/bots/bot_runtime.*`. It registers the initial `sg_bot_*` cvars, hooks map start/entity reload/frame/shutdown lifecycle, probes `maps/<map>.aas` through the filesystem extension, decodes the Q3A/BSPC AAS v5 header transform, validates the `EAAS` version 5 lump table, and records AAS structural counts for debug status. Runtime smoke against refreshed `.install` loads packaged `maps/mm-rage.aas` with `428` areas, `562` reachability records, and `4` clusters. The Q3A import boundary is now compiled into `sgame`: `src/game/sgame/bots/q3a/` is reserved for commit-pinned imports, `q3a_botlib_boundary.*` records the planned runtime/AAS inventory, and `botlib_adapter.*` owns the future setup/shutdown/map/frame bridge. The first commit-pinned Q3A utility subset (`q_shared.h`, `surfaceflags.h`, `botlib.h`, `be_interface.h`, `l_log.h`, `l_memory.*`, and `l_libvar.*`) now compiles through `q3a_botlib_utility`; `q3a_botlib_import.*` provides tracked memory/shared-utility callbacks, and verbose runtime smoke reports `Q3A LibVar smoke passed`. The next commit-pinned AAS loader subset (`be_aas_file.c`, `aasfile.h`, `be_aas*.h`, and parser utility headers) now loads the active packaged AAS through Q3A's native `AAS_LoadAASFile` path using the callback-backed WORR filesystem bridge, with the active-memory file bridge retained as a fallback. It records matching Q3A world counts and unloads through imported Q3A shutdown. Q3A `be_aas_sample.c`, `be_aas_reach.c`, `be_aas_route.c`, `be_aas_routealt.c`, `l_crc.*`, `be_aas_main.c`, `be_aas_entity.c`, `be_aas_move.c`, and `be_aas_debug.c` are now imported for read-only AAS query, frame-lifecycle, entity-cache, movement-helper, and debug-area helper smoke; the previous temporary `AAS_AreaReachability`, `aasworld`, `AAS_Time`, `AAS_ProjectPointOntoVector`, `AAS_Error`, `AAS_ResetEntityLinks`, `AAS_InvalidateEntities`, `AAS_UnlinkInvalidEntities`, `AAS_InitAlternativeRouting`, `AAS_ShutdownAlternativeRouting`, movement prediction/drop/jump shims, and debug-line helper definitions have been removed in favor of imported Q3A implementations or callback-backed bridge code. The bridge now feeds `level.time.milliseconds()` into Q3A `Sys_MilliSeconds` each frame, uses real Q3A-style `AngleVectors`, maps Q3A debug line/cross/arrow and area-helper output to WORR debug callbacks under debug cvars, and runs a route/goal overlay smoke under `sg_bot_debug_route` / `sg_bot_debug_goal`; verbose smoke reports `q3a_angle_vectors=Q3A AngleVectors smoke passed`, `q3a_time_ms=25`, `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`, `q3a_route_overlay=Q3A route overlay passed: callback=yes start=3 goal=6 end=6 travel_time=113 reachability=1 lines=2 crosses=3 arrows=1 clears=1 failures=0`, `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`, `q3a_debug_draw=Q3A route overlay debug draw passed: callback=yes lines=2 crosses=3 arrows=1 clears=1 failures=0`, `q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0`, and `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`. Active-map Q2 BSP entity data is now validated from `maps/<map>.bsp` as `IBSP` version 38, parsed into Q3A-style epairs before AAS load, and reported as `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start` with `q3a_bsp_entity_smoke=yes`. Active-map Q2 BSP model data is also parsed from lump 13 and reported as `q3a_bsp_model=Q3A BSP model lump load passed: maps/mm-rage.bsp models=18 smoke_model=1 mins=(-328.0 -584.0 24.0) maxs=(-192.0 -440.0 256.0)` with `q3a_bsp_model_smoke=yes`. Active-map Q2 BSP collision data is now parsed from the static-world collision lumps and reported as `q3a_bsp_collision=Q3A BSP collision load passed: maps/mm-rage.bsp planes=1367 nodes=1863 leafs=1882 brushes=1142 point_contents=1 trace_fraction=0.347 startsolid=0 allsolid=0` with `q3a_bsp_point_contents_smoke=yes` and `q3a_bsp_trace_smoke=yes`. Active-map Q2 BSP visibility data is now parsed from leaf cluster IDs and the compressed visibility lump and reported as `q3a_bsp_visibility=Q3A BSP visibility load passed: maps/mm-rage.bsp clusters=303 smoke_cluster=0 pvs_visible=142 phs_visible=289` with `q3a_bsp_pvs_smoke=yes` and `q3a_bsp_phs_smoke=yes`. The server frame now pushes WORR bot-facing entity snapshots into imported Q3A `AAS_UpdateEntity` after the entity update pass; verbose smoke reports `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`. Q3A `AAS_EntityCollision` now reaches a WORR `gi.clip` entity trace callback, SOLID_BSP snapshots translate server model config indices into Q3A inline BSP model numbers, and verbose smoke reports `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`. Dynamic BSP leaf entity links and Q3A `AAS_BoxEntities` now use active-map Q2 BSP node/leaf data, with verbose smoke reporting `q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18`, `q3a_bsp_leaf_link_failures=0`, and `q3a_bsp_box_entities_smoke=yes`. Native cached route/goal debug markers, reachability labels, bounded polylines, selected-client filtering, persistent item goals, exact-origin position goals, stuck-progress repath, short stuck recovery commands, item-goal blacklist cooldowns, failed-goal reason diagnostics, reachability-aware movement-state command counters, natural jump/ladder/barrier-jump/walk-off-ledge/elevator travel-type validation, natural crouch/swim/waterjump support diagnostics, interaction wait/use retry telemetry, and `bot_brain.*` command/status ownership now draw or report for live bot route state, while natural crouch/swim/waterjump runtime proof waits on reference maps and higher-level behavior remains pending.
  Optimize import update: Q3A `be_aas_optimize.c` now compiles beside the existing AAS runtime imports, and the previous temporary `AAS_Optimize` no-op is removed in favor of the imported implementation. WORR keeps `aasoptimize=0` for the default loaded-AAS smoke because the Q3A optimization path mutates geometry/index arrays for opt-in save/forcewrite flows.
  Current AAS runtime adapter closeout: the imported Q3A AAS runtime C surface required by WORR route loading/query/movement/debug now rebuilds as part of `sgame_x86_64`; `AAS_Trace` and `AAS_PointContents` are final-owned by the active-map Q2 BSP collision bridge, `AAS_inPVS` / `AAS_inPHS` are final-owned by the active-map Q2 BSP visibility bridge, and `AAS_EntityCollision` reaches the WORR `gi.clip` path through `botlib_adapter.*` and `BotRuntimeEntityTrace`.
  Runtime lifecycle closeout: BotLib adapter initialization is idempotent for the game-module lifetime, `ShutdownGame()` calls `Bot_RuntimeShutdown()` after level unload/lifecycle status, and Q3A `bot_*` LibVars are documented as internal AAS-runtime state while public WORR policy stays on `sg_bot_*`.
  Entity snapshot closeout: `BotRuntimeBuildEntitySnapshot()` now separates players, bots, spectators, and monsters/NPCs before pushing snapshots into imported `AAS_UpdateEntity`, and loaded `sg_bot_debug_aas` output reports those current snapshot counts.
  Entity scheduling and fairness closeout: regular pickups, dropped items, traps/projectiles/hazards, doors/plats/movers, and objective/flag entities now have explicit snapshot categories and debug counts; item desirability refreshes are staggered per bot with cached candidate validation; route recomputation rate-limit checks/reuses/refreshes have dedicated status; and live aim remains gated by blackboard-visible, FOV-checked, shootable enemy facts.
  Print bridge update: Q3A `botimport.Print` now crosses the adapter into WORR logging with warning/error/fatal forwarding, verbose message-level output behind `sg_bot_debug_aas >= 3`, and `q3a_print_*` counters in adapter status.
  BotClientCommand safety update: Q3A `botimport.BotClientCommand` now crosses the adapter into a WORR runtime validation callback that requires a bot client and rejects execution until a dedicated bot command dispatcher exists.
  Memory allocator update: Q3A `botimport.GetMemory`, `FreeMemory`, and `HunkAlloc` now use tracked bot-owned zone/hunk allocation lists, with grouped hunk release after AAS shutdown and verbose memory counters.
  Filesystem bridge update: Q3A `botimport.FS_FOpenFile`, `FS_Read`, `FS_Seek`, and `FS_FCloseFile` now cross the adapter into WORR's filesystem load/free callbacks with tracked read-only file handles and an active-memory fallback.
  Lifecycle telemetry update: the Q3A BotLib bridge now reports init/load/unload/shutdown counters, transient unload residue, open file handles, and persistent LibVar zone bytes through verbose adapter status.
  Latest slice: the repeated lifecycle import harness runs three active `maps/mm-rage.aas` load/unload cycles and reports `loads=3/3`, `active_unloads=3`, `clean_unloads=3`, `unload_failures=0`, `last_unload_zone_active=0`, `last_unload_hunk_active=0`, and `last_unload_files=0`.
  Dedicated lifecycle smoke update: `sg_bot_lifecycle_smoke` now provides an unattended dedicated-server self-smoke that initializes real BotLib on `mm-rage`, lowers itself across the dedicated `map` game-DLL reload, reloads the map once, prints shutdown-time lifecycle status before each DLL unload, and exits with both shutdown lines reporting `q3a_lifecycle_clean_unloads=1`, `q3a_lifecycle_unload_failures=0`, and zero transient zone/hunk/file residue.
  Nav reachability/debug update: native cached route/goal debug now labels live bot route state with current AAS area and next reachability travel type, draws a bounded sampled route polyline, supports `sg_bot_debug_client` selected-client filtering, and the dedicated frame-command smoke reports `route_debug_labels=8`, `route_debug_polyline_points=16`, `route_debug_polyline_segments=24`, `route_debug_filtered_slots=0`, `last_current_area=224`, `last_route_point_count=2`, `last_reachability_type=2`, `last_reachability_flags=2`, `last_reachability_end_area=217`, and `last_debug_filter_client=0`.
  Persistent route-goal update: native `bot_nav` route slots now remember the first successful route goal area, request it on refresh, clear it on goal reach/fallback, and report goal counters through `q3a_bot_frame_command_status`.
  Legacy surface update: inherited Q2R `bot_debug.*`, `bot_exports.*`, and `bot_utils.*` are no longer part of `sgame`; BotLib/AAS runtime work now has a clean WORR-owned replacement boundary.
  Item route-goal update: native `bot_nav` route slots can now select an active pickup entity, resolve it through the BotLib adapter to an AAS area, keep the item entity/spawn-count metadata across cached route commands, and clear the goal when the pickup disappears or respawn-hides.
  Position route-goal update: native `bot_brain` and `bot_nav` can now drive a debug/smoke world-position route request, resolve it to an AAS area, and route to the exact resolved goal origin through `BotLibAdapter_BuildRouteSteerToGoal()` without changing area-only item or fallback routes.
  Natural travel-type goal update: staged `sv_bot_frame_command_smoke 9`, `10`, `11`, `12`, and `13` now validate natural packaged-AAS `TRAVEL_JUMP`, `TRAVEL_LADDER`, direct `TRAVEL_BARRIERJUMP`, and route-only `TRAVEL_WALKOFFLEDGE` / `TRAVEL_ELEVATOR` routes without forcing `sg_bot_frame_command_smoke_travel_type`; the barrier-jump run reports `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `last_reachability=319`, `last_reachability_type=4`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `travel_type_goal_start_warps=1`, `route_failures=0`, and `pass=1`.
  Rocket-jump route policy update: staged `sv_bot_frame_command_smoke 14` enables `sg_bot_allow_rocketjump 1`, selects packaged-AAS `TRAVEL_ROCKETJUMP`, and reports `last_reachability_type=12`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `route_failures=0`, and `pass=1`; staged `sv_bot_frame_command_smoke 15` leaves rocket-jump routing disabled, expects the route to be blocked, and reports `commands=0`, `route_commands=0`, `travel_type_goal_resolved=0`, `route_failures=8`, `travel_type_goal_expect_blocked=1`, and `pass=1`.
- [ ] `FR-04-T13` Implement WORR-native bot fake-client commands, slot lifecycle, and profile loading.
  Dependency: `FR-04-T01`. Priority: P0.
  Progress: First engine-owned fake-client lifecycle slice is in place. `sg_bot_add`, `sg_bot_remove`, `sg_bot_kick_all`, and `sg_bot_list` can add/list/remove local bot slots, bot clients call the game bot connect/begin path, server send/disconnect/status/population paths skip bot-only network work, and shutdown removes bot slots. Same-frame multi-bot adds now defer through a one-bot-per-frame queue, `ClientSpawn()` preserves `SVF_BOT`, and bot team assignment runs before host/owner auto-join logic so slot 0 bots spawn as bot participants instead of host spectators. `sg_bot_min_players` now fills auto-managed bots while `sg_bot_enable` is active, treats manual bots as satisfying the target, clamps the target to public slots, and removes only auto-managed bots when the target drops or bot support is disabled. `sg_bot_reload_profiles` now scans `botfiles/bots/*.c`, `bots/profiles/*.bot`, and `bots/*.bot`; `sg_bot_add [profile] [team]` resolves loaded profiles before falling back to display-name behavior; and `sg_bot_profile` can feed profile-backed min-player autofill. Profiles now bridge reaction, aggression, aim error, preferred weapon, chat personality, team role, and movement style into `bot_*` userinfo keys; role, team, item, and movement hints feed supported match-policy helpers, while chat remains preserved metadata for future behavior work. The first WORR-native botfiles pack now ships Q3/Gladiator-style `*_c.c` character entry points plus `_w/_i/_t.c` companions for `smoke`, `vanguard`, `bulwark`, `relay`, and `vector` under `assets/botfiles/bots/`; validation now checks behavior metadata, companion-family integrity, shared teamplay event-name parity, and Q2-oriented utility/chat/weapon/script polish. The loader strips `_c` to keep stable profile IDs and skips companion scripts as profile records; `refresh_install.py` packages the profiles into `pak0.pkz` and mirrors `botfiles` loose for no-zlib dedicated builds; and `profile_backed_spawn` runs in the implemented scenario suite. Initial bot team placement now respects match lock, one-on-one two-player active caps, and positive `maxplayers`; the per-frame policy cleanup preserves active humans first and moves surplus active bots to spectators when mode or active-player limits tighten. Clean dedicated smoke on `mm-rage` validates Alpha add/remove, Bravo add, queued Charlie add into slot 1, active count `2`, cleanup back to `0`, min-player fill to `B|bot1`/`B|bot2`/`B|bot3`, target trim to one auto bot, disable cleanup back to `0`, and direct game-side team-policy status for a three-bot Duel setup with `playing=2`, `spectators=1`, `bots=3`, plus cleanup to zero bots. The fake-client frame path now asks `sgame` for cached AAS route-steered bot commands and runs accepted commands through `SV_BotClientThink()` with `lastcmd` bookkeeping; long bot runs now top up local bot command budget before applying server-authored commands so fake clients avoid the human-client `commandMsec underflow` path. Basic active-pickup route goals, position route goals, item reservations, route-point look-ahead steering, velocity-aware command yaw, route-target stabilization, trace-checked corner cutting, stuck-progress repath, short stuck recovery commands, item-goal blacklist cooldowns, failed-goal reason diagnostics, reachability-aware movement-state command intent, natural jump/ladder/barrier-jump/walk-off-ledge/elevator travel-type route validation, default-off rocket-jump route gating, and the `bot_brain.*` command/status ownership split are in place; broader autonomous profile behavior and higher-level navigation policy remain pending.
  Legacy action boundary: the inherited Q2R bot export/action helper layer is removed, so weapon, item, trigger, pickup, and inventory behavior needs a new WORR bot action dispatcher above the fake-client command path.
  Implementation logs: `docs-dev/q3a-botlib-bot-slot-lifecycle-2026-06-17.md`, `docs-dev/q3a-botlib-multibot-slot-queue-2026-06-17.md`, `docs-dev/q3a-botlib-min-players-autofill-2026-06-17.md`, `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`, `docs-dev/q3a-botlib-profile-behavior-fields-2026-06-17.md`, `docs-dev/q3a-botlib-team-policy-cleanup-2026-06-17.md`, `docs-dev/q3a-botlib-frame-command-dispatch-2026-06-17.md`, `docs-dev/q3a-botlib-route-steered-frame-commands-2026-06-17.md`, `docs-dev/q3a-botlib-nav-route-cache-2026-06-17.md`, `docs-dev/q3a-botlib-nav-debug-overlay-2026-06-17.md`, `docs-dev/q3a-botlib-nav-reachability-debug-2026-06-17.md`, `docs-dev/q3a-botlib-nav-polyline-debug-2026-06-17.md`, `docs-dev/q3a-botlib-nav-debug-client-filter-2026-06-17.md`.
  Latest bot logs: `docs-dev/q3a-botlib-nav-persistent-goal-2026-06-18.md`, `docs-dev/q3a-botlib-legacy-bot-surface-removal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-item-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-item-reservation-2026-06-18.md`, `docs-dev/q3a-botlib-nav-lookahead-steering-2026-06-18.md`, `docs-dev/q3a-botlib-nav-velocity-steering-2026-06-18.md`, `docs-dev/q3a-botlib-route-target-stabilization-2026-06-18.md`, `docs-dev/q3a-botlib-trace-checked-corner-cutting-2026-06-18.md`, `docs-dev/q3a-botlib-nav-stuck-repath-2026-06-18.md`, `docs-dev/q3a-botlib-nav-stuck-recovery-command-2026-06-18.md`, `docs-dev/q3a-botlib-nav-goal-blacklist-cooldown-2026-06-18.md`, `docs-dev/q3a-botlib-nav-failed-goal-reason-2026-06-18.md`, `docs-dev/q3a-botlib-nav-movement-state-commands-2026-06-18.md`, `docs-dev/q3a-botlib-bot-brain-command-ownership-2026-06-18.md`, `docs-dev/q3a-botlib-nav-position-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-ladder-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-walkoffledge-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-elevator-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-barrierjump-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-rocketjump-policy-2026-06-18.md`, `docs-dev/q3a-botlib-nav-four-bot-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-eight-bot-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-soak-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-map-change-repeat-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-movement-door-retry-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-dispatcher-2026-06-18.md`, `docs-dev/q3a-botlib-q3-style-botfiles-2026-06-18.md`.
  Latest botfile/profile logs: `docs-dev/q3a-botlib-native-botfiles-assets-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-validation-tool-2026-06-18.md`, `docs-dev/q3a-botlib-profile-loader-hardening-2026-06-18.md`, `docs-dev/q3a-botlib-profile-scenario-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-loose-staging-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-user-docs-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-style-audit-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-q3a-style-expansion-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-scripts-package-coverage-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-scripts-support-2026-06-18.md`, `docs-dev/q3a-botlib-botfile-script-parity-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-worker-i-validation-2026-06-18.md`, `docs-dev/q3a-botlib-profile-behavior-validation-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-parity-polish-2026-06-18.md`, and `docs-dev/q3a-botlib-botfiles-q3a-parity-followup-2026-06-18.md`.
- [ ] `FR-04-T14` Implement AAS-backed navigation, route following, stuck recovery, and debug overlays.
  Dependency: `FR-04-T11`, `FR-04-T12`, `FR-04-T13`. Priority: P0.
  Progress: A spawned bot can now receive a cached AAS route-steered movement command through the normal server fake-client command path. `BotLibAdapter_BuildRouteSteer()` asks imported Q3A AAS for a reachable route from the bot's current area, validates the full route, returns the first route step as the steering target, and carries the selected reachability's travel type, travel flags, end area, and a bounded sampled route-point list into native `bot_nav` state. `BotLibAdapter_BuildRouteSteerToGoal()` also lets position route goals keep the exact resolved goal origin instead of falling back to an AAS area center. `BotLibAdapter_BuildRouteSteerForTravelType()` can now request a route whose selected next reachability matches a requested travel type. `bot_nav.*` now owns per-client route cache state, refresh cadence, reset on BotLib level lifetime, native cached route/goal debug markers, current-area/next-reachability labels, bounded route polyline drawing, selected-client filtering, position goals, travel-type smoke goals, route-target stabilization, trace-checked corner-cut candidate selection, stuck-progress monitoring, item-goal blacklist cooldowns, failed-goal reason diagnostics, natural movement support diagnostics, and interaction wait/use retry telemetry. `sv_bot_frame_command_smoke` proves command frames execute against packaged `maps/mm-rage.aas` with `route_requests=8`, `route_queries=2`, `route_refreshes=2`, `route_reuses=6`, `route_commands=8`, `route_failures=0`, and `pass=1`; the route/goal debug variant reports native overlay and reachability counters, and the filtered variant with `sg_bot_debug_client 1` reports filtered slots and `pass=1`. Natural map-backed jump, ladder, and barrier-jump validation pass, route-only walk-off-ledge/elevator/teleporter traversal is recognized as route-owned, interaction retry telemetry covers door/platform/train/trigger/mover wait/use paths, and controlled inactive recovery is available only behind `sg_bot_controlled_inactive_recovery`.
  Persistent route-goal update: `bot_nav.*` now owns a per-client persistent route goal area and reports `route_goal_requests=1`, `route_goal_assignments=1`, `route_goal_cache_reuses=6`, `route_goal_clears=0`, `route_goal_fallbacks=0`, `last_persistent_goal_area=227`, `last_goal_clear_reason=0`, and `pass=1`; door/trigger retry remains pending.
  Item route-goal update: `bot_nav.*` now selects a live active pickup entity, resolves its origin to an AAS area through `BotLibAdapter_FindRouteAreaForPoint()`, remembers entity/spawn-count/item metadata, and reports `item_goal_scans=1`, `item_goal_candidates=45`, `item_goal_assignments=1`, `item_goal_reuses=7`, `last_item_goal_entity=32`, `last_item_goal_area=415`, `last_item_goal_item=53`, `last_item_goal_score=828`, `last_persistent_goal_area=415`, and `pass=1` on refreshed `mm-rage`.
  Item/route scheduling update: item desirability scans are now split across bots with per-slot cached candidate validation and status counters for updates, cache reuses, and stagger deferrals; route recomputation cadence exposes rate-limit checks, reuses, and refreshes through `q3a_bot_route_schedule_status`.
  Item reservation update: `bot_nav.*` now skips active pickups already reserved by another bot's selected item route slot, resets nav route slots on bot disconnect, and reports `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `last_item_goal_reserved_entity=32`, `last_item_goal_reserved_by_client=0`, `route_goal_assignments=2`, `route_failures=0`, and `pass=1` in a two-bot `sv_bot_frame_command_smoke 3` run.
  Look-ahead steering update: `Bot_BuildFrameCommand()` now selects from the cached route-point polyline inside a short look-ahead distance before computing command yaw, and the two-bot smoke reports `lookahead_attempts=17`, `lookahead_uses=9`, `last_lookahead_point_count=2`, `route_failures=0`, and `pass=1`.
  Velocity-aware steering update: `Bot_BuildFrameCommand()` now applies a short projected-origin yaw adjustment when current horizontal bot velocity is measurable, and the two-bot smoke reports `velocity_lead_attempts=17`, `velocity_lead_uses=3`, `last_velocity_lead_speed_sq=182`, `last_velocity_lead_offset_sq=1`, `route_failures=0`, and `pass=1`.
  Route-target stabilization update: `bot_nav.*` now promotes a farther sampled route point when a refreshed route returns a near-origin `moveTarget`, recording stabilization checks/applications/skips and target-distance counters. Route reuse plus target stabilization closes the current adjacent-area jitter mitigation subitem; the later trace-checked corner-cutting slice adds direct server-trace and ground-probe proof before shortcut promotion.
  Stuck-repath update: `bot_nav.*` now monitors goal-distance progress across cached route uses and cadence refreshes, reports stuck reason fields in route debug/status output, and forces `BotNavRefreshReason::Stuck` through the native route refresh path; normal two-bot smoke reports `stuck_detections=0`, while stalled-command smoke reports `stuck_detections=2`, `stuck_repath_refreshes=2`, `last_stuck_reason=1`, `route_failures=0`, and `pass=1`.
  Stuck recovery command update: stuck detections now activate a short recovery window, and `Bot_BuildFrameCommand()` applies a back/strafe command during that window; normal smoke reports `stuck_recovery_activations=0` and `recovery_command_uses=0`, while stalled-command smoke reports `stuck_recovery_activations=2`, `stuck_recovery_frames=11`, `recovery_command_uses=11`, `last_recovery_forward_move=-80`, `last_recovery_side_move=-140`, `route_failures=0`, and `pass=1`.
  Goal blacklist cooldown update: stuck detections against a still-available active-pickup goal now blacklist that item identity for the stuck bot, clear the persistent goal with `last_goal_clear_reason=5`, and immediately scan for a different pickup while preserving the short recovery window; normal smoke reports zero blacklist activations/skips, while stalled-command smoke reports `item_goal_blacklist_activations=2`, `item_goal_blacklist_skips=2`, `item_goal_blacklist_active=2`, `last_item_goal_blacklisted_entity=68`, `route_failures=0`, and `pass=1`.
  Failed-goal reason update: abandoned persistent goals now record explicit failed-goal diagnostics before the goal identity is cleared; normal smoke reports `failed_goal_events=0` and `last_failed_goal_reason=0`, while stalled-command smoke reports blacklist-derived `failed_goal_events=2`, `last_failed_goal_reason=3`, `last_failed_goal_client=1`, `last_failed_goal_area=251`, `last_failed_goal_entity=74`, `last_failed_goal_item=2`, `route_failures=0`, and `pass=1`.
  Movement-state command update: `Bot_BuildFrameCommand()` now maps AAS crouch reachability to `BUTTON_CROUCH`, jump/barrier-jump/waterjump reachability to `BUTTON_JUMP`, and swim or ladder reachability to vertical jump/crouch intent based on target height. Normal `sv_bot_frame_command_smoke 3` reports walk-only `movement_state_attempts=17`, `movement_state_commands=0`, and `last_movement_state_travel_type=2`; forced smoke modes `5`, `6`, and `7` report `movement_state_jump_commands=17`, `movement_state_crouch_commands=17`, and `movement_state_swim_commands=17` respectively with `pass=1`.
  Bot brain ownership update: `bot_think.*` now preserves the stable `Bot_*` wrapper surface while `bot_brain.*` owns the current high-level frame command/status implementation. Refreshed-install smokes report normal `frames=17`, `commands=17`, `route_failures=0`, and `pass=1`; forced jump smoke reports `movement_state_jump_commands=17` and `pass=1`; stalled-command smoke reports `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, and `pass=1`.
  Position route-goal update: `bot_brain.*` can now request a debug/smoke world-space route goal and `bot_nav.*` resolves it ahead of item-goal scans, keeps it persistent across cache reuse, and routes through the exact resolved goal origin. `sv_bot_frame_command_smoke 8` reports `position_goal_requests=8`, `position_goal_resolved=8`, `position_goal_assignments=1`, `position_goal_cache_reuses=6`, `item_goal_scans=0`, `route_goal_fallbacks=0`, `last_position_goal_area=227`, `last_position_goal_z=98`, `route_failures=0`, and `pass=1`.
  Natural travel-type goal update: `sv_bot_frame_command_smoke 9` requests natural `TRAVEL_JUMP`, enables a smoke-only AAS start warp, and reports `last_reachability_type=5`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; `sv_bot_frame_command_smoke 10` requests natural `TRAVEL_LADDER` and reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=142`, `last_travel_type_goal_start_goal_area=143`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_reachability_type=6`, `movement_state_ladder_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; `sv_bot_frame_command_smoke 11` requests route-only natural `TRAVEL_WALKOFFLEDGE` and reports `last_reachability_type=7`, `route_commands=8`, `movement_state_commands=0`, `route_failures=0`, and `pass=1`; `sv_bot_frame_command_smoke 12` requests route-only natural `TRAVEL_ELEVATOR` and reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=241`, `last_travel_type_goal_start_goal_area=261`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_reachability_type=11`, `route_commands=8`, `movement_state_commands=0`, `movement_state_unsupported=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; `sv_bot_frame_command_smoke 13` requests natural `TRAVEL_BARRIERJUMP` and reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `travel_type_goal_resolved=8`, `travel_type_goal_assignments=8`, `last_reachability=319`, `last_reachability_type=4`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
  Rocket-jump route policy update: Q3A route flags now include `TFL_ROCKETJUMP` only when `sg_bot_allow_rocketjump 1` is active, and direct travel-type helpers obey the same policy. `sv_bot_frame_command_smoke 14` enables the cvar and reports `last_reachability_type=12`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `route_failures=0`, and `pass=1`; `sv_bot_frame_command_smoke 15` leaves the cvar disabled, expects a blocked travel-type goal, and reports `commands=0`, `route_commands=0`, `travel_type_goal_resolved=0`, `route_failures=8`, `travel_type_goal_expect_blocked=1`, and `pass=1`.
  Eight-bot frame-command smoke update: `sv_bot_frame_command_smoke 17` adds eight fake-client bots, holds eight active item reservations, and reports `frames=92`, `commands=92`, `route_commands=92`, `route_failures=0`, `route_goal_assignments=11`, `item_goal_assignments=11`, `item_goal_reservation_skips=49`, `item_goal_active_reservations=8`, `route_debug_routes=92`, `route_debug_goals=92`, and `pass=1`.
  Ten-minute soak update: `sv_bot_frame_command_smoke 18` keeps eight fake-client bots active for `600000` ms, records `item_goal_peak_active_reservations`, and reports `elapsed_ms=600001`, `frames=192036`, `commands=192036`, `route_requests=187232`, `route_commands=192036`, `route_failures=0`, `route_goal_assignments=4889`, `item_goal_assignments=1451`, `stuck_recovery_activations=11789`, `recovery_command_uses=72066`, `skipped_inactive=0`, and `pass=1`.
  Map-change repeat update: `sv_bot_frame_command_smoke 19` repeats the eight-bot route-command proof across a same-map reload and reports two passing cycles, `map_changes=1`, `final_count=0`, and zero route failures. The optional restart path `sv_bot_frame_command_smoke_map_repeat_restart 1` now uses `map "<map>" force`, reports `command=map_force restart=1`, gates cleanup with `active_reservations=0`, and passed three cycles with two forced restarts.
  Natural movement and interaction update: `q3a_bot_nav_natural_support_status` reports natural crouch/swim/waterjump support absence on packaged `mm-rage.aas`, including unsupported masks, reason codes, route-start areas, and origins for future reference maps, while the elevator proof reports `nav_interaction_elevator_activations=1`, `interaction_wait_command_uses=8`, and `interaction_use_command_uses=8`.
  Legacy surface update: route/debug work no longer depends on Q2R `Bot_MoveToPoint`, `Bot_FollowActor`, `GetPathToGoal`, or `bot_debug.*`; active navigation debug state lives in `bot_nav` and the BotLib adapter.
- [ ] `FR-04-T15` Translate Q3A behavior concepts into WORR/Q2 weapons, items, combat, team modes, and architecture boundaries.
  Dependency: `FR-04-T14`. Priority: P1.
  Progress: First native item reservation policy is in place above route-goal selection so two bots do not select the same active pickup by default. Rocket-jump route selection is policy-gated behind `sg_bot_allow_rocketjump`. The first compile-ready WORR action boundary exists in `bot_actions.*`, `bot_items.*`, and `bot_combat.*`, exposing decision/status APIs for item, combat, inventory, weapon, and world-use policy. `bot_brain.*` samples that dispatcher, applies validated action decisions to the current `usercmd_t` for attack/use button intents, records pending weapon/inventory intents, and emits `q3a_bot_action_status`. A per-bot blackboard now supplies visible enemy, shootability, last-seen, heard, and damaged facts for combat/objective smoke. `bot_combat.*` now covers Q2/WORR weapon metadata, current-vs-preferred scoring, real enemy fact construction, nearest visible/shootable target search, combat context enrichment, filtered bot-attributed damage records, enemy health/armor estimate scoring, an opt-in aim/fairness policy helper for skill-based reaction, FOV, turn, aim-settle, burst, aim-error, tracking-noise metadata, live aim-profile status, and live projectile-lead scaling consumed by the known-enemy brain aim path. `bot_actions.*` tracks validated pending weapon-switch requests through observed completion/failure proof state, dispatches accepted exact `use_index_only` weapon/inventory requests through the brain-owned frame path and item `use` callback boundary, scans carried weapons after enemy-fact enrichment so the best scorer-approved weapon can become the preferred switch target, scans carried non-weapon inventory for conservative combat/survival powerup or power-armor use, and now covers hazard/underwater utility, sphere deployable use, placement-checked doppelganger use, last-resort personal teleporter escape use, and safety-gated nuke use with active-effect, placement, owned-sphere, nuke-safety, friendly-deferral, self-deferral, and use counters. `bot_brain.*` now routes submitted safe nuke inventory use, submitted personal teleporter escape use, coop follow/regroup/support leader policy, coop no-leader LeadAdvance policy, default-off FFA roam/collect/engage policy, default-off FFA spawn-camp avoidance policy, default-off TDM role/lane match policy, default-off CTF role/lane match policy, default-off CTF dropped-flag response policy, default-off CTF carrier-support policy, default-off CTF base-return policy, and default-off CTF objective route-priority policy through route owners, exposes `timed_route_goal_*`, `nuke_retreat_*`, `teleporter_escape_*`, `coop_leader_route_*`, compact `coop_lead_advance_*`, `ffa_roam_route_*`, `ffa_spawn_camp_avoidance_*`, `team_role_route_*`, `team_role_combat_*`, `ctf_role_route_*`, `ctf_dropped_flag_route_*`, `ctf_carrier_support_route_*`, `ctf_base_return_route_*`, and `ctf_objective_route_*` status for owner kind, route ownership, source selection, fallback use, deferrals, expirations, invalid skips, and last source/goal/leader/role/lane/carrier-client/priority metadata, records `ctf_role_route_objective_deferrals` when the objective route policy takes precedence over the generic role-route owner, applies a default-off CTF role-combat bridge through `sg_bot_ctf_role_combat` with `ctf_role_combat_*` status before attack buttons are applied, applies a default-off TDM role-combat bridge through `sg_bot_team_role_combat` with `team_role_combat_*` status before CTF role combat and friendly-fire suppression, applies a default-off TDM friendly-fire bridge through `sg_bot_team_fire_avoidance` with `team_fire_avoidance_*` status before attack buttons are applied, and mode `44` proves those TDM role-combat and friendly-fire bridges compose so friendly-line attacks are vetoed after role-combat selection. It also applies a default-off WaitForLeader stop-and-face command through `sg_bot_coop_progress_wait` with `coop_progress_wait_*` status, applies a default-off route interaction wait/use retry command bridge through `sg_bot_coop_interaction_retry` with `coop_interaction_retry_*` status, lets default-off `sg_bot_coop_resource_share` consume coop/resource policy during item route-goal selection with compact `item_reserved_deferrals` evidence, applies a default-off `sg_bot_coop_anti_blocking` close-leader command owner with compact `coop_anti_block_*` evidence, applies default-off `sg_bot_coop_target_share` blackboard adoption so support-policy bots can inherit a teammate's current hostile monster target, and applies default-off `sg_bot_coop_door_elevator` source-owner plus support-hold commands for mover/elevator cooperation. `bot_nav.*` now lets default-off `sg_bot_team_item_roles` consume TDM match item-role policy, default-off `sg_bot_team_resource_denial` consume deny-enemy resource policy, default-off `sg_bot_match_item_policy` compose the FFA/CTF/TDM item-role plus TDM resource-denial scoring bridges during live pickup-goal scoring, and default-off `sg_bot_behavior_enable` activate the current command/nav behavior family through one integrated switch. The new `q3a_bot_behavior_policy_status` marker records umbrella activation for TDM role-route, role-combat, friendly-fire, match item-policy gates, central behavior owner arbitration, cvar classification, and handoff telemetry without setting the individual proof cvars. `bot_items.*` scores existing pickups, feeds health/armor routing, exposes deterministic health/armor setup plus real pickup-delta snapshot/observation helpers, distinguishes damage boosts, protection, invisibility, mobility, utility powerups, techs, and CTF objective utility buckets, applies an item timer disable/fuzz policy where it already owns timing knowledge, exposes special-item/power-armor classification wrappers for action policy, and now has conservative live pickup/observed-respawn timing consumers with status metadata. `bot_objectives.*` provides target-source-aware selection, assignment, route-goal handoff, event-record helper APIs, deterministic attacker/defender/returner/support role-policy helpers, lane/depth metadata, FFA/TDM/CTF match/item/friendly-fire helper policy, profile role consumption for match requested-role selection, profile teamplay/objective/friendly-fire-care hint consumption for match priority and friendly-fire avoidance, profile item-greed/item-denial/powerup-timing/retreat-health hint consumption for match item/resource policy, profile movement-style hint consumption for match movement bonuses, CTF dropped-flag, flag-carrier support, and own-flag return assignment helpers, and coop/resource helper metadata for future autonomous behavior. Dedicated smoke modes `20` through `63` now pass as implemented scenario rows for enemy engagement, weapon switching, health/armor pickup, team objective proof, live aim, item timing, match readiness, coop lead advance, coop resource sharing, coop anti-blocking, coop target sharing, coop door/elevator cooperation, team-role route ownership, team item-role pickup scoring, team resource-denial pickup scoring, team friendly-fire suppression, team role-combat ownership, team role-combat/friendly-fire precedence, CTF role-route ownership, CTF role-combat ownership, CTF dropped-flag route ownership, CTF carrier-support route ownership, CTF base-return route ownership, CTF objective-route policy ownership, CTF objective-route precedence, FFA roam-route ownership, FFA spawn-camp avoidance, FFA item-role pickup scoring, FFA role-combat attack ownership, FFA spawn-camp combat avoidance, CTF item-role pickup scoring, match item-policy pickup scoring, behavior policy umbrella activation, profile-role match-policy selection, profile team-policy match selection, profile item-policy match selection, profile movement-policy match selection, bot chat-policy live-dispatch gating, bot chat team-only audience gating, bot chat rate-limit gating, bot chat initial-personality selection, bot chat reply-policy selection, bot chat event-policy selection, and behavior arbitration owner/cvar classification; the coop leader-route, progress-wait, and interaction-retry reuse rows also pass. Broader autonomous team/coop role consumption, broader campaign-specific coordination, and richer Q3A-style bot chat live trigger coverage and phrase libraries remain pending.
  Latest FFA item-role behavior update: `bot_nav.*` now also lets default-off `sg_bot_ffa_item_roles` consume FFA match item-role policy during live pickup-goal scoring, exposing `ffa_item_role_*` and `last_ffa_item_role_*` evidence on compact nav policy status. Dedicated smoke mode `46` now passes as the `ffa_item_roles` implemented scenario row for FFA pickup scoring.
  Latest CTF item-role behavior update: `bot_nav.*` now also lets default-off `sg_bot_ctf_item_roles` consume CTF match item-role policy during live pickup-goal scoring, exposing `ctf_item_role_*` and `last_ctf_item_role_*` evidence on compact nav policy status. Dedicated smoke mode `47` now passes as the `ctf_item_roles` implemented scenario row for CTF pickup scoring.
  Latest FFA role-combat behavior update: `bot_brain.*` now lets default-off `sg_bot_ffa_role_combat` consume FFA match role/lane/engage policy during live attack-decision ownership, exposing `ffa_role_combat_*` and `last_ffa_role_combat_*` evidence on compact frame-command status. Dedicated smoke mode `48` now passes as the `ffa_role_combat` implemented scenario row for FFA attack ownership.
  Latest FFA spawn-camp combat-avoidance behavior update: `bot_brain.*` now lets default-off `sg_bot_ffa_spawn_camp_combat_avoidance` compose FFA role-combat target ownership with FFA anti-camp source policy, exposing `ffa_spawn_camp_combat_avoidance_*` and `last_ffa_spawn_camp_combat_avoidance_*` evidence on compact frame-command status. Dedicated smoke mode `49` now passes as the `ffa_spawn_camp_combat_avoidance` implemented scenario row for FFA anti-camp attack veto precedence.
  Earlier proof-hook update: real bot-attributed damage is observed from the authoritative damage path, health/armor pickup proof observations are tied to successful item touches, CTF objective hooks feed mode `23`, live-aim proof feeds mode `24`, item-timer proof feeds mode `25`, match-readiness proof feeds mode `26`, coop lead-advance proof uses mode `27`, coop resource-share proof uses mode `28`, coop anti-blocking proof uses mode `29`, coop target-sharing proof uses mode `30`, coop door/elevator proof uses mode `31`, team-role route ownership uses mode `32`, team item-role pickup scoring uses mode `33`, team friendly-fire suppression uses mode `34`, CTF role-route ownership uses mode `35`, CTF role-combat ownership uses mode `36`, CTF dropped-flag route ownership uses mode `37`, CTF carrier-support route ownership uses mode `38`, CTF base-return route ownership uses mode `39`, CTF objective-route policy ownership uses mode `40`, CTF objective-route precedence uses mode `41`, FFA roam-route ownership uses mode `42`, TDM role-combat ownership uses mode `43`, TDM role-combat/friendly-fire precedence uses mode `44`, FFA spawn-camp avoidance uses mode `45`, map-restart cleanup reuses mode `19` with `sv_bot_frame_command_smoke_map_repeat_restart 1`, warmup bot-start readiness uses `sv_bot_warmup_smoke 2`, vote bot-exclusion uses `sv_bot_vote_smoke 2`, admin bot privilege audit uses `sv_bot_admin_audit_smoke 2`, tournament bot veto-exclusion uses `sv_bot_tournament_smoke 2`, tournament replay reset uses `sv_bot_tournament_smoke 3`, match logging schema proof uses `sv_bot_matchlog_smoke 2`, MyMap queue proof uses `sv_bot_mymap_smoke 2`, queued nextmap transition proof uses `sv_bot_nextmap_smoke 2`, map-vote selector transition proof uses `sv_bot_mapvote_smoke 2`, scoreboard classification proof uses `sv_bot_scoreboard_smoke 2`, intermission bot cleanup proof uses `sv_bot_intermission_smoke 2`, coop leader-route and progress-wait proofs reuse mode `3`, coop interaction-retry proof reuses mode `12`, compact proof rows now precede oversized verbose diagnostics, marker checks now require the newer live-aim, match-policy, FFA-roam-route, FFA-spawn-camp-avoidance, team-role-route, team-item-role, team-fire-avoidance, team-role-combat, team-role-combat/friendly-fire precedence, CTF-role-route, CTF-role-combat, CTF-dropped-flag-route, CTF-carrier-support-route, CTF-base-return-route, CTF-objective-route, CTF-objective-route-precedence, map-restart command/reload/cleanup, warmup bot-only start/readiness, vote bot-exclusion, admin bot privilege audit, tournament bot veto-exclusion, tournament replay reset, match logging schema/versioning, MyMap queue/consume, queued nextmap transition, map-vote bot exclusion/finalization/reload, scoreboard bot classification, intermission bot cleanup, coop command-owner, resource-deferral, anti-blocking, target-sharing, and door/elevator evidence fields, and the implemented scenario catalog contained 50 short-run rows plus one manual degradation row at that point.
  Latest FFA item-role proof update: mode `46` hard-gates FFA item-role pickup scoring through FFA readiness, objective item-role policy selection, nav score boosts, selected pickup goals, and latest role/category/item metadata.
  Latest CTF item-role proof update: mode `47` hard-gates CTF item-role pickup scoring through CTF readiness, objective item-role policy selection, nav score boosts, selected pickup goals, invalid-skip absence, and latest mode/role/lane/category/item metadata; the implemented scenario catalog contained 52 short-run rows plus one manual degradation row after that promotion.
  Latest FFA role-combat proof update: mode `48` hard-gates FFA role-combat attack ownership through FFA readiness, objective role policy selection, visible and shootable enemy facts, target selection, attack decisions, and applied attack-button metadata; the implemented scenario catalog contained 53 short-run rows plus one manual degradation row after that promotion.
  Latest TDM resource-denial proof update: mode `50` hard-gates deny-enemy resource policy through live pickup-candidate scoring, nav score boosts, selected denial-shaped pickup goals, TDM readiness, and compact `team_resource_denial_*` status.
  Latest behavior policy umbrella update: mode `52` hard-gates `sg_bot_behavior_enable` through TDM role-route, role-combat, friendly-fire, match item-policy activation, and begin-marker evidence that the individual proof cvars remain disabled. The profile-role follow-up adds mode `53`, where staged profile ids feed `bot_role` userinfo into match-policy requested-role selection and prove attacker, defender, and support/midfield selections with no profile-role fallback. The profile team-policy follow-up adds mode `54`, where staged profile ids feed teamplay, objective, and friendly-fire-care userinfo into CTF match-policy priority bonuses and friendly-fire avoidance. The profile item-policy follow-up adds mode `55`, where staged profile ids feed item greed, item denial, powerup timing, and retreat-health userinfo into TDM match item/resource policy bonuses. The profile movement-policy follow-up adds mode `56`, where staged profile ids feed movement-style userinfo into TDM attack, defense, roam, collect, and selected-role bonuses without relying on the umbrella behavior cvar. The bot chat-policy dispatch follow-up updates mode `57`, where staged profile ids prove chat metadata and `sg_bot_allow_chat` status while `consumer_ready=1`, `dispatch_enabled=1`, `dispatch_attempts>=1`, `dispatch_submitted>=1`, `dispatch_failures=0`, and `blocked_until_consumer=0` prove the conservative live dispatch path. The team-policy follow-up adds mode `58`, where `sg_bot_chat_team_only` proves the team-only audience path with `team_only=1` and `last_dispatch_team=1`. The rate-policy follow-up adds mode `59`, where `sg_bot_chat_min_interval_ms` proves global rate limiting with `rate_limit_ms=60000`, `dispatch_rate_limited>=1`, and `dispatch_failures=0`. The initial-policy follow-up adds mode `60`, where profile `bot_chat_personality` metadata proves deterministic initial utterance bucket selection with four known profile personalities and zero dispatch failures. The reply-policy follow-up adds mode `61`, where a smoke-only gate proves deterministic reply selection for the first team-ready event with `reply_chat_selections=4`, `reply_chat_submitted=4`, and zero reply failures. The event-policy follow-up adds mode `62`, where a smoke-only gate proves team-ready plus route-ready reply events with `reply_chat_events=8`, `reply_chat_route_ready=4`, `reply_chat_submitted=8`, and zero reply failures. The behavior-arbitration follow-up adds mode `63`, where `sg_bot_behavior_enable` proves route/item/combat candidate discovery, combat owner selection, handoff telemetry, and live/smoke/debug/deprecated cvar classification without individual proof cvars. The target-memory follow-up adds mode `64`, where a two-bot FFA proof verifies blackboard target retention through a smoke occlusion, memory-age/window telemetry, a `1000` ms decay, and clear-after-decay counters. The weapon-scoring follow-up adds mode `65`, where a two-bot FFA proof verifies carried arsenal scanning, insufficient-ammo rejection, splash-risk pressure, range/estimate-backed super-shotgun selection, and switch completion. The aim/fire policy follow-up adds mode `66`, where a rocket actor proves reaction-delay withholding, aim-settle withholding, burst-cooldown pacing, live-aim policy blocks, rocket projectile lead, and attack application. The ammo-pressure follow-up adds mode `67`, where a one-bot FFA low-shell shotgun proof verifies ammo focus, ammo candidates, ammo seek decisions, ammo goal assignments, `item_last_item=30`, and zero route failures. The survival-inventory follow-up adds mode `68`, where a one-bot FFA low-health/no-armor proof verifies carried power-armor selection, pending inventory-use acceptance, validated `use_inventory_index` request construction, submitted dispatch outcome, and zero route failures. The survival-health follow-up adds mode `69`, where a one-bot FFA low-health/no-armor proof verifies health candidates, health seek decisions, low-health boosts, health goal assignments, selected health utility, and zero route failures. The survival-armor follow-up adds mode `70`, where a one-bot FFA full-health/no-armor proof verifies armor candidates, armor seek decisions, low-armor boosts, armor goal assignments, selected armor utility, and zero route failures. The combat-survival follow-up adds mode `71`, where a two-bot FFA low-health health-route proof verifies visible/shootable enemy pressure, action-layer target facts, withheld fire, health goal assignment, item ownership, and recovery ownership. The implemented scenario catalog now contains 77 total rows: 76 automated short-run rows plus one manual degradation row; focused validation passed from `.tmp\bot_scenarios\behavior_policy_umbrella\20260622T050833Z`, `.tmp\bot_scenarios\profile_role_policy\20260622T052929Z`, `.tmp\bot_scenarios\profile_team_policy\20260622T055119Z`, `.tmp\bot_scenarios\profile_item_policy\20260622T062835Z`, `.tmp\bot_scenarios\profile_movement_policy\20260622T070032Z`, `.tmp\bot_scenarios\20260622T080531Z`, `.tmp\bot_scenarios\20260622T080044Z`, `.tmp\bot_scenarios\20260622T081428Z`, `.tmp\bot_scenarios\20260622T085845Z`, `.tmp\bot_scenarios\20260622T092009Z`, `.tmp\bot_scenarios\20260622T093637Z`, `.tmp\bot_scenarios\20260622T112202Z`, `.tmp\bot_scenarios\20260622T120742Z`, `.tmp\bot_scenarios\20260622T123648Z`, `.tmp\bot_scenarios\20260622T125826Z`, `.tmp\bot_scenarios\ammo_pressure_pickup\20260622T132231Z`, `.tmp\bot_scenarios\survival_inventory_use\20260622T161739Z`, `.tmp\bot_scenarios\survival_health_route\20260622T164109Z`, `.tmp\bot_scenarios\survival_armor_route\20260622T165918Z`, and `.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z`; the latest full automated `implemented` run passed 76/76 rows from `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`, leaving live behavior depth as the next follow-up.
  Latest match item-policy proof update: mode `51` hard-gates the `sg_bot_match_item_policy` umbrella cvar through TDM item-role pickup scoring and deny-enemy resource scoring while the individual proof cvars remain disabled; the focused revalidation passed from `.tmp\bot_scenarios\match_item_policy_check\20260622T050722Z`, and the previous full implemented run passed from `.tmp\bot_scenarios\20260621T210229Z`.
  Stability update: mode `34` team-fire proof and modes `38`/`39` CTF carrier proofs now avoid teleporting live players during smoke setup; focused five-run stress loops passed for `team_fire_avoidance`, `ctf_carrier_support_route`, and `ctf_base_return_route` before that round's implemented-suite pass.
  Implementation logs: `docs-dev/q3a-botlib-perception-blackboard-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-dispatcher-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-brain-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-action-item-utility-2026-06-18.md`, `docs-dev/q3a-botlib-special-item-utility-2026-06-18.md`, `docs-dev/q3a-botlib-combat-weapon-metadata-2026-06-18.md`, `docs-dev/q3a-botlib-aim-fairness-policy-2026-06-18.md`, `docs-dev/q3a-botlib-live-aim-policy-integration-2026-06-18.md`, `docs-dev/q3a-botlib-live-combat-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-enemy-health-armor-estimates-2026-06-20.md`, `docs-dev/q3a-botlib-estimate-aware-weapon-selection-2026-06-20.md`, `docs-dev/q3a-botlib-carried-arsenal-selection-2026-06-20.md`, `docs-dev/q3a-botlib-nonweapon-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-utility-deployable-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-escape-deployable-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-safe-nuke-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-nuke-retreat-route-ownership-2026-06-21.md`, `docs-dev/q3a-botlib-timed-route-goal-owner-2026-06-21.md`, `docs-dev/q3a-botlib-teleporter-escape-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`, `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-progress-wait-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`, `docs-dev/q3a-botlib-coop-anti-blocking-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-target-share-2026-06-21.md`, `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-roam-route-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-team-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-fire-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-dropped-flag-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-carrier-support-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-base-return-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-policy-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-precedence-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-warmup-bot-start-readiness-2026-06-21.md`, `docs-dev/q3a-botlib-vote-bot-exclusion-2026-06-21.md`, `docs-dev/q3a-botlib-mymap-bot-queue-2026-06-21.md`, `docs-dev/q3a-botlib-queued-nextmap-transition-2026-06-21.md`, `docs-dev/q3a-botlib-mapvote-bot-exclusion-transition-2026-06-21.md`, `docs-dev/q3a-botlib-scoreboard-bot-classification-2026-06-21.md`, `docs-dev/q3a-botlib-intermission-bot-cleanup-2026-06-21.md`, `docs-dev/q3a-botlib-item-timer-fairness-2026-06-18.md`, `docs-dev/q3a-botlib-live-item-timing-consumers-2026-06-18.md`, `docs-dev/q3a-botlib-action-application-helpers-2026-06-18.md`, `docs-dev/q3a-botlib-weapon-inventory-command-api-2026-06-18.md`, `docs-dev/q3a-botlib-weapon-inventory-dispatch-2026-06-18.md`, `docs-dev/q3a-botlib-nav-health-armor-focus-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-helper-scaffold-2026-06-18.md`, `docs-dev/q3a-botlib-smoke-scenario-modes-2026-06-18.md`, `docs-dev/q3a-botlib-engage-enemy-proof-2026-06-18.md`, `docs-dev/q3a-botlib-weapon-switch-proof-2026-06-18.md`, `docs-dev/q3a-botlib-health-armor-pickup-proof-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-proof-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-depth-2026-06-18.md`, `docs-dev/q3a-botlib-ffa-tdm-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-team-coop-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`.
  Latest entity/scheduling/fairness behavior log: `docs-dev/q3a-botlib-entity-scheduling-fairness-closeout-2026-06-21.md`.
  Latest movement/recovery/inventory closeout log: `docs-dev/q3a-botlib-movement-recovery-inventory-closeout-2026-06-21.md`.
  Latest FFA item-role behavior log: `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`.
  Latest FFA role-combat behavior log: `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`.
  Latest TDM resource-denial behavior log: `docs-dev/q3a-botlib-team-resource-denial-2026-06-21.md`.
  Latest profile team-policy behavior log: `docs-dev/q3a-botlib-profile-team-policy-2026-06-22.md`.
  Latest profile item-policy behavior log: `docs-dev/q3a-botlib-profile-item-policy-2026-06-22.md`.
  Latest profile movement-policy behavior log: `docs-dev/q3a-botlib-profile-movement-policy-2026-06-22.md`.
  Latest bot chat-policy behavior log: `docs-dev/q3a-botlib-bot-chat-dispatch-2026-06-22.md`.
  Latest bot chat team-policy behavior log: `docs-dev/q3a-botlib-bot-chat-team-policy-2026-06-22.md`.
  Latest bot chat rate-policy behavior log: `docs-dev/q3a-botlib-bot-chat-rate-policy-2026-06-22.md`.
  Latest bot chat initial-policy behavior log: `docs-dev/q3a-botlib-bot-chat-initial-policy-2026-06-22.md`.
  Latest bot chat reply-policy behavior log: `docs-dev/q3a-botlib-bot-chat-reply-policy-2026-06-22.md`.
  Latest bot chat event-policy behavior log: `docs-dev/q3a-botlib-bot-chat-event-policy-2026-06-22.md`.
  Latest CTF item-role behavior log: `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`.
  Latest behavior policy log: `docs-dev/q3a-botlib-behavior-policy-umbrella-2026-06-22.md`.
  Latest profile role-policy log: `docs-dev/q3a-botlib-profile-role-policy-2026-06-22.md`.
  Latest match item-policy behavior log: `docs-dev/q3a-botlib-match-item-policy-2026-06-21.md`.
  Latest proof-hook logs: `docs-dev/q3a-botlib-combat-damage-event-hook-2026-06-18.md`, `docs-dev/q3a-botlib-gameplay-item-hooks-2026-06-18.md`, and `docs-dev/q3a-botlib-health-armor-scenario-promotion-gate-2026-06-18.md`.
- [ ] `FR-04-T16` Stage/package generated AAS assets or generator outputs and add bot/AAS smoke validation.
  Dependency: `FR-04-T11`, `FR-04-T14`. Priority: P1.
  Progress: First staging, staged-artifact audit, packaged-map extraction, package-readiness audit, archive packaging, refresh-install integration, and generic archive-member release validation slices are implemented. `q2aas-stage-aas` runs strict manifest validation, stages `.install\basew\maps\mm-rage.aas`, and writes `.tmp\q2aas\stage-report.json` with the staged path and SHA-256. `q2aas-stage-audit` verifies staged AAS path, size, and hashes against the stage report and writes `.tmp\q2aas\stage-audit-report.json`. `q2aas-package-map-smoke` validates pkz extraction/conversion from a scratch packaged `maps/mm-rage.bsp`. `q2aas-package-audit` verifies staged AAS release payload representation and writes `.tmp\q2aas\package-audit-report.json`. `q2aas-package-aas` injects `maps/mm-rage.aas` into `.install\basew\pak0.pkz`, writes `.tmp\q2aas\package-archive-report.json`, and `q2aas-package-archive-audit` verifies the packaged member under an archive-required policy. `validate_stage.py --required-archive-member` can require `maps/mm-rage.aas` by name and SHA-256, and `refresh_install.py --package-q2aas-aas` passes those q2aas archive requirements from the stage report while validating the `windows-x86_64` staged payload. Release packaging now also validates complete botfile families, package-member hashes, loose mirror hashes, valid q2aas SHA-256 archive expectations, q2aas/BSPC tool-binary default exclusion, and required license/credit notice sidecars. The available-reference inventory/validation flow can generate a focused manifest for currently staged assets and passes strict reference coverage for the available `mm-rage` subset. The dedicated frame-command smoke proves the refreshed install can load packaged `mm-rage.aas`, execute cached route-steered bot command frames, and exercise native route/goal debug overlay counters plus current-area/next-reachability/polyline/filter status fields from that runtime state. Broader reference-map coverage and future release-readiness automation remain pending.
  Reference-map packaging update: `q2aas-stage-aas` now stages eight generated `.aas` files for `mm-rage`, `q2dm1`, `q2dm2`, `q2dm8`, `q2ctf1`, `base1`, `base2`, and `train`, and `refresh_install.py --package-q2aas-aas` packages/audits all eight archive members after rebuilding `.install\basew\pak0.pkz`. Broader release-readiness automation and slime/lava-specific reference candidates remain pending.
  Persistent route-goal smoke also passes from the refreshed install, proving packaged `mm-rage.aas` can service a remembered preferred route goal through cadence refreshes.
  Legacy surface removal validation now passes through the same refreshed-install frame-command smoke, with `frames=8`, `commands=8`, `route_failures=0`, `route_goal_assignments=1`, `last_persistent_goal_area=227`, and `pass=1` after removing the Q2R `sgame/bots` helper files.
  Item route-goal smoke also passes from the refreshed install, proving packaged `mm-rage.aas` can service an active-pickup selected preferred route goal with `last_item_goal_area=415` and `pass=1`.
  Item reservation smoke also passes from the refreshed install, proving two bots can reserve separate active pickup route goals with `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `last_item_goal_reserved_entity=32`, and `pass=1`.
  Look-ahead steering smoke also passes from the refreshed install, proving staged binaries can steer against cached route-point payloads with `lookahead_attempts=17`, `lookahead_uses=9`, and `pass=1`.
  Velocity-aware steering smoke also passes from the refreshed install, proving staged binaries can apply projected-origin command yaw with `velocity_lead_attempts=17`, `velocity_lead_uses=3`, `last_velocity_lead_speed_sq=182`, `last_velocity_lead_offset_sq=1`, and `pass=1`.
  Stuck-repath smoke also passes from the refreshed install, proving staged binaries avoid false stuck detections during normal movement with `stuck_detections=0`, then detect forced no-progress command frames with `stuck_detections=2`, `stuck_repath_refreshes=2`, `last_stuck_reason=1`, and `pass=1`.
  Stuck recovery command smoke also passes from the refreshed install, proving normal movement keeps `stuck_recovery_activations=0` and `recovery_command_uses=0`, while forced no-progress command frames report `stuck_recovery_activations=2`, `stuck_recovery_frames=11`, `recovery_command_uses=11`, `last_recovery_forward_move=-80`, `last_recovery_side_move=-140`, and `pass=1`.
  Goal blacklist cooldown smoke also passes from the refreshed install, proving normal movement keeps `item_goal_blacklist_activations=0`, `item_goal_blacklist_skips=0`, and `item_goal_blacklist_active=0`, while forced no-progress command frames report `item_goal_blacklist_activations=2`, `item_goal_blacklist_skips=2`, `item_goal_blacklist_active=2`, `last_goal_clear_reason=5`, `route_failures=0`, and `pass=1`.
  Failed-goal reason smoke also passes from the refreshed install, proving normal movement keeps `failed_goal_events=0` and `last_failed_goal_reason=0`, while forced no-progress command frames report `failed_goal_events=2`, `last_failed_goal_reason=3`, `last_failed_goal_client=1`, `last_failed_goal_area=251`, `last_failed_goal_entity=74`, `last_failed_goal_item=2`, and `pass=1`.
  Movement-state command smoke also passes from the refreshed install, proving normal `mm-rage` route following remains walk-only with `movement_state_commands=0`, while forced smoke modes `5`, `6`, and `7` produce `movement_state_jump_commands=17`, `movement_state_crouch_commands=17`, and `movement_state_swim_commands=17` respectively with `pass=1`.
  Bot brain command-ownership smoke also passes from the refreshed install after moving command/status ownership behind `bot_brain.*`, with normal `frames=17`, `commands=17`, `route_failures=0`, forced jump `movement_state_jump_commands=17`, stalled `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, and all three modes reporting `pass=1`.
  Position route-goal smoke also passes from the refreshed install, proving staged binaries can route a debug world-position goal through packaged `maps/mm-rage.aas` with `position_goal_requests=8`, `position_goal_resolved=8`, `position_goal_assignments=1`, `item_goal_scans=0`, `route_goal_fallbacks=0`, `last_position_goal_area=227`, and `pass=1`.
  Natural travel-type goal smoke also passes from the refreshed install, proving staged binaries can select packaged-AAS `TRAVEL_JUMP`, `TRAVEL_LADDER`, direct `TRAVEL_BARRIERJUMP`, and route-only `TRAVEL_WALKOFFLEDGE` / `TRAVEL_ELEVATOR` routes without forced travel-type overrides; the barrier-jump run reports `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `last_reachability=319`, `last_reachability_type=4`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `travel_type_goal_start_warps=1`, `route_failures=0`, and `pass=1`.
  Rocket-jump route policy smoke also passes from the refreshed install, proving staged binaries default-block inherited rocket-jump reachability and opt into it only with `sg_bot_allow_rocketjump 1`; mode `14` reports `last_reachability_type=12`, `travel_type_goal_resolved=2`, `route_failures=0`, and `pass=1`, while mode `15` reports `travel_type_goal_expect_blocked=1`, `commands=0`, `route_commands=0`, `travel_type_goal_resolved=0`, `route_failures=8`, and `pass=1`.
  Four-bot frame-command smoke also passes from the refreshed install, proving staged binaries can add four bot clients, assign four active-pickup route goals, reserve four distinct item targets, and build route commands for all four with `frames=38`, `commands=38`, `route_commands=38`, `route_failures=0`, `route_goal_assignments=4`, `item_goal_assignments=4`, `item_goal_reservation_skips=6`, `item_goal_active_reservations=4`, and `pass=1`.
  Eight-bot frame-command smoke also passes from the refreshed install, proving staged binaries can add eight bot clients, keep eight active item reservations, and build route commands for all eight with `frames=92`, `commands=92`, `route_commands=92`, `route_failures=0`, `route_goal_assignments=11`, `item_goal_assignments=11`, `item_goal_reservation_skips=49`, `item_goal_active_reservations=8`, and `pass=1`.
  Ten-minute eight-bot soak also passes from the refreshed install, proving staged binaries can sustain eight fake-client bots through a long route-command run with `elapsed_ms=600001`, `frames=192036`, `commands=192036`, `route_commands=192036`, `route_failures=0`, `item_goal_reservation_skips=3455`, `item_goal_peak_active_reservations=2`, `skipped_inactive=0`, and `pass=1`.
  Map-change repeat smoke also passes from the refreshed install, proving staged binaries can repeat the eight-bot route-command proof across same-map reload with `cycles=2`, `map_changes=1`, `final_count=0`, and `pass=1`.

## Epic FR-05: Asset and Format Expansion
Objective: expand supported content formats without breaking current workflows.

Primary Areas: `src/renderer/*`, `src/rend_gl/*`, `src/rend_vk/*`, `inc/format/*`

Exit Criteria:
- Planned format support (IQM and extended BSP variants) has either landed or has approved implementation tracks with owners.

Tasks:
- [ ] `FR-05-T01` Build full format support matrix (current vs target) for MD2/MD3/MD5/IQM/BSP variants/DDS.
  Dependency: none. Priority: P0.
- [ ] `FR-05-T02` Define IQM implementation plan and shared loader boundaries.
  Dependency: `FR-05-T01`. Priority: P1.
- [ ] `FR-05-T03` Define extended BSP support plan (`IBSP29`, `BSP2`, `BSP2L`, `BSPX`) with compatibility rules.
  Dependency: `FR-05-T01`. Priority: P1.
- [ ] `FR-05-T04` Add renderer-side format fallback diagnostics for unsupported assets.
  Dependency: `FR-05-T01`. Priority: P2.
- [ ] `FR-05-T05` Add staged asset validation checks to packaging pipeline for new formats.
  Dependency: `DV-02-T04`. Priority: P1.
- [ ] `FR-05-T06` Add user-facing docs describing supported asset formats and caveats.
  Dependency: `FR-05-T01..T05`. Priority: P2.

## Epic FR-06: Audio, Feedback, and Accessibility
Objective: improve clarity and accessibility of gameplay feedback while preserving style.

Primary Areas: `src/client/sound/*`, `src/game/cgame/*`, localization and font docs

Exit Criteria:
- Critical feedback channels (audio cues, UI text, readability) are configurable and regression-tested.

Tasks:
- [ ] `FR-06-T01` Consolidate spatial audio follow-up backlog into implementation tasks.
  Dependency: none. Priority: P1.
- [ ] `FR-06-T02` Complete graphical obituaries/chatbox enhancement track and integrate with localization.
  Dependency: none. Priority: P1.
- [x] `FR-06-T03` Add accessibility pass for text backgrounds, scaling, contrast defaults, and fallback fonts.
  Dependency: none. Priority: P1.
- [ ] `FR-06-T04` Add presets for competitive readability vs immersive presentation.
  Dependency: `FR-06-T03`. Priority: P2.
- [ ] `FR-06-T05` Add QA script/checklist for multi-language font rendering in main HUD/menu surfaces.
  Dependency: `FR-06-T03`. Priority: P1.
- [x] `FR-06-T06` Implement first-wave spatial audio consolidation defaults, reverb-send decoupling, and built-in environment fallback.
  Dependency: `FR-06-T01`. Priority: P1.
- [x] `FR-06-T07` Implement second-wave spatial audio acoustic material resolver with `.mat` ID keyed banded coefficients.
  Dependency: `FR-06-T06`. Priority: P1.
- [x] `FR-06-T08` Implement third-wave spatial audio BSP acoustic regions for region-aware reverb selection.
  Dependency: `FR-06-T07`. Priority: P1.
- [x] `FR-06-T09` Implement fourth-wave spatial audio portal-aware propagation through one- and two-hop areaportal routes.
  Dependency: `FR-06-T08`. Priority: P1.
- [x] `FR-06-T10` Implement fifth-wave spatial audio two-identity listener/source room path model.
  Dependency: `FR-06-T09`. Priority: P1.

## Epic FR-07: Multiplayer Operations and Match Tooling
Objective: harden map vote, match logging, tournament, and admin workflows.

Primary Areas: `src/game/sgame/match/*`, `src/game/sgame/menu/*`, `src/game/sgame/commands/*`

Exit Criteria:
- Admin and competitive flows are stable across map transitions and match-state changes.

Tasks:
- [ ] `FR-07-T01` Add end-to-end validation scenarios for map vote, mymap queue, and nextmap transitions.
  Dependency: none. Priority: P1.
  Progress: `vote_bot_exclusion` covers bot-origin vote exclusion, `mymap_queue_bot_request` covers bot-attributed MyMap queue insertion plus `ConsumeQueuedMap` cleanup through `sv_bot_mymap_smoke 2`, `queued_nextmap_transition` covers bot-attributed queued-map insertion, queue consumption, and observed same-map `gamemap` reload through `sv_bot_nextmap_smoke 2`, `mapvote_bot_exclusion_transition` covers bot-only map selector setup, blocked bot ballots, deterministic selector finalization, observed same-map reload, and cleanup through `sv_bot_mapvote_smoke 2`, `scoreboard_bot_classification` covers bot-only sorted standings through `sv_bot_scoreboard_smoke 2`, and `intermission_bot_cleanup` covers native bot-only intermission entry plus fake-client/sorted-client cleanup through `sv_bot_intermission_smoke 2`. Tournament veto bot-exclusion and replay reset are covered under `FR-07-T02`; competitive admin flows remain under later FR-07 work.
- [ ] `FR-07-T02` Harden tournament veto/replay flows with explicit error handling and state resets.
  Dependency: none. Priority: P1.
  Progress: `tournament_bot_veto_exclusion` covers the tournament veto bot boundary through `sv_bot_tournament_smoke 2`, assigns a bot the active home-side tournament identity, attempts a veto pick, requires `reason=bot_blocked` with `allowed=0`, and verifies `picks=0` and `bans=0` after cleanup. `tournament_replay_reset` covers replay reset/error handling through `sv_bot_tournament_smoke 3`, rejects out-of-range game `99` with `reason=range_error` and `preserved=1`, then replays game `2` with `reset_applied=1`, one retained winner/map/id, wins rewound to `1-0`, and the series reopened.
- [x] `FR-07-T03` Improve match logging artifact schema/versioning for downstream tooling.
  Dependency: none. Priority: P2.
  Progress: Match-stats and tournament-series JSON artifacts now advertise top-level schema/artifact metadata (`worr.match_stats` and `worr.tournament_series`, version `1`). Successful exports also update `basew/matches/catalog.json` as `worr.match_catalog` version `1`, with relative artifact paths and latest-artifact IDs for downstream tools. `match_logging_schema` validates these fields through `sv_bot_matchlog_smoke 2`, requiring retained players/event-log/matches arrays, embedded match schema metadata, catalog artifact count, latest pointers, relative JSON paths, scratch catalog write/read proof, and final zero-bot cleanup.
- [ ] `FR-07-T04` Add command-level audit for vote/admin privileges and abuse controls.
  Dependency: none. Priority: P1.
  Progress: `admin_bot_privilege_audit` covers the bot/admin command boundary through `sv_bot_admin_audit_smoke 2`, temporarily grants a bot admin session bit, attempts the registered `lock_team red` command, requires `reason=bot_admin_blocked` with `executed=0`, and verifies `red_locked=0` after cleanup. Vote-origin bot privilege checks are covered by `vote_bot_exclusion`; the public operator-facing command/cvar pass is covered by `FR-07-T05`.
- [x] `FR-07-T05` Add server-operator docs for new competitive tooling and expected cvars.
  Dependency: `FR-07-T01..T04`. Priority: P2.
  Progress: `docs-user/competitive-server-tools.md` documents warmup/bot-practice cvars, voting, MyMap, queued nextmap, map selector, Duel queue, tournament veto/replay, admin controls, match logging cvars, and the bot-boundary notes proven by the recent scenario suite. It is linked from `docs-user/server-quickstart.md` and `docs-user/server.asciidoc`, with the implementation/provenance note in `docs-dev/q3a-botlib-competitive-server-tools-docs-2026-06-21.md`.

## Epic FR-08: Online Ecosystem Foundations
Objective: prepare for web integration, identity, and service coupling without destabilizing core runtime.

Primary Areas: updater, release index tooling, future external services

Exit Criteria:
- Online roadmap is decomposed into incremental, testable tasks with security and reliability guardrails.

Tasks:
- [ ] `FR-08-T01` Define service boundary document for engine, game module, updater, and external web services.
  Dependency: none. Priority: P1.
- [ ] `FR-08-T02` Define authentication and identity model (Discord OAuth or alternative) with threat model.
  Dependency: `FR-08-T01`. Priority: P2.
- [ ] `FR-08-T03` Define server browser data contract between in-game UI and backend service.
  Dependency: `FR-08-T01`. Priority: P2.
- [x] `FR-08-T04` Define CDN/update channel strategy aligned with existing release index format.
  Dependency: none. Priority: P2.
- [ ] `FR-08-T05` Stage a minimal public server deployment runbook and monitoring checklist.
  Dependency: `FR-08-T01`. Priority: P2.

## Development Roadmap (Task-Based Project)

## Timeline
- Phase D1 (2026-03-01 to 2026-04-30): governance and quality-gate foundation
- Phase D2 (2026-05-01 to 2026-08-31): test automation, architecture cleanup, and CI scale-up
- Phase D3 (2026-09-01 to 2026-12-31): hardening, sustainability, and release excellence

## Epic DV-01: Project Governance and Team Workflow
Objective: make project tracking mandatory and consistent across all major work.

Primary Areas: `AGENTS.md`, `README.md`, `docs-dev/projects` process docs

Exit Criteria:
- All significant initiatives are tracked with epic/task IDs and lifecycle states.

Tasks:
- [ ] `DV-01-T01` Establish canonical project board template and required fields.
  Dependency: none. Priority: P0.
- [ ] `DV-01-T02` Define naming conventions for epics/tasks/milestones and enforce in docs.
  Dependency: `DV-01-T01`. Priority: P0.
- [ ] `DV-01-T03` Define WIP limits and escalation rules for blocked tasks.
  Dependency: `DV-01-T01`. Priority: P1.
- [ ] `DV-01-T04` Add project status review ritual (weekly) with owners and outputs.
  Dependency: `DV-01-T01`. Priority: P1.
- [ ] `DV-01-T05` Require roadmap task references in major PR descriptions and dev docs.
  Dependency: `DV-01-T02`. Priority: P0.

## Epic DV-02: CI and Validation Pipeline Expansion
Objective: move from release-focused automation to continuous confidence for day-to-day development.

Primary Areas: `.github/workflows/*`, `tools/release/*`, build scripts

Exit Criteria:
- Every non-trivial change path has automated build/test/smoke coverage before merge.

Tasks:
- [ ] `DV-02-T01` Add PR CI workflow for configure + compile on Windows/Linux/macOS.
  Dependency: none. Priority: P0.
- [ ] `DV-02-T02` Add matrix targets for external renderer libraries (`opengl`, `vulkan`, `rtx`) in CI.
  Dependency: `DV-02-T01`. Priority: P0.
- [ ] `DV-02-T03` Add runtime smoke launch checks against `.install/` staging for each platform.
  Dependency: `DV-02-T01`. Priority: P1.
- [ ] `DV-02-T04` Add staged payload validation for format/manifest completeness in PR CI.
  Dependency: `DV-02-T01`. Priority: P1.
- [ ] `DV-02-T05` Add failure triage guide and flaky test quarantine workflow.
  Dependency: `DV-02-T01`. Priority: P2.
- [x] `DV-02-T06` Add renderer guardrail scans for removed shadow fallback/cache paths.
  Dependency: `DV-02-T01`. Priority: P1.

## Epic DV-03: Automated Test Strategy
Objective: expand meaningful automated tests across protocol, gameplay, renderer, and tooling.

Primary Areas: `q2proto/tests`, `src/common/tests.c`, future test harness paths

Exit Criteria:
- Core regression-prone systems are covered by deterministic tests and smoke checks.

Tasks:
- [ ] `DV-03-T01` Integrate `q2proto/tests` into main CI path and publish result artifacts.
  Dependency: `DV-02-T01`. Priority: P0.
- [ ] `DV-03-T02` Add unit-level tests for high-risk shared utilities (`files`, parsing, cvar helpers).
  Dependency: none. Priority: P1.
- [ ] `DV-03-T03` Add deterministic server game rule tests for match-state transitions.
  Dependency: none. Priority: P1.
- [ ] `DV-03-T04` Add renderer smoke scenes with pixel/hash tolerance checks for key features.
  Dependency: `DV-02-T03`. Priority: P1.
- [x] `DV-03-T05` Add bot scenario tests for spawn, navigation, and objective behavior.
  Dependency: `FR-04-T02`. Priority: P2.
  Progress: `tools/bot_scenarios/run_bot_scenarios.py` now reports 77 implemented catalog rows total: 76 automated short-run bot scenario smokes plus one manual high-bot soak/degradation row. Server smoke modes `20` through `71`, mode `19` map-change/map-restart rows, the coop mode `3`/`12` reuse rows, and the dedicated warmup/vote/admin/tournament/matchlog/MyMap/nextmap/mapvote/scoreboard/intermission smokes validate through frame-command, blackboard, action, objective, nav, match-readiness, coop-readiness, coop-command, team-policy, behavior-policy, match-flow, and source-counter markers. The latest full automated `implemented` run passed 76/76 rows from `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`; focused validation also passed for `behavior_policy_umbrella` in mode `52`, `profile_role_policy` in mode `53`, `profile_team_policy` in mode `54`, `profile_item_policy` in mode `55`, `profile_movement_policy` in mode `56`, `bot_chat_policy` in mode `57`, `bot_chat_team_policy` in mode `58`, `bot_chat_rate_policy` in mode `59`, `bot_chat_initial_policy` in mode `60`, `bot_chat_reply_policy` in mode `61`, `bot_chat_event_policy` in mode `62`, `behavior_arbitration` in mode `63`, `target_memory_decay` in mode `64`, `weapon_scoring_arsenal` in mode `65`, `aim_fire_policy_depth` in mode `66`, `ammo_pressure_pickup` in mode `67`, `survival_inventory_use` in mode `68`, `survival_health_route` in mode `69`, `survival_armor_route` in mode `70`, and `combat_survival_regression` in mode `71`. That green aggregate is the active baseline for future live behavior work.
  Latest scenario update: the promoted `behavior_policy_umbrella` row uses mode `52` with `sg_bot_behavior_enable 1`, hard-gates umbrella TDM role-route, role-combat, friendly-fire, and match item-policy activation through runtime status markers, and verifies the individual proof cvars remain disabled in the begin marker. The row passed focused validation from `.tmp\bot_scenarios\behavior_policy_umbrella\20260622T050833Z`; `match_item_policy` mode `51` was revalidated from `.tmp\bot_scenarios\match_item_policy_check\20260622T050722Z`; `profile_team_policy` mode `54` passed from `.tmp\bot_scenarios\profile_team_policy\20260622T055119Z`; `profile_item_policy` mode `55` passed from `.tmp\bot_scenarios\profile_item_policy\20260622T062835Z`; `profile_movement_policy` mode `56` passed from `.tmp\bot_scenarios\profile_movement_policy\20260622T070032Z`; `bot_chat_policy` mode `57` passed from `.tmp\bot_scenarios\20260622T080531Z`; `bot_chat_team_policy` mode `58` passed from `.tmp\bot_scenarios\20260622T080044Z`; `bot_chat_rate_policy` mode `59` passed from `.tmp\bot_scenarios\20260622T081428Z`; `bot_chat_initial_policy` mode `60` passed from `.tmp\bot_scenarios\20260622T085845Z`; `bot_chat_reply_policy` mode `61` passed from `.tmp\bot_scenarios\20260622T092009Z`; `bot_chat_event_policy` mode `62` passed from `.tmp\bot_scenarios\20260622T093637Z`; `behavior_arbitration` mode `63` passed from `.tmp\bot_scenarios\20260622T112202Z`; `target_memory_decay` mode `64` passed from `.tmp\bot_scenarios\20260622T120742Z`; `weapon_scoring_arsenal` mode `65` passed from `.tmp\bot_scenarios\20260622T123648Z`; `aim_fire_policy_depth` mode `66` passed from `.tmp\bot_scenarios\20260622T125826Z`; `ammo_pressure_pickup` mode `67` passed from `.tmp\bot_scenarios\ammo_pressure_pickup\20260622T132231Z`; `survival_inventory_use` mode `68` passed from `.tmp\bot_scenarios\survival_inventory_use\20260622T161739Z`; `survival_health_route` mode `69` passed from `.tmp\bot_scenarios\survival_health_route\20260622T164109Z`; and `survival_armor_route` mode `70` passed from `.tmp\bot_scenarios\survival_armor_route\20260622T165918Z`.
  Latest promotion update: raw diagnostics preserve marker event order and latest source lines, missing metrics report their expected source marker, prior blocked mode `22` diagnostics are superseded by the promoted `health_armor_pickup` row, CTF objective hooks feed the implemented `team_objective` smoke, the default pending rows are promoted into source-backed gates, `sv_bot_team_policy_smoke 3` hard-gates duel queue/spectator handling through queued spectator and cleanup status evidence, `map_restart_cleanup` hard-gates the mode `19` forced restart path through `command=map_force`, `restart=1`, observed reload, cleanup-status, and final zero-bot completion markers, `warmup_bot_start_readiness` hard-gates `sv_bot_warmup_smoke 2` through accepted bot add requests, two-bot ready-up status, `minplayers_met=1`, `bot_only_start=1`, `can_start=1`, and final zero-bot cleanup, `vote_bot_exclusion` hard-gates `sv_bot_vote_smoke 2` through bot-only `voting_clients=0`, explicit `bot_blocked` vote-launch rejection, and final no-active-vote cleanup, `admin_bot_privilege_audit` hard-gates `sv_bot_admin_audit_smoke 2` through a bot-only setup, temporary forced admin session state, registered `lock_team red` command lookup, `reason=bot_admin_blocked`, `executed=0`, and final unlocked red-team cleanup, `tournament_bot_veto_exclusion` hard-gates `sv_bot_tournament_smoke 2` through bot-only tournament setup, active-side bot identity, veto pick denial, `reason=bot_blocked`, and final zero-pick/zero-ban cleanup, `tournament_replay_reset` hard-gates `sv_bot_tournament_smoke 3` through completed-series setup, invalid game `99` rejection with `reason=range_error` and `preserved=1`, valid game `2` replay with `reset_applied=1`, one retained winner/map/id, and reopened series state, `match_logging_schema` hard-gates `sv_bot_matchlog_smoke 2` through match-stats and tournament-series schema names, version `1`, artifact types, retained array shape, embedded match metadata, and final zero-bot cleanup, `mymap_queue_bot_request` hard-gates `sv_bot_mymap_smoke 2` through bot-only MyMap enablement, deterministic social attribution, queue insertion into `playQueue` and `myMapQueue`, `ConsumeQueuedMap` cleanup, and final zero-bot/no-queue status, `queued_nextmap_transition` hard-gates `sv_bot_nextmap_smoke 2` through bot-attributed queued-map insertion, play/MyMap queue consumption, successful queued transition, observed same-map reload, retained transition status, and final zero-bot cleanup, `mapvote_bot_exclusion_transition` hard-gates `sv_bot_mapvote_smoke 2` through bot-only selector setup, explicit `bot_blocked` ballot rejection with zero counted votes, selected-current-map finalization, observed same-map reload, retained finalize status, and final zero-bot cleanup, `scoreboard_bot_classification` hard-gates `sv_bot_scoreboard_smoke 2` through two bot-only sorted standings rows, zero voting clients, deterministic 7/3 scores, bot-owned leader and runner-up rows, ordered ranks, and final zero-bot cleanup, `intermission_bot_cleanup` hard-gates `sv_bot_intermission_smoke 2` through native intermission entry, frozen/freecam/non-solid bot state, current-map transition targeting, and final zero-bot/zero-connected/zero-sorted-client cleanup, the follow-on `coop_leader_route` reuse row hard-gates leader-route ownership, mode `27` hard-gates coop LeadAdvance route ownership, mode `28` hard-gates coop resource-share policy plus item reserved-deferral evidence, mode `29` hard-gates close-leader coop anti-blocking command ownership, mode `30` hard-gates support-policy coop target-sharing adoption, mode `31` hard-gates coop door/elevator source ownership plus teammate hold evidence, mode `32` hard-gates TDM role/lane route ownership from live match policy, mode `33` hard-gates TDM item-role pickup scoring from live match item-role policy, mode `34` hard-gates TDM friendly-fire suppression, mode `35` hard-gates CTF role-route ownership from live match role/lane policy, mode `36` hard-gates CTF role-combat ownership from visible, shootable target facts, mode `37` hard-gates CTF dropped-flag response route ownership from dropped enemy flag target-source facts, mode `38` hard-gates CTF carrier-support route ownership from same-team flag-carrier target-source facts, mode `39` hard-gates CTF base-return route ownership from enemy own-flag carrier target-source facts, mode `40` hard-gates combined CTF objective-route policy ownership from candidate, selection, deferral, and route-command evidence, mode `41` hard-gates objective-route precedence with role-route deferral, zero role-route activations, and objective-route command evidence, mode `42` hard-gates FFA roam-route ownership from match-policy selection, timed owner kind, route activation, and route-request evidence, mode `43` hard-gates TDM role-combat ownership from match-policy selection, visible/shootable target facts, and attack-button evidence, mode `44` hard-gates TDM role-combat/friendly-fire precedence with role-combat attack decisions, friendly-fire evaluations, friendly-line blocks, and final blocked-state metadata, mode `45` hard-gates FFA spawn-camp avoidance with anti-camp policy selection, live source selection, timed route-goal ownership, and route-request evidence, mode `46` hard-gates FFA item-role pickup scoring from objective item-role policy and nav score-boost evidence, mode `47` hard-gates CTF item-role pickup scoring from CTF readiness, objective item-role policy, nav score boosts, selected pickup goals, invalid-skip absence, and latest role/item metadata, mode `48` hard-gates FFA role-combat ownership from match-policy selection, visible/shootable target facts, and attack-button evidence, mode `49` hard-gates FFA spawn-camp combat avoidance with role-combat attack decisions, anti-camp source selection, same-source target blocks, and final blocked-state metadata, and mode `50` hard-gates TDM resource-denial pickup scoring from deny-enemy resource policy, nav score boosts, selected denial-shaped pickup goals, and latest resource intent metadata.
  Latest behavior policy promotion update: mode `52` hard-gates `sg_bot_behavior_enable` while the individual behavior proof cvars stay disabled; mode `51` remains the live item-goal selection proof for the match item-policy umbrella, mode `53` proves profile `WORR_ROLE` metadata feeds match-policy requested-role selection, mode `54` proves profile teamplay/objective/friendly-fire-care metadata feeds CTF match-policy bonuses, mode `55` proves profile item-greed/item-denial/powerup-timing/retreat-health metadata feeds TDM match item/resource policy bonuses, mode `56` proves profile `WORR_MOVEMENT_STYLE` metadata feeds TDM match-policy movement bonuses, mode `57` proves profile chat metadata plus `sg_bot_allow_chat` while submitting a conservative live dispatch, mode `58` proves `sg_bot_chat_team_only`, mode `59` proves `sg_bot_chat_min_interval_ms` rate limiting without dispatch failures, mode `60` proves profile chat-personality initial utterance selection before live dispatch, mode `61` proves smoke-gated profile chat-personality reply selection for the first team-ready event, mode `62` proves smoke-gated profile chat-personality reply selection across team-ready and route-ready proof events, mode `63` proves central live behavior arbitration plus behavior-policy cvar classification, mode `64` proves target-memory retention and decay, mode `65` proves weapon-scoring arsenal depth, mode `66` proves aim/fire policy depth, mode `67` proves ammo-pressure pickup routing, mode `68` proves survival inventory use, mode `69` proves survival health routing, and mode `70` proves survival armor routing.
  Implementation logs: `docs-dev/q3a-botlib-scenario-smoke-harness-2026-06-18.md`, `docs-dev/q3a-botlib-profile-scenario-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-pending-scenario-counters-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-pending-gap-report-2026-06-18.md`, `docs-dev/q3a-botlib-smoke-scenario-modes-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotions-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-raw-reserved-diagnostics-2026-06-18.md`, `docs-dev/q3a-botlib-nav-health-armor-focus-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-helper-scaffold-2026-06-18.md`, `docs-dev/q3a-botlib-pending-scenario-promotion-tooling-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`, `docs-dev/q3a-botlib-status-harness-expansion-2026-06-18.md`, `docs-dev/q3a-botlib-duel-queue-spectator-2026-06-21.md`, `docs-dev/q3a-botlib-map-restart-cleanup-2026-06-21.md`, `docs-dev/q3a-botlib-warmup-bot-start-readiness-2026-06-21.md`, `docs-dev/q3a-botlib-vote-bot-exclusion-2026-06-21.md`, `docs-dev/q3a-botlib-admin-bot-privilege-audit-2026-06-21.md`, `docs-dev/q3a-botlib-tournament-bot-veto-exclusion-2026-06-21.md`, `docs-dev/q3a-botlib-mymap-bot-queue-2026-06-21.md`, `docs-dev/q3a-botlib-queued-nextmap-transition-2026-06-21.md`, `docs-dev/q3a-botlib-mapvote-bot-exclusion-transition-2026-06-21.md`, `docs-dev/q3a-botlib-scoreboard-bot-classification-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`, `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`, `docs-dev/q3a-botlib-coop-anti-blocking-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-target-share-2026-06-21.md`, `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-roam-route-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-combat-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-team-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-fire-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-dropped-flag-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-carrier-support-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-base-return-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-policy-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-precedence-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-match-item-policy-2026-06-21.md`, `docs-dev/q3a-botlib-docs-progress-tracking-round-2026-06-18.md`, `docs-dev/q3a-botlib-extensive-implementation-round-2026-06-18.md`.
  Latest replay implementation log: `docs-dev/q3a-botlib-tournament-replay-reset-2026-06-21.md`.
  Latest competitive server docs log: `docs-dev/q3a-botlib-competitive-server-tools-docs-2026-06-21.md`.
  Latest match logging catalog log: `docs-dev/q3a-botlib-match-logging-catalog-2026-06-21.md`.
  Latest aim/fire policy depth log: `docs-dev/q3a-botlib-aim-fire-policy-depth-2026-06-22.md`.
  Latest ammo pressure pickup log: `docs-dev/q3a-botlib-ammo-pressure-pickup-2026-06-22.md`.
  Latest FFA item-role scenario log: `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`.
  Latest FFA role-combat scenario log: `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`.
  Latest FFA spawn-camp combat-avoidance scenario log: `docs-dev/q3a-botlib-ffa-spawn-camp-combat-avoidance-2026-06-21.md`.
  Latest profile team-policy scenario log: `docs-dev/q3a-botlib-profile-team-policy-2026-06-22.md`.
  Latest profile item-policy scenario log: `docs-dev/q3a-botlib-profile-item-policy-2026-06-22.md`.
  Latest profile movement-policy scenario log: `docs-dev/q3a-botlib-profile-movement-policy-2026-06-22.md`.
  Latest bot chat-policy scenario log: `docs-dev/q3a-botlib-bot-chat-dispatch-2026-06-22.md`.
  Latest bot chat team-policy scenario log: `docs-dev/q3a-botlib-bot-chat-team-policy-2026-06-22.md`.
  Latest bot chat rate-policy scenario log: `docs-dev/q3a-botlib-bot-chat-rate-policy-2026-06-22.md`.
  Latest bot chat initial-policy scenario log: `docs-dev/q3a-botlib-bot-chat-initial-policy-2026-06-22.md`.
  Latest bot chat reply-policy scenario log: `docs-dev/q3a-botlib-bot-chat-reply-policy-2026-06-22.md`.
  Latest bot chat event-policy scenario log: `docs-dev/q3a-botlib-bot-chat-event-policy-2026-06-22.md`.
  Latest CTF item-role scenario log: `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`.
  Latest scenario-tooling logs: `docs-dev/q3a-botlib-scenario-tooling-source-aware-raw-diagnostics-2026-06-18.md`, `docs-dev/q3a-botlib-health-armor-scenario-promotion-gate-2026-06-18.md`, and `docs-dev/q3a-botlib-ctf-objective-gameplay-hooks-2026-06-18.md`.
- [x] `DV-03-T06` Add updater/release index parser tests for stable and nightly channels.
  Dependency: none. Priority: P1.

## Epic DV-04: Architecture and Code Quality
Objective: reduce maintenance overhead and complete key modernization tracks.

Primary Areas: `meson.build`, `src/client/*`, `src/game/*`, naming policy docs

Exit Criteria:
- Module boundaries are cleaner, duplication is reduced, and coding standards are enforceable.

Tasks:
- [ ] `DV-04-T01` Define C/C++ migration target map with boundaries and no-go zones.
  Dependency: none. Priority: P1.
- [ ] `DV-04-T02` Complete client/cgame ownership map for duplicated behavior paths.
  Dependency: none. Priority: P1.
  Progress: First-person viewweapon pose calculation now has a cgame-local `cg_view.cpp` helper used by both the weapon entity and local beam starts, reducing drift between duplicated cgame view paths.
- [ ] `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.
  Dependency: `DV-02-T01`. Priority: P1.
- [ ] `DV-04-T04` Create cvar namespace modernization plan (`g_` to `sg_` for new server-side controls).
  Dependency: none. Priority: P1.
- [ ] `DV-04-T05` Track and burn down top 100 first-party TODO/FIXME markers by severity.
  Dependency: none. Priority: P1.
- [ ] `DV-04-T06` Add subsystem ownership map (maintainers by directory) for faster review routing.
  Dependency: none. Priority: P2.

## Epic DV-05: Performance and Observability
Objective: establish measurable performance baselines and regression visibility.

Primary Areas: renderers, server frame loop, profiling/logging tools

Exit Criteria:
- Baseline metrics exist and regressions can be identified quickly in development and CI.

Tasks:
- [ ] `DV-05-T01` Define canonical benchmark scenes/maps for renderer and gameplay performance checks.
  Dependency: none. Priority: P1.
- [ ] `DV-05-T02` Add standardized perf capture commands and output schema.
  Dependency: `DV-05-T01`. Priority: P1.
  Progress: `tools/bot_perf/analyze_bot_perf.py` now parses bot frame-command smoke logs, supports text/JSON/CSV output, accepts scenario report sidecars for duration metadata, emits multi-run comparison summaries, warns when comparisons mix scenario names, bot counts, duration sources, or missing duration data, can write Markdown reports under `.tmp/bot_perf/`, and derives CPU/visibility/trace/memory metrics when source-counter fields appear in status output. The import/adapter layer now emits split route-build, PVS/PHS, visibility-decompression, entity-trace, entity-clip, static BSP trace, BotLib memory, bot-frame CPU, route CPU, Q3A route CPU, static BSP CPU, and entity-clip CPU source-counter status for implemented scenario parsing. The scenario harness now also reports optional status-field discoveries so new action/aim/item/route status fields can be observed before becoming hard budget or promotion gates. The analyzer now exposes source-counter group pass/fail fields and `missing_current_counters` diagnostics for long-soak logs that predate current source-counter fields. The latest `engage_enemy` perf parse with the scenario sidecar reports no missing source-counter groups and derives bot-frame, route-query, route-reuse, Q3A-route, static-BSP, entity-clip, visibility, and Q3A-memory fields. `tools/bot_perf/README.md` documents quickstart, scenario glob input, comparison, budget, guard warnings, and test commands.
  Implementation logs: `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`, `docs-dev/q3a-botlib-source-counter-plumbing-2026-06-18.md`, `docs-dev/q3a-botlib-route-cpu-timing-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`, `docs-dev/q3a-botlib-static-bsp-trace-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-entity-trace-clip-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-aas-memory-source-counters-2026-06-18.md`, `docs-dev/q3a-botlib-status-harness-expansion-2026-06-18.md`, `docs-dev/q3a-botlib-high-bot-soak-budget-2026-06-18.md`, `docs-dev/q3a-botlib-long-soak-source-counter-round-2026-06-18.md`.
- [ ] `DV-05-T03` Add lightweight frame-time and subsystem timing instrumentation toggles.
  Dependency: none. Priority: P1.
  Progress: OpenGL now exposes the first renderer baseline through `gl_cpu_timers`, `gl_gpu_timers`, `gl_profile_log`, `gl_debug_markers`, `gl_telemetry`, and renderer stats. Bot smoke log tooling now records derived command, route, debug, recovery, and scenario rates from existing status lines, merges split `q3a_bot_source_counter_status` fields, and now consumes emitted bot-frame CPU, route CPU, Q3A route CPU, Q3A memory, visibility, static BSP trace, static BSP CPU, entity-trace, and entity-clip CPU source counters. Fresh long-soak CPU baselines remain pending because the current ten-minute soak fixture predates the new source-counter fields.
- [ ] `DV-05-T04` Add nightly trend report for key performance metrics.
  Dependency: `DV-05-T02`. Priority: P2.
- [ ] `DV-05-T05` Add performance budget thresholds for major renderer and server paths.
  Dependency: `DV-05-T01`. Priority: P2.
  Progress: `tools/bot_perf/default_soak_budget.json` defines a generous baseline budget for the current ten-minute eight-bot `mm-rage` route-command soak. The analyzer returns a failing exit code for required threshold failures and the current soak fixture passes all default checks. The scenario harness now exposes a manual `high_bot_soak_degradation` row for mode `18`; the soak budget records preserved high-bot invariants and keeps CPU source-counter checks optional until a fresh ten-minute source-counter soak exists. Optional budgets can target split source-counter metrics such as `aas_inpvs_checks_per_bot_sec`, `bsp_trace_calls_per_bot_sec`, `bot_frame_cpu_ms_per_bot_sec`, `route_query_cpu_ms_per_bot_sec`, `q3a_route_cpu_ms_per_bot_sec`, `bsp_trace_cpu_ms_per_bot_sec`, and `entity_trace_clip_cpu_ms_per_sec`. The latest diagnostics surface optional missing source-counter groups explicitly instead of burying them in warning prose; fresh source-counter long-soak baselines remain pending. Comparison guards are warnings only, so strict regression gates should compare like-for-like scenarios until broader source-counter baselines exist.
  Implementation logs: `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`, `docs-dev/q3a-botlib-source-counter-plumbing-2026-06-18.md`, `docs-dev/q3a-botlib-route-cpu-timing-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`, `docs-dev/q3a-botlib-static-bsp-trace-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-entity-trace-clip-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-high-bot-degradation-policy-2026-06-18.md`, `docs-dev/q3a-botlib-high-bot-soak-budget-2026-06-18.md`, `docs-dev/q3a-botlib-long-soak-source-counter-round-2026-06-18.md`.

## Epic DV-06: Dependency Lifecycle and Security Hygiene
Objective: reduce dependency sprawl and improve update confidence.

Primary Areas: `subprojects/`, Meson wraps, release/build docs

Exit Criteria:
- Dependency versions are intentional, documented, and reviewable with lower drift risk.

Tasks:
- [ ] `DV-06-T01` Audit duplicate vendored versions and define active baseline per dependency.
  Dependency: none. Priority: P0.
- [ ] `DV-06-T02` Remove or archive superseded dependency trees not needed for reproducible builds.
  Dependency: `DV-06-T01`. Priority: P1.
- [ ] `DV-06-T03` Add dependency update checklist including security notes and regression tests.
  Dependency: `DV-06-T01`. Priority: P1.
- [ ] `DV-06-T04` Add monthly dependency maintenance review cadence and owner.
  Dependency: `DV-06-T01`. Priority: P2.

## Epic DV-07: Documentation Quality and Traceability
Objective: keep docs synchronized with implementation and projects.

Primary Areas: `docs-dev/`, `docs-user/`, root docs

Exit Criteria:
- Significant implementation changes have corresponding current docs with task references.

Tasks:
- [ ] `DV-07-T01` Add docs freshness audit for architecture docs that reference moved/renamed paths.
  Dependency: none. Priority: P1.
- [ ] `DV-07-T02` Require task ID linkage in all new significant `docs-dev` change logs.
  Dependency: `DV-01-T02`. Priority: P1.
- [ ] `DV-07-T03` Add concise subsystem index pages (`renderer`, `game`, `build`, `release`) for discoverability.
  Dependency: none. Priority: P2.
- [ ] `DV-07-T04` Add user-doc parity pass whenever user-visible cvars/features are changed.
  Dependency: none. Priority: P1.
  Progress: `docs-user/client.asciidoc` documents `cg_weapon_bob`, its disabled/Quake 3/Doom 3 values, and the legacy `cg_weaponBob` alias. Bot user docs now also cover practical setup/debug cvars, packaged botfile locations, current AAS/map readiness limits, available-reference validation, and the item timer controls `sg_bot_allow_item_timers` / `sg_bot_item_timer_fuzz_ms`. `docs-user/competitive-server-tools.md` adds operator parity for the competitive server cvars and commands under `FR-07-T05`, including the `basew/matches/catalog.json` match logging index added under `FR-07-T03`.
- [x] `DV-07-T05` Keep the canonical shadowmapping replacement baseline synchronized with implementation status.
  Dependency: `FR-02-T09`. Priority: P1.
- [ ] `DV-07-T06` Maintain imported-source credits and provenance ledgers for the Q3A BotLib and `TTimo/bspc` AAS work.
  Dependency: `FR-04-T10`. Priority: P0.
  Progress: `docs-dev/q3a-botlib-aas-credits.md` now tracks initial source baselines, contributors, candidate files, import requirements, the `tools/q2aas/` `TTimo/bspc` vendor snapshot, modified imported BSPC files, WORR-native q2aas build/config/validation/trace-bridge/manifest-schema/manifest-smoke/metadata/diagnostic-gate/baseline-gate/AAS-staging/stage-audit/packaged-map-smoke/archive-guardrail/package-audit/archive-packaging/refresh-install/stage-archive-validation files, the WORR-native BotLib/AAS runtime shell, the WORR-native Q3A BotLib import boundary, the Q3A utility imports, the Q3A AAS file-loader imports, the Q3A AAS sampling import, the Q3A AAS reachability import, the Q3A AAS route/CRC import with per-file pinned hashes, the Q3A AAS alternative-route import, the Q3A AAS entity-cache import, the WORR-owned Q3A AAS entity sync and entity trace bridges, the WORR-owned Q3A bridge time/vector helper work, the WORR-owned active-map Q2 BSP entity-lump bridge, the WORR-owned active-map Q2 BSP model-lump bridge, the WORR-owned active-map Q2 BSP static collision bridge, the WORR-owned active-map Q2 BSP visibility bridge, the WORR-owned active-map Q2 BSP leaf entity-link bridge, the WORR-owned BotLib memory/filesystem bridges, and WORR-owned bot frame command, nav route-cache, nav debug-overlay, nav reachability-debug, nav polyline-debug, nav debug-client-filter, nav persistent-goal, nav item-goal, nav item-reservation, nav look-ahead steering, nav velocity-aware steering, nav route-target stabilization, nav stuck-repath, nav stuck recovery command, nav goal-blacklist cooldown, nav failed-goal reason, nav movement-state commands, bot brain command ownership, nav natural travel goals, nav rocket-jump route policy, nav four-bot frame-command smoke, nav eight-bot frame-command smoke, nav soak frame-command smoke, nav map-change repeat/restart smoke, nav natural movement support diagnostics, behavior action dispatcher/brain telemetry boundary, coop command-owner and target-sharing bridges, validation tooling, and legacy Q2R bot surface removal work.
  Latest credit/status update: the current 77-row implemented catalog, including 76 automated short-run rows plus one manual high-bot degradation-policy row, records the WORR-owned `sg_bot_behavior_enable` mode `52` behavior policy umbrella proof, the mode `51` match item-policy pickup-scoring proof, the mode `53` profile-role policy proof, the mode `54` profile team-policy proof, the mode `55` profile item-policy proof, the mode `56` profile movement-policy proof, the mode `57` bot chat-policy live-dispatch proof, the mode `58` bot chat team-policy proof, the mode `59` bot chat rate-policy proof, the mode `60` bot chat initial-policy proof, the mode `61` bot chat reply-policy proof, the mode `62` bot chat event-policy proof, the mode `63` behavior-arbitration proof, the mode `64` target-memory-decay proof, the mode `65` weapon-scoring arsenal proof, the mode `66` aim/fire policy depth proof, the mode `67` ammo-pressure pickup proof, the mode `68` survival inventory-use proof, the mode `69` survival health-route proof, the mode `70` survival armor-route proof, and the mode `71` combat-survival regression proof with no new upstream imports claimed. Focused validation remains recorded with mode `52` validation from `.tmp\bot_scenarios\behavior_policy_umbrella\20260622T050833Z`, mode `53` validation from `.tmp\bot_scenarios\profile_role_policy\20260622T052929Z`, mode `54` validation from `.tmp\bot_scenarios\profile_team_policy\20260622T055119Z`, mode `55` validation from `.tmp\bot_scenarios\profile_item_policy\20260622T062835Z`, mode `56` validation from `.tmp\bot_scenarios\profile_movement_policy\20260622T070032Z`, mode `57` validation from `.tmp\bot_scenarios\20260622T080531Z`, mode `58` validation from `.tmp\bot_scenarios\20260622T080044Z`, mode `59` validation from `.tmp\bot_scenarios\20260622T081428Z`, mode `60` validation from `.tmp\bot_scenarios\20260622T085845Z`, mode `61` validation from `.tmp\bot_scenarios\20260622T092009Z`, mode `62` validation from `.tmp\bot_scenarios\20260622T093637Z`, mode `63` validation from `.tmp\bot_scenarios\20260622T112202Z`, mode `64` validation from `.tmp\bot_scenarios\20260622T120742Z`, mode `65` validation from `.tmp\bot_scenarios\20260622T123648Z`, mode `66` validation from `.tmp\bot_scenarios\20260622T125826Z`, mode `67` validation from `.tmp\bot_scenarios\ammo_pressure_pickup\20260622T132231Z`, mode `68` validation from `.tmp\bot_scenarios\survival_inventory_use\20260622T161739Z`, mode `69` validation from `.tmp\bot_scenarios\survival_health_route\20260622T164109Z`, mode `70` validation from `.tmp\bot_scenarios\survival_armor_route\20260622T165918Z`, and mode `71` validation from `.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z`; the latest full automated `implemented` run passed 76/76 rows from `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`, and the credits ledger records the native status families plus validation evidence.
  Latest FFA item-role credit update: the WORR-owned `sg_bot_ffa_item_roles` pickup scoring bridge, mode `46` scenario promotion, and `ffa_item_role_*` status family are recorded as native work with no new upstream imports claimed.
  Latest CTF item-role credit update: the WORR-owned `sg_bot_ctf_item_roles` pickup scoring bridge, mode `47` scenario promotion, and `ctf_item_role_*` status family are recorded as native work with no new upstream imports claimed; the catalog had 52 implemented short-run rows plus one manual degradation row after that promotion.
  Latest FFA role-combat credit update: the WORR-owned `sg_bot_ffa_role_combat` attack-decision bridge, mode `48` scenario promotion, and `ffa_role_combat_*` status family are recorded as native work with no new upstream imports claimed; the catalog had 53 implemented short-run rows plus one manual degradation row after that promotion.
  Latest team resource-denial credit update: the WORR-owned `sg_bot_team_resource_denial` pickup-scoring bridge, mode `50` scenario promotion, and `team_resource_denial_*` status family are recorded as native work with no new upstream imports claimed.
  Latest behavior policy credit update: the WORR-owned `sg_bot_behavior_enable` umbrella bridge, mode `52` scenario promotion, `q3a_bot_behavior_policy_status` family, profile role-policy bridge, mode `53` scenario promotion, profile team-policy bridge, mode `54` scenario promotion, profile item-policy bridge, mode `55` scenario promotion, profile movement-policy bridge, mode `56` scenario promotion, bot chat-policy live-dispatch bridge, mode `57` scenario promotion, bot chat team-only audience policy, mode `58` scenario promotion, bot chat rate-limit policy, mode `59` scenario promotion, bot chat initial-personality policy, mode `60` scenario promotion, bot chat reply-policy proof, mode `61` scenario promotion, bot chat event-policy proof, mode `62` scenario promotion, behavior-arbitration owner/cvar telemetry, mode `63` scenario promotion, target-memory blackboard retention/decay telemetry, mode `64` scenario promotion, weapon-scoring arsenal telemetry plus mode `65` scenario promotion, aim/fire policy depth telemetry plus mode `66` scenario promotion, ammo-pressure pickup telemetry plus mode `67` scenario promotion, survival inventory-use telemetry plus mode `68` scenario promotion, survival health-route telemetry plus mode `69` scenario promotion, survival armor-route telemetry plus mode `70` scenario promotion, and combat/survival regression telemetry plus mode `71` scenario promotion are recorded as native work with no new upstream imports claimed; the current catalog now has 77 implemented rows total, made of 76 automated short-run rows plus one manual degradation row.
  Latest match item-policy credit update: the WORR-owned `sg_bot_match_item_policy` umbrella bridge, mode `51` scenario promotion, and objective/nav status ordering fix are recorded as native work with no new upstream imports claimed.
  Docs-progress tracking note: `docs-dev/q3a-botlib-docs-progress-tracking-round-2026-06-18.md` and `docs-dev/q3a-botlib-extensive-round-closeout-2026-06-18.md` record final checklist math, scenario counts, build/test commands, package/install validation status, and the remaining reference-map/long-soak/coop/CI evidence gaps for the current round.

## Epic DV-08: Release and Updater Hardening
Objective: ensure staged artifacts, update metadata, and updater behavior remain reliable under growth.

Primary Areas: `tools/release/*`, `tools/refresh_install.py`, `src/updater/worr_updater.c`

Exit Criteria:
- Release artifacts are consistently valid and updater behavior is deterministic across channels.

Tasks:
- [x] `DV-08-T01` Add test fixtures for release index parsing edge cases (missing assets, mixed channels, malformed metadata).
  Dependency: `DV-03-T06`. Priority: P1.
- [ ] `DV-08-T02` Add checksum/signature policy review for package trust model.
  Dependency: none. Priority: P2.
- [ ] `DV-08-T03` Add rollback and failed-update recovery validation scenarios.
  Dependency: none. Priority: P1.
- [ ] `DV-08-T04` Add release readiness checklist tied to roadmap milestone gates.
  Dependency: `DV-01-T01`. Priority: P1.
- [x] `DV-08-T05` Split client/server archive payloads and stage the canonical repo assets as `basew/pak0.pkz`.
  Dependency: none. Priority: P1.
- [x] `DV-08-T06` Unify local and published runtime layouts under a single `basew/` gamedir and make release binaries boot that layout by default.
  Dependency: `DV-08-T05`. Priority: P1.
- [x] `DV-08-T07` Standardize arch-suffixed bootstrap/engine binary names and updater metadata across supported platforms.
  Dependency: `DV-08-T06`. Priority: P1.
- [x] `DV-08-T08` Align nightly release publishing and updater channel selection after dropping GitHub prerelease publishing.
  Dependency: `DV-08-T07`. Priority: P1.
- [x] `DV-08-T09` Implement the cross-platform desktop bootstrap updater flow with bootstrap/engine-library split, splash-first startup, and role-scoped installer staging.
  Dependency: `DV-08-T07`. Priority: P0.
- [x] `DV-08-T10` Repair the vendored libcurl wrap and bootstrap launcher Windows build path so local fallback builds can compile and stage the desktop updater layout.
  Dependency: `DV-08-T09`. Priority: P1.
- [x] `DV-08-T11` Stabilize the Windows public-bootstrap-to-temp-worker approved-update handoff and add deterministic local automation for that path.
  Dependency: `DV-08-T09`. Priority: P0.
- [ ] `DV-08-T12` Convert the client bootstrap into a long-lived session shell that owns the display/window lifecycle, keeps updater UX in-process, and reserves the external worker for locked-file replacement and relaunch only.
  Dependency: `DV-08-T09`, `DV-08-T11`. Priority: P1.
  Progress: Windows session-shell work introduced native splash-shell startup, adopted-window activation, synchronized `.install` staging, and engine-side menu backdrops. This follow-up temporarily disables Windows shared-HWND handoff because Win11 capture/preview APIs were still sampling the bootstrap-owned surface; the splash is kept out of taskbar previews, fullscreen defaults to capture-friendly borderless behavior for PrintScreen/Snipping Tool, and the renderer-owned engine window becomes the app frame. Non-transparent menus now clear the engine backbuffer every frame, the main menu backdrop is fully opaque, and hosted launches request only a short engine-owned fade from black, so stale splash pixels cannot remain blended into the main menu.
  Implementation logs: `docs-dev/bootstrap-session-shell-handoff-2026-04-01.md`, `docs-dev/ui-bootstrap-font-handoff-2026-04-27.md`.

## Immediate 90-Day Priority Queue (2026-03-01 to 2026-05-31)
- [ ] `P0` `FR-01-T01` Vulkan particle style parity
- [ ] `P0` `FR-01-T04` MD2/MD5 parity pass
- [ ] `P0` `FR-03-T02` JSON dropdown overlay
- [ ] `P0` `FR-04-T02` Bot frame logic implementation
- [ ] `P0` `DV-01-T01` Project board template rollout
- [ ] `P0` `DV-02-T01` PR CI workflow
- [ ] `P0` `DV-03-T01` Integrate q2proto tests into CI
- [ ] `P0` `DV-06-T01` Dependency baseline audit

## Governance Note
This roadmap is intended to be the live planning source for WORR 2026 execution. Any significant new initiative should be added here first as an epic/task set (or linked as a child project) before implementation starts.
