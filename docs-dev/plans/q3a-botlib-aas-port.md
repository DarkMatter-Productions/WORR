# Quake III Arena BotLib and Q2 AAS Port Plan

Date: 2026-06-16

Status: Implementation In Progress

Roadmap tasks: `FR-04-T01` through `FR-04-T07`, `FR-04-T10` through `FR-04-T16`, and `DV-07-T06`.

## Purpose

Elegantly port the Quake III Arena bot system into WORR by bringing across the useful architecture, data formats, tooling, and behavior patterns while respecting WORR's Quake II Rerelease gameplay, server-game ownership, build layout, and documentation rules.

The port is not a blind file drop. The target is a maintained WORR bot stack with:

- A Quake II map aware AAS generator based on `TTimo/bspc`.
- A rehosted Q3A BotLib/AAS runtime behind a narrow WORR adapter.
- WORR-native fake-client, command, cvar, match, and debug integration.
- Q2/Q2R weapon, item, movement, team, and coop behavior layered above the imported navigation primitives.
- Full source credit, license, and provenance tracking for every upstream-derived file and concept.

## Source Baseline

Primary local source:

- Quake III Arena source tree: `E:\_SOURCE\_CODE\Quake-III-Arena-master`
- Q3A BotLib runtime: `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\botlib`
- Q3A game bot behavior: `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\game\g_bot.c`, `ai_main.c`, `ai_dmq3.c`, `ai_dmnet.c`, `ai_team.c`, `ai_chat.c`, `ai_cmd.c`, and related `be_ai_*.h` APIs.
- Q3A fake-client and BotLib import glue: `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\server\sv_bot.c`
- Q3A BSPC/AAS generator lineage: `E:\_SOURCE\_CODE\Quake-III-Arena-master\code\bspc`

Required external tool baseline:

- `TTimo/bspc`: `https://github.com/TTimo/bspc`
- The repository describes itself as the Quake III Arena BSP-to-AAS compiler, is forked from `bnoordhuis/bspc`, includes Quake II BSP/map support files such as `l_bsp_q2.c`, `map_q2.c`, and `q2files.h`, and is licensed as GPL-2.0-or-later per its README/license.

Current WORR bot surface:

- `src/game/sgame/bots/bot_think.cpp` keeps `Bot_BeginFrame` and `Bot_EndFrame` as AAS-gated lifecycle stubs and now exposes an AAS route-steered `Bot_BuildFrameCommand()` path for server-owned bot `usercmd_t` dispatch.
- `src/game/sgame/bots/bot_exports.cpp` already exposes bot action helpers for weapon selection, item use, trigger use, forced look direction, and pickup checks.
- `src/game/sgame/bots/bot_utils.cpp` already builds useful bot-facing entity state and registers players, monsters, items, traps, and movers through the game import API.
- `src/game/sgame/bots/bot_debug.cpp` already exercises engine-side navigation/debug imports through `Bot_MoveToPoint`, `Bot_FollowActor`, `GetPathToGoal`, and debug drawing.
- `src/game/sgame/client/client_session_service_impl.cpp` already marks bot clients with `SVF_BOT` and calls `Bot_BeginFrame`.
- `src/game/sgame/player/p_view.cpp` already calls `Bot_EndFrame`.

## Guiding Decisions

1. Port the architecture before the behavior. First make AAS generation, BotLib loading, entity updates, and bot input plumbing reliable; only then expand combat/team intelligence.

2. Keep Q3A code in a quarantine boundary. Upstream-derived C code should live in clearly named BotLib/tooling directories with minimal local edits, explicit wrappers, and retained license headers. WORR behavior should call into it through adapter APIs instead of spreading Q3 globals through `sgame`.

3. Build Q2-native behavior above Q3 primitives. Q3A's Area Awareness System, goal weights, movement results, character files, and team AI are valuable. Q3A's item IDs, weapons, powerups, game modes, VM trap model, and player movement assumptions need translation.

4. Make AAS a first-class build artifact. Bots must not depend on hand-authored waypoints. AAS generation should be reproducible, validated, and staged with `.install/basew` whenever maps/assets are packaged.

5. Use `sg_` for new server-game cvars. Existing imported `bot_*` debug names may be retained inside quarantined upstream compatibility layers, but new public WORR controls should use `sg_bot_*`.

6. Preserve credits continuously. Every phase has an explicit credit/provenance checklist. A task is not done until source attribution is updated.

## Q3A to WORR Difference Matrix

This project must treat game and architecture differences as first-order design input, not cleanup after the port.

| Area | Quake III Arena Assumption | WORR / Quake II Rerelease Reality | Porting Response |
|---|---|---|---|
| Game module architecture | Q3 game code calls engine traps from a VM-style boundary. | WORR `sgame` is native C++ and already has KEX/Q2R-style game imports. | Replace trap calls with a narrow BotLib adapter and keep imported globals quarantined. |
| Fake clients | Q3 server owns `SV_BotAllocateClient`, `NA_BOT`, and BotLib import setup. | WORR already has client session services, `SVF_BOT`, and match logic that distinguishes bots from humans. | Integrate through WORR session services, not by copying Q3 server slot code directly. |
| Movement physics | Q3 pmove, acceleration, jump, crouch, and weapon movement expectations. | Q2 movement, step heights, water movement, crouch behavior, ladders where supported, knockback, and Q2 weapon timing. | Translate route results into Q2 `usercmd_t` steering and tune AAS reachability to Q2 movement constants. |
| Map format | Q3 BSP plus Q3-oriented AAS generation. | Q2 IBSP38 maps, Q2R-era extensions/tolerance needs, `.pak`/`.pkz` staging under `basew`. | Tailor `TTimo/bspc` Q2 loaders and configs; validate BSPX/extra lump tolerance before runtime reliance. |
| Navigation data | Q3 maps generally expect prebuilt `.aas` for BotLib. | WORR must support Q2 maps without manual waypoint files. | Make `worr_q2aas` reproducible and package/generate AAS as part of the build/release workflow. |
| Entity model | Q3 `gentity_t`, `entityState_t`, item/powerup IDs, and event model. | WORR has Q2/KEX `gentity_t`, `gclient_t`, `sv` bot-facing state, monsters, traps, movers, and Q2 item IDs. | Build explicit entity-state translation and avoid direct struct reuse. |
| Weapons | Q3 weapons and ammo economy. | Q2 weapons, ammo, inventory items, expansion weapons, splash rules, BFG behavior, weapon switching helpers. | Create WORR weapon metadata and use existing bot export helpers instead of Q3 weapon IDs. |
| Items and goals | Q3 item config, weights, and arena item distribution. | Q2 pickups, armor tiers, powerups, keys/objectives in coop, flags/techs in team modes. | Keep the goal/weight concept but author Q2-specific item configs and filters. |
| Match modes | Q3 FFA, tournament, team, CTF assumptions. | WORR includes Q2/Q2R match states, map voting, mymap queue, warmup/intermission logic, and possible coop scope. | Integrate with existing match services and make bots opt into each mode deliberately. |
| Coop/NPC context | Q3 bot AI is multiplayer-arena focused. | WORR also has monsters/NPCs, campaign maps, triggers, doors, and coop progression needs. | Keep coop as a later phase with explicit follow/wait/lead/resource-sharing behavior. |
| Filesystem/assets | Q3 `baseq3`, `pk3`, botfiles, arenas, scripts. | WORR `basew`, `.install`, `pak0.pkz`, current asset packaging rules. | Adapt paths and staging; do not assume Q3 directory names survive publicly. |
| Build system | Q3 make/project files and old C assumptions. | WORR Meson, mixed C/C++20, warning cleanup, local `.install` staging. | Build imported C behind Meson object/static-library boundaries and document warning exceptions. |
| Debug rendering | Q3 debug lines/polygons through server/BotLib hooks. | WORR already has debug draw imports (`Draw_Line`, `Draw_Point`, `Draw_Bounds`, etc.). | Map Q3 debug primitives to WORR draw APIs and preserve developer cvar gates. |
| Protocol/networking | Q3 bot commands and client state are Q3-specific. | WORR must preserve legacy Q2 server/demo compatibility and avoid q2proto churn. | Keep bot control server-side; do not change protocol unless a separate task explicitly proves the need. |
| Licensing/provenance | Q3A and BSPC are GPL-family upstreams with historical file headers. | WORR also carries GPL-compatible lineage and local ZeniMax/WORR headers. | Maintain file-level source ledger, retain headers, and credit direct imports separately from inspirations. |

## Target Architecture

```mermaid
flowchart LR
    A["Q2/Q2R BSP + entities"] --> B["WORR q2aas generator"]
    B --> C["maps/<map>.aas"]
    C --> D["BotLib AAS runtime"]
    E["WORR sgame entity state"] --> F["BotLib adapter"]
    F --> D
    D --> G["WORR bot brain"]
    G --> H["usercmd_t / client session"]
    G --> I["weapon/item/use helpers"]
    G --> J["match/team/coop systems"]
    K["Debug draw + telemetry"] --> G
    K --> D
```

Target source layout, subject to adjustment during implementation:

