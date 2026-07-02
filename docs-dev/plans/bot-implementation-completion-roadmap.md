# Full Bot Implementation Completion Roadmap

Date: 2026-06-29

Status: Living roadmap for post-checklist bot completion.

Primary tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T04`, `FR-04-T05`,
`FR-04-T06`, `FR-04-T07`, `FR-04-T11`, `FR-04-T12`, `FR-04-T14`,
`FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T05`,
and `DV-07-T06`.

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

Snapshot from 2026-07-02 after the stuck recovery obstacle-probe follow-up:

| Area | Current State |
|---|---|
| Original phase checklist | `809/809` phase items complete. |
| Raw markdown checklist | `809/809` rows complete. |
| Scenario catalog | `123` implemented rows, with `0` pending rows. |
| Default pending rows | `0`. |
| Highest bot frame-command smoke mode | `96`. |
| Latest aggregate artifact | `.tmp\bot_scenarios\implemented_stuck_recovery_probe.json`, with `123/123` automated `implemented` rows passing and `0` failed, timeout, error, or pending rows. Release acceptance passed 15/15 with 0 warnings from `.tmp\bot_release\bot_release_acceptance_stuck_recovery_probe.json`. |
| Latest focused artifact | `.tmp\bot_scenarios\stuck_recovery_probe_focus2.json` for obstacle-aware stuck recovery probe selection; `.tmp\bot_scenarios\stuck_recovery_probe_nav_focus.json` for elevator, corner-cutting, FFA roam, and q2dm2 combat/survival navigation regression coverage; `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json` for trace-gated command look-ahead, ordered sequential fallback, close route-point skip telemetry, and route/combat/coop movement regression coverage; `.tmp\bot_scenarios\route_sequential_trace_q2dm2_fix.json` for the q2dm2 survival regression after the command-route harness correction; `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json` for consumed route-target watchdog telemetry, horizontal route-target shift matching, and route/combat/movement regression coverage; `.tmp\bot_scenarios\route_spin_projection_focus.json` for route movement projection, approximate route move-target matching, and route/combat steering regression coverage; `.tmp\bot_scenarios\route_spin_final_after_status.json` for route target anti-spin, close route-point skip telemetry, and route-progress regression coverage; `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json` for live match-result status-surface correction; `.tmp\bot_scenarios\movement_elevator_physical_final.json` for post-cleanup direct elevator mover activation and physical moving-state observation, `.tmp\bot_scenarios\mover_direct_use_regression.json` for mover direct-use regression coverage across elevator, coop interaction retry, coop door/elevator, coop live-loop, and train key-carry rows, `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json` for generic coop door/elevator and coop live-loop wait/board/leave lifecycle gates, `.tmp\bot_scenarios\mover_ride_observation_final.json` for bounded train bridge ride observation, terminal leave-phase preservation, parked-mover diagnostics, and warp-free key-lock continuation, `.tmp\bot_scenarios\mover_ride_state.json` for train mover wait/board/ride/leave lifecycle telemetry, `.tmp\bot_scenarios\interaction_arrival_mover_endpoint.json` for train interaction-arrival mover endpoint discovery/selection telemetry, `.tmp\bot_scenarios\coop_campaign_key_carry_train.json` for train red-key pickup, live `func_train` interaction-goal lookup, natural bridge-start approach, key-side bridge interaction, post-mover arrival projection, direct lock-warp removal, and `trigger_key` lock carry, `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json` for train key-lock/key-path progression selection, `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json` for base2 progression carry across multiple distinct completed interactions, `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json` for base2 post-interaction progression completion/refresh/suppression, `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json` for base2 target-chain/progression consumption, `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json` for base2 target-chain/progression context, `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json` for deeper base2 campaign interaction context, `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json` for the second packaged campaign interaction row, `.tmp\bot_scenarios\min_players_profile_coverage.json` for public `bot_min_players` full first-party profile coverage, and `.tmp\bot_scenarios\movement_reference_gap_audit.json` still accepting both natural crouch and hazard context. |
| Core runtime | Q3A BotLib/AAS loads generated AAS, updates entity snapshots, routes bots, and emits source counters. |
| Bot lifecycle | Server-owned fake clients can be added, removed, auto-filled, cleaned up, and classified in match flow. |
| Profiles | Q3-style WORR botfiles load behavior metadata, roles, movement style, item policy, team policy, aim hints, and chat personality. |
| Behavior status | Many policies have default-off proof gates, status markers, implemented smoke rows, and first-pass live blackboard target-memory telemetry. |
| Release staging | `.install/` staging packages botfiles and validated AAS artifacts, with release workflow hooks in place. |

The important remaining gap is not "can a bot be spawned and proven in a smoke
case?" That is already true. The remaining gap is "can bots make durable,
autonomous, map-backed decisions across normal play without relying on staged
proof cvars?"

The 2026-06-27 stabilization round reduces that gap directly: min-player
autofill now uses profile-backed first-party bots, local paused games still
process autofill changes, generic FFA roam goals no longer churn from current
view direction every frame, pickup decisions can take over from generic roam,
movement-facing follows the selected route unless the bot is actually firing,
and role combat only boosts a valid base attack instead of fabricating one.

The 2026-06-28 movement context gap round extends that into the map matrix:
bots now have implemented scenario coverage for forced jump/crouch/swim
command output, map-backed jump, ladder, walk-off-ledge, elevator,
barrier-jump, rocket-jump, swim, waterjump, door context, and explicit
crouch/teleporter context rows. Live FFA roam yields to item goals, combat facing
no longer overrides route facing unless the bot is firing, role combat defers
behind the base action layer when the bot is weak, underpowered, switching
weapons, or not actually attacking, and scenario runs pin `bot_min_players 0`
unless a row intentionally overrides it.

The 2026-06-29 teleporter entity-route promotion converts mode `95` from an
expected-blocked teleporter gap into `movement_teleporter_entity_route` on
packaged map `train`. Exact Q3A `TRAVEL_TELEPORT` route support is still
reported as unsupported, but runtime nav now selects a touch-capable teleporter
entity, persists it as a position goal, and builds a first-reachability route
toward that entity without bot-nav route failures.

The 2026-06-28 hazard context gap round added mode `96` on packaged map
`base2`, proving live runtime interaction context while explicitly recording
the missing staged hazard content.

The 2026-06-30 movement reference gap audit made the remaining M6 gaps
machine-checkable. q2aas reference validation now reports `crouch_reference`
from generated `TRAVEL_CROUCH` counts beside the existing `slime_reference` and
`lava_reference` gates. Same-day follow-up slices promoted optional `q2dm7` as
the first staged slime reference, optional official campaign map `fact2` as
the lava/runtime hazard reference, and WORR-authored `worr_crouch_ref` as the
required natural-crouch reference. With those maps staged locally, q2aas
validation passes `crouch_reference`, `slime_reference`, `lava_reference`, and
`runtime_hazard_entity_reference`; mode `92` is now accepted as
`movement_crouch_route` and mode `96` remains accepted as
`movement_hazard_context`.

The 2026-07-01 min-player profile coverage gate promoted the public autofill
regression proof into a first-class implemented scenario. `bot_min_players`
now has focused evidence that autofill reaches the five first-party profiles
from `botfiles/bots.txt`, including `smoke`, then trims and disables cleanly.
Release acceptance now treats `min_players_profile_coverage` as required
scenario evidence, discoverable from the focused supplemental report.

The 2026-07-01 train campaign key-carry bridge round extends the key-carry
proof through a real key-side `func_train` interaction before the final lock
leg. At that stage the scenario required bridge route requests, a visible
bridge-start proof warp, wait/use command telemetry,
`last_key_carry_bridge_kind=4`, and `last_key_carry_bridge_travel_type=11`.
Later same-day bridge-arrival work removes the direct lock-side warp from the
focused row, and bridge-approach work removes the bridge-start proof warp. The
remaining M4/M6 target is broader off-mesh mover-graph generalization beyond
the focused train proof.

The follow-up 2026-07-01 interaction-goal round removes the hardcoded
bridge-start dependency when the map can supply a live interaction entity. The
same `coop_campaign_key_carry_train` row now requires
`interaction_goal_requests=1`, `interaction_goal_resolved=1`,
`last_interaction_goal_kind=4`, `last_interaction_goal_entity > 0`, and
`last_interaction_goal_area > 0`; the focused run resolved three train
candidates and selected entity `60` in AAS area `2338`. The bridge-start warp
was still present at that stage, but later bridge-approach work replaced it
with a natural route endpoint latch.

The follow-up 2026-07-01 bridge-arrival round extends that row again by asking
bot nav for a routeable post-mover arrival goal derived from the same
`func_train` source and the final lock destination. The row now requires
`interaction_arrival_goal_requests=1`, `interaction_arrival_goal_resolved=1`,
`last_interaction_arrival_goal_kind=4`,
`last_interaction_arrival_goal_area > 0`,
`last_interaction_arrival_goal_destination_distance_sq > 0`,
a temporary bridge-arrival warp counter, and `key_carry_lock_warps=0`, while
a one-shot lock trigger activation preserves the `trigger_key` key-path
selection and required-item telemetry. The later bridge-arrival-route round
supersedes that temporary warp requirement.

The follow-up 2026-07-01 bridge-approach round removes the remaining
bridge-start proof warp. The key-carry bridge phase now routes naturally to
the live train bridge start, accepts a matched one-point route endpoint as the
approach latch, clears only the proof bot's stale generic interaction slot,
then activates the train interaction once. The row now requires
`key_carry_bridge_approach_ready=1`, `key_carry_bridge_warps=0`,
`key_carry_bridge_interactions=1`, and kept the post-mover arrival proof warp
explicit until the next arrival-route slice.

The follow-up 2026-07-01 bridge-arrival-route round removes that remaining
post-mover proof warp from the focused train key-carry row. The smoke slot now
stores the routeable bridge-arrival projection and holds it as a normal
position goal until the bot reaches it before advancing to the final lock leg.
The row now requires `key_carry_bridge_arrival_route_requests >= 1`,
`key_carry_bridge_arrival_reached >= 1`,
`key_carry_bridge_arrival_warps=0`, and `key_carry_lock_warps=0`. Focused
validation passed from `.tmp\bot_scenarios\20260701T145703Z`; the fresh full
implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_bridge_arrival_route.json`, and release
acceptance passed 15/15.

The follow-up 2026-07-01 interaction-arrival route-state round lifts the
arrival-leg request/reach bookkeeping into `bot_nav`. `BotNavRouteRequest` can
now tag a position goal as the arrival side of an interaction entity, and route
status reports `interaction_arrival_route_requests`,
`interaction_arrival_route_assignments`, `interaction_arrival_route_reached`,
and `last_interaction_arrival_route_*` metadata. Focused validation from
`.tmp\bot_scenarios\20260701T162117Z` passed with
`interaction_arrival_route_assignments=2`,
`interaction_arrival_route_reached=1`,
`key_carry_bridge_arrival_reached=1`, and
`key_carry_lock_route_requests=42`.

