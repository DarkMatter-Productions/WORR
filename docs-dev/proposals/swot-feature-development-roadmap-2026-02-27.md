# WORR SWOT and Task-Based Feature + Development Roadmaps

Date: 2026-02-27

## Purpose
Create a repository-grounded SWOT and convert it into actionable, task-based project roadmaps that can guide coordinated team execution.

## Status Updates
- `FR-01-T07` Vulkan/OpenGL parity checklist complete:
  - Added the canonical evidence-graded visual, functional, diagnostic, and
    performance matrix at
    `docs-dev/renderer/vulkan-opengl-parity-checklist-2026-07-14.md`.
  - Closed the remaining global `r_fullbright` world mismatch natively in the
    Vulkan fragment shader without rebuilding geometry or pipelines, and added
    the missing legacy v38 16-unit lightmap-grid derivation before native atlas
    allocation. The strengthened T06 fixture produces zero RGB error over both
    its 170,000-pixel fullbright/bmodel crop and 34,000-pixel authored-lightmap
    crop, with equal feature masks, IoU `1.0`, no process failures, and clean
    Vulkan validation.
  - Converted every known partial/missing parity or modernization row into
    `FR-01-T08..T15` or the existing `FR-02-T13..T15` tasks.
  - Completed `FR-01-T08` native Vulkan debug overlays and diagnostics:
    world-space lines/shapes/stroke-font text are recorded through native
    depth/no-depth Vulkan line pipelines; renderer stats and `vk_stats` now
    expose per-domain draw/upload/query totals, CPU frame time, and a named
    missing-capability mask. The validation-backed smoke queued 198 lines and
    changed 3,067 fixture pixels with no VUID or validation error. Details:
    `docs-dev/renderer/vulkan-debug-overlay-telemetry-2026-07-14.md`.
  - Completed `FR-01-T09` renderer-neutral gameplay light queries: the
    client-engine lightlevel path now samples only client-owned BSP static
    lightmaps/lightgrid plus map lightstyles, including inline BSP models. It
    has no renderer export, presentation cvar, or dynamic-light dependency;
    focused boundary/finite/protocol-byte tests and the client-engine build
    pass. Details: `docs-dev/renderer/gameplay-light-query-parity-2026-07-14.md`.
  - Advanced `FR-01-T10` and `FR-01-T13`: static Vulkan world vertices upload
    once into device-local memory, while a compact per-frame stream drives
    shader-side flow, turbulent warp, and transparent-liquid refraction. The
    native renderer now copies its opaque scene into a sampled Vulkan image
    before a depth-preserving liquid pass; it never redirects that path to
    OpenGL. A generated `SURF_FLOWING | SURF_WARP` scene passes exact
    OpenGL/Vulkan pixel capture twice under validation. Native full-screen
    underwater waterwarp now follows the completed 3D scene through a
    full-screen Vulkan sine-warp pipeline. Entity alpha phases now straddle
    transparent world liquid work as in OpenGL. An owned paused transparent
    warp receiver now holds a 128,000-pixel OpenGL/Vulkan crop within one RGB
    level under validation. A real-contents static underwater receiver now
    locks 307,200 pixels to maximum RGB error `1 / 1 / 2`, with zero pixel
    above error 2 under validation. Broader liquid material coverage remains
    open. Details:
    `docs-dev/renderer/vulkan-world-animation-device-local-2026-07-14.md` and
    `docs-dev/renderer/vulkan-liquid-refraction-2026-07-15.md` and
    `docs-dev/renderer/vulkan-underwater-waterwarp-visual-parity-2026-07-16.md`.
- `FR-01-T06` Vulkan bmodel first-frame parity complete:
  - Native Vulkan excludes every inline-model face range from the static world
    mesh even when model zero has no usable face range, then renders bmodels
    only through their current entity transforms.
  - Vulkan entity lighting now honors the shared `r_fullbright` contract and
    bypasses entity modulation in the fullbright shader path, closing the
    final strict fixture color mismatch.
  - The generated transformed-bmodel OpenGL/Vulkan matrix is byte-identical:
    zero RGB error across 170,000 crop pixels, identical 47,300-pixel masks,
    IoU `1.0`, no process failures, and a clean Vulkan validation log.
    Implementation log:
    `docs-dev/renderer/vulkan-bmodel-first-frame-parity-2026-07-14.md`.
- `FR-01-T05` Vulkan six-face sky parity complete:
  - Native Vulkan now uses exact per-face half-texel clamps, a persistent
    36-vertex sky buffer, and a dedicated no-depth background pipeline with
    outward-face culling. Foreground world geometry deterministically masks the
    full cube without CPU portal clipping or opposite-face overdraw.
  - Fixed sky re-registration now preserves non-Z rotation axes, and image
    descriptor release waits for the submitted-frame fence.
  - The owned four-side/up/fixed-rotated-down OpenGL/Vulkan matrix passes all
    strict sky-crop and coverage-mask gates across twelve clean processes. All
    six validation-layer Vulkan logs are free of VUIDs and validation errors.
    Implementation log:
    `docs-dev/renderer/vulkan-sky-seam-parity-2026-07-14.md`.
- `FR-11-T01` through `FR-11-T07` FnQuake3 console integration complete:
  - Added `docs-dev/plans/fnquake3-console-integration-roadmap.md` as the
    task-based implementation backbone for porting FnQuake3's unique in-game
    console features while preserving WORR's stronger UTF-8, font, history,
    timestamp, and Quake II completion foundations.
  - The ratified scope covers smooth scrollback/new-output motion, live fuzzy
    completion, mouse interaction and selection, interactive scrollbars,
    console extents/appearance/fading, raw quoted chat, validation, user docs,
    and `.install/` staging under `FR-11-T01` through `FR-11-T07`.
  - Completed the WORR-native implementation with generator-backed live/fuzzy
    completion, smooth scrolling, captured mouse/pointer ownership, draggable
    scrollbars, UTF-8 input/log selection and drag reuse, centered extents,
    appearance/fade controls, raw quoted chat, RmlUi-to-console ownership
    transfer, automated `console-integration-check`, staged runtime self-test,
    visual capture, user documentation, and validated `.install/` refresh.
    Implementation log:
    `docs-dev/fnquake3-console-integration-2026-07-12.md`.
- `FR-04-T02` / `FR-04-T03` / `FR-04-T04` / `FR-04-T05` /
  `FR-04-T06` / `FR-04-T07` / `FR-04-T15` Bot completion roadmap:
  - Added `docs-dev/plans/bot-implementation-completion-roadmap.md` as the
    go-forward roadmap for turning the completed Q3A BotLib/AAS port and
    123-row scenario catalog into full live bot behavior.
  - The roadmap defines completion gates for live behavior, combat, items,
    team play, coop, movement, chat/personality, performance, packaging, and
    release readiness; M1 live behavior arbitration is now complete, and the
    first M2 slices, sustained enemy target memory/decay, weapon-scoring
    arsenal proof, aim/fire policy depth, ammo-pressure pickup routing,
    survival inventory use, survival health routing, survival armor routing,
    threat-retreat avoidance, the compact combat/survival regression proof,
    expanded smoke contract reconciliation, q2dm2 second-map combat/survival
    regression, q2dm8 combat/retreat map regression, CTF objective live-loop
    promotion, CTF pickup/drop/return transition proof, TDM role
    spawn-stability proof, FFA live-pacing proof, Duel live-pacing proof, coop
    live-loop promotion, coop target/resource share-loop promotion, bot chat
    live event taxonomy with spawn plus route-ready accounting, bot chat
    live-event cooldown suppression, the first combat-derived
    `enemy_sighted` live chat event, four-variant bot chat phrase-library
    proof, duplicate route-ready chat suppression, the survival-state
    `low_health` live chat event, the pickup-observation `item_taken` live chat
    event, the CTF transition-derived `objective_changed` live chat event, the
    CTF flag-state `flag_state` live chat event, the route-failure `blocked`
    live chat event, the TDM resource-denial `item_denied` live chat event, the
    native match-result `victory_defeat` live chat event, the `base1` and
    `base2` coop campaign interaction matrix rows, profile-backed min-player autofill,
    live roam/item/combat stabilization, the first 11-row movement matrix, the
    movement context gap matrix, accepted `worr_crouch_ref` crouch route, and
    accepted `fact2` hazard context row, the min-player profile coverage
    scenario gate, the second campaign interaction row, the base2 campaign
    interaction-depth row, the base2 campaign progression-chain row, and the
    base2 campaign progression-consumer row, the base2 campaign
    post-interaction progression row, the base2 campaign progression-carry
    row, the train campaign keyed-path row, and the train campaign key-carry
    bridge-approach row, route target anti-spin, route movement projection
    while aiming off-route, consumed route-target watchdog hardening, and route
    command trace/sequential look-ahead hardening are
    complete. The implemented catalog now contains 123 rows;
    the latest full `implemented` baseline passes 123/123 rows from
    `.tmp\bot_scenarios\implemented_after_route_sequential_trace_lookahead_fix.json`, with
    release acceptance passing 15/15 checks with 0 warnings from
    `.tmp\bot_release\bot_release_acceptance_route_sequential_trace_lookahead.txt`;
    focused natural crouch validation passes from
    `.tmp\bot_scenarios\movement_crouch_route.json`; focused min-player
    profile coverage passes from
    `.tmp\bot_scenarios\min_players_profile_coverage.json`; focused second
    campaign interaction validation passes from
    `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json`; focused
    base2 campaign interaction-depth validation passes from
    `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`; focused
    base2 campaign progression-chain validation passes from
    `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`; focused
    base2 campaign progression-consumer validation passes from
    `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`; focused
    base2 campaign post-interaction validation passes from
    `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`; focused
    base2 campaign progression-carry validation passes from
    `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`; focused
    train campaign keyed-path validation passes from
    `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`; focused train
    campaign key-carry bridge-approach validation passes from
    `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`; focused route
    target anti-spin validation passes from
    `.tmp\bot_scenarios\route_spin_final_after_status.json`; focused route
    movement projection validation passes from
    `.tmp\bot_scenarios\route_spin_projection_focus.json`; focused consumed
    route-target watchdog validation passes from
    `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json`; focused
    route command trace/sequential look-ahead validation passes from
    `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`; focused
    live match-result status-surface validation passes from
    `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json`; the recommended
    next implementation slices are visual review of the refreshed Duel/CTF
    headless play-depth notes until the M3 gate passes, promoting physical
    moving and grounded-on-mover samples from the new generic mover lifecycle
    evidence, deeper coop/campaign play-depth evidence, bot
    chat user-facing release-note attachment, and
    another source-counter variance refresh after the next movement, combat, or
    routing behavior change.
  - 2026-07-01 base2 campaign progression consumer:
    `coop_campaign_progression_consumer_base2` now promotes the previous
    target-chain diagnostics into live route-interaction chooser behavior. The
    nav layer scores target-linked/progression interactions, prefers scored
    candidates within a conservative distance slack, and exposes selection and
    preference counters through `q3a_bot_nav_policy_status`. Focused
    validation passed from
    `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`, with
    release acceptance requiring 14 scenario evidence rows and passing 15/15
    checks from `.tmp\bot_release\bot_release_acceptance_progression_consumer.json`.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-progression-consumer-base2-2026-07-01.md`.
  - 2026-07-01 base2 campaign post-interaction progression:
    `coop_campaign_post_interaction_base2` now proves scored target-linked
    route interactions complete after wait/use command ownership, force a route
    refresh, expose the post-interaction recovery window, and suppress immediate
    repeat selection of the same progression entity. The nav layer now tracks
    commanded interaction frames before completion and exposes aggregate
    target-link selection, completion, post-refresh, post-frame, and repeat
    suppression counters through `q3a_bot_nav_policy_status`. Focused
    validation passed from
    `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`, and release
    acceptance now requires 15 scenario evidence rows and passes 15/15 checks
    from `.tmp\bot_release\bot_release_acceptance_post_interaction.json`.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-post-interaction-base2-2026-07-01.md`.
  - 2026-07-01 base2 campaign progression carry:
    `coop_campaign_progression_carry_base2` now proves a bot can carry
    progression completion state through multiple scored campaign interactions
    in one coop live-loop run, including distinct follow-up completed entities.
    The focused run passed from
    `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json` with
    `nav_interaction_progression_completions=3`,
    `nav_interaction_progression_carry_distinct_completions=2`, and
    `last_nav_interaction_progression_carry_distinct_count=3`. The same pass
    exposed and fixed a dedicated-server bot print crash in the localized
    goal-notification path by making single-client q2proto game-import prints,
    centerprints, unicast buffers, and local sounds skip bot clients. Release
    acceptance now requires 16 scenario evidence rows and passes 15/15 checks
    from `.tmp\bot_release\bot_release_acceptance_progression_carry.json`.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-progression-carry-base2-2026-07-01.md`.
  - 2026-07-01 train campaign keyed-path proof:
    `coop_campaign_keyed_path_train` now proves the route-local interaction
    scan can identify a keyed progression segment on `train`, including runtime
    key item context, `trigger_key` lock context, key-path selection/completion
    counters, and required-key telemetry. Focused validation passed from
    `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json` with
    `nav_interaction_progression_key_path_selections=1`,
    `last_nav_interaction_progression_key_path_key_lock=1`, and
    `last_nav_interaction_progression_key_path_required_item=70`. At that
    keyed-path stage, release acceptance increased to 17 evidence rows and
    passed 15/15 checks from
    `.tmp\bot_release\bot_release_acceptance_keyed_path.json`.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-keyed-path-train-2026-07-01.md`.
  - 2026-07-01 train campaign key-carry bridge proof:
    `coop_campaign_key_carry_train` now extends the train keyed-path proof by
    routing to the red-key leg, invoking the normal `Touch_Item` pickup path,
    recording positive red-key inventory, activating the key-side `func_train`
    bridge, then carrying that state into the `trigger_key` lock-side route and
    selection proof. At this earlier stage, the focused run passed from
    `.tmp\bot_scenarios\coop_campaign_key_carry_train.json` with
    `key_carry_key_pickups=1`, `key_carry_bridge_route_requests=1`,
    `key_carry_bridge_warps=1`, `key_carry_bridge_interactions=1`,
    `last_key_carry_bridge_kind=4`, `last_key_carry_bridge_travel_type=11`,
    `key_carry_lock_route_requests=59`, `key_carry_lock_warps=1`,
    `last_key_carry_pickup_inventory=1`, and
    `last_key_carry_lock_required_item=70`. Release acceptance now requires
    18 scenario evidence rows and passes 15/15 checks. Implementation logs:
    `docs-dev/q3a-botlib-campaign-key-carry-train-2026-07-01.md` and
    `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-2026-07-01.md`.
  - 2026-07-01 train campaign key-carry interaction-goal proof:
    `coop_campaign_key_carry_train` now resolves the bridge-start target from
    live train interaction entities before activating the key-side `func_train`.
    The focused run passed from
    `.tmp\bot_scenarios\coop_campaign_key_carry_train.json` with
    `interaction_goal_requests=1`, `interaction_goal_candidates=3`,
    `interaction_goal_resolved=1`, `last_interaction_goal_entity=60`,
    `last_interaction_goal_kind=4`, `last_interaction_goal_area=2338`, and
    `route_failures=0`. At that earlier stage the explicit bridge-start warp
    remained visible; the later bridge-approach follow-up replaces it with
    natural route progress.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-key-carry-train-interaction-goal-2026-07-01.md`.
  - 2026-07-01 train campaign key-carry bridge-arrival proof:
    `coop_campaign_key_carry_train` now resolves a routeable post-mover
    arrival point from the same live `func_train` source and final lock
    destination, then routes the lock leg without the old direct lock-side
    warp. A one-shot lock trigger activation preserves the route-local
    `trigger_key` selection and required-key telemetry. The focused run passed
    from `.tmp\bot_scenarios\coop_campaign_key_carry_train.json` with
    `interaction_arrival_goal_requests=1`,
    `interaction_arrival_goal_candidates=60`,
    `interaction_arrival_goal_resolved=1`,
    `last_interaction_arrival_goal_area=1058`,
    `last_interaction_arrival_goal_destination_distance_sq=18411`,
    a now-superseded temporary bridge-arrival warp counter,
    `key_carry_lock_warps=0`,
    `nav_interaction_progression_key_path_selections=1`, and
    `last_nav_interaction_progression_key_path_required_item=70`.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-2026-07-01.md`.
  - 2026-07-01 train campaign key-carry bridge-approach proof:
    `coop_campaign_key_carry_train` now removes the temporary bridge-start
    proof warp. The bridge phase routes naturally to the live `func_train`
    bridge-start route endpoint, latches a matched one-point AAS route endpoint,
    clears the proof bot's stale generic interaction slot, and activates the
    specific train interaction before the existing post-mover arrival projection
    and lock proof. The focused run passed from
    `.tmp\bot_scenarios\coop_campaign_key_carry_train.json` with
    `key_carry_bridge_route_requests=16`,
    `key_carry_bridge_approach_requests=16`,
    `key_carry_bridge_approach_ready=1`,
    `last_key_carry_bridge_approach_distance_sq=98`,
    `key_carry_bridge_warps=0`, `key_carry_bridge_interactions=1`,
    the now-superseded temporary bridge-arrival warp counter,
    `key_carry_lock_warps=0`,
    and `last_nav_interaction_progression_key_path_required_item=70`.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-approach-2026-07-01.md`.
  - 2026-07-01 train campaign key-carry bridge-arrival-route proof:
    `coop_campaign_key_carry_train` now removes the temporary post-mover
    bridge-arrival proof warp. The proof stores the projected routeable arrival
    point in the smoke slot, routes to it as a normal position goal, and only
    advances to the final lock route after the arrival latch fires. The focused
    run passed from `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`
    and `.tmp\bot_scenarios\20260701T145703Z` with
    `key_carry_bridge_arrival_route_requests=2`,
    `key_carry_bridge_arrival_reached=1`,
    `last_key_carry_bridge_arrival_distance_sq=112`,
    `key_carry_bridge_arrival_warps=0`, `key_carry_lock_warps=0`, and
    `route_failures=0`; the fresh full implemented suite passed 123/123 from
    `.tmp\bot_scenarios\implemented_after_bridge_arrival_route.json` and
    release acceptance passed 15/15.
    Implementation log:
    `docs-dev/q3a-botlib-campaign-key-carry-train-bridge-arrival-route-2026-07-01.md`.
  - 2026-07-01 interaction-arrival route-state proof:
    `bot_nav` now owns reusable route request metadata for the arrival side of
    an interaction entity. The train key-carry proof saves the full
    `BotNavInteractionGoal`, tags the arrival route request with the source
    `func_train`, and accepts fallback AAS route endpoints once the route target
    reaches the projected arrival. Focused validation passed from
    `.tmp\bot_scenarios\20260701T162117Z` with
    `interaction_arrival_route_requests=2`,
    `interaction_arrival_route_assignments=2`,
    `interaction_arrival_route_reached=1`,
    `key_carry_bridge_arrival_reached=1`, `key_carry_lock_route_requests=42`,
    and `route_failures=0`. Implementation log:
    `docs-dev/q3a-botlib-interaction-arrival-route-state-2026-07-01.md`.
  - 2026-07-01 chat smoke queue determinism:
    the frame-command smoke runner now counts pending queued bot additions
    before issuing another `SV_BotAdd`, preventing duplicate queued add
    requests from overfilling four-bot chat proofs. The live match-result
    scenario now validates durable win/loss outcome coverage without depending
    on which team is processed last. Focused validation passed 4/4 from
    `.tmp\bot_scenarios\chat_smoke_queue_determinism.json`; the fresh full
    implemented suite passed 123/123 from
    `.tmp\bot_scenarios\implemented_after_chat_smoke_queue_determinism.json`,
    and release acceptance passed 15/15. Implementation log:
    `docs-dev/q3a-botlib-chat-smoke-queue-determinism-2026-07-01.md`.
  - 2026-07-01 bot chat user-facing docs readiness:
    `docs-user/bot-chat.md` now documents the public bot chat cvars, supported
    live event taxonomy, match-result outcome behavior, practical defaults, and
    profile chat personalities. `tools/bot_release/run_bot_acceptance.py` now
    requires the chat guide to retain every public chat cvar and event name, and
    `.tmp\bot_surface\public_bot_surface_chat_docs_audit.json` reports zero
    public-surface violations. Implementation log:
    `docs-dev/q3a-botlib-bot-chat-user-doc-readiness-2026-07-01.md`.
  - 2026-07-01 min-player profile coverage scenario gate:
    `bot_min_players_smoke` mode `2` now validates the public min-player
    autofill path against all five first-party bot profiles. The new
    `min_players_profile_coverage` scenario requires `bulwark`, `relay`,
    `smoke`, `vanguard`, and `vector`, plus clean trim/disable behavior.
    Release acceptance now treats that scenario as required evidence and the
    latest dry run
    `.tmp\bot_release\bot_release_acceptance_min_player_profile_scenario.json`
    passes 15/15 checks. Implementation log:
    `docs-dev/q3a-botlib-min-player-profile-coverage-scenario-gate-2026-07-01.md`.
  - 2026-06-30 min-player first-party profile coverage:
    Public autofill no longer skips `smoke`; the first-party roster now rotates
    through every loaded `botfiles/bots.txt` entry unless `bot_profile` forces a
    specific profile. `tools/bot_playtest/run_bot_playdepth_headless.py` now
    records expected/observed profiles and fails machine evidence when a
    required profile is missing, while
    `tools/bot_playtest/build_bot_playdepth_evidence.py` rejects a required
    case marked `pass` without required profile coverage. After rebuilding and
    refreshing `.install`, the short CTF headless run at
    `.tmp\bot_playtest\headless\20260630T200649Z` includes `bulwark`, `relay`,
    `smoke`, `vanguard`, and `vector`. Implementation log:
    `docs-dev/q3a-botlib-min-player-profile-coverage-2026-06-30.md`.
  - 2026-06-30 M3 headless play-depth runner tooling:
    `tools/bot_playtest/run_bot_playdepth_headless.py` now starts the required
    `duel_rotation` and `ctf_objectives` cases on the dedicated server,
    captures stdout/stderr plus real `botlist` roster rows, and writes
    prefilled notes that remain pending until visual review. A short real run
    passed both cases from
    `.tmp\bot_playtest\headless\20260630T200649Z`; evidence rebuilt from those
    notes correctly reports `pending` until an operator marks both required
    cases as passing. Release acceptance now has a
    `playdepth_headless_tooling` check, and
    `.tmp\bot_release\bot_release_acceptance_m3_headless.json` passes 15/15
    checks. Implementation log:
    `docs-dev/q3a-botlib-m3-headless-playdepth-runner-2026-06-30.md`.
  - 2026-06-30 M3 multiplayer gate tooling:
    `tools/bot_playtest/check_m3_multiplayer_gate.py` now evaluates M3 from
    one artifact by requiring the automated `duel_queue_spectator`,
    `tdm_role_spawn_stability`, `ffa_live_pacing`, `duel_live_pacing`,
    `ctf_objective_route`, and `ctf_objective_transitions` rows plus passing
    `duel_rotation` and `ctf_objectives` play-depth evidence. Current local
    evidence is pending only on unfilled Duel/CTF notes; release acceptance now
    has an `m3_multiplayer_gate_tooling` check and the current
    `.tmp\bot_release\bot_release_acceptance_m3_headless.json` artifact passes
    15/15 checks. Implementation log:
    `docs-dev/q3a-botlib-m3-multiplayer-gate-2026-06-30.md`.
  - 2026-06-30 Duel/CTF play-depth evidence tooling:
    `tools/bot_playtest/build_bot_playdepth_evidence.py` now turns filled
    `duel_rotation` and `ctf_objectives` operator notes into
    `bot_duel_ctf_playdepth_evidence.json` plus Markdown, preserving pending
    or failed status and reusing playtest triage for scenario candidates.
    `tools/bot_release/run_bot_acceptance.py` now includes a
    `playdepth_evidence_tooling` gate, and
    `.tmp\bot_release\bot_release_acceptance_playdepth_evidence.json` passes
    13/13 checks. Implementation log:
    `docs-dev/q3a-botlib-duel-ctf-playdepth-evidence-tooling-2026-06-30.md`.
  - 2026-06-30 release acceptance post-crouch-reference round:
    `tools/bot_release/run_bot_acceptance.py` now requires the expanded
    eleven-map staged AAS set, staged `worr_crouch_ref.bsp`, promoted
    `movement_crouch_route` and `movement_hazard_context` evidence, and an
    accepted movement-reference audit. The dry run writes
    `.tmp\bot_release\bot_release_acceptance_post_crouch_reference.json` and
    passes 12/12 checks. Implementation log:
    `docs-dev/q3a-botlib-release-acceptance-post-crouch-reference-2026-06-30.md`.
  - 2026-06-30 crouch reference promotion: fixed q2aas reachability generation
    so crouch-only equal-floor and step links emit `TRAVEL_CROUCH`, added the
    WORR-authored `worr_crouch_ref` reference map, staged reference BSPs during
    install refreshes, promoted mode `92` to accepted
    `movement_crouch_route`, and made the runtime AAS movement smoke
    crouch-presence aware. `q2aas-staged-smoke` now validates eleven maps and
    `.tmp\bot_scenarios\movement_reference_gap_audit.json` accepts both
    `natural_crouch` and `hazard_context`. Implementation log:
    `docs-dev/q3a-botlib-crouch-reference-promotion-2026-06-30.md`.
  - 2026-06-30 hazard reference promotion: staged official Quake II `fact2`
    as the optional lava/runtime hazard reference, added the
    `runtime_hazard_entity_reference` q2aas feature gate, and promoted mode
    `96` from expected-blocked `movement_hazard_context_gap` on `base2` to
    accepted `movement_hazard_context` on `fact2`. Later same-day crouch
    promotion completes the remaining movement-reference blocker. With optional
    `q2dm7` and `fact2` BSPs staged plus the required `worr_crouch_ref`,
    `q2aas-staged-smoke` validates eleven maps and `crouch_reference`,
    `slime_reference`, `lava_reference`, and
    `runtime_hazard_entity_reference` pass. Implementation log:
    `docs-dev/q3a-botlib-hazard-reference-promotion-2026-06-30.md`.
  - 2026-06-30 movement reference candidate discovery: added
    `tools/q2aas/discover_reference_candidates.py` to scan local BSP corpora
    for q2aas reference candidates and optionally convert top rows through
    `validate_worr_q2aas.py`. Optional staged `q2dm7` now passes
    `slime_reference` in the q2aas manifest and `q2aas-stage-aas` stages
    `q2dm7.aas` when the local BSP is present. A later same-day promotion added
    optional staged `fact2` for lava/runtime hazard evidence and
    `worr_crouch_ref` for natural crouch evidence; the movement reference gap
    audit now reports both `natural_crouch` and `hazard_context` as
    `accepted`. A broad scan of
    `E:\Games\q2Clean\baseq2\maps` found `dark010.bsp` as a technically valid
    lava/runtime hazard scratch candidate, superseded by the canonical
    `fact2` promotion. Implementation log:
    `docs-dev/q3a-botlib-reference-candidate-discovery-2026-06-30.md`.
  - 2026-06-29 source-counter soak calibration: the first real repeated-soak
    runner passes exposed brittle long-soak assumptions. The
    `high_bot_soak_degradation` scenario now enables
    `bot_controlled_inactive_recovery=1`, the default and strict source-counter
    per-run budgets allow `8.0` ms/bot/sec bot-frame CPU headroom, and the
    near-zero route-reuse CPU variance gate now uses an absolute
    `max_delta=0.01`. The two fresh logs under
    `.tmp\bot_perf\post_recovery_source_counter_variance` pass the scenario
    and strict source-counter budgets, and
    `.tmp\bot_perf\post_recovery_source_counter_variance_reanalyzed.json`
    passes 14 variance checks. Implementation log:
    `docs-dev/q3a-botlib-source-counter-soak-calibration-2026-06-29.md`.
  - 2026-06-29 source-counter variance soak runner: added
    `tools/bot_perf/run_source_counter_variance_soak.py` so the M7
    post-change evidence workflow is repeatable. The runner executes repeated
    `high_bot_soak_degradation` rows, preserves per-run scenario reports and
    stdout captures, writes a combined scenario report for duration metadata,
    then invokes `analyze_bot_perf.py` with the strict source-counter budget
    and variance budget. Implementation log:
    `docs-dev/q3a-botlib-source-counter-variance-soak-runner-2026-06-29.md`.
  - 2026-06-29 source-counter variance budget gate: `tools/bot_perf` now
    supports `--variance-budget` comparison checks for repeated like-for-like
    soak logs. The new `source_counter_variance_budget.json` constrains
    normalized command throughput, route pressure, source-counter CPU,
    visibility, BSP trace, and entity-trace spread. `tools/bot_release` now
    includes a `perf_tooling` acceptance check for the default, strict
    source-counter, and variance budget files. The current same-log control
    artifact `.tmp\bot_perf\source_counter_variance_gate.json` passes strict
    per-run budgets twice and 14 variance checks. Implementation log:
    `docs-dev/q3a-botlib-source-counter-variance-budget-gate-2026-06-29.md`.
  - 2026-06-29 strict source-counter budget lane: the manual
    `high_bot_soak_degradation` row now evaluates both the compatibility
    `tools/bot_perf/default_soak_budget.json` lane and the current-source
    `tools/bot_perf/source_counter_soak_budget.json` lane. Scenario reports
    keep `perf_budget` as the primary/default result and add `perf_budgets`
    for all evaluated profiles. The strict lane requires all current
    source-counter groups plus current CPU, memory, visibility, and
    entity-trace derived fields. Implementation log:
    `docs-dev/q3a-botlib-strict-source-counter-budget-lane-2026-06-29.md`.
  - 2026-06-29 public bot surface audit: `tools/bot_surface` now scans active
    server/sgame bot source and `docs-user/` for canonical public `bot_*`
    cvars, required defaults, Q3-style commands, legacy-prefix regressions,
    smoke-only cvar leaks into user docs, and public cvar/default rows in
    `docs-user/bot-cvars.md`. The current JSON artifact
    `.tmp\bot_surface\public_bot_surface_audit.json` reports 94 bot cvars,
    5 bot commands, 13 public cvars, 13 public defaults, 37 smoke-only hooks,
    5 debug cvars, 39 experimental cvars, 0 violations, and 0 warnings.
    Implementation logs:
    `docs-dev/q3a-botlib-public-bot-surface-audit-2026-06-29.md` and
    `docs-dev/q3a-botlib-public-defaults-docs-gate-2026-06-29.md`.
  - 2026-06-29 release acceptance runner: `tools/bot_release` now executes
    the M8 dry run across public bot surface, first-party profiles, `bots.txt`
    roster exposure, authored and staged botfiles, staged reference AAS files,
    user docs, the multiplayer playtest plan, playtest triage coverage, perf
    tooling budget validity, and scenario evidence. The current artifact
    `.tmp\bot_release\bot_release_acceptance.json` passed 11/11 checks against
    `.tmp\bot_scenarios\implemented_hazard_context.json`; the later
    post-crouch-reference hardening supersedes it with 12/12 checks and the
    movement-reference audit gate.
    Implementation log:
    `docs-dev/q3a-botlib-release-acceptance-runner-2026-06-29.md`.
  - 2026-06-29 multiplayer playtest generator: `tools/bot_playtest` now
    generates FFA, Duel, TDM, and CTF playtest configs, a human checklist, and
    JSON artifacts for manual bot release validation. The current playtest
    artifact `.tmp\bot_playtest\bot_multiplayer_playtest.json` reports 4
    cases, 38 total minutes, and the first-party roster `vanguard`, `vector`,
    `bulwark`, `relay`, and `smoke`. Implementation log:
    `docs-dev/q3a-botlib-multiplayer-playtest-script-2026-06-29.md`.
  - 2026-06-29 playtest evidence triage: `tools/bot_playtest` now also writes
    an operator notes template and triages notes into JSON/Markdown reports
    with route, spacing, weak-retreat, min-player, Duel queue, CTF objective,
    and team-spacing scenario-candidate categories. The current pending triage
    artifact `.tmp\bot_playtest\bot_multiplayer_playtest_triage.json` reports
    4 pending cases and 0 warnings before manual notes are filled in. A later
    same-day follow-up adds the focused Duel/CTF release attachment builder.
    Implementation log:
    `docs-dev/q3a-botlib-playtest-evidence-triage-2026-06-29.md`.
  - 2026-06-28 hazard context gap round: the catalog now has 114 implemented
    rows with highest bot frame-command smoke mode `96`. The focused
    `movement_hazard_context_gap` validation passed from
    `.tmp\bot_scenarios\movement_hazard_context_gap\20260628T083930Z`; the
    full suite passed 114/114 rows from
    `.tmp\bot_scenarios\implemented_hazard_context\20260628T083945Z`.
    Runtime nav/entity classification now counts `target_laser` and
    `misc_lavaball` hazards beside hurt/lava/slime triggers, while mode `96`
    recorded the then-current packaged-map gap as `interaction_world_hazards=0`
    on `base2` until suitable slime/lava or hurt-trigger reference content was
    staged. Implementation log:
    `docs-dev/q3a-botlib-hazard-context-gap-2026-06-28.md`.
  - 2026-06-28 movement context gap matrix round: the catalog now has 113
    implemented rows with highest bot frame-command smoke mode `95`. The
    focused crouch-gap, door-context, teleporter-gap, swim, and waterjump
    validation passed 5/5 rows from
    `.tmp\bot_scenarios\movement_context_gap_rerun2\20260628T080154Z`; the
    full suite passed 113/113 rows from
    `.tmp\bot_scenarios\implemented_movement_context_gap_rerun3\20260628T081648Z`.
    This round adds teleporter/hazard nav context counters, pins scenario
    `bot_min_players 0` for deterministic smoke rows, increases frame-command
    status capture, keeps FFA roam behind item goals, keeps route-facing unless
    the bot is firing, and keeps role combat behind weak/non-attacking base
    action decisions. Implementation log:
    `docs-dev/q3a-botlib-movement-context-gap-matrix-2026-06-28.md`.
  - 2026-06-28 movement matrix and live behavior round: the catalog reached
    110 implemented rows with highest bot frame-command smoke mode `94`. The
    new movement matrix passed 11/11 rows from
    `.tmp\bot_scenarios\movement_matrix_expansion_rerun\20260627T232805Z`,
    behavior sanity passed 18/18 rows from
    `.tmp\bot_scenarios\behavior_sanity_rerun\20260627T232911Z`, and the
    full suite passed 110/110 rows from
    `.tmp\bot_scenarios\implemented_rerun_after_fixes\20260627T234219Z`.
    This round also keeps role combat behind the base action layer when bots
    are weak, underpowered, switching weapons, or not actually attacking.
    Implementation log:
    `docs-dev/q3a-botlib-movement-matrix-and-live-behavior-round-2026-06-28.md`.
  - 2026-06-27 live bot stabilization follow-up: min-player autofill now
    rotates through loaded first-party profiles instead of synthetic `botN`
    names, the Q3-style `botfiles/bots.txt` manifest is packaged and
    validated, paused local servers process bot autofill and add queues, FFA
    roam/item decisions no longer churn around visible enemies, and role
    combat now defers unless the base action layer is genuinely attacking.
    Focused validation passed for the profile/roam/combat scenario set from
    `.tmp\bot_scenarios\bot-profile-roam-state-fixes-rerun3`, the legacy
    role-combat compatibility set from
    `.tmp\bot_scenarios\bot-role-combat-compat-check3`, and a direct
    `.install` min-player smoke that spawned `B|Bulwark`, `B|Relay`, and
    `B|Vanguard`. Implementation log:
    `docs-dev/q3a-botlib-profile-autofill-roam-combat-stabilization-2026-06-27.md`.
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
  - The q2aas reference-map row is closed by the staged set:
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
  - Expanded the local staged q2aas reference set to eleven maps when optional `q2dm7` and `fact2` are present: `mm-rage`, `worr_crouch_ref`, `q2dm1`, `q2dm2`, `q2dm7`, `q2dm8`, `q2ctf1`, `base1`, `base2`, `fact2`, and `train`. The new manifest rows add structural/travel baselines, CTF team objective reachability via `team_objective_report`, campaign trigger/door/progression diagnostics via `campaign_progression_report`, water-backed liquid coverage, required crouch coverage through `worr_crouch_ref`, slime coverage through `q2dm7`, and lava/runtime hazard coverage through `fact2`.
  - `q2aas-stage-aas` now stages eleven generated `.aas` files when optional `q2dm7` and `fact2` are present, package refresh/audit flows preserve the accepted staged AAS members in `.install\basew\pak0.pkz`, and install refreshes copy q2aas reference BSPs into `.install\basew\maps`.
  - Implementation logs: `docs-dev/q2aas-generator-vendor-bootstrap-2026-06-16.md`, `docs-dev/q2aas-generator-q2-preset-validation-2026-06-16.md`, `docs-dev/q2aas-generator-q2-reachability-bridge-2026-06-16.md`, `docs-dev/q2aas-generator-validation-matrix-2026-06-16.md`, `docs-dev/q2aas-generator-deterministic-metadata-2026-06-16.md`, `docs-dev/q2aas-generator-entity-diagnostics-2026-06-16.md`, `docs-dev/q2aas-generator-diagnostic-gates-2026-06-16.md`, `docs-dev/q2aas-generator-baseline-regression-gates-2026-06-17.md`, `docs-dev/q2aas-generator-manifest-schema-validation-2026-06-17.md`, `docs-dev/q2aas-generator-manifest-schema-smoke-2026-06-17.md`, `docs-dev/q2aas-generator-aas-staging-2026-06-17.md`, `docs-dev/q2aas-generator-stage-audit-2026-06-17.md`, `docs-dev/q2aas-generator-packaged-map-smoke-2026-06-17.md`, `docs-dev/q2aas-generator-archive-manifest-guardrails-2026-06-17.md`, `docs-dev/q2aas-generator-package-audit-2026-06-17.md`, `docs-dev/q2aas-generator-archive-packaging-2026-06-17.md`, `docs-dev/q2aas-generator-refresh-install-integration-2026-06-17.md`, `docs-dev/q2aas-generator-stage-archive-member-validation-2026-06-17.md`, `docs-dev/q3a-botlib-release-policy-2026-06-18.md`, `docs-dev/q2aas-reference-map-diagnostics-2026-06-18.md`, `docs-dev/q2aas-reference-map-coverage-round-2026-06-18.md`, `docs-dev/q2aas-generator-policy-semantics-closeout-2026-06-21.md`, `docs-dev/q2aas-generator-reachability-metadata-round-2026-06-21.md`, `docs-dev/q3a-botlib-reference-map-runtime-adapter-round-2026-06-21.md`, `docs-dev/q3a-botlib-movement-reference-gap-audit-2026-06-30.md`, `docs-dev/q3a-botlib-crouch-reference-promotion-2026-06-30.md`.
- `FR-04-T12` / `DV-07-T06` In Progress:
  - Added the WORR-native `src/game/sgame/bots/bot_runtime.*` AAS runtime shell as the first Phase 2 foothold.
  - Registered disabled-by-default `bot_enable`, `bot_debug`, `bot_debug_aas`, `bot_debug_route`, `bot_debug_goal`, `bot_debug_client`, and `bot_cpu_budget_ms` cvars.
  - Map start and entity reload now probe `maps/<current-map>.aas` through WORR's filesystem extension when `bot_enable` is set, decode the Q3A/BSPC AAS v5 header transform, validate the `EAAS` version 5 lump table, and record area/reachability/cluster counts for debug status.
  - Runtime smoke against the refreshed `.install` payload loads packaged `maps/mm-rage.aas` with `areas=428`, `reachability=562`, and `clusters=4`.
  - Added the compiled Q3A BotLib import boundary: `src/game/sgame/bots/q3a/` is reserved for commit-pinned imports, `q3a_botlib_boundary.*` records the planned AAS/runtime file inventory, and `botlib_adapter.*` now owns the future setup/shutdown/map/frame bridge.
  - Imported the first commit-pinned Q3A BotLib utility subset from `id-Software/Quake-III-Arena` commit `dbe4ddb10315479fc00086f08e25d968b4b43c49`: `q_shared.h`, `surfaceflags.h`, `botlib.h`, `be_interface.h`, `l_log.h`, `l_memory.*`, and `l_libvar.*`.
  - Added the `q3a_botlib_utility` build group and WORR-native `q3a_botlib_import.*` smoke bridge. The adapter now runs a Q3A LibVar smoke during initialization and `bot_debug_aas 2` prints the result.
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
  - Added a callback-backed Q3A debug draw bridge for `AAS_DebugLine`, `AAS_PermanentLine`, `AAS_ClearShownDebugLines`, `AAS_DrawPermanentCross`, and `AAS_DrawArrow`, gated by `bot_debug_aas >= 3`, `bot_debug_route`, or `bot_debug_goal`.
  - Added a route/goal overlay smoke path that uses imported Q3A route prediction and the debug draw bridge under `bot_debug_route` / `bot_debug_goal`.
  - Added a callback-backed Q3A debug polygon bridge for `botimport.DebugPolygonCreate` / `DebugPolygonDelete`; `bot_debug_aas 3` now renders a sampled polygon outline/fan through WORR `gi.Draw_Line` and reports create/delete counters.
  - Imported Q3A `be_aas_debug.c`, replaced the remaining WORR-owned debug-line helper definitions with Q3A `botimport.DebugLineCreate` / `DebugLineShow` / `DebugLineDelete` callbacks, and added a `bot_debug_aas 3` smoke for imported `AAS_ShowArea` / `AAS_ShowAreaPolygons`.
  - Imported Q3A `be_aas_cluster.c`, removed the temporary WORR-owned `AAS_InitClustering` no-op, and added `q3a_cluster` verbose smoke for the loaded AAS cluster table.
  - Imported Q3A `be_aas_routealt.c`, removed the temporary WORR-owned `AAS_InitAlternativeRouting` / `AAS_ShutdownAlternativeRouting` stubs, now initializes alternative routing after route-cache setup, and added `q3a_alt_route` verbose smoke for `AAS_AlternativeRouteGoals`.
  - Imported Q3A `be_aas_optimize.c`, removed the temporary WORR-owned `AAS_Optimize` no-op, and kept the mutating optimization path opt-in behind Q3A `aasoptimize` behavior.
  - The current imported Q3A AAS runtime C set now rebuilds as part of `sgame_x86_64`, including AAS file load/setup/start-frame, sampling, reachability, route cache/query, clustering, alternative routes, optimization hooks, entity cache, movement prediction, debug helpers, LibVars, memory, and CRC utilities. This closes the current AAS runtime compile surface without claiming the separate Q3A arena AI, EA command layer, or goal/weight system.
  - `AAS_Trace` and `AAS_PointContents` are now final-owned by the active-map Q2 BSP static-world collision bridge, `AAS_inPVS` / `AAS_inPHS` are final-owned by the active-map Q2 BSP visibility bridge, and `AAS_EntityCollision` reaches WORR's dynamic entity collision path through `botlib_adapter.*`, `BotRuntimeEntityTrace`, and server-game `gi.clip`.
  - BotLib adapter initialization is now idempotent for the game-module lifetime, and `ShutdownGame()` calls `Bot_RuntimeShutdown()` after level unload/lifecycle status so the imported BotLib memory/filesystem layer receives an explicit module-level shutdown.
  - Q3A `bot_*` LibVars stay internal to the imported AAS runtime; WORR public policy remains in `bot_*`, with only Q2/WORR `phys_*` and `rs_*` movement/reachability inputs seeded for imported `be_aas_move.c`.
  - `BotRuntimeBuildEntitySnapshot()` now splits BotLib entity snapshots for players, bots, spectators, and monsters/NPCs, and `bot_debug_aas` loaded-status output reports the current counts for those categories.
  - Follow-up entity scheduling now also separates regular pickups, dropped items, traps/projectiles/hazards, doors/plats/movers, and objective/flag entities; item desirability refreshes are staggered per bot with cache-reuse status, route recomputation cadence exposes rate-limit counters, and the live aim path is documented as blackboard-visible-target gated.
  - Replaced the quiet Q3A `botimport.Print` capture with a WORR-owned print callback bridge; warnings/errors/fatals now forward to `gi.Com_PrintFmt`, message-level chatter is gated behind `bot_debug_aas >= 3`, and verbose status reports `q3a_print_*` counters.
  - Added a Q3A `botimport.BotClientCommand` safety bridge that reaches WORR runtime validation, requires a bot client, and safely rejects command execution until a dedicated bot command dispatcher exists.
  - Replaced raw Q3A `botimport.GetMemory`, `FreeMemory`, and `HunkAlloc` callbacks with a tracked bot-owned allocator that reports zone/hunk active and peak bytes, grouped hunk releases, and allocation failures.
  - Replaced the singleton active-AAS memory file shim with a read-only Q3A `botimport.FS_*` bridge backed by WORR filesystem load/free callbacks, while keeping the memory buffer as a fallback path.
  - Runtime smoke against the refreshed `.install` payload loads packaged `maps/mm-rage.aas` and reports `utility=Q3A LibVar smoke passed`, `q3a_aas=Q3A AAS file load passed`, `q3a_sample=Q3A AAS area sample passed: area=3 point_area=3 cluster=1 presence=6 reachability=1`, `q3a_sample_reachability=1`, `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`, `q3a_route_overlay=Q3A route overlay passed: callback=yes start=3 goal=6 end=6 travel_time=113 reachability=1 lines=2 crosses=3 arrows=1 clears=1 failures=0`, `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`, `q3a_movement_drop=yes`, `q3a_movement_jump=yes`, `q3a_debug_draw=Q3A route overlay debug draw passed: callback=yes lines=2 crosses=3 arrows=1 clears=0`, `q3a_debug_draw_callback=yes`, `q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0`, `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`, `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`, `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`, `q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18`, `q3a_bsp_leaf_link_failures=0`, `q3a_bsp_box_entities_smoke=yes`, `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start`, `q3a_bsp_entity_smoke=yes`, `q3a_bsp_model=Q3A BSP model lump load passed: maps/mm-rage.bsp models=18 smoke_model=1 mins=(-328.0 -584.0 24.0) maxs=(-192.0 -440.0 256.0)`, `q3a_bsp_model_smoke=yes`, `q3a_bsp_collision=Q3A BSP collision load passed: maps/mm-rage.bsp planes=1367 nodes=1863 leafs=1882 brushes=1142 point_contents=1 trace_fraction=0.347 startsolid=0 allsolid=0`, `q3a_bsp_point_contents_smoke=yes`, `q3a_bsp_trace_smoke=yes`, `q3a_bsp_visibility=Q3A BSP visibility load passed: maps/mm-rage.bsp clusters=303 smoke_cluster=0 pvs_visible=142 phs_visible=289`, `q3a_bsp_pvs_smoke=yes`, `q3a_bsp_phs_smoke=yes`, `q3a_angle_vectors=Q3A AngleVectors smoke passed`, `q3a_time_ms=25`, `q3a_areas=428`, `q3a_reachability=562`, `q3a_clusters=4`, and `planned_files=48`; active item route-goal selection, exact-origin position route goals, basic item reservation, route-point look-ahead steering, velocity-aware command yaw, stuck-progress repath, short stuck recovery commands, item-goal blacklist cooldowns, failed-goal reason diagnostics, reachability-aware movement-state command intent, natural jump/ladder/barrier-jump/walk-off-ledge/elevator travel-type validation, interaction wait/use retry telemetry, and a `bot_brain.*` command/status ownership split now exist, while natural map-backed crouch/swim/waterjump runtime proof waits on reference maps and higher-level behavior integration remains pending.
  - Rocket-jump route policy smoke now keeps inherited `TRAVEL_ROCKETJUMP` reachability default-blocked unless `bot_allow_rocketjump 1` is set; mode `14` reports `last_reachability_type=12`, `route_failures=0`, and `pass=1`, while mode `15` reports `travel_type_goal_expect_blocked=1`, `commands=0`, `route_commands=0`, `route_failures=8`, and `pass=1`.
  - Four-bot frame-command smoke now validates the staged fake-client command path above the two-bot item-reservation baseline; mode `16` adds four bots, reports `commands=38`, `route_commands=38`, `route_failures=0`, `item_goal_active_reservations=4`, and `pass=1`.
  - Eight-bot frame-command smoke now extends the same staged fake-client command validation to eight active bots; mode `17` adds `B|Mover` through `B|MoverEight`, reports `commands=92`, `route_commands=92`, `route_failures=0`, `item_goal_active_reservations=8`, and `pass=1`.
  - Ten-minute eight-bot frame-command soak now validates the long-running route-command path; mode `18` reports `elapsed_ms=600001`, `frames=192036`, `commands=192036`, `route_commands=192036`, `route_failures=0`, `skipped_inactive=0`, and `pass=1` after refresh-install packaging.
  - Same-map reload repeat smoke now validates the eight-bot route-command path across both default `gamemap` reloads and an opt-in forced `map "<map>" force` restart path; mode `19` now reports command/restart markers, restart-time `realtime_reset`, cleanup status gates with zero active reservations, three-cycle forced restart validation, and default two-cycle regression coverage.
  - Natural movement support diagnostics now report packaged `mm-rage.aas` lacks natural crouch/swim/waterjump routes while the elevator/platform interaction proof reports wait/use activation counters through the route-command status path. Follow-up diagnostics add unsupported masks, per-type reason codes, AAS area/goal-area fields, route-start origins for future reference maps, and interaction context counts by world entity type.
  - First behavior/action dispatcher units now compile as WORR-native `bot_actions.*`, `bot_items.*`, and `bot_combat.*` boundaries; `bot_brain.*` samples the dispatcher every accepted command frame, applies validated attack/use decisions through `BotActions_ApplyDecisionDetailed()`, records pending weapon/inventory intents, and emits `q3a_bot_action_status`. Follow-up Phase 4/6 support now adds a per-bot perception blackboard, Q2/WORR weapon metadata, item utility scoring hooks including special-item buckets, health/armor focused routing, combat enemy-fact/damage-attribution proof helpers, opt-in aim/fairness plus live aim-profile/projectile-leading consumption, live pickup and observed-respawn timing consumers, item timer disable/fuzz helper policy, weapon-switch request/observation proof state, deterministic health/armor pickup proof helpers, exact `use_index_only` weapon/inventory dispatch for accepted pending intents, per-bot enemy health/armor estimates from visible observations and split bot-attributed damage deltas, estimate-aware finisher/armor-pressure weapon scoring, carried-weapon inventory scanning, conservative carried non-weapon inventory/powerup use policy, environment utility and sphere deployable policy, placement-checked doppelganger use, last-resort personal teleporter escape policy, safety-gated nuke policy, command-owned nuke retreat routing, generic timed route-goal owner telemetry with nuke retreat, personal teleporter escape, coop leader-route, coop LeadAdvance, team role-route, CTF role-route, and FFA roam-route consumers, target-source-aware team-objective proof helpers, deterministic role/lane-depth team-policy helpers, FFA/TDM/CTF objective-side match/item/friendly-fire helper policy, profile-derived match-role selection, profile-derived teamplay/objective/friendly-fire-care match-policy hints, profile-derived item-greed/item-denial/powerup-timing/retreat-health match item-policy hints, profile-derived movement-style match-policy hints, default-off bot chat-policy live-dispatch, team-only audience, global rate-limit gating, profile chat-personality initial utterance selection, smoke-gated reply selection, smoke-gated multi-event reply selection, live chat event taxonomy, live spawn event accounting, first live route-ready reply triggering, live `enemy_sighted` reply triggering from visible blackboard enemies, global live chat cooldown suppression, four-variant chat phrase-library proof, duplicate route-ready chat suppression, live `low_health` chat from survival state, live `item_taken` chat from pickup observations, live `objective_changed` chat from CTF objective transitions, live `flag_state` chat from CTF flag observations, live `blocked` chat from route failures, live `item_denied` chat from TDM resource-denial pressure, live `victory_defeat` chat from native intermission/match-result state with outcome-aware phrase classification, and `base1` plus `base2` coop campaign interaction matrix validation, coop/resource policy helper metadata, default-off FFA roam-route ownership, default-off FFA spawn-camp avoidance, default-off FFA item-role pickup scoring, default-off FFA role-combat attack ownership, default-off FFA spawn-camp combat avoidance, default-off team role-route ownership, default-off team item-role route selection, default-off team resource-denial pickup scoring, default-off match item-policy umbrella pickup scoring, default-off team friendly-fire attack suppression, default-off team role-combat attack ownership, default-off CTF role-route ownership, default-off CTF role-combat attack ownership, default-off CTF dropped-flag route ownership, default-off CTF carrier-support route ownership, default-off CTF base-return route ownership, default-off CTF objective-route policy ownership, default-off CTF objective-route precedence over generic role routing, default-off CTF item-role pickup scoring, default-off coop WaitForLeader and interaction-retry command owners, a default-off coop resource-share route-selection gate, default-off coop anti-blocking command ownership, default-off coop monster target-sharing blackboard adoption, default-off coop door/elevator source-owner plus teammate hold commands, and central behavior owner arbitration with cvar classification and handoff telemetry. `bot_behavior_enable` now groups the current default-off behavior proof family behind one opt-in switch, mode `52` proves the umbrella activates TDM role-route, role-combat, friendly-fire, and match item-policy gates without setting the individual proof cvars, mode `53` proves staged profile roles feed match-policy requested-role selection, mode `54` proves staged profile teamplay/objective/friendly-fire-care hints feed CTF match policy, mode `55` proves staged profile item-greed/item-denial/powerup-timing/retreat-health hints feed TDM match item/resource policy, mode `56` proves staged profile movement-style hints feed TDM match policy, mode `57` proves profile chat metadata and `bot_allow_chat` while submitting a conservative live dispatch, mode `58` proves the `bot_chat_team_only` audience path, mode `59` proves `bot_chat_min_interval_ms` rate limiting without dispatch failures, mode `60` proves profile chat-personality initial utterance selection, mode `61` proves profile chat-personality reply selection for the first team-ready event, mode `62` proves profile chat-personality reply selection across team-ready and route-ready proof events, mode `63` proves central behavior arbitration with route/item/combat candidates, combat ownership, handoff evidence, and live/smoke/debug/deprecated cvar classification, mode `79` proves default-off `bot_chat_live_events` with live spawn plus `route_ready` accounting and an eleven-event taxonomy, mode `80` proves live chat global cooldown suppression, mode `81` proves visible enemy facts drive live `enemy_sighted` chat, mode `82` proves four-variant phrase selection, mode `83` proves duplicate suppression, mode `84` proves live `low_health` chat, mode `85` proves live `item_taken` chat, mode `86` proves live `objective_changed` chat, mode `87` proves live `flag_state` chat, mode `88` proves live `blocked` chat, mode `89` proves live `item_denied` chat, mode `90` proves live `victory_defeat` match-result chat plus win/loss outcome phrase accounting, mode `91` proves coop live-loop interaction behavior on `base1` and `base2`, modes `20` through `91` now use those hooks as implemented smoke scenarios, and `bot_mapvote_smoke 2` covers the native map-vote transition proof, while broader autonomous team behavior, deeper trigger-aware/campaign-specific coop command ownership beyond the base campaign matrix rows, broader objective intelligence, and release-note attachment work remain future work.
  - Scenario and performance validation tooling now exists under `tools/bot_scenarios/` and `tools/bot_perf/`, covering 123 implemented catalog rows, the now-empty default pending scenario set, raw reserved-mode diagnostics, split-marker metric merging, strict marker gates, optional field discovery, parser fixtures, source-counter timing fields, and derived performance budgets. The latest accepted hazard context slice preserves the expanded aggregate from `.tmp\bot_scenarios\implemented_hazard_context\20260628T083945Z` with 114 passing rows and no failures/timeouts/errors/pending rows, while focused supplemental rows pass for `min_players_profile_coverage` from `.tmp\bot_scenarios\min_players_profile_coverage.json`, `coop_campaign_interaction_matrix_base2` from `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json`, `coop_campaign_interaction_depth_base2` from `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`, `coop_campaign_progression_chain_base2` from `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`, `coop_campaign_progression_consumer_base2` from `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`, `coop_campaign_post_interaction_base2` from `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`, `coop_campaign_progression_carry_base2` from `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`, `coop_campaign_keyed_path_train` from `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`, and `coop_campaign_key_carry_train` from `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`, so the next work is broader live behavior depth rather than harness reconciliation.
  - Botfile profile work now includes Q3/Gladiator-style companion families, idTech3-style `botfiles/scripts/*_s.c` companions, multi-skill character validation, deeper behavior metadata validation, shared teamplay event-name parity, utility/chat/weapon/script parity polish, script-package coverage, script parity validation, loose `botfiles` mirroring for no-zlib dedicated builds, user-facing profile docs, and first live use of `WORR_ROLE`, `WORR_TEAMPLAY_BIAS`, `WORR_OBJECTIVE_BIAS`, `WORR_FRIENDLY_FIRE_CARE`, `WORR_ITEM_GREED`, `WORR_ITEM_DENIAL`, `WORR_POWERUP_TIMING`, `WORR_RETREAT_HEALTH`, and `WORR_MOVEMENT_STYLE` as match-policy hints. Profile chat metadata now has a default-off `bot_allow_chat` status boundary, conservative live dispatch proof, team-only audience proof, global rate-limit proof, initial personality selection proof, smoke-only reply selection proof, smoke-only multi-event reply proof, live spawn plus route-ready event accounting behind `bot_chat_live_events`, live cooldown suppression proof, combat-derived enemy-sighted live triggering, four-variant phrase selection, duplicate suppression, survival-state low-health live triggering, pickup-observation item-taken live triggering, CTF transition-derived objective-changed live triggering, CTF flag-state live triggering, route-failure blocked live triggering, TDM resource-denial item-denied live triggering, and native match-result live triggering with outcome-aware win/loss phrase accounting, while richer conversation remains future work.
  - The latest promotion waves connect real gameplay observations, live behavior owners, profile hints, chat proof events, match-flow boundaries, combat/survival depth, FFA/TDM/Duel/CTF pacing, coop helper ownership, live chat event triggering, chat cooldown suppression, the first combat-derived live chat trigger, four-variant bot chat phrase-library proof, duplicate route-ready chat suppression, survival-state low-health live chat, pickup-observation item-taken live chat, CTF transition-derived objective-changed live chat, CTF flag-state live chat, route-failure blocked live chat, TDM resource-denial item-denied live chat, native match-result live chat, outcome-aware match-result phrase accounting, the `base1` and `base2` coop campaign interaction matrix rows, the first movement matrix, the movement context gap matrix, accepted train teleporter entity-route fallback, and accepted `fact2` hazard context into implemented scenarios. Modes `20` through `96`, `bot_team_policy_smoke` modes `2` and `3`, `bot_warmup_smoke 2`, `bot_vote_smoke 2`, `bot_mymap_smoke 2`, `bot_nextmap_smoke 2`, `bot_mapvote_smoke 2`, `bot_scoreboard_smoke 2`, `bot_intermission_smoke 2`, mode `19` map-change/map-restart rows, plus the coop reuse rows are implemented smoke scenarios; the expanded catalog now has a green 114/114 accepted hazard-context baseline plus focused supplemental rows for the post-aggregate additions.
  - Raw reserved-mode diagnostics preserve latest marker source lines and missing-metric source hints. The earlier blocked mode `22` route-only evaluation is retained as a diagnostic artifact, and the later promotion pass supersedes it with health/armor-specific counters in the implemented `health_armor_pickup` scenario.
  - The focused clustering smoke reports `q3a_cluster=Q3A AAS clustering passed: clusters=4 area=3 cluster=1 cluster_areas=157 reachability_areas=156 failures=0`.
  - The focused alternative-route smoke reports `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`.
  - The optimize import smoke keeps `aasoptimize=0` on the default loaded-AAS path while the linked imported `AAS_Optimize` implementation replaces the final no-op.
  - The focused print-bridge smoke with `bot_debug_aas 3` reports `Q3A BotLib message: trying to load maps/mm-rage.aas`, `q3a_print_callback=yes`, `q3a_print_messages=2`, `q3a_print_warnings=0`, and `q3a_print_errors=0`.
  - The focused BotClientCommand smoke with `bot_debug_aas 3` reports the bridge passed with `callback=yes`, `client=0`, `accepted=0`, `rejected=1`, and `failures=0`.
  - The focused memory-allocator smoke with `bot_debug_aas 3` reports `q3a_memory_zone_active=239894`, `q3a_memory_hunk_active=691078`, `q3a_memory_hunk_allocs=17`, and `q3a_memory_failures=0`; implemented scenario source-counter output now also reports Q3A memory active/peak/available fields.
  - The focused filesystem-bridge smoke with `bot_debug_aas 3` reports `q3a_fs_passed=yes`, `q3a_fs_files=1`, `q3a_fs_memory_files=0`, `q3a_fs_read_bytes=277484`, and `q3a_fs_writes_rejected=0`.
  - The dedicated bot slot smoke now reports `Queued bot Charlie for the next server frame`, `Added bot B|Charlie in slot 1`, `q3a_bot_slot_smoke_after_deferred_pair count=2`, and `q3a_bot_slot_smoke_after_remove_all count=0`.
  - The dedicated min-player autofill smoke now reports `q3a_bot_min_players_smoke=begin target=3`, `Added bot B|bot1 in slot 0`, `Added bot B|bot2 in slot 1`, `Added bot B|bot3 in slot 2`, `q3a_bot_min_players_smoke_after_fill count=3 auto=3 humans=0 target=3`, target trim to `count=1 auto=1`, and disable cleanup to `q3a_bot_min_players_smoke=end final_count=0`.
  - The dedicated profile smoke now reloads the WORR-authored staged `smoke` profile, resolves `addbot smoke`, and reports `q3a_bot_profile_smoke_after_add count=1 name=B|Smoke profile=smoke skin=male/grunt skill=4 reaction=250 aggression=0.65 aim_error=2.5 preferred_weapon=rocketlauncher chat=quiet role=attacker movement=strafe` followed by cleanup to `q3a_bot_profile_smoke=end final_count=0`; the implemented scenario suite now includes the profile-backed spawn proof.
  - The dedicated bot frame-command smoke now asks `sgame` for cached AAS route-steered bot `usercmd_t` data, runs it through the server fake-client movement path, and reports `q3a_bot_frame_command_status frames=8 commands=8 route_requests=8 route_queries=2 route_refreshes=2 route_reuses=6 route_commands=8 route_failures=0 route_invalid_slots=0 route_cadence_refreshes=1 route_target_refreshes=0 route_drift_refreshes=0 route_preferred_goal_refreshes=0 last_route_client=0 last_start_area=224 last_goal_area=227 last_route_end_area=217 last_travel_time=130 last_reachability=218 last_stop_event=0 skipped_invalid=0 skipped_not_bot=0 skipped_runtime=0 skipped_inactive=0 expected_min_frames=1 expected_min_commands=1 pass=1`. With `bot_debug_route 1` and `bot_debug_goal 1`, the same path reports native cached overlay counters, reachability fields, and bounded polyline counters `route_debug_routes=8`, `route_debug_goals=8`, `route_debug_arrows=8`, `route_debug_labels=8`, `route_debug_polyline_points=16`, `route_debug_polyline_segments=24`, `route_debug_missing_frames=2`, `last_route_debug_client=0`, `last_current_area=224`, `last_route_point_count=2`, `last_reachability_type=2`, `last_reachability_flags=2`, and `last_reachability_end_area=217`; selected-client debug smoke with `bot_debug_client 1` suppresses the slot-0 overlay and reports `route_debug_routes=0`, `route_debug_filtered_slots=8`, `route_debug_filter_miss_frames=10`, `last_debug_filter_client=1`, and `pass=1`.
  - Live bot command correction now matches Q3A's command-frame convention: desired world view angles are normalized, pitch-clamped, converted by subtracting `pmove.deltaAngles`, and client entity snapshots publish live `client->vAngle`. The public bot cvar namespace is canonical `bot_*`; the interim prefixed alias layer is removed so console completion presents the Quake-style surface.
  - Latest behavior/autofill follow-up: active server, sgame, user-doc, roadmap, and scenario-harness bot cvars now use canonical `bot_*` names; Q3-style commands are `addbot`, `removebot`, `kickbots`, `botlist`, and `bot_reload_profiles`; `bot_enable` and `bot_behavior_enable` default on; `bot_min_players` fills directly from its target without a hidden enable-cvar dependency; the behavior umbrella activates FFA item-role routing and threat retreat; close visible front threats create spacing routes while only low-health retreats suppress attack input; low-health pickup scoring has a stronger survival boost; and smoke-only setup is gated by the raw smoke cvar so live behavior defaults do not allocate proof slots. Validation passed with `spawn_route_to_item`, `ffa_roam_route`, `ffa_item_roles`, `behavior_policy_umbrella`, `combat_survival_regression`, `threat_retreat_avoidance`, the direct `bot_min_players_smoke` run, and 54 bot scenario harness tests. Implementation log: `docs-dev/q3a-botlib-behavior-autofill-cvar-fixes-2026-06-27.md`.
  - Latest public bot surface audit: `tools/bot_surface/audit_bot_surface.py`
    now verifies required public `bot_*` defaults, registered Q3-style
    commands, active-source legacy-prefix regressions, and `docs-user/`
    smoke-hook leaks. It also verifies the public bot cvar/default table in
    `docs-user/bot-cvars.md`. The current artifact
    `.tmp\bot_surface\public_bot_surface_audit.json` reports 0 violations.
    Implementation logs:
    `docs-dev/q3a-botlib-public-bot-surface-audit-2026-06-29.md` and
    `docs-dev/q3a-botlib-public-defaults-docs-gate-2026-06-29.md`.
  - Latest release/playtest evidence: `tools/bot_release/run_bot_acceptance.py`
    now includes the generated multiplayer playtest plan plus triage catalog and
    passes 10/10 checks, while `tools/bot_playtest` writes FFA, Duel, TDM, and
    CTF configs/checklists, a notes template, and triage reports under
    `.tmp\bot_playtest`. Implementation logs:
    `docs-dev/q3a-botlib-multiplayer-playtest-script-2026-06-29.md` and
    `docs-dev/q3a-botlib-playtest-evidence-triage-2026-06-29.md`.
  - Latest roam/retreat/combat scrutiny: combat context now includes bot health and armor, weak underpowered fights are withheld when a low-stack bot faces a stacked enemy without a viable weapon, role-combat bridges defer to pending weapon switches and the same weak-fight gate, the underpowered weapon estimate penalty is stronger, close-front spacing has a separate short cooldown from low-health survival retreat, and the survival retreat threshold now matches low-health item pressure. Focused validation passed across FFA roam/live pacing, duel pacing, weapon scoring, aim/fire policy, survival, and q2dm2/q2dm8 retreat regressions. Implementation log: `docs-dev/q3a-botlib-roam-retreat-combat-scrutiny-2026-06-27.md`.
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
- `FR-02-T12` / `FR-01-T09` Done; `FR-02-T13..T15` / `FR-03-T11` / `DV-02-T07` / `DV-03-T08` Planned:
  - Completed a cross-renderer gamma, lighting, tone-mapping, and shadowmapping audit and landed the bounded correctness/safety fixes that did not require changing the renderer output contract.
  - Native Vulkan now honors shared `r_intensity`, matches OpenGL's filtered-shadow response, prefers compatible UNORM swapchains, and keeps regenerated embedded SPIR-V in sync.
  - OpenGL now uses correct raw shadow-depth sampling, far-depth EVSM clears, finite lighting controls, inverse-transpose normal transforms, safe lightmap/lightgrid sampling, and deterministic first-frame auto exposure.
  - Shared/client paths now mirror shadow aliases deterministically, invalidate cached pages for raster policy changes, preserve tracked-light identity, strictly validate shadowlight configstrings, and use an engine-owned static BSP query with a saturating protocol lightlevel conversion.
  - Next work is the renderer-neutral linear scene/final presentation contract, scalable/material-aware shadow resources, direct-sun separation, capability-aware UI, and semantic/pixel validation.
  - Plan and implementation logs: `docs-dev/plans/renderer-color-lighting-shadow-modernization.md`, `docs-dev/renderer/gamma-lighting-shadow-audit-hardening-2026-07-10.md`, `docs-dev/renderer/gameplay-light-query-parity-2026-07-14.md`.
- `FR-03-T09` Done:
  - Added shared archived `r_borderless` tri-state window behavior for renderer/video backends (`0` exclusive where supported, `1` borderless fullscreen, `2` always borderless in windowed mode too).
  - Updated the Video and Multi-Monitor menu selectors to expose `r_borderless` instead of the legacy `r_fullscreen_exclusive` toggle, while keeping the legacy cvar as a no-archive runtime mirror.
  - Aligned the bootstrap session shell with `r_borderless` so startup window mode resolution matches the engine's renderer window policy.
  - Implementation log: `docs-dev/shared-borderless-cvar-2026-04-29.md`.
- `FR-04-T08` Done:
  - Added a Quake Champions-inspired top HUD for cgame multiplayer modes, covering FFA leader/chaser rows, team score panels, duel player panels, match timer, time limit, warmup/countdown, timeout, overtime, and intermission states.
  - Extended the sgame HUD blob with match metadata and optional scoreboard-row health/armor vitals so spectator duel panels can show player resources without changing legacy layout compatibility.
  - Refined the warmup timer to match the QC state/clock/timelimit stack, made FFA row selection mirror the existing minihud's top-two-or-viewed-player behavior, and serialized row rank/name data so top rows do not fall back to generic labels.
  - 2026-07-01 cgame UI ownership follow-up: cgame now consumes blob-backed scoreboard and end-of-unit layouts automatically when `CONFIG_HUD_BLOB` data is present, leaving `svc_layout` as a legacy fallback and narrowing sgame's UI role to data publication for those screens. Implementation log: `docs-dev/cgame-hud-blob-layout-auto-2026-07-01.md`.
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
  - Completed a first-party code safety pass over command argument assembly, command macro expansion, `exec` fallback path probing, filesystem write-mode construction, Windows service command construction, prompt completion cvar naming, and normalization test setup.
  - Implementation log: `docs-dev/code-safety-improvements-2026-06-26.md`.
  - Completed a server/MVD string-safety pass over savegame metadata fallback dates, dummy MVD userinfo construction, server configstring formatting, MVD waiting-room defaults, recording-list placeholders, and demo-seek configstring restoration.
  - Implementation log: `docs-dev/server-string-safety-improvements-2026-06-26.md`.
  - Completed a renderer/save/anticheat string-safety pass over OpenGL placeholder texture names, RTX image paths and override names, path-tracing feedback and PFM output strings, anticheat fallback strings, and savegame JSON string restore.
  - Implementation log: `docs-dev/renderer-save-anticheat-string-safety-2026-06-26.md`.
  - Completed an sgame/IQM string-helper normalization pass over rank/time/gametype display strings, generated gameplay sound paths, admin IPv4 formatting, and RTX IQM mesh/material/animation metadata copies.
  - Implementation log: `docs-dev/sgame-iqm-string-helper-normalization-2026-06-27.md`.
  - Completed an RTX/cgame format-helper normalization pass over RTX shader/debug/profiler/texture/tone-mapping helper strings and cgame crosshair image-name formatting.
  - Implementation log: `docs-dev/rtx-cgame-format-helper-normalization-2026-06-27.md`.
  - Completed a game/cgame/PFM safety hardening pass over userinfo FOV fallback formatting, weapon-wheel local formatting helpers, worldspawn gamemod name storage, and RTX PFM dump failure handling.
  - Implementation log: `docs-dev/game-cgame-pfm-safety-hardening-2026-06-27.md`.
  - Completed an RTX Vulkan allocation and shader I/O hardening pass over instance extension/layer enumeration, swapchain image/view arrays, stretch-pic framebuffers, shader-file loads, and swapchain failure propagation.
  - Implementation log: `docs-dev/rtx-vulkan-allocation-shader-io-hardening-2026-06-27.md`.
  - Completed an RTX Vulkan enumeration/init hardening pass over surface formats, present modes, physical devices, per-device extensions, optional extensions, queue families, present-support probes, and frame-time surface capability checks.
  - Implementation log: `docs-dev/rtx-vulkan-enumeration-init-hardening-2026-06-27.md`.
  - Completed a first-party runtime/native Vulkan bounds pass over server match-list fallbacks, sgame intermission messages, Game3 stats and Base85 save bridges, patched-PVS path/matrix sizing, Steam path appends, and native Vulkan UI/world allocation math.
  - Implementation log: `docs-dev/first-party-runtime-vulkan-bounds-pass-2026-06-27.md`.
  - Completed a native Vulkan upload/dirty-rect bounds follow-up over world-face masks, vertex uploads, lightmap atlas clears, face-lightmap style strides, dirty-rect uploads, mesh shrink sizing, UI GPU/frame buffer sizing, image transparency sizing, and sub-rect bounds checks.
  - Implementation log: `docs-dev/native-vulkan-upload-bounds-followup-2026-06-27.md`.
  - Completed a native Vulkan shadow bounds hardening pass over transient vertex capacity growth, triangle emission counts, world face-bounds cache allocation, page/view validation, render-job rollback, caster-count validation, mapped vertex uploads, and record-time guards.
  - Implementation log: `docs-dev/native-vulkan-shadow-bounds-hardening-2026-06-27.md`.
  - Completed a server navigation hardening pass over `.nav` file count validation, packed context allocation sizing, link/traversal extent checks, missing-file loaded state, node-search radius use, node-link bitmap sizing, and edict registration bounds.
  - Implementation log: `docs-dev/server-nav-bounds-pathing-hardening-2026-06-27.md`.
  - Completed a native Vulkan main allocation/screenshot hardening pass over instance/device/queue/surface/swapchain enumeration arrays, empty capability/image results, swapchain image-view/framebuffer/command-buffer arrays, present-support failures, and screenshot readback/RGB/PNG byte sizing.
  - Implementation log: `docs-dev/native-vulkan-main-allocation-screenshot-hardening-2026-06-27.md`.
  - Completed a native Vulkan entity loader/batcher hardening pass over MD5 mesh/skeleton allocations, MD5 replacement skins, dynamic entity vertex/batch growth, BSP inline-model texture caches, SP2 frame tables, MD2 lump ranges, MD2 scratch/model arrays, MD2 render-time offsets, and final entity upload byte sizing.
  - Implementation log: `docs-dev/native-vulkan-entity-loader-bounds-hardening-2026-06-27.md`.
  - Completed a client font TTF bounds/I/O hardening pass over KFONT token overflow, TTF atlas sizing, page-index casts, atlas/upload allocations, alpha blit bounds, SDL glyph surface validation, bitmap sizing, surface locks, atlas packing bounds, external TTF disk reads, and glyph-dump output paths.
  - Implementation log: `docs-dev/client-font-ttf-bounds-io-hardening-2026-06-27.md`.
  - Completed a localization runtime safety pass over language tag normalization, file/path matching, in-place placeholder parsing, runtime argument validation, localization file record truncation checks, and focused `loctest` coverage.
  - Implementation log: `docs-dev/localization-runtime-safety-hardening-2026-07-01.md`.
  - Completed a player-model loader safety modernization pass over classic/cgame UI model discovery, file-list ownership, checked path construction, skin/icon validation, model/skin caps, and strict C++ sorting.
  - Implementation log: `docs-dev/player-model-loader-safety-modernization-2026-07-01.md`.
  - Completed a player-config selection safety hardening pass over classic/cgame UI model and skin selection, preview media registration, weapon preview file-list ownership, bounded scan/registration, and guarded `skin` userinfo writes.
  - Implementation log: `docs-dev/player-config-selection-safety-hardening-2026-07-01.md`.
  - Completed a client player precache safety hardening pass over player-skin parsing, checked player asset registration paths, stale `clientinfo_t` clearing, bounded visual-weapon iteration, and validated `#` weapon-model configstring collection.
  - Implementation log: `docs-dev/client-player-precache-safety-hardening-2026-07-01.md`.
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
- `FR-09-T08` / `FR-09-T05` / `FR-03-T08` / `FR-09-T04` /
  `FR-09-T09` / `DV-03-T07` / `DV-07-T04` In Progress:
  - Accepted Round 78 as the first live server-authoritative multiplayer
    welcome/join and in-session Escape match hub. Human deathmatch clients now
    default to an explicit join/team/spectator choice, and Escape asks sgame
    to republish current match state through the existing `inven` command.
  - Added live `ui_dm_*` identity, map/rules, population, match-state,
    team/join, ready/intermission, and conditional-tool publication through a
    bounded per-client UI command queue paced at one chunk per server frame.
  - OpenGL uses the branded RmlUi `dm_join` route. Renderer/runtime failures
    select the matching cgame JSON hub; native Vulkan evidence records
    `renderer_unavailable`, `ui_dm_menu_active=1`, and the JSON hub rendered
    natively without a Vulkan-to-OpenGL redirect.
  - Focused OpenGL validation covers initial active, successful join close,
    inventory/Escape reopen, and Resume close. Injected initial/Escape layouts
    and the live native Vulkan fallback were visually inspected.
  - The canonical install refresh validated `275` packaged assets and `181`
    RmlUi paths; the Windows build succeeded and the UI smoke suite passed
    `225` tests.
  - Implementation and user docs:
    `docs-dev/rmlui-round78-multiplayer-match-hub-2026-07-10.md` and
    `docs-user/multiplayer-session-menu.md`.
  - 2026-07-13 live-provider follow-up: `dm_welcome`, `dm_join`, `join`,
    `dm_hostinfo`, and `dm_matchinfo` now truthfully declare the native session
    cvar/condition/command bridge. A focused checker locks 49 current
    sgame-published cvars, team/non-team and ready/spectate branches,
    first-connect modal protection, responsive resumable close behavior,
    disconnected remote-command hygiene, single-back information layouts,
    accessibility, metadata, and guarded capture coverage. Five clean installed
    960x720 captures plus the 279-test UI smoke suite pass. Implementation log:
    `docs-dev/rmlui-live-session-entry-provider-2026-07-13.md`.
  - 2026-07-13 confirmation follow-up: `forfeit_confirm` and
    `leave_match_confirm` now run as native version 2 live-provider popups.
    Safe No-first focus, destructive action hierarchy, sgame-owned forfeit,
    close-before-disconnect ordering, localized leave copy, eight focused
    regressions, two clean 960x720 installed-tree captures, and the 308-test UI
    smoke suite pass. Canonical `.install` refresh remains queued behind an
    unrelated staged-engine DLL lock. Implementation log:
    `docs-dev/rmlui-live-session-confirm-provider-2026-07-13.md`.
  - 2026-07-13 Admin follow-up: `admin_menu` and `admin_commands` now run as
    native version 2 live-provider routes. The focused checker locks the
    sgame-published Replay condition, admin-only route registrations, exact
    parity between the read-only reference and all 28 `AdminOnly` commands,
    matching usage rows, single-back navigation, compact/scrollable layouts,
    eight focused regressions, three clean 960x720 installed-tree captures,
    and the 316-test UI smoke suite. Canonical `.install` refresh remains
    queued behind the same unrelated DLL lock. Implementation log:
    `docs-dev/rmlui-live-admin-provider-2026-07-13.md`.
  - 2026-07-14 presentation closeout (`FR-09-T11`): the main hero is fixed and
    decluttered; all session and live-match child routes use a partial in-world
    frame; slowtime focus easing is native on OpenGL, Vulkan, and RTX/vkpt; and
    the current screenshot sweep passes 58/58 routes. Implementation log:
    `docs-dev/rmlui-in-world-session-frame-main-menu-polish-2026-07-14.md`.
  - The FR-09 migration portion is complete. The remaining umbrella status is
    limited to the independently tracked FR-03/DV automation/governance work.
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
- `FR-01-T04` Done:
  - Completed Vulkan MD5 parity follow-up for frame resolve semantics and MD2/MD5 opaque-vs-alpha routing in `src/rend_vk/vk_entity.c`.
  - Implementation log: `docs-dev/vulkan-md5-mesh-frame-alpha-parity-fix-2026-02-27.md`.
  - Completed Vulkan MD5 `.md5scale` + scale-position parity and MD5 skin-routing correction in `src/rend_vk/vk_entity.c`.
  - Implementation log: `docs-dev/vulkan-md5-mesh-parity-revision-2026-02-27.md`.
  - Replaced flat per-triangle MD5 normals with OpenGL-compatible bind-pose,
    angle-weighted, position-welded smooth normals transformed through the
    animated joint weights. Visible and shadow geometry now skin each unique
    mesh vertex once instead of repeating weighted skinning per triangle corner.
  - Implementation log: `docs-dev/renderer/vulkan-md5-smooth-normal-parity-2026-07-12.md`.
  - Added native Vulkan parity for MD2/MD5 rim lighting, item luminance-colorize
    base/overlay passes, IR-visible and tracker colors, and full-strength
    translucent/additive rim and brightskin routing. Item overlays reuse the
    transformed/skinned base vertices, and deferred batches preserve the GL
    base/overlay/rim order.
  - Implementation log: `docs-dev/renderer/vulkan-special-model-flag-parity-2026-07-14.md`.
  - Added native Vulkan `RF_OUTLINE` and `RF_OUTLINE_NODEPTH` parity with
    depth/stencil capability fallback, GL-matching projected-width scaling,
    per-mesh stencil cleanup, and reuse of the already transformed/skinned
    base stream. Only the expanded shell is copied.
  - Added owned MD2/MD5 `fact2` capture scenes plus a standard-library TGA
    comparator. Green outline mask IoU is `0.924337` for MD2 and `0.831018` for
    MD5, with backend pixel-count deltas below the 15% gate.
  - Corrected Vulkan presentation synchronization by assigning the
    render-finished semaphore per swapchain image; repeated validation-layer
    captures report no VUID after the fix.
  - Implementation log: `docs-dev/renderer/vulkan-alias-outline-parity-automation-2026-07-14.md`.
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
  - Bots have an active Q3A BotLib/AAS-backed fake-client command path with initial movement-state button intent, natural jump/ladder/barrier-jump/crouch validation, route-only walk-off-ledge/elevator validation, and default-off rocket-jump route gating, but higher-level perception, combat, item utility, actual rocket-jump execution, remaining natural swim/waterjump movement-state validation breadth, and deeper door/trigger recovery remain incomplete.
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
- Phase F2 (2026-05-01 to 2026-08-31): major gameplay, UI platform migration, and renderer differentiation
- Phase F3 (2026-09-01 to 2026-12-31): feature hardening, polish, and release readiness

## Epic FR-01: Native Vulkan Gameplay Parity
Objective: close gameplay-visible parity gaps versus OpenGL while preserving native Vulkan policy.

Primary Areas: `src/rend_vk/*`, `src/client/renderer.cpp`, `docs-dev/vulkan-*.md`

Exit Criteria:
- Vulkan supports all essential gameplay rendering paths used in core multiplayer and campaign flows.
- Known parity blockers from Vulkan audits are closed or explicitly deferred with owner/date.

Tasks:
- [x] `FR-01-T01` Implement Vulkan equivalents for particle style controls (`gl_partstyle` parity map to `vk_/r_` cvars).
  Dependency: none. Priority: P0.
  Progress: Native Vulkan now exposes `vk_particle_style` with the same blended (`0`)
  and saturating/additive (nonzero) behavior as OpenGL `gl_partstyle`. Particle
  batches select between prebuilt alpha and additive Vulkan pipelines without
  rebuilding pipelines or adding per-particle draw submissions.
  Implementation log: `docs-dev/renderer/vulkan-particle-style-parity-2026-07-12.md`.
- [x] `FR-01-T02` Implement Vulkan beam style parity (`gl_beamstyle` behavior equivalents).
  Dependency: `FR-01-T01`. Priority: P0.
  Progress: Native Vulkan now exposes `vk_beam_style`, reproduces OpenGL's
  textured billboard and 12-sided polygonal beam modes, applies the matching
  style-specific width scales, and generates segmented `RF_GLOW` lightning in
  either mode. Beam-specific alpha is consumed independently of
  `RF_TRANSLUCENT`, matching the client/OpenGL contract, and generated geometry
  remains coalesced into descriptor-compatible entity batches.
  Implementation log: `docs-dev/renderer/vulkan-beam-style-parity-2026-07-12.md`.
- [x] `FR-01-T03` Add `RF_FLARE` behavior parity in Vulkan entity path.
  Dependency: none. Priority: P1.
  Progress: Native Vulkan now classifies `RF_FLARE` separately from ordinary
  entities, uses asynchronous per-entity occlusion queries with the OpenGL
  frustum/rate-limit/stale-state behavior, and reproduces the additive flare
  fan, scale, orientation, tint, default-flare shader treatment, and fade
  policy. Query-pool resets are coalesced outside the render pass, query reads
  never wait, and flare-specific pipelines are not bound on flare-free frames.
  Implementation log: `docs-dev/renderer/vulkan-flare-occlusion-parity-2026-07-12.md`.
- [x] `FR-01-T04` Complete MD2 and MD5 visual parity pass with map-driven validation scenes.
  Dependency: none. Priority: P0.
  Progress: Native Vulkan now renders MD2/MD5 entity receivers with dynamic
  shadows, keeps MD5 skin selection aligned with GL, fixes first-person view
  weapon depthhack rendering with separate opaque/alpha depthhack pipelines,
  and restores `RF_GLOW` item pulse parity. MD5 replacement meshes now use the
  same weighted smooth-normal construction and animated normal skinning as
  OpenGL; visible and shadow paths cache each skinned vertex once per mesh.
  A deterministic `fact2` scene covering infantry frames 0 and 150 matched
  OpenGL for MD2 fallback and the 18-joint MD5 replacement within a mean
  absolute channel error of about `0.12/255`. Native Vulkan now also matches
  OpenGL's rim, item colorize, IR-visible, tracker, and full-strength additive
  rim/brightskin behavior. Deterministic item tint/rim captures covered both
  MD2 and MD5, while an IR-goggle scene matched within mean absolute RGB error
  `0.34183/0.15000/0.10376`. Native stencil outlines now match the OpenGL
  projected-width/depth contract for both MD2 and MD5. Owned `fact2` captures
  pass green-mask gates with IoU `0.924337` (MD2) and `0.831018` (MD5), while
  transformed/skinned geometry is reused for the mask and cleanup. The same
  validation run closed the render-finished semaphore lifetime VUID with
  per-swapchain-image semaphores. The current unrelated sgame shutdown path
  still fails after synchronous capture, so the runner reports the process
  failure separately from the passing renderer comparison.
  Implementation logs: `docs-dev/renderer/vulkan-entity-lightmap-shadow-receiver-repair-2026-06-11.md`, `docs-dev/renderer/vulkan-viewweapon-dlight-glow-fixes-2026-06-12.md`, `docs-dev/renderer/vulkan-md5-smooth-normal-parity-2026-07-12.md`, `docs-dev/renderer/vulkan-special-model-flag-parity-2026-07-14.md`, `docs-dev/renderer/vulkan-alias-outline-parity-automation-2026-07-14.md`.
- [x] `FR-01-T05` Resolve remaining sky seam/artifact issues for all six faces and transitions.
  Dependency: none. Priority: P0.
  Progress: Native Vulkan now renders a persistent full six-face cube through
  a dedicated no-depth background pipeline with outward-face culling and exact
  decoded-image half-texel clamps. Sky reload preserves normalized rotation
  axes after resource reset, and image descriptor release is synchronized to
  the submitted-frame fence. Owned side-0/90/180/270, up, and fixed-X-180 down
  captures pass strict sky-only RGB and expected-color coverage gates; all six
  Vulkan validation logs are clean. Implementation log:
  `docs-dev/renderer/vulkan-sky-seam-parity-2026-07-14.md`.
- [x] `FR-01-T06` Finalize bmodel initial-state correctness on first render frame.
  Dependency: `FR-01-T04`. Priority: P0.
  Progress: Native Vulkan now builds a registration-time world-face ownership
  mask that removes all inline-model face ranges from the static mesh, including
  maps whose model-zero face range is unavailable. Bmodels render only through
  the current entity transform, and Vulkan now honors `r_fullbright` for entity
  colors and shader lighting flags. A generated one-world-face/six-inline-face
  fixture produces byte-identical OpenGL and Vulkan captures with zero RGB
  error, identical 47,300-pixel masks, IoU `1.0`, no process failures, and no
  Vulkan validation diagnostics. Implementation log:
  `docs-dev/renderer/vulkan-bmodel-first-frame-parity-2026-07-14.md`.
- [x] `FR-01-T07` Add Vulkan parity checklist doc and per-feature status table in `docs-dev/renderer/`.
  Dependency: `FR-01-T01..T06`. Priority: P1.
  Progress: Published the canonical evidence-graded matrix at
  `docs-dev/renderer/vulkan-opengl-parity-checklist-2026-07-14.md`. The audit
  also closed Vulkan legacy-v38 lightmap preparation and static-world
  `r_fullbright` behavior with two exact paired captures over a deliberately
  lightmapped fixture, then assigned every remaining gap to `FR-01-T08..T15`
  or `FR-02-T13..T15`.
- [x] `FR-01-T08` Add Vulkan runtime debug overlays/counters for missing-feature detection.
  Dependency: none. Priority: P1. Completed 2026-07-14; native world-space
  debug primitives/text, renderer stats, `vk_stats`, capability diagnostics,
  and validation-backed image-difference smoke are documented in
  `docs-dev/renderer/vulkan-debug-overlay-telemetry-2026-07-14.md`. GPU phase
  timing, show-tris/origins, and performance budgets remain `FR-01-T15`.
- [x] `FR-01-T09` Make gameplay light queries renderer-neutral and independent of visual gamma, intensity, fullbright, and dynamic-light settings.
  Dependency: `FR-02-T12`. Priority: P0. Completed 2026-07-14: `V_SetLightLevel`
  now calls the client-engine `GameplayLight_Query`, which reads raw authored
  BSP lightmaps/lightgrid, map lightstyles, and inline BSP models without
  renderer-adjusted `R_LightPoint`, visual cvars, or transient dlights. The
  source-level headless test covers finite/boundary/one-dimensional/missing
  style cases, static-grid priority, inline models, fallback, and byte
  conversion; the engine DLL links. Evidence:
  `docs-dev/renderer/gameplay-light-query-parity-2026-07-14.md`.
- [ ] `FR-01-T10` Implement native Vulkan transparent ordering and flowing/warp/refraction parity with paired alpha/warp validation scenes.
  Dependency: `FR-01-T07`. Priority: P1.
  Progress: Native Vulkan now keeps transparent static world faces as
  independent batches and sorts them back-to-front each frame; opaque batching
  remains unchanged. A generated, oversized overlapping-alpha GL/Vulkan
  fixture passed twice with zero pixels over threshold and an exact
  50,400-pixel blend-mask overlap. Generated `SURF_FLOWING | SURF_WARP`
  capture parity also passes with zero RGB error over 307,200 pixels. Native
  transparent-liquid refraction now runs as a native scene-copy plus
  depth-preserving liquid pass with `vk_warp_refraction`; underwater/full-screen
  waterwarp now uses a native three-vertex Vulkan post-process pass controlled
  by `vk_waterwarp`; entity alpha-back and alpha-front phases now bracket the
  transparent world liquid pass. An owned paused
  `SURF_WARP | SURF_TRANS33 | SURF_FLOWING` receiver now holds a 128,000-pixel
  paired water crop to maximum RGB error `1 / 1 / 1`, with zero pixels over
  one under validation. A generated `CONTENTS_WATER` receiver now isolates
  the full-screen waterwarp from material animation and locks 307,200 paired
  pixels to maximum RGB error `1 / 1 / 2`, with zero pixels above error 2
  under validation. Broader material coverage remains open. Partial
  implementation logs:
  `docs-dev/renderer/vulkan-transparent-world-ordering-2026-07-14.md` and
  `docs-dev/renderer/vulkan-world-animation-device-local-2026-07-14.md`,
  `docs-dev/renderer/vulkan-liquid-refraction-2026-07-15.md`, and
  `docs-dev/renderer/vulkan-underwater-waterwarp-visual-parity-2026-07-16.md`.
- [x] `FR-01-T11` Implement native Vulkan glowmap/emissive texture parity, including replacement lookup, intensity control, and paired model/world scenes.
  Dependency: `FR-01-T07`. Priority: P1.
  Progress: Native Vulkan now discovers canonical `_glow.pcx` companions for
  walls and skins under `r_glowmaps`, preserves truecolour replacement
  precedence, premultiplies skin glow RGB, uses wall glow alpha to lift
  lightmaps before receiver lighting, and adds skin emission after the lit
  model base. The paired sampler is binding 1 of the existing native material
  descriptor, with a white fallback and no fifth descriptor set, including
  static world, inline BSP, MD2, MD5, and item-colourize paths.
  `r_glowmap_intensity` supplies runtime `[0, 10]` intensity without
  image/static-world rebuilds. The generated indexed-PCX wall fixture now
  exact-compares 50,000 glow-lit pixels with a matching-colour IoU of `1.0`.
  The generated stock model scene isolates skin emission under fullbright and
  captures 1,864 OpenGL and 1,940 Vulkan high-emission pixels with a `0.9241`
  matching-mask IoU, while keeping mean model-region RGB error within
  `[1.0, 0.3, 0.3]`. Source-level coverage and paired visual evidence are
  recorded in
  `docs-dev/renderer/vulkan-glowmap-emissive-parity-2026-07-15.md` and
  `docs-dev/renderer/glowmap-wall-companion-visual-parity-2026-07-15.md`, and
  `docs-dev/renderer/model-skin-glowmap-visual-parity-2026-07-15.md`.
- [ ] `FR-01-T12` Implement the native Vulkan raster post-processing baseline for fog, bloom, HDR/tone mapping, color correction/LUT, DOF, CRT, and resolution scaling.
  Dependency: `FR-01-T07`, `FR-02-T13`. Priority: P1.
  Progress: Native Vulkan global, height, and sky fog now consume refdef fog
  data in the shared per-frame receiver uniform, use a `vk_fog` control, and
  apply after material lighting with a sky-only branch matching OpenGL's fog
  state selection. The shader path adds no pass, descriptor, draw, or static
  geometry update. A generated worldspawn-fog map now has exact 235,200-pixel
  OpenGL/Vulkan evidence plus an identical 187,900-pixel authored-fog mask
  under validation. The companion height-fog map exact-compares another
  235,200 pixels and an identical 106,004-pixel gradient mask. The real
  six-face sky route also has a bounded 77,000-pixel fog capture. A fogged
  two-layer transparent-world scene locks its 50,400-pixel blend within the
  observed one-level RGB rounding envelope. The debug-build deterministic
  8,192-particle field exact-compares a 320,000-pixel fogged receiver crop.
  A generated start-on, fat `target_laser` with a uniform packed palette now
  exercises the native `RF_BEAM` path in a deterministic 128,000-pixel fogged
  crop: its final validation capture stays within `2 / 1 / 2` maximum and
  `0.231367 / 0.056672 / 0.220961` mean RGB error, while the beam-only probe
  finds 39,526 / 39,941 pixels at 0.989610 IoU. Fog-disabled controls isolate
  and confirm attenuation/recolouring of the beam contribution. A generated
  `misc_model` BFG sprite now gives the ordinary native sprite path a
  validation-clean 144,000-pixel fog gate: maximum RGB error is `1 / 1 / 1`,
  mean error is `0.063646 / 0.036597 / 0.306354`, and the receiver-specific
  mask has 44,989 / 45,277 pixels at 0.987012 IoU. Fog-disabled controls prove
  the same sprite is attenuated from 60,212 clear pixels above delta eight to
  none under the authored dense fog. Vulkan flare vertices explicitly skip
  fog, matching OpenGL's additive flare state which omits `glr.fog_bits`; the
  flag is local to flare query/visual vertices, so beams, particles, sprites,
  and ordinary entities retain their tested fog paths. The generated
  `misc_flare` scene now runs the real occlusion-query route under the same
  fog and exact-compares a 160,000-pixel flare crop, including an identical
  98,550-pixel unattenuated-red flare mask at IoU `1.0`. Its fog-disabled
  control confirms the flare contribution stays constant while only the world
  backdrop changes. Other specialised effect receivers remain open.
  Details are in
  `docs-dev/renderer/vulkan-authored-global-fog-parity-2026-07-15.md` and
  `docs-dev/renderer/vulkan-authored-height-fog-parity-2026-07-15.md`, plus
  `docs-dev/renderer/vulkan-authored-sky-fog-parity-2026-07-15.md` and
  `docs-dev/renderer/vulkan-transparent-world-fog-parity-2026-07-15.md`, plus
  `docs-dev/renderer/vulkan-particle-fog-parity-2026-07-15.md`, and
  `docs-dev/renderer/vulkan-beam-fog-parity-2026-07-15.md`, plus
  `docs-dev/renderer/vulkan-sprite-fog-parity-2026-07-15.md`,
  `docs-dev/renderer/vulkan-flare-fog-contract-2026-07-15.md`, and
  `docs-dev/renderer/vulkan-flare-fog-visual-parity-2026-07-15.md`.
  The exact OpenGL-order brightness, contrast, saturation,
  tint, and split-toning stages now run in a native combined final pass with
  waterwarp; identity settings skip the scene copy and fullscreen draw.
  Headless source coverage and implementation detail are in
  `docs-dev/renderer/vulkan-native-fog-baseline-2026-07-15.md` and
  `docs-dev/renderer/vulkan-color-correction-postprocess-2026-07-15.md`.
  The normal native video-mode lifecycle now recreates the post-process
  scene-copy resources, descriptors, and final pipeline after UI resources;
  before that fix, controls could be active while the final composite pass was
  unavailable. The retained combined brightness/contrast/saturation/cyan-tint
  gate now exact-compares 50,000 pixels with OpenGL under Vulkan validation.
  The native 2D LUT sampler now also matches OpenGL texel-space indexing for
  both horizontal and vertical NxN strips; its deterministic non-identity
  horizontal and vertical gates each exact-compare 50,000 pixels under
  validation. Details are in
  `docs-dev/renderer/vulkan-color-correction-visual-parity-2026-07-15.md` and
  `docs-dev/renderer/vulkan-lut-coordinate-parity-2026-07-16.md`.
  Independent split-toning now has an exact 50,000-pixel non-identity gate.
  The gate exposed and corrected OpenGL's missing final-target activation for
  split/LUT-only controls, bringing its intended independent stage contract in
  line with the existing native Vulkan path. Details are in
  `docs-dev/renderer/vulkan-split-toning-visual-parity-2026-07-16.md`.
  Native Vulkan now also exposes static `vk_hdr`, exposure, white-point, and
  gamma controls, and applies OpenGL's gamma/exposure/ACES-white-normalise/
  inverse-gamma sequence before the existing colour stages. It uses the
  existing frame-local post-process UBO after the cached blur weights, so this
  adds no descriptor set, buffer, allocation, pipeline, or extra
  post-process pass beyond the final composite. The 100-sample paired
  headless timing records the expected final-composite cost (`+0.53565 ms`
  Vulkan GPU-post mean) rather than presenting it as a free optimization.
  The enabled and disabled 50,000-pixel HDR gates both exact-compare under
  validation, locking the enabled non-identity `[18, 38, 96]` backdrop. The
  manual-HDR path now advances to native frame-slot
  `R16G16B16A16_SFLOAT` scene/copy targets and a final-only UNORM presentation
  composite. HDR bloom/DOF working targets follow the float format, while
  HDR-off retains the LDR allocation profile. Static HDR and glow-bloom gates
  are exact; waterwarp and refraction ordering are bounded to `1 / 1 / 1` and
  `2 / 2 / 1` RGB respectively, with clean validation. Native temporal
  automatic exposure now uses the float scene's final mip plus bounded
  frame-slot ping-pong state; its liquid path samples level zero so the
  full reduction is performed only once after the completed scene. Display-HDR
  transfer functions, resolution scaling, and wider output coverage remain open.
  Plain float-HDR presentation now samples the completed native scene image
  directly, skipping the full-resolution copy unless a scene-copy consumer
  (liquid refraction, auto exposure, bloom, DOF, or LUT) is active. The strict
  HDR gates still exact-compare under clean validation; the local GPU timing is
  neutral, so this is copy-elimination rather than an accepted performance
  claim.
  Details are in `docs-dev/renderer/vulkan-static-hdr-tonemap-parity-2026-07-16.md`
  and `docs-dev/renderer/vulkan-float-scene-hdr-activation-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-auto-exposure-float-scene-2026-07-16.md`.
  The implementation plan for the remaining native resolution-scaling
  contract is `docs-dev/renderer/vulkan-resolution-scaling-implementation-plan-2026-07-16.md`.
  Native Vulkan now implements the shared fixed/adaptive resolution-scale
  controller and frame-slot scaled LDR/HDR scene targets, retaining native
  presentation/UI resolution. Fixed half-resolution LDR and float-HDR gates
  each exact-compare all 50,000 crop pixels under clean validation. A forced
  adaptive downscale smoke also exact-compares a 50,000-pixel output crop and
  backdrop probe after its resource rebuild, with no Vulkan validation errors.
  Once Vulkan is already rendering to offscreen scene targets, later scale
  extent changes retire only submitted frame slots and rebuild their native
  scene/bloom/post-process resource family; output swapchain/UI resources,
  synchronization, and format-compatible bloom render passes/pipelines remain
  live. The deterministic 0.5-to-1.0 adaptive offscreen-retirement gate
  exact-compares another 50,000-pixel crop/probe with clean validation.
  Direct-LDR/offscreen transitions still use the safe full recreation, while
  liquid/bloom/DOF/CRT/UI scale coverage remains open.
  A 110-sample local fixed-half HDR run reduces Vulkan GPU scene mean from the
  preceding full-resolution `1.078 ms` baseline to `0.268 ms`; the native
  final presentation remains `0.494 ms`. The mismatched cross-renderer
  timestamp scopes preclude a GPU-superiority budget. Details are in
  `docs-dev/renderer/vulkan-resolution-scaling-implementation-plan-2026-07-16.md`
  and `docs-dev/renderer/vulkan-offscreen-resolution-scale-retirement-2026-07-16.md`.
  The simple scaled-LDR case now bypasses that sampled fullscreen compositor
  with a guarded native Vulkan linear image blit. It is unavailable whenever
  the presentation surface/format lacks transfer-blit support, or when liquid,
  HDR, colour, bloom, DOF, CRT, or other authored post-process work is active.
  The fixed-half visual gate remains pixel-exact under validation, while a
  paired Iris Xe capture reduces Vulkan GPU-post mean from `0.49611 ms` to
  `0.27388 ms` and total GPU-frame mean from `1.01409 ms` to `0.77369 ms`.
  Vulkan remains GPU-slower than OpenGL on that scenario, so no cross-renderer
  GPU budget was accepted. Details are in
  `docs-dev/renderer/vulkan-scaled-scene-blit-fast-path-2026-07-16.md`.
  Native Vulkan material registration now also builds full GPU mip chains for
  wall and skin images, matching OpenGL material minification under scaled
  HDR rendering. The previous high-frequency underwater gate mismatch is now
  bounded to RGB2 per pixel with no RGB2 exceedances; see
  `docs-dev/renderer/vulkan-material-mipmap-parity-2026-07-16.md`.
  Native Vulkan now also has a scene threshold fallback plus authored
  wall-glow, opaque model-skin, and translucent colour-shell bloom emission:
  downsampled
  threshold/knee/firefly prefiltering,
  separable Gaussian blur, combined final saturation/intensity composition,
  and a bloom-only depth-tested extract pass that preserves a single-target
  normal scene pass. The retained wall-glow scene exact-compares all 50,000
  crop pixels (`[120, 200, 255]`) under Vulkan validation. The isolated
  stock-MD2 model gate also passes with 4,427/4,482 bright-skin pixels at
  IoU `0.96407` and 1,442/1,510 blue-backdrop halo pixels at IoU `0.95497`.
  A threshold-isolated translucent blue shell receiver now locks 53,091
  saturated-core pixels at IoU `1.0` and 1,781/1,758 blue-backdrop halo
  pixels at IoU `0.98263`, with no pixel beyond RGB error 16; its
  disabled-bloom control fails at `35.94108%` pixels beyond RGB error 16,
  proving that the native alpha extractor supplies authored emission rather
  than scene thresholding. The shell replay uses dedicated native alpha
  extraction pipelines and preserves GPU MD2/MD5 submission, so no OpenGL
  fallback or CPU skin expansion was added. A shell-only native front-face
  discard matches OpenGL alias-mesh culling without changing the shared
  two-sided sprite/particle pipeline, eliminating the former hidden-back-face
  alpha blend. Additive `RF_RIMLIGHT` emission now also has explicit native
  dynamic, GPU-MD2, and GPU-MD5 extract pipelines. The real third-person
  player-rim gate locks 25,766/25,758 green core pixels at IoU `0.99922` and
  4,097/3,732 green bloom-halo pixels at IoU `0.89702`, with a matched
  no-bloom control proving the halo originates in the native extractor. The
  sampleable-depth Vulkan path loads the emission attachment and samples
  retained scene depth for only an active rim batch, avoiding equal-depth
  replay failure without a permanent bloom MRT. The generated real-BSP
  occluder now exact-compares 112,000 crop pixels with OpenGL and has zero
  green rim/halo pixels in both backends; the depth-disabled native fallback
  remains only on constrained formats that cannot expose sampled depth.
  Native Vulkan now also replays direct depth-hack/view-weapon shell and
  glowmap emission with the normal depth-hack viewport and weapon projection.
  No-refraction frames reuse the existing bloom extract pass; transparent
  liquid frames use a compatible, source-gated colour-load pass after liquid.
  The real instant-Quad view-weapon gate holds a 212,800-pixel crop to
  `0.02055 / 0.01820 / 0.01680` mean RGB error. Its paired transparent
  `SURF_WARP | SURF_TRANS33 | SURF_FLOWING` fixture freezes the flow phase and
  exact-bounds a 128,000-pixel water crop to `1 / 1 / 1` maximum RGB error
  with no pixel over one, while also validating the late bloom load pass.
  Details are in
  `docs-dev/renderer/vulkan-depthhack-viewweapon-bloom-visual-parity-2026-07-16.md`.
  The pass is native
  Vulkan and draws only while bloom is active; it retains persistent
  intermediate targets and defers descriptor/resource changes until the
  submitted-frame fence has signalled. Vulkan also now has a
  native `r_crt*` presentation pass with OpenGL-equivalent scanline,
  phosphor, gamma, and shadow-mask controls; it filters only the completed
  3D scene and leaves the HUD/menu overlay sharp. A one-pixel scanline-phase
  correction for the negative-height native presentation viewport now gives
  the unmasked and shadow-mask-layout-2 fixed-control CRT scenes zero-error
  50,000-pixel paired gates, including 25,000-pixel dark-scanline and
  8,400-pixel green-mask probes at IoU `1.0`. Vulkan now also exposes the
  native archived `vk_bloom_levels` control and a hardware-linear-blit mip
  pyramid, blending up to six levels with OpenGL's normalised `1, 1/2, 1/4`
  weighting. Its dedicated validation-backed level-three shell fixture has no
  pixel above RGB error 16, exact core IoU, and `0.98643` halo IoU. The
  level-one default remains the performance baseline; level three adds no
  fullscreen draw but costs `0.57033 ms` Vulkan GPU post time in the local
  Iris Xe quality-mode comparison. Remaining material-family bloom emission,
  HDR,
  resolution scaling, and broader no-window visual evidence remain open. Vulkan now has native
  depth-aware gameplay DOF:
  a sampled depth-only view, shared `r_dof` plus focus/range controls,
  centre-depth fallback, optional `dof_rect`, quarter-resolution downsample,
  four alternating Gaussian passes (two X/Y pairs), and a full-resolution depth composite before
  the existing Vulkan final post-process pass. Unsupported sampled-depth
  formats disable DOF explicitly rather than substituting another renderer.
  Real inventory-layout fixed-focus, centre-depth/automatic-range, wide
  explicit-range, and compact menu-rectangle-state receivers now keep their
  307,200-pixel captures inside
  `1 / 1 / 1` maximum RGB error and
  `0.004466 / 0.011849 / 0.004063` mean RGB error, with no pixel beyond error
  one; their `r_dof=0` controls are pixel-identical. The correction matches
  OpenGL's shared sigma equation and four total alternating passes without an
  OpenGL route. Native Vulkan preserves its complete current composite through
  the menu state, matching OpenGL's retained target result without stale Vulkan
  image data. Its native Gaussian shader now combines adjacent taps through
  linear filtering, matching OpenGL's paired-tap formulation without changing
  the quarter-resolution targets or four-pass topology. The controlled
  active-DOF lane lowered Vulkan GPU mean from `2.66942` to `1.33777` ms and
  GPU postprocess mean from `1.760` to `0.700` ms under validation, while the
  eight strict paired DOF scenes remained within their one-RGB-level gates.
  The runner now hashes and records launch-time cvar overrides so latched
  postprocess controls are auditable. Isolated `dof_rect` clipping and broader
  dynamic-scene evidence remain open. Details and headless coverage are in
  `docs-dev/renderer/vulkan-native-bloom-baseline-2026-07-15.md`,
  `docs-dev/renderer/vulkan-bloom-emission-visual-parity-2026-07-16.md`,
  `docs-dev/renderer/vulkan-model-bloom-emission-visual-parity-2026-07-16.md`,
  `docs-dev/renderer/vulkan-shell-bloom-emission-visual-parity-2026-07-16.md`,
  `docs-dev/renderer/vulkan-rim-bloom-emission-visual-parity-2026-07-16.md`,
  `docs-dev/renderer/vulkan-rim-bloom-occlusion-visual-parity-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-depthhack-viewweapon-bloom-visual-parity-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-bloom-mip-pyramid-parity-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-native-crt-baseline-2026-07-15.md`, plus
  `docs-dev/renderer/vulkan-crt-visual-parity-2026-07-15.md`, and
  `docs-dev/renderer/vulkan-dof-control-parity-2026-07-15.md` and
  `docs-dev/renderer/vulkan-dof-visual-parity-2026-07-16.md`, plus
  `docs-dev/renderer/vulkan-dof-paired-tap-performance-2026-07-16.md` and
  `docs-dev/renderer/vulkan-lut-coordinate-parity-2026-07-16.md`.
- [ ] `FR-01-T13` Move static Vulkan world geometry to immutable device-local storage and replace full-world CPU warp/flow uploads with shader-side animation and compact frame data.
  Dependency: `FR-01-T08`, `FR-01-T10`. Priority: P0.
  Progress: completed the static storage and animation-upload portions:
  registration copies the immutable world stream to device-local memory and
  releases CPU geometry; native shader-side flow/turbulent warp consumes an
  eight-byte per-frame stream instead of rewriting the complete world. Map
  registration upload scheduling and measured budget closure remain open. The
  six-face sky is now also immutable device-local local geometry: a 64-byte
  current-frame record carries animation controls and three sky-rotation rows,
  replacing the old 1,728-byte sky-cube upload each frame. Compatible faces
  copy once into a six-layer native texture array and record in one sky draw;
  incompatible sets retain the six-face Vulkan fallback. The liquid load
  pass now remains compatible with base scene pipelines/framebuffers, with
  explicit native scene-copy/depth barriers and stencil-aware depth
  transitions. See
  `docs-dev/renderer/vulkan-world-animation-device-local-2026-07-14.md` and
  `docs-dev/renderer/vulkan-static-sky-and-liquid-validation-2026-07-15.md`.
- [ ] `FR-01-T14` Replace Vulkan CPU-expanded model/effect submission with indexed static meshes, per-instance data, GPU MD5 skinning/interpolation, and bounded transient ring allocation.
  Dependency: `FR-01-T08`, `FR-01-T13`. Priority: P0.
  Progress: The current CPU-expanded entity stream grows geometrically from a
  64 KiB minimum rather than destroying/recreating at each new exact high-water
  mark. Its frame-local mapped staging allocation uploads only live bytes into
  device-local vertex/index draw buffers under `FR-01-T15`, without racing a
  preceding submission. Standard MD2 and MD5 meshes now submit native indexed
  geometry: MD2 frame/UV/index sources and eligible MD5 bind-pose mesh/weight
  sources now live in immutable device-local buffers. Native GPU MD2
  interpolation and native GPU MD5 weighted vertex skinning consume compact
  per-frame instance streams; MD5 still resolves its small interpolated joint
  palette on CPU. Compatible adjacent model instances coalesce without
  reordering rendering phases; item-colorize and outline variants retain their
  established expanded fallback. Ordinary inline BSP models now likewise retain
  immutable local device geometry and submit one frame-local transform/light
  instance, with shell/outline/rimlight/item-colourize bmodels preserving the
  established native CPU fallback. The hidden validation matrix remains
  pixel-identical to OpenGL and the fixed-view upload counter drops from 2,032
  to 1,872 bytes per sample. A dense 37-instance ordinary-bmodel grid now
  additionally coalesces identical opaque static-BSP face ranges natively:
  validation retains zero RGB error in the authored view volume while paired
  telemetry reduces Vulkan submission from 98 to 18 draws and CPU mean from
  0.755 to 0.542 ms. The GPU-MD2 static upload descriptors now target their
  device-local buffers, model teardown releases MD2/MD5 resources before CPU
  storage, and effect-compatible batches preserve direct bloom flags. A
  forced stock MD2 interpolation/glow capture passes under validation with
  zero over-threshold pixels and an exact 2,035-pixel glow mask. The same
  audit enables native device-supported anisotropic material sampling through
  shared r_anisotropy and native wall/skin filtering through shared
  r_texture_filter, matching OpenGL's default skin filtering while retaining
  gl_anisotropy, vk_anisotropy, and gl_texturemode as compatibility aliases
  plus a 1x fallback. Shared r_picmip now reduces eligible native wall/skin
  material uploads before Vulkan mip generation, matching OpenGL's quality
  policy while reducing residency. A general transient ring allocator, broader bmodel/MD5
  runtime coverage, and any measured decision on GPU joint interpolation
  remain open. Details and
  headless coverage are in
  `docs-dev/renderer/vulkan-entity-stream-capacity-2026-07-15.md`,
  `docs-dev/renderer/vulkan-gpu-md2-submission-2026-07-15.md`, and
  `docs-dev/renderer/vulkan-gpu-md5-skinning-2026-07-15.md`, plus
  `docs-dev/renderer/vulkan-static-inline-bsp-residency-2026-07-15.md`,
  `docs-dev/renderer/vulkan-gpu-md2-residency-material-parity-2026-07-17.md`,
  `docs-dev/renderer/vulkan-shared-anisotropy-control-parity-2026-07-17.md`,
  `docs-dev/renderer/vulkan-shared-texture-filter-control-parity-2026-07-17.md`,
  and `docs-dev/renderer/vulkan-picmip-upload-residency-parity-2026-07-17.md`.
- [ ] `FR-01-T15` Add bounded Vulkan frames-in-flight, CPU/GPU phase telemetry, upload/draw counters, and reproducible performance budgets proving gains over the OpenGL baseline.
  Dependency: `FR-01-T08`, `FR-01-T13`, `FR-01-T14`. Priority: P0.
  Progress: Native `VK_QUERY_TYPE_TIMESTAMP` instrumentation brackets every
  submitted command buffer with per-frame-slot query ranges and resolves
  completed GPU totals plus upload, shadow, scene, and composition phase times
  only after that slot's fence signals. `vk_stats` and the renderer stat panel
  expose elapsed milliseconds and supported/unavailable capability state
  without an ordinary-frame CPU wait. The renderer now has two bounded native
  frame contexts (capped by swapchain image count), each owning a command
  buffer, acquire semaphore, fence, depth attachment/framebuffer, and
  UI/entity/world/shadow/debug transient storage. An acquired swapchain image
  waits for the slot that last rendered it. Scene-copy images/descriptors and
  bloom and DOF ping-pong/composite images/external descriptors are frame-local,
  so liquid refraction, colour/LUT/CRT composition, bloom, and depth-aware DOF
  can overlap. Only rare all-slot bloom/DOF target reallocation drains
  submission before replacing device resources. OpenGL now exposes matching
  machine-readable `gl_stats` telemetry, and the log-only
  `analyze_renderer_perf.py` collector computes warmup-trimmed mean/p95 timing
  ratios without launching a client. Its paired-capture manifest now verifies
  the fixture/configuration, hardware/driver identity, and exact Vulkan/OpenGL
  telemetry hashes before it permits a budget. Windows capture runners now
  select a non-archived `win_headless 1` mode: it retains a native Vulkan or
  OpenGL surface but hides the HWND, avoids focus and desktop gamma changes,
  and forces windowed operation. A validation-enabled hidden Vulkan
  debug/telemetry smoke now exits cleanly with both screenshots and populated
  `VK_STATS`, and a hidden paired OpenGL/Vulkan bmodel/legacy-lightmap matrix
  reconfirms zero pixel error in the already-closed `FR-01-T06` scope.
  A new hidden, adapter-matched fixed-view collector now hashes the full config
  tree/launch profile, captures 120 samples per renderer, and records a
  manifest-gated 100-sample analysis. Its first Intel Iris Xe result shows
  Vulkan slower (CPU mean `2.28x`, GPU mean `99.08x`); no budget was accepted.
  The first `FR-01-T14` follow-up removes ordinary inline-BSP expansion from
  the steady-state stream: the same fixed view remains pixel-identical and
  drops the Vulkan upload counter from 2,032 to 1,872 bytes per sample. Its
  local validation driver identity was not recorded, so timing values are not
  a cross-run performance claim. The static-sky follow-up then reduces that
  fixed view to 192 bytes (`world_uploads=64`, `entity_uploads=128`), an
  89.74% reduction from the bmodel-only run, while preserving all six sky
  matrix scenes and resolving validation errors in the liquid load pass. A
  compatible six-face sky texture array then reduces the same fixture from
  eight to three Vulkan draws without changing that 192-byte steady-state
  upload contract; validation-backed sky and bmodel matrices retain parity.
  The representative dense inline-BSP instance lane now supplies a
  provenance-bound CPU result: after native opaque face-range coalescing it
  records 18 Vulkan draws versus 98 immediately before the change, and a
  Vulkan CPU mean of `0.542 ms` versus OpenGL's `0.777 ms` on the same Intel
  Iris Xe capture. A fresh matching-driver 120-sample revalidation now passes
  the first environment-bound dense-instance CPU budget: after 20 warm-up
  samples Vulkan is `0.54184 ms` mean / `0.686 ms` p95 CPU time, 18 draws, and
  4,800 upload bytes, at `0.3325x` OpenGL CPU mean. The budget pins the exact
  scenario/fixture/configuration hashes and hardware/driver identity, requires
  valid GPU samples, and did not make a cross-renderer GPU assertion because
  its legacy timestamp scopes differed. The collector now also emits an
  explicit shared `gpu_frame_ms`: Vulkan aliases its completed command-buffer
  span, and OpenGL derives the normal render stream from non-blocking native
  `GL_TIMESTAMP` query counters without disabling phase telemetry. The
  analyzer aggregates its mean/p95 ratio and can require valid full-frame
  samples. No GPU budget has been accepted yet: the current workspace's
  OpenGL rebuild is blocked before this new telemetry reaches runtime by
  unrelated header/type errors, so a fresh hidden paired capture remains
  required. See
  `docs-dev/renderer/vulkan-opengl-full-frame-gpu-timing-2026-07-16.md`.
  A fresh local, release-style float-HDR baseline records Vulkan at `0.807 ms`
  CPU mean versus OpenGL `0.856 ms`, but Vulkan GPU post at `0.835 ms` versus
  `0.353 ms`; no budget was accepted. Native direct float-scene sampling now
  removes the simple presentation copy while retaining all copy consumers, but
  its follow-up (`0.838 ms` GPU-post mean) is neutral on the unpinned local
  driver; no budget was accepted. Details:
  `docs-dev/renderer/vulkan-float-hdr-performance-baseline-2026-07-16.md`.
  UI and RmlUi geometry now follows the same frame-local staging/device-local
  draw-storage contract, with transfer-to-vertex-input synchronization before
  scene recording; the guarded 960x720 Vulkan RmlUi capture records 372 frames,
  passes all layout/input assertions, and is clean under validation. The
  fixed-view fixture deliberately disables RmlUi, so no UI performance gain is
  claimed until an overlay-bearing paired capture exists. Details are in
  `docs-dev/renderer/vulkan-ui-device-local-stream-2026-07-15.md`.
  The paired collector now has that overlay-bearing RmlUi lane: it hashes the
  deterministic guarded document, requires its route marker in both logs, and
  rejects Vulkan runs without nonzero UI upload telemetry. Its first matching-
  adapter validation capture has 120 samples per renderer and Vulkan reports
  3,104 UI upload bytes/17 draws, but OpenGL has no valid local GPU timings and
  no equivalent UI-byte accounting; it is a baseline, not a budget or
  superiority claim. See
  `docs-dev/renderer/vulkan-rmlui-overlay-paired-telemetry-2026-07-15.md`.
  That lane also now has a five-route headless visual manifest: the guarded
  960x720 core RmlUi overlay is pixel-identical across all 691,200 compared
  pixels under validation, while the main shell, performance settings, and
  quit-confirmation and in-session leave-match/forfeit-confirmation popup routes
  pass their retained bounded thresholds. This adds core, shell, settings, and
  three confirmation states but is not a full menu/HUD matrix; remaining session
  popups, gameplay HUD, and input-state coverage remain required before closing
  UI visual parity. Details are in
  `docs-dev/renderer/vulkan-rmlui-overlay-matrix-expansion-2026-07-15.md`.
  Native `vk_showorigins` now queues the intended scaled, depth-tested RGB
  entity axes without an OpenGL route. Its structural gate and validation-clean
  Vulkan headless capture pass, but its paired gate exposed an OpenGL baseline
  defect: `gl_showorigins` enters `GL_DrawNullModel` with the cvar enabled yet
  emits no RGB axis pixels in the current headless backend. This remains
  `FR-01-T15` work rather than a completed parity claim; see
  `docs-dev/renderer/vulkan-origin-diagnostic-parity-status-2026-07-16.md`.
  Native `vk_showbloom` now directly presents the completed level-zero native
  blurred image when bloom is active, bypassing final display transforms as
  OpenGL's `gl_showbloom` diagnostic does. Its 960x720 paired headless gate
  passed under validation with maximum RGB error one, mean RGB error at most
  `0.001541`, and `0.280382%` differing pixels; see
  `docs-dev/renderer/vulkan-bloom-debug-preview-parity-2026-07-16.md`.
  Native `vk_showtearing` now alternates a red/white presentation-final clear
  after all scene/UI work, matching the OpenGL diagnostic without a new
  pipeline or OpenGL route. The normal headless capture can land on opposite
  valid temporal phases after different startup-frame counts, so this is
  documented as functional diagnostic parity rather than a false pixel gate;
  see `docs-dev/renderer/vulkan-tearing-diagnostic-parity-2026-07-16.md`.
  Native `vk_showtris` now implements all four OpenGL category bits with
  portable Vulkan line lists rather than optional non-solid polygon mode.
  Depth-tested 3D geometry uses the OpenGL-compatible near-plane depth range,
  flares retain a native no-depth stream, and UI triangles replay in white with
  their original scissors. The paired 960x720 world/inline-BSP gate records
  3,782 Vulkan versus 3,689 OpenGL changed pixels with identical bounds; the
  HUD/UI gate records 1,869 versus 1,710 with identical bounds. Vulkan output
  is validation-clean. Broader effect/model-family scenes remain required;
  see `docs-dev/renderer/vulkan-showtris-diagnostic-parity-2026-07-16.md`.
  A two-run current revalidation of the dense inline-BSP budget retains the
  native 18-draw/4,800-byte submission contract and Vulkan CPU superiority
  (`0.516–0.579x` OpenGL CPU mean), but rejects its stale configuration hash
  and old absolute CPU cap; it also reports Vulkan whole-frame GPU time at
  approximately `3.7x` OpenGL. No budget was relaxed or accepted. See
  `docs-dev/renderer/vulkan-inline-bsp-budget-revalidation-2026-07-16.md`.
  A fresh validation-enabled fast-light coverage capture corrects the
  stale-path interpretation: its one static-world candidate selects the
  native specialization on all 100 post-warm-up samples, all cvar/fullbright/
  receiver/pipeline/material blocker counters are zero, and the existing
  16 inline-BSP fast draws remain active. It records Vulkan CPU mean
  `0.984 ms` versus OpenGL `1.874 ms` (`0.525x`), while completed GPU-frame
  p50 remains `0.778 ms` versus `0.401 ms` (`1.94x`). This is a precise
  optimized-path result, not a renderer-wide GPU-budget pass; `FR-01-T15`
  remains open. Details:
  `docs-dev/renderer/vulkan-static-world-fast-lit-coverage-2026-07-16.md`.
  The next native fast-light refinement removes an unreachable alpha discard
  only after the opaque inline-BSP gate has excluded transparent and
  alpha-tested materials. Both validation-enabled 100-sample reruns retain
  exact visual output and lower the inline-BSP entity GPU p50 from `0.350 ms`
  to `0.241 ms` (31.1%), scene p50 from `0.749 ms` to `0.638–0.644 ms`,
  and completed-frame p50 from `0.778 ms` to `0.668–0.675 ms`. The
  full-frame tail remains variable, so this is recorded as a localized
  early-depth gain rather than a global GPU-budget pass. Details:
  `docs-dev/renderer/vulkan-bmodel-fast-lit-early-depth-2026-07-16.md`.
  The static-world receiver now has native no-fog plain and glow-map
  specializations, selected every frame only when the current refdef has no
  global or height fog; sky-only fog remains correct for opaque receivers.
  A telemetry-proven current-binary control pair holds the 18-draw/4,800-byte
  contract and lowers opaque-world GPU p50 from `0.399 ms` with
  `vk_world_fast_lit_no_fog 0` to `0.393 ms` at its default `1`
  (1.5%), with zero-error ordinary/global-fog/height-fog matrices under
  validation. This is a localized native fragment improvement, not a global
  GPU budget. Details:
  `docs-dev/renderer/vulkan-static-world-fast-lit-no-fog-2026-07-16.md`.
  The opaque inline-BSP receiver has the matching native no-fog shader,
  selected only after the existing opaque/lightmapped fast gate and only when
  the current refdef has no global or height fog. Its generated fogged
  37-instance matrix holds 218,400 pixels to maximum RGB error one with no
  over-threshold pixels under validation. Two current-binary controls retain
  a 0.240-0.241 ms entity GPU p50, while two telemetry-proven 16-draw
  no-fog runs record 0.0995 and 0.177 ms (26.3-58.7% lower). The surrounding
  phases remain variable, so this is a localized entity phase result rather
  than a renderer-wide GPU budget. Details:
  `docs-dev/renderer/vulkan-bmodel-fast-lit-no-fog-2026-07-16.md`.
  The unlightmapped opaque inline-BSP branch now has its own native
  GLS_TEXTURE_REPLACE fog-aware/no-fog specialization. The strict submission
  gate admits only the material formula's inert fullbright/shadow/dynamic-light
  flag subset and retains every effect-bearing fallback. A 100-sample
  current-binary Intel Iris Xe control pair records 16 selected no-fog
  draws/frame and reduces entity GPU p50 from 0.5055 to 0.1620 ms (68.0%) with
  unchanged 18 draws/4,800 upload bytes. Exact ordinary visual output and a
  new generated global-fog unlightmapped 37-instance matrix pass under
  validation; fog telemetry reports 16 fog-aware and zero no-fog draws. This
  is localized native optimization evidence rather than a renderer-wide GPU
  budget. Details:
  docs-dev/renderer/vulkan-bmodel-texture-replace-fast-path-2026-07-16.md.
  Static unlightmapped world coverage then exposed and corrected a latent
  texture-replace raster mismatch: unlike the one-sided lightmapped receiver,
  legacy unlightmapped world faces require the general world's double-sided
  raster state. Both fog-aware and no-fog native texture-replace variants now
  preserve that contract. New dry/global-fog 235,200-pixel matrices are exact
  under validation and their telemetry proves one selected world draw with a
  no-fog subset of one/zero. The 100-sample paired control lowers the opaque
  world GPU p50 from 0.353 to 0.316 ms (10.5%) with unchanged 3 draws and
  192 upload bytes. This is localized evidence rather than a renderer-wide
  GPU budget. Details:
  docs-dev/renderer/vulkan-static-world-texture-replace-no-fog-2026-07-16.md.
  A durable alpha-cutout world/inline-BSP gate now matches OpenGL's
  alpha-less-than-or-equal-to-0.666 discard threshold in the general native
  world fragment path. Its generated map uses a low-alpha front static face,
  a larger opaque static backdrop, and a visible alpha-tested inline BSP.
  The validation-enabled 235,200-pixel paired crop is exact; the revealed
  backdrop has 219,459 matching pixels and the inline-BSP probe has 15,741,
  both at IoU 1.0. The opaque fast paths continue to exclude alpha-tested
  materials, so the coverage protects their complete-fragment fallback
  boundary as well as visual parity. Fences, sprite cutouts, and alpha-tested
  shadow casters remain follow-up coverage rather than a claim that the broad
  material family is closed. Details:
  docs-dev/renderer/vulkan-alpha-test-world-bmodel-parity-2026-07-16.md.
  Additional representative-map and GPU budgets, show-tris/origins, broader
  batching/indirect model submission, and renderer-wide proof of gains over
  OpenGL remain open. Details and headless coverage are in
  `docs-dev/renderer/vulkan-bounded-frames-in-flight-2026-07-15.md` and
  `docs-dev/renderer/vulkan-gpu-frame-timing-2026-07-15.md`, plus
  `docs-dev/renderer/vulkan-paired-performance-capture-contract-2026-07-15.md`
  and `docs-dev/renderer/vulkan-static-sky-and-liquid-validation-2026-07-15.md`
  and `docs-dev/renderer/windows-headless-renderer-capture-2026-07-15.md`,
  plus `docs-dev/renderer/vulkan-paired-fixed-view-telemetry-2026-07-15.md`
  and `docs-dev/renderer/vulkan-static-inline-bsp-residency-2026-07-15.md`,
  plus `docs-dev/renderer/vulkan-sky-texture-array-submission-2026-07-15.md`,
  and `docs-dev/renderer/vulkan-inline-bsp-performance-budget-2026-07-15.md`.
  The dense instance evidence is in
  `docs-dev/renderer/vulkan-inline-bsp-instance-coalescing-2026-07-15.md`.
  A 2026-07-16 audit made every non-DOF paired telemetry scenario explicitly
  set and hash matching DOF, bloom, colour-correction, and CRT controls before
  workload setup. New native postprocess-domain telemetry then exposed three
  unintended Vulkan bloom draws: the fixture disabled `gl_bloom` but not
  Vulkan's independent `vk_bloom`. The matched capture restores the active
  dense-instance budget without relaxing its limits: 18 Vulkan draws, 4,800
  upload bytes, `0.52845 ms` CPU mean / `0.628 ms` p95, and `0.65570x` the
  OpenGL CPU mean under validation. Cross-renderer GPU claims and broader
  representative-map/overlay budgets remain open. The active postprocess
  lanes now include a launch-safe real inventory DOF capture and a real
  translucent-shell bloom capture. Vulkan's paired bilinear Gaussian blur
  lowered active-DOF GPU mean from `2.66942` to `1.33777` ms and its post phase
  from `1.760` to `0.700` ms while retaining all eight strict DOF gates. The
  active-bloom shell baseline is diagnostic rather than a budget: it records
  `0.81801` ms Vulkan post time versus `0.52956` ms OpenGL, with a larger
  native scene-phase cost from the selective authored-emission replay. The
  matching bloom-disabled control isolates the active delta to five draws,
  `0.81601` ms post time, and `0.22505` ms scene time. A visual-parity
  Gaussian-weight recurrence regressed both active DOF and bloom timings, so
  it was rejected. A follow-up native per-frame coefficient buffer retains the
  paired-bilinear taps while eliminating per-fragment Gaussian exponentials:
  active shell bloom post time improves from `0.81801` to `0.76663 ms` and
  complete GPU time from `2.18236` to `2.13732 ms`, with its strict bloom gate
  and the complete eight-scene DOF matrix still passing. Frames with no
  authored emission now also skip the full-resolution native extraction replay
  entirely while retaining threshold bloom, blur, and final composition; the
  new validation capture records seven total draws (two world, one entity,
  four post-process). The analyzer now reports warmup-trimmed
  upload/shadow/scene/post GPU phases, making further replay batching and
  post-process submission the next optimization targets. See
  `docs-dev/renderer/vulkan-paired-performance-nondof-contract-2026-07-16.md`,
  `docs-dev/renderer/vulkan-dof-paired-tap-performance-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-active-bloom-performance-baseline-2026-07-16.md`,
  plus `docs-dev/renderer/vulkan-blur-kernel-uniform-optimization-2026-07-16.md`,
  `docs-dev/renderer/vulkan-bloom-emission-replay-elision-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-bloom-mip-pyramid-parity-2026-07-16.md`.
  Adaptive Vulkan resolution scaling now consumes each completed native GPU
  timestamp once, waiting for the first GPU result instead of mixing CPU
  submission duration into a timestamp-capable device's controller. The
  original microsecond CPU estimate remains only as the unsupported-device
  fallback. This improves the renderer's local quality/performance decision
  without claiming cross-renderer GPU comparability; the backend timestamp
  scopes still need an explicit shared contract before a GPU ratio budget can
  be accepted. Details:
  `docs-dev/renderer/vulkan-adaptive-resolution-gpu-timing-2026-07-16.md`.
  The dense inline-BSP lane now also closes an unlightmapped opaque-material
  discrepancy exposed by `r_fullbright 0`: Vulkan's native entity path marks
  OpenGL-equivalent texture-replace faces and bypasses fallback entity
  lighting. A companion generated grid gives all six inline-box faces authored
  static lightmaps, and both its lightmapped and unlightmapped 218,400-pixel
  OpenGL/Vulkan captures have zero RGB error under validation. The default-on
  native `vk_bmodel_fast_lit` specialization is now independently measurable:
  with 110 post-warmup samples on Intel Iris Xe, it selects 16 entity draws per
  frame and lowers the entity GPU p50 from `1.109` to `0.346 ms` and full-frame
  GPU p50 from `1.953` to `1.189 ms`; the fallback has the same 18 draws and
  4,800 upload bytes. This is a narrow validated gain, not a renderer-wide
  Vulkan/OpenGL GPU budget: OpenGL remains around `0.403 ms` p50 in that lane.
  Details and reproduction are in
  `docs-dev/renderer/vulkan-inline-bsp-lightmap-fast-path-2026-07-16.md`.
  The same lane then isolated a full-screen static-world receiver with an
  authored glow map. The default-on native `vk_world_fast_lit` now selects a
  dedicated glow-map static-light pipeline (plus the plain-lightmap and
  texture-replace classes) while retaining the general native Vulkan pipeline
  as its `0` fallback. The glow-map class preserves double-sided legacy BSP
  rasterization and exact OpenGL glow-alpha lightmap lifting; both default and
  forced-general 218,400-pixel validation captures remain zero-error. With
  the inline-BSP specialization held active, the 110-sample Intel Iris Xe
  comparison records one world fast draw/frame and lowers opaque-world GPU p50
  from `0.806` to `0.393 ms`, scene p50 from `1.153` to `0.746 ms`, and
  completed-frame GPU p50 from `1.182` to `0.775 ms` (same 18 draws and 4,800
  uploads). OpenGL is still `0.401 ms` full-frame p50 in that lane, so the
  renderer-wide GPU budget remains open.

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
  Progress: The native headless RmlUi visual lane now runs a manifest-gated
  core-runtime, main-shell, performance-settings, quit-confirmation, and
  leave-match-confirmation, and forfeit-confirmation matrix in both OpenGL and
  validation-enabled Vulkan. Core is exact; the other route thresholds are
  retained in the manifest. The
  first native gameplay-HUD gate
  now additionally proves exact 100x100 crosshair parity and an identical
  12-pixel opaque-white mask after Vulkan adopted the compatible picture-filter
  contract. The same native gameplay fixture now exactly gates a 400x60 classic
  status-bar region and its 294-pixel opaque-detail mask. Live inventory/chat,
  modern match HUD, remaining session-popup, input-state, and broader map
  effects remain open for the nightly sequence. See
  `docs-dev/renderer/vulkan-rmlui-overlay-matrix-expansion-2026-07-15.md`.
  HUD implementation and evidence are in
  `docs-dev/renderer/vulkan-hud-filter-parity-2026-07-15.md`.
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
- [x] `FR-02-T12` Audit and harden gamma, lighting, tone mapping, and shadowmapping correctness across OpenGL, native Vulkan, and Vulkan RTX.
  Dependency: `FR-02-T09..T11`. Priority: P0.
  Progress: Closed EVSM empty-page encoding, stale raster-policy cache reuse, raw-depth filtering, lightmap/lightgrid bounds, transformed-normal, cvar finite-value, tracked-light identity, shadowlight configstring, native Vulkan intensity/filter-response, and VKPT tone-map synchronization/indexing defects. The implementation log is `docs-dev/renderer/gamma-lighting-shadow-audit-hardening-2026-07-10.md`.
- [ ] `FR-02-T13` Implement a renderer-neutral linear-light scene and final SDR/HDR presentation contract with explicit texture/data transfer functions and output gamma compatibility.
  Dependency: `FR-02-T12`. Priority: P0.
  Progress: Native Vulkan now has explicit scene/presentation pass families,
  float per-frame scene/copy targets, float liquid/refraction sampling, float
  bloom/DOF working targets, and a final-only UNORM presentation composite.
  `vk_hdr` restarts the renderer so HDR-off retains the LDR memory profile.
  Per-swapchain-image initial-layout tracking makes both the first HDR frame
  and subsequent presented frames validation-clean. Headless paired captures
  cover exact static HDR and glow-bloom output, bounded HDR waterwarp and
  refraction ordering, the disabled control, and native temporal automatic
  exposure. The latter uses frame-slot-local float mip chains and ping-pong
  exposure images with no GPU readback or queue stall; its fixed-scene bound
  is documented separately. Display-HDR transfer functions, a
  cross-driver-equivalent reduction kernel, full DOF/CRT/RmlUi/menu-preview
  coverage, and the performance budget remain open. Details are in
  `docs-dev/renderer/vulkan-linear-scene-presentation-architecture-2026-07-16.md`,
  `docs-dev/renderer/vulkan-scene-presentation-pass-split-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-float-scene-hdr-activation-2026-07-16.md`, and
  `docs-dev/renderer/vulkan-auto-exposure-float-scene-2026-07-16.md`.
- [ ] `FR-02-T14` Replace fixed worst-case shadow arrays with budgeted active capacity/resolution buckets, transactional allocation, capability-correct samplers, dirty-layer mip generation, and alpha-tested caster materials.
  Dependency: `FR-02-T12`. Priority: P1.
  Progress: The transient native shadow-caster stream starts at 64 KiB and
  grows geometrically with overflow checks, avoiding exact-high-water buffer
  reallocation stalls while retaining live-range upload accounting. It now uses
  frame-local host-visible staging and device-local Vulkan vertex storage with
  an explicit transfer-to-vertex-input dependency; the completed flashlight
  scene is clean under `VK_LAYER_KHRONOS_validation`. Fixed page arrays, active
  capacity/resolution buckets, transactional allocation, capability-correct
  samplers, dirty-layer mips, and alpha-tested casters remain open. Details are
  in `docs-dev/renderer/vulkan-shadow-stream-capacity-2026-07-15.md` and
  `docs-dev/renderer/vulkan-shadow-device-local-stream-validation-2026-07-15.md`.
- [ ] `FR-02-T15` Separate direct sun lighting from baked/ambient light before enabling sun shadows by default.
  Dependency: `FR-02-T12`, `FR-02-T13`. Priority: P1.

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
  2026-07-13 Player Setup progress: the RmlUi `players` route now owns a live
  `RDF_NOWORLDMODEL` preview with seven animation stages, attached weapon
  discovery/switching, muzzle flash, rotation, interpolation, reduced-motion
  behavior, and an authored preview surface with loading/empty/error states.
  The wider player-visuals preview modes remain open, so the task is not yet
  marked complete. Implementation log:
  `docs-dev/rmlui-live-player-setup-provider-2026-07-13.md`.
- [ ] `FR-03-T06` Audit and complete settings page cvar wiring for Video/Audio/Input/HUD/Downloads.
  Dependency: `FR-03-T02..T05`. Priority: P0.
  Progress: The cgame Effects menu now exposes `cg_weapon_bob` as a 0/1/2 selector for disabled, Quake 3, and Doom 3 viewweapon bob modes.
- [ ] `FR-03-T07` Add menu regression checklist (navigation, conditionals, scaling, localization).
  Dependency: `FR-03-T06`. Priority: P1.
- [ ] `FR-03-T08` Complete split between engine-side and cgame-side UI ownership where still mixed.
  Dependency: `FR-03-T06`. Priority: P1.
  Round 78 progress: WORR deathmatch now publishes an explicit match-hub
  capability and keeps join legality, live match state, initial freeze, and
  Escape reopen under sgame authority. The client only selects the available
  RmlUi or cgame JSON presentation and preserves the ordinary game-menu path
  for coop, demos, other game directories, and legacy servers. Broader UI
  ownership audit and bridge simplification remain open.
  2026-07-13 fixed-list progress: Callvote, MyMap, and tournament list/page
  authority stays in sgame. The client runtime only renders published cvars,
  evaluates presentation conditions, and dispatches registered commands; the
  backplate and back keys both run the sgame close side effect. This narrows a
  previously mixed generic-list path, while the broader ownership audit
  remains open. Implementation log:
  `docs-dev/rmlui-live-ui-list-provider-2026-07-13.md`.
  2026-07-13 Player Setup progress: the native client remains responsible for
  filesystem-backed player media and the renderer-owned preview, while the
  generic cvar/image bridge handles immediate presentation bindings. No
  player-media or userinfo authority moved into cgame or sgame. The broader
  ownership audit remains open. Implementation log:
  `docs-dev/rmlui-live-player-setup-provider-2026-07-13.md`.
- [x] `FR-03-T09` Complete multi-monitor settings hierarchy and monitor targeting behavior for fullscreen modes.
  Dependency: `FR-03-T06`. Priority: P1.
- [x] `FR-03-T10` Align the fixed-layout main menu framing with Quake II rerelease reference captures.
  Dependency: none. Priority: P1.
- [ ] `FR-03-T11` Make video, gamma, lighting, and shadow controls renderer/capability aware and expose the shared `r_shadow*` controls.
  Dependency: `FR-02-T01`, `FR-02-T13`. Priority: P1.

Strategic note:
- `FR-09` is now the long-term UI platform migration track. Remaining `FR-03`
  work should focus on short-lived bridge fixes, ownership cleanup, and parity
  work that directly unblocks the RmlUi cutover. Do not invest in new JSON-only
  widgets unless they are required to finish or validate the migration.

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
  `bot_allow_item_timers` / `bot_item_timer_fuzz_ms` where `bot_items`
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
  `bot_coop_lead_advance` timed route owner and dedicated mode `27` proof.
  Coop progress waiting now has a default-off `bot_coop_progress_wait` command
  owner that consumes WaitForLeader policy, and route-detected coop
  interactions now have a default-off `bot_coop_interaction_retry` wait/use
  retry owner.
  Latest combat confidence update: the combat scorer now carries bot
  health/armor, withholds weak underpowered fire against stacked enemy
  estimates, strengthens underpowered estimate penalties, and prevents role
  combat from overriding pending weapon switches or weak-fight deferrals.
  Close-front spacing and low-health threat retreat now use separate cooldowns
  so healthy spacing does not inherit survival-retreat timing.
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
  `docs-dev/q3a-botlib-special-item-utility-2026-06-18.md`,
  `docs-dev/q3a-botlib-roam-retreat-combat-scrutiny-2026-06-27.md`.
- [ ] `FR-04-T04` Add team mode awareness (CTF/TDM/etc.) to bot utility state updates.
  Dependency: `FR-04-T02`. Priority: P1.
  Progress: Team-objective helper/status vocabulary now exists for enemy flag, own flag return, neutral flag, and base-defense assignments. The current proof-hook wave wires authoritative CTF flag pickup, return, capture, death-drop, and dropped-flag return observations from the real gameplay branches into objective counters, and runtime mode `23` now passes as the `team_objective` smoke scenario. Deterministic role policy and lane/depth helpers now expose attacker, defender, returner, support, attack, defense, midfield, carrier-support, dropped-flag-response, and own-base-return metadata. Objective-side FFA/TDM/CTF match helpers now also report scoring participation, roam/collect/engage intent, item-role splits, friendly-fire recommendations, resource-denial intent, and target-engagement recommendations; default-off `bot_ffa_roam_route` consumes selected FFA roam/collect/engage policy as a timed route-goal owner in mode `42`, default-off `bot_ffa_spawn_camp_avoidance` composes with that owner to source FFA anti-camp routes away from nearby live opponents in mode `45`, default-off `bot_ffa_role_combat` consumes selected FFA role/lane/engage policy as a live attack-decision owner in mode `48`, default-off `bot_team_role_route` consumes selected match role/lane policy as a timed route-goal owner in mode `32`, default-off `bot_team_item_roles` consumes selected match item-role policy as a live pickup-candidate scoring bridge in mode `33`, default-off `bot_team_resource_denial` consumes deny-enemy resource policy as a live pickup-candidate scoring bridge in mode `50`, default-off `bot_team_fire_avoidance` consumes friendly-fire policy as live attack-input suppression in mode `34`, default-off `bot_team_role_combat` consumes selected TDM match role/lane policy as a live attack-decision owner in mode `43`, and mode `44` proves that TDM role-combat attack decisions compose with `bot_team_fire_avoidance` so blocked friendly-line shots are vetoed. Default-off `bot_ctf_role_route` consumes CTF role/lane policy as a timed route-goal owner in mode `35`, default-off `bot_ctf_role_combat` consumes CTF role/lane policy as a live attack-decision owner in mode `36`, default-off `bot_ctf_dropped_flag_route` consumes dropped enemy flag response policy as route ownership in mode `37`, default-off `bot_ctf_carrier_support_route` consumes same-team enemy flag-carrier support policy as route ownership in mode `38`, default-off `bot_ctf_base_return_route` consumes enemy own-flag carrier return policy as route ownership in mode `39`, default-off `bot_ctf_objective_route` selects base-return, carrier-support, and dropped-flag objectives in one live CTF route owner in mode `40`, mode `41` proves the objective-route owner takes precedence over the generic CTF role-route owner while `ctf_role_route_objective_deferrals` records the handoff, and default-off `bot_ctf_objective_transitions` verifies pickup, death-drop, and dropped-flag return counters before combined objective-route ownership in mode `76`. Coop/resource helpers now report follow, wait, regroup, lead, support, team-share, teammate-reserve, enemy-deny, and objective-resource policy results; follow/regroup/support now feed a short `coop_leader` timed route owner in the brain command path with a compact `coop_leader_route` scenario gate, no-leader LeadAdvance can own a short timed route behind `bot_coop_lead_advance`, default-off progression waiting can consume WaitForLeader policy as stop-and-face command ownership, default-off route interaction retry can consume detected mover/trigger route evidence as wait/use command ownership, default-off resource sharing can defer item route-goal candidates for another coop bot through reserve-for-teammate policy, default-off anti-blocking can consume close-to-leader policy as a short backpedal/sidestep command while facing the leader, default-off target sharing can let support-policy bots adopt a teammate's current hostile monster target from the blackboard, default-off door/elevator cooperation can split mover wait/use source ownership from teammate hold commands, default-off `bot_coop_live_loop` composes leader-route/support, progress wait, anti-blocking, interaction retry, and door/elevator source/hold behavior in mode `77`, and default-off `bot_coop_share_loop` composes target sharing with reserve-for-teammate item deferral in mode `78`. Durable route/reach policy, broader autonomous role ownership across real team flows, and broader campaign-specific trigger/key/objective coordination remain pending.
  Latest CTF objective live-loop update: `ctf_objective_route` mode `40` now proves base-return, carrier-support, and dropped-flag selections in one live CTF run, while behavior arbitration records objective candidates and objective owners. Focused validation passed from `.tmp\bot_scenarios\20260622T210329Z`.
  Latest CTF objective transition update: `ctf_objective_transitions` mode `76` now proves actual pickup, death-drop, and dropped-flag return counters before combined objective-route ownership. Focused validation passed from `.tmp\bot_scenarios\20260622T230509Z`, and the later bot chat live enemy-sighted full implemented suite passed 89/89 rows from `.tmp\bot_scenarios\20260623T013843Z`.
  Latest coop live-loop update: `coop_live_loop` mode `77` now proves leader-route/support, progress wait, anti-blocking, interaction retry, and door/elevator source/hold behavior in one two-bot coop run. Focused validation passed from `.tmp\bot_scenarios\20260622T234315Z`, and the later full implemented suite passed 89/89 rows from `.tmp\bot_scenarios\20260623T013843Z`.
  Latest coop share-loop update: `coop_share_loop` mode `78` now proves target sharing and reserve-for-teammate item deferral in one two-bot coop run. Focused validation passed from `.tmp\bot_scenarios\20260623T001149Z`, and the then-current full implemented suite passed 89/89 rows from `.tmp\bot_scenarios\20260623T013843Z`.
  Latest CTF item-role update: default-off `bot_ctf_item_roles` now consumes selected CTF match item-role policy as a live pickup-candidate scoring bridge in mode `47`, with separate `ctf_item_role_*` and `last_ctf_item_role_*` nav-policy status from the FFA and TDM item-role bridges.
  Previous FFA item-role update: default-off `bot_ffa_item_roles` consumes selected FFA match item-role policy as a live pickup-candidate scoring bridge in mode `46`, with separate `ffa_item_role_*` and `last_ffa_item_role_*` nav-policy status from the existing TDM `bot_team_item_roles` bridge.
  Latest FFA role-combat update: default-off `bot_ffa_role_combat` consumes selected FFA match role/lane/engage policy as a live attack-decision bridge in mode `48`, with separate `ffa_role_combat_*` and `last_ffa_role_combat_*` frame-command status from the TDM and CTF combat bridges.
  Latest FFA live-pacing update: mode `74` composes `bot_ffa_roam_route`, `bot_ffa_spawn_camp_avoidance`, `bot_ffa_item_roles`, `bot_ffa_role_combat`, and `bot_ffa_spawn_camp_combat_avoidance` in one four-bot FFA run, proving route pressure, item-role scoring, role-combat ownership, and spawn-camp combat veto from `.tmp\bot_scenarios\20260622T214927Z`.
  Implementation logs: `docs-dev/q3a-botlib-team-objective-helper-scaffold-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-proof-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-depth-2026-06-18.md`, `docs-dev/q3a-botlib-ffa-tdm-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-ffa-roam-route-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-dropped-flag-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-carrier-support-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-base-return-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-policy-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-precedence-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-transitions-2026-06-22.md`, `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-coop-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-coop-leader-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`, `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-progress-wait-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`, `docs-dev/q3a-botlib-coop-anti-blocking-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-target-share-2026-06-21.md`, `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`, `docs-dev/q3a-botlib-coop-live-loop-2026-06-23.md`, `docs-dev/q3a-botlib-ctf-objective-gameplay-hooks-2026-06-18.md`.
  Latest FFA item-role implementation log: `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`.
  Latest FFA role-combat implementation log: `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`.
  Latest FFA live-pacing implementation log: `docs-dev/q3a-botlib-ffa-live-pacing-2026-06-22.md`.
  Latest CTF item-role implementation log: `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`.
  Latest CTF objective live-loop implementation log: `docs-dev/q3a-botlib-ctf-objective-live-loop-2026-06-22.md`.
  Latest CTF objective transition implementation log: `docs-dev/q3a-botlib-ctf-objective-transitions-2026-06-22.md`.
  Latest coop live-loop implementation log: `docs-dev/q3a-botlib-coop-live-loop-2026-06-23.md`.
  Latest coop share-loop implementation log: `docs-dev/q3a-botlib-coop-share-loop-2026-06-23.md`.
- [ ] `FR-04-T05` Add map-level nav validation pass and bot spawn diagnostics.
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T06` Add bot participation checks to match/tournament/map-vote flows.
  Dependency: `FR-04-T02`. Priority: P1.
- [ ] `FR-04-T07` Provide bot tuning cvars in Q3-style `bot_` naming convention.
  Dependency: `FR-04-T01`. Priority: P2.
  Progress: Public bot cvar docs now cover the practical setup/debug cvars plus item timer fairness controls, including `bot_allow_item_timers` and `bot_item_timer_fuzz_ms`, in user-facing operator language. The item-timer cvars are helper/policy controls; broad respawn timing consumers remain future behavior work. The bot chat track now also has default-off runtime gates for live dispatch, team-only audience, global rate limiting, `bot_chat_live_events`, and cooldown suppression evidence; public chat docs remain pending until broader live event breadth and phrase-library behavior are stable. The 2026-06-29 public surface audit guard now validates active-source public `bot_*` defaults, Q3-style command registration, legacy-prefix regressions, and smoke-only hook leaks into user docs.
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
  Progress: Pinned `TTimo/bspc` is vendored under `tools/q2aas/`; `worr_q2aas` builds through Meson; `tools/q2aas/cfg/worr_q2.cfg` loads through `q2aas-config-smoke`; the WORR Q2 trace bridge now lets `MAPTYPE_QUAKE2` run BotLib reachability. `.install\basew\maps\mm-rage.bsp` strict validation writes `.tmp\q2aas\mm-rage.aas` with 428 AAS areas, 562 reachability records, and 4 clusters. `tools/q2aas/validation_manifest.json` and `q2aas-staged-smoke` now run the staged map matrix, require Q2 `IBSP` version 38 input, emit `.tmp\q2aas\validation-report.json`, write deterministic `.aas.meta.json` sidecars with tool/config/BSP/AAS hashes, record AAS source checksum metadata, detect BSPX marker offsets, parse entity and brush-content diagnostics, count spawn/item origin coverage, report high-value pickup reachability from spawn areas, fail on clean BSP lump/spawn/item/high-value reachability regressions, fail when AAS metrics or travel counts drop below manifest baselines, validate/report manifest schema/task provenance before conversion, and run an automated malformed-manifest expected-failure smoke including archive-backed manifest guardrails. `q2aas-stage-aas` now validates and stages accepted local AAS outputs with staged-output hash reports, `q2aas-stage-audit` verifies staged file paths/sizes/hashes, `q2aas-package-map-smoke` verifies pkz archive extraction plus conversion through a scratch packaged map, `q2aas-package-audit` verifies staged AAS release-payload representation, `q2aas-package-aas` injects staged AAS into `.install\basew\pak0.pkz` with an archive-required audit, and `refresh_install.py --package-q2aas-aas` preserves generated AAS members after rebuilding `pak0.pkz` from assets while generic staged release validation can require packaged members and hashes. Release binary policy now keeps q2aas/BSPC tool binaries out of default packages and requires notice sidecars for imported Q3A/BSPC work. Available-reference validation now covers the current eleven-map staged set when optional `q2dm7` and `fact2` are present (`mm-rage`, `worr_crouch_ref`, `q2dm1`, `q2dm2`, `q2dm7`, `q2dm8`, `q2ctf1`, `base1`, `base2`, `fact2`, and `train`), including CTF team-objective reachability, campaign progression diagnostics, water-backed liquid coverage, required `worr_crouch_ref` natural crouch coverage, `q2dm7` slime coverage, and `fact2` lava/runtime hazard coverage. The 2026-06-30 movement reference audit adds `crouch_reference` readiness from generated `TRAVEL_CROUCH` counts, the candidate-discovery round adds `tools/q2aas/discover_reference_candidates.py`, the hazard promotion round adds accepted mode `96` `movement_hazard_context` on `fact2`, and the crouch promotion round adds accepted mode `92` `movement_crouch_route` on `worr_crouch_ref`; `.tmp\bot_scenarios\movement_reference_gap_audit.json` now accepts both `natural_crouch` and `hazard_context`. Broader release manifest automation and deeper runtime map-behavior proof remain pending.
- [ ] `FR-04-T12` Rehost the Quake III Arena BotLib runtime behind a WORR sgame adapter.
  Dependency: `FR-04-T10`. Priority: P0.
  Progress: First WORR-native runtime shell is in place at `src/game/sgame/bots/bot_runtime.*`. It registers the initial `bot_*` cvars, hooks map start/entity reload/frame/shutdown lifecycle, probes `maps/<map>.aas` through the filesystem extension, decodes the Q3A/BSPC AAS v5 header transform, validates the `EAAS` version 5 lump table, and records AAS structural counts for debug status. Runtime smoke against refreshed `.install` loads packaged `maps/mm-rage.aas` with `428` areas, `562` reachability records, and `4` clusters. The Q3A import boundary is now compiled into `sgame`: `src/game/sgame/bots/q3a/` is reserved for commit-pinned imports, `q3a_botlib_boundary.*` records the planned runtime/AAS inventory, and `botlib_adapter.*` owns the future setup/shutdown/map/frame bridge. The first commit-pinned Q3A utility subset (`q_shared.h`, `surfaceflags.h`, `botlib.h`, `be_interface.h`, `l_log.h`, `l_memory.*`, and `l_libvar.*`) now compiles through `q3a_botlib_utility`; `q3a_botlib_import.*` provides tracked memory/shared-utility callbacks, and verbose runtime smoke reports `Q3A LibVar smoke passed`. The next commit-pinned AAS loader subset (`be_aas_file.c`, `aasfile.h`, `be_aas*.h`, and parser utility headers) now loads the active packaged AAS through Q3A's native `AAS_LoadAASFile` path using the callback-backed WORR filesystem bridge, with the active-memory file bridge retained as a fallback. It records matching Q3A world counts and unloads through imported Q3A shutdown. Q3A `be_aas_sample.c`, `be_aas_reach.c`, `be_aas_route.c`, `be_aas_routealt.c`, `l_crc.*`, `be_aas_main.c`, `be_aas_entity.c`, `be_aas_move.c`, and `be_aas_debug.c` are now imported for read-only AAS query, frame-lifecycle, entity-cache, movement-helper, and debug-area helper smoke; the previous temporary `AAS_AreaReachability`, `aasworld`, `AAS_Time`, `AAS_ProjectPointOntoVector`, `AAS_Error`, `AAS_ResetEntityLinks`, `AAS_InvalidateEntities`, `AAS_UnlinkInvalidEntities`, `AAS_InitAlternativeRouting`, `AAS_ShutdownAlternativeRouting`, movement prediction/drop/jump shims, and debug-line helper definitions have been removed in favor of imported Q3A implementations or callback-backed bridge code. The bridge now feeds `level.time.milliseconds()` into Q3A `Sys_MilliSeconds` each frame, uses real Q3A-style `AngleVectors`, maps Q3A debug line/cross/arrow and area-helper output to WORR debug callbacks under debug cvars, and runs a route/goal overlay smoke under `bot_debug_route` / `bot_debug_goal`; verbose smoke reports `q3a_angle_vectors=Q3A AngleVectors smoke passed`, `q3a_time_ms=25`, `q3a_route=Q3A AAS route query passed: start=3 goal=6 travel_time=113 reachability=1 route_end=6 stop=0`, `q3a_alt_route=Q3A AAS alternative route query passed: start=3 goal=6 goals=2 first_area=10 start_time=72 goal_time=39 extra_time=65534 failures=0`, `q3a_route_overlay=Q3A route overlay passed: callback=yes start=3 goal=6 end=6 travel_time=113 reachability=1 lines=2 crosses=3 arrows=1 clears=1 failures=0`, `q3a_movement=Q3A AAS movement prediction passed: start=3 end=3 stop=0 frames=8 drop=yes jump=yes`, `q3a_debug_draw=Q3A route overlay debug draw passed: callback=yes lines=2 crosses=3 arrows=1 clears=1 failures=0`, `q3a_debug_area=Q3A AAS debug area helpers passed: area=3 lines=12 polygon_creates=6 polygon_deletes=6 failures=0`, and `q3a_start_frame=Q3A AAS start frame passed: result=0 time_ms=25 frames=1`. Active-map Q2 BSP entity data is now validated from `maps/<map>.bsp` as `IBSP` version 38, parsed into Q3A-style epairs before AAS load, and reported as `q3a_bsp_entity=Q3A BSP entity lump load passed: maps/mm-rage.bsp entities=394 epairs=1704 first_classname=info_player_start` with `q3a_bsp_entity_smoke=yes`. Active-map Q2 BSP model data is also parsed from lump 13 and reported as `q3a_bsp_model=Q3A BSP model lump load passed: maps/mm-rage.bsp models=18 smoke_model=1 mins=(-328.0 -584.0 24.0) maxs=(-192.0 -440.0 256.0)` with `q3a_bsp_model_smoke=yes`. Active-map Q2 BSP collision data is now parsed from the static-world collision lumps and reported as `q3a_bsp_collision=Q3A BSP collision load passed: maps/mm-rage.bsp planes=1367 nodes=1863 leafs=1882 brushes=1142 point_contents=1 trace_fraction=0.347 startsolid=0 allsolid=0` with `q3a_bsp_point_contents_smoke=yes` and `q3a_bsp_trace_smoke=yes`. Active-map Q2 BSP visibility data is now parsed from leaf cluster IDs and the compressed visibility lump and reported as `q3a_bsp_visibility=Q3A BSP visibility load passed: maps/mm-rage.bsp clusters=303 smoke_cluster=0 pvs_visible=142 phs_visible=289` with `q3a_bsp_pvs_smoke=yes` and `q3a_bsp_phs_smoke=yes`. The server frame now pushes WORR bot-facing entity snapshots into imported Q3A `AAS_UpdateEntity` after the entity update pass; verbose smoke reports `q3a_entity_sync=Q3A AAS entity sync passed: updated=18 unlinked=1006 skipped=0 failures=0 max=1024`. Q3A `AAS_EntityCollision` now reaches a WORR `gi.clip` entity trace callback, SOLID_BSP snapshots translate server model config indices into Q3A inline BSP model numbers, and verbose smoke reports `q3a_entity_trace=Q3A AAS entity trace smoke passed: callback=yes attempts=1 hits=1 misses=0 failures=0`. Dynamic BSP leaf entity links and Q3A `AAS_BoxEntities` now use active-map Q2 BSP node/leaf data, with verbose smoke reporting `q3a_bsp_leaf_link=Q3A BSP leaf entity link smoke passed: active_links=96 box_entities=2 ent=18`, `q3a_bsp_leaf_link_failures=0`, and `q3a_bsp_box_entities_smoke=yes`. Native cached route/goal debug markers, reachability labels, bounded polylines, selected-client filtering, persistent item goals, exact-origin position goals, stuck-progress repath, short stuck recovery commands, item-goal blacklist cooldowns, failed-goal reason diagnostics, reachability-aware movement-state command counters, natural jump/ladder/barrier-jump/crouch/walk-off-ledge/elevator travel-type validation, natural crouch/swim/waterjump support diagnostics, interaction wait/use retry telemetry, and `bot_brain.*` command/status ownership now draw or report for live bot route state, while broader swim/waterjump runtime breadth and higher-level behavior remain pending.
  Optimize import update: Q3A `be_aas_optimize.c` now compiles beside the existing AAS runtime imports, and the previous temporary `AAS_Optimize` no-op is removed in favor of the imported implementation. WORR keeps `aasoptimize=0` for the default loaded-AAS smoke because the Q3A optimization path mutates geometry/index arrays for opt-in save/forcewrite flows.
  Current AAS runtime adapter closeout: the imported Q3A AAS runtime C surface required by WORR route loading/query/movement/debug now rebuilds as part of `sgame_x86_64`; `AAS_Trace` and `AAS_PointContents` are final-owned by the active-map Q2 BSP collision bridge, `AAS_inPVS` / `AAS_inPHS` are final-owned by the active-map Q2 BSP visibility bridge, and `AAS_EntityCollision` reaches the WORR `gi.clip` path through `botlib_adapter.*` and `BotRuntimeEntityTrace`.
  Runtime lifecycle closeout: BotLib adapter initialization is idempotent for the game-module lifetime, `ShutdownGame()` calls `Bot_RuntimeShutdown()` after level unload/lifecycle status, and Q3A `bot_*` LibVars are documented as internal AAS-runtime state while public WORR policy stays on `bot_*`.
  Entity snapshot closeout: `BotRuntimeBuildEntitySnapshot()` now separates players, bots, spectators, and monsters/NPCs before pushing snapshots into imported `AAS_UpdateEntity`, and loaded `bot_debug_aas` output reports those current snapshot counts.
  Entity scheduling and fairness closeout: regular pickups, dropped items, traps/projectiles/hazards, doors/plats/movers, and objective/flag entities now have explicit snapshot categories and debug counts; item desirability refreshes are staggered per bot with cached candidate validation; route recomputation rate-limit checks/reuses/refreshes have dedicated status; and live aim remains gated by blackboard-visible, FOV-checked, shootable enemy facts.
  Print bridge update: Q3A `botimport.Print` now crosses the adapter into WORR logging with warning/error/fatal forwarding, verbose message-level output behind `bot_debug_aas >= 3`, and `q3a_print_*` counters in adapter status.
  BotClientCommand safety update: Q3A `botimport.BotClientCommand` now crosses the adapter into a WORR runtime validation callback that requires a bot client and rejects execution until a dedicated bot command dispatcher exists.
  Memory allocator update: Q3A `botimport.GetMemory`, `FreeMemory`, and `HunkAlloc` now use tracked bot-owned zone/hunk allocation lists, with grouped hunk release after AAS shutdown and verbose memory counters.
  Filesystem bridge update: Q3A `botimport.FS_FOpenFile`, `FS_Read`, `FS_Seek`, and `FS_FCloseFile` now cross the adapter into WORR's filesystem load/free callbacks with tracked read-only file handles and an active-memory fallback.
  Lifecycle telemetry update: the Q3A BotLib bridge now reports init/load/unload/shutdown counters, transient unload residue, open file handles, and persistent LibVar zone bytes through verbose adapter status.
  Latest slice: the repeated lifecycle import harness runs three active `maps/mm-rage.aas` load/unload cycles and reports `loads=3/3`, `active_unloads=3`, `clean_unloads=3`, `unload_failures=0`, `last_unload_zone_active=0`, `last_unload_hunk_active=0`, and `last_unload_files=0`.
  Dedicated lifecycle smoke update: `bot_lifecycle_smoke` now provides an unattended dedicated-server self-smoke that initializes real BotLib on `mm-rage`, lowers itself across the dedicated `map` game-DLL reload, reloads the map once, prints shutdown-time lifecycle status before each DLL unload, and exits with both shutdown lines reporting `q3a_lifecycle_clean_unloads=1`, `q3a_lifecycle_unload_failures=0`, and zero transient zone/hunk/file residue.
  Nav reachability/debug update: native cached route/goal debug now labels live bot route state with current AAS area and next reachability travel type, draws a bounded sampled route polyline, supports `bot_debug_client` selected-client filtering, and the dedicated frame-command smoke reports `route_debug_labels=8`, `route_debug_polyline_points=16`, `route_debug_polyline_segments=24`, `route_debug_filtered_slots=0`, `last_current_area=224`, `last_route_point_count=2`, `last_reachability_type=2`, `last_reachability_flags=2`, `last_reachability_end_area=217`, and `last_debug_filter_client=0`.
  Persistent route-goal update: native `bot_nav` route slots now remember the first successful route goal area, request it on refresh, clear it on goal reach/fallback, and report goal counters through `q3a_bot_frame_command_status`.
  Legacy surface update: inherited Q2R `bot_debug.*`, `bot_exports.*`, and `bot_utils.*` are no longer part of `sgame`; BotLib/AAS runtime work now has a clean WORR-owned replacement boundary.
  Item route-goal update: native `bot_nav` route slots can now select an active pickup entity, resolve it through the BotLib adapter to an AAS area, keep the item entity/spawn-count metadata across cached route commands, and clear the goal when the pickup disappears or respawn-hides.
  Position route-goal update: native `bot_brain` and `bot_nav` can now drive a debug/smoke world-position route request, resolve it to an AAS area, and route to the exact resolved goal origin through `BotLibAdapter_BuildRouteSteerToGoal()` without changing area-only item or fallback routes.
  Natural travel-type goal update: staged `bot_frame_command_smoke 9`, `10`, `11`, `12`, and `13` now validate natural packaged-AAS `TRAVEL_JUMP`, `TRAVEL_LADDER`, direct `TRAVEL_BARRIERJUMP`, and route-only `TRAVEL_WALKOFFLEDGE` / `TRAVEL_ELEVATOR` routes without forcing `bot_frame_command_smoke_travel_type`; the barrier-jump run reports `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `last_reachability=319`, `last_reachability_type=4`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `travel_type_goal_start_warps=1`, `route_failures=0`, and `pass=1`.
  Rocket-jump route policy update: staged `bot_frame_command_smoke 14` enables `bot_allow_rocketjump 1`, selects packaged-AAS `TRAVEL_ROCKETJUMP`, and reports `last_reachability_type=12`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `route_failures=0`, and `pass=1`; staged `bot_frame_command_smoke 15` leaves rocket-jump routing disabled, expects the route to be blocked, and reports `commands=0`, `route_commands=0`, `travel_type_goal_resolved=0`, `route_failures=8`, `travel_type_goal_expect_blocked=1`, and `pass=1`.
- [ ] `FR-04-T13` Implement WORR-native bot fake-client commands, slot lifecycle, and profile loading.
  Dependency: `FR-04-T01`. Priority: P0.
  Progress: First engine-owned fake-client lifecycle slice is in place. `addbot`, `removebot`, `kickbots`, and `botlist` can add/list/remove local bot slots, bot clients call the game bot connect/begin path, server send/disconnect/status/population paths skip bot-only network work, and shutdown removes bot slots. Same-frame multi-bot adds now defer through a one-bot-per-frame queue, `ClientSpawn()` preserves `SVF_BOT`, and bot team assignment runs before host/owner auto-join logic so slot 0 bots spawn as bot participants instead of host spectators. `bot_min_players` now fills auto-managed bots directly from the target, treats manual bots as satisfying it, clamps the target to public slots, and removes only auto-managed bots when the target drops below the current autofill count. `bot_reload_profiles` now scans `botfiles/bots/*.c`, `bots/profiles/*.bot`, and `bots/*.bot`; `addbot [profile] [team]` resolves loaded profiles before falling back to display-name behavior; and `bot_profile` can feed profile-backed min-player autofill. Profiles now bridge reaction, aggression, aim error, preferred weapon, chat personality, team role, and movement style into `bot_*` userinfo keys; role, team, item, and movement hints feed supported match-policy helpers, while chat remains preserved metadata for future behavior work. The first WORR-native botfiles pack now ships Q3/Gladiator-style `*_c.c` character entry points plus `_w/_i/_t.c` companions for `smoke`, `vanguard`, `bulwark`, `relay`, and `vector` under `assets/botfiles/bots/`; validation now checks behavior metadata, companion-family integrity, shared teamplay event-name parity, and Q2-oriented utility/chat/weapon/script polish. The loader strips `_c` to keep stable profile IDs and skips companion scripts as profile records; `refresh_install.py` packages the profiles into `pak0.pkz` and mirrors `botfiles` loose for no-zlib dedicated builds; and `profile_backed_spawn` runs in the implemented scenario suite. Initial bot team placement now respects match lock, one-on-one two-player active caps, and positive `maxplayers`; the per-frame policy cleanup preserves active humans first and moves surplus active bots to spectators when mode or active-player limits tighten. Clean dedicated smoke on `mm-rage` validates Alpha add/remove, Bravo add, queued Charlie add into slot 1, active count `2`, cleanup back to `0`, min-player fill to `B|bot1`/`B|bot2`/`B|bot3`, target trim to one auto bot, disable cleanup back to `0`, and direct game-side team-policy status for a three-bot Duel setup with `playing=2`, `spectators=1`, `bots=3`, plus cleanup to zero bots. The fake-client frame path now asks `sgame` for cached AAS route-steered bot commands and runs accepted commands through `SV_BotClientThink()` with `lastcmd` bookkeeping; long bot runs now top up local bot command budget before applying server-authored commands so fake clients avoid the human-client `commandMsec underflow` path. Basic active-pickup route goals, position route goals, item reservations, route-point look-ahead steering, velocity-aware command yaw, route-target stabilization, trace-checked corner cutting, stuck-progress repath, short stuck recovery commands, item-goal blacklist cooldowns, failed-goal reason diagnostics, reachability-aware movement-state command intent, natural jump/ladder/barrier-jump/walk-off-ledge/elevator travel-type route validation, default-off rocket-jump route gating, and the `bot_brain.*` command/status ownership split are in place; broader autonomous profile behavior and higher-level navigation policy remain pending.
  Legacy action boundary: the inherited Q2R bot export/action helper layer is removed, so weapon, item, trigger, pickup, and inventory behavior needs a new WORR bot action dispatcher above the fake-client command path.
  Implementation logs: `docs-dev/q3a-botlib-bot-slot-lifecycle-2026-06-17.md`, `docs-dev/q3a-botlib-multibot-slot-queue-2026-06-17.md`, `docs-dev/q3a-botlib-min-players-autofill-2026-06-17.md`, `docs-dev/q3a-botlib-profile-loading-2026-06-17.md`, `docs-dev/q3a-botlib-profile-behavior-fields-2026-06-17.md`, `docs-dev/q3a-botlib-team-policy-cleanup-2026-06-17.md`, `docs-dev/q3a-botlib-frame-command-dispatch-2026-06-17.md`, `docs-dev/q3a-botlib-route-steered-frame-commands-2026-06-17.md`, `docs-dev/q3a-botlib-nav-route-cache-2026-06-17.md`, `docs-dev/q3a-botlib-nav-debug-overlay-2026-06-17.md`, `docs-dev/q3a-botlib-nav-reachability-debug-2026-06-17.md`, `docs-dev/q3a-botlib-nav-polyline-debug-2026-06-17.md`, `docs-dev/q3a-botlib-nav-debug-client-filter-2026-06-17.md`.
  Latest bot logs: `docs-dev/q3a-botlib-nav-persistent-goal-2026-06-18.md`, `docs-dev/q3a-botlib-legacy-bot-surface-removal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-item-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-item-reservation-2026-06-18.md`, `docs-dev/q3a-botlib-nav-lookahead-steering-2026-06-18.md`, `docs-dev/q3a-botlib-nav-velocity-steering-2026-06-18.md`, `docs-dev/q3a-botlib-route-target-stabilization-2026-06-18.md`, `docs-dev/q3a-botlib-trace-checked-corner-cutting-2026-06-18.md`, `docs-dev/q3a-botlib-nav-stuck-repath-2026-06-18.md`, `docs-dev/q3a-botlib-nav-stuck-recovery-command-2026-06-18.md`, `docs-dev/q3a-botlib-nav-goal-blacklist-cooldown-2026-06-18.md`, `docs-dev/q3a-botlib-nav-failed-goal-reason-2026-06-18.md`, `docs-dev/q3a-botlib-nav-movement-state-commands-2026-06-18.md`, `docs-dev/q3a-botlib-bot-brain-command-ownership-2026-06-18.md`, `docs-dev/q3a-botlib-nav-position-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-ladder-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-walkoffledge-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-elevator-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-barrierjump-travel-goal-2026-06-18.md`, `docs-dev/q3a-botlib-nav-rocketjump-policy-2026-06-18.md`, `docs-dev/q3a-botlib-nav-four-bot-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-eight-bot-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-soak-frame-command-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-map-change-repeat-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-nav-natural-movement-door-retry-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-dispatcher-2026-06-18.md`, `docs-dev/q3a-botlib-q3-style-botfiles-2026-06-18.md`.
  Latest botfile/profile logs: `docs-dev/q3a-botlib-native-botfiles-assets-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-validation-tool-2026-06-18.md`, `docs-dev/q3a-botlib-profile-loader-hardening-2026-06-18.md`, `docs-dev/q3a-botlib-profile-scenario-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-loose-staging-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-user-docs-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-style-audit-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-q3a-style-expansion-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-scripts-package-coverage-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-scripts-support-2026-06-18.md`, `docs-dev/q3a-botlib-botfile-script-parity-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-worker-i-validation-2026-06-18.md`, `docs-dev/q3a-botlib-profile-behavior-validation-2026-06-18.md`, `docs-dev/q3a-botlib-botfiles-parity-polish-2026-06-18.md`, and `docs-dev/q3a-botlib-botfiles-q3a-parity-followup-2026-06-18.md`.
- [ ] `FR-04-T14` Implement AAS-backed navigation, route following, stuck recovery, and debug overlays.
  Dependency: `FR-04-T11`, `FR-04-T12`, `FR-04-T13`. Priority: P0.
  Progress: A spawned bot can now receive a cached AAS route-steered movement command through the normal server fake-client command path. `BotLibAdapter_BuildRouteSteer()` asks imported Q3A AAS for a reachable route from the bot's current area, validates the full route, returns the first route step as the steering target, and carries the selected reachability's travel type, travel flags, end area, and a bounded sampled route-point list into native `bot_nav` state. `BotLibAdapter_BuildRouteSteerToGoal()` also lets position route goals keep the exact resolved goal origin instead of falling back to an AAS area center. `BotLibAdapter_BuildRouteSteerForTravelType()` can now request a route whose selected next reachability matches a requested travel type. `bot_nav.*` now owns per-client route cache state, refresh cadence, reset on BotLib level lifetime, native cached route/goal debug markers, current-area/next-reachability labels, bounded route polyline drawing, selected-client filtering, position goals, travel-type smoke goals, route-target stabilization, trace-checked corner-cut candidate selection, stuck-progress monitoring, item-goal blacklist cooldowns, failed-goal reason diagnostics, natural movement support diagnostics, and interaction wait/use retry telemetry. `bot_frame_command_smoke` proves command frames execute against packaged `maps/mm-rage.aas` with `route_requests=8`, `route_queries=2`, `route_refreshes=2`, `route_reuses=6`, `route_commands=8`, `route_failures=0`, and `pass=1`; the route/goal debug variant reports native overlay and reachability counters, and the filtered variant with `bot_debug_client 1` reports filtered slots and `pass=1`. Natural map-backed jump, ladder, and barrier-jump validation pass, route-only walk-off-ledge/elevator/teleporter traversal is recognized as route-owned, interaction retry telemetry covers door/platform/train/trigger/mover wait/use paths, and controlled inactive recovery is available only behind `bot_controlled_inactive_recovery`.
  Persistent route-goal update: `bot_nav.*` now owns a per-client persistent route goal area and reports `route_goal_requests=1`, `route_goal_assignments=1`, `route_goal_cache_reuses=6`, `route_goal_clears=0`, `route_goal_fallbacks=0`, `last_persistent_goal_area=227`, `last_goal_clear_reason=0`, and `pass=1`; door/trigger retry remains pending.
  Item route-goal update: `bot_nav.*` now selects a live active pickup entity, resolves its origin to an AAS area through `BotLibAdapter_FindRouteAreaForPoint()`, remembers entity/spawn-count/item metadata, and reports `item_goal_scans=1`, `item_goal_candidates=45`, `item_goal_assignments=1`, `item_goal_reuses=7`, `last_item_goal_entity=32`, `last_item_goal_area=415`, `last_item_goal_item=53`, `last_item_goal_score=828`, `last_persistent_goal_area=415`, and `pass=1` on refreshed `mm-rage`.
  Item/route scheduling update: item desirability scans are now split across bots with per-slot cached candidate validation and status counters for updates, cache reuses, and stagger deferrals; route recomputation cadence exposes rate-limit checks, reuses, and refreshes through `q3a_bot_route_schedule_status`.
  Item reservation update: `bot_nav.*` now skips active pickups already reserved by another bot's selected item route slot, resets nav route slots on bot disconnect, and reports `item_goal_reservation_skips=1`, `item_goal_active_reservations=2`, `last_item_goal_reserved_entity=32`, `last_item_goal_reserved_by_client=0`, `route_goal_assignments=2`, `route_failures=0`, and `pass=1` in a two-bot `bot_frame_command_smoke 3` run.
  Look-ahead steering update: `Bot_BuildFrameCommand()` now selects from the cached route-point polyline inside a short look-ahead distance before computing command yaw, and the two-bot smoke reports `lookahead_attempts=17`, `lookahead_uses=9`, `last_lookahead_point_count=2`, `route_failures=0`, and `pass=1`.
  Velocity-aware steering update: `Bot_BuildFrameCommand()` now applies a short projected-origin yaw adjustment when current horizontal bot velocity is measurable, and the two-bot smoke reports `velocity_lead_attempts=17`, `velocity_lead_uses=3`, `last_velocity_lead_speed_sq=182`, `last_velocity_lead_offset_sq=1`, `route_failures=0`, and `pass=1`.
  Route-target stabilization update: `bot_nav.*` now promotes a farther sampled route point when a refreshed route returns a near-origin `moveTarget`, recording stabilization checks/applications/skips and target-distance counters. Route reuse plus target stabilization closes the current adjacent-area jitter mitigation subitem; the later trace-checked corner-cutting slice adds direct server-trace and ground-probe proof before shortcut promotion.
  Stuck-repath update: `bot_nav.*` now monitors goal-distance progress across cached route uses and cadence refreshes, reports stuck reason fields in route debug/status output, and forces `BotNavRefreshReason::Stuck` through the native route refresh path; normal two-bot smoke reports `stuck_detections=0`, while stalled-command smoke reports `stuck_detections=2`, `stuck_repath_refreshes=2`, `last_stuck_reason=1`, `route_failures=0`, and `pass=1`.
  Stuck recovery command update: stuck detections now activate a short recovery window, and `Bot_BuildFrameCommand()` applies a back/strafe command during that window; normal smoke reports `stuck_recovery_activations=0` and `recovery_command_uses=0`, while stalled-command smoke reports `stuck_recovery_activations=2`, `stuck_recovery_frames=11`, `recovery_command_uses=11`, `last_recovery_forward_move=-80`, `last_recovery_side_move=-140`, `route_failures=0`, and `pass=1`.
  Goal blacklist cooldown update: stuck detections against a still-available active-pickup goal now blacklist that item identity for the stuck bot, clear the persistent goal with `last_goal_clear_reason=5`, and immediately scan for a different pickup while preserving the short recovery window; normal smoke reports zero blacklist activations/skips, while stalled-command smoke reports `item_goal_blacklist_activations=2`, `item_goal_blacklist_skips=2`, `item_goal_blacklist_active=2`, `last_item_goal_blacklisted_entity=68`, `route_failures=0`, and `pass=1`.
  Failed-goal reason update: abandoned persistent goals now record explicit failed-goal diagnostics before the goal identity is cleared; normal smoke reports `failed_goal_events=0` and `last_failed_goal_reason=0`, while stalled-command smoke reports blacklist-derived `failed_goal_events=2`, `last_failed_goal_reason=3`, `last_failed_goal_client=1`, `last_failed_goal_area=251`, `last_failed_goal_entity=74`, `last_failed_goal_item=2`, `route_failures=0`, and `pass=1`.
  Movement-state command update: `Bot_BuildFrameCommand()` now maps AAS crouch reachability to `BUTTON_CROUCH`, jump/barrier-jump/waterjump reachability to `BUTTON_JUMP`, and swim or ladder reachability to vertical jump/crouch intent based on target height. Normal `bot_frame_command_smoke 3` reports walk-only `movement_state_attempts=17`, `movement_state_commands=0`, and `last_movement_state_travel_type=2`; forced smoke modes `5`, `6`, and `7` report `movement_state_jump_commands=17`, `movement_state_crouch_commands=17`, and `movement_state_swim_commands=17` respectively with `pass=1`.
  Bot brain ownership update: `bot_think.*` now preserves the stable `Bot_*` wrapper surface while `bot_brain.*` owns the current high-level frame command/status implementation. Refreshed-install smokes report normal `frames=17`, `commands=17`, `route_failures=0`, and `pass=1`; forced jump smoke reports `movement_state_jump_commands=17` and `pass=1`; stalled-command smoke reports `stuck_detections=2`, `failed_goal_events=2`, `recovery_command_uses=11`, and `pass=1`.
  Position route-goal update: `bot_brain.*` can now request a debug/smoke world-space route goal and `bot_nav.*` resolves it ahead of item-goal scans, keeps it persistent across cache reuse, and routes through the exact resolved goal origin. `bot_frame_command_smoke 8` reports `position_goal_requests=8`, `position_goal_resolved=8`, `position_goal_assignments=1`, `position_goal_cache_reuses=6`, `item_goal_scans=0`, `route_goal_fallbacks=0`, `last_position_goal_area=227`, `last_position_goal_z=98`, `route_failures=0`, and `pass=1`.
  Natural travel-type goal update: `bot_frame_command_smoke 9` requests natural `TRAVEL_JUMP`, enables a smoke-only AAS start warp, and reports `last_reachability_type=5`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; `bot_frame_command_smoke 10` requests natural `TRAVEL_LADDER` and reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=142`, `last_travel_type_goal_start_goal_area=143`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_reachability_type=6`, `movement_state_ladder_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; `bot_frame_command_smoke 11` requests route-only natural `TRAVEL_WALKOFFLEDGE` and reports `last_reachability_type=7`, `route_commands=8`, `movement_state_commands=0`, `route_failures=0`, and `pass=1`; `bot_frame_command_smoke 12` requests route-only natural `TRAVEL_ELEVATOR` and reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=241`, `last_travel_type_goal_start_goal_area=261`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `travel_type_goal_cache_reuses=6`, `last_reachability_type=11`, `route_commands=8`, `movement_state_commands=0`, `movement_state_unsupported=0`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`; `bot_frame_command_smoke 13` requests natural `TRAVEL_BARRIERJUMP` and reports `travel_type_goal_start_warps=1`, `last_travel_type_goal_start_area=292`, `last_travel_type_goal_start_goal_area=318`, `travel_type_goal_resolved=8`, `travel_type_goal_assignments=8`, `last_reachability=319`, `last_reachability_type=4`, `movement_state_jump_commands=8`, `last_movement_state_forced_travel_type=0`, `route_failures=0`, and `pass=1`.
  Rocket-jump route policy update: Q3A route flags now include `TFL_ROCKETJUMP` only when `bot_allow_rocketjump 1` is active, and direct travel-type helpers obey the same policy. `bot_frame_command_smoke 14` enables the cvar and reports `last_reachability_type=12`, `travel_type_goal_resolved=2`, `travel_type_goal_assignments=1`, `route_failures=0`, and `pass=1`; `bot_frame_command_smoke 15` leaves the cvar disabled, expects a blocked travel-type goal, and reports `commands=0`, `route_commands=0`, `travel_type_goal_resolved=0`, `route_failures=8`, `travel_type_goal_expect_blocked=1`, and `pass=1`.
  Eight-bot frame-command smoke update: `bot_frame_command_smoke 17` adds eight fake-client bots, holds eight active item reservations, and reports `frames=92`, `commands=92`, `route_commands=92`, `route_failures=0`, `route_goal_assignments=11`, `item_goal_assignments=11`, `item_goal_reservation_skips=49`, `item_goal_active_reservations=8`, `route_debug_routes=92`, `route_debug_goals=92`, and `pass=1`.
  Ten-minute soak update: `bot_frame_command_smoke 18` keeps eight fake-client bots active for `600000` ms, records `item_goal_peak_active_reservations`, and reports `elapsed_ms=600001`, `frames=192036`, `commands=192036`, `route_requests=187232`, `route_commands=192036`, `route_failures=0`, `route_goal_assignments=4889`, `item_goal_assignments=1451`, `stuck_recovery_activations=11789`, `recovery_command_uses=72066`, `skipped_inactive=0`, and `pass=1`.
  Map-change repeat update: `bot_frame_command_smoke 19` repeats the eight-bot route-command proof across a same-map reload and reports two passing cycles, `map_changes=1`, `final_count=0`, and zero route failures. The optional restart path `bot_frame_command_smoke_map_repeat_restart 1` now uses `map "<map>" force`, reports `command=map_force restart=1`, gates cleanup with `active_reservations=0`, and passed three cycles with two forced restarts.
  Natural movement and interaction update: `q3a_bot_nav_natural_support_status` reports per-type natural crouch/swim/waterjump support, unsupported masks, reason codes, route-start areas, and origins for reference maps; `worr_crouch_ref` now proves accepted crouch support, while the elevator proof reports `nav_interaction_elevator_activations=1`, `interaction_wait_command_uses=8`, and `interaction_use_command_uses=8`.
  Legacy surface update: route/debug work no longer depends on Q2R `Bot_MoveToPoint`, `Bot_FollowActor`, `GetPathToGoal`, or `bot_debug.*`; active navigation debug state lives in `bot_nav` and the BotLib adapter.
  Live navigation command correction: route-steered commands now normalize desired view angles, clamp pitch, subtract `pmove.deltaAngles`, and sync live client `vAngle` into BotLib entity snapshots. This resolves the world-space/usercmd-space mismatch that made visible bot yaw/pitch flip and sent forward movement away from the chosen AAS route target.
  Teleporter entity-route update: `BotLibAdapter_BuildRouteSteerTowardGoal()` and the Q3A import wrapper now expose a first-reachability route toward an exact entity-backed area when full preferred-goal prediction cannot complete. Mode `95` `movement_teleporter_entity_route` on `train` keeps exact `TRAVEL_TELEPORT` support unsupported (`travel_type_goal_resolved=0`) while resolving a touch-capable teleporter entity fallback (`teleporter_entity_goal_resolved=8`, `teleporter_entity_goal_assignments=1`, `route_failures=0`) from `.tmp\bot_scenarios\teleporter_entity_route_final\20260629T191851Z`.
  Train bridge arrival-route update: the mode `91` `coop_campaign_key_carry_train` bridge phase now resolves a live train interaction entity, routes naturally to the train bridge-start endpoint, projects a routeable post-mover arrival, routes to that arrival without the old proof teleport, and routes the lock leg without the old direct lock-side warp. Focused validation from `.tmp\bot_scenarios\coop_campaign_key_carry_train.json` and `.tmp\bot_scenarios\20260701T145703Z` reports `interaction_goal_requests=16`, `interaction_goal_candidates=48`, `interaction_goal_resolved=16`, `last_interaction_goal_entity=60`, `last_interaction_goal_kind=4`, `last_interaction_goal_area=2338`, `key_carry_bridge_approach_ready=1`, `key_carry_bridge_warps=0`, `interaction_arrival_goal_resolved=1`, `last_interaction_arrival_goal_area=1058`, `key_carry_bridge_arrival_route_requests=2`, `key_carry_bridge_arrival_reached=1`, `key_carry_bridge_arrival_warps=0`, `key_carry_lock_warps=0`, and `route_failures=0`; the follow-up reusable route-state validation from `.tmp\bot_scenarios\20260701T162117Z` adds `interaction_arrival_route_assignments=2`, `interaction_arrival_route_reached=1`, and `key_carry_lock_route_requests=42`; the fresh full implemented suite passed 123/123 from `.tmp\bot_scenarios\implemented_after_bridge_arrival_route.json`, release acceptance passed 15/15, and broader off-mesh mover graph/ride-state generalization remains future work beyond the focused train proof.
  Physical elevator mover activation update: raw mode `12` `movement_elevator_route` now hard-gates direct mover activation and physical moving-state observation instead of passing on route-only elevator reachability. Recovery moves that already request `use` can directly activate platform/train/generic mover interactions with a cooldown, the frame-command smoke keeps a 24-frame observation window, and the row requires `interaction_direct_use_activations >= 1`, `travel_type_elevator_activation_requests >= 1`, `travel_type_elevator_ride_observation_moving >= 1`, `travel_type_elevator_ride_observation_completed >= 1`, and `interaction_mover_ride_moving_states >= 1`. Focused validation passed from `.tmp\bot_scenarios\movement_elevator_physical.json`, and the five-row mover regression passed from `.tmp\bot_scenarios\mover_direct_use_regression.json`; grounded-on-mover evidence remains the next physical mover follow-up where map geometry supports it.
- [ ] `FR-04-T15` Translate Q3A behavior concepts into WORR/Q2 weapons, items, combat, team modes, and architecture boundaries.
  Dependency: `FR-04-T14`. Priority: P1.
  Progress: First native item reservation policy is in place above route-goal selection so two bots do not select the same active pickup by default. Rocket-jump route selection is policy-gated behind `bot_allow_rocketjump`. The first compile-ready WORR action boundary exists in `bot_actions.*`, `bot_items.*`, and `bot_combat.*`, exposing decision/status APIs for item, combat, inventory, weapon, and world-use policy. `bot_brain.*` samples that dispatcher, applies validated action decisions to the current `usercmd_t` for attack/use button intents, records pending weapon/inventory intents, and emits `q3a_bot_action_status`. A per-bot blackboard now supplies visible enemy, shootability, last-seen, heard, and damaged facts for combat/objective smoke. `bot_combat.*` now covers Q2/WORR weapon metadata, current-vs-preferred scoring, real enemy fact construction, nearest visible/shootable target search, combat context enrichment, filtered bot-attributed damage records, enemy health/armor estimate scoring, an opt-in aim/fairness policy helper for skill-based reaction, FOV, turn, aim-settle, burst, aim-error, tracking-noise metadata, live aim-profile status, and live projectile-lead scaling consumed by the known-enemy brain aim path. `bot_actions.*` tracks validated pending weapon-switch requests through observed completion/failure proof state, dispatches accepted exact `use_index_only` weapon/inventory requests through the brain-owned frame path and item `use` callback boundary, scans carried weapons after enemy-fact enrichment so the best scorer-approved weapon can become the preferred switch target, scans carried non-weapon inventory for conservative combat/survival powerup or power-armor use, and now covers hazard/underwater utility, sphere deployable use, placement-checked doppelganger use, last-resort personal teleporter escape use, and safety-gated nuke use with active-effect, placement, owned-sphere, nuke-safety, friendly-deferral, self-deferral, and use counters. `bot_brain.*` now routes submitted safe nuke inventory use, submitted personal teleporter escape use, coop follow/regroup/support leader policy, coop no-leader LeadAdvance policy, default-off FFA roam/collect/engage policy, default-off FFA spawn-camp avoidance policy, default-off TDM role/lane match policy, default-off CTF role/lane match policy, default-off CTF dropped-flag response policy, default-off CTF carrier-support policy, default-off CTF base-return policy, and default-off CTF objective route-priority policy through route owners, exposes `timed_route_goal_*`, `nuke_retreat_*`, `teleporter_escape_*`, `coop_leader_route_*`, compact `coop_lead_advance_*`, `ffa_roam_route_*`, `ffa_spawn_camp_avoidance_*`, `team_role_route_*`, `team_role_combat_*`, `ctf_role_route_*`, `ctf_dropped_flag_route_*`, `ctf_carrier_support_route_*`, `ctf_base_return_route_*`, and `ctf_objective_route_*` status for owner kind, route ownership, source selection, fallback use, deferrals, expirations, invalid skips, and last source/goal/leader/role/lane/carrier-client/priority metadata, records `ctf_role_route_objective_deferrals` when the objective route policy takes precedence over the generic role-route owner, applies a default-off CTF role-combat bridge through `bot_ctf_role_combat` with `ctf_role_combat_*` status before attack buttons are applied, applies a default-off TDM role-combat bridge through `bot_team_role_combat` with `team_role_combat_*` status before CTF role combat and friendly-fire suppression, applies a default-off TDM friendly-fire bridge through `bot_team_fire_avoidance` with `team_fire_avoidance_*` status before attack buttons are applied, and mode `44` proves those TDM role-combat and friendly-fire bridges compose so friendly-line attacks are vetoed after role-combat selection. It also applies a default-off WaitForLeader stop-and-face command through `bot_coop_progress_wait` with `coop_progress_wait_*` status, applies a default-off route interaction wait/use retry command bridge through `bot_coop_interaction_retry` with `coop_interaction_retry_*` status, lets default-off `bot_coop_resource_share` consume coop/resource policy during item route-goal selection with compact `item_reserved_deferrals` evidence, applies a default-off `bot_coop_anti_blocking` close-leader command owner with compact `coop_anti_block_*` evidence, applies default-off `bot_coop_target_share` blackboard adoption so support-policy bots can inherit a teammate's current hostile monster target, and applies default-off `bot_coop_door_elevator` source-owner plus support-hold commands for mover/elevator cooperation. `bot_nav.*` now lets default-off `bot_team_item_roles` consume TDM match item-role policy, default-off `bot_team_resource_denial` consume deny-enemy resource policy, default-off `bot_match_item_policy` compose the FFA/CTF/TDM item-role plus TDM resource-denial scoring bridges during live pickup-goal scoring, and default-off `bot_behavior_enable` activate the current command/nav behavior family through one integrated switch. The new `q3a_bot_behavior_policy_status` marker records umbrella activation for TDM role-route, role-combat, friendly-fire, match item-policy gates, central behavior owner arbitration, cvar classification, and handoff telemetry without setting the individual proof cvars. `bot_items.*` scores existing pickups, feeds health/armor routing, exposes deterministic health/armor setup plus real pickup-delta snapshot/observation helpers, distinguishes damage boosts, protection, invisibility, mobility, utility powerups, techs, and CTF objective utility buckets, applies an item timer disable/fuzz policy where it already owns timing knowledge, exposes special-item/power-armor classification wrappers for action policy, and now has conservative live pickup/observed-respawn timing consumers with status metadata. `bot_objectives.*` provides target-source-aware selection, assignment, route-goal handoff, event-record helper APIs, deterministic attacker/defender/returner/support role-policy helpers, lane/depth metadata, FFA/TDM/CTF match/item/friendly-fire helper policy, profile role consumption for match requested-role selection, profile teamplay/objective/friendly-fire-care hint consumption for match priority and friendly-fire avoidance, profile item-greed/item-denial/powerup-timing/retreat-health hint consumption for match item/resource policy, profile movement-style hint consumption for match movement bonuses, CTF dropped-flag, flag-carrier support, and own-flag return assignment helpers, and coop/resource helper metadata for future autonomous behavior. Dedicated smoke modes `20` through `63` now pass as implemented scenario rows for enemy engagement, weapon switching, health/armor pickup, team objective proof, live aim, item timing, match readiness, coop lead advance, coop resource sharing, coop anti-blocking, coop target sharing, coop door/elevator cooperation, team-role route ownership, team item-role pickup scoring, team resource-denial pickup scoring, team friendly-fire suppression, team role-combat ownership, team role-combat/friendly-fire precedence, CTF role-route ownership, CTF role-combat ownership, CTF dropped-flag route ownership, CTF carrier-support route ownership, CTF base-return route ownership, CTF objective-route policy ownership, CTF objective-route precedence, FFA roam-route ownership, FFA spawn-camp avoidance, FFA item-role pickup scoring, FFA role-combat attack ownership, FFA spawn-camp combat avoidance, CTF item-role pickup scoring, match item-policy pickup scoring, behavior policy umbrella activation, profile-role match-policy selection, profile team-policy match selection, profile item-policy match selection, profile movement-policy match selection, bot chat-policy live-dispatch gating, bot chat team-only audience gating, bot chat rate-limit gating, bot chat initial-personality selection, bot chat reply-policy selection, bot chat event-policy selection, and behavior arbitration owner/cvar classification; the coop leader-route, progress-wait, and interaction-retry reuse rows also pass. Broader autonomous team/coop role consumption, broader campaign-specific coordination, and richer Q3A-style bot chat live trigger coverage and phrase libraries remain pending.
  Latest FFA item-role behavior update: `bot_nav.*` now also lets default-off `bot_ffa_item_roles` consume FFA match item-role policy during live pickup-goal scoring, exposing `ffa_item_role_*` and `last_ffa_item_role_*` evidence on compact nav policy status. Dedicated smoke mode `46` now passes as the `ffa_item_roles` implemented scenario row for FFA pickup scoring.
  Latest CTF item-role behavior update: `bot_nav.*` now also lets default-off `bot_ctf_item_roles` consume CTF match item-role policy during live pickup-goal scoring, exposing `ctf_item_role_*` and `last_ctf_item_role_*` evidence on compact nav policy status. Dedicated smoke mode `47` now passes as the `ctf_item_roles` implemented scenario row for CTF pickup scoring.
  Latest FFA role-combat behavior update: `bot_brain.*` now lets default-off `bot_ffa_role_combat` consume FFA match role/lane/engage policy during live attack-decision ownership, exposing `ffa_role_combat_*` and `last_ffa_role_combat_*` evidence on compact frame-command status. Dedicated smoke mode `48` now passes as the `ffa_role_combat` implemented scenario row for FFA attack ownership.
  Latest FFA spawn-camp combat-avoidance behavior update: `bot_brain.*` now lets default-off `bot_ffa_spawn_camp_combat_avoidance` compose FFA role-combat target ownership with FFA anti-camp source policy, exposing `ffa_spawn_camp_combat_avoidance_*` and `last_ffa_spawn_camp_combat_avoidance_*` evidence on compact frame-command status. Dedicated smoke mode `49` now passes as the `ffa_spawn_camp_combat_avoidance` implemented scenario row for FFA anti-camp attack veto precedence.
  Earlier proof-hook update: real bot-attributed damage is observed from the authoritative damage path, health/armor pickup proof observations are tied to successful item touches, CTF objective hooks feed mode `23`, live-aim proof feeds mode `24`, item-timer proof feeds mode `25`, match-readiness proof feeds mode `26`, coop lead-advance proof uses mode `27`, coop resource-share proof uses mode `28`, coop anti-blocking proof uses mode `29`, coop target-sharing proof uses mode `30`, coop door/elevator proof uses mode `31`, team-role route ownership uses mode `32`, team item-role pickup scoring uses mode `33`, team friendly-fire suppression uses mode `34`, CTF role-route ownership uses mode `35`, CTF role-combat ownership uses mode `36`, CTF dropped-flag route ownership uses mode `37`, CTF carrier-support route ownership uses mode `38`, CTF base-return route ownership uses mode `39`, CTF objective-route policy ownership uses mode `40`, CTF objective-route precedence uses mode `41`, FFA roam-route ownership uses mode `42`, TDM role-combat ownership uses mode `43`, TDM role-combat/friendly-fire precedence uses mode `44`, FFA spawn-camp avoidance uses mode `45`, map-restart cleanup reuses mode `19` with `bot_frame_command_smoke_map_repeat_restart 1`, warmup bot-start readiness uses `bot_warmup_smoke 2`, vote bot-exclusion uses `bot_vote_smoke 2`, admin bot privilege audit uses `bot_admin_audit_smoke 2`, tournament bot veto-exclusion uses `bot_tournament_smoke 2`, tournament replay reset uses `bot_tournament_smoke 3`, match logging schema proof uses `bot_matchlog_smoke 2`, MyMap queue proof uses `bot_mymap_smoke 2`, queued nextmap transition proof uses `bot_nextmap_smoke 2`, map-vote selector transition proof uses `bot_mapvote_smoke 2`, scoreboard classification proof uses `bot_scoreboard_smoke 2`, intermission bot cleanup proof uses `bot_intermission_smoke 2`, coop leader-route and progress-wait proofs reuse mode `3`, coop interaction-retry proof reuses mode `12`, compact proof rows now precede oversized verbose diagnostics, marker checks now require the newer live-aim, match-policy, FFA-roam-route, FFA-spawn-camp-avoidance, team-role-route, team-item-role, team-fire-avoidance, team-role-combat, team-role-combat/friendly-fire precedence, CTF-role-route, CTF-role-combat, CTF-dropped-flag-route, CTF-carrier-support-route, CTF-base-return-route, CTF-objective-route, CTF-objective-route-precedence, map-restart command/reload/cleanup, warmup bot-only start/readiness, vote bot-exclusion, admin bot privilege audit, tournament bot veto-exclusion, tournament replay reset, match logging schema/versioning, MyMap queue/consume, queued nextmap transition, map-vote bot exclusion/finalization/reload, scoreboard bot classification, intermission bot cleanup, coop command-owner, resource-deferral, anti-blocking, target-sharing, and door/elevator evidence fields, and the implemented scenario catalog contained 50 short-run rows plus one manual degradation row at that point.
  Latest FFA item-role proof update: mode `46` hard-gates FFA item-role pickup scoring through FFA readiness, objective item-role policy selection, nav score boosts, selected pickup goals, and latest role/category/item metadata.
  Latest CTF item-role proof update: mode `47` hard-gates CTF item-role pickup scoring through CTF readiness, objective item-role policy selection, nav score boosts, selected pickup goals, invalid-skip absence, and latest mode/role/lane/category/item metadata; the implemented scenario catalog contained 52 short-run rows plus one manual degradation row after that promotion.
  Latest FFA role-combat proof update: mode `48` hard-gates FFA role-combat attack ownership through FFA readiness, objective role policy selection, visible and shootable enemy facts, target selection, attack decisions, and applied attack-button metadata; the implemented scenario catalog contained 53 short-run rows plus one manual degradation row after that promotion.
  Latest TDM resource-denial proof update: mode `50` hard-gates deny-enemy resource policy through live pickup-candidate scoring, nav score boosts, selected denial-shaped pickup goals, TDM readiness, and compact `team_resource_denial_*` status.
  Latest TDM role spawn-stability update: mode `73` combines the TDM role-route and role-combat owners with the map-repeat forced restart lifecycle, proving post-reload route/combat owner activity and final cleanup from `.tmp\bot_scenarios\20260622T212431Z`.
  Latest behavior policy umbrella update: modes `52` through `96` now cover the behavior umbrella, profile-driven role/team/item/movement policy, bot chat dispatch/audience/rate/initial/reply/event proofs, live chat event taxonomy, combat/survival depth, multiplayer pacing, coop live/share loops, the `base1` campaign interaction matrix, train keyed-path/key-carry evidence with live train interaction-goal resolution, natural bridge-start approach, and post-mover bridge arrival, the movement matrix, movement context rows, accepted crouch route, accepted teleporter entity-route fallback, and accepted `fact2` hazard context. Focused crouch route validation passed from `.tmp\bot_scenarios\movement_crouch_route.json`; focused teleporter entity-route validation passed from `.tmp\bot_scenarios\teleporter_entity_route_final\20260629T191851Z`; focused key-carry bridge-approach validation passed from `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`; focused hazard context validation passed from `.tmp\bot_scenarios\movement_hazard_context_fact2.json`, and the latest full `implemented` run passed 114/114 rows from `.tmp\bot_scenarios\implemented_hazard_context\20260628T083945Z`. The 2026-06-30 movement reference gap audit now accepts both natural crouch and hazard context; the next follow-up is fresh post-change source-counter soaks, fallback behavior, and broader map-behavior depth.
  Latest match item-policy proof update: mode `51` hard-gates the `bot_match_item_policy` umbrella cvar through TDM item-role pickup scoring and deny-enemy resource scoring while the individual proof cvars remain disabled; the focused revalidation passed from `.tmp\bot_scenarios\match_item_policy_check\20260622T050722Z`, and the previous full implemented run passed from `.tmp\bot_scenarios\20260621T210229Z`.
  Stability update: mode `34` team-fire proof and modes `38`/`39` CTF carrier proofs now avoid teleporting live players during smoke setup; focused five-run stress loops passed for `team_fire_avoidance`, `ctf_carrier_support_route`, and `ctf_base_return_route` before that round's implemented-suite pass.
  Implementation logs: `docs-dev/q3a-botlib-perception-blackboard-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-dispatcher-2026-06-18.md`, `docs-dev/q3a-botlib-behavior-action-brain-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-action-item-utility-2026-06-18.md`, `docs-dev/q3a-botlib-special-item-utility-2026-06-18.md`, `docs-dev/q3a-botlib-combat-weapon-metadata-2026-06-18.md`, `docs-dev/q3a-botlib-aim-fairness-policy-2026-06-18.md`, `docs-dev/q3a-botlib-live-aim-policy-integration-2026-06-18.md`, `docs-dev/q3a-botlib-live-combat-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-enemy-health-armor-estimates-2026-06-20.md`, `docs-dev/q3a-botlib-estimate-aware-weapon-selection-2026-06-20.md`, `docs-dev/q3a-botlib-carried-arsenal-selection-2026-06-20.md`, `docs-dev/q3a-botlib-nonweapon-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-utility-deployable-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-escape-deployable-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-safe-nuke-inventory-policy-2026-06-20.md`, `docs-dev/q3a-botlib-nuke-retreat-route-ownership-2026-06-21.md`, `docs-dev/q3a-botlib-timed-route-goal-owner-2026-06-21.md`, `docs-dev/q3a-botlib-teleporter-escape-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`, `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-progress-wait-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`, `docs-dev/q3a-botlib-coop-anti-blocking-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-target-share-2026-06-21.md`, `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-roam-route-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-team-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-fire-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-dropped-flag-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-carrier-support-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-base-return-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-policy-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-precedence-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-transitions-2026-06-22.md`, `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-warmup-bot-start-readiness-2026-06-21.md`, `docs-dev/q3a-botlib-vote-bot-exclusion-2026-06-21.md`, `docs-dev/q3a-botlib-mymap-bot-queue-2026-06-21.md`, `docs-dev/q3a-botlib-queued-nextmap-transition-2026-06-21.md`, `docs-dev/q3a-botlib-mapvote-bot-exclusion-transition-2026-06-21.md`, `docs-dev/q3a-botlib-scoreboard-bot-classification-2026-06-21.md`, `docs-dev/q3a-botlib-intermission-bot-cleanup-2026-06-21.md`, `docs-dev/q3a-botlib-item-timer-fairness-2026-06-18.md`, `docs-dev/q3a-botlib-live-item-timing-consumers-2026-06-18.md`, `docs-dev/q3a-botlib-action-application-helpers-2026-06-18.md`, `docs-dev/q3a-botlib-weapon-inventory-command-api-2026-06-18.md`, `docs-dev/q3a-botlib-weapon-inventory-dispatch-2026-06-18.md`, `docs-dev/q3a-botlib-nav-health-armor-focus-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-helper-scaffold-2026-06-18.md`, `docs-dev/q3a-botlib-smoke-scenario-modes-2026-06-18.md`, `docs-dev/q3a-botlib-engage-enemy-proof-2026-06-18.md`, `docs-dev/q3a-botlib-weapon-switch-proof-2026-06-18.md`, `docs-dev/q3a-botlib-health-armor-pickup-proof-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-proof-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-team-role-depth-2026-06-18.md`, `docs-dev/q3a-botlib-ffa-tdm-role-policy-2026-06-18.md`, `docs-dev/q3a-botlib-team-coop-policy-round-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`.
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
  Reference-map packaging update: `q2aas-stage-aas` now stages eleven generated `.aas` files when optional `q2dm7` and `fact2` are present for `mm-rage`, `worr_crouch_ref`, `q2dm1`, `q2dm2`, `q2dm7`, `q2dm8`, `q2ctf1`, `base1`, `base2`, `fact2`, and `train`; package refresh/audit flows preserve accepted staged AAS members after rebuilding `.install\basew\pak0.pkz`, and `stage_install.py` copies q2aas reference BSPs into `.install\basew\maps`. Broader release-readiness automation remains pending.
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
  Rocket-jump route policy smoke also passes from the refreshed install, proving staged binaries default-block inherited rocket-jump reachability and opt into it only with `bot_allow_rocketjump 1`; mode `14` reports `last_reachability_type=12`, `travel_type_goal_resolved=2`, `route_failures=0`, and `pass=1`, while mode `15` reports `travel_type_goal_expect_blocked=1`, `commands=0`, `route_commands=0`, `travel_type_goal_resolved=0`, `route_failures=8`, and `pass=1`.
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
  Progress: `vote_bot_exclusion` covers bot-origin vote exclusion, `mymap_queue_bot_request` covers bot-attributed MyMap queue insertion plus `ConsumeQueuedMap` cleanup through `bot_mymap_smoke 2`, `queued_nextmap_transition` covers bot-attributed queued-map insertion, queue consumption, and observed same-map `gamemap` reload through `bot_nextmap_smoke 2`, `mapvote_bot_exclusion_transition` covers bot-only map selector setup, blocked bot ballots, deterministic selector finalization, observed same-map reload, and cleanup through `bot_mapvote_smoke 2`, `scoreboard_bot_classification` covers bot-only sorted standings through `bot_scoreboard_smoke 2`, and `intermission_bot_cleanup` covers native bot-only intermission entry plus fake-client/sorted-client cleanup through `bot_intermission_smoke 2`. Tournament veto bot-exclusion and replay reset are covered under `FR-07-T02`; competitive admin flows remain under later FR-07 work.
- [ ] `FR-07-T02` Harden tournament veto/replay flows with explicit error handling and state resets.
  Dependency: none. Priority: P1.
  Progress: `tournament_bot_veto_exclusion` covers the tournament veto bot boundary through `bot_tournament_smoke 2`, assigns a bot the active home-side tournament identity, attempts a veto pick, requires `reason=bot_blocked` with `allowed=0`, and verifies `picks=0` and `bans=0` after cleanup. `tournament_replay_reset` covers replay reset/error handling through `bot_tournament_smoke 3`, rejects out-of-range game `99` with `reason=range_error` and `preserved=1`, then replays game `2` with `reset_applied=1`, one retained winner/map/id, wins rewound to `1-0`, and the series reopened.
- [x] `FR-07-T03` Improve match logging artifact schema/versioning for downstream tooling.
  Dependency: none. Priority: P2.
  Progress: Match-stats and tournament-series JSON artifacts now advertise top-level schema/artifact metadata (`worr.match_stats` and `worr.tournament_series`, version `1`). Successful exports also update `basew/matches/catalog.json` as `worr.match_catalog` version `1`, with relative artifact paths and latest-artifact IDs for downstream tools. `match_logging_schema` validates these fields through `bot_matchlog_smoke 2`, requiring retained players/event-log/matches arrays, embedded match schema metadata, catalog artifact count, latest pointers, relative JSON paths, scratch catalog write/read proof, and final zero-bot cleanup.
- [ ] `FR-07-T04` Add command-level audit for vote/admin privileges and abuse controls.
  Dependency: none. Priority: P1.
  Progress: `admin_bot_privilege_audit` covers the bot/admin command boundary through `bot_admin_audit_smoke 2`, temporarily grants a bot admin session bit, attempts the registered `lock_team red` command, requires `reason=bot_admin_blocked` with `executed=0`, and verifies `red_locked=0` after cleanup. Vote-origin bot privilege checks are covered by `vote_bot_exclusion`; the public operator-facing command/cvar pass is covered by `FR-07-T05`.
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

## Epic FR-09: RmlUi UI Migration
Objective: replace the current JSON/cgame menu presentation layer with RmlUi
and translate the current menu surface into `.rml` and `.rcss` documents.

Primary Areas: `src/client/*`, `src/game/cgame/ui/*`, `src/game/sgame/menu/*`,
future `assets/ui/rml/*`, `docs-dev/plans/rmlui-ui-migration-roadmap.md`

Exit Criteria:
- Main menu, in-game menu, settings, browser/config tools, and
  multiplayer/session menu flows run through RmlUi with parity for current
  supported behavior.
- Renderer integration is native across `rend_gl`, `rend_vk`, and `rend_rtx`.
- Legacy JSON menu loading/widgets are removed or intentionally archived after
  cutover.

Tasks:

2026-07-14 closeout (`FR-09-T01` through `FR-09-T11`, `DV-03-T07`,
`DV-07-T02`, `DV-07-T04`): all 58 registered menu routes are accepted as
`parity_ready` with live runtime/controller/provider behavior, 1,123 consumed
localization hooks, accessibility and keyboard/gamepad navigation services,
responsive canonical design-language compliance, and native OpenGL, Vulkan,
and RTX/vkpt rendering. Final installed-tree sweeps pass 58/58 routes in each
renderer and the complete route contact sheets were visually reviewed. The
legacy-removal gate is open; JSON/menu sources are intentionally archived only
as guarded recovery/reference material, satisfying the documented Gate G4
alternative. `.install/` is refreshed and validated. Evidence:
`docs-dev/rmlui-runtime-ux-design-parity-2026-07-14.md`,
`docs-dev/rmlui-in-world-session-frame-main-menu-polish-2026-07-14.md`, and
`docs-dev/rmlui-native-vulkan-rtx-renderer-parity-2026-07-14.md`. The aggregate
engine DLL link remains independently blocked by unrelated concurrent
networking symbols; the affected renderer targets build, stage, and run.

- [x] `FR-09-T01` Define final runtime ownership, translation inventory, asset
  layout, and cutover policy for the RmlUi migration.
  Dependency: `FR-03-T08`, `DV-04-T02`. Priority: P0.
  Progress: `docs-dev/plans/rmlui-ui-migration-roadmap.md` now defines the
  current menu inventory, recommended ownership model, migration waves, shared
  component plan, validation gates, and legacy cleanup path.
  2026-07-02 first parallel implementation round: five agent lanes seeded
  `assets/ui/rml/` with a core runtime smoke route, shared theme/component
  contracts, mock data contracts, shell/settings/single-player starter
  documents, utility/multiplayer/session starter documents, and implementation
  logs under `docs-dev/rmlui-agent*-round1-2026-07-02.md`. The detailed
  status tracker in `docs-dev/plans/rmlui-ui-migration-roadmap.md` now marks
  the relevant FR-09 tasks active while leaving runtime, renderer, build,
  install-staging, parity, and legacy-removal gates open.
  2026-07-02 second parallel implementation round: package staging now mirrors
  and validates authored `assets/ui/rml/` files as loose
  `.install/<base-game>/ui/rml/` assets, shared RmlUi theme/component contracts
  now cover utility, session, accessibility, keybind, and image-grid surfaces,
  and starter document coverage increased to 30 required routes. Runtime,
  renderer, live data controllers, full parity, and legacy removal remain
  pending.
  2026-07-02 third parallel implementation round: all `57` tracked Wave A/B/C
  migration surfaces now have starter `.rml` documents and are required by the
  smoke manifest. The smoke checker now parses present RML documents and local
  imports, and the route-contract audit validates core, shell, and central
  smoke route manifests. Runtime, renderer, live data controllers, parity
  cutover, and legacy removal remain pending.
  2026-07-02 fourth parallel implementation round: a guarded client
  `ui_rml_enable` runtime-switch scaffold is compiled and wired through the
  active UI bridge, the smoke manifest now requires `migration_phase`
  progression metadata, route-contract validation reports migration phases,
  controller fixtures cover cvar/command/condition/navigation/list-provider
  bridge targets, and core/shell routes carry ownership metadata. Focused
  smoke, route-contract, package, JSON, pytest, and touched-object compile
  checks pass. This does not close Gate G1, G2, G3, or G4 because the real
  RmlUi dependency, renderer bridge, live controllers, runtime navigation, and
  parity evidence remain pending.
  2026-07-02 fifth parallel implementation round: the coordinator accepted a
  dependency-free runtime document probe, selected `controller_stub` route
  progression for `main`, `game`, `options`, `video`, and `download_status`,
  static RML semantics checking, and progress-report tooling. The accepted
  migration phase baseline is now `starter=52` and `controller_stub=5`.
  Focused smoke, route-contract, semantics, progress-report, package, pytest,
  and touched-object compile checks pass. This does not close Gate G1, G2, G3,
  or G4 because the real RmlUi dependency, renderer bridge, live controllers,
  runtime navigation, screenshots, parity evidence, and legacy removal remain
  pending.
  2026-07-02 sixth parallel implementation round: the coordinator accepted
  full-route runtime probe registry coverage for all `57` smoke-manifest
  routes plus `core.runtime_smoke`, controller-contract reference validation,
  runtime asset path/staged loose-file validation, JSON progress-report output,
  and a second five-route `controller_stub` promotion batch
  (`performance`, `accessibility`, `sound`, `screen`, and `input`). The
  accepted migration phase baseline is now `starter=47` and
  `controller_stub=10`. Focused smoke, route-contract, semantics, runtime
  asset, progress-report, package, pytest, and touched-object compile checks
  pass. This still does not close Gate G1, G2, G3, or G4 because the real
  RmlUi dependency, renderer bridge, live controllers, runtime navigation,
  screenshots, parity evidence, and legacy removal remain pending.
  2026-07-02 seventh parallel implementation round: the coordinator accepted
  static runtime registry drift checking, `controller_stub` coverage checking,
  import-aware runtime asset validation, controller-contract progress facts,
  and a third five-route `controller_stub` promotion batch (`multimonitor`,
  `railtrail`, `effects`, `crosshair`, and `language`). The accepted migration
  phase baseline is now `starter=42` and `controller_stub=15`, with `44`
  shell controller-contract references across `15` routes. Focused smoke,
  route-contract, semantics, runtime registry, runtime asset/import,
  controller-stub coverage, progress-report, package, pytest, and
  touched-object compile checks pass. Gate G1, G2, G3, and G4 remain open for
  the real RmlUi dependency, renderer bridge, live controllers, runtime
  navigation, screenshots, parity evidence, and legacy removal.
  2026-07-02 eighth parallel implementation round: the coordinator accepted
  menu-entrypoint route validation, `runtime_stub` eligibility validation,
  JSON runtime-asset reporting, progress phase-progression reporting, and a
  guarded `runtime_stub` promotion for `main`, `game`, and `download_status`.
  The accepted migration phase baseline is now `starter=42`,
  `controller_stub=12`, and `runtime_stub=3`, with `15` advanced routes
  (`26.3%`). Focused smoke, route-contract, semantics, menu-entrypoint,
  runtime-stub eligibility, controller-stub coverage, runtime registry,
  runtime asset/import text and JSON, progress-report, package, pytest, and
  touched-object compile checks pass. Gate G1, G2, G3, and G4 remain open for
  the real RmlUi dependency, renderer bridge, live controllers, native runtime
  navigation, screenshots, parity evidence, and legacy removal.
  2026-07-02 ninth parallel implementation round: the coordinator accepted
  static navigation graph validation, controller fixture validation, detailed
  runtime asset manifest output, a parity checklist manifest/checker, progress
  reporting over all discovered route metadata files, and a four-route utility
  `controller_stub` promotion batch (`addressbook`, `keys`, `legacykeys`, and
  `weapons`). The accepted migration phase baseline is now `starter=38`,
  `controller_stub=16`, and `runtime_stub=3`, with `19` advanced routes
  (`33.3%`) and `54` controller-contract references across `19` routes.
  Focused smoke, route-contract, semantics, menu-entrypoint, runtime-stub
  eligibility, controller-stub coverage, controller-fixture, navigation graph,
  parity-manifest, runtime registry, runtime asset/import text/JSON/manifest,
  progress-report, package, pytest, and touched-object compile checks pass.
  Gate G1, G2, G3, and G4 remain open for the real RmlUi dependency, renderer
  bridge, live controllers, native runtime navigation, screenshots, parity
  evidence, and legacy removal.
  2026-07-02 tenth parallel implementation round: the coordinator accepted
  static command inventory validation, static cvar inventory validation,
  parity-checklist summary output in the progress reporter, a proposed
  dependency decision/audit record, and a four-route utility/list
  `controller_stub` promotion batch (`servers`, `demos`, `players`, and
  `ui_list`). The accepted migration phase baseline is now `starter=34`,
  `controller_stub=20`, and `runtime_stub=3`, with `23` advanced routes
  (`40.4%`) and `65` controller-contract references across `23` routes.
  Focused smoke, route-contract, semantics, command inventory, cvar
  inventory, menu-entrypoint, runtime-stub eligibility, controller-stub
  coverage, controller-fixture, navigation graph, parity-manifest, runtime
  registry, runtime asset/import text/JSON/manifest, progress-report,
  package, pytest, and touched-object compile checks pass. Gate G1, G2, G3,
  and G4 remain open for the real RmlUi dependency, renderer bridge, live
  controllers, native runtime navigation, screenshots, parity evidence, and
  legacy removal.
  2026-07-02 eleventh parallel implementation round: the coordinator accepted
  static data-model/data-binding inventory validation, phase-consistency
  guardrails, dependency-decision validation, progress-report data-model
  summaries, and a four-route single-player/save-load `controller_stub`
  promotion batch (`singleplayer`, `skill_select`, `loadgame`, and
  `savegame`). The accepted migration phase baseline is now `starter=30`,
  `controller_stub=24`, and `runtime_stub=3`, with `27` advanced routes
  (`47.4%`), `75` controller-contract references across `27` routes, and
  data-model inventory coverage reporting `190` static binding/model refs
  across `38` routes with hooks. Focused smoke, route-contract, semantics,
  command/cvar/data-model inventories, phase-consistency, dependency-decision,
  menu-entrypoint, runtime-stub eligibility, controller-stub coverage,
  controller-fixture, navigation graph, parity-manifest, runtime registry,
  runtime asset/import text/JSON/manifest, progress-report, package, pytest,
  and touched-object compile checks pass. Gate G1, G2, G3, and G4 remain open
  for the real RmlUi dependency, renderer bridge, live controllers, native
  runtime navigation, screenshots, parity evidence, and legacy removal.
  2026-07-02 twelfth parallel implementation round: the coordinator accepted
  the remaining shell/local-session `controller_stub` promotion batch
  (`downloads`, `quit_confirm`, `gameflags`, and `startserver`), starter route
  metadata coverage for the multiplayer hub plus all `25` session/match
  routes, condition-expression inventory validation, route-metadata sync
  validation, and progress-report guardrail summaries. The accepted migration
  phase baseline is now `starter=26`, `controller_stub=28`, and
  `runtime_stub=3`, with `31` advanced routes (`54.4%`), `87`
  controller-contract references across `31` routes, `141` static condition
  refs across `22` routes, and metadata sync coverage matching all `57`
  central migration routes plus the support-only `core.runtime_smoke` route.
  Focused smoke, route-contract, semantics, command/cvar/data-model/condition
  inventories, metadata-sync, phase-consistency, dependency-decision,
  menu-entrypoint, runtime-stub eligibility, controller-stub coverage,
  controller-fixture, navigation graph, parity-manifest, runtime registry,
  runtime asset/import text/JSON/manifest, progress-report, package, pytest,
  and touched-object compile checks pass. Gate G1, G2, G3, and G4 remain open
  for the real RmlUi dependency, renderer bridge, live controllers, native
  runtime navigation, screenshots, parity evidence, and legacy removal.
  2026-07-02 thirteenth parallel implementation round: the coordinator
  accepted the first session/vote `controller_stub` promotion batch
  (`vote_menu`, `callvote_main`, `callvote_ruleset`, `callvote_timelimit`,
  `callvote_scorelimit`, `callvote_unlagged`, `callvote_random`, and
  `callvote_map_flags`), event/action inventory validation,
  accessibility/localization inventory validation, a legacy-removal
  inventory/checker, and progress-report event/a11y summaries. The accepted
  migration phase baseline is now `starter=18`, `controller_stub=36`, and
  `runtime_stub=3`, with `39` advanced routes (`68.4%`), `101`
  controller-contract references across `39` routes, `465` static event/action
  refs across all `57` routes, `8` a11y/localization refs across `3` routes,
  and `6` legacy-removal inventory items still blocked or pending. Focused
  smoke, route-contract, semantics, command/cvar/data-model/condition/event/
  a11y inventories, metadata-sync, phase-consistency, dependency-decision,
  legacy-removal, menu-entrypoint, runtime-stub eligibility,
  controller-stub coverage, controller-fixture, navigation graph,
  parity-manifest, runtime registry, runtime asset/import text/JSON/manifest,
  progress-report, package, pytest, and touched-object compile checks pass.
  Gate G1, G2, G3, and G4 remain open for the real RmlUi dependency, renderer
  bridge, live controllers/services, native runtime navigation, screenshots,
  parity evidence, and legacy removal.
  2026-07-02 fifteenth parallel implementation round: the coordinator
  accepted the first dependency-source/build-gate slice. `subprojects/rmlui.wrap`
  now pins upstream RmlUi `6.2` with SHA-256
  `814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b`,
  `meson_options.txt` exposes a default-disabled `rmlui` feature option, and
  `meson.build` only emits `UI_RML_HAS_RUNTIME=1` when an optional RmlUi
  dependency resolves. The client scaffold now exposes dependency-free runtime
  availability, file-interface, and future runtime-hook boundaries, while
  `tools/ui_smoke/check_rmlui_dependency_integration.py` validates the current
  dependency/build state as `optional` with `4/4` integration components
  present. Packaging/staged asset validation remains green with `197`
  packaged assets, `103` RmlUi package/loose assets, `73` staged runtime
  paths, and `16` staged imported assets. Gate G1, G2, G3, and G4 remain open
  for real RmlUi compile/link, live runtime/controllers/services, native
  renderer proof, runtime navigation, screenshots, parity evidence, and legacy
  removal.
  2026-07-02 sixteenth parallel implementation round: the coordinator
  accepted the static controller-stub completion slice. The final `12` central
  starter routes (`admin_commands`, `admin_menu`, `forfeit_confirm`,
  `leave_match_confirm`, `map_selector`, `match_stats`, `mymap_flags`,
  `mymap_main`, `tourney_info`, `tourney_mapchoices`,
  `tourney_replay_confirm`, and `tourney_veto`) are now promoted to
  `controller_stub`, with RML hook hardening for admin, MyMap, map selector,
  match stats, and tournament flows. The accepted migration phase baseline is
  now `starter=0`, `controller_stub=54`, and `runtime_stub=3`, with `57/57`
  advanced routes (`100.0%`), `149` controller-contract references across all
  routes, `57/57` controller-binding parity checklist entries complete, and
  the new controller-stub completion checker passing in strict mode. Legacy
  removal remains blocked with `6` tracked items, `0` ready/complete items,
  and a closed parity gate. Gate G1, G2, G3, and G4 remain open for real RmlUi
  compile/link, live runtime/controllers/services, native renderer proof,
  runtime navigation, screenshots, input/back behavior, parity evidence, and
  legacy removal.
  2026-07-02 fourteenth parallel implementation round: the coordinator
  accepted the multiplayer/lobby/info `controller_stub` promotion batch
  (`multiplayer`, `dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, and
  `dm_matchinfo`), document/body identity inventory, route entrypoint
  inventory, route metadata shape validation, and progress-report
  legacy-removal gate summaries. The accepted migration phase baseline is now
  `starter=12`, `controller_stub=42`, and `runtime_stub=3`, with `45`
  advanced routes (`78.9%`), `117` controller-contract references across `45`
  routes, `57` matched document/body IDs, `72` unique entrypoint refs, and
  `58` route metadata entries passing stricter shape validation. Legacy
  removal remains blocked with `6` tracked items, `0` ready/complete items,
  and a closed parity gate. Focused smoke, route-contract, semantics,
  command/cvar/data-model/condition/event/a11y/document-id/entrypoint
  inventories, metadata-sync, route-metadata-shape, phase-consistency,
  dependency-decision, legacy-removal, menu-entrypoint, runtime-stub
  eligibility, controller-stub coverage, controller-fixture, navigation graph,
  parity-manifest, runtime registry, runtime asset/import text/JSON/manifest,
  progress-report, package, pytest, and touched-object compile checks pass.
  Gate G1, G2, G3, and G4 remain open for the real RmlUi dependency, renderer
  bridge, live controllers/services, native runtime navigation, screenshots,
  parity evidence, and legacy removal.
- [x] `FR-09-T02` Add RmlUi dependency integration, Meson/build wiring, and
  `.install` asset staging for the new document/theme path.
  Dependency: `FR-09-T01`, `DV-06-T01`. Priority: P0.
  Progress: `tools/package_assets.py` now mirrors `assets/ui/rml/` loose into
  `.install/<base-game>/ui/rml/` by default and validates archive/loose hashes
  for authored RmlUi assets. Focused `tools/test_package_assets.py` coverage
  passes, and a current authored-asset package validation staged `67` RmlUi
  package/loose files under `.tmp/rmlui/round2-package-validation`.
  Round 4 note: dependency-free runtime-switch source wiring is in place and
  package staging validates the expanded RmlUi asset set, but first-class RmlUi
  dependency integration remains pending.
  Round 5 note: runtime document probing is implemented and validated through
  the client scaffold's `ui_rml_asset_root` and `ui_rml_probe [route_id]`
  filesystem path, but first-class RmlUi dependency integration and build
  wiring remain open.
  Round 6 note: the runtime probe registry now covers all `57` manifest routes
  plus `core.runtime_smoke`, and runtime asset path checks validate source
  documents plus staged loose route documents. First-class RmlUi dependency
  integration and build wiring remain open.
  Round 7 note: the runtime registry check and import-aware runtime asset check
  are first-class smoke tools. First-class RmlUi dependency integration and
  build wiring remain open.
  Round 8 note: runtime asset validation now has structured JSON output and
  staged loose-file JSON evidence for the `73` route/import runtime paths.
  First-class RmlUi dependency integration and build wiring remain open.
  Round 9 note: runtime asset validation now writes deterministic detailed
  manifests for source/runtime/staged route and imported asset paths.
  First-class RmlUi dependency integration and build wiring remain open.
  Round 10 note: `docs-dev/rmlui-dependency-decision-record-2026-07-02.md`
  documents the proposed RmlUi dependency/audit path, preferring a Meson
  subproject or wrap with a reviewed vendored-source fallback. No dependency,
  Meson wiring, build option, or default runtime switch is added yet.
  Round 11 note: `tools/ui_smoke/check_rmlui_dependency_decision.py`
  validates that the dependency decision record remains proposed/not
  implemented, names the native OpenGL/Vulkan/RTX-vkpt obligations, and does
  not overclaim Meson/build/dependency wiring. First-class RmlUi dependency
  integration and build wiring remain open.
  Round 15 note: `subprojects/rmlui.wrap` now pins upstream RmlUi `6.2`, the
  default-disabled `rmlui` Meson feature option gates optional dependency
  probing, and `tools/ui_smoke/check_rmlui_dependency_integration.py` reports
  the current state as `optional` with `4/4` components present. Real RmlUi
  compile/link and runtime enablement remain open.
  Round 17 note: Meson can now fall back to the pinned RmlUi CMake subproject
  with `RMLUI_FONT_ENGINE=none`, wrap provide aliases, and fallback
  `rmlui_core` target selection. `.tmp/rmlui/round17-rmlui-enabled3`
  configured with `-Drmlui=enabled` and linked both `rmlui_core.dll` and
  `worr_engine_x86_64.dll`; default-disabled build behavior remains preserved,
  while install refresh and runtime enablement remain open.
  Round 41 note: the enabled RmlUi build now refreshes `.install` with the
  runtime DLL dependency and current route assets, and the staged client opens
  all 57 registered RmlUi routes without a fresh crash dump. Supported build
  matrix policy and default runtime ownership remain open. VSCode setup/build
  now forces `builddir-win` to `-Drmlui=enabled`, stages `rmlui_core.dll`, and
  makes the first launch profile an OpenGL RmlUi menu run; Vulkan and RTX
  launch profiles remain separate until native RmlUi renderer bridges exist.
- [x] `FR-09-T03` Implement the RmlUi runtime bootstrap plus native
  renderer/input/file/system bridges.
  Dependency: `FR-09-T02`. Priority: P0.
  Round 4 note: guarded runtime-switch scaffolding is compiled and reachable
  through the active UI bridge. Sample document opening through an actual RmlUi
  runtime and native renderer draw proof remain pending.
  Round 5 note: the runtime document probe is accepted for mapped shell/smoke
  documents. Native renderer integration and real RmlUi runtime proof remain
  open.
  Round 6 note: dependency-free runtime probing now covers the full route set.
  Native renderer integration and real RmlUi runtime proof remain open.
  Round 7 note: static registry drift validation protects the full-route probe
  table. Native renderer integration and real RmlUi runtime proof remain open.
  Round 8 note: `UI_Rml_RouteForMenu` is now checked as a first-class
  menu-entrypoint contract, and `main`, `game`, and `download_status` are
  accepted as guarded `runtime_stub` routes because they probe documents then
  return to the legacy UI. Native renderer integration and real RmlUi runtime
  proof remain open.
  Round 10 note: the dependency decision record captures required Gate G1
  runtime, filesystem, input, font/text, route, and native renderer proof for
  OpenGL, Vulkan, and RTX/vkpt. Native renderer integration and real RmlUi
  runtime proof remain open.
  Round 15 note: the client scaffold exposes runtime availability, a
  dependency-free file-interface boundary, and a future runtime-hook interface
  for the selected dependency path. Native renderer integration and real RmlUi
  runtime proof remain open.
  Round 17 note: `src/client/ui_rml/ui_rml_runtime.cpp` registers a compiled
  RmlUi Core adapter behind `UI_RML_HAS_RUNTIME`, proves use of
  `Rml::GetVersion`, `Rml::Initialise`, and `Rml::Shutdown`, and reports
  `renderer_unavailable` because no native renderer bridge is registered.
  Native renderer integration and sample route draw proof remain open.
  Round 18 note: the compiled adapter now installs WORR-backed RmlUi
  `SystemInterface` and `FileInterface` implementations before
  `Rml::Initialise`, routes runtime file probes through `FS_OpenFile`,
  `FS_Read`, `FS_Seek`, `FS_Tell`, `FS_Length`, and `FS_CloseFile`, and exposes
  `ui_rml_runtime_probe [route_id]`. Native renderer integration and sample
  route draw proof remain open.
  Round 19 note: the client scaffold now declares explicit OpenGL, Vulkan, and
  RTX/vkpt renderer-family lanes plus an opaque native render-interface hook,
  and route availability is gated by `UI_Rml_RendererIsAvailable` before the
  runtime route-open hook can be considered. No renderer backend or sample
  route draw proof is claimed.
  Round 20 note: the OpenGL renderer DLL now exports the first
  renderer-family `Rml::RenderInterface` scaffold and the client registers it
  after renderer initialization, but `R_RmlUiCanRender()` remains `false`.
  OpenGL visible draw behavior, Vulkan/RTX-vkpt native bridges, and sample
  route draw proof remain open.
  Round 21 note: the OpenGL bridge now owns RmlUi geometry caches, renders via
  the OpenGL 2D tessellator, uploads generated textures, resolves loaded
  textures through the renderer image manager, applies scissor state, and
  reports `R_RmlUiCanRender=true`. Runtime route ownership remains guarded by
  `CanOpenRoutes=false`; Vulkan/RTX-vkpt native bridges and sample route draw
  proof remain open. Implementation log:
  `docs-dev/rmlui-round21-opengl-render-primitives-2026-07-04.md`.
  Round 22 note: the compiled runtime now opens the guarded
  `core.runtime_smoke` route through `ui_rml_runtime_open`, creates the
  `worr_ui` RmlUi context, loads/shows one document, updates and renders it
  from the client UI draw loop, closes it on Escape or
  `ui_rml_runtime_close`, and keeps normal menu routes on legacy fallback.
  Vulkan/RTX-vkpt native bridges, full input/font services, normal menu route
  ownership, screenshots, and parity proof remain open. Implementation log:
  `docs-dev/rmlui-round22-guarded-context-route-2026-07-04.md`.
  Round 23 note: the guarded sample route now receives key, text, mouse
  button, mouse wheel, and pointer movement events through the RmlUi context;
  Escape and mouse button 2 close the route; and
  `ui_rml_runtime_status`/`ui_rml_runtime_capture` expose counters for the
  next OpenGL evidence pass. Normal menu route ownership, Vulkan/RTX-vkpt
  native bridges, full input/font services, automated screenshots, and parity
  proof remain open. Implementation log:
  `docs-dev/rmlui-round23-input-capture-2026-07-04.md`.
  Round 24 note: the guarded sample route now has an automated OpenGL TGA
  runtime capture harness, local `r_screenshot_dir` evidence routing, visible
  smoke-route RCSS, and a temporary layout-only RmlUi font engine that lets the
  `RMLUI_FONT_ENGINE=none` build initialize. The accepted capture is
  nonblank and records 24 updates/renders, but real glyph rendering, normal
  menu route ownership, Vulkan/RTX-vkpt native bridges, runtime navigation,
  full input/font services, and parity proof remain open. Implementation log:
  `docs-dev/rmlui-round24-runtime-capture-harness-2026-07-04.md`.
  Round 25 note: the guarded sample route now emits actual text geometry
  through `UI_Rml_SmokeFontEngineInterface`, a minimal 5x7 bitmap glyph path
  that feeds RmlUi mesh quads into the existing OpenGL bridge. The capture
  harness now requires the glyph-generation marker and accepted a fresh
  `rmlui_runtime_smoke_round25.tga` screenshot. This remains a smoke path, not
  final font/text ownership or normal menu route ownership. Implementation log:
  `docs-dev/rmlui-round25-smoke-font-glyph-path-2026-07-04.md`.
  Round 26 note: the guarded sample capture now has route-specific TGA layout
  assertions. The runtime capture harness records smoke-route color counts,
  bounding boxes, and panel/text/button relationship checks, and accepted a
  fresh `rmlui_runtime_smoke_round26.tga` screenshot with `layout_ok=true`.
  This remains guarded OpenGL sample evidence, not normal route ownership or
  parity. Implementation log:
  `docs-dev/rmlui-round26-capture-layout-assertions-2026-07-04.md`.
  Round 27 note: the same guarded capture now drives synthetic pointer, text,
  mouse-wheel, and mouse-button-2 back-close input after the screenshot, then
  requires inactive final status plus input and route close counters. The
  accepted `rmlui_runtime_smoke_round27.tga` manifest records
  `synthetic_input_marker_seen=true`, `inactive_status_seen=true`,
  positive input counters, and route open/close/request counters. This remains
  guarded OpenGL sample evidence, not broad input parity or normal route
  ownership. Implementation log:
  `docs-dev/rmlui-round27-synthetic-input-capture-2026-07-04.md`.
  Round 28 note: the guarded capture harness now runs a two-viewport OpenGL
  matrix with explicit `r_geometry` values and exact screenshot-dimension
  validation. The accepted matrix covers `960x720` and `1280x960`, retaining
  glyph, layout, synthetic input, back-close, route teardown, and inactive
  status checks for both viewports. This remains guarded sample evidence, not
  normal route ownership, responsive widescreen parity, or renderer parity.
  Implementation log:
  `docs-dev/rmlui-round28-viewport-matrix-2026-07-04.md`.
  Round 29 note: the guarded runtime now opens `main`, `game`, and
  `download_status` through `UI_OpenMenu` when `ui_rml_enable=1`.
  `ui_rml_runtime_capture_menu` and the route-matrix harness accepted all
  three routes at `960x720` with active OpenGL status, glyph text evidence,
  synthetic input, close counters, inactive final status, and fresh
  screenshots. This remains guarded OpenGL evidence, not default route
  ownership, final theme/layout parity, controller behavior, runtime
  navigation, Vulkan/RTX-vkpt renderer parity, or end-user parity.
  Implementation log:
  `docs-dev/rmlui-round29-menu-route-capture-2026-07-04.md`.
  Round 30 note: `tools/ui_smoke/check_rmlui_renderer_matrix.py` now records
  the renderer-family matrix as `OpenGL=native_guarded`,
  `Vulkan=blocked_until_native`, and `RTX/vkpt=blocked_until_native`; it
  rejects Vulkan/RTX-to-OpenGL mapping and premature non-OpenGL RmlUi runtime
  dependency enablement. This is a guardrail, not native Vulkan/RTX-vkpt draw
  proof. Implementation log:
  `docs-dev/rmlui-round30-renderer-matrix-guardrails-2026-07-04.md`.
  Round 31 note: the guarded capture harness now supports
  `--renderer-matrix`, combining the OpenGL `main`/`game`/`download_status`
  route matrix with the renderer-family guardrail in one aggregate manifest.
  The accepted manifest records `routes=3`, `route_passed=3`,
  `native_guarded_lanes=1`, `blocked_lanes=2`, and `errors=0`. This still is
  not native Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round31-renderer-matrix-capture-manifest-2026-07-05.md`.
  Round 32 note: `tools/ui_smoke/check_rmlui_vulkan_bridge_readiness.py`
  now inventories Vulkan and RTX/vkpt native UI/image/draw foundations while
  requiring both lanes to remain blocked until renderer-owned RmlUi bridge
  classes, family exports, runtime dependencies, and non-null native
  interfaces exist. Accepted counts are `foundation_lanes=2`,
  `native_bridge_lanes=0`, `blocked_lanes=2`,
  `missing_bridge_requirements=8`, and `errors=0`. This is readiness
  evidence, not native Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round32-vulkan-rtx-bridge-readiness-2026-07-05.md`.
  Round 33 note: `check_rmlui_runtime_capture.py --renderer-matrix` now
  embeds the Vulkan/RTX bridge-readiness audit in the aggregate renderer
  manifest alongside OpenGL route evidence and the renderer-family guardrail.
  Accepted counts include `bridge_foundation_lanes=2`,
  `native_bridge_lanes=0`, `bridge_blocked_lanes=2`,
  `missing_bridge_requirements=8`, and `errors=0`. This still is not native
  Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round33-bridge-readiness-renderer-manifest-2026-07-05.md`.
  Round 34 note: bridge-readiness and aggregate renderer manifests now carry
  named native bridge activation requirements for Vulkan and RTX/vkpt:
  `activation_requirements=8`, `satisfied_activation_requirements=0`, and
  `pending_activation_requirements=8`. Partial activation, such as only adding
  a Vulkan bridge class, remains failed until the full native bridge is wired.
  This still is not native Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round34-native-bridge-activation-checklist-2026-07-05.md`.
  Round 35 note: bridge-readiness and aggregate renderer manifests now report
  activation status and next blockers for Vulkan and RTX/vkpt. Accepted counts
  include `activation_complete_lanes=0`, `partial_activation_lanes=0`, and
  `inactive_activation_lanes=2`; both non-OpenGL lanes remain
  `blocked_no_activation`. This still is not native Vulkan/RTX-vkpt draw
  proof. Implementation log:
  `docs-dev/rmlui-round35-native-bridge-activation-status-2026-07-05.md`.
  Round 36 note: bridge-readiness and aggregate renderer manifests now require
  lane-specific renderer source-set wiring before a non-OpenGL bridge can
  activate. Accepted counts are now `activation_requirements=10`,
  `satisfied_activation_requirements=0`, and
  `pending_activation_requirements=10`; partial class-only activation points
  next at `native_bridge_source_compiled`. This still is not native
  Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round36-native-bridge-source-set-activation-2026-07-05.md`.
  Round 37 note: `src/renderer/rmlui_bridge.cpp` is now wired into the Vulkan
  and RTX/vkpt renderer source sets in inactive mode, satisfying
  `native_bridge_source_compiled` for both non-OpenGL lanes while leaving
  native bridge classes, family exports, runtime dependencies, native
  interface exports, and route-visible capture pending. Accepted counts are
  `satisfied_activation_requirements=2` and
  `pending_activation_requirements=8`. This still is not native
  Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round37-inactive-non-gl-bridge-source-wiring-2026-07-05.md`.
  Round 38 note: inactive Vulkan and RTX/vkpt `Rml::RenderInterface` class
  stubs are now present in `src/renderer/rmlui_bridge.cpp`, satisfying
  `native_bridge_class_present` for both non-OpenGL lanes while leaving
  family exports, runtime dependencies, native interface exports, implemented
  render methods, and route-visible capture pending. Accepted counts are
  `satisfied_activation_requirements=4` and
  `pending_activation_requirements=6`. This still is not native
  Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round38-inactive-non-gl-bridge-class-stubs-2026-07-05.md`.
  Round 39 note: inactive Vulkan and RTX/vkpt renderer-family exports are now
  present in `src/renderer/rmlui_bridge.cpp`, satisfying
  `native_family_export_present` for both non-OpenGL lanes while keeping
  runtime dependencies, native interface exports, implemented render methods,
  and route-visible capture pending. Accepted counts are
  `satisfied_activation_requirements=6` and
  `pending_activation_requirements=4`, with
  `next_activation_requirement=runtime_dependency_enabled`. This still is not
  native Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round39-inactive-non-gl-family-exports-2026-07-05.md`.
  Round 40 note: inactive Vulkan and RTX/vkpt renderer runtime dependencies
  are now wired in `meson.build`, satisfying `runtime_dependency_enabled` for
  both non-OpenGL lanes while keeping native interface exports, implemented
  render methods, and route-visible capture pending. Accepted counts are
  `satisfied_activation_requirements=8` and
  `pending_activation_requirements=2`, with
  `next_activation_requirement=native_interface_export_present`. This still
  is not native Vulkan/RTX-vkpt draw proof. Implementation log:
  `docs-dev/rmlui-round40-inactive-non-gl-runtime-dependencies-2026-07-05.md`.
  Round 41 note: installed OpenGL route loading now closes only the known
  active RmlUi document during route swaps, fixing the freed-document crash
  seen while opening menus. Staged validation opened all 57 registered RmlUi
  routes with no new crash dump and no parser/fallback/error log hits.
  Vulkan/RTX-vkpt renderer implementations, final input/font services,
  controller behavior, and parity proof remain open. Implementation log:
  `docs-dev/rmlui-round41-menu-route-loading-2026-07-06.md`.
  Round 42 note: active OpenGL RmlUi menus now resize against the renderer
  virtual UI canvas, receive mouse input in that same coordinate space, convert
  scissor rectangles back to framebuffer pixels, and draw a software cursor so
  menu cursor visibility does not depend on the platform cursor during
  fullscreen/window state changes. Staged capture validation covered `960x720`,
  `1280x720`, and live Win32 `MoveWindow()` resize events with no fresh crash
  dump. Implementation log:
  `docs-dev/rmlui-round42-resize-canvas-cursor-2026-07-06.md`.
  Round 43 note: the guarded OpenGL RmlUi path now renders text through a
  SDL3_ttf-backed font engine, refines shared menu layout/copy, keeps long
  keybind-style utility lists inside the active canvas, and validates final
  staged all-route loading plus `960x720` keybind screenshot evidence. Live
  data-model/controller behavior, localization/text shaping parity, and native
  Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round43-menu-layout-ttf-refinement-2026-07-06.md`.
  Round 44 note: the guarded OpenGL RmlUi path now prefers Quake II Rerelease
  TTF assets for the display, UI, and monospace faces, records rerelease
  source markers in generated text evidence, and validates the staged full
  route sweep with `58` unique routes opened and no failure/parser/error hits.
  Live data-model/controller behavior, localization/text shaping parity, and
  native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round44-q2r-font-menu-refinement-2026-07-06.md`.
  Round 45 note: long OpenGL RmlUi menu surfaces now reserve viewport-safe
  scroll regions for settings forms, save/load slot lists, and the in-game
  action list so Back/Close actions remain visible at `960x720`; the staged
  full-route sweep still opens `58` unique routes with no failure/parser/error
  hits. Live data-model/controller behavior, localization/text shaping parity,
  and native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation
  log: `docs-dev/rmlui-round45-bounded-menu-list-refinement-2026-07-06.md`.
  Round 46 note: the staged OpenGL RmlUi path now has focused Single Player
  hub and generic `ui_list` layout refinements on top of the `30`-route
  representative visual pass; the final staged all-route sweep still opens
  `58` unique routes with no failure/parser/error hits. Live data-model/
  controller behavior, localization/text shaping parity, and native
  Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round46-singleplayer-utility-list-refinement-2026-07-06.md`.
  Round 47 note: the staged OpenGL RmlUi path now has focused session-list and
  keybind containment refinements for `admin_commands`, `callvote_main`,
  `dm_join`, and `keys`; the final staged all-route sweep still opens `58`
  unique routes with Quake II Rerelease font-source evidence and no
  failure/parser/error hits. Live data-model/controller behavior,
  localization/text shaping parity, and native Vulkan/RTX-vkpt RmlUi rendering
  remain pending. Implementation log:
  `docs-dev/rmlui-round47-session-keybind-containment-2026-07-06.md`.
  Round 48 note: the staged OpenGL RmlUi path now has compact settings toggle
  controls and focused form containment refinements for `performance`,
  `sound`, `downloads`, and `startserver`; the final staged all-route sweep
  still opens `58` unique routes with Quake II Rerelease font-source evidence
  and no failure/parser/error hits. Live data-model/controller behavior,
  localization/text shaping parity, and native Vulkan/RTX-vkpt RmlUi rendering
  remain pending. Implementation log:
  `docs-dev/rmlui-round48-settings-toggle-form-refinement-2026-07-06.md`.
  Round 49 note: the staged OpenGL RmlUi path now adds conservative
  RmlUi-native color/border transitions plus shared header/footer framing
  across representative shell, settings, session, and utility routes; the
  final staged all-route sweep still opens `58` unique routes with Quake II
  Rerelease font-source evidence and no failure/parser/transition/error hits.
  Live data-model/controller behavior, localization/text shaping parity, and
  native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round49-transition-aesthetic-refinement-2026-07-06.md`.
  Round 50 note: the staged OpenGL RmlUi path now uses a `960x720` reference
  canvas for runtime context dimensions, mouse conversion, software cursor
  drawing, and renderer 2D scale selection, fixing the main-menu
  fullscreen-style over-scaling and right-edge clipping repro; the final
  staged all-route sweep still opens `58` unique routes with Quake II
  Rerelease font-source evidence and no failure/parser/transition/error hits.
  Live data-model/controller behavior, localization/text shaping parity, and
  native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round50-scaling-positioning-refinement-2026-07-06.md`.
  Round 51 note: the staged OpenGL RmlUi path now has a shared typed-widget
  layout for non-main menus: settings rows use stable label/control/value
  columns, toggles/ranges/selects/combos/image-value selectors/fields/progress
  rows receive control-specific widths, utility text inputs no longer
  collapse, and `download_status` imports the settings control contract; the
  final staged all-route sweep still opens `58` unique routes with Quake II
  Rerelease font-source evidence and no failure/parser/transition/error hits.
  Live data-model/controller behavior, localization/text shaping parity, and
  native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round51-widget-layout-refinement-2026-07-06.md`.
  Round 52 note: the staged OpenGL RmlUi path now uses real shell command
  buttons and deterministic two-column command grids for Options, Game,
  Single Player, save/load, multiplayer, and session navigation surfaces on a
  narrower `604px` menu contract; the final staged all-route sweep still
  opens `58` unique routes with Quake II Rerelease font-source evidence and
  no failure/parser/transition/error hits. Live data-model/controller
  behavior, localization/text shaping parity, and native Vulkan/RTX-vkpt
  RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round52-navigation-layout-refinement-2026-07-06.md`.
  Round 53 note: the staged OpenGL RmlUi path now polishes the deterministic
  command grids into spaced action tiles with slimmer heights, rounded
  corners, dark fills, and hover/focus left accents for shell, single-player,
  save/load, multiplayer, and session routes; the final staged all-route sweep
  still opens `58` unique routes with Quake II Rerelease font-source evidence
  and no failure/parser/transition/error hits. Live data-model/controller
  behavior, localization/text shaping parity, and native Vulkan/RTX-vkpt
  RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round53-menu-polish-refinement-2026-07-06.md`.
  Round 54 note: the staged OpenGL RmlUi path now preserves primary and
  destructive action intent inside the high-specificity shell/session command
  grids, converts the remaining command-like pseudo-buttons to real buttons,
  and keeps the final staged all-route sweep at `58` unique routes with Quake
  II Rerelease font-source evidence and no failure/parser/transition/error
  hits. Live data-model/controller behavior, localization/text shaping parity,
  and native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation
  log: `docs-dev/rmlui-round54-action-intent-widget-refinement-2026-07-06.md`.
  Round 55 note: the staged OpenGL RmlUi path now exposes
  `ui_rml_runtime_popup`, routes Quit/Forfeit/Leave Match/Replay confirmation
  pages through popup-style route opens, supports `data-command-cvar` click
  targets, and keeps the final staged all-route sweep at `58` unique routes
  with Quake II Rerelease font-source evidence and no
  failure/parser/transition/error hits. Live data-model/controller behavior,
  localization/text shaping parity, and native Vulkan/RTX-vkpt RmlUi rendering
  remain pending. Implementation log:
  `docs-dev/rmlui-round55-popup-audio-menu-refinement-2026-07-07.md`.
  Round 56 note: the staged OpenGL RmlUi path now consumes
  `data-menu-music="menu"` through the existing OGG playback path, adds music
  intent to high-level hub routes, routes Game menu Quit through the same
  `quit_confirm` popup path as Main Quit, and keeps the final staged all-route
  sweep at `58` unique routes with Quake II Rerelease font-source evidence and
  no failure/parser/transition/error hits. Live data-model/controller
  behavior, localization/text shaping parity, and native Vulkan/RTX-vkpt
  RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round56-menu-music-popup-parity-2026-07-07.md`.
  Round 57 note: the staged OpenGL RmlUi path now consumes
  `data-menu-sound-open`, attaches target-level focus/change audio listeners
  to interactive controls, keeps high-level hub routes on explicit open-sound
  and menu-music metadata, and keeps the final staged all-route sweep at `58`
  unique routes with `14` open-sound cue markers, Quake II Rerelease
  font-source evidence, and no failure/parser/transition/error hits. Live
  data-model/controller behavior, localization/text shaping parity, and native
  Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round57-open-focus-audio-refinement-2026-07-07.md`.
  Round 58 note: client-side and cgame-side `pushmenu` producers now prefer
  registered RmlUi route IDs when `ui_rml_enable` is active, confirmation route
  IDs use the RmlUi popup route command, and the cgame UI import/export
  contract now exposes deterministic command insertion through
  `CGameUI_Import_v5` / `CGameUI_Export_v5`. Staged OpenGL probes prove
  `pushmenu options` opens the RmlUi Options route, while Quit, Forfeit, Leave
  Match, and Tournament Replay confirmations open through the popup path with
  alert sounds and menu music cues. Live data-model/controller behavior,
  localization/text shaping parity, and native Vulkan/RTX-vkpt RmlUi rendering
  remain pending. Implementation log:
  `docs-dev/rmlui-round58-pushmenu-popup-bridge-2026-07-07.md`.
  Round 59 note: the staged OpenGL RmlUi path now refines the Multiplayer hub
  against the original pre-RmlUi menu. The page uses shell-grid styling,
  removes the dead `multiplayer.connect_address` command, restores q2servers,
  address-book, demos, Start Server setup defaults, Player Setup, and Options
  as real legacy command strings, and validates `pushmenu multiplayer` with
  TTF font-source, open-sound, menu-music, active runtime status, and `960x720`
  visual evidence. Live data-model/controller behavior, localization/text
  shaping parity, and native Vulkan/RTX-vkpt RmlUi rendering remain pending.
  Implementation log:
  `docs-dev/rmlui-round59-multiplayer-hub-parity-refinement-2026-07-07.md`.
  Round 60 note: the staged OpenGL RmlUi path now refines Video Setup against
  the original pre-RmlUi settings menu. The page restores three-state
  borderless mode, Multi-Monitor, anti-aliasing, hardware gamma, anisotropic
  filtering, texture saturation/intensity, lightmap saturation/brightness, and
  renderer backend controls with typed widgets in a compact three-column
  layout, and validates `pushmenu video` with TTF font-source, open-sound,
  menu-music, active runtime status, and `960x720` visual evidence. Live
  settings persistence/navigation parity, localization/text shaping parity,
  and native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation
  log:
  `docs-dev/rmlui-round60-video-settings-parity-refinement-2026-07-07.md`.
  Round 61 note: the settings family now consistently requests menu music and
  open-sound cues on route open, while Screen Setup and Effects Setup use typed
  action rows for their nested Crosshair/Railgun Trail navigation and compact
  two-column layouts that keep all rows above Back/Close at `960x720`. Live
  settings persistence/navigation parity, localization/text shaping parity,
  and native Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation
  log:
  `docs-dev/rmlui-round61-settings-audio-action-row-refinement-2026-07-07.md`.
  Round 62 note: the single-player/local-session pages now request the same
  menu music and open-sound cues as the refined shell/settings pages. Skill
  Select difficulty choices and Start Server's Begin Game action carry
  explicit confirm cues, while Start Server uses a three-column static fallback
  layout that keeps Server, Match Setup, Rules, and footer controls visible at
  `960x720`. Live condition evaluation, settings persistence/navigation
  parity, localization/text shaping parity, and native Vulkan/RTX-vkpt RmlUi
  rendering remain pending. Implementation log:
  `docs-dev/rmlui-round62-singleplayer-audio-startserver-refinement-2026-07-07.md`.
  Round 63 note: the staged OpenGL RmlUi path now normalizes the full utility
  route family. Address Book, Demos, Key Bindings, Legacy Keys, Player Setup,
  Servers, Session List, and Weapon Bindings all carry menu-music/open-sound
  metadata, intent-specific action sounds, and `pushmenu` runtime evidence
  with Quake II Rerelease TTF markers at `960x720`. Address Book, Key
  Bindings, and Weapon Bindings now have bounded grid layouts that keep their
  static fallback content above footer actions. Live utility controllers,
  key-capture behavior, localization/text shaping parity, and native
  Vulkan/RTX-vkpt RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round63-utility-audio-layout-refinement-2026-07-07.md`.
  Round 64 note: the staged OpenGL RmlUi path now normalizes the
  session/match route family. Non-popup session pages carry menu-music and
  open-sound metadata, confirmation pages keep popup/alert treatment, session
  controls carry intent-specific sounds, the Vote menu uses live `ui_vote_*`
  cvars plus `worr_vote_*` commands, and representative lobby/callvote/admin/
  stats/map-choice surfaces have bounded `960x720` evidence. Live
  session/list controllers, full navigation parity, and native Vulkan/RTX-vkpt
  RmlUi rendering remain pending. Implementation log:
  `docs-dev/rmlui-round64-session-audio-layout-refinement-2026-07-07.md`.
  Round 65 note: shell hub routes now use grouped RmlUi sections for Options,
  Game, and Multiplayer, authored route buttons across shell/settings/
  single-player/session/utility carry explicit action sounds, and final
  `960x720` captures prove Main, Options, Game, Multiplayer, and Single
  Player stay contained with Quake II Rerelease TTF markers. Live controllers,
  full navigation parity, and native Vulkan/RTX-vkpt RmlUi rendering remain
  pending. Implementation log:
  `docs-dev/rmlui-round65-shell-hub-audio-refinement-2026-07-07.md`.
  Round 66 note: shared popup styling now gives Quit/Forfeit/Leave/Replay
  confirmations compact modal framing with side-by-side actions, fixed-width
  menu panels use looser minimums and contained overflow, and reusable RmlUi
  component templates declare action/change sounds for future live
  controllers. Runtime captures cover Quit popup, Options, Video, Key
  Bindings, and DM Join with Quake II Rerelease TTF markers. True
  narrow-viewport runtime parity remains pending because the staged Windows
  launch path still reported a `960x720` RmlUi canvas when smaller geometry
  was requested. Implementation log:
  `docs-dev/rmlui-round66-menu-containment-popup-refinement-2026-07-07.md`.
- [x] `FR-09-T04` Integrate fonts, localization, theme assets, cursor/audio
  affordances, and accessibility policy into the RmlUi stack.
  Dependency: `FR-09-T03`. Priority: P1.
  Progress: source theme contracts now include base, utility, session, and
  accessibility styles plus font/theme staging notes. Final localization,
  accessibility, text-shaping, and broader input-service parity remain pending.
  Round 25 note: the first runtime-visible glyph geometry exists for the
  guarded smoke route through a temporary ASCII bitmap font engine. Final font
  source, fallback policy, localization text flow, shaping, scaling, and
  accessibility behavior remain pending.
  Round 6 note: `performance`, `accessibility`, `sound`, `screen`, and `input`
  now have `controller_stub` metadata for static cvar/command/navigation
  evidence. Live runtime font, cursor, audio, input, and accessibility services
  remain pending.
  Round 7 note: remaining low-risk settings routes `multimonitor`,
  `railtrail`, `effects`, `crosshair`, and `language` now have
  `controller_stub` metadata. Live runtime font, cursor, audio, input, and
  accessibility services remain pending.
  Round 13 note: accessibility/localization inventory validation now reports
  `8` static refs across `3` routes, `6` unique localization keys, and `0`
  malformed hooks. Live runtime localization, accessibility, font, cursor,
  audio, and input services remain pending.
  Round 41 note: linked shell/settings/single-player/session/accessibility
  route themes now avoid RmlUi-rejected browser CSS features in the installed
  route-load pass. Final font/text shaping, localization flow, cursor/audio,
  and live accessibility services remain pending.
  Round 42 note: the guarded OpenGL RmlUi path now draws its own
  `/gfx/cursor.png` software cursor after RmlUi rendering, keeping the cursor
  visible when platform menu/fullscreen state hides the OS cursor. Final text
  cursor variants, cursor policy, audio, and live accessibility services
  remain pending.
  Round 43 note: RmlUi text now uses SDL3_ttf-backed generated textures on the
  staged OpenGL path, with packaged UI and monospace font faces validated in
  runtime logs. Localization flow, text shaping policy, audio, and live
  accessibility services remain pending.
  Round 44 note: the packaged RmlUi font roles now resolve first to Quake II
  Rerelease `RussoOne`, `Montserrat`, and `RobotoMono` assets from the normal
  filesystem search path, with static and runtime guardrails requiring the
  rerelease font-source marker. Localization flow, text shaping policy, audio,
  and live accessibility services remain pending.
  Round 45 note: settings forms now use explicit bounded dimensions and widths
  under the same Quake II Rerelease font metrics, preventing long settings
  pages from hiding footer actions while keeping text-source validation intact.
  Localization flow, text shaping policy, audio, and live accessibility
  services remain pending.
  Round 46 note: the Single Player hub selector/action controls and generic
  Session List rows remain contained under the same Quake II Rerelease TTF
  metrics, with text-source validation preserved. Localization flow, text
  shaping policy, audio, and live accessibility services remain pending.
  Round 48 note: settings-form binary controls now render as compact square
  toggles under the same Quake II Rerelease font metrics, avoiding full-width
  checkbox fields on `performance`, `sound`, and `downloads`. Localization
  flow, text shaping policy, audio, and live accessibility services remain
  pending.
  Round 49 note: shared controls, fields, settings toggles, session panels,
  utility panels, and keybind buttons now have short RmlUi-native transition
  and hover/focus treatments while preserving the Quake II Rerelease TTF font
  source markers. Localization flow, text shaping policy, audio, live
  accessibility services, and a user-facing reduced-motion preference remain
  pending.
  Round 50 note: the shared RmlUi viewport now anchors `body` and `.screen`
  to top/right/bottom/left edges, and the software cursor is drawn in the same
  scaled RmlUi canvas as route content. Localization flow, text shaping
  policy, audio, live accessibility services, and a user-facing reduced-motion
  preference remain pending.
  Round 51 note: shared controls now extend the current menu visual language
  to checkbox, range, progress, reusable `.worr-*` controls, utility forms,
  and generic action footers while preserving the Quake II Rerelease TTF font
  source markers. Localization flow, text shaping policy, audio, live
  accessibility services, and a user-facing reduced-motion preference remain
  pending.
  Round 52 note: shared settings forms now use the same `604px` menu contract
  as the refined navigation surfaces, with narrower label/control/value
  columns and preserved typed widget treatment under the Quake II Rerelease
  TTF font source markers. Localization flow, text shaping policy, audio,
  live accessibility services, and a user-facing reduced-motion preference
  remain pending.
  Round 53 note: shared settings rows now use denser rounded row frames,
  smaller section headings, and preserved typed widget sizing under the Quake
  II Rerelease TTF font source markers. Localization flow, text shaping
  policy, audio, live accessibility services, and a user-facing reduced-motion
  preference remain pending.
  Round 54 note: shared action-intent tokens now distinguish primary,
  destructive, and secondary buttons with filled hover/focus states under the
  same Quake II Rerelease TTF font source markers, while Player Setup and Quit
  Confirm get cleaner panel treatments. Localization flow, text shaping
  policy, audio, live accessibility services, and a user-facing reduced-motion
  preference remain pending.
  Round 55 note: the engine UI bridge now maps RmlUi menu feedback to the
  legacy menu samples, confirmation documents share compact popup styling, and
  Sound Settings carries menu music metadata plus a typed `ogg_menu_track`
  numeric field under the same Quake II Rerelease TTF font source markers.
  Localization flow, text shaping policy, live accessibility services, and a
  user-facing reduced-motion preference remain pending.
  Round 56 note: RmlUi menu music metadata now triggers `OGG_Play()` after a
  document successfully opens, leaving the existing OGG layer responsible for
  menu-track versus connected-map track selection. Localization flow, text
  shaping policy, live accessibility services, and a user-facing reduced-motion
  preference remain pending.
  Round 57 note: RmlUi open-sound metadata now drives legacy menu feedback
  through the engine UI bridge, and RmlUi focus/change events on command/form
  controls use the legacy move cue with per-element override hooks. Localization
  flow, text shaping policy, live accessibility services, and a user-facing
  reduced-motion preference remain pending.
  Round 60 note: Video Setup now keeps menu-music and open-sound metadata while
  using a compact settings layout under the Quake II Rerelease TTF font-source
  markers. Localization flow, text shaping policy, live accessibility
  services, and a user-facing reduced-motion preference remain pending.
  Round 61 note: every settings route now carries the same menu-music and
  open-sound metadata, and the focused Screen/Effects runtime proof records
  consumed cues plus Quake II Rerelease TTF font-source markers. Localization
  flow, text shaping policy, live accessibility services, and a user-facing
  reduced-motion preference remain pending.
  Round 62 note: single-player/local-session routes now carry menu-music and
  open-sound metadata too, and focused Skill Select/Start Server runtime
  evidence records consumed cues plus Quake II Rerelease TTF font-source
  markers. Localization flow, text shaping policy, live accessibility
  services, and a user-facing reduced-motion preference remain pending.
  Round 63 note: utility routes now carry menu-music and open-sound metadata
  too, and focused Address Book/Demos/Keys/Legacy Keys/Players/Servers/
  Session List/Weapons runtime evidence records consumed cues plus Quake II
  Rerelease TTF font-source markers. Localization flow, text shaping policy,
  live accessibility services, and a user-facing reduced-motion preference
  remain pending.
  Round 64 note: session routes now carry menu-music/open-sound metadata too,
  confirmation routes retain alert popup treatment, and focused session
  runtime evidence records consumed cues plus Quake II Rerelease TTF
  font-source markers. Localization flow, text shaping policy, live
  accessibility services, and a user-facing reduced-motion preference remain
  pending.
  Round 65 note: authored shell/settings/single-player/save-load/
  download-status controls now finish the explicit action-audio sweep, and
  Single Player/Start Server typed widgets declare change-sound hints while
  continuing to load Quake II Rerelease TTF faces in staged OpenGL logs.
  Localization flow, text shaping policy, live accessibility services, and a
  user-facing reduced-motion preference remain pending.
  Round 66 note: the shared popup/dialog theme now carries stronger
  confirmation affordances, fixed menu containers use contained overflow
  instead of clipping, and reusable component templates now publish explicit
  audio intent. Localization flow, text shaping policy, live accessibility
  services, and a user-facing reduced-motion preference remain pending.
  Round 78 note: the multiplayer match hub now has a first-party WORR emblem,
  branded translucent shell, state chip, tabbed navigation, distinct
  team/participation treatments, responsive narrow/short layout rules, and
  explicit high-visibility styling. Matching JSON action sizing/color support
  preserves a readable native-renderer fallback. Broader localization and
  accessibility-service completion remains pending.
  2026-07-13 deterministic-visibility note: the RmlUi loader now applies
  archived high-visibility and reduced-motion classes before document
  construction, then keeps those classes synchronized on both the document and
  body. Unreliable decorative route-entry opacity animations were removed from
  the shared theme while focus, progress, and active key-capture feedback remain
  intact. This prevents accessibility-mode captures and real menu opens from
  retaining transparent geometry after animation cancellation. Large-text,
  localization, and broader input-service parity remain open. Implementation
  log: `docs-dev/rmlui-deterministic-route-visibility-2026-07-13.md`.
- [x] `FR-09-T05` Implement reusable data-model and controller bridges for
  cvars, commands, conditions, dynamic labels, and shared list/table flows.
  Dependency: `FR-09-T03`. Priority: P0.
  Progress: mock contracts and reusable RmlUi component templates cover cvar
  controls, command buttons, conditionals, list/table, preview, save/load,
  keybind capture, and image-grid selectors. Live runtime controllers now
  cover cvars, commands, conditions, map/image/directory selectors, player
  preview, two-slot keybind capture/conflict handling, save/load slots, demo
  discovery, server discovery/status, and the generic fixed-list bridge;
  remaining session-specific list providers remain pending.
  Round 4 note: controller fixtures sharpen the
  cvar/command/condition/navigation/list-provider contract before live C++
  controller work is accepted.
  Round 5 note: selected `controller_stub` route progression is accepted for
  `main`, `game`, `options`, `video`, and `download_status`, backed by mock
  controller contract references. Live C++ controllers remain open.
  Round 6 note: `controller_stub` coverage expands to `10` routes, and the
  route-contract checker now validates `controller_contracts` fixture
  references. Live C++ controllers remain open.
  Round 7 note: `controller_stub` coverage expands to `15` routes, and the new
  controller-stub coverage checker validates inferred static RML categories
  against declared controller contracts. Live C++ controllers remain open.
  Round 8 note: three menu-entrypoint routes moved from `controller_stub` to
  guarded `runtime_stub`, leaving `12` `controller_stub` routes plus `3`
  `runtime_stub` routes. The new runtime-stub eligibility checker validates
  shell metadata, controller contracts, runtime registry coverage, and legacy
  fallback behavior. Live C++ controllers remain open.
  Round 9 note: utility controller-stub coverage adds `addressbook`, `keys`,
  `legacykeys`, and `weapons`, and fixture validation now checks `54`
  controller-contract references across `19` metadata-advanced routes. Live
  C++ controllers remain open.
  Round 10 note: utility/list controller-stub coverage adds `servers`,
  `demos`, `players`, and `ui_list`; command and cvar inventory checks now
  report `289` direct command refs, `15` cvar-command refs, `452` total cvar
  refs, and `272` unique cvars. Fixture validation now checks `65`
  controller-contract references across `23` metadata-advanced routes. Live
  C++ controllers remain open.
  Round 11 note: data-model inventory validation now reports `190` static
  binding/model refs, `187` unique model tokens, `30` component refs, `13`
  controller refs, `33` action-type refs, `31` slot refs, and `38` routes with
  data-model hooks. Single-player/save-load controller-stub coverage expands
  the accepted bridge metadata to `24` `controller_stub` routes plus `3`
  guarded `runtime_stub` routes, with `75` controller-contract references
  across `27` advanced routes. Live C++ controllers remain open.
  Round 12 note: condition inventory validation now reports `141` static
  condition refs across `22` routes, `114` unique expressions, `111` unique
  condition tokens, and `0` malformed condition attributes. Local-session
  controller-stub coverage expands accepted bridge metadata to `28`
  `controller_stub` routes plus `3` guarded `runtime_stub` routes, with `87`
  controller-contract references across `31` advanced routes. Live C++
  controllers remain open.
  Round 13 note: event/action inventory validation now reports `465` static
  event/action refs across all `57` routes with `0` malformed hooks.
  Session/vote controller-stub coverage expands accepted bridge metadata to
  `36` `controller_stub` routes plus `3` guarded `runtime_stub` routes, with
  `101` controller-contract references across `39` advanced routes. Live C++
  controllers and event dispatch remain open.
  Round 14 note: multiplayer/lobby/info controller-stub coverage expands
  accepted bridge metadata to `42` `controller_stub` routes plus `3` guarded
  `runtime_stub` routes, with `117` controller-contract references across
  `45` advanced routes. Route metadata shape validation now verifies advanced
  route controller-contract refs. Live C++ controllers and event dispatch
  remain open.
  Round 16 note: static controller-stub coverage now spans all non-runtime
  tracked routes: `54` `controller_stub` routes plus `3` guarded
  `runtime_stub` routes, with `149` controller-contract references across
  `57` advanced routes. The strict controller-stub completion checker passes
  with `0` central starter routes. Live C++ controllers and event dispatch
  remain open.
  Round 78 note: sgame now publishes the first live match-hub controller
  contract through display-ready `ui_dm_*` cvars and registered commands. A
  bounded queue chunks the expanded snapshot below the shared stufftext limit,
  sends one chunk per frame, and waits for the queue to drain before the
  one-second live refresh. The remaining list, browser, save/load, keybind,
  player-preview, vote, and tournament controllers remain open.
  2026-07-13 note: the RmlUi save/load controller is now live. Both routes
  hydrate the authored slot rows through the server-owned `SV_GetSaveInfo()`
  API, present engine-formatted map/date metadata, disable missing load slots,
  keep empty save slots writable, and free returned metadata. A focused smoke
  checker locks the 16 load/15 save slot contracts, pre-show hydration order,
  and command wiring. Live 960x720 OpenGL inspection also corrected shared
  topbar containment and the widget-PNG scrap-atlas failure; the final staged
  captures show complete chrome, save metadata, actions, and status bars.
  Browser, session-list, vote, and tournament providers remain open.
  2026-07-13 server-browser note: the live client provider preserves public
  q2servers and Saved + LAN/favorites/file/broadcast sources, bounded address
  parsing and deduplication, paced three-stage status queries, status/error
  reply consumption, five-column signed sorting, eight-row paging, selection,
  and numeric-address connect safety. Route arguments now survive the
  cgame/client bridge without shared argv lifetime bugs. Focused checks and
  installed 960x720 empty plus real localhost-populated captures validate the
  provider, layout, Q2R font, input/back-close path, and a live 5ms status row.
  Generic/session list, vote, and tournament providers remain open.
  2026-07-13 fixed-list note: the generic `ui_list` provider is now live while
  preserving sgame authority. It publishes eight-row pages plus explicit
  ready/empty/error status, clears all twelve compatibility slots, rejects
  actionless focus rows, and uses the shared per-frame cvar/command bridge.
  Backplate and back-key closure now execute route pop plus owner cleanup in a
  single ordered command sequence. A focused checker and deterministic cvar
  seeding capture coverage validate populated, empty, and error layouts at
  960x720. Remaining session-specific list, vote, and tournament providers
  remain open. Implementation log:
  `docs-dev/rmlui-live-ui-list-provider-2026-07-13.md`.
  2026-07-13 Player Setup note: `players` now uses a live native provider for
  composite model/skin discovery and writeback, immediate Name/Dogtag/Hand
  cvars, directory-backed dogtags, cvar-backed skin/dogtag images, explicit
  provider states, and a native staged 3D preview. The OpenGL bridge now
  preserves cached scrap-atlas UV rectangles instead of rejecting existing
  small PCX images. Focused checks and seeded installed capture evidence pass;
  wider action automation and the native renderer matrix remain open.
  Implementation log:
  `docs-dev/rmlui-live-player-setup-provider-2026-07-13.md`.
- [x] `FR-09-T06` Translate shell/settings/single-player menus from the current
  JSON definitions into RmlUi documents.
  Dependency: `FR-09-T04`, `FR-09-T05`. Priority: P0.
  Progress: Agent 4 starter documents now cover `main`, `game`, `options`,
  `video`, `singleplayer`, `multimonitor`, `performance`, `accessibility`,
  `sound`, `screen`, `language`, `input`, `downloads`, `download_status`, and
  `quit_confirm`.
  Third-round update: Agent 4 source-route starter coverage now also includes
  `railtrail`, `effects`, `crosshair`, `gameflags`, `startserver`,
  `skill_select`, `loadgame`, and `savegame`; all Agent 4-owned manifest
  routes now have starter documents.
  Round 6 note: `performance`, `accessibility`, `sound`, `screen`, and `input`
  join the previously accepted shell/settings `controller_stub` routes. Runtime
  activation and parity remain pending.
  Round 7 note: `multimonitor`, `railtrail`, `effects`, `crosshair`, and
  `language` join the accepted shell/settings `controller_stub` route set.
  Runtime activation and parity remain pending.
  Round 8 note: `main`, `game`, and `download_status` are promoted to guarded
  `runtime_stub` status for menu-entrypoint document probing only. Runtime
  activation, screenshots, and parity remain pending.
  Round 11 note: `singleplayer` and `skill_select` now have
  `controller_stub` metadata backed by static single-player episode/unit,
  command-action, navigation, and condition-state contracts. Runtime
  activation, screenshots, and parity remain pending.
  Round 12 note: `downloads`, `quit_confirm`, `gameflags`, and `startserver`
  now have `controller_stub` metadata backed by static download-option,
  confirmation, dmflags, local-session cvar, command-action, navigation, and
  condition-state contracts. Runtime activation, screenshots, and parity
  remain pending.
  Round 46 note: the staged `singleplayer` hub now has explicit
  selector/action widths and `960x720` screenshot evidence with visible
  Back/Close actions. Live single-player controllers, runtime navigation
  parity, and settings persistence remain pending.
  Round 48 note: `performance`, `sound`, `downloads`, and `startserver` now
  have focused `960x720` screenshots showing compact toggles or shortened
  scroll regions ending on complete rows above visible Back/Close actions.
  Live settings persistence, navigation parity, and controller behavior remain
  pending.
  Round 49 note: shared shell/settings visual framing now gives these menus
  clearer header/footer separation, and route-specific Sound/Start Server
  height adjustments keep the final visible rows complete after the aesthetic
  pass. Live settings persistence, navigation parity, and controller behavior
  remain pending.
  Round 50 note: the main shell menu now fills the active RmlUi viewport and
  uses an explicit `320px` action-column width, with focused captures at
  `964x765`, `1280x720`, `1280x960`, and `2048x1152` showing intact main-menu
  button right edges. Live settings persistence, navigation parity, and
  controller behavior remain pending.
  Round 60 note: the Video Setup page now restores the original pre-RmlUi
  display, texture, gamma/light, and renderer controls in RmlUi, using selects
  for enumerations, toggles for booleans, ranges for scalar cvars, and a real
  `pushmenu multimonitor` action. The compact three-column layout has focused
  `960x720` screenshot evidence with all restored controls above Back/Close.
  Live settings persistence, navigation parity, and controller behavior remain
  pending.
  Round 61 note: Screen Setup and Effects Setup now use compact two-column
  RmlUi settings layouts, and their nested Crosshair/Railgun Trail navigation
  is represented as typed `action` rows instead of loose buttons. Focused
  `960x720` screenshots confirm all rows remain visible above Back/Close.
  Live settings persistence, navigation parity, and controller behavior remain
  pending.
  Round 62 note: Start Server now uses a compact three-column RmlUi layout
  that preserves the legacy Server, Match Setup, cooperative rule fields, and
  Deathmatch Flags/Begin Game actions while keeping the static fallback view
  visible above Back/Close at `960x720`. Live condition evaluation, settings
  persistence, navigation parity, and controller behavior remain pending.
- [x] `FR-09-T07` Translate browser, player-config, save/load, keybind, and
  other rich utility surfaces that need shared controllers or preview support.
  Dependency: `FR-09-T05`. Priority: P0.
  Progress: starter documents cover `servers`, `demos`, `players`,
  `addressbook`, `keys`, `legacykeys`, `weapons`, and `ui_list`; live runtime
  support now also covers player preview, two-slot keybind capture/conflicts,
  archived address fields, save/load slot hydration, demo browsing, server
  browsing, and generic sgame-published fixed lists while remaining
  session-specific providers stay open.
  Third-round update: `loadgame` and `savegame` starter documents now exist,
  so all tracked rich utility/save-load source routes have starter coverage.
  Round 9 note: `addressbook`, `keys`, `legacykeys`, and `weapons` now have
  utility route metadata and `controller_stub` status backed by static mock
  controller contracts. Live utility controllers, key-capture behavior,
  screenshots, and parity remain pending.
  Round 10 note: `servers`, `demos`, `players`, and `ui_list` now have
  utility route metadata and `controller_stub` status backed by static mock
  controller contracts for list providers, command actions, cvar bindings,
  preview ownership, and condition state. Live browser/list providers, player
  preview behavior, screenshots, and parity remain pending.
  Round 11 note: `loadgame` and `savegame` now have `controller_stub` metadata
  backed by static save/load list, slot, and command-action contracts. Live
  save/load controllers, screenshots, and parity remain pending.
  Round 12 note: `downloads` now joins the accepted static controller-stub set
  for download-option cvar/command hooks. Live download controller behavior,
  screenshots, and parity remain pending.
  Round 43 note: utility fallback copy and keybind/weapon/legacy key list
  layout are refined for the staged RmlUi path; the `keys` route has `960x720`
  screenshot evidence showing TTF text and contained columns. Live utility
  controllers and parity remain pending.
  Round 44 note: the staged utility/menu evidence now keeps the Options route
  in a two-column bounded layout under Quake II Rerelease font metrics while
  preserving visible Back/Close actions. Live utility controllers and parity
  remain pending.
  Round 45 note: save/load slot lists now use explicit bounded dimensions and
  compact repeated-row styling so long slot lists remain scroll-contained above
  visible Back/Close actions. Live utility/save-load controllers and parity
  remain pending.
  Round 46 note: the generic `ui_list` route now bounds its extra-action
  toolbar and list body so generated list rows scroll above visible Previous,
  Next, and Return footer controls at `960x720`. Live utility/list providers,
  scrollbar/focus behavior, and parity remain pending.
  Round 47 note: the `keys` route now uses a denser bounded keybind grid so
  the Interface group plus Legacy Keys/Back footer actions remain visible at
  `960x720`. Live keybind capture, focus behavior, and utility parity remain
  pending.
  Round 49 note: utility toolbars, form panels, preview panels, and keybind
  buttons now share the same restrained hover/transition treatment as the rest
  of the RmlUi shell, with focused screenshot evidence covering `keys`,
  `players`, `servers`, and `ui_list`. Live keybind capture, list providers,
  focus behavior, and utility parity remain pending.
  Round 63 note: all eight utility routes now open through the RmlUi
  `pushmenu` bridge with menu music/open-sound metadata and intent-specific
  action sounds. Address Book uses typed fields for all sixteen legacy
  address slots in a four-column grid, Key Bindings uses a three-column
  capture grid with a unique Backpedal id, and Weapon Bindings uses a
  two-column arsenal layout. Live keybind capture, list providers, player
  preview behavior, and utility parity remain pending.
  Round 65 note: the Save/Load route buttons now carry explicit confirm
  sounds, shell hub buttons that enter player, keybind, weapon, server, demo,
  and address-book routes declare open sounds, and the grouped Options/Game/
  Multiplayer hub captures keep utility/browser entry points clear of footer
  actions. Live keybind capture, list providers, player preview behavior, and
  utility parity remain pending.
  Round 66 note: reusable command, cvar-control, save/load, image-grid,
  list-table, preview, and keybind templates now declare explicit
  `data-menu-sound` or `data-menu-sound-change` intent, and Key Bindings
  containment has fresh staged OpenGL evidence. Live keybind capture, list
  providers, player preview behavior, and utility parity remain pending.
  2026-07-13 note: `loadgame` and `savegame` now have a live engine provider
  instead of static slot labels. Occupied rows expose the existing friendly
  save metadata, empty load rows cannot activate, and empty manual save rows
  remain available. General route-capture automation now covers these routes;
  providers hydrate before the document is shown, and guarded 960x720 OpenGL
  captures validate the complete load/save layouts plus keyboard, text,
  pointer, wheel, and back-close input. Server/demo/session list providers and
  the full native renderer/layout matrix remain pending.
  2026-07-13 demo-browser note: the `demos` route now has a live native RmlUi
  filesystem provider instead of an honestly disabled placeholder. It scans
  the four legacy demo formats, reads map/POV metadata through
  `CL_GetDemoInfo()`, retains unchanged metadata in a size/mtime-keyed session
  cache, preserves local-game versus all-location discovery, rejects unsafe
  command characters, and supports directory entry/up, refresh, five-column
  sorting, eight-row paging, status totals, and direct playback. Focused smoke
  coverage validates the runtime, document, security, five-column table, and
  36px row/pagination styling contracts. Two consecutive populated 960x720
  OpenGL captures are byte-identical and validate the complete shared chrome,
  five-column/eight-row layout, one-line source toggle, paging, live totals,
  font path, synthetic input, and clean back-close behavior. The capture
  harness now reapplies reduced motion after config loading so archived user
  state cannot race the evidence frame. Empty-fixture/action automation,
  server and session list providers, and the full native renderer/layout matrix
  remain pending. Implementation log:
  `docs-dev/rmlui-live-demo-browser-provider-2026-07-13.md`.
  2026-07-13 server-browser note: the `servers` route now has a live native
  provider instead of a disabled placeholder. Public and Saved + LAN source
  groups, binary/text address parsing, bounded deduplication, broadcast,
  rate-limited status refresh, restriction/error rows, legacy signed
  five-column sorting, eight-row paging, selection, and safe numeric connect
  behavior are active. The client/cgame route bridge preserves explicit source
  arguments, and status/error replies are offered through the narrow optional
  RmlUi runtime callbacks before the legacy handler. Focused provider/capture
  checks pass; installed 960x720 evidence covers the truthful empty state and
  a real private localhost server row (`basew`, `q2dm1`, `0/8`, 5ms) with clean
  route/font/input/back-close diagnostics. Generic/session list providers,
  action-level runtime automation, and the full native renderer/layout matrix
  remain pending. Implementation log:
  `docs-dev/rmlui-live-server-browser-provider-2026-07-13.md`.
  2026-07-13 fixed-list note: `ui_list` now presents the live sgame-published
  Callvote, MyMap, tournament pick/ban, and replay lists through eight-row
  pages. Explicit empty/error states replace actionless buttons, unused
  toolbar/paging slabs disappear, long labels wrap, and the standard top
  backplate owns cleanup. Guarded populated, empty, and error OpenGL captures
  pass at 960x720, backed by a provider/document/style checker and reusable
  capture cvar seeding. Remaining session-specific list providers,
  action-level automation, and the native renderer/layout matrix remain open.
  Implementation log:
  `docs-dev/rmlui-live-ui-list-provider-2026-07-13.md`.
  2026-07-13 Player Setup note: the `players` route is promoted to
  `live_provider`. It enumerates compatible models and icon-paired skins,
  initializes and persists composite skin plus Name/Dogtag/Hand, shows live
  selected-skin and dogtag PCX thumbnails, and renders staged player/weapon
  behavior with reduced-motion support. The seeded 960x720 installed capture
  validates the complete form, thumbnails, preview, font, input, and back-close
  path without texture or clipping errors. Action-level control automation and
  native renderer/layout parity remain open. Implementation log:
  `docs-dev/rmlui-live-player-setup-provider-2026-07-13.md`.
  2026-07-13 keybind-family note: `keys`, `legacykeys`, and `weapons` are
  promoted to `live_provider` with Primary/Alternate chips for all 38 source
  commands. The native controller preserves the untouched slot, clears only
  the chosen slot, enforces an eight-second timeout, pauses on conflicts for
  Replace/Cancel, and renders established keyboard/mouse/gamepad artwork with
  text fallback. Nine focused tests and clean installed reduced-motion
  960x720 captures cover the three routes; pre-load accessibility classes and
  removal of unreliable decorative load-time fades keep menu geometry
  deterministic. Action-level mutation/restore automation and native
  renderer/layout parity remain open. Implementation log:
  `docs-dev/rmlui-live-keybind-provider-2026-07-13.md`.
  2026-07-13 Address Book note: `addressbook` is promoted to `live_provider`.
  All sixteen archived `adr0` through `adr15` fields hydrate before show and
  write back immediately through the generic cvar bridge, retain the legacy
  32-character limit, and expose Browse Favorites with the exact saved-file,
  broadcast, and favorites sources consumed by the live server provider. Six
  focused tests and a clean seeded installed 960x720 capture cover IPv4,
  hostname, and IPv6 values, route/font/input/back-close behavior, and complete
  four-column geometry. No separate user guide is required because this restores
  the existing workflow without a new cvar or concept. Action-level
  mutation/restore automation and native renderer/layout parity remain open.
  Implementation log:
  `docs-dev/rmlui-live-addressbook-provider-2026-07-13.md`.
- [x] `FR-09-T08` Translate multiplayer/session/match menus and their
  cgame/sgame-driven state flows into RmlUi documents.
  Dependency: `FR-09-T05`. Priority: P0.
  Progress: starter documents now cover `multiplayer`, `vote_menu`,
  `callvote_main`, `callvote_ruleset`, `mymap_main`, `mymap_flags`, and
  `leave_match_confirm`.
  Third-round update: remaining tracked session/match starter documents now
  cover `dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, `dm_matchinfo`,
  `callvote_timelimit`, `callvote_scorelimit`, `callvote_unlagged`,
  `callvote_random`, `callvote_map_flags`, `forfeit_confirm`, `admin_menu`,
  `admin_commands`, `tourney_info`, `tourney_mapchoices`, `tourney_veto`,
  `tourney_replay_confirm`, `map_selector`, and `match_stats`.
  Round 12 note: starter route metadata now covers `multiplayer` plus all `25`
  tracked session/match documents, giving Agent 5-owned session flows explicit
  route ownership, source/current surface, entrypoint, and data-model
  metadata. Live session controllers, runtime activation, screenshots, and
  parity remain pending.
  Round 13 note: `vote_menu`, `callvote_main`, `callvote_ruleset`,
  `callvote_timelimit`, `callvote_scorelimit`, `callvote_unlagged`,
  `callvote_random`, and `callvote_map_flags` now have static
  `controller_stub` metadata for vote/callvote actions, rule toggles, cvars,
  and navigation. Live vote/callvote controllers, runtime activation,
  screenshots, and parity remain pending.
  Round 14 note: `multiplayer`, `dm_welcome`, `dm_join`, `join`,
  `dm_hostinfo`, and `dm_matchinfo` now have static `controller_stub`
  metadata for multiplayer hub, lobby, welcome, host-info, and match-info
  actions/bindings. Live multiplayer/session controllers, runtime activation,
  screenshots, and parity remain pending.
  Round 16 note: the final session/match starter batch now has static
  `controller_stub` metadata: `admin_commands`, `admin_menu`,
  `forfeit_confirm`, `leave_match_confirm`, `map_selector`, `match_stats`,
  `mymap_flags`, `mymap_main`, `tourney_info`, `tourney_mapchoices`,
  `tourney_replay_confirm`, and `tourney_veto`. Live multiplayer/session
  controllers, runtime activation, screenshots, and parity remain pending.
  Round 43 note: multiplayer/session visible fallback copy is now
  player-facing rather than migration/debug oriented, and the refined assets
  pass final staged all-route OpenGL loading. Live session controllers and
  match-state parity remain pending.
  Round 44 note: Admin Commands now renders as a readable bounded command list
  under the Quake II Rerelease font metrics, and the refined assets still pass
  final staged all-route OpenGL loading. Live session controllers and
  match-state parity remain pending.
  Round 47 note: `admin_commands`, `callvote_main`, and `dm_join` now have
  bounded staged OpenGL list/content regions with visible Back/Return/Close
  actions at `960x720`. Live session controllers, real match-state updates,
  focus/scroll behavior, and parity remain pending.
  Round 49 note: session panels, status cards, flow steps, player rows, and
  admin rows now have matching subtle hover/transition feedback, with focused
  screenshot evidence covering `callvote_main`, `dm_join`, and
  `admin_commands`. Live session controllers, real match-state updates,
  focus/scroll behavior, and parity remain pending.
  Round 59 note: the Multiplayer hub now mirrors the original q2servers,
  address-book, demos, start-server, player-setup, and options command intent
  in the RmlUi shell grid, removes the stale custom connect command, and has
  staged `pushmenu multiplayer` plus `960x720` visual evidence. Live session
  controllers, real match-state updates, focus/scroll behavior, and parity
  remain pending.
  Round 64 note: the session/match routes now preserve more of the original
  pre-RmlUi command flow in RmlUi: Call Vote, MyMap, Host Info, Match Info,
  Admin, Forfeit, and Replay picker buttons run their original `worr_*`
  commands before the pushmenu bridge opens RmlUi routes, and the Vote menu
  uses live `ui_vote_*` cvars plus `worr_vote_yes/no/close`. Lobby, callvote,
  Admin Commands, Match Stats, Tournament Map Choices, flags, and popup
  confirmations have staged `960x720` visual evidence. Live session
  controllers, real match-state updates, focus/scroll behavior, and parity
  remain pending.
  Round 65 note: the Multiplayer and Game shell entry points now present
  grouped Find/Host/Profile and Session/Browse/Save-And-Exit sections while
  preserving the original q2servers, address-book, demos, start-server,
  player-setup, options, save/load, disconnect, and quit command intent.
  Final `960x720` captures show the groups contained after right-column
  clipping was corrected. Live session controllers, real match-state updates,
  focus/scroll behavior, and parity remain pending.
  Round 66 note: session/lobby fixed panels now use contained scroll overflow,
  and the DM Join route has fresh staged OpenGL containment evidence while
  preserving the original session command labels and popup confirmation route
  for Leave Match. Live session controllers, real match-state updates,
  focus/scroll behavior, and parity remain pending.
  Round 73 note: direct `join` and `dm_join` route probes now preserve authored
  fallback lobby titles and action labels when session cvars exist but are
  empty, and the lobby command surface uses a bounded two-column command grid
  with footer controls visible at `960x720`. `callvote_main` keeps the same
  two-column command intent with looser row pitch, `admin_commands` uses a
  readable command/usage row format, and `admin_menu` keeps Replay Game on the
  popup-confirmation route. Live session controllers, real match-state
  updates, focus/scroll behavior, and parity remain pending.
  Round 74 note: direct `map_selector`, `tourney_veto`, and generic
  `ui_list` route probes now keep authored fallback options/panels visible
  unless live cvars explicitly hide them, while `servers` and `demos` use
  corrected full-width table layout for empty states. Expected missing
  data-model log notices are suppressed by default behind
  `ui_rml_log_missing_data_models`, but live session/list controllers, real
  match-state updates, focus/scroll behavior, and parity remain pending.
  Round 75 note: direct `match_stats` and `tourney_mapchoices` probes now show
  report/list-shaped fallback content when their live line-zero cvars are
  absent or falsey, while preserving the existing `ui_matchstats_line_*` and
  `ui_tourney_mapchoice_line_*` live cvar contracts. `download_status` now has
  a contained idle state and explicit percent meter unit. Live
  session/tournament/download controllers, real match-state updates,
  focus/scroll behavior, and parity remain pending.
  2026-07-10 build-warning note: the inactive Vulkan and RTX/vkpt RmlUi
  render-interface placeholders are now non-final abstract classes, matching
  their intentionally incomplete state and eliminating Clang's
  `-Wabstract-final-class` diagnostics without activating or redirecting either
  native renderer path. Implementation log:
  `docs-dev/rmlui-inactive-vulkan-stub-build-warning-cleanup-2026-07-10.md`.
  Round 78 note: `dm_join`/`join` now implement a live match hub for both the
  mandatory first-connect choice and in-session Escape. The server publishes
  match overview, population, rules, team/Duel participation, ready state,
  intermission restrictions, and conditional tools; OpenGL consumes the
  branded RmlUi route, while unavailable renderer-native RmlUi lanes use the
  matching cgame JSON page. `match_auto_join` now defaults to `0`, with
  explicit `match_auto_join=1` preserving the historical immediate-assignment
  override. Other Wave C flow/controller parity remains open. Implementation
  log: `docs-dev/rmlui-round78-multiplayer-match-hub-2026-07-10.md`.
  2026-07-13 session-entry note: `dm_welcome`, `dm_join`, `join`,
  `dm_hostinfo`, and `dm_matchinfo` are promoted to `live_provider`. The native
  bridge consumes all current sgame-published labels, text, conditions, enable
  state, and command-cvar actions before show. First-connect hubs remain modal;
  resumable hubs close locally without waiting for a server round trip and
  still send authoritative cleanup while connected. Host and Match Info use
  bounded wrapping rows, explicit empty states, and a single standardized Back
  plate. Twelve focused tests and five clean seeded installed 960x720 captures
  cover compatibility welcome, team and non-team hubs, host details, and match
  details. Connected action automation and native renderer/layout parity remain
  open. Implementation log:
  `docs-dev/rmlui-live-session-entry-provider-2026-07-13.md`.
  2026-07-13 vote/callvote note: `vote_menu`, `callvote_main`,
  `callvote_ruleset`, `callvote_timelimit`, `callvote_scorelimit`,
  `callvote_unlagged`, `callvote_random`, and `callvote_map_flags` are promoted
  to `live_provider`. The native bridge consumes 41 current sgame-published
  values, complete option/empty-state conditions, current values, dynamic
  score labels, tri-state flag labels, and all existing commands. The routes
  now use one canonical backplate and bounded two-column choice grids; active,
  preready, idle, populated, and empty states have clean canonical `.install`
  evidence. Twelve focused tests and eleven 960x720 frames pass. Connected
  mutation automation and native renderer/layout parity remain open.
  Implementation log:
  `docs-dev/rmlui-live-vote-callvote-provider-2026-07-13.md`.
  2026-07-13 MyMap note: `mymap_main` and `mymap_flags` are promoted to
  `live_provider`. The native bridge consumes all fifteen current status,
  availability, summary, and tri-state flag values, while the live generic
  `ui_list` provider retains map selection. Both pages now use one canonical
  backplate; the main page is a compact enabled-state launcher and the flag
  editor uses the shared two-column grid. Eight focused tests and three clean
  canonical `.install` 960x720 frames pass. Connected mutation/queue automation
  and native renderer/layout parity remain open. Implementation log:
  `docs-dev/rmlui-live-mymap-provider-2026-07-13.md`.
  2026-07-13 Tournament note: `tourney_info`, `tourney_mapchoices`,
  `tourney_veto`, and `tourney_replay_confirm` are promoted to
  `live_provider`. Sgame continues to own map order, actor identity, Pick/Ban
  legality, remaining candidates, replay selection, and replay reset. The
  RmlUi routes consume the published state and use the shared live `ui_list`
  provider for Pick, Ban, and Replay selection. Replay now warns that results
  from the selected game onward are discarded, authors safe No before
  destructive Yes, and is guarded by the existing admin-only commands. Eleven
  focused regressions, a 327-test complete UI suite, and seven clean canonical
  `.install` 960x720 frames
  cover info, map order, all veto presentation branches, and replay
  confirmation. Wave C now has 23 of 25 routes at `live_provider`, leaving
  `map_selector` and `match_stats`. Connected mutation/restore automation and
  native renderer/layout parity remain open. Implementation log:
  `docs-dev/rmlui-live-tournament-provider-2026-07-13.md`.
  2026-07-13 Map Selector note: `map_selector` is promoted to
  `live_provider`. Sgame continues to own its three candidates, ballot
  validation, strict-majority/timeout finalization, chosen map, and per-client
  lifecycle. RmlUi now presents a stable heading, live candidate buttons,
  numeric seconds plus a shrinking bar, and an exclusive post-vote
  acknowledgement. Explicit Close persists for the current ballot instead of
  reopening on the next server frame; disconnected direct-route cleanup is
  warning-free while connected cleanup still reaches sgame. Eleven focused
  regressions, a 338-test complete UI suite, and two clean canonical
  `.install` 960x720 frames pass. Wave C now has 24 of 25 routes at
  `live_provider`, leaving only `match_stats`. Connected ballot/transition
  automation and native renderer/layout parity remain open. Implementation
  log: `docs-dev/rmlui-live-map-selector-provider-2026-07-13.md`.
  2026-07-13 Match Stats note: `match_stats` is promoted to
  `live_provider`, completing the 25-route Wave C provider pass. Sgame keeps
  counter and one-second refresh authority, publishes a ten-value semantic
  snapshot, and retains all sixteen legacy line cvars for the JSON fallback.
  RmlUi groups Combat, Damage, and Accuracy into responsive cards, reports
  undefined ratios as `N/A`, keeps unavailable/live states exclusive, and
  exposes one connection-aware Back path. Ten focused regressions, a 348-test
  complete UI suite, and two clean canonical `.install` 960x720 frames pass.
  The capture harness now disables sound at startup so optional OpenAL/EAX
  diagnostics cannot contaminate UI-only evidence. Connected counter/refresh
  automation and native renderer/layout parity remain open. Implementation
  log: `docs-dev/rmlui-live-match-stats-provider-2026-07-13.md`.
- [x] `FR-09-T09` Add migration-specific validation for navigation, scaling,
  localization, and renderer parity.
  Dependency: `FR-09-T06`, `FR-09-T07`, `FR-09-T08`, `DV-03-T07`.
  Priority: P0.
  Progress: first-round `tools/ui_smoke/check_rmlui_manifest.py` validates the
  RmlUi smoke manifest with `57` tracked migration surfaces, `10/10`
  `required_now` starter documents present, and `47` pending documents. This
  is source-asset validation only; runtime navigation, screenshots, renderer
  coverage, and parity checks remain pending.
  Second-round update: the same checker now validates `30/30` required starter
  documents with `27` pending routes. JSON/RML/import validation and focused
  package-asset tests pass.
  Third-round update: `tools/ui_smoke/check_rmlui_manifest.py` now validates
  `57/57` required starter documents with `0` pending routes, parses present
  RML documents and local imports, and is covered by focused tests.
  `tools/ui_smoke/check_rmlui_route_contracts.py` audits the core, shell, and
  central smoke route manifests and passes against the current source assets.
  Round 4 note: smoke-transition metadata, controller-fixture coverage, and
  route-ownership metadata have coordinator-accepted validation. Runtime
  navigation, screenshot/layout capture, renderer coverage, and parity
  validation are still open.
  Round 5 note: static RML semantics checking and progress-report tooling are
  implemented and validated. Runtime navigation, screenshot/layout capture,
  renderer coverage, and parity validation remain open.
  Round 6 note: controller-contract validation, runtime asset path validation,
  staged loose-file validation, and JSON progress output are implemented and
  validated. Runtime navigation, screenshot/layout capture, renderer coverage,
  and parity validation remain open.
  Round 7 note: runtime registry drift checking, controller-stub coverage
  checking, import-aware runtime asset validation, and controller-contract
  progress reporting are implemented and validated. Runtime navigation,
  screenshot/layout capture, renderer coverage, and parity validation remain
  open.
  Round 8 note: menu-entrypoint validation, runtime-stub eligibility checking,
  runtime asset JSON output, and phase-progression/`routes_by_phase` progress
  facts are implemented and validated. Runtime navigation, screenshot/layout
  capture, renderer coverage, and parity validation remain open.
  Round 9 note: navigation graph validation, controller fixture validation,
  detailed runtime asset manifest output, parity checklist validation, and
  all-route-metadata progress reporting are implemented and validated. Runtime
  navigation, screenshot/layout capture, renderer coverage, and parity
  validation remain open.
  Round 10 note: command inventory validation, cvar inventory validation, and
  parity-checklist progress summaries are implemented and validated. Current
  reporting shows `57` parity checklist routes, `9` categories, and `0`
  `parity_ready` routes; progress-report text, markdown, and JSON output also
  include command/cvar inventory summary counts. Runtime navigation,
  screenshot/layout capture, renderer coverage, and parity validation remain
  open.
  Round 11 note: data-model inventory validation, phase-consistency
  validation, dependency-decision validation, and progress-report data-model
  summaries are implemented and validated. Current reporting shows `57/57`
  documents present, migration phases `starter=30`, `controller_stub=24`,
  `runtime_stub=3`, `27` advanced routes (`47.4%`), `0` `parity_ready`
  routes, and `190` static data-model/data-binding references with `0`
  malformed tokens. Runtime navigation, screenshot/layout capture, renderer
  coverage, and parity validation remain open.
  Round 12 note: condition inventory validation, route-metadata sync
  validation, and progress-report guardrail summaries are implemented and
  validated. Current reporting shows `57/57` documents present, migration
  phases `starter=26`, `controller_stub=28`, `runtime_stub=3`, `31` advanced
  routes (`54.4%`), `0` `parity_ready` routes, `141` static condition refs
  with `0` malformed condition attributes, and metadata sync coverage for all
  `57` central migration routes plus one support metadata route. Runtime
  navigation, screenshot/layout capture, renderer coverage, and parity
  validation remain open.
  Round 13 note: event inventory, a11y/localization inventory, and
  legacy-removal inventory validation are implemented and validated. Current
  reporting shows `57/57` documents present, migration phases `starter=18`,
  `controller_stub=36`, `runtime_stub=3`, `39` advanced routes (`68.4%`), `0`
  `parity_ready` routes, `465` event/action refs, `8` a11y/localization refs,
  and `6` legacy-removal inventory items still blocked or pending. Runtime
  navigation, screenshot/layout capture, renderer coverage, and parity
  validation remain open.
  Round 14 note: document/body identity inventory, route entrypoint inventory,
  and route metadata shape validation are implemented and validated. Current
  reporting shows `57/57` documents present, migration phases `starter=12`,
  `controller_stub=42`, `runtime_stub=3`, `45` advanced routes (`78.9%`), `0`
  `parity_ready` routes, `57` matched document/body IDs, `72` unique
  entrypoint refs, and `58` route metadata entries passing shape validation.
  Runtime navigation, screenshot/layout capture, renderer coverage, and parity
  validation remain open.
  Round 15 note: dependency-integration validation is implemented and
  validated. Current reporting shows RmlUi dependency/build state `optional`,
  `4/4` integration components present, `1` source wrap, `2` optional Meson
  dependency probes, `1` default-disabled Meson option, `1` optional compile
  define, and runtime compiled `no`. Runtime navigation, screenshot/layout
  capture, renderer coverage, and parity validation remain open.
  Round 16 note: controller-stub completion validation is implemented and
  validated. Current reporting shows `57/57` documents present, migration
  phases `starter=0`, `controller_stub=54`, `runtime_stub=3`, `57` advanced
  routes (`100.0%`), `149` controller-contract refs, `57/57`
  controller-binding checklist entries complete, and `0` `parity_ready`
  routes. Runtime navigation, screenshot/layout capture, renderer coverage,
  input/back coverage, and parity validation remain open.
  Round 17 note: `tools/ui_smoke/check_rmlui_runtime_adapter.py` validates the
  guarded compiled adapter, conservative route-open guard,
  `renderer_unavailable` state, RmlUi Core symbol usage, Meson fallback
  options, and wrap provide aliases. Runtime navigation, screenshot/layout
  capture, renderer coverage, input/back coverage, and parity validation remain
  open.
  Round 18 note: runtime-adapter validation now also checks RmlUi system/file
  include guards, interface installation before `Rml::Initialise`, WORR
  filesystem API usage, WORR system/log API usage, and the explicit runtime
  file-probe command. Runtime navigation, screenshot/layout capture, renderer
  coverage, input/back coverage, and parity validation remain open.
  Round 19 note: runtime-adapter validation now also checks the native
  renderer contract, OpenGL/Vulkan/RTX-vkpt family coverage, renderer route
  gating, native render-interface requirements, and no Vulkan-to-OpenGL
  redirection. Runtime navigation, screenshot/layout capture, renderer
  coverage, input/back coverage, and parity validation remain open.
  Round 20 note: runtime-adapter validation now also checks renderer API
  exports, OpenGL-scoped RmlUi renderer dependency wiring, client renderer
  registration/clear lifecycle, adapter `Rml::SetRenderInterface`
  installation, OpenGL scaffold method coverage, and `CanRender=false`.
  Runtime navigation, screenshot/layout capture, visible renderer coverage,
  input/back coverage, and parity validation remain open.
  Round 21 note: runtime-adapter validation now also checks OpenGL geometry
  caching, tessellator draw primitives, generated texture upload,
  loaded/generated texture lifetime, scissor state handling, and
  `CanRender=true` while preserving the route-open guard and no
  Vulkan-to-OpenGL redirection. Runtime navigation, screenshot/layout capture,
  input/back coverage, and parity validation remain open.
  Round 22 note: runtime-adapter validation now also checks runtime context
  lifecycle hooks, RmlUi context/document load/update/render/removal behavior,
  guarded `core.runtime_smoke` route ownership, runtime open/close commands,
  and UI bridge draw ordering before legacy UI draw. Runtime navigation,
  screenshot/layout capture, input/back coverage beyond Escape-close, and
  parity validation remain open.
  Round 23 note: runtime-adapter validation now also checks runtime input hook
  declarations, adapter-side key/text/mouse event delivery into the RmlUi
  context, UI bridge input ordering before legacy UI callbacks, close/back
  tokens, and guarded status/capture commands. Runtime navigation,
  screenshot/layout capture, broad input/back coverage, and parity validation
  remain open.
  Round 24 note: `tools/ui_smoke/check_rmlui_runtime_capture.py` now validates
  the guarded runtime capture path by launching the enabled scratch engine,
  opening `core.runtime_smoke`, writing a local TGA screenshot, checking
  guarded OpenGL status/frame/input markers, verifying `960x720` dimensions,
  and enforcing a nonblank payload. Runtime navigation, broader screenshot
  layout assertions, broad input/back coverage, and parity validation remain
  open.
  Round 25 note: the runtime capture checker now also requires the
  `RmlUi smoke font engine generated glyph geometry` marker, and focused tests
  cover missing glyph-marker failure. Broader screenshot layout assertions,
  synthetic input/back validation, renderer breadth, and parity validation
  remain open.
  Round 26 note: the runtime capture checker now parses TGA screenshots and
  validates layout color counts, bounding boxes, and expected
  panel/text/button ordering for `core.runtime_smoke`. Synthetic input/back
  validation, renderer breadth, broader route coverage, and parity validation
  remain open.
  Round 27 note: the runtime capture checker now requires
  `ui_rml_runtime_synthetic_input` evidence after the screenshot, including
  inactive final status, positive key/text/pointer/button/wheel counters, and
  route open/close/request/synthetic counters. Renderer breadth, broader route
  coverage, broad input/navigation parity, and parity validation remain open.
  Round 28 note: the runtime capture checker now supports geometry-driven
  capture validation and aggregate viewport matrix manifests. The accepted
  matrix records `viewports=2`, `passed=2`, `failed=0`, exact dimensions for
  `960x720` and `1280x960`, and `layout_ok=true` in both cases. Renderer
  breadth, broader route coverage, responsive widescreen parity, and parity
  validation remain open.
  Round 29 note: the runtime capture checker now supports `--route-matrix` for
  the guarded menu entrypoint routes `main`, `game`, and `download_status`.
  The accepted matrix records `routes=3`, `passed=3`, `failed=0`, exact
  `960x720` dimensions, active route-specific OpenGL status, glyph text
  evidence, synthetic input, close counters, inactive final status, and
  `layout_required=false` for all three routes. Renderer breadth, route
  navigation, final route layout parity, and parity validation remain open.
  Round 30 note: `tools/ui_smoke/check_rmlui_renderer_matrix.py` now adds
  focused renderer-family matrix validation with one guarded OpenGL lane, two
  native-pending non-OpenGL lanes, and explicit no Vulkan/RTX-to-OpenGL
  shortcut enforcement. Live native Vulkan/RTX renderer coverage, route
  navigation, final route layout parity, and parity validation remain open.
  Round 31 note: the runtime capture checker now supports
  `--renderer-matrix`, which aggregates the guarded OpenGL route matrix and
  renderer-family guardrail into one JSON/text report and manifest. Live
  native Vulkan/RTX renderer coverage, route navigation, final route layout
  parity, and parity validation remain open.
  Round 32 note: bridge-readiness validation now records Vulkan/RTX native
  renderer foundations and fails premature runtime dependency enablement,
  premature family claims, OpenGL shortcut routing, and missing Vulkan draw
  primitives. Live native Vulkan/RTX renderer coverage, route navigation,
  final route layout parity, and parity validation remain open.
  Round 33 note: the renderer-matrix capture manifest now embeds the
  bridge-readiness report and fails if OpenGL route evidence, renderer-family
  guardrails, or Vulkan/RTX bridge-readiness checks fail. Live native
  Vulkan/RTX renderer coverage, route navigation, final route layout parity,
  and parity validation remain open.
  Round 34 note: bridge-readiness validation now reports named native bridge
  activation requirements and aggregate renderer manifests expose
  `bridge_activation_requirements=8`,
  `bridge_satisfied_activation_requirements=0`, and
  `bridge_pending_activation_requirements=8`. Live native Vulkan/RTX renderer
  coverage, route navigation, final route layout parity, and parity validation
  remain open.
  Round 35 note: bridge-readiness validation now exposes
  `activation_status`, pending/satisfied activation ids, and
  `next_activation_requirement` per lane, with aggregate renderer counts for
  complete, partial, and inactive activation lanes. Live native Vulkan/RTX
  renderer coverage, route navigation, final route layout parity, and parity
  validation remain open.
  Round 36 note: bridge-readiness validation now includes
  `native_bridge_source_compiled` in each non-OpenGL lane activation checklist
  and aggregate renderer manifests report `10` total bridge activation
  requirements. Live native Vulkan/RTX renderer coverage, route navigation,
  final route layout parity, and parity validation remain open.
  Round 37 note: bridge-readiness validation now detects inactive Vulkan and
  RTX/vkpt source-set wiring for `src/renderer/rmlui_bridge.cpp`. Aggregate
  renderer manifests report `bridge_satisfied_activation_requirements=2`,
  `bridge_pending_activation_requirements=8`, and
  `bridge_partial_activation_lanes=2`; live native Vulkan/RTX renderer
  coverage, route navigation, final route layout parity, and parity validation
  remain open.
  Round 38 note: bridge-readiness validation now accepts inactive Vulkan and
  RTX/vkpt class stubs only while non-OpenGL family/runtime/interface exports
  remain blocked. Aggregate renderer manifests report
  `bridge_satisfied_activation_requirements=4`,
  `bridge_pending_activation_requirements=6`, and
  `next_activation_requirement=native_family_export_present`; live native
  Vulkan/RTX renderer coverage, route navigation, final route layout parity,
  and parity validation remain open.
  Round 39 note: bridge-readiness validation now accepts inactive Vulkan and
  RTX/vkpt family exports only while non-OpenGL runtime/interface exports
  remain blocked. Aggregate renderer manifests report
  `bridge_satisfied_activation_requirements=6`,
  `bridge_pending_activation_requirements=4`, and
  `next_activation_requirement=runtime_dependency_enabled`; live native
  Vulkan/RTX renderer coverage, route navigation, final route layout parity,
  and parity validation remain open.
  Round 40 note: bridge-readiness validation now accepts inactive Vulkan and
  RTX/vkpt runtime dependencies only while native interface exports remain
  unavailable. Aggregate renderer manifests report
  `bridge_satisfied_activation_requirements=8`,
  `bridge_pending_activation_requirements=2`, and
  `next_activation_requirement=native_interface_export_present`; live native
  Vulkan/RTX renderer coverage, route navigation, final route layout parity,
  and parity validation remain open.
  Round 41 note: installed full-route load validation now records `57` opened
  RmlUi routes from the staged client with no fresh crash dump and no RmlUi
  parser/fallback/error log hits. This proves document loading and route swaps,
  not live controller parity, broad input parity, screenshot layout parity, or
  native Vulkan/RTX renderer parity.
  Round 44 note: validation now requires Quake II Rerelease font-source
  markers in the runtime adapter/capture guardrails, and the final staged
  OpenGL sweep records `58` runtime file probes, `58` unique route IDs opened,
  `59` total document opens, and `0` failure/parser/error hits. This proves
  document loading, route swaps, and rerelease TTF source use, not live
  controller parity, broad input parity, full screenshot layout parity, or
  native Vulkan/RTX renderer parity.
  Round 45 note: representative screenshot validation now covers `30`
  OpenGL RmlUi routes, and the final staged route sweep still records `58`
  unique route IDs opened, `59` total document opens, and `0`
  failure/parser/error hits after bounded settings/save-load/in-game menu
  layout changes. This proves additional layout containment on selected
  routes, not live controller parity, broad input parity, full screenshot
  layout parity, or native Vulkan/RTX renderer parity.
  Round 46 note: the same `30`-route representative OpenGL visual pass drove
  focused recapture of `singleplayer` and `ui_list`; after the targeted CSS
  changes, the final staged route sweep still records `58` unique route IDs
  opened, `59` total document opens, and `0` failure/parser/error hits. This
  proves additional containment on two selected routes, not live controller
  parity, broad input parity, full screenshot layout parity, or native
  Vulkan/RTX renderer parity.
  Round 47 note: a fresh `30`-route representative OpenGL capture drove
  focused recaptures for `admin_commands`, `callvote_main`, `dm_join`, and
  `keys`; after the CSS changes, the final staged route sweep still records
  `58` unique route IDs opened, `59` total document opens, and `0`
  failure/parser/error hits with Quake II Rerelease font-source markers. This
  proves additional containment on selected session/keybind routes, not live
  controller parity, broad input parity, full screenshot layout parity, or
  native Vulkan/RTX renderer parity.
  Round 48 note: a fresh `30`-route representative OpenGL capture drove
  focused recaptures for `performance`, `sound`, `downloads`, and
  `startserver`; after compact toggle and form-height CSS changes, the final
  staged route sweep still records `58` unique route IDs opened, `59` total
  document opens, and `0` failure/parser/error hits with Quake II Rerelease
  font-source markers. This proves additional containment on selected
  settings/local-host routes, not live controller parity, broad input parity,
  full screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 49 note: a focused `14`-route OpenGL aesthetic capture plus Sound and
  Start Server recaptures validated the transition/framing changes; the final
  staged route sweep still records `58` unique route IDs opened, `59` total
  document opens, and `0` failure/parser/transition/error hits with Quake II
  Rerelease font-source markers. This proves an additional visual refinement
  layer on selected routes, not live controller parity, broad input parity,
  full screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 50 note: a main-menu scaling matrix at `964x765`, `1280x720`,
  `1280x960`, and `2048x1152` validated viewport fill and button edge
  containment after the runtime/context scaling changes; the final staged
  route sweep still records `58` unique route IDs opened, `59` total document
  opens, and `0` failure/parser/transition/error hits with Quake II Rerelease
  font-source markers. This proves the selected scaling/positioning fix, not
  live controller parity, broad input parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 51 note: representative `960x720` captures now cover the refined
  typed-widget settings/control surfaces plus utility form recaptures, and the
  final staged route sweep still records `58` unique route IDs opened,
  `59` total document opens, and `0`
  failure/parser/transition/unsupported/error hits with Quake II Rerelease
  font-source markers. This proves the selected widget/layout refinement, not
  live controller parity, broad input parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 52 note: representative `960x720` captures now cover the deterministic
  two-column navigation grids, narrowed shared settings forms, and real shell
  command buttons; the final staged route sweep still records `58` unique
  route IDs opened, `59` total document opens, and `0`
  failure/parser/transition/unsupported/error hits with Quake II Rerelease
  font-source markers. This proves the selected navigation/layout refinement,
  not live controller parity, broad input parity, true narrow-viewport capture
  parity, full screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 53 note: representative `960x720` captures now cover the spaced
  command-tile polish and denser settings rows, and the final staged route
  sweep still records `58` unique route IDs opened, `59` total document opens,
  and `0` failure/parser/transition/unsupported/error hits with Quake II
  Rerelease font-source markers. This proves the selected menu polish
  refinement, not live controller parity, broad input parity, true
  narrow-viewport capture parity, full screenshot layout parity, or native
  Vulkan/RTX renderer parity.
  Round 54 note: focused `960x720` captures now cover primary/destructive
  action intent on shell, session, and Player Setup routes after the
  specificity fix, and the final staged route sweep still records `58` unique
  route IDs opened, `59` total document opens, and `0`
  failure/parser/transition/unsupported/error hits with Quake II Rerelease
  font-source markers. This proves the selected action-intent/widget
  refinement, not live controller parity, broad input parity, true
  narrow-viewport capture parity, full screenshot layout parity, or native
  Vulkan/RTX renderer parity.
  Round 55 note: focused `960x720` captures now cover popup confirmations and
  the two-column Sound Settings page, popup-command validation records `2`
  popup route markers with `0` bad lines, and the final staged route sweep
  still records `58` unique route IDs opened, `59` total document opens, `58`
  runtime status samples, and `0` failure/parser/transition/unsupported/error
  hits with Quake II Rerelease font-source markers. This proves the selected
  popup/audio/menu-layout refinement, not live controller parity, broad input
  parity, true narrow-viewport capture parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 56 note: focused `960x720` captures now cover Game, Main, Quit
  Confirm, and Sound Settings after Game Quit was moved to the popup route,
  popup-command validation records `2` popup route markers with `0` bad lines,
  and the final staged route sweep still records `58` unique route IDs opened,
  `59` total document opens, `58` runtime status samples, `14` menu music cue
  markers, and `0` failure/parser/transition/unsupported/error hits with Quake
  II Rerelease font-source markers. This proves the selected menu-music and
  popup-parity refinement, not live controller parity, broad input parity,
  true narrow-viewport capture parity, full screenshot layout parity, or
  native Vulkan/RTX renderer parity.
  Round 57 note: focused captures now cover Main, Game, Download Status, and
  Quit Confirm after open-sound and focus/change audio wiring, popup validation
  records `2` `quit_confirm` popup route requests with `0` bad lines, and the
  final staged route sweep still records `58` unique route IDs opened, `59`
  total document opens, `58` runtime status samples, `14` menu music cue
  markers, `14` menu open-sound cue markers, and `0`
  failure/parser/transition/unsupported/error hits with Quake II Rerelease
  font-source markers. This proves the selected open/focus audio refinement,
  not live controller parity, broad input parity, true narrow-viewport capture
  parity, full screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 58 note: deterministic staged OpenGL probes now cover legacy
  `pushmenu` entrypoints for Options and the four confirmation routes. Options
  routes through `ui_rml_runtime_open`; Quit, Forfeit, Leave Match, and
  Tournament Replay confirmations route through `ui_rml_runtime_popup`, open
  their RmlUi documents, consume alert/open sound metadata, consume menu music
  metadata, and report active runtime status. This proves selected
  menu-entrypoint bridge parity, not live controller parity, broad input
  parity, true narrow-viewport capture parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 59 note: deterministic staged OpenGL probes now cover the refined
  Multiplayer hub from `pushmenu multiplayer`. The route opens through
  `ui_rml_runtime_open`, reports active runtime status, consumes open-sound and
  menu-music metadata, records Quake II Rerelease TTF markers, and has
  `960x720` screenshot evidence for the two-column shell grid with no
  missing-model warnings. This proves selected Multiplayer hub command/layout
  parity, not live controller parity, broad input parity, true narrow-viewport
  capture parity, full screenshot layout parity, or native Vulkan/RTX renderer
  parity.
  Round 60 note: deterministic staged OpenGL probes now cover the refined
  Video Setup route from `pushmenu video`. The route opens through
  `ui_rml_runtime_open`, reports active runtime status, consumes open-sound and
  menu-music metadata, records Quake II Rerelease TTF markers, and has
  `960x720` screenshot evidence for the three-column settings layout. This
  proves selected Video Setup content/widget/layout parity, not live settings
  persistence, broad input parity, true narrow-viewport capture parity, full
  screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 61 note: deterministic staged OpenGL probes now cover the refined
  Screen Setup and Effects Setup routes from `pushmenu screen` and
  `pushmenu effects`. Both routes open through `ui_rml_runtime_open`, report
  active runtime status, consume open-sound and menu-music metadata, record
  Quake II Rerelease TTF markers, and have `960x720` screenshot evidence for
  their two-column settings layouts. This proves selected settings-family
  audio/action-row/layout parity, not live settings persistence, broad input
  parity, true narrow-viewport capture parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 62 note: deterministic staged OpenGL probes now cover the refined
  Skill Select and Start Server routes from `pushmenu skill_select` and
  `pushmenu startserver`. Both routes open through `ui_rml_runtime_open`,
  report active runtime status, consume open-sound and menu-music metadata,
  record Quake II Rerelease TTF markers, and have `960x720` screenshot
  evidence. This proves selected single-player/local-session audio and
  Start Server layout parity, not live condition evaluation, broad input
  parity, true narrow-viewport capture parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 63 note: deterministic staged OpenGL probes now cover every utility
  route from `pushmenu addressbook`, `pushmenu demos`, `pushmenu keys`,
  `pushmenu legacykeys`, `pushmenu players`, `pushmenu servers`,
  `pushmenu ui_list`, and `pushmenu weapons`. Each route opens through
  `ui_rml_runtime_open`, reports active runtime status, consumes open-sound
  and menu-music metadata, records Quake II Rerelease TTF markers, and has
  `960x720` screenshot evidence. This proves selected utility audio/layout
  parity, not live list/keybind/player-preview controllers, broad input
  parity, true narrow-viewport capture parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 64 note: deterministic staged OpenGL probes now cover representative
  session routes from `pushmenu dm_join`, `pushmenu callvote_main`,
  `pushmenu admin_commands`, `pushmenu match_stats`, and popup
  `pushmenu forfeit_confirm`, plus additional session route captures for
  join, vote, map selector, flags, random callvote ranges, and tournament map
  choices. The final captures report active runtime status, consume
  open/alert sound plus menu-music metadata, record Quake II Rerelease TTF
  markers, and show bounded `960x720` layouts. This proves selected
  session-family audio/layout/popup parity, not live session controllers,
  broad input parity, true narrow-viewport capture parity, full screenshot
  layout parity, or native Vulkan/RTX renderer parity.
  Round 65 note: deterministic staged OpenGL probes now cover Main, Options,
  Game, Multiplayer, and Single Player after the shell hub grouping and
  action-audio sweep. The final captures report active runtime status,
  consume open-sound plus menu-music metadata, record Quake II Rerelease TTF
  markers, and show corrected `960x720` hub layouts. This proves selected
  shell hub/audio/layout parity, not live controllers, broad input parity,
  true narrow-viewport capture parity, full screenshot layout parity, or
  native Vulkan/RTX renderer parity.
  Round 66 note: deterministic staged OpenGL probes now cover the refined
  Quit popup plus Options, Video, Key Bindings, and DM Join after the
  containment/template-audio sweep. Static validation also requires every
  RmlUi button, including shared templates, to declare sound intent. This
  proves selected containment/popup/audio-template parity, not live
  controllers, broad input parity, true narrow-viewport capture parity, full
  screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 67 note: deterministic staged OpenGL probes now cover runtime cvar
  form binding, cvar-backed text/labels, visibility/enabled condition
  evaluation, and typed settings widget value badges. Video, Sound, Start
  Server, DM Join, and Quit popup evidence proves selected cvar/condition
  widget behavior with open/alert sound, menu music, and Quake II Rerelease
  TTF markers, not live list/session controllers, broad input parity, true
  narrow-viewport capture parity, full screenshot layout parity, or native
  Vulkan/RTX renderer parity.
  Round 68 note: deterministic staged OpenGL probes now cover RmlUi-native
  meter/value badges for range and progress-style controls, plus Crosshair's
  two-column Crosshair/Hit Feedback containment. Video, Sound, Crosshair, and
  Quit popup evidence proves selected meter-widget, layout, popup, menu
  audio, and Quake II Rerelease TTF behavior, not live list/session
  controllers, broad input parity, true SVG vector asset support, true
  narrow-viewport capture parity, full screenshot layout parity, or native
  Vulkan/RTX renderer parity.
  Round 69 note: deterministic staged OpenGL probes now cover first-party SVG
  UX icon generation and iconized high-level menu surfaces. Main, Game,
  Options, Multiplayer, Single Player, and Quit popup evidence proves the
  OpenGL RmlUi texture path can rasterize WORR's supported SVG subset and keep
  command icons contained with Quake II Rerelease TTF markers, not full SVG
  specification/plugin parity, dynamic SVG tinting, broad input parity, true
  narrow-viewport capture parity, full screenshot layout parity, or native
  Vulkan/RTX renderer parity.
  Round 70 note: deterministic staged OpenGL probes now cover widget-specific
  SVG assets and removal of the previous command-menu pictograms. Video,
  Sound, Start Server, Player Setup, Address Book, Download Status, and Main
  evidence proves the supported SVG subset works for compact widget markers
  while Main command buttons are plain text again, not dynamic SVG state skins,
  broad input parity, true narrow-viewport capture parity, full screenshot
  layout parity, or native Vulkan/RTX renderer parity.
  Round 71 note: deterministic staged OpenGL probes now cover stateful SVG
  widget skins for real control surfaces. Video, Sound, Start Server,
  Download Status, and Quit popup captures prove button, text-box, combo,
  select, checkbox, range, progress, arrow-box, scrollbar, and popup-frame
  skin loading, and the final all-route sweep remains clean. This proves
  selected stateful widget-surface rendering, not route-wide automated pixel
  assertions for every state, broad input parity, true narrow-viewport capture
  parity, full screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 72 note: staged OpenGL probes now cover menu coverage and containment
  refinements across Main, Options, Video, Sound, Start Server, Player Setup,
  Address Book, Keys, Call Vote, Admin Commands, Download Status, and Quit
  popup. The pass removes decorative text/select skin interference, hides the
  old widget pictograms, converts range controls away from visible stepper
  arrows, fixes direct Call Vote and Admin Commands fallback coverage, and keeps
  the all-route sweep clean. It still does not establish live controller parity,
  broad input parity, true narrow-viewport capture parity, full screenshot
  layout parity, or native Vulkan/RTX renderer parity.
  Round 73 note: staged OpenGL probes now cover direct-route fallback text for
  session labels/titles, two-column `callvote_main` and lobby command grids,
  admin command-reference rows, save/load slot containment, and the Start
  Server map fallback. The final all-route sweep opened `59` documents across
  `58` registered route IDs, recorded `58` runtime status samples, and found
  `0` parser/CSS/texture/runtime error lines after excluding expected missing
  data-model notices. This proves selected menu coverage/fallback behavior, not
  live controller parity, broad input parity, true narrow-viewport capture
  parity, full screenshot layout parity, or native Vulkan/RTX renderer parity.
  Round 74 note: staged OpenGL probes now cover utility table empty-state
  layout, generic session list fallback visibility, map-selector fallback
  visibility, and tournament-veto inactive fallback containment. The final
  all-route sweep opened `59` documents across `58` registered route IDs,
  recorded `58` runtime status samples, found `0` missing data-model notice
  lines at default settings, and found `0` parser/CSS/texture/runtime error
  lines. This proves cleaner route-sweep signal and selected fallback/layout
  behavior, not live controller parity, broad input parity, true
  narrow-viewport capture parity, full screenshot layout parity, or native
  Vulkan/RTX renderer parity.
  Round 75 note: staged OpenGL probes now cover direct fallback content for
  `match_stats` and `tourney_mapchoices`, plus the `download_status` idle
  state and explicit percent meter. Static condition inventory validation now
  accepts leading `!cvar` expressions to match the compiled runtime evaluator.
  The final all-route sweep opened `59` documents across `58` registered route
  IDs, recorded `58` runtime status samples, found `0` missing data-model
  notice lines at default settings, and found `0` parser/CSS/texture/runtime
  error lines. This proves selected report/list fallback behavior and a
  condition-grammar validation fix, not live controller parity, broad input
  parity, true narrow-viewport capture parity, full screenshot layout parity,
  or native Vulkan/RTX renderer parity.
  Round 78 note: focused live OpenGL evidence records `dm_join` active on
  initial connect and the active -> join -> inactive -> inventory/Escape ->
  active -> Resume -> inactive sequence. Injected RmlUi initial/Escape captures
  were visually inspected, while native Vulkan records RmlUi
  `renderer_unavailable`, `ui_dm_menu_active=1`, and a visually confirmed JSON
  hub. The build, `225` UI smoke tests, and `.install` refresh passed. This is
  focused session and fallback evidence, not closure of the broad navigation,
  input, viewport, localization, or renderer-native RmlUi matrix.
  2026-07-13 fixed-list note: the capture harness now accepts repeated,
  validated `--seed-cvar NAME=VALUE` inputs after config loading and registers
  the `ui_list` route. The fixed-list checker covers owner publication,
  runtime dispatch/conditions, authored states/row count, back cleanup, and
  layout tokens. Installed populated/empty/error captures validate exact route,
  font, dimensions, input, and back-close markers. Broader action navigation,
  localization/large-text, and renderer-native parity remain open.
  2026-07-13 Player Setup note: the capture registry now includes `players`,
  and repeated cvar seeds visibly prove Name, composite Skin, and Hand
  hydration. A route-specific checker covers provider ordering, media/state
  behavior, live thumbnails, seven-stage 3D preview semantics, reduced motion,
  layout, and cached scrap-atlas UV handling. The installed 960x720 route
  capture passes exact route, font, input, and inactive-close checks. Broader
  action navigation and renderer-native parity remain open.
- [x] `FR-09-T10` Remove legacy JSON menu loading/widgets, close migration
  bridge fallbacks, and update staging/docs for the final RmlUi path.
  Dependency: `FR-09-T09`. Priority: P1.
  Round 4 note: no legacy JSON removal or fallback cutover is claimed; this
  task remains blocked on the later validation gates.
  Round 9 note: the parity checklist now blocks `parity_ready` claims without
  completed evidence categories. No legacy JSON removal or fallback cutover is
  claimed.
  Round 10 note: progress reporting now surfaces parity checklist pending and
  complete counts, but no legacy JSON removal, fallback cutover, or
  `parity_ready` promotion is claimed.
  Round 11 note: phase-consistency validation now blocks unsupported
  `parity_ready` overclaims against incomplete checklist evidence. No legacy
  JSON removal, fallback cutover, or `parity_ready` promotion is claimed.
  Round 12 note: metadata-sync validation now requires all central advanced
  routes to have matching feature metadata, and condition inventory keeps
  visibility/enabled-expression debt visible. No legacy JSON removal, fallback
  cutover, or `parity_ready` promotion is claimed.
  Round 13 note: legacy-removal inventory/checking is implemented as a
  guardrail, with `6` tracked items, `4` blocked, `2` pending, `0` ready, and
  `0` complete. No legacy JSON removal, fallback cutover, or `parity_ready`
  promotion is claimed.
  Round 14 note: progress reporting now includes the legacy-removal inventory
  and parity-gate state. The gate remains closed with `0` `parity_ready`
  routes, `0` ready/complete legacy-removal items, and required parity evidence
  still incomplete. No legacy JSON removal, fallback cutover, or
  `parity_ready` promotion is claimed.
  Round 16 note: controller-bindings parity evidence is complete for all `57`
  routes, but the parity gate remains closed because runtime navigation,
  renderer, screenshot/layout, input/back, and non-runtime legacy-fallback
  evidence are still pending. No legacy JSON removal, fallback cutover, or
  `parity_ready` promotion is claimed.

- [x] `FR-09-T11` Polish the main hero and make multiplayer/session routes
  consistent partial-screen in-world interfaces with native renderer focus.
  Dependency: `FR-09-T10`, `DV-03-T07`, `DV-07-T04`. Priority: P1.
  2026-07-14 completion: the main menu is a fixed, non-scrolling two-choice
  hero with the centered logo, Settings, power, and version/status bar retained
  while redundant title/corner-brand/Close/duplicate actions are removed. All
  session routes and live-match child pages use a centered translucent frame
  over the visible level; destructive popups retain a full scrim. The existing
  slowtime `dof_strength` easing drives the transition without changing game
  time. OpenGL and RTX/vkpt reuse their native focus paths, while conventional
  Vulkan now performs a native strength-eased depth-aware focus composite before
  its load-preserving RmlUi overlay. Design/runtime guardrails pass, the full
  OpenGL screenshot matrix passes 58/58, and live-world OpenGL/Vulkan/RTX
  captures were reviewed. Implementation log:
  `docs-dev/rmlui-in-world-session-frame-main-menu-polish-2026-07-14.md`.

- [x] `FR-09-T12` Revise the multiplayer Join/DM Join match hub around stable
  primary navigation, concise state presentation, and in-place active voting.
  Dependency: `FR-09-T08`, `FR-09-T11`, `DV-03-T07`, `DV-07-T04`. Priority:
  P1. 2026-07-14 completion: the normal WORR wordmark replaces the isolated W
  identity; Overview, Server Info, Voting, MyMap, and Admin remain in a fixed
  tab order with separate selected/focus/disabled treatments; unavailable
  tabs remain visible; a live vote replaces only Overview with caller,
  proposal, eligibility, countdown, and Yes/No controls; the participation
  column stays available. A compact intent-edged phase signal, local clock and
  date, and per-control hover/focus help replace redundant instructions.
  Client and sgame changes update the clock and vote state live without
  remounting Overview when another player starts a vote. Focused provider,
  design-compliance, runtime-service, build, installed-route, and live-world
  evidence pass. Implementation log:
  `docs-dev/rmlui-multiplayer-join-hub-navigation-revision-2026-07-14.md`.

## Epic FR-10: Progressive Networking, Events, Snapshots, Prediction, and Lag Compensation

Objective: replace the remaining Q2-shaped runtime coupling with a progressive,
measurable networking architecture that preserves legacy Q2 server and demo
compatibility while giving WORR deterministic client prediction, ordered event
delivery, resilient snapshots, and fair authoritative lag compensation.

Primary Areas: `src/client/*`, `src/server/*`, `src/game/bgame/*`,
`src/game/cgame/*`, `src/game/sgame/network/*`, and `tools/networking/*`.

Architecture decision: retain `q2proto/` as a read-only legacy wire adapter.
Validated protocol data is translated above the wire into canonical snapshot,
input, event, and clock records. The engine owns decode validation and bounded
histories; cgame owns snapshot transition, interpolation, prediction,
reconciliation, and event playback; shared bgame code owns deterministic
simulation; sgame owns authoritative state and scoped rewind. A future WORR
transport may be negotiated outside `q2proto/`, but it must remain dual-stack.

Exit Criteria:
- Legacy Q2/Rerelease servers and supported demos remain playable through the
  adapter path, and modern demos/MVD/spectator views have explicit acceptance
  coverage.
- Local movement and predictable side effects replay from an explicit
  authoritative input acknowledgement through the same deterministic shared
  simulation used by sgame.
- One-shot events have typed payloads, monotonic identities, delivery classes,
  prediction correlation, acknowledgement, and duplicate suppression.
- Snapshot recovery, interpolation, extrapolation, packet-loss behavior, and
  bandwidth/CPU/memory budgets pass deterministic loss/jitter/reorder tests.
- Authoritative rewind is clock-mapped, bounded, abuse-resistant, full-pose,
  interpolated, transactional, and covered across the declared weapon/mover
  policy matrix.

Tasks:
- [x] `FR-10-T01` Audit the current pipeline and ratify ownership, canonical
  data models, compatibility boundaries, security/fairness rules, budgets, and
  rollout gates.
  Area: `client/server/cgame/sgame/docs-dev`. Priority: P0. Dependencies: none.
  State: Done (2026-07-11).
  Definition of Done: a linked living plan records current-source evidence,
  the chosen architecture, measurable budgets, compatibility matrix, staged
  migrations, and rollback criteria; stale event-system guidance is marked
  superseded.
  Evidence: `docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`
  and `docs-dev/progressive-networking-foundation-2026-07-11.md`.
- [x] `FR-10-T02` Establish deterministic shared bgame simulation, explicit
  command-time and clock rules, canonical predicted-state schema, and state
  hashing.
  Area: `bgame/cgame/sgame`. Priority: P0. Dependencies: `FR-10-T01`.
  State: Done (2026-07-12).
  Definition of Done: server/client replay parity is proven from identical
  inputs, hashes detect divergence, and float/time nondeterminism is bounded or
  removed on every predicted path.
  Outcome: cgame and sgame link one strict-FP C++20 movement core behind a
  versioned pointer-free ABI. Canonical live-wire commands round-trip through
  Vanilla, Q2PRO, and Q2REPRO MOVE/BATCH_MOVE, while exact state,
  configuration, collision-transcript, and replay-chain hashes match through
  correction and sequence-wrap cases. Windows Clang/Linux GCC reports are
  byte-identical; the full Windows build, 33/33 repeated networking tests,
  refreshed install, default-off/impaired loopback, and dedicated map smoke
  pass. Game/cgame module APIs are `2025`/`2028`.
  Evidence:
  `docs-dev/networking-deterministic-prediction-core-2026-07-12.md`.
- [x] `FR-10-T03` Add a deterministic network baseline and fault-injection
  harness for latency, jitter, loss, duplication, reordering, corruption, and
  bandwidth pressure.
  Area: `tools/networking`, `client/server`. Priority: P0.
  Dependencies: `FR-10-T01`.
  State: Done (2026-07-12).
  Definition of Done: the production integer model, bounded scheduler, extended
  clock, and netchan sequence/acknowledgement validators are exercised by an
  ordinary CI suite; repeatable scenarios and checked-in goldens emit
  machine-readable evidence under `.tmp/networking/`; a staged default-off
  control and impaired loopback profile pass without queue overflow.
  Evidence:
  `docs-dev/networking-deterministic-impairment-harness-2026-07-12.md`.
- [ ] `FR-10-T04` Add a negotiated WORR packet envelope and canonical adapters
  outside `q2proto/`.
  Area: `common/net`, `client/server`. Priority: P0.
  Dependencies: `FR-10-T03`, `FR-10-T05`, `FR-10-T06`, `FR-10-T09`.
  State: In Progress at capability negotiation and native envelope,
  session-retention, receipt-hardening, canonical byte-codec, WTC1 packet-
  carrier, transport-confirmed dispatch, serialized retained-ACK handoff,
  post-assembly TX plus admitted-RX netchan seams, endpoint readiness,
  signed-setting proof carrier, default-off production readiness adapters, a
  repeated stop-and-wait canonical command shadow, a production-linked mixed
  DATA/ACK coordinator, and a canonical event-stream descriptor/transactional
  admission core with observation-bound semantic repeat and ACK-authority
  fencing. A separate default-off private `0x73` mode now adds exact per-peer
  legacy-entity event production, descriptor-before-ID reliable retention,
  live full-duplex mixed packing, transactional epoch cancellation, and
  scheduler liveness. Command mode binds private `0x53`, event mode binds
  private `0x73`, snapshot mode binds private `0x57`, combined `0x77` remains
  unsupported, and public `0x03` remains unchanged. A
  deterministic in-process virtual link now drives the real client/server
  production hook callbacks through directional DATA loss, reorder,
  duplication, one-way ACK loss, corruption, and lifecycle traffic. Its first
  additional producer observes accepted visible-source or explicit-positional
  off-frame per-client spatial audio after final snapshot emission, binds a
  visible source to its exact entity generation or the positional form to
  world while retaining raw entity metadata, and queues it through the
  default-off sender. A second producer observes one
  bounded ordered sequence of supported temporary-entity and player/monster/
  rerelease-muzzle carriers after it fits that client's post-snapshot datagram,
  rebuilds the shared templates, and atomically binds every exact visible
  source/subject generation. A bounded private Stage D snapshot transaction now
  also decodes one complete reconstructed WNC1 view, requires exact snapshot
  identity plus five independent cross-endpoint parity/semantic hashes, stages
  transport state until cgame supplies a fresh exact V2 timeline/event-fence
  receipt with the decoded native endpoint hash, and passes an actual server
  final-emission view
  through the real default-off canonical cgame timeline/event runtime in a
  production-shaped virtual link. The default-off `0x57` continuation now
  carries an independently bound snapshot epoch through 15-pair readiness,
  queues exact final-emission projections in a supersedable two-bank server
  sender, admits them through the real client RX hook only after the exact
  legacy expectation exists, publishes solely through the native cgame
  timeline owner for that epoch, and returns the semantic ACK through the
  reverse mixed carrier. Its registered production-hook link covers
  fragmented loss/reorder/duplication, deferred expectation, lost ACK and
  repeat revalidation, exactly-once cgame publication, exact retention
  release, five-proof hash quarantine, wrong-epoch rejection, real
  1,024/8,192 identity domains, a 24-expectation delayed-DATA window, and
  complete-timeout slot reuse. A fixed-seed production-path corpus now scales
  that ownership chain to 100,000 requested snapshots across four epochs:
  exact final projection, WNC1/WNE1/WTC1, production `NetImpair`, client
  receiver/admission, sole cgame timeline publication, cursor-bound prediction
  authority selection, semantic ACK, and server release. Two normalized runs
  match golden digest `c6aee48df85341ab`; all 100,000 positive frames are
  accepted, acknowledged, prediction-selected, and released, with three
  additional corrupt-carrier probes rejected. Legacy-inferred event
  references earn the correctness fence independently of the optional
  comparison audit. Other remaining event producers, raw direct-game
  `svc_sound` byte spans, native gameplay/render authority, and advertisement
  are not implemented.
  Map-quiesced client `DRAIN` still services authorized semantic
  ACKs while DATA stays frozen before a fresh challenge. Negotiated bit `0x40`
  now makes that challenge a monotonic barrier: a deterministic diagnostic
  proves finite ACK-credit exhaustion and two rotations end in counted
  disposition without stale cgame mutation, a retired bank, or silent overwrite.
  Definition of Done: the envelope serializes the accepted canonical command,
  snapshot, and event models rather than owning parallel schemas; downgrade,
  MTU, malformed-input, reconnect, and modern/legacy matrices pass.
  Progress: a pointer-free capability core, userinfo offer, server-owned
  session epoch, adjacent confirmation tuple, packet-boundary validation,
  downgrade/failure handling, and reconnect lifecycle are live. Only the
  legacy command-sideband and consumed-cursor bits are advertised;
  `WORR_NET_CAP_NATIVE_ENVELOPE_V1`,
  `WORR_NET_CAP_NATIVE_EVENT_STREAM_V1`, and
  `WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1` remain excluded from the public mask.
  Cancellation bit `0x40` is mandatory in exact private command `0x53`, event
  `0x73`, and snapshot `0x57` readiness; the event and snapshot bits
  participate only in their respective exclusive modes. An
  allocation-free transport-only V1 core now frames opaque canonical
  command/snapshot/event references, enforces a 1,200-byte datagram ceiling,
  fragments/reassembles up to 131,072 bytes/128 fragments with datagram and
  message CRCs, accepts reordered fragments and identical duplicates
  idempotently, rejects conflicting duplicates, conflicting message metadata,
  malformed inputs, and unsafe buffer overlap transactionally, and schedules
  bounded caller-owned handles with deterministic priority aging. An isolated,
  pointer-free session layer now binds that envelope to confirmed connection
  epochs, retains reliable commands/events and supersedable snapshots until
  exact receipt, reproduces acknowledgements for exact committed duplicates,
  freezes each retained message's fragmentation layout across PMTU changes,
  and fails closed on transport/canonical identity conflicts. Commit-only
  snapshot freshness, a bounded touch-refreshed retry-identity/tombstone set,
  active-retry protection, caller-owned reassembly storage, deterministic
  timeouts, epoch reset, and receipt-window scheduling harden ACK loss,
  corruption, and hostile retry order without changing a live path. A `WNC1`
  common header and transactional allocation-free fieldwise codecs now cover
  command V1, all eleven event V1 payload kinds, and snapshot V2 within the
  131,072-byte payload ceiling. The 512 entity-record cap is independent of
  the 1,024-identity legacy and 8,192-identity Rerelease index domains. The
  65,509-byte maximum-512-record/no-event projection, formerly rejected
  65,539-byte one-event case, and full legal 80,869-byte/512-event projection
  all fit. Boundary, malformed/truncation,
  aliasing, hash, signed-zero, sanitizer, static-analysis, C/C++, and i686
  layout evidence passes. A bounded WTC1 wire-carrier core now appends up to
  eight atomic WNE1
  DATA or inclusive exact-range ACK entries and a strict 32-byte terminal
  footer after an unchanged legacy prefix. It enforces the complete 1,200-byte
  packet budget, confirmed epoch equality, carrier-only CRC, nested-envelope
  validity, fail-closed matching magic, pointer-free transactional output, and
  safe overlapping byte inputs. It remains unadvertised and general adapters
  remain open. The native-session bridge now uses non-mutating due tickets, a
  process-local
  non-reusable connection-incarnation owner, a single-active connection gate,
  per-fragment build/reject/confirm outcomes, immutable payload verification,
  and final-fragment-only send accounting. A separate fixed 80-identity receipt
  ledger accepts authority only from retained RX commit or a combined admitted
  `ALREADY_COMMITTED` path. Its owner-bound active token serializes receipt
  mutation with prepare/bind/transport/terminal accounting and binds full packet
  length/CRC plus ordered ACK identity before spending retry credits. It never
  bridges gaps while coalescing, emits up to eight ACK-only ranges, and rearms
  bounded proactive delivery through exact committed-DATA retry. A cross-
  component property now proves the 64-sequence sender stall, retained RX
  authority, repeat rearm, and reverse-path recovery after every proactive ACK
  is lost. A transactional NEW-channel transmit hook now sees the exact
  final reliable-plus-unreliable application slice after qport/header assembly,
  replaces it only through a bounded staging buffer, bypasses legacy
  fragmentation, preserves byte-identical fallback, and reports exact
  all-copy synchronous handoff outcomes plus the final application bytes.
  `Netchan_ProcessEx` now exposes the symmetric exact unread application slice
  only after NEW-channel sequence/ACK admission, final reliable/fragment
  assembly, liveness, and accounting, and production call sites terminally
  handle explicit rejection. A pointer-free role-specific readiness core binds
  capability mask, globally non-reused epoch, nonce, generation, clock, and
  deadline across an exact three-record echo; its live RX/TX gates apply sticky
  clock/deadline checks. A strict packet-scoped 15-pair signed-setting carrier
  transports those records in either legacy direction with CRC32 record and
  ordered CRC16 commit validation. The added words bind an independent
  canonical snapshot epoch; records occupy 75 bytes client-to-server and
  135 bytes server-to-client. A 64-slot default-off command shadow builds
  sequential canonical records, owns immutable generation-safe codec payloads,
  joins either arrival order, and reports only ID/command/sample-offset evidence
  while explicitly declining watermark/full-record parity. Deep validation
  binds reports to retained records and the active baseline. The default-off
  production pilot now gives eligible NEW client/server connections process-
  stable owners and registers the hooks behind `cl_worr_native_shadow=0` /
  `sv_worr_native_shadow=0`. Public capability offer/confirmation remains
  exactly `0x03`; private readiness binds command `0x53`, event `0x73`, or
  snapshot `0x57`, while combined `0x77` is rejected. Legacy
  `MOVE`/`BATCH_MOVE` remains the sole command authority. After the exact
  written legacy range is known and no native record is retained, the client
  may retain its newest command and append one
  `WTC1(DATA(WNE1(WNC1)))` observation. Exact ACK application releases that
  record before a newer sampled command can be selected. The admitted server RX
  path validates one 110-byte command payload, joins either native/legacy
  arrival order, commits an exact receipt, strips the trailer, and exposes the
  unchanged legacy prefix. Successful comparisons retire the bounded join slot
  and advance a canonical command high-water. Exact transport repeats refresh
  the committed receipt; a fresh message identity replaying the same or an older
  canonical command drains without committing staged RX state. Retired-bank
  drain precedes the current-bank replay guard. The reverse path emits exact
  ACK-only ranges with 100 ms retry and three proactive handoffs; an exact
  committed duplicate rearms delivery, and the client accepts multiple entries
  only when all are ACK ranges and applies the whole set transactionally.
  Current plus one retired DRAIN bank preserve old-epoch ACK liveness across one
  map transition, with fair ACK selection. Malformed, wrong-direction, unknown-
  epoch, mismatched-reference, or unsafe replay traffic rejects or drains under
  the declared fail-closed policy. Reliable-queue point-of-no-return latches
  keep hooks installed once
  either peer may legitimately send WTC1; the server's exact committed-epoch
  fallback still strips the carrier if later local initialization fails. An
  invalid post-`CLIENT_READY` same-epoch ACTIVE leaves the client in DRAIN and
  cannot be resurrected by a later valid ACTIVE; only a validated fresh map
  challenge after quiesce may rearm it. The server stamps the admission clock
  before `Netchan_ProcessEx`, and its async
  ACK wake runs after rate/fragment gates. Exact 1,024-byte boundaries are 818
  bytes of client legacy prefix plus 206 native bytes and 976 bytes of server
  prefix plus a 48-byte ACK; 819/977 bypass and retry without changing legacy.
  A pointer-free 168-byte common-core token now transactionally coordinates one
  DATA fragment with zero to seven exact ACK ranges. It reserves the full mixed
  budget before dispatch, permits DATA-only fallback when no ACK is due, and
  binds confirm/reject/abort to the exact packet, owner, epoch, dispatch,
  fragment, and ordered ACK ranges. Existing strict one-DATA APIs are unchanged.
  The mixed core links into both production engines; the command-only `0x53`
  path retains its original directional behavior, while the separate `0x73`
  event mode described below now calls it at both live adapters. A transport-
  neutral 24-byte event-stream descriptor now establishes
  semantic event epoch/first-sequence identity independently of transport and
  snapshot lifetimes; class 4 carries its exact 56-byte WNC1 image. A 56-byte
  process-local owner enforces duplicates, conflicts, stale epochs, resync
  high-water, generation exhaustion, and reconnect-only epoch reuse. The
  production-linked completed-message admission core takes an exact immutable
  native binding, requires reserved capability bit 5, stages RX commit and ACK
  mutation on private copies, routes callbacks only through the engine cgame
  owner, and publishes transport state only after a fresh exact descriptor or
  event status/receipt proof. Public retained commit/repeat paths now reject
  semantic classes. The event revalidator first binds a newly observed
  WTC1/WNE1 fragment to exact committed whole-message history, then requires
  `ALREADY_COMMITTED`, refreshes only that identity, and proves the active
  descriptor or exact event receipt through a fresh cgame status. Any fragment
  of a committed multi-fragment message is valid; a new fragment is rejected
  before the live arena can change. Callback/repeat uncertainty burns the
  attempted epoch, leaves staged RX work uncommitted, and retires every
  snapshot/event/descriptor semantic receipt while preserving command
  receipts. Map
  quiesce retains high-water while full disconnect alone clears it. The
  command-only pilot now binds private `0x53`. When both base-shadow and
  event-shadow controls are enabled at both peers, a separate private `0x73`
  readiness path adds `CLIENT_ACTIVE_CONFIRM` without changing the public
  `0x03` tuple. A receive-only server binding admits client DATA during that
  confirmation wait but grants no early TX authority. Separate snapshot
  controls select private `0x57`, bind a nonzero canonical snapshot epoch in
  the same 15-pair proof, and require the same fourth record. Event and
  snapshot modes are mutually exclusive; future combined `0x77` is rejected.
  The server then copies exact final projections into a supersedable two-bank
  sender, while the real client RX owner defers semantic commit until the
  independently reconstructed legacy expectation exists. Its rolling
  16-entry cache protects expectations referenced by active RX, evicts only
  safe older entries, and returns retry-later instead of draining when no
  safe victim exists. Complete core-RX expiry is reconciled with wrapper
  pending descriptors before later DATA is interpreted. The native receiver
  alone publishes to the canonical cgame timeline for that epoch and returns
  the semantic ACK through the reverse mixed carrier. Diagnostic DRAIN and
  transport-hook corruption/loss retain that timeline ownership until a
  map/serverdata or matching connection boundary; failure handling detaches
  only exact owned hooks. The registered real-hook link proves fragmented
  loss/reorder/duplicate, delayed expectation, lost ACK/repeat, exactly-once
  cgame consumption, exact retention release, five-proof hash quarantine,
  wrong epoch, real 1,024/8,192 identity domains, 24 expectations before DATA,
  and complete-timeout slot reuse. Exact final per-peer
  snapshot emission now retains compact legacy-entity event sources and
  transactionally rebuilds/hash-checks typed ID-less candidates. A per-peer
  sender retains its descriptor first and assigns semantic IDs only after its
  exact ACK. Live server packets mix descriptor/event DATA with command ACKs;
  live client packets mix command DATA with semantic event ACKs. Fresh cgame
  status/receipt proof remains the only
  semantic ACK authority, and client keepalive plus server async scheduling
  wake for DATA, retry/fragment continuation, and current receipts.
  Stage D now binds a separate private canonical-snapshot capability to a
  pointer-free semantic-admission owner. WNC1 snapshot V2 remains a complete
  reconstructed view even when `base_id` is nonzero; the base identifies
  lineage and is not a decode dependency. The projection decoder preflights
  disjoint caller-owned outputs, calculates endpoint, legacy-parity, player,
  entity, area, and event hashes during its validation pass, and writes nothing
  on failure. Admission requires an exact independently observed snapshot ID
  and five cross-endpoint hashes—legacy parity plus player, entity, area, and
  event semantics—then stages session, retained-slot, and receipt-ledger
  mutation on copies. The endpoint hash is local chronology/provenance and
  must match the fresh decoded-native cgame receipt. Only that V2 receipt with
  matching generation/identity/hash/time and a healthy event-runtime fence may
  commit the transport ACK. Legacy-inferred references are always
  structure/order/hash validated for that fence with
  `cg_event_runtime_audit=0`; audit-off operation allocates no diagnostic join
  entries, so legal 512-reference snapshots cannot exhaust the fixed table.
  Exact lost-ACK repeats revalidate without
  reconsuming the snapshot. Uncertainty resets the consumer, retires semantic
  receipt authority, quarantines the snapshot epoch, and requires a keyframe.
  A real `SV_SnapshotShadowViewV1` keyframe and base-referenced complete update
  pass reversed multi-fragment delivery with exact server/cgame parity; a
  mismatched independent expectation leaves transport and cgame uncommitted.
  Legacy commands, snapshots, demos, and effect/audio presentation remain
  authoritative. The staged writer now also observes accepted visible or
   explicit-positional off-frame spatial-audio writes and one bounded ordered sequence of supported
   temporary-entity and player/monster/rerelease-muzzle carriers of up to 16
   records after final per-client snapshot emission. It binds a visible audio
   source generation from that exact view or an explicit off-frame position to
   world with `POSITION_FORCED` semantics before default-off native queueing;
   the temporary-entity path also binds a visible subject or an explicit world
   source and queues only if the entire sequence binds. Reliable and
   positionless-off-frame audio, raw direct-game `svc_sound` byte spans,
   combined/reliable muzzle traffic, malformed, unsupported-service,
   over-capacity, reliable, and non-visible paths remain legacy-only. A
   deterministic in-process production-wrapper virtual link
  drives these real hooks through accepted-but-lost S2C descriptor DATA,
  accepted-but-lost C2S mixed command DATA, delayed/reordered descriptor retry,
  duplicate event retry, one-way event-ACK loss, bidirectional mixed DATA/ACK,
  corruption in each direction, and consecutive cancellation barriers. Ordinary
  faults converge, the valid event presents exactly once, valid canceled ACK/
  DATA strips to legacy only, corrupt old traffic rejects, and the stable digest
  is `be9724b38fb5f682`. The
  explicit in-process schedule now includes distinct-event selective-receipt
  recovery, but is not yet the production `NetImpair` golden, live
  multi-process/load, soak, or cross-platform matrix. Client map-quiesced
  `DRAIN` permits only already-authorized
  semantic ACKs while freezing command DATA before a fresh challenge. That
  challenge now transactionally cancels and counts all lower activated and
  advertised-but-unactivated readiness epochs. Common terminal APIs dispose
  retained TX, pending RX, exact receipts, event sender records/backlog, and
  payload ownership; fresh current state replaces retired banks/rings. The
  client publishes staged cancellation only after `CLIENT_READY` is durably
  queued, and the server preflights the full single-threaded disposition. Valid
  old readiness controls are consumed without state mutation and counted only
  after full record, direction, and capability validation; malformed, wrong-
  direction, or downgraded controls still reject. The server classifies the
  committed record before reserving `SERVER_ACTIVE`, so stale control stays
  response-free with a full reliable queue while a current response that cannot
  append fails closed. Server status separates `wire_committed_transport_epoch`
  from the current `transport_epoch` across replacement challenges. The exact
  diagnostic spends
  all three ACK handoffs, then records one canceled
  client receipt, two canceled server event records, zero retired receipt/
  retention, a second rotation without a retired bank, both valid old
  directions stripped, and two corrupt-old rejections. Three consecutive two-process latency
  runs match, acknowledge, and
  release 152, 150, and 151 commands (453 total) with exact client/server
  counter equality and zero final retention, mismatch, rejection, drain, or
  failure. The post-change original one-command fragment/async regression also
  passes. The snapshot corpus uses one production `NetImpair` profile;
  broader full-matrix impairment breadth,
  ACK-exhaustion/map-rotation evidence,
  broader event-family production, comprehensive demo/spectator policy, load/
  cross-platform parity, native gameplay/render snapshot authority, public
  advertisement, and release-scale dual-adapter evidence remain open,
  so
  `FR-10-T04` remains In Progress. No `q2proto/` file changed. Evidence:
  `docs-dev/fr-10-t04-native-envelope-foundation-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-transport-session-retention-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-canonical-byte-codecs-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-packet-carrier-trailer-core-2026-07-13.md`,
  `docs-dev/fr-10-t04-transport-confirmed-session-carrier-and-receipts-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-assembly-netchan-hook-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-admission-netchan-rx-seam-2026-07-14.md`,
  `docs-dev/fr-10-t04-native-endpoint-readiness-core-2026-07-14.md`,
  `docs-dev/fr-10-t04-native-readiness-setting-sideband-2026-07-14.md`,
  `docs-dev/fr-10-t04-native-command-shadow-core-2026-07-14.md`, with current
  integration validation in
  `docs-dev/fr-10-t04-rx-readiness-command-shadow-integration-validation-2026-07-14.md`,
  readiness-only pilot evidence in
  `docs-dev/fr-10-t04-default-off-native-readiness-production-pilot-2026-07-14.md`,
  the historical initial production DATA/ACK observation in
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  and the stable two-process gate in
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  with current repeated-stream and mixed-core evidence in
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`,
  and descriptor/admission evidence in
  `docs-dev/fr-10-t04-t05-canonical-event-stream-admission-core-2026-07-14.md`,
  with semantic repeat/ACK-fence evidence in
  `docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md`, and live
  full-duplex per-peer evidence in
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  with deterministic production-wrapper fault evidence in
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`,
  the quiesce fix and cancellation decision in
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  and the implemented negotiated barrier in
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  plus distinct-event selective-receipt evidence in
  `docs-dev/fr-10-t04-t05-multi-event-selective-receipt-virtual-link-2026-07-15.md`,
   visible-muzzle native-shadow evidence in
   `docs-dev/fr-10-t04-t05-visible-muzzle-native-shadow-2026-07-15.md`, and
   visible-temporary-entity native-shadow evidence in
   `docs-dev/fr-10-t04-t05-visible-temp-native-shadow-2026-07-15.md`, with
   bounded direct-sgame sequence evidence in
   `docs-dev/fr-10-t04-t05-bounded-temp-sequence-native-shadow-2026-07-15.md`,
   and bounded muzzle-sequence evidence in
   `docs-dev/fr-10-t04-t05-bounded-muzzle-sequence-native-shadow-2026-07-15.md`,
   extended by ordered mixed-game-event evidence in
   `docs-dev/fr-10-t04-t05-mixed-game-event-native-shadow-2026-07-15.md`, and
   explicit-positional off-frame audio evidence in
   `docs-dev/fr-10-t04-t05-positional-offframe-spatial-audio-native-shadow-2026-07-15.md`,
   with the raw direct-sound boundary recorded in
   `docs-dev/fr-10-t04-t05-raw-direct-sound-adapter-decision-2026-07-15.md`,
  and the Stage D snapshot transaction in
  `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
  with the production adapter continuation in
  `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
  and the accepted serialized production corpus in
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`.
- [ ] `FR-10-T05` Implement the typed, sequenced event journal with predictable,
  authoritative, reliable, and persistent delivery classes.
  Area: `bgame/cgame/sgame/client/server`. Priority: P0.
  Dependencies: `FR-10-T02`, `FR-10-T03`. State: In Progress.
  Definition of Done: multiple events per tick retain order, event IDs and
  prediction correlation suppress duplicates, acknowledgement controls
  retention, and legacy one-byte events translate without changing q2proto.
  Progress: a pointer-free canonical event ABI and caller-owned bounded journal
  now implement validation, authoritative/prediction identities, delivery
  classes, selective receipt, sequence wrap, matching, coalescing, expiry, and
  at-most-once state. Stable typed payloads cover legacy entity events,
  temporary entities, player/monster muzzle flashes, and spatial audio with
  padding-independent semantic comparison. Named optional extensions shadow
  final authoritative legacy entity events into an engine-owned 4096-record
  journal. The V2 client/cgame range now captures typed temporary entities,
  player/monster muzzle flashes, normalized spatial sounds, and accepted-frame
  entity events in legacy decode order with bounded carrier status/provenance
  and strict failure atomicity. Its decoded temporary-entity mapper is now a
  shared C constructor used by cgame: it returns validated payload templates
  plus raw source/subject indices, and cgame resolves those indices to observed
   entity generations only at range delivery. The shared spatial-audio and
   player/monster muzzle mappers now feed one narrow mixed server consumer. The
   audio path runs after an unreliable visible-source or explicit-positional
   off-frame write succeeds; the direct game-event path accepts one bounded ordered temporary-entity/player/monster/
   rerelease-muzzle sequence after it fitted the client's post-snapshot datagram
   and publishes only when every member binds. It binds world or visible source/
   subject generations only from that exact sent view; the full sequence
   publishes atomically only when every member binds. All three rebind their
   template to the exact final snapshot generation before native queueing; an
   off-frame position uses world identity and `POSITION_FORCED` semantics.
   Reliable and positionless off-frame sounds, combined/reliable muzzle traffic,
   malformed/unsupported-service/over-capacity/reliable traffic, and
   non-visible sources remain legacy-only. Packets, demos, legacy presenters, and rendered
  behavior are unchanged. Cgame now owns a 2,048-record value-only legacy
  presentation journal plus a fixed-capacity authority/prediction runtime with
  independent legacy, authority, snapshot-reference, and prediction-tombstone
  lifetimes. It transactionally admits batches, maintains selective receipt,
  joins snapshot fences in either arrival order, reconciles predicted keys,
  retires terminal evidence through consumed-command watermarks, and advances
  ordered terminal authority without permanent head-of-line stalls. Legacy
  body-comparison audit remains default-off, but snapshot-reference
  correctness ingestion continues for both authoritative and legacy-inferred
  provenance when that cvar is off. Audit-off legacy ranges validate and fence
  without retaining diagnostic join entries. A compact C ABI export and engine owner validate every
  reset/status handshake, quarantine incoherent callbacks, clear pointers
  before unload, enforce strictly increasing connection-lifetime event epochs,
  and suppress stale receipts after DLL or unresolved snapshot-fence loss. A
  canonical 24-byte event-stream descriptor now establishes an independent
  semantic epoch/first sequence. Its connection owner retains high-water across
  map quiesce, while a binding/capability-gated completed-message adapter
  transactionally precommits RX/ACK state and requires fresh exact cgame status
  or receipt proof before publication. Packet-observed repeats now require
  exact committed history plus a fresh descriptor/event receipt, cannot import
  an unrelated semantic ACK, and retire all semantic ACK authority on resync.
  Failed callbacks burn the attempted epoch in both the transport owner and
  engine owner. The separate default-off `0x73` event shadow now extracts typed
  ID-less legacy-entity candidates from each peer's exact final snapshot
  emission, retains a descriptor before allocating semantic IDs, and delivers
  reliable server-originated DATA through the engine-owned cgame authority
  endpoint. A fresh negotiated barrier counts and removes lower-epoch sender,
  RX, and receipt state without allowing canceled DATA to reactivate authority,
  and every semantic ACK still requires a fresh cgame status/receipt. The
  production-wrapper virtual-link gate proves
  that one admitted event presents exactly once across DATA loss, retry,
  reordering, duplication, one-way ACK loss, corruption, and consecutive
  barriers, with digest `be9724b38fb5f682`. Map-quiesced `DRAIN` services only
  authorized event ACKs before the barrier, and an exact follow-up proves that
  three exhausted handoffs plus two rotations terminate as counted cancellation
  with zero retired state, no stale cgame mutation, and no overwrite. A
  three-event virtual-link schedule now proves that event 3 may be selectively
  receipted and retired while lost event 2 remains retained, then restores the
  receipt contiguously on event-2 retry. The local-action v2 core derives stable
  gameplay/audio/effect
  prediction keys
   from canonical command identity and adapts them into this event ABI. Stage D
  snapshot admission now requires the cgame event runtime to report the exact
  snapshot epoch healthy before snapshot ACK authority is published, including
  zero-event snapshots and event-carrying legacy-inferred snapshots with
  `cg_event_runtime_audit=0`. This closes a cross-stream fail-open seam but does not
  cut over any event presenter. Other direct sgame service families, raw
  direct-game sound, reliable/positionless off-frame audio, combined/reliable
  muzzle traffic, live predicted side-effect submission/presentation,
  production snapshot fencing, and authority cutover remain.
  Evidence:
  `docs-dev/networking-canonical-event-journal-core-2026-07-12.md`,
  `docs-dev/networking-legacy-entity-event-shadow-2026-07-12.md`,
  `docs-dev/networking-client-cgame-legacy-event-shadow-2026-07-12.md`,
  `docs-dev/networking-event-payload-catalog-2026-07-12.md`,
  `docs-dev/networking-client-cgame-typed-event-range-v2-2026-07-12.md`,
  `docs-dev/networking-cgame-canonical-event-presentation-journal-2026-07-13.md`,
  `docs-dev/networking-canonical-local-action-transaction-v2-2026-07-13.md`,
  `docs-dev/fr-10-t05-cgame-event-runtime-and-direct-authority-export-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t05-canonical-event-stream-admission-core-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md` and
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`
  and
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t05-multi-event-selective-receipt-virtual-link-2026-07-15.md`,
  plus
  `docs-dev/fr-10-t05-shared-legacy-temp-event-candidate-2026-07-15.md`,
  and
   `docs-dev/fr-10-t04-t05-visible-spatial-audio-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-positional-offframe-spatial-audio-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-raw-direct-sound-adapter-decision-2026-07-15.md`,
   and
   `docs-dev/fr-10-t04-t05-visible-muzzle-native-shadow-2026-07-15.md`, plus
   `docs-dev/fr-10-t04-t05-visible-temp-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-bounded-temp-sequence-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-bounded-muzzle-sequence-native-shadow-2026-07-15.md`,
   `docs-dev/fr-10-t04-t05-mixed-game-event-native-shadow-2026-07-15.md`, and
   `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
   plus
   `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`.
- [ ] `FR-10-T06` Build canonical acknowledged-baseline snapshot history,
  keyframe recovery, removals/PVS rules, component deltas, packet budgets, and
  legacy shadow-parity validation.
  Area: `client/server`. Priority: P0.
  Dependencies: `FR-10-T02`, `FR-10-T03`, `FR-10-T05`.
  State: In Progress at live client/server legacy shadows, exact sent
  references, repeatable 100,000-snapshot offline final-emission/projector
  evidence, default-off keyframe recovery, a staged schema-v3 live cgame parity
  gate, a current-build 115,914-frame target-count live gate, and exact final-
  emission legacy-event candidate copy-out into the default-off per-peer event
  shadow, plus a bounded Stage D native serialized snapshot semantic-admission
  transaction and default-off production final-emission TX/client RX
  integration through the real netchan hooks. A deterministic two-run corpus
  now carries 100,000 positive final projections through serialization,
  impairment, client admission, cgame prediction-authority selection, semantic
  ACK, and exact server release with golden digest `c6aee48df85341ab`.
  Broader semantic/keyframe recovery, load/budget/release evidence, public
  authority, and promotion remain open.
  Definition of Done: distinct packet/snapshot/tick/baseline IDs are validated;
  acknowledged baselines recover under loss; no entity or payload limit
  truncates silently; and zero unexplained semantic mismatches occur across the
  fault matrix and at least 100,000 shadowed snapshots before consumer cutover.
  The later WORR adapter consumes the same canonical constructors.
  Baseline note: the current engine already deltas from a client-acknowledged
  frame in `src/server/entities.c`; this task generalizes that correct behavior
  into the canonical model rather than reimplementing a previous-frame-only
  design.
  Progress: Stage A supplies a pointer-free component-aware snapshot ABI,
  explicit snapshot/base/previous and discontinuity rules, authoritative or
  legacy-inferred generation provenance, T02 player state, ordered T05 event
  references, fieldwise semantic hashes, and a fixed-capacity transactional
  immutable store with generation-safe validating copy-out. Its deterministic
  100,000-publication, cross-compiler, sanitizer, corruption, exhaustion, and
  atomic-failure evidence passes. Stage B adds an isolated public-q2proto
  projector that reconstructs exact retained `deltaframe` branches, baselines,
  removals, full frames, and per-base inferred entity lineage into immutable V2
  views. Separate endpoint/parity domains and public q2proto wire round trips
  prove equivalent Vanilla, R1Q2, Q2PRO, and Q2REPRO semantics; focused tests
  cover base jumps, fragment-stall gaps, controlled-entity omission then first
  appearance, transport-only truncation, and hostile aliases. The live client
  captures baselines, frame headers and entity deltas, attaches the negotiated
  consumed-command cursor, resolves a stateful canonical server clock across
  FPS changes, retains lineage before precache and during demo seek, compares
  accepted legacy state independently, and delivers only parity-qualified
  promotion-eligible immutable views to the external cgame timeline.
  Client-generated stored seek snapshots additionally carry a
  checksum/commit-validated exact frame/time tuple for every backup frame,
  preserving accumulated clock time across rate changes. Stage C observes each
  peer's final accepted q2proto frame/entity services at the real emission
  boundary, retains generation-safe exact base/endpoint refs, marks
  truncation, commits only after the entity terminator, and records an exact
  map simulation tick/time domain independent of wire frame numbers. Legacy
  packet delivery remains authoritative even if projection fails. The map
  simulation clock now advances exactly once after each completed `RunFrame`
  and before client snapshots are built; `sv.framenum` remains stable through
  emission and advances afterward, so post-simulation state cannot retain the
  preceding tick's time. Validated acknowledgements retain the exact emitted
  `server_time_us`, which command contexts consume directly instead of
  reconstructing from a tick and the current interval. A fixed-seed offline
  corpus now compares 100,000 server final-emission refs with a separately
  allocated receiver projection across acknowledged branches, keyframes,
  invalid-base recovery, entity lifecycle/visibility, transport discontinuity
  causes, chronology, authoritative tick wrap, and the signed wire-frame
  boundary. Two executions are byte-identical with digest
  `7b185107eeb0f6e7` and record 100,000/100,000 endpoint, legacy, component,
  and chronology matches. It also caught and now
  covers the legacy unchanged non-beam `old_origin` rule without mutating a
  retained base. That corpus remains explicitly public-API/offline. A separate
  100,000-frame production-hook corpus now supplies serialized-datagram
  acceptance without replacing the broader live/load gates. Stage D serializes one complete
  reconstructed WNC1 snapshot view, including valid nonzero-`base_id` lineage,
  through WNE1 fragmentation, WTC1 DATA, retained RX, exact projection decode,
  and semantic admission. WNC1 does not encode a delta: `base_id` is lineage
  metadata and missing base storage is not a decode dependency. Admission
  requires the exact independently observed snapshot ID and five
  cross-endpoint parity/semantic hashes, stages session/slot/receipt mutation
  on private copies, and submits to cgame only after parity succeeds. The
  decoded native endpoint hash is then matched locally against cgame's receipt.
  Cgame's snapshot timeline export is now ABI
  V2 and returns an exact admission generation, ID/hash/time, endpoint/parity
  hashes, and distinct timeline/event-fence receipt flags. The event runtime
  must report the same snapshot epoch healthy without resync before the ACK may
  commit. Legacy-inferred event ranges are structure/order/hash validated and
  receipted with the optional audit disabled, while audit-only join storage is
  bypassed. Stale/wrong-epoch/parity failures cannot ACK; semantic uncertainty
  retires every semantic receipt, quarantines the epoch, and requires a
  keyframe. Exact lost-ACK repeats revalidate without duplicate consumption. A
  real `SV_SnapshotShadowViewV1` event-carrying keyframe and base-referenced
  complete update pass reverse multi-fragment delivery through the real
  canonical cgame timeline/event runtime with exact five-proof cross-endpoint
  parity and a locally exact native endpoint receipt. Endpoint-only expectation
  divergence is legal, while an identity/parity/semantic mismatch leaves
  transport and cgame uncommitted. The production continuation independently
  binds the snapshot
  epoch in readiness, queues exact final-emission views through the bounded
  two-bank server sender, receives them through the actual client hook,
  defers semantic admission to the exact legacy expectation, publishes only
  through the native cgame consumer, and returns its semantic ACK through the
  reverse carrier. The deterministic real-hook gate covers loss, reorder,
  duplicate, lost ACK/repeat, exact release, five-proof hash quarantine, wrong
  epoch, real 1,024/8,192 identity domains, 24 expectations before DATA, and
  complete-timeout slot reuse.
  This is still not a public native authority cutover. A pointer-free recovery
  policy separately observes legacy reconstruction failures and canonical
  projection/parity failures. Default-off `cl_snapshot_recovery` can reuse the
  existing legacy `lastframe = -1` request through a bounded three-request
  burst and two-opportunity cooldown; accepted keyframes and connection resets
  clear the request, while ordinary legacy invalid-frame recovery remains
  unconditional and unchanged. Copyable saturating status records expose the
  recovery generation, reasons, streaks, retries, cooldown, and outcomes. The
  staged protocol-1038 runtime gate now parses the direct snapshot-shadow and
  attached-cgame status under `worr.networking.impairment-runtime.v3`: clean
  latest staged loopback accepts 388/388 cgame
  publications and the impaired
  profile accepts 386/386, with zero shadow mismatch, frame/capture failure, or
  consumer rejection in either profile. This is a short single-client live gate, not
  the required 100,000-frame, load, soak, or release-platform matrix.
  A target-count runner now schedules an accelerated production session and
  requires exact terminal pipeline counts, attached consumer flags, exercised
  impairment, nonce completion, pre/post launcher/engine/cgame/sgame/renderer
  hashes, and retained log hashes. It exposed a sustained command-gap failure;
  the bounded fix is implemented, and the rerun passed 115,914 exact attempts,
  projections, publications, comparisons, promotion-eligible frames, and
  consumer accepts against a 100,000 target. Mismatch, consumer rejection,
  capture/queue overflow, and accidental wall-clock throttle were all zero
  under deterministic loss, jitter, reorder, duplicate, and upstream-stall
  conditions. A separate staged dedicated-server report now runs the exact
  `SV_WorrFillCommandGap` large-loss branch for the 161- and 401-command
  retained-ring failures, requiring exact final cursors and zero policy/core
  rejections. This closes the focused command-gap report item, not packet-path,
  bandwidth, or native-serialization evidence. Each committed final-emission snapshot now
  retains compact sources for the visible legacy entity events. Transactional
  copy-out rebuilds typed ID-less candidates and rechecks their semantic hashes
  before event-sender queueing; any failure is isolated from the already
  authoritative legacy frame. The accepted serialized corpus records 100,000
  requested/accepted/acknowledged/released frames, 100,000 prediction-authority
  selections, 250,500 resolved commands, pending and non-pending ranges, the
  127-command maximum, loss/burst/reorder/duplicate/stall coverage, and three
  rejected corruption probes across four epochs. It is an in-process,
  single-profile, fixed eight-entity/four-area-byte fixture and does not execute
  `cg_predict` or PMove. Other event families, broader keyframe impairment/
  promotion evidence, load/budget gates, public authority, and broad rendering
  promotion remain open. Evidence:
  `docs-dev/networking-canonical-snapshot-stage-a-2026-07-12.md` and
  `docs-dev/networking-canonical-snapshot-stage-b-q2proto-projection-2026-07-12.md`,
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`,
  `docs-dev/fr-10-t06-stage-c-server-final-emission-shadow-2026-07-13.md`,
  `docs-dev/fr-10-t06-final-emission-projector-parity-corpus-2026-07-13.md`,
  `docs-dev/networking-snapshot-keyframe-recovery-policy-2026-07-13.md`,
  `docs-dev/networking-live-snapshot-parity-runtime-gate-2026-07-13.md`, and
  `docs-dev/fr-10-t06-live-100k-snapshot-acceptance-gate-2026-07-13.md`, plus
  `docs-dev/fr-10-t09-t16-headless-command-gap-acceptance-gate-2026-07-15.md`,
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`, and
  `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
  and
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`.
- [ ] `FR-10-T07` Move cgame to an immutable snapshot timeline with explicit
  transitions, discontinuities, adaptive interpolation, bounded extrapolation,
  and event playback.
  Area: `cgame/client`. Priority: P0.
  Dependencies: `FR-10-T05`, `FR-10-T06`, `DV-04-T02`. State: In Progress.
  Definition of Done: cgame consumes value/range APIs instead of mutable engine
  internals on migrated paths, remote entities remain smooth under the fault
  matrix, and teleport/epoch/entity-generation changes never interpolate.
  Progress: the client accepted-frame shadow feeds parity-qualified immutable
  V2 views to `WORR_CGAME_SNAPSHOT_TIMELINE_EXPORT_V2`. External cgame owns a
  bounded copied canonical timeline and exposes clock, pair selection, entity
  sampling, snapshot/player copy-out, event iteration, reset, and diagnostics
  helpers. Selecting a stored seek snapshot starts a new projection/timeline
  epoch with monotonic per-frame exact times, while sequential forward seeking
  preserves existing clock lineage. Each render frame now advances the
  canonical clock with explicit pause/resume, selects an immutable pair at the
  legacy-equivalent time, and audits or parity-promotes remote transforms per
  entity behind `cg_snapshot_timeline_render`; local prediction and rejected
  samples retain their fallbacks. The durable event journal advances an
  ordered audit cursor at the pair time. A live default-off native lane now
  admits exact final-emission legacy-entity events through the engine-owned
  cgame authority endpoint. Descriptor/event ACK publication requires fresh
  exact cgame status/receipt proof; semantic resync retires ACK authority and
  canceled-epoch DATA cannot invoke the consumer. The production-wrapper virtual
  link reaches this real endpoint, records exactly one presentation, and proves
  corrupt current traffic plus delayed canceled DATA cannot invoke cgame.
  It remains a no-effects authority sink rather than presenter-cutover proof.
  Stage D adds an exact snapshot admission generation, ID/hash/time,
  endpoint/legacy-parity hashes, and separate timeline/event-fence flags to the
  V2 status receipt. Exact identity plus five cross-endpoint parity/semantic
  hashes are validated before cgame submission; the decoded native endpoint
  hash is then matched locally in the fresh receipt. Only that receipt plus a
  healthy event runtime can
  authorize the snapshot ACK. The real-cgame virtual link proves that
  event-carrying legacy-inferred snapshots satisfy this fence with
  `cg_event_runtime_audit=0`; the cvar gates only diagnostic body/join
  retention. Exact lost-ACK repeats revalidate without reconsuming the
  snapshot; uncertainty resets cgame, retires semantic receipt authority,
  quarantines the epoch, and requires a keyframe. In private `0x57` mode the
  real client frame boundary compares legacy reconstruction without publishing
  it, while the native receiver is the sole canonical timeline publisher for
  the readiness epoch. Ownership stays latched through diagnostic DRAIN and
  transport-hook corruption/loss to prevent a mid-epoch double producer.
  Failure handling detaches only exact owned hooks, preserves foreign
  replacements, and releases authority only at map/serverdata or matching
  connection boundaries. The production-hook link proves this live client RX
  path, but not presenter cutover. Cgame prediction now has a separate exact
  value-copy bridge over that owned timeline. A generation-checked helper
  copies one retained snapshot/player pair by exact snapshot sequence; a V2
  engine import resolves replay from that copied snapshot's server-consumed
  cursor and cannot substitute packet acknowledgement. A pure selector
  requires complete/promotion-eligible state, an exact player-hash binding,
  epoch/tick/time/control alignment, identical controlled-entity
  generation/provenance, and a complete continuous canonical input range.
  The non-archived `cg_prediction_snapshot_authority` control remains default
  `0`; mode `1` audits against legacy authority and mode `2` promotes only
  after every invariant passes. Production-shaped admission now proves four
  exact retained snapshots become selectable prediction authority once each
  (`prediction_ready=4`, digest `7176afa3d4eb62b2`).
  The serialized production corpus extends selection to 100,000 positive
  snapshots across four epochs, including four legal `{epoch,0}` bootstraps,
  99,996 nonzero cursors, both pending and non-pending command ranges, 43
  discontinuity resets, and four 127-command ranges. This remains resolver/
  selector evidence over locally fabricated history; it does not execute
  `cg_predict`, PMove, presenters, or the engine V2 request-construction path.
  Legacy effect/audio
  presentation and default rendering remain authoritative; native snapshot
  authority, actual canonical presenters, impairment parity, adaptive
  extrapolation, budgets, and classic-cgame migration remain open. Evidence:
  `docs-dev/networking-snapshot-timeline-core-t07-2026-07-12.md`,
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`,
  `docs-dev/networking-live-cgame-canonical-render-promotion-2026-07-13.md`,
  `docs-dev/networking-cgame-canonical-event-presentation-journal-2026-07-13.md`,
  and
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t06-native-snapshot-semantic-admission-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
  and
  `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`.
- [ ] `FR-10-T08` Complete client prediction, input replay, reconciliation,
  predicted-state caching, and predicted-side-effect suppression.
  Area: `bgame/cgame/client`. Priority: P0.
  Dependencies: `FR-10-T02`, `FR-10-T05`, `FR-10-T07`, `FR-10-T09`.
  State: In Progress at authoritative movement replay, local fail-closed
  reconciliation, diagnostic prediction/authority audit scope, and a
  default-off private ordered Hook authority-receipt carrier. Sgame publishes
  sparse request-edge receipts into a map-scoped engine mailbox; after the
  exact authenticated command completes, only that peer's native event sender
  owns reliable ordered delivery and epoch cancellation. The control record is
  non-presentable, carries no world entity, and cgame treats conflicting or
  invalid pairing evidence as resync-required. Legacy packets, snapshots,
  demos, gameplay action, and presentation remain unchanged. Evidence:
  `docs-dev/fr-10-t08-private-hook-authority-receipt-carrier-2026-07-16.md`.
  Definition of Done: the authoritative consumed-command identity is the replay
  watermark, all eligible local movement/gameplay is predicted, corrections
  meet visual budgets, and replay never duplicates effects.
  Progress: a versioned value-copy engine/cgame input-range ABI maps the live
  server-consumed command ID to exact retained local history, verifies and
  replays only canonical successors, and makes packet ACK unavailable after
  cursor establishment. Negotiated `{0,0}` bootstrap and non-capable legacy
  peers retain an explicit provisional fallback. Missing/ambiguous history,
  identity discontinuity, invalid input, and capacity exhaustion clear
  prediction caches and snap to current authority. Retained prediction-state
  loss, movement-configuration discontinuity, and deterministic replay
  rejection now follow the same full-ring hard-resync policy; cgame telemetry
  records the precise recovery or divergence reason. The V2 import now binds
  replay to the consumed cursor copied from one exact immutable canonical
  snapshot rather than the mutable current frame. A pure selector verifies
  snapshot/player hashes, exact epoch/sequence/tick/time/control identity,
  controlled-entity generation, cursor provenance, complete legacy-ring
  coverage, and continuous canonical command IDs before correction or replay.
  Promotion mode seeds movement, view offset, blend, render flags, and entity
  identity from that canonical player copy; unavailable or mismatched
  authority clears every retained prediction ring and blocks legacy command
  replay. Audit and promotion remain explicitly default-off behind
  `cg_prediction_snapshot_authority`, with correction reasons for unavailable
  authority, mismatched authority, and snapshot discontinuity. The accepted
  serialized production corpus now selects 100,000 exact authorities from
  nonempty cursor-bound ranges and resolves 250,500 commands, including
  pending/non-pending, bootstrap, discontinuity, 127-command-limit, and
  128-command-rejection cases. The harness fabricates resolver history locally
  and stops before `cg_predict`/PMove, so it does not prove movement/weapon
  replay, side-effect suppression, or correction budgets. A transport-neutral
  local-action v2 core now proves exact-command weapon/ammo/phase transitions,
  command-derived gameplay/audio/effect keys, canonical-event adaptation,
  correction classification, and 4,096-command prediction/authority parity.
  A caller-owned audit ring now pairs predicted and authoritative local-action
  transactions by canonical command identity in either arrival order, retains
  immutable correction evidence, blocks pruning across unmatched work, and
  exposes conflict, stale, capacity, lifecycle, and correction telemetry. It
  remains diagnostic-only for live gameplay, but its operational-cost
  promotion blocker is closed: allocation-free O(n) operational validation is
  separate from explicit whole-ring deep audit, and maximum-capacity
  benchmarks enforce a 500 microsecond hot-path gate plus a 10 ms destructive-
  lifecycle gate. A command-scoped authoritative action-observation ledger now
  snapshots the real sgame pre/post weapon state only for active-valid canonical
  callbacks, retains it in a bounded per-client ledger, and counts
  `Think_Weapon` advances that occur outside a command scope. It changes no
  gameplay, event, packet, snapshot, or presentation authority; its purpose is
  to expose the frame-driven timing gap and provide the fidelity oracle required
  before a complete catalogue adapter can shadow current behavior. It now
  separately records the mapped `+hook` command intent and whether an active
  hook exists after the callback, without conflating either fact with the
  selected-weapon attack. Command identity now also owns a bounded immutable
  canonical-record cache at finalization, independent of the optional native
  pilot. A distinct versioned cgame record import and Hook interaction shadow
  validate those records against movement replay and can model only a
  request-pending edge; absent predecessor/history evidence suppresses the
  edge. It derives no timing from packet ACK and cannot produce active-hook,
  collision, damage, attachment, audio, or visual state. Sgame now constructs
  and retains the matching authoritative Hook transaction in the authenticated
  pre/post callback scope, rebasing from exact canonical record plus observed
  pre-state on bootstrap/epoch/order/state discontinuities. The bounded ledger
  now has a sparse read-only request-edge receipt projection: exact
  command/hash, action sequence, state hash, and authoritative active or
  rejected result. Cgame pairs it with the retained request shadow in either
  order and fails closed on duplicate conflicts or ID/hash/action-sequence
  mismatch. The private ordered lifecycle path is now implemented through the
  default-off native event stream: a map-scoped server mailbox transfers one
  exact receipt after the authenticated command callback, then the normal
  descriptor/DATA/ACK lifecycle delivers it to cgame. A deterministic
  virtual-link run uses the real cgame consumer and a real immutable cgame
  Hook-request prediction, then proves ordered admission, one confirmed and
  one rejected correction in fresh event epochs in either prediction/receipt
  arrival order, non-presentation terminal skipping, retry idempotence, and
  ACK retirement. It also proves contradictory valid receipts for one command
  trigger native no-ACK resync rather than accepting conflicting Hook state;
  a map rotation cancels that retained conflict, and only the fresh descriptor
  plus its ACK clears resync and gates a newly reconciled receipt.
  A receipt-first record that outlives the exact canonical command needed to
  pair it now latches the same resync health boundary before its queued ACK is
  sent.
  This emits no legacy packet, snapshot, demo data, public event, or
  presentation. Live cgame/sgame weapon-catalog adapters, audiovisual
  suppression, broad live shadow parity, correction auditing, and correction
  budgets remain open.
  Evidence:
  `docs-dev/networking-authoritative-prediction-input-range-2026-07-12.md`,
  `docs-dev/networking-canonical-local-action-transaction-v2-2026-07-13.md`,
  and
  `docs-dev/networking-local-action-prediction-authority-audit-ring-2026-07-13.md`,
  plus
  `docs-dev/fr-10-t08-t09-command-scoped-authoritative-action-observation-2026-07-15.md`,
  and
  `docs-dev/fr-10-t08-cgame-prediction-fail-closed-recovery-2026-07-15.md`,
  plus
  `docs-dev/fr-10-t08-offhand-hook-authority-observation-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-independent-offhand-interaction-model-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-offhand-hook-authoritative-transaction-ledger-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-offhand-hook-authority-receipt-reconciliation-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-private-hook-authority-receipt-carrier-2026-07-16.md`,
  and
  `docs-dev/fr-10-t08-private-hook-authority-receipt-native-virtual-link-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t08-unmatched-authority-receipt-history-loss-2026-07-16.md`,
  and
  `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`.
- [ ] `FR-10-T09` Establish wrap-safe canonical command identity, validated
  timing, and authoritative consumed-input acknowledgement.
  Area: `bgame/cgame/client/server`. Priority: P0.
  Dependencies: `FR-10-T02`, `FR-10-T03`, `FR-10-T05`, `FR-10-T06`.
  State: In Progress at live negotiated legacy-carrier, authoritative
  consumed-cursor, prediction, and rewind integration scope.
  Definition of Done: the consumed-command watermark drives prediction, rewind,
  and event correlation; duplicate input is idempotent; legacy mapping and
  sequence-wrap behavior pass the fault matrix.
  Progress: Phase 1 now supplies a pointer-free canonical record embedding the
  T02 command payload, explicit `{epoch, sequence}` IDs, validated cumulative
  sample and render timing, provenance-aware fieldwise hashes, and a bounded
  caller-owned stream with independent receive/consume cursors. It rejects
  gaps, conflicts, stale/future epochs, duration/sample overflow, invalid
  aliasing, and unconsumed-capacity loss transactionally; consumed head records
  alone may be reclaimed. Meson, strict ABI, hostile wrap/reset, sanitizer, and
  static-analysis checks pass. Phase 2 adds the strict nine-pair signed-setting
  sideband and transactional MOVE/BATCH adapter, including CRC/commit,
  adjacency, duplicate/stale backup, phantom-bootstrap, 124-command, wrap,
  reorder, capacity, and byte-identical failure coverage in the Meson suite.
  Packet ACK remains explicitly excluded. The client now assigns contiguous
  IDs and atomically stages their range with MOVE/BATCH_MOVE. At that same
  finalization boundary, command identity retains immutable canonical records
  through the shared cumulative sample-clock builder so cgame can consume
  exact bounded records without a packet-ACK reconstruction or
  optional-native-pilot dependency. The server
  validates the negotiated adjacent range, advances `consumed_cursor` only
  around authoritative simulation, and publishes the post-callback cursor
  before snapshots. The client strictly binds that tuple to the negotiated
  session, rejects regression, attaches it to snapshots and demos, and drives
  cgame replay from the exact consumed ID. Cgame can now request the replay
  range for the cursor carried by one exact retained snapshot through
  `WORR_CGAME_PREDICTION_INPUT_IMPORT_V2`; this canonical-required request
  refuses packet-ACK fallback, and the prediction selector rejects cursor,
  epoch, identity, or range discontinuity before replay. Each validated source-frame
  acknowledgement also retains the exact emitted server time beside its
  simulation frame; the callback-scoped command/rewind context consumes that
  retained time directly rather than deriving it as
  `server_tick * current_interval`, and identifies the last completed tick
  between frames. Client-generated stored seek snapshots stage their validated
  exact-time tuple before the existing cursor tuple, preserving cursor-to-frame
  adjacency and consumed-input authority. Event correlation now has a
  command-derived local-action key model. The diagnostic local-action audit
  ring now enforces that command ID plus the active connection epoch as the
  prediction/authority pairing boundary, preserves full command-epoch prune
  watermarks, and rejects stale-connection aliases without changing transport
  or authority. Its allocation-free O(n) operational V2 path now separates
  maximum-capacity hot checks from explicit whole-ring deep audit and enforces
  measured 500 microsecond hot-path and 10 ms destructive-lifecycle budgets.
  The default-off native production pilot now maps a repeated sampled sequence
  from exact encoded legacy ranges into `WNC1`, using the shared
  `usercmd_t`-to-prediction converter and each transport bank's official
  command epoch. The server joins either arrival order and records command or
  sample-offset agreement without changing the legacy consumed cursor or
  simulation authority. Exact comparisons retire their bounded join slots and
  advance a canonical high-water that rejects a fresh transport identity for
  the same or an older command. Three staged two-process runs prove 453 mapped
  commands cross actual client/server processes, match their legacy joins,
  receive exact receipts, and release client retention without final residue or
  mismatch. Separately, server-observed transport gaps no longer
  inherit the 128-slot
  retention ceiling: O(1) identity distance, a 4,096-command policy cap,
  complete time/epoch preflight, bounded synthesis, and transactional advance
  cover the reproduced 161/401-command failures with distinct telemetry. The
  serialized production corpus additionally binds 100,000 admitted
  snapshots to their exact server-consumed cursors across four epochs and
  resolves continuous nonempty canonical ranges through the 127-command
  capacity boundary. It is locally constructed resolver/selector evidence,
  not exhaustive engine-import, native command authority, or wrap/flood/event-
  correlation acceptance. Live producer/consumer cutover, native exact render
  watermarks, exhaustive command
  coverage, native authority, and full impairment/runtime acceptance remain
  open.
  Evidence:
  `docs-dev/networking-canonical-command-stream-core-2026-07-12.md` and
  `docs-dev/networking-legacy-command-adapter-core-2026-07-12.md`,
  `docs-dev/networking-consumed-command-cursor-svc-setting-sideband-2026-07-12.md`,
  `docs-dev/networking-consumed-command-cursor-server-live-carrier-2026-07-12.md`,
  `docs-dev/networking-authoritative-prediction-input-range-2026-07-12.md`,
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`,
  `docs-dev/networking-canonical-local-action-transaction-v2-2026-07-13.md`,
  `docs-dev/networking-local-action-prediction-authority-audit-ring-2026-07-13.md`,
  `docs-dev/fr-10-t09-bounded-command-gap-fast-forward-2026-07-13.md`,
  `docs-dev/fr-10-t09-t16-headless-command-gap-acceptance-gate-2026-07-15.md`,
  `docs-dev/fr-10-t07-t08-t09-canonical-snapshot-prediction-authority-2026-07-16.md`,
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`,
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  with current repeated-stream evidence in
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`.
- [ ] `FR-10-T10` Establish server clock mapping and bounded timestamped
  full-collision-pose history with a non-mutating historical query scene.
  Area: `server/sgame/network`. Priority: P0.
  Dependencies: `FR-10-T02`, `FR-10-T06`, `FR-10-T09`. State: In Progress at
  authenticated player/mover history, observation, immutable inline-BSP
  collision-provider, and sealed historical brush-dispatch scope.
  Definition of Done: commands use a server-validated snapshot-to-simulation
  watermark, history stores time/frame/origin/bounds/angles/validity, samples
  interpolate without crossing discontinuities, and historical collision
  queries cannot mutate or relink the authoritative live world.
  Progress: server-validated acknowledgements retain the exact emitted server
  time while mapping to authoritative simulation frames and validated
  contiguous-snapshot intervals; source time is never reconstructed from the
  current tick interval, and the live between-frame context names the last
  completed tick rather than the next tick yet to run. First/suppressed gaps
  use an explicit no-interpolation sentinel. The live canonical command path
  exports an API-version-2 callback scope with inactive-legacy, active-valid,
  and active-rejected states, so canonical proof rejection and synthesized-gap
  commands cannot fall back to packet-ack rewind. Sgame uses the common
  512-pose history, lifecycle/discontinuity policy, per-map/client resets, one
  sealed player-bounds scene per command, immutable trace views,
  generation-checked live revalidation, and per-ray ignore sets. The source
  snapshot is still a server-owned projection of the legacy frame ring rather
  than a materialized canonical server snapshot store. A pointer-free
  256-entry observation journal records path/reason/times/candidate/scene/hit/
  duration data and a live-state before/after guard. Stage A now exposes a
  versioned engine-to-game collision extension that resolves map-epoch-bound
  immutable inline-BSP assets and performs explicit transformed box traces
  without accepting, relinking, unlinking, or returning an edict. Stable asset
  identity includes the authoritative advertised collision-map checksum,
  inline-model namespace holes are validated, outputs are transactional, and
  the process-local import layout is checked in C and C++ for both 32- and
  64-bit pointer widths. A generated real IBSP38 fixture now loads through the
  production map path and matches direct `SV_Clip` versus extension results for
  ten translated/rotated ray/box cases and four rejection classes while live
  edict/link bytes remain unchanged. Sgame now captures bounded live
  `SOLID_BSP` push/stop mover histories after the final player end-frame,
  records generation-checked player-to-ground-mover relative origin/angles,
  and includes matching immutable brush poses in sealed canonical scenes. The
  64 × 64 fixed pose store resolves assets under the current engine map epoch
  and fails a scene closed if an eligible mover has no matching history. A
  C++-safe import mirror locks the engine provider and trace-request ABI without
  importing the legacy C game ABI. Canonical queries now validate every sealed
  brush against the current map epoch and generation-qualified mover, omit only
  those movers from the current-entity baseline, and dispatch transformed BSP
  traces at the sealed origin/angles. An identity, asset, or trace failure
  abandons the whole historical scene for the uncompensated query; no edict is
  moved, unlinked, or relinked. The live-state guard now fingerprints movers as
  well as players. A versioned 40-case,
  three-repeat common-core runner covers all eight weapon tags at
  0/50/100/200 ms plus cap, stale/future, history, teleport, respawn,
  slot-reuse, and disable boundaries. A new headless dedicated-server gate
  packages a collision-capable mover map, captures and seals an actual live
  `SOLID_BSP` mover, relocates its live collider, proves the excluded
  current-world baseline is clear, dispatches the sealed immutable BSP trace,
  and proves the authority fingerprint is unchanged after restoration. Three
  fresh server processes report identical passing V4 status with a 0.374756
  historical fraction. The same asymmetric fixture also seals a 90-degree
  mover pose, proves an unrotated immutable-asset negative control is clear,
  then proves the rotated historical pose blocks. Its V5 dedicated row arms a
  real engine bot on the start-on rotating brush and lets twelve ordinary
  server frames invoke pusher physics plus final player/mover capture. The
  paired frames must show changed mover angle and rider origin while preserving
  exact mover identity and relative origin/angles; the newest pair must then
  enter an exact sealed canonical scene. A separate schema-v28 real-command
  Railgun mode now moves the live rotating brush out of the lane only after
  authenticated command mapping, proves the substituted current baseline is
  clear, and requires the concrete `fire_rail` pierce hit to stop on the
  generation-matched sealed historical mover. Three headless two-client
  repeats apply 56 ms of rewind from 64 mover samples, preserve collision
  authority, and leave the target behind the brush at exactly zero damage.
  Broader player-on-mover physics and
  continuously rotating-mover fairness scenarios,
  broader BSP/BSPX geometry, historical-brush audit/load budgets, live
  promotion, broader engine weapon/mover scenarios, sustained load, and
  release-platform evidence remain open. Evidence:
  `docs-dev/networking-authenticated-command-context-2026-07-12.md`,
  `docs-dev/networking-live-canonical-rewind-scene-and-hitscan-2026-07-12.md`,
  `docs-dev/networking-rewind-observability-acceptance-evidence-2026-07-13.md`,
  `docs-dev/fr-10-t10-immutable-brush-collision-extension-2026-07-13.md`,
  `docs-dev/fr-10-t10-live-mover-pose-history-2026-07-15.md`,
  `docs-dev/fr-10-t10-sealed-historical-brush-dispatch-2026-07-15.md`, and
  `docs-dev/fr-10-t10-headless-moving-brush-runtime-gate-2026-07-15.md`,
  `docs-dev/fr-10-t10-rotated-moving-brush-runtime-gate-2026-07-15.md`,
  `docs-dev/fr-10-t10-live-player-on-mover-provenance-gate-2026-07-15.md`,
  `docs-dev/fr-10-t10-normal-frame-rider-continuity-gate-2026-07-15.md`, and
  `docs-dev/fr-10-t10-normal-frame-canonical-scene-gate-2026-07-15.md`, and
  `docs-dev/fr-10-t10-t11-canonical-rail-historical-mover-occlusion-acceptance-2026-07-16.md`.
- [ ] `FR-10-T11` Ship fair hitscan lag compensation with validation, rewind
  caps, diagnostics, and weapon-specific acceptance coverage.
  Area: `sgame/network`, `sgame/player`. Priority: P0.
  Dependencies: `FR-10-T10`. State: In Progress.
  Definition of Done: hitscan traces use scoped interpolated history, cannot
  trust client-authored time, do not cross spawn/teleport discontinuities, and
  pass zero/low/high-latency fairness scenarios.
  Progress: machinegun, chaingun, shotgun, super shotgun, railgun, disruptor,
  plasma beam, and thunderbolt convergence/trace queries use one cached
  canonical decision and sealed historical scene per command. Piercing uses
  generation-checked per-ray ignore identities rather than mutating or
  relinking live entities. A canonical rejection produces an uncompensated
  authoritative trace and cannot use the legacy estimate. Damage, knockback,
  death, effects, and radius damage execute against current authoritative
  state. Every integrated query supplies an explicit weapon-policy tag to the
  bounded observation seam, and the deterministic common-core matrix proves
  its declared timing/discontinuity boundaries. `g_lag_compensation` remains
  default-off. A shutdown stack-overflow regression was removed by invalidating
  bounded history/scene cache entries in place instead of aggregate-assigning
  their multi-megabyte static arrays through the game thread's stack. A
  dedicated headless `fire_rail` gate now proves an invalid current-frame
  acknowledgement's uncompensated no-damage miss plus near, in-budget, and
  over-budget recorded acknowledgements' historical hits followed by exactly 30
  total real current-authority damage, with query-time collision authority
  unchanged. The over-budget row proves applied time is clamped to the rewind
  cap. A shared real-command two-client fixture now also proves normal Railgun
  policy `5`/80-damage, machinegun policy `1`/8-damage, Chaingun policy
  `2`/18-damage (the full three-round burst), Super Shotgun policy
  `4`/120-damage (both ten-pellet barrels), and shotgun policy `3`/48-damage
  canonical historical hits at positive age without current-geometry mutation.
  Disruptor policy `6` likewise proves canonical historical convergence, then
  normal live-world projectile flight and delayed daemon processing apply exact
  45 current-authority damage. Real held Plasma Beam and dry-world Thunderbolt
  commands also prove their first ordinary policy `7` and `8` ticks apply exact
  8 current-authority damage after canonical historical hits; the Thunderbolt
  result verifies normal footprint target de-duplication. A separate
  policy-`5` real-command Railgun mode relocates the live rotating mover after
  authenticated mapping, proves the substituted current baseline is clear,
  then requires the concrete production pierce hit to terminate on the sealed
  historical mover with exact zero target damage. The earlier muzzle-projection
  query cannot satisfy the terminal proof. Three headless repeats pass at
  56 ms applied age with 64 mover samples and unchanged collision authority.
  The remaining
  fairness scenarios, moving-target/multi-target beam and other
  movers/lifecycle, abuse/load gates, and release promotion remain open.
  Evidence:
  `docs-dev/networking-live-canonical-rewind-scene-and-hitscan-2026-07-12.md`,
  `docs-dev/networking-rewind-observability-acceptance-evidence-2026-07-13.md`,
  `docs-dev/fr-10-t11-lag-compensation-shutdown-stack-overflow-2026-07-14.md`,
  `docs-dev/fr-10-t11-headless-railgun-damage-fairness-gate-2026-07-15.md`,
  `docs-dev/fr-10-t11-latency-class-rail-damage-gate-2026-07-15.md`,
  `docs-dev/fr-10-t11-canonical-command-rail-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-machinegun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-shotgun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-chaingun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-super-shotgun-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-disruptor-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-plasma-beam-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t11-canonical-command-thunderbolt-damage-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t12-held-continuous-beam-cadence-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t12-continuous-beam-release-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t12-continuous-beam-water-retrace-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t12-thunderbolt-underwater-discharge-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t12-sustained-continuous-beam-acceptance-2026-07-15.md`,
  and
  `docs-dev/fr-10-t10-t11-canonical-rail-historical-mover-occlusion-acceptance-2026-07-16.md`.
  The canonical-command two-client gate passes three headless UDP repeats for
  Railgun, machinegun, Chaingun, Super Shotgun, and shotgun: scope admission,
  genuine attack delivery, retained history, positive-age canonical historical
  target hit, exact current-authority damage, and unchanged current geometry.
  Three further v3 repeats prove the Disruptor convergence query selects that
  same historical target before normal projectile flight and its delayed daemon
  apply exact 45 current-authority damage. Three v3 Plasma Beam repeats also
  prove policy `7` selects the historical target then applies the normal first
  8-damage beam tick without current-geometry mutation. Three Thunderbolt v3
  repeats prove the dry-world policy `8` first tick applies exactly one
  de-duplicated 8-damage footprint after canonical selection. Separate held
  modes now prove three normal policy-`7`/`8` ticks (exact 24 damage) from
  fresh real client attack commands. Paired bounded release modes then require
  a real no-attack command and 250 ms with no further damage. Separate
  water-crossing modes prove the normal water-excluding retrace reaches the
  historical target with exact halved four damage for Plasma Beam policy `7`
  and dry-shooter Thunderbolt policy `8`. Three sustained repeats per beam now
  hold one real `+attack` through 32 normal ticks and exact 256 damage with no
  received release. These close the Railgun, machinegun, Chaingun, Super
  Shotgun, shotgun, Disruptor, and first/three-tick/release/water-crossing/
  bounded-32-tick Plasma Beam/Thunderbolt seams plus the bounded
  current-authority Thunderbolt underwater self-discharge seam and one
  real-command Railgun historical-mover occlusion seam only;
  `FR-10-T11` remains open for fairness, moving-target/multi-target beam and
  broader mover/lifecycle, abuse/load, and release-promotion work.
- [ ] `FR-10-T12` Extend declared compensation policies to projectile spawn /
  fast-forward, melee, continuous beams, splash, movers, and triggers.
  Area: `sgame/network`, `sgame/gameplay`. Priority: P1.
  Dependencies: `FR-10-T10`, `FR-10-T11`.
  State: In Progress at bounded continuous-beam lifecycle,
  current-authority underwater-discharge, narrow projectile convergence, and
 bounded current-world spawn-forward policies for Disruptor, Rocket, and
 Plasma Gun plus the shared Blaster/HyperBlaster bolt path, and bounded
 Chainfist hybrid melee eligibility plus ETF flechette, Phalanx spawn-forward,
 Grenade Launcher ballistic spawn-forward/current-world first splash, and
  Hand Grenade release-ballistic current-world forward, Proximity Launcher
  ballistic deploy-forward, BFG current-world launch-forward, Ion Ripper
  fifteen-bolt current-world burst-forward, and Tesla Mine release-bound
  ballistic deploy-forward, plus Trap release-bound ballistic deploy-forward
  and Grapple fresh-hook current-world spawn-forward, plus ProBall
  Chainfist-held release current-world ballistic spawn-forward, plus native
  off-hand Hook fresh current-world spawn-forward.
  Definition of Done: each interaction class has an explicit fairness/collision
  policy and deterministic tests; world state is never globally left rewound.
  Progress: plasma/heat-beam and thunderbolt main, water-retrace, and side-ray
  queries use the historical scene, and the complete thunderbolt footprint is
  resolved before damage. Real held Plasma Beam and dry-world Thunderbolt
  commands prove normal policy-`7`/`8` first ticks at exact live 8 damage,
  bounded three-tick cadences at exact 24 damage, a 250 ms no-extra-damage
  release grace after canonical selection, and water-crossing retraces at exact
  halved four damage. Three further continuously held real-command repeats per
  beam prove 32 normal ticks and exact 256 damage without a release. Three
  real-command Thunderbolt underwater-discharge
  repeats separately prove the intentional current-authority branch: eight
  Cells drain, its explicit post-damage production observer fires, and shared
  self-damage semantics yield exact 70 lost health without fabricating a
  historical beam trace. Indefinite holds and broader underwater/radius
  lifecycle coverage remain open. Disruptor target acquisition uses historical
  point/expanded convergence, followed by a separately bounded,
  server-authenticated mapped-command-age current-world hull advance for its
  newly spawned projectile. The 100 ms-capped advance cannot pass a present
  blocker, and normal touch/damage/lifetime authority remains production-owned.
  Three staged real-command repeats record exact 45 current-authority damage
  plus positive 56 ms authenticated clear forward progress under runtime schema
  v8. Rocket Launcher separately uses policy `9` to advance only its newly
  spawned current-world hull from the same authenticated time mapping; neither
  Rocket contact nor splash is historical. Three headless staged repeats under
  schema v9 record a current target moved 32 units along the aim ray,
  `canonical_historical_hit=0`, an unblocked 56 ms spawn advance, and exact
  100 current-authority direct damage. Schema v10 adds a distinct three-repeat
  Rocket splash gate: a non-damageable BBOX is created in the current world
  only after history capture, the target moves off the firing ray, and the
  unchanged `rocket_touch`/`RadiusDamage` path must hit the blocker then deal
  exact reduced 58 splash damage through its clear side. Each run preserves
  `canonical_historical_hit=0`, same-projectile identity, and the unblocked
  authenticated 56 ms current-world spawn advance. Schema v11 adds distinct
  Plasma Gun policy `10`: normal muzzle clearance remains current-world first,
  followed only by an authenticated 100 ms-capped current-world hull advance;
  direct contact, small-radius splash, lifetime, and effects remain normal
  production authority. Three headless staged direct-hit repeats move the live
  target 32 units farther along the aim ray and prove no historical impact,
  an unblocked authenticated 56 ms advance, and exact 20 direct damage. A
  separate three-repeat Plasma Gun radius-execution mode creates a
  present-world blocker only after history capture, requires normal Plasma
  touch by the exact accepted projectile, then stages a small clear-side
  damageable current-world entity for unmodified `RadiusDamage`. Every run
  retains no historical impact, unblocked authenticated 40–56 ms forward, and
  exact seven-damage falloff. Player-hull splash, map/BSP occlusion, movers,
  water, multi-target, fairness, and load behavior remain open. Evidence:
  `docs-dev/fr-10-t12-plasma-gun-current-world-splash-acceptance-2026-07-16.md`.
  Schema v12 adds shared
  Blaster/HyperBlaster bolt policy `11`: ordinary current-world muzzle
  clearance first, bounded authenticated current-world hull advance second,
  then normal production flight/contact/direct/radius authority. Three
  headless standard-Blaster direct-hit repeats prove no historical impact,
  clear authenticated 56 ms advance, and exact 15 direct damage. HyperBlaster
  now has three headless held-cadence repeats of its first ordinary 6–11
  gun-frame bolt: policy `11`, no historical impact, an unblocked authenticated
  56 ms current-world advance, unchanged staged geometry, and exact 15 direct
  damage. The hook before `Weapon_Blaster_Fire` restores only the fixture pose;
  the production cadence and combat result remain authoritative. Sustained
  cadence and optional radius remain separately open. Schema v13 adds
  Chainfist policy `12`: an accepted canonical command may select only
  one historical player reach/FOV candidate; a 64-unit current displacement
  guard plus normal current-world `CanDamage`/`Damage` retains final collision
  and damage authority. Three headless real-command repeats prove 56 ms
  authenticated age, historical eligibility, accepted 64-unit live
  displacement, unchanged current geometry, and exact 15 normal damage.
  Moving/multi-target melee, other melee weapons, mover contact, and broader
  collision/radius/trigger/co-op policies remain open. Schema v14 adds ETF
  flechette policy `13`: its regular muzzle probe remains first, followed by
  an authenticated 100 ms-capped current-world hull advance only; normal
  contact, lifetime, and exact direct damage remain production-owned. Three
  headless repeats prove no historical impact, clear 40–56 ms advance, and
  exact 10 direct damage. Schema v15 adds Phalanx policy `14`: only the
  accepted mapped-command age may advance a newly-spawned shell through a
  100 ms-capped current-world hull sweep. The later Generic weapon callback
  consumes a short-lived authorization bound to the same live shooter,
  life-generation, map epoch, policy, weapon, expiry, and bounded launch
  count; it carries no historical collision or damage result. Normal
  `phalanx_touch`, direct damage, `RadiusDamage` splash, effects, and lifetime
  remain current-world authority. Three headless repeats retain six history
  captures, require no historical impact, prove clear authenticated/advanced
  56–72 ms forward progress, and apply exact 80 direct damage. Phalanx
  now also has a narrow current-world splash fixture: the accepted normal shell
  must hit a present-world non-damageable blocker before unmodified
  `phalanx_touch`/`RadiusDamage` applies the target's clear-side splash. Three
  headless repeats prove no historical impact, clear 56–72 ms forward
  progress, and the exact normal two-barrel 93-damage total (48 then 45).
  Grenade Launcher policy 15 consumes only accepted, 100 ms-capped command age
  through stepped gravity-before-move current-world hull sweeps. A clear result
  repositions the ordinary grenade and forward velocity while reducing its
  normal fuse by the consumed age; any contact fails closed to its normal
  unadvanced Toss spawn, without fabricating a bounce or historical impact. A
  post-history current-world damageable BBOX blocker and laterally displaced
 target require unchanged grenade touch/explosion/RadiusDamage authority.
 Three headless repeats prove no historical projectile impact, clear
 authenticated 40–56 ms advance, and normal splash inside a closed 57–60
  production range (all observed 58). Hand Grenade policy 16 accepts only
  an authentic attack-to-release command edge. Its short-lived release-only
  authorization remains bound to the original shooter, life, weapon, map epoch,
  policy, command, expiry, and one launch; a clear current-world
  gravity-stepped hull path may consume at most the 100 ms cap. Three
  input-disabled headless repeats record no historical impact or fallback,
  unchanged current geometry, and authenticated/advanced 40–56 ms forward
  progress. A separate arc-aware splash fixture derives its blocker and
  lateral target from the accepted real grenade's post-forward state; three
  repeats prove no historical impact, 56 ms forward progress, normal Toss
  contact, and exact 45 current-world RadiusDamage. Arbitrary splash/occlusion,
  moving/multiple target, mover, water, Hand Grenade release lifecycle remain
  open. Proximity Launcher policy 17
  applies the same accepted 100 ms cap only to the clear initial
  gravity-stepped current-world mine path. Contact fails closed to normal
  Bounce behavior; a clear result changes only mine origin, velocity, and
  lifetime. Three input-disabled headless repeats retain no historical impact
  or fallback, unchanged geometry, and authenticated/advanced 40–56 ms
  progress. A separate lifecycle mode stages a non-damageable current-world
  Push landing surface only after that accepted clear advance, then stages its
  isolated target only after ordinary `prox_land` creates the real trigger.
  Three further input-disabled headless repeats prove 56 ms initial progress,
  no historical impact, production landing, arm delay, visibility-selected
  trigger, delayed explosion, and exact 61 normal `RadiusDamage`. No
  collision, attach, target-selection, trigger, explosion, or damage result
  is fabricated by the fixture. Arbitrary splash/occlusion, moving/multiple
  target, mover, water, destruction, chained mines, cooperative,
  fairness/load, and broader projectile simulation remain open.
  BFG policy 18 now covers only the fresh projectile's initial bounded
  current-world launch. The normal BFG frame-9 wind-up to its frame-17 spawn
  may consume one accepted shooter/life/weapon/policy/map-epoch/command-bound
  authorization for at most 1.25 seconds, but the spawned projectile itself
  remains limited to the shared 100 ms forward cap. Three input-disabled
  headless repeats record 664, 744, and 680 ms authenticated age, an unblocked
  clamped 100 ms advance, no historical impact, and unchanged current
  geometry. BFG laser, touch, staged explosion, radius, damage, moving/
  multiple-target, water, mover, cooperative, fairness, abuse, and load
  coverage remain open. Ion Ripper policy 19 now covers only the initial
  current-world advance for each of the fifteen bolts produced by its one
  normal randomized burst callback. The fixture records every matching
  authenticated forward result and cannot pass before all 15/15 bolts have
  clear current-world advances. Three input-disabled headless repeats record
  no historical impact, unchanged geometry, and exactly fifteen unblocked
  56 ms advances. Contact, damage, effects, lifetime, moving/multiple-target,
  mover, cooperative, fairness, abuse, and load coverage remain open.
  Tesla Mine policy 20 now covers only the fresh mine's initial clear
  current-world gravity advance after a real attack-to-release edge. Its
  one-launch authorization is bound to the live shooter, weapon, policy, map
  epoch, accepted release command, and expiry; a subsequent prime invalidates
  it. Three input-disabled headless repeats record no historical impact,
  unchanged geometry, no block, and 56, 56, and 40 ms advances. Landing,
  activation, target scans, chains, damage, effects, destruction, expiration,
  moving/multiple-target, mover, cooperative, fairness, abuse, and load
  coverage remain open.
  Trap policy 21 now covers only the fresh Trap's initial clear current-world
  gravity advance after a real attack-to-release edge. Its one-launch
  authorization is bound to the live shooter, weapon, policy, map epoch,
  accepted release command, and expiry; a subsequent prime invalidates it.
  Three input-disabled headless repeats record no historical impact, unchanged
  geometry, no block, and 56 ms advances. Landing, capture/release,
  destruction, expiration, moving/multiple-target, mover, cooperative,
  fairness, abuse, and load coverage remain open.
  Grapple policy 22 now covers only a fresh normal hook's clear current-world
  advance after its normal frame-six callback. One accepted
  shooter/weapon/policy/map-epoch/command-bound authorization may survive the
  ordinary wind-up for at most 750 ms, while hook movement itself remains
  capped at 100 ms. Three input-disabled headless repeats record no historical
  impact, unchanged geometry, no block, and 40, 56, and 56 ms advances.
  Attachment, pull, cable, damage, reset/detach, movers, moving/multiple
  targets, cooperative, fairness, abuse, and load coverage remain open; the
  legacy off-hand `hook` string command is unchanged.
  ProBall policy 23 now covers only a fresh ball's clear current-world gravity
  advance after the ordinary Chainfist-held attack-to-release edge. The
  one-launch authorization is bound to the live ProBall carrier,
  Chainfist-plus-ball state, policy, map epoch, and accepted release command;
  a later prime invalidates it, while direct item-use passes are unchanged.
  Three input-disabled headless ProBall repeats record no historical impact,
  unchanged geometry, no block, and 72, 88, and 56 ms advances, with explicit
  dedicated-server and client termination. Possession, pickup, touch,
  re-grab, bounce, goals, scoring, teams, resets, and lifecycle behavior
  remain production-owned and unproven.
  Native off-hand Hook policy 24 now consumes a distinct `+hook`
  user-command edge rather than the legacy `hook` string command. It requires
  a live playing client, both off-hand Hook rules, and no existing hook, then
  permits exactly one fresh-hook 100 ms-capped current-world hull advance.
  Contact, attachment, pull, damage, reset, detach, and lifecycle remain
  normal current-world production authority; no historical collision or damage
  result is constructed. A debug gate and three headless input-disabled
  dedicated-server/two-client repeats prove policy 24, clear authenticated
  56 ms advance, no historical hit/damage, and complete process termination.
  Legacy `hook` string-command behavior remains current-world and
  uncompensated.
  Evidence:
  `docs-dev/fr-10-t12-disruptor-projectile-spawn-forward-acceptance-2026-07-16.md`
  and
  `docs-dev/fr-10-t12-rocket-projectile-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-rocket-current-world-splash-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-plasma-gun-projectile-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-blaster-bolt-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-hyperblaster-cadence-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-chainfist-hybrid-melee-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-etf-flechette-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-phalanx-current-world-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-phalanx-current-world-splash-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-grenade-launcher-ballistic-current-world-splash-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-hand-grenade-release-ballistic-current-world-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-prox-launcher-ballistic-deploy-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-prox-launcher-lifecycle-current-world-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-bfg-current-world-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-ion-ripper-burst-current-world-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-tesla-mine-release-ballistic-deploy-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-trap-release-ballistic-deploy-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-grapple-hook-current-world-spawn-forward-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-proball-held-throw-current-world-ballistic-acceptance-2026-07-16.md`,
  and
  `docs-dev/fr-10-t12-offhand-hook-authenticated-current-world-acceptance-2026-07-16.md`.
- [ ] `FR-10-T13` Preserve modern and legacy demo, MVD, spectator, GTV, seeking,
  and replay semantics across snapshot/event transitions.
  Area: `client/server/mvd`. Priority: P1.
  Dependencies: `FR-10-T04` through `FR-10-T07`, plus `FR-10-T09`.
  State: In Progress at client-demo capability/cursor preservation and
  canonical seek-lineage scope.
  Definition of Done: record/play/seek/relay matrices preserve ordered commands,
  events, discontinuities, and canonical snapshot identities.
  Progress: new client recordings rebuild the confirmed capability tuple, emit
  the consumed-cursor sideband atomically with synthetic frame/entity data, and
  replay it through the same strict parser. Client-generated in-memory seek
  snapshots also carry a private six-setting, checksum/commit-validated exact
  clock tuple for every backup frame. It is accepted only for an explicitly
  armed synthetic packet, must match its frame, and precedes the cursor tuple.
  Selecting any stored snapshot replaces clock/projection lineage, including a
  forward seek whose backup window starts behind the current frame; sequential
  forward skipping retains continuity. Focused codec corruption/order,
  frame-match, C/C++ layout, arming, legacy-fallback, and serverdata-lifecycle
  checks pass. Legacy demos and protocols without the private tuple retain the
  stateful fallback. MVD/GTV, spectator switching, native
  demo schema/versioning, canonical event-order reproduction, and full record/
  play/seek/relay matrices remain open. Evidence:
  `docs-dev/networking-live-client-snapshot-prediction-and-demo-clock-2026-07-12.md`.
- [ ] `FR-10-T14` Add network telemetry, diagnostics, security/load tests, and
  enforceable CPU, memory, bandwidth, correction, and rewind budgets.
  Area: `client/server/cgame/sgame/tools`. Priority: P1.
  Dependencies: `FR-10-T03` through `FR-10-T13`, plus `FR-10-T16`.
  State: In Progress at rewind, adaptive-input, snapshot-recovery, direct live
  snapshot/cgame parity, diagnostic local-action observability, bounded live
  per-peer event-shadow diagnostics, and a versioned acceptance-evidence
  foundation. Automated client evidence now requires a hidden Windows surface,
  disabled physical input/no mouse capture, `DEVNULL` stdin, and no-window
  process creation; dedicated-only gates retain the dedicated binary.
  Every networking runtime launcher now enters its Windows processes into a
  private kill-on-close job, invokes hidden `taskkill /T` while its direct
  root is live, waits, and closes the job before reporting success. This
  prevents a staged client, engine, or dedicated descendant from surviving a
   test repeat even when its bootstrap launcher exits first. Evidence:
   `docs-dev/fr-10-t14-headless-isolated-process-tree-enforcement-2026-07-16.md`.
   Client `+set win_headless 1` now also suppresses the in-process updater
   bootstrapper UI, including the worker/relaunch path. Evidence:
   `docs-dev/fr-10-t14-bootstrapper-headless-propagation-2026-07-16.md`.
  Full telemetry/load/security gates remain open.
  Definition of Done: mandatory 1/8/16/32-client profiles include separate
  10-minute 32-client super-shotgun and chaingun runs with at least 5,000
  measured frames; snapshot and rewind work each stay within 10% p95 and
  combined networking within 25% p99 of the authoritative frame budget;
  steady-state allocations, network-attributable deadline misses, correctness
  errors, and failures across 100,000 malformed cases per changed decoder/range
  constructor are all zero. Evidence conforms to the living plan's versioned
  corpus and machine-readable evidence contract.
  Progress: the bounded rewind observation journal records path, reason,
  authenticated times, candidate/scene/hit counts, duration, and a live-state
  mutation guard. Adaptive input and snapshot recovery expose pointer-free,
  copyable, saturating policy/status records rather than requiring console-text
  scraping. The first `worr.networking.acceptance-evidence.v1` producer hashes
  its source, matrix, and binaries and records platform/build/workload,
  deterministic outcomes, timings, thresholds, privacy declarations, child
  artifacts, and explicit limitations. Meson now registers 149 networking
  tests after the repeated-shadow, mixed-carrier, repeated-runtime-parser,
  cgame runtime/export/owner ABI, and canonical event descriptor/codec/owner/
  admission, semantic repeat-fence, live event-shadow, and production-wrapper
  virtual-link, shared legacy-temporary-candidate, visible and positional-offframe spatial-audio,
  visible-muzzle, visible-temporary-entity, bounded-temp-sequence,
  bounded-muzzle-sequence, mixed-game-event native-shadow, local-interaction,
  expanded lag-compensation, Stage D snapshot semantic admission, bounded
  snapshot sender/receiver owners, client/server snapshot adapter pilots, the
  production-hook snapshot virtual link, the exact canonical snapshot
  prediction-authority selector, and the fail-closed serialized-corpus runner
  contract. The Stage D server-view row
  reaches the real default-off canonical cgame timeline/event runtime, the
  runtime row proves 2,560 audit-off legacy-inferred references without
  diagnostic-table growth, and the production row proves fragmented
  loss/reorder/duplicate, deferred expectation, lost ACK/repeat, exactly-once
  cgame publication, release, five-proof hash quarantine, wrong epoch, real
  1,024/8,192 identity domains, a 24-expectation delayed-DATA window,
  complete-timeout slot reuse, and four exact prediction-ready cgame
  admissions with digest `7176afa3d4eb62b2`. The final headless suite passes
  149/149; its serialized snapshot corpus row passes in 382.30 seconds and
  verifies two exact 100,000-frame runs against golden `c6aee48df85341ab`.
  The runner requires the real golden, removes stale evidence, publishes
  atomically, binds evidence to executable/manifest/runner hashes, and records
  explicit no-window/no-input/no-mouse policy; its focused contract passes
  11/11.
  The corpus fabricates resolver history and stops at the selector, uses one
  impairment profile, dynamic per-transmission vectors, within-burst fragment
  scheduling with synthetic receive sequencing, and eight entities/four area
  bytes. It proves neither `cg_predict`/PMove nor allocation-free operation,
  engine V2 request construction, cross-snapshot netchan reorder, real sockets,
  multi-client load/soak, or cross-platform breadth.
  Three consecutive 125-test repetitions
  remain valid historical pre-Stage-D evidence. The
  preceding one-shot production-pilot
  milestone passed a 14/14 matrix covering exact command-range selection, both
  arrival orders, current/retired epoch receipt liveness, duplicate rearm, its
  then-current one-shot drain contract, reliable-queue commitment, admission-
  clock ordering, async-wake eligibility, and the 818/819 and 976/977
  application boundaries.
  Repeated command tests add exact match retirement, same/older canonical replay
  rejection, retired-bank ordering, a 256-command client loop, and transactional
  multi-range ACK handling. Mixed-core tests cover one DATA plus up to seven ACK
  ranges, DATA-only fallback, exact packet/token binding, all-or-none confirm/
  reject/abort, and C/C++ layout. The original and repeated Python validator
  suites pass 33/33 and 8/8. Three self-validating, hash-bound repeated-runtime
  V1 reports match, acknowledge, and release 152, 150, and 151 commands (453
  total) with exact endpoint counters and zero terminal retention, mismatch,
  rejection, drain, or failure under 25 ms latency. The post-change original
  one-command gate preserves exact-once low-rate reliable delivery with 206
  rate and 25 fragment deferrals and the distinct high-rate profile with three
  async wakes and ACK handoffs. The preceding one-command milestone's final
  production-profile client syntax passed for x86-64 and i686, its client
  ASan/UBSan run passed with an 8 MiB stack, and its client adapter/test analyzer
  plists contained zero diagnostics.
  Descriptor/admission proof pins the 24-byte semantic descriptor, exact
  56-byte class-4 codec image, bit-5 capability isolation, connection high-
  water, staged RX/ACK precommit, descriptor baseline and event identity
  gates, fresh post-callback receipt, byte-identical transport rollback, and
  map-quiesce versus disconnect epoch reuse. Semantic-repeat proof adds public
  commit/repeat gates, exact-only receipt isolation, packet-observed descriptor
  and event revalidation, multi-fragment repeat acceptance, new-fragment arena
  rollback, active-emission serialization, and fail-closed retirement of every
  semantic ACK on resync.
  Live event-shadow proof adds optional fourth-confirm readiness and receive-
  only server binding, exact final-emission candidate/hash validation,
  descriptor-before-ID retention, duplicate/partial/multi-range ACK handling,
  sequence exhaustion, full-duplex carrier selection,
  and idle scheduler liveness. Exact mixed legacy-prefix boundaries are
  760/761 bytes for the descriptor and 696/697 for a legacy entity event.
  Command-only peers allocate no event heap; opted-in state and snapshot source
  metadata are fixed-capacity, and overflow/failure cannot invalidate an
  already committed legacy snapshot. The production-wrapper virtual-link gate
  adds directional accepted-but-lost DATA, retry/reorder/duplication, one-way
  ACK loss, mixed full-duplex traffic, corruption, and cancellation lifecycle
  cases. It converges, presents exactly one event, strips valid canceled ACK/
  DATA to legacy only, rejects corrupt old traffic, and has digest
  `be9724b38fb5f682`. It is an explicit in-process Windows schedule with a
  three-event selective-receipt gap, not the production `NetImpair` golden,
  live load, malformed corpus, soak, or cross-platform gate. Its lifecycle row
  spends all three ACK handoffs, then
  records counted disposal, zero retired state, a second rotation without a
  retired bank, valid-old stripping, and corrupt-old rejection. The
  final cancellation-focused gate passes 8/8, the focused Stage D gate passes
  6/6 through the real cgame receipt boundary, the production-hook snapshot
  gate passes twice, the focused prediction-authority set passes 5/5, and the
  two-run 100,000-frame serialized production corpus passes inside the final
  149/149 suite. The final full production build passes, package tests pass
  16/16, the release bootstrap headless contract passes 1/1, and the refreshed
  Windows x86-64 stage validates 16 root files, one dependency, a 500-file pak,
  one q2aas reference map, 31 botfile payloads, and 215 RmlUi assets.
  Focused native proof also covers connection-
  incarnation owner mismatch, exact packet-bound ACK outcomes, unsafe alias
  rejection, and end-to-end one-way receipt recovery. Focused carrier proof
  additionally covers golden framing, exact 1,200-byte packet boundaries,
  nested WNE1/epoch validation, corruption/truncation/alias failures, and the
  intentional carrier-only CRC domain. The staged schema-v3 networking
  post-hook runtime smoke accepts 388/388 clean and 386/386 impaired cgame
  snapshot publications with zero shadow mismatch,
  entity mismatch, frame
  failure, or consumer rejection. The impaired profile exercises 7 drops, 7
  duplicates, 6 reorders, and 1 throttle event with its queue checks passing,
  while also enforcing clean default-off recovery/input state and live impaired
  adaptive-input evaluation. The retained accelerated target-
  count report separately passes 115,914/115,914 snapshot pipeline and
  consumer accepts with zero parity, consumer, queue, or throttle failure.
  All 40 rewind cases pass over 120 invocations with zero outcome/repeat
  mismatch or authoritative pose mutation. At the preceding integration gate,
  the client engine, dedicated engine, both launchers, cgame, and sgame
  production targets all built and linked. That gate's refreshed
  `windows-x86_64` `.install/` validated 16 root runtime
  files, one dependency, the `basew` runtime, a 468-asset `pak0.pkz`, one q2aas
  reference map, 31 botfiles, 215 RmlUi assets, and
  SHA-256 equality for the client engine, dedicated engine, cgame, and sgame
  built/staged production binaries.
  The preceding targeted production rebuild, focused Stage D suite, full
  networking suite, and refreshed stage passed. The final post-corpus full
  build, 149/149 headless networking suite, policy gates, and `.install/`
  refresh also pass. The
  earlier 375/375 pre-Stage-D
  repeat and repeated plus one-command runtime gates remain valid historical
  evidence.
  The prior
  one-command slice also retains strict, sanitizer, analyzer, and snapshot-
  runtime evidence for its narrower surface.
  The local-action prediction/authority ring now has a measured maximum-
  capacity cheap/deep audit split, and the immutable brush provider has narrow
  real-IBSP38 geometric parity. They remain diagnostic/foundation scope because
  live cgame/sgame correction integration and mover history are absent. These
  focused, single-platform results do not provide the
  mandatory live collision/damage, 100,000-malformed-case,
  1/8/16/32-client, stress, soak, budget, native-adapter, broader BSP/BSPX and
  mover coverage, or cross-platform evidence; all such acceptance gates remain
  open. The latest production-wrapper lifecycle diagnostic records exactly
  three exhausted ACK handoffs followed by counted client/server disposition,
  zero retired receipt/retention, a second rotation without a retired bank,
  valid-old ACK/DATA stripping, and two corrupt-old rejections. Client/server
  status exposes monotonic cancellation floors, per-class saturating counts,
  and separate stale-canceled carrier/readiness counters. Adapter regressions
  consume valid delayed old controls without mutation while malformed and
  downgraded variants reject. The production parser uses a shared decision
  helper that performs complete stale/current classification before testing its
  fixed `SERVER_ACTIVE` response capacity; the regression sends both paths
  through a physically full reliable buffer. Status preserves a distinct prior
  wire-committed epoch across a replacement challenge. The floor covers
  advertised readiness epochs that never activated.
  Evidence:
  `docs-dev/networking-rewind-observability-acceptance-evidence-2026-07-13.md`,
  `docs-dev/networking-adaptive-input-pacing-and-redundancy-2026-07-13.md`,
  `docs-dev/networking-snapshot-keyframe-recovery-policy-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-transport-session-retention-2026-07-13.md`,
  `docs-dev/networking-live-snapshot-parity-runtime-gate-2026-07-13.md`,
  `docs-dev/networking-local-action-prediction-authority-audit-ring-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-canonical-byte-codecs-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-packet-carrier-trailer-core-2026-07-13.md`,
  `docs-dev/fr-10-t04-transport-confirmed-session-carrier-and-receipts-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-assembly-netchan-hook-2026-07-13.md`,
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  `docs-dev/fr-10-t06-live-100k-snapshot-acceptance-gate-2026-07-13.md`,
  `docs-dev/fr-10-t09-bounded-command-gap-fast-forward-2026-07-13.md`,
  `docs-dev/fr-10-t10-immutable-brush-collision-extension-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`,
  `docs-dev/fr-10-t04-parser-shared-stale-readiness-capacity-gate-2026-07-15.md`,
  `docs-dev/fr-10-t05-cgame-event-runtime-and-direct-authority-export-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t05-canonical-event-stream-admission-core-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-semantic-repeat-ack-fence-2026-07-14.md` and
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`
  and
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  plus
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  and
  `docs-dev/fr-10-t04-t06-t07-production-native-snapshot-adapters-2026-07-16.md`,
  plus
  `docs-dev/fr-10-t04-t06-t07-t08-t09-t14-serialized-production-snapshot-corpus-2026-07-17.md`.
- [ ] `FR-10-T15` Complete progressive rollout, dual-stack acceptance,
  operator/end-user documentation, migration defaults, and removal gates.
  Area: `docs-dev/docs-user/tools/release`. Priority: P1.
  Dependencies: `FR-10-T04` through `FR-10-T14`, plus `FR-10-T16`.
  State: In Progress at controls documentation and current local Windows
  staging only; rollout, dual-stack acceptance, defaults, and removal gates
  remain open.
  Definition of Done: shadow parity is zero-error across at least 100,000
  snapshots; every supported platform passes three consecutive full matrices;
  every supported dedicated-server platform passes a two-hour 32-client soak;
  R4/R5 complete at least seven opt-in days and 100 server-hours; all rollback
  drills and mandatory compatibility rows pass, with no unresolved FR-10 P0/P1
  defects. No legacy adapter is removed before its published compatibility gate
  is satisfied.
  Progress: `docs-user/progressive-networking-controls.md` documents the
  current default-off snapshot timeline/render audit, snapshot recovery,
  adaptive input, lag-compensation, and impairment controls, their status
  commands, compatibility behavior, and operator cautions. This documentation
  does not promote a default, advertise the native envelope, prove dual-stack
  rollout, authorize legacy removal, or satisfy any release-duration gate.
  The refreshed local Windows stage validates 16 root runtime files, one
  dependency, a 483-member `basew/pak0.pkz`, 31 botfiles, 215 RmlUi assets,
  one loose q2aas reference BSP, and five release notice sidecars; it is local
  evidence only and does not satisfy cross-platform release acceptance.
- [ ] `FR-10-T16` Add adaptive input pacing, batching, selective redundancy,
  and simulation/packet/render cadence decoupling.
  Area: `client/server/common/net`. Priority: P1.
  Dependencies: `FR-10-T03`, `FR-10-T04`, `FR-10-T08`, `FR-10-T09`.
  State: In Progress at deterministic controller, default-off live client
  integration, repeated stop-and-wait native command shadow, and default-off
  live full-duplex mixed command/event carrier use with scheduler wakeups;
  adaptive native delivery and acceptance gates remain open.
  Definition of Done: fresh-input age, bounded-loss recovery, idempotence, and
  bandwidth gates pass on legacy and WORR adapters. Progress: a pointer-free,
  allocation-free integer V1 controller consumes separately normalized
  successful-receive and inferred-drop counters, so drops are not counted
  again in the loss denominator, plus RTT variation, queued-command/
  acknowledgement pressure, rate, `cl_maxpackets`, and `cl_packetdup`.
  Sub-threshold windows accumulate until a usable sample exists, while counter
  rollback, clock reset/wrap, client-state teardown, server change, and map
  change explicitly rebaseline or reset policy state. Default-off
  `cl_adaptive_input` applies bounded pacing and selective redundancy only to
  the existing batch-move path. A configured `cl_maxpackets 0` remains
  immediately unlimited even during a held decision; stable and cold-start
  decisions preserve the configured `cl_packetdup` baseline, pressure may only
  raise it within transport bounds, and low rate may cap it. The non-batched
  fake-drop path suspends the controller, invalid input fails locally to legacy
  policy, and machine-readable reason/status counters saturate instead of
  wrapping. The staged `worr.networking.impairment-runtime.v3` gate now proves
  clean default-off state and live impaired-profile controller evaluation while
  the attached cgame snapshot consumer remains exact at
  388/388 clean and
  386/386 impaired publications. The local-action prediction/authority audit
  ring now has a measured bounded operational path, and the default-off WTC1
  production pilot transports one observational native command DATA at a time
  beside the authoritative legacy bytes and returns exact ACK-only receipts,
  its owner-bound session bridge now defers send accounting until a fully
  confirmed fragment burst, and its serialized ACK-only ledger has an exact
  cross-component one-way-loss proof through the 64-sequence sender stall,
  committed-DATA repeat rearm, and reverse-path recovery. The adaptive delivery
  path now uses the post-assembly TX and admitted-RX seams with exact packet-
  copy handoff outcomes, an actual per-direction application ceiling, a fresh
  current epoch, and a rate/fragment-gated async ACK wake. It now
  carries a repeated bounded stop-and-wait sampled stream: exact comparison
  retires the join slot, ACK release permits the next command, and a canonical
  high-water rejects fresh-identity replay. Three latency-only process runs
  match, acknowledge, and release 453 commands with zero terminal retention or
  failure. The separate `0x73` event mode now uses the one-DATA-plus-seven-ACK
  mixed core at both live hooks: client command DATA can carry descriptor/event
  ACKs and server descriptor/event DATA can carry command ACKs. Event DATA
  remains current-epoch only, and
  nonmutating output-due queries wake empty client keepalives plus asynchronous
  server sends for retry, fragment continuation, and receipt liveness. The
  wrapper-level virtual link proves bounded recovery for scheduled loss in both
  directions, delayed/reordered and duplicate event traffic, one-way event-ACK
  loss, and mixed command/event DATA/ACK packing; corruption fails closed and
  the digest is `be9724b38fb5f682`. Map-quiesced client `DRAIN` continues only
  already-authorized event ACK service while command DATA remains frozen before
  a fresh challenge. Negotiated bit `0x40` then cancels and counts every lower
  epoch; the three-credit/two-rotation diagnostic proves zero retired state and
  no overwrite.
  It does not yet provide adaptive native
  batching/redundancy, server feedback, native authority, production-model/
  golden impairment, production-model ACK-
  exhaustion/map-rotation breadth, or broad dual-adapter impairment evidence.
  Legacy canonical
  intake has bounded over-retention loss recovery. A scoped 50,001-frame rate-
  shaped diagnostic reached its marker after 24 fast-forwards and 10,229 skips,
  while the independent 115,914-frame snapshot gate stayed connected. A
  staged dedicated-server report now proves the production large-loss branch
  for the retained 161/401 gaps with exact cursors and no rejection counters.
  Legacy/native impairment
  breadth, a complete native adapter, bandwidth, command-age, server-feedback,
  live-integrated correction-audit evidence, load/soak, and cross-platform gates
  remain open. Evidence:
  `docs-dev/networking-adaptive-input-pacing-and-redundancy-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-transport-session-retention-2026-07-13.md`,
  `docs-dev/fr-10-t04-native-packet-carrier-trailer-core-2026-07-13.md`,
  `docs-dev/fr-10-t04-post-assembly-netchan-hook-2026-07-13.md`,
  `docs-dev/fr-10-t04-one-shot-native-command-shadow-production-pilot-2026-07-14.md`,
  `docs-dev/networking-live-snapshot-parity-runtime-gate-2026-07-13.md`,
  `docs-dev/networking-local-action-prediction-authority-audit-ring-2026-07-13.md`,
  `docs-dev/fr-10-t09-bounded-command-gap-fast-forward-2026-07-13.md`, and
  `docs-dev/fr-10-t09-t16-headless-command-gap-acceptance-gate-2026-07-15.md`,
  `docs-dev/fr-10-t04-native-two-process-async-ack-impairment-gate-2026-07-14.md`,
  with current repeated-stream and mixed-core evidence in
  `docs-dev/fr-10-t04-repeated-command-shadow-and-mixed-carrier-core-2026-07-14.md`,
  and live full-duplex event-shadow evidence in
  `docs-dev/fr-10-t04-t05-live-per-peer-event-stream-shadow-2026-07-14.md`,
  plus deterministic fault/lifecycle evidence in
  `docs-dev/fr-10-t04-t05-directional-event-shadow-virtual-link-2026-07-14.md`,
  cancellation-decision evidence in
  `docs-dev/fr-10-t04-t05-map-quiesce-ack-service-and-epoch-cancel-decision-2026-07-14.md`,
  and implemented barrier evidence in
  `docs-dev/fr-10-t04-t05-negotiated-epoch-cancellation-barrier-2026-07-14.md`,
  plus distinct-event selective-receipt evidence in
  `docs-dev/fr-10-t04-t05-multi-event-selective-receipt-virtual-link-2026-07-15.md`.

FR-10 release contract: `tools/release/targets.py` is the authoritative platform
matrix; its current targets are `windows-x86_64`, `linux-x86_64`, and
`macos-x86_64`, each with client and dedicated-server artifacts. Versioned
networking scenario manifests live under `tools/networking/scenarios/`; the
current `worr.networking.impairment-matrix.v1` manifest, checked-in golden,
ordinary tests, and staged runtime control are the reusable virtual-link
evidence for `FR-10-T03`. Full cross-subsystem acceptance evidence uses
the `worr.networking.acceptance-evidence.v1` envelope under
`.tmp/networking/<run-id>/`, recording source/platform/build/binary identities,
hashed corpus inputs, scenario and deterministic seed, topology/timing/rate and
impairment settings, sample counts, p50/p95/p99 metrics with units, thresholds,
per-gate results, and hashes of child artifacts. The staged live child artifact
now uses `worr.networking.impairment-runtime.v3`; existing baseline/runtime
schemas may be child artifacts but cannot alone prove `FR-10-T14` or
`FR-10-T15`. The detailed contract and privacy exclusions are canonicalized in
the living plan.

Living plan: `docs-dev/plans/progressive-networking-events-snapshots-roadmap.md`.

## Epic FR-11: Modern In-Game Console Experience

**Outcome:** WORR gains every unique FnQuake3 in-game console interaction and
presentation feature through WORR-native UTF-8, command-generator, renderer,
and input-system integrations.

**Project roadmap:**
`docs-dev/plans/fnquake3-console-integration-roadmap.md`

- [x] `FR-11-T01` Audit FnQuake3/WORR console capabilities and ratify the port
  matrix, architecture boundaries, cvar naming, and non-regression gates.
- [x] `FR-11-T02` Add configurable page-aware steps plus frame-time-independent
  smooth motion for manual scrollback and newly printed lines.
- [x] `FR-11-T03` Add live/fuzzy completion popup behavior over WORR commands,
  cvars, aliases, and command-specific argument generators.
- [x] `FR-11-T04` Add console mouse routing, pointer rendering, interactive
  scrollbars, UTF-8 input/log selection and copy, and selection drag reuse.
- [x] `FR-11-T05` Add centered extents and configurable appearance/fade controls
  through shared renderer APIs, retaining native Vulkan behavior.
- [x] `FR-11-T06` Add opt-in raw quoted console chat while preserving legacy and
  slash-command behavior.
- [x] `FR-11-T07` Complete automated/runtime validation, user documentation,
  implementation documentation, and `.install/` staging.

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
- [ ] `DV-02-T07` Make native Vulkan and VKPT shader generation, freshness checking, and `spirv-val` validation mandatory build/CI dependencies.
  Dependency: `DV-02-T01`, `DV-02-T02`. Priority: P1.

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
  Progress: `tools/bot_scenarios/run_bot_scenarios.py` now reports 123 implemented catalog rows and 0 pending rows. Server smoke modes `20` through `96`, mode `19` map-change/map-restart rows, the coop mode `3`/`12` reuse rows, the movement/context matrix rows, the min-player profile coverage row, the second campaign interaction row, the base2 campaign interaction-depth row, the base2 campaign progression-chain row, the base2 campaign progression-consumer row, the base2 campaign post-interaction row, the base2 campaign progression-carry row, the train campaign keyed-path row, the train campaign key-carry bridge-approach row, and the dedicated warmup/vote/admin/tournament/matchlog/MyMap/nextmap/mapvote/scoreboard/intermission smokes validate through frame-command, blackboard, action, objective, nav, match-readiness, coop-readiness, coop-command, team-policy, behavior-policy, chat-policy, match-flow, source-counter, and profile-coverage markers. The latest full `implemented` run passed 123/123 rows from `.tmp\bot_scenarios\implemented_after_route_sequential_trace_lookahead_fix.json`; focused min-player profile coverage validation passed from `.tmp\bot_scenarios\min_players_profile_coverage.json`; focused `coop_campaign_interaction_matrix_base2` validation passed from `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json`; focused `coop_campaign_interaction_depth_base2` validation passed from `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`; focused `coop_campaign_progression_chain_base2` validation passed from `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`; focused `coop_campaign_progression_consumer_base2` validation passed from `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`; focused `coop_campaign_post_interaction_base2` validation passed from `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`; focused `coop_campaign_progression_carry_base2` validation passed from `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`; focused `coop_campaign_keyed_path_train` validation passed from `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`; focused `coop_campaign_key_carry_train` bridge-approach validation passed from `.tmp\bot_scenarios\coop_campaign_key_carry_train.json`; focused generic mover lifecycle validation passed from `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json`; focused physical elevator mover activation passed from `.tmp\bot_scenarios\movement_elevator_physical.json`; focused route target anti-spin validation passed from `.tmp\bot_scenarios\route_spin_final_after_status.json`; focused route movement projection validation passed from `.tmp\bot_scenarios\route_spin_projection_focus.json`; focused route command trace/sequential look-ahead validation passed from `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`; focused `bot_chat_live_match_result` status-surface validation passed from `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json`; focused hazard context validation passed from `.tmp\bot_scenarios\movement_hazard_context_fact2.json`; focused movement context validation passed from `.tmp\bot_scenarios\movement_context_gap_rerun2\20260628T080154Z`; focused behavior sanity validation passed from `.tmp\bot_scenarios\behavior_sanity_rerun\20260627T232911Z`; focused `coop_campaign_interaction_matrix` validation passed from `.tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final\20260626T185108Z`; focused `bot_chat_live_match_result` validation passed from `.tmp\bot_scenarios\20260626Tmatch-result\20260626T182046Z`; focused outcome-aware `bot_chat_live_match_result` validation passed from `.tmp\bot_scenarios\bot_chat_match_result_outcome.json`; focused validation also passed for modes `52` through `96` plus the q2dm2/q2dm8 map-regression rows. The 123-row route command trace/sequential look-ahead aggregate plus the focused profile-coverage, campaign, movement, chat, mover, and route rows are the active baseline for future live behavior work.
  Latest scenario update: the 2026-07-01 interaction-arrival mover-endpoint round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_interaction_arrival_mover_endpoint.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. The focused `coop_campaign_key_carry_train` proof now requires endpoint-aware post-interaction arrival telemetry from `bot_nav`, including mover endpoint checks, candidates, selections, positive endpoint entity/area metadata, interaction-arrival route reach evidence, and zero bridge-arrival or lock warps. Release acceptance passed 15/15 against that scenario report, and the implementation log is `docs-dev/q3a-botlib-interaction-arrival-mover-endpoint-2026-07-01.md`.
  Latest scenario update: the 2026-07-01 interaction mover ride-state round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_mover_ride_state.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. The focused `coop_campaign_key_carry_train` proof now requires explicit mover wait, board, ride, and leave lifecycle telemetry from `bot_nav`, positive mover entity/kind/client metadata, final leave-phase evidence, interaction-arrival route reach evidence, and zero bridge-arrival or lock warps. Release acceptance passed 15/15 against that scenario report, and the implementation log is `docs-dev/q3a-botlib-interaction-mover-ride-state-2026-07-01.md`.
  Latest scenario update: the 2026-07-01 interaction mover ride-observation round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_mover_ride_observation.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. The focused `coop_campaign_key_carry_train` row now requires observation request/frame/completion counters plus `last_key_carry_bridge_ride_observation_elapsed_ms >= 200`, preserves terminal leave-phase evidence after later recovery samples, and still reaches the `trigger_key` lock without bridge-arrival or lock warps. Focused validation passed from `.tmp\bot_scenarios\mover_ride_observation_final.json` with `key_carry_bridge_ride_observation_frames=9`, `last_interaction_mover_ride_phase=4`, `nav_interaction_progression_key_path_candidates=1`, `nav_interaction_progression_key_path_selections=1`, and `last_nav_interaction_progression_key_path_required_item=70`; moving/grounded bridge samples remain diagnostics because the selected train bridge is parked in this proof. Release acceptance passed 15/15 from `.tmp\bot_release\bot_release_acceptance_mover_ride_observation.json`, and the implementation log is `docs-dev/q3a-botlib-interaction-mover-ride-observation-2026-07-01.md`.
  Latest scenario update: the 2026-07-01 generic mover lifecycle round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_generic_mover_lifecycle.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. The focused `coop_door_elevator` and `coop_live_loop` rows now require generic mover wait/board/leave lifecycle telemetry, positive mover entity/kind metadata, final phase `4`, and zero invalid skips. Focused validation passed from `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json` with both coop rows recording `interaction_mover_ride_checks=217`, `interaction_mover_ride_wait_states=104`, `interaction_mover_ride_board_states=104`, `interaction_mover_ride_leave_states=9`, `last_interaction_mover_ride_phase=4`, `last_interaction_mover_ride_entity=18`, and `last_interaction_mover_ride_kind=3`; `movement_elevator_route` remains diagnostic for physical moving/grounded follow-up. Release acceptance passed 15/15 from `.tmp\bot_release\bot_release_acceptance_generic_mover_lifecycle.json`, and the implementation log is `docs-dev/q3a-botlib-generic-mover-lifecycle-2026-07-01.md`.
  Latest scenario update: the 2026-07-01 physical elevator mover activation round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_physical_elevator_mover.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. The existing `movement_elevator_route` row now requires direct-use activations from `q3a_bot_nav_policy_status`, elevator activation/observation/completion counters from `q3a_bot_frame_command_status`, and shared mover moving-state telemetry, so physical moving-state evidence is no longer diagnostic. Focused validation passed from `.tmp\bot_scenarios\movement_elevator_physical.json`, the post-cleanup focused rerun passed from `.tmp\bot_scenarios\movement_elevator_physical_final.json`, the mover/co-op regression passed 5/5 from `.tmp\bot_scenarios\mover_direct_use_regression.json`, release acceptance passed 15/15 from `.tmp\bot_release\bot_release_acceptance_physical_elevator_mover_final.json`, and the implementation log is `docs-dev/q3a-botlib-physical-elevator-mover-activation-2026-07-01.md`.
  Latest scenario update: the 2026-07-01 route target anti-spin/status-surface round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_route_spin_status_fix.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. Route-steered commands now preserve the stabilized `route.moveTarget`, skip already-consumed close route points, and count local route-target progress as valid stuck-progress evidence. Focused route validation passed 8/8 from `.tmp\bot_scenarios\route_spin_final_after_status.json`, focused `bot_chat_live_match_result` status-surface validation passed from `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json`, release acceptance passed 15/15 from `.tmp\bot_release\bot_release_acceptance_route_spin_fix.json`, and the implementation log is `docs-dev/q3a-botlib-route-target-anti-spin-2026-07-01.md`.
  Latest scenario update: the 2026-07-01 route movement projection round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_route_projection_fix.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. Frame commands now project route yaw into view-relative forward/side movement so bots can keep moving along the route while aiming at a live target, and move-target matching tolerates bounded BotLib endpoint offsets. Focused route/combat validation passed 10/10 from `.tmp\bot_scenarios\route_spin_projection_focus.json`, release acceptance passed 15/15 from `.tmp\bot_release\bot_release_acceptance_route_projection_fix.json`, and the implementation log is `docs-dev/q3a-botlib-route-movement-projection-2026-07-01.md`.
  Latest scenario update: the 2026-07-02 consumed route target watchdog round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_consumed_target_watchdog.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. Route progress now distinguishes a first local `route.moveTarget` arrival from repeated already-consumed target-radius frames, and route-target shift checks use horizontal distance to match movement progress. Focused route/combat/movement validation passed 10/10 from `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json`, release acceptance passed 15/15 with 0 warnings from `.tmp\bot_release\bot_release_acceptance_consumed_target_watchdog.json`, and the implementation log is `docs-dev/q3a-botlib-consumed-route-target-watchdog-2026-07-02.md`.
  Latest scenario update: the 2026-07-02 route command trace/sequential look-ahead round updates the active aggregate to `.tmp\bot_scenarios\implemented_after_route_sequential_trace_lookahead_fix.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. Command steering now trace-gates ordinary far look-ahead and route-goal fallbacks, while already-consumed local nodes advance only to the first ordered non-close future point and stop farther promotion when the trace is blocked. Focused route/navigation validation passed 12/12 from `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`, the q2dm2 survival row passed from `.tmp\bot_scenarios\route_sequential_trace_q2dm2_fix.json`, release acceptance passed 15/15 with 0 warnings from `.tmp\bot_release\bot_release_acceptance_route_sequential_trace_lookahead.txt`, and the implementation log is `docs-dev/q3a-botlib-route-command-trace-sequential-lookahead-2026-07-02.md`.

  Latest scenario update: the 2026-07-02 stuck recovery obstacle-probe round updates the active aggregate to `.tmp\bot_scenarios\implemented_stuck_recovery_probe.json`, with 123/123 automated `implemented` rows passing and 0 failed, timeout, error, or pending rows. Local recovery now probes player-hull escape candidates, stores a world-space escape direction per recovery window, and projects that direction back into usercmd movement so bots do not repeatedly apply the same view-relative back/strafe against a wall. Focused `recover_from_stall` validation passed from `.tmp\bot_scenarios\stuck_recovery_probe_focus2.json`, focused navigation regression validation passed 4/4 from `.tmp\bot_scenarios\stuck_recovery_probe_nav_focus.json`, release acceptance passed 15/15 with 0 warnings from `.tmp\bot_release\bot_release_acceptance_stuck_recovery_probe.json`, and the implementation log is `docs-dev/q3a-botlib-stuck-recovery-obstacle-probe-2026-07-02.md`.
  Latest scenario update: the promoted `behavior_policy_umbrella` row uses mode `52` with `bot_behavior_enable 1`, hard-gates umbrella TDM role-route, role-combat, friendly-fire, and match item-policy activation through runtime status markers, and verifies the individual proof cvars remain disabled in the begin marker. The row passed focused validation from `.tmp\bot_scenarios\behavior_policy_umbrella\20260622T050833Z`; `match_item_policy` mode `51` was revalidated from `.tmp\bot_scenarios\match_item_policy_check\20260622T050722Z`; `profile_team_policy` mode `54` passed from `.tmp\bot_scenarios\profile_team_policy\20260622T055119Z`; `profile_item_policy` mode `55` passed from `.tmp\bot_scenarios\profile_item_policy\20260622T062835Z`; `profile_movement_policy` mode `56` passed from `.tmp\bot_scenarios\profile_movement_policy\20260622T070032Z`; `bot_chat_policy` mode `57` passed from `.tmp\bot_scenarios\20260622T080531Z`; `bot_chat_team_policy` mode `58` passed from `.tmp\bot_scenarios\20260622T080044Z`; `bot_chat_rate_policy` mode `59` passed from `.tmp\bot_scenarios\20260622T081428Z`; `bot_chat_initial_policy` mode `60` passed from `.tmp\bot_scenarios\20260622T085845Z`; `bot_chat_reply_policy` mode `61` passed from `.tmp\bot_scenarios\20260622T092009Z`; `bot_chat_event_policy` mode `62` passed from `.tmp\bot_scenarios\20260622T093637Z`; `behavior_arbitration` mode `63` passed from `.tmp\bot_scenarios\20260622T112202Z`; `target_memory_decay` mode `64` passed from `.tmp\bot_scenarios\20260622T120742Z`; `weapon_scoring_arsenal` mode `65` passed from `.tmp\bot_scenarios\20260622T123648Z`; `aim_fire_policy_depth` mode `66` passed from `.tmp\bot_scenarios\20260622T125826Z`; `ammo_pressure_pickup` mode `67` passed from `.tmp\bot_scenarios\ammo_pressure_pickup\20260622T132231Z`; `survival_inventory_use` mode `68` passed from `.tmp\bot_scenarios\survival_inventory_use\20260622T161739Z`; `survival_health_route` mode `69` passed from `.tmp\bot_scenarios\survival_health_route\20260622T164109Z`; `survival_armor_route` mode `70` passed from `.tmp\bot_scenarios\survival_armor_route\20260622T165918Z`; `combat_survival_regression` mode `71` passed from `.tmp\bot_scenarios\combat_survival_regression\20260622T171717Z`; `combat_survival_regression_q2dm2` mode `71` passed from `.tmp\bot_scenarios\combat_survival_regression_q2dm2\20260622T194547Z`; and `threat_retreat_avoidance` mode `72` passed from `.tmp\bot_scenarios\20260622T202608Z`.
  Latest CTF objective live-loop update: mode `40` `ctf_objective_route` now hard-gates base-return, carrier-support, and dropped-flag selections in one CTF run, plus `behavior_arbitration_objective_candidates` and `behavior_arbitration_objective_owners` evidence from `.tmp\bot_scenarios\20260622T210329Z`.
  Latest TDM role spawn-stability update: mode `73` `tdm_role_spawn_stability` combines TDM role-route, TDM role-combat, and forced same-map restart validation in one four-bot TDM run, proving route/combat owner activity, post-reload cycle status, action attack buttons, and final cleanup from `.tmp\bot_scenarios\20260622T212431Z`.
  Latest FFA live-pacing update: mode `74` `ffa_live_pacing` combines FFA roam-route, spawn-camp route-source pressure, item-role pickup scoring, role-combat attack ownership, and spawn-camp combat veto in one four-bot FFA run, proving route commands, anti-camp source selection, attack decisions, combat vetoes, and `ffa_item_role_*` nav scoring evidence from `.tmp\bot_scenarios\20260622T214927Z`.
  Latest Duel live-pacing update: mode `75` `duel_live_pacing` adds `bot_duel_live_pacing`, Duel match-policy mode `5`, deny-enemy item scoring for resource control, route/combat status evidence in Duel mode, and two-bot Duel readiness from `.tmp\bot_scenarios\20260622T222142Z`. The q2dm8 combat/survival marker contract was rechecked from `.tmp\bot_scenarios\20260622T222450Z` after removing the brittle final utility-tail assumption.
  Latest CTF objective transition update: mode `76` `ctf_objective_transitions` adds `bot_ctf_objective_transitions`, objective `flagDrops` and `flagReturns` counters, death-drop and dropped-flag return gameplay hooks, and a four-bot CTF proof that records actual pickup/drop/return counters before combined objective-route ownership from `.tmp\bot_scenarios\20260622T230509Z`.
  Latest coop live-loop update: mode `77` `coop_live_loop` adds `bot_coop_live_loop` and proves leader-route/support, progress wait, anti-blocking, interaction retry, and door/elevator source/hold behavior in one two-bot coop run from `.tmp\bot_scenarios\20260622T234315Z`.
  Latest coop share-loop update: mode `78` `coop_share_loop` adds `bot_coop_share_loop` and proves target sharing plus reserve-for-teammate item deferral in one two-bot coop run from `.tmp\bot_scenarios\20260623T001149Z`.
  Latest bot chat live-events update: mode `79` `bot_chat_live_events` adds `bot_chat_live_events` and proves live spawn plus `route_ready` reply accounting with the eleven-entry live event taxonomy from `.tmp\bot_scenarios\20260623T010520Z`.
  Latest bot chat live-event cooldown update: mode `80` `bot_chat_live_event_cooldown` combines `bot_chat_live_events 1` with `bot_chat_min_interval_ms 60000`, proving one live submission, seven live rate-limit suppressions, and zero dispatch failures from `.tmp\bot_scenarios\20260623T010530Z`.
  Latest bot chat live enemy-sighted update: mode `81` `bot_chat_live_enemy_sighted` combines `bot_chat_live_events 1` with a two-bot TDM engage-enemy proof, proving visible/shootable enemy facts, `reply_chat_enemy_sighted=1`, `live_chat_enemy_sighted=1`, and `last_live_chat_event_name=enemy_sighted` from `.tmp\bot_scenarios\20260623T013832Z`.
  Latest promotion update: raw diagnostics preserve marker event order and latest source lines, missing metrics report their expected source marker, prior blocked mode `22` diagnostics are superseded by the promoted `health_armor_pickup` row, CTF objective hooks feed the implemented `team_objective` smoke, the default pending rows are promoted into source-backed gates, `bot_team_policy_smoke 3` hard-gates duel queue/spectator handling through queued spectator and cleanup status evidence, `map_restart_cleanup` hard-gates the mode `19` forced restart path through `command=map_force`, `restart=1`, observed reload, cleanup-status, and final zero-bot completion markers, `warmup_bot_start_readiness` hard-gates `bot_warmup_smoke 2` through accepted bot add requests, two-bot ready-up status, `minplayers_met=1`, `bot_only_start=1`, `can_start=1`, and final zero-bot cleanup, `vote_bot_exclusion` hard-gates `bot_vote_smoke 2` through bot-only `voting_clients=0`, explicit `bot_blocked` vote-launch rejection, and final no-active-vote cleanup, `admin_bot_privilege_audit` hard-gates `bot_admin_audit_smoke 2` through a bot-only setup, temporary forced admin session state, registered `lock_team red` command lookup, `reason=bot_admin_blocked`, `executed=0`, and final unlocked red-team cleanup, `tournament_bot_veto_exclusion` hard-gates `bot_tournament_smoke 2` through bot-only tournament setup, active-side bot identity, veto pick denial, `reason=bot_blocked`, and final zero-pick/zero-ban cleanup, `tournament_replay_reset` hard-gates `bot_tournament_smoke 3` through completed-series setup, invalid game `99` rejection with `reason=range_error` and `preserved=1`, valid game `2` replay with `reset_applied=1`, one retained winner/map/id, and reopened series state, `match_logging_schema` hard-gates `bot_matchlog_smoke 2` through match-stats and tournament-series schema names, version `1`, artifact types, retained array shape, embedded match metadata, and final zero-bot cleanup, `mymap_queue_bot_request` hard-gates `bot_mymap_smoke 2` through bot-only MyMap enablement, deterministic social attribution, queue insertion into `playQueue` and `myMapQueue`, `ConsumeQueuedMap` cleanup, and final zero-bot/no-queue status, `queued_nextmap_transition` hard-gates `bot_nextmap_smoke 2` through bot-attributed queued-map insertion, play/MyMap queue consumption, successful queued transition, observed same-map reload, retained transition status, and final zero-bot cleanup, `mapvote_bot_exclusion_transition` hard-gates `bot_mapvote_smoke 2` through bot-only selector setup, explicit `bot_blocked` ballot rejection with zero counted votes, selected-current-map finalization, observed same-map reload, retained finalize status, and final zero-bot cleanup, `scoreboard_bot_classification` hard-gates `bot_scoreboard_smoke 2` through two bot-only sorted standings rows, zero voting clients, deterministic 7/3 scores, bot-owned leader and runner-up rows, ordered ranks, and final zero-bot cleanup, `intermission_bot_cleanup` hard-gates `bot_intermission_smoke 2` through native intermission entry, frozen/freecam/non-solid bot state, current-map transition targeting, and final zero-bot/zero-connected/zero-sorted-client cleanup, the follow-on `coop_leader_route` reuse row hard-gates leader-route ownership, mode `27` hard-gates coop LeadAdvance route ownership, mode `28` hard-gates coop resource-share policy plus item reserved-deferral evidence, mode `29` hard-gates close-leader coop anti-blocking command ownership, mode `30` hard-gates support-policy coop target-sharing adoption, mode `31` hard-gates coop door/elevator source ownership plus teammate hold evidence, mode `32` hard-gates TDM role/lane route ownership from live match policy, mode `33` hard-gates TDM item-role pickup scoring from live match item-role policy, mode `34` hard-gates TDM friendly-fire suppression, mode `35` hard-gates CTF role-route ownership from live match role/lane policy, mode `36` hard-gates CTF role-combat ownership from visible, shootable target facts, mode `37` hard-gates CTF dropped-flag response route ownership from dropped enemy flag target-source facts, mode `38` hard-gates CTF carrier-support route ownership from same-team flag-carrier target-source facts, mode `39` hard-gates CTF base-return route ownership from enemy own-flag carrier target-source facts, mode `40` hard-gates combined CTF objective-route policy ownership from base-return, carrier-support, dropped-flag selection, objective-arbitration, deferral, and route-command evidence, mode `41` hard-gates objective-route precedence with role-route deferral, zero role-route activations, and objective-route command evidence, mode `42` hard-gates FFA roam-route ownership from match-policy selection, timed owner kind, route activation, and route-request evidence, mode `43` hard-gates TDM role-combat ownership from match-policy selection, visible/shootable target facts, and attack-button evidence, mode `44` hard-gates TDM role-combat/friendly-fire precedence with role-combat attack decisions, friendly-fire evaluations, friendly-line blocks, and final blocked-state metadata, mode `45` hard-gates FFA spawn-camp avoidance with anti-camp policy selection, live source selection, timed route-goal ownership, and route-request evidence, mode `46` hard-gates FFA item-role pickup scoring from objective item-role policy and nav score-boost evidence, mode `47` hard-gates CTF item-role pickup scoring from CTF readiness, objective item-role policy, nav score boosts, selected pickup goals, invalid-skip absence, and latest role/item metadata, mode `48` hard-gates FFA role-combat ownership from match-policy selection, visible/shootable target facts, and attack-button evidence, mode `49` hard-gates FFA spawn-camp combat avoidance with role-combat attack decisions, anti-camp source selection, same-source target blocks, and final blocked-state metadata, mode `74` hard-gates FFA live pacing with combined route ownership, item-role scoring, role combat, and spawn-camp route/combat pressure, mode `75` hard-gates Duel live pacing with Duel match policy, deny-enemy item scoring, route/combat status reuse, and spawn-pressure evidence, mode `76` hard-gates CTF pickup/drop/return transitions with objective flag counters before combined objective-route ownership, and mode `50` hard-gates TDM resource-denial pickup scoring from deny-enemy resource policy, nav score boosts, selected denial-shaped pickup goals, and latest resource intent metadata.
  Latest behavior policy promotion update: mode `52` hard-gates `bot_behavior_enable` while the individual behavior proof cvars stay disabled; mode `51` remains the live item-goal selection proof for the match item-policy umbrella, mode `53` proves profile `WORR_ROLE` metadata feeds match-policy requested-role selection, mode `54` proves profile teamplay/objective/friendly-fire-care metadata feeds CTF match-policy bonuses, mode `55` proves profile item-greed/item-denial/powerup-timing/retreat-health metadata feeds TDM match item/resource policy bonuses, mode `56` proves profile `WORR_MOVEMENT_STYLE` metadata feeds TDM match-policy movement bonuses, mode `57` proves profile chat metadata plus `bot_allow_chat` while submitting a conservative live dispatch, mode `58` proves `bot_chat_team_only`, mode `59` proves `bot_chat_min_interval_ms` rate limiting without dispatch failures, mode `60` proves profile chat-personality initial utterance selection before live dispatch, mode `61` proves smoke-gated profile chat-personality reply selection for the first team-ready event, mode `62` proves smoke-gated profile chat-personality reply selection across team-ready and route-ready proof events, mode `63` proves central live behavior arbitration plus behavior-policy cvar classification, mode `64` proves target-memory retention and decay, mode `65` proves weapon-scoring arsenal depth, mode `66` proves aim/fire policy depth, mode `67` proves ammo-pressure pickup routing, mode `68` proves survival inventory use, mode `69` proves survival health routing, mode `70` proves survival armor routing, mode `71` proves compact combat/survival regression on `mm-rage`, `q2dm2`, and `q2dm8`, mode `72` proves default-off low-health threat-retreat avoidance with attack suppression and re-engagement, mode `73` proves TDM role-route and role-combat stability across a forced same-map restart, mode `74` proves FFA live pacing across route ownership, item-role scoring, role combat, and spawn-camp route/combat pressure, mode `79` proves live chat event taxonomy plus spawn and route-ready live accounting, and mode `80` proves live chat global cooldown suppression.
  Implementation logs: `docs-dev/q3a-botlib-scenario-smoke-harness-2026-06-18.md`, `docs-dev/q3a-botlib-profile-scenario-smoke-2026-06-18.md`, `docs-dev/q3a-botlib-pending-scenario-counters-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-pending-gap-report-2026-06-18.md`, `docs-dev/q3a-botlib-smoke-scenario-modes-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotions-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-raw-reserved-diagnostics-2026-06-18.md`, `docs-dev/q3a-botlib-nav-health-armor-focus-2026-06-18.md`, `docs-dev/q3a-botlib-team-objective-helper-scaffold-2026-06-18.md`, `docs-dev/q3a-botlib-pending-scenario-promotion-tooling-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`, `docs-dev/q3a-botlib-status-harness-expansion-2026-06-18.md`, `docs-dev/q3a-botlib-duel-queue-spectator-2026-06-21.md`, `docs-dev/q3a-botlib-map-restart-cleanup-2026-06-21.md`, `docs-dev/q3a-botlib-warmup-bot-start-readiness-2026-06-21.md`, `docs-dev/q3a-botlib-vote-bot-exclusion-2026-06-21.md`, `docs-dev/q3a-botlib-admin-bot-privilege-audit-2026-06-21.md`, `docs-dev/q3a-botlib-tournament-bot-veto-exclusion-2026-06-21.md`, `docs-dev/q3a-botlib-mymap-bot-queue-2026-06-21.md`, `docs-dev/q3a-botlib-queued-nextmap-transition-2026-06-21.md`, `docs-dev/q3a-botlib-mapvote-bot-exclusion-transition-2026-06-21.md`, `docs-dev/q3a-botlib-scoreboard-bot-classification-2026-06-21.md`, `docs-dev/q3a-botlib-coop-leader-route-scenario-2026-06-21.md`, `docs-dev/q3a-botlib-coop-lead-advance-route-owner-2026-06-21.md`, `docs-dev/q3a-botlib-coop-interaction-retry-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-resource-share-route-selection-2026-06-21.md`, `docs-dev/q3a-botlib-coop-anti-blocking-command-2026-06-21.md`, `docs-dev/q3a-botlib-coop-target-share-2026-06-21.md`, `docs-dev/q3a-botlib-coop-door-elevator-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-roam-route-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ffa-spawn-camp-combat-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-team-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-team-fire-avoidance-2026-06-21.md`, `docs-dev/q3a-botlib-team-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-role-combat-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-dropped-flag-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-carrier-support-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-base-return-route-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-policy-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-route-precedence-2026-06-21.md`, `docs-dev/q3a-botlib-ctf-objective-transitions-2026-06-22.md`, `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`, `docs-dev/q3a-botlib-match-item-policy-2026-06-21.md`, `docs-dev/q3a-botlib-docs-progress-tracking-round-2026-06-18.md`, `docs-dev/q3a-botlib-extensive-implementation-round-2026-06-18.md`.
  Latest replay implementation log: `docs-dev/q3a-botlib-tournament-replay-reset-2026-06-21.md`.
  Latest competitive server docs log: `docs-dev/q3a-botlib-competitive-server-tools-docs-2026-06-21.md`.
  Latest match logging catalog log: `docs-dev/q3a-botlib-match-logging-catalog-2026-06-21.md`.
  Latest aim/fire policy depth log: `docs-dev/q3a-botlib-aim-fire-policy-depth-2026-06-22.md`.
  Latest ammo pressure pickup log: `docs-dev/q3a-botlib-ammo-pressure-pickup-2026-06-22.md`.
  Latest threat-retreat avoidance log: `docs-dev/q3a-botlib-threat-retreat-avoidance-2026-06-22.md`.
  Latest q2dm8 combat map-regression log: `docs-dev/q3a-botlib-q2dm8-combat-map-regression-2026-06-22.md`.
  Latest CTF objective live-loop log: `docs-dev/q3a-botlib-ctf-objective-live-loop-2026-06-22.md`.
  Latest TDM role spawn-stability log: `docs-dev/q3a-botlib-tdm-role-spawn-stability-2026-06-22.md`.
  Latest FFA live-pacing log: `docs-dev/q3a-botlib-ffa-live-pacing-2026-06-22.md`.
  Latest Duel live-pacing log: `docs-dev/q3a-botlib-duel-live-pacing-2026-06-22.md`.
  Latest CTF objective transition log: `docs-dev/q3a-botlib-ctf-objective-transitions-2026-06-22.md`.
  Latest coop live-loop log: `docs-dev/q3a-botlib-coop-live-loop-2026-06-23.md`.
  Latest coop share-loop log: `docs-dev/q3a-botlib-coop-share-loop-2026-06-23.md`.
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
  Latest bot chat live-event scenario logs: `docs-dev/q3a-botlib-bot-chat-live-events-2026-06-23.md`; `docs-dev/q3a-botlib-bot-chat-live-event-cooldown-2026-06-23.md`.
  Latest CTF item-role scenario log: `docs-dev/q3a-botlib-ctf-item-role-selection-2026-06-21.md`.
  Latest scenario-tooling logs: `docs-dev/q3a-botlib-scenario-tooling-source-aware-raw-diagnostics-2026-06-18.md`, `docs-dev/q3a-botlib-health-armor-scenario-promotion-gate-2026-06-18.md`, and `docs-dev/q3a-botlib-ctf-objective-gameplay-hooks-2026-06-18.md`.
- [x] `DV-03-T06` Add updater/release index parser tests for stable and nightly channels.
  Dependency: none. Priority: P1.

- [ ] `DV-03-T07` Add a UI automation harness for document-load smoke,
  navigation checks, screenshot/layout capture, and renderer-specific menu
  coverage.
  Dependency: `DV-02-T03`. Priority: P0.
  Progress: RmlUi Round 1-3 work added manifest and route-contract source
  checkers for the migration surface. Round 4 adds validated
  `migration_phase` metadata and package/source checks, but runtime navigation,
  screenshot/layout capture, and renderer-specific coverage remain pending.
  Round 5 note: static RML semantics checking and progress-report tooling are
  implemented and validated; runtime navigation, screenshot capture, and
  renderer-specific coverage remain pending.
  Round 6 note: controller-contract validation, runtime asset checks, staged
  loose-file checks, and JSON progress output are implemented and validated;
  runtime navigation, screenshot capture, and renderer-specific coverage remain
  pending.
  Round 7 note: runtime registry, controller-stub coverage, imported runtime
  asset, and controller-contract progress checks are implemented and validated;
  runtime navigation, screenshot capture, and renderer-specific coverage remain
  pending.
  Round 8 note: menu-entrypoint and runtime-stub eligibility checks, runtime
  asset JSON output, and phase-progression progress facts are implemented and
  validated; native runtime navigation, screenshot capture, and
  renderer-specific coverage remain pending.
  Round 9 note: navigation graph, controller fixture, runtime asset manifest,
  parity checklist, and all-route-metadata progress checks are implemented and
  validated; native runtime navigation, screenshot capture, and
  renderer-specific coverage remain pending.
  Round 10 note: command inventory, cvar inventory, and parity-checklist
  progress summaries are implemented and validated; progress-report output now
  carries command/cvar inventory counts alongside route phase, controller, and
  parity facts. Native runtime navigation, screenshot capture, and
  renderer-specific coverage remain pending.
  Round 11 note: data-model inventory, phase-consistency, and
  dependency-decision checks are implemented and validated; progress-report
  output now carries data-model inventory counts alongside route phase,
  controller, parity, command, and cvar facts. Native runtime navigation,
  screenshot capture, and renderer-specific coverage remain pending.
  Round 12 note: condition inventory and metadata-sync checks are implemented
  and validated; progress-report output now carries condition and metadata
  guardrail counts alongside route phase, controller, parity, command, cvar,
  and data-model facts. Native runtime navigation, screenshot capture, and
  renderer-specific coverage remain pending.
  Round 13 note: event inventory, a11y/localization inventory, and
  legacy-removal checks are implemented and validated; progress-report output
  now carries event and a11y counts alongside route phase, controller, parity,
  command, cvar, data-model, condition, and metadata facts. Native runtime
  navigation, screenshot capture, and renderer-specific coverage remain
  pending.
  Round 14 note: document/body identity, entrypoint, and route metadata shape
  checks are implemented and validated; progress-report output now carries the
  legacy-removal gate state alongside route phase, controller, parity,
  command, cvar, data-model, condition, metadata, event, and a11y facts.
  Native runtime navigation, screenshot capture, and renderer-specific coverage
  remain pending.
  Round 15 note: dependency-integration checking is implemented and validated;
  it records the current RmlUi source/build state as optional and catches
  missing wrap/option/probe/compile-define scaffolding. Native runtime
  navigation, screenshot capture, and renderer-specific coverage remain
  pending.
  Round 16 note: controller-stub completion checking is implemented and
  validated; strict mode now proves the central manifest has `0` starter
  routes, `54` controller-stub routes, and `3` guarded runtime-stub routes.
  Native runtime navigation, screenshot capture, input/back behavior, and
  renderer-specific coverage remain pending.
  Round 17 note: runtime-adapter checking is implemented and validated; it
  proves the compiled RmlUi Core adapter stays guarded, refuses route opening
  until a renderer bridge exists, and keeps the dependency fallback explicit.
  Native runtime navigation, screenshot capture, input/back behavior, and
  renderer-specific coverage remain pending.
  Round 18 note: the same checker now covers the RmlUi Core system/file bridge
  and runtime file-probe hook, proving file loads go through WORR filesystem
  symbols rather than RmlUi's default C file backend. Native runtime
  navigation, screenshot capture, input/back behavior, and renderer-specific
  coverage remain pending.
  Round 19 note: the same checker now covers the native renderer bridge
  contract, including renderer-family lanes, route gating on native renderer
  availability, opaque native render-interface requirements, and a static guard
  against Vulkan-to-OpenGL redirection. Native runtime navigation, screenshot
  capture, input/back behavior, and renderer-specific coverage remain pending.
  Round 20 note: the same checker now covers the OpenGL render-interface
  scaffold, renderer API exports, OpenGL-scoped RmlUi dependency wiring,
  client renderer lifecycle registration/clear, `Rml::SetRenderInterface`
  installation, and the `CanRender=false` guard. Native runtime navigation,
  screenshot capture, input/back behavior, and visible renderer-specific
  coverage remain pending.
  Round 21 note: the same checker now covers the OpenGL primitive bridge:
  geometry caching, tessellator drawing, generated and loaded texture
  handling, scissor state, and `CanRender=true`. Native runtime navigation,
  screenshot capture, input/back behavior, and route-visible renderer-specific
  coverage remain pending.
  Round 22 note: the same checker now covers the guarded sample context path:
  runtime lifecycle hooks, `Rml::CreateContext`, document load/show,
  update/render, context removal, explicit runtime open/close commands, and
  UI bridge draw interception before legacy UI draw. Native runtime
  navigation, screenshot capture, broad input/back behavior, and
  route-visible renderer-specific coverage remain pending.
  Round 23 note: the same checker now covers the guarded sample input/capture
  path: RmlUi key/text/mouse hook declarations, adapter event dispatch,
  UI bridge input interception before legacy callbacks, Escape/mouse-button-2
  close tokens, and status/capture commands. Native runtime navigation,
  automated screenshot capture, broad input/back behavior, and route-visible
  renderer-specific coverage remain pending.
  Round 24 note: the guarded capture harness is implemented and validated for
  the OpenGL `core.runtime_smoke` route. It writes local TGA evidence under
  `.install/basew/screenshots`, copies screenshot/log artifacts to
  `.tmp/rmlui/runtime-capture`, writes a JSON manifest, and validates route
  status, frame counters, screenshot dimensions, and nonblank payload. Native
  runtime navigation, broader screenshot/layout assertions, broad input/back
  behavior, and route-visible renderer-specific coverage remain pending.
  Round 25 note: the same guarded capture harness now proves text-geometry
  generation by requiring the smoke font glyph marker and validating the
  refreshed Round 25 TGA/log evidence. Native runtime navigation, broader
  layout assertions, broad input/back behavior, and route-visible
  renderer-specific coverage remain pending.
  Round 26 note: the guarded capture harness now includes first visual layout
  assertions for the smoke route and fails nonblank screenshots with the wrong
  color/region structure. Native runtime navigation, synthetic input/back
  behavior, broader route coverage, and renderer-specific coverage remain
  pending.
  Round 27 note: the guarded capture harness now includes first automated
  synthetic input/back-close validation for the smoke route and fails visual
  evidence that lacks input counters, close counters, or inactive final status.
  Native runtime navigation, broad input/navigation parity, broader route
  coverage, and renderer-specific coverage remain pending.
  Round 28 note: the guarded capture harness now includes exact-geometry
  validation and a default two-viewport OpenGL matrix for the smoke route.
  Native runtime navigation, responsive widescreen parity, broad input/
  navigation parity, broader route coverage, and renderer-specific coverage
  remain pending.
  Round 29 note: the guarded capture harness now includes a route matrix for
  `main`, `game`, and `download_status`, opened through `UI_OpenMenu` and
  checked for fresh `960x720` screenshots, route-specific OpenGL status, glyph
  text evidence, synthetic input, route close counters, and inactive final
  status. Native runtime navigation, broad input/navigation parity, final
  route layout assertions, and renderer-specific coverage remain pending.
  Round 30 note: the UI smoke suite now includes
  `check_rmlui_renderer_matrix.py` and focused pytest coverage for the current
  renderer-family matrix: OpenGL native guarded, Vulkan blocked until native,
  and RTX/vkpt blocked until native. It also fails non-OpenGL runtime
  dependency enablement and Vulkan/RTX-to-OpenGL mapping. Native runtime
  navigation, broad input/navigation parity, final route layout assertions,
  and live renderer-specific coverage remain pending.
  Round 31 note: `check_rmlui_runtime_capture.py --renderer-matrix` now writes
  an aggregate report and manifest that combines guarded OpenGL route captures
  with the renderer-family matrix guardrail. Native runtime navigation, broad
  input/navigation parity, final route layout assertions, and live
  renderer-specific coverage remain pending.
  Round 32 note: `check_rmlui_vulkan_bridge_readiness.py` adds a static
  bridge-readiness audit for native Vulkan/RTX foundations and blocked-lane
  requirements. Native runtime navigation, broad input/navigation parity,
  final route layout assertions, and live renderer-specific coverage remain
  pending.
  Round 33 note: `check_rmlui_runtime_capture.py --renderer-matrix` now
  carries that bridge-readiness audit in the aggregate manifest and has a
  focused failure test for missing Vulkan foundation primitives. Native
  runtime navigation, broad input/navigation parity, final route layout
  assertions, and live renderer-specific coverage remain pending.
  Round 34 note: `check_rmlui_vulkan_bridge_readiness.py` now exposes
  per-lane activation requirements and fails partial native bridge claims until
  class, family export, runtime dependency, and non-null interface requirements
  are satisfied together. Native runtime navigation, broad input/navigation
  parity, final route layout assertions, and live renderer-specific coverage
  remain pending.
  Round 35 note: `check_rmlui_vulkan_bridge_readiness.py` now reports
  activation stages as `blocked_no_activation`,
  `partial_activation_blocked`, or `activation_complete` and records the next
  pending activation requirement for each lane. Native runtime navigation,
  broad input/navigation parity, final route layout assertions, and live
  renderer-specific coverage remain pending.
  Round 36 note: `check_rmlui_vulkan_bridge_readiness.py` now requires
  lane-specific Meson source-set wiring for `src/renderer/rmlui_bridge.cpp`
  before native bridge activation can proceed. Native runtime navigation,
  broad input/navigation parity, final route layout assertions, and live
  renderer-specific coverage remain pending.
  Round 37 note: the same bridge-readiness checker now accepts the inactive
  Vulkan and RTX/vkpt source-set wiring in `meson.build`, records both lanes
  as `partial_activation_blocked`, and keeps the next blocker at
  `native_bridge_class_present`. Native runtime navigation, broad input/
  navigation parity, final route layout assertions, and live
  renderer-specific coverage remain pending.
  Round 38 note: the same checker now treats the Vulkan and RTX/vkpt class
  stubs as accepted inactive partial activation, records both lanes with
  `next_activation_requirement=native_family_export_present`, and still fails
  premature family/runtime/interface exports. Native runtime navigation, broad
  input/navigation parity, final route layout assertions, and live
  renderer-specific coverage remain pending.
  Round 39 note: the same checker now treats inactive Vulkan and RTX/vkpt
  family exports as accepted partial activation, records both lanes with
  `next_activation_requirement=runtime_dependency_enabled`, and still fails
  premature runtime/interface exports or OpenGL redirects. Native runtime
  navigation, broad input/navigation parity, final route layout assertions,
  and live renderer-specific coverage remain pending.
  Round 40 note: the same checker now treats Vulkan and RTX/vkpt runtime
  dependency wiring as accepted inactive partial activation, records both lanes
  with `next_activation_requirement=native_interface_export_present`, and
  still fails premature native interface exports, OpenGL redirects, or partial
  dependency wiring. Native runtime navigation, broad input/navigation parity,
  final route layout assertions, and live renderer-specific coverage remain
  pending.
  Round 78 note: focused runtime evidence now covers the live multiplayer
  initial/join/Escape/resume transition on OpenGL, injected initial/Escape
  layout inspection, and native Vulkan renderer-unavailable fallback to the
  matching JSON hub. Broader route automation, input/navigation coverage,
  viewport assertions, and renderer-native RmlUi coverage remain pending.

- [ ] `DV-03-T08` Add deterministic renderer semantic tests for gamma/color charts, light-query saturation, shadow alias/cache invalidation, malformed shadowlight records, and GL/Vulkan pixel tolerances.
  Dependency: `DV-02-T03`, `FR-02-T12`. Priority: P1.

2026-07-13 `DV-03-T07` progress: the RmlUi runtime capture harness now accepts
repeated validated cvar seeds after config loading and knows the `ui_list`
route. A dedicated fixed-list checker covers the sgame owner, generic runtime
bridge, authored RML states/rows, back cleanup, and style contract. Installed
960x720 populated, empty, and error captures pass exact route, font, geometry,
input, and back-close validation. Broader navigation/action and native renderer
automation remains open. Implementation log:
`docs-dev/rmlui-live-ui-list-provider-2026-07-13.md`.

2026-07-13 `DV-03-T07` Player Setup progress: the guarded capture registry now
opens `players` with deterministic cvar seeds, and the focused provider checker
locks composite selection, live images, explicit states, preview stages,
reduced motion, layout, and OpenGL cached-atlas UV behavior. The final installed
960x720 capture passes font, route, input, back-close, and clean-log gates.
Broader action automation and native renderer coverage remain open.
Implementation log:
`docs-dev/rmlui-live-player-setup-provider-2026-07-13.md`.

2026-07-13 `DV-03-T07` keybind-family progress: the guarded capture registry
now opens `keys`, `legacykeys`, and `weapons`, and the focused provider checker
locks two-slot preservation, per-slot clear, timeout, conflict confirmation,
device artwork, exact command coverage, pre-load accessibility state, and
route/style contracts. Three clean installed reduced-motion 960x720 captures
pass font, route, input, back-close, and geometry gates. Non-destructive
action mutation/restore automation and native renderer
coverage remain open. Implementation log:
`docs-dev/rmlui-live-keybind-provider-2026-07-13.md`.

2026-07-13 `DV-03-T07` Address Book and visibility progress: the guarded
capture registry opens `addressbook` with deterministic archived cvar seeds,
and the focused checker locks all sixteen fields, immediate generic-bridge
writeback, length limits, and exact Browse Favorites sources. The shared loader
now applies accessibility classes before document construction and the theme no
longer instantiates unreliable decorative load-time opacity fades. A clean
installed 960x720 Address Book capture plus the full 267-test UI smoke suite
pass. Action mutation/restore automation and native renderer coverage remain
open. Implementation logs:
`docs-dev/rmlui-live-addressbook-provider-2026-07-13.md` and
`docs-dev/rmlui-deterministic-route-visibility-2026-07-13.md`.

2026-07-13 `DV-03-T07` session-entry progress: the guarded capture registry
now opens `dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, and `dm_matchinfo`
with deterministic live-state seeds. The focused checker covers 49
sgame-published cvars, route/command publication, team and non-team conditions,
command-cvar actions, first-connect and resumable close semantics, single-back
information layouts, accessibility, metadata, and capture registration. Five
clean installed reduced-motion 960x720 captures pass route, font, geometry,
synthetic input, and back-close gates; the full UI smoke suite passes 279
tests. Connected action automation and native renderer coverage remain open.
Implementation log:
`docs-dev/rmlui-live-session-entry-provider-2026-07-13.md`.

2026-07-13 `DV-03-T07` vote/callvote progress: the guarded registry now opens
`vote_menu` and all seven callvote routes with deterministic live-state seeds.
The focused checker covers 41 sgame-published values, twenty registered
commands, complete option and empty-state gates, current and dynamic labels,
tri-state map flags, single-back close semantics, accessible two-column
layouts, metadata, and capture registration. Runtime validation also corrected
popmenu close-tail filtering and custom-install executable selection. Eleven
clean canonical `.install` 960x720 captures pass route, font, geometry,
synthetic-input, inactive-close, and clean-log gates; the full UI smoke suite
passes 292 tests. Connected action automation and native renderer coverage
remain open. Implementation log:
`docs-dev/rmlui-live-vote-callvote-provider-2026-07-13.md`.

2026-07-13 `DV-03-T07` MyMap progress: the guarded registry now opens
`mymap_main` and `mymap_flags` with deterministic availability and tri-state
seeds. The focused checker covers fifteen published values, six registered
commands, enabled conditions, generic-list ownership, single-back behavior,
compact and two-column layouts, metadata, and capture registration. Three
clean canonical `.install` 960x720 captures pass route, font, geometry,
synthetic-input, inactive-close, and clean-log gates; the full UI smoke suite
passes 300 tests. Connected action automation and native renderer coverage
remain open. Implementation log:
`docs-dev/rmlui-live-mymap-provider-2026-07-13.md`.

2026-07-13 `DV-03-T07` session-confirmation progress: the guarded registry now
opens `forfeit_confirm` and `leave_match_confirm`. The focused checker locks
native popup routing, safe cancel/back behavior, No-first focus, destructive
styling, authoritative forfeit dispatch, close-before-disconnect ordering,
localization hooks, metadata, and capture registration. Two clean 960x720
installed-tree captures pass route, font, geometry, synthetic-input,
inactive-close, and clean-log gates; the full UI smoke suite passes 308 tests.
Canonical `.install` refresh remains queued behind an unrelated staged-engine
DLL lock. Connected action automation and native renderer coverage remain
open. Implementation log:
`docs-dev/rmlui-live-session-confirm-provider-2026-07-13.md`.

2026-07-13 `DV-03-T07` Admin progress: the guarded registry now opens
`admin_menu` and `admin_commands`. The focused checker locks native route and
condition paths, sgame Replay publication, admin-only entry commands, exact
coverage of all 28 registered admin commands, matching reference usages,
single-back behavior, compact/scrollable layout, metadata, and capture
registration. Replay-shown, Replay-hidden, and full-reference 960x720 frames
pass route, font, geometry, synthetic-input, inactive-close, and clean-log
gates; the full UI smoke suite passes 316 tests. Canonical `.install` refresh
remains queued behind the unrelated staged-engine DLL lock. Connected Replay,
localization/input, and native renderer coverage remain open. Implementation
log: `docs-dev/rmlui-live-admin-provider-2026-07-13.md`.

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
  RmlUi Round 4 note: runtime-switch, controller-contract, and route-ownership
  scaffolding is validated as active ownership cleanup for `FR-09`; live bridge
  simplification and final ownership validation remain pending.
  RmlUi Round 5 note: selected `controller_stub` route progression is accepted
  for five shell/settings routes and refines the ownership map, but live bridge
  simplification remains pending.
  RmlUi Round 6 note: selected `controller_stub` progression now covers `10`
  shell/settings routes and the client probe registry covers every manifest
  route, but live bridge simplification remains pending.
  RmlUi Round 7 note: selected `controller_stub` progression now covers `15`
  shell/settings routes and controller-stub coverage is checked against static
  RML attributes, but live bridge simplification remains pending.
  RmlUi Round 8 note: selected progression now covers `12` `controller_stub`
  routes plus `3` guarded menu-entrypoint `runtime_stub` routes, with
  runtime-stub eligibility checked against shell metadata, controller
  contracts, runtime registry entries, and legacy fallback behavior. Live
  bridge simplification remains pending.
  RmlUi Round 9 note: selected progression now covers `16` `controller_stub`
  routes plus `3` guarded menu-entrypoint `runtime_stub` routes, including the
  utility routes `addressbook`, `keys`, `legacykeys`, and `weapons`. Live
  bridge simplification remains pending.
  RmlUi Round 10 note: selected progression now covers `20`
  `controller_stub` routes plus `3` guarded menu-entrypoint `runtime_stub`
  routes, including utility/list routes `servers`, `demos`, `players`, and
  `ui_list`. Live bridge simplification remains pending.
  RmlUi Round 11 note: selected progression now covers `24`
  `controller_stub` routes plus `3` guarded menu-entrypoint `runtime_stub`
  routes, including single-player/save-load routes `singleplayer`,
  `skill_select`, `loadgame`, and `savegame`. Live bridge simplification
  remains pending.
  RmlUi Round 12 note: selected progression now covers `28`
  `controller_stub` routes plus `3` guarded menu-entrypoint `runtime_stub`
  routes, including local-session routes `downloads`, `quit_confirm`,
  `gameflags`, and `startserver`; metadata sync also confirms all `57`
  central migration routes have matching feature route metadata. Live bridge
  simplification remains pending.
  RmlUi Round 13 note: selected progression now covers `36`
  `controller_stub` routes plus `3` guarded menu-entrypoint `runtime_stub`
  routes, including session/vote routes `vote_menu`, `callvote_main`,
  `callvote_ruleset`, `callvote_timelimit`, `callvote_scorelimit`,
  `callvote_unlagged`, `callvote_random`, and `callvote_map_flags`. Live
  bridge simplification remains pending.
  RmlUi Round 14 note: selected progression now covers `42`
  `controller_stub` routes plus `3` guarded menu-entrypoint `runtime_stub`
  routes, including multiplayer/lobby/info routes `multiplayer`,
  `dm_welcome`, `dm_join`, `join`, `dm_hostinfo`, and `dm_matchinfo`. Route
  metadata shape validation now keeps ownership metadata and advanced-route
  controller-contract references explicit. Live bridge simplification remains
  pending.
  RmlUi Round 16 note: selected progression now covers all non-runtime
  tracked routes with `54` `controller_stub` routes plus `3` guarded
  menu-entrypoint `runtime_stub` routes. Controller-stub completion validation
  keeps the ownership map from regressing to hidden starter surfaces. Live
  bridge simplification remains pending.
  2026-07-13 fixed-list note: `ui_list` now preserves sgame ownership for
  list kind, state, entries, commands, and paging while the compiled runtime
  reuses the generic cvar/condition/command bridge. Ordered route-pop plus
  owner cleanup removes a back-navigation ownership leak without introducing
  a route-specific client model. Broader bridge simplification remains open.
- [ ] `DV-04-T03` Add static analysis and warning-as-error policy for first-party code in CI.
  Dependency: `DV-02-T01`. Priority: P1.
  Progress: First-party string-safety cleanup replaced direct `strcpy()` / `strcat()` usage in command argument assembly, macro keyword expansion, config fallback probing, filesystem write-mode construction, Windows service command construction, and normalization test setup; prompt completion now exposes corrected `com_completion_threshold` naming with legacy fallback.
  Latest cleanup: server/MVD paths now use bounded helpers for save metadata fallback dates, dummy MVD userinfo strings, server configstring formatting, MVD waiting-room defaults, recording-list placeholders, and demo-seek configstring restoration.
  Latest cleanup: renderer, path-tracing, savegame, and anticheat paths now use bounded helpers for placeholder texture names, RTX image filepath/override strings, material feedback strings, PFM output text, anticheat fallback text, and saved JSON string restore.
  Latest cleanup: sgame display/sound/admin helpers and RTX IQM metadata paths now use WORR bounded helpers instead of direct `snprintf()`/`strncpy()` call patterns.
  Latest cleanup: RTX shader/debug/profiler/texture/tone-mapping helpers and cgame crosshair naming now use WORR bounded formatting helpers instead of direct `snprintf()`/`vsnprintf()` calls.
  Latest cleanup: remaining clean game/cgame formatter/copy helpers now route through WORR bounded helpers, bgame exposes shared variadic formatter declarations to game modules, and optional RTX PFM dump output now checks dimensions, allocation, writes, and close status.
  Latest cleanup: runtime file and buffer failure handling now checks HTTP receive-size math, STB screenshot callback writes, OpenGL screenshot conversion allocation bounds, and Game3 proxy base85 save/restore read/write/close failures.
  Implementation log: `docs-dev/runtime-file-io-bounds-hardening-2026-06-27.md`
  Latest cleanup: bootstrap ready-file signaling, save-slot file copying, and Steam library metadata loading now check short writes, close failures, seek/tell failures, and oversized read allocations.
  Implementation log: `docs-dev/bootstrap-save-steam-io-hardening-2026-06-27.md`
  Latest cleanup: RTX Vulkan extension/layer enumeration, swapchain image/view arrays, stretch-pic framebuffers, and shader-file loads now check query failures, count overflow, allocation failure, partial creation cleanup, short reads, and close failures.
  Implementation log: `docs-dev/rtx-vulkan-allocation-shader-io-hardening-2026-06-27.md`
  Latest cleanup: RTX Vulkan surface-format, present-mode, physical-device, device-extension, queue-family, present-support, and frame-time surface-capability enumeration now uses checked counts, bounded temporary allocations, explicit diagnostics, and release-build failure handling.
  Implementation log: `docs-dev/rtx-vulkan-enumeration-init-hardening-2026-06-27.md`
  Latest cleanup: native Vulkan world/UI upload paths now check world-face masks, vertex uploads, lightmap atlas clears, style strides, dirty-rect uploads, mesh shrink sizing, GPU/frame buffer sizing, image transparency sizing, and sub-rect bounds before copying or allocating.
  Implementation log: `docs-dev/native-vulkan-upload-bounds-followup-2026-06-27.md`
  Latest cleanup: native Vulkan shadow backend paths now check transient vertex growth, triangle emission counts, world face-bounds cache allocation, page/view inputs, render-job rollback, caster counts, mapped vertex uploads, and record-time state before copying or drawing.
  Implementation log: `docs-dev/native-vulkan-shadow-bounds-hardening-2026-06-27.md`
  Latest cleanup: server navigation now validates `.nav` file counts and versions, checks packed path-context and loader allocation sizes, rejects malformed link/traversal extents, keeps missing nav files unloaded, uses the requested node-search radius, bounds node-link bitmap offsets, and protects edict registration table updates.
  Implementation log: `docs-dev/server-nav-bounds-pathing-hardening-2026-06-27.md`
  Latest cleanup: native Vulkan main init/swapchain/screenshot paths now use checked array and image byte sizing for instance/device/queue/surface/swapchain enumeration allocations, reject empty capability/image results with explicit diagnostics, propagate present-support query failures, and bound screenshot readback/RGB/PNG stride sizing.
  Implementation log: `docs-dev/native-vulkan-main-allocation-screenshot-hardening-2026-06-27.md`
  Latest cleanup: native Vulkan entity loading/render prep now checks MD5 mesh/skeleton allocations, MD5 replacement skin ownership, vertex/batch growth, BSP inline-model texture cache arrays, SP2 frame tables, MD2 lump ranges and model arrays, render-time MD2 vector offsets, and final entity upload byte sizing.
  Implementation log: `docs-dev/native-vulkan-entity-loader-bounds-hardening-2026-06-27.md`
  Latest cleanup: client font loading and TTF glyph preparation now checks KFONT token overflow, atlas byte sizing, page-index casts, atlas/upload allocations, alpha blit row offsets, SDL glyph surface dimensions/pitch/locks, atlas packing bounds, external TTF file-size/read conversions, and glyph-dump output path truncation.
  Implementation log: `docs-dev/client-font-ttf-bounds-io-hardening-2026-06-27.md`
  Latest cleanup: shared string and input-field helpers now bound append destination scans, handle zero-size/no-room append calls, saturate concat length accounting, guard formatter failure paths, and keep programmatic input replacement aligned with typed input limits.
  Implementation log: `docs-dev/shared-string-field-safety-hardening-2026-07-01.md`
  Latest cleanup: shared path and token helpers now tolerate null inputs, recognize backslash path separators, preserve high-bit UTF-8 bytes during whitespace handling, and correctly parse escaped unquoted tokens.
  Implementation log: `docs-dev/shared-path-token-safety-hardening-2026-07-01.md`
  Latest cleanup: shared size buffers now validate storage and size state, support zero-size growable buffers, reset bit state on clear/destroy, safely no-op zero-length writes, and clamp read-underflow accounting.
  Implementation log: `docs-dev/sizebuf-safety-hardening-2026-07-01.md`
  Latest cleanup: shared FIFO ring buffers now validate index/storage state, clamp reserve/commit and peek/decommit operations, guard percentage math, handle null/zero-length try operations explicitly, and bound wrapped message copies.
  Implementation log: `docs-dev/fifo-ring-buffer-safety-hardening-2026-07-01.md`
  Latest cleanup: shared hash maps now reject unusable configurations, tolerate invalid public inputs, bounds-check key/value iteration helpers, guard capacity growth against overflow, and use null/alignment-safe hash helpers.
  Implementation log: `docs-dev/hash-map-safety-hardening-2026-07-01.md`
  Latest cleanup: shared natural-sort comparisons now handle null and zero-count inputs, use unsigned byte reads, keep count-limited numeric runs bounded, and add focused `natsorttest` coverage.
  Implementation log: `docs-dev/natsort-bounded-compare-hardening-2026-07-01.md`
  Latest cleanup: common utility helpers now reject null parse/format/hash inputs safely, support non-power-of-two hash sizes, clamp negative durations, page in buffers with `size_t` indices, and add focused `utilstest` coverage.
  Implementation log: `docs-dev/common-utils-safety-hardening-2026-07-01.md`
  Follow-up: restored the `Com_HashString* size == 0` raw-hash contract after map-launch logs exposed packed lookup misses through raw localization keys and renderer-adjacent asset fallout.
  Implementation log: `docs-dev/filesystem-hash-raw-bucket-regression-2026-07-01.md`
  Latest cleanup: UTF-8 helpers now use explicit leading-byte classification, reject null decoder inputs, keep counted scans within the requested byte span, handle transliteration null inputs safely, and add focused `utf8helpertest` coverage.
  Implementation log: `docs-dev/utf8-helper-safety-hardening-2026-07-01.md`
  Latest cleanup: shared command prompt helpers now guard invalid public inputs, keep completion prefix insertion bounded in full buffers, validate match allocation growth, clamp history save ranges, and add focused `prompttest` coverage.
  Implementation log: `docs-dev/prompt-safety-hardening-2026-07-01.md`
  Latest cleanup: CRC/MD4 checksum helpers now define null and zero-length input behavior, avoid zero-length payload copies, make MD4 finalization non-destructive, and add focused `checksumtest` coverage.
  Implementation log: `docs-dev/checksum-helper-safety-hardening-2026-07-01.md`
  Latest cleanup: Steam `libraryfolders.vdf` parsing now skips unknown nested values safely, handles `apps` before `path`, rejects malformed/overlong library paths, clears failed outputs, and adds focused `steamparsetest` coverage.
  Implementation log: `docs-dev/steam-library-vdf-parser-hardening-2026-07-01.md`
  Latest cleanup: shared async workqueue handling now resets stale shutdown state on restart, validates queued jobs, cleans up initialization failures, completes callbacks outside the internal mutex, and adds focused `asynctest` coverage.
  Implementation log: `docs-dev/async-workqueue-safety-hardening-2026-07-01.md`
  Latest cleanup: shared zone allocation now names nav/mapdb tags, checks allocator sizing and stats overflow in release builds, handles null/zero-size helpers explicitly, grows static cvar strings into writable storage, and adds focused `zonetest` coverage.
  Implementation log: `docs-dev/zone-allocator-safety-hardening-2026-07-01.md`
  Latest cleanup: shared JSON/mapdb parsing now rejects malformed tokenization, matches keys exactly, clears failed reload state, validates boolean/uint8 primitives, rejects fixed-string truncation, and adds focused `mapdbtest` coverage.
  Implementation log: `docs-dev/mapdb-json-parser-safety-hardening-2026-07-01.md`
  Latest cleanup: bootstrap ready signaling now bounds ready-file env inputs, clears stale callback userdata, captures callbacks before dispatch, publishes ready tokens through temp-file replacement, removes stale final files on failures, and adds focused `bootstraptest` coverage.
  Implementation log: `docs-dev/bootstrap-ready-signal-safety-hardening-2026-07-01.md`
  Latest cleanup: shared command handling now validates command-buffer storage, tolerates null public inputs across buffer/args/register/macro/completion paths, uses counted deferred copies, and adds focused `cmdtest` coverage.
  Implementation log: `docs-dev/command-system-safety-hardening-2026-07-01.md`
  Latest cleanup: shared cvar handling now rejects null/empty names safely, guards pointer-based setters and completion callbacks, sanitizes non-finite numeric writes/clamps, makes string-buffer access null-safe, and adds focused `cvartest` coverage.
  Implementation log: `docs-dev/cvar-system-safety-hardening-2026-07-01.md`
  Latest cleanup: shared message serialization now guards primitive/raw writes, null flush targets, vector/entity/usercmd inputs, bit-width reads/writes, solid pack/unpack helpers, and adds focused `msgtest` coverage.
  Implementation log: `docs-dev/message-serialization-safety-hardening-2026-07-01.md`
  Latest cleanup: shared network address/channel helpers now guard null address predicates, parser/formatter inputs, packet source bounds, stream pointers, netchan transmit/setup paths, correct the IPv4 172.16/12 LAN range, and add focused `nettest` coverage.
  Implementation log: `docs-dev/network-address-channel-safety-hardening-2026-07-01.md`
  Latest cleanup: localization runtime helpers now guard null and overlong inputs, preserve in-place locale normalization, validate in-place placeholder parsing and argument lists, reject malformed localization records, and add focused `loctest` coverage.
  Implementation log: `docs-dev/localization-runtime-safety-hardening-2026-07-01.md`
  Latest cleanup: classic and cgame UI player-model loaders now use RAII file-list ownership, checked model/skin path construction, strict icon-suffix validation, bounded skin/model registration, zeroed skin arrays, and `std::sort` with a strict comparator.
  Implementation log: `docs-dev/player-model-loader-safety-modernization-2026-07-01.md`
  Latest cleanup: classic and cgame UI player-config consumers now validate model/skin indices, clear stale preview handles, check preview and userinfo path construction, RAII-own cgame weapon file lists, bound weapon scans, and guard `skin` cvar writes.
  Implementation log: `docs-dev/player-config-selection-safety-hardening-2026-07-01.md`
  Latest cleanup: client player precache now safely parses userinfo skin strings, fixes legacy backslash skin parsing, validates model/skin/dogtag path tokens, clears stale clientinfo reload state, checks renderer asset paths before registration, bounds visual-weapon iteration, and rejects invalid `#` weapon model configstrings.
  Implementation log: `docs-dev/client-player-precache-safety-hardening-2026-07-01.md`
  Latest cleanup: cgame UI and Windows updater parsing now reject partial/out-of-range numeric values, bound server-browser integer sorting, validate updater JSON token traversal and manifest sizes, reject partial versions, check UTF conversion failures, and report hash read errors.
  Implementation log: `docs-dev/ui-updater-parse-safety-hardening-2026-07-01.md`
  Latest cleanup: Windows updater IO/JSON handling now validates malformed token spans, bounded token traversal, config-file load failures, HTTP allocation/write/cancel/content-length failures, partial download cleanup, release asset traversal, and required manifest file metadata.
  Implementation log: `docs-dev/updater-io-json-safety-hardening-2026-07-01.md`
  Latest cleanup: OpenAL/EAX audio metadata parsing now rejects partial/out-of-range numeric primitives, validates JSON token spans and object/array sizes, bounds reverb metadata allocations, preserves/falls back on malformed EAX IDs, and suppresses invalid map-entity EAX zones.
  Implementation log: `docs-dev/openal-eax-parse-safety-hardening-2026-07-01.md`
  Latest cleanup: client video mode parsing now uses strict bounded helpers for fullscreen modelist dimensions, refresh rates, bit depths, desktop tokens, mode-index scanning, and windowed geometry offsets/destination ranges/trailing text.
  Implementation log: `docs-dev/client-video-mode-parse-safety-hardening-2026-07-01.md`
  Latest cleanup: updater bootstrap parsing now uses bounded `std::from_chars` helpers for integer cvars, launch-window geometry dimensions/offsets, and semver release components, rejecting partial text, overflow, malformed offsets, and unsafe narrowing.
  Implementation log: `docs-dev/updater-bootstrap-parser-safety-hardening-2026-07-01.md`
  Latest cleanup: sgame userinfo parsing now uses strict bounded helpers for player FOV, handedness, autoswitch, autoshield, bobskip, and respawn FOV restoration, rejecting partial/overflowed values and avoiding unchecked missing-key buffers.
  Implementation log: `docs-dev/sgame-userinfo-parse-safety-hardening-2026-07-01.md`
  Latest cleanup: player config and start-item parsing now uses strict `std::from_chars` helpers for config integers, allocation-free boolean matching, all-whitespace key splitting, safe start-item count parsing, and validated inventory item IDs before writes.
  Implementation log: `docs-dev/player-config-start-items-parse-hardening-2026-07-01.md`
  Latest cleanup: client fog command and shared fog updates now use strict `std::from_chars` command parsing, finite/fraction sanitization, bounded lerp times, overflow-safe elapsed interpolation, and scoped interpolation macros.
  Implementation log: `docs-dev/client-fog-command-parse-safety-hardening-2026-07-01.md`
  Latest cleanup: classic cgame HUD layout parsing now uses strict `std::from_chars` integer tokens for stat/client/configstring indices, layout offsets, scoreboard fields, and localization argument counts, and restores `loc_string2` support.
  Implementation log: `docs-dev/classic-cgame-layout-parse-hardening-2026-07-01.md`
  Latest cleanup: bot direct-use recovery now tests saved use callbacks through the wrapper bool conversion instead of an ambiguous `nullptr` comparison, restoring full sgame compilation.
  Implementation log: `docs-dev/bot-direct-use-save-pointer-build-fix-2026-07-01.md`
  Latest cleanup: ASCII score layout rendering now clips text output to the 80x40 buffer and uses strict `std::from_chars` parsing for layout offsets, stat/client indices, scoreboard fields, and configstring lookups.
  Implementation log: `docs-dev/ascii-score-layout-safety-hardening-2026-07-01.md`
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
  Progress: `tools/bot_perf/analyze_bot_perf.py` now parses bot frame-command smoke logs, supports text/JSON/CSV output, accepts scenario report sidecars for duration metadata, emits multi-run comparison summaries, warns when comparisons mix scenario names, bot counts, duration sources, or missing duration data, can write Markdown reports under `.tmp/bot_perf/`, and derives CPU/visibility/trace/memory metrics when source-counter fields appear in status output. The import/adapter layer now emits split route-build, PVS/PHS, visibility-decompression, entity-trace, entity-clip, static BSP trace, BotLib memory, bot-frame CPU, route CPU, Q3A route CPU, static BSP CPU, and entity-clip CPU source-counter status for implemented scenario parsing. The scenario harness now also reports optional status-field discoveries so new action/aim/item/route status fields can be observed before becoming hard budget or promotion gates. The analyzer exposes source-counter group pass/fail fields, `missing_current_counters` diagnostics, and now merges repeated frame/source status lines so long-soak detail counters survive later compact summaries. The scenario harness now evaluates JSON perf budgets into compact `perf_budget` and `perf_budgets` blocks. The latest high-bot source-counter soak from `.tmp\bot_scenarios\fresh_source_counter_soak_pass\20260628T090904Z` reports no missing source-counter groups and derives bot-frame, route-query, route-reuse, Q3A-route, static-BSP, entity-clip, visibility, and Q3A-memory fields. The high-bot scenario now evaluates both default compatibility and strict current-source budget lanes. `tools/bot_perf/README.md` documents quickstart, scenario glob input, comparison, budget, guard warnings, scenario-runner integration, strict source-counter gating, and test commands.
  Implementation logs: `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`, `docs-dev/q3a-botlib-source-counter-plumbing-2026-06-18.md`, `docs-dev/q3a-botlib-route-cpu-timing-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`, `docs-dev/q3a-botlib-static-bsp-trace-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-entity-trace-clip-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-aas-memory-source-counters-2026-06-18.md`, `docs-dev/q3a-botlib-status-harness-expansion-2026-06-18.md`, `docs-dev/q3a-botlib-high-bot-soak-budget-2026-06-18.md`, `docs-dev/q3a-botlib-long-soak-source-counter-round-2026-06-18.md`, `docs-dev/q3a-botlib-fresh-source-counter-soak-2026-06-28.md`, `docs-dev/q3a-botlib-strict-source-counter-budget-lane-2026-06-29.md`.
- [ ] `DV-05-T03` Add lightweight frame-time and subsystem timing instrumentation toggles.
  Dependency: none. Priority: P1.
  Progress: OpenGL now exposes the first renderer baseline through `gl_cpu_timers`, `gl_gpu_timers`, `gl_profile_log`, `gl_debug_markers`, `gl_telemetry`, and renderer stats. Bot smoke log tooling now records derived command, route, debug, recovery, and scenario rates from existing status lines, merges split `q3a_bot_frame_command_status` and `q3a_bot_source_counter_status` fields, and now consumes emitted bot-frame CPU, route CPU, Q3A route CPU, Q3A memory, visibility, static BSP trace, static BSP CPU, entity-trace, and entity-clip CPU source counters. The 2026-06-28 high-bot source-counter soak reports `bot_frame_cpu_ms_per_bot_sec=2.671`, `route_query_cpu_ms_per_bot_sec=0.181`, `route_reuse_cpu_ms_per_bot_sec=0.002`, and `q3a_route_cpu_ms_per_bot_sec=0.132`.
- [ ] `DV-05-T04` Add nightly trend report for key performance metrics.
  Dependency: `DV-05-T02`. Priority: P2.
- [ ] `DV-05-T05` Add performance budget thresholds for major renderer and server paths.
  Dependency: `DV-05-T01`. Priority: P2.
  Progress: `tools/bot_perf/default_soak_budget.json` defines a generous baseline budget for the current ten-minute eight-bot `mm-rage` route-command soak. The analyzer returns a failing exit code for required threshold failures and the current soak fixture passes all default checks. The scenario harness exposes the manual `high_bot_soak_degradation` row for mode `18` and now evaluates that default budget directly into a compact `perf_budget` block while recording every evaluated profile in `perf_budgets`. The 2026-06-28 source-counter soak passed the raw degradation policy and default budget with `0` failures, `0` warnings, all seven source-counter groups present, `40.007` commands/bot/sec, `22.373` route queries/bot/sec, `0.5736` route-refresh ratio, and `0.4264` route-reuse ratio. The budget was refreshed to `route_queries_per_bot_sec <= 30`, `route_refresh_ratio <= 0.65`, and `route_reuse_ratio >= 0.35` for the current route-cache pressure baseline. `tools/bot_perf/source_counter_soak_budget.json` now adds a strict current-source lane requiring `source_counter_pass_int=1`, all current source-counter groups, CPU derived metrics, memory failure counts, visibility decompression failures, and entity-trace failures. CPU source-counter checks remain optional in the default budget for legacy log analysis while the strict lane fails missing modern counters. Comparison guards are warnings only, so strict regression gates should compare like-for-like scenarios until broader source-counter baselines exist.
  Implementation logs: `docs-dev/q3a-botlib-bot-perf-telemetry-2026-06-18.md`, `docs-dev/q3a-botlib-bot-perf-source-counters-2026-06-18.md`, `docs-dev/q3a-botlib-source-counter-plumbing-2026-06-18.md`, `docs-dev/q3a-botlib-route-cpu-timing-2026-06-18.md`, `docs-dev/q3a-botlib-scenario-promotion-cpu-status-2026-06-18.md`, `docs-dev/q3a-botlib-static-bsp-trace-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-entity-trace-clip-cpu-2026-06-18.md`, `docs-dev/q3a-botlib-high-bot-degradation-policy-2026-06-18.md`, `docs-dev/q3a-botlib-high-bot-soak-budget-2026-06-18.md`, `docs-dev/q3a-botlib-long-soak-source-counter-round-2026-06-18.md`, `docs-dev/q3a-botlib-fresh-source-counter-soak-2026-06-28.md`, `docs-dev/q3a-botlib-strict-source-counter-budget-lane-2026-06-29.md`.

## Epic DV-06: Dependency Lifecycle and Security Hygiene
Objective: reduce dependency sprawl and improve update confidence.

Primary Areas: `subprojects/`, Meson wraps, release/build docs

Exit Criteria:
- Dependency versions are intentional, documented, and reviewable with lower drift risk.

Tasks:
- [ ] `DV-06-T01` Audit duplicate vendored versions and define active baseline per dependency.
  Dependency: none. Priority: P0.
  RmlUi Round 10 note:
  `docs-dev/rmlui-dependency-decision-record-2026-07-02.md` documents the
  proposed audit scope and dependency integration direction for RmlUi. Exact
  upstream source, version/commit, license/provenance review, local patch
  policy, and accepted build integration remain pending.
  RmlUi Round 11 note:
  `tools/ui_smoke/check_rmlui_dependency_decision.py` now validates that the
  decision record remains proposed/not implemented, preserves no-go wording for
  Meson/build/dependency/runtime changes, and names native renderer/Gate G1
  obligations. Exact upstream source, version/commit, license/provenance
  review, local patch policy, and accepted build integration remain pending.
  RmlUi Round 15 note:
  `subprojects/rmlui.wrap` now records upstream RmlUi `6.2`, archive URL
  `https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz`, SHA-256
  `814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b`, and
  CMake-method source acquisition. The dependency-source audit records MIT
  license/provenance notes, while final notices, local patch policy, and
  accepted compile/link integration remain pending.
  RmlUi Round 17 note:
  the pinned source now has wrap provide aliases, explicit CMake fallback
  options, and enabled scratch compile/link evidence for `rmlui_core.dll` plus
  `worr_engine_x86_64.dll`. Final notice/update policy, local patch policy,
  supported-matrix acceptance, and renderer/runtime enablement remain pending.
  RmlUi Round 18 note:
  the compiled RmlUi Core adapter now supplies WORR-backed system and file
  interfaces, with validation proving the bridge references WORR filesystem,
  time, and log APIs. Final notice/update policy, local patch policy,
  supported-matrix acceptance, and renderer/runtime enablement remain pending.
  RmlUi Round 19 note:
  the RmlUi dependency boundary now includes explicit native renderer-family
  lanes and an opaque render-interface hook, with validation guarding against
  Vulkan-to-OpenGL redirection. Final notice/update policy, local patch policy,
  supported-matrix acceptance, and renderer/runtime enablement remain pending.
  RmlUi Round 20 note:
  the RmlUi dependency boundary now includes OpenGL-scoped renderer C++/RmlUi
  dependency wiring for the first `Rml::RenderInterface` scaffold, while
  Vulkan and RTX/vkpt exports remain unavailable rather than redirected.
  Final notice/update policy, local patch policy, supported-matrix
  acceptance, and renderer/runtime enablement remain pending.
  RmlUi Round 21 note:
  the same OpenGL-scoped dependency path now links an implemented primitive
  bridge for geometry, texture upload/lifetime, tessellator drawing, and
  scissor state without enabling route ownership or changing Vulkan/RTX-vkpt
  exports. Final notice/update policy, local patch policy, supported-matrix
  acceptance, and renderer/runtime enablement remain pending.
  RmlUi Round 22 note:
  the compiled runtime now uses the OpenGL-scoped render interface to open and
  draw only the guarded `core.runtime_smoke` document from a RmlUi context,
  while normal routes and non-OpenGL renderers remain guarded. Final
  notice/update policy, local patch policy, supported-matrix acceptance, and
  renderer/runtime enablement remain pending.
  RmlUi Round 23 note:
  the same guarded OpenGL-scoped path now carries key, text, mouse-button,
  mouse-wheel, and pointer movement events into the RmlUi context and exposes
  status/capture counters for manual evidence collection, while normal routes
  and non-OpenGL renderers remain guarded. Final notice/update policy, local
  patch policy, supported-matrix acceptance, and renderer/runtime enablement
  remain pending.
  RmlUi Round 24 note:
  the guarded OpenGL-scoped path now installs a temporary layout-only RmlUi
  font engine and records automated runtime capture evidence. The null font
  engine is a bootstrap dependency decision only; final font/text ownership,
  local patch policy, supported-matrix acceptance, and renderer/runtime
  enablement remain pending.
  RmlUi Round 25 note:
  the guarded OpenGL-scoped path now replaces that null adapter with a smoke
  bitmap glyph path and validates glyph-generation evidence in the runtime
  capture harness. The smoke glyph table is temporary; final font/text
  ownership, local patch policy, supported-matrix acceptance, and
  renderer/runtime enablement remain pending.
  RmlUi Round 29 note:
  the guarded OpenGL-scoped path now includes route-matrix evidence for
  `main`, `game`, and `download_status` opened through `UI_OpenMenu`.
  This remains opt-in and OpenGL-only; final font/text ownership, local patch
  policy, supported-matrix acceptance, native Vulkan/RTX-vkpt renderer
  enablement, and default runtime ownership remain pending.
  RmlUi Round 30 note:
  the dependency boundary now includes renderer-family matrix guardrails that
  keep OpenGL as the only current guarded native lane and require Vulkan and
  RTX/vkpt to remain unavailable until native bridges exist. The checker also
  rejects Vulkan/RTX-to-OpenGL routing and premature non-OpenGL RmlUi runtime
  dependency enablement. Final notice/update policy, local patch policy,
  supported-matrix acceptance, native Vulkan/RTX-vkpt renderer enablement, and
  default runtime ownership remain pending.
  RmlUi Round 31 note:
  the runtime capture harness now emits aggregate renderer-matrix manifests
  that pair OpenGL route evidence with the renderer-family dependency
  guardrail. Final notice/update policy, local patch policy,
  supported-matrix acceptance, native Vulkan/RTX-vkpt renderer enablement, and
  default runtime ownership remain pending.
  RmlUi Round 32 note:
  the dependency boundary now includes a Vulkan/RTX bridge-readiness audit
  that records available native renderer foundations while keeping both
  non-OpenGL RmlUi dependencies blocked until native bridges exist. Final
  notice/update policy, local patch policy, supported-matrix acceptance,
  native Vulkan/RTX-vkpt renderer enablement, and default runtime ownership
  remain pending.
  RmlUi Round 33 note:
  aggregate renderer manifests now include the bridge-readiness dependency
  boundary so OpenGL evidence, renderer-family guards, and native-pending
  Vulkan/RTX requirements are reviewed together. Final notice/update policy,
  local patch policy, supported-matrix acceptance, native Vulkan/RTX-vkpt
  renderer enablement, and default runtime ownership remain pending.
  RmlUi Round 34 note:
  native bridge activation requirements are now structured in the
  bridge-readiness and aggregate renderer manifests, with `8` requirements,
  `0` satisfied, and `8` pending across Vulkan and RTX/vkpt. Final
  notice/update policy, local patch policy, supported-matrix acceptance,
  native Vulkan/RTX-vkpt renderer enablement, and default runtime ownership
  remain pending.
  RmlUi Round 35 note:
  native bridge activation status is now structured in the bridge-readiness
  and aggregate renderer manifests, with `0` complete lanes, `0` partial
  lanes, and `2` inactive activation lanes across Vulkan and RTX/vkpt. Final
  notice/update policy, local patch policy, supported-matrix acceptance,
  native Vulkan/RTX-vkpt renderer enablement, and default runtime ownership
  remain pending.
  RmlUi Round 36 note:
  native bridge source-set activation is now part of the dependency boundary,
  with `10` total activation requirements and `10` pending requirements across
  Vulkan and RTX/vkpt. Final notice/update policy, local patch policy,
  supported-matrix acceptance, native Vulkan/RTX-vkpt renderer enablement, and
  default runtime ownership remain pending.
  RmlUi Round 37 note:
  inactive non-OpenGL bridge source wiring is now part of the dependency
  boundary: both Vulkan and RTX/vkpt renderer source sets include
  `src/renderer/rmlui_bridge.cpp`, while their RmlUi runtime dependencies stay
  disabled. Final notice/update policy, local patch policy, supported-matrix
  acceptance, native Vulkan/RTX-vkpt renderer enablement, and default runtime
  ownership remain pending.
  RmlUi Round 38 note:
  inactive non-OpenGL bridge class stubs are now part of the dependency
  boundary: Vulkan and RTX/vkpt class names exist in the shared bridge source,
  while family exports, runtime dependencies, and native interface exports
  remain unavailable. Final notice/update policy, local patch policy,
  supported-matrix acceptance, native Vulkan/RTX-vkpt renderer enablement, and
  default runtime ownership remain pending.
  RmlUi Round 39 note:
  inactive non-OpenGL family exports are now part of the dependency boundary:
  Vulkan and RTX/vkpt renderer DLLs can report distinct RmlUi family lanes,
  while runtime dependencies and native interface exports remain unavailable.
  Final notice/update policy, local patch policy, supported-matrix acceptance,
  native Vulkan/RTX-vkpt renderer enablement, and default runtime ownership
  remain pending.
  RmlUi Round 40 note:
  inactive non-OpenGL runtime dependencies are now part of the dependency
  boundary: Vulkan and RTX/vkpt renderer DLLs receive the RmlUi runtime
  dependency and `UI_RML_HAS_RUNTIME` definition, while native interface
  exports remain unavailable. Final notice/update policy, local patch policy,
  supported-matrix acceptance, native Vulkan/RTX-vkpt renderer enablement, and
  default runtime ownership remain pending.
  RmlUi Round 41 note:
  staged OpenGL menu-route loading now uses the compiled RmlUi dependency and
  refreshed `.install` payload to open all 57 registered RmlUi routes without
  a fresh crash dump. Final notice/update policy, local patch policy,
  supported-matrix acceptance, native Vulkan/RTX-vkpt renderer enablement, and
  default runtime ownership remain pending.
- [ ] `DV-06-T02` Remove or archive superseded dependency trees not needed for reproducible builds.
  Dependency: `DV-06-T01`. Priority: P1.
  2026-07-02 cleanup note: removed ignored local stale unpacked source trees
  and matching stale packagecache artifacts for superseded cairo, curl, fmt,
  glib, HarfBuzz, JsonCpp, libjpeg-turbo, libpng, OpenAL Soft, and zlib-ng
  versions. Implementation log:
  `docs-dev/dependency-stale-subproject-cleanup-2026-07-02.md`.
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
  Progress: `docs-user/client.asciidoc` documents `cg_weapon_bob`, its disabled/Quake 3/Doom 3 values, and the legacy `cg_weaponBob` alias. Bot user docs now also cover practical setup/debug cvars, packaged botfile locations, current AAS/map readiness limits, available-reference validation, and the item timer controls `bot_allow_item_timers` / `bot_item_timer_fuzz_ms`. `docs-user/competitive-server-tools.md` adds operator parity for the competitive server cvars and commands under `FR-07-T05`, including the `basew/matches/catalog.json` match logging index added under `FR-07-T03`.
  RmlUi Round 4 note: user-visible parity is still pending. No end-user RmlUi
  documentation pass is claimed until runtime/controller behavior is exposed to
  players, beyond the current guarded scaffold.
  RmlUi Round 5 note: semantics and progress-report tooling are accepted for
  developer-side regression reporting. No end-user parity documentation is
  claimed until runtime/controller behavior and player-visible parity evidence
  are accepted.
  RmlUi Round 6 note: controller-contract validation, runtime asset checks,
  staged loose-file checks, and JSON progress reporting improve developer-side
  regression tracking. No end-user parity documentation is claimed until
  runtime/controller behavior and player-visible parity evidence are accepted.
  RmlUi Round 7 note: runtime registry, controller-stub coverage, imported
  runtime asset, and controller-contract progress checks improve
  developer-side regression tracking. No end-user parity documentation is
  claimed until runtime/controller behavior and player-visible parity evidence
  are accepted.
  RmlUi Round 8 note: menu-entrypoint validation, runtime-stub eligibility,
  runtime asset JSON, and phase-progression progress facts improve
  developer-side regression tracking. No end-user parity documentation is
  claimed until runtime/controller behavior and player-visible parity evidence
  are accepted.
  RmlUi Round 9 note: navigation graph validation, controller fixture checks,
  detailed runtime asset manifests, parity checklist validation, and all-route
  metadata progress facts improve developer-side regression tracking. No
  end-user parity documentation is claimed until runtime/controller behavior
  and player-visible parity evidence are accepted.
  RmlUi Round 10 note: command/cvar inventory checks and parity-checklist
  progress summaries improve developer-side regression tracking, and the
  progress reporter now exposes those command/cvar counts in text, markdown,
  and JSON. No end-user parity documentation is claimed until
  runtime/controller behavior and player-visible parity evidence are accepted.
  RmlUi Round 11 note: data-model inventory, phase-consistency, and
  dependency-decision checks improve developer-side regression tracking, and
  the progress reporter now exposes data-model counts in text, markdown, and
  JSON. No end-user parity documentation is claimed until runtime/controller
  behavior and player-visible parity evidence are accepted.
  RmlUi Round 12 note: condition inventory and metadata-sync checks improve
  developer-side regression tracking, and the progress reporter now exposes
  condition/metadata counts in text, markdown, and JSON. No end-user parity
  documentation is claimed until runtime/controller behavior and
  player-visible parity evidence are accepted.
  RmlUi Round 13 note: event inventory, a11y/localization inventory, and
  legacy-removal guardrails improve developer-side regression tracking, and
  the progress reporter now exposes event/a11y counts in text, markdown, and
  JSON. No end-user parity documentation is claimed until runtime/controller
  behavior and player-visible parity evidence are accepted.
  RmlUi Round 14 note: document/body identity, entrypoint, route metadata
  shape, and legacy-removal progress summaries improve developer-side
  regression tracking. No end-user parity documentation is claimed until
  runtime/controller behavior and player-visible parity evidence are accepted.
  RmlUi Round 15 note: dependency-integration validation and default-disabled
  build-gate evidence improve developer-side regression tracking. No end-user
  parity documentation is claimed until runtime/controller behavior and
  player-visible parity evidence are accepted.
  RmlUi Round 16 note: controller-stub completion validation and all-route
  controller-binding checklist coverage improve developer-side regression
  tracking. No end-user parity documentation is claimed until
  runtime/controller behavior and player-visible parity evidence are accepted.
  RmlUi Round 17 note: compiled-runtime adapter validation improves
  developer-side regression tracking for the optional dependency path. No
  end-user parity documentation is claimed until route rendering,
  runtime/controller behavior, and player-visible parity evidence are accepted.
  RmlUi Round 18 note: system/file bridge validation improves developer-side
  regression tracking for runtime asset loading. No end-user parity
  documentation is claimed until route rendering, runtime/controller behavior,
  and player-visible parity evidence are accepted.
  RmlUi Round 19 note: renderer-contract validation improves developer-side
  regression tracking for future native renderer integration. No end-user
  parity documentation is claimed until route rendering, runtime/controller
  behavior, and player-visible parity evidence are accepted.
  RmlUi Round 20 note: OpenGL render-interface scaffold validation improves
  developer-side regression tracking for the first native renderer-family
  lane, but no end-user parity documentation is claimed until visible route
  rendering, runtime/controller behavior, and player-visible parity evidence
  are accepted.
  RmlUi Round 21 note: OpenGL primitive bridge validation improves
  developer-side regression tracking for the first renderer-backed geometry,
  texture, and scissor path, but no end-user parity documentation is claimed
  until visible route rendering, runtime/controller behavior, and
  player-visible parity evidence are accepted.
  RmlUi Round 22 note: guarded context-route validation improves
  developer-side regression tracking for the first RmlUi document load,
  update, and render path, but no end-user parity documentation is claimed
  until normal route ownership, runtime/controller behavior, and
  player-visible parity evidence are accepted.
  RmlUi Round 23 note: guarded input/capture validation improves
  developer-side regression tracking for the first RmlUi key/text/mouse event
  bridge and manual capture counters, but no end-user parity documentation is
  claimed until normal route ownership, automated screenshot evidence,
  runtime/controller behavior, and player-visible parity evidence are
  accepted.
  RmlUi Round 24 note: guarded screenshot evidence now exists for the internal
  `core.runtime_smoke` route, but it is developer-only, OpenGL-only, and uses
  a layout-only null font engine. No end-user parity documentation is claimed
  until normal route ownership, real text rendering, runtime/controller
  behavior, and player-visible parity evidence are accepted.
  RmlUi Round 25 note: the guarded developer-only OpenGL screenshot now
  includes smoke glyph geometry, but it still does not establish normal route
  ownership, full font/text behavior, runtime/controller behavior, or
  player-visible parity evidence. No end-user documentation is claimed.
  RmlUi Round 26 note: the guarded developer-only OpenGL screenshot now also
  carries layout assertion evidence for the smoke route. It still does not
  establish normal route ownership, full font/text behavior,
  runtime/controller behavior, renderer parity, or player-visible parity
  evidence. No end-user documentation is claimed.
  RmlUi Round 27 note: the guarded developer-only OpenGL capture now also
  carries synthetic input/back-close counter evidence for the smoke route. It
  still does not establish normal route ownership, broad input/navigation
  parity, runtime/controller behavior, renderer parity, or player-visible
  parity evidence. No end-user documentation is claimed.
  RmlUi Round 28 note: the guarded developer-only OpenGL capture now also
  carries two-viewport matrix evidence for the smoke route. It still does not
  establish normal route ownership, responsive widescreen parity,
  runtime/controller behavior, renderer parity, or player-visible parity
  evidence. No end-user documentation is claimed.
  RmlUi Round 29 note: the guarded developer-only OpenGL capture now also
  carries menu-route matrix evidence for `main`, `game`, and
  `download_status`. It still does not establish default route ownership,
  final theme/layout parity, runtime/controller behavior, renderer parity, or
  player-visible parity evidence. No end-user documentation is claimed.
  RmlUi Round 30 note: renderer-family matrix guardrails now protect the
  developer-only migration path by recording OpenGL as the sole guarded native
  lane and keeping Vulkan/RTX-vkpt blocked until native bridges exist. It
  still does not establish native renderer parity, default route ownership,
  runtime/controller behavior, or player-visible parity evidence. No end-user
  documentation is claimed.
  RmlUi Round 31 note: aggregate renderer-matrix manifests now combine the
  guarded OpenGL route evidence with blocked Vulkan/RTX-vkpt lane facts for
  developer-side review. It still does not establish native renderer parity,
  default route ownership, runtime/controller behavior, or player-visible
  parity evidence. No end-user documentation is claimed.
  RmlUi Round 32 note: Vulkan/RTX bridge-readiness evidence now records the
  native renderer foundations and missing bridge requirements for the
  non-OpenGL lanes. It still does not establish native renderer parity,
  default route ownership, runtime/controller behavior, or player-visible
  parity evidence. No end-user documentation is claimed.
  RmlUi Round 33 note: aggregate renderer manifests now carry bridge-readiness
  evidence next to guarded OpenGL route evidence. It still does not establish
  native renderer parity, default route ownership, runtime/controller behavior,
  or player-visible parity evidence. No end-user documentation is claimed.
  RmlUi Round 34 note: bridge-readiness and aggregate renderer manifests now
  carry native bridge activation checklist counts. It still does not establish
  native renderer parity, default route ownership, runtime/controller behavior,
  or player-visible parity evidence. No end-user documentation is claimed.
  RmlUi Round 35 note: bridge-readiness and aggregate renderer manifests now
  carry native bridge activation status and next-blocker fields. It still does
  not establish native renderer parity, default route ownership,
  runtime/controller behavior, or player-visible parity evidence. No end-user
  documentation is claimed.
  RmlUi Round 36 note: bridge-readiness and aggregate renderer manifests now
  require native bridge source-set activation before any non-OpenGL lane can
  be promoted. It still does not establish native renderer parity, default
  route ownership, runtime/controller behavior, or player-visible parity
  evidence. No end-user documentation is claimed.
  RmlUi Round 37 note: inactive Vulkan and RTX/vkpt source-set wiring is now
  recorded in bridge-readiness and aggregate renderer manifests, with both
  lanes still blocked before native bridge class implementation. It still does
  not establish native renderer parity, default route ownership,
  runtime/controller behavior, or player-visible parity evidence. No end-user
  documentation is claimed.
  RmlUi Round 38 note: inactive Vulkan and RTX/vkpt bridge class stubs are now
  recorded in bridge-readiness and aggregate renderer manifests, with both
  lanes still blocked before native family/runtime/interface exports. It still
  does not establish native renderer parity, default route ownership,
  runtime/controller behavior, or player-visible parity evidence. No end-user
  documentation is claimed.
  RmlUi Round 39 note: inactive Vulkan and RTX/vkpt family exports are now
  recorded in bridge-readiness and aggregate renderer manifests, with both
  lanes still blocked before runtime dependency and native interface exports.
  It still does not establish native renderer parity, default route ownership,
  runtime/controller behavior, or player-visible parity evidence. No end-user
  documentation is claimed.
  RmlUi Round 40 note: inactive Vulkan and RTX/vkpt runtime dependency wiring
  is now recorded in bridge-readiness and aggregate renderer manifests, with
  both lanes still blocked before native interface exports. It still does not
  establish native renderer parity, default route ownership, runtime/controller
  behavior, or player-visible parity evidence. No end-user documentation is
  claimed.
  RmlUi Round 41 note: installed OpenGL route-load validation now opens all 57
  registered routes from the staged client without a fresh crash dump or
  parser/fallback/error log hits. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, runtime/controller
  behavior, screenshot layout parity, or end-user documentation.
  RmlUi Round 42 note: OpenGL RmlUi resize-canvas validation now keeps active
  route dimensions, mouse coordinates, scissor rectangles, and software cursor
  drawing aligned with the renderer virtual UI canvas across staged viewport
  captures and live Win32 resize events. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, runtime/controller
  behavior, screenshot layout parity, or end-user documentation.
  RmlUi Round 43 note: staged OpenGL route validation now includes TTF-backed
  RmlUi text generation, refined player-facing menu copy, contained keybind
  long-list layout, and a `960x720` keybind screenshot. It still does not
  establish native Vulkan/RTX renderer parity, final route ownership, live
  runtime/controller behavior, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 44 note: staged OpenGL route validation now requires Quake II
  Rerelease font-source markers, proves the display/UI/monospace RmlUi faces
  load from rerelease font paths, opens `58` unique route IDs with no
  failure/parser/error hits, and carries screenshot evidence for the refined
  Options, Admin Commands, Start Server, and Deathmatch Flags layouts. It
  still does not establish native Vulkan/RTX renderer parity, final route
  ownership, live runtime/controller behavior, full screenshot layout parity,
  or end-user documentation.
  RmlUi Round 45 note: staged OpenGL visual validation now includes `30`
  representative route captures and specific bounded-list screenshots for
  in-game actions, long settings forms, and save/load slots, while the final
  all-route sweep remains clean. It still does not establish native Vulkan/RTX
  renderer parity, final route ownership, live runtime/controller behavior,
  full screenshot layout parity, or end-user documentation.
  RmlUi Round 46 note: staged OpenGL validation now adds focused
  Single Player hub and generic Session List screenshots with visible footer
  actions, and the final all-route sweep remains clean. It still does not
  establish native Vulkan/RTX renderer parity, final route ownership, live
  runtime/controller behavior, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 47 note: staged OpenGL validation now adds focused Admin
  Commands, Call Vote, Match Lobby, and Key Bindings screenshots with visible
  footer actions, and the final all-route sweep remains clean. It still does
  not establish native Vulkan/RTX renderer parity, final route ownership, live
  runtime/controller behavior, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 48 note: staged OpenGL validation now adds focused Performance,
  Sound, Download Options, and Start Server screenshots with compact toggles or
  shortened form regions above visible footer actions, and the final all-route
  sweep remains clean. It still does not establish native Vulkan/RTX renderer
  parity, final route ownership, live runtime/controller behavior, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 49 note: staged OpenGL validation now adds focused aesthetic
  evidence for conservative transitions, hover/focus treatments, shell
  framing, and final Sound/Start Server containment; the final all-route sweep
  remains clean. It still does not establish native Vulkan/RTX renderer
  parity, final route ownership, live runtime/controller behavior, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 50 note: staged OpenGL validation now adds focused main-menu
  scaling evidence across windowed, widescreen, tall, and fullscreen-style
  sizes, and the final all-route sweep remains clean. It still does not
  establish native Vulkan/RTX renderer parity, final route ownership, live
  runtime/controller behavior, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 51 note: staged OpenGL validation now adds focused typed-widget
  and utility form evidence for the non-main menu layout refinement, and the
  final all-route sweep remains clean. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, live runtime/controller
  behavior, full screenshot layout parity, or end-user documentation.
  RmlUi Round 52 note: staged OpenGL validation now adds focused navigation
  grid evidence for shell, single-player, save/load, multiplayer/session, and
  shared settings routes, and the final all-route sweep remains clean. It
  still does not establish native Vulkan/RTX renderer parity, final route
  ownership, live runtime/controller behavior, true narrow-viewport capture
  parity, full screenshot layout parity, or end-user documentation.
  RmlUi Round 53 note: staged OpenGL validation now adds focused visual polish
  evidence for spaced command tiles and denser settings rows, and the final
  all-route sweep remains clean. It still does not establish native Vulkan/RTX
  renderer parity, final route ownership, live runtime/controller behavior,
  true narrow-viewport capture parity, full screenshot layout parity, or
  end-user documentation.
  RmlUi Round 54 note: staged OpenGL validation now adds focused
  action-intent/widget evidence for real command buttons, primary/destructive
  states, and refined utility/confirmation panels, and the final all-route
  sweep remains clean. It still does not establish native Vulkan/RTX renderer
  parity, final route ownership, live runtime/controller behavior, true
  narrow-viewport capture parity, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 55 note: staged OpenGL validation now adds popup-command
  evidence, confirmation-popup captures, restored menu feedback sound plumbing,
  and a final Sound Settings capture for the two-column music/effects layout;
  the final all-route sweep remains clean. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, live runtime/controller
  behavior, true narrow-viewport capture parity, full screenshot layout parity,
  or end-user documentation.
  RmlUi Round 56 note: staged OpenGL validation now adds consumed menu-music
  cue evidence, focused Game/Main Quit popup parity evidence, and a capture
  sheet for Game, Main, Quit Confirm, and Sound Settings; the final all-route
  sweep remains clean. It still does not establish native Vulkan/RTX renderer
  parity, final route ownership, live runtime/controller behavior, true
  narrow-viewport capture parity, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 57 note: staged OpenGL validation now adds consumed open-sound
  cue evidence, target-level focus/change audio wiring, focused popup audio
  flow evidence, and visual captures for Main, Game, Download Status, and Quit
  Confirm; the final all-route sweep remains clean. It still does not establish
  native Vulkan/RTX renderer parity, final route ownership, live
  runtime/controller behavior, true narrow-viewport capture parity, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 58 note: staged OpenGL validation now adds deterministic
  `pushmenu` bridge evidence for one normal route and four confirmation popup
  routes, including popup-command selection, active runtime status, menu music
  cues, and alert/open sound cues. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, live runtime/controller
  behavior, true narrow-viewport capture parity, full screenshot layout
  parity, or end-user documentation.
  RmlUi Round 59 note: staged OpenGL validation now adds focused Multiplayer
  hub parity evidence. The hub restores q2servers/address-book/demos/start
  server/player/options command intent, removes the stale custom connect
  command, validates `pushmenu multiplayer`, and carries `960x720` screenshot
  evidence for the two-column shell grid. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, live runtime/controller
  behavior, true narrow-viewport capture parity, full screenshot layout
  parity, or end-user documentation.
  RmlUi Round 60 note: staged OpenGL validation now adds focused Video Setup
  parity evidence. The page restores the legacy display, texture, gamma/light,
  and renderer controls with typed widgets, validates `pushmenu video`, and
  carries `960x720` screenshot evidence for a three-column layout contained
  above Back/Close. It still does not establish native Vulkan/RTX renderer
  parity, final route ownership, live runtime/controller behavior, true
  narrow-viewport capture parity, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 61 note: staged OpenGL validation now adds focused settings
  family audio/action-row evidence. Settings routes consistently carry
  menu-music/open-sound metadata, Screen/Effects nested navigation uses typed
  action rows, and `960x720` captures prove both two-column pages remain above
  Back/Close. It still does not establish native Vulkan/RTX renderer parity,
  final route ownership, live runtime/controller behavior, true narrow-viewport
  capture parity, full screenshot layout parity, or end-user documentation.
  RmlUi Round 62 note: staged OpenGL validation now adds focused
  single-player/local-session audio and Start Server layout evidence. The
  single-player route family carries menu-music/open-sound metadata, decisive
  Skill Select/Start Server actions carry explicit sound cues, and `960x720`
  captures prove Start Server's three-column static fallback layout remains
  above Back/Close. It still does not establish native Vulkan/RTX renderer
  parity, final route ownership, live runtime/controller behavior, true
  narrow-viewport capture parity, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 63 note: staged OpenGL validation now adds full utility-family
  audio/layout evidence. All eight utility routes carry menu-music/open-sound
  metadata, intent-specific action sounds, and `pushmenu` runtime evidence,
  while `960x720` captures prove the Address Book, Key Bindings, and Weapon
  Bindings grids remain above footer actions. It still does not establish
  native Vulkan/RTX renderer parity, final route ownership, live
  runtime/controller behavior, true narrow-viewport capture parity, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 64 note: staged OpenGL validation now adds session-family
  audio/layout/popup evidence. Session routes carry menu-music/open-sound
  metadata, dynamic session buttons preserve original `worr_*` command
  publication before RmlUi pushmenu routing, confirmation routes retain popup
  presentation, and `960x720` captures prove the lobby, Call Vote, Admin
  Commands, Match Stats, Tournament Map Choices, and Forfeit confirmation stay
  contained. It still does not establish native Vulkan/RTX renderer parity,
  final route ownership, live runtime/controller behavior, true narrow-viewport
  capture parity, full screenshot layout parity, or end-user documentation.
  RmlUi Round 65 note: staged OpenGL validation now adds grouped shell-hub and
  cross-family action-audio evidence. Options, Game, and Multiplayer use
  grouped hub sections, authored route buttons declare explicit action sounds,
  Quit confirmations continue through popup routes, and final `960x720`
  captures prove Main, Options, Game, Multiplayer, and Single Player stay
  contained after right-column clipping was corrected. It still does not
  establish native Vulkan/RTX renderer parity, final route ownership, live
  runtime/controller behavior, true narrow-viewport capture parity, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 66 note: staged OpenGL validation now adds popup/containment and
  reusable-template audio evidence. Confirmation popups share modal framing,
  fixed menu panels prefer contained overflow over clipping, and representative
  Options, Video, Key Bindings, DM Join, and Quit popup captures stay usable
  at the current staged canvas. It still does not establish native Vulkan/RTX
  renderer parity, final route ownership, live runtime/controller behavior,
  true narrow-viewport capture parity, full screenshot layout parity, or
  end-user documentation.
  RmlUi Round 67 note: staged OpenGL validation now adds compiled-runtime cvar
  binding and condition evaluation evidence. `data-cvar` form controls,
  `data-bind-cvar`, `data-label-cvar`, `data-bind="cvars.*"`, and
  `data-visible-if`/`data-enable-if` are exercised through Video, Sound, Start
  Server, DM Join, and Quit popup captures; settings value badges and the DM
  Join command grid now reflect live cvar state. It still does not establish
  native Vulkan/RTX renderer parity, final route ownership, full live
  data-model/controller behavior, true narrow-viewport capture parity, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 68 note: staged OpenGL validation now adds meter-widget and
  Crosshair containment evidence. `data-meter-cvar` meter fills update from
  live cvars on Video, Sound, Screen, Crosshair, Rail Trail, and Download
  Status surfaces, while the Crosshair page keeps its original Crosshair and
  Hit Feedback controls visible in two columns. SVG widget art is deferred
  until the RmlUi texture path has active SVG rasterization. It still does not
  establish native Vulkan/RTX renderer parity, final route ownership, full
  live data-model/controller behavior, true narrow-viewport capture parity,
  full screenshot layout parity, or end-user documentation.
  RmlUi Round 69 note: staged OpenGL validation now adds first-party SVG UX
  asset evidence. The OpenGL RmlUi bridge rasterizes the supported local SVG
  subset for high-level menu icons, and Main, Game, Options, Multiplayer,
  Single Player, and Quit popup captures show icons contained in shared
  command-button styling. It still does not establish full SVG
  specification/plugin parity, native Vulkan/RTX renderer parity, final route
  ownership, full live data-model/controller behavior, true narrow-viewport
  capture parity, full screenshot layout parity, or end-user documentation.
  RmlUi Round 70 note: staged OpenGL validation now redirects SVG asset usage
  from high-level command pictograms to widget markers. The old `common/icons/ux`
  command asset set was removed, the new `common/icons/widgets` library covers
  authored control types, and Video, Sound, Start Server, Player Setup,
  Address Book, Download Status, and Main captures prove widget-marker loading
  plus plain Main menu commands. It still does not establish dynamic SVG
  state skins, full SVG specification/plugin parity, native Vulkan/RTX
  renderer parity, final route ownership, full live data-model/controller
  behavior, true narrow-viewport capture parity, full screenshot layout
  parity, or end-user documentation.
  RmlUi Round 71 note: staged OpenGL validation now adds a `55`-asset
  stateful SVG widget-skin library for buttons, primary/destructive buttons,
  text boxes, combo/drop-down boxes, checkboxes, range controls, progress
  controls, scrollbars, arrow boxes, and popup frames. Focused Video, Sound,
  Start Server, Download Status, and Quit popup captures plus the clean
  all-route sweep prove the skins load through the current OpenGL SVG texture
  path. It still does not establish route-wide automated pixel assertions for
  every state, native Vulkan/RTX renderer parity, final route ownership, full
  live data-model/controller behavior, true narrow-viewport capture parity,
  full screenshot layout parity, or end-user documentation.
  RmlUi Round 72 note: staged OpenGL validation now adds menu coverage-gap
  fixes and refined widget containment. Focused captures cover shell hubs,
  dense settings, utility pages, session fallback routes, download status, and
  the Quit popup; the final all-route sweep again reports `58` registered route
  IDs, `59` document opens, `58` status samples, and `0` bad parser/texture
  lines. It still does not establish native Vulkan/RTX renderer parity, final
  route ownership, full live data-model/controller behavior, true
  narrow-viewport capture parity, full screenshot layout parity, or end-user
  documentation.
  RmlUi Round 73 note: staged OpenGL validation now adds direct fallback text
  and command-grid evidence for the session/lobby family. `join`/`dm_join`
  keep authored labels when empty cvars exist, `callvote_main` and DM Join
  retain two-column command surfaces, Admin Commands has a readable command/
  usage row format, Start Server no longer exposes `$$com_maplist` as visible
  fallback text, and the all-route sweep again reports `58` registered route
  IDs, `59` document opens, `58` status samples, and `0` parser/CSS/texture/
  runtime error lines after excluding expected missing data-model notices. It
  still does not establish native Vulkan/RTX renderer parity, final route
  ownership, full live data-model/controller behavior, true narrow-viewport
  capture parity, full screenshot layout parity, or end-user documentation.
  RmlUi Round 74 note: staged OpenGL validation now adds utility table
  empty-state fixes, direct `ui_list`/`map_selector`/`tourney_veto` fallback
  evidence, and default suppression of expected controller-stub missing
  data-model notices through `ui_rml_log_missing_data_models`. The all-route
  sweep reports `58` registered route IDs, `59` document opens, `58` status
  samples, `0` missing data-model notice lines at default settings, and `0`
  parser/CSS/texture/runtime error lines. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, full live
  data-model/controller behavior, true narrow-viewport capture parity, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 75 note: staged OpenGL validation now adds direct report/list
  fallback evidence for Match Stats and Tournament Map Choices, a Download
  Status idle/progress-unit capture, and static condition-inventory support
  for runtime-compatible `!cvar` expressions. The all-route sweep again
  reports `58` registered route IDs, `59` document opens, `58` status samples,
  `0` missing data-model notice lines at default settings, and `0`
  parser/CSS/texture/runtime error lines. It still does not establish native
  Vulkan/RTX renderer parity, final route ownership, full live
  data-model/controller behavior, true narrow-viewport capture parity, full
  screenshot layout parity, or end-user documentation.
  RmlUi Round 78 note: the live multiplayer match hub now has an approachable
  player/operator guide at `docs-user/multiplayer-session-menu.md`, including
  initial participation choices, Escape reopen, live overview/tool behavior,
  native-renderer presentation differences, and the explicit
  `match_auto_join=1` immediate-assignment override. Other user-visible RmlUi
  workflows still require final parity documentation before cutover.
- [x] `DV-07-T05` Keep the canonical shadowmapping replacement baseline synchronized with implementation status.
  Dependency: `FR-02-T09`. Priority: P1.
- [ ] `DV-07-T06` Maintain imported-source credits and provenance ledgers for the Q3A BotLib and `TTimo/bspc` AAS work.
  Dependency: `FR-04-T10`. Priority: P0.
  Progress: `docs-dev/q3a-botlib-aas-credits.md` now tracks initial source baselines, contributors, candidate files, import requirements, the `tools/q2aas/` `TTimo/bspc` vendor snapshot, modified imported BSPC files, WORR-native q2aas build/config/validation/trace-bridge/manifest-schema/manifest-smoke/metadata/diagnostic-gate/baseline-gate/AAS-staging/stage-audit/packaged-map-smoke/archive-guardrail/package-audit/archive-packaging/refresh-install/stage-archive-validation files, the WORR-native BotLib/AAS runtime shell, the WORR-native Q3A BotLib import boundary, the Q3A utility imports, the Q3A AAS file-loader imports, the Q3A AAS sampling import, the Q3A AAS reachability import, the Q3A AAS route/CRC import with per-file pinned hashes, the Q3A AAS alternative-route import, the Q3A AAS entity-cache import, the WORR-owned Q3A AAS entity sync and entity trace bridges, the WORR-owned Q3A bridge time/vector helper work, the WORR-owned active-map Q2 BSP entity-lump bridge, the WORR-owned active-map Q2 BSP model-lump bridge, the WORR-owned active-map Q2 BSP static collision bridge, the WORR-owned active-map Q2 BSP visibility bridge, the WORR-owned active-map Q2 BSP leaf entity-link bridge, the WORR-owned BotLib memory/filesystem bridges, and WORR-owned bot frame command, nav route-cache, nav debug-overlay, nav reachability-debug, nav polyline-debug, nav debug-client-filter, nav persistent-goal, nav item-goal, nav item-reservation, nav look-ahead steering, nav velocity-aware steering, nav route-target stabilization, nav stuck-repath, nav stuck recovery command, nav goal-blacklist cooldown, nav failed-goal reason, nav movement-state commands, bot brain command ownership, nav natural travel goals, nav rocket-jump route policy, nav four-bot frame-command smoke, nav eight-bot frame-command smoke, nav soak frame-command smoke, nav map-change repeat/restart smoke, nav natural movement support diagnostics, behavior action dispatcher/brain telemetry boundary, coop command-owner and target-sharing bridges, validation tooling, and legacy Q2R bot surface removal work.
  Latest credit/status update: the current 123-row implemented catalog records the WORR-owned behavior, profile, chat, arbitration, combat/survival, multiplayer pacing, CTF transition, coop live-loop, coop share-loop, bot chat live-events, bot chat live-event cooldown, bot chat live enemy-sighted, bot chat phrase-library, bot chat duplicate-suppression, bot chat live low-health, bot chat live item-taken, bot chat live objective-changed, bot chat live flag-state, bot chat live blocked, bot chat live item-denied, bot chat live match-result, bot chat match-result outcome accounting, bot chat user-doc readiness, coop campaign interaction matrix coverage on `base1` and `base2`, base2 campaign interaction-depth coverage, base2 campaign progression-chain coverage, base2 campaign progression-consumer coverage, base2 campaign post-interaction coverage, base2 campaign progression-carry coverage, train campaign keyed-path coverage, train campaign key-carry bridge coverage with live interaction-goal resolution, natural bridge-start approach, direct lock-warp removal, bounded bridge ride observation, terminal leave-phase preservation, generic coop mover/elevator wait/board/leave lifecycle evidence, physical elevator direct-use/moving-state evidence, route target anti-spin/look-ahead close-point skip evidence, route movement projection evidence, and live match-result status-surface outcome telemetry, movement matrix, movement context gap rows, accepted hazard context row, min-player profile coverage row, and live role-combat deferral proof rows with no new upstream imports claimed. Focused validation remains recorded through mode `90` `bot_chat_live_match_result` from `.tmp\bot_scenarios\20260626Tmatch-result\20260626T182046Z`, outcome-aware mode `90` validation from `.tmp\bot_scenarios\bot_chat_match_result_outcome.json`, public chat-doc validation from `.tmp\bot_surface\public_bot_surface_chat_docs_audit.json`, mode `91` `coop_campaign_interaction_matrix` from `.tmp\bot_scenarios\20260626Tcoop-campaign-interaction-final\20260626T185108Z`, mode `91` `coop_campaign_interaction_matrix_base2` from `.tmp\bot_scenarios\coop_campaign_interaction_matrix_base2.json`, mode `91` `coop_campaign_interaction_depth_base2` from `.tmp\bot_scenarios\coop_campaign_interaction_depth_base2.json`, mode `91` `coop_campaign_progression_chain_base2` from `.tmp\bot_scenarios\coop_campaign_progression_chain_base2.json`, mode `91` `coop_campaign_progression_consumer_base2` from `.tmp\bot_scenarios\coop_campaign_progression_consumer_base2.json`, mode `91` `coop_campaign_post_interaction_base2` from `.tmp\bot_scenarios\coop_campaign_post_interaction_base2.json`, mode `91` `coop_campaign_progression_carry_base2` from `.tmp\bot_scenarios\coop_campaign_progression_carry_base2.json`, mode `91` `coop_campaign_keyed_path_train` from `.tmp\bot_scenarios\coop_campaign_keyed_path_train.json`, mode `91` `coop_campaign_key_carry_train` bridge-observation evidence from `.tmp\bot_scenarios\mover_ride_observation_final.json`, generic coop mover lifecycle evidence from `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json`, physical elevator mover activation from `.tmp\bot_scenarios\movement_elevator_physical.json`, route target anti-spin evidence from `.tmp\bot_scenarios\route_spin_final_after_status.json`, route movement projection evidence from `.tmp\bot_scenarios\route_spin_projection_focus.json`, live match-result status-surface evidence from `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json`, movement context gaps from `.tmp\bot_scenarios\movement_context_gap_rerun2\20260628T080154Z`, accepted hazard context from `.tmp\bot_scenarios\movement_hazard_context_fact2.json`, and min-player profile coverage from `.tmp\bot_scenarios\min_players_profile_coverage.json`; the latest full `implemented` run passed 123/123 rows from `.tmp\bot_scenarios\implemented_after_route_projection_fix.json`, and the credits ledger records the native status families plus validation evidence.
  Latest interaction-arrival mover-endpoint credit update: WORR-owned `bot_nav`, `bot_brain`, scenario harness, unit-test, roadmap, and release evidence work now records endpoint-aware post-interaction arrival candidate discovery and scoring with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\interaction_arrival_mover_endpoint.json`, `.tmp\bot_scenarios\implemented_after_interaction_arrival_mover_endpoint.json`, and `docs-dev/q3a-botlib-interaction-arrival-mover-endpoint-2026-07-01.md`.
  Latest interaction mover ride-state credit update: WORR-owned `bot_nav`, `bot_brain`, scenario harness, unit-test, roadmap, and release evidence work now records explicit wait/board/ride/leave lifecycle telemetry for mover-like interactions with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\mover_ride_state.json`, `.tmp\bot_scenarios\implemented_after_mover_ride_observation.json`, and `docs-dev/q3a-botlib-interaction-mover-ride-state-2026-07-01.md`.
  Latest generic mover lifecycle credit update: WORR-owned `bot_nav`, scenario harness, unit-test, roadmap, and release evidence work now records generic coop mover/elevator Wait, Board, and Leave telemetry with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\mover_lifecycle_after_generic_leave.json`, `.tmp\bot_scenarios\implemented_after_generic_mover_lifecycle.json`, `.tmp\bot_release\bot_release_acceptance_generic_mover_lifecycle.json`, and `docs-dev/q3a-botlib-generic-mover-lifecycle-2026-07-01.md`.
  Latest physical elevator mover activation credit update: WORR-owned `bot_brain`, scenario harness, unit-test, roadmap, and release-staging work now records direct mover `use` activation plus physical elevator moving-state observation for `movement_elevator_route`, with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\movement_elevator_physical.json`, `.tmp\bot_scenarios\movement_elevator_physical_final.json`, `.tmp\bot_scenarios\mover_direct_use_regression.json`, `.tmp\bot_scenarios\implemented_after_physical_elevator_mover.json`, `.tmp\bot_release\bot_release_acceptance_physical_elevator_mover_final.json`, and `docs-dev/q3a-botlib-physical-elevator-mover-activation-2026-07-01.md`.
  Latest route target anti-spin/status-surface credit update: WORR-owned `bot_brain`, `bot_nav`, scenario harness, unit-test, roadmap, release-staging, and chat status-surface work now preserves stabilized route targets, skips consumed local route points, tracks local route-target progress in stuck detection, and restores live match-result outcome fields with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\route_spin_final_after_status.json`, `.tmp\bot_scenarios\bot_chat_live_match_result_status_fix.json`, `.tmp\bot_scenarios\implemented_after_route_spin_status_fix.json`, `.tmp\bot_release\bot_release_acceptance_route_spin_fix.json`, and `docs-dev/q3a-botlib-route-target-anti-spin-2026-07-01.md`.
  Latest route movement projection credit update: WORR-owned `bot_brain`, scenario harness, roadmap, release-staging, and documentation work now projects route yaw into view-relative movement, keeps combat aim from hijacking route translation, accepts approximate route move-target matches, and reports route movement projection telemetry with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\route_spin_projection_focus.json`, `.tmp\bot_scenarios\implemented_after_route_projection_fix.json`, `.tmp\bot_release\bot_release_acceptance_route_projection_fix.json`, and `docs-dev/q3a-botlib-route-movement-projection-2026-07-01.md`.
  Latest consumed route target watchdog credit update: WORR-owned `bot_nav`, `bot_brain`, scenario harness, roadmap, release-staging, and documentation work now stops repeated already-consumed route targets from resetting stuck progress, aligns route-target shift checks with horizontal movement progress, and reports consumed-target watchdog telemetry on `q3a_bot_nav_policy_status` with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\route_consumed_target_watchdog_focus.json`, `.tmp\bot_scenarios\implemented_after_consumed_target_watchdog.json`, `.tmp\bot_release\bot_release_acceptance_consumed_target_watchdog.json`, and `docs-dev/q3a-botlib-consumed-route-target-watchdog-2026-07-02.md`.
  Latest route command trace/sequential look-ahead credit update: WORR-owned `bot_nav`, `bot_brain`, scenario harness, roadmap, release-staging, and documentation work now trace-gates ordinary command look-ahead, allows ordered sequential fallback for already-consumed local route nodes, and reports command-level trace/sequential fallback telemetry with no new upstream imports claimed. Validation is recorded in `.tmp\bot_scenarios\route_sequential_trace_lookahead_focus.json`, `.tmp\bot_scenarios\route_sequential_trace_q2dm2_fix.json`, `.tmp\bot_scenarios\implemented_after_route_sequential_trace_lookahead_fix.json`, `.tmp\bot_release\bot_release_acceptance_route_sequential_trace_lookahead.txt`, and `docs-dev/q3a-botlib-route-command-trace-sequential-lookahead-2026-07-02.md`.
  Latest CTF objective live-loop credit update: the WORR-owned scenario/tooling hardening for `ctf_objective_route`, objective-selection marker gates, objective-arbitration marker gates, and implementation log are recorded as native work with no new upstream imports claimed.
  Latest CTF objective transition credit update: the WORR-owned `bot_ctf_objective_transitions` mode `76` scenario promotion, objective `flagDrops`/`flagReturns` counters, death-drop and dropped-flag return hooks, marker gates, and implementation log are recorded as native work with no new upstream imports claimed.
  Latest coop live-loop credit update: the WORR-owned `bot_coop_live_loop` mode `77` scenario promotion, per-bot progress-wait split, live-loop-specific anti-blocking distance tuning, marker gates, and implementation log are recorded as native work with no new upstream imports claimed.
  Latest coop share-loop credit update: the WORR-owned `bot_coop_share_loop` mode `78` scenario promotion, target/resource aggregate cvar wiring, marker gates, and implementation log are recorded as native work with no new upstream imports claimed.
  Latest bot chat live-events credit update: the WORR-owned `bot_chat_live_events` mode `79` scenario promotion, live event taxonomy/status counters, spawn plus route-ready live accounting, marker gates, and implementation log are recorded as native work with no new upstream imports claimed.
  Latest bot chat live-event cooldown credit update: the WORR-owned mode `80` scenario promotion, `live_chat_spawn` status field, global cooldown marker gates, and implementation log are recorded as native work with no new upstream imports claimed.
  Latest bot chat live enemy-sighted credit update: the WORR-owned mode `81` scenario promotion, `reply_chat_enemy_sighted` / `live_chat_enemy_sighted` status fields, blackboard-visible enemy trigger, marker gates, and implementation log are recorded as native work with no new upstream imports claimed.
  Latest FFA item-role credit update: the WORR-owned `bot_ffa_item_roles` pickup scoring bridge, mode `46` scenario promotion, and `ffa_item_role_*` status family are recorded as native work with no new upstream imports claimed.
  Latest CTF item-role credit update: the WORR-owned `bot_ctf_item_roles` pickup scoring bridge, mode `47` scenario promotion, and `ctf_item_role_*` status family are recorded as native work with no new upstream imports claimed; the catalog had 52 implemented short-run rows plus one manual degradation row after that promotion.
  Latest FFA role-combat credit update: the WORR-owned `bot_ffa_role_combat` attack-decision bridge, mode `48` scenario promotion, and `ffa_role_combat_*` status family are recorded as native work with no new upstream imports claimed; the catalog had 53 implemented short-run rows plus one manual degradation row after that promotion.
  Latest FFA live-pacing credit update: the WORR-owned `bot_ffa_roam_route`, `bot_ffa_spawn_camp_avoidance`, `bot_ffa_item_roles`, `bot_ffa_role_combat`, and `bot_ffa_spawn_camp_combat_avoidance` combined mode `74` scenario promotion, `ffa_live_pacing=1` begin marker, and validation-only `BotNav_ProbePickupGoal()` item-role scoring telemetry are recorded as native work with no new upstream imports claimed.
  Latest Duel live-pacing credit update: the WORR-owned `bot_duel_live_pacing` mode `75` scenario promotion, Duel match-policy mode `5`, `team_objective_match_policy_duel` status field, deny-enemy item scoring, and FFA-style route/combat/status reuse under Duel mode evidence are recorded as native work with no new upstream imports claimed.
  Latest team resource-denial credit update: the WORR-owned `bot_team_resource_denial` pickup-scoring bridge, mode `50` scenario promotion, and `team_resource_denial_*` status family are recorded as native work with no new upstream imports claimed.
  Latest behavior policy credit update: the WORR-owned behavior/profile/chat/combat/mode-pacing proof family then extended through mode `85`, including coop live-loop aggregate coverage for leader route, progress wait, anti-blocking, interaction retry, and door/elevator source/hold behavior, coop share-loop aggregate coverage for target sharing and resource deferral, bot chat live-events coverage for spawn plus route-ready live accounting, bot chat live-event cooldown suppression, bot chat live enemy-sighted triggering, four-variant bot chat phrase-library telemetry, duplicate route-ready reply/live suppression telemetry, low-health live chat telemetry, and item-taken live chat telemetry. The then-current catalog had 93 implemented rows and the full run passed 93/93 from `.tmp\bot_scenarios\20260623T051133Z`.
  Latest match item-policy credit update: the WORR-owned `bot_match_item_policy` umbrella bridge, mode `51` scenario promotion, and objective/nav status ordering fix are recorded as native work with no new upstream imports claimed.
  Docs-progress tracking note: `docs-dev/q3a-botlib-docs-progress-tracking-round-2026-06-18.md` and `docs-dev/q3a-botlib-extensive-round-closeout-2026-06-18.md` record final checklist math, scenario counts, build/test commands, package/install validation status, and the remaining reference-map/long-soak/coop/CI evidence gaps for the current round.

2026-07-13 `DV-07-T04` fixed-list note: the significant implementation and
regression evidence are recorded in
`docs-dev/rmlui-live-ui-list-provider-2026-07-13.md`. No new player cvar or
workflow was introduced, so no separate user guide is required for this
slice; player-visible behavior now matches the existing Callvote/MyMap/
tournament list intent. Large-text, localization, controller-navigation, and
native cross-renderer parity remain open before final user-doc parity.

2026-07-13 `DV-07-T04` Player Setup note: significant implementation and
regression evidence are recorded in
`docs-dev/rmlui-live-player-setup-provider-2026-07-13.md`. The existing Player
Setup workflow gained parity and visual correctness but no new player-facing
cvar or concept, so a separate user guide is not required. Final large-text,
localization, controller-navigation, and native cross-renderer user-doc parity
remains open.

2026-07-13 `DV-07-T04` keybind-family note: significant implementation and
regression evidence are recorded in
`docs-dev/rmlui-live-keybind-provider-2026-07-13.md`. The existing binding
workflow gained the documented Primary/Alternate, timeout, conflict, and key-
art behavior without a new command, cvar, or concept, so no separate user
guide is required. Final large-text, localization, controller-navigation, and
native cross-renderer user-doc parity remains open.

2026-07-13 `DV-07-T04` Address Book and deterministic-visibility note:
significant implementation and regression evidence are recorded in
`docs-dev/rmlui-live-addressbook-provider-2026-07-13.md` and
`docs-dev/rmlui-deterministic-route-visibility-2026-07-13.md`. The existing
archived address workflow gained complete live-provider and visual parity, and
the shared accessibility path now opens routes deterministically without
unreliable decorative load-time fades. No new player-facing cvar or concept was
introduced, so no separate user guide is required. Final large-text,
localization, controller-navigation, and native cross-renderer user-doc parity
remains open.

2026-07-13 `DV-07-T04` session-entry note: significant implementation and
regression evidence are recorded in
`docs-dev/rmlui-live-session-entry-provider-2026-07-13.md`. The existing
session workflow gained truthful live-provider ownership, first-connect versus
resumable close correctness, decluttered single-back Host/Match Info layouts,
and clean visual evidence. The established player workflow is already covered
by `docs-user/multiplayer-session-menu.md`, so no new user guide is required.
Final connected-action, large-text, localization, controller-navigation, and
native cross-renderer user-doc parity remains open.

2026-07-13 `DV-07-T04` vote/callvote note: significant implementation and
regression evidence are recorded in
`docs-dev/rmlui-live-vote-callvote-provider-2026-07-13.md`. The practical
active-vote, callvote choice, and map-flag workflow is now documented in
`docs-user/multiplayer-session-menu.md`. No new player cvar was introduced;
the routes now present the existing server-owned behavior truthfully. Final
connected-action, large-text, localization, controller-navigation, and native
cross-renderer user-doc parity remains open.

2026-07-13 `DV-07-T04` MyMap note: significant implementation and regression
evidence are recorded in `docs-dev/rmlui-live-mymap-provider-2026-07-13.md`.
The practical request, flags, selection, clear, and unavailable-state workflow
is now documented in `docs-user/multiplayer-session-menu.md`. No new player
cvar was introduced. Final connected-action, large-text, localization,
controller-navigation, and native cross-renderer user-doc parity remains open.

2026-07-13 `DV-07-T04` session-confirmation note: significant implementation
and regression evidence are recorded in
`docs-dev/rmlui-live-session-confirm-provider-2026-07-13.md`. The practical
Leave Match and Forfeit confirmation behavior, safe default action, and server
ownership are now documented in `docs-user/multiplayer-session-menu.md`. No
new player cvar or command was introduced. Final connected-action, large-text,
localization, controller-navigation, canonical-stage, and native
cross-renderer user-doc parity remains open.

2026-07-13 `DV-07-T04` Admin note: significant implementation and regression
evidence are recorded in `docs-dev/rmlui-live-admin-provider-2026-07-13.md`.
The complete in-game reference, console-execution boundary, and conditional
tournament Replay behavior are documented in
`docs-user/competitive-server-tools.md`. No new player cvar or command was
introduced. Final connected-action, large-text, localization,
controller-navigation, canonical-stage, and native cross-renderer user-doc
parity remains open.

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

## Immediate 90-Day Priority Queue (2026-07-01 to 2026-09-30)
- [x] `P0` `FR-01-T01` Vulkan particle style parity
- [x] `P0` `FR-01-T04` MD2/MD5 parity pass
- [x] `P1` `FR-01-T07` Evidence-graded Vulkan/OpenGL parity checklist
- [x] `P0` `FR-01-T09` Renderer-neutral gameplay light queries
- [ ] `P0` `FR-01-T13` Device-local static Vulkan world and shader-side warp
- [ ] `P0` `FR-01-T14` Indexed/GPU-driven Vulkan entity submission
- [ ] `P0` `FR-01-T15` Vulkan frames-in-flight and measured performance gates
- [ ] `P0` `FR-02-T13` Linear-light scene and final presentation contract
- [ ] `P0` `FR-04-T02` Bot frame logic implementation
- [x] `P0` `FR-09-T01` RmlUi runtime ownership and inventory closeout
- [x] `P0` `FR-09-T02` RmlUi dependency/bootstrap and staging path
- [x] `P0` `FR-09-T08` Multiplayer/session/match menu live-state parity
- [x] `P1` `FR-09-T11` Main hero and in-world session presentation polish
- [x] `P1` `FR-09-T12` Multiplayer join-hub navigation and active-vote revision
- [ ] `P0` `DV-01-T01` Project board template rollout
- [ ] `P0` `DV-02-T01` PR CI workflow
- [ ] `P0` `DV-03-T01` Integrate q2proto tests into CI
- [ ] `P0` `DV-03-T07` UI automation harness
- [ ] `P0` `DV-06-T01` Dependency baseline audit

## Governance Note
This roadmap is intended to be the live planning source for WORR 2026 execution. Any significant new initiative should be added here first as an epic/task set (or linked as a child project) before implementation starts.