- `tools/q2aas/`: WORR-tailored BSP-to-AAS generator derived from `TTimo/bspc`.
- `src/game/sgame/bots/q3a/`: imported or lightly wrapped Q3A BotLib runtime code, kept behind C-compatible adapter boundaries.
- `src/game/sgame/bots/bot_runtime.*`: WORR-native BotLib/AAS runtime shell, cvar registration, active-map AAS load probe, and lifecycle/status surface.
- `src/game/sgame/bots/botlib_adapter.*`: WORR import table, map load/unload, entity sync, trace/PVS/FS/memory/debug bridging.
- `src/game/sgame/bots/bot_brain.*`: WORR-native bot lifecycle, per-frame scheduling, goal selection, and usercmd generation.
- `src/game/sgame/bots/bot_nav.*`: AAS area lookup, route requests, movement steering, stuck recovery, and debug overlays.
- `src/game/sgame/bots/bot_combat.*`: weapon selection, aim, reaction, projectile prediction, ammo/resource use.
- `src/game/sgame/bots/bot_team.*`: TDM/CTF/duel/match role logic.
- `src/game/sgame/bots/bot_profiles.*`: character/profile loading and skill knobs.
- `docs-dev/plans/q3a-botlib-aas-port.md`: this project plan.
- `docs-dev/q3a-botlib-aas-credits.md`: source ledger and credit tracker, created in Phase 0 and updated with each import/adaptation.
- `docs-dev/q3a-botlib-aas-source-audit-2026-06-16.md`: first-round source audit, baseline pins, license review, and import gate.
- `docs-dev/q2aas-generator-q2-preset-validation-2026-06-16.md`: Q2 movement preset, validation harness, and first staged map smoke log.
- `docs-dev/q2aas-generator-q2-reachability-bridge-2026-06-16.md`: Q2 trace bridge, reachability, clustering, and strict staged-map validation log.
- `docs-dev/q2aas-generator-validation-matrix-2026-06-16.md`: manifest-driven staged-map validation, JSON report, and invalid-input smoke log.
- `docs-dev/q2aas-generator-deterministic-metadata-2026-06-16.md`: AAS sidecar metadata, hash/report schema, Q2 BSP header preflight, and BSPX marker detection log.
- `docs-dev/q2aas-generator-entity-diagnostics-2026-06-16.md`: entity lump parsing, brush-content diagnostics, spawn/item AAS coverage, high-value pickup reachability, and mover inventory log.
- `docs-dev/q2aas-generator-diagnostic-gates-2026-06-16.md`: staged smoke diagnostic gates for clean BSP lumps, spawn/item AAS coverage, and high-value pickup reachability.
- `docs-dev/q2aas-generator-baseline-regression-gates-2026-06-17.md`: manifest-driven structural metric and travel-count regression gates for staged AAS output.
- `docs-dev/q2aas-generator-manifest-schema-validation-2026-06-17.md`: manifest schema validation, task provenance, and validation-report manifest metadata.
- `docs-dev/q2aas-generator-manifest-schema-smoke-2026-06-17.md`: automated malformed-manifest expected-failure smoke in the staged q2aas validation target.
- `docs-dev/q2aas-generator-aas-staging-2026-06-17.md`: validated `.aas` staging into `.install/basew/maps/` and staged-output report metadata.
- `docs-dev/q2aas-generator-stage-audit-2026-06-17.md`: staged `.aas` artifact audit against the validation stage report.
- `docs-dev/q2aas-generator-packaged-map-smoke-2026-06-17.md`: archive-backed map extraction and scratch pkz conversion smoke.
- `docs-dev/q2aas-generator-archive-manifest-guardrails-2026-06-17.md`: archive-backed manifest member validation and expected-failure smoke coverage.
- `docs-dev/q2aas-generator-package-audit-2026-06-17.md`: staged AAS package-readiness audit for loose-or-archive release payload representation.
- `docs-dev/q2aas-generator-archive-packaging-2026-06-17.md`: validated staged AAS injection into `.install/basew/pak0.pkz` and archive-required package audit.
- `docs-dev/q2aas-generator-refresh-install-integration-2026-06-17.md`: `refresh_install.py --package-q2aas-aas` integration for preserving generated AAS through `.install` refreshes.
- `docs-dev/q2aas-generator-stage-archive-member-validation-2026-06-17.md`: generic staged-release archive member validation and q2aas refresh wiring for required packaged AAS hashes.
- `docs-dev/q3a-botlib-runtime-aas-shell-2026-06-17.md`: WORR-native BotLib/AAS runtime shell, `sg_bot_*` cvars, active-map AAS header probe, and server-game lifecycle hooks.
- `docs-dev/q3a-botlib-import-boundary-2026-06-17.md`: Q3A BotLib import root, adapter shell, planned-file inventory, and build strategy decision.
- `docs-dev/q3a-botlib-utility-import-2026-06-17.md`: first commit-pinned Q3A BotLib utility source/header import, local build wrapper, LibVar smoke bridge, and runtime validation log.
- `docs-dev/q3a-botlib-aas-file-loader-2026-06-17.md`: imported Q3A `be_aas_file.c` AAS loader subset, in-memory WORR-to-Q3A filesystem bridge, and staged map loader smoke.
- `docs-dev/q3a-botlib-aas-sample-query-2026-06-17.md`: imported Q3A `be_aas_sample.c`, temporary bridge shims for later runtime hooks, and first loaded-area query smoke.
- `docs-dev/q3a-botlib-aas-reach-query-2026-06-17.md`: imported Q3A `be_aas_reach.c`, removal of the temporary `AAS_AreaReachability` shim, scoped warning policy, and imported reachability smoke.
- `docs-dev/q3a-botlib-aas-route-query-2026-06-17.md`: imported Q3A `be_aas_route.c`, `l_crc.*`, route-cache initialization/unload handling, and imported travel-time/predict-route smoke.
- `docs-dev/q3a-botlib-aas-start-frame-2026-06-17.md`: imported Q3A `be_aas_main.c`, Q3A setup/shutdown ownership for loaded AAS, and per-frame `AAS_StartFrame` smoke.
- `docs-dev/q3a-botlib-aas-entity-cache-2026-06-17.md`: imported Q3A `be_aas_entity.c`, removal of temporary entity reset/invalidation shims, and entity-cache start-frame smoke.
- `docs-dev/q3a-botlib-aas-entity-sync-2026-06-17.md`: WORR bot-facing entity snapshot translation into imported Q3A `AAS_UpdateEntity` and verbose sync counters.
- `docs-dev/q3a-botlib-aas-entity-trace-2026-06-17.md`: Q3A `AAS_EntityCollision` callback bridge into WORR `gi.clip` entity clipping, plus BSP model-index sync correction.
- `docs-dev/q3a-botlib-aas-bsp-leaf-link-2026-06-17.md`: active-map Q2 BSP leaf-link bridge for imported Q3A dynamic entity links and `AAS_BoxEntities`.
- `docs-dev/q3a-botlib-aas-movement-import-2026-06-17.md`: imported Q3A `be_aas_move.c`, WORR/Q2 movement LibVar seeding, and imported floor-drop/jump/client-movement smoke.
- `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`: callback-backed Q3A debug line/cross/arrow bridge to WORR `gi.Draw_*` imports, gated by bot debug cvars.
- `docs-dev/q3a-botlib-aas-route-overlay-2026-06-17.md`: route/goal overlay smoke using imported Q3A route prediction and the WORR debug draw bridge under `sg_bot_debug_route` / `sg_bot_debug_goal`.
- `docs-dev/q3a-botlib-aas-debug-polygon-bridge-2026-06-17.md`: callback-backed Q3A debug polygon create/delete bridge to WORR debug-line outline rendering, gated by `sg_bot_debug_aas >= 3`.
- `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`: imported Q3A `be_aas_debug.c` debug-area helper smoke through WORR debug line/polygon callbacks.
- `docs-dev/q3a-botlib-aas-cluster-import-2026-06-17.md`: imported Q3A `be_aas_cluster.c`, removed the temporary `AAS_InitClustering` shim, and added loaded-cluster smoke status.
- `docs-dev/q3a-botlib-aas-alternative-route-import-2026-06-17.md`: imported Q3A `be_aas_routealt.c`, removed the temporary alternative-routing lifecycle shims, and added `AAS_AlternativeRouteGoals` smoke status.
- `docs-dev/q3a-botlib-aas-optimize-import-2026-06-17.md`: imported Q3A `be_aas_optimize.c`, removed the temporary `AAS_Optimize` no-op, and kept optimization opt-in behind Q3A `aasoptimize` behavior.
- `docs-dev/q3a-botlib-print-bridge-2026-06-17.md`: callback-backed Q3A `botimport.Print` bridge to WORR logging with verbose status counters.
- `docs-dev/q3a-botlib-bot-client-command-bridge-2026-06-17.md`: callback-backed Q3A `botimport.BotClientCommand` bridge with safe WORR runtime rejection and smoke counters.
- `docs-dev/q3a-botlib-memory-allocator-bridge-2026-06-17.md`: tracked bot-owned Q3A `GetMemory` / `FreeMemory` / `HunkAlloc` bridge with grouped hunk release and status counters.
- `docs-dev/q3a-botlib-filesystem-bridge-2026-06-17.md`: read-only callback-backed Q3A filesystem bridge through WORR FS with file-handle tracking and fallback memory support.
- `docs-dev/q3a-botlib-route-cache-miss-policy-2026-06-17.md`: explicit optional `.rcd` route-cache miss classification so expected cache probes no longer inflate filesystem failure counters.
- `docs-dev/q3a-botlib-lifecycle-telemetry-2026-06-17.md`: explicit BotLib init/load/unload/shutdown counters and repeated import-harness proof of clean active AAS unloads.
- `docs-dev/q3a-botlib-dedicated-lifecycle-smoke-2026-06-17.md`: dedicated server self-smoke cvar, map-reload lifecycle status command, and shutdown-time clean-unload proof.
- `docs-dev/q3a-botlib-bot-slot-lifecycle-2026-06-17.md`: first engine-owned fake-client lifecycle, operator commands, bot-aware teardown, and one-bot slot smoke.
- `docs-dev/q3a-botlib-multibot-slot-queue-2026-06-17.md`: deferred multi-bot add queue, bot team assignment fix, and two-active-bot dedicated smoke.
- `docs-dev/q3a-botlib-min-players-autofill-2026-06-17.md`: `sg_bot_min_players` auto-fill policy, auto/manual bot separation, generated-name fix, and fill/trim/disable smoke.
- `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`: `sg_bot_reload_profiles`, first Q3A-style profile parser, profile-aware add/autofill, and profile smoke.
- `docs-dev/q3a-botlib-profile-behavior-fields-2026-06-17.md`: richer profile behavior field parsing, bot userinfo mapping, and expanded profile smoke.
- `docs-dev/q3a-botlib-team-policy-cleanup-2026-06-17.md`: bot initial placement and per-frame cleanup respect Duel/`maxplayers` active match limits.
- `docs-dev/q3a-botlib-team-policy-smoke-2026-06-17.md`: direct game-side team-policy smoke status through a lightweight game extension.
- `docs-dev/q3a-botlib-frame-command-dispatch-2026-06-17.md`: first AAS-gated bot `usercmd_t` generation and server fake-client movement dispatch smoke.
- `docs-dev/q3a-botlib-route-steered-frame-commands-2026-06-17.md`: AAS-backed route-step steering for bot frame command angles and route diagnostics in the dedicated smoke.
- `docs-dev/q3a-botlib-nav-route-cache-2026-06-17.md`: WORR-owned per-bot route cache, route-query cadence, and route reuse diagnostics.
- `docs-dev/q3a-botlib-nav-debug-overlay-2026-06-17.md`: live cached `bot_nav` route/goal debug overlay counters and fallback to the imported sample overlay before a bot route is cached.
- `docs-dev/q3a-botlib-nav-reachability-debug-2026-06-17.md`: live current-area and next-reachability travel metadata in cached route overlay labels and dedicated frame-command smoke status.
- `docs-dev/q3a-botlib-nav-polyline-debug-2026-06-17.md`: bounded cached route-point polyline drawing and headless polyline counters for live bot route debug.
- `docs-dev/q3a-botlib-nav-debug-client-filter-2026-06-17.md`: `sg_bot_debug_client` selected-client filtering for cached route/goal debug overlays and headless filter counters.
- `docs-dev/q3a-botlib-bridge-time-vector-2026-06-17.md`: bridge-fed Q3A runtime milliseconds, real `AngleVectors`, adapter status, and verbose debug smoke.
- `docs-dev/q3a-botlib-bsp-entity-bridge-2026-06-17.md`: active-map Q2 BSP entity-lump bridge for Q3A `AAS_NextBSPEntity` and epair helper callbacks.
- `docs-dev/q3a-botlib-bsp-model-bridge-2026-06-17.md`: active-map Q2 BSP model-lump bridge for Q3A inline BSP model bounds.
- `docs-dev/q3a-botlib-bsp-collision-bridge-2026-06-17.md`: active-map Q2 BSP static-world collision bridge for Q3A `AAS_Trace` and `AAS_PointContents`.
- `docs-dev/q3a-botlib-bsp-visibility-bridge-2026-06-17.md`: active-map Q2 BSP PVS/PHS visibility bridge for Q3A `AAS_inPVS` and `AAS_inPHS`.

## Task Board

Use these tasks as the maintainable checklist backbone. Status values should follow the roadmap states: `Backlog`, `Ready`, `In Progress`, `In Review`, `Blocked`, `Done`.

