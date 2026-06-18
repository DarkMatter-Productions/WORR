# Q3A BotLib and Q2 AAS Port Credits Ledger

Date: 2026-06-16

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

Related audit: `docs-dev/q3a-botlib-aas-source-audit-2026-06-16.md`

Related tasks: `FR-04-T02`, `FR-04-T10`, `FR-04-T11`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Purpose

Track source provenance, credits, licenses, and local modification notes for the Quake III Arena BotLib and Quake II AAS generator work. This ledger must be updated in the same change set as any imported, adapted, or substantially referenced upstream file.

## Initial Credit Sources

| Source | Role in Project | URL / Local Path | License / Notice | Credit Requirement |
|---|---|---|---|---|
| Quake III Arena source code | BotLib runtime, game bot behavior, server bot glue, original BSPC lineage | `E:\_SOURCE\_CODE\Quake-III-Arena-master`; public mirror HEAD checked at `https://github.com/id-Software/Quake-III-Arena` commit `dbe4ddb10315479fc00086f08e25d968b4b43c49` | GPL family headers in source files; retain original id Software notices. | Credit id Software and file-level authors/contributors where discoverable from headers/history. Do not import from the unpinned local tree until matched to a pinned source or approved snapshot manifest. |
| `TTimo/bspc` | Required baseline for the WORR Quake II BSP-to-AAS generator | `https://github.com/TTimo/bspc`, audited at commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | README/license identify GPL-2.0-or-later. | Credit `TTimo/bspc`, retain license text, and record exact imported commit. Contributors seen in history: Ben Noordhuis, Chris Brooke, Joel Baxter, Thomas Köppe, Timothee "TTimo" Besset, Victor Luchits. |
| `bnoordhuis/bspc` | Fork lineage for `TTimo/bspc` | `https://github.com/bnoordhuis/bspc`, audited at commit `6c11357e6d79a89e88cda2fe0e67c99a8923e116` | GPL-family source lineage; verify per-file on import. | Credit as upstream fork lineage and record exact source relationship. Contributors seen in history: Ben Noordhuis, Chris Brooke. |
| WORR existing bot scaffolding | Local integration surface and existing helpers | `src/game/sgame/bots/*`, `src/game/sgame/client/client_session_service_impl.cpp`, `src/game/sgame/player/p_view.cpp` | Existing WORR/ZeniMax notices in files. | Preserve existing local notices and document WORR-native changes separately from upstream imports. |

## Imported File Ledger

The first source import is the `TTimo/bspc` snapshot under `tools/q2aas/`. The snapshot row covers the upstream-tracked files copied from `TTimo/bspc` unless a file has its own modified row below. WORR-native build, config, validation, trace bridge, and note files are recorded separately.

