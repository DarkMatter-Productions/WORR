# Bot Scenario Smokes

Lightweight local harness for WORR Q3A BotLib scenario validation. It wraps existing dedicated-server smoke modes, parses `q3a_bot_frame_command_status`, and can emit text, JSON, Markdown, and comparison reports.

For implementation history and validation notes, see `docs-dev/q3a-botlib-scenario-smoke-harness-2026-06-18.md`.

## Requirements

- Python 3 standard library only.
- For catalog/report/test-only commands: no game launch is required.
- For implemented scenario runs: `.install/worr_ded_x86_64.exe` and packaged `basew` / `mm-rage` assets must exist, usually after a refreshed install.
- `profile_backed_spawn` also requires a staged smoke profile asset resolvable as `smoke`, normally `.install/basew/botfiles/bots/smoke_c.c`. The harness does not create profile assets.
- Reports and stdout/stderr artifacts are written under `.tmp/bot_scenarios/` by default.

## Quickstart

List known scenarios:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --list
```

Run the implemented smoke suite:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --base-port 28000 --format text --json-out .tmp\bot_scenarios\latest_report.json
```

`implemented` runs the default short suite and skips manual long-running scenarios. Use an explicit scenario name or tag for long soaks.

Run one scenario:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario spawn_route_to_item --timeout 60
```

Run the manual high-bot degradation soak:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario high_bot_soak_degradation --timeout 720 --base-port 28000 --format text --json-out .tmp\bot_scenarios\high_bot_soak_report.json
```