| ID | Status | Area | Priority | Depends On | Definition of Done |
|---|---|---|---|---|---|
| `FR-04-T01` | Ready | `sgame/bots` | P0 | none | MVP behavior scope is written, accepted, and mapped to Q3A/WORR boundaries. |
| `FR-04-T02` | In Progress | `sgame/bots` | P0 | `FR-04-T01`, `FR-04-T12`, `FR-04-T14` | `Bot_BeginFrame` and `Bot_EndFrame` produce stable bot usercmds with scheduling, perception, and debug hooks. |
| `FR-04-T03` | Backlog | `sgame/bots` | P1 | `FR-04-T02` | Bots select Q2/Q2R weapons, ammo, powerups, and inventory items through WORR helpers. |
| `FR-04-T04` | Backlog | `sgame/bots`, `sgame/match` | P1 | `FR-04-T02`, `FR-04-T15` | Bots understand supported team/objective modes and avoid sabotaging match flow. |
| `FR-04-T05` | Backlog | `tools/q2aas`, `sgame/bots` | P1 | `FR-04-T11`, `FR-04-T14` | Map-level nav diagnostics validate generated AAS, spawn routing, reachability, and common blockers. |
| `FR-04-T06` | Backlog | `sgame/match` | P1 | `FR-04-T02` | Tournament, vote, map queue, and scoreboard flows handle bot participants intentionally. |
| `FR-04-T07` | Backlog | `sgame/bots`, docs | P2 | `FR-04-T01` | Public bot cvars use `sg_bot_*`, have sane defaults, and are documented in dev/user docs as appropriate. |
| `FR-04-T10` | In Progress | docs, provenance | P0 | none | Source audit, license notes, and credits ledger exist before code import starts. |
| `FR-04-T11` | In Progress | `tools/q2aas` | P0 | `FR-04-T10` | `TTimo/bspc` based Q2 AAS generator builds locally, accepts WORR/Q2R map inputs, and emits validated `.aas` files. |
| `FR-04-T12` | In Progress | `sgame/bots/q3a` | P0 | `FR-04-T10` | Q3A BotLib runtime compiles behind a WORR adapter and can load/unload generated AAS for the active map. |
| `FR-04-T13` | In Progress | `sgame/client`, `sgame/commands` | P0 | `FR-04-T01` | Bots can be added/removed through WORR commands without network-client hacks or stale session state. |
| `FR-04-T14` | In Progress | `sgame/bots/bot_nav` | P0 | `FR-04-T11`, `FR-04-T12`, `FR-04-T13` | A spawned bot can route, steer, recover from simple stalls, and reach item/position goals on reference maps. |
| `FR-04-T15` | Backlog | `sgame/bots/bot_brain` | P1 | `FR-04-T14` | Q3A behavior concepts are translated into Q2 item, weapon, combat, and mode decisions. |
| `FR-04-T16` | In Progress | packaging, validation | P1 | `FR-04-T11`, `FR-04-T14` | AAS assets/tooling are staged under `.install/`, smoke tested, and covered by release packaging checks. |
| `DV-03-T05` | Backlog | tests | P2 | `FR-04-T02` | Bot scenario tests cover spawn, navigation, combat, and objective behavior. |
| `DV-07-T06` | In Progress | docs | P0 | none | Imported-source credit and provenance requirements are documented and checked before each bot PR/merge. |

## Checklist System

Every task above should be tracked with the same small checklist. If a task does not need one line, mark it `N/A` with a short reason in the implementation log.

- [ ] Scope and owner recorded in the roadmap or issue tracker.
- [ ] Upstream/source files and concepts inventoried.
- [ ] Credit ledger updated before code lands.
- [ ] License headers retained or rewritten only when legally and historically correct.
- [ ] Design notes added under `docs-dev/` for significant implementation choices.
- [ ] Code implemented behind the planned ownership boundary.
- [ ] Build validation run.
- [ ] Runtime validation run on at least one reference map.
- [ ] Debug/telemetry path verified.
- [ ] `.install/` staging impact checked if binaries, maps, AAS files, or packaged assets changed.
- [ ] User docs updated under `docs-user/` if new commands/cvars are exposed.
- [ ] Roadmap task status updated.

## Phase 0: Governance, Source Audit, and Credits

Primary tasks: `FR-04-T01`, `FR-04-T10`, `DV-07-T06`

Goal: define the project contract before importing code.

Checklist:

- [x] Create `docs-dev/q3a-botlib-aas-credits.md`.
- [x] Create `docs-dev/q3a-botlib-aas-source-audit-2026-06-16.md`.
- [x] Record source repositories and local paths:
  - [x] `E:\_SOURCE\_CODE\Quake-III-Arena-master`
  - [x] `https://github.com/TTimo/bspc`
  - [x] `https://github.com/bnoordhuis/bspc`
- [x] Capture upstream baseline commit IDs before any imported snapshots.
- [ ] Record every imported file with:
  - [x] BSPC snapshot destination path recorded as `tools/q2aas/**`.
  - [x] BSPC upstream path and commit hash recorded.
  - [x] BSPC license/header retention recorded.
  - [x] BSPC local modifications summary recorded.
  - [x] BSPC contributor baseline recorded.
  - [x] Add per-file rows when individual BSPC files are locally tailored in the Q2 reachability bridge slice.
  - [ ] Continue adding per-file rows for future BSPC local tailoring.
  - [x] Add per-file rows for the first imported Q3A BotLib utility subset under `src/game/sgame/bots/q3a/`.
  - [ ] Continue adding Q3A BotLib runtime rows before future imported `src/game/sgame/bots/q3a/` source/header files land.
- [x] Complete first-pass GPL-2.0/GPL-2.0-or-later compatibility review against WORR's current license obligations before code import.
- [x] Decide whether Q3A BotLib code is copied into `src/game/sgame/bots/q3a/`, built as a static library, or built as an internal game-module object group.
  - Decision: copy only commit-pinned, ledger-recorded Q3A files into `src/game/sgame/bots/q3a/` and compile them as an internal server-game object group behind `botlib_adapter.*`.
- [x] Decide whether `tools/q2aas/` is a copied source snapshot, a git subtree, or a documented vendored import.
- [ ] Write the MVP behavior slice for `FR-04-T01`:
  - [ ] Spawn and leave cleanly.
  - [ ] Load character/profile data.
  - [ ] Find AAS area near spawn.
  - [ ] Route to a visible item or roam goal.
  - [ ] Engage visible enemies with a basic weapon policy.
  - [ ] Recover from simple stuck states.
  - [ ] Participate in FFA/TDM scoring without breaking match flow.

Credits requirements:

- [x] Retain id Software copyright notices on imported BSPC/Q3A-derived source.
- [ ] Retain ZeniMax/WORR notices on existing WORR files.
- [x] Credit `TTimo/bspc` as the BSP-to-AAS compiler baseline.
- [x] Credit `bnoordhuis/bspc` as the fork lineage shown by `TTimo/bspc`.
- [x] Credit individual upstream commit authors when file-level git history is imported for the BSPC snapshot.
- [x] Add a "Modified for WORR" note to imported files modified in the Q2 reachability bridge slice.
- [ ] Continue adding "Modified for WORR" notes to future locally modified imported files.

Exit criteria:

- The project can import code without losing provenance.
- The roadmap has bot/AAS/credit tasks.
- No source files are copied before their credit ledger rows exist.

## Phase 1: Q2 AAS Generator Based on `TTimo/bspc`

Primary tasks: `FR-04-T11`, `FR-04-T16`

Goal: produce reliable AAS files from Quake II / Quake II Rerelease maps.

Recommended target:

- Tool name: `worr_q2aas` or `q2aas`.
- Source root: `tools/q2aas/`.
- Output: `maps/<mapname>.aas` staged into `.install/basew/` or packaged into `.install/basew/pak0.pkz` when appropriate.
- Development output and generated scratch files: `.tmp/q2aas/`.

Implementation checklist:

- [x] Import or vendor the `TTimo/bspc` source baseline after Phase 0 credits are in place.
- [x] Build a standalone local executable through Meson.
- [x] Add a Meson cfg smoke target for the WORR preset.
- [x] Keep the Q2 loader path active:
  - [x] `l_bsp_q2.c`
  - [x] `l_bsp_q2.h`
  - [x] `map_q2.c`
  - [x] `q2files.h`
  - [x] `textures.c`
- [ ] Remove or isolate unused Q1/HL/Sin/Q3 map loaders only after validating they are not needed by shared code.
- [x] Add a WORR config preset, for example `tools/q2aas/cfg/worr_q2.cfg`.
- [x] Add `tools/q2aas/validation_manifest.json` as the staged-map validation matrix seed.
- [ ] Define WORR player presence types and movement constants:
  - [x] Standing player bounds.
  - [x] Crouched player bounds.
  - [x] Swimming movement constants.
  - [ ] Optional large/NPC presence type if later shared with monster AI.
- [x] Add local validation helper for cfg/map smoke checks under `.tmp/q2aas/`.
- [x] Add manifest-driven staged-map smoke validation and JSON report output.
- [x] Add manifest schema/version/task validation and report manifest provenance in the staged JSON output.
- [x] Add automated malformed-manifest expected-failure smoke coverage to `q2aas-staged-smoke`.
- [x] Add opt-in `.install/basew/maps` staging for generated AAS files after strict validation passes.
- [x] Add staged AAS artifact audit that verifies `.install/basew/maps` files against the stage report.
- [x] Add package-readiness audit that verifies staged AAS files are represented in the local release payload.
- [x] Add package archive step that injects validated staged AAS files into `.install/basew/pak0.pkz`.
- [x] Add archive-required package audit that verifies packaged AAS members match staged-output hashes.
- [x] Add opt-in `refresh_install.py` integration that repackages q2aas AAS after `pak0.pkz` is rebuilt from `assets/`.
- [x] Add generic staged-release validation for required package archive members and hash-matched q2aas AAS refresh checks.
- [ ] Map Q2 contents/surface flags to AAS travel flags:
  - [x] Solid/world clipping for the first static-world Q2 trace bridge.
  - [ ] Water.
    - [x] Water brush counts are recorded in validation diagnostics.
  - [ ] Slime/lava/hurt volumes.
    - [x] Slime/lava brush counts and `trigger_hurt` entity counts are recorded in validation diagnostics.
  - [x] Ladders if represented by map/entity metadata in the inherited BotLib pass.
    - [x] Ladder brush counts are recorded in validation diagnostics.
  - [ ] Slick/sky/nodraw/detail/translucent surfaces where relevant.
    - [x] Detail/translucent brush counts are recorded in validation diagnostics.
- [ ] Teach generator about Q2/Q2R map quirks:
  - [x] IBSP38 header validation in staged-map preflight.
  - [x] Invalid or unknown BSP headers fail clearly before AAS generation continues.
  - [ ] Rerelease/BSPX lump tolerance if present.
    - [x] BSPX marker offsets are detected and recorded in validation metadata.
  - [x] Pak/pkz map lookup from WORR's `basew` staging layout.
    - [x] Manifest entries can use `archive` plus `archive_member` instead of a loose `path`.
    - [x] `q2aas-package-map-smoke` creates a scratch `.pkz`, extracts `maps/mm-rage.bsp`, and validates the extracted BSP.
    - [x] Manifest schema smoke rejects path/archive conflicts, missing archive members, absolute archive members, and traversal archive members.
  - [x] Entity lump parsing for doors, plats, teleporters, triggers, hurt volumes, and spawn/item points in validation diagnostics.