| WORR Path | Upstream Path / URL | Upstream Commit | Use Type | License | Copyright / Header | Contributors | Local Changes | Verification |
|---|---|---|---|---|---|---|---|---|
| `tools/q2aas/**` excluding `bspc.c`, `be_aas_bspc.c`, `map.c`, `meson.build`, `worr_q2aas_compat.h`, `worr_q2aas_q2trace.c`, `worr_q2aas_q2trace.h`, `README.WORR.md`, `cfg/worr_q2.cfg`, `validate_worr_q2aas.py`, `audit_worr_q2aas_stage.py`, `audit_worr_q2aas_package.py`, `package_worr_q2aas_archive.py`, and `validation_manifest.json` | `https://github.com/TTimo/bspc` repository root | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Direct vendored source snapshot | GPL-2.0-or-later | Upstream `LICENSE` retained; original id Software GPL headers retained where present. | Ben Noordhuis, Chris Brooke, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset, Victor Luchits; id Software retained from source headers. | Copied under `tools/q2aas/`; no local source edits outside separately listed modified files. | `meson compile -C builddir-win worr_q2aas`; `builddir-win\tools\q2aas\worr_q2aas.exe` usage smoke. |
| `tools/q2aas/bspc.c` | `https://github.com/TTimo/bspc/blob/10d23c5ebd042ddc5d03e17de0f560f5076649dc/bspc.c` | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Modified direct import | GPL-2.0-or-later | Original id Software GPL header retained; `Modified for WORR 2026-06-16` note added. | Ben Noordhuis, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset; id Software retained from source header. | Allows `MAPTYPE_QUAKE2` to run reachability/clustering, centralizes loaded-map reach eligibility, frees retained Q2 BSP data after each AAS conversion, and treats failed BSP loads as fatal before generation continues. | `meson compile -C builddir-win worr_q2aas`; `meson compile -C builddir-win q2aas-staged-smoke` reports `reachabilitysize = 562`, `numclusters = 4`, and invalid BSP failure. |
| `tools/q2aas/be_aas_bspc.c` | `https://github.com/TTimo/bspc/blob/10d23c5ebd042ddc5d03e17de0f560f5076649dc/be_aas_bspc.c` | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Modified direct import | GPL-2.0-or-later | Original id Software GPL header retained; `Modified for WORR 2026-06-16` note added. | id Software retained from source header; TTimo/bspc lineage retained. | Routes Q2 BotLib trace, point contents, entity data, inline model bounds, and checksum setup through the WORR Q2 trace bridge while preserving the Q3 collision path. | `meson compile -C builddir-win worr_q2aas`; strict `mm-rage.bsp` validation. |
| `tools/q2aas/map.c` | `https://github.com/TTimo/bspc/blob/10d23c5ebd042ddc5d03e17de0f560f5076649dc/map.c` | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Modified direct import | GPL-2.0-or-later | Original id Software GPL header retained; `Modified for WORR 2026-06-16` note added. | Ben Noordhuis, Chris Brooke, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset; id Software retained from source header. | Retains Q2 BSP collision lumps during AAS generation so the Q2 reachability bridge can use them after `AAS_Create`. | `meson compile -C builddir-win worr_q2aas`; strict `mm-rage.bsp` validation. |
| `tools/q2aas/meson.build` | WORR-native build wrapper for the imported snapshot | N/A | Native implementation | WORR project license | No upstream header; local build integration file. | WORR contributors | Adds the standalone `worr_q2aas` executable, include paths, warning exceptions, platform defines, thread/math deps, Q2 trace bridge source, and `q2aas-config-smoke` / `q2aas-staged-smoke` / `q2aas-package-map-smoke` / `q2aas-stage-aas` / `q2aas-stage-audit` / `q2aas-package-audit` / `q2aas-package-aas` / `q2aas-package-archive-audit` run targets. The staged smoke requires Q2 BSP input, writes deterministic AAS metadata sidecars, enforces clean BSP lump/spawn/item/high-value reachability gates, and runs expected-failure invalid BSP plus malformed-manifest checks. The package-map smoke creates a scratch pkz, extracts `maps/mm-rage.bsp`, and validates archive-backed conversion. The stage target copies validated AAS output into `.install/basew/maps/` and writes a staged-output report. The stage audit target verifies staged AAS path, size, and hashes against that report. The package audit target verifies staged AAS release payload representation. The package AAS target injects validated staged AAS into `.install/basew/pak0.pkz`; the archive audit target requires the packaged AAS member to match the staged hash. | `meson setup builddir-win --reconfigure`; `meson compile -C builddir-win worr_q2aas`; `meson compile -C builddir-win q2aas-config-smoke`; `meson compile -C builddir-win q2aas-staged-smoke`; `meson compile -C builddir-win q2aas-package-map-smoke`; `meson compile -C builddir-win q2aas-stage-aas`; `meson compile -C builddir-win q2aas-stage-audit`; `meson compile -C builddir-win q2aas-package-audit`; `meson compile -C builddir-win q2aas-package-aas`; `meson compile -C builddir-win q2aas-package-archive-audit`. |
| `tools/q2aas/worr_q2aas_compat.h` | WORR-native compatibility shim | N/A | Native implementation | WORR project license | No upstream header; local compatibility file. | WORR contributors | Force-included only for the tool build; normalizes `_WIN32` to upstream `WIN32` and declares the existing BSPC `COM_Compress` function. | `meson compile -C builddir-win worr_q2aas`. |
| `tools/q2aas/worr_q2aas_q2trace.c`, `tools/q2aas/worr_q2aas_q2trace.h` | WORR-native Q2 BSP trace bridge, informed by WORR collision behavior and existing Q2 BSP structures | N/A | Native implementation | WORR project license | Local source comments. | WORR contributors | Provides static-world Q2 point contents, box traces, inline model bounds, entity string access, and BSP checksum support for BotLib reachability generation. | `meson compile -C builddir-win worr_q2aas`; strict `mm-rage.bsp` validation. |
| `tools/q2aas/cfg/worr_q2.cfg` | WORR-native Q2 movement/AAS preset | WORR movement constants from `src/game/sgame/player/p_move.cpp` and `src/game/bgame/game.hpp` | Native configuration | WORR project license | Local config comments. | WORR contributors | Defines first standing/crouched Q2 player hulls and WORR movement constants for AAS generation. | `meson compile -C builddir-win q2aas-config-smoke`; `python tools\q2aas\validate_worr_q2aas.py --map .install\basew\maps\mm-rage.bsp`. |
| `tools/q2aas/validation_manifest.json` | WORR-native staged-map validation matrix | N/A | Native configuration | WORR project license | Local JSON configuration. | WORR contributors | Records the manifest schema, task IDs, current staged strict smoke map, per-map diagnostic gate requirements, per-map baseline metric/travel minima, and pending reference-map categories for future Q2 generator validation expansion. | `meson compile -C builddir-win q2aas-staged-smoke`. |
| `tools/q2aas/validate_worr_q2aas.py` | WORR-native Q2 AAS validation helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Runs cfg, map, manifest, invalid-BSP, malformed-manifest, and packaged-map smoke checks; keeps scratch output under `.tmp/q2aas/`; validates manifest schema/version/task IDs/gate types/baseline keys before conversion; supports loose map `path` entries and archive-backed `archive`/`archive_member` entries extracted under `.tmp/q2aas/packaged-maps/`; rejects path/archive conflicts, missing archive members, absolute archive members, traversal members, and drive-root-like archive member components before extraction; records `map_source` provenance in reports and metadata; parses AAS summary/travel metrics; preflights Q2 `IBSP` version 38 headers; detects BSPX markers; decodes AAS headers; parses entity and brush-content diagnostics; maps spawn/item origins to generated AAS area bounds; reports high-value pickup reachability from spawn areas; writes JSON reports and deterministic `.aas.meta.json` sidecars; records manifest provenance and manifest schema smoke results in reports; exposes the strict `--require-reachability` gate; enforces staged diagnostic gates for clean BSP lump tables, spawn/item AAS coverage, and high-value pickup reachability; enforces manifest minimum AAS metric/travel-count baselines; and can stage validated `.aas` output with staged path/hash report metadata. | `meson compile -C builddir-win q2aas-config-smoke`; `meson compile -C builddir-win q2aas-staged-smoke`; `meson compile -C builddir-win q2aas-package-map-smoke`; `meson compile -C builddir-win q2aas-stage-aas`; `python tools\q2aas\validate_worr_q2aas.py --manifest tools\q2aas\validation_manifest.json --require-q2-bsp --require-reachability --require-clean-bsp-lumps --require-spawn-coverage --require-item-coverage --require-high-value-reachability --manifest-schema-smoke`. |
| `tools/q2aas/audit_worr_q2aas_stage.py` | WORR-native staged AAS audit helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Reads `.tmp/q2aas/stage-report.json`, verifies staged `.aas` files live under `.install/basew/maps/`, checks extension, existence, non-zero size, staged-output hash, and generated scratch AAS hash, and writes `.tmp/q2aas/stage-audit-report.json`. | `python -m py_compile tools\q2aas\audit_worr_q2aas_stage.py`; `meson compile -C builddir-win q2aas-stage-audit`. |
| `tools/q2aas/audit_worr_q2aas_package.py` | WORR-native q2aas package-readiness audit helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Reads `.tmp/q2aas/stage-report.json`, verifies each staged AAS has a valid loose `.install/basew/` representation or a matching `pak0.pkz` archive member, supports loose-or-archive and archive-required policies, and writes package audit reports under `.tmp/q2aas/`. | `python -m py_compile tools\q2aas\audit_worr_q2aas_package.py`; `meson compile -C builddir-win q2aas-package-audit`; `meson compile -C builddir-win q2aas-package-archive-audit`. |
| `tools/q2aas/package_worr_q2aas_archive.py` | WORR-native q2aas AAS package archive helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Reads `.tmp/q2aas/stage-report.json`, verifies staged AAS hashes, injects generated `maps/<map>.aas` members into `.install/basew/pak0.pkz`, and writes `.tmp/q2aas/package-archive-report.json`. | `python -m py_compile tools\q2aas\package_worr_q2aas_archive.py`; `meson compile -C builddir-win q2aas-package-aas`; `meson compile -C builddir-win q2aas-package-archive-audit`. |
| `tools/refresh_install.py` | WORR-native local install refresh helper with q2aas packaging integration | N/A | Native implementation | WORR project license | Local script comments/CLI help. | WORR contributors | Adds opt-in `--package-q2aas-aas` support that runs q2aas archive packaging and archive-required audit after `pak0.pkz` is rebuilt from `assets/`, preserving generated AAS members through `.install` refreshes. When platform validation is requested, it derives required packaged AAS member names and hashes from the q2aas stage report and passes them to the generic staged-release validator. | `python -m py_compile tools\refresh_install.py`; `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`. |
| `tools/release/validate_stage.py` | WORR-native staged release validator with archive member checks | N/A | Native implementation | WORR project license | Local script comments/CLI help. | WORR contributors | Adds generic `--required-archive-member MEMBER[=SHA256]` validation for members inside the configured base game package archive. This supports q2aas packaged AAS release checks without making the release validator q2aas-specific. | `python -m py_compile tools\release\validate_stage.py`; `python tools\release\validate_stage.py --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --required-archive-member maps/mm-rage.aas=6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`. |
| `tools/q2aas/README.WORR.md` | WORR-native vendor note | N/A | Native documentation | WORR project license | Local documentation. | WORR contributors | Records snapshot, build targets, validation commands, manifest-matrix behavior, manifest schema validation, archive manifest guardrails, automated malformed-manifest smoke coverage, deterministic metadata sidecars, entity/content diagnostics, enforced diagnostic gates, manifest baseline regression gates, archive-backed map validation, packaged-map smoke, validated AAS staging, staged AAS audit, package-readiness audit, AAS archive packaging, refresh-install q2aas packaging integration, generic staged-release archive member validation, first Q2 reachability bridge status, and credit-maintenance expectations beside the vendored source. | Reviewed with this ledger update. |
| `src/game/sgame/bots/bot_runtime.cpp`, `src/game/sgame/bots/bot_runtime.hpp`, `src/game/sgame/gameplay/g_main.cpp` | WORR-native BotLib/AAS runtime shell informed by the credited Q3A/BSPC AAS file format | N/A | Native implementation | WORR project license | Local source header. | WORR contributors | Registers the initial public `sg_bot_*` cvars, hooks server-game map/frame/shutdown lifecycle, probes `maps/<map>.aas` through WORR filesystem search paths, decodes the Q3A/BSPC AAS v5 header transform, validates the `EAAS` version 5 lump table, records runtime AAS structural status, gates map readiness on imported Q3A AAS loader, sample-query, reachability-query, clustering, route-query, and movement-helper results, validates the active `maps/<map>.bsp` as Q2 `IBSP` version 38, extracts lump 0 entity text, lump 13 model records, full static collision data, and PVS/PHS visibility data for the Q3A bridge before AAS load, feeds `level.time.milliseconds()` into the Q3A bridge each frame, calls the imported Q3A AAS start-frame path through the adapter, runs after the server entity update pass, translates WORR SOLID_BSP server model config indices to Q3A inline BSP model numbers, pushes WORR bot-facing snapshots into imported `AAS_UpdateEntity`, registers a Q3A entity trace callback backed by WORR `gi.clip`, maps Q3A debug line/cross/arrow primitives to WORR `gi.Draw_*` imports under bot debug cvars, routes Q3A `botimport.Print` warnings/errors/fatals into WORR logging with verbose message-level forwarding behind `sg_bot_debug_aas >= 3`, and prints verbose adapter/import-smoke status, including clustering, BSP leaf-link/box-query, movement-prediction, and debug-draw results, through `sg_bot_debug_aas`. | `meson compile -C builddir-win sgame_x86_64`; dedicated-server smoke with packaged `maps/mm-rage.aas` reports `areas=428`, `reachability=562`, `clusters=4`, `utility=Q3A LibVar smoke passed`, `q3a_aas=Q3A AAS file load passed`, `q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1`, `q3a_sample_reachability=1`, `q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0`, `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`, `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`, `q3a_movement_drop=yes`, `q3a_movement_jump=yes`, `q3a_debug_draw=Q3A debug draw bridge passed: callback=yes lines=2 crosses=1 arrows=1 clears=1 failures=0`, `q3a_debug_draw_callback=yes`, `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`, `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`, `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`, `q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18`, `q3a_bsp_leaf_link_failures=0`, `q3a_bsp_box_entities_smoke=yes`, `q3a_bsp_box_entities=2`, `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start`, `q3a_bsp_entity_smoke=yes`, `q3a_bsp_model=Q3A BSP model lump load passed: maps/mm-rage.bsp models=18 smoke_model=1 mins=(-328.0 -584.0 24.0) maxs=(-192.0 -440.0 256.0)`, `q3a_bsp_model_smoke=yes`, `q3a_bsp_collision=Q3A BSP collision load passed: maps/mm-rage.bsp planes=1367 nodes=1863 leafs=1882 brushes=1142 point_contents=1 trace_fraction=0.347 startsolid=0 allsolid=0`, `q3a_bsp_point_contents_smoke=yes`, `q3a_bsp_trace_smoke=yes`, `q3a_bsp_visibility=Q3A BSP visibility load passed: maps/mm-rage.bsp clusters=303 smoke_cluster=0 pvs_visible=142 phs_visible=289`, `q3a_bsp_pvs_smoke=yes`, `q3a_bsp_phs_smoke=yes`, `q3a_angle_vectors=Q3A AngleVectors smoke passed`, and `q3a_time_ms=25`. |
| `src/game/sgame/bots/botlib_adapter.cpp`, `src/game/sgame/bots/botlib_adapter.hpp`, `src/game/sgame/bots/q3a/q3a_botlib_boundary.cpp`, `src/game/sgame/bots/q3a/q3a_botlib_boundary.hpp`, `src/game/sgame/bots/q3a/README.WORR.md` | WORR-native Q3A BotLib import boundary and adapter shell informed by the credited Q3A BotLib layout | N/A | Native implementation/documentation | WORR project license | Local source headers and local README. | WORR contributors | Reserves `src/game/sgame/bots/q3a/` for commit-pinned Q3A imports, records planned AAS/runtime source inventory for the pinned id Software baseline, documents import rules, and adds a compiled adapter shell for BotLib setup/shutdown/map/frame calls. The adapter now records Q3A print callback/counter status, the imported utility smoke result, imported AAS loader result, imported AAS sample result, imported AAS reachability sample count, imported AAS clustering status/counters, imported route query, alternative-route query, and opt-in optimization status, imported movement prediction/drop/jump status, imported debug draw callback/counter status, imported AAS start-frame status/counter, entity-sync status/counters, entity-trace callback/counter status, Q3A AAS world counts, sampled area metadata, bridge runtime milliseconds, `AngleVectors` smoke status, active-map BSP entity/epair load status, active-map BSP model bounds status, active-map BSP static collision counts/smoke status, active-map BSP PVS/PHS visibility status, active-map BSP leaf-link/box-query status, and `planned_files=48` inventory status. The local README now records the `be_aas_reach.c`, `be_aas_cluster.c`, `be_aas_route.c`, `be_aas_routealt.c`, `be_aas_optimize.c`, `be_aas_main.c`, `be_aas_entity.c`, and `be_aas_move.c` imports, active-map Q2 BSP entity/model/collision/visibility bridges, imported route-cache/travel-time, alternative-route, and opt-in optimization source status, imported start-frame/entity-cache smoke, WORR snapshot sync into imported `AAS_UpdateEntity`, Q3A `AAS_EntityCollision` to WORR `gi.clip` bridge status, active-map Q2 BSP dynamic leaf links and `AAS_BoxEntities`, imported movement-helper smoke, imported clustering smoke, callback-backed debug line/cross/arrow draw status, Q3A print callback bridge status, and debug polygon/area helper work. | `meson compile -C builddir-win sgame_x86_64`; staged dedicated smoke loads packaged `maps/mm-rage.aas` and reports `utility=Q3A LibVar smoke passed`, `q3a_aas=Q3A AAS file load passed`, `q3a_sample_area=3`, `q3a_sample_point_area=3`, `q3a_sample_reachability=1`, `q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0`, `q3a_cluster_area=3`, `q3a_cluster_cluster=1`, `q3a_cluster_count=4`, `q3a_cluster_areas=157`, `q3a_cluster_reachability_areas=156`, `q3a_cluster_failures=0`, `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`, `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`, `q3a_movement_drop=yes`, `q3a_movement_jump=yes`, `q3a_debug_draw=Q3A debug draw bridge passed: callback=yes lines=2 crosses=1 arrows=1 clears=1 failures=0`, `q3a_debug_draw_callback=yes`, `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`, `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`, `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`, `q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18`, `q3a_bsp_leaf_link_failures=0`, `q3a_bsp_box_entities_smoke=yes`, `q3a_bsp_box_entities=2`, `q3a_bsp_entities=394`, `q3a_bsp_epairs=1704`, `q3a_bsp_entity_smoke=yes`, `q3a_bsp_models=18`, `q3a_bsp_model_smoke=yes`, `q3a_bsp_planes=1367`, `q3a_bsp_nodes=1863`, `q3a_bsp_leafs=1882`, `q3a_bsp_brushes=1142`, `q3a_bsp_point_contents_smoke=yes`, `q3a_bsp_trace_smoke=yes`, `q3a_bsp_vis_clusters=303`, `q3a_bsp_pvs_smoke=yes`, `q3a_bsp_phs_smoke=yes`, `q3a_angle_vectors=Q3A AngleVectors smoke passed`, `q3a_time_ms=25`, `q3a_areas=428`, `q3a_reachability=562`, and `q3a_clusters=4`. |
| `src/game/sgame/bots/q3a/q3a_botlib_import.c`, `src/game/sgame/bots/q3a/q3a_botlib_import.h`, `meson.build` Q3A utility build group | WORR-native bridge/build wrapper for the imported Q3A utility, AAS file-loader, AAS sampling, AAS reachability, AAS clustering, AAS route, AAS alternative-routing, AAS start-frame, AAS entity-cache, AAS movement, and opt-in AAS optimization subsets | N/A | Native implementation | WORR project license | Local source headers. | WORR contributors | Defines the temporary `botimport` memory callbacks, callback-backed Q3A print bridge, `Q_stricmp`, `Com_Memset`, `Com_Memcpy`, `Com_sprintf`, `VectorNormalize`, callback-backed `AAS_EntityCollision` bridge, callback-backed Q3A debug line/cross/arrow bridge, print callback registration/status counters, `Q3A_BotLibImport_RunLibVarSmoke`, `Q3A_BotLibImport_LoadAASBuffer`, `Q3A_BotLibImport_UnloadAAS`, `Q3A_BotLibImport_StartFrame`, entity-sync translation/counting wrappers for imported `AAS_UpdateEntity`, active-map BSP data load/clear functions, `Q3A_BotLibImport_SetMilliseconds`, bridge-backed `Sys_MilliSeconds`, real Q3A-style `AngleVectors`, active-map Q2 BSP entity/model/collision/visibility helpers, dynamic BSP leaf entity-link tables backed by the active-map Q2 BSP tree, `AAS_BSPLinkEntity`, `AAS_UnlinkFromBSPLeaves`, `AAS_BoxEntities`, leaf-link unload cleanup, imported route-cache initialization/freeing, route smoke, alternative-route smoke, and imported opt-in `AAS_Optimize` implementation, imported Q3A `AAS_Setup`/`AAS_SetInitialized`/`AAS_StartFrame`/`AAS_Shutdown` lifecycle calls, imported Q3A `AAS_InitClustering` smoke, WORR/Q2 movement LibVar seeding before imported `AAS_InitSettings`, imported movement prediction/drop/jump smoke, imported debug draw smoke, read-only in-memory FS callbacks, quiet log stubs, exported `vec3_origin`, imported optimization source for the Q3A `aasoptimize` path, and imported alternative-routing initialization/smoke for `be_aas_routealt.c`. The previous loaded-world `AAS_AreaReachability`, `aasworld`, `AAS_Time`, `AAS_ProjectPointOntoVector`, `AAS_Error`, `AAS_ResetEntityLinks`, `AAS_InvalidateEntities`, `AAS_UnlinkInvalidEntities`, `AAS_InitClustering`, `AAS_InitAlternativeRouting`, `AAS_ShutdownAlternativeRouting`, `AAS_Optimize`, movement prediction/drop/jump shims, and debug-line no-ops are removed in favor of imported Q3A implementations or WORR-owned callbacks. Adds `q3a_botlib_utility` with `Q3A_BOTLIB_WORR_BOUNDARY=1`, `MEMORYMANEGER=1`, Windows `WIN32` endian selection, non-Windows `ID_INLINE=inline`, and local warning policy for legacy Q3A C including scoped `-Wno-absolute-value`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `utility=Q3A LibVar smoke passed`, `q3a_print_callback=yes`, `q3a_aas=Q3A AAS file load passed`, `q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1`, `q3a_sample_reachability=1`, `q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0`, `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`, `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`, `q3a_movement_drop=yes`, `q3a_movement_jump=yes`, `q3a_debug_draw=Q3A debug draw bridge passed: callback=yes lines=2 crosses=1 arrows=1 clears=1 failures=0`, `q3a_debug_draw_callback=yes`, `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`, `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`, `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`, `q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18`, `q3a_bsp_leaf_link_failures=0`, `q3a_bsp_box_entities_smoke=yes`, `q3a_bsp_box_entities=2`, `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start`, `q3a_bsp_entity_smoke=yes`, `q3a_bsp_model=Q3A BSP model lump load passed: maps/mm-rage.bsp models=18 smoke_model=1 mins=(-328.0 -584.0 24.0) maxs=(-192.0 -440.0 256.0)`, `q3a_bsp_model_smoke=yes`, `q3a_bsp_collision=Q3A BSP collision load passed: maps/mm-rage.bsp planes=1367 nodes=1863 leafs=1882 brushes=1142 point_contents=1 trace_fraction=0.347 startsolid=0 allsolid=0`, `q3a_bsp_point_contents_smoke=yes`, `q3a_bsp_trace_smoke=yes`, `q3a_bsp_visibility=Q3A BSP visibility load passed: maps/mm-rage.bsp clusters=303 smoke_cluster=0 pvs_visible=142 phs_visible=289`, `q3a_bsp_pvs_smoke=yes`, `q3a_bsp_phs_smoke=yes`, `q3a_angle_vectors=Q3A AngleVectors smoke passed`, and `q3a_time_ms=25`. |
| `src/game/sgame/bots/q3a/game/q_shared.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/game/q_shared.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `9083a35790991b674bc58c3800b068a9a978898508c5fb08123ea52e1dc8597a`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-utility-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/game/surfaceflags.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/game/surfaceflags.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `f05993c571858f3bb86cdcb9121d1748351377746916b5db0e28312bdb3b6722`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-utility-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/game/botlib.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/game/botlib.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `5eb2397db26ea5463aa1153431b8f39ac8ad92fcb2d4e3108e6b33fc894515af`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-utility-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_interface.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_interface.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `f715954bdbeeef62fa8946f35894da1bab7bdadca1dae2abece4f4c2e80e6358`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-utility-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_log.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_log.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `c48bdc73031a087055afb363123c72bdf5d349b76b8c4c02ff0ee8affb214a1e`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-utility-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_memory.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_memory.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `45c52cbde96d3675ac2e48adaf7bcd21c4a2c744f81e5be13880fa76c7e29237`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-utility-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_memory.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_memory.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `fcee4453a6a9b76347ae8aa068e711afa55b35a017cdb1f2bd625123decc81fa`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-utility-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_libvar.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_libvar.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `96966243e0c590649a0287f5c1b01970425cc65e364c58aee43fedb7165bab40`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `Q3A LibVar smoke passed`. |
| `src/game/sgame/bots/q3a/botlib/l_libvar.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_libvar.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `60a403b2bba9cb0f813253bbd1121f606f83bdb21bffaeb8e7633a6ba6c34f41`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `Q3A LibVar smoke passed`. |
| `src/game/sgame/bots/q3a/botlib/aasfile.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/aasfile.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `937ea3f9143c44e8d61002a369e92fc44f5c416b7f3c92d088c4914fe28ebdcb`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/game/be_aas.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/game/be_aas.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `86ac70f29f7c387b255b0c8b6da56841cc773f26ac7551923e4b8429652dad70`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_file.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_file.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `7306cf38153ede8c7608b1d07fd174901a69639c5ff1e47cc11c2e616b08a9a5`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_aas=Q3A AAS file load passed`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_file.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_file.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `d92e01098c310137d3e003fc394627aca726868a92b61c287eacd3c139c51982`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_def.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_def.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `a248bab0f92165bbf0382997d401409394be0f053382b8367e9bd138e148b054`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_funcs.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_funcs.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `c018b4a9a7c076924b406f464318d32c178ff051bfeefb93ffef74eedf39bedb`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_main.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_main.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `ce3e5ecba4742e0853c79edd685b3d5317aeb9f9b836d437c4d2dfa872c81c0b`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`; `docs-dev/q3a-botlib-aas-start-frame-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_main.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_main.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `d1abfb9d2da6af9cfc89ba4d0ee1dc1bde7f82d086a727edae5038705ac06d8a`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_entity.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_entity.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `2176a5e3f63127c759a95318f3c4d1c9cbf47052c43ca5c0a0d1121d3729dd37`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`; `docs-dev/q3a-botlib-aas-entity-cache-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_entity.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_entity.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `c3d0ced64df7b73f6b1ef9d6d63a359024f1d2f55977a96aed89ea13e130c22b`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`; `docs-dev/q3a-botlib-aas-entity-cache-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_sample.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_sample.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `41a5699e9c23c772f2937cad3b20cb4f4a40c17e17a214e9e26049e0d59f1330`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_sample.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_sample.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `5fc4b89d263f12c7b79843f7d589b231f8ff96c8af09154f79f9d6478a6df993`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_cluster.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_cluster.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `fc6274401718ea0a95c4912e530ec13544bd4f7d71bb040f662a6189be469899`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_cluster.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_cluster.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `abcdf5913ff4120925fcf0a63aae9224dae8d88886cb344726c000311be267cd`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0`; `docs-dev/q3a-botlib-aas-cluster-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_reach.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_reach.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `b5622a0e7c6d6dfbf8a82078f4629a3b2885d087eba434a47dedb83908c71548`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports imported `AAS_AreaReachability` via `q3a_sample_reachability=1`; `docs-dev/q3a-botlib-aas-reach-query-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_reach.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_reach.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `7e16dbfa25178cd11edcdd480155e49c3dbe7aed809f4fe0adafca3c836e7227`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_route.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_route.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `78303f3c3d0e834b36db7acf205b45b7f6afafd69cb16bf0257ec47d02d41011`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`; `docs-dev/q3a-botlib-aas-route-query-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_route.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_route.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `7de8aff915f394452c405e2c0c5d2f617158c8a0cee5ebb79eb0daeba2d7f96c`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_routealt.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_routealt.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `204b16897a6a95b7cf7d392e5b1637f1b89823695d3a5c47a3e327a3ed8efae9`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_routealt.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_routealt.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `d74b894c09316ca8161875d8499a7fc0b61bd195f8fda83d9dd6adfc4dc78081`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`; `docs-dev/q3a-botlib-aas-alternative-route-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_debug.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_debug.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `acaab8949a831f6e2ee0418e80edad4215b03406555aecd86f9970ed6b719ff6`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_debug.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_debug.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `f88ebba452590852cb60f5a3a90d16140929127beb602b04749c6442c234d14e`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0`; `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_optimize.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_optimize.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `92be274f1e600df658d588d924e7b030806b455ff7b706afb28c489bd3ceb92c`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_optimize.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_optimize.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `69098a4cecf6137704304237b5b300e97801d1531bdb7ed35776af9f4a5d44b1`. | `meson compile -C builddir-win sgame_x86_64`; staged smoke keeps `aasoptimize=0` on the default loaded-AAS path and reports existing `q3a_route` / `q3a_alt_route` / `q3a_movement` coverage; `docs-dev/q3a-botlib-aas-optimize-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_bsp.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_bsp.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `b7c48e3fe8c353445ede0b35ebaa6ff1bdfe8cab9bc48b8b8816945d382b5db0`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_move.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_move.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `1058490f47f3061c90afd78a03016af57a8037576de85feb154e1de679de12d8`. | `meson compile -C builddir-win sgame_x86_64`; runtime smoke reports `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`; `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/be_aas_move.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/be_aas_move.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `22cc80f7362514aeba0d2cb9b1550cfb0adee96912ff94269af7bf5bad1ba719`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_crc.c` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_crc.c` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `d743b8019b6393d8b9e5463beec113aabbea94aa8e65188c674c841bf9c9ccac`. | `meson compile -C builddir-win sgame_x86_64`; linked for imported route-cache CRC checks; `docs-dev/q3a-botlib-aas-route-query-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_crc.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_crc.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `cadd973a3d1396936cb3a985e8802f24a8ac5f3e58136220e47cf1034b5bec1f`. | `meson compile -C builddir-win sgame_x86_64`; linked for imported route-cache CRC checks; `docs-dev/q3a-botlib-aas-route-query-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_script.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_script.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `5415db33c82197b7375942762df34ef535b23c464a672aba5f11944a7af444d9`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_precomp.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_precomp.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `dd5692a0991b9428a8af203f3586525a3d569323695f77f4839d5fe7deb2b419`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_struct.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_struct.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `b18d46536025610ca9782c66ff3744640d05cc20f6e988dc45e7ab6633613a40`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |
| `src/game/sgame/bots/q3a/botlib/l_utils.h` | `https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/botlib/l_utils.h` | `dbe4ddb10315479fc00086f08e25d968b4b43c49` | Direct imported source/header | GPL-2.0-or-later | Original id Software GPL header retained. | id Software retained from source header. | Exact copy from pinned upstream; SHA-256 `5512c0f7554791aa95b02a5960eec635eb6a391894034611eab82c041cdfdaeb`. | `meson compile -C builddir-win sgame_x86_64`; `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`. |

## Native Bridge Update: Route Overlay

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now expose a route/goal overlay smoke path. The path uses the already imported Q3A `be_aas_route.c` queries and the WORR-owned debug draw callback bridge; no additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_route 1` reports `q3a_route_overlay=Q3A route overlay passed: callback=yes start=3 goal=6 end=6 travel_time=113 reachability=1 lines=2 crosses=3 arrows=1 clears=1 failures=0`.
- Implementation log: `docs-dev/q3a-botlib-aas-route-overlay-2026-06-17.md`.

