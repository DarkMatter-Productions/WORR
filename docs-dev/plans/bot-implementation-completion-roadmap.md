# Full Bot Implementation Completion Roadmap

Date: 2026-06-22

Status: Living roadmap for post-checklist bot completion.

Primary tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T04`, `FR-04-T05`,
`FR-04-T06`, `FR-04-T07`, `FR-04-T11`, `FR-04-T12`, `FR-04-T14`,
`FR-04-T15`, `FR-04-T16`, `DV-03-T05`, and `DV-07-T06`.

Strategic parent:
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md`.

Implementation foundation:
`docs-dev/plans/q3a-botlib-aas-port.md`.

## Purpose

This is the go-forward roadmap for completing WORR bots beyond the original
Q3A BotLib/AAS port checklist. The earlier port plan proves that the core
runtime, generated AAS, fake-client lifecycle, profile loading, command path,
smoke scenarios, and validation harness are in place. This document tracks the
remaining work needed to turn those proof surfaces into bots that are fun,
reliable, useful on real servers, and shippable.

Use this file when choosing the next implementation slice. Each slice should
land with:

- Source changes that move one roadmap item from proof or smoke behavior toward
  live behavior.
- A focused implementation log under `docs-dev/`.
- Scenario, perf, or map validation evidence under `.tmp/`.
- Updated stats in this file and in the parent roadmap docs when the catalog or
  completion state changes.
- A refreshed `.install/` payload whenever binaries, botfiles, AAS assets, or
  packaged data changed.

This file intentionally avoids raw markdown task checkboxes. Completion is
tracked through milestone tables, status fields, scenario evidence, and the
canonical strategic roadmap.

## Current Baseline

Snapshot from 2026-06-22 after the smoke contract reconciliation round:

| Area | Current State |
|---|---|
| Original phase checklist | `809/809` phase items complete. |
| Raw markdown checklist | `809/809` rows complete. |
| Scenario catalog | `77` implemented rows total: `76` automated short-run rows plus `1` manual high-bot degradation row. |
| Default pending rows | `0`. |
| Highest bot frame-command smoke mode | `71`. |
| Latest aggregate artifact | `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`, with `76/76` automated `implemented` rows passing. |
| Latest focused artifact | `.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z` for `combat_survival_regression`. |
| Core runtime | Q3A BotLib/AAS loads generated AAS, updates entity snapshots, routes bots, and emits source counters. |
| Bot lifecycle | Server-owned fake clients can be added, removed, auto-filled, cleaned up, and classified in match flow. |
| Profiles | Q3-style WORR botfiles load behavior metadata, roles, movement style, item policy, team policy, aim hints, and chat personality. |
| Behavior status | Many policies have default-off proof gates, status markers, implemented smoke rows, and first-pass live blackboard target-memory telemetry. |
| Release staging | `.install/` staging packages botfiles and validated AAS artifacts, with release workflow hooks in place. |

The important remaining gap is not "can a bot be spawned and proven in a smoke
case?" That is already true. The remaining gap is "can bots make durable,
autonomous, map-backed decisions across normal play without relying on staged
proof cvars?"

## Completion Definition

Bot implementation is considered complete for the current FR-04 scope when all
of the following are true:

| Gate | Requirement | Evidence |
|---|---|---|
| Live loop | Bots can play FFA, TDM, Duel, CTF, and selected coop/campaign maps through default or documented cvars without smoke-only setup. | Full implemented scenario suite plus representative live server runs. |
| Combat | Bots acquire enemies, aim, fire, select weapons, manage ammo, use inventory, and avoid obvious self/team sabotage. | Combat, weapon, inventory, friendly-fire, and survival scenarios with non-scripted live observations. |
| Items | Bots pursue useful items, deny resources, time observed pickups, respect reservations, and recover when goals disappear. | Item economy scenarios across multiple maps and modes. |
| Team play | Bots choose roles, lanes, objectives, support tasks, and handoffs in TDM/CTF without fighting the match systems. | Role-route, role-combat, objective, carrier-support, base-return, dropped-flag, and scoreboard/match-flow validation. |
| Coop | Bots can follow, wait, lead, unblock, share resources, target monsters, operate simple interactions, and avoid blocking progression. | Coop reference-map scenarios and long-form campaign flow tests. |
| Movement | Bots handle AAS routes, stuck recovery, jumps, ladders, elevators, doors, teleporters, water, crouch, and known hazards at a playable level. | Reference-map movement matrix and diagnostics for known unsupported cases. |
| Chat/personality | Profile personality affects safe live chat, not only smoke proof events, with rate limits and team/global audience rules. | Chat event scenarios plus user-facing documentation of supported behavior. |
| Performance | CPU, trace, route, visibility, and memory budgets are known and stable for normal bot counts and manual high-bot soaks. | Fresh source-counter long soaks, budget files, and regression checks. |
| Packaging | Bots, botfiles, AAS files, cvars, docs, and validation tools work from a refreshed `.install/` payload and release workflows. | Package audit, no-zlib dedicated validation, CI builds, and user docs. |