- [ ] Add reachability passes for:
  - [x] Walk.
  - [x] Step up/down through inherited walk reachability, validated on staged smoke map.
  - [x] Walk off ledges within controlled drop limits.
  - [x] Jumps and barrier jumps tuned to Q2 movement preset.
  - [ ] Water entry/exit.
  - [ ] Elevators/plats/doors as conditional reachability.
    - [x] First elevator candidate generated by the static Q2 bridge smoke.
  - [ ] Teleports.
  - [ ] Optional rocket-jump routes behind an explicit `sg_bot_allow_rocketjump` style setting.
    - [x] Generator can emit inherited rocket-jump candidates; runtime policy gate still pending.
- [ ] Add deterministic metadata to generated AAS:
  - [x] Source BSP checksum.
  - [x] Tool version and executable hash in validation report/sidecar.
  - [x] Config hash in validation report/sidecar.
  - [x] Generation time omitted by policy; reproducible identity uses tool/config/input/output hashes.
  - [ ] Decide whether sidecar metadata is packaged, folded into package manifests, or replaced by a runtime AAS metadata extension.
- [ ] Add diagnostics:
  - [x] AAS area count.
  - [x] Reachability size and cluster count summary.
  - [x] Reachability count by travel type.
  - [x] Structured JSON validation report under `.tmp/q2aas/`.
  - [x] Q2 BSP and AAS header/hash metadata sidecars under `.tmp/q2aas/`.
  - [x] First-pass orphaned item/spawn count using AAS area-bounds origin coverage.
  - [x] First-pass unreachable high-value item report using the generated AAS reachability graph.
  - [x] Staged smoke fails on clean BSP lump, spawn coverage, item coverage, and high-value pickup reachability regressions.
  - [x] Staged smoke fails when manifest minimum AAS metrics or travel counts regress below the `mm-rage` baseline.
  - [x] Staged smoke rejects malformed manifest schema, unknown baseline keys, and non-integer baseline thresholds before generation.
  - [x] Staged smoke verifies the malformed-manifest rejection path through `--manifest-schema-smoke`.
  - [ ] Door/elevator route report.
    - [x] Door/elevator entity inventory in validation diagnostics.

Initial staged map smoke:

- [x] `.install/basew/maps/mm-rage.bsp`: structural AAS generation succeeds and writes `.tmp/q2aas/mm-rage.aas`.
  - Baseline result before Q2 bridge: `numareas = 428`, `numareasettings = 428`, `reachabilitysize = 0`, `numclusters = 0`.
- [x] `.install/basew/maps/mm-rage.bsp`: strict Q2 reachability validation succeeds after the Q2 trace bridge.
  - Result: `numareas = 428`, `numareasettings = 428`, `reachabilitysize = 562`, `numclusters = 4`.
  - Travel counts: `468 walk`, `1 barrier jump`, `7 jump`, `1 ladder`, `81 walk off ledge`, `1 elevator`, `2 rocket jump`.
- [x] `q2aas-staged-smoke`: manifest-driven validation passes for `mm-rage.bsp`, writes `.tmp/q2aas/validation-report.json`, and verifies invalid BSP input fails with `unknown BSP format`.
  - [x] Writes `.tmp/q2aas/mm-rage.aas.meta.json` with tool/config/BSP/AAS hashes, Q2 header data, AAS header data, and no generation timestamp.
  - [x] Detects `mm-rage.bsp` as Quake II `IBSP` version `38` and records a BSPX marker at offset `766956`.
  - [x] Reports `9` spawn points, `2` intermission cameras, `48` pickups, `2` high-value pickups, `2` movers, `1` trigger, `0` spawn/item origin orphans, `0` unreachable high-value pickups, and `16` ladder brushes.
  - [x] Enforces diagnostic gates: clean BSP lump table, `9/9` spawn origins mapped to AAS areas, `48/48` item origins mapped to AAS areas, and `0` unreachable high-value pickups.
  - [x] Enforces baseline minima: `428` areas, `428` area settings, `562` reachability records, `4` clusters, `468` walk, `1` barrier jump, `7` jump, `1` ladder, `81` walk off ledge, and `1` elevator.
  - [x] Reports manifest provenance: schema `worr-q2aas-validation-manifest-v1`, version `1`, task IDs, pending reference categories, and no manifest errors.
  - [x] Reports `manifest_schema_smoke.status = passed` for a generated malformed manifest with an unknown metric key, a string travel-count threshold, path/archive conflicts, missing archive members, absolute archive members, and traversal archive members.