The follow-up 2026-07-01 chat smoke queue-determinism round fixes the
post-arrival full-suite blockers in the M5 chat/personality lane. The
frame-command smoke runner now counts pending queued bot additions before
issuing another `SV_BotAdd`, preventing duplicate late-spawned fifth bots in
the four-bot chat policy rows. The live match-result gate now treats
`last_match_result_outcome*` as order-dependent final-bot telemetry while still
requiring both win and loss outcome counters. Focused validation passed 4/4
from `.tmp\bot_scenarios\chat_smoke_queue_determinism.json`; the fresh full
implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_chat_smoke_queue_determinism.json`, and
release acceptance passed 15/15.

The follow-up 2026-07-01 interaction-arrival mover-endpoint round teaches
`bot_nav` to inspect mover endpoint candidates while resolving the
post-interaction arrival side of a route. Endpoint candidates are collected
from mover destinations and target links, scored before the destination-offset
sweep, and exposed through `q3a_bot_frame_command_status`. The train key-carry
row now requires endpoint checks, candidates, selections, a positive endpoint
entity/area, interaction-arrival route reach evidence, and zero bridge-arrival
or lock warps. Focused validation passed from
`.tmp\bot_scenarios\interaction_arrival_mover_endpoint.json` with
`interaction_arrival_mover_endpoint_checks=3`,
`interaction_arrival_mover_endpoint_candidates=3`,
`interaction_arrival_mover_endpoint_selections=2`, and
`last_interaction_arrival_mover_endpoint_entity=60`; the final selected
arrival source remained a destination offset because that routeable point was
closer to the lock-side target than the exposed mover endpoint. The fresh full
implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_interaction_arrival_mover_endpoint.json`,
and release acceptance passed 15/15.

The follow-up 2026-07-01 interaction mover ride-state round adds a reusable
wait/board/ride/leave lifecycle on top of the train mover bridge proof.
`BotNav_RecordMoverRideState` now records mover-like interaction lifecycle
checks, phase counters, invalid skips, last mover entity/kind/action/client,
last mover `MoveState`, ground entity, position, and distance. The train
key-carry row records wait when the bridge interaction is observed, board when
the post-mover arrival is resolved, ride while the interaction-arrival route is
requested, and leave when the arrival route is reached. Focused validation
passed from `.tmp\bot_scenarios\mover_ride_state.json` with
`interaction_mover_ride_checks=5`,
`interaction_mover_ride_wait_states=1`,
`interaction_mover_ride_board_states=1`,
`interaction_mover_ride_ride_states=2`,
`interaction_mover_ride_leave_states=1`, and
`last_interaction_mover_ride_phase=4`; the fresh full implemented suite passed
123/123 from `.tmp\bot_scenarios\implemented_after_mover_ride_state.json`.

The follow-up 2026-07-01 interaction mover ride-observation round adds a
bounded post-activation observation window for the same train bridge. The
proof bot now samples the recorded bridge entity for 200 ms after activation,
records the parked mover state, then resumes the normal post-mover arrival and
lock route. `bot_nav` also preserves a terminal leave phase for the same
mover/client so later recovery samples cannot overwrite completed lifecycle
evidence. Focused validation passed from
`.tmp\bot_scenarios\mover_ride_observation_final.json` with
`key_carry_bridge_ride_observation_requests=1`,
`key_carry_bridge_ride_observation_frames=9`,
`key_carry_bridge_ride_observation_completed=1`,
`last_key_carry_bridge_ride_observation_elapsed_ms=200`,
`interaction_mover_ride_checks=20`,
`last_interaction_mover_ride_phase=4`,
`nav_interaction_progression_key_path_candidates=1`,
`nav_interaction_progression_key_path_selections=1`,
`last_nav_interaction_progression_key_path_required_item=70`, and
`key_carry_lock_warps=0`. The fresh full implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_mover_ride_observation.json`, and
release acceptance passed 15/15 from
`.tmp\bot_release\bot_release_acceptance_mover_ride_observation.json`.
Moving and grounded-on-mover counters remain
diagnostic on this row because the selected train bridge is parked during the
short proof.

The follow-up 2026-07-01 generic mover lifecycle round broadens mover
ride-state evidence beyond the train key-carry proof. The generic recovery
sampler now records Wait and Board separately for WaitUse mover interactions
and records terminal Leave when the mover interaction window completes. The
standalone `coop_door_elevator` and `coop_live_loop` rows now hard-gate
wait/board/leave lifecycle telemetry with `invalid_skips=0`. Focused
validation from `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json`
passed with `coop_door_elevator` and `coop_live_loop` each recording
`checks=217`, `wait=104`, `board=104`, `leave=9`, `last_phase=4`,
`last_entity=18`, and `last_kind=3`; `movement_elevator_route` records
wait/board diagnostics without a leave gate. The full suite passed 123/123
from `.tmp\bot_scenarios\implemented_after_generic_mover_lifecycle.json`, and
release acceptance passed 15/15 from
`.tmp\bot_release\bot_release_acceptance_generic_mover_lifecycle.json`.

The follow-up 2026-07-01 physical elevator mover activation round promotes
`movement_elevator_route` from route-only elevator reachability into a hard
gate for direct mover activation and observed physical movement. Recovery
commands that already request `use` can now directly invoke platform, train,
or generic mover `use` callbacks with a per-client/entity cooldown; raw mode
`12` receives a proof slot, a 24-frame settle window, direct-use telemetry, and
travel-type elevator observation counters. Focused validation passed from
`.tmp\bot_scenarios\movement_elevator_physical.json`, the post-cleanup focused
rerun passed from `.tmp\bot_scenarios\movement_elevator_physical_final.json`,
and the five-row mover regression passed from
`.tmp\bot_scenarios\mover_direct_use_regression.json`.
The row now hard-gates `interaction_direct_use_activations >= 1`,
`travel_type_elevator_activation_requests >= 1`,
`travel_type_elevator_ride_observation_moving >= 1`,
`travel_type_elevator_ride_observation_completed >= 1`, and
`interaction_mover_ride_moving_states >= 1`. The fresh full implemented suite
passed 123/123 from
`.tmp\bot_scenarios\implemented_after_physical_elevator_mover.json`.
Release acceptance passed 15/15 from
`.tmp\bot_release\bot_release_acceptance_physical_elevator_mover_final.json`.
Grounded-on-mover samples remain diagnostic until a map/interactions gives a
reliable platform-riding sample.

The follow-up 2026-07-01 route target anti-spin round fixes local route-node
churn seen as bots spinning in place. Command steering now starts from the
authoritative stabilized `route.moveTarget`, matches it into the returned route
points, skips route points inside a 24-unit consumed radius, and falls back to
the route goal when all local points are consumed. Stuck detection now measures
both final-goal progress and current local route-target progress so valid corner
or switchback movement does not trigger false recovery. The
`trace_checked_corner_cutting` row now hard-gates
`lookahead_close_point_skips >= 1`, and the final focused route batch passed
8/8 from `.tmp\bot_scenarios\route_spin_final_after_status.json` with zero
route failures across the batch. The full implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_route_spin_status_fix.json`, and release
acceptance passed 15/15 from
`.tmp\bot_release\bot_release_acceptance_route_spin_fix.json`.

The follow-up 2026-07-01 route movement projection round fixes the remaining
spin/jam pattern where route selection was correct but physical movement still
followed the bot's combat view angles. Frame commands now compute route-facing
once, aim at combat targets only when attacking, and project the route yaw into
view-relative `forwardMove` plus `sideMove`, allowing strafing or backpedaling
along the route while looking elsewhere. Route move-target matching also accepts
bounded endpoint offsets so BotLib edge/z variation does not break look-ahead
continuity. Focused validation passed 10/10 from
`.tmp\bot_scenarios\route_spin_projection_focus.json` with zero route failures
across the batch; the full implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_route_projection_fix.json`, and release
acceptance passed 15/15 from
`.tmp\bot_release\bot_release_acceptance_route_projection_fix.json`.

The follow-up 2026-07-02 consumed route target watchdog round fixes another
spin-in-place cause in route progress accounting. The watchdog no longer treats
remaining inside the same already-reached local `route.moveTarget` radius as
fresh progress every frame, and route-target shift detection now uses horizontal
distance to match the watchdog's progress model. Focused validation passed
10/10 from `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json`; the
full implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_consumed_target_watchdog.json`, and
release acceptance passed 15/15 with 0 warnings from
`.tmp\bot_release\bot_release_acceptance_consumed_target_watchdog.json`.