Run only pending placeholders without launching the game:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --json-out .tmp\bot_scenarios\pending_report.json
```

## Scenarios

Implemented:

- `spawn_route_to_item`: mode `2`, verifies item-backed route commands.
- `recover_from_stall`: mode `4`, verifies stuck detection and recovery commands.
- `multi_bot_reservation`: mode `17`, verifies eight-bot route pressure and item reservation peak.
- `map_change_repeat`: mode `19`, verifies two map-repeat cycles, one map change, and final bot cleanup.
- `map_restart_cleanup`: mode `19` with `sv_bot_frame_command_smoke_map_repeat_restart 1`, verifies two route-proof cycles across a forced restart plus final bot cleanup.
- `warmup_bot_start_readiness`: `sv_bot_warmup_smoke 2`, verifies bot-only warmup ready-up start eligibility and final cleanup.
- `vote_bot_exclusion`: `sv_bot_vote_smoke 2`, verifies bot-only players do not count as voting clients, bot-origin vote launches are rejected, and cleanup leaves no active vote.
- `admin_bot_privilege_audit`: `sv_bot_admin_audit_smoke 2`, verifies a forced-admin bot is still blocked from executing the registered `lock_team` admin command and cleanup leaves red team unlocked.
- `tournament_bot_veto_exclusion`: `sv_bot_tournament_smoke 2`, verifies a bot holding the active tournament side identity is still blocked from veto picks and cleanup leaves zero picks/bans.
- `tournament_replay_reset`: `sv_bot_tournament_smoke 3`, verifies invalid replay requests preserve completed-series state and valid game-2 replay rewinds wins/history.
- `match_logging_schema`: `sv_bot_matchlog_smoke 2`, verifies match-stats, tournament-series, and match-catalog JSON schema names, schema versions, artifact types, artifact versions, array shape, embedded match metadata, latest-artifact pointers, indexed relative JSON paths, and scratch catalog write/read behavior.
- `mymap_queue_bot_request`: `sv_bot_mymap_smoke 2`, verifies a bot-attributed MyMap request enters both map queues, is consumed, and cleanup leaves no queued map behind.
- `scoreboard_bot_classification`: `sv_bot_scoreboard_smoke 2`, verifies bot-only FFA standings are sorted by score with bot-classified leader and runner-up rows, then cleaned up.
- `intermission_bot_cleanup`: `sv_bot_intermission_smoke 2`, verifies bot-only intermission entry freezes/moves bots to freecam state and cleanup leaves no sorted-client residue.
- `queued_nextmap_transition`: `sv_bot_nextmap_smoke 2`, verifies a bot-attributed queued map is consumed by nextmap transition, reloads the map, and cleans up retained fake clients.
- `mapvote_bot_exclusion_transition`: `sv_bot_mapvote_smoke 2`, verifies bot selector ballots are blocked, the deterministic selector finalizes, the map reload is observed, and retained fake clients are cleaned up.
- `profile_backed_spawn`: `sv_bot_profile_smoke 2`, verifies profile-backed spawn, userinfo profile fields, and final cleanup.
- `team_policy_duel_readiness`: `sv_bot_team_policy_smoke 2`, verifies existing bot team-policy status before and after cleanup.
- `duel_queue_spectator`: `sv_bot_team_policy_smoke 3`, verifies a surplus Duel bot remains spectator-owned while entering the duel queue.
- `engage_enemy`: mode `20`, verifies live enemy acquisition, attack intent, attack-button application, combat detail counters, and attributed damage.
- `switch_weapons`: mode `21`, verifies weapon-switch decision/request/dispatch/completion counters.
- `health_armor_pickup`: mode `22`, verifies health/armor focus boosts, goal assignments, seek-decision detail, pickups, and pickup deltas.
- `team_objective`: mode `23`, verifies CTF objective route/reach/pickup plus role-policy and lane status.
- `aim_fairness_policy_integration`: mode `24`, verifies live firing flows through aim/fairness policy counters and the live-aim consumer before applying attack input.
- `item_timer_fairness_signals`: mode `25`, verifies deterministic observed-pickup item-timer policy telemetry plus timing-consumer evaluation and ready/live pickup evidence.
- `trace_checked_corner_cutting`: mode `21`, verifies route corner-cut candidates, trace checks, accepted trace-checked shortcuts, and BSP trace telemetry from an existing route-rich smoke.
- `ffa_tdm_match_readiness`: mode `26`, verifies dedicated FFA and TDM readiness proof status with four active bots.
- `coop_match_readiness`: mode `3` with `deathmatch 0` and `coop 1`, verifies cooperative readiness status with active playing bots.
- `coop_leader_route`: mode `3` with `deathmatch 0` and `coop 1`, verifies coop follow/regroup/support policy reaches the timed route-goal owner and compact coop command status.
- `coop_lead_advance`: mode `27` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_lead_advance 1`, verifies no-leader LeadAdvance coop policy reaches timed route-goal ownership.
- `coop_resource_share`: mode `28` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_resource_share 1`, verifies coop resource-share policy reserves route-goal candidates for teammates and defers them in item scoring.
- `coop_anti_blocking`: mode `29` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_anti_blocking 1`, verifies close-to-leader coop policy can own a short anti-blocking movement command.
- `coop_target_share`: mode `30` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_target_share 1`, verifies a coop bot can adopt a teammate's hostile non-client target from the blackboard.
- `coop_door_elevator`: mode `31` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_door_elevator 1`, verifies one coop bot can own a route-detected mover/elevator wait/use interaction while a teammate holds.
- `team_role_route`: mode `32` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_role_route 1`, verifies TDM match role/lane policy can own timed route-goal commands.
- `team_item_roles`: mode `33` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_item_roles 1`, verifies TDM match item-role policy can shape live pickup-goal scoring.
- `team_resource_denial`: mode `50` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_resource_denial 1`, verifies TDM resource policy can boost deny-enemy pickup-goal scoring for contested weapons, powerups, tech, and utility items.
- `match_item_policy`: mode `51` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_match_item_policy 1`, verifies the umbrella match item-policy cvar can activate both TDM item-role pickup scoring and deny-enemy resource scoring without setting the individual proof cvars.
- `behavior_policy_umbrella`: mode `52` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_behavior_enable 1`, verifies the integrated behavior switch activates role-route, role-combat, friendly-fire, and match item-policy helpers without setting the individual proof cvars.
- `behavior_arbitration`: mode `63` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_behavior_enable 1`, verifies central behavior owner arbitration, live/smoke/debug/deprecated cvar classification, route/item/combat candidates, combat ownership, and owner handoffs.
- `target_memory_decay`: mode `64` with `deathmatch 1` and `g_gametype 1`, verifies blackboard enemy acquisition, retained unseen target memory, sticky target-memory smoke occlusion, `1000` ms decay, and clear-after-decay counters.
- `weapon_scoring_arsenal`: mode `65` with `deathmatch 1` and `g_gametype 1`, verifies carried weapon inventory scanning, insufficient-ammo rejection, splash-risk pressure, close-range super-shotgun selection, enemy-estimate finisher scoring, and exact weapon-switch completion.
- `aim_fire_policy_depth`: mode `66` with `deathmatch 1` and `g_gametype 1`, verifies reaction-delay withholding, aim-settle withholding, burst-cooldown pacing, live-aim policy blocks, rocket projectile lead, and eventual attack-button application.
- `ammo_pressure_pickup`: mode `67` with `deathmatch 1`, `g_gametype 1`, and `item_focus=ammo`, verifies low-shell ammo pressure selects a staged shell pickup through item utility, route ownership, ammo-goal assignment counters, and ammo focus boosts.
- `survival_inventory_use`: mode `68` with `deathmatch 1`, `g_gametype 1`, and `survival_inventory=1`, verifies a low-health/no-armor bot selects carried power armor, accepts pending inventory intent, and dispatches the live `use_index_only` inventory command.
- `survival_health_route`: mode `69` with `deathmatch 1`, `g_gametype 1`, and `survival_route=1`, verifies low-health/no-armor survival pressure naturally selects a routeable health pickup without item focus and records health candidate, seek, utility-kind, and health goal-assignment telemetry.
- `survival_armor_route`: mode `70` with `deathmatch 1`, `g_gametype 1`, and `survival_route=armor`, verifies full-health/no-armor survival pressure naturally selects a routeable armor pickup without item focus and records armor candidate, seek, utility-kind, and armor goal-assignment telemetry.
- `combat_survival_regression`: mode `71` with `deathmatch 1`, `g_gametype 1`, and `survival_route=combat_health`, verifies visible enemy combat pressure and low-health survival item pressure coexist in one run, including blackboard/action target facts, withheld-fire evidence, health candidate/seek telemetry, health goal assignment, and item/recovery arbitration ownership.
- `profile_role_policy`: mode `53` with `deathmatch 1` and `g_gametype 3`, verifies staged profile roles feed TDM match-policy requested-role selection.
- `profile_team_policy`: mode `54` with `deathmatch 1` and `g_gametype 5`, verifies staged profile teamplay, objective, and friendly-fire-care hints feed CTF match-policy bonuses.
- `profile_item_policy`: mode `55` with `deathmatch 1`, `g_gametype 3`, `sg_bot_match_item_policy 1`, and `sg_bot_profile_item_policy_smoke 1`, verifies staged profile item-greed, item-denial, powerup-timing, and retreat-health hints feed TDM match item/resource policy bonuses.
- `profile_movement_policy`: mode `56` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_profile_movement_policy_smoke 1`, verifies staged profile movement-style hints feed TDM match-policy attack, defense, roam, collect, and selected-role bonuses without relying on the umbrella behavior cvar.
- `bot_chat_policy`: mode `57` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_allow_chat 1`, verifies staged profile chat metadata and the default-off chat policy gate while proving the conservative live chat consumer submits at least one dispatch without failures.
- `bot_chat_team_policy`: mode `58` with `deathmatch 1`, `g_gametype 3`, `sg_bot_allow_chat 1`, and `sg_bot_chat_team_only 1`, verifies the conservative live chat consumer routes through the team-only audience path.
- `bot_chat_rate_policy`: mode `59` with `deathmatch 1`, `g_gametype 3`, `sg_bot_allow_chat 1`, and `sg_bot_chat_min_interval_ms 60000`, verifies the conservative live chat consumer submits the first dispatch and rate-limits later bot attempts without failures.
- `bot_chat_initial_policy`: mode `60` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_allow_chat 1`, verifies profile chat personalities select deterministic initial utterance buckets before live dispatch.
- `bot_chat_reply_policy`: mode `61` with `deathmatch 1`, `g_gametype 3`, `sg_bot_allow_chat 1`, and `sg_bot_chat_reply_policy_smoke 1`, verifies profile chat personalities select deterministic reply utterances for the first team-ready proof event.
- `bot_chat_event_policy`: mode `62` with `deathmatch 1`, `g_gametype 3`, `sg_bot_allow_chat 1`, and `sg_bot_chat_event_policy_smoke 1`, verifies profile chat personalities select deterministic reply utterances for team-ready and route-ready proof events.
- `team_fire_avoidance`: mode `34` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_fire_avoidance 1`, verifies TDM friendly-fire policy can suppress live attack input before `BUTTON_ATTACK` is applied.
- `ctf_role_route`: mode `35` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_role_route 1`, verifies CTF match role/lane policy can own timed route-goal commands.
- `ctf_role_combat`: mode `36` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_role_combat 1`, verifies CTF match role/lane policy can own live attack input from visible, shootable enemy facts.
- `ctf_dropped_flag_route`: mode `37` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_dropped_flag_route 1`, verifies CTF dropped enemy flag response policy can own route commands to a dropped-flag objective.
- `ctf_carrier_support_route`: mode `38` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_carrier_support_route 1`, verifies CTF same-team flag-carrier support policy can own route commands to the carrier-support objective.
- `ctf_base_return_route`: mode `39` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_base_return_route 1`, verifies CTF own-flag return policy can own route commands to an enemy own-flag carrier through the own-base-return lane.
- `ctf_objective_route`: mode `40` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_objective_route 1`, verifies the combined CTF objective route policy sees base-return, carrier-support, and dropped-flag candidates while recording higher-priority route selections and lower-priority deferrals.
- `ctf_objective_route_precedence`: mode `41` with `deathmatch 1`, `g_gametype 5`, `sg_bot_ctf_role_route 1`, and `sg_bot_ctf_objective_route 1`, verifies the generic CTF role-route owner records objective-route deferrals while the objective route policy still commands the selected flag route.
- `ffa_roam_route`: mode `42` with `deathmatch 1`, `g_gametype 1`, and `sg_bot_ffa_roam_route 1`, verifies FFA roam/collect/engage policy can own timed route-goal commands.
- `team_role_combat`: mode `43` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_role_combat 1`, verifies TDM match role/lane policy can own live attack input from visible, shootable enemy facts.
- `team_role_combat_avoidance`: mode `44` with `deathmatch 1`, `g_gametype 3`, `sg_bot_team_role_combat 1`, and `sg_bot_team_fire_avoidance 1`, verifies TDM role-combat attack ownership can feed the friendly-fire avoidance veto path.
- `ffa_spawn_camp_avoidance`: mode `45` with `deathmatch 1`, `g_gametype 1`, `sg_bot_ffa_roam_route 1`, and `sg_bot_ffa_spawn_camp_avoidance 1`, verifies FFA anti-camp policy can source timed route-goal commands away from a nearby live opponent.
- `ffa_item_roles`: mode `46` with `deathmatch 1`, `g_gametype 1`, and `sg_bot_ffa_item_roles 1`, verifies FFA match item-role policy can shape live pickup-goal scoring.
- `ffa_role_combat`: mode `48` with `deathmatch 1`, `g_gametype 1`, and `sg_bot_ffa_role_combat 1`, verifies FFA match role/lane policy can own live attack input from visible, shootable enemy facts.
- `ffa_spawn_camp_combat_avoidance`: mode `49` with `deathmatch 1`, `g_gametype 1`, `sg_bot_ffa_role_combat 1`, `sg_bot_ffa_spawn_camp_avoidance 1`, and `sg_bot_ffa_spawn_camp_combat_avoidance 1`, verifies FFA anti-camp policy can veto a role-combat attack when the selected target is the nearby spawn-camp source.
- `ctf_item_roles`: mode `47` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_item_roles 1`, verifies CTF match item-role policy can shape live pickup-goal scoring.
- `coop_progress_wait`: mode `3` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_progress_wait 1`, verifies WaitForLeader coop policy consumption reaches command ownership.
- `coop_interaction_retry`: mode `12` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_interaction_retry 1`, verifies detected route interactions can own wait/use command retry windows.