- [x] `q2aas-stage-aas`: strict manifest validation passes for `mm-rage.bsp`, stages `.install/basew/maps/mm-rage.aas`, and writes `.tmp/q2aas/stage-report.json`.
  - [x] Staged AAS size is `277484` bytes and staged SHA-256 is `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
- [x] `q2aas-stage-audit`: verifies `.install/basew/maps/mm-rage.aas` exists under the expected stage directory, is non-empty, and matches the staged/generated AAS hashes.
  - [x] Writes `.tmp/q2aas/stage-audit-report.json` with schema `worr-q2aas-stage-audit-v1`, `map_count = 1`, `failed_count = 0`, and `status = passed`.
- [x] `q2aas-package-map-smoke`: validates archive-backed map extraction by building `.tmp/q2aas/package-map-smoke.pkz`, extracting `maps/mm-rage.bsp`, and converting the extracted BSP.
  - [x] Writes `.tmp/q2aas/package-map-smoke-report.json` with `map_source.type = archive`, archive hash/member metadata, and strict Q2 validation status `passed`.
- [x] `q2aas-package-audit`: verifies the staged `mm-rage.aas` release payload representation after staging.
  - [x] Writes `.tmp/q2aas/package-audit-report.json` with schema `worr-q2aas-package-audit-v1`, `map_count = 1`, `failed_count = 0`, and `status = passed`.
  - [x] Loose-or-archive mode confirms `.install/basew/maps/mm-rage.aas` is hash-matched before archive packaging, and also passes after the archive member is present.
- [x] `q2aas-package-aas`: injects validated staged AAS output into `.install/basew/pak0.pkz`.
  - [x] Writes `.tmp/q2aas/package-archive-report.json` with schema `worr-q2aas-package-archive-v1`, `added_count = 1`, `replaced_count = 0`, and `status = passed` on the first package run.
  - [x] Adds `maps/mm-rage.aas` with size `277484` bytes and SHA-256 `6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`.
- [x] `q2aas-package-archive-audit`: verifies the packaged `maps/mm-rage.aas` member under an archive-required policy.
  - [x] Writes `.tmp/q2aas/package-archive-audit-report.json` with schema `worr-q2aas-package-audit-v1`, `map_count = 1`, `failed_count = 0`, `policy = archive-required`, and `status = passed`.
- [x] `refresh_install.py --package-q2aas-aas`: refreshes `.install/`, rebuilds `pak0.pkz` from assets, injects staged q2aas AAS, audits the archive member, and validates the staged payload.
  - [x] Writes `.tmp/q2aas/refresh-package-archive-report.json` and `.tmp/q2aas/refresh-package-archive-audit-report.json`.
  - [x] `windows-x86_64` staged install validation passes after q2aas AAS is re-injected.
- [x] `validate_stage.py --required-archive-member`: verifies the packaged `maps/mm-rage.aas` member exists inside `.install/basew/pak0.pkz` and matches the staged SHA-256.
- [x] `refresh_install.py --package-q2aas-aas --platform-id windows-x86_64`: derives required archive member checks from `.tmp/q2aas/stage-report.json` before generic staged payload validation.

Reference map checklist:

- [x] `validation_manifest.json`: current staged smoke map and pending reference categories recorded.
- [ ] `q2dm1`: basic DM routing, weapon pickup, elevator/vertical movement.
- [ ] `q2dm2`: multi-level combat routing.
- [ ] `q2dm8` or another open map: long sightline and item timing checks.
- [ ] A CTF map: flag route and team objective reachability.
- [ ] A campaign map: coop progression and door/trigger issues.
- [ ] At least one map with water/lava/slime.

Exit criteria:

- `worr_q2aas -bsp2aas <map>.bsp -output .tmp/q2aas` succeeds on the reference set.
- Generated `.aas` files load in the runtime shell from Phase 2.
- AAS validation failures produce actionable diagnostics rather than silent bot failure.

## Phase 2: BotLib Runtime Rehost

Primary task: `FR-04-T12`

Goal: compile and initialize the Q3A BotLib/AAS runtime behind a WORR adapter.

Implementation checklist:

- [x] Add a WORR-native runtime shell under `src/game/sgame/bots/bot_runtime.*`.
- [x] Register the first public `sg_bot_*` cvars for runtime gating and debug/status work.
- [x] Probe the active map's `maps/<map>.aas` through WORR filesystem search paths, decode the Q3A/BSPC AAS v5 header transform, and validate the `EAAS` version 5 lump table.
- [x] Add server-game lifecycle hooks for map start, entity reload, frame tick, and shutdown.
- [x] Create an imported-code boundary for Q3A BotLib files.
  - [x] Add `src/game/sgame/bots/q3a/` as the reserved import root with local import rules.
  - [x] Add a compiled WORR-native Q3A BotLib boundary inventory/status layer.
  - [x] Add a WORR-facing `botlib_adapter.*` shell for future setup/shutdown/map/frame calls.
- [ ] Compile the BotLib C files with first-party warning policy decisions documented.
  - [x] Compile the first commit-pinned utility subset: `l_memory.c`, `l_libvar.c`, and required Q3A headers.
  - [x] Document the local `q3a_botlib_utility` warning/compatibility policy and runtime LibVar smoke.
  - [x] Compile the Q3A AAS file loader subset: `be_aas_file.c`, AAS declarations, and parser utility headers.
  - [x] Compile the Q3A AAS sampling subset: `be_aas_sample.c`.
  - [x] Compile the Q3A AAS reachability subset: `be_aas_reach.c`.
  - [x] Compile the Q3A AAS route subset: `be_aas_route.c` plus `l_crc.*`.
  - [x] Compile the Q3A AAS runtime start-frame subset: `be_aas_main.c`.
  - [x] Compile the Q3A AAS entity-cache subset: `be_aas_entity.c`.
  - [x] Compile the Q3A AAS movement-prediction subset: `be_aas_move.c`.
  - [x] Compile the Q3A AAS debug helper subset: `be_aas_debug.c`.
  - [x] Compile the Q3A AAS clustering subset: `be_aas_cluster.c`.
  - [x] Compile the Q3A AAS alternative-route subset: `be_aas_routealt.c`.
  - [x] Compile the Q3A AAS optimization subset: `be_aas_optimize.c`.
  - [x] Document the temporary `WIN32`, `MEMORYMANEGER`, and legacy warning policy used by the Q3A object group.
  - [x] Document the scoped `-Wno-absolute-value` warning exception required by the legacy Q3A reachability source.
  - [x] Document the remaining temporary callback/import boundaries used before the full Q3A runtime is imported.
  - [x] Replace the temporary `AngleVectors` and `Sys_MilliSeconds` shims with bridge-owned implementations.
  - [x] Replace the temporary BSP/entity epair callbacks with active-map Q2 BSP entity-lump parsing.
  - [x] Replace the temporary BSP inline model callback with active-map Q2 BSP model-lump parsing.
  - [x] Replace the temporary static `AAS_Trace` and `AAS_PointContents` stubs with active-map Q2 BSP collision-lump parsing and static-world lookup.
  - [x] Replace the missing `AAS_inPVS` and `AAS_inPHS` callbacks with active-map Q2 BSP visibility-lump parsing.
  - [x] Replace the temporary BSP-leaf entity-link and `AAS_BoxEntities` stubs with active-map Q2 BSP leaf linking.
  - [x] Replace the temporary `AAS_InitClustering` stub with imported Q3A clustering support and loaded-cluster smoke.
  - [x] Replace the temporary alternative-routing lifecycle stubs with imported Q3A `be_aas_routealt.c` and `q3a_alt_route` smoke.
  - [x] Replace the temporary `AAS_Optimize` no-op with imported Q3A `be_aas_optimize.c` while leaving the default loaded-AAS path unoptimized.
  - [ ] Compile the full BotLib runtime/AAS file set.
- [ ] Build a WORR-facing adapter for the Q3A `botlib_import_t` callbacks:
  - [x] Add adapter shell/status layer that keeps the runtime unavailable until Q3A files are imported.
  - [x] `Print` to WORR logging with warning/error/fatal forwarding and verbose message-level forwarding behind `sg_bot_debug_aas >= 3`.
  - [ ] `Trace` to final WORR collision ownership.
    - [x] Add an interim active-map Q2 BSP static-world `AAS_Trace` bridge.
  - [ ] `EntityTrace` to final WORR collision ownership.
    - [x] Add an interim WORR `gi.clip` entity trace bridge for Q3A `AAS_EntityCollision`.
  - [x] `BSPLinkEntity` / `BoxEntities` to active-map Q2 BSP leaf access.
  - [x] AAS movement prediction/drop/jump helpers through imported `be_aas_move.c` with WORR/Q2 LibVar seeding.
  - [ ] `PointContents` to final WORR collision ownership.
    - [x] Add an interim active-map Q2 BSP leaf `AAS_PointContents` bridge.
  - [ ] `inPVS` / `inPHS` to final WORR visibility ownership.
    - [x] Add an interim active-map Q2 BSP PVS/PHS visibility bridge.
  - [x] `BSPEntityData` to active map entity lump access.
  - [x] `BSPModelMinsMaxsOrigin` to inline model bounds.
  - [x] `BotClientCommand` to a safe sgame command path that validates WORR bot clients and rejects execution until a dedicated bot command dispatcher exists.
  - [x] Memory allocation to a tracked bot-owned allocator.
    - [x] Add temporary `malloc` / `free` callbacks for the imported utility smoke only.
    - [x] Enable Q3A's existing memory-manager path so temporary AAS hunk allocations can unload cleanly.
    - [x] Replace raw BotLib import `malloc` / `free` callbacks with tracked zone/hunk allocation lists and grouped hunk release after AAS shutdown.
  - [x] Filesystem reads through WORR FS and `basew` search paths.
    - [x] Add a temporary in-memory read-only FS bridge for the already-loaded active AAS buffer.
    - [x] Replace the singleton active-AAS memory shim with a callback-backed read-only WORR FS file-handle bridge while keeping the memory buffer as a fallback.
    - [x] Classify optional imported Q3A `.rcd` route-cache read misses separately from real filesystem open failures.
  - [x] Debug lines/polygons to WORR debug draw imports.
    - [x] Add temporary debug-line no-op stubs required by the imported reachability source.
    - [x] Replace Q3A debug line/cross/arrow no-ops with a WORR debug draw callback bridge gated by `sg_bot_debug_aas >= 3`, `sg_bot_debug_route`, or `sg_bot_debug_goal`.
    - [x] Add route/goal overlay smoke that draws imported Q3A route start, goal, and predicted-end markers through the WORR debug draw bridge.
    - [x] Add Q3A debug polygon create/delete callbacks with runtime outline/fan rendering through WORR `gi.Draw_Line`.
    - [x] Import Q3A AAS debug helpers and smoke `AAS_ShowArea` / `AAS_ShowAreaPolygons` through WORR debug line/polygon callbacks.
- [ ] Add map lifecycle:
  - [ ] Init BotLib once per game module load.
  - [x] Probe active map AAS on map start through the WORR filesystem extension.
  - [x] Load active map Q2 BSP entity data before the Q3A AAS buffer handoff.
  - [x] Load active map Q2 BSP model data before the Q3A AAS buffer handoff.
  - [x] Load active map Q2 BSP collision data before the Q3A AAS buffer handoff.
  - [x] Load active map Q2 BSP visibility data before the Q3A AAS buffer handoff.
  - [x] Load active map AAS through the imported Q3A AAS file loader on map start.
  - [x] Run a first imported Q3A area sample smoke with `AAS_AreaInfo` and `AAS_PointAreaNum` after load.
  - [x] Run imported Q3A `AAS_AreaReachability` against the sampled loaded area after load.
  - [x] Run imported Q3A AAS clustering validation against the loaded active-map AAS after load.
  - [x] Initialize imported Q3A route caches and run `AAS_AreaTravelTimeToGoalArea` / `AAS_PredictRoute` against the loaded active-map AAS after load.
  - [x] Initialize imported Q3A alternative routing and run `AAS_AlternativeRouteGoals` against the loaded active-map AAS after load.
  - [x] Run imported Q3A `AAS_DropToFloor`, `AAS_HorizontalVelocityForJump`, and `AAS_PredictClientMovement` smoke against the loaded active-map AAS after load.
  - [x] Update bridge-owned Q3A runtime milliseconds from `level.time` each server frame.
  - [x] Call imported Q3A AAS start-frame code each server frame after `be_aas_main.c` lands.
  - [x] Let imported Q3A entity-cache code own `AAS_ResetEntityLinks`, `AAS_InvalidateEntities`, and `AAS_UnlinkInvalidEntities` during setup/start-frame.
  - [x] Push WORR bot-facing entity snapshots into imported Q3A `AAS_UpdateEntity` after each start-frame.
  - [x] Shutdown/unload cleanly on map restart, game unload, or dedicated server exit.
    - [x] Add explicit BotLib lifecycle counters for init, shutdown, load attempts/successes, active unloads, clean unloads, unload failures, transient unload bytes, open file handles, and persistent LibVar zone bytes.
    - [x] Prove three repeated active AAS load/unload cycles through the import harness with zero transient AAS memory/file residue.
    - [x] Add a dedicated server lifecycle self-smoke that starts `mm-rage`, reloads it once, captures clean unload counters during shutdown, and exits.
- [x] Add `sg_bot_enable` gate.
- [ ] Add developer/debug gates:
  - [x] Register `sg_bot_debug`.
  - [x] Register `sg_bot_debug_aas` and wire periodic AAS status output.
  - [x] Print verbose adapter/import-smoke status through `sg_bot_debug_aas 2`.
  - [x] Print Q3A AAS loader status and counts through `sg_bot_debug_aas 2`.
  - [x] Print Q3A AAS area sample status through `sg_bot_debug_aas 2`.
  - [x] Print imported Q3A reachability sample status through `sg_bot_debug_aas 2`.
  - [x] Print imported Q3A AAS clustering status through `sg_bot_debug_aas 2`.
  - [x] Register `sg_bot_lifecycle_smoke` for explicit developer self-smoke of load, reload, shutdown, and clean-unload status.
  - [x] Print bridge-owned Q3A time and `AngleVectors` smoke status through `sg_bot_debug_aas 2`.
  - [x] Print Q3A BSP entity-lump load and epair smoke status through `sg_bot_debug_aas 2`.
  - [x] Print Q3A BSP model-lump bounds smoke status through `sg_bot_debug_aas 2`.
  - [x] Print Q3A BSP collision-lump point/trace smoke status through `sg_bot_debug_aas 2`.
  - [x] Print Q3A BSP visibility-lump PVS/PHS smoke status through `sg_bot_debug_aas 2`.
  - [x] Print imported Q3A route-query status through `sg_bot_debug_aas 2`.
  - [x] Print imported Q3A alternative-route query status through `sg_bot_debug_aas 2`.
  - [x] Print imported Q3A movement-prediction/drop/jump status through `sg_bot_debug_aas 2`.
  - [x] Print imported Q3A AAS start-frame status through `sg_bot_debug_aas 2`.
  - [x] Print Q3A AAS entity-sync counters through `sg_bot_debug_aas 2`.
  - [x] Print Q3A debug draw bridge callback/counter status through `sg_bot_debug_aas 3`.
  - [x] Print Q3A debug polygon callback/counter status through `sg_bot_debug_aas 3`.
  - [x] Print imported Q3A AAS debug area helper status through `sg_bot_debug_aas 3`.
  - [x] Print Q3A route overlay callback/counter status through `sg_bot_debug_aas 2` when `sg_bot_debug_route` or `sg_bot_debug_goal` is active.
  - [x] Print Q3A BotLib memory allocator active/peak byte and failure counters through `sg_bot_debug_aas 2`.
  - [x] Print Q3A BotLib filesystem callback/open/read/close counters through `sg_bot_debug_aas 2`.
  - [x] Print Q3A BotLib optional route-cache miss counters separately from filesystem open failures.
  - [x] Print Q3A BotLib lifecycle counters and persistent LibVar zone bytes through `sg_bot_debug_aas 2`.
  - [x] Register `sg_bot_debug_route`.
  - [x] Register `sg_bot_debug_goal`.
  - [x] Register `sg_bot_debug_client`.
  - [x] Wire route/goal debug overlay smoke after imported route queries exist.
  - [x] Feed real per-bot route/goal state into the debug overlay once `bot_nav.*` owns route following.
- [ ] Decide which upstream `bot_*` libvars remain internal and document their mapping.

Exit criteria:

- BotLib setup/shutdown can run through repeated map changes without leaks or stale pointers.
- The runtime can load a generated `.aas`, answer simple area/reachability/route/movement-helper queries, and advance the imported AAS start-frame path.
- Debug polygons/lines and imported Q3A AAS area helper output can render through WORR debug draw.

## Phase 3: Fake Clients, Commands, and Profiles

Primary task: `FR-04-T13`

Goal: make bots join/leave like intentional WORR participants.

Implementation checklist:

- [x] Audit current bot slot creation in `client_session_service_impl.cpp`.
- [ ] Add commands:
  - [x] `sg_bot_add [profile] [team]`
  - [x] `sg_bot_remove <name|slot|all>`
  - [x] `sg_bot_kick_all`
  - [x] `sg_bot_list`
  - [x] `sg_bot_min_players`
  - [x] `sg_bot_reload_profiles`
- [ ] Add safeguards:
  - [x] Respect maxclients.
  - [x] Defer same-frame multi-bot add requests so more than one local bot can spawn safely.
  - [x] Clamp automatic min-player fill to public client slots.
  - [x] Respect match mode team limits.
  - [x] Do not count bots as humans for server population policies that already distinguish them.
  - [x] Remove only auto-managed bots when lowering `sg_bot_min_players` or disabling `sg_bot_enable`.
  - [x] Free bot clients cleanly on disconnect, map end, and mode changes.
    - [x] Explicit remove, kick-all, and server shutdown use bot-aware teardown without q2proto or anti-cheat disconnect traffic.
    - [x] Mode-change and team-limit cleanup moves surplus bots to spectators when active match limits tighten.
- [ ] Add profile loading:
  - [x] Start with Q3A-style character files as an import format.
  - [x] Add a WORR-local key/value `.bot` profile format using the same parser.
  - [x] Map initial profile fields to display name, skin, team, and skill.
  - [x] Map richer profile fields to reaction, aggression, aim error, preferred weapons, chat personality, team role, and movement style.
- [ ] Add initial profile assets under `assets/` only after credit/source ownership is clear.

2026-06-17 implementation slice:

- Engine-owned fake bot clients now allocate real public client slots, call game `ClientConnect(..., true)` / `ClientBegin`, and mark `client_t::bot` so game session state receives the existing bot path.
- Bot slots are removed through `SV_BotRemove()` / `SV_BotRemoveAll()` and are skipped by q2proto disconnect writes, anti-cheat disconnect handling, client print/send loops, final server messages, status responses, and human population counts.
- `sg_bot_add`, `sg_bot_remove`, `sg_bot_kick_all`, and `sg_bot_list` provide the first operator command surface.
- `SV_BotAdd()` now queues same-frame add requests after the first active bot and processes one queued bot per following server frame.
- Bot entity spawn preserves `SVF_BOT`, and `InitPlayerTeam()` assigns bots before host/owner auto-join logic so slot 0 bots do not land as spectators.
- `sv_bot_slot_smoke` provides an unattended dedicated-server add/remove lifecycle smoke; mode `2` now validates Alpha add/remove, Bravo active add, Charlie deferred add, two active bots, and full cleanup before exit.
- `sg_bot_min_players` now maintains auto-managed fake clients while `sg_bot_enable` is active, treats manual bots as satisfying the target, clamps the target to public slots, and removes only `bot_autofill` clients when the target drops or bot support is disabled.
- `sv_bot_min_players_smoke` provides an unattended dedicated-server fill/trim/disable smoke; mode `2` validates `B|bot1`, `B|bot2`, and `B|bot3` auto-fill to count `3`, target trim to one auto bot, disable cleanup back to `0`, and the prior multi-slot smoke remains clean afterward.
- `sg_bot_reload_profiles` now reloads a bounded server profile table from `botfiles/bots/*.c`, `bots/profiles/*.bot`, and `bots/*.bot`; `sg_bot_add [profile] [team]` resolves profiles before falling back to display-name behavior, and `sg_bot_profile` can feed min-player autofill when it names a loaded profile.
- `sv_bot_profile_smoke` provides an unattended dedicated-server profile smoke; mode `2` validates a temporary `smoke` profile, `B|Smoke` spawn, `bot_profile=smoke`, `skin=male/grunt`, `skill=4`, and full cleanup before exit.
- Bot profiles now preserve reaction, aggression, aim error, preferred weapon, chat personality, team role, and movement-style fields, accepting common Q3A/WORR aliases and exposing the values as `bot_*` userinfo keys for later game-side policy.
- `sv_bot_profile_smoke` now validates the richer profile bridge with `reaction=250`, `aggression=0.65`, `aim_error=2.5`, `preferred_weapon=rocketlauncher`, `chat=quiet`, `role=attacker`, and `movement=strafe` on the temporary `smoke` profile.
- Bot initial team assignment now respects match lock, `GameFlags::OneVOne` two-player active caps, and positive `maxplayers` limits; surplus bots start as spectators instead of bypassing `SetTeam()` by direct session assignment.
- `Bot_EnforceMatchTeamPolicy(true)` runs after cvar checks each game frame, preserving active humans first and moving surplus active bots to spectators when `maxplayers` or mode rules tighten.
- `sv_bot_team_policy_smoke` now validates the policy directly from the game module: a three-bot Duel setup reports `playing=2`, `spectators=1`, `bots=3`, then cleanup reports zero bots with both status lines passing.
- The fake-client frame-command path now requests an AAS route-steered command from `sgame`, faces the first predicted route step, and reports route counters through `sv_bot_frame_command_smoke`.
- Remaining limitations: curated profile assets, persistent navigation goals, route reuse, and stuck recovery remain pending.

Exit criteria:

- A server operator can add and remove a named bot without restarting the map.
- Bot profiles load deterministically.
- Bot names, skins, teams, score, deaths, and spectator state remain sane in match UI.

## Phase 4: Entity Sync, Perception, and Scheduling

Primary tasks: `FR-04-T02`, `FR-04-T14`

Goal: feed bots a coherent world model on a fixed budget.

Implementation checklist:

- [ ] Expand `Entity_UpdateState` coverage where needed:
  - [ ] Players.
  - [ ] Bots.
  - [ ] Spectators.
  - [ ] Monsters/NPCs.
  - [ ] Items/powerups/ammo.
  - [ ] Dropped weapons/items.
  - [ ] Traps/projectiles/hazards.
  - [ ] Doors/plats/movers.
  - [ ] Objectives/flags.
- [x] Push entity updates into BotLib each frame or on a staggered schedule.
  - [x] Push a full per-frame WORR snapshot into imported `AAS_UpdateEntity` after the server entity-state update pass.
  - [ ] Add staggered scheduling for expensive perception checks that do not need full-rate updates.
- [ ] Add bot blackboard state:
  - [ ] Current enemy.
  - [ ] Last seen enemy.
  - [ ] Heard/damaged-by events.
  - [ ] Current goal.
  - [ ] Route state.
  - [ ] Stuck timer.
  - [ ] Item reservation.
  - [ ] Team role.
- [ ] Use staggered expensive checks:
  - [ ] Visibility traces split across frames.
  - [ ] Item desirability updates split across bots.
  - [ ] Route recomputation rate limited.
  - [ ] Enemy memory decay instead of all-knowing target locks.
- [ ] Add fairness constraints:
  - [ ] Bots only aim at entities they could plausibly know.
  - [ ] Skill affects reaction and accuracy, not omniscience.
  - [ ] Item timers can be disabled or fuzzed through cvars.

Exit criteria:

- Eight bots can run the perception loop without large server frame spikes.
- Debug overlay can show a selected bot's enemy, route, goal, and known items.

## Phase 5: Navigation and Movement

Primary tasks: `FR-04-T02`, `FR-04-T05`, `FR-04-T14`

Goal: turn AAS route information into Quake II movement commands.

Implementation checklist:

- [ ] Map Q3A `bot_input_t` style output to WORR/Q2 `usercmd_t`.
  - [x] Add first-step AAS route steering directly into `Bot_BuildFrameCommand()`.
  - [x] Move route cache and query cadence into `bot_nav.*`.
  - [ ] Move high-level command/goal ownership into `bot_brain.*`.
- [ ] Implement movement states:
  - [ ] Ground steering.
    - [x] Initial route-target yaw plus forward movement for a spawned bot.
  - [ ] Jump.
  - [ ] Crouch.
  - [ ] Swim.
  - [ ] Ladder if supported by map metadata.
  - [ ] Door/plat wait/use.
  - [ ] Teleporter traversal.
- [ ] Add steering smoothing:
  - [ ] Look-ahead route points.
  - [ ] Corner cutting where safe.
  - [ ] Velocity-aware aim direction.
  - [ ] Avoid jittering between adjacent areas.
    - [x] Reuse cached route steps for short windows instead of rebuilding the route every command frame.
- [ ] Add stuck recovery:
  - [ ] Repath.
  - [ ] Short dodge/back-off.
  - [ ] Goal blacklist cooldown.
  - [ ] Door/trigger retry.
  - [ ] Last-resort respawn/spectator handling only in debug or controlled modes.
- [ ] Add movement debug:
  - [x] Current AAS area.
  - [x] Route polyline.
    - [x] Draw cached bot route step and goal markers while route/goal debug is enabled.
    - [x] Draw bounded sampled route points from the cached Q3A route-steer result.
  - [x] Selected debug client filter for cached route/goal overlays.
  - [x] Next reachability type.
  - [ ] Stuck reason.
  - [ ] Failed goal reason.
  - [x] Route query/cache counters in the dedicated frame-command smoke.

2026-06-17 route-steered command slice:

- `Q3A_BotLibImport_BuildRouteSteer()` and `BotLibAdapter_BuildRouteSteer()` expose a deterministic, live AAS route-steering query without spreading Q3A globals into `sgame`.
- `Bot_BuildFrameCommand()` now queries AAS route state each accepted bot frame, turns toward the first predicted route step, and emits forward movement through the normal server fake-client command path.
- `sv_bot_frame_command_smoke 2` now reports `route_queries=8`, `route_commands=8`, `route_failures=0`, `last_start_area=224`, `last_goal_area=227`, `last_route_end_area=217`, `last_travel_time=130`, and `last_reachability=218`.
- Implementation log: `docs-dev/q3a-botlib-route-steered-frame-commands-2026-06-17.md`.

2026-06-17 route-cache slice:

- `bot_nav.*` now owns per-client cached route-steer results and resets them on BotLib level begin/end.
- `Bot_BuildFrameCommand()` now asks `BotNav_GetRouteSteer()` for route state, so accepted command frames reuse a recent route step unless cadence, target reach, origin drift, or preferred-goal changes require a refresh.
- `sv_bot_frame_command_smoke 2` now reports `route_requests=8`, `route_queries=2`, `route_refreshes=2`, `route_reuses=6`, `route_commands=8`, `route_failures=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-route-cache-2026-06-17.md`.

2026-06-17 nav debug overlay slice:

- `BotNav_DrawDebugOverlay()` now draws cached bot route-step arrows and goal markers for `sg_bot_debug_route` / `sg_bot_debug_goal`.
- `RunBotLibDebugDrawIfRequested()` uses the cached native `bot_nav` overlay once a route exists, with the imported Q3A sample route overlay retained as a fallback before any bot route is cached.
- `sv_bot_frame_command_smoke 2` with route/goal debug enabled reports `route_debug_frames=10`, `route_debug_routes=8`, `route_debug_goals=8`, `route_debug_missing_frames=2`, `route_debug_arrows=8`, `last_route_debug_client=0`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-debug-overlay-2026-06-17.md`.

2026-06-17 nav reachability debug slice:

- `Q3A_BotLibImport_BuildRouteSteer()` now resolves the selected reachability and carries travel type, travel flags, and reachability end area through `BotLibAdapterRouteSteer`.
- `bot_nav.*` now records current AAS area and next reachability metadata in `BotNavRouteStatus`, draws a short cached route label while route debug is enabled, and counts emitted labels.
- `sv_bot_frame_command_smoke 2` with route/goal debug enabled reports `route_debug_labels=8`, `last_current_area=224`, `last_reachability_type=2`, `last_reachability_flags=2`, `last_reachability_end_area=217`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-reachability-debug-2026-06-17.md`.

2026-06-17 nav polyline debug slice:

- `Q3A_BotLibImport_BuildRouteSteer()` now samples up to eight predicted route endpoints after validating the full route, while preserving the existing first-step steering result.
- `BotLibAdapterRouteSteer` and `bot_nav.*` now carry cached route points, draw the first segment as the route arrow, draw intermediate sampled segments as a bounded polyline, and keep the goal marker.
- `sv_bot_frame_command_smoke 2` with route/goal debug enabled reports `route_debug_polyline_points=16`, `route_debug_polyline_segments=24`, `last_route_point_count=2`, `route_debug_lines=16`, `route_debug_arrows=8`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-polyline-debug-2026-06-17.md`.

2026-06-17 nav debug client-filter slice:

- `sg_bot_debug_client` now filters native cached route/goal overlays by zero-based client slot, with `-1` retaining the default all-bots overlay mode.
- `BotNav_DrawDebugOverlay()` skips non-selected cached route slots, counts filtered slots and filter-miss frames, and keeps the selected filter value in `last_debug_filter_client`.
- `sv_bot_frame_command_smoke 2` with `sg_bot_debug_client 0` reports the active bot route overlay with `route_debug_routes=8`, `route_debug_filtered_slots=0`, `last_debug_filter_client=0`, and `pass=1`.
- `sv_bot_frame_command_smoke 2` with `sg_bot_debug_client 1` filters out slot 0 and reports `route_debug_routes=0`, `route_debug_filtered_slots=8`, `route_debug_filter_miss_frames=10`, `last_debug_filter_client=1`, and `pass=1`.
- Implementation log: `docs-dev/q3a-botlib-nav-debug-client-filter-2026-06-17.md`.

Exit criteria:

- Bots can move from spawn to several item goals on `q2dm1` without manual waypoints.
- Movement looks like Q2 player movement rather than a Q3 movement transplant.
- Failed navigation reports enough detail to fix the generator or runtime.

## Phase 6: Combat, Item Utility, and Inventory

Primary tasks: `FR-04-T03`, `FR-04-T15`

Goal: translate Q3A combat and item concepts into WORR/Q2 rules.

Implementation checklist:

- [ ] Build Q2 weapon metadata:
  - [ ] Blaster.
  - [ ] Shotgun.
  - [ ] Super shotgun.
  - [ ] Machinegun.
  - [ ] Chaingun.
  - [ ] Grenades.
  - [ ] Grenade launcher.
  - [ ] Rocket launcher.
  - [ ] Hyperblaster.
  - [ ] Railgun.
  - [ ] BFG.
  - [ ] Expansion/rerelease weapons where present.
- [ ] Implement weapon selection:
  - [ ] Range bands.
  - [ ] Ammo availability.
  - [ ] Splash safety.
  - [ ] Enemy armor/health estimate.
  - [ ] Self-damage risk.
  - [ ] Projectile leading.
- [ ] Implement aim model:
  - [ ] Skill-based reaction delay.
  - [ ] Skill-based tracking noise.
  - [ ] Burst/commit behavior.
  - [ ] FOV/perception limits.
  - [ ] No instant 180-degree perfect shots except explicit debug modes.
- [ ] Implement item utility:
  - [ ] Health.
  - [ ] Armor.
  - [ ] Ammo.
  - [ ] Weapons.
  - [ ] Quad/damage boosts.
  - [ ] Invulnerability/protection.
  - [ ] Invisibility.
  - [ ] Techs/runes/CTF items if enabled.
- [ ] Add item reservation to avoid every bot choosing the same pickup.
- [ ] Add inventory use through existing `Bot_UseItem`.

Exit criteria:

- Bots can duel a human or another bot without standing idle, fixating on useless items, or choosing impossible weapons.
- Skill levels are visibly different but fair.

## Phase 7: Team Modes, Match Flow, and Coop

Primary tasks: `FR-04-T04`, `FR-04-T06`, later coop extension tasks

Goal: make bots useful in WORR's supported multiplayer and eventually cooperative contexts.

Implementation checklist:

- [ ] FFA:
  - [ ] Roam, collect, engage.
  - [ ] Avoid spawn camping loops where possible.
- [ ] TDM:
  - [ ] Team-aware target selection.
  - [ ] Item role split.
  - [ ] Avoid friendly fire where rules require it.
- [ ] CTF:
  - [ ] Attack/defend/mid roles.
  - [ ] Flag carrier support.
  - [ ] Dropped flag response.
  - [ ] Base return priorities.
- [ ] Duel/tournament:
  - [ ] Bot queue/spectator handling.
  - [ ] Warmup behavior.
  - [ ] Map restart cleanup.
- [ ] Match tools:
  - [ ] Votes.
  - [ ] Map queue/mymap.
  - [ ] Scoreboard classification.
  - [ ] Intermission and reconnect cleanup.
- [ ] Coop later phase:
  - [ ] Follow/wait/lead commands.
  - [ ] Door/elevator cooperation.
  - [ ] Monster target sharing.
  - [ ] Resource sharing.
  - [ ] Anti-blocking behavior.

Exit criteria:

- Bots do not destabilize existing match state.
- Team bots pursue objectives without all selecting the same role.

## Phase 8: UX, Cvars, Debug, and Docs

Primary tasks: `FR-04-T07`, `DV-07-T04`, `DV-07-T06`

Goal: expose the feature in a way server operators and developers can understand.

Suggested public cvars:

- `sg_bot_enable`
- `sg_bot_min_players`
- `sg_bot_skill`
- `sg_bot_profile`
- `sg_bot_max_clients`
- `sg_bot_allow_chat`
- `sg_bot_allow_item_timers`
- `sg_bot_allow_rocketjump`
- `sg_bot_debug`
- `sg_bot_debug_aas`
- `sg_bot_debug_route`
- `sg_bot_debug_goal`
- `sg_bot_debug_client`
- `sg_bot_cpu_budget_ms`
- `sg_bot_lifecycle_smoke`

Docs checklist:

- [ ] `docs-dev/q3a-botlib-aas-credits.md`: source and contributor credit ledger.
- [x] `docs-dev/q3a-botlib-runtime-aas-shell-2026-06-17.md`: runtime AAS shell implementation log.
- [x] `docs-dev/q3a-botlib-import-boundary-2026-06-17.md`: Q3A BotLib import boundary and adapter-shell implementation log.
- [x] `docs-dev/q3a-botlib-aas-reach-query-2026-06-17.md`: Q3A AAS reachability import and runtime smoke log.
- [x] `docs-dev/q3a-botlib-aas-route-query-2026-06-17.md`: Q3A AAS route-query import and runtime smoke log.
- [x] `docs-dev/q3a-botlib-aas-entity-cache-2026-06-17.md`: Q3A AAS entity-cache import and runtime smoke log.
- [x] `docs-dev/q3a-botlib-aas-entity-sync-2026-06-17.md`: WORR entity snapshot to Q3A AAS entity-cache sync implementation log.
- [x] `docs-dev/q3a-botlib-aas-entity-trace-2026-06-17.md`: Q3A AAS entity collision to WORR entity trace bridge implementation log.
- [x] `docs-dev/q3a-botlib-aas-bsp-leaf-link-2026-06-17.md`: Q3A AAS BSP leaf entity-link and box-query implementation log.
- [x] `docs-dev/q3a-botlib-aas-debug-draw-bridge-2026-06-17.md`: Q3A debug draw callback bridge implementation log.
- [x] `docs-dev/q3a-botlib-aas-route-overlay-2026-06-17.md`: Q3A route/goal overlay smoke implementation log.
- [x] `docs-dev/q3a-botlib-aas-debug-polygon-bridge-2026-06-17.md`: Q3A debug polygon callback bridge implementation log.
- [x] `docs-dev/q3a-botlib-aas-debug-area-helpers-2026-06-17.md`: Q3A AAS debug-area helper import and runtime smoke log.
- [x] `docs-dev/q3a-botlib-aas-cluster-import-2026-06-17.md`: Q3A AAS clustering import and runtime smoke log.
- [x] `docs-dev/q3a-botlib-aas-alternative-route-import-2026-06-17.md`: Q3A AAS alternative-route import and runtime smoke log.
- [x] `docs-dev/q3a-botlib-aas-optimize-import-2026-06-17.md`: Q3A AAS optimization import and no-op replacement log.
- [x] `docs-dev/q3a-botlib-print-bridge-2026-06-17.md`: Q3A BotLib print callback bridge and runtime smoke log.
- [x] `docs-dev/q3a-botlib-bot-client-command-bridge-2026-06-17.md`: Q3A BotLib client-command safety bridge and runtime smoke log.
- [x] `docs-dev/q3a-botlib-memory-allocator-bridge-2026-06-17.md`: Q3A BotLib tracked memory allocator bridge and runtime smoke log.
- [x] `docs-dev/q3a-botlib-filesystem-bridge-2026-06-17.md`: Q3A BotLib read-only filesystem callback bridge and runtime smoke log.
- [x] `docs-dev/q3a-botlib-dedicated-lifecycle-smoke-2026-06-17.md`: dedicated server lifecycle self-smoke and map-reload clean-unload log.
- [x] `docs-dev/q3a-botlib-bot-slot-lifecycle-2026-06-17.md`: first fake-client slot lifecycle and operator command log.
- [x] `docs-dev/q3a-botlib-multibot-slot-queue-2026-06-17.md`: deferred multi-bot slot queue and two-active-bot smoke log.
- [x] `docs-dev/q3a-botlib-min-players-autofill-2026-06-17.md`: min-player autofill and fill/trim/disable smoke log.
- [x] `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`: profile loader, reload command, and profile smoke log.
- [x] `docs-dev/q3a-botlib-profile-behavior-fields-2026-06-17.md`: richer profile behavior fields and expanded profile smoke log.
- [x] `docs-dev/q3a-botlib-team-policy-cleanup-2026-06-17.md`: bot team-limit and mode-change cleanup log.
- [x] `docs-dev/q3a-botlib-team-policy-smoke-2026-06-17.md`: direct game-side bot team-policy smoke log.
- [x] `docs-dev/q3a-botlib-bridge-time-vector-2026-06-17.md`: bridge time and vector helper implementation log.
- [x] `docs-dev/q3a-botlib-bsp-entity-bridge-2026-06-17.md`: active-map Q2 BSP entity-lump bridge implementation log.
- [x] `docs-dev/q3a-botlib-bsp-model-bridge-2026-06-17.md`: active-map Q2 BSP model-lump bridge implementation log.
- [x] `docs-dev/q3a-botlib-bsp-collision-bridge-2026-06-17.md`: active-map Q2 BSP static collision bridge implementation log.
- [x] `docs-dev/q3a-botlib-bsp-visibility-bridge-2026-06-17.md`: active-map Q2 BSP PVS/PHS visibility bridge implementation log.
- [ ] `docs-dev/q3a-botlib-runtime-implementation-YYYY-MM-DD.md`: imported runtime/adapter implementation log.
- [x] `docs-dev/q2aas-generator-vendor-bootstrap-2026-06-16.md`: generator vendor/build bootstrap log.
- [x] `docs-dev/q2aas-generator-q2-preset-validation-2026-06-16.md`: generator Q2 preset and validation harness log.
- [x] `docs-dev/q2aas-generator-q2-reachability-bridge-2026-06-16.md`: generator Q2 reachability bridge log.
- [x] `docs-dev/q2aas-generator-validation-matrix-2026-06-16.md`: generator validation matrix and invalid-input smoke log.
- [x] `docs-dev/q2aas-generator-deterministic-metadata-2026-06-16.md`: deterministic AAS sidecar metadata and Q2 header preflight log.
- [x] `docs-dev/q2aas-generator-entity-diagnostics-2026-06-16.md`: generator entity/content diagnostics log.
- [x] `docs-dev/q2aas-generator-diagnostic-gates-2026-06-16.md`: generator diagnostic-gate validation log.
- [x] `docs-dev/q2aas-generator-baseline-regression-gates-2026-06-17.md`: generator baseline-regression validation log.
- [x] `docs-dev/q2aas-generator-manifest-schema-validation-2026-06-17.md`: generator manifest-schema validation log.
- [x] `docs-dev/q2aas-generator-manifest-schema-smoke-2026-06-17.md`: generator manifest expected-failure smoke log.
- [x] `docs-dev/q2aas-generator-aas-staging-2026-06-17.md`: generator validated AAS staging log.
- [x] `docs-dev/q2aas-generator-stage-audit-2026-06-17.md`: generator staged artifact audit log.
- [x] `docs-dev/q2aas-generator-packaged-map-smoke-2026-06-17.md`: generator archive-backed map smoke log.
- [x] `docs-dev/q2aas-generator-archive-manifest-guardrails-2026-06-17.md`: generator archive manifest guardrail log.
- [x] `docs-dev/q2aas-generator-package-audit-2026-06-17.md`: generator package-readiness audit log.
- [x] `docs-dev/q2aas-generator-archive-packaging-2026-06-17.md`: generator AAS archive packaging log.
- [x] `docs-dev/q2aas-generator-refresh-install-integration-2026-06-17.md`: generator refresh-install integration log.
- [x] `docs-dev/q2aas-generator-stage-archive-member-validation-2026-06-17.md`: generator release archive member validation log.
- [ ] `docs-dev/q2-aas-generator-implementation-YYYY-MM-DD.md`: generator tailoring implementation log.
- [ ] `docs-user/bots.md` or equivalent user-facing page:
  - [ ] How to add/remove bots.
  - [ ] Recommended cvars.
  - [ ] Known limitations.
  - [ ] AAS generation/package behavior in practical language.
- [ ] Roadmap updated after each task reaches `Done`.

Exit criteria:

- Developers can diagnose bot/AAS failures without reading Q3A source first.
- Server operators can enable basic bots with a small set of documented commands/cvars.

## Phase 9: Validation, Performance, and Release Packaging

Primary tasks: `DV-03-T05`, `FR-04-T16`, `DV-05-T05`

Goal: keep bots shippable rather than "works on one map."

Validation checklist:

- [ ] Build:
  - [ ] Windows local build.
  - [ ] Linux CI build once CI coverage exists.
  - [ ] macOS CI build once CI coverage exists.
  - [ ] Dedicated server build.
- [ ] Tool:
  - [x] `worr_q2aas` builds.
  - [x] WORR/Q2 generator cfg smoke target runs.
  - [x] Staged map structural AAS smoke emits `.tmp/q2aas/mm-rage.aas`.
  - [x] Staged map strict reachability smoke passes for `.install/basew/maps/mm-rage.bsp`.
  - [x] Manifest-driven staged smoke target writes `.tmp/q2aas/validation-report.json`.
  - [x] Manifest-driven staged smoke target writes `.tmp/q2aas/mm-rage.aas.meta.json`.
  - [x] Manifest-driven staged smoke target requires Q2 `IBSP` version 38 input.
  - [x] Manifest-driven staged smoke target reports spawn/item origin coverage, high-value pickup reachability, brush contents, and mover/trigger inventory.
  - [x] Manifest-driven staged smoke target fails on clean BSP lump, spawn coverage, item coverage, and high-value pickup reachability regressions.
  - [x] Manifest-driven staged smoke target fails on structural AAS metric and travel-count regressions below manifest minima.
  - [x] Manifest-driven staged smoke target validates manifest schema, task IDs, gate types, and baseline keys before conversion.
  - [x] Manifest-driven staged smoke target runs an expected-failure malformed-manifest schema smoke.
  - [x] Manifest-driven staged smoke target rejects malformed archive-backed map entries before conversion.
  - [x] Manifest-driven stage target writes validated `.install/basew/maps/mm-rage.aas` and `.tmp/q2aas/stage-report.json`.
  - [x] Manifest-driven stage audit target validates staged `.aas` path, size, and hashes against `.tmp/q2aas/stage-report.json`.
  - [x] Package map smoke target validates pkz archive extraction and conversion using a scratch packaged `mm-rage.bsp`.
  - [x] Package audit target validates staged AAS release payload representation and writes `.tmp/q2aas/package-audit-report.json`.
  - [x] Package archive target injects staged AAS output into `.install/basew/pak0.pkz` and writes `.tmp/q2aas/package-archive-report.json`.
  - [x] Package archive audit target requires the packaged `maps/mm-rage.aas` member and writes `.tmp/q2aas/package-archive-audit-report.json`.
  - [x] Refresh-install workflow supports `--package-q2aas-aas`, re-injects generated AAS after asset packaging, and validates the staged Windows payload.
  - [x] Generic stage validation can require packaged archive members with SHA-256 checks, and refresh-install passes q2aas AAS requirements from the stage report.
  - [ ] Reference maps generate AAS.
  - [x] Invalid BSP inputs fail clearly.
  - [x] AAS metadata receives the source BSP checksum during Q2 bridge generation.
- [ ] Runtime smoke:
  - [x] Server-game module builds with the WORR-native AAS runtime shell.
  - [x] Start dedicated server on reference map with `sg_bot_enable 1` and load packaged `maps/mm-rage.aas` through the runtime shell.
  - [x] Run imported Q3A `AAS_AreaInfo` / `AAS_PointAreaNum` sample query against loaded `maps/mm-rage.aas`.
  - [x] Run imported Q3A `AAS_AreaReachability` sample query against loaded `maps/mm-rage.aas`.
  - [x] Verify imported Q3A AAS clustering reports cluster counts and sampled cluster area/reachability counts in verbose adapter output.
  - [x] Run imported Q3A `AAS_AreaTravelTimeToGoalArea` / `AAS_PredictRoute` route query against loaded `maps/mm-rage.aas`.
  - [x] Run imported Q3A `AAS_AlternativeRouteGoals` query against loaded `maps/mm-rage.aas`.
  - [x] Verify imported Q3A entity cache survives setup/start-frame invalidation in verbose adapter output.
  - [x] Verify WORR entity snapshots update imported Q3A `AAS_UpdateEntity` with passed counters in verbose adapter output.
  - [x] Verify Q3A `AAS_EntityCollision` reaches the WORR entity trace callback and reports a smoke hit in verbose adapter output.
  - [x] Verify bridge-fed Q3A runtime time and `AngleVectors` smoke status in verbose adapter output.
  - [x] Verify active-map Q2 BSP entity-lump parsing and Q3A epair smoke status in verbose adapter output.
  - [x] Verify active-map Q2 BSP model-lump parsing and Q3A inline model bounds smoke status in verbose adapter output.
  - [x] Verify active-map Q2 BSP collision-lump parsing plus Q3A static point-contents and trace smoke status in verbose adapter output.
  - [x] Verify imported Q3A BSP leaf entity links and `AAS_BoxEntities` return linked entities in verbose adapter output.
  - [x] Verify active-map Q2 BSP visibility-lump parsing plus Q3A PVS/PHS smoke status in verbose adapter output.
  - [x] Verify Q3A debug polygon create/delete callbacks report passed counters in verbose adapter output.
  - [x] Verify imported Q3A `AAS_ShowArea` / `AAS_ShowAreaPolygons` report passed line/polygon counters in verbose adapter output.
  - [x] Verify Q3A `BotClientCommand` reaches the WORR adapter and is safely rejected until the bot command dispatcher exists.
  - [x] Verify Q3A BotLib memory allocator counters report active/peak zone and hunk usage with zero failures in verbose adapter output.
  - [x] Verify Q3A BotLib filesystem counters report packaged active-AAS reads through WORR FS with zero fallback memory opens in verbose adapter output.
  - [x] Verify Q3A BotLib filesystem counters keep optional `.rcd` route-cache misses out of `q3a_fs_open_failures` in the import smoke harness.
  - [x] Verify three repeated imported Q3A AAS load/unload cycles report `clean_unloads=3`, `unload_failures=0`, `last_unload_zone_active=0`, `last_unload_hunk_active=0`, and `last_unload_open_files=0` in the lifecycle import harness.
  - [x] Start dedicated server on reference map with real BotLib initialized.
  - [x] Run dedicated self-smoke with `sg_bot_lifecycle_smoke 3` so `mm-rage` loads, reloads once, captures `q3a_lifecycle_clean_unloads=1` during each shutdown, and exits.
  - [x] Verify dedicated team-policy smoke counts bot `gclient_t::sess.team` state through the game module and reports `playing=2`, `spectators=1`, `bots=3` for a three-bot Duel setup.
  - [ ] Add one bot.
  - [ ] Add four bots.
  - [ ] Add eight bots.
  - [ ] Run 10 minutes without crash.
  - [ ] Map change and repeat.
- [ ] Scenario tests:
  - [ ] Spawn and route to item.
  - [ ] Engage enemy.
  - [ ] Switch weapons.
  - [ ] Pick up health/armor.
  - [ ] Follow team objective.
  - [ ] Recover from blocked route.
- [ ] Performance:
  - [ ] CPU cost per bot.
  - [ ] Route recomputation rate.
  - [ ] Visibility trace count.
  - [ ] Memory used by AAS.
  - [ ] High bot count degradation policy.
- [ ] Packaging:
  - [ ] `.install/` refreshed after build.
  - [ ] Generated AAS included or intentionally generated on demand.
  - [ ] Tool binary included only if release policy allows it.
  - [ ] Credit/license files included with distributed source/binaries.

Exit criteria:

- Bots and generated AAS survive normal release packaging and smoke testing.
- Known map failures are tracked with diagnostics, not rediscovered manually.

## Credit and Attribution Policy

The project must maintain full credits as work proceeds. The minimum bar is:

- Preserve original source headers in imported Q3A/BSPC files.
- Add local modification notes with date and scope when files are edited.
- Maintain a source ledger under `docs-dev/`.
- Include `TTimo/bspc`, `bnoordhuis/bspc`, id Software, and file-level contributors discovered from upstream history.
- Keep the ledger updated in the same PR/commit as any imported or substantially adapted file.
- Do not collapse "inspired by" and "copied from" into one bucket. The ledger must distinguish:
  - Direct source import.
  - Heavily modified source derivative.
  - Algorithm/concept reference.
  - Clean WORR-native implementation.
- For external source snapshots, record exact commit hashes and retrieval date.
- For vendored code, include license text in the distributed source tree and release packaging where required.

Recommended ledger columns:

| WORR Path | Upstream Path/URL | Upstream Commit | Use Type | License | Copyright/Header | Contributors | Local Changes | Verification |
|---|---|---|---|---|---|---|---|---|

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Q3A BotLib assumes Q3 movement and item rules. | Bots route or fight poorly in Q2 maps. | Treat BotLib as nav/goal infrastructure and build WORR-native Q2 behavior above it. |
| AAS generation fails on Q2R/BSPX map variants. | Bots cannot navigate affected maps. | Add tolerant BSP parsing, checksum metadata, and map-specific diagnostics in Phase 1. |
| Mover/door/elevator reachability is inaccurate. | Bots get stuck in common maps. | Model movers from entity lump and add runtime conditional reachability/stuck recovery. |
| Imported C code spreads globals into sgame. | Long-term maintainability suffers. | Keep imported code quarantined behind adapter APIs. |
| License/provenance is incomplete. | Legal and community trust risk. | Make credit ledger a Definition of Done item for every task. |
| High bot counts spike server frames. | Server performance regression. | Stagger expensive work, add CPU budgets, add perf telemetry before polish. |
| Bots feel unfair. | Poor player experience. | Use perception, reaction, aim noise, and item timer fairness gates. |
| Packaging misses AAS/tool files. | Release works locally but not for users. | Tie `.install/` and package validation into `FR-04-T16`. |

## Final Acceptance Criteria

The Q3A bot port is complete when:

- `worr_q2aas` or equivalent can generate usable AAS for the supported reference map set.
- WORR can load generated AAS and initialize BotLib on map start.
- Bots can be added/removed during a running server.
- Bots can navigate, collect useful items, fight, and recover from common route failures.
- Bots participate correctly in FFA/TDM/CTF and do not break match/vote/map transition flows.
- All new public cvars use `sg_bot_*` unless explicitly documented as imported compatibility controls.
- `.install/` staging and packaging include all required bot/AAS artifacts or a documented generation path.
- Credits, license files, source provenance, implementation logs, user docs, and roadmap task statuses are current.