The follow-up 2026-07-02 route command trace/sequential look-ahead round fixes
the remaining command-layer shortcut issue. Ordinary command look-ahead and
goal fallback targets now reuse nav-layer hull trace safety before promotion,
while already-consumed local route nodes advance only to the first ordered
non-close future route point. A blocked sequential trace stops further
promotion instead of letting a farther point shortcut through map geometry.
Focused validation passed 12/12 from
`.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`; the q2dm2
survival row passed from
`.tmp\bot_scenarios\route_sequential_trace_q2dm2_fix.json`; the full
implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_after_route_sequential_trace_lookahead_fix.json`;
and release acceptance passed 15/15 with 0 warnings from
`.tmp\bot_release\bot_release_acceptance_route_sequential_trace_lookahead.txt`.

The follow-up 2026-07-02 stuck recovery obstacle-probe round fixes the
remaining wall-sticking pattern where the route was valid but local recovery
kept emitting a fixed view-relative back/strafe command. `bot_nav` now probes
short player-hull escape candidates when stuck recovery activates, stores the
selected world-space escape direction for the recovery window, and lets
`bot_brain` project that direction back into the current usercmd view. The new
`q3a_bot_nav_policy_status` fields report probe checks, selected probe moves,
blocks, fallbacks, last probe candidate/fraction, and last projected movement.
Focused `recover_from_stall` validation passed from
`.tmp\bot_scenarios\stuck_recovery_probe_focus2.json`; the navigation slice
passed 4/4 from `.tmp\bot_scenarios\stuck_recovery_probe_nav_focus.json`; the
full implemented suite passed 123/123 from
`.tmp\bot_scenarios\implemented_stuck_recovery_probe.json` with 69 selected
probe moves, zero blocked probes, and one no-clear fallback; release acceptance
passed 15/15 with 0 warnings from
`.tmp\bot_release\bot_release_acceptance_stuck_recovery_probe.json`.

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
| M2 | Combat And Inventory Depth | `FR-04-T03`, `FR-04-T15` | In progress; target-memory, weapon-scoring, aim/fire policy, ammo-pressure, survival-inventory, survival-health/armor routing, threat-retreat avoidance, compact combat regression, q2dm2/q2dm8 map regressions, full-suite smoke contract reconciliation, and weak/underpowered role-combat deferral done | Bots fight with sensible weapons, aim, ammo, inventory, and survival decisions. |
| M3 | Multiplayer Mode Intelligence | `FR-04-T04`, `FR-04-T06`, `FR-04-T15` | In progress; CTF objective live-loop and transition proofs, TDM role spawn-stability, FFA live-pacing, and Duel live-pacing proofs done | Bots play FFA, Duel, TDM, and CTF objectives coherently. |
| M4 | Coop And Campaign Behavior | `FR-04-T04`, `FR-04-T05`, `FR-04-T15` | In progress; coop live-loop, target/resource share, campaign interaction matrix/depth/progression rows, post-interaction refresh, progression-carry, train keyed-path, train key-carry bridge-approach proofs, and physical mover direct-use regression done | Bots help rather than block campaign and coop progression. |
| M5 | Chat And Personality | `FR-04-T07`, `FR-04-T15` | In progress; live spawn, route-ready, enemy-sighted, low-health, item-taken, objective-changed, flag-state, blocked, item-denied, match-result events, outcome-aware match-result phrases, global cooldown, duplicate suppression, and four-variant phrase libraries done | Profile personality influences safe live communication and behavior flavor. |
| M6 | Map, AAS, And Movement Coverage | `FR-04-T05`, `FR-04-T11`, `FR-04-T14`, `FR-04-T16` | In progress; coop campaign map/depth/progression/keyed-path/key-carry bridge-approach rows plus forced movement, map-backed jump/ladder/ledge/elevator/barrier-jump/rocket-jump/swim/waterjump rows, physical elevator mover activation/moving-state proof, route target anti-spin/look-ahead skip hardening, local route-target stuck progress accounting, approximate move-target matching, route movement projection while aiming off-route, trace-gated command look-ahead with ordered sequential fallback, obstacle-aware stuck recovery probes, door context, accepted natural crouch route, accepted teleporter entity-route fallback, and accepted `fact2` hazard context done | Bots have reliable navigation evidence across representative map families. |
| M7 | Performance, Soak, And Reliability | `FR-04-T02`, `FR-04-T14`, `FR-04-T16`, `DV-03-T05` | In progress; fresh source-counter soak, strict source-counter budget lane, and repeated-run variance tooling done | Performance budgets and long-run behavior are stable enough to ship. |
| M8 | Productization And Release Readiness | `FR-04-T07`, `FR-04-T16`, `DV-07-T06` | In progress; public surface/default docs gates, playtest tooling, release acceptance runner, and perf tooling checks done | Operator docs, defaults, packaging, and release notes are complete. |

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
  parsing, perf analysis, manual high-bot soak policy, and 114 implemented
  historical aggregate rows plus the focused 115th profile-coverage row.

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

- Existing modes `20` through `96` are implemented in the catalog.
- `behavior_arbitration` proves route, item, and combat candidates plus combat
  ownership without setting individual proof cvars.
- The 2026-06-27 stabilization rerun proves arbitration no longer lets visible
  role-combat targets steal movement from route, item, retreat, or weapon-switch
  decisions.
- No smoke-only cvar is required for the M1 behavior arbitration proof.

## M2: Combat And Inventory Depth

Status: In progress; sustained enemy target memory/decay, weapon-scoring
arsenal, aim/fire policy depth proof, ammo-pressure pickup proof, carried
survival inventory-use proof, survival health-route proof, survival
armor-route proof, low-health threat-retreat avoidance proof, compact
combat/survival regression proofs on `mm-rage`, `q2dm2`, and `q2dm8`, the expanded
full-suite smoke contract reconciliation, and role-combat deferral behind the
base action layer are
implemented.

Goal: make bots fight competently in normal Q2/WORR situations.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Enemy selection loop | Promote visible/shootable enemy facts into sustained target selection with target memory and decay. | Mode `64` `target_memory_decay` proves retained unseen target memory, a `1000` ms decay window, blackboard memory age/window telemetry, and clear-after-decay status. |
| 2 | Weapon scoring | Expand weapon choice by distance, ammo, splash risk, enemy health/armor estimate, water/hazard context, and inventory state. | Mode `65` `weapon_scoring_arsenal` proves carried arsenal scanning, insufficient-ammo rejection, splash-risk pressure, close-range super-shotgun selection, enemy-estimate finisher scoring, and switch completion. |
| 3 | Aim and fire policy | Tune aim profile, reaction timing, projectile leading, splash caution, and line-of-fire checks. | Mode `66` `aim_fire_policy_depth` proves reaction-delay withholding, aim-settle withholding, burst-cooldown pacing, live-aim policy blocks, rocket projectile lead, and eventual attack-button application. |
| 4 | Ammo and pickup pressure | Connect low-ammo and preferred-weapon state to item/route priorities. | Mode `67` `ammo_pressure_pickup` proves ammo focus, low-shell staged routeable shell pickup selection, ammo candidates/seek decisions, ammo goal assignments, and zero route failures. |
| 5 | Inventory usage | Promote carried inventory and powerup decisions into live use policy with safety checks. | Mode `68` `survival_inventory_use` proves low-health/no-armor survival pressure can select carried power armor, accept a pending inventory intent, build a validated command request, and dispatch `use_index_only`; other special inventory classes remain covered by existing policy counters until broader regression rows land. |
| 6 | Survival behavior | Expand health/armor retreat, threat avoidance, and emergency item routing. | Mode `69` `survival_health_route` proves low-health health routing, mode `70` `survival_armor_route` proves full-health/no-armor armor routing, and mode `72` `threat_retreat_avoidance` proves a low-health bot can source a live threat, activate a short retreat route, suppress attack during retreat, and re-engage afterward; `threat_retreat_avoidance_q2dm8` repeats the threat-retreat contract on the `q2dm8` reference DM map. |
| 7 | Combat regression set | Build compact combat scenarios that are not single-script proofs. | Mode `71` `combat_survival_regression` proves visible/shootable enemy pressure remains visible to blackboard/action telemetry while low-health health routing, withheld-fire policy, item ownership, and recovery ownership can safely win under survival pressure; `combat_survival_regression_q2dm2` and `combat_survival_regression_q2dm8` repeat the same contract on the `q2dm2` and `q2dm8` reference DM maps. |

Latest validation note:

- The 2026-06-27 focused behavior stabilization run passed
  `combat_survival_regression`, `threat_retreat_avoidance`,
  `weapon_scoring_arsenal`, `aim_fire_policy_depth`, and
  `behavior_arbitration` from
  `.tmp\bot_scenarios\bot-profile-roam-state-fixes-rerun3`. The compatible
  role-combat rerun passed `behavior_policy_umbrella`, `team_role_combat`,
  `team_role_combat_avoidance`, `ctf_role_combat`, `ffa_role_combat`,
  `ffa_spawn_camp_combat_avoidance`, and `tdm_role_spawn_stability` from
  `.tmp\bot_scenarios\bot-role-combat-compat-check3`.
- Focused mode `71` validation passed from
  `.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z`.
  The row recorded `frames=121`, `commands=121`, `route_commands=121`,
  `route_failures=0`, `item_goal_assignments=7`,
  `last_item_goal_area=224`, `combat_enemy_visible=120`,
  `combat_enemy_shootable=119`, `combat_withheld_fire=35`,
  `behavior_arbitration_item_owners=3`, and
  `behavior_arbitration_recovery_owners=40`.
- Focused second-map validation passed from
  `.tmp\bot_scenarios\combat_survival_regression_q2dm2\20260622T194547Z`.
  The row recorded `map_name=q2dm2`, begin-marker `map=q2dm2`,
  `frames=121`, `commands=121`, `route_failures=0`,
  `item_goal_assignments=5`, `stuck_detections=9`,
  `recovery_command_uses=54`, visible/shootable enemy facts, withheld-fire
  evidence, and item/recovery arbitration owners.
- Focused q2dm8 map-matrix validation passed from
  `.tmp\bot_scenarios\20260622T204956Z`. The two promoted rows recorded
  begin-marker `map=q2dm8`, route-clean mode `71` combat/survival evidence
  with `item_goal_assignments=11`, `recovery_command_uses=51`, and
  visible/shootable enemy facts, plus mode `72` threat-retreat evidence with
  attack suppression and combat ownership.
- Focused mode `72` validation passed from
  `.tmp\bot_scenarios\20260622T202608Z`, proving low-health threat selection,
  one retreat activation, route requests, attack suppression, and post-retreat
  re-engagement without the older smoke combat cvar.
- The bot chat phrase-library implemented run added mode `82`
  `bot_chat_phrase_library`, proving four initial and four reply phrase
  variants with focused validation from `.tmp\bot_scenarios\20260623T020850Z`
  and a full 90-row implemented suite from
  `.tmp\bot_scenarios\20260623T021355Z`.
- The bot chat duplicate-suppression implemented run added mode `83`
  `bot_chat_duplicate_suppression`, proving a 5000 ms duplicate reply window
  suppresses repeated route-ready reply/live events while preserving successful
  dispatch telemetry. Focused validation passed from
  `.tmp\bot_scenarios\20260623T023211Z`, and the full 91-row implemented suite
  passed from `.tmp\bot_scenarios\20260623T023230Z`.
- The bot chat live low-health implemented run added mode `84`
  `bot_chat_live_low_health`, proving a real low-health survival route can
  emit event id `9` / `low_health` through the live chat pipeline while
  recording `reply_chat_low_health=1`, `live_chat_low_health=1`, and
  `last_live_chat_event_name=low_health`. Focused validation passed from
  `.tmp\bot_scenarios\20260623T025752Z`, and the full 92-row implemented suite
  passed from `.tmp\bot_scenarios\20260623T025801Z`.
- The bot chat live item-taken implemented run added mode `85`
  `bot_chat_live_item_taken`, proving real health/armor pickup observations can
  emit event id `4` / `item_taken` through the live chat pipeline while
  recording `reply_chat_item_taken=1`, `live_chat_item_taken=1`, and
  `last_live_chat_event_name=item_taken`. Focused validation passed from
  `.tmp\bot_scenarios\20260623T051126Z`, and the full 93-row implemented suite
  passed from `.tmp\bot_scenarios\20260623T051133Z`.
- The bot chat live objective-changed implemented run added mode `86`
  `bot_chat_live_objective_changed`, proving real CTF pickup, death-drop, and
  dropped-flag return transitions can emit event id `7` / `objective_changed`
  through the live chat pipeline while recording `reply_chat_objective_changed=4`,
  `live_chat_objective_changed=4`, `live_chat_event_taxonomy=11`, and zero
  dispatch, reply, or live failures. Focused validation passed from
  `.tmp\bot_scenarios\20260626T140601Z`, and the full 94-row implemented suite
  passed from `.tmp\bot_scenarios\20260626T140621Z`.
- The bot chat live flag-state implemented run added mode `87`
  `bot_chat_live_flag_state`, proving real CTF pickup, death-drop, and
  dropped-flag return observations can emit event id `8` / `flag_state`
  through the live chat pipeline while recording `reply_chat_flag_state=4`,
  `live_chat_flag_state=4`, `live_chat_event_taxonomy=11`, and zero dispatch,
  reply, or live failures. Focused validation passed from
  `.tmp\bot_scenarios\20260626Tflagstate3\20260626T144136Z`, and the full
  95-row implemented suite passed from
  `.tmp\bot_scenarios\20260626Timplemented-flagstate-fixed\20260626T144511Z`.
- The bot chat live blocked implemented run added mode `88`
  `bot_chat_live_blocked`, proving a blocked rocketjump travel-type route
  failure can emit event id `10` / `blocked` through the live chat pipeline
  while recording `reply_chat_blocked=1`, `live_chat_blocked=1`,
  `live_chat_event_taxonomy=11`, and zero dispatch, reply, or live failures.
  Focused validation passed from
  `.tmp\bot_scenarios\20260626Tblocked-fixed\20260626T151437Z`, and the full
  96-row implemented suite passed from
  `.tmp\bot_scenarios\20260626Timplemented-blocked\20260626T151446Z`.
- The bot chat live item-denied implemented run added mode `89`
  `bot_chat_live_item_denied`, proving TDM deny-enemy resource policy pressure
  can emit event id `5` / `item_denied` through the live chat pipeline while
  recording `reply_chat_item_denied=4`, `live_chat_item_denied=4`,
  `team_resource_denial_policy_denies=112`, `live_chat_event_taxonomy=11`, and
  zero dispatch, reply, or live failures. Focused validation passed from
  `.tmp\bot_scenarios\20260626Titem-denied\20260626T154429Z`, and the full
  97-row implemented suite passed from
  `.tmp\bot_scenarios\20260626Timplemented-item-denied-json-file\20260626T154954Z`.
- The bot chat live match-result implemented run added mode `90`
  `bot_chat_live_match_result`, proving the native intermission/match-result
  path can emit event id `11` / `victory_defeat` through the live chat pipeline
  while recording `reply_chat_match_result=4`, `live_chat_match_result=4`,
  `intermission_bots=4`, `pm_freeze_bots=4`, `live_chat_event_taxonomy=11`,
  and zero dispatch, reply, or live failures. Focused validation passed from
  `.tmp\bot_scenarios\20260626Tmatch-result\20260626T182046Z`, and the full
  98-row implemented suite passed from
  `.tmp\bot_scenarios\20260626Timplemented-match-result\20260626T182111Z`.
- The match-result outcome-chat polish round keeps mode `90`
  `bot_chat_live_match_result` in the native intermission/live-chat path, adds
  explicit win/loss/tie/abort/unknown phrase classification, and hard-gates the
  scenario on outcome evidence. Focused validation passed from
  `.tmp\bot_scenarios\bot_chat_match_result_outcome.json` with
  `reply_chat_match_result=4`, `reply_chat_match_result_win=2`,
  `reply_chat_match_result_loss=2`, zero unknown/tie/abort classifications,
  `last_reply_chat_phrase=12223`, and
  `last_match_result_outcome_name=loss`. Implementation log:
  `docs-dev/q3a-botlib-match-result-outcome-chat-2026-07-01.md`.
- The later bot chat live enemy-sighted implemented run preserved the full
  `implemented` catalog from `.tmp\bot_scenarios\20260623T013843Z`,
  reporting `89` passed rows,
  `0` failed rows, `0` timeouts, `0` errors, and `0` pending rows.
  The q2dm8 combat/survival marker contract was also hardened so the row no
  longer assumes final tail metadata must remain on the health utility after
  later pickup scans advance the action-status tail.

Validation gates:

- Combat scenarios pass on at least two reference maps.
- Friendly-fire suppression still wins over attack decisions in team modes.
- Bots do not rely on direct teleport/staged target setup for all combat proof.
- Manual playtest confirms bots are beatable but not inert.

## M3: Multiplayer Mode Intelligence

Status: In progress; CTF objective live-loop promotion, CTF pickup/drop/return
transition proof, TDM role spawn-stability proof, FFA live-pacing proof, and
Duel live-pacing proof done.

Goal: make bots understand the mode they are playing, not just move and fight
inside it.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | FFA loop | Combine roam, item, anti-camp, combat, and spawn-pressure decisions. | Mode `74` `ffa_live_pacing` proves route ownership, item-role scoring, role combat, and spawn-camp route/combat pressure cooperate in one four-bot FFA run; broader live-map pacing still needs play-depth validation. |
| 2 | Duel loop | Add duel-specific pacing, item denial, spawn pressure, and spectator/queue behavior. | Mode `75` `duel_live_pacing` proves Duel match-policy selection, deny-enemy item scoring, route ownership, role combat, and spawn-pressure route/combat evidence cooperate in one two-bot Duel run; broader Duel play-depth validation still needs live-server observation. |
| 3 | TDM role loop | Promote attacker/defender/support/roam role selection into ongoing team behavior. | Mode `73` `tdm_role_spawn_stability` proves TDM role-route and role-combat owners remain active across a forced same-map restart; broader live role distribution still needs play-depth validation. |
| 4 | CTF objective loop | Promote dropped flag, carrier support, base return, defense, offense, and item role policies into live objective choice. | Mode `40` `ctf_objective_route` now proves base-return, carrier-support, and dropped-flag selections in one CTF run, route-clean objective commands, and objective-owner arbitration. |
| 5 | Objective transition handoff | Add explicit handoff around pickup, death-drop, and dropped-flag return transitions. | Mode `76` `ctf_objective_transitions` proves real CTF pickup, death-drop, and dropped-flag return hooks feed objective counters before the combined objective route owner commands the flag loop. |
| 6 | Match ecosystem audits | Keep map vote, MyMap, nextmap, warmup, scoreboard, intermission, tournament, and admin boundaries bot-safe. | Existing match-flow smoke rows still pass after live behavior changes. |

Validation gates:

- One representative scenario per mode proves live policy cooperation.
- CTF scenarios verify flag pickup, dropped flag, carrier support, base return,
  and scoring/return gameplay hooks.
- Team modes avoid obvious team sabotage such as friendly fire, objective
  abandonment, and resource starvation.

Latest validation note:

- The 2026-06-27 focused live-mode rerun passed `ffa_roam_route`,
  `ffa_live_pacing`, and `duel_live_pacing` from
  `.tmp\bot_scenarios\bot-profile-roam-state-fixes-rerun3`. The updated
  contract preserves route/item/anti-camp evidence while treating visible
  role-combat targets as deferrals unless the base action layer is actually
  attacking.
- Focused CTF objective-loop validation passed from
  `.tmp\bot_scenarios\20260622T210329Z`. The row recorded `frames=246`,
  `commands=246`, `route_commands=246`, `route_failures=0`,
  `ctf_objective_route_base_return_selections=106`,
  `ctf_objective_route_carrier_support_selections=53`,
  `ctf_objective_route_dropped_flag_selections=53`,
  `ctf_objective_route_route_commands=212`, and
  `behavior_arbitration_objective_owners=192`.
- Focused TDM role spawn-stability validation passed from
  `.tmp\bot_scenarios\20260622T212431Z`. The row recorded `frames=246`,
  `commands=245`, `route_commands=245`, `route_failures=0`, `cycles=2`,
  `map_changes=1`, `final_count=0`, `team_role_route_activations=245`,
  `team_role_route_route_requests=245`,
  `team_role_combat_target_selections=245`,
  `team_role_combat_attack_decisions=245`, and `action_attack_buttons=245`.
- Focused FFA live-pacing validation passed from
  `.tmp\bot_scenarios\20260622T214927Z`. The row recorded `frames=187`,
  `commands=187`, `route_commands=187`, `route_failures=0`,
  `ffa_roam_route_activations=187`,
  `ffa_spawn_camp_avoidance_source_selections=68`,
  `ffa_role_combat_attack_decisions=186`,
  `ffa_spawn_camp_combat_avoidance_source_blocks=186`,
  `ffa_item_role_evaluations=3938`,
  `ffa_item_role_score_boosts=3938`, and
  `ffa_item_role_selected_goals=82`.
- Focused Duel live-pacing validation passed from
  `.tmp\bot_scenarios\20260622T222142Z`. The row recorded `frames=121`,
  `commands=121`, `route_commands=121`, `route_failures=0`,
  Duel match-mode evidence through `last_team_objective_match_mode=5` and
  `last_team_objective_match_mode_name=duel`, deny-enemy item-role selection,
  route/combat status in Duel mode, and spawn-source combat-veto evidence.
- Focused CTF objective transition validation passed from
  `.tmp\bot_scenarios\20260622T230509Z`. The row recorded `frames=246`,
  `commands=246`, `route_commands=246`, `route_failures=0`,
  `team_objective_flag_pickups=2`, `team_objective_flag_drops=1`,
  `team_objective_flag_returns=1`,
  `ctf_objective_route_assignments=212`,
  `ctf_objective_route_base_return_candidates=106`,
  `ctf_objective_route_dropped_flag_candidates=212`,
  `ctf_objective_route_route_commands=212`, and
  `ctf_objective_route_invalid_skips=0`.
- The follow-up bot chat live enemy-sighted implemented run passed the full
  `implemented` catalog from `.tmp\bot_scenarios\20260623T013843Z`,
  reporting `89` passed rows,
  `0` failed rows, `0` timeouts, `0` errors, and `0` pending rows.

## M4: Coop And Campaign Behavior

Status: In progress; coop live-loop and target/resource share aggregate proofs done.

Goal: make bots useful in coop and campaign maps instead of treating them like
arena deathmatch with monsters.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Follow/wait baseline | Promote FollowLeader, WaitForLeader, LeadAdvance, and progress-wait owners into live coop loop. | Mode `77` `coop_live_loop` proves leader-route activation and per-bot WaitForLeader commands can coexist in one two-bot coop run. |
| 2 | Interaction ownership | Expand door/elevator/use retry logic to common map entities and teammate hold behavior. | Mode `77` proves interaction retry commands, door/elevator source ownership, and teammate hold commands can compose in the same run. |
| 3 | Monster target sharing | Promote blackboard target-sharing from smoke proof to live combat support. | Mode `78` `coop_share_loop` proves a support-policy bot can adopt a teammate's hostile non-client target while other coop policy remains active. |
| 4 | Resource sharing | Make health/ammo/powerup choices coop-aware, especially around low-health humans and scarce pickups. | Mode `78` proves reserve-for-teammate resource policy and item scoring deferrals compose with target sharing in the same two-bot coop run. |
| 5 | Anti-blocking | Expand close-leader and choke-point anti-blocking movement. | Mode `77` proves close-policy anti-blocking commands can compose with wait/route/mover behavior; broader choke-point map coverage remains pending. |
| 6 | Trigger/key/objective support | Add map-backed evidence for trigger, key, button, and objective progression. | Reference campaign maps have explicit pass/fail diagnostics. |

Latest validation note:

- Focused coop live-loop validation passed from
  `.tmp\bot_scenarios\20260622T234315Z`. The row recorded `frames=121`,
  `commands=121`, `route_commands=61`, `route_failures=0`,
  `coop_leader_route_activations=60`, `coop_progress_wait_commands=60`,
  `coop_anti_blocking_policy_close=60`, `coop_anti_blocking_commands=60`,
  `coop_interaction_retry_commands=36`,
  `coop_door_elevator_source_commands=36`, and
  `coop_door_elevator_hold_commands=60`.
- Focused coop share-loop validation passed from
  `.tmp\bot_scenarios\20260623T001149Z`. The row recorded `frames=121`,
  `commands=121`, `route_commands=121`, `route_failures=0`,
  `team_objective_coop_policy_resource_share=129`,
  `team_objective_resource_policy_reserve=56`,
  `item_reserved_deferrals=62`, and `coop_target_share_adoptions=1`.
- Focused coop campaign interaction matrix validation passed from
  `.tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final\20260626T185108Z`.
  The row recorded `mode=91`, `map=base1`, `coop_live_loop=1`, `frames=121`,
  `commands=121`, `route_commands=61`, `route_failures=0`,
  `coop_leader_route_activations=60`, `coop_progress_wait_commands=60`,
  `coop_interaction_retry_commands=3`,
  `coop_door_elevator_source_commands=3`,
  `coop_door_elevator_hold_commands=60`,
  `last_coop_door_elevator_entity=360`,
  `nav_interaction_activations=3`, and `nav_interaction_candidates=21`.
- Focused `base2` campaign interaction depth validation passed from
  `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`. The row
  recorded `interaction_world_doors=19`, `interaction_world_buttons=5`,
  `interaction_world_triggers=22`, `interaction_world_movers=7`,
  `interaction_world_use_entities=49`, `interaction_world_touch_entities=49`,
  `nav_interaction_wait_frames=8`, `nav_interaction_use_frames=8`,
  `nav_interaction_misses=0`, `coop_interaction_retry_commands=3`,
  `coop_door_elevator_source_commands=3`,
  `coop_door_elevator_hold_commands=60`,
  `team_objective_coop_policy_follow=60`,
  `team_objective_coop_policy_wait=60`,
  `team_objective_coop_policy_regroup=60`,
  `team_objective_coop_policy_lead=1`,
  `team_objective_coop_policy_resource_share=120`, and
  `item_timing_consumer_live_pickups=59`.
- Focused `base2` campaign progression-chain validation passed from
  `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`. The row
  keeps the same wait/use interaction and coop intent gates while also
  requiring target-chain diagnostics: `interaction_world_target_entities`,
  `interaction_world_progression_targets`, `interaction_world_target_links`,
  `interaction_world_named_targets`, `interaction_world_key_entities`, and
  `interaction_world_progression_entities`.
- Focused `base2` campaign progression-consumer validation passed from
  `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`. The row
  proves the route-interaction chooser consumes target-chain context with
  `nav_interaction_progression_candidates=14`,
  `nav_interaction_progression_selections=3`,
  `nav_interaction_progression_preference_selections=2`,
  `last_nav_interaction_progression_score=6`, and
  `last_nav_interaction_target_link=1`.
- Focused `base2` campaign post-interaction validation passed from
  `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`. The row
  proves a scored progression interaction can complete, force a route refresh,
  hold a post-interaction recovery window, and suppress immediate repeat
  selection with `nav_interaction_progression_target_link_selections=3`,
  `nav_interaction_progression_completions=3`,
  `nav_interaction_progression_post_refreshes=3`,
  `nav_interaction_progression_post_frames=241`,
  `nav_interaction_progression_repeat_suppressions=1`, and
  `last_nav_interaction_progression_suppressed_entity=340`.
- Focused `base2` campaign progression-carry validation passed from
  `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`. The row
  proves one bot can retain progression-completion memory through multiple
  scored interactions, including distinct follow-up entities, with
  `nav_interaction_progression_completions=3`,
  `nav_interaction_progression_carry_completions=2`,
  `nav_interaction_progression_carry_distinct_completions=2`,
  `last_nav_interaction_progression_carry_previous_entity=42`,
  `last_nav_interaction_progression_carry_entity=332`, and
  `last_nav_interaction_progression_carry_distinct_count=3`.
- Focused `train` campaign keyed-path validation passed from
  `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`. The row proves
  the route-local interaction scan can select a keyed progression segment,
  including runtime key item/lock context and required-key metadata, with
  `nav_interaction_progression_key_path_candidates=1`,
  `nav_interaction_progression_key_path_selections=1`,
  `nav_interaction_progression_key_path_completions=1`,
  `last_nav_interaction_progression_key_path_key_lock=1`,
  `last_nav_interaction_progression_key_path_required_item=70`, and
  `interaction_world_key_locks=1`.
- Focused `train` campaign key-carry bridge validation passed from
  `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`. The row proves the
  proof bot can request the red-key leg, exercise the normal `Touch_Item`
  pickup path, observe positive red-key inventory, resolve a live train
  interaction goal with `interaction_goal_requests=1`,
  `interaction_goal_candidates=3`, `interaction_goal_resolved=1`,
  `last_interaction_goal_entity=60`, `last_interaction_goal_kind=4`, and
  `last_interaction_goal_area=2338`, activate the key-side `func_train` bridge
  interaction, resolve a routeable post-mover bridge arrival with
  `interaction_arrival_goal_requests=1`,
  `interaction_arrival_goal_candidates >= 60`,
  `interaction_arrival_goal_resolved=1`,
  `interaction_arrival_mover_endpoint_checks=3`,
  `interaction_arrival_mover_endpoint_candidates=3`,
  `interaction_arrival_mover_endpoint_selections=2`,
  `last_interaction_arrival_mover_endpoint_entity=60`,
  `last_interaction_arrival_mover_endpoint_area=3131`,
  `last_interaction_arrival_goal_area=1058`,
  and `last_interaction_arrival_goal_destination_distance_sq=18411`, then
  route the lock leg without the old direct lock-side warp and select the
  `trigger_key` lock with
  `key_carry_key_pickups=1`, `key_carry_bridge_route_requests=16`,
  `key_carry_bridge_approach_ready=1`, `key_carry_bridge_warps=0`,
  `key_carry_bridge_interactions=1`,
  `key_carry_bridge_arrival_route_requests=2`,
  `key_carry_bridge_arrival_reached=1`,
  `key_carry_bridge_arrival_warps=0`,
  `key_carry_bridge_ride_observation_requests=1`,
  `key_carry_bridge_ride_observation_frames=9`,
  `key_carry_bridge_ride_observation_completed=1`,
  `last_key_carry_bridge_ride_observation_elapsed_ms=200`,
  `interaction_mover_ride_checks=20`,
  `last_interaction_mover_ride_phase=4`,
  `last_key_carry_bridge_arrival_distance_sq=312`,
  `last_key_carry_bridge_kind=4`, `last_key_carry_bridge_travel_type=11`,
  `key_carry_lock_route_requests=34`, `key_carry_lock_warps=0`,
  `last_key_carry_pickup_inventory=1`, and
  `last_key_carry_lock_required_item=70`.
- The follow-up movement context gap run passed the full 113-row catalog from
  `.tmp\bot_scenarios\implemented_movement_context_gap_rerun3\20260628T081648Z`
  with `0` failures, timeouts, errors, or pending rows.

Validation gates:

- Coop reference scenarios cover both `base1` and `base2`, including a
  button/trigger/mover/use/touch depth gate on `base2`.
- Bots can finish or assist a small campaign flow with a human or simulated
  leader.
- Bot behavior never blocks final progression without a recovery path.

## M5: Chat And Personality

Status: In progress; live spawn, route-ready, enemy-sighted, low-health,
item-taken, objective-changed, flag-state, blocked, item-denied, and match-result event
coverage, global cooldown suppression, duplicate suppression, and four-variant
phrase libraries are implemented.

Goal: convert profile chat metadata from smoke proof into safe, useful live
personality.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Event taxonomy | Define supported live chat events: spawn, team ready, route ready, item taken, item denied, enemy sighted, objective changed, flag state, low health, blocked, and victory/defeat. | Mode `79` `bot_chat_live_events` exposes an eleven-event taxonomy and proves live `spawn` plus `route_ready` triggers through `bot_chat_live_events`; mode `81` `bot_chat_live_enemy_sighted` proves the first combat-derived `enemy_sighted` live event; mode `84` `bot_chat_live_low_health` proves the first survival-state `low_health` live event; mode `85` `bot_chat_live_item_taken` proves pickup-observation `item_taken`; mode `86` `bot_chat_live_objective_changed` proves CTF transition-driven `objective_changed`; mode `87` `bot_chat_live_flag_state` proves CTF flag-state observations; mode `88` `bot_chat_live_blocked` proves blocked route-failure observations; mode `89` `bot_chat_live_item_denied` proves TDM deny-enemy resource-policy `item_denied` observations; mode `90` `bot_chat_live_match_result` proves native intermission/match-result observations drive `victory_defeat`. |
| 2 | Phrase libraries | Expand phrase buckets for quiet, direct, taunting, helpful, steady, and future personalities. | Mode `82` `bot_chat_phrase_library` proves four initial and four reply variants are exercised by staged profile bots. |
| 3 | Audience policy | Harden global/team/private audience selection and human-only broadcast behavior. | Bots do not send reliable-message chatter to bot clients. |
| 4 | Conversation safety | Add global and per-bot cooldowns, event suppression, and duplicate prevention. | Mode `80` `bot_chat_live_event_cooldown` proves global cooldown rate limiting, and mode `83` `bot_chat_duplicate_suppression` proves repeated route-ready reply/live events are suppressed inside the 5000 ms duplicate window without dispatch failures. |
| 5 | Profile integration | Let personality influence small behavior flavor where safe, such as aggression phrasing, support communication, and objective callouts. | Personality changes are visible but not balance-breaking. |

Validation gates:

- Existing chat modes `57` through `62` remain as regression proofs.
- Mode `79` verifies live `spawn` plus `route_ready` coverage with taxonomy,
  submission, rate-limit, failure, and event-name counters.
- Mode `80` verifies cooldown suppression and rate-limit stress for the live
  event path.
- Mode `81` verifies visible/shootable enemy facts can drive
  `enemy_sighted` reply and live event accounting without bypassing dispatch
  safety.
- Mode `83` verifies duplicate reply/live event suppression across repeated
  route-ready events while preserving telemetry for the last suppressed event.
- Mode `84` verifies survival-health routing can drive `low_health` reply and
  live event accounting without bypassing dispatch safety.
- Mode `85` verifies pickup observations can drive `item_taken` reply and live
  event accounting without bypassing dispatch safety.
- Mode `86` verifies CTF pickup, death-drop, and dropped-flag return transitions
  can drive `objective_changed` reply and live event accounting without bypassing
  dispatch safety.
- Mode `87` verifies CTF pickup, death-drop, and dropped-flag return observations
  can drive `flag_state` reply and live event accounting without bypassing
  dispatch safety.
- Mode `88` verifies blocked route failures can drive `blocked` reply and live
  event accounting without bypassing dispatch safety.
- Mode `89` verifies team resource-denial policy pressure can drive
  `item_denied` reply and live event accounting without bypassing dispatch
  safety.
- Mode `90` verifies native intermission/match-result state can drive
  `victory_defeat` reply and live event accounting without bypassing dispatch
  safety.
- Outcome-specific victory/defeat phrasing is validated by the focused
  `.tmp\bot_scenarios\bot_chat_match_result_outcome.json` artifact.
- `docs-user/bot-chat.md` and the release acceptance `user_docs` check present
  chat as a supported default-off public behavior while requiring every public
  chat cvar and supported live event name to stay documented.

## M6: Map, AAS, And Movement Coverage

Status: In progress; first coop campaign map-matrix row, train key-carry
bridge-arrival proof, forced
jump/crouch/swim command rows, map-backed jump, ladder, walk-off-ledge,
elevator, barrier-jump, rocket-jump, swim, and waterjump rows, plus door
context, route target anti-spin, route movement projection while aiming
off-route, consumed route-target watchdog hardening, trace-gated command
look-ahead with ordered sequential fallback, accepted natural crouch routing,
accepted teleporter entity-route fallback, and accepted `fact2` hazard context
row are implemented. q2aas now
exposes `crouch_reference`, required `worr_crouch_ref` satisfies the natural
crouch route gate, optional `q2dm7` satisfies `slime_reference` when staged
locally, optional `fact2` satisfies `lava_reference` and
`runtime_hazard_entity_reference`, and the movement reference gap audit records
no remaining M6 reference blockers.

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

Latest validation note:

- Focused movement context validation passed 5/5 rows from
  `.tmp\bot_scenarios\movement_context_gap_rerun2\20260628T080154Z`.
  Focused natural crouch validation passed from
  `.tmp\bot_scenarios\movement_crouch_route.json`.
  Focused hazard context validation passed from
  `.tmp\bot_scenarios\movement_hazard_context_fact2.json`.
  Focused teleporter entity-route validation passed from
  `.tmp\bot_scenarios\teleporter_entity_route_final\20260629T191851Z`.
  Focused train key-carry bridge observation validation passed from
  `.tmp\bot_scenarios\mover_ride_observation_final.json`.
  Focused route movement projection validation passed 10/10 from
  `.tmp\bot_scenarios\route_spin_projection_focus.json` with zero route
  failures and projected route movement telemetry on every route-command row.
  Focused consumed route-target watchdog validation passed 10/10 from
  `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json` with the new
  nav-policy consumed-target counters present in raw logs.
  Focused route command trace/sequential look-ahead validation passed 12/12
  from `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`, and
  the q2dm2 survival regression passed from
  `.tmp\bot_scenarios\route_sequential_trace_q2dm2_fix.json`.
  Focused obstacle-aware stuck recovery validation passed from
  `.tmp\bot_scenarios\stuck_recovery_probe_focus2.json`, and the four-row
  navigation regression slice passed from
  `.tmp\bot_scenarios\stuck_recovery_probe_nav_focus.json` with selected probe
  moves and no fallback in the elevator and q2dm2 recovery rows.
  The full catalog now includes 20 movement-tagged implemented rows covering
  forced jump/crouch/swim commands; map-backed jump, ladder, walk-off-ledge,
  elevator, barrier-jump, rocket-jump, crouch, swim, and waterjump routes;
  `base1` door context; accepted train teleporter entity-route fallback; and
  accepted `fact2` hazard context.
  `tools\q2aas\discover_reference_candidates.py` now scans local BSP corpora for
  liquid and runtime hazard candidates. With local optional BSPs plus the
  WORR-authored crouch reference staged,
  `meson compile -C builddir-win q2aas-staged-smoke` validates eleven maps.
  `.tmp\bot_scenarios\movement_reference_gap_audit.json` reports
  `natural_crouch` and `hazard_context` as `accepted`.

## M7: Performance, Soak, And Reliability

Status: In progress; fresh source-counter soak, strict current-source budget
lane, repeated-run variance budget tooling, the repeatable variance soak
runner, and a fresh two-log post-change variance evidence pass are implemented.

Goal: make bot behavior stable for real server runtimes, not only short proof
runs.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Fresh source-counter soak | Rerun the ten-minute eight-bot soak with current source-counter fields. | Done 2026-06-28: `.tmp\bot_scenarios\fresh_source_counter_soak_pass_report.json` passed with CPU, route, visibility, trace, entity-trace, and memory fields present. |
| 2 | Budget tightening | Convert stable observed ranges into budget thresholds by scenario class. | Initial high-bot lane done 2026-06-29: default budget remains legacy-compatible and `source_counter_soak_budget.json` hard-gates fresh current-source telemetry. |
| 3 | Repeated-run variance gate | Fail like-for-like soak comparisons when normalized pressure or CPU metrics swing outside accepted ranges. | Done 2026-06-29: `analyze_bot_perf.py --variance-budget` evaluates `source_counter_variance_budget.json`, emits JSON/Markdown comparison results, and is covered by the release acceptance `perf_tooling` gate. Fresh two-log post-change evidence passed after the high-bot soak calibration round. |
| 4 | Multi-map soak | Add shorter multi-map soaks for map change, restart, CTF, and coop transitions. | Bots survive repeated transitions without stale state. |
| 5 | Higher bot pressure | Test above the default eight-bot manual row if server limits and maps allow it. | Degradation behavior is understood and documented. |
| 6 | Crash/leak audit | Track active route goals, reservations, BotLib memory, file handles, and bot slots through repeated cycles. | Clean unload and final cleanup counters remain zero-leak. |

Validation gates:

- Manual high-bot soak has a fresh artifact using current counters.
- Budget files are updated and documented.
- Repeated current-source soaks can be compared with a variance budget.
- Post-change repeated soaks can be launched through a single runner that
  preserves per-run scenario reports, duration metadata, analyzer JSON, and
  Markdown comparison output.
- `.install/` refresh plus scenario suite remains part of the normal validation
  rhythm after any behavior or packaging change.

2026-06-28 update: `high_bot_soak_degradation` now evaluates
`tools/bot_perf/default_soak_budget.json` directly in the scenario harness.
The green source-counter artifact at
`.tmp\bot_scenarios\fresh_source_counter_soak_pass\20260628T090904Z` reports
`perf_budget.status=pass`, `source_counter_status=pass`, all seven expected
source-counter groups present, `40.007` commands/bot/sec, `22.373`
route-queries/bot/sec, `0.5736` route-refresh ratio, and `0.4264` route-reuse
ratio. The default soak budget was refreshed to that route-cache pressure
baseline with modest headroom.

2026-06-29 update: `high_bot_soak_degradation` now evaluates both
`tools/bot_perf/default_soak_budget.json` and
`tools/bot_perf/source_counter_soak_budget.json`. The default budget remains
the compatibility `perf_budget` lane, while `perf_budgets` records the strict
current-source lane that requires all source-counter groups, current CPU
derived metrics, memory failure counts, visibility decompression failures, and
entity-trace failure counters. Scenario comparisons now expose named
per-budget pass metrics plus `perf_budget_all_pass_int`.

2026-06-29 variance gate update: `tools/bot_perf/analyze_bot_perf.py` now
accepts `--variance-budget` for comparison-level repeated-run gates. The new
`tools/bot_perf/source_counter_variance_budget.json` requires two or more
like-for-like current-source soak logs and constrains normalized throughput,
route pressure, source-counter CPU, visibility, BSP trace, and entity-trace
spread. The proof artifact
`.tmp\bot_perf\source_counter_variance_gate.json` passed as a same-log control
against the fresh source-counter soak stdout with both strict per-run budgets
green and `variance_budget.status=pass`.

2026-06-29 source-counter variance soak runner update:
`tools/bot_perf/run_source_counter_variance_soak.py` now automates the
post-change evidence workflow. It runs `high_bot_soak_degradation` repeatedly,
captures each scenario report, merges scenario metadata for the analyzer, and
then invokes `analyze_bot_perf.py` with `source_counter_soak_budget.json` and
`source_counter_variance_budget.json`. Implementation details are recorded in
`docs-dev/q3a-botlib-source-counter-variance-soak-runner-2026-06-29.md`.

2026-06-29 high-bot soak calibration update: the first real runner passes
exposed brittle long-soak assumptions. `high_bot_soak_degradation` now enables
`bot_controlled_inactive_recovery=1` so FFA deaths emit controlled respawn
commands instead of transient inactive skips. The bot-frame CPU per-run cap was
raised to `8.0` ms/bot/sec in the default and strict source-counter budgets,
and near-zero route-reuse CPU variance now uses an absolute `max_delta=0.01`
gate. The two fresh logs under
`.tmp\bot_perf\post_recovery_source_counter_variance` both passed the scenario
and strict source-counter budgets; reanalysis artifact
`.tmp\bot_perf\post_recovery_source_counter_variance_reanalyzed.json` passed
the variance gate with `14` checks, `0` failures, and `0` warnings.
Implementation details are recorded in
`docs-dev/q3a-botlib-source-counter-soak-calibration-2026-06-29.md`.

## M8: Productization And Release Readiness

Status: In progress; the public bot surface/defaults audit guard, executable
release acceptance runner, multiplayer playtest generator, playtest triage
tooling, Duel/CTF play-depth evidence attachment tooling, Duel/CTF headless
play-depth runner tooling with profile coverage checks, M3 multiplayer gate
tooling, perf budget/variance acceptance check, and post-crouch-reference
release acceptance dry run are implemented.

Goal: make bots understandable and supportable for users and server operators.

Implementation slices:

| Order | Slice | Details | Done When |
|---|---|---|---|
| 1 | Public cvar audit | Done for current source/docs via `tools/bot_surface/audit_bot_surface.py`; public `bot_*` defaults, Q3-style commands, smoke-only hooks, forbidden legacy prefixes, and user-doc default rows are now checked by tests. | User docs only present supported controls. |
| 2 | Defaults pass | Choose safe defaults for practice servers and dedicated servers. | A user can enable bots with a small config and get sane behavior. |
| 3 | Profile pack polish | Balance bundled profiles by skill, role, personality, preferred weapons, and team behavior. | Packaged profiles feel distinct and validate cleanly. |
| 4 | Operator docs | Update `docs-user/bots.md`, `docs-user/bot-profiles.md`, `docs-user/bot-playtest.md`, and relevant server docs. | Operators know setup, map/AAS limits, performance guidance, playtest flow, and troubleshooting steps. |
| 5 | Release packaging | Post-crouch-reference executable dry run done via `tools/bot_release/run_bot_acceptance.py`, validating botfiles, `.install/basew` archive/loose mirrors, eleven staged AAS maps, the staged `worr_crouch_ref` BSP, docs, profile roster, perf tooling, promoted scenario evidence, and movement-reference audit status. | Release artifacts contain everything needed for supported bot play. |
| 6 | Final acceptance run | Run fresh build/`.install` refresh, full automated suite, focused live behavior scenarios, manual soaks, and representative playtests. | FR-04 scope can be marked complete in the strategic roadmap. |

Validation gates:

- User docs match actual supported behavior.
- `.install/` payload is refreshed and audited.
- `tools/bot_release/run_bot_acceptance.py` passes against the selected
  scenario report and writes a JSON artifact under `.tmp\bot_release`.
- Release acceptance requires the promoted `movement_crouch_route` and
  `movement_hazard_context` rows plus an accepted movement-reference audit.
- `tools/bot_surface/audit_bot_surface.py` verifies every public bot cvar
  source default and `docs-user/bot-cvars.md` default row.
- `tools/bot_playtest/generate_bot_playtest.py` writes the FFA, Duel, TDM, and
  CTF playtest checklist and structured artifact under `.tmp\bot_playtest`.
- `tools/bot_playtest/triage_bot_playtest.py` converts operator playtest notes
  into triage reports and scenario-candidate recommendations under
  `.tmp\bot_playtest`.
- `tools/bot_playtest/build_bot_playdepth_evidence.py` converts the required
  Duel and CTF play-depth notes into JSON/Markdown release attachments and can
  run strict once those notes are expected to be complete.
- `tools/bot_playtest/run_bot_playdepth_headless.py` starts the required Duel
  and CTF cases on the dedicated server, captures roster/stdout/stderr
  artifacts, validates expected first-party profile coverage, and writes
  prefilled notes that remain pending until visual review is complete.
- `tools/bot_playtest/check_m3_multiplayer_gate.py` combines the automated M3
  scenario baseline with the Duel/CTF play-depth attachment and reports
  whether M3 is passed, pending, or failed.
- `tools/bot_perf/analyze_bot_perf.py` validates single-run soak budgets and
  repeated-run variance budgets, and `tools/bot_release/run_bot_acceptance.py`
  validates the budget files.
- Windows local build and CI platform builds cover bot-related targets.
- Credits and provenance remain current.

## Recommended Next Ten Slices

These are the next practical implementation slices, ordered to reduce rework.
Each slice should be small enough to validate independently. The prior "Fresh
multiplayer playtest script" slice is complete via
`tools/bot_playtest/generate_bot_playtest.py` and
`docs-dev/q3a-botlib-multiplayer-playtest-script-2026-06-29.md`; the
"Playtest evidence triage" slice is complete via
`tools/bot_playtest/triage_bot_playtest.py` and
`docs-dev/q3a-botlib-playtest-evidence-triage-2026-06-29.md`; the "Public
defaults/docs release pass" slice is complete via
`docs-user/bot-cvars.md`,
`tools/bot_surface/audit_bot_surface.py`, and
`docs-dev/q3a-botlib-public-defaults-docs-gate-2026-06-29.md`; the
"Source-counter variance gate" slice is complete via
`tools/bot_perf/source_counter_variance_budget.json` and
`docs-dev/q3a-botlib-source-counter-variance-budget-gate-2026-06-29.md`; the
"Source-counter variance soak runner" slice is complete via
`tools/bot_perf/run_source_counter_variance_soak.py` and
`docs-dev/q3a-botlib-source-counter-variance-soak-runner-2026-06-29.md`; the
"Fresh post-change source-counter variance evidence run" slice is complete via
`.tmp\bot_perf\post_recovery_source_counter_variance_reanalyzed.json` and
`docs-dev/q3a-botlib-source-counter-soak-calibration-2026-06-29.md`; the
"Post-build release acceptance dry run" slice is complete via
`.tmp\bot_release\bot_release_acceptance_post_crouch_reference.json` and
`docs-dev/q3a-botlib-release-acceptance-post-crouch-reference-2026-06-30.md`;
the "Duel/CTF play-depth evidence attachment tooling" slice is complete via
`tools/bot_playtest/build_bot_playdepth_evidence.py` and
`docs-dev/q3a-botlib-duel-ctf-playdepth-evidence-tooling-2026-06-30.md`; the
"M3 multiplayer milestone gate" slice is complete via
`tools/bot_playtest/check_m3_multiplayer_gate.py` and
`docs-dev/q3a-botlib-m3-multiplayer-gate-2026-06-30.md`; the "Headless
Duel/CTF play-depth runner" slice is complete via
`tools/bot_playtest/run_bot_playdepth_headless.py` and
`docs-dev/q3a-botlib-m3-headless-playdepth-runner-2026-06-30.md`; the
"Min-player first-party profile coverage" slice is complete via
`src/server/main.c`, the refreshed headless run
`.tmp\bot_playtest\headless\20260630T200649Z\bot_playdepth_headless_runs.json`,
and `docs-dev/q3a-botlib-min-player-profile-coverage-2026-06-30.md`.
The "Min-player profile coverage scenario gate" slice is complete via
`src/server/main.c`, `tools/bot_scenarios/run_bot_scenarios.py`,
`.tmp\bot_scenarios\min_players_profile_coverage.json`,
`.tmp\bot_release\bot_release_acceptance_min_player_profile_scenario.json`,
and
`docs-dev/q3a-botlib-min-player-profile-coverage-scenario-gate-2026-07-01.md`.
The "Outcome-aware match-result chat polish" slice is complete via
`src/game/sgame/bots/bot_brain.cpp`,
`tools/bot_scenarios/run_bot_scenarios.py`,
`.tmp\bot_scenarios\bot_chat_match_result_outcome.json`, and
`docs-dev/q3a-botlib-match-result-outcome-chat-2026-07-01.md`.
The "Bot chat user-facing docs readiness pass" slice is complete via
`docs-user/bot-chat.md`, the release acceptance chat-doc contract in
`tools/bot_release/run_bot_acceptance.py`, the focused public surface audit
`.tmp\bot_surface\public_bot_surface_chat_docs_audit.json`, and
`docs-dev/q3a-botlib-bot-chat-user-doc-readiness-2026-07-01.md`.
The "Second campaign interaction map row" and first campaign interaction-depth
slice are complete via `coop_campaign_interaction_matrix_base2`,
`coop_campaign_interaction_depth_base2`, the focused artifacts
`.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json` and
`.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`, and
`docs-dev/q3a-botlib-campaign-interaction-depth-base2-2026-07-01.md`.
The base2 campaign progression-chain, progression-consumer,
post-interaction/progression-carry, and train keyed-path/key-carry bridge-approach/arrival slices are
complete via `coop_campaign_progression_chain_base2`,
`coop_campaign_progression_consumer_base2`,
`coop_campaign_post_interaction_base2`, `coop_campaign_progression_carry_base2`,
`coop_campaign_keyed_path_train`, `coop_campaign_key_carry_train`, their
focused artifacts, `docs-dev/q3a-botlib-campaign-keyed-path-train-2026-07-01.md`,
`docs-dev/q3a-botlib-campaign-key-carry-train-2026-07-01.md`, and
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-2026-07-01.md`, plus
the live train interaction-goal follow-up
`docs-dev/q3a-botlib-campaign-key-carry-train-interaction-goal-2026-07-01.md`
and the bridge-arrival follow-up
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-2026-07-01.md`,
the bridge-approach and bridge-arrival-route follow-ups
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-approach-2026-07-01.md`
and
`docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-route-2026-07-01.md`,
and the reusable arrival route-state, mover-endpoint, mover ride-state, and
physical elevator mover activation follow-ups
`docs-dev/q3a-botlib-interaction-arrival-route-state-2026-07-01.md` and
`docs-dev/q3a-botlib-interaction-arrival-mover-endpoint-2026-07-01.md`, plus
`docs-dev/q3a-botlib-interaction-mover-ride-state-2026-07-01.md` and
`docs-dev/q3a-botlib-physical-elevator-mover-activation-2026-07-01.md`.