## Roadmap At A Glance

| Milestone | Name | Main Task IDs | Status | Outcome |
|---|---|---|---|---|
| M0 | Foundation Snapshot | `FR-04-T10`, `FR-04-T11`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06` | Done for current scope | The BotLib/AAS runtime, bot lifecycle, scenarios, assets, and ledgers exist. |
| M1 | Live Behavior Arbitration | `FR-04-T02`, `FR-04-T15` | Done for initial live owner model | The bot brain now exposes ordered owner arbitration, cvar classification, handoff status, and mode `63` runtime proof. |
| M2 | Combat And Inventory Depth | `FR-04-T03`, `FR-04-T15` | In progress; target-memory, weapon-scoring, aim/fire policy, ammo-pressure, survival-inventory, survival-health/armor routing, compact combat regression, and full-suite smoke contract reconciliation done | Bots fight with sensible weapons, aim, ammo, inventory, and survival decisions. |
| M3 | Multiplayer Mode Intelligence | `FR-04-T04`, `FR-04-T06`, `FR-04-T15` | Planned | Bots play FFA, Duel, TDM, and CTF objectives coherently. |
| M4 | Coop And Campaign Behavior | `FR-04-T04`, `FR-04-T05`, `FR-04-T15` | Planned | Bots help rather than block campaign and coop progression. |
| M5 | Chat And Personality | `FR-04-T07`, `FR-04-T15` | Planned | Profile personality influences safe live communication and behavior flavor. |
| M6 | Map, AAS, And Movement Coverage | `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16` | Planned | Bots have reliable navigation evidence across representative map families. |
| M7 | Performance, Soak, And Reliability | `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05` | Planned | Performance budgets and long-run behavior are stable enough to ship. |
| M8 | Productization And Release Readiness | `FR-04-T07`, `FR-04-T16`, `DV-07-T06` | Planned | Operator docs, defaults, packaging, and release notes are complete. |

## M0: Foundation Snapshot

Status: Done for the current post-checklist baseline.

This milestone is the launchpad for the remaining work, not a new work queue.
It captures what the existing Q3A BotLib/AAS port already proved.

Delivered:

- Q3A BotLib/AAS import boundary, adapter, loader, entity sync, trace, PVS/PHS,
  debug draw, memory, filesystem, and source-counter surfaces.
- WORR q2aas generator, reference-map validation, AAS staging, package audits,
  archive injection, and refresh-install integration.
- Server-owned bot add/remove/autofill lifecycle, profile loading, botfiles,
  package/loose profile staging, and user-facing setup docs.
- Route-steered command generation, route cache, item goals, item reservation,
  stuck recovery, movement-state commands, natural movement diagnostics, and
  map restart cleanup.
- Behavior proof surfaces for combat, items, roles, CTF objectives, coop
  helpers, match flow, profile hints, behavior umbrella, and bot chat policy.
- Scenario harness, pending-gap tooling, raw marker parsing, source-counter
  parsing, perf analysis, manual high-bot soak policy, and 77 implemented
  catalog rows.

Keep this milestone stable by preserving existing smoke scenarios whenever a
proof-only behavior is promoted into live behavior.

## M1: Live Behavior Arbitration

Status: Done for the initial live owner model.

Goal: turn the current family of proof helpers into one predictable bot brain
decision loop that can run in normal matches.

Why this is first: most remaining behavior work depends on a clean arbitration
layer. Without it, every feature competes through special-case cvars and proof
hooks.

Implemented slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Decision priority model | Defined the owner order as recovery, interaction, objective, combat, item, route, then idle. | `Bot_CommandSelectBehaviorArbitrationOwner` chooses the winning owner and records id, name, reason, and priority. |
| 2 | Proof-cvar audit | Classified behavior cvars as live, smoke-only, debug-only, or deprecated in `q3a_bot_behavior_policy_status`. | Mode `63` proves `behavior_live_policy_cvars=8` and all smoke/debug/deprecated counts at `0`. |
| 3 | Ownership handoff rules | Added per-client owner memory and handoff counting. | Mode `63` passed with route, item, and combat candidates plus `behavior_arbitration_handoffs=3`. |
| 4 | Live behavior scenario | Added `behavior_arbitration` as frame-command mode `63`. | Focused validation passed from `.tmp\bot_scenarios\20260622T112202Z`. |
| 5 | Status cleanup | Added a dedicated optional field family and parser checks for the arbitration status surface. | Scenario marker checks are strict without relying on individual proof cvars. |

Validation gates:

- Existing modes `20` through `70` are implemented in the catalog.
- `behavior_arbitration` proves route, item, and combat candidates plus combat
  ownership without setting individual proof cvars.
- No smoke-only cvar is required for the M1 behavior arbitration proof.

## M2: Combat And Inventory Depth

Status: In progress; sustained enemy target memory/decay, weapon-scoring
arsenal, aim/fire policy depth proof, ammo-pressure pickup proof, carried
survival inventory-use proof, survival health-route proof, survival
armor-route proof, a compact combat/survival regression proof, and the
expanded full-suite smoke contract reconciliation are implemented.

Goal: make bots fight competently in normal Q2/WORR situations.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Enemy selection loop | Promote visible/shootable enemy facts into sustained target selection with target memory and decay. | Mode `64` `target_memory_decay` proves retained unseen target memory, a `1000` ms decay window, blackboard memory age/window telemetry, and clear-after-decay status. |
| 2 | Weapon scoring | Expand weapon choice by distance, ammo, splash risk, enemy health/armor estimate, water/hazard context, and inventory state. | Mode `65` `weapon_scoring_arsenal` proves carried arsenal scanning, insufficient-ammo rejection, splash-risk pressure, close-range super-shotgun selection, enemy-estimate finisher scoring, and switch completion. |
| 3 | Aim and fire policy | Tune aim profile, reaction timing, projectile leading, splash caution, and line-of-fire checks. | Mode `66` `aim_fire_policy_depth` proves reaction-delay withholding, aim-settle withholding, burst-cooldown pacing, live-aim policy blocks, rocket projectile lead, and eventual attack-button application. |
| 4 | Ammo and pickup pressure | Connect low-ammo and preferred-weapon state to item/route priorities. | Mode `67` `ammo_pressure_pickup` proves ammo focus, low-shell staged routeable shell pickup selection, ammo candidates/seek decisions, ammo goal assignments, and zero route failures. |
| 5 | Inventory usage | Promote carried inventory and powerup decisions into live use policy with safety checks. | Mode `68` `survival_inventory_use` proves low-health/no-armor survival pressure can select carried power armor, accept a pending inventory intent, build a validated command request, and dispatch `use_index_only`; other special inventory classes remain covered by existing policy counters until broader regression rows land. |
| 6 | Survival behavior | Expand health/armor retreat, threat avoidance, and emergency item routing. | Mode `69` `survival_health_route` proves low-health health routing, and mode `70` `survival_armor_route` proves full-health/no-armor armor routing with armor candidates, seek decisions, low-armor boosts, armor goal assignment, resolved AAS goal area, and zero route failures without item focus. |
| 7 | Combat regression set | Build compact combat scenarios that are not single-script proofs. | Mode `71` `combat_survival_regression` proves visible/shootable enemy pressure remains visible to blackboard/action telemetry while low-health health routing, withheld-fire policy, item ownership, and recovery ownership can safely win under survival pressure. |

Latest validation note:

- Focused mode `71` validation passed from
  `.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z`.
  The row recorded `frames=121`, `commands=121`, `route_commands=121`,
  `route_failures=0`, `item_goal_assignments=7`,
  `last_item_goal_area=224`, `combat_enemy_visible=120`,
  `combat_enemy_shootable=119`, `combat_withheld_fire=35`,
  `behavior_arbitration_item_owners=3`, and
  `behavior_arbitration_recovery_owners=40`.
- The follow-up smoke contract reconciliation run passed the full automated
  `implemented` catalog from
  `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`,
  reporting `76` passed rows, `0` failed rows, `0` timeouts, `0` errors, and
  `0` pending rows. Focused checks also passed for `profile_item_policy`,
  `team_fire_avoidance`, `ffa_roam_route`, `team_role_route`, and
  `aim_fairness_policy_integration` after the status-surface fixes.

Validation gates:

- Combat scenarios pass on at least two reference maps.
- Friendly-fire suppression still wins over attack decisions in team modes.
- Bots do not rely on direct teleport/staged target setup for all combat proof.
- Manual playtest confirms bots are beatable but not inert.

## M3: Multiplayer Mode Intelligence

Status: Planned.

Goal: make bots understand the mode they are playing, not just move and fight
inside it.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | FFA loop | Combine roam, item, anti-camp, combat, and spawn-pressure decisions. | FFA bots circulate, fight, and recover without camping one proof location. |
| 2 | Duel loop | Add duel-specific pacing, item denial, spawn pressure, and spectator/queue behavior. | Duel bots respect match flow and make duel-relevant item/combat decisions. |
| 3 | TDM role loop | Promote attacker/defender/support/roam role selection into ongoing team behavior. | TDM bots distribute useful roles and avoid all choosing the same high-value target. |
| 4 | CTF objective loop | Promote dropped flag, carrier support, base return, defense, offense, and item role policies into live objective choice. | CTF bots react to flag state without dedicated proof setup. |
| 5 | Objective handoff | Add explicit handoff between generic role route and higher-priority objective route. | Objective precedence remains stable when multiple goals are valid. |
| 6 | Match ecosystem audits | Keep map vote, MyMap, nextmap, warmup, scoreboard, intermission, tournament, and admin boundaries bot-safe. | Existing match-flow smoke rows still pass after live behavior changes. |

Validation gates:

- One representative scenario per mode proves live policy cooperation.
- CTF scenarios verify flag pickup, dropped flag, carrier support, base return,
  and scoring/return gameplay hooks.
- Team modes avoid obvious team sabotage such as friendly fire, objective
  abandonment, and resource starvation.

## M4: Coop And Campaign Behavior

Status: Planned.

Goal: make bots useful in coop and campaign maps instead of treating them like
arena deathmatch with monsters.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Follow/wait baseline | Promote FollowLeader, WaitForLeader, LeadAdvance, and progress-wait owners into live coop loop. | Bots keep up with humans and wait near progression boundaries. |
| 2 | Interaction ownership | Expand door/elevator/use retry logic to common map entities and teammate hold behavior. | Bots can help with simple interactions and stop jamming movers. |
| 3 | Monster target sharing | Promote blackboard target-sharing from smoke proof to live combat support. | Bots adopt relevant monster targets without stealing every player fight. |
| 4 | Resource sharing | Make health/ammo/powerup choices coop-aware, especially around low-health humans and scarce pickups. | Bots do not greedily consume critical resources in coop tests. |
| 5 | Anti-blocking | Expand close-leader and choke-point anti-blocking movement. | Bots clear doorways, elevators, and narrow progression paths. |
| 6 | Trigger/key/objective support | Add map-backed evidence for trigger, key, button, and objective progression. | Reference campaign maps have explicit pass/fail diagnostics. |

Validation gates:

- Coop reference scenarios cover at least one base campaign map and one
  interaction-heavy map.
- Bots can finish or assist a small campaign flow with a human or simulated
  leader.
- Bot behavior never blocks final progression without a recovery path.

## M5: Chat And Personality

Status: Planned.

Goal: convert profile chat metadata from smoke proof into safe, useful live
personality.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Event taxonomy | Define supported live chat events: spawn, team ready, route ready, item taken, item denied, enemy sighted, objective changed, flag state, low health, blocked, and victory/defeat. | Events are named, documented, and rate-limit aware. |
| 2 | Phrase libraries | Expand phrase buckets for quiet, direct, taunting, helpful, steady, and future personalities. | Repeated events do not spam identical lines. |
| 3 | Audience policy | Harden global/team/private audience selection and human-only broadcast behavior. | Bots do not send reliable-message chatter to bot clients. |
| 4 | Conversation safety | Add global and per-bot cooldowns, event suppression, and duplicate prevention. | Chat remains readable in four and eight bot tests. |
| 5 | Profile integration | Let personality influence small behavior flavor where safe, such as aggression phrasing, support communication, and objective callouts. | Personality changes are visible but not balance-breaking. |

Validation gates:

- Existing chat modes `57` through `62` remain as regression proofs.
- New live event chat scenario verifies at least four event types and rate
  limiting.
- User docs clearly state which chat behavior is supported.

## M6: Map, AAS, And Movement Coverage

Status: Planned.

Goal: make navigation reliable across the maps players actually use.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Reference-map matrix | Expand the map matrix by DM, TDM, CTF, coop/campaign, expansion, BSPX-heavy, liquid, teleport, and mover-heavy categories. | Each category has at least one staged map with validation expectations. |
| 2 | Movement feature proof | Add map-backed proof for crouch, swim, waterjump, ladders, walk-off ledges, jumps, elevators, doors, teleporters, slime/lava-adjacent hazards, and recovery. | Unsupported features are explicit known failures, not surprises. |
| 3 | AAS diagnostics | Improve q2aas reports for unreachable spawns/items/objectives, bad entity coverage, mover routes, and required-feature gaps. | A map can be triaged from reports without manual spelunking first. |
| 4 | Runtime fallback policy | Define what bots do when AAS is missing, partial, or invalid. | Servers fail gracefully with clear console/status output. |
| 5 | Packaging breadth | Stage and package validated AAS for the accepted map set. | `.install/basew` has all approved AAS files loose or in `pak0.pkz` as policy requires. |

Validation gates:

- Reference validation passes for the accepted map set.
- Scenario rows use more than one map for key navigation and combat claims.
- Release packaging audits include the accepted AAS artifacts.

## M7: Performance, Soak, And Reliability

Status: Planned.

Goal: make bot behavior stable for real server runtimes, not only short proof
runs.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Fresh source-counter soak | Rerun the ten-minute eight-bot soak with current source-counter fields. | CPU, trace, route, visibility, and memory fields are present in the soak artifact. |
| 2 | Budget tightening | Convert stable observed ranges into budget thresholds by scenario class. | Regressions fail loudly while expected variance remains tolerated. |
| 3 | Multi-map soak | Add shorter multi-map soaks for map change, restart, CTF, and coop transitions. | Bots survive repeated transitions without stale state. |
| 4 | Higher bot pressure | Test above the default eight-bot manual row if server limits and maps allow it. | Degradation behavior is understood and documented. |
| 5 | Crash/leak audit | Track active route goals, reservations, BotLib memory, file handles, and bot slots through repeated cycles. | Clean unload and final cleanup counters remain zero-leak. |

Validation gates:

- Manual high-bot soak has a fresh artifact using current counters.
- Budget files are updated and documented.
- `.install/` refresh plus scenario suite remains part of the normal validation
  rhythm after any behavior or packaging change.

## M8: Productization And Release Readiness

Status: Planned.

Goal: make bots understandable and supportable for users and server operators.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Public cvar audit | Decide which `sg_bot_*` cvars are public, experimental, debug, or smoke-only. | User docs only present supported controls. |
| 2 | Defaults pass | Choose safe defaults for practice servers and dedicated servers. | A user can enable bots with a small config and get sane behavior. |
| 3 | Profile pack polish | Balance bundled profiles by skill, role, personality, preferred weapons, and team behavior. | Packaged profiles feel distinct and validate cleanly. |
| 4 | Operator docs | Update `docs-user/bots.md`, `docs-user/bot-profiles.md`, and relevant server docs. | Operators know setup, map/AAS limits, performance guidance, and troubleshooting steps. |
| 5 | Release packaging | Keep botfiles, AAS, package audits, no-zlib mirrors, and CI release matrices aligned. | Release artifacts contain everything needed for supported bot play. |
| 6 | Final acceptance run | Run full automated suite, focused live behavior scenarios, manual soaks, and representative playtests. | FR-04 scope can be marked complete in the strategic roadmap. |

Validation gates:

- User docs match actual supported behavior.
- `.install/` payload is refreshed and audited.
- Windows local build and CI platform builds cover bot-related targets.
- Credits and provenance remain current.

## Recommended Next Ten Slices

These are the next practical implementation slices, ordered to reduce rework.
Each slice should be small enough to validate independently.

| Order | Slice | Primary Milestone | Main Task IDs | Expected Artifact |
|---|---|---|---|---|
| 1 | Combat regression set across a second reference map | M2 | `FR-04-T03`, `FR-04-T15`, `DV-03-T05` | Compact combat rows that reuse blackboard target-memory, weapon, aim, and survival checks away from one staged layout. |
| 2 | Threat-retreat and avoidance behavior | M2 | `FR-04-T03`, `FR-04-T15` | Scenario proving low-health bots can break contact, recover, and re-engage without smoke-only target setup. |
| 3 | CTF live objective loop promotion | M3 | `FR-04-T04`, `FR-04-T15` | CTF scenario that reacts to flag state without depending on only one proof target source. |
| 4 | TDM role/lane stability over spawn changes | M3 | `FR-04-T04`, `FR-04-T15` | Scenario proving role-route and combat owners remain stable after deaths/restarts. |
| 5 | FFA/Duel live pacing | M3 | `FR-04-T04`, `FR-04-T06`, `FR-04-T15` | Scenario proving roam, item denial, spawn pressure, and combat arbitration in normal match flow. |
| 6 | Coop follow/wait/anti-blocking live loop | M4 | `FR-04-T04`, `FR-04-T05`, `FR-04-T15` | Coop reference-map scenario with leader, wait, interaction, and unblock evidence. |
| 7 | Coop target/resource sharing promotion | M4 | `FR-04-T04`, `FR-04-T05`, `FR-04-T15` | Scenario proving target adoption and item deferral from live coop observations. |
| 8 | Live chat event taxonomy and first non-smoke event trigger | M5 | `FR-04-T07`, `FR-04-T15` | Chat event scenario proving real event source plus audience/rate limits. |
| 9 | Movement and hazard matrix expansion | M6 | `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16` | Reference-map rows for water, crouch, doors, elevators, teleporters, and hazards with explicit known gaps. |
| 10 | Fresh source-counter eight-bot soak | M7 | `FR-04-T16`, `DV-03-T05` | New `.tmp/bot_perf/` or `.tmp/bot_scenarios/` soak artifact and budget update. |

## Scenario Strategy

Scenario growth should stay evidence-driven. Do not add rows only to increase
the count. Add a row when it proves a new behavior contract, protects against a
real regression, or promotes a smoke-only proof into live behavior.

Preferred scenario ladder:

| Stage | Purpose | Example |
|---|---|---|
| Smoke proof | Prove a source-owned counter or bridge exists. | Existing modes `20` through `70`. |
| Focused integration | Prove two or three owners cooperate. | Route plus item plus combat live behavior. |
| Mode scenario | Prove mode-specific behavior over real match state. | CTF objective loop or Duel pressure loop. |
| Map matrix | Prove behavior survives different geometry and entity layouts. | Same combat/item behavior on DM and CTF maps. |
| Soak/regression | Prove stability over time and transitions. | Eight-bot ten-minute soak and map restart cycles. |

Every new scenario should define:

- The behavior contract in plain English.
- Required cvars and why each one is needed.
- Required status markers and exact pass/fail metrics.
- Expected artifact location under `.tmp/`.
- Whether it is default automated, manual-only, or diagnostic-only.

## Documentation And Tracking Rules

At the end of each significant bot slice:

- Add or update an implementation log under `docs-dev/`.
- Update this roadmap if a milestone status, next-slice order, validation gate,
  or completion definition changes.
- Update `docs-dev/plans/q3a-botlib-aas-port.md` when the scenario catalog,
  completion snapshot, or outstanding-work summary changes.
- Update
  `docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md` when a
  strategic task moves, a new task is accepted, or completion status changes.
- Update `docs-dev/q3a-botlib-aas-credits.md` when imported-source provenance,
  native replacement status, or validation evidence changes.
- Update `docs-user/` only for supported or intentionally exposed user-facing
  behavior.

## Completion Scoreboard

Keep this table current as milestones move. "Done" means the validation gates
for the milestone are satisfied, not merely that code exists.

| Milestone | Status | Last Evidence | Next Action |
|---|---|---|---|
| M0 Foundation Snapshot | Done | 77-row implemented catalog, latest automated aggregate `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z` with `76/76` rows passing. | Preserve while deepening live behavior. |
| M1 Live Behavior Arbitration | Done | Mode `63` `behavior_arbitration` proof with cvar audit, candidates, owners, and handoffs. | Use the owner model while implementing M2 combat/inventory depth. |
| M2 Combat And Inventory Depth | In progress | Mode `64` `target_memory_decay`, mode `65` `weapon_scoring_arsenal`, mode `66` `aim_fire_policy_depth`, mode `67` `ammo_pressure_pickup`, mode `68` `survival_inventory_use`, mode `69` `survival_health_route`, mode `70` `survival_armor_route`, mode `71` `combat_survival_regression`, and the green full-suite smoke contract reconciliation. | Use the green catalog as the baseline, then promote broader threat-retreat/avoidance behavior and second-map combat/item regression. |
| M3 Multiplayer Mode Intelligence | Planned | Existing FFA/TDM/CTF role and objective proof rows. | Build live mode loops and objective handoffs. |
| M4 Coop And Campaign Behavior | Planned | Existing coop readiness, leader, progress-wait, interaction, resource, anti-blocking, target-share, and door/elevator proof rows. | Promote coop helper policies into map-backed live behavior. |
| M5 Chat And Personality | Planned | Modes `57` through `62` prove dispatch, audience, rate, initial, reply, and event-policy selection. | Define live chat event taxonomy. |
| M6 Map, AAS, And Movement Coverage | Planned | Eight staged reference maps and current movement diagnostics. | Expand reference-map matrix and movement feature proof. |
| M7 Performance, Soak, And Reliability | Planned | Existing manual high-bot soak row and source-counter tooling. | Run fresh source-counter soak. |
| M8 Productization And Release Readiness | Planned | Current bot user docs, profile docs, package audits, and CI release matrix hooks. | Public cvar/defaults audit and final acceptance plan. |

## Risks To Watch

| Risk | Impact | Mitigation |
|---|---|---|
| Proof cvars leak into user-facing behavior. | Bots feel brittle and require obscure setup. | Classify cvars early in M1 and keep smoke-only gates out of public docs. |
| Behavior owners fight each other. | Bots oscillate or freeze between route, combat, item, and objective goals. | Centralize arbitration and expose last-owner reason fields. |
| Scenario count grows without coverage quality. | Validation looks broad but misses live regressions. | Require every row to state its behavior contract and promotion reason. |
| AAS quality varies by map. | Bots work on one map and fail elsewhere. | Expand the reference matrix and document known failures explicitly. |
| CPU budgets drift upward as behavior deepens. | Server operators cannot run useful bot counts. | Run fresh source-counter soaks and add budget thresholds after stable observations. |
| Coop behavior blocks progression. | Bots become harmful in campaign play. | Treat anti-blocking, wait/follow, and interaction recovery as first-class coop gates. |
| Chat becomes spammy. | Users disable it immediately. | Keep audience, rate limits, duplicate suppression, and event priority in the core chat contract. |

## Final Acceptance Run

Before marking the bot implementation complete for this FR-04 scope, run and
record:

- Windows local build of changed targets.
- Relevant Linux/macOS CI evidence or release matrix build evidence.
- `.install/` refresh with current binaries, botfiles, and packaged AAS assets.
- Full automated implemented scenario suite.
- Focused live behavior scenario suite from M1 through M6.
- Fresh manual high-bot soak with current source-counter fields.
- Package audit for botfiles and AAS assets.
- User-doc review against current public cvars and defaults.
- Credits/provenance review confirming no unrecorded upstream import or q2proto
  change.

The final closeout should update this file, the Q3A BotLib/AAS port plan, the
strategic roadmap, the credits ledger, and user docs in one coordinated pass.