Manual long-running:

- `high_bot_soak_degradation`: mode `18`, ten-minute eight-bot soak. Select it by name or with `--scenario soak`; it is omitted from `--scenario implemented` so the default suite stays fast.

Pending placeholders:

- None in the default catalog after the 2026-06-22 aim/fire policy promotion round.

Pending rows are reported but do not fail the suite unless `--fail-on-pending` is passed. With the current catalog, `--scenario pending` is a no-launch empty report unless new future rows are added.

## Catalog

Emit the declarative scenario catalog:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --catalog --format json --json-out .tmp\bot_scenarios\catalog_report.json
```

Catalog entries include scenario status, task IDs, smoke mode, runtime budget, manual-only status, selection tags, required status metrics, marker metrics, degradation policy metadata, extra cvars, and pending blockers.

When pending catalog rows exist, they also include planned source smoke modes, promotion-required metrics, and promotion check criteria. Those fields describe the counters and pass/fail values a future source-backed smoke must satisfy before the placeholder can become an implemented scenario.

## Optional Status Fields

The harness tolerates and reports newly added status fields without making them hard gates. Scenario JSON rows include `optional_fields` when matching metrics appear in the selected `q3a_bot_frame_command_status` line or optional marker streams such as `q3a_bot_action_status`.

Current optional discovery families:

- `action_dispatch_counters`: weapon/inventory command-request build, accept/reject, dispatch, defer, submit, failure, carried-weapon inventory scan, ammo-skip, splash-safety, range-selection, estimate-selection, and selected-weapon metadata counters from `q3a_bot_action_status`.
- `live_combat_firing_counters`: enemy acquisition, combat evaluation, fire/withhold, attack-button, and damage proof counters.
- `aim_policy_counters`: aim/fairness policy evaluation, allow/block, live-aim consumer, projectile lead, and last-policy metadata when combat status owners expose it.
- `special_item_utility_buckets`: special item candidate, seek-decision, boost, and last-kind buckets for powerups, techs, mobility, protection, invisibility, and CTF objective pickups.
- `item_timer_fairness_signals`: item-timer fairness observation, allowance/block, fuzzing, timing-policy details, timing-consumer details, and last-timer metadata. When `item_timing_consumer_ready` or `item_timing_consumer_live_pickups` is present, the parser also reports `item_timing_consumer_ready_or_live` as a derived ready/live proof.
- `route_target_stabilization_counters`: route-target stabilization checks, applications, skips, and last sampled target metadata from frame-command status.
- `trace_checked_corner_cutting_signals`: trace-checked corner-cut candidate, trace, accept/reject, and last-corner metadata.
- `team_mode_readiness_signals`: team-policy, objective role/lane, and blackboard team-role signals used by FFA/TDM/CTF/coop readiness work.
- `mymap_match_flow_signals`: bot-attributed MyMap request, queue counts, and queued-map consumption status from match-flow MyMap smoke.
- `scoreboard_match_flow_signals`: bot/human standings classification, sorted-client ranks, and score application status from match-flow scoreboard smoke.
- `intermission_match_flow_signals`: bot-only intermission entry, frozen/freecam state, change-map target, and cleanup status from match-flow intermission smoke.
- `nextmap_match_flow_signals`: queued nextmap target, queue consumption, map-transition request, and post-reload cleanup status from match-flow nextmap smoke.
- `mapvote_match_flow_signals`: map selector activity, candidate/vote/finalize state, bot ballot exclusion, reload transition, and post-reload cleanup status from match-flow map-vote smoke.
- `match_logging_schema_signals`: match-stats and tournament-series schema/version metadata from match logging schema smoke.
- `match_logging_catalog_signals`: match catalog schema/version metadata, latest-artifact pointers, indexed relative JSON paths, and scratch write/read proof from match logging schema smoke.
- `ffa_roam_route_counters`: default-off FFA roam/collect/engage route-owner requests, activations, route requests, and latest role metadata from frame-command status.
- `ffa_spawn_camp_avoidance_counters`: default-off FFA anti-camp route-source requests, policy/source selections, activations, fallbacks, route requests, and latest source/goal metadata from compact frame-command status.
- `ffa_item_role_counters`: default-off FFA match item-role scoring bridge evaluations, selected pickup goals, and latest role/category metadata from nav policy status.
- `ffa_role_combat_counters`: default-off FFA match role/lane combat-owner requests, target selections, attack decisions, and latest visible/shootable target metadata from frame-command status.
- `ffa_spawn_camp_combat_avoidance_counters`: default-off FFA anti-camp combat-veto evaluations, blocks, clears, and latest target/source metadata from compact frame-command status.
- `team_role_route_counters`: default-off match role/lane route-owner requests, activations, route requests, and latest role metadata from frame-command status.
- `team_role_combat_counters`: default-off TDM match role/lane combat-owner requests, target selections, attack decisions, and latest visible/shootable target metadata from frame-command status.
- `ctf_role_route_counters`: default-off CTF match role/lane route-owner requests, activations, objective-route deferrals, route requests, and latest role metadata from frame-command status.
- `ctf_role_combat_counters`: default-off CTF match role/lane combat-owner requests, target selections, attack decisions, and latest visible/shootable target metadata from frame-command status.
- `ctf_dropped_flag_route_counters`: default-off CTF dropped enemy flag route-owner requests, assignments, route requests, route commands, invalid skips, and latest dropped-flag objective metadata from frame-command status.
- `ctf_carrier_support_route_counters`: default-off CTF flag-carrier support route-owner requests, assignments, route requests, route commands, invalid skips, and latest carrier-support objective metadata from frame-command status.
- `ctf_base_return_route_counters`: default-off CTF base-return route-owner requests, assignments, route requests, route commands, invalid skips, and latest own-flag return objective metadata from frame-command status.
- `ctf_objective_route_counters`: default-off CTF objective-route policy requests, candidate availability, priority selections, lower-priority deferrals, route commands, invalid skips, and latest selected objective metadata from frame-command status.
- `ctf_item_role_counters`: default-off CTF match item-role scoring bridge evaluations, selected pickup goals, and latest role/category metadata from nav policy status.
- `team_fire_avoidance_counters`: default-off TDM friendly-fire policy evaluations, live attack suppressions, and latest blocked target/line metadata from frame-command status.
- `team_item_role_counters`: default-off TDM match item-role scoring bridge evaluations, selected pickup goals, and latest role/category metadata from nav policy status.
- `team_resource_denial_counters`: default-off TDM resource-denial scoring bridge evaluations, deny-enemy policy selections, selected pickup goals, and latest role/category/intent metadata from nav policy status.
- `profile_item_policy_counters`: profile item-greed, item-denial, powerup-timing, and retreat-health match-policy presence/applied counters plus selected-goal bonus propagation from objective and nav policy status.
- `profile_movement_policy_counters`: profile movement-style presence, attack/defense/roam/evasive buckets, applied counts, selected-role bonuses, and last movement style metadata from objective policy status.
- `coop_leader_route_counters`: timed route-goal activation, refresh, source-selection, deferral, and last-leader metadata from frame-command and compact coop command status.
- `coop_lead_advance_counters`: compact coop command-owner counters for the default-off no-leader LeadAdvance timed route-goal proof.
- `coop_progress_wait_counters`: compact coop command-owner counters for the default-off WaitForLeader progression-wait proof.
- `coop_interaction_retry_counters`: compact coop command-owner counters for the default-off route interaction wait/use retry proof.
- `coop_anti_block_counters`: compact coop command-owner counters for the default-off close-leader anti-blocking proof.
- `coop_target_share_counters`: compact coop target-sharing counters for blackboard source scans, adoptions, and last shared target/source metadata.
- `coop_door_elevator_counters`: compact coop command-owner counters for source mover/elevator interaction ownership, teammate hold commands, and last interaction metadata.
- `target_memory_counters`: blackboard enemy memory retention, target-memory smoke occlusion, decay, seed diagnostics, memory age/window, and final decay entity/client metadata from `q3a_bot_blackboard_status`.

These fields are visible in text, JSON, Markdown, and pending-gap reports when present. They do not satisfy or fail scenario gates unless a scenario explicitly promotes one of them into `checks` or `marker_checks`.

## High-Bot Degradation Policy

High bot count validation is split between a fast pressure proof and an opt-in soak:

- `multi_bot_reservation` is the short eight-bot pressure gate. It does not allow degradation: all eight bots must emit commands, route commands must stay clean, and item reservation pressure must reach eight active reservations.
- `high_bot_soak_degradation` is the long eight-bot degradation gate. It allows final item reservation occupancy and peak reservations to drop below the short proof because long soaks consume, hide, clear, and reassign goals over time. It still requires sustained command throughput, sustained route commands, zero route failures, zero invalid route slots, no inactive target bots, and regular soak progress reports.
- Derived per-bot/sec budget thresholds remain owned by `tools/bot_perf/default_soak_budget.json`; the scenario harness reports that budget profile instead of duplicating the perf analyzer.

## Pending Gap Reports

Analyze an existing JSON report, usually `.tmp\bot_scenarios\latest_report.json`, to see which pending scenario rows and source counters are still missing:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --format text --json-out .tmp\bot_scenarios\pending_gap_report.json
```