| Order | Slice | Primary Milestone | Main Task IDs | Expected Artifact |
|---|---|---|---|---|
| 1 | Duel live-server play-depth pass | M3 | `FR-04-T04`, `FR-04-T06`, `DV-07-T06` | Start from the headless notes or `bot_playtest_duel_rotation.cfg`, visually review the match, mark the case pass/fail, rebuild `bot_duel_ctf_playdepth_evidence.*`, and rerun `check_m3_multiplayer_gate.py` while checking item-denial timing, spawn pressure, and queue boundaries beyond the mode `75` smoke. |
| 2 | CTF live-server play-depth pass | M3 | `FR-04-T04`, `FR-04-T15`, `DV-07-T06` | Start from the headless notes or `bot_playtest_ctf_objectives.cfg`, visually review the match, mark the case pass/fail, rebuild `bot_duel_ctf_playdepth_evidence.*`, and rerun `check_m3_multiplayer_gate.py` while checking pickup/drop/return handoffs, carrier support, and base-return decisions beyond the mode `76` smoke. |
| 3 | Campaign transition/keyed-path progression proof | M4/M6 | `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-07-T06` | Complete via `coop_campaign_keyed_path_train`, `coop_campaign_key_carry_train`, `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`, `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`, `.tmp\bot_scenarios\interaction_arrival_mover_endpoint.json`, `.tmp\bot_scenarios\mover_ride_state.json`, `.tmp\bot_scenarios\mover_ride_observation_final.json`, `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json`, `.tmp\bot_scenarios\movement_elevator_physical.json`, `.tmp\bot_scenarios\mover_direct_use_regression.json`, `docs-dev/q3a-botlib-campaign-keyed-path-train-2026-07-01.md`, `docs-dev/q3a-botlib-campaign-key-carry-train-2026-07-01.md`, `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-2026-07-01.md`, `docs-dev/q3a-botlib-campaign-key-carry-train-interaction-goal-2026-07-01.md`, `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-2026-07-01.md`, `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-approach-2026-07-01.md`, `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-route-2026-07-01.md`, `docs-dev/q3a-botlib-interaction-arrival-route-state-2026-07-01.md`, `docs-dev/q3a-botlib-interaction-arrival-mover-endpoint-2026-07-01.md`, `docs-dev/q3a-botlib-interaction-mover-ride-state-2026-07-01.md`, `docs-dev/q3a-botlib-interaction-mover-ride-observation-2026-07-01.md`, `docs-dev/q3a-botlib-generic-mover-lifecycle-2026-07-01.md`, and `docs-dev/q3a-botlib-physical-elevator-mover-activation-2026-07-01.md`; next transition work should promote physical grounded-on-mover samples on additional maps/interactions. |
| 4 | Triage-backed playtest evidence run | M3/M8 | `FR-04-T04`, `FR-04-T06`, `FR-04-T16`, `DV-07-T06` | Run the generated Duel/CTF play-depth cases, fill the notes template, rerun triage, and convert any promoted candidate into a scenario row. |
| 5 | Bot cvar docs release freeze | M8 | `FR-04-T07`, `FR-04-T16`, `DV-07-T06` | Keep `docs-user/bot-cvars.md` frozen to supported public cvars while release notes and server references are finalized. |
| 6 | Next behavior-change source-counter variance refresh | M7 | `FR-04-T16`, `DV-03-T05`, `DV-05-T05` | Re-run `tools/bot_perf/run_source_counter_variance_soak.py` after the next movement/combat/routing behavior change and attach the fresh JSON/Markdown artifacts. |