## Native Bridge Update: Debug Polygons

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now expose Q3A `botimport.DebugPolygonCreate` / `DebugPolygonDelete` through a WORR-owned callback. The runtime renders debug polygon outlines and fan diagonals through existing `gi.Draw_Line` debug imports; no additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 3` reports `q3a_debug_polygon=Q3A debug polygon bridge passed: callback=yes creates=1 deletes=1 points=4 last_id=1 failures=0`.
- Implementation log: `docs-dev/q3a-botlib-aas-debug-polygon-bridge-2026-06-17.md`.

## Native Bridge Update: AAS Debug Area Helpers

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, `src/game/sgame/bots/q3a/README.WORR.md`, and `meson.build` now compile imported Q3A `be_aas_debug.c` and provide Q3A debug-line create/show/delete callbacks so imported `AAS_ShowArea` / `AAS_ShowAreaPolygons` can render through the existing WORR debug draw and debug polygon callbacks.
- The imported `src/game/sgame/bots/q3a/botlib/be_aas_debug.c` file was not locally modified; the native adapter owns the WORR callback/status/smoke logic.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 3` reports `q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0`.
- Implementation log: `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`.

## Native Bridge Update: AAS Clustering

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, `src/game/sgame/bots/q3a/README.WORR.md`, and `meson.build` now compile imported Q3A `be_aas_cluster.c` and expose loaded-cluster smoke status through `sg_bot_debug_aas 2`.
- The imported `src/game/sgame/bots/q3a/botlib/be_aas_cluster.c` file was not locally modified; the native adapter owns the WORR status, failure text, and smoke logic.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 2` reports `q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0`.
- Implementation log: `docs-dev/q3a-botlib-aas-cluster-import-2026-06-17.md`.

## Native Bridge Update: AAS Alternative Routing

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, `src/game/sgame/bots/q3a/README.WORR.md`, and `meson.build` now compile imported Q3A `be_aas_routealt.c`, replace the temporary alternative-routing lifecycle stubs, initialize alternative routing after loaded-AAS route-cache setup, and expose `AAS_AlternativeRouteGoals` smoke status through `sg_bot_debug_aas 2`.
- The imported `src/game/sgame/bots/q3a/botlib/be_aas_routealt.c` file was not locally modified; the native adapter owns the WORR status, failure text, direct-call initialization point, and smoke logic.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 2` reports `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`.
- Implementation log: `docs-dev/q3a-botlib-aas-alternative-route-import-2026-06-17.md`.

