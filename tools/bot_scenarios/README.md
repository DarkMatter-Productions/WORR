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
- `team_fire_avoidance`: mode `34` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_fire_avoidance 1`, verifies TDM friendly-fire policy can suppress live attack input before `BUTTON_ATTACK` is applied.
- `ctf_role_route`: mode `35` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_role_route 1`, verifies CTF match role/lane policy can own timed route-goal commands.
- `ctf_role_combat`: mode `36` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_role_combat 1`, verifies CTF match role/lane policy can own live attack input from visible, shootable enemy facts.
- `ctf_dropped_flag_route`: mode `37` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_dropped_flag_route 1`, verifies CTF dropped enemy flag response policy can own route commands to a dropped-flag objective.
- `ctf_carrier_support_route`: mode `38` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_carrier_support_route 1`, verifies CTF same-team flag-carrier support policy can own route commands to the carrier-support objective.
- `ctf_base_return_route`: mode `39` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_base_return_route 1`, verifies CTF own-flag return policy can own route commands to an enemy own-flag carrier through the own-base-return lane.
- `ctf_objective_route`: mode `40` with `deathmatch 1`, `g_gametype 5`, and `sg_bot_ctf_objective_route 1`, verifies the combined CTF objective route policy sees base-return, carrier-support, and dropped-flag candidates while recording higher-priority route selections and lower-priority deferrals.
- `ctf_objective_route_precedence`: mode `41` with `deathmatch 1`, `g_gametype 5`, `sg_bot_ctf_role_route 1`, and `sg_bot_ctf_objective_route 1`, verifies the generic CTF role-route owner records objective-route deferrals while the objective route policy still commands the selected flag route.
- `ffa_roam_route`: mode `42` with `deathmatch 1`, `g_gametype 1`, and `sg_bot_ffa_roam_route 1`, verifies FFA roam/collect/engage policy can own timed route-goal commands.
- `ffa_spawn_camp_avoidance`: mode `45` with `deathmatch 1`, `g_gametype 1`, `sg_bot_ffa_roam_route 1`, and `sg_bot_ffa_spawn_camp_avoidance 1`, verifies FFA anti-camp policy can source timed route-goal commands away from a nearby live opponent.
- `team_role_combat`: mode `43` with `deathmatch 1`, `g_gametype 3`, and `sg_bot_team_role_combat 1`, verifies TDM match role/lane policy can own live attack input from visible, shootable enemy facts.
- `team_role_combat_avoidance`: mode `44` with `deathmatch 1`, `g_gametype 3`, `sg_bot_team_role_combat 1`, and `sg_bot_team_fire_avoidance 1`, verifies TDM role-combat attack ownership can feed the friendly-fire avoidance veto path.
- `coop_progress_wait`: mode `3` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_progress_wait 1`, verifies WaitForLeader coop policy consumption reaches command ownership.
- `coop_interaction_retry`: mode `12` with `deathmatch 0`, `coop 1`, and `sg_bot_coop_interaction_retry 1`, verifies detected route interactions can own wait/use command retry windows.

Manual long-running:

- `high_bot_soak_degradation`: mode `18`, ten-minute eight-bot soak. Select it by name or with `--scenario soak`; it is omitted from `--scenario implemented` so the default suite stays fast.

Pending placeholders:

- None in the default catalog after the 2026-06-21 promotion round.

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

- `action_dispatch_counters`: weapon/inventory command-request build, accept/reject, dispatch, defer, submit, and failure counters from `q3a_bot_action_status`.
- `live_combat_firing_counters`: enemy acquisition, combat evaluation, fire/withhold, attack-button, and damage proof counters.
- `aim_policy_counters`: aim/fairness policy evaluation, allow/block, live-aim consumer, projectile lead, and last-policy metadata when combat status owners expose it.
- `special_item_utility_buckets`: special item candidate, seek-decision, boost, and last-kind buckets for powerups, techs, mobility, protection, invisibility, and CTF objective pickups.
- `item_timer_fairness_signals`: item-timer fairness observation, allowance/block, fuzzing, timing-policy details, timing-consumer details, and last-timer metadata. When `item_timing_consumer_ready` or `item_timing_consumer_live_pickups` is present, the parser also reports `item_timing_consumer_ready_or_live` as a derived ready/live proof.
- `route_target_stabilization_counters`: route-target stabilization checks, applications, skips, and last sampled target metadata from frame-command status.
- `trace_checked_corner_cutting_signals`: trace-checked corner-cut candidate, trace, accept/reject, and last-corner metadata.
- `team_mode_readiness_signals`: team-policy, objective role/lane, and blackboard team-role signals used by FFA/TDM/CTF/coop readiness work.
- `ffa_roam_route_counters`: default-off FFA roam/collect/engage route-owner requests, activations, route requests, and latest role metadata from frame-command status.
- `ffa_spawn_camp_avoidance_counters`: default-off FFA anti-camp route-source requests, policy/source selections, activations, fallbacks, route requests, and latest source/goal metadata from compact frame-command status.
- `team_role_route_counters`: default-off match role/lane route-owner requests, activations, route requests, and latest role metadata from frame-command status.
- `team_role_combat_counters`: default-off TDM match role/lane combat-owner requests, target selections, attack decisions, and latest visible/shootable target metadata from frame-command status.
- `ctf_role_route_counters`: default-off CTF match role/lane route-owner requests, activations, objective-route deferrals, route requests, and latest role metadata from frame-command status.
- `ctf_role_combat_counters`: default-off CTF match role/lane combat-owner requests, target selections, attack decisions, and latest visible/shootable target metadata from frame-command status.
- `ctf_dropped_flag_route_counters`: default-off CTF dropped enemy flag route-owner requests, assignments, route requests, route commands, invalid skips, and latest dropped-flag objective metadata from frame-command status.
- `ctf_carrier_support_route_counters`: default-off CTF flag-carrier support route-owner requests, assignments, route requests, route commands, invalid skips, and latest carrier-support objective metadata from frame-command status.
- `ctf_base_return_route_counters`: default-off CTF base-return route-owner requests, assignments, route requests, route commands, invalid skips, and latest own-flag return objective metadata from frame-command status.
- `ctf_objective_route_counters`: default-off CTF objective-route policy requests, candidate availability, priority selections, lower-priority deferrals, route commands, invalid skips, and latest selected objective metadata from frame-command status.
- `team_fire_avoidance_counters`: default-off TDM friendly-fire policy evaluations, live attack suppressions, and latest blocked target/line metadata from frame-command status.
- `team_item_role_counters`: default-off TDM match item-role scoring bridge evaluations, selected pickup goals, and latest role/category metadata from nav policy status.
- `coop_leader_route_counters`: timed route-goal activation, refresh, source-selection, deferral, and last-leader metadata from frame-command and compact coop command status.
- `coop_lead_advance_counters`: compact coop command-owner counters for the default-off no-leader LeadAdvance timed route-goal proof.
- `coop_progress_wait_counters`: compact coop command-owner counters for the default-off WaitForLeader progression-wait proof.
- `coop_interaction_retry_counters`: compact coop command-owner counters for the default-off route interaction wait/use retry proof.
- `coop_anti_block_counters`: compact coop command-owner counters for the default-off close-leader anti-blocking proof.
- `coop_target_share_counters`: compact coop target-sharing counters for blackboard source scans, adoptions, and last shared target/source metadata.
- `coop_door_elevator_counters`: compact coop command-owner counters for source mover/elevator interaction ownership, teammate hold commands, and last interaction metadata.

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

This command does not launch the game. It compares pending placeholders against the report fixture and prints whether each scenario is ready for harness promotion or blocked by missing scenario rows, wrong smoke modes, pending fixture rows, absent status/marker metrics, absent policy-consumer evidence, or failed promotion metric checks. After modes `20` through `45`, `trace_checked_corner_cutting`, `coop_match_readiness`, `coop_leader_route`, `coop_progress_wait`, and `coop_interaction_retry` were promoted, the default pending set is empty.

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

Additional promoted rows reuse existing smoke coverage:

- `trace_checked_corner_cutting`: mode `21`, using the existing switch-weapon reserved smoke because it produces deterministic route corner-cut trace and acceptance telemetry.
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