This command does not launch the game. It compares pending placeholders against the report fixture and prints whether each scenario is ready for harness promotion or blocked by missing scenario rows, wrong smoke modes, pending fixture rows, absent status/marker metrics, absent policy-consumer evidence, or failed promotion metric checks. After modes `20` through `66`, `trace_checked_corner_cutting`, `coop_match_readiness`, `coop_leader_route`, `coop_progress_wait`, and `coop_interaction_retry` were promoted, the default pending set is empty.

Raw reserved-mode logs can be included when reserved modes have been run outside the normal scenario catalog:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --pending-gap-report .tmp\bot_scenarios\latest_report.json --pending-gap-raw-log .tmp\bot_scenarios\raw_modes --format text --json-out .tmp\bot_scenarios\pending_gap_with_raw_modes.json
```

`--pending-gap-raw-log` accepts a file or directory and may be repeated. The parser groups logs by `q3a_bot_frame_command_smoke_scenario=begin`, then reads the latest `q3a_bot_frame_command_status`, `q3a_bot_blackboard_status`, `q3a_bot_action_status`, `q3a_bot_objective_status`, and `q3a_bot_source_counter_status` markers for that reserved run. If repeated diagnostics exist for one reserved mode, the latest parsed run for that mode is the promotion source. Raw diagnostics can satisfy marker/metric presence in the gap report, but the scenarios remain pending until the real runtime counters pass their promotion checks.

When the same metric appears on more than one raw marker inside a reserved run, the later log line wins. Gap reports also include `missing_metric_sources` so absent promotion counters identify the raw status marker that should emit them, for example `item_health_pickups<-q3a_bot_action_status`. Policy scenarios additionally include `missing_policy_consumer_fields` so absent live-aim or item timing-consumer markers are visible without reading every marker check by hand.

The promoted source-backed smoke mode numbers are fixed for compatibility with server work:

- `engage_enemy`: mode `20`
- `switch_weapons`: mode `21`
- `health_armor_pickup`: mode `22`
- `team_objective`: mode `23`
- `aim_fairness_policy_integration`: mode `24`
- `item_timer_fairness_signals`: mode `25`
- `ffa_tdm_match_readiness`: mode `26`
- `coop_lead_advance`: mode `27`
- `coop_resource_share`: mode `28`
- `coop_anti_blocking`: mode `29`
- `coop_target_share`: mode `30`
- `coop_door_elevator`: mode `31`
- `team_role_route`: mode `32`
- `team_item_roles`: mode `33`
- `team_fire_avoidance`: mode `34`
- `ctf_role_route`: mode `35`
- `ctf_role_combat`: mode `36`
- `ctf_dropped_flag_route`: mode `37`
- `ctf_carrier_support_route`: mode `38`
- `ctf_base_return_route`: mode `39`
- `ctf_objective_route`: mode `40`
- `ctf_objective_route_precedence`: mode `41`
- `ffa_roam_route`: mode `42`
- `team_role_combat`: mode `43`
- `team_role_combat_avoidance`: mode `44`
- `ffa_spawn_camp_avoidance`: mode `45`
- `ffa_item_roles`: mode `46`
- `ctf_item_roles`: mode `47`
- `ffa_role_combat`: mode `48`
- `ffa_spawn_camp_combat_avoidance`: mode `49`
- `team_resource_denial`: mode `50`
- `match_item_policy`: mode `51`
- `behavior_policy_umbrella`: mode `52`
- `profile_role_policy`: mode `53`
- `profile_team_policy`: mode `54`
- `profile_item_policy`: mode `55`
- `profile_movement_policy`: mode `56`
- `bot_chat_policy`: mode `57`
- `bot_chat_team_policy`: mode `58`
- `bot_chat_rate_policy`: mode `59`
- `bot_chat_initial_policy`: mode `60`
- `bot_chat_reply_policy`: mode `61`
- `bot_chat_event_policy`: mode `62`
- `behavior_arbitration`: mode `63`
- `target_memory_decay`: mode `64`
- `weapon_scoring_arsenal`: mode `65`
- `aim_fire_policy_depth`: mode `66`
- `ammo_pressure_pickup`: mode `67`
- `survival_inventory_use`: mode `68`
- `survival_health_route`: mode `69`
- `survival_armor_route`: mode `70`
- `combat_survival_regression`: mode `71`

Additional promoted rows reuse existing smoke coverage:

- `trace_checked_corner_cutting`: mode `21`, using the existing switch-weapon reserved smoke because it produces deterministic route corner-cut trace and acceptance telemetry.
- `map_restart_cleanup`: mode `19`, with extra cvar `sv_bot_frame_command_smoke_map_repeat_restart 1`, so the existing map-repeat smoke runs the forced restart path and validates restart cleanup markers.
- `warmup_bot_start_readiness`: `sv_bot_warmup_smoke 2`, so the warmup smoke reports `q3a_bot_warmup_status` before and after cleanup while validating the bot-only `match_start_no_humans` path.
- `vote_bot_exclusion`: `sv_bot_vote_smoke 2`, so the vote smoke reports `q3a_bot_vote_status`, tries a harmless bot-origin `random 2` vote through the game vote helper, and requires the explicit `bot_blocked` rejection path.
- `admin_bot_privilege_audit`: `sv_bot_admin_audit_smoke 2`, so the admin audit smoke reports `q3a_bot_admin_audit_status`, temporarily grants a bot admin session state, attempts `lock_team red`, requires `reason=bot_admin_blocked`, and verifies `red_locked=0` after cleanup.
- `tournament_bot_veto_exclusion`: `sv_bot_tournament_smoke 2`, so the tournament smoke reports `q3a_bot_tournament_status`, assigns the bot the active home-side veto identity, attempts a veto pick, requires `reason=bot_blocked`, and verifies `picks=0` and `bans=0` after cleanup.
- `tournament_replay_reset`: `sv_bot_tournament_smoke 3`, so the tournament smoke seeds a completed best-of-three history, requires an out-of-range replay to preserve state with `reason=range_error`, then replays game 2 and verifies `games_played=1`, one retained winner/map/id, reopened series state, and `reset_applied=1`.
- `match_logging_schema`: `sv_bot_matchlog_smoke 2`, so the match logging smoke builds sample artifacts through the native JSON exporters and requires `worr.match_stats` / `worr.tournament_series` schema metadata plus retained array shape, then verifies the `worr.match_catalog` downstream index contract.
- `mapvote_bot_exclusion_transition`: `sv_bot_mapvote_smoke 2`, so the map-vote smoke reports `q3a_bot_mapvote_status`, begins the native selector on the staged current map, proves a bot ballot is blocked with zero counted votes, finalizes, observes reload, and validates final cleanup.
- `coop_match_readiness`: mode `3`, with extra cvars `deathmatch 0` and `coop 1`, so the existing frame-command smoke reports `q3a_bot_coop_readiness_status pass=1`.
- `coop_leader_route`: mode `3`, with extra cvars `deathmatch 0` and `coop 1`, so the existing frame-command smoke reports `coop_leader_route_*` route-owner counters on both the verbose frame-command marker and compact coop command marker.
- `coop_progress_wait`: mode `3`, with extra cvars `deathmatch 0`, `coop 1`, and `sg_bot_coop_progress_wait 1`, so the existing frame-command smoke reports `coop_progress_wait_*` command-owner counters and WaitForLeader objective policy telemetry.
- `coop_interaction_retry`: mode `12`, with extra cvars `deathmatch 0`, `coop 1`, and `sg_bot_coop_interaction_retry 1`, so the existing elevator route smoke reports `coop_interaction_retry_*` command-owner counters plus route interaction context telemetry.

There are currently no default pending scenarios. Future pending rows should only be added when a new behavior has a concrete promotion contract and a known source-owned status surface.

For `health_armor_pickup`, generic `item_goal_*`, `last_item_goal_*`, and `last_failed_goal_*` status metrics are surfaced as related telemetry when present. They do not satisfy the health/armor promotion gate by themselves; the gate still requires health/armor-specific boost, assignment, pickup, and delta counters.

## Markdown And Comparison Reports

Write JSON and Markdown for a run:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario implemented --timeout 120 --json-out .tmp\bot_scenarios\latest_report.json --markdown-out .tmp\bot_scenarios\latest_report.md
```

Compare a current report with a previous JSON report:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario pending --json-out .tmp\bot_scenarios\pending_compare_report.json --markdown-out .tmp\bot_scenarios\pending_compare_report.md --compare .tmp\bot_scenarios\latest_report.json
```

The comparison is name-based and reports status changes plus selected key metric deltas. It is intended as a quick local regression aid, not a statistical trend analyzer.

## Tests

Run offline parser/reporting tests:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

The tests use only the Python standard library. If `.tmp/bot_scenarios/latest_report.json` exists, they also validate key real-report scenario outcomes, including `map_change_repeat` when present. If the fixture is missing, that fixture check is skipped.

Compile-check the harness:

```powershell
python -m py_compile tools\bot_scenarios\run_bot_scenarios.py tools\bot_scenarios\test_run_bot_scenarios.py
```