## Native Bridge Update: AAS Optimization Import

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/q3a/q3a_botlib_boundary.cpp`, `src/game/sgame/bots/q3a/README.WORR.md`, and `meson.build` now compile imported Q3A `be_aas_optimize.c` and remove the temporary local `AAS_Optimize` no-op.
- The imported `src/game/sgame/bots/q3a/botlib/be_aas_optimize.c` file was not locally modified. The default loaded-AAS smoke keeps `aasoptimize=0` because Q3A optimization rewrites AAS geometry/index arrays and is intended for opt-in save/forcewrite flows.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 2` reports the existing loaded-AAS `q3a_aas`, `q3a_route`, `q3a_alt_route`, and `q3a_movement` checks passing.
- Implementation log: `docs-dev/q3a-botlib-aas-optimize-import-2026-06-17.md`.

## Native Bridge Update: Q3A Print Callback

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now route Q3A `botimport.Print` through a callback-backed WORR logging bridge.
- The bridge forwards Q3A warnings/errors/fatals to `gi.Com_PrintFmt`, exposes message-level Q3A print chatter only when `sg_bot_debug_aas >= 3`, and records `q3a_print_*` counters in verbose adapter status.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 3` reports `Q3A BotLib message: trying to load maps/mm-rage.aas`, `q3a_print_callback=yes`, `q3a_print_messages=2`, `q3a_print_warnings=0`, and `q3a_print_errors=0`.
- Implementation log: `docs-dev/q3a-botlib-print-bridge-2026-06-17.md`.

## Native Bridge Update: BotClientCommand Safety Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now route Q3A `botimport.BotClientCommand` through a callback-backed WORR safety bridge.
- The runtime validates the imported client index against WORR client entities, requires a bot client, and currently rejects command execution until a future validated bot command dispatcher owns the command whitelist and execution path. No additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 3` reports `q3a_bot_client_command=Q3A BotClientCommand bridge passed: callback=yes client=0 accepted=0 rejected=1 failures=0`.
- Implementation log: `docs-dev/q3a-botlib-bot-client-command-bridge-2026-06-17.md`.

## Native Bridge Update: BotLib Memory Allocator

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now replace the raw Q3A `botimport.GetMemory`, `botimport.FreeMemory`, and `botimport.HunkAlloc` callbacks with a tracked bot-owned allocator.
- The allocator keeps Q3A `MEMORYMANEGER` semantics by freeing zone allocations individually and releasing hunk allocations as a group after AAS shutdown. It reports active/peak byte counts, allocation/free counters, grouped hunk releases, failure counts, and available-budget estimates through verbose adapter status. No additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated reload smoke with `sg_bot_debug_aas 3` reports `q3a_memory_zone_active=239894`, `q3a_memory_hunk_active=691078`, `q3a_memory_hunk_allocs=17`, and `q3a_memory_failures=0`.
- Implementation log: `docs-dev/q3a-botlib-memory-allocator-bridge-2026-06-17.md`.

## Native Bridge Update: BotLib Filesystem Bridge

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now replace the singleton active-AAS memory file shim with a read-only callback-backed Q3A filesystem bridge.
- Q3A `botimport.FS_FOpenFile`, `FS_Read`, `FS_Seek`, and `FS_FCloseFile` now use a tracked file-handle table. Normal reads load through WORR's `FILESYSTEM_API_V1::LoadFile`, close frees callback-owned buffers through the runtime callback, and the active in-memory AAS buffer remains as a fallback for failure isolation. No additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_aas 3` reports `q3a_fs_passed=yes`, `q3a_fs_files=1`, `q3a_fs_memory_files=0`, `q3a_fs_read_bytes=277484`, and `q3a_fs_writes_rejected=0`.
- Implementation log: `docs-dev/q3a-botlib-filesystem-bridge-2026-06-17.md`.

## Native Bridge Update: BotLib Route Cache Miss Policy

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now classify imported Q3A `.rcd` route-cache read probes as optional cache misses instead of filesystem open failures.
- The bridge still rejects write-mode opens until WORR intentionally supports route-cache dump writes. No additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; scratch import harness reports `loaded=1`, `q3a_fs_open_failures=0`, and `q3a_fs_route_cache_misses=1`.
- Implementation log: `docs-dev/q3a-botlib-route-cache-miss-policy-2026-06-17.md`.

## Native Bridge Update: BotLib Lifecycle Telemetry

Date: 2026-06-17

Tasks: `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/q3a/README.WORR.md` now report BotLib lifecycle counters for init, shutdown, load attempts/successes, active unloads, clean unloads, unload failures, transient unload residue, open file handles, and persistent LibVar zone bytes.
- The lifecycle proof treats persistent Q3A LibVar allocations as module-lifetime state and requires transient AAS file/hunk/zone state to return to zero after active unloads. No additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `refresh_install.py --package-q2aas-aas`; scratch lifecycle harness reports `loads=3/3`, `active_unloads=3`, `clean_unloads=3`, `unload_failures=0`, and `last_unload_files=0`.
- Implementation log: `docs-dev/q3a-botlib-lifecycle-telemetry-2026-06-17.md`.

## Native Bridge Update: Bot Frame Command Dispatch

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `inc/shared/bot_frame_command.h`, `inc/shared/gameext.h`, `src/game/sgame/bots/bot_think.*`, `src/game/sgame/gameplay/g_main.cpp`, `src/server/main.c`, `src/server/server.h`, and `src/server/user.c` now expose a lightweight game extension for bot frame command generation and run accepted commands through the server fake-client `SV_ClientThink` path.
- The first command builder is AAS-gated and emits deterministic placeholder movement so the dispatch, usercmd ABI, and fake-client movement path are validated before full route following lands. This behavior is superseded by the route-steered frame-command bridge entry below. No additional upstream source was imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sv_bot_frame_command_smoke 2` reports `q3a_bot_frame_command_status frames=8 commands=8 skipped_invalid=0 skipped_not_bot=0 skipped_runtime=0 skipped_inactive=0 expected_min_frames=1 expected_min_commands=1 pass=1`.
- Implementation log: `docs-dev/q3a-botlib-frame-command-dispatch-2026-06-17.md`.

## Native Bridge Update: Route-Steered Frame Commands

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, and `src/game/sgame/bots/bot_think.cpp` now expose a live route-steering query and use it for bot frame command yaw/forward movement.
- The bridge finds a reachable AAS area near the bot, chooses a deterministic reachable goal, validates the full imported Q3A route, and returns the first route step as the immediate steering target.
- No new upstream source files were imported for this slice; the work uses already-imported Q3A AAS route APIs behind WORR-owned boundary code.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sv_bot_frame_command_smoke 2` reports `q3a_bot_frame_command_status frames=8 commands=8 route_queries=8 route_commands=8 route_failures=0 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_travel_time=130 last_reachability=218 last_stop_event=0 ... pass=1`.
- Implementation log: `docs-dev/q3a-botlib-route-steered-frame-commands-2026-06-17.md`.

