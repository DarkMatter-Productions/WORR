# Q3A BotLib and Q2 AAS Port Credits Ledger

Date: 2026-06-16

Related plan: `docs-dev/plans/q3a-botlib-aas-port.md`

Related audit: `docs-dev/q3a-botlib-aas-source-audit-2026-06-16.md`

Related tasks: `FR-04-T01`, `FR-04-T02`, `FR-04-T10`, `FR-04-T11`, `FR-04-T12`, `FR-04-T13`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-05-T02`, `DV-05-T05`, `DV-07-T06`

## Purpose

Track source provenance, credits, licenses, and local modification notes for the Quake III Arena BotLib and Quake II AAS generator work. This ledger must be updated in the same change set as any imported, adapted, or substantially referenced upstream file.

## Recent Native Validation Updates

- 2026-07-02: The stuck recovery obstacle-probe follow-up is WORR-native
  bot-nav, bot-brain, scenario-harness, roadmap, documentation, build, and
  release-staging work with no new upstream imports claimed. Local stuck
  recovery now probes short player-hull escape candidates when a recovery
  window activates, stores the selected world-space direction, and projects it
  back into view-relative usercmd movement. The fixed legacy back/strafe
  command remains only as a no-clear fallback. The new nav-policy fields report
  probe checks, selected probe moves, blocked probes, fallbacks, last probe
  candidate/fraction, and last projected recovery movement. Focused
  `recover_from_stall` evidence passed from
  `.tmp\bot_scenarios\stuck_recovery_probe_focus2.json`; focused navigation
  regression evidence passed 4/4 from
  `.tmp\bot_scenarios\stuck_recovery_probe_nav_focus.json`; the fresh full
  implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_stuck_recovery_probe.json`; and release
  acceptance passed 15/15 with 0 warnings from
  `.tmp\bot_release\bot_release_acceptance_stuck_recovery_probe.json`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-stuck-recovery-obstacle-probe-2026-07-02.md`.
- 2026-07-02: The route command trace/sequential look-ahead follow-up is
  WORR-native bot-nav, bot-brain, scenario-harness, roadmap, documentation,
  build, and release-staging work with no new upstream imports claimed.
  Command steering now uses nav-layer hull trace safety before promoting
  ordinary far look-ahead or final goal fallback targets, while already
  consumed route nodes advance only to the first ordered non-close future route
  point and stop further promotion when that trace is blocked. Endpoint trace
  checks now try small vertical normalizations for AAS floor/contact points.
  The new frame-command fields report trace checks, trace blocks,
  goal-trace blocks, sequential fallbacks, and the last blocked look-ahead
  index. Focused route/navigation evidence passed 12/12 from
  `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`; focused
  q2dm2 survival regression evidence passed from
  `.tmp\bot_scenarios\route_sequential_trace_q2dm2_fix.json`; the fresh full
  implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_route_sequential_trace_lookahead_fix.json`;
  and release acceptance passed 15/15 with 0 warnings from
  `.tmp\bot_release\bot_release_acceptance_route_sequential_trace_lookahead.txt`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-route-command-trace-sequential-lookahead-2026-07-02.md`.
- 2026-07-02: The consumed route target watchdog follow-up is WORR-native
  bot-nav, bot-brain, scenario-harness, roadmap, documentation, and
  release-staging work with no new upstream imports claimed. Route-progress
  accounting now accepts a fresh local `route.moveTarget` arrival once, treats
  repeated already-consumed target-radius frames as stagnant unless real goal
  or target progress occurs, and compares target shifts horizontally to match
  the watchdog's progress model. The new `q3a_bot_nav_policy_status` fields
  report `stuck_target_reached_progresses`,
  `stuck_consumed_target_stalls`, `last_stuck_target_distance_sq`, and
  `last_stuck_consumed_target`. Focused route/combat/movement evidence passed
  10/10 from `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json`;
  the fresh full implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_consumed_target_watchdog.json`, and
  release acceptance passed 15/15 with 0 warnings from
  `.tmp\bot_release\bot_release_acceptance_consumed_target_watchdog.json`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-consumed-route-target-watchdog-2026-07-02.md`.
- 2026-07-01: The route movement projection follow-up is WORR-native
  bot-brain, scenario-harness, roadmap, documentation, and release-staging
  work with no new upstream imports claimed. Route command generation now
  separates movement from view aim by projecting route yaw into view-relative
  `forwardMove` and `sideMove`, so bots can keep moving along a route while
  aiming at a visible combat target. The command layer also accepts bounded
  approximate `route.moveTarget` matches and records projected route movement,
  strafe, backpedal, unmatched-target, approximate-match, and goal-fallback
  telemetry. Focused route/combat/movement evidence passed 10/10 from
  `.tmp\bot_scenarios\route_spin_projection_focus.json`; the fresh full
  implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_route_projection_fix.json`, and
  release acceptance passed 15/15 from
  `.tmp\bot_release\bot_release_acceptance_route_projection_fix.json`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-route-movement-projection-2026-07-01.md`.
- 2026-07-01: The route target anti-spin/status-surface follow-up is
  WORR-native bot-brain, bot-nav, scenario, status-surface, test, and
  documentation work with no new upstream imports claimed. Route-steered
  commands now preserve the stabilized `route.moveTarget`, match it back into
  the active route point list, skip already-consumed close route points, and
  fall back to the final route goal only when the local route points are
  exhausted. Stuck-progress accounting now tracks both final-goal and local
  route-target progress so valid corner, switchback, and mover approaches do
  not look stagnant. The scenario harness records look-ahead preserved-target
  and close-point skip counters, and the full sweep restored missing live
  match-result outcome fields on `q3a_bot_chat_policy_status`. Focused route
  evidence passed 8/8 from
  `.tmp\bot_scenarios\route_spin_final_after_status.json`; focused
  `bot_chat_live_match_result` status evidence passed from
  `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json`; the fresh
  full implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_route_spin_status_fix.json`, and
  release acceptance passed 15/15 from
  `.tmp\bot_release\bot_release_acceptance_route_spin_fix.json`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-route-target-anti-spin-2026-07-01.md`.
- 2026-07-01: The physical elevator mover activation follow-up is WORR-native
  bot-brain, scenario, test, and documentation work with no new upstream
  imports claimed. Recovery moves that already request `use` can now directly
  activate platform, train, and generic mover interactions with a
  per-client/entity cooldown. Raw mode `12` now receives a proof slot and a
  24-frame settle window so `movement_elevator_route` hard-gates direct-use
  activation and physical mover movement instead of route-only elevator
  reachability. Focused evidence records direct-use activation,
  travel-type elevator activation, ride-observation moving/completion, and
  shared mover moving-state telemetry from
  `.tmp\bot_scenarios\movement_elevator_physical.json`; the post-cleanup
  focused rerun passed from
  `.tmp\bot_scenarios\movement_elevator_physical_final.json`; the five-row
  mover/co-op regression passed from
  `.tmp\bot_scenarios\mover_direct_use_regression.json`; the fresh full
  implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_physical_elevator_mover.json`, and
  release acceptance passed 15/15 from
  `.tmp\bot_release\bot_release_acceptance_physical_elevator_mover_final.json`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-physical-elevator-mover-activation-2026-07-01.md`.
- 2026-07-01: The generic mover lifecycle follow-up is WORR-native bot-nav,
  scenario, test, and documentation work with no new upstream imports claimed.
  `bot_nav` now records separate Wait and Board samples for generic WaitUse
  mover/elevator recovery windows and preserves terminal Leave samples when
  those interactions complete. The standalone `coop_door_elevator` and
  umbrella `coop_live_loop` rows now hard-gate generic mover lifecycle
  evidence with `interaction_mover_ride_invalid_skips=0`; focused evidence
  records `interaction_mover_ride_checks=217`,
  `interaction_mover_ride_wait_states=104`,
  `interaction_mover_ride_board_states=104`,
  `interaction_mover_ride_leave_states=9`,
  `last_interaction_mover_ride_phase=4`,
  `last_interaction_mover_ride_entity=18`, and
  `last_interaction_mover_ride_kind=3` for both coop rows from
  `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json`. The fresh
  full implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_generic_mover_lifecycle.json`, and
  release acceptance passed 15/15 from
  `.tmp\bot_release\bot_release_acceptance_generic_mover_lifecycle.json`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-generic-mover-lifecycle-2026-07-01.md`.
- 2026-07-01: The interaction mover ride-observation follow-up is WORR-native
  bot-nav, bot-brain, scenario, test, and documentation work with no new
  upstream imports claimed. `bot_brain` now keeps the train key-carry bridge
  proof in a bounded 200 ms post-activation observation window, records
  parked-mover sample diagnostics, and then returns to the normal
  post-arrival/lock route. `bot_nav` preserves terminal leave-phase evidence
  for the same mover/client so later generic recovery samples cannot overwrite
  completed lifecycle status. Mode `91` `coop_campaign_key_carry_train`
  focused evidence records
  `key_carry_bridge_ride_observation_requests=1`,
  `key_carry_bridge_ride_observation_frames=9`,
  `key_carry_bridge_ride_observation_completed=1`,
  `last_key_carry_bridge_ride_observation_elapsed_ms=200`,
  `interaction_mover_ride_checks=20`,
  `last_interaction_mover_ride_phase=4`,
  `nav_interaction_progression_key_path_candidates=1`,
  `nav_interaction_progression_key_path_selections=1`,
  `last_nav_interaction_progression_key_path_required_item=70`, and
  `key_carry_lock_warps=0` from
  `.tmp\bot_scenarios\mover_ride_observation_final.json`; the fresh full
  implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_mover_ride_observation.json`, and
  release acceptance passed 15/15 from
  `.tmp\bot_release\bot_release_acceptance_mover_ride_observation.json`.
  Implementation details are recorded in
  `docs-dev/q3a-botlib-interaction-mover-ride-observation-2026-07-01.md`.