## Scenario Strategy

Scenario growth should stay evidence-driven. Do not add rows only to increase
the count. Add a row when it proves a new behavior contract, protects against a
real regression, or promotes a smoke-only proof into live behavior.

Preferred scenario ladder:

| Stage | Purpose | Example |
|---|---|---|
| Smoke proof | Prove a source-owned counter or bridge exists. | Existing modes `20` through `77`. |
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
| M0 Foundation Snapshot | Done | 123-row implemented catalog, latest aggregate `.tmp\bot_scenarios\implemented_after_consumed_target_watchdog.json` with `123/123` automated rows passing and focused supplemental artifacts retained for the campaign/train/mover/elevator/route-projection/consumed-target watchdog evidence trail. | Preserve while deepening live behavior. |
| M1 Live Behavior Arbitration | Done | Mode `63` `behavior_arbitration` proof with cvar audit, candidates, owners, handoffs, and the 2026-06-27 visible-target deferral rerun. | Use the owner model while implementing M2 combat/inventory depth. |
| M2 Combat And Inventory Depth | In progress | Mode `64` `target_memory_decay`, mode `65` `weapon_scoring_arsenal`, mode `66` `aim_fire_policy_depth`, mode `67` `ammo_pressure_pickup`, mode `68` `survival_inventory_use`, mode `69` `survival_health_route`, mode `70` `survival_armor_route`, mode `71` `combat_survival_regression`, `combat_survival_regression_q2dm2`, `combat_survival_regression_q2dm8`, mode `72` `threat_retreat_avoidance`, `threat_retreat_avoidance_q2dm8`, the 2026-06-28 role-combat/base-action deferral rerun, and the green 114-row full-suite run. | Promote deeper live combat while preserving route/item/retreat ownership. |
| M3 Multiplayer Mode Intelligence | In progress | Existing FFA/TDM/CTF role and objective proof rows, mode `40` `ctf_objective_route` live-loop validation from `.tmp\bot_scenarios\20260622T210329Z`, mode `73` `tdm_role_spawn_stability` validation from `.tmp\bot_scenarios\20260622T212431Z`, mode `74` `ffa_live_pacing` validation from `.tmp\bot_scenarios\20260622T214927Z`, mode `75` `duel_live_pacing` validation from `.tmp\bot_scenarios\20260622T222142Z`, mode `76` `ctf_objective_transitions` validation from `.tmp\bot_scenarios\20260622T230509Z`, the 2026-06-27 FFA/Duel pacing rerun from `.tmp\bot_scenarios\bot-profile-roam-state-fixes-rerun3`, the 2026-06-29 multiplayer playtest generator plus triage tooling covering FFA, Duel, TDM, and CTF, the 2026-06-30 Duel/CTF play-depth evidence attachment builder, the refreshed headless Duel/CTF run `.tmp\bot_playtest\headless\20260630T200649Z\bot_playdepth_headless_runs.json` proving full CTF first-party profile coverage including `smoke`, the focused public min-player profile coverage row `.tmp\bot_scenarios\min_players_profile_coverage.json`, and the M3 gate artifact `.tmp\bot_playtest\bot_m3_multiplayer_gate.json` still pending on visual review of the Duel/CTF play-depth notes. | Review the headless Duel/CTF notes in live play, mark both required outcomes, attach completed evidence output, and mark M3 done when `check_m3_multiplayer_gate.py` reports `passed`. |
| M4 Coop And Campaign Behavior | In progress | Existing coop readiness, leader, progress-wait, interaction, resource, anti-blocking, target-share, and door/elevator proof rows, mode `77` `coop_live_loop` validation from `.tmp\bot_scenarios\20260622T234315Z`, mode `78` `coop_share_loop` validation from `.tmp\bot_scenarios\20260623T001149Z`, mode `91` `coop_campaign_interaction_matrix` validation from `.tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final\20260626T185108Z`, mode `91` `coop_campaign_interaction_matrix_base2` validation from `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json`, mode `91` `coop_campaign_interaction_depth_base2` validation from `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`, mode `91` `coop_campaign_progression_chain_base2` validation from `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`, mode `91` `coop_campaign_progression_consumer_base2` validation from `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`, mode `91` `coop_campaign_post_interaction_base2` validation from `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`, mode `91` `coop_campaign_progression_carry_base2` validation from `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`, mode `91` `coop_campaign_keyed_path_train` validation from `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`, mode `91` `coop_campaign_key_carry_train` validation from `.tmp\bot_scenarios\mover_ride_observation_final.json` including live train interaction-goal selection, natural bridge-start approach, key-side `func_train` bridge evidence, reusable post-mover interaction-arrival route assignment/reached telemetry, mover endpoint discovery/selection telemetry, explicit wait/board/ride/leave lifecycle telemetry, bounded bridge ride observation, terminal leave-phase preservation, and warp-free lock routing, plus generic coop door/elevator and coop live-loop wait/board/leave gates from `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json`, and physical elevator direct-use/moving-state evidence from `.tmp\bot_scenarios\movement_elevator_physical.json`. | Promote physical grounded-on-mover samples where available. |
| M5 Chat And Personality | In progress | Modes `57` through `62` prove dispatch, audience, rate, initial, reply, and event-policy selection; mode `79` `bot_chat_live_events` proves live spawn plus route-ready accounting from `.tmp\bot_scenarios\20260623T010520Z`; mode `80` `bot_chat_live_event_cooldown` proves global cooldown suppression from `.tmp\bot_scenarios\20260623T010530Z`; mode `81` `bot_chat_live_enemy_sighted` proves blackboard-visible enemy chat from `.tmp\bot_scenarios\20260623T013832Z`; mode `82` `bot_chat_phrase_library` proves four-variant phrase selection from `.tmp\bot_scenarios\20260623T020850Z`; mode `83` `bot_chat_duplicate_suppression` proves duplicate route-ready reply/live event suppression from `.tmp\bot_scenarios\20260623T023211Z`; mode `84` `bot_chat_live_low_health` proves survival-state low-health live chat from `.tmp\bot_scenarios\20260623T025752Z`; mode `85` `bot_chat_live_item_taken` proves pickup-observation live chat from `.tmp\bot_scenarios\20260623T051126Z`; mode `86` `bot_chat_live_objective_changed` proves CTF transition-driven objective live chat from `.tmp\bot_scenarios\20260626T140601Z`; mode `87` `bot_chat_live_flag_state` proves CTF flag-state live chat from `.tmp\bot_scenarios\20260626Tflagstate3\20260626T144136Z`; mode `88` `bot_chat_live_blocked` proves blocked route-failure live chat from `.tmp\bot_scenarios\20260626Tblocked-fixed\20260626T151437Z`; mode `89` `bot_chat_live_item_denied` proves TDM deny-enemy resource-policy live chat from `.tmp\bot_scenarios\20260626Titem-denied\20260626T154429Z`; mode `90` `bot_chat_live_match_result` proves native intermission/match-result live chat from `.tmp\bot_scenarios\20260626Tmatch-result\20260626T182046Z` and outcome-aware win/loss phrasing from `.tmp\bot_scenarios\bot_chat_match_result_outcome.json`; `docs-user/bot-chat.md` plus release acceptance now cover the public chat cvars and event taxonomy. | Finish any remaining personality-copy polish. |
| M6 Map, AAS, And Movement Coverage | In progress | Eleven local staged q2aas reference maps when optional `q2dm7` and `fact2` are present, mode `91` `base1` and `base2` coop campaign interaction evidence, `base2` campaign depth context covering buttons/triggers/movers/use/touch entities plus target-chain/progression context, `train` keyed-path context covering key items, `trigger_key` locks, required-key telemetry, red-key pickup, live `func_train` interaction-goal resolution, natural bridge-start approach, real bridge activation, reusable post-mover bridge arrival route assignment/reached telemetry, interaction-arrival mover endpoint discovery/selection telemetry, mover wait/board/ride/leave lifecycle telemetry, bounded parked-mover bridge observation, generic coop mover/elevator wait/board/leave lifecycle gates, mode `12` physical elevator direct-use and moving-state evidence, route target anti-spin, route movement projection, consumed route-target watchdog evidence from `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json`, and trace-gated command look-ahead with ordered sequential fallback evidence from `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`, lock-side carry without proof warps, `base1` door context evidence, mode `92` `worr_crouch_ref` natural crouch route evidence, modes `93`/`94` q2dm2 swim/waterjump evidence, mode `95` train teleporter entity-route evidence, mode `96` accepted `fact2` hazard context, the movement-tagged implemented rows, and `.tmp\bot_scenarios\movement_reference_gap_audit.json` accepting both `natural_crouch` and `hazard_context`. | Promote grounded-on-mover diagnostics into hard gates where the map supports them. |
| M7 Performance, Soak, And Reliability | In progress | Fresh source-counter high-bot soak passed from `.tmp\bot_scenarios\fresh_source_counter_soak_pass\20260628T090904Z`; scenario default `perf_budget`, strict `perf_budgets` source-counter lane, standalone analyzer pass with all seven source-counter groups present, variance gate artifact `.tmp\bot_perf\source_counter_variance_gate.json`, repeatable runner `tools/bot_perf/run_source_counter_variance_soak.py`, and fresh two-log post-change reanalysis `.tmp\bot_perf\post_recovery_source_counter_variance_reanalyzed.json` passing 14 variance checks. | Rerun the variance runner after the next movement/combat/routing behavior change. |
| M8 Productization And Release Readiness | In progress | Current bot user docs, public bot cvar/default docs, profile docs, bot chat docs, bot playtest docs, package audits, CI release matrix hooks, packaged `botfiles/bots.txt`, `tools\bot_surface` public surface/default audit with `.tmp\bot_surface\public_bot_surface_chat_docs_audit.json` reporting 0 violations, `tools\bot_playtest` generated checklist/triage/headless/profile-coverage/evidence-attachment/M3-gate support, refreshed `.install` after the key-carry bridge-approach scenario build, focused supplemental scenario evidence at `.tmp\bot_scenarios\min_players_profile_coverage.json`, `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json`, `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`, `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`, `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`, `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`, `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`, `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`, and `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`, and release acceptance requiring 18 scenario evidence rows including the second campaign interaction, depth, progression-chain, progression-consumer, post-interaction, progression-carry, keyed-path, and key-carry bridge-approach row. | Run the manual review on the generated Duel/CTF headless notes and attach completed M3 gate output to release notes. |