## Native Bridge Update: Nav Route Cache

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/bot_think.cpp`, `src/game/sgame/bots/bot_runtime.cpp`, and `meson.build` now move route-steer reuse and refresh cadence into a dedicated `bot_nav` module.
- The cache stores per-client route-steer results, refreshes on cadence/target/origin/preferred-goal changes, resets on BotLib level begin/end, and reports cache counters through the existing dedicated frame-command smoke.
- No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sv_bot_frame_command_smoke 2` reports `route_requests=8`, `route_queries=2`, `route_refreshes=2`, `route_reuses=6`, `route_commands=8`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-route-cache-2026-06-17.md`.

## Native Bridge Update: Nav Debug Overlay

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/bot_runtime.cpp`, and `src/game/sgame/bots/bot_think.cpp` now feed cached per-bot route/goal state into the route/goal debug overlay.
- `sg_bot_debug_route` / `sg_bot_debug_goal` draw native cached route-step and goal markers after a bot route is cached, while the existing imported Q3A route-overlay smoke remains a fallback before live bot route state exists.
- No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_route 1`, `sg_bot_debug_goal 1`, and `sv_bot_frame_command_smoke 2` reports `route_debug_frames=10`, `route_debug_routes=8`, `route_debug_goals=8`, `route_debug_missing_frames=2`, `route_debug_arrows=8`, `last_route_debug_client=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-debug-overlay-2026-06-17.md`.

## Native Bridge Update: Nav Reachability Debug

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_nav.*`, and `src/game/sgame/bots/bot_think.cpp` now carry current-area and next-reachability metadata from the imported Q3A route result into native route debug.
- `sg_bot_debug_route` labels the cached live route step with current AAS area, reachability id, travel type, and reachability end area; the dedicated frame-command smoke also reports the same fields for headless validation.
- No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_route 1`, `sg_bot_debug_goal 1`, and `sv_bot_frame_command_smoke 2` reports `route_debug_labels=8`, `last_current_area=224`, `last_reachability_type=2`, `last_reachability_flags=2`, `last_reachability_end_area=217`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-reachability-debug-2026-06-17.md`.

## Native Bridge Update: Nav Polyline Debug

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_nav.*`, and `src/game/sgame/bots/bot_think.cpp` now carry a bounded route-point payload for live bot route debug.
- `sg_bot_debug_route` draws sampled cached route points as a polyline while keeping the existing route arrow, reachability label, and goal marker; the dedicated frame-command smoke reports polyline point/segment counters for headless validation.
- No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_route 1`, `sg_bot_debug_goal 1`, and `sv_bot_frame_command_smoke 2` reports `route_debug_polyline_points=16`, `route_debug_polyline_segments=24`, `last_route_point_count=2`, `route_debug_lines=16`, `route_debug_arrows=8`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-polyline-debug-2026-06-17.md`.

## Native Bridge Update: Nav Debug Client Filter

Date: 2026-06-17

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_runtime.cpp`, `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/bot_think.cpp`, `src/game/sgame/g_local.hpp`, and `src/game/sgame/gameplay/g_main.cpp` now carry a selected-client filter for live bot route debug.
- `sg_bot_debug_client` defaults to `-1` for all cached bot routes; non-negative values select one zero-based client slot for `sg_bot_debug_route` / `sg_bot_debug_goal` overlays.
- No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_client 0` reports `route_debug_routes=8`, `route_debug_filtered_slots=0`, `last_debug_filter_client=0`, and `pass=1`; dedicated smoke with `sg_bot_debug_client 1` reports `route_debug_routes=0`, `route_debug_filtered_slots=8`, `route_debug_filter_miss_frames=10`, `last_debug_filter_client=1`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-debug-client-filter-2026-06-17.md`.

## Native Bridge Update: Nav Persistent Goal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/bot_think.cpp`, and `src/game/sgame/bots/botlib_adapter.*` now carry per-client persistent route-goal ownership across cache reuses and route refreshes.
- The native adapter can ask imported Q3A AAS routing for a preferred goal area, while `bot_nav` clears or falls back when that area is reached or stops routing.
- No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated smoke with `sg_bot_debug_route 1`, `sg_bot_debug_goal 1`, and `sv_bot_frame_command_smoke 2` reports `route_goal_requests=1`, `route_goal_assignments=1`, `route_goal_cache_reuses=6`, `route_goal_clears=0`, `route_goal_fallbacks=0`, `last_persistent_goal_area=227`, `last_goal_clear_reason=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-persistent-goal-2026-06-18.md`.

## Native Bridge Update: Nav Item Goals

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/bot_nav.*`, and `src/game/sgame/bots/bot_think.cpp` now expose point-to-route-area lookup through the Q3A adapter boundary and use it to select live active-pickup route goals.
- `bot_nav` scores active pickup entities, records selected item entity/spawn/item identity, and clears the persistent goal when that item disappears, respawn-hides, or stops routing to the requested AAS area.
- No new upstream source files were imported for this slice; the work is WORR-native adapter and navigation code around already imported Q3A AAS route/sample behavior.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 2` on `mm-rage` reports `item_goal_scans=1`, `item_goal_candidates=45`, `item_goal_assignments=1`, `item_goal_reuses=7`, `item_goal_clears=0`, `last_item_goal_entity=32`, `last_item_goal_area=415`, `last_item_goal_item=53`, `last_item_goal_score=828`, `last_persistent_goal_area=415`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-item-goal-2026-06-18.md`.

## Native Bridge Update: Nav Item Reservations

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/bot_think.cpp`, `src/game/sgame/bots/bot_includes.hpp`, `src/game/sgame/client/client_session_service_impl.cpp`, and `src/server/main.c` now add a first item reservation policy over active-pickup route goals.
- `bot_nav` skips active pickups already selected by another bot's live route slot, reports reservation counters in the frame-command status, and releases stale reservations when bot clients disconnect.
- The server frame-command smoke keeps the existing one-bot mode and adds `sv_bot_frame_command_smoke 3` as a two-bot reservation proof.
- No new upstream source files were imported for this slice; the work is WORR-native route-goal ownership policy above the existing BotLib/AAS adapter path.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `frames=17`, `commands=17`, `route_goal_assignments=2`, `item_goal_scans=2`, `item_goal_candidates=89`, `item_goal_assignments=2`, `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `last_item_goal_reserved_entity=32`, `last_item_goal_reserved_by_client=0`, `last_item_goal_entity=74`, `last_item_goal_area=251`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-item-reservation-2026-06-18.md`.

## Native Bridge Update: Nav Look-Ahead Steering

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native file `src/game/sgame/bots/bot_think.cpp` now selects a command steering target from the bounded route-point payload returned by the BotLib adapter, rather than always aiming only at the immediate route step.
- The frame-command status reports look-ahead attempts, uses, selected index, and route-point count so the behavior can be validated headlessly.
- No new upstream source files were imported for this slice; this is WORR-native command steering over already imported Q3A AAS route-query output exposed through `botlib_adapter.*`.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `frames=17`, `commands=17`, `last_route_point_count=2`, `lookahead_attempts=17`, `lookahead_uses=9`, `last_lookahead_index=0`, `last_lookahead_point_count=2`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-lookahead-steering-2026-06-18.md`.

## Native Bridge Update: Nav Velocity-Aware Steering

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native file `src/game/sgame/bots/bot_think.cpp` now adjusts command yaw from a short projected horizontal bot origin when the bot is already moving toward a route target.
- The frame-command status reports velocity-lead attempts, uses, last speed squared, and last lead-offset squared so the behavior can be validated headlessly.
- No new upstream source files were imported for this slice; this is WORR-native command steering over already imported Q3A AAS route-query output exposed through `botlib_adapter.*`.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `frames=17`, `commands=17`, `lookahead_attempts=9`, `lookahead_uses=9`, `velocity_lead_attempts=17`, `velocity_lead_uses=3`, `last_velocity_lead_speed_sq=182`, `last_velocity_lead_offset_sq=1`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-velocity-steering-2026-06-18.md`.

## Native Bridge Update: Nav Stuck Repath

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/bot_think.cpp`, and `src/server/main.c` now add a progress watchdog over cached route goals, report stuck reason counters, and expose an internal stalled-command smoke mode.
- `BotNavRefreshReason::Stuck` forces the existing native route refresh path when a bot remains stagnant toward its active goal for a sustained window.
- No new upstream source files were imported for this slice; this is WORR-native route-cache and validation policy over already imported Q3A AAS route-query output exposed through `botlib_adapter.*`.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; normal dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `stuck_detections=0`, `stuck_repath_refreshes=0`, `route_failures=0`, and `pass=1`; stalled `sv_bot_frame_command_smoke 4` reports `frames=29`, `commands=29`, `stuck_checks=27`, `stuck_stalls=25`, `stuck_detections=2`, `stuck_repath_refreshes=2`, `last_stuck_reason=1`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-stuck-repath-2026-06-18.md`.

## Native Bridge Update: Nav Stuck Recovery Command

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*` and `src/game/sgame/bots/bot_think.cpp` now add a short back/strafe recovery command window after the native stuck-progress watchdog fires.
- `bot_nav` tracks recovery activations, active recovery frames, last recovery client, side, and frames remaining; `bot_think` reports recovery command uses and last movement values.
- No new upstream source files were imported for this slice; this is WORR-native command recovery policy above already imported Q3A AAS route-query output exposed through `botlib_adapter.*`.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; normal dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `stuck_recovery_activations=0`, `stuck_recovery_frames=0`, `recovery_command_uses=0`, `route_failures=0`, and `pass=1`; stalled `sv_bot_frame_command_smoke 4` reports `frames=29`, `commands=29`, `stuck_recovery_activations=2`, `stuck_recovery_frames=11`, `last_stuck_recovery_side=-1`, `recovery_command_uses=11`, `last_recovery_forward_move=-80`, `last_recovery_side_move=-140`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-stuck-recovery-command-2026-06-18.md`.

## Native Bridge Update: Nav Goal Blacklist Cooldown

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*` and `src/game/sgame/bots/bot_think.cpp` now add a per-bot active-pickup goal blacklist cooldown after stuck detections.
- `bot_nav` records the stuck item's entity number, spawn count, item id, and cooldown expiry; active-pickup scans skip matching blacklisted goals for that bot and continue scoring alternate pickups.
- The stuck blacklist clear path records `last_goal_clear_reason=5` and preserves the already active short recovery movement window. No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; normal dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `item_goal_blacklist_activations=0`, `item_goal_blacklist_skips=0`, `item_goal_blacklist_active=0`, `route_failures=0`, and `pass=1`; stalled `sv_bot_frame_command_smoke 4` reports `frames=29`, `commands=29`, `item_goal_blacklist_activations=2`, `item_goal_blacklist_skips=2`, `item_goal_blacklist_active=2`, `last_item_goal_blacklisted_entity=68`, `last_item_goal_blacklisted_by_client=1`, `last_item_goal_blacklist_frames_remaining=96`, `last_goal_clear_reason=5`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-goal-blacklist-cooldown-2026-06-18.md`.

## Native Bridge Update: Nav Failed Goal Reason

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*` and `src/game/sgame/bots/bot_think.cpp` now record failed-goal reason diagnostics for abandoned persistent route goals.
- `bot_nav` records route fallback, item unavailable, and blacklisted item goals as failed-goal events before clearing the persistent goal state. Reached goals and resets do not count as failed goals.
- The debug label and frame-command smoke now expose the last failed reason, client, area, entity, and item id. No new upstream source files were imported for this slice.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; normal dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `failed_goal_events=0`, `last_failed_goal_reason=0`, `route_failures=0`, and `pass=1`; stalled `sv_bot_frame_command_smoke 4` reports `frames=29`, `commands=29`, `failed_goal_events=2`, `last_goal_clear_reason=5`, `last_failed_goal_reason=3`, `last_failed_goal_client=1`, `last_failed_goal_area=251`, `last_failed_goal_entity=74`, `last_failed_goal_item=2`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-failed-goal-reason-2026-06-18.md`.