- 2026-07-01: The interaction mover ride-state follow-up is WORR-native
  bot-nav, bot-brain, scenario, test, and documentation work with no new
  upstream imports claimed. `bot_nav` now records wait, board, ride, and leave
  lifecycle telemetry for mover-like interactions, including invalid skips,
  phase counters, mover entity/kind/action/client metadata, move-state and
  ground-entity diagnostics, and last-position distance data. Mode `91`
  `coop_campaign_key_carry_train` focused evidence records
  `interaction_mover_ride_checks=5`,
  `interaction_mover_ride_wait_states=1`,
  `interaction_mover_ride_board_states=1`,
  `interaction_mover_ride_ride_states=2`,
  `interaction_mover_ride_leave_states=1`, and
  `last_interaction_mover_ride_phase=4` from
  `.tmp\bot_scenarios\mover_ride_state.json`; the fresh full implemented
  suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_mover_ride_state.json`, and release
  acceptance passed 15/15. Implementation details are recorded in
  `docs-dev/q3a-botlib-interaction-mover-ride-state-2026-07-01.md`.
- 2026-07-01: The interaction-arrival mover-endpoint follow-up is WORR-native
  bot-nav, bot-brain, scenario, test, and documentation work with no new
  upstream imports claimed. `bot_nav` now inspects mover destination and target
  link endpoints while resolving the post-interaction arrival side of a route,
  records endpoint checks/candidates/selections and last-endpoint metadata, and
  still permits a closer routeable destination-offset point to win when it is
  the safer final arrival goal. Mode `91` `coop_campaign_key_carry_train`
  focused evidence records `interaction_arrival_mover_endpoint_checks=3`,
  `interaction_arrival_mover_endpoint_candidates=3`,
  `interaction_arrival_mover_endpoint_selections=2`,
  `last_interaction_arrival_mover_endpoint_entity=60`, and
  `last_interaction_arrival_mover_endpoint_area=3131` from
  `.tmp\bot_scenarios\interaction_arrival_mover_endpoint.json`; the fresh full
  implemented suite passed 123/123 from
  `.tmp\bot_scenarios\implemented_after_interaction_arrival_mover_endpoint.json`,
  and release acceptance passed 15/15. Implementation details are recorded in
  `docs-dev/q3a-botlib-interaction-arrival-mover-endpoint-2026-07-01.md`.
- 2026-07-01: The interaction-arrival route-state follow-up is WORR-native
  bot-nav, bot-brain, scenario, and documentation work with no new upstream
  imports claimed. `BotNavRouteRequest` can now tag a position route as the
  arrival side of a specific interaction entity, and route status records
  generic request, assignment, cache-reuse, reached, and last-route telemetry.
  Mode `91` `coop_campaign_key_carry_train` focused evidence records
  `interaction_arrival_route_requests=2`,
  `interaction_arrival_route_assignments=2`,
  `interaction_arrival_route_reached=1`,
  `key_carry_bridge_arrival_reached=1`,
  `key_carry_lock_route_requests=42`, and `route_failures=0` from
  `.tmp\bot_scenarios\20260701T162117Z`. Implementation details are recorded in
  `docs-dev/q3a-botlib-interaction-arrival-route-state-2026-07-01.md`.
- 2026-07-01: The train campaign key-carry bridge-approach follow-up is
  WORR-native bot-brain, scenario, release-gate, and documentation work with
  no new upstream imports claimed. Mode `91`
  `coop_campaign_key_carry_train` now validates natural route progress to the
  live `func_train` bridge start with `key_carry_bridge_approach_ready=1` and
  `key_carry_bridge_warps=0`, while preserving the bridge interaction,
  post-mover arrival projection, and lock-side key-path telemetry. Focused
  evidence records `key_carry_bridge_route_requests=16`,
  `key_carry_bridge_approach_requests=16`,
  `last_key_carry_bridge_approach_distance_sq=98`,
  `key_carry_bridge_interactions=1`, the then-temporary bridge-arrival warp
  counter, and `key_carry_lock_warps=0` from
  `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`. Implementation
  details are recorded in
  `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-approach-2026-07-01.md`.
- 2026-07-01: The train campaign key-carry bridge-arrival-route follow-up is
  WORR-native bot-brain, scenario, release-gate, and documentation work with
  no new upstream imports claimed. Mode `91`
  `coop_campaign_key_carry_train` now routes to the projected post-mover
  bridge-arrival point instead of teleporting to it, then advances to the lock
  route. Focused evidence records
  `key_carry_bridge_arrival_route_requests=2`,
  `key_carry_bridge_arrival_reached=1`,
  `last_key_carry_bridge_arrival_distance_sq=112`,
  `key_carry_bridge_arrival_warps=0`, `key_carry_lock_warps=0`, and
  `route_failures=0` from
  `.tmp\bot_scenarios\coop_campaign_key_carry_train.json` and
  `.tmp\bot_scenarios\20260701T145703Z`; the fresh full implemented suite
  passed 123/123 from `.tmp\bot_scenarios\implemented_after_bridge_arrival_route.json`
  and release acceptance passed 15/15. Implementation details are recorded in
  `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-route-2026-07-01.md`.
- 2026-07-01: The train campaign key-carry bridge-arrival follow-up is
  WORR-native bot-nav, bot-brain, scenario, release-gate, and documentation
  work with no new upstream imports claimed. Mode `91`
  `coop_campaign_key_carry_train` now validates routeable post-mover arrival
  projection from the live `func_train` source to the final lock destination,
  removes the previous direct lock-side warp from the focused row, and
  preserves `trigger_key` key-path selection through a one-shot lock
  activation. Focused evidence records
  `interaction_arrival_goal_resolved=1`,
  `last_interaction_arrival_goal_area=1058`,
  `last_interaction_arrival_goal_destination_distance_sq=18411`,
  the then-temporary bridge-arrival warp counter, and
  `key_carry_lock_warps=0` from
  `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`. Implementation
  details are recorded in
  `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-2026-07-01.md`.
- 2026-07-01: The train campaign key-carry interaction-goal follow-up is
  WORR-native bot-nav, bot-brain, scenario, release-gate, and documentation
  work with no new upstream imports claimed. Mode `91`
  `coop_campaign_key_carry_train` now validates that the bridge-start proof
  resolves a live train interaction entity before activating the key-side
  `func_train`; focused evidence records `interaction_goal_candidates=3`,
  `interaction_goal_resolved=1`, `last_interaction_goal_entity=60`,
  `last_interaction_goal_kind=4`, and `last_interaction_goal_area=2338` from
  `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`. Release acceptance
  passed against the historical aggregate plus supplemental focused rows, and
  implementation details are recorded in
  `docs-dev/q3a-botlib-campaign-key-carry-train-interaction-goal-2026-07-01.md`.
- 2026-07-01: The train campaign key-carry bridge proof is WORR-native
  bot-nav, bot-brain, scenario, release-gate, and documentation work with no
  new upstream imports claimed. Mode `91` `coop_campaign_key_carry_train` now
  validates red-key pickup, key-side `func_train` bridge activation with
  elevator/mover travel telemetry, explicit bridge-start and lock-side proof
  warp accounting, and final `trigger_key` required-key telemetry on packaged
  campaign map `train`. Focused evidence passed from
  `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`, release acceptance
  passed against the historical aggregate plus supplemental focused rows, and
  implementation details are recorded in
  `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-2026-07-01.md`.
- 2026-07-01: The train campaign keyed-path proof is WORR-native
  route-status, bot-brain, scenario, release-gate, and documentation work with
  no new upstream imports claimed. Mode `91`
  `coop_campaign_keyed_path_train` validates runtime key item context,
  `trigger_key` lock context, key-path progression selection/completion, and
  required-key telemetry on packaged campaign map `train`. Focused evidence
  passed from `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`,
  release acceptance passed from
  `.tmp\bot_release\bot_release_acceptance_keyed_path.json`, and
  implementation details are recorded in
  `docs-dev/q3a-botlib-campaign-keyed-path-train-2026-07-01.md`.
- 2026-07-01: The bot chat user-doc readiness pass is WORR-native
  documentation/release-tooling/test work with no new upstream imports claimed.
  It adds `docs-user/bot-chat.md`, updates the public bot docs to point at the
  supported chat setup, and extends release acceptance so the chat guide must
  mention every public chat cvar and supported live event name. The focused
  public surface artifact
  `.tmp\bot_surface\public_bot_surface_chat_docs_audit.json` reports zero
  violations, and the release acceptance artifact
  `.tmp\bot_release\bot_release_acceptance_chat_docs.json` passes 15/15 checks.
  Implementation log:
  `docs-dev/q3a-botlib-bot-chat-user-doc-readiness-2026-07-01.md`.
- 2026-07-01: The match-result outcome-chat polish is WORR-native
  bot-brain/status-surface/scenario/test/documentation work with no new
  upstream imports claimed. It classifies native match-result chat into
  win/loss/tie/abort/unknown phrase paths, exposes the outcome counters through
  `q3a_bot_chat_policy_status`, and hardens mode `90`
  `bot_chat_live_match_result` on deterministic win/loss evidence. The focused
  artifact `.tmp\bot_scenarios\bot_chat_match_result_outcome.json` passes with
  `reply_chat_match_result_win=2`, `reply_chat_match_result_loss=2`, zero
  unknown/tie/abort classifications, and
  `last_match_result_outcome_name=loss`. Implementation log:
  `docs-dev/q3a-botlib-match-result-outcome-chat-2026-07-01.md`.
- 2026-07-01: The chat smoke queue-determinism pass is WORR-native
  server/scenario/test/documentation work with no new upstream imports claimed.
  It makes frame-command smoke staging count queued bot additions before
  issuing another `SV_BotAdd`, preventing duplicate pending adds from
  overfilling four-bot chat policy rows, and makes mode `90` validate
  order-independent win/loss outcome coverage. Focused validation passed 4/4,
  the full automated implemented suite passed 123/123, and release acceptance
  passed 15/15. Implementation log:
  `docs-dev/q3a-botlib-chat-smoke-queue-determinism-2026-07-01.md`.
- 2026-07-01: The min-player profile coverage scenario gate is WORR-native
  server/scenario/release tooling, tests, and documentation work with no new
  upstream imports claimed. It adds a dedicated min-player autofill profile
  coverage marker, promotes `min_players_profile_coverage` into the scenario
  catalog, and requires that row in release acceptance. The focused scenario
  artifact `.tmp\bot_scenarios\min_players_profile_coverage.json` passes with
  `covered=5` and `pass=1`, and the release artifact
  `.tmp\bot_release\bot_release_acceptance_min_player_profile_scenario.json`
  passes 15/15 checks. Implementation log:
  `docs-dev/q3a-botlib-min-player-profile-coverage-scenario-gate-2026-07-01.md`.
- 2026-06-30: The min-player first-party profile coverage fix is WORR-native
  server/tooling/tests/documentation work with no new upstream imports claimed.
  It removes the public autofill special case that skipped `smoke`, extends the
  headless Duel/CTF runner and play-depth evidence builder with expected
  profile coverage checks, and refreshes `.install` from the rebuilt Windows
  dedicated runtime. The refreshed headless artifact
  `.tmp\bot_playtest\headless\20260630T200649Z\bot_playdepth_headless_runs.json`
  shows CTF coverage for `bulwark`, `relay`, `smoke`, `vanguard`, and
  `vector`. Implementation log:
  `docs-dev/q3a-botlib-min-player-profile-coverage-2026-06-30.md`.
- 2026-06-30: The M3 headless play-depth runner is WORR-native
  playtest/release tooling, tests, and documentation work with no new upstream
  imports claimed. It adds `tools/bot_playtest/run_bot_playdepth_headless.py`,
  captures dedicated-server stdout/stderr plus real `botlist` roster rows for
  `duel_rotation` and `ctf_objectives`, writes pending review notes, and adds a
  `playdepth_headless_tooling` check to
  `tools/bot_release/run_bot_acceptance.py`. A short real run passed from
  `.tmp\bot_playtest\headless\20260630T195219Z`, while evidence rebuilt from
  those notes correctly remains pending until visual review. The latest
  acceptance artifact `.tmp\bot_release\bot_release_acceptance_m3_headless.json`
  passes 15/15 checks. Implementation log:
  `docs-dev/q3a-botlib-m3-headless-playdepth-runner-2026-06-30.md`.
- 2026-06-30: The M3 multiplayer gate is WORR-native playtest/release
  tooling, tests, and documentation work with no new upstream imports claimed.
  It adds `tools/bot_playtest/check_m3_multiplayer_gate.py`, combines the
  automated multiplayer scenario baseline with the Duel/CTF play-depth
  attachment, and adds an `m3_multiplayer_gate_tooling` check to
  `tools/bot_release/run_bot_acceptance.py`. Current local gate evidence is
  pending only on unfilled live Duel/CTF notes, while the release tooling proof
  artifact `.tmp\bot_release\bot_release_acceptance_m3_gate.json` passes
  14/14 checks. Implementation log:
  `docs-dev/q3a-botlib-m3-multiplayer-gate-2026-06-30.md`.
- 2026-06-30: The Duel/CTF play-depth evidence attachment tooling is
  WORR-native playtest/release tooling, tests, and documentation work with no
  new upstream imports claimed. It adds
  `tools/bot_playtest/build_bot_playdepth_evidence.py`, release-note style
  JSON/Markdown output for the required `duel_rotation` and `ctf_objectives`
  notes, and a `playdepth_evidence_tooling` gate in
  `tools/bot_release/run_bot_acceptance.py`. The latest acceptance artifact
  `.tmp\bot_release\bot_release_acceptance_playdepth_evidence.json` passes
  13/13 checks. Implementation log:
  `docs-dev/q3a-botlib-duel-ctf-playdepth-evidence-tooling-2026-06-30.md`.
- 2026-06-30: The post-crouch-reference release acceptance hardening is
  WORR-native release tooling, tests, and documentation work with no new
  upstream imports claimed. It expands `tools/bot_release/run_bot_acceptance.py`
  to require the eleven-map staged AAS set, staged `worr_crouch_ref.bsp`,
  promoted `movement_crouch_route` and `movement_hazard_context` evidence, and
  an accepted movement-reference audit with zero blockers. The post-promotion
  artifact `.tmp\bot_release\bot_release_acceptance_post_crouch_reference.json`
  passes 12/12 checks. Implementation log:
  `docs-dev/q3a-botlib-release-acceptance-post-crouch-reference-2026-06-30.md`.
- 2026-06-30: The crouch reference promotion modifies the existing vendored
  q2aas/BSPC reachability generator file
  `tools/q2aas/deps/botlib/be_aas_reach.c` and otherwise adds WORR-native
  reference-map, manifest, staging, scenario-tooling, server smoke helper, and
  documentation work. It preserves `TRAVEL_CROUCH` for crouch-only equal-floor
  and step reachabilities, adds required `worr_crouch_ref` natural-crouch
  coverage, promotes mode `92` to accepted `movement_crouch_route`, and updates
  the runtime AAS movement smoke to accept crouch presence. Focused validation
  passed from `.tmp\bot_scenarios\movement_crouch_route.json`; q2aas staged
  smoke validates eleven maps; and
  `.tmp\bot_scenarios\movement_reference_gap_audit.json` now accepts both
  `natural_crouch` and `hazard_context`. Implementation log:
  `docs-dev/q3a-botlib-crouch-reference-promotion-2026-06-30.md`.
- 2026-06-30: The hazard reference promotion is WORR-native q2aas,
  scenario-tooling, server smoke helper, and documentation work with no new
  upstream imports claimed. It promotes optional official Quake II `fact2` as
  the lava/runtime hazard reference, adds
  `runtime_hazard_entity_reference` manifest coverage, renames mode `96` to
  accepted `movement_hazard_context`. Later same-day crouch-reference work
  resolves the remaining natural-crouch audit blocker. Focused validation
  passed from
  `.tmp\bot_scenarios\movement_hazard_context_fact2.json`. Implementation log:
  `docs-dev/q3a-botlib-hazard-reference-promotion-2026-06-30.md`.
- 2026-06-30: The movement reference gap audit is WORR-native q2aas,
  scenario-tooling, and documentation work with no new upstream imports
  claimed. It adds `crouch` reference-feature readiness from generated
  `TRAVEL_CROUCH` counts, declares the `crouch_reference` manifest gate, and
  adds `tools/bot_scenarios/audit_movement_reference_gaps.py` to report when
  natural crouch or hazard-context rows can be promoted or accepted.
  Later same-day evidence promotes both hazard context and natural crouch; the
  audit now accepts both gaps at
  `.tmp\bot_scenarios\movement_reference_gap_audit.json`.
- 2026-06-30: The reference candidate discovery follow-up is WORR-native q2aas,
  scenario-audit, and documentation work with no new upstream imports claimed.
  It adds `tools/q2aas/discover_reference_candidates.py`, promotes optional
  `q2dm7` as the first slime reference candidate in the manifest, stages
  `q2dm7.aas` when the local BSP is available, and records scratch
  lava/runtime hazard candidates. Later same-day `fact2` promotion provides the
  accepted hazard reference. Implementation log:
  `docs-dev/q3a-botlib-reference-candidate-discovery-2026-06-30.md`.
- 2026-06-29: The teleporter entity-route promotion is WORR-native adapter,
  nav, scenario, and documentation work with no new upstream imports claimed.
  It adds `Q3A_BotLibImport_BuildRouteSteerTowardGoal()` and
  `BotLibAdapter_BuildRouteSteerTowardGoal()` as a first-reachability route
  path for exact entity-backed goals when full preferred-goal prediction stops
  short. Focused validation passed for mode `95`
  `movement_teleporter_entity_route` from
  `.tmp\bot_scenarios\teleporter_entity_route_final\20260629T191851Z`.
- 2026-06-29: The source-counter variance soak runner is WORR-native tooling
  and documentation work with no new upstream imports claimed. It adds
  `tools/bot_perf/run_source_counter_variance_soak.py` and
  `tools/bot_perf/test_run_source_counter_variance_soak.py` to standardize the
  repeated `high_bot_soak_degradation` post-change workflow, merge scenario
  metadata for duration-aware analyzer runs, and evaluate both
  `source_counter_soak_budget.json` and
  `source_counter_variance_budget.json`.
- 2026-06-29: The high-bot source-counter soak calibration is WORR-native
  scenario, budget, and documentation work with no new upstream imports
  claimed. It enables `bot_controlled_inactive_recovery=1` for the long soak,
  recalibrates the default/strict bot-frame CPU cap to `8.0`
  ms/bot/sec, converts near-zero route-reuse CPU variance to an absolute
  `max_delta=0.01`, and records the passing two-log reanalysis artifact
  `.tmp\bot_perf\post_recovery_source_counter_variance_reanalyzed.json`.
- 2026-06-29: The source-counter variance budget gate is WORR-native tooling
  and documentation work with no new upstream imports claimed. It adds
  `tools/bot_perf/source_counter_variance_budget.json`,
  `analyze_bot_perf.py --variance-budget`, comparison-level JSON/text/Markdown
  pass/fail output, and a `perf_tooling` acceptance check in
  `tools/bot_release/run_bot_acceptance.py`. The same-log control artifact
  `.tmp\bot_perf\source_counter_variance_gate.json` passed strict per-run
  source-counter budgets twice and 14 variance checks; focused validation
  passed with `tools\bot_perf` reporting 18 tests and `tools\bot_release`
  reporting 8 tests.
- 2026-06-29: The strict source-counter budget lane is WORR-native tooling
  and documentation work with no new upstream imports claimed. It adds
  `tools/bot_perf/source_counter_soak_budget.json`, multi-budget
  `perf_budgets` scenario reporting for `high_bot_soak_degradation`, named
  comparison pass metrics, and strict current-source checks for all seven
  source-counter groups plus CPU, memory, visibility, and entity-trace fields.
  Focused validation passed with the scenario/perf pytest suite reporting
  72 tests.
- 2026-06-28: The hazard context gap round is WORR-native work with no new
  upstream imports claimed. It adds mode `96` `movement_hazard_context_gap`
  on packaged map `base2`, records the current staged-content gap with
  `interaction_world_hazards=0`, and broadens runtime/nav hazard
  classification for `target_laser` and `misc_lavaball` beside
  hurt/lava/slime triggers. Focused validation passed from
  `.tmp\bot_scenarios\movement_hazard_context_gap\20260628T083930Z`;
  the full implemented suite passed 114/114 rows from
  `.tmp\bot_scenarios\implemented_hazard_context\20260628T083945Z`.
- 2026-06-28: The movement context gap matrix round is WORR-native work with
  no new upstream imports claimed. It adds compact nav interaction context
  counters for teleporters and hazards, keeps the interaction marker ahead of
  verbose frame-command output, expands frame-command status capture for long
  aggregate runs, adds implemented crouch-gap, door-context, swim,
  waterjump, and teleporter-gap scenario rows through mode `95`, and isolates
  scenario runs with `bot_min_players 0` unless a row intentionally overrides
  the baseline. Live FFA roam now yields to item goals, route-facing wins
  unless the bot is firing, and role combat defers behind weak/non-attacking
  base decisions. The full implemented suite passed 113/113 rows from
  `.tmp\bot_scenarios\implemented_movement_context_gap_rerun3\20260628T081648Z`;
  focused movement context validation passed 5/5 rows from
  `.tmp\bot_scenarios\movement_context_gap_rerun2\20260628T080154Z`.
- 2026-06-28: The movement matrix and live behavior round is WORR-native work
  with no new upstream imports claimed. It adds implemented movement matrix
  rows for forced jump/crouch/swim commands and map-backed jump, ladder,
  walk-off-ledge, elevator, barrier-jump, rocket-jump, swim, and waterjump
  routes; stabilizes live FFA roam/item ownership; keeps route-facing ahead of
  visible-enemy facing unless the bot is firing; and makes role combat defer
  behind weak, underpowered, weapon-switching, or non-attacking base decisions.
  The full implemented suite passed 110/110 rows from
  `.tmp\bot_scenarios\implemented_rerun_after_fixes\20260627T234219Z`;
  focused movement validation passed 11/11 rows from
  `.tmp\bot_scenarios\movement_matrix_expansion_rerun\20260627T232805Z`.
- 2026-06-27: The profile autofill, roam, and combat stabilization slice is
  WORR-native work with no new upstream imports claimed. It adds a packaged
  Q3-style `botfiles/bots.txt` manifest for the first-party bot pack, rotates
  min-player autofill through loaded profiles, processes autofill while local
  simulation is paused, and stabilizes live roam/item/combat arbitration.
  Focused evidence passed from
  `.tmp\bot_scenarios\bot-profile-roam-state-fixes-rerun3` and
  `.tmp\bot_scenarios\bot-role-combat-compat-check3`; direct `.install`
  min-player smoke spawned profile-backed `B|Bulwark`, `B|Relay`, and
  `B|Vanguard`.
- 2026-06-26: The coop campaign interaction matrix slice is WORR-native work with no new upstream imports claimed. Mode `91` `coop_campaign_interaction_matrix` validates the coop live-loop interaction family on packaged campaign map `base1`, with route-interaction retry, campaign mover source ownership, teammate hold commands, nav interaction candidates, and coop wait policy evidence at `.tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final\20260626T185108Z`. The latest full `implemented` suite passed 99/99 rows from `.tmp\bot_scenarios\20260626Timplemented-coop-campaign-interaction-json\20260626T185549Z`.
- 2026-07-01: The second campaign interaction row is WORR-native scenario and release-gate work with no new upstream imports claimed. Mode `91` `coop_campaign_interaction_matrix_base2` validates the same coop live-loop interaction family on packaged campaign map `base2`, with focused evidence at `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json` and implementation log `docs-dev/q3a-botlib-second-campaign-interaction-row-2026-07-01.md`.
- 2026-07-01: The base2 campaign interaction-depth row is WORR-native scenario and release-gate work with no new upstream imports claimed. Mode `91` `coop_campaign_interaction_depth_base2` validates button, trigger, mover, use/touch, wait/use-frame, coop intent, and live pickup timing evidence on packaged campaign map `base2`, with focused evidence at `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json` and implementation log `docs-dev/q3a-botlib-campaign-interaction-depth-base2-2026-07-01.md`.

- 2026-07-01: The base2 campaign progression-chain row is WORR-native route-status, scenario, and release-gate work with no new upstream imports claimed. Mode `91` `coop_campaign_progression_chain_base2` validates target-chain diagnostics for level-flow target classes, outbound target links, named target anchors, key-entity counter exposure, and combined progression context on packaged campaign map `base2`, with focused evidence at `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json` and implementation log `docs-dev/q3a-botlib-campaign-progression-chain-base2-2026-07-01.md`.
- 2026-07-01: The base2 campaign progression-consumer row is WORR-native route-status, interaction-selection, scenario, and release-gate work with no new upstream imports claimed. Mode `91` `coop_campaign_progression_consumer_base2` validates that the live route-interaction chooser scores target-chain candidates, selects scored progression interactions, and records at least one preference selection over pure nearest-distance choice on packaged campaign map `base2`, with focused evidence at `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json` and implementation log `docs-dev/q3a-botlib-campaign-progression-consumer-base2-2026-07-01.md`.
- 2026-07-01: The base2 campaign post-interaction progression row is WORR-native route-status, interaction-lifecycle, scenario, and release-gate work with no new upstream imports claimed. Mode `91` `coop_campaign_post_interaction_base2` validates target-linked progression selection, command-backed completion, post-interaction route refresh, post-window telemetry, and repeat suppression on packaged campaign map `base2`, with focused evidence at `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json` and implementation log `docs-dev/q3a-botlib-campaign-post-interaction-base2-2026-07-01.md`.
- 2026-07-01: The base2 campaign progression-carry row and dedicated-server bot print crash guard are WORR-native route-status, engine-safety, scenario, and release-gate work with no new upstream imports claimed. Mode `91` `coop_campaign_progression_carry_base2` validates that one bot can complete multiple scored progression interactions, including distinct follow-up entities after a prior completion, on packaged campaign map `base2`. Focused evidence passed from `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`, release acceptance passed from `.tmp\bot_release\bot_release_acceptance_progression_carry.json`, and implementation details are recorded in `docs-dev/q3a-botlib-campaign-progression-carry-base2-2026-07-01.md`.
- 2026-06-26: The bot chat live match-result slice is WORR-native work with no new upstream imports claimed. Mode `90` `bot_chat_live_match_result` validates native intermission/match-result `victory_defeat` live chat telemetry through `q3a_bot_chat_policy_status` and `q3a_bot_intermission_status`, with focused evidence at `.tmp\bot_scenarios\20260626Tmatch-result\20260626T182046Z`. The then-current full `implemented` suite passed 98/98 rows from `.tmp\bot_scenarios\20260626Timplemented-match-result\20260626T182111Z`.
- 2026-06-26: The bot chat live item-denied slice is WORR-native work with no new upstream imports claimed. Mode `89` `bot_chat_live_item_denied` validates TDM deny-enemy resource-policy `item_denied` live chat telemetry through `q3a_bot_chat_policy_status`, with focused evidence at `.tmp\bot_scenarios\20260626Titem-denied\20260626T154429Z`. The then-current full `implemented` suite passed 97/97 rows from `.tmp\bot_scenarios\20260626Timplemented-item-denied-json-file\20260626T154954Z`.
- 2026-06-26: The bot chat live blocked slice is WORR-native work with no new upstream imports claimed. Mode `88` `bot_chat_live_blocked` validates route-failure `blocked` live chat telemetry through `q3a_bot_chat_policy_status`, with focused evidence at `.tmp\bot_scenarios\20260626Tblocked-fixed\20260626T151437Z`. The then-current full `implemented` suite passed 96/96 rows from `.tmp\bot_scenarios\20260626Timplemented-blocked\20260626T151446Z`.
- 2026-06-26: The bot chat live flag-state slice is WORR-native work with no new upstream imports claimed. Mode `87` `bot_chat_live_flag_state` validates CTF pickup/drop/return flag-state live chat telemetry through `q3a_bot_chat_policy_status`, with focused evidence at `.tmp\bot_scenarios\20260626Tflagstate3\20260626T144136Z`. The then-current full `implemented` suite passed 95/95 rows from `.tmp\bot_scenarios\20260626Timplemented-flagstate-fixed\20260626T144511Z`.
- 2026-06-26: The bot chat live objective-changed slice is WORR-native work with no new upstream imports claimed. Mode `86` `bot_chat_live_objective_changed` validates CTF pickup/drop/return transition `objective_changed` live chat telemetry through `q3a_bot_chat_policy_status`, with focused evidence at `.tmp\bot_scenarios\20260626T140601Z`. The then-current full `implemented` suite passed 94/94 rows from `.tmp\bot_scenarios\20260626T140621Z`.
- 2026-06-23: The bot chat live item-taken slice is WORR-native work with no new upstream imports claimed. Mode `85` `bot_chat_live_item_taken` validates health/armor pickup observation `item_taken` live chat telemetry through `q3a_bot_chat_policy_status`, with focused evidence at `.tmp\bot_scenarios\20260623T051126Z`. The then-current full `implemented` suite passed 93/93 rows from `.tmp\bot_scenarios\20260623T051133Z`.
- 2026-06-23: The bot chat live low-health slice is WORR-native work with no new upstream imports claimed. Mode `84` `bot_chat_live_low_health` validates survival-state `low_health` live chat telemetry through `q3a_bot_chat_policy_status`, with focused evidence at `.tmp\bot_scenarios\20260623T025752Z`. The then-current full `implemented` suite passed 92/92 rows from `.tmp\bot_scenarios\20260623T025801Z`.
- 2026-06-23: The bot chat duplicate-suppression slice is WORR-native work with no new upstream imports claimed. Mode `83` `bot_chat_duplicate_suppression` validates the per-bot 5000 ms reply duplicate window, duplicate suppression telemetry, and repeated route-ready reply/live event suppression through `q3a_bot_chat_policy_status`, with focused evidence at `.tmp\bot_scenarios\20260623T023211Z`. The then-current full `implemented` suite passed 91/91 rows from `.tmp\bot_scenarios\20260623T023230Z`.
- 2026-06-23: The bot chat phrase-library expansion is WORR-native work with no new upstream imports claimed. Mode `82` `bot_chat_phrase_library` validates four initial and four reply phrase variants through `q3a_bot_chat_policy_status` telemetry, with focused evidence at `.tmp\bot_scenarios\20260623T020850Z`. The then-current full `implemented` suite passed 90/90 rows from `.tmp\bot_scenarios\20260623T021355Z`.

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
| `tools/q2aas/**` excluding `bspc.c`, `be_aas_bspc.c`, `map.c`, `deps/botlib/be_aas_reach.c`, `meson.build`, `worr_q2aas_compat.h`, `worr_q2aas_q2trace.c`, `worr_q2aas_q2trace.h`, `README.WORR.md`, `cfg/worr_q2.cfg`, `validate_worr_q2aas.py`, `test_validate_worr_q2aas.py`, `discover_reference_candidates.py`, `test_discover_reference_candidates.py`, `audit_worr_q2aas_stage.py`, `audit_worr_q2aas_package.py`, `package_worr_q2aas_archive.py`, and `validation_manifest.json` | `https://github.com/TTimo/bspc` repository root | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Direct vendored source snapshot | GPL-2.0-or-later | Upstream `LICENSE` retained; original id Software GPL headers retained where present. | Ben Noordhuis, Chris Brooke, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset, Victor Luchits; id Software retained from source headers. | Copied under `tools/q2aas/`; no local source edits outside separately listed modified files. | `meson compile -C builddir-win worr_q2aas`; `builddir-win\tools\q2aas\worr_q2aas.exe` usage smoke. |
| `tools/q2aas/bspc.c` | `https://github.com/TTimo/bspc/blob/10d23c5ebd042ddc5d03e17de0f560f5076649dc/bspc.c` | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Modified direct import | GPL-2.0-or-later | Original id Software GPL header retained; `Modified for WORR 2026-06-16` note added. | Ben Noordhuis, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset; id Software retained from source header. | Allows `MAPTYPE_QUAKE2` to run reachability/clustering, centralizes loaded-map reach eligibility, frees retained Q2 BSP data after each AAS conversion, and treats failed BSP loads as fatal before generation continues. | `meson compile -C builddir-win worr_q2aas`; `meson compile -C builddir-win q2aas-staged-smoke` reports `reachabilitysize = 562`, `numclusters = 4`, and invalid BSP failure. |
| `tools/q2aas/be_aas_bspc.c` | `https://github.com/TTimo/bspc/blob/10d23c5ebd042ddc5d03e17de0f560f5076649dc/be_aas_bspc.c` | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Modified direct import | GPL-2.0-or-later | Original id Software GPL header retained; `Modified for WORR 2026-06-16` note added. | id Software retained from source header; TTimo/bspc lineage retained. | Routes Q2 BotLib trace, point contents, entity data, inline model bounds, and checksum setup through the WORR Q2 trace bridge while preserving the Q3 collision path. | `meson compile -C builddir-win worr_q2aas`; strict `mm-rage.bsp` validation. |
| `tools/q2aas/map.c` | `https://github.com/TTimo/bspc/blob/10d23c5ebd042ddc5d03e17de0f560f5076649dc/map.c` | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Modified direct import | GPL-2.0-or-later | Original id Software GPL header retained; `Modified for WORR 2026-06-16` note added. | Ben Noordhuis, Chris Brooke, Joel Baxter, Thomas Koeppe, Timothee "TTimo" Besset; id Software retained from source header. | Retains Q2 BSP collision lumps during AAS generation so the Q2 reachability bridge can use them after `AAS_Create`. | `meson compile -C builddir-win worr_q2aas`; strict `mm-rage.bsp` validation. |
| `tools/q2aas/deps/botlib/be_aas_reach.c` | `https://github.com/TTimo/bspc/blob/10d23c5ebd042ddc5d03e17de0f560f5076649dc/deps/botlib/be_aas_reach.c` | `10d23c5ebd042ddc5d03e17de0f560f5076649dc` | Modified direct import | GPL-2.0-or-later | Original id Software GPL header retained; `Modified for WORR 2026-06-30` note added. | id Software retained from source header; TTimo/bspc lineage retained. | Preserves `TRAVEL_CROUCH` instead of emitting `TRAVEL_WALK` when equal-floor, step-up, or small step-down reachabilities connect crouch-only areas. | `meson compile -C builddir-win q2aas-staged-smoke`; focused `movement_crouch_route` validation reports `last_reachability_type=3` and `movement_state_crouch_commands>=1`. |
| `tools/q2aas/meson.build` | WORR-native build wrapper for the imported snapshot | N/A | Native implementation | WORR project license | No upstream header; local build integration file. | WORR contributors | Adds the standalone `worr_q2aas` executable, include paths, warning exceptions, platform defines, thread/math deps, Q2 trace bridge source, and `q2aas-config-smoke` / `q2aas-staged-smoke` / `q2aas-package-map-smoke` / `q2aas-stage-aas` / `q2aas-stage-audit` / `q2aas-package-audit` / `q2aas-package-aas` / `q2aas-package-archive-audit` run targets. The staged smoke requires Q2 BSP input, writes deterministic AAS metadata sidecars, enforces clean BSP lump/spawn/item/high-value reachability gates, and runs expected-failure invalid BSP plus malformed-manifest checks. The package-map smoke creates a scratch pkz, extracts `maps/mm-rage.bsp`, and validates archive-backed conversion. The stage target copies validated AAS output into `.install/basew/maps/` and writes a staged-output report. The stage audit target verifies staged AAS path, size, and hashes against that report. The package audit target verifies staged AAS release payload representation. The package AAS target injects validated staged AAS into `.install/basew/pak0.pkz`; the archive audit target requires the packaged AAS member to match the staged hash. | `meson setup builddir-win --reconfigure`; `meson compile -C builddir-win worr_q2aas`; `meson compile -C builddir-win q2aas-config-smoke`; `meson compile -C builddir-win q2aas-staged-smoke`; `meson compile -C builddir-win q2aas-package-map-smoke`; `meson compile -C builddir-win q2aas-stage-aas`; `meson compile -C builddir-win q2aas-stage-audit`; `meson compile -C builddir-win q2aas-package-audit`; `meson compile -C builddir-win q2aas-package-aas`; `meson compile -C builddir-win q2aas-package-archive-audit`. |
| `tools/q2aas/worr_q2aas_compat.h` | WORR-native compatibility shim | N/A | Native implementation | WORR project license | No upstream header; local compatibility file. | WORR contributors | Force-included only for the tool build; normalizes `_WIN32` to upstream `WIN32` and declares the existing BSPC `COM_Compress` function. | `meson compile -C builddir-win worr_q2aas`. |
| `tools/q2aas/worr_q2aas_q2trace.c`, `tools/q2aas/worr_q2aas_q2trace.h` | WORR-native Q2 BSP trace bridge, informed by WORR collision behavior and existing Q2 BSP structures | N/A | Native implementation | WORR project license | Local source comments. | WORR contributors | Provides static-world Q2 point contents, box traces, inline model bounds, entity string access, and BSP checksum support for BotLib reachability generation. | `meson compile -C builddir-win worr_q2aas`; strict `mm-rage.bsp` validation. |
| `tools/q2aas/cfg/worr_q2.cfg` | WORR-native Q2 movement/AAS preset | WORR movement constants from `src/game/sgame/player/p_move.cpp` and `src/game/bgame/game.hpp` | Native configuration | WORR project license | Local config comments. | WORR contributors | Defines first standing/crouched Q2 player hulls and WORR movement constants for AAS generation. | `meson compile -C builddir-win q2aas-config-smoke`; `python tools\q2aas\validate_worr_q2aas.py --map .install\basew\maps\mm-rage.bsp`. |
| `tools/q2aas/validation_manifest.json` | WORR-native staged-map validation matrix | N/A | Native configuration | WORR project license | Local JSON configuration. | WORR contributors | Records the manifest schema, task IDs, current staged strict smoke/reference maps, per-map diagnostic gate requirements, per-map baseline metric/travel minima for the local validation set, CTF/team-objective/campaign/water/mover/teleport/elevator/door reference coverage, required `worr_crouch_ref` crouch reference coverage, optional `q2dm7` slime reference coverage, optional `fact2` lava/runtime hazard reference coverage, and no pending reference-map feature categories. | `meson compile -C builddir-win q2aas-staged-smoke`; `meson compile -C builddir-win q2aas-stage-aas`. |
| `tools/q2aas/validate_worr_q2aas.py` | WORR-native Q2 AAS validation helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Runs cfg, map, manifest, invalid-BSP, malformed-manifest, and packaged-map smoke checks; keeps scratch output under `.tmp/q2aas/`; validates manifest schema/version/task IDs/gate types/baseline keys before conversion; supports loose map `path` entries and archive-backed `archive`/`archive_member` entries extracted under `.tmp/q2aas/packaged-maps/`; rejects path/archive conflicts, missing archive members, absolute archive members, traversal members, and drive-root-like archive member components before extraction; records `map_source` provenance in reports and metadata; parses AAS summary/travel metrics; preflights Q2 `IBSP` version 38 headers; detects BSPX markers; decodes AAS headers; parses entity and brush-content diagnostics; maps spawn/item origins to generated AAS area bounds; reports high-value pickup reachability from spawn areas; records CTF team spawn/flag reachability in `team_objective_report`; records campaign goal/changelevel/key/trigger/door/mover evidence in `campaign_progression_report`; reports reference-feature readiness for crouch from generated `TRAVEL_CROUCH` counts plus water, slime, lava, teleport, elevator, and door evidence; lets manifest-loaded maps own strict diagnostic gates so CTF/campaign references do not inherit deathmatch-only high-value gating; writes JSON reports and deterministic `.aas.meta.json` sidecars; records manifest provenance and manifest schema smoke results in reports; exposes the strict `--require-reachability` gate; enforces staged diagnostic gates for clean BSP lump tables, spawn/item AAS coverage, and high-value pickup reachability where required; enforces manifest minimum AAS metric/travel-count baselines; and can stage validated `.aas` output with staged path/hash report metadata. | `meson compile -C builddir-win q2aas-config-smoke`; `meson compile -C builddir-win q2aas-staged-smoke`; `meson compile -C builddir-win q2aas-package-map-smoke`; `meson compile -C builddir-win q2aas-stage-aas`; `python tools\q2aas\validate_worr_q2aas.py --manifest tools\q2aas\validation_manifest.json --require-q2-bsp --require-reachability --require-clean-bsp-lumps --require-spawn-coverage --require-item-coverage --require-high-value-reachability --manifest-schema-smoke`. |
| `tools/q2aas/discover_reference_candidates.py`, `tools/q2aas/test_discover_reference_candidates.py` | WORR-native q2aas reference-candidate discovery tooling | N/A | Native implementation and tests | WORR project license | Local script comments/docstring. | WORR contributors | Scans explicit BSPs or directories for Q2 IBSP38 maps, scores water/slime/lava/door/teleport/runtime-hazard signals, optionally converts top candidates through `validate_worr_q2aas.py`, and writes JSON/Markdown reports under `.tmp/q2aas/`. The unit tests cover scoring, invalid-BSP exclusion, conversion selection, and Markdown status rendering. | `python -m py_compile tools\q2aas\discover_reference_candidates.py tools\q2aas\test_discover_reference_candidates.py`; `python -m unittest tools.q2aas.test_discover_reference_candidates`; scratch candidate scans at `.tmp\q2aas\reference-candidates*.json`. |
| `tools/q2aas/audit_worr_q2aas_stage.py` | WORR-native staged AAS audit helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Reads `.tmp/q2aas/stage-report.json`, verifies staged `.aas` files live under `.install/basew/maps/`, checks extension, existence, non-zero size, staged-output hash, and generated scratch AAS hash, and writes `.tmp/q2aas/stage-audit-report.json`. | `python -m py_compile tools\q2aas\audit_worr_q2aas_stage.py`; `meson compile -C builddir-win q2aas-stage-audit`. |
| `tools/q2aas/audit_worr_q2aas_package.py` | WORR-native q2aas package-readiness audit helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Reads `.tmp/q2aas/stage-report.json`, verifies each staged AAS has a valid loose `.install/basew/` representation or a matching `pak0.pkz` archive member, supports loose-or-archive and archive-required policies, and writes package audit reports under `.tmp/q2aas/`. | `python -m py_compile tools\q2aas\audit_worr_q2aas_package.py`; `meson compile -C builddir-win q2aas-package-audit`; `meson compile -C builddir-win q2aas-package-archive-audit`. |
| `tools/q2aas/package_worr_q2aas_archive.py` | WORR-native q2aas AAS package archive helper | N/A | Native implementation | WORR project license | Local script comments/docstring. | WORR contributors | Reads `.tmp/q2aas/stage-report.json`, verifies staged AAS hashes, injects generated `maps/<map>.aas` members into `.install/basew/pak0.pkz`, and writes `.tmp/q2aas/package-archive-report.json`. | `python -m py_compile tools\q2aas\package_worr_q2aas_archive.py`; `meson compile -C builddir-win q2aas-package-aas`; `meson compile -C builddir-win q2aas-package-archive-audit`. |
| `tools/refresh_install.py` | WORR-native local install refresh helper with q2aas packaging integration | N/A | Native implementation | WORR project license | Local script comments/CLI help. | WORR contributors | Adds opt-in `--package-q2aas-aas` support that runs q2aas archive packaging and archive-required audit after `pak0.pkz` is rebuilt from `assets/`, preserving generated AAS members through `.install` refreshes. When platform validation is requested, it derives required packaged AAS member names and hashes from the q2aas stage report and passes them to the generic staged-release validator. | `python -m py_compile tools\refresh_install.py`; `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json --q2aas-package-audit-report .tmp\q2aas\refresh-package-archive-audit-report.json`. |
| `tools/release/validate_stage.py` | WORR-native staged release validator with archive member checks | N/A | Native implementation | WORR project license | Local script comments/CLI help. | WORR contributors | Adds generic `--required-archive-member MEMBER[=SHA256]` validation for members inside the configured base game package archive. This supports q2aas packaged AAS release checks without making the release validator q2aas-specific. | `python -m py_compile tools\release\validate_stage.py`; `python tools\release\validate_stage.py --install-dir .install --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64 --required-archive-member maps/mm-rage.aas=6459585e3c15eaa4170e23ca7465fc8255bd95b9b59d42e8615c39a67b707f9c`. |
| `tools/q2aas/README.WORR.md` | WORR-native vendor note | N/A | Native documentation | WORR project license | Local documentation. | WORR contributors | Records snapshot, build targets, validation commands, reference-candidate discovery, manifest-matrix behavior, manifest schema validation, archive manifest guardrails, automated malformed-manifest smoke coverage, deterministic metadata sidecars, entity/content diagnostics, enforced diagnostic gates, manifest baseline regression gates, archive-backed map validation, packaged-map smoke, validated AAS staging, staged AAS audit, package-readiness audit, AAS archive packaging, refresh-install q2aas packaging integration, generic staged-release archive member validation, first Q2 reachability bridge status, manifest-row-owned strictness gates, team-objective and campaign-progression diagnostics, optional `q2dm7` slime reference coverage, optional `fact2` lava/runtime hazard coverage, and credit-maintenance expectations beside the vendored source. | Reviewed with this ledger update. |
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

## Native Validation Update: Duel Queue Spectator Proof

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

- WORR native files `inc/shared/bot_team_policy_status.h`, `src/game/sgame/g_local.hpp`, `src/game/sgame/gameplay/g_svcmds.cpp`, `src/game/sgame/player/p_client.cpp`, `src/server/main.c`, and `tools/bot_scenarios/*` now expose a queue-enabled Duel team-policy smoke proof. The server smoke enables `g_allow_duel_queue`, bot initial team assignment routes queue-capable surplus Duel bots through the normal `SetTeam` path, the game-side status extension verifies `queued=1` for the surplus spectator bot, and the promoted `duel_queue_spectator` scenario preserves the proof in the implemented suite.
- No new Q3A, BSPC, or q2proto files were imported or modified. This is WORR-owned validation and harness code layered over the existing bot team-policy and scenario surfaces.
- Validation: `meson compile -C builddir-win`; `refresh_install.py`; focused `duel_queue_spectator`; full implemented scenario suite passed for that round.
- Implementation log: `docs-dev/q3a-botlib-duel-queue-spectator-2026-06-21.md`.

## Native Validation Update: Q2AAS Policy Semantics

Date: 2026-06-21

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

- WORR native files `tools/q2aas/validate_worr_q2aas.py`, `tools/q2aas/README.WORR.md`, and the q2aas planning docs now record generator scope, player presence, Q2 content/surface semantics, and BSPX tolerance as validation-report evidence. The report keeps WORR's supported q2aas path Q2 `IBSP` version 38 only, while inherited Q1/HL/Sin/Q3 BSPC loaders remain compiled compatibility code isolated by strict validation.
- No new Q3A, BSPC, or q2proto files were imported or modified. This round edited only WORR-owned validation/documentation files, so no additional imported-file `Modified for WORR` source notes were required.
- Validation: `python -m py_compile tools\q2aas\validate_worr_q2aas.py`; final q2aas staged smoke and install-refresh validation are recorded in the implementation log for this round.
- Implementation log: `docs-dev/q2aas-generator-policy-semantics-closeout-2026-06-21.md`.

## Native Validation Update: Q2AAS Reachability Metadata

Date: 2026-06-21

Tasks: `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

- WORR native files `tools/q2aas/validate_worr_q2aas.py`, `tools/q2aas/validation_manifest.json`, `tools/q2aas/test_validate_worr_q2aas.py`, `tools/q2aas/README.WORR.md`, and the q2aas planning docs now record reachability policy diagnostics, mover route reports, deterministic metadata packaging policy, and optional `q2dm1` structural/travel-count baselines when a local Quake II BSP is staged.
- WORR native q2aas validation now also records optional local baselines for `q2dm2`, `q2dm8`, `q2ctf1`, `base1`, `base2`, and `train`, plus CTF team-objective reachability, campaign progression diagnostics, water-backed liquid coverage, and explicit crouch/slime/lava feature-candidate gaps. The same round packages/audits eight generated AAS archive members and documents current Q3A AAS runtime `Trace` / `EntityTrace` ownership without importing new upstream source.
- The local validation input `E:\Games\Quake2\baseq2\maps\q2dm1.bsp` was copied into `.install\basew\maps\q2dm1.bsp` for this round only. The BSP remains user-local validation input and is not imported into the WORR source tree.
- No new Q3A, BSPC, or q2proto files were imported or modified. This round edited only WORR-owned validation, manifest, and documentation files, so no additional imported-file `Modified for WORR` source notes were required.
- Validation: `python -m py_compile tools\q2aas\validate_worr_q2aas.py tools\q2aas\test_validate_worr_q2aas.py`; `python -m unittest tools.q2aas.test_validate_worr_q2aas`; `meson compile -C builddir-win q2aas-staged-smoke`; `meson compile -C builddir-win q2aas-stage-aas`; `refresh_install.py --package-q2aas-aas`.
- Implementation log: `docs-dev/q2aas-generator-reachability-metadata-round-2026-06-21.md`.

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

## Native Bridge Update: Nav Route Target Stabilization

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T05`, `FR-04-T14`, `DV-07-T06`

- WORR native files `src/game/sgame/bots/bot_nav.*` now stabilize refreshed route targets by promoting a farther sampled route point when a new route returns a near-origin `moveTarget`.
- The route status records stabilization checks, applications, skips, source/stable target distances, and the sampled route-point index used for future scenario/status surfacing.
- No new upstream source files were imported for this slice; this is WORR-native smoothing over already imported Q3A AAS route-query output exposed through `botlib_adapter.*`.
- Validation: `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_nav.cpp.obj` passed, with the existing shared-build-dir warning `premature end of file; recovering`.
- Implementation log: `docs-dev/q3a-botlib-route-target-stabilization-2026-06-18.md`.

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

## Native Release Update: q2aas Binary Distribution Policy

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T13`, `FR-04-T16`, `DV-08-T05`, `DV-07-T06`

- WORR native release packaging now encodes the policy that q2aas/BSPC tool binaries are not distributed by default. `tools/package_assets.py` rejects `worr_q2aas` / `q2aas` / `bspc` executable, library, or debug-symbol artifacts in the asset pack, and `tools/refresh_install.py` fails if those artifacts appear in the staged `.install` tree.
- `tools/refresh_install.py` now stages a required `.install/licenses/` bundle containing the WORR license, the vendored BSPC license, the Q3A/BSPC credit ledger, and the WORR vendor notes for q2aas and Q3A BotLib imports.
- `tools/release/targets.py` requires that notice bundle in client, server, and update package manifests while forbidding q2aas/BSPC tool artifacts; `tools/release/verify_artifacts.py` also rejects required notice files that are missing or zero-byte in the manifest.
- No new upstream source files, q2aas validation files, q2aas inventory files, or `q2proto/` files were changed for this slice; this is release packaging policy and notice validation only.
- Validation: Python compile passed for `tools\package_assets.py`, `tools\refresh_install.py`, `tools\test_package_assets.py`, `tools\release\targets.py`, `tools\release\verify_artifacts.py`, and `tools\release\tests\test_target_contract.py`; `python tools\test_package_assets.py -v` passed 11 tests; `python -m unittest tools.release.tests.test_target_contract -v` passed 5 tests.
- Implementation log: `docs-dev/q3a-botlib-release-policy-2026-06-18.md`.

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

## Native Bridge Update: Weapon and Inventory Dispatch Wiring

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_brain.cpp` now consumes accepted pending weapon/inventory action results, asks `BotActions_BuildCommandRequest()` for the validated exact request, and records dispatch outcomes.
- Exact `UseWeaponIndex` and `UseInventoryIndex` requests are submitted through the existing item `use` callback boundary only after client, item-index, usability, and inventory-count validation.
- Weapon dispatch records whether the requested weapon became current or pending, preserving the proof flow while avoiding the removed Q2R `Bot_UseItem` export callback.
- No new upstream source files or copied Q3A behavior code were imported for this slice; this is a local dispatcher integration over the existing WORR action boundary.
- Validation: focused object build passed for `bot_actions.cpp` and `bot_brain.cpp`; `meson compile -C builddir-win sgame_x86_64` linked successfully.
- Implementation log: `docs-dev/q3a-botlib-weapon-inventory-dispatch-2026-06-18.md`.

## Native Bridge Update: Aim Fairness Policy Helpers

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_combat.*` now exposes an opt-in aim/fairness policy helper for future brain-owned aiming.
- The helper gates aim/fire permission on enemy state, visibility, shootability, weapon readiness, skill, reaction delay, FOV, yaw/pitch turn limits, aim-settle time, burst cooldown, and burst limits while returning aim-error and tracking-noise metadata.
- Existing combat behavior is unchanged unless a caller enables the policy. The helper does not mutate view angles, submit attacks, or implement autonomous firing.
- No new upstream source files or copied Q3A behavior code were imported for this slice; this is WORR-native fairness metadata and policy scaffolding.
- Validation: focused `bot_combat.cpp` object build passed; no local lightweight unit-test pattern exists for these helper structs.
- Implementation log: `docs-dev/q3a-botlib-aim-fairness-policy-2026-06-18.md`.

## Native Bridge Update: Special Item Utility Buckets

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_items.*` now classifies special pickup utility separately from broad item utility.
- The helper distinguishes damage boosts, protection, invisibility, mobility, utility powerups, techs, and CTF objective pickups while avoiding ordinary pickup value for already-active powerups or already-held techs.
- This does not change route ownership, item reservation, inventory execution, command dispatch, or item timer fairness policy.
- No new upstream source files or copied Q3A item behavior code were imported for this slice; this is local item-utility scoring scaffolding.
- Validation: focused `bot_items.cpp` object build passed.
- Implementation log: `docs-dev/q3a-botlib-special-item-utility-2026-06-18.md`.

## Native Bridge Update: Team Role Policy Helpers

Date: 2026-06-18

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_objectives.*` now adds deterministic returner/support role policy helpers above the existing objective proof layer, while preserving explicit requested roles for the current smoke paths.
- `src/game/sgame/bots/bot_brain.cpp` now emits the new role-policy evaluation, selection, fallback, and priority-breakdown counters on `q3a_bot_objective_status`.
- No new upstream source files or copied Q3A team behavior were imported for this slice; this is local policy scaffolding for future autonomous CTF/TDM role consumption.
- Validation: `meson compile -C builddir-win sgame_x86_64` passed after the integration fix; `python tools\bot_scenarios\test_run_bot_scenarios.py` passed 23 tests.
- Implementation log: `docs-dev/q3a-botlib-team-role-policy-2026-06-18.md`.

## Native Bridge Update: Team Role Depth Policy

Date: 2026-06-18

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_objectives.*` now adds lane/depth metadata beside objective roles for attack, defense, midfield, carrier support, dropped-flag response, and own-base return.
- The helper remains a pure priority table over caller-provided target facts and exposes lane names, lane priorities, per-lane breakdowns, and latest lane status facts for future brain integration.
- No `bot_brain.cpp` integration was added in this lane, and autonomous CTF/TDM team behavior remains pending.
- No new upstream source files or copied Q3A team behavior were imported for this slice; this is local role-policy scaffolding.
- Validation: `meson compile -C builddir-win sgame_x86_64`, `python tools\refresh_install.py --build-dir builddir-win`, `python tools\bot_scenarios\test_run_bot_scenarios.py`, and a focused `team_objective` scenario run passed.
- Implementation log: `docs-dev/q3a-botlib-team-role-depth-2026-06-18.md`.

## Native Bridge Update: AAS Memory Source Counters

Date: 2026-06-18

Tasks: `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `FR-04-T12`

- WORR native source-counter status now includes the existing BotLib zone/hunk allocator active and peak byte counters on `q3a_bot_source_counter_status`, alongside route, visibility, trace, and CPU fields.
- The current status surface records `q3a_memory_zone_active`, `q3a_memory_zone_peak`, `q3a_memory_hunk_active`, `q3a_memory_hunk_peak`, `q3a_memory_total_active`, `q3a_memory_total_peak`, `q3a_memory_failures`, and `q3a_memory_available`.
- No new upstream source files were imported for this slice; this is local status emission over allocator counters already owned by the WORR BotLib adapter boundary.
- Validation: final round validation rebuilt `sgame_x86_64` and `worr_ded_engine_x86_64`, refreshed `.install`, reran the 9/9 implemented bot scenario suite, passed the bot perf analyzer tests, and confirmed the latest `engage_enemy` parse reports the `q3a_memory` source-counter group with no missing source-counter groups.
- Implementation log: `docs-dev/q3a-botlib-aas-memory-source-counters-2026-06-18.md`.

## Native Tooling Update: Status Harness Expansion

Date: 2026-06-18

Tasks: `DV-03-T05`, `DV-05-T02`, `FR-04-T16`, `DV-07-T06`

- WORR native scenario tooling now discovers optional status fields for action dispatch, aim policy, special item utility, and route-target stabilization without turning them into hard scenario gates.
- `tools/bot_scenarios/run_bot_scenarios.py` parses optional marker fields from action, blackboard, and selected frame-command status rows, and carries discoveries into text, JSON, Markdown, and pending-gap reports.
- No new upstream source files or copied behavior code were imported for this slice; this is local validation/reporting support for parallel runtime lanes.
- Validation: `python tools\bot_scenarios\test_run_bot_scenarios.py` passed 25 tests; Python compile passed for the scenario harness and tests.
- Implementation log: `docs-dev/q3a-botlib-status-harness-expansion-2026-06-18.md`.

## Native Bridge Update: Live Aim and Projectile Lead Helpers

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_combat.*` now exposes pure helper APIs for direct-projectile lead calculation and a combined live-aim decision over an existing combat context.
- The helper combines aim-policy results with projectile lead hints for direct projectile weapons and reports whether a caller may aim, may fire, should press attack, and which aim point should be used.
- This does not mutate `usercmd_t`, view angles, burst state, blackboard state, or weapon state; brain-owned live aim/firing consumption remains future work.
- No new upstream source files or copied Q3A behavior code were imported for this slice; this is WORR-native combat policy scaffolding over the existing local action/combat boundary.
- Validation: focused object builds passed for `bot_combat.cpp`, `bot_actions.cpp`, and `bot_brain.cpp`; full `sgame_x86_64.dll` linking was blocked at the time by a concurrent unrelated `bot_nav.cpp` signature mismatch.
- Implementation log: `docs-dev/q3a-botlib-live-aim-policy-integration-2026-06-18.md`.

## Native Bridge Update: Item Timer Fairness Policy

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T07`, `FR-04-T15`, `DV-07-T06`

- WORR native `src/game/sgame/bots/bot_items.*` now exposes `BotItems_EvaluatePickupTimingPolicy()` so bot item timing knowledge can be disabled or deterministically fuzzed before a caller uses it.
- Public cvars `sg_bot_allow_item_timers` and `sg_bot_item_timer_fuzz_ms` are read lazily by the helper when no explicit policy override is supplied.
- The current runtime application is intentionally narrow: existing self-owned powerup timer checks use the policy result. Hidden respawn scheduling, broad pickup timing consumers, and status-line emission remain follow-up work.
- No new upstream source files or copied Q3A item behavior code were imported for this slice; this is local fairness policy over existing WORR item helper state.
- Validation: focused `bot_items.cpp` object build passed.
- Implementation log: `docs-dev/q3a-botlib-item-timer-fairness-2026-06-18.md`.

## Native Bridge Update: FFA/TDM/CTF Objective-Side Policy Helpers

Date: 2026-06-18

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `DV-03-T05`

- WORR native `src/game/sgame/bots/bot_objectives.*` now exposes match-policy, item-role, and friendly-fire helper metadata for FFA, TDM, and CTF role shape.
- The helpers report scoring participation, roam/collect/engage intent, attack/defense/midfield lanes, item-role splits, and friendly-fire avoidance recommendations without tracing, suppressing fire, or changing live bot command ownership.
- Existing CTF target-specific helper work still owns enemy-flag, own-flag-return, carrier-support, dropped-flag, and base-return proof paths. Default-off TDM and CTF role/lane route consumers are now WORR-owned; broader autonomous FFA/TDM/CTF combat and objective role consumption remains future work.
- No new upstream source files or copied Q3A team behavior were imported for this slice; this is WORR-native objective policy scaffolding.
- Validation: `meson compile -C builddir-win sgame_x86_64`, `python tools\refresh_install.py --build-dir builddir-win`, and `python tools\bot_scenarios\test_run_bot_scenarios.py` passed.
- Implementation log: `docs-dev/q3a-botlib-ffa-tdm-role-policy-2026-06-18.md`.

## Native Bridge Update: Trace-Checked Corner Cutting

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T05`, `FR-04-T14`

- WORR native `src/game/sgame/bots/bot_nav.*` now evaluates farther sampled route points after successful route refresh and promotes a shortcut only after a clean player-hull server trace.
- Walk/crouch shortcuts also require downward ground probes, and accepted shortcuts only adjust the local steering payload; route ownership, goal areas, item reservations, and reachability metadata are unchanged.
- Route-target stabilization now uses the same trace proof before promoting a farther sampled point. Corner-cut status counters exist in nav route status, but brain-owned status output and scenario hard gates remain follow-up work.
- No new upstream source files or copied Q3A navigation code were imported for this slice; this is WORR-native steering safety around existing BotLib route samples.
- Validation: focused `bot_nav.cpp` object build passed; `meson compile -C builddir-win sgame_x86_64` passed; install refresh with `--package-q2aas-aas` passed; a dedicated `mm-rage` frame-command smoke passed with zero route failures.
- Implementation log: `docs-dev/q3a-botlib-trace-checked-corner-cutting-2026-06-18.md`.

## Native Tooling Update: Available Reference Map Validation

Date: 2026-06-18

Tasks: `FR-04-T11`, `FR-04-T14`, `FR-04-T16`, `DV-07-T06`

- WORR native AAS inventory tooling now distinguishes manifest placeholders from actually staged BSP/AAS assets, discovers feature candidates from available BSPs, and can write a focused validation manifest for currently available reference maps.
- The current workspace selects `mm-rage` as the only runtime-ready available reference map; focused q2aas validation passes strict reference coverage for that subset while broad deathmatch, CTF, campaign/coop, liquid, teleport, and door candidates remain data dependencies.
- No new upstream source files, q2proto files, or server-game bot code were changed for this slice; this is local validation tooling and report generation over the existing q2aas manifest.
- Validation: AAS inventory unit tests passed 5 tests; q2aas manifest unit tests passed 3 tests; inventory exited `0`; focused q2aas validation exited `0` and wrote `.tmp/q2aas/available-reference-validation-report.json`.
- Implementation log: `docs-dev/q2aas-reference-map-validation-expansion-2026-06-18.md`.

## Native Tooling Update: Scenario Coverage Expansion

Date: 2026-06-18

Tasks: `DV-03-T05`, `FR-04-T03`, `FR-04-T04`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

- WORR native scenario tooling now parses detailed action, objective, nav, team-policy, natural-support, and interaction-context status markers and adds a `team_policy_duel_readiness` implemented scenario row over the existing team-policy smoke path.
- Implemented combat/item/objective scenario checks are stricter, while unfinished aim integration, item timer status, trace-checked corner-cut surfacing, FFA/TDM readiness, and coop readiness stay as pending or optional contracts instead of hard pass/fail gates.
- No new upstream source files or copied behavior code were imported for this slice; this is local scenario validation and report plumbing over existing runtime markers.
- Validation: Python compile passed for the scenario harness and tests; `python tools\bot_scenarios\test_run_bot_scenarios.py` passed 26 tests.
- Implementation log: `docs-dev/q3a-botlib-scenario-coverage-expansion-2026-06-18.md`.

## Native Asset and Validation Update: Botfile/Profile Parity

Date: 2026-06-18

Tasks: `FR-04-T01`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `FR-04-T16`, `DV-07-T04`, `DV-07-T06`

- WORR-authored botfile assets now have additional Q3/Gladiator-style parity polish for shared team chat coverage, Q2 utility/coop inventory symbols, shared item/weapon weights, role-specific `_i.c` weights, role-specific chat events, and script `wave(...)` coverage.
- A follow-up pass brings `teamplay.h` to parity with the Q3A shared teamplay event-name surface using original WORR responses, and profile validation now checks behavior metadata labels and warns on incomplete packaged Q3-style behavior families.
- The current profile loader still consumes `_c.c` character entries and skips companion files as staged data. Companion weights, chat, and scripts are parity-ready assets for later runtime consumers, not proof that every event is emitted today.
- No Q3A or Gladiator botfile text was copied; reference assets were used for layout, vocabulary, and event-name shape only.
- Validation: profile validation passed for 5 profiles with 0 errors and 0 warnings; profile pytest coverage passed 18 tests; package asset tests passed 11 tests where run; botfile brace/family/parity checks passed in the lane notes.
- Implementation logs: `docs-dev/q3a-botlib-profile-behavior-validation-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-parity-polish-2026-06-18.md`, and `docs-dev/q3a-botlib-botfiles-q3a-parity-followup-2026-06-18.md`.

## Native Docs Update: Public Bot Cvars and User Guides

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T07`, `FR-04-T11`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-07-T04`, `DV-07-T06`

- User-facing bot docs now cover practical bot setup/debug cvars, packaged and loose botfile locations, current AAS/map readiness limits, available-reference validation output, and the item timer controls `sg_bot_allow_item_timers` and `sg_bot_item_timer_fuzz_ms`.
- The docs describe current helper/control scope in operator language and avoid claiming broader respawn-timer behavior or full map coverage beyond the available `mm-rage` reference subset.
- No runtime source, q2proto files, or upstream imports were changed for this docs lane.
- Validation: docs whitespace checks and targeted text searches passed in the lane notes.
- Implementation logs: `docs-dev/q3a-botlib-user-docs-round-2026-06-18.md`, `docs-dev/q3a-botlib-public-cvar-docs-round-2026-06-18.md`, and `docs-dev/q3a-botlib-bot-map-readiness-docs-2026-06-18.md`.

## Native Status Update: Status Surface Integration

Date: 2026-06-18

Tasks: `FR-04-T12`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

- The current status surface can expose optional action-dispatch, aim-policy, special-item utility, route-target stabilization, and objective lane/depth fields through existing status markers.
- These fields are optional evidence for scenario/reporting tools. They should become hard scenario gates only after stable runtime logs prove they come from live behavior, not helper scaffolding alone.
- No new upstream source files or copied behavior code were imported for this docs/status note.
- Implementation log: `docs-dev/q3a-botlib-status-surface-integration-2026-06-18.md`.

## Native Tooling and Runtime Update: Final Scenario Promotion Wave

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T04`, `FR-04-T13`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`, `DV-08-T05`

- WORR-native server smoke modes `24`, `25`, and `26` now drive aim/fairness, item-timer, and FFA/TDM match-readiness proof lanes. The existing mode `21` route-rich smoke now gates trace-checked corner cutting, and the existing mode `3` frame-command smoke gates coop readiness under cooperative cvars.
- WORR-native `bot_brain.*` status/proof plumbing records aim-policy, item-timer, match-readiness, route-owner, FFA spawn-camp avoidance, friendly-fire, TDM role-combat, TDM role-combat/friendly-fire precedence, CTF role-route, CTF role-combat, CTF dropped-flag route, CTF carrier-support route, CTF base-return route, and CTF objective-route proof fields through the existing action/detail/match/frame-command status surfaces.
- WORR-native `tools/bot_scenarios/` now treats `aim_fairness_policy_integration`, `item_timer_fairness_signals`, `trace_checked_corner_cutting`, `ffa_tdm_match_readiness`, `ffa_roam_route`, `ffa_spawn_camp_avoidance`, `team_role_route`, `team_item_roles`, `team_fire_avoidance`, `team_role_combat`, `team_role_combat_avoidance`, `ctf_role_route`, `ctf_role_combat`, `ctf_dropped_flag_route`, `ctf_carrier_support_route`, `ctf_base_return_route`, `ctf_objective_route`, `ctf_objective_route_precedence`, `coop_match_readiness`, `coop_leader_route`, `coop_lead_advance`, `coop_resource_share`, `coop_anti_blocking`, `coop_target_share`, `coop_door_elevator`, `coop_progress_wait`, and `coop_interaction_retry` as implemented rows. The then-current default implemented suite passed with no failed, timed-out, errored, or pending rows from `.tmp/bot_scenarios/latest_report.json`.
- First-party WORR botfile scripts under `assets/botfiles/scripts/*_s.c` gained additive named tactical routines. Q3A and Gladiator assets were consulted for layout/vocabulary parity, but no reference script text was copied.
- No new upstream Q3A, Gladiator, or BSPC source files were imported for this update.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py` passed; `meson compile -C builddir-win` passed; `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --package-q2aas-aas` passed; focused promotion and full implemented scenario runs passed from the refreshed `.install` payload.
- Implementation logs: `docs-dev/q3a-botlib-docs-progress-tracking-round-2026-06-18.md`, `docs-dev/q3a-botlib-extensive-implementation-round-2026-06-18.md`, and `docs-dev/botfiles/q3a-botfile-script-tactical-routines-2026-06-18.md`.

## Native Tracking Update: Extensive Worker Closeout

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T04`, `FR-04-T11`, `FR-04-T13`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T05`, `DV-07-T06`

- WORR-native combat work now records live aim-profile state, projectile-lead scaling, and brain-owned live-aim consumption. No Q3A combat behavior code was copied.
- WORR-native item work now has live pickup/observed-respawn timing consumer frames/results and conservative timing gates, with broader timed route ownership still pending.
- WORR-native objective work now records coop follow/wait/regroup/lead/support helper metadata and resource policy helper metadata, without claiming autonomous coop commands.
- WORR-native validation work now reports required-feature reference-map coverage/gap diagnostics and long-soak source-counter completeness diagnostics.
- WORR-authored botfiles now carry richer behavior metadata validated by the local profile tooling. Q3A and Gladiator assets remain style references only; no reference botfile text was copied.
- Scenario evidence checks were tightened around live-aim and match-policy markers. This is local harness logic over WORR status lines, not upstream BotLib import work.
- No new upstream Q3A, Gladiator, BSPC, or q2proto source files were imported or modified for this closeout.
- Implementation logs: `docs-dev/q3a-botlib-live-combat-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-live-item-timing-consumers-2026-06-18.md`, `docs-dev/q3a-botlib-team-coop-policy-round-2026-06-18.md`, `docs-dev/q2aas-reference-map-coverage-round-2026-06-18.md`, `docs-dev/q3a-botlib-long-soak-source-counter-round-2026-06-18.md`, `docs-dev/q3a-botlib-profile-behavior-depth-round-2026-06-18.md`, and `docs-dev/q3a-botlib-extensive-round-closeout-2026-06-18.md`.

## Native Runtime Update: Enemy Health and Armor Estimates

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native combat/blackboard work now records per-bot enemy health and armor
  estimates from visible observations and split bot-attributed damage deltas.
- The real WORR `Damage()` path supplies already-computed health and armor
  damage to the existing bot-attributed combat hook; no Q3A damage or combat
  behavior code was copied.
- Status markers expose the estimate state for weapon-selection and
  inventory-policy consumers.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-enemy-health-armor-estimates-2026-06-20.md`.

## Native Runtime Update: Estimate-Aware Weapon Selection

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native combat work now consumes the local enemy health/armor estimates in
  current-vs-preferred weapon scoring for finisher, armor-pressure, and
  underpowered-choice adjustments.
- Preferred-weapon switch decisions now follow the selected score result so the
  scorer can keep a suitable current weapon instead of always chasing a pending
  preferred weapon.
- Compact action status exposes estimate-use counters and the last selected
  estimate adjustment for scenario/tooling visibility.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-estimate-aware-weapon-selection-2026-06-20.md`.

## Native Runtime Update: Carried Arsenal Selection

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native action work now scans carried weapon inventory after enemy facts
  are enriched and feeds the best scorer-approved candidate into combat as the
  preferred switch target.
- The scanner defers to existing pending weapon switches and keeps range, ammo,
  readiness, splash safety, and estimate-aware scoring centralized in
  `bot_combat.*`.
- Compact action status exposes weapon-inventory scan, candidate, selection,
  and last-selection reason fields for scenario/tooling visibility.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-carried-arsenal-selection-2026-06-20.md`.

## Native Runtime Update: Non-Weapon Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native action work now scans carried usable non-weapon inventory after
  enemy facts and carried-arsenal enrichment.
- The policy selects conservative combat/survival uses for damage boosts,
  protection, invisibility, mobility, regeneration, and power armor while
  deferring active timed effects, spheres/deployables, environment-only utility,
  weapon items, already-active power armor, and power armor without cells.
- Compact and detailed action status expose inventory-policy scan, candidate,
  selection, deferral, last-item, priority, score, special-kind, and reason
  fields for scenario/tooling visibility.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-nonweapon-inventory-policy-2026-06-20.md`.

## Native Runtime Update: Utility and Deployable Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native action work now extends carried non-weapon inventory use to
  environment utility and sphere deployable cases.
- Enviro suit/rebreather respond to actual hazard or underwater pressure,
  IR goggles and silencer respond to narrow combat utility cases, and
  defender/hunter/vengeance spheres launch only under combat/survival pressure.
- Action status exposes utility, environment, deployable, and owned-sphere
  deferral counters for scenario/tooling visibility.
- This earlier utility/sphere round did not enable nuke, doppelganger, or
  personal teleporter; the follow-on escape/deployable round adds doppelganger
  placement and teleporter escape checks while keeping nuke deferred.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-utility-deployable-inventory-policy-2026-06-20.md`.

## Native Runtime Update: Escape and Deployable Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native action work now adds placement-aware doppelganger use and
  last-resort personal teleporter escape use to the carried inventory policy.
- Doppelganger scoring reuses the same gameplay spawn and ground-support
  helpers as the item callback before requesting the item.
- Personal teleporter scoring requires deathmatch inventory semantics, avoids
  CTF objective carriers, and only triggers under critical health plus immediate
  enemy or hazard pressure.
- Action status exposes escape-use, placement-check, placement-deferral, and
  nuke-deferral counters for scenario/tooling visibility.
- This escape/deployable round still deferred nuke; the follow-on safe-nuke
  round adds friendly-fire, blast-radius, objective, self-pressure, launch, and
  enemy-value checks.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-escape-deployable-inventory-policy-2026-06-20.md`.

## Native Runtime Update: Safe Nuke Inventory Policy

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native action work now turns nuke from an unconditional deferral into a
  conservative safety-gated combat utility option.
- The policy requires deathmatch inventory semantics, avoids CTF objective
  carriers, requires actionable enemy pressure, checks target distance and enemy
  value, rejects low self-resource or hazard pressure, checks launch clearance,
  and avoids live teammate exposure inside the nuke falloff-risk radius.
- Action status exposes nuke safety-check, friendly-deferral, self-deferral,
  use, and general deferral counters for scenario/tooling visibility.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-safe-nuke-inventory-policy-2026-06-20.md`.

## Native Runtime Update: Nuke Retreat Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native brain/route work now arms a short-lived retreat route owner after
  a submitted safe nuke inventory command.
- The retreat source prefers the bot blackboard's remembered enemy and falls
  back to the launch direction when no live remembered enemy is available.
- Active retreat state overlays a temporary position route goal and exposes
  `nuke_retreat_*` plus `last_nuke_retreat_*` frame-command status fields for
  scenario/tooling visibility.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-nuke-retreat-route-ownership-2026-06-21.md`.

## Native Runtime Update: Timed Route Goal Owner

Date: 2026-06-21

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native brain/route work now generalizes the first nuke retreat route
  state into a reusable timed route-goal owner.
- The owner records kind, duration, source, fallback direction, current goal,
  route request, deferral, expiration, invalid-skip, and last-owner metadata.
- Nuke retreat, teleporter escape, and coop leader routing now consume the
  owner while future tactical/team/coop behaviors can attach additional timed
  route-goal kinds without adding another bespoke per-slot route overlay.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-timed-route-goal-owner-2026-06-21.md`.

## Native Runtime Update: Teleporter Escape Route Owner

Date: 2026-06-21

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

- WORR-native brain/route work now attaches last-resort personal teleporter
  escape use to the generic timed route-goal owner.
- A submitted `IT_TELEPORTER` inventory command arms a short escape route using
  remembered enemy pressure, then recent damage origin, then a view-direction
  fallback source.
- Frame-command status now reports `teleporter_escape_*` counters for
  activations, fallback source use, damage-source use, and invalid skips.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-teleporter-escape-route-owner-2026-06-21.md`.

## Native Runtime Update: Coop Leader Route Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-07-T06`

- WORR-native brain/route work now attaches coop follow, regroup, and
  support-combat leader policy to the generic timed route-goal owner.
- Follow/regroup intents synthesize a source behind the bot on the leader vector
  so the shared owner routes toward the selected leader.
- Support-combat intent uses the leader as the source for a short spacing route.
- Active nuke retreat and teleporter escape owners remain higher priority and
  are not overwritten by coop leader routing.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-leader-route-owner-2026-06-21.md`.

## Native Validation Update: Coop Leader Route Scenario Gate

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native frame-command status now mirrors `coop_leader_route_*` and
  `last_coop_leader_route_*` counters on the compact
  `q3a_bot_coop_command_status` marker.
- Scenario tooling now promotes `coop_leader_route` as a mode `3` coop reuse
  row that hard-gates leader-route activation, refresh, support-spacing source
  generation, valid coop intent evidence, and the `coop_leader` timed route
  owner kind.
- Optional-field discovery recognizes coop leader route counters from both the
  verbose frame-command status marker and compact coop command marker.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`.

## Native Runtime Update: Coop LeadAdvance Route Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain/objective work now adds a default-off
  `sg_bot_coop_lead_advance` bridge for no-leader coop LeadAdvance policy.
- Valid LeadAdvance policy arms a short `coop_lead_advance` timed route-goal
  owner that routes from a source behind the bot toward the bot's current
  advance direction.
- Server smoke mode `27` runs a one-bot coop proof, and compact coop-command
  status reports `coop_lead_advance_*` and `last_coop_lead_advance_*` counters
  for policy requests, activations, route requests, deferrals, invalid skips,
  and last intent metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`.

## Native Runtime Update: Coop Progress Wait Command Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-07-T06`

- WORR-native brain/objective work now adds a default-off
  `sg_bot_coop_progress_wait` bridge that requests coop WaitForLeader policy
  during frame-command evaluation.
- Valid WaitForLeader policy owns a stop-and-face command by clearing
  forward/side movement, jump, and crouch movement-state buttons while facing
  the selected leader.
- Compact coop-command status and scenario tooling now report
  `coop_progress_wait_*` counters and a promoted `coop_progress_wait` smoke
  row; short proof rows are emitted before verbose diagnostics so no new
  upstream source is needed for the validation surface.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-progress-wait-command-2026-06-21.md`.

## Native Runtime Update: Coop Interaction Retry Command Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-07-T06`

- WORR-native brain/navigation work now exposes the existing route interaction
  retry activator through a narrow `BotNav_RequestInteractionRetry(...)`
  wrapper.
- A default-off `sg_bot_coop_interaction_retry` bridge lets coop frame commands
  request detected route interaction retries from the current route and records
  only those cvar-gated requests through `coop_interaction_retry_*` counters.
- The promoted `coop_interaction_retry` smoke row reuses the elevator route
  proof to validate wait/use command ownership plus interaction-context
  telemetry.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`.

## Native Runtime Update: Coop Resource Share Route Selection

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native navigation/objective work now adds a default-off
  `sg_bot_coop_resource_share` bridge for cooperative resource-sharing policy.
- Item route-goal selection evaluates the existing resource policy before item
  scoring and marks reserve-for-teammate candidates as reserved, allowing
  `BotItems_Evaluate(...)` to defer them and report `item_reserved_deferrals`.
- Server smoke mode `28` runs a two-bot coop proof, and the promoted
  `coop_resource_share` scenario validates coop readiness, resource-share
  policy, reserve-for-teammate policy, route cleanliness, and item-scoring
  deferral evidence.
- Compact and proof action status now expose `item_reserved_deferrals` so this
  gate does not depend on oversized verbose diagnostics.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`.

## Native Runtime Update: Coop Anti-Blocking Command Owner

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain/objective work now adds a default-off
  `sg_bot_coop_anti_blocking` bridge for close-to-leader cooperative spacing.
- Valid close-leader coop policy can own a short backpedal plus sidestep
  command while keeping the bot facing the selected leader.
- The bridge clears jump/crouch movement-state buttons so the proof is a clean
  command-owner behavior rather than a side effect of route travel state.
- Compact coop-command status exposes `coop_anti_block_*` and
  `last_coop_anti_block_*` counters for request, close-policy, command,
  invalid-skip, leader, intent, distance, and movement evidence.
- Server smoke mode `29` runs a two-bot coop proof, and the promoted
  `coop_anti_blocking` scenario validates coop readiness, non-deathmatch match
  readiness, close-policy evidence, command ownership, and support-combat intent
  evidence.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-anti-blocking-command-2026-06-21.md`.

## Native Runtime Update: Coop Target Sharing

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain/perception work now adds a default-off
  `sg_bot_coop_target_share` bridge for cooperative blackboard target adoption.
- Hostile non-client `SVF_MONSTER` entities can act as combat targets for this
  gated path, letting a support-policy bot adopt a teammate's current hostile
  monster target when the source bot, target memory, and coop policy are valid.
- Compact coop-command status exposes `coop_target_share_*` and
  `last_coop_target_share_*` counters for request, policy, source scan,
  source-candidate, adoption, invalid-skip, source-client, target-entity,
  target-client, target-distance, and intent evidence.
- Server smoke mode `30` runs a two-bot coop proof with a lightweight
  proof-only hostile monster target, and the promoted `coop_target_share`
  scenario validates coop readiness, target-share setup, source-candidate
  evidence, adoption evidence, valid target identity, and support-combat intent.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-target-share-2026-06-21.md`.

## Native Runtime Update: Coop Door/Elevator Cooperation

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_coop_door_elevator` bridge for cooperative mover/elevator
  ownership.
- Server smoke mode `31` runs a two-bot coop `TRAVEL_ELEVATOR` proof where
  the selected source bot owns route-detected wait/use interaction commands
  while the teammate holds position through a non-route support command.
- Compact coop-command status exposes `coop_door_elevator_*` and
  `last_coop_door_elevator_*` counters for request, source activation, source
  command, support hold, invalid-skip, client, action, kind, entity, and intent
  evidence.
- The promoted `coop_door_elevator` scenario validates coop readiness,
  elevator activation, source ownership, teammate hold ownership, and mover
  metadata from the refreshed `.install` payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`.

## Native Runtime Update: Team Role Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_team_role_route` bridge for live match role/lane route ownership.
- `BotTimedRouteGoalKind::TeamRole` consumes existing FFA/TDM/CTF match-policy
  output through the generic timed route-goal owner, with conservative
  attack/defense/midfield movement directions.
- Frame-command status exposes `team_role_route_*` and
  `last_team_role_route_*` counters for request, policy-selection, activation,
  refresh, deferral, route request, expiration, invalid-skip, mode, role, lane,
  priority, remaining-time, and route-distance evidence.
- Server smoke mode `32` runs a four-bot TDM proof, and the promoted
  `team_role_route` scenario validates TDM readiness, TDM match-policy
  selection, timed route owner kind `5`, owner activation, route requests, and
  latest role/lane metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`.

## Native Runtime Update: Team Item Role Route Selection

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native nav behavior now adds a default-off
  `sg_bot_team_item_roles` bridge for live TDM item-route candidate scoring.
- `BotNavFindPickupGoal` can consume existing match item-role policy output
  during pickup selection, applying a role-policy score boost before the
  normal distance-penalized best-candidate choice.
- Nav policy status exposes `team_item_role_*` and
  `last_team_item_role_*` counters for evaluation, valid selection, score
  boost, selected pickup goal, mode, role, lane, category, item role, entity,
  item id, and final score evidence.
- Server smoke mode `33` runs a four-bot TDM proof, and the promoted
  `team_item_roles` scenario validates TDM readiness, objective item-role
  policy selection, nav candidate scoring, selected pickup goals, and latest
  role/category metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-team-item-role-selection-2026-06-21.md`.

## Native Runtime Update: FFA Item Role Route Selection

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native nav behavior now adds a default-off
  `sg_bot_ffa_item_roles` bridge for live FFA item-route candidate scoring.
- `BotNavFindPickupGoal` reuses existing match item-role policy output in FFA
  mode, applying role-priority score boosts before normal item utility and
  distance scoring select the pickup goal.
- Nav policy status exposes separate `ffa_item_role_*` and
  `last_ffa_item_role_*` counters for evaluation, selection, score boost,
  selected pickup goal, invalid skips, mode, role, lane, category, item role,
  entity, item id, and final score evidence. Existing TDM
  `team_item_role_*` counters remain separate.
- Compact nav-policy status rows now print item-role and interaction fields
  before the full verbose diagnostic so promoted scenario gates still see the
  required evidence when console output truncates later fields.
- Server smoke mode `46` runs a four-bot FFA proof, and the promoted
  `ffa_item_roles` scenario validates FFA readiness, objective item-role
  policy selection, nav candidate scoring, selected pickup goals, and latest
  role/category/item metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests; `meson compile
  -C builddir-win` passed after rerun; `python tools\refresh_install.py
  --build-dir builddir-win --install-dir .install --package-q2aas-aas` passed;
  and focused `ffa_item_roles` passed from
  `.tmp\bot_scenarios\20260621T173656Z` with `route_commands=246`,
  `route_failures=0`, `item_goal_assignments=24`, `pass=1`, and positive
  `ffa_item_role_*` scoring counters; the full implemented scenario suite
  passed 51 rows from `.tmp\bot_scenarios\20260621T173703Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`.

## Native Runtime Update: CTF Item Role Route Selection

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native nav behavior now adds a default-off
  `sg_bot_ctf_item_roles` bridge for live Capture the Flag item-route
  candidate scoring.
- `BotNavFindPickupGoal` reuses existing match item-role policy output in CTF
  mode, applying role-priority score boosts before normal item utility and
  distance scoring select the pickup goal.
- Nav policy status exposes separate `ctf_item_role_*` and
  `last_ctf_item_role_*` counters for evaluation, selection, score boost,
  selected pickup goal, invalid skips, mode, role, lane, category, item role,
  entity, item id, and final score evidence. Existing FFA and TDM
  item-role counters remain separate.
- Compact CTF nav-policy status rows now print selected-goal and latest-item
  metadata before the full verbose diagnostic so promoted scenario gates still
  see the required evidence when console output truncates later fields.
- Server smoke mode `47` runs a four-bot CTF proof, and the promoted
  `ctf_item_roles` scenario validates CTF readiness, objective item-role
  policy selection, nav candidate scoring, selected pickup goals,
  invalid-skip absence, and latest role/lane/category/item metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests; `meson compile
  -C builddir-win` passed; `meson compile -C builddir-win sgame_x86_64`
  passed after the compact-status adjustment; `python tools\refresh_install.py
  --build-dir builddir-win --install-dir .install --package-q2aas-aas` passed
  after each build; focused `ctf_item_roles` passed from
  `.tmp\bot_scenarios\20260621T175557Z` with `route_commands=246`,
  `route_failures=0`, `item_goal_assignments=17`, `pass=1`, and positive
  `ctf_item_role_*` scoring counters; and the full implemented scenario suite
  passed 52 rows from `.tmp\bot_scenarios\20260621T175605Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`.

## Native Runtime Update: FFA Role Combat Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain behavior now adds a default-off
  `sg_bot_ffa_role_combat` bridge for live FFA attack-decision ownership.
- `bot_brain.*` consumes existing FFA match role, lane, and engage policy
  output, validates visible shootable enemy facts, adopts the selected target,
  and returns a normal attack decision before attack-button application.
- Frame-command status exposes separate `ffa_role_combat_*` and
  `last_ffa_role_combat_*` counters for request, target selection, attack
  decision, deferral, role, lane, target client, visible, and shootable
  evidence. Existing TDM and CTF role-combat counters remain separate.
- Server smoke mode `48` runs a four-bot FFA proof, and the promoted
  `ffa_role_combat` scenario validates FFA readiness, objective role-policy
  selection, visible/shootable target facts, attack decisions, and applied
  attack-button metadata.
- Compact frame-command proof rows now print FFA role-combat, item-role, and
  nav-policy evidence before the full verbose diagnostic so promoted scenario
  gates still see required counters when console output truncates later fields.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests; `meson compile
  -C builddir-win sgame_x86_64` passed; `meson compile -C builddir-win`
  passed; `python tools\refresh_install.py --build-dir builddir-win
  --install-dir .install --package-q2aas-aas` passed after the full build;
  focused `ffa_role_combat` passed from
  `.tmp\bot_scenarios\20260621T184033Z`; focused regression coverage for
  role-combat, item-role, friendly-fire, and coop interaction rows passed from
  `.tmp\bot_scenarios\20260621T184941Z`; focused
  `trace_checked_corner_cutting` passed from
  `.tmp\bot_scenarios\20260621T185247Z`; and the full implemented scenario
  suite passed 53 rows from `.tmp\bot_scenarios\20260621T185255Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`.

## Native Runtime Update: Team Fire Avoidance

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_team_fire_avoidance` bridge for live TDM friendly-fire suppression.
- `bot_brain.*` consumes the existing objective friendly-fire policy before
  action application and suppresses attack input when a teammate is in the
  line of fire or the current target is not allowed.
- Frame-command status exposes `team_fire_avoidance_*` and
  `last_team_fire_avoidance_*` counters for evaluation, block, target-block,
  line-block, clear, invalid-skip, client, target, friendly-line, target
  allowance, blocked, and reason evidence.
- Server smoke mode `34` runs a four-bot TDM proof, and the promoted
  `team_fire_avoidance` scenario validates TDM readiness, live attack
  decisions, objective friendly-fire policy evaluation, and blocked attack
  input metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-team-fire-avoidance-2026-06-21.md`.

## Native Runtime Update: CTF Role Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_ctf_role_route` bridge for live Capture the Flag role/lane route
  ownership.
- `BotTimedRouteGoalKind::CtfRole` consumes existing CTF match role/lane policy
  output through the generic timed route-goal owner, keeping its counters
  separate from the TDM `team_role` proof.
- Frame-command status exposes `ctf_role_route_*` and
  `last_ctf_role_route_*` counters for request, policy-selection, activation,
  refresh, deferral, route request, expiration, invalid-skip, mode, role, lane,
  priority, remaining-time, and route-distance evidence.
- Server smoke mode `35` runs a four-bot CTF proof, and the promoted
  `ctf_role_route` scenario validates CTF team-mode readiness, Capture the
  Flag match-policy selection, timed route owner kind `6`, owner activation,
  route requests, and latest role/lane/distance metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-role-route-2026-06-21.md`.

## Native Runtime Update: CTF Role Combat Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_ctf_role_combat` bridge for live Capture the Flag role/lane combat
  ownership.
- The bridge consumes existing CTF match role/lane policy as an attack-decision
  owner only when policy requests engagement and the selected target facts are
  valid, alive, visible, shootable, and spawn-count matched.
- The selected target is adopted into `bot->enemy` and the per-bot blackboard
  before final frame-command angles are calculated, so the role-owned attack
  command faces the same enemy it selected while later team-fire suppression can
  still veto unsafe attack input.
- Frame-command status exposes `ctf_role_combat_*` and
  `last_ctf_role_combat_*` counters for request, policy-selection,
  target-selection, attack-decision, override, deferral, invalid-skip, mode,
  role, lane, priority, target identity, distance, visibility, shootability, and
  reason evidence.
- Server smoke mode `36` runs a four-bot CTF proof, and the promoted
  `ctf_role_combat` scenario validates CTF readiness, Capture the Flag
  match-policy selection, visible/shootable client target facts, role-owned
  attack decisions, and applied attack input from the refreshed `.install`
  payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --package-q2aas-aas` passed; focused
  `ctf_role_combat` and `team_fire_avoidance` scenario runs passed; and the
  then-current full implemented scenario suite passed before the mode `37`
  dropped-flag route row was added.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-role-combat-2026-06-21.md`.

## Native Runtime Update: CTF Dropped Flag Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_ctf_dropped_flag_route` bridge for live Capture the Flag dropped
  enemy flag route ownership.
- Server smoke mode `37` seeds routeable synthetic red and blue dropped flags
  for the proof path, then restores that synthetic state outside the mode so
  normal gameplay is unaffected.
- The bridge asks existing objective role policy for an attacker enemy-flag
  pickup assignment and requires the selected objective to come from
  `DroppedFlagEntity` before it records a route request.
- Frame-command status exposes `ctf_dropped_flag_route_*` and
  `last_ctf_dropped_flag_route_*` counters for request, assignment, route
  request, route command, invalid-skip, role, lane, objective type, target
  source, entity, item, priority, and goal-distance evidence.
- The promoted `ctf_dropped_flag_route` scenario validates CTF readiness,
  dropped-flag response lane selection, dropped-flag target-source selection,
  route requests, route commands, and zero invalid skips from the refreshed
  `.install` payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `ctf_dropped_flag_route` passed with
  `frames=246`, `commands=246`, `route_commands=246`, `route_failures=0`, and
  `ctf_dropped_flag_route_invalid_skips=0`; and the full implemented scenario
  suite reported 29 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-dropped-flag-route-2026-06-21.md`.

## Native Runtime Update: CTF Carrier Support Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_ctf_carrier_support_route` bridge for live Capture the Flag
  same-team enemy flag-carrier support route ownership.
- Server smoke mode `38` waits for four active players plus a short command
  warmup, then seeds a routeable same-team carrier with the enemy flag for the
  proof path without changing normal gameplay outside the mode.
- The bridge asks the objective helper for a support-role enemy-flag assignment
  and requires the selected objective to come from `FlagCarrier` before it
  records a route request.
- Frame-command status exposes `ctf_carrier_support_route_*` and
  `last_ctf_carrier_support_route_*` counters for request, assignment, route
  request, route command, invalid-skip, role, lane, objective type, target
  source, entity, carrier client, item, priority, and goal-distance evidence.
- The promoted `ctf_carrier_support_route` scenario validates CTF readiness,
  carrier-support lane selection, flag-carrier target-source selection, route
  requests, route commands, carrier-client identity, and zero invalid skips from
  the refreshed `.install` payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `ctf_carrier_support_route` passed with
  `frames=246`, `commands=246`, `route_commands=246`, `route_failures=0`,
  `ctf_carrier_support_route_invalid_skips=0`, and
  `last_ctf_carrier_support_route_source=3`; and the then-current 30-row
  implemented scenario suite passed without failures, timeouts, errors, or
  pending rows.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-carrier-support-route-2026-06-21.md`.

## Native Runtime Update: CTF Base Return Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_ctf_base_return_route` bridge for live Capture the Flag own-flag
  return route ownership against an enemy carrier.
- Server smoke mode `39` waits for four active players plus a short command
  warmup, then seeds an enemy carrier with the bot team's own flag inventory for
  the proof path without moving live players or changing normal gameplay outside
  the mode.
- Carrier-support and base-return smoke setup now grant carrier flag inventory
  only after team readiness and carrier validation, leaving player origins
  intact during the proof warmup.
- The bridge asks the objective helper for a returner-role own-flag assignment
  and requires the selected objective to come from `FlagCarrier` before it
  records a route request.
- Frame-command status exposes `ctf_base_return_route_*` and
  `last_ctf_base_return_route_*` counters for request, assignment, route
  request, route command, invalid-skip, role, lane, objective type, target
  source, entity, carrier client, item, priority, and goal-distance evidence.
- The promoted `ctf_base_return_route` scenario validates CTF readiness,
  returner role, own-base-return lane, own-flag target type, flag-carrier target
  source, route requests, route commands, carrier-client identity, and zero
  invalid skips from the refreshed `.install` payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `ctf_carrier_support_route` and
  `ctf_base_return_route` runs passed; and the then-current implemented
  scenario suite completed without failures, timeouts, errors, or pending rows.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-base-return-route-2026-06-21.md`.

## Native Runtime Update: CTF Objective Route Policy

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now adds a default-off
  `sg_bot_ctf_objective_route` bridge that composes CTF base-return,
  carrier-support, and dropped/enemy-flag fallback candidates through one
  priority route owner.
- Server smoke mode `40` uses the four-bot CTF setup, retains the seeded dropped
  enemy flag target from the focused dropped-flag proof, and keeps smoke-seeded
  flag carriers alive long enough for deterministic route-owner status capture.
- Frame-command status exposes `ctf_objective_route_*` and
  `last_ctf_objective_route_*` request, assignment, candidate, selection,
  lower-priority deferral, route command, invalid-skip, role, lane, target,
  carrier-client, item, priority, and goal-distance evidence.
- The promoted `ctf_objective_route` scenario validates CTF readiness, route
  ownership, base-return priority, carrier-support fallback selection,
  dropped-flag deferral evidence, route commands, and zero invalid skips from
  the refreshed `.install` payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `ctf_base_return_route` and
  `ctf_objective_route` runs passed; and the full implemented scenario suite
  reported 32 passed, 0 failed, 0 timed out, 0 errored, and 0 pending.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-objective-route-policy-2026-06-21.md`.

## Native Runtime Update: CTF Objective Route Precedence

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now records
  `ctf_role_route_objective_deferrals` when the default-off
  `sg_bot_ctf_objective_route` policy is active alongside
  `sg_bot_ctf_role_route`.
- The generic CTF role-route path still counts valid role-policy requests and
  selections, but it exits before timed route activation so the more specific
  objective-route policy owns the selected flag route.
- Server smoke mode `41` uses the four-bot CTF setup, enables both CTF route
  bridges, emits `ctf_objective_route_precedence=1` in the begin marker, and
  keeps the role-route activation/route-request counters at zero while
  objective-route route commands increase.
- The same stabilization pass removes live-player teleport setup from the
  adjacent CTF carrier-support/base-return proofs and supplies a smoke-only
  friendly-line proof for the TDM team-fire scenario, preserving the policy
  evidence while avoiding setup-induced command-frame stalls.
- The promoted `ctf_objective_route_precedence` scenario validates CTF
  readiness, role-route request/selection evidence, objective-route deferral
  evidence, objective-route assignment/route-command evidence, latest selected
  objective metadata, and zero invalid skips from the refreshed `.install`
  payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `ctf_objective_route_precedence` passed; and
  focused no-teleport stress loops for `ctf_carrier_support_route`,
  `ctf_base_return_route`, and `team_fire_avoidance` each passed five
  consecutive runs; the full implemented scenario suite reported 33 passed, 0
  failed, 0 timed out, 0 errored, and 0 pending from
  `.tmp\bot_scenarios\20260621T091013Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-objective-route-precedence-2026-06-21.md`.

## Native Runtime Update: FFA Roam Route Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now lets default-off
  `sg_bot_ffa_roam_route` consume FFA roam/collect/engage match policy as a
  short timed route-goal owner.
- The new `FfaRoam` timed route kind records route-owner status under
  `ffa_roam_route_*` and `last_ffa_roam_route_*`, including request,
  selection, activation, route request, invalid-skip, latest mode/role/lane,
  priority, remaining time, and goal-distance fields.
- Server smoke mode `42` runs a four-bot FFA setup, sets `deathmatch 1` and
  `g_gametype 1`, enables `sg_bot_ffa_roam_route`, and emits
  `ffa_roam_route=1` in the begin marker.
- The promoted `ffa_roam_route` scenario validates FFA readiness, match-policy
  selection evidence, timed owner kind `7`, FFA route activations and route
  requests, zero invalid skips, and positive latest goal distance from the
  refreshed `.install` payload.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 32 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `ffa_roam_route` passed with
  `route_commands=246`, `route_failures=0`, and `pass=1`; and the full
  implemented scenario suite reported 34 passed, 0 failed, 0 timed out, 0
  errored, and 0 pending from `.tmp\bot_scenarios\20260621T092925Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ffa-roam-route-2026-06-21.md`.

## Native Runtime Update: Team Role Combat Ownership

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now lets default-off
  `sg_bot_team_role_combat` consume TDM match role/lane policy as a live
  attack-decision owner.
- Frame-command status records `team_role_combat_*` and
  `last_team_role_combat_*` counters for request, policy-selection,
  target-selection, attack-decision, invalid-skip, last mode/role/lane,
  priority, target, visibility, shootability, and reason metadata.
- Server smoke mode `43` runs a four-bot TDM setup, sets `deathmatch 1` and
  `g_gametype 3`, enables `sg_bot_team_role_combat`, and emits
  `team_role_combat=1` in the begin marker.
- The promoted `team_role_combat` scenario validates TDM readiness,
  match-policy selection evidence, live visible/shootable target facts, attack
  decisions, zero invalid skips, and attack-button application from the
  refreshed `.install` payload.
- Compact action/detail proof rows now print before oversized verbose
  diagnostics so reserved-mode marker gates keep parsing the action evidence.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed; `meson compile -C
  builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `team_role_combat` passed with
  `route_commands=246`, `route_failures=0`, and `pass=1`; and the full
  implemented scenario suite reported 35 passed, 0 failed, 0 timed out, 0
  errored, and 0 pending from `.tmp\bot_scenarios\20260621T101221Z`.
- Implementation log:
  `docs-dev/q3a-botlib-team-role-combat-2026-06-21.md`.

## Native Runtime Update: Team Role Combat Friendly-Fire Precedence

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now treats smoke mode `44` as a composed TDM
  proof that enables both `sg_bot_team_role_combat` and
  `sg_bot_team_fire_avoidance`.
- Mode `44` deliberately leaves the generic smoke combat cvar disabled, so the
  attack decision must come from TDM role/lane policy before friendly-fire
  suppression evaluates it.
- Frame-command status reuses the existing `team_role_combat_*` and
  `team_fire_avoidance_*` counters to prove policy stacking without adding a
  new gameplay-facing cvar or status family.
- The promoted `team_role_combat_avoidance` scenario validates TDM readiness,
  match-policy selection evidence, visible/shootable role-combat target facts,
  attack decisions, friendly-fire policy evaluations, friendly-line blocks, and
  final blocked-state metadata.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed; `meson compile -C
  builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `team_role_combat_avoidance` passed with
  `route_commands=246`, `route_failures=0`, and `pass=1`; and the full
  implemented scenario suite reported 36 passed, 0 failed, 0 timed out, 0
  errored, and 0 pending from `.tmp\bot_scenarios\20260621T103520Z`.
- Implementation log:
  `docs-dev/q3a-botlib-team-role-combat-avoidance-2026-06-21.md`.

## Native Runtime Update: FFA Spawn-Camp Avoidance Route Source

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native brain command work now treats smoke mode `45` as a composed FFA
  proof that enables both `sg_bot_ffa_roam_route` and
  `sg_bot_ffa_spawn_camp_avoidance`.
- Mode `45` keeps the timed route-goal owner boundary from the FFA roam-route
  proof, but chooses a nearby active player as the route source when FFA policy
  says to avoid spawn-camp loops.
- Frame-command status now reports compact
  `ffa_spawn_camp_avoidance_*` counters before the verbose route diagnostic so
  source selection, policy use, route requests, fallbacks, invalid skips, and
  last source/goal metadata remain visible to the scenario parser.
- The existing trace-checked corner-cut proof also now gets a compact
  `q3a_bot_nav_policy_status` route-corner line before the long nav-policy
  diagnostic so its alias counters are not lost to log-line truncation.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed; `meson compile -C
  builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `trace_checked_corner_cutting` and
  `ffa_spawn_camp_avoidance` passed; and the full implemented scenario suite
  passed with no failed, timed-out, errored, or pending rows from
  `.tmp\bot_scenarios\20260621T111215Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ffa-spawn-camp-avoidance-2026-06-21.md`.

## Native Runtime Update: Map-Restart Cleanup Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-04-T16`, `DV-03-T05`, `DV-07-T06`

- WORR-native scenario harness work now promotes the restart-capable mode `19`
  map-repeat smoke into the implemented `map_restart_cleanup` row.
- The scenario keeps the existing server restart path, enables
  `sv_bot_frame_command_smoke_map_repeat_restart 1`, and hard-gates
  `command=map_force` plus `restart=1` on the cycle-begin, queued-reload, and
  observed-reload markers.
- The row also requires the observed reload to happen after one completed proof
  cycle, verifies cleanup status with `count=0`, and accepts final completion
  only when `cycles=2`, `map_changes=1`, and `final_count=0`.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 34 tests; the implemented
  scenario catalog reported 39 implemented rows and 0 pending rows; focused
  `map_restart_cleanup` passed from the current staged install with
  `commands=91`, `route_commands=91`, `route_failures=0`,
  `item_goal_peak_active_reservations=8`, `cycles=2`, `map_changes=1`, and
  `final_count=0`; and the full implemented scenario suite reported 39 passed,
  0 failed, 0 timed out, 0 errored, and 0 pending.
- Implementation log:
  `docs-dev/q3a-botlib-map-restart-cleanup-2026-06-21.md`.

## Native Runtime Update: Warmup Bot-Start Readiness Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `DV-03-T05`, `DV-07-T06`

- WORR-native server/game extension work now adds `BOT_WARMUP_STATUS_API_V1`
  and a game-side `q3a_bot_warmup_status` line for warmup population,
  ready-up, minplayers, bot-only start, and cleanup evidence.
- The new `sv_bot_warmup_smoke 2` path configures a two-bot FFA warmup with
  `warmup_do_ready_up 1`, `match_start_no_humans 1`, and `minplayers 2`,
  then validates `bots=2`, `playing=2`, `minplayers_met=1`,
  `bot_only_start=1`, `can_start=1`, and `pass=1` before removing all bots.
- The promoted `warmup_bot_start_readiness` scenario validates the live warmup
  status and final cleanup through marker checks and optional team/match
  readiness field discovery.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 35 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `warmup_bot_start_readiness` passed from
  `.install`; and the full implemented scenario suite reported 40 passed, 0
  failed, 0 timed out, 0 errored, and 0 pending.
- Implementation log:
  `docs-dev/q3a-botlib-warmup-bot-start-readiness-2026-06-21.md`.

## Native Runtime Update: Vote Bot-Exclusion Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native server/game extension work now adds `BOT_VOTE_STATUS_API_V1`
  and a game-side `q3a_bot_vote_status` line for vote population, active
  vote state, vote caller/voter breakdowns, and the last bot-origin launch
  attempt.
- The vote command layer now rejects bot-origin vote launch and vote-cast
  paths explicitly, so future bot client-command dispatch work cannot
  accidentally let fake clients skew human vote flow.
- The new `sv_bot_vote_smoke 2` path configures two bot-only FFA
  participants with voting enabled, validates `voting_clients=0`, attempts a
  harmless bot-origin `random 2` vote through the game vote helper, requires
  `q3a_bot_vote_launch reason=bot_blocked`, and verifies cleanup leaves zero
  bots and no active vote.
- The promoted `vote_bot_exclusion` scenario validates the status/launch
  markers and contributes the vote slice of `FR-07-T01`; MyMap queue, queued
  nextmap, and map-vote selector coverage are recorded in later sections.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 36 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `vote_bot_exclusion` passed from
  `.install`; and the full implemented scenario suite reported 41 passed, 0
  failed, 0 timed out, 0 errored, and 0 pending from
  `.tmp\bot_scenarios\20260621T123039Z`.
- Implementation log:
  `docs-dev/q3a-botlib-vote-bot-exclusion-2026-06-21.md`.

## Native Runtime Update: MyMap Bot Queue Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native server/game extension work now adds
  `BOT_MYMAP_STATUS_API_V1`, a game-side `q3a_bot_mymap_status` line, and
  dedicated `q3a_bot_mymap_queue` / `q3a_bot_mymap_consume` markers for map
  queue population, MyMap queue population, MyMap cvar gates, front queued
  map/social IDs, and the latest queue/consume attempt.
- The new `sv_bot_mymap_smoke 2` path configures one bot-only FFA participant,
  enables MyMap, assigns a deterministic test social ID to the bot when needed,
  validates the MyMap gate, queues the active map through `MapSystem`, consumes
  the queued map through `ConsumeQueuedMap`, and verifies cleanup leaves zero
  bots with empty queues.
- The smoke helper seeds a temporary active-map `MapEntry` only when the map
  pool is empty. Current staged installs do not include a loose
  `basew/mapdb.json` for the direct `std::ifstream` pool loader, so the proof
  exposes that fallback through `last_queue_map_seeded=1` instead of hiding it.
- The promoted `mymap_queue_bot_request` scenario validates the queue/status
  markers and contributes the MyMap queue slice of `FR-07-T01`; queued nextmap
  and map-vote selector coverage are recorded in later sections.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 37 tests; `meson compile
  -C builddir-win sgame_x86_64` passed; `meson compile -C builddir-win`
  passed; `python tools\refresh_install.py --build-dir builddir-win
  --install-dir .install --base-game basew --platform-id windows-x86_64`
  passed; focused `mymap_queue_bot_request` passed from
  `.tmp\bot_scenarios\20260621T125839Z`; and the full implemented scenario
  suite reported 42 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T125848Z`.
- Implementation log:
  `docs-dev/q3a-botlib-mymap-bot-queue-2026-06-21.md`.

## Native Runtime Update: Scoreboard Bot Classification Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native server/game extension work now adds
  `BOT_SCOREBOARD_STATUS_API_V1`, a game-side
  `q3a_bot_scoreboard_status` line, and a `q3a_bot_scoreboard_scores`
  marker for bot/human/player counts, voting-client counts, sorted-client
  classification, leader/runner-up row metadata, score ordering, FFA rank
  ordering, and the latest diagnostic score-application outcome.
- The new `sv_bot_scoreboard_smoke 2` path configures two bot-only FFA
  participants, waits for both queued fake clients to materialize, validates
  the zero-score standings view, applies deterministic proof scores of 7 and
  3 through the game-side diagnostic hook, and verifies cleanup leaves zero
  bots with no sorted clients.
- The promoted `scoreboard_bot_classification` scenario validates the
  scoreboard status/score markers and contributes the scoreboard-classification
  slice of Phase 7 match tooling; intermission and nextmap transition scenarios
  remain separate follow-up work.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 38 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `scoreboard_bot_classification` passed from
  `.tmp\bot_scenarios\20260621T132803Z`; and the full implemented scenario
  suite reported 43 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T132811Z`.
- Implementation log:
  `docs-dev/q3a-botlib-scoreboard-bot-classification-2026-06-21.md`.

## Native Runtime Update: Intermission Bot Cleanup Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native server/game extension work now adds
  `BOT_INTERMISSION_STATUS_API_V1`, a game-side
  `q3a_bot_intermission_status` line, and a
  `q3a_bot_intermission_begin` marker for bot/human/player counts,
  connected and sorted-client counts, intermission/queued/post-exit state,
  current/change-map target state, frozen/freecam/non-solid bot counts, and
  the latest native begin-intermission outcome.
- The new `sv_bot_intermission_smoke 2` path configures two bot-only FFA
  participants, waits for both queued fake clients to materialize, enters the
  native `BeginIntermission()` / `MoveClientToIntermission()` path through the
  game-side extension, validates frozen/freecam/non-solid bot state, removes
  all bots while the map remains in intermission, and verifies cleanup leaves
  zero bots, zero connected clients, and no sorted clients.
- The promoted `intermission_bot_cleanup` scenario validates the intermission
  status/begin markers and contributes the intermission/reconnect cleanup slice
  of Phase 7 match tooling; queued nextmap and map-vote selector coverage are
  recorded in the following sections.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 39 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `intermission_bot_cleanup` passed from
  `.tmp\bot_scenarios\20260621T134839Z`; and the full implemented scenario
  suite reported 44 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T134846Z`.
- Implementation log:
  `docs-dev/q3a-botlib-intermission-bot-cleanup-2026-06-21.md`.

## Native Runtime Update: Queued Nextmap Transition Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native server/game extension work now adds
  `BOT_NEXTMAP_STATUS_API_V1`, a game-side `q3a_bot_nextmap_status` line, and
  a `q3a_bot_nextmap_transition` marker for bot/human/player counts, connected
  clients, current/front queued map state, queue sizes, `changeMap` state,
  queued-transition outcome, queue consumption, override flags, and target map
  retention.
- The new `sv_bot_nextmap_smoke 2` path configures one bot-only FFA
  participant, enables MyMap, queues the current staged map through the existing
  MyMap helper, executes the queued transition through the game-side extension,
  waits for the dedicated server `sv.spawncount` reload edge, prints
  post-reload status, removes retained fake clients, and verifies final
  zero-bot cleanup.
- The promoted `queued_nextmap_transition` scenario validates the status,
  transition, reload, and cleanup markers and contributes the queued nextmap
  transition slice of Phase 7 match tooling; map-vote selector coverage is
  recorded in the next section, while broader tournament/admin match-flow
  hardening remains follow-up work.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 40 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `queued_nextmap_transition` passed from
  `.tmp\bot_scenarios\20260621T140550Z`; and the full implemented scenario
  suite reported 45 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T140557Z`.
- Implementation log:
  `docs-dev/q3a-botlib-queued-nextmap-transition-2026-06-21.md`.

## Native Runtime Update: Map-Vote Bot Exclusion Transition Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T01`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native server/game extension work now adds
  `BOT_MAPVOTE_STATUS_API_V1`, a game-side `q3a_bot_mapvote_status` line,
  and dedicated `q3a_bot_mapvote_begin`, `q3a_bot_mapvote_bot_vote`, and
  `q3a_bot_mapvote_finalize` markers for selector state, candidates, vote
  counts, bot/human ballot attribution, selected map, reload request state,
  and retained finalization status.
- The map selector vote path now explicitly ignores bot clients before
  storing or broadcasting selector votes. That keeps bot-origin selector input
  out of human map-vote flow even after future fake-client command dispatch
  grows more capable.
- The new `sv_bot_mapvote_smoke 2` path configures two bot-only FFA
  participants, enables the map selector, seeds the current staged map only
  when the runtime map pool is empty, starts a deterministic selector against
  that map, attempts a bot ballot through the guarded cast path, finalizes the
  selector, observes the dedicated server `sv.spawncount` reload edge, and
  verifies final zero-bot cleanup.
- The promoted `mapvote_bot_exclusion_transition` scenario validates the
  status, bot-vote, finalize, reload, and cleanup markers and closes the
  map-vote selector slice of `FR-07-T01`. Tournament veto/replay and match
  logging remain separate FR-07 work; command-level bot/admin isolation is
  covered by the admin audit update below.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 41 tests; `meson compile
  -C builddir-win` passed; `python tools\refresh_install.py --build-dir
  builddir-win --install-dir .install --base-game basew --platform-id
  windows-x86_64` passed; focused `mapvote_bot_exclusion_transition` passed
  from `.tmp\bot_scenarios\20260621T142951Z`; and the full implemented
  scenario suite reported 46 passed, 0 failed, 0 timed out, 0 errored, and 0
  pending from `.tmp\bot_scenarios\20260621T142957Z`.
- Implementation log:
  `docs-dev/q3a-botlib-mapvote-bot-exclusion-transition-2026-06-21.md`.

## Native Runtime Update: Admin Bot Privilege Audit Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T04`, `DV-03-T05`, `FR-04-T16`

- WORR-native command and status work now adds
  `BOT_ADMIN_AUDIT_STATUS_API_V1`, game-side
  `q3a_bot_admin_audit_status` and `q3a_bot_admin_audit_attempt` markers,
  and a registered-command audit helper that can inspect admin-only commands
  without routing a fake client through the normal network print path.
- The admin command permission path now treats bot clients as ineligible
  before checking cvar/session admin state. This makes bot admin rejection
  deterministic even when a test temporarily forces a bot session admin bit.
- The new `sv_bot_admin_audit_smoke 2` path stages one bot-only FFA
  participant, enables admin commands globally, temporarily sets that bot's
  admin session flag, attempts the registered `lock_team red` command,
  restores the session flag, and verifies the red team remains unlocked
  through cleanup.
- The promoted `admin_bot_privilege_audit` scenario validates command lookup,
  `admin_only=1`, `admin_session=1`, `allowed=0`, `executed=0`,
  `blocked=1`, `reason=bot_admin_blocked`, `admin_bots=0`, and
  `red_locked=0` after final zero-bot cleanup. This closes the first
  command-level bot/admin isolation proof for `FR-07-T04`.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `git diff --check` passed; `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py` passed 42 tests; `python -m
  py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `meson compile -C
  builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed; focused `admin_bot_privilege_audit` passed from
  `.tmp\bot_scenarios\20260621T150348Z`; and the full implemented scenario
  suite reported 47 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T150437Z`.
- Implementation log:
  `docs-dev/q3a-botlib-admin-bot-privilege-audit-2026-06-21.md`.

## Native Runtime Update: Tournament Bot Veto Exclusion Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T02`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native tournament/status work now adds
  `BOT_TOURNAMENT_STATUS_API_V1`, game-side
  `q3a_bot_tournament_status`, `q3a_bot_tournament_setup`, and
  `q3a_bot_tournament_veto` markers for tournament activation, veto phase
  state, active-side identity, map pool size, pick/ban counts, and the latest
  bot veto attempt outcome.
- The tournament veto authorization path now explicitly rejects bot clients
  before checking social ID, captain, or active-side eligibility. That prevents
  a fake client from making a tournament veto choice even if its session
  identity matches the active side.
- The new `sv_bot_tournament_smoke 2` path stages one bot-only FFA
  participant, configures a minimal best-of-three tournament veto state,
  assigns that bot the active home-side identity, attempts a veto pick through
  `Tournament_HandleVetoAction()`, and verifies no pick or ban state changes.
- The promoted `tournament_bot_veto_exclusion` scenario validates
  `active=1`, `veto_started=1`, `last_setup_bot_is_home=1`, `allowed=0`,
  `blocked=1`, `reason=bot_blocked`, `picks=0`, and `bans=0` after final
  zero-bot cleanup. Tournament replay reset/error handling is covered by the
  follow-up native runtime update below.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -k
  "tournament_bot_veto_exclusion or admin_bot_privilege_audit"` passed 2
  selected tests; `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `meson compile -C
  builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed; focused `tournament_bot_veto_exclusion` passed from
  `.tmp\bot_scenarios\20260621T152536Z`; and the full implemented scenario
  suite reported 48 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T153725Z`.
- Implementation log:
  `docs-dev/q3a-botlib-tournament-bot-veto-exclusion-2026-06-21.md`.

## Native Runtime Update: Tournament Replay Reset Scenario

Date: 2026-06-21

Tasks: `FR-04-T06`, `FR-07-T02`, `DV-03-T05`, `FR-04-T16`,
`DV-07-T06`

- WORR-native tournament replay work now extends
  `BOT_TOURNAMENT_STATUS_API_V1` with replay setup and replay attempt helpers,
  plus game-side `q3a_bot_tournament_replay_setup` and
  `q3a_bot_tournament_replay` markers for completed history setup, invalid
  replay preservation, valid replay rewind state, target map, win totals, and
  retained match history counts.
- `Tournament_ReplayGame()` now avoids clearing a completed tournament series
  through the generic match setup refresh before replay state is inspected, and
  rejects a missing replay map before mutating wins, match history, or
  map-change state.
- The new `sv_bot_tournament_smoke 3` path seeds a completed best-of-three
  tournament history without requiring bot participants, attempts out-of-range
  replay game `99`, verifies state preservation, then replays game `2` and
  verifies the history is truncated to one retained winner/map/id, wins rewind
  to `1-0`, the replay map is queued, and the series is reopened.
- The promoted `tournament_replay_reset` scenario validates
  `reason=range_error`, `preserved=1`, `reason=queued_replay`,
  `reset_applied=1`, `games_played=1`, `match_winners=1`, and
  `last_replay_reset_applied=1` through final zero-bot cleanup.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -k
  "tournament_replay_reset or tournament_bot_veto_exclusion"` passed 2
  selected tests; `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py` passed 44 tests; `python -m
  py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `meson compile -C
  builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed; focused `tournament_replay_reset` passed from
  `.tmp\bot_scenarios\20260621T154924Z`; and the full implemented scenario
  suite reported 49 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T155255Z`.
- Implementation log:
  `docs-dev/q3a-botlib-tournament-replay-reset-2026-06-21.md`.

## Native Runtime Update: Match Logging Schema Scenario

Date: 2026-06-21

Tasks: `FR-07-T03`, `DV-03-T05`, `FR-04-T16`, `DV-07-T06`

- WORR-native match logging work now stamps match-stats and
  tournament-series JSON artifacts with top-level `schemaName`,
  `schemaVersion`, `artifactType`, and `artifactVersion` fields.
- `MATCH_LOGGING_STATUS_API_V1` and `sv_bot_matchlog_smoke 2` provide a
  zero-bot proof path that builds sample artifacts through the native JSON
  exporters and emits `q3a_match_logging_schema` for the scenario harness.
- The promoted `match_logging_schema` scenario validates `worr.match_stats`,
  `worr.tournament_series`, version `1`, retained players/event-log/matches
  arrays, embedded match schema metadata, and final zero-bot cleanup.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -k match_logging_schema`
  passed 1 selected test; `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `meson compile -C
  builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed; focused `match_logging_schema` passed from
  `.tmp\bot_scenarios\20260621T161415Z`; and the full implemented scenario
  suite reported 50 passed, 0 failed, 0 timed out, 0 errored, and 0 pending
  from `.tmp\bot_scenarios\20260621T161434Z`.
- Implementation log:
  `docs-dev/q3a-botlib-match-logging-schema-2026-06-21.md`.

## Native Runtime Update: Match Logging Catalog Scenario

Date: 2026-06-21

Tasks: `FR-07-T03`, `DV-03-T05`, `FR-04-T16`, `DV-07-T04`,
`DV-07-T06`

- WORR-native match logging now writes `basew/matches/catalog.json` after
  successful match-stats or tournament-series exports.
- The catalog advertises `schemaName=worr.match_catalog`,
  `schemaVersion=1`, `artifactType=match_catalog`, and
  `artifactVersion=1`, then records relative artifact paths, source artifact
  schema metadata, summary fields, and latest-artifact IDs for downstream
  tooling.
- Catalog writes are guarded by a dedicated mutex because match-stat exports
  are processed by the detached worker while tournament series exports can be
  emitted from the game thread.
- `MatchLogging_PrintSchemaStatus()` now emits
  `q3a_match_logging_catalog`, and the existing `match_logging_schema`
  scenario hard-gates catalog schema metadata, artifact count, latest pointers,
  relative JSON paths, scratch catalog write/read proof, and final zero-bot cleanup through
  `sv_bot_matchlog_smoke 2`.
- `docs-user/competitive-server-tools.md` now notes the catalog location and
  `worr.match_catalog` schema for operators.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -k match_logging_schema`
  passed 1 selected test; `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `meson compile -C
  builddir-win worr_ded_engine_x86_64 sgame_x86_64` passed; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  passed; focused `match_logging_schema` passed from
  `.tmp\bot_scenarios\20260621T163834Z`; and `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py` passed 45 tests.
- Implementation log:
  `docs-dev/q3a-botlib-match-logging-catalog-2026-06-21.md`.

## Native Documentation Update: Competitive Server Tools Operator Docs

Date: 2026-06-21

Tasks: `FR-07-T05`, `DV-07-T04`, `DV-07-T06`

- WORR-native user documentation now gives server operators a practical guide
  for competitive cvars and commands spanning warmup, bot practice, voting,
  MyMap, queued nextmap, map selection, Duel queue, tournament veto/replay,
  admin controls, and match logging.
- `docs-user/competitive-server-tools.md` records the expected public cvars
  and commands rather than internal smoke cvars, including
  `g_allow_voting`, `g_allow_vote_mid_game`, `g_allow_spec_vote`,
  `g_vote_limit`, `g_vote_flags`, `g_maps_selector`, `g_maps_mymap`,
  `g_allow_mymap`, `g_maps_mymap_queue_limit`, `g_allow_duel_queue`,
  `match_setup_type`, `match_setup_bestof`, `g_tourney_cfg`,
  `g_statex_enabled`, `g_statex_humans_present`, and
  `g_statex_export_html`.
- The guide also documents the bot-boundary behavior proven by the recent
  scenario suite: bots cannot call or cast votes, cannot cross the admin
  boundary, are rejected from tournament veto identity, and do not satisfy
  human-presence match-log requirements when `g_statex_humans_present 1` is
  active.
- `docs-user/server-quickstart.md` and `docs-user/server.asciidoc` now link to
  the competitive guide, and the roadmap marks `FR-07-T05` complete.
- No new upstream Q3A, Gladiator, BSPC, idTech3, or q2proto source files were
  imported or modified for this update.
- Validation: source grep checks confirmed the documented cvars and commands
  in `g_main.cpp`, `command_voting.cpp`, `command_client.cpp`,
  `command_admin.cpp`, and `match_logging.cpp`; documentation/progress grep
  checked the new links and task references; `git diff --check` passed.
- Implementation log:
  `docs-dev/q3a-botlib-competitive-server-tools-docs-2026-06-21.md`.

## Native Validation Update: FFA Spawn-Camp Combat Avoidance

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native files `src/game/sgame/bots/bot_brain.cpp`,
  `src/server/main.c`, and `tools/bot_scenarios/*` now expose a default-off
  FFA spawn-camp combat-avoidance proof. The bridge composes FFA role-combat
  attack ownership with the FFA anti-camp source selector and vetoes attack
  input when both identify the same nearby live opponent.
- The promoted `ffa_spawn_camp_combat_avoidance` scenario uses server smoke mode
  `49` and records compact `ffa_spawn_camp_combat_avoidance_*` plus
  `last_ffa_spawn_camp_combat_avoidance_*` status fields for evaluation,
  source-block, target/source identity, policy, distance, and final blocked
  state.
- No new Q3A, Gladiator, BSPC, idTech3, or q2proto source files were imported
  or modified. This is WORR-owned behavior glue, status instrumentation, and
  scenario validation layered over the existing FFA match-policy proof surface.
- Validation: `meson compile -C builddir-win`; `meson compile -C builddir-win
  sgame_x86_64`; `python tools\refresh_install.py --build-dir builddir-win
  --install-dir .install --package-q2aas-aas`; focused
  `ffa_spawn_camp_combat_avoidance`; focused objective-detail truncation
  regression batch; full implemented suite passed with 54 short-run rows.
- Implementation log:
  `docs-dev/q3a-botlib-ffa-spawn-camp-combat-avoidance-2026-06-21.md`.

## Native Validation Update: Team Resource Denial

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native files `src/game/sgame/bots/bot_nav.cpp`,
  `src/game/sgame/bots/bot_nav.hpp`, `src/game/sgame/bots/bot_brain.cpp`,
  `src/server/main.c`, and `tools/bot_scenarios/*` now expose a default-off
  TDM resource-denial pickup-scoring proof. The bridge feeds contestable
  weapons, powerups, tech, and utility pickups through the existing
  `BotObjectiveResourcePolicy` helper as enemy-contested resources, then boosts
  candidates whose resource intent is deny-enemy.
- The promoted `team_resource_denial` scenario uses server smoke mode `50` and
  records compact `team_resource_denial_*` plus
  `last_team_resource_denial_*` nav-policy status fields for evaluations,
  deny selections, boosts, selected goals, invalid skips, selected
  role/lane/category/intent, item entity, item id, and final score.
- No new Q3A, Gladiator, BSPC, idTech3, or q2proto source files were imported
  or modified. This is WORR-owned behavior glue, status instrumentation, and
  scenario validation layered over the existing resource-policy helper surface.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests; `meson compile
  -C builddir-win sgame_x86_64`; `meson compile -C builddir-win`; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --package-q2aas-aas`; and focused `team_resource_denial` passed from
  `.tmp\bot_scenarios\20260621T200539Z`; full implemented suite passed 55
  rows from `.tmp\bot_scenarios\20260621T201034Z`.
- Implementation log:
  `docs-dev/q3a-botlib-team-resource-denial-2026-06-21.md`.

## Native Validation Update: Match Item Policy Umbrella

Date: 2026-06-21

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native files `src/game/sgame/bots/bot_nav.cpp`,
  `src/game/sgame/bots/bot_brain.cpp`, `src/server/main.c`, and
  `tools/bot_scenarios/*` now expose a default-off
  `sg_bot_match_item_policy` umbrella proof. The umbrella enables the existing
  FFA, CTF, and TDM item-role scoring bridges plus the TDM deny-enemy
  resource-denial scoring bridge without setting the individual proof cvars.
- The promoted `match_item_policy` scenario uses server smoke mode `51` and
  records existing `team_item_role_*` and `team_resource_denial_*` nav-policy
  status fields as proof that the umbrella cvar activates both score paths.
  The begin marker also proves `sg_bot_team_item_roles` and
  `sg_bot_team_resource_denial` remain disabled for the smoke.
- The compact objective-detail status row now prints lane and target-source
  proof fields before the long per-frame counters, keeping CTF target-source
  gates readable in long 246-frame scenario rows.
- No new Q3A, Gladiator, BSPC, idTech3, or q2proto source files were imported
  or modified. This is WORR-owned behavior glue, status ordering, and scenario
  validation layered over the existing match-policy helper surface.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests; `meson compile
  -C builddir-win worr_ded_engine_x86_64`; `meson compile -C builddir-win
  sgame_x86_64`; `python tools\refresh_install.py --build-dir builddir-win
  --install-dir .install --package-q2aas-aas`; focused `match_item_policy`
  passed from `.tmp\bot_scenarios\20260621T203348Z`; focused
  `ctf_dropped_flag_route` status regression passed from
  `.tmp\bot_scenarios\20260621T204037Z`; full implemented suite passed 56 rows
  from `.tmp\bot_scenarios\20260621T204044Z`.
- Implementation log:
  `docs-dev/q3a-botlib-match-item-policy-2026-06-21.md`.

## Governance Update: Phase 0 MVP Closeout

Date: 2026-06-21

Tasks: `FR-04-T01`, `FR-04-T10`, `DV-03-T05`, `DV-07-T06`

- The current BSPC imported-file ledger is complete for the imported files now
  present under `tools/q2aas/`, including separate modified-file rows for
  `bspc.c`, `be_aas_bspc.c`, and `map.c`. Future local tailoring must add a
  fresh modified-file row before landing.
- The current Q3A BotLib runtime ledger is complete for the imported files now
  present under `src/game/sgame/bots/q3a/`. Remaining Q3A runtime and behavior
  files stay reference-only until matched to a pinned source and recorded in
  this ledger before import.
- `FR-04-T01` MVP behavior scope is accepted against promoted scenario proof
  rows covering spawn/leave, profile loading, AAS area lookup near spawn,
  route-to-item/roam, visible-enemy engagement, stuck recovery, and FFA/TDM
  match-flow participation.
- Existing WORR-owned bot files touched by the project retain ZeniMax/WORR
  notices. Imported Q3A/BSPC files retain upstream notices, and locally
  modified imported files carry `Modified for WORR` notes where applicable.
- No new Q3A, Gladiator, BSPC, idTech3, or q2proto source files were imported
  or modified for this closeout.
- Validation: `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios` passed 45 tests; `meson compile
  -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --package-q2aas-aas`; focused MVP closeout scenario set passed from
  `.tmp\bot_scenarios\20260621T210213Z`; full implemented suite passed 56 rows
  from `.tmp\bot_scenarios\20260621T210229Z`.
- Implementation log:
  `docs-dev/q3a-botlib-phase0-mvp-closeout-2026-06-21.md`.

## Native Validation Update: Reference Map Runtime Adapter Round

Date: 2026-06-21

Tasks: `FR-04-T11`, `FR-04-T12`, `FR-04-T16`, `DV-07-T06`

- WORR-owned q2aas validation tooling now records per-map
  `team_objective_report` and `campaign_progression_report` diagnostics.
  Manifest-loaded maps own their diagnostic gates, so the strict DM
  high-value pickup reachability check remains required for `q2dm1`/`q2dm2`
  while CTF and campaign references validate their structural/objective
  evidence without inheriting a deathmatch-only gate.
- Local reference BSPs staged for validation were copied from
  `E:\Games\Quake2\baseq2\maps\` (`q2dm2`, `q2dm8`, `base1`, `base2`,
  `train`) and extracted from `E:\Games\Quake2\ctf\pak0.pak`
  (`maps/q2ctf1.bsp`). These BSPs are local validation inputs under
  `.install\basew\maps\`, not committed WORR source artifacts.
- The Q3A AAS runtime/adapter checklist wording was promoted to reflect
  existing compiled ownership: the imported AAS runtime C set is complete for
  current WORR route/query use, `AAS_Trace` stays in the WORR-native active-map
  Q2 BSP collision bridge, and `AAS_EntityCollision` crosses
  `botlib_adapter.*` into `BotRuntimeEntityTrace` / `gi.clip` for linked
  BBOX/BSP entities. This does not import the full Q3A arena AI/EA/goal system.
- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- Validation: `python -m py_compile
  tools\q2aas\validate_worr_q2aas.py
  tools\q2aas\test_validate_worr_q2aas.py`; `python -m unittest
  tools.q2aas.test_validate_worr_q2aas`; `meson compile -C builddir-win
  q2aas-staged-smoke`; `meson compile -C builddir-win q2aas-stage-aas`;
  `meson compile -C builddir-win sgame_x86_64`; and `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --archive-name pak0.pkz --platform-id windows-x86_64
  --package-q2aas-aas --q2aas-stage-report .tmp\q2aas\stage-report.json
  --q2aas-package-report .tmp\q2aas\refresh-package-archive-report.json
  --q2aas-package-audit-report
  .tmp\q2aas\refresh-package-archive-audit-report.json` passed. The refresh
  packaged and audited 8 generated AAS archive members.
- Implementation log:
  `docs-dev/q3a-botlib-reference-map-runtime-adapter-round-2026-06-21.md`.

## Native Runtime Update: Runtime Entity and Lifecycle Closeout

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T14`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native runtime and adapter files now promote existing active-map Q2 BSP
  point-contents and PVS/PHS bridges from interim smoke status to final adapter
  ownership: `AAS_PointContents` stays in the static Q2 BSP collision bridge,
  and `AAS_inPVS` / `AAS_inPHS` stay in the active-map leaf-cluster visibility
  bridge.
- `botlib_adapter.*`, `bot_runtime.*`, and `g_main.cpp` now treat BotLib
  initialization as idempotent for the game-module lifetime and call the
  imported BotLib shutdown path explicitly during `ShutdownGame()` after the
  existing level unload/lifecycle status path.
- `src/game/sgame/bots/q3a/README.WORR.md` records the LibVar boundary:
  upstream Q3A `bot_*` LibVars remain internal to imported AAS behavior, public
  WORR policy remains in `sg_bot_*`, and only Q2/WORR movement and
  reachability-cost `phys_*` / `rs_*` values are seeded for imported
  `be_aas_move.c`.
- `BotRuntimeBuildEntitySnapshot()` now distinguishes player, bot, spectator,
  and monster/NPC snapshots before handing data to imported `AAS_UpdateEntity`;
  loaded AAS debug output reports the current counts for those categories.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `git diff --check`.
- Implementation log:
  `docs-dev/q3a-botlib-runtime-entity-lifecycle-closeout-2026-06-21.md`.

## Native Runtime Update: Entity Scheduling and Aim Knowledge Closeout

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T12`, `FR-04-T14`, `FR-04-T15`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native files `src/game/sgame/bots/bot_runtime.*`,
  `src/game/sgame/bots/botlib_adapter.hpp`,
  `src/game/sgame/bots/bot_nav.*`, and `src/game/sgame/bots/bot_brain.cpp`
  now close the remaining Phase 4 entity snapshot categories, item
  desirability staggering, route recomputation rate-limit status, and
  blackboard-visible aim-knowledge proof.
- The existing WORR-native `bot_combat.*` and blackboard path are the fairness
  reference for this round: live aim/fire policy depends on current enemy facts,
  visibility, FOV, and shootability rather than imported Q3A behavior code or
  hidden omniscient state.
- Validation: `meson compile -C builddir-win sgame_x86_64`; `git diff --check`.
- Implementation log:
  `docs-dev/q3a-botlib-entity-scheduling-fairness-closeout-2026-06-21.md`.

## Native Behavior Update: Movement, Recovery, and Inventory Closeout

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T15`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_combat.cpp` now consumes the existing
  skill aim-error and tracking-noise policy as a deterministic bounded aim-point
  offset after visibility, FOV, shootability, reaction, and projectile-lead
  gates have selected a live aim point.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` now recognizes Q3A
  `TRAVEL_TELEPORT` as route-only traversal and exposes default-off controlled
  inactive recovery through `sg_bot_controlled_inactive_recovery`, with compact
  `q3a_bot_controlled_recovery_status` counters for respawn commands,
  spectator joins, and skips.
- Existing WORR-native `bot_nav.*`, `bot_actions.*`, and `bot_brain.*` coverage
  closes the stale movement/retry/inventory dispatcher checklist rows: route
  interaction retry owns door/platform/train/trigger/mover wait/use windows,
  the action dispatcher owns exact inventory `use_index_only` requests, and
  broader command ownership is fed by perception, item/inventory policy, live
  aim, route goals, timed route owners, role-combat owners, coop command owners,
  and inventory escape/retreat consumers.
- Validation: `meson compile -C builddir-win sgame_x86_64`.
- Implementation log:
  `docs-dev/q3a-botlib-movement-recovery-inventory-closeout-2026-06-21.md`.

## Final Checklist Closeout: Runtime, Generator, CI, and Reference Validation

Date: 2026-06-21

Tasks: `FR-04-T02`, `FR-04-T11`, `FR-04-T12`, `FR-04-T14`, `FR-04-T16`,
`DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this closeout.
- `docs-dev/q3a-botlib-runtime-implementation-2026-06-21.md` records the
  current imported Q3A AAS runtime subset, WORR adapter ownership, explicit
  non-imported Q3A behavior boundaries, and Linux/macOS CI build coverage
  evidence from the release workflow matrix.
- `docs-dev/q2-aas-generator-implementation-2026-06-21.md` records the current
  `TTimo/bspc`-derived q2aas generator tailoring, validation/staging/package
  target surface, eight-map reference validation set, and Linux/macOS CI build
  coverage evidence.
- The final plan closeout converted the reusable checklist gate template to
  plain guidance, so raw markdown checkbox counting no longer treats it as open
  task debt. The phase checklist and raw checkbox count both now report
  `809/809` complete.
- Validation evidence for this ledger update is local build/tool validation plus
  static workflow coverage review: `meson compile -C builddir-win
  sgame_x86_64`, `meson compile -C builddir-win worr_ded_engine_x86_64`,
  `meson compile -C builddir-win q2aas-staged-smoke`, `meson compile -C
  builddir-win q2aas-stage-aas`, `meson compile -C builddir-win
  q2aas-stage-audit`, `python tools\release\targets.py --matrix-json
  --pretty`, and `git diff --check`.

## Native Behavior Update: Behavior Policy Umbrella

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- The WORR-owned `sg_bot_behavior_enable` cvar now composes the current
  default-off behavior proof gates across `bot_brain.*` and `bot_nav.*`,
  including TDM role-route, TDM role-combat, team friendly-fire avoidance, and
  match item-policy activation.
- The WORR-owned `q3a_bot_behavior_policy_status` marker records the umbrella
  state and selected subpolicy gates so smoke tooling can prove the integrated
  switch without inferring it from unrelated command counters.
- Dedicated smoke mode `52` and the `behavior_policy_umbrella` scenario verify
  that only `sg_bot_behavior_enable` is set while the narrower proof cvars stay
  disabled in the begin marker. Focused validation passed from
  `.tmp\bot_scenarios\behavior_policy_umbrella\20260622T050833Z`.
- The implemented catalog now reports 59 rows total: 58 automated short-run
  rows plus one manual high-bot degradation row. The previous full implemented
  run remains
  `.tmp\bot_scenarios\20260621T210229Z`, and the dedicated mode `51`
  `match_item_policy` pickup-goal proof was revalidated from
  `.tmp\bot_scenarios\match_item_policy_check\20260622T050722Z`.
- Implementation log:
  `docs-dev/q3a-botlib-behavior-policy-umbrella-2026-06-22.md`.

## Native Behavior Update: Profile Role Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `bot_objectives.*` now consumes the existing `bot_role` userinfo
  value that the server profile loader derives from profile `WORR_ROLE` /
  `role` metadata. When a match-policy caller does not provide an explicit
  requested role, the profile role becomes the requested match role.
- WORR-native `bot_brain.*` compact objective status now reports match-policy
  requested-role counts, profile-role counts, honored-role counts, fallback
  counts, and last requested/profile role values.
- Dedicated smoke mode `53` and the `profile_role_policy` scenario prove staged
  profile ids `smoke`, `bulwark`, `relay`, and `vanguard` can drive TDM
  attacker, defender, and support/midfield match-policy selections with zero
  profile-role fallback. Focused validation passed from
  `.tmp\bot_scenarios\profile_role_policy\20260622T052929Z`.
- The implemented catalog now reports 59 rows total: 58 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-profile-role-policy-2026-06-22.md`.

## Native Behavior Update: Profile Team Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native server profile loading now parses profile teamplay,
  objective, and friendly-fire-care hints, including the
  `WORR_TEAMPLAY_BIAS`, `WORR_OBJECTIVE_BIAS`, and
  `WORR_FRIENDLY_FIRE_CARE` aliases, then publishes them into bot userinfo.
- WORR-native `bot_objectives.*` now consumes those userinfo hints during
  match-policy evaluation. Teamplay and friendly-fire care apply in team modes,
  objective bias applies in CTF, and high friendly-fire care can activate the
  friendly-fire-avoidance policy even when the damage rule itself is lenient.
- WORR-native `bot_brain.*` compact objective status now reports profile
  teamplay/objective/friendly-fire-care presence, applied counts, last accepted
  bias values, and last priority bonuses.
- Dedicated smoke mode `54` and the `profile_team_policy` scenario prove staged
  profile ids `smoke`, `bulwark`, `relay`, and `vanguard` can drive CTF
  teamplay, objective, and friendly-fire-care match-policy bonuses with route
  commands and zero route failures. Focused validation passed from
  `.tmp\bot_scenarios\profile_team_policy\20260622T055119Z`.
- After this profile-team round, the implemented catalog reported 60 rows
  total: 59 automated short-run rows plus one manual high-bot degradation row;
  raw plan checklist rows remained `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-profile-team-policy-2026-06-22.md`.

## Native Behavior Update: Profile Item Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native server profile loading now parses profile item-greed,
  item-denial, powerup-timing, and retreat-health hints, including the
  `WORR_ITEM_GREED`, `WORR_ITEM_DENIAL`, `WORR_POWERUP_TIMING`, and
  `WORR_RETREAT_HEALTH` aliases, then publishes them into bot userinfo.
- WORR-native `bot_objectives.*` now consumes those userinfo hints during
  match item/resource policy evaluation. Item greed raises self pickup
  preference, item denial raises deny-enemy scoring in team modes, powerup
  timing raises major item preference, and retreat health raises recovery
  priority when the bot is at or below the configured health threshold.
- WORR-native objective/nav status now reports profile item-policy presence,
  applied counts, last values, last bonuses, and selected-goal bonus
  propagation.
- Dedicated smoke mode `55` and the `profile_item_policy` scenario prove staged
  profile ids `smoke`, `bulwark`, `relay`, and `vanguard` can drive TDM
  item-greed, item-denial, powerup-timing, and retreat-health bonuses with item
  assignments and zero route failures. Focused validation passed from
  `.tmp\bot_scenarios\profile_item_policy\20260622T062835Z`.
- The implemented catalog now reports 61 rows total: 60 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-profile-item-policy-2026-06-22.md`.

## Native Behavior Update: Profile Movement Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `bot_objectives.*` now consumes staged `bot_movement_style`
  userinfo from profile `WORR_MOVEMENT_STYLE` metadata. It maps attack,
  defense, roam, and evasive movement labels into deterministic match-policy
  bonuses for selected role priority, attack/defense/midfield priority,
  roam priority, collect priority, major-item pressure, and team
  resource-sharing preference.
- WORR-native objective status now reports profile movement-style presence,
  attack/defense/roam/evasive buckets, applied counts, last style/name, and
  last movement bonus values.
- Dedicated smoke mode `56` and the `profile_movement_policy` scenario prove
  staged profile ids `smoke`, `bulwark`, `relay`, and `vanguard` can drive TDM
  profile movement-policy bonuses while `sg_bot_behavior_enable` and
  `sg_bot_match_item_policy` remain disabled. Focused validation passed from
  `.tmp\bot_scenarios\profile_movement_policy\20260622T070032Z`.
- The implemented catalog now reports 62 rows total: 61 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-profile-movement-policy-2026-06-22.md`.

## Native Validation Update: Bot Chat Policy Boundary

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `inc/shared/bot_chat_policy_status.h` adds
  `BOT_CHAT_POLICY_STATUS_API_V1`, and `inc/shared/gameext.h` exposes it to the
  existing game-extension query surface.
- WORR-native `src/game/sgame/bots/bot_runtime.cpp` now registers the public
  default-off `sg_bot_allow_chat` cvar. `src/game/sgame/g_local.hpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and
  `src/game/sgame/gameplay/g_svcmds.cpp` implement the compact
  `q3a_bot_chat_policy_status` output.
- WORR-native `src/server/main.c` stages dedicated smoke mode `57` for the
  `bot_chat_policy` scenario, and `tools/bot_scenarios/run_bot_scenarios.py`
  plus `tools/bot_scenarios/test_run_bot_scenarios.py` hard-gate the status
  marker.
- Earlier dedicated smoke mode `57` validation proved staged profile ids
  `smoke`, `bulwark`, `relay`, and `vanguard` carried chat metadata and could
  report `sg_bot_allow_chat 1` while no live consumer was present. That round
  was the boundary-only prerequisite for the live-dispatch follow-up below.
- Focused validation passed from
  `.tmp\bot_scenarios\bot_chat_policy\20260622T081452Z\20260622T071452Z`.
  Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- The implemented catalog now reports 63 rows total: 62 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-policy-boundary-2026-06-22.md`.

## Native Validation Update: Bot Chat Live Dispatch

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/commands/command_client.cpp` now provides the
  narrow `BotChatPolicy_Dispatch` helper and dispatch counters for attempts,
  submitted messages, failures, last dispatching client, and last team scope.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` consumes
  `sg_bot_allow_chat` with profile chat metadata and submits one sanitized proof
  message per bot spawn. `src/game/sgame/bots/bot_runtime.cpp` resets both the
  per-client chat guard and dispatch counters at level begin/end.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` reports
  `consumer_ready=1`, `dispatch_enabled=1`, `dispatch_submitted`,
  `dispatch_failures`, `last_dispatch_client`, `last_dispatch_team`, and
  `blocked_until_consumer=0` for the current live-dispatch bridge.
- Dedicated smoke mode `57` now proves the conservative live dispatch path
  rather than the earlier blocked-consumer boundary. Focused validation passed
  from `.tmp\bot_scenarios\20260622T080531Z`.
- Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  focused scenario validation passed with
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_policy --install-dir .install --format text`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- The implemented catalog remains 63 rows total: 62 automated short-run rows
  plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-dispatch-2026-06-22.md`.

## Native Validation Update: Bot Chat Team Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_runtime.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and `src/game/sgame/g_local.hpp` now
  register and expose the default-off `sg_bot_chat_team_only` cvar.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` consumes
  `sg_bot_chat_team_only` when submitting the conservative profile-chat proof
  dispatch.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` reports `team_only` in
  `q3a_bot_chat_policy_status`, and `src/server/main.c` stages dedicated smoke
  mode `58` for the `bot_chat_team_policy` scenario.
- Dedicated smoke mode `58` proves the team-only audience path with
  `team_only=1`, `dispatch_enabled=1`, `dispatch_submitted>=1`,
  `dispatch_failures=0`, `last_dispatch_team=1`, and
  `blocked_until_consumer=0`. Focused validation passed from
  `.tmp\bot_scenarios\20260622T080044Z`.
- Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  focused scenario validation passed with
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_team_policy --install-dir .install --format text`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- The implemented catalog now reports 64 rows total: 63 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-team-policy-2026-06-22.md`.

## Native Validation Update: Bot Chat Rate Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_runtime.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and `src/game/sgame/g_local.hpp` now
  register and expose the default-off `sg_bot_chat_min_interval_ms` cvar.
- WORR-native `src/game/sgame/commands/command_client.cpp` tracks bot chat
  dispatch rate limits, keeps rate-limited attempts separate from dispatch
  failures, and exposes the configured interval plus last submitted dispatch
  time to the status surface.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` treats a rate-limited proof
  attempt as handled for the current spawn so a blocked bot does not repeatedly
  retry inside the interval.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` reports
  `dispatch_rate_limited`, `rate_limit_ms`, and `last_dispatch_time_ms` in
  `q3a_bot_chat_policy_status`, and `src/server/main.c` stages dedicated smoke
  mode `59` for the `bot_chat_rate_policy` scenario.
- Dedicated smoke mode `59` proves the global minimum-interval path with
  `dispatch_attempts=4`, `dispatch_submitted=1`, `dispatch_rate_limited=3`,
  `dispatch_failures=0`, `rate_limit_ms=60000`, and `blocked_until_consumer=0`.
  Focused validation passed from `.tmp\bot_scenarios\20260622T081428Z`.
- Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  focused scenario validation passed with
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_rate_policy --install-dir .install --format text`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- The implemented catalog now reports 65 rows total: 64 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-rate-policy-2026-06-22.md`.

## Native Validation Update: Bot Chat Initial Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` now maps profile
  `bot_chat_personality` metadata into deterministic initial utterance buckets
  before the conservative live chat dispatch path submits a proof line.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` reports
  `initial_chat_selections`, known/unknown personality counts, per-bucket
  quiet/direct/taunting/helpful/steady counters, and last initial
  client/personality/phrase fields in `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` stages dedicated smoke mode `60` for the
  `bot_chat_initial_policy` scenario while reusing the four profile-backed TDM
  chat setup and keeping `sg_bot_allow_chat` default-off outside the proof.
- Dedicated smoke mode `60` proves initial profile selection with
  `initial_chat_selections=4`, `initial_chat_known_personalities=4`,
  `initial_chat_unknown_personalities=0`, `initial_chat_quiet=1`,
  `initial_chat_direct=1`, `initial_chat_helpful=1`,
  `initial_chat_steady=1`, and `dispatch_failures=0`. Focused validation
  passed from `.tmp\bot_scenarios\20260622T085845Z`.
- Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  focused scenario validation passed with
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_initial_policy --install-dir .install --format text`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- The implemented catalog now reports 66 rows total: 65 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-initial-policy-2026-06-22.md`.

## Native Validation Update: Bot Chat Reply Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_runtime.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and `src/game/sgame/g_local.hpp` now
  register and expose the smoke-only `sg_bot_chat_reply_policy_smoke` cvar.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` records one reply selection
  per staged bot spawn after the initial proof line, maps profile
  `bot_chat_personality` metadata into deterministic first-event reply phrase
  ids, and submits the selected reply through the existing chat dispatch
  bridge.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` reports
  `reply_chat_enabled`, `reply_chat_events`, `reply_chat_selections`,
  known/unknown personality counts, `reply_chat_team_ready`,
  `reply_chat_submitted`, `reply_chat_rate_limited`, `reply_chat_failures`, and
  latest reply client/personality/phrase/event fields in
  `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` stages dedicated smoke mode `61` for the
  `bot_chat_reply_policy` scenario while reusing the four profile-backed TDM
  chat setup and keeping the reply selector disabled outside the proof.
- Dedicated smoke mode `61` proves reply profile selection with
  `dispatch_submitted=8`, `reply_chat_events=4`,
  `reply_chat_selections=4`, `reply_chat_known_personalities=4`,
  `reply_chat_team_ready=4`, `reply_chat_submitted=4`,
  `reply_chat_rate_limited=0`, `reply_chat_failures=0`, and
  `last_reply_chat_event=1`. Focused validation passed from
  `.tmp\bot_scenarios\20260622T092009Z`.
- Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  focused scenario validation passed with
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_reply_policy --install-dir .install --format text`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- The implemented catalog now reports 67 rows total: 66 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-reply-policy-2026-06-22.md`.

## Native Validation Update: Bot Chat Event Policy

Date: 2026-06-22

Tasks: `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_runtime.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, and `src/game/sgame/g_local.hpp` now
  register and expose the smoke-only `sg_bot_chat_event_policy_smoke` cvar.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` now dispatches reply events
  through an explicit event-policy helper, keeps per-event spawn guards, and
  maps staged chat personalities to deterministic team-ready and route-ready
  proof replies.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` reports
  `reply_chat_route_ready` alongside the existing reply status fields in
  `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` stages dedicated smoke mode `62` for the
  `bot_chat_event_policy` scenario while reusing the four profile-backed TDM
  chat setup and keeping the event selector disabled outside the proof.
- Dedicated smoke mode `62` proves multi-event reply profile selection with
  `dispatch_submitted=12`, `reply_chat_events=8`,
  `reply_chat_selections=8`, `reply_chat_known_personalities=8`,
  `reply_chat_team_ready=4`, `reply_chat_route_ready=4`,
  `reply_chat_submitted=8`, `reply_chat_rate_limited=0`,
  `reply_chat_failures=0`, and `last_reply_chat_event=2`. Focused validation
  passed from `.tmp\bot_scenarios\20260622T093637Z`.
- Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  focused scenario validation passed with
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario bot_chat_event_policy --install-dir .install --format text`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- This event-policy checkpoint added the sixty-eighth catalog row before the
  later behavior-arbitration promotion; raw plan checklist rows remained
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-event-policy-2026-06-22.md`.

## Native Validation Update: Behavior Arbitration

Date: 2026-06-22

Tasks: `FR-04-T02`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` now records central behavior
  owner arbitration with route, item, combat, objective, interaction, recovery,
  and idle owners; per-client owner memory; handoff counts; last owner
  id/name/priority/reason fields; and live/smoke/debug/deprecated behavior cvar
  classification in `q3a_bot_behavior_policy_status`.
- WORR-native `src/server/main.c` stages dedicated smoke mode `63` for the
  `behavior_arbitration` scenario, running the live `sg_bot_behavior_enable`
  umbrella without enabling individual proof cvars.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `behavior_arbitration` catalog row,
  marker gates, optional counter family, parser/unit coverage, and scenario
  documentation.
- Dedicated smoke mode `63` proves
  `behavior_arbitration_evaluations=246`,
  `behavior_arbitration_route_candidates=246`,
  `behavior_arbitration_item_candidates=246`,
  `behavior_arbitration_combat_candidates=245`,
  `behavior_arbitration_combat_owners=239`,
  `behavior_arbitration_handoffs=3`, `behavior_live_policy_cvars=8`, and zero
  smoke/debug/deprecated policy cvars. Focused validation passed from
  `.tmp\bot_scenarios\20260622T112202Z`.
- Build validation passed with
  `meson compile -C builddir-win sgame_x86_64 worr_ded_engine_x86_64`,
  install refresh passed with
  `python tools\refresh_install.py --build-dir builddir-win --install-dir .install --base-game basew --platform-id windows-x86_64`,
  focused scenario validation passed with
  `python tools\bot_scenarios\run_bot_scenarios.py --scenario behavior_arbitration --install-dir .install --timeout 120 --format text`,
  and unit coverage passed with
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- This behavior-arbitration checkpoint added the sixty-ninth catalog row before
  the later target-memory and weapon-scoring promotions; raw plan checklist rows
  remained `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-behavior-arbitration-2026-06-22.md`.

## Native Validation Update: Target Memory Decay

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` and
  `src/game/sgame/bots/bot_brain.hpp` now expose live blackboard target-memory
  telemetry: retained-from-memory state, memory age/window, smoke-occlusion
  counts, retain counts, decay counts, clear-after-decay evidence, and final
  decay entity/client ids.
- WORR-native `src/server/main.c` stages dedicated smoke mode `64` for the
  `target_memory_decay` scenario, using a two-bot FFA setup with a smoke-only
  short `1000` ms memory window.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `target_memory_decay` catalog row,
  marker gates, `target_memory_counters` optional field family, parser/unit
  coverage, and scenario documentation.
- Dedicated smoke mode `64` proves `combat_enemy_acquisitions>=1`,
  `combat_enemy_memory_retains>=1`, `combat_enemy_memory_decays>=1`,
  `combat_enemy_memory_smoke_occlusions>=1`, `combat_enemy_clears>=1`,
  `last_combat_enemy_memory_age_ms>0`,
  `last_combat_enemy_memory_window_ms=1000`,
  `last_combat_enemy_memory_decay_entity>=0`, and
  `last_combat_enemy_memory_decay_client>=0`. Focused validation passed from
  `.tmp\bot_scenarios\20260622T120742Z`.
- Build and package validation passed with the same command family as the
  implementation log, including `meson compile -C builddir-win sgame_x86_64`,
  `python tools\refresh_install.py --build-dir builddir-win`, focused
  `target_memory_decay` scenario validation, and
  `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- At this target-memory checkpoint, the implemented catalog reported 70 rows
  total: 69 automated short-run rows plus one manual high-bot degradation row;
  raw plan checklist rows remained `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-target-memory-decay-2026-06-22.md`.

## Native Validation Update: Weapon Scoring Arsenal

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_combat.*` now preserves
  current/preferred weapon usability, safety, and scoring reasons in
  `BotWeaponSelectionResult` so action-layer audits can report scorer context
  without changing the existing scoring threshold.
- WORR-native `src/game/sgame/bots/bot_actions.*` now records carried-weapon
  inventory scoring telemetry: ammo skips, splash-unsafe candidates, range
  selections, estimate selections, selected ammo, score margin, metadata
  priority/ammo-per-shot, splash/self-damage flags, range band, attack model,
  estimate adjustment, and estimate reason.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` and `src/server/main.c`
  add dedicated mode `65` `weapon_scoring_arsenal`, using a two-bot FFA proof
  with unsafe close-range rocket state, ready super shotgun, insufficient
  rail/BFG ammo, low enemy health, and exact weapon-switch completion.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `weapon_scoring_arsenal` catalog row,
  marker gates, optional-field documentation, parser/unit coverage, and
  scenario documentation.
- Dedicated smoke mode `65` proves `action_weapon_inventory_scans>=1`,
  `action_weapon_inventory_candidates>=4`,
  `action_weapon_inventory_ready_candidates>=2`,
  `action_weapon_inventory_selections>=1`,
  `action_weapon_inventory_switch_recommendations>=1`,
  `action_weapon_inventory_ammo_skips>=1`,
  `action_weapon_inventory_splash_unsafe_skips>=1`,
  `action_weapon_inventory_range_selections>=1`,
  `action_weapon_inventory_estimate_selections>=1`,
  `last_action_weapon_inventory_selected_item=11`,
  `last_action_weapon_inventory_selected_range_band_name=close`,
  `last_action_weapon_inventory_selected_attack_model_name=hitscan`,
  `last_action_weapon_inventory_estimate_reason=enemy_estimate_finisher`,
  `weapon_switch_completions>=1`, and `weapon_switch_failures=0`. Focused
  validation passed from `.tmp\bot_scenarios\20260622T123648Z`.
- Build and package validation passed with `meson compile -C builddir-win
  sgame_x86_64 worr_ded_engine_x86_64`, `python tools\refresh_install.py
  --build-dir builddir-win`, focused `weapon_scoring_arsenal` scenario
  validation, and `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- This weapon-scoring round brought the implemented catalog to 71 rows total:
  70 automated short-run rows plus one manual high-bot degradation row; raw
  plan checklist rows remained `809/809` checked at that point.
- Implementation log:
  `docs-dev/q3a-botlib-weapon-scoring-arsenal-2026-06-22.md`.

## Native Validation Update: Aim/Fire Policy Depth

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` now stages a deterministic
  mode `66` `aim_fire_policy_depth` proof with one rocket actor and a moving
  visible peer, forcing rocket inventory while recording reaction-delay,
  aim-settle, burst-cooldown, live-aim policy-block, projectile-lead, and
  attack-application counters.
- WORR-native `src/server/main.c` registers mode `66` as an FFA aim/fire policy
  smoke with aim fairness enabled and begin-marker field `aim_fire_policy=1`.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the implemented
  `aim_fire_policy_depth` row, reserved mode mapping, marker gates, synthetic
  test log coverage, and catalog documentation.
- Focused mode `66` validation passed from `.tmp\bot_scenarios\20260622T125826Z`
  with `combat_withheld_fire=35`, `combat_fire_decisions=8`,
  `action_applied_attack_buttons=8`, `aim_policy_blocks_reaction=24`,
  `aim_policy_blocks_aim_settle=24`,
  `aim_policy_blocks_burst_cooldown=22`, `aim_policy_fire_allowed=16`,
  `live_aim_policy_blocks=35`, `live_aim_projectile_lead_uses=43`,
  `last_live_aim_weapon=20`, `last_live_aim_reason=projectile_lead`,
  `projectile_lead_uses=43`, and `last_projectile_lead_weapon=20`.
- Build and package validation passed with `meson compile -C builddir-win
  sgame_x86_64 worr_ded_engine_x86_64`, `python tools\refresh_install.py
  --build-dir builddir-win`, focused `aim_fire_policy_depth` scenario
  validation, and `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`.
- The implemented catalog now reports 72 rows total: 71 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-aim-fire-policy-depth-2026-06-22.md`.

## Native Validation Update: Ammo Pressure Pickup

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_items.*` adds ammo item-focus parsing,
  ammo focus boost telemetry, and ammo/weapon goal-assignment counters while
  preserving the existing health/armor focus paths.
- WORR-native `src/game/sgame/bots/bot_nav.cpp` maps
  `sg_bot_frame_command_smoke_item_focus ammo` into ammo-only item utility for
  route-goal selection.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` stages mode `67`
  `ammo_pressure_pickup` with one shotgun bot, depleted shells, stocked
  non-shell ammo, and a routeable dropped shell pickup. Action proof status now
  reports ammo candidates, ammo seek decisions, ammo goal assignments, ammo
  focus boosts, last item, and last utility-kind metadata.
- WORR-native `src/server/main.c` reserves mode `67` as a one-bot FFA
  ammo-pressure smoke with begin-marker fields `item_focus=ammo` and
  `ammo_pressure=1`.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the implemented
  `ammo_pressure_pickup` row, reserved mode mapping, optional-field metadata,
  marker gates, synthetic test log coverage, and catalog documentation.
- Focused mode `67` validation passed from
  `.tmp\bot_scenarios\ammo_pressure_pickup\20260622T132231Z` with
  `frames=60`, `commands=60`, `route_commands=60`, `route_failures=0`,
  `item_goal_assignments=10`, `item_goal_peak_active_reservations=1`,
  `item_focus_ammo_boosts=17`, `item_ammo_goal_assignments=10`,
  `item_last_utility_kind_name=ammo`, and `pass=1`.
- Build and package validation passed with `ninja -C builddir-win
  sgame_x86_64.dll`, `ninja -C builddir-win worr_ded_x86_64.exe`,
  `ninja -C builddir-win worr_ded_engine_x86_64.dll`, `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64`, focused
  `ammo_pressure_pickup` scenario validation, `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -q`, and `python -m
  py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`.
- The implemented catalog now reports 73 rows total: 72 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-ammo-pressure-pickup-2026-06-22.md`.

## Survival Inventory Use Proof Credit Update (2026-06-22)

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` stages mode `68`
  `survival_inventory_use` with one FFA bot at low health, no armor, a carried
  power shield, and cells. Action proof status now reports cumulative
  inventory-use decisions, accepted pending inventory uses, inventory
  command-request builds/accepts/submissions, inventory dispatches, and last
  request/dispatch names.
- WORR-native `src/server/main.c` reserves mode `68` as a one-bot FFA
  survival-inventory smoke with begin-marker field `survival_inventory=1`.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the implemented
  `survival_inventory_use` row, reserved mode mapping, optional-field metadata,
  marker gates, synthetic test log coverage, and catalog documentation.
- Focused mode `68` validation passed from
  `.tmp\bot_scenarios\survival_inventory_use\20260622T161739Z` with
  `frames=60`, `commands=60`, `route_commands=60`, `route_failures=0`,
  `action_inventory_policy_scans=60`,
  `action_inventory_policy_usable_candidates=1`,
  `action_inventory_policy_selections=1`,
  `action_inventory_policy_survival_uses=1`,
  `action_inventory_policy_power_armor_uses=1`,
  `action_use_inventory_decisions=1`,
  `action_pending_inventory_uses=1`,
  `action_inventory_command_requests=1`,
  `action_command_request_submitted=1`,
  `action_inventory_command_dispatches=1`, and `pass=1`.
- Build and package validation passed with `ninja -C builddir-win
  sgame_x86_64.dll`, `ninja -C builddir-win worr_ded_x86_64.exe`,
  `ninja -C builddir-win worr_ded_engine_x86_64.dll`, `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64`, focused
  `survival_inventory_use` scenario validation, `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -q`, and `python -m
  py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`.
- After that round, the implemented catalog reported 76 rows total: 75 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-survival-inventory-use-2026-06-22.md`.

## Survival Health Route Proof Credit Update (2026-06-22)

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` stages mode `69`
  `survival_health_route` with one FFA bot at low health, no armor, no carried
  survival inventory pressure, and a routeable medium health target. Action
  proof status now reports health candidates, health seek decisions,
  low-health boosts, health goal assignments, last selected utility kind, and
  last selected route area/item evidence.
- WORR-native `src/server/main.c` reserves mode `69` as a one-bot FFA
  survival-health routing smoke with begin-marker fields `survival_route=1`
  and `survival_route_kind=health`.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the implemented
  `survival_health_route` row, reserved mode mapping, optional-field metadata,
  marker gates, synthetic test log coverage, and catalog documentation.
- Focused mode `69` validation passed from
  `.tmp\bot_scenarios\survival_health_route\20260622T164109Z` with
  `frames=60`, `commands=60`, `route_commands=60`, `route_failures=0`,
  `item_goal_assignments=3`, `last_item_goal_area=224`,
  `item_low_health_boosts=3`, `item_health_goal_assignments=3`,
  `item_health_candidates=3`, `item_health_seek_decisions=3`,
  `item_last_utility_kind_name=health`, and `pass=1`.
- Build and package validation passed with `ninja -C builddir-win
  sgame_x86_64.dll`, `ninja -C builddir-win worr_ded_x86_64.exe`,
  `ninja -C builddir-win worr_ded_engine_x86_64.dll`, `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64`, focused
  `survival_health_route` scenario validation, `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -q`, and `python -m
  py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`.
- After that round, the implemented catalog reported 76 rows total: 75 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-survival-health-route-2026-06-22.md`.

## Survival Armor Route Proof Credit Update (2026-06-22)

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` stages mode `70`
  `survival_armor_route` with one FFA bot at full health, no armor, no carried
  survival inventory pressure, and a routeable jacket armor target. Action
  proof status now reports armor candidates, armor seek decisions, low-armor
  boosts, armor goal assignments, last selected utility kind, and last selected
  route area/item evidence in the compact status marker.
- WORR-native `src/server/main.c` reserves mode `70` as a one-bot FFA
  survival-armor routing smoke with `sg_bot_frame_command_smoke_survival_route`
  set to `armor`, plus begin-marker fields `survival_route=1` and
  `survival_route_kind=armor`.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the implemented
  `survival_armor_route` row, reserved mode mapping, marker gates, synthetic
  test log coverage, and catalog documentation.
- Focused mode `70` validation passed from
  `.tmp\bot_scenarios\survival_armor_route\20260622T165918Z` with
  `frames=60`, `commands=60`, `route_commands=60`, `route_failures=0`,
  `item_goal_assignments=15`, `last_item_goal_area=188`,
  `item_low_armor_boosts=15`, `item_armor_goal_assignments=15`,
  `item_armor_candidates=15`, `item_armor_seek_decisions=15`,
  `item_last_utility_kind_name=armor`, and `pass=1`.
- Build and package validation passed with `ninja -C builddir-win
  sgame_x86_64.dll`, `ninja -C builddir-win worr_ded_x86_64.exe`,
  `ninja -C builddir-win worr_ded_engine_x86_64.dll`, `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64`, focused
  `survival_armor_route` scenario validation, `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -q`, and `python -m
  py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py`.
- After that round, the implemented catalog reported 76 rows total: 75 automated short-run
  rows plus one manual high-bot degradation row; raw plan checklist rows remain
  `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-survival-armor-route-2026-06-22.md`.

## Combat Survival Regression Proof Credit Update (2026-06-22)

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` stages mode `71`
  `combat_survival_regression` by composing the existing combat peer setup
  with low-health survival health-route pressure. The proof keeps the staged
  enemy visible and shootable while a routeable health pickup remains
  available.
- WORR-native `src/server/main.c` reserves mode `71` as a two-bot FFA
  combat/survival route proof with `sg_bot_frame_command_smoke_survival_route`
  set to `combat_health`, `survival_route=1`, and
  `survival_route_kind=combat_health` in the begin marker.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the implemented
  `combat_survival_regression` row, reserved mode mapping, marker gates,
  synthetic test log coverage, and catalog documentation. The row proves
  blackboard/action enemy pressure, withheld fire, health candidate/seek
  telemetry, health goal assignment, item arbitration ownership, and recovery
  arbitration ownership.
- Focused mode `71` validation passed from
  `.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z` with
  `frames=121`, `commands=121`, `route_commands=121`, `route_failures=0`,
  `item_goal_assignments=7`, `last_item_goal_area=224`,
  `combat_enemy_visible=120`, `combat_enemy_shootable=119`,
  `combat_withheld_fire=35`, `behavior_arbitration_item_owners=3`,
  `behavior_arbitration_recovery_owners=40`, and `pass=1`.
- Fast harness validation passed with `python -m py_compile
  tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` and `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -q`.
- The first expanded automated catalog attempt after mode `71` ran from
  `.tmp\bot_scenarios\20260622T171732Z` and exposed older status-surface
  contract drift. That failed aggregate is now superseded by the smoke
  contract reconciliation round recorded below.
- At that point, the implemented catalog reported 77 rows total: 76 automated
  short-run rows plus one manual high-bot degradation row; raw plan checklist
  rows remained `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-combat-survival-regression-2026-06-22.md`.

## Combat Survival Second-Map Regression Proof Credit Update (2026-06-22)

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- No new Q3A, BSPC, idTech3, Quake3e, baseq3a, Gladiator, or q2proto source
  files were imported or modified for this round.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` now supports
  per-scenario map overrides and reports `map_name` in catalog/run output. The
  new `combat_survival_regression_q2dm2` row pins `q2dm2` while reusing smoke
  mode `71`.
- WORR-native `src/server/main.c` adds the current map name to the
  `q3a_bot_frame_command_smoke_scenario=begin` marker, giving map-specific
  scenario rows a direct `map=q2dm2` proof gate.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py` and
  `tools/bot_scenarios/README.md` add the q2dm2 scenario row, map override
  coverage, marker gates, synthetic log coverage, and catalog documentation.
- Focused q2dm2 validation passed from
  `.tmp\bot_scenarios\combat_survival_regression_q2dm2\20260622T194547Z` with
  `map_name=q2dm2`, begin-marker `map=q2dm2`, `frames=121`, `commands=121`,
  `route_failures=0`, `item_goal_assignments=5`, visible/shootable enemy facts,
  withheld-fire evidence, and item/recovery arbitration ownership.
- The full automated `implemented` catalog passed 77 rows, 0 failed, 0 timeout,
  0 error, and 0 pending from
  `.tmp\bot_scenarios\implemented_after_q2dm2_regression\20260622T194653Z`.
- At that point, the implemented catalog reported 78 rows total: 77 automated
  short-run rows plus one manual high-bot degradation row; raw plan checklist
  rows remained `809/809` checked.
- Implementation log:
  `docs-dev/q3a-botlib-combat-survival-second-map-regression-2026-06-22.md`.

## Native Runtime Update: Smoke Contract Reconciliation

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-owned status and harness work reconciles the expanded automated bot
  scenario catalog after the mode `71` status-surface growth. No upstream Q3A,
  Gladiator, BSPC, idTech3, or q2proto source files were imported or modified
  for this update.
- `src/game/sgame/bots/bot_brain.cpp` now emits compact FFA roam-route and
  TDM role-route proof lines, keeps aim-fairness proof status at `none` after
  fire becomes allowed, and lets the mode `34` team-fire proof produce attack
  intent before friendly-line suppression vetoes it.
- `src/game/sgame/bots/bot_objectives.cpp` now preserves last-positive
  profile-derived bonus telemetry for match/item policy proof rows so later
  aggregate samples do not erase the positive evidence.
- `tools/bot_scenarios/test_run_bot_scenarios.py` no longer requires stale
  CTF objective-detail markers where route-specific status markers are the
  authoritative proof surface.
- Validation: `ninja -C builddir-win sgame_x86_64.dll` passed; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64` refreshed the staged
  payload; `python -m py_compile tools\bot_scenarios\run_bot_scenarios.py
  tools\bot_scenarios\test_run_bot_scenarios.py` passed; `pytest
  tools\bot_scenarios\test_run_bot_scenarios.py` passed 45 tests; focused
  `profile_item_policy`, `team_fire_avoidance`, `ffa_roam_route`,
  `team_role_route`, and `aim_fairness_policy_integration` checks passed; and
  the full automated `implemented` catalog passed 76 rows, 0 failed, 0 timeout,
  0 error, and 0 pending from
  `.tmp\bot_scenarios\implemented_after_next_round_stable_green\20260622T182201Z`.
- Implementation log:
  `docs-dev/q3a-botlib-smoke-contract-reconciliation-2026-06-22.md`.

## Native Behavior Update: Threat Retreat Avoidance

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds the default-off
  `sg_bot_threat_retreat` behavior, timed route-goal kind `ThreatRetreat`,
  low-health threat source selection, attack suppression during the retreat
  window, post-retreat re-engagement telemetry, and compact status counters for
  threat-retreat requests, sources, activations, route requests, suppressions,
  and re-engagements.
- WORR-native `src/server/main.c` reserves smoke mode `72`, stages the
  two-bot FFA low-health proof, emits begin-marker `threat_retreat=1`
  evidence, and intentionally avoids the older
  `sg_bot_frame_command_smoke_combat=engage_enemy` target setup.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the `threat_retreat_avoidance` scenario,
  optional status-field discovery, parser coverage for supplemental status
  rows, route-owner harness cleanup, and catalog documentation.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 47 tests; `meson compile -C builddir-win` passed; `.install/` was
  refreshed with current Windows binaries and `basew` game DLLs; focused
  `threat_retreat_avoidance` validation passed from
  `.tmp\bot_scenarios\20260622T202608Z`; focused neighboring route-owner
  validation passed from `.tmp\bot_scenarios\20260622T203111Z`; and the full
  `implemented` catalog passed 78 rows with 0 failures from
  `.tmp\bot_scenarios\20260622T203125Z`.
- Implementation log:
  `docs-dev/q3a-botlib-threat-retreat-avoidance-2026-06-22.md`.

## Native Validation Update: Q2DM8 Combat Map Regression

Date: 2026-06-22

Tasks: `FR-04-T03`, `FR-04-T11`, `FR-04-T15`, `DV-03-T05`

- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` now promotes
  `combat_survival_regression_q2dm8` and `threat_retreat_avoidance_q2dm8`.
  Both rows use the existing per-scenario map override path and hard-gate the
  server begin marker with `map=q2dm8`.
- The q2dm8 combat/survival row reuses mode `71` to prove route-clean command
  output, visible/shootable enemy telemetry, low-health health-item goal
  assignment, item arbitration ownership, and recovery ownership on the
  reference `q2dm8` deathmatch layout.
- The q2dm8 threat-retreat row reuses mode `72` to prove low-health live threat
  sourcing, retreat route requests, attack suppression, and combat ownership on
  the same reference map.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py` and
  `tools/bot_scenarios/README.md` add catalog, marker, synthetic parser, and
  documentation coverage for the two q2dm8 rows.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 47 tests; focused q2dm8 validation passed 2 rows with 0 failures from
  `.tmp\bot_scenarios\20260622T204956Z`; and the full `implemented` catalog
  passed 80 rows with 0 failures from
  `.tmp\bot_scenarios\20260622T205123Z`.
- Implementation log:
  `docs-dev/q3a-botlib-q2dm8-combat-map-regression-2026-06-22.md`.

## Native Validation Update: CTF Objective Live Loop

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`

- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` now hardens the
  existing `ctf_objective_route` scenario without adding a new smoke mode or
  catalog row. Mode `40` requires base-return, carrier-support, and
  dropped-flag objective selections in one CTF run.
- WORR-native marker gates also require objective arbitration evidence through
  `behavior_arbitration_objective_candidates` and
  `behavior_arbitration_objective_owners`.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py` and
  `tools/bot_scenarios/README.md` add parser/catalog coverage and scenario
  documentation for the stricter live-loop contract.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 47 tests; focused `ctf_objective_route` validation passed from
  `.tmp\bot_scenarios\20260622T210329Z`; and the full `implemented` catalog
  passed 80 rows with 0 failures from `.tmp\bot_scenarios\20260622T210348Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-objective-live-loop-2026-06-22.md`.

## Native Validation Update: TDM Role Spawn Stability

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/server/main.c` reserves smoke mode `73`, marks it as a
  map-repeat/restart scenario, and routes it through the existing TDM
  role-route and role-combat proof helpers. The begin marker emits
  `tdm_role_spawn_stability=1` so the integrated run is attributable in logs.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` promotes
  `tdm_role_spawn_stability` with forced same-map restart cvars, route/combat
  owner gates, post-reload cycle gates, TDM readiness gates, action attack
  gates, and final cleanup gates.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py` and
  `tools/bot_scenarios/README.md` add parser, catalog, marker-contract,
  command-construction, and documentation coverage for the new row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 48 tests; `meson compile -C builddir-win worr_ded_x86_64` passed;
  `meson compile -C builddir-win worr_ded_engine_x86_64` passed; `.install/`
  was refreshed with current Windows binaries and `basew` game data; focused
  `tdm_role_spawn_stability` validation passed from
  `.tmp\bot_scenarios\20260622T212431Z`; and the full `implemented` catalog
  passed 81 rows with 0 failures from `.tmp\bot_scenarios\20260622T212440Z`.
- Implementation log:
  `docs-dev/q3a-botlib-tdm-role-spawn-stability-2026-06-22.md`.

## Native Validation Update: FFA Live Pacing

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/server/main.c` reserves smoke mode `74`, routes it through
  the existing FFA roam-route, spawn-camp route-source, item-role, role-combat,
  and spawn-camp combat-veto helper predicates, and emits
  `ffa_live_pacing=1` in the begin marker.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` recognizes the combined FFA
  live-pacing cvar set as its own proof mode, preserving the route/combat owner
  setup while requesting validation-only pickup scoring telemetry.
- WORR-native `src/game/sgame/bots/bot_nav.*` adds `BotNav_ProbePickupGoal()`,
  which reuses normal pickup-goal scoring to populate `ffa_item_role_*` nav
  policy telemetry without replacing the active timed-route owner.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, command-construction,
  synthetic parser, and documentation coverage for the new row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 49 tests; `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll
  worr_ded_engine_x86_64 worr_ded_x86_64` passed; `.install/` was refreshed
  with current Windows binaries and `basew` game data; focused
  `ffa_live_pacing` validation passed from
  `.tmp\bot_scenarios\20260622T214927Z`; and the full `implemented` catalog
  passed 82 rows with 0 failures from `.tmp\bot_scenarios\20260622T215343Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ffa-live-pacing-2026-06-22.md`.

## Native Validation Update: Duel Live Pacing

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_objectives.*` adds
  `BotObjectiveMatchMode::Duel` with stable value `5`, maps
  `GameType::Duel` into that mode, records
  `team_objective_match_policy_duel`, and assigns Duel weapons, ammo,
  powerups, and techs to deny-enemy item policy.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds the default-off
  `sg_bot_duel_live_pacing` gate, reserves smoke mode `75`, reuses the
  FFA-style route, role-combat, spawn-pressure, and validation-only pickup
  scoring paths only for Duel policy when the cvar is enabled, and emits
  compact status with Duel mode/name evidence.
- WORR-native `src/game/sgame/bots/bot_nav.cpp` allows the existing item-role
  scoring bridge to evaluate Duel match policy under the Duel live-pacing gate.
- WORR-native `src/server/main.c` stages mode `75` as a two-bot Duel run with
  `g_gametype 2`, resets `sg_bot_duel_live_pacing`, and prints
  `duel_live_pacing=1` while the FFA proof cvars remain disabled.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, command-construction,
  synthetic parser, and documentation coverage for the new row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 50 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  q2aas AAS archive members; focused `duel_live_pacing` validation passed from
  `.tmp\bot_scenarios\20260622T222142Z`; q2dm8 combat/survival marker-contract
  recheck passed from `.tmp\bot_scenarios\20260622T222450Z`; and the full
  `implemented` catalog passed 83 rows with 0 failures from
  `.tmp\bot_scenarios\20260622T222457Z`.
- Implementation log:
  `docs-dev/q3a-botlib-duel-live-pacing-2026-06-22.md`.

## Native Validation Update: CTF Objective Transitions

Date: 2026-06-22

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_objectives.*` now records objective
  `flagDrops` and `flagReturns` counters alongside the existing pickup and
  capture counters.
- WORR-native `src/game/sgame/gameplay/g_capture.cpp` feeds those counters from
  `CTF_DeadDropFlag` and the same-team dropped-flag return branch. The generic
  item-touch path is still not used to infer flag events.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds the default-off
  `sg_bot_ctf_objective_transitions` proof path and stages pickup,
  death-drop, and dropped-flag return through CTF gameplay entry points before
  the combined CTF objective route policy owns the live flag loop.
- WORR-native `src/server/main.c` reserves smoke mode `76`, emits
  `ctf_objective_transitions=1`, stages the four-bot CTF setup, and resets the
  new proof cvar after the scenario.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, synthetic raw-mode,
  command-construction, and documentation coverage for the new row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 51 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  all eight staged q2aas AAS archive members; focused
  `ctf_objective_transitions` validation passed from
  `.tmp\bot_scenarios\20260622T230509Z`; and the full `implemented` catalog
  passed 84 rows with 0 failures from `.tmp\bot_scenarios\20260622T230519Z`.
- Implementation log:
  `docs-dev/q3a-botlib-ctf-objective-transitions-2026-06-22.md`.

## Native Validation Update: Coop Live Loop

Date: 2026-06-23

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds the default-off
  `sg_bot_coop_live_loop` aggregate proof gate, per-bot progress-wait
  activation, leader-route composition with door/elevator ownership, preserved
  wait telemetry during hold-command preemption, and live-loop-specific
  anti-blocking distance tuning.
- WORR-native `src/server/main.c` reserves smoke mode `77`, stages a two-bot
  coop run, emits `coop_live_loop=1`, resets the new proof cvar, and supports
  the elevator travel goal needed by the aggregate row.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, synthetic raw-mode,
  command-construction, and documentation coverage for the new row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 52 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  all eight staged q2aas AAS archive members; focused `coop_live_loop`
  validation passed from `.tmp\bot_scenarios\20260622T234315Z`; and the later
  full `implemented` catalog passed 86 rows with 0 failures from
  `.tmp\bot_scenarios\20260623T001205Z`.
- Implementation log:
  `docs-dev/q3a-botlib-coop-live-loop-2026-06-23.md`.

## Native Validation Update: Coop Share Loop

Date: 2026-06-23

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds the default-off
  `sg_bot_coop_share_loop` aggregate proof gate, maps it to reserved smoke mode
  `78`, activates the existing coop target-sharing and resource-sharing
  bridges, and preserves the target-share smoke target for the aggregate row.
- WORR-native `src/game/sgame/bots/bot_nav.cpp` lets the same aggregate gate
  activate coop reserve-for-teammate pickup-goal scoring.
- WORR-native `src/server/main.c` reserves smoke mode `78`, stages a two-bot
  coop run, emits `coop_share_loop=1`, resets the new proof cvar, and keeps the
  individual target/resource proof cvars available for narrow regression rows.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, synthetic raw-mode,
  command-construction, reserved-mode diagnostic, and documentation coverage
  for the new row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 53 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  all eight staged q2aas AAS archive members; focused `coop_share_loop`
  validation passed from `.tmp\bot_scenarios\20260623T001149Z`; and the full
  `implemented` catalog passed 86 rows with 0 failures from
  `.tmp\bot_scenarios\20260623T001205Z`.
- Implementation log:
  `docs-dev/q3a-botlib-coop-share-loop-2026-06-23.md`.

## Native Validation Update: Bot Chat Live Events

Date: 2026-06-23

Tasks: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds the default-off
  `sg_bot_chat_live_events` live chat event gate, tracks live chat event
  counters separately from smoke reply-policy counters, exposes an eleven-entry
  event taxonomy, records live spawn events, and submits live `route_ready`
  replies after route-command ownership succeeds.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp`,
  `src/game/sgame/gameplay/g_main.cpp`, `src/game/sgame/g_local.hpp`, and
  `src/game/sgame/bots/bot_runtime.cpp` expose/register the new cvar and print
  live event status fields in `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` reserves smoke mode `79`, stages a four-bot
  TDM run with `sg_bot_allow_chat 1` and `sg_bot_chat_live_events 1`, prints
  the live-events begin marker, and resets the cvar during smoke cleanup.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, synthetic raw-mode,
  command-construction, string-metric, and documentation coverage for the new
  row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 53 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  all eight staged q2aas AAS archive members; focused `bot_chat_live_events`
  validation passed from `.tmp\bot_scenarios\20260623T010520Z`; and the full
  `implemented` catalog passed 88 rows with 0 failures from
  `.tmp\bot_scenarios\20260623T010536Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-events-2026-06-23.md`.

## Native Validation Update: Bot Chat Live Event Cooldown

Date: 2026-06-23

Tasks: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` now records live spawn-event
  selections through the same reply-policy status path as route-ready live
  events, preserving submitted, rate-limited, and failure accounting for
  dispatch outcomes.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `live_chat_spawn` in
  `q3a_bot_chat_policy_status` so live spawn and route-ready breadth can be
  audited independently.
- WORR-native `src/server/main.c` reserves smoke mode `80`, stages the live
  chat cooldown run with `sg_bot_chat_live_events 1` and
  `sg_bot_chat_min_interval_ms 60000`, and prints the cooldown begin-marker
  field.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, parser-fixture,
  command-construction, and documentation coverage for the cooldown row.
- No Q3A, BSPC, Quake3e, baseq3a, Gladiator, or `q2proto/` source files were
  imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 53 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  all eight staged q2aas AAS archive members; focused `bot_chat_live_events`
  validation passed from `.tmp\bot_scenarios\20260623T010520Z`; focused
  `bot_chat_live_event_cooldown` validation passed from
  `.tmp\bot_scenarios\20260623T010530Z`; and the full `implemented` catalog
  passed 88 rows with 0 failures from `.tmp\bot_scenarios\20260623T010536Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-event-cooldown-2026-06-23.md`.

## Native Validation Update: Bot Chat Live Enemy Sighted

Date: 2026-06-23

Tasks: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` now records event id `6`
  as live `enemy_sighted` chat when `sg_bot_chat_live_events` is enabled and
  the bot blackboard has a valid current visible enemy. Personality-specific
  reply phrases and per-spawn duplicate suppression are local game behavior.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `reply_chat_enemy_sighted` and
  `live_chat_enemy_sighted` in `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` reserves smoke mode `81`, marks it as an
  engage-enemy live chat proof, prints the `bot_chat_live_enemy_sighted`
  begin-marker field, and runs a two-bot TDM profile proof for stable combat
  contact.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, parser-fixture,
  command-construction, and documentation coverage for the new row.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 53 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  all eight staged q2aas AAS archive members; focused
  `bot_chat_live_enemy_sighted` validation passed from
  `.tmp\bot_scenarios\20260623T013832Z`; and the full `implemented` catalog
  passed 89 rows with 0 failures from `.tmp\bot_scenarios\20260623T013843Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-enemy-sighted-2026-06-23.md`.

## Native Validation Update: Bot Chat Live Low Health

Date: 2026-06-23

Tasks: `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` now records event id `9`
  as live `low_health` chat when `sg_bot_chat_live_events` is enabled and the
  bot's live health is at or below the chat low-health threshold. The event
  uses personality-specific reply phrases plus the existing safe dispatch,
  cooldown, duplicate, and bot-client broadcast protections.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `reply_chat_low_health` and
  `live_chat_low_health` in `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` reserves smoke mode `84`, reuses the
  survival-health route staging so the chat event is sourced from a real
  low-health gameplay state, prints `bot_chat_live_low_health=1`, and runs a
  one-bot FFA proof with `sg_bot_chat_live_events 1`.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add catalog, marker, parser-fixture,
  command-construction, and documentation coverage for the new row.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios`
  passed 53 tests; `meson compile -C builddir-win sgame_x86_64
  worr_ded_engine_x86_64 worr_ded_x86_64 copy_sgame_dll` passed; `.install/`
  was refreshed with current Windows binaries, packaged `basew` game data, and
  all eight staged q2aas AAS archive members; focused
  `bot_chat_live_low_health` validation passed from
  `.tmp\bot_scenarios\20260623T025752Z`; and the full `implemented` catalog
  passed 92 rows with 0 failures from `.tmp\bot_scenarios\20260623T025801Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-low-health-2026-06-23.md`.

## Native Validation Update: Bot Chat Live Objective Changed

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` routes smoke mode `86`
  through the existing CTF objective transition preparation path so the live
  objective-changed chat row is sourced from real CTF pickup, death-drop, and
  dropped-flag return hooks.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` keeps
  `bot_chat_live_objective_changed` hard-gated on objective counters, taxonomy
  size, route evidence, and zero failure counters while allowing natural
  `enemy_sighted` and `item_taken` co-events from the CTF run.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py` updates the
  scenario catalog assertions to avoid requiring `objective_changed` to remain
  the final live event after all gameplay observations finish.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  passed 53 tests; `meson compile -C builddir-win sgame_x86_64
  copy_sgame_dll` passed; `.install/` was refreshed with current Windows
  binaries and packaged `basew` game data; focused
  `bot_chat_live_objective_changed` validation passed from
  `.tmp\bot_scenarios\20260626T140601Z`; and the full `implemented` catalog
  passed 94 rows with 0 failures, timeouts, errors, or pending rows from
  `.tmp\bot_scenarios\20260626T140621Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-objective-changed-2026-06-26.md`.

## Native Validation Update: Bot Chat Live Flag State

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds live event id `8`
  / `flag_state` phrase, reply, and live-dispatch accounting and routes smoke
  mode `87` through the existing CTF objective transition preparation path.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `reply_chat_flag_state` and
  `live_chat_flag_state` through `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` exposes the `bot_chat_live_flag_state`
  frame-command smoke lane and begin marker for the dedicated server harness.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` adds the
  `bot_chat_live_flag_state` row, hard-gating CTF transition evidence,
  route ownership, flag-state counters, taxonomy size, and zero failure
  counters while allowing natural later `item_taken` or `enemy_sighted`
  co-events.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py` adds reserved
  mode `87` parser/catalog assertions and raw-marker coverage.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  passed 53 tests; `meson compile -C builddir-win sgame_x86_64
  copy_sgame_dll` passed; `meson compile -C builddir-win
  worr_ded_x86_64 sgame_x86_64 copy_sgame_dll` passed; `meson compile -C
  builddir-win worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll` passed;
  `.install/` was refreshed with current Windows binaries and packaged `basew`
  game data; focused `bot_chat_live_flag_state` validation passed from
  `.tmp\bot_scenarios\20260626Tflagstate3\20260626T144136Z`; and the full
  `implemented` catalog passed 95 rows with 0 failures, timeouts, errors, or
  pending rows from
  `.tmp\bot_scenarios\20260626Timplemented-flagstate-fixed\20260626T144511Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-flag-state-2026-06-26.md`.

## Native Validation Update: Bot Chat Live Blocked

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds live event id `10`
  / `blocked` phrase, reply, and live-dispatch accounting and emits the
  event from route-failure status when `sg_bot_chat_live_events` is enabled.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `reply_chat_blocked` and
  `live_chat_blocked` through `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` exposes the `bot_chat_live_blocked`
  frame-command smoke lane and begin marker, reusing the blocked rocketjump
  travel-type route proof to produce a deterministic route failure.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the mode `88` scenario, parser/catalog
  assertions, synthetic raw-marker coverage, and scenario documentation.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  passed 53 tests; `meson compile -C builddir-win worr_ded_engine_x86_64
  sgame_x86_64 copy_sgame_dll` passed; `.install/` was refreshed with current
  Windows binaries and packaged `basew` game data; focused
  `bot_chat_live_blocked` validation passed from
  `.tmp\bot_scenarios\20260626Tblocked-fixed\20260626T151437Z`; and the full
  `implemented` catalog passed 96 rows with 0 failures, timeouts, errors, or
  pending rows from
  `.tmp\bot_scenarios\20260626Timplemented-blocked\20260626T151446Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-blocked-2026-06-26.md`.

## Native Validation Update: Bot Chat Live Item Denied

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds live event id `5`
  / `item_denied` phrase, reply, and live-dispatch accounting and emits the
  event from team resource-denial policy deny counters when
  `sg_bot_chat_live_events` is enabled.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `reply_chat_item_denied` and
  `live_chat_item_denied` through `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` exposes the `bot_chat_live_item_denied`
  frame-command smoke lane and begin marker, reusing the four-bot TDM team
  resource-denial proof to produce deterministic deny-enemy item pressure.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the mode `89` scenario, parser/catalog
  assertions, synthetic raw-marker coverage, command-building coverage, and
  scenario documentation.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  passed 53 tests; `meson compile -C builddir-win worr_ded_engine_x86_64
  sgame_x86_64 copy_sgame_dll` passed; `.install/` was refreshed with current
  Windows binaries and packaged `basew` game data; focused
  `bot_chat_live_item_denied` validation passed from
  `.tmp\bot_scenarios\20260626Titem-denied\20260626T154429Z`; and the full
  `implemented` catalog passed 97 rows with 0 failures, timeouts, errors, or
  pending rows from
  `.tmp\bot_scenarios\20260626Timplemented-item-denied-json-file\20260626T154954Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-item-denied-2026-06-26.md`.

## Native Validation Update: Bot Chat Live Match Result

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T07`, `FR-04-T13`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_brain.cpp` adds live event id `11`
  / `victory_defeat` phrase, reply, and live-dispatch accounting and emits the
  event from native intermission or ended-match state when
  `sg_bot_chat_live_events` is enabled.
- WORR-native `src/game/sgame/gameplay/g_svcmds.cpp` and
  `src/game/sgame/g_local.hpp` expose `reply_chat_match_result` and
  `live_chat_match_result` through `q3a_bot_chat_policy_status`.
- WORR-native `src/server/main.c` exposes the `bot_chat_live_match_result`
  frame-command smoke lane and begin marker, requests the native intermission
  path during mode `90`, and prints matching intermission status evidence.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`, and
  `tools/bot_scenarios/README.md` add the mode `90` scenario, parser/catalog
  assertions, synthetic raw-marker coverage, command-building coverage, and
  scenario documentation.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  passed 53 tests; `meson compile -C builddir-win worr_ded_engine_x86_64
  sgame_x86_64 copy_sgame_dll` passed; `.install/` was refreshed with current
  Windows binaries and packaged `basew` game data; focused
  `bot_chat_live_match_result` validation passed from
  `.tmp\bot_scenarios\20260626Tmatch-result\20260626T182046Z`; and the full
  `implemented` catalog passed 98 rows with 0 failures, timeouts, errors, or
  pending rows from
  `.tmp\bot_scenarios\20260626Timplemented-match-result\20260626T182111Z`.
- Implementation log:
  `docs-dev/q3a-botlib-bot-chat-live-match-result-2026-06-26.md`.

## Native Validation Update: Coop Campaign Interaction Matrix

Date: 2026-06-26

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/server/main.c` reserves mode `91` for
  `coop_campaign_interaction_matrix`, maps it onto the coop live-loop setup,
  and lets the row reuse the travel-type goal path needed for campaign
  interaction evidence.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` adds the implemented
  `base1` scenario with `deathmatch 0`, `coop 1`, and
  `sg_bot_coop_live_loop 1`, hard-gating route-interaction retry, campaign
  mover source ownership, teammate hold commands, nav interaction candidates,
  coop wait policy, and route-clean frame-command status.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py` and
  `tools/bot_scenarios/README.md` add the synthetic raw-marker coverage,
  catalog/command assertions, marker-contract assertions, and scenario
  documentation for mode `91`.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools/bot_scenarios/test_run_bot_scenarios.py`
  passed 54 tests; `meson compile -C builddir-win worr_ded_engine_x86_64
  sgame_x86_64 copy_sgame_dll` passed; `.install/` was refreshed with current
  Windows binaries and packaged `basew` game data; focused
  `coop_campaign_interaction_matrix` validation passed from
  `.tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final\20260626T185108Z`;
  and the full `implemented` catalog passed 99 rows with 0 failures, timeouts,
  errors, or pending rows from
  `.tmp\bot_scenarios\20260626Timplemented-coop-campaign-interaction-json\20260626T185549Z`.
- Implementation log:
  `docs-dev/q3a-botlib-coop-campaign-interaction-matrix-2026-06-26.md`.

## Native Validation Update: Profile Autofill, Roam, And Combat Stabilization

Date: 2026-06-27

Tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T04`, `FR-04-T06`, `FR-04-T15`,
`FR-04-T16`, `DV-03-T05`, `DV-07-T06`

- WORR-native `assets/botfiles/bots.txt` records the first-party Q3-style
  bot manifest entries for `vanguard`, `vector`, `bulwark`, `relay`, and
  `smoke`.
- WORR-native `src/server/main.c` rotates `bot_min_players` autofill through
  loaded non-`smoke` profiles, keeps `bot_profile` as an explicit override,
  processes queued bot adds and min-player maintenance while local simulation
  is paused, and uses realtime for the one-add-per-frame throttle.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` keeps active FFA roam goals
  stable, lets item decisions defer generic roam, faces route targets unless
  an attack is actually firing, and makes role-combat defer when the base
  action layer is switching, weak/underpowered, or not pressing attack.
- WORR-native `tools/package_assets.py`, `tools/test_package_assets.py`,
  `tools/bot_scenarios/run_bot_scenarios.py`, and
  `tools/bot_scenarios/test_run_bot_scenarios.py` update package validation
  and scenario contracts around the stabilized behavior.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: package and scenario parser tests passed with 65 tests; profile
  validation passed with 5 profiles, 0 errors, and 0 warnings; Windows
  `worr_ded_engine_x86_64` and `sgame_x86_64` compile passed; `.install/` was
  refreshed and validated; focused live behavior validation passed from
  `.tmp\bot_scenarios\bot-profile-roam-state-fixes-rerun3`; legacy
  role-combat compatibility validation passed from
  `.tmp\bot_scenarios\bot-role-combat-compat-check3`; direct `.install`
  min-player smoke spawned `B|Bulwark`, `B|Relay`, and `B|Vanguard`.
- Implementation log:
  `docs-dev/q3a-botlib-profile-autofill-roam-combat-stabilization-2026-06-27.md`.

## Native Validation Update: Movement Matrix And Live Behavior

Date: 2026-06-28

Tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T04`, `FR-04-T05`, `FR-04-T06`,
`FR-04-T11`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`,
`DV-07-T06`

- WORR-native `src/server/main.c` reserves movement modes `92`, `93`, and
  `94`, mapping `93` and `94` to swim and waterjump route proofs on `q2dm2`.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` adds 11 movement
  scenarios: forced jump/crouch/swim commands and map-backed jump, ladder,
  walk-off-ledge, elevator, barrier-jump, rocket-jump, swim, and waterjump
  routes.
- WORR-native `src/game/sgame/bots/bot_brain.cpp` lets live item goals defer
  active FFA roam, keeps active FFA roam goals stable, faces route targets
  unless a selected attack is firing, and defers role combat behind weak,
  underpowered, weapon-switching, or non-attacking base action decisions.
- WORR-native `src/game/sgame/bots/bot_nav.cpp` preserves last item-goal
  telemetry after clears so pickup assignment evidence survives active route
  cleanup.
- WORR-native scenario tests keep live chat event rows gated on durable event
  counters instead of final-event ordering, because legitimate follow-up
  events may arrive after the event under test.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: Windows `worr_ded_engine_x86_64` and `sgame_x86_64` compile
  passed; `.install/` was refreshed and validated; the movement matrix passed
  11/11 rows from
  `.tmp\bot_scenarios\movement_matrix_expansion_rerun\20260627T232805Z`;
  behavior sanity passed 18/18 rows from
  `.tmp\bot_scenarios\behavior_sanity_rerun\20260627T232911Z`; direct
  `.install` min-player smoke loaded 5 profiles and autofilled `B|Bulwark`,
  `B|Relay`, and `B|Vanguard`; and the full implemented suite passed 110/110
  rows from `.tmp\bot_scenarios\implemented_rerun_after_fixes\20260627T234219Z`.
- Implementation log:
  `docs-dev/q3a-botlib-movement-matrix-and-live-behavior-round-2026-06-28.md`.

## Native Validation Update: Fresh Source-Counter Soak

Date: 2026-06-28

Tasks: `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T05`

- WORR-native `tools/bot_perf/analyze_bot_perf.py` now merges repeated
  frame-command and source-counter status lines so detailed long-soak counters
  survive later compact summary status lines.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` evaluates JSON perf
  budgets for degradation-policy scenarios, records compact `perf_budget`
  results in JSON/Markdown/text reports, and includes prefixed perf metrics in
  scenario comparison reports.
- WORR-native `tools/bot_perf/default_soak_budget.json` was refreshed to the
  2026-06-28 source-counter high-bot route-pressure baseline.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools\bot_perf\test_analyze_bot_perf.py -q`
  passed 14 tests; `python -m pytest
  tools\bot_scenarios\test_run_bot_scenarios.py -q` passed 57 tests; the
  final `high_bot_soak_degradation` source-counter soak passed from
  `.tmp\bot_scenarios\fresh_source_counter_soak_pass\20260628T090904Z` with
  `perf_budget.status=pass`, `source_counter_status=pass`, and all seven
  expected source-counter groups present.
- Implementation log:
  `docs-dev/q3a-botlib-fresh-source-counter-soak-2026-06-28.md`.

## Native Validation Update: Strict Source-Counter Budget Lane

Date: 2026-06-29

Tasks: `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T05`

- WORR-native `tools/bot_perf/source_counter_soak_budget.json` now defines the
  strict current-source high-bot budget lane.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` now evaluates
  multiple budget profiles for degradation-policy scenarios, preserving the
  primary default result as `perf_budget` and recording the full set as
  `perf_budgets`.
- WORR-native docs and tests now distinguish the legacy-compatible default
  budget from the strict current-source budget.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: Python compile checks passed for the scenario harness, scenario
  tests, and perf analyzer; the focused scenario/perf pytest suite passed
  72 tests.
- Implementation log:
  `docs-dev/q3a-botlib-strict-source-counter-budget-lane-2026-06-29.md`.

## Native Validation Update: Public Bot Surface Audit

Date: 2026-06-29

Tasks: `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

- WORR-native `tools/bot_surface/audit_bot_surface.py` now scans active
  server/sgame bot source and `docs-user/` for the current public bot cvar and
  command contract.
- The audit validates canonical `bot_*` public cvars, required defaults,
  Q3-style commands (`addbot`, `removebot`, `kickbots`, `botlist`,
  `bot_reload_profiles`), active-source legacy-prefix regressions, and
  smoke-only cvar leaks into user docs. Follow-up coverage also validates all
  13 public cvar defaults and matching `docs-user/bot-cvars.md` rows.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: Python compile checks passed for the bot surface audit and its
  tests; `python -m pytest tools\bot_surface\test_audit_bot_surface.py -q`
  passed 5 tests; the JSON audit artifact at
  `.tmp\bot_surface\public_bot_surface_audit.json` reports 94 bot cvars,
  5 bot commands, 13 public cvars, 13 public cvar defaults, 37 smoke-only
  hooks, 5 debug cvars, 39 experimental cvars, 0 violations, and 0 warnings.
- Implementation log:
  `docs-dev/q3a-botlib-public-bot-surface-audit-2026-06-29.md`.

## Native Validation Update: Release Acceptance Runner

Date: 2026-06-29

Tasks: `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

- WORR-native `tools/bot_release/run_bot_acceptance.py` now executes the bot
  release-readiness dry run across public cvars/commands, first-party profiles,
  `bots.txt`, authored/staged botfile payloads, staged reference AAS files,
  user docs, the multiplayer playtest plan, playtest triage coverage, and
  selected scenario-report evidence.
- The current acceptance artifact at
  `.tmp\bot_release\bot_release_acceptance.json` reports 10/10 checks passing
  against `.tmp\bot_scenarios\implemented_hazard_context.json`.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: Python compile checks passed for the release acceptance runner
  and tests; the text and JSON acceptance runs passed; `python -m pytest
  tools\bot_release\test_run_bot_acceptance.py -q` passed 7 tests.
- Implementation log:
  `docs-dev/q3a-botlib-release-acceptance-runner-2026-06-29.md`.

## Native Validation Update: Multiplayer Playtest Generator

Date: 2026-06-29

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

- WORR-native `tools/bot_playtest/generate_bot_playtest.py` now generates the
  FFA, Duel, TDM, and CTF manual playtest scaffold for bot release validation.
- The generated configs reset mode-specific bot policy cvars before enabling
  each case, use canonical `bot_` names, and exercise `bot_min_players` for the
  target population.
- The generated artifact at `.tmp\bot_playtest\bot_multiplayer_playtest.json`
  reports 4 cases, modes `CTF`, `Duel`, `FFA`, and `TDM`, 38 total minutes,
  and the first-party profile roster `vanguard`, `vector`, `bulwark`, `relay`,
  and `smoke`.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: Python compile checks passed for the playtest generator, its
  tests, the release acceptance runner, and acceptance tests; `python -m pytest
  tools\bot_playtest\test_generate_bot_playtest.py
  tools\bot_release\test_run_bot_acceptance.py
  tools\bot_surface\test_audit_bot_surface.py -q` passed 16 tests; the later
  triage follow-up refreshes the release acceptance artifact to 10/10 checks.
- Implementation log:
  `docs-dev/q3a-botlib-multiplayer-playtest-script-2026-06-29.md`.

## Native Validation Update: Playtest Evidence Triage

Date: 2026-06-29

Tasks: `FR-04-T04`, `FR-04-T06`, `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

- WORR-native `tools/bot_playtest/triage_bot_playtest.py` now converts
  generated multiplayer playtest notes into JSON and Markdown triage reports.
- The triage catalog maps generated failure signals to route commitment, route
  stuck, close-threat spacing, weak-state retreat, min-player autofill, Duel
  queue/active-count, CTF objective response, and team fire/spacing
  categories.
- The generated notes template at
  `.tmp\bot_playtest\bot_multiplayer_playtest_notes_template.json` gives each
  FFA, Duel, TDM, and CTF case stable fields for outcome, botlist, failure
  signals, custom signals, reproduction steps, and notes.
- The fresh triage artifact at
  `.tmp\bot_playtest\bot_multiplayer_playtest_triage.json` is pending by
  design before manual notes are filled in: 4 cases, 4 pending, 0 failure
  signals, 0 scenario candidates, and 0 warnings.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: Python compile checks passed for the playtest generator, triage
  tool, tests, release acceptance runner, and acceptance tests; `python -m
  pytest tools\bot_playtest\test_generate_bot_playtest.py
  tools\bot_playtest\test_triage_bot_playtest.py
  tools\bot_release\test_run_bot_acceptance.py
  tools\bot_surface\test_audit_bot_surface.py -q` passed 22 tests; the release
  acceptance artifact reports 10/10 checks passing.
- Implementation log:
  `docs-dev/q3a-botlib-playtest-evidence-triage-2026-06-29.md`.

## Native Validation Update: Public Defaults Docs Gate

Date: 2026-06-29

Tasks: `FR-04-T07`, `FR-04-T16`, `DV-07-T06`

- WORR-native `tools/bot_surface/audit_bot_surface.py` now validates every
  public bot cvar source default, not only the original required subset.
- The audit now requires `docs-user/bot-cvars.md` and validates a matching
  default row for each public bot cvar.
- `docs-user/bot-cvars.md` lists the supported public bot cvars, defaults,
  common practice setup, chat controls, and related Q3-style bot commands.
- `tools/bot_release/run_bot_acceptance.py` now requires the public bot cvar
  reference in the user-doc gate.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m pytest tools\bot_surface\test_audit_bot_surface.py
  -q` passed 6 tests; `python tools\bot_surface\audit_bot_surface.py --format
  json --output .tmp\bot_surface\public_bot_surface_audit.json` reports 0
  violations and 0 warnings; the broad bot/tooling pytest suite passed 126
  tests; release acceptance reports 10/10 checks passing.
- Implementation log:
  `docs-dev/q3a-botlib-public-defaults-docs-gate-2026-06-29.md`.

## Native Validation Update: Second Campaign Interaction Row

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` factors the mode
  `91` coop campaign marker contract and adds
  `coop_campaign_interaction_matrix_base2` on packaged campaign map `base2`.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py`,
  `tools/bot_scenarios/README.md`, `tools/bot_release/run_bot_acceptance.py`,
  and `tools/bot_release/README.md` add catalog, command, marker, and release
  acceptance coverage for the second campaign interaction row.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios
  tools.bot_release.test_run_bot_acceptance` passed 76 tests; focused
  `coop_campaign_interaction_matrix_base2` validation passed from
  `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json`.
- Implementation log:
  `docs-dev/q3a-botlib-second-campaign-interaction-row-2026-07-01.md`.

## Native Validation Update: Base2 Campaign Interaction Depth

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `tools/bot_scenarios/run_bot_scenarios.py` adds
  `coop_campaign_interaction_depth_base2`, a stricter mode `91` campaign row
  on `base2` that requires button, trigger, mover, use/touch, wait/use-frame,
  coop intent, and live pickup timing marker evidence.
- WORR-native `tools/bot_scenarios/test_run_bot_scenarios.py`,
  `tools/bot_scenarios/README.md`, `tools/bot_release/run_bot_acceptance.py`,
  and `tools/bot_release/README.md` add catalog, command, marker, and release
  acceptance coverage for the depth row.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `python -m unittest tools.bot_scenarios.test_run_bot_scenarios
  tools.bot_release.test_run_bot_acceptance` passed 77 tests; focused
  `coop_campaign_interaction_depth_base2` validation passed from
  `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`; release
  acceptance passed 15/15 checks from
  `.tmp\bot_release\bot_release_acceptance_campaign_depth.json`.
- Implementation log:
  `docs-dev/q3a-botlib-campaign-interaction-depth-base2-2026-07-01.md`.

## Native Validation Update: Base2 Campaign Post-Interaction Progression

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_nav.cpp`,
  `src/game/sgame/bots/bot_nav.hpp`, and
  `src/game/sgame/bots/bot_brain.cpp` add progression interaction lifecycle
  telemetry: commanded wait/use frames, completion on natural expiry or
  command-backed goal reset, post-interaction route refresh, post-window status,
  repeat suppression, and aggregate target-link selection counters.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`,
  `tools/bot_scenarios/README.md`, `tools/bot_release/run_bot_acceptance.py`,
  and `tools/bot_release/README.md` add
  `coop_campaign_post_interaction_base2`, require target-linked progression
  selection, completion, post-refresh, post-frame, and repeat-suppression
  evidence, and promote the row into release acceptance.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll`
  passed; `python tools\refresh_install.py --build-dir builddir-win
  --install-dir .install --base-game basew --platform-id windows-x86_64
  --package-q2aas-aas` refreshed `.install`; focused
  `coop_campaign_post_interaction_base2` validation passed from
  `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json` with
  `nav_interaction_progression_target_link_selections=3`,
  `nav_interaction_progression_completions=3`,
  `nav_interaction_progression_post_refreshes=3`,
  `nav_interaction_progression_post_frames=241`, and
  `nav_interaction_progression_repeat_suppressions=1`; `python -m unittest
  tools.bot_scenarios.test_run_bot_scenarios
  tools.bot_release.test_run_bot_acceptance` passed 80 tests; release
  acceptance passed 15/15 checks from
  `.tmp\bot_release\bot_release_acceptance_post_interaction.json`; the
  scenario catalog now reports 121 implemented rows and zero pending rows.
- Implementation log:
  `docs-dev/q3a-botlib-campaign-post-interaction-base2-2026-07-01.md`.

## Native Validation Update: Base2 Campaign Progression Carry

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_nav.cpp`,
  `src/game/sgame/bots/bot_nav.hpp`, and
  `src/game/sgame/bots/bot_brain.cpp` add per-route-slot progression
  completion memory and aggregate status counters for follow-up completions,
  distinct follow-up completions, completed clients, and the last carried
  previous/current entity pair.
- WORR-native `src/server/game.c` now skips bot clients in single-client
  q2proto game-import output paths (`PF_Unicast`, `PF_Client_Print`,
  `PF_Center_Print`, and `PF_LocalSound`). The focused carry run exposed the
  existing crash when a bot triggered `G_PlayerNotifyGoal`, which attempted to
  send localized objective text through a bot client's null network writer.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`,
  `tools/bot_scenarios/README.md`, `tools/bot_release/run_bot_acceptance.py`,
  and `tools/bot_release/README.md` add
  `coop_campaign_progression_carry_base2`, require the carry counters, and
  promote the row into release acceptance.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `meson compile -C builddir-win worr_ded_x86_64
  worr_ded_engine_x86_64 sgame_x86_64 copy_sgame_dll` passed; `python
  tools\refresh_install.py --build-dir builddir-win --install-dir .install
  --base-game basew --platform-id windows-x86_64 --package-q2aas-aas`
  refreshed `.install`; focused `coop_campaign_progression_carry_base2`
  validation passed from
  `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json` with
  `nav_interaction_progression_completions=3`,
  `nav_interaction_progression_carry_completions=2`,
  `nav_interaction_progression_carry_distinct_completions=2`,
  `last_nav_interaction_progression_carry_previous_entity=42`,
  `last_nav_interaction_progression_carry_entity=332`, and
  `last_nav_interaction_progression_carry_distinct_count=3`; `python -m
  unittest tools.bot_scenarios.test_run_bot_scenarios
  tools.bot_release.test_run_bot_acceptance` passed 81 tests; release
  acceptance passed 15/15 checks from
  `.tmp\bot_release\bot_release_acceptance_progression_carry.json`; the
  scenario catalog now reports 122 implemented rows and zero pending rows.
- Implementation log:
  `docs-dev/q3a-botlib-campaign-progression-carry-base2-2026-07-01.md`.

## Native Validation Update: Train Campaign Keyed Path

Date: 2026-07-01

Tasks: `FR-04-T04`, `FR-04-T05`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

- WORR-native `src/game/sgame/bots/bot_nav.cpp` and
  `src/game/sgame/bots/bot_brain.cpp` add key entity/lock classification,
  `trigger_key` progression scoring, stable keyed-path telemetry, and the raw
  mode `91` train proof route.
- WORR-native `tools/bot_scenarios/run_bot_scenarios.py`,
  `tools/bot_scenarios/test_run_bot_scenarios.py`,
  `tools/bot_scenarios/README.md`, `tools/bot_release/run_bot_acceptance.py`,
  and `tools/bot_release/README.md` add `coop_campaign_keyed_path_train`,
  require key-path selection and required-key counters, and promote the row
  into release acceptance.
- No new upstream Q3A, Gladiator, BSPC, idTech3, Quake3e, baseq3a, or
  `q2proto/` source files were imported or modified for this round.
- Validation: `meson compile -C builddir-win sgame_x86_64 copy_sgame_dll`
  passed; `python tools\refresh_install.py --build-dir builddir-win
  --install-dir .install --base-game basew --platform-id windows-x86_64
  --package-q2aas-aas` refreshed `.install`; focused
  `coop_campaign_keyed_path_train` validation passed from
  `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json` with
  `nav_interaction_progression_key_path_selections=1`,
  `last_nav_interaction_progression_key_path_key_lock=1`, and
  `last_nav_interaction_progression_key_path_required_item=70`; `python -m
  unittest tools.bot_scenarios.test_run_bot_scenarios
  tools.bot_release.test_run_bot_acceptance` passed 82 tests; release
  acceptance passed 15/15 checks from
  `.tmp\bot_release\bot_release_acceptance_keyed_path.json`; the scenario
  catalog now reports 123 implemented rows and zero pending rows.
- Implementation log:
  `docs-dev/q3a-botlib-campaign-keyed-path-train-2026-07-01.md`.

## Candidate Source Inventory

These files were audited as likely first candidates or reference points. BSPC candidates now land through the `tools/q2aas/` snapshot; the first Q3A utility, AAS file-loader, AAS sampling, AAS reachability, AAS clustering, AAS route-query, AAS alternative-routing, AAS optimization, AAS start-frame, AAS entity-cache, AAS movement, and AAS debug helper subsets are imported and recorded above, while the WORR-owned entity-sync, entity-trace, BSP leaf-link/box-query, debug draw, route-overlay, debug-polygon, debug-area, cluster, alternative-route, memory allocator, filesystem, route-cache miss policy, lifecycle telemetry, bot frame command dispatch, route-steered frame command, nav route-cache, nav debug-overlay, nav reachability-debug, nav polyline-debug, nav debug-client-filter, nav persistent-goal, nav item-goal, nav item-reservation, nav look-ahead steering, nav velocity-aware steering, nav route-target stabilization, route-target anti-spin, route movement projection, trace-checked corner cutting, nav stuck-repath, nav stuck recovery command, nav goal-blacklist cooldown, nav failed-goal reason, nav movement-state commands, bot brain command ownership, target-memory retention and decay telemetry, weapon scoring arsenal telemetry, aim/fire policy depth telemetry, ammo pressure pickup telemetry, survival inventory-use telemetry, survival health-route telemetry, survival armor-route telemetry, combat/survival regression telemetry, q2dm2 second-map combat/survival regression telemetry, q2dm8 combat/retreat map-regression telemetry, CTF objective live-loop telemetry, CTF objective transition telemetry, TDM role spawn-stability telemetry, FFA live-pacing telemetry, Duel live-pacing telemetry, coop live-loop combined validation, coop share-loop target/resource validation, coop campaign interaction matrix validation, coop campaign progression-chain validation, coop campaign progression-consumer validation, coop campaign post-interaction progression validation, coop campaign progression-carry validation, coop campaign keyed-path validation, dedicated-server bot print safety, bot chat live-event taxonomy, cooldown, enemy-sighted, duplicate-suppression, low-health, item-taken, objective-changed, flag-state, blocked, item-denied, match-result validation, and match-result status-surface telemetry, nuke retreat route ownership, timed route-goal ownership, teleporter escape route ownership, team role route ownership, team item-role route selection, FFA item-role route selection, CTF item-role route selection, team fire-avoidance command suppression, team role-combat command ownership, FFA roam-route ownership, FFA role-combat command ownership, FFA spawn-camp-combat-avoidance command veto, Duel deny-enemy item scoring, Duel route/combat pacing status reuse, team resource-denial pickup scoring, match item-policy umbrella scoring, behavior policy umbrella cvar/status/scenario gating, profile role-policy status/scenario gating, profile team-policy status/scenario gating, profile item-policy status/scenario gating, profile movement-policy status/scenario gating, bot chat-policy live-dispatch, team-policy, rate-policy, initial-policy, reply-policy, event-policy, live-events policy, behavior-arbitration owner/cvar status/scenario gating, and target-memory blackboard status/scenario gating, CTF role-route ownership, CTF role-combat command ownership, CTF dropped-flag route ownership, CTF carrier-support route ownership, CTF base-return route ownership, CTF objective route-policy ownership, CTF objective route precedence ownership, CTF objective pickup/drop/return transition ownership, coop leader route ownership and validation gating, coop lead-advance route ownership, coop progress-wait command ownership, coop interaction-retry command ownership, coop resource-share route selection, coop anti-blocking command ownership, coop target-sharing blackboard adoption, coop door/elevator source-hold command ownership, bot warmup readiness status and smoke validation, bot vote-exclusion status and smoke validation, bot admin-audit status/attempt smoke validation, bot tournament status/veto/replay smoke validation, match logging schema status and smoke validation, match logging catalog/index status and smoke validation, bot MyMap status/queue/consume smoke validation, bot queued-nextmap transition status and smoke validation, bot map-vote status/finalize smoke validation, bot scoreboard classification status and smoke validation, bot intermission cleanup status and smoke validation, nav position-goal, nav natural travel-goal including barrier-jump direct reach validation, nav rocket-jump route policy, nav four-bot frame-command smoke, nav eight-bot frame-command smoke, nav soak frame-command smoke, nav map-change repeat/restart smoke, map-restart cleanup scenario promotion, warmup bot-start readiness scenario promotion, vote bot-exclusion scenario promotion, admin bot privilege audit scenario promotion, tournament bot veto-exclusion scenario promotion, tournament replay reset scenario promotion, match logging schema scenario promotion, match logging catalog/index scenario proof, MyMap queue scenario promotion, queued nextmap transition scenario promotion, map-vote bot-exclusion transition scenario promotion, scoreboard bot-classification scenario promotion, intermission bot-cleanup scenario promotion, nav natural movement support diagnostics, behavior action dispatcher and telemetry boundary, weapon/inventory command-request API and exact dispatch, aim/fairness and live-aim/projectile-leading helper APIs, live combat policy consumption, live item timing consumers, item timer fairness helper policy, special-item utility buckets, static BSP trace CPU counters, entity-clip CPU counters, AAS memory source counters, source-counter completeness diagnostics, FFA/TDM/CTF/Duel objective-side helper policy, team-role policy and lane/depth helpers, coop/resource policy helpers, status harness/status surface expansion, bot validation tooling, scenario coverage expansion and marker hardening including the current 123-row implemented catalog total, profile behavior validation, botfile behavior-depth metadata, botfile parity polish, public bot/user documentation, competitive server tools operator documentation, high-bot degradation policy and soak budget, q2aas reference-map coverage and available-reference validation reporting, q2aas required-feature gap diagnostics, q2aas binary/license notice policy, release packaging hardening, Q3-style WORR botfile layout correction, and legacy Q2R bot surface removal work is recorded as native adapter, tooling, asset, documentation, status, or replacement work. The remaining Q3A runtime and behavior files remain reference-only until matched to a pinned source.

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