## Risks To Watch

| Risk | Impact | Mitigation |
|---|---|---|
| Proof cvars leak into user-facing behavior. | Bots feel brittle and require obscure setup. | Classify cvars early in M1 and keep smoke-only gates out of public docs. |
| Behavior owners fight each other. | Bots oscillate or freeze between route, combat, item, and objective goals. | Centralize arbitration, expose last-owner reason fields, and keep visible-target combat as a deferral unless the base action layer is attacking. |
| Scenario count grows without coverage quality. | Validation looks broad but misses live regressions. | Require every row to state its behavior contract and promotion reason. |
| AAS quality varies by map. | Bots work on one map and fail elsewhere. | Expand the reference matrix and document known failures explicitly. |
| CPU budgets drift upward as behavior deepens. | Server operators cannot run useful bot counts. | Run fresh source-counter soaks, keep strict per-run budgets green, and compare repeated logs with the variance budget after behavior changes. |
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
- Repeated current-source soak comparison through the variance budget.
- Package audit for botfiles and AAS assets.
- User-doc review against current public cvars and defaults.
- Credits/provenance review confirming no unrecorded upstream import or q2proto
  change.

The final closeout should update this file, the Q3A BotLib/AAS port plan, the
strategic roadmap, the credits ledger, and user docs in one coordinated pass.