## Native Bridge Update: Nav Movement State Commands

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_think.cpp` and `src/server/main.c` now translate selected AAS reachability travel types into Q2 `usercmd_t` button intent and expose forced movement-state smoke modes.
- `bot_think` maps crouch reachability to `BUTTON_CROUCH`, jump/barrier-jump/waterjump reachability to `BUTTON_JUMP`, and swim/ladder reachability to vertical jump/crouch intent based on the current route target height.
- No new upstream source files were imported for this slice; this is WORR-native command translation over already imported Q3A AAS route-query output exposed through `botlib_adapter.*`.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; normal dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `movement_state_attempts=17`, `movement_state_commands=0`, `last_movement_state_travel_type=2`, and `pass=1`; forced `sv_bot_frame_command_smoke 5`, `6`, and `7` report `movement_state_jump_commands=17`, `movement_state_crouch_commands=17`, and `movement_state_swim_commands=17` respectively with `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-movement-state-commands-2026-06-18.md`.

## Native Bridge Update: Legacy Q2R Bot Surface Removal

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- Removed the inherited Q2R `src/game/sgame/bots/bot_debug.*`, `bot_exports.*`, and `bot_utils.*` layer from the active server-game bot implementation because it targeted a different engine-side bot system.
- WORR bot work now continues through `bot_runtime.*`, `botlib_adapter.*`, `bot_nav.*`, `bot_think.*`, and the quarantined `q3a/` BotLib/AAS boundary.
- No new upstream source files were imported for this slice; this is a local removal/replacement-boundary cleanup.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 2` on `mm-rage` reports `frames=8`, `commands=8`, `route_failures=0`, `route_goal_assignments=1`, `last_persistent_goal_area=227`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-legacy-bot-surface-removal-2026-06-18.md`.

## Native Bridge Update: Bot Brain Command Ownership

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_brain.*`, `src/game/sgame/bots/bot_think.cpp`, `src/game/sgame/bots/bot_includes.hpp`, and `meson.build` now split current high-level frame command/status ownership into `bot_brain.*`.
- `bot_think.cpp` keeps the stable `Bot_*` wrapper surface for existing game/server extension callers, while `bot_brain.cpp` owns the existing route steering, look-ahead steering, velocity lead, recovery command, movement-state button intent, and frame-command smoke status implementation.
- No new upstream source files were imported for this slice; this is WORR-native module ownership around the existing BotLib/AAS adapter and native navigation path.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; normal dedicated `sv_bot_frame_command_smoke 3` on `mm-rage` reports `frames=17`, `commands=17`, `route_failures=0`, `movement_state_commands=0`, and `pass=1`; forced jump `sv_bot_frame_command_smoke 5` reports `movement_state_jump_commands=17` and `pass=1`; stalled `sv_bot_frame_command_smoke 4` reports `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-bot-brain-command-ownership-2026-06-18.md`.

## Native Bridge Update: Nav Position Goals

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_brain.cpp`, `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/q3a/q3a_botlib_import.*`, and `src/server/main.c` now support a debug/smoke world-position route goal path.
- `bot_nav.*` resolves position goals through the adapter, records position-goal counters, and skips item-goal scans while a position goal is active.
- `Q3A_BotLibImport_BuildRouteSteerToGoal()` and `BotLibAdapter_BuildRouteSteerToGoal()` preserve the exact resolved goal origin for preferred position routes while leaving existing area-only item and fallback routes on `BuildRouteSteer()`.
- No new upstream source files were imported for this slice; this is WORR-native adapter and command-policy work over already imported Q3A AAS route-query output.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 8` on `mm-rage` reports `position_goal_requests=8`, `position_goal_resolved=8`, `position_goal_assignments=1`, `position_goal_cache_reuses=6`, `item_goal_scans=0`, `route_goal_fallbacks=0`, `last_position_goal_area=227`, `last_position_goal_z=98`, `route_failures=0`, and `pass=1`; normal, stalled, and forced-jump regression smokes also report `route_failures=0` and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-position-goal-2026-06-18.md`.

## Native Bridge Update: Nav Natural Travel Goals

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_brain.cpp`, `src/game/sgame/bots/bot_nav.*`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/q3a/q3a_botlib_import.*`, and `src/server/main.c` now support an internal travel-type route-goal smoke path.
- `Q3A_BotLibImport_BuildRouteSteerForTravelType()` and `BotLibAdapter_BuildRouteSteerForTravelType()` find a route whose selected next AAS reachability matches the requested travel type.
- `Q3A_BotLibImport_FindRouteStartForTravelType()` and the adapter wrapper validate a smoke start area whose normal route result begins with the requested travel type, letting the natural movement-state smokes prove real AAS jump and ladder reachability without forcing `sg_bot_frame_command_smoke_travel_type`.
- The travel-type route helper now falls back to direct reachability endpoint selection when broad area-goal scanning misses a deterministic route, enabling route-only `TRAVEL_WALKOFFLEDGE`, route-only `TRAVEL_ELEVATOR`, and direct `TRAVEL_BARRIERJUMP` validation on packaged `mm-rage.aas`.
- No new upstream source files were imported for this slice; this is WORR-native adapter, route-request, and smoke harness work over already imported Q3A AAS route-query output.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 9` on `mm-rage` reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=107`, `last_travel_type_goal_start_goal_area=111`, `last_reachability_type=5`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; dedicated `sv_bot_frame_command_smoke 10` reports `last_travel_type_goal_start_area=142`, `last_travel_type_goal_start_goal_area=143`, `last_reachability_type=6`, `movement_state_ladder_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; dedicated `sv_bot_frame_command_smoke 11` reports `last_travel_type_goal_start_area=29`, `last_travel_type_goal_start_goal_area=34`, `last_reachability_type=7`, `route_commands=8`, `movement_state_commands=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; dedicated `sv_bot_frame_command_smoke 12` reports `last_travel_type_goal_start_area=241`, `last_travel_type_goal_start_goal_area=261`, `last_reachability_type=11`, `route_commands=8`, `movement_state_commands=0`, `movement_state_unsupported=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; dedicated `sv_bot_frame_command_smoke 13` reports `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `last_reachability=319`, `last_reachability_type=4`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; normal and elevator regression smokes also report `route_failures=0` and `pass=1`.
- Implementation logs: `docs-dev/q3a-botlib-nav-natural-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-ladder-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-walkoffledge-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-elevator-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-barrierjump-travel-goal-2026-06-18.md`.

## Native Bridge Update: Nav Rocket-Jump Route Policy

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_brain.cpp`, `src/game/sgame/bots/bot_nav.cpp`, `src/game/sgame/bots/botlib_adapter.*`, `src/game/sgame/bots/q3a/q3a_botlib_import.*`, and `src/server/main.c` now support a runtime rocket-jump route policy.
- `Q3A_BotLibImport_SetRoutePolicy()` keeps `TRAVEL_ROCKETJUMP` out of default route flags and adds `TFL_ROCKETJUMP` only when `sg_bot_allow_rocketjump` is enabled.
- Direct travel-type start/route helpers obey the same policy as ordinary route queries, so debug/smoke travel-type requests cannot bypass the default-off rocket-jump gate.
- No new upstream source files were imported for this slice; this is WORR-native adapter, route-policy, and smoke-harness work over already imported Q3A AAS route-query output.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 14` on `mm-rage` enables `sg_bot_allow_rocketjump 1` and reports `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=282`, `last_travel_type_goal_start_goal_area=304`, `last_reachability=312`, `last_reachability_type=12`, `route_failures=0`, and `pass=1`; dedicated `sv_bot_frame_command_smoke 15` leaves rocket-jump routing disabled and reports `travel_type_goal_expect_blocked=1`, `commands=0`, `route_commands=0`, `travel_type_goal_resolved=0`, `travel_type_goal_assignments=0`, `travel_type_goal_start_warps=0`, `route_failures=8`, and `pass=1`; normal, elevator, and barrier-jump regression smokes also report `route_failures=0` and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-rocketjump-policy-2026-06-18.md`.

## Native Bridge Update: Nav Four-Bot Frame Command Smoke

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native file `src/server/main.c` now supports `sv_bot_frame_command_smoke 16`, a four-bot route-command validation mode.
- The smoke harness grows the bot target one bot per server frame, reports `q3a_bot_frame_command_smoke_multi_bot_target=4`, and reuses the existing route-command status contract with `expected_min_frames=4` and `expected_min_commands=4`.
- No new upstream source files were imported for this slice; this is WORR-native dedicated-server smoke harness work over the existing bot slot lifecycle, route cache, item-goal, item-reservation, and command dispatch paths.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 16` on `mm-rage` adds `B|Mover`, `B|MoverTwo`, `B|MoverThree`, and `B|MoverFour`, then reports `frames=38`, `commands=38`, `route_requests=38`, `route_queries=11`, `route_refreshes=11`, `route_reuses=27`, `route_commands=38`, `route_failures=0`, `route_goal_assignments=4`, `item_goal_assignments=4`, `item_goal_reservation_skips=6`, `item_goal_active_reservations=4`, `route_debug_routes=38`, `route_debug_goals=38`, and `pass=1`; normal two-bot `sv_bot_frame_command_smoke 3` regression also reports `route_failures=0`, `item_goal_active_reservations=2`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-four-bot-frame-command-smoke-2026-06-18.md`.

## Native Bridge Update: Nav Eight-Bot Frame Command Smoke

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native file `src/server/main.c` now supports `sv_bot_frame_command_smoke 17`, an eight-bot route-command validation mode.
- The mode reuses the existing one-bot-per-frame smoke add loop and extends the smoke bot-name helper through `B|MoverEight`.
- No new upstream source files were imported for this slice; this is WORR-native dedicated-server smoke harness work over the existing bot lifecycle, item-reservation, route-cache, debug overlay, and command dispatch paths.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 17` on `mm-rage` adds `B|Mover` through `B|MoverEight`, then reports `frames=92`, `commands=92`, `route_requests=92`, `route_queries=29`, `route_refreshes=29`, `route_reuses=63`, `route_commands=92`, `route_failures=0`, `route_goal_assignments=11`, `item_goal_assignments=11`, `item_goal_reservation_skips=49`, `item_goal_active_reservations=8`, `route_debug_routes=92`, `route_debug_goals=92`, and `pass=1`; four-bot `sv_bot_frame_command_smoke 16` regression also reports `route_failures=0`, `item_goal_active_reservations=4`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-eight-bot-frame-command-smoke-2026-06-18.md`.

## Native Bridge Update: Nav Soak Frame Command Smoke

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native file `src/server/main.c` now supports `sv_bot_frame_command_smoke 18`, an eight-bot long-running route-command soak with configurable `sv_bot_frame_command_smoke_soak_ms` duration.
- WORR native file `src/server/user.c` now preserves bot fake-client command accounting during server-authored command playback so long bot runs do not hit the human-client `commandMsec underflow` path.
- WORR native files `src/game/sgame/bots/bot_nav.*` and `src/game/sgame/bots/bot_brain.cpp` now report peak active item reservations and keep mode `18` pass semantics focused on sustained commands plus zero route failures while mode `17` remains the short reservation-pressure proof.
- No new upstream source files were imported for this slice; this is WORR-native dedicated-server smoke harness, command-accounting, and bot status work over the existing bot lifecycle, route-cache, item-goal, and command dispatch paths.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated `sv_bot_frame_command_smoke 18` on `mm-rage` ran for `elapsed_ms=600001`, reported `reports=9`, `frames=192036`, `commands=192036`, `route_requests=187232`, `route_commands=192036`, `route_failures=0`, `route_goal_assignments=4889`, `item_goal_assignments=1451`, `item_goal_reservation_skips=3455`, `item_goal_active_reservations=1`, `item_goal_peak_active_reservations=2`, `stuck_detections=11789`, `stuck_recovery_activations=11789`, `recovery_command_uses=72066`, `skipped_inactive=0`, and `pass=1`; eight-bot `sv_bot_frame_command_smoke 17` regression reports `item_goal_active_reservations=8`, `item_goal_peak_active_reservations=8`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-soak-frame-command-smoke-2026-06-18.md`.

## Native Bridge Update: Nav Map-Change Repeat Smoke

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native file `src/server/main.c` now supports `sv_bot_frame_command_smoke 19`, a same-map reload repeat smoke for the eight-bot route-command path.
- The mode adds `sv_bot_frame_command_smoke_map_repeat_cycles` and `sv_bot_frame_command_smoke_map_repeat_reload_timeout_ms`, emits explicit cycle/reload/cleanup markers, and fails cleanly on reload timeout.
- No new upstream source files were imported for this slice; this is WORR-native dedicated-server smoke harness work over the existing bot lifecycle and route-command path.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; dedicated mode `19` on `mm-rage` reports cycle 1 `route_failures=0`, `item_goal_peak_active_reservations=8`, and `pass=1`, observes same-map reload in `61` ms, reports cycle 2 `route_failures=0`, `item_goal_peak_active_reservations=8`, and `pass=1`, and completes with `cycles=2`, `map_changes=1`, `final_count=0`; mode `17` regression also passes.
- Implementation log: `docs-dev/q3a-botlib-nav-map-change-repeat-smoke-2026-06-18.md`.

## Native Bridge Update: Nav Map Restart Lifecycle Smoke

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native file `src/server/main.c` now extends mode `19` with `sv_bot_frame_command_smoke_map_repeat_restart`, default `0`, so the same lifecycle proof can exercise either `gamemap` or forced `map "<current map>" force` reload behavior.
- The mode reports `command=<gamemap|map_force>`, `restart=<0|1>`, `realtime_reset=<0|1>`, and a cleanup status gate that requires both bot count and active item reservations to return to zero between cycles.
- No new upstream source files were imported for this slice; this is WORR-native dedicated-server lifecycle-smoke hardening.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; forced restart mode `19` on `mm-rage` passed three proof cycles, two forced restart transitions, all cleanup checks with `count=0 active_reservations=0 pass=1`, final `cycles=3`, `map_changes=2`, `final_count=0`, and no `commandMsec underflow`; default `gamemap` regression still passes with two cycles, one map change, and final count zero.
- Implementation log: `docs-dev/q3a-botlib-nav-map-restart-lifecycle-smoke-2026-06-18.md`.

## Native Asset and Tooling Update: Botfiles Profile Pack

Date: 2026-06-18

Tasks: `FR-04-T13`, `DV-03-T05`, `DV-07-T06`, `DV-08-T05`

- WORR native profile assets now live under `assets/botfiles/` with
  Q3/Gladiator-style `botfiles/bots/*_c.c` character entry points plus
  `_w.c`, `_i.c`, and `_t.c` companions for the first-party profile IDs
  `smoke`, `vanguard`, `bulwark`, `relay`, and `vector`.
- Local Quake III Arena and Gladiator botfiles were used as format references
  for directory shape, suffix conventions, and broad script vocabulary only. No
  Q3A, Gladiator, external profile pack, or imported script text was copied for
  this slice.
- WORR native file `src/server/main.c` now reports deterministic profile scan
  markers, parse diagnostics, reload counts, and `sv_bot_profile_smoke_target`.
- WORR native tools now include `tools/bot_profiles/validate_bot_profiles.py`
  and `tools/test_package_assets.py`; the validator strips `_c` profile
  suffixes, skips `_w/_i/_t` companions, and accepts the authored Q3-style
  `CHARACTERISTIC_*` plus `WORR_*` bridge subset. `tools/package_assets.py`
  packages botfiles into `pak0.pkz` and mirrors `botfiles` loose in refreshed
  installs for no-zlib dedicated builds.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; profile validator passed with 5 files, 5 profiles, 0 errors, and 0 warnings; `profile_backed_spawn` passed; the implemented bot scenario suite passed 5/5.
- Implementation logs: `docs-dev/q3a-botlib-native-botfiles-assets-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-validation-tool-2026-06-18.md`, `docs-dev/q3a-botlib-profile-loader-hardening-2026-06-18.md`, `docs-dev/q3a-botlib-profile-scenario-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-loose-staging-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-user-docs-2026-06-18.md`, and `docs-dev/q3a-botlib-q3-style-botfiles-2026-06-18.md`.

## Native Bridge Update: Nav Natural Movement and Interaction Retry

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*` and `src/game/sgame/bots/bot_brain.cpp` now report natural crouch/swim/waterjump AAS support status and first-pass interaction wait/use retry telemetry.
- Current packaged `mm-rage.aas` reports no natural crouch, swim, or water-jump routes, so the slice records exact reference-map requirements instead of claiming runtime proof for unsupported travel types.
- Follow-up telemetry adds unsupported masks, per-type reason codes, resolved AAS area/goal-area fields, route-start origins for future reference maps, and interaction context counters by world entity type.
- The elevator/platform proof now records interaction wait/use activations and command uses while keeping the existing route-command smoke pass gates.
- No new upstream source files were imported for this slice; this is WORR-native navigation policy/status work over existing imported route output.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; elevator smoke mode `12` passes with `nav_interaction_elevator_activations=1`, `interaction_wait_command_uses=8`, and `interaction_use_command_uses=8`; forced crouch mode `6` and forced swim mode `7` still pass.
- Implementation logs: `docs-dev/q3a-botlib-nav-natural-movement-door-retry-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-interaction-diagnostics-2026-06-18.md`.

## Native Bridge Update: Behavior Action Dispatcher Boundary

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_actions.*`, `bot_items.*`, and `bot_combat.*` now provide the first compile-ready action/decision boundary above the movement path.
- The boundary exposes status-bearing APIs for future item, combat, inventory, weapon, and world-use policy while keeping weapon switching and inventory use as intent-only until `bot_brain.*` integration.
- WORR native file `src/game/sgame/bots/bot_brain.cpp` now samples that boundary as telemetry only and emits `q3a_bot_action_status`; it intentionally does not call `BotActions_ApplyDecision()` or mutate attack/use command buttons from this new boundary.
- `meson.build` now compiles the new WORR-native units. No new upstream source files were imported.
- Validation: `meson compile -C builddir-win`; `refresh_install.py --package-q2aas-aas`; mode `17` smoke reports `action_evaluations=92`, `action_noop_decisions=92`, `action_applied_cmds=0`, `action_applied_attack_buttons=0`, and `action_applied_use_buttons=0`; `git diff --check` with only existing LF/CRLF warnings.
- Implementation logs: `docs-dev/q3a-botlib-behavior-action-dispatcher-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-brain-telemetry-2026-06-18.md`.

## Native Tooling Update: Scenario and Performance Validation

Date: 2026-06-18

Tasks: `DV-03-T05`, `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T16`, `DV-07-T06`

- WORR native tool directory `tools/bot_scenarios/` now contains a local scenario-smoke harness, catalog output, JSON/Markdown/comparison reports, fixture-aware parser tests, and a README.
- WORR native tool directory `tools/bot_scenarios/` now also contains pending promotion metadata and a `--pending-gap-report` mode that evaluates existing JSON reports for missing scenario rows and missing promotion counters without launching the game.
- WORR native tool directory `tools/bot_perf/` now contains a bot smoke log analyzer, default soak budget, multi-run comparison and Markdown reporting, scenario-report duration sidecar support, comparison guard warnings, parser/fixture tests, and a README.
- `docs-dev/q3a-botlib-pending-scenario-counters-2026-06-18.md` records proposed status counters and pass gates for enemy engagement, weapon switching, health/armor pickup, and team-objective scenario promotion.
- `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md` records the follow-up source-counter plan for true bot CPU, route CPU, visibility/PVS/PHS, and trace-pressure budgets.
- No new upstream source files were imported for this slice; this is WORR-native validation tooling and planning work.
- Validation: scenario harness tests pass with eight standard-library tests and fixture validation when `.tmp/bot_scenarios/latest_report.json` exists; pending-gap report against the current implemented report returns four blocked pending rows; perf analyzer tests pass with eight standard-library tests and the real soak fixture; the default soak budget passes the current ten-minute mode `18` log.
- Implementation logs: `docs-dev/q3a-botlib-scenario-smoke-harness-2026-06-18.md`, `docs-dev/q3a-botlib-pending-scenario-counters-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-pending-gap-report-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`.

## Native Tooling Update: High-Bot Degradation Policy

Date: 2026-06-18

Tasks: `DV-03-T05`, `FR-04-T16`

- WORR native tool `tools/bot_scenarios/run_bot_scenarios.py` now records an explicit high-bot degradation policy that keeps the short eight-bot reservation pressure proof strict while allowing long-soak item reservation occupancy to decay under the manual mode `18` soak.
- The policy is emitted through catalog, JSON, Markdown, and text reports so scenario evidence can distinguish expected long-run item churn from command-throughput, route-cleanliness, active-bot-count, route-slot, debug-coverage, or progress-report failures.
- No new upstream source files or bot behavior imports were added for this slice; this is local scenario-harness status policy over the existing smoke modes and performance budget file.
- Validation: `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py`; `python tools\bot_scenarios\test_run_bot_scenarios.py` passed 23 tests; catalog text output reports `high_bot_soak_degradation` as manual-only with `high_bot_long_soak` policy metadata.
- Implementation log: `docs-dev/q3a-botlib-high-bot-degradation-policy-2026-06-18.md`.

## Native Tooling Update: High-Bot Soak Budget Sidecar

Date: 2026-06-18

Tasks: `DV-03-T05`, `DV-05-T02`, `DV-05-T05`, `FR-04-T16`

- WORR native file `tools/bot_perf/default_soak_budget.json` now documents and enforces the manual eight-bot soak invariants that should remain strict even while long-run item-reservation occupancy is allowed to decay.
- WORR native documentation in `tools/bot_perf/README.md` explains the manual mode `18` launch/analyze flow, the distinction between strict command/route guardrails and allowed reservation churn, and optional CPU checks for legacy soak logs.
- No new upstream source files, copied algorithms, or behavior imports were added for this slice; this is local budget metadata and operator documentation over existing scenario and perf tooling.
- Validation: JSON validation passed; the existing ten-minute fixture passed `tools/bot_perf/default_soak_budget.json` with 22 checks and expected optional CPU-field warnings; bot perf tests passed 12 tests; scenario catalog output reports `high_bot_soak_degradation` as manual-only mode `18`.
- Implementation log: `docs-dev/q3a-botlib-high-bot-soak-budget-2026-06-18.md`.

## Native Release Update: BotLib Packaging Hardening

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T13`, `FR-04-T16`, `DV-08-T05`, `DV-07-T06`

- WORR native release tools now enforce complete BotLib botfile payloads in `assets/botfiles`, validate required profile/script companion families, and hash-check both `pak0.pkz` members and loose `.install/basew/botfiles` mirrors against source assets.
- `tools/refresh_install.py` now passes botfile and generated q2aas AAS archive-member expectations into `tools/release/validate_stage.py`, and requires staged q2aas AAS outputs to provide valid SHA-256 values before release archive validation.
- No new upstream source files, botfile text, or AAS generator imports were added for this slice; this is WORR-native release packaging and staging hardening over existing local assets and q2aas stage reports.
- Validation: Python compile passed for `tools\package_assets.py`, `tools\refresh_install.py`, and `tools\test_package_assets.py`; `python tools\test_package_assets.py -v` passed 8 tests; a scratch `refresh_install.py --package-q2aas-aas --platform-id windows-x86_64` run validated 30 botfile package/loose files and packaged `maps/mm-rage.aas` with SHA-256 `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
- Implementation log: `docs-dev/q3a-botlib-release-packaging-hardening-2026-06-18.md`.

## Native Tooling Update: Q2AAS Reference Map Coverage

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

- WORR native q2aas validation now records manifest `coverage_categories`, optional reference map candidates, skipped missing optional maps, and reference-coverage summaries without implying that the current single staged map covers the full future reference set.
- `tools/q2aas/validation_manifest.json`, `tools/q2aas/validate_worr_q2aas.py`, and `tools/aas_inventory/inventory_aas_assets.py` report the current `mm-rage` coverage as ready while marking id deathmatch, open deathmatch, CTF, campaign, and liquid/hazard reference categories incomplete until their BSPs are staged.
- No `q2proto/` files were changed and no new upstream source files were imported for this slice; this is WORR-native validation, manifest, and inventory reporting around existing q2aas output.
- Validation: Python compile and JSON manifest checks passed; q2aas validation unit tests passed 2 tests; AAS inventory unit tests passed 3 tests; inventory exits `0` for the current staged set; `meson compile -C builddir-win q2aas-staged-smoke` exits `0`; strict `--require-reference-coverage` exits `2` as expected while optional reference candidates remain unstaged.
- Implementation log: `docs-dev/q2aas-reference-map-coverage-2026-06-18.md`.

## Native Bridge Update: Static BSP Trace CPU Counters

Date: 2026-06-18

Tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T16`

- WORR native adapter/status code now exposes CPU timing for active-map static BSP trace and point-contents work through `bsp_trace_cpu_ns`, `bsp_trace_cpu_samples`, and `bsp_trace_cpu_max_ns` on `q3a_bot_source_counter_status`.
- `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `botlib_adapter.*`, `bot_brain.cpp`, and `tools/bot_perf/analyze_bot_perf.py` carry the counters into scenario/perf logs and prefer `bsp_trace_cpu_samples` as the average denominator while preserving older log fallback behavior.
- No new upstream source files were imported for this slice; this is WORR-native timing instrumentation around the existing Q3A BotLib import boundary and existing Q2 BSP collision callbacks.
- Validation: Python compile passed for the bot perf analyzer/tests; analyzer unit tests passed 12 tests; focused Ninja object builds passed for `q3a_botlib_import.c`, `botlib_adapter.cpp`, and `bot_brain.cpp`; `meson compile -C builddir-win sgame_x86_64` passed.
- Implementation log: `docs-dev/q3a-botlib-static-bsp-trace-cpu-2026-06-18.md`.

## Native Bridge Update: Entity Trace Clip CPU Counters

Date: 2026-06-18

Tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

- WORR native adapter/status code now exposes CPU timing for the dynamic entity clip callback path used by imported Q3A BotLib AAS queries when they cross into WORR `gi.clip(...)`.
- `src/game/sgame/bots/q3a/q3a_botlib_import.*`, `botlib_adapter.*`, and `bot_brain.cpp` carry `entity_trace_clip_calls`, result buckets, total CPU nanoseconds, and max CPU nanoseconds into `q3a_bot_source_counter_status`.
- No new upstream source files or copied behavior logic were imported for this slice; this is local timing instrumentation around the existing WORR entity collision callback bridge.
- Validation: Python compile and bot perf analyzer tests passed; focused object builds passed for `q3a_botlib_import.c`, `botlib_adapter.cpp`, and `bot_brain.cpp`; `meson compile -C builddir-win sgame_x86_64` passed.
- Implementation log: `docs-dev/q3a-botlib-entity-trace-clip-cpu-2026-06-18.md`.

## Native Bridge Update: Weapon and Inventory Command Request API

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_actions.*` now translates validated pending weapon-switch or inventory-use decisions into inspectable command-request objects without submitting client commands or changing `bot_brain.cpp` command ownership.
- The helper surface records command-request status counters, validates client/item/request kind safety, emits exact `use_index_only` requests for accepted weapon or inventory decisions, and intentionally leaves live inventory count, game state, selected-item state, and final command dispatch to future authoritative integration.
- No new upstream source files or copied Q3A behavior code were imported for this slice; this is a local command-request API above the existing WORR-native action boundary.
- Validation: focused `bot_actions.cpp` object compile passed; the initial full build exposed a separate team-role helper declaration issue, which the later team-role integration fixed; final round validation rebuilt `sgame_x86_64` successfully.
- Implementation log: `docs-dev/q3a-botlib-weapon-inventory-command-api-2026-06-18.md`.

## Native Bridge Update: Team Role Policy Helpers

Date: 2026-06-18

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_objectives.*` now adds deterministic returner/support role policy helpers above the existing objective proof layer, while preserving explicit requested roles for the current smoke paths.
- `src/game/sgame/bots/bot_brain.cpp` now emits the new role-policy evaluation, selection, fallback, and priority-breakdown counters on `q3a_bot_objective_status`.
- No new upstream source files or copied Q3A team behavior were imported for this slice; this is local policy scaffolding for future autonomous CTF/TDM role consumption.
- Validation: `meson compile -C builddir-win sgame_x86_64` passed after the integration fix; `python tools\bot_scenarios\test_run_bot_scenarios.py` passed 23 tests.
- Implementation log: `docs-dev/q3a-botlib-team-role-policy-2026-06-18.md`.

## Native Bridge Update: AAS Memory Source Counters

Date: 2026-06-18

Tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T12`

- WORR native source-counter status now includes the existing BotLib zone/hunk allocator active and peak byte counters on `q3a_bot_source_counter_status`, alongside route, visibility, trace, and CPU fields.
- The current status surface records `q3a_memory_zone_active`, `q3a_memory_zone_peak`, `q3a_memory_hunk_active`, `q3a_memory_hunk_peak`, `q3a_memory_total_active`, `q3a_memory_total_peak`, `q3a_memory_failures`, and `q3a_memory_available`.
- No new upstream source files were imported for this slice; this is local status emission over allocator counters already owned by the WORR BotLib adapter boundary.
- Validation: final round validation rebuilt `sgame_x86_64` and `worr_ded_engine_x86_64`, refreshed `.install`, reran the 9/9 implemented bot scenario suite, passed the bot perf analyzer tests, and confirmed the latest `engage_enemy` parse reports the `q3a_memory` source-counter group with no missing source-counter groups.
- Implementation log: `docs-dev/q3a-botlib-aas-memory-source-counters-2026-06-18.md`.

## Candidate Source Inventory

These files were audited as likely first candidates or reference points. BSPC candidates now land through the `tools/q2aas/` snapshot; the first Q3A utility, AAS file-loader, AAS sampling, AAS reachability, AAS clustering, AAS route-query, AAS alternative-routing, AAS optimization, AAS start-frame, AAS entity-cache, AAS movement, and AAS debug helper subsets are imported and recorded above, while the WORR-owned entity-sync, entity-trace, BSP leaf-link/box-query, debug draw, route-overlay, debug-polygon, debug-area, cluster, alternative-route, memory allocator, filesystem, route-cache miss policy, lifecycle telemetry, bot frame command dispatch, route-steered frame command, nav route-cache, nav debug-overlay, nav reachability-debug, nav polyline-debug, nav debug-client-filter, nav persistent-goal, nav item-goal, nav item-reservation, nav look-ahead steering, nav velocity-aware steering, nav stuck-repath, nav stuck recovery command, nav goal-blacklist cooldown, nav failed-goal reason, nav movement-state commands, bot brain command ownership, nav position-goal, nav natural travel-goal including barrier-jump direct reach validation, nav rocket-jump route policy, nav four-bot frame-command smoke, nav eight-bot frame-command smoke, nav soak frame-command smoke, nav map-change repeat/restart smoke, nav natural movement support diagnostics, behavior action dispatcher and telemetry boundary, weapon/inventory command-request API, static BSP trace CPU counters, entity-clip CPU counters, AAS memory source counters, team-role policy helpers, bot validation tooling, high-bot degradation policy and soak budget, q2aas reference-map coverage reporting, release packaging hardening, Q3-style WORR botfile layout correction, and legacy Q2R bot surface removal work is recorded as native adapter, tooling, status, or replacement work. The remaining Q3A runtime and behavior files remain reference-only until matched to a pinned source.

| Candidate | Upstream / Local Ref | Current Use Decision | Required Before Import |
|---|---|---|---|
| `bspc.c` | `TTimo/bspc` commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Imported and locally tailored at `tools/q2aas/bspc.c`. | Modified row above records retained header, credits, and WORR reachability changes. |
| `map_q2.c` | `TTimo/bspc` commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Imported at `tools/q2aas/map_q2.c`. | Preserve GPL header/license and credit Ben Noordhuis from fork history plus original id Software header when this file is locally tailored. |
| `l_bsp_q2.c` | `TTimo/bspc` commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Imported at `tools/q2aas/l_bsp_q2.c`. | Preserve GPL header/license and credit Ben Noordhuis and Thomas Koeppe from fork history plus original id Software header when this file is locally tailored. |
| `q2files.h` | `TTimo/bspc` commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Imported at `tools/q2aas/q2files.h`. | Preserve GPL header/license and credit Ben Noordhuis from fork history plus original id Software header when this file is locally tailored. |
| `map.c` | `TTimo/bspc` commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Imported and locally tailored at `tools/q2aas/map.c`. | Modified row above records retained header, credits, and WORR Q2 BSP lifetime changes. |
| `be_aas_bspc.c` | `TTimo/bspc` commit `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Imported and locally tailored at `tools/q2aas/be_aas_bspc.c`. | Modified row above records retained header, credits, and WORR Q2 BotLib trace bridge changes. |
| Q3A BotLib runtime files | `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\botlib`; public mirror commit `dbe4ddb10315479fc00086f08e25d968b4b43c49` for imported files | First utility, AAS file-loader, AAS sampling, AAS reachability, AAS clustering, AAS route-query, AAS alternative-routing, AAS optimization, AAS start-frame, AAS entity-cache, AAS movement, and AAS debug helper subsets imported and recorded above; remaining runtime and behavior files are reference-only for now. | Match each future file to a commit-pinned upstream source, then add per-file ledger rows before copying. |
| Q3A `g_bot.c` / `ai_*.c` behavior files | `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\game`; must be matched to pinned public source before import | Concept/reference only for WORR-native behavior translation. | Prefer concept-reference rows unless code is directly copied. |
| Q3A `sv_bot.c` | `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\server`; must be matched to pinned public source before import | Fake-client/BotLib adapter reference only. | Do not copy server slot allocation directly; document any adapter code as WORR-native unless source is copied. |

## Contributor Discovery Checklist

For each imported upstream file or copied algorithm:

- [ ] Capture the upstream commit hash.
- [ ] Preserve the file's original copyright/license header.
- [ ] Run source history review where available, for example `git log --follow -- <path>`.
- [ ] Add distinct upstream contributors to the `Contributors` field when they can be identified from file history or headers.
- [ ] Add `Modified for WORR` notes when the local file diverges from upstream.
- [ ] Record whether the work is a direct import, derivative, concept reference, or clean WORR-native implementation.
- [ ] Verify release packaging includes required license/credit material if the imported code ships in binaries or source archives.

## Notes

- Do not use this ledger as legal advice. It is a project hygiene artifact that keeps engineering, review, and release work honest.
- If a file is only used as inspiration, record that as a concept reference rather than implying copied source.
- If future work consults additional projects such as Quake3e, baseq3a, or ioquake3, add them here before their code or algorithms influence implementation.
