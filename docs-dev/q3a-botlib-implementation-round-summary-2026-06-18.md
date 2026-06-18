# Q3A BotLib Implementation Round Summary - 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T13`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-03-T05`, `DV-05-T02`, `DV-05-T03`, `DV-05-T05`, `DV-07-T06`

## Completed Lanes

- Profile and botfile lane: first-party Q3/Gladiator-style `assets/botfiles` families now exist for `smoke`, `vanguard`, `bulwark`, `relay`, and `vector`; the loader strips `_c`, skips `_w/_i/_t` companions as profile records, emits deterministic scan markers, and refreshed installs mirror `botfiles` loose while still packaging them in `pak0.pkz`.
- Scenario lane: the implemented scenario suite now includes `spawn_route_to_item`, `recover_from_stall`, `multi_bot_reservation`, `map_change_repeat`, and `profile_backed_spawn`. Pending promotion modes `20` through `23` are reserved, but still pending.
- Perception lane: `bot_brain.*` now owns a per-bot blackboard for visible enemy, last-seen, heard/damaged facts, staggered visible-enemy scans, memory decay, and status lines for future combat/objective smokes.
- Phase 6 support lane: `bot_combat.*` has Q2/WORR weapon metadata and a pure preferred-weapon scoring helper; `bot_items.*` has item utility scoring, health/armor focus routing support, and observed pickup hooks; `bot_actions.*` exposes future weapon-switch and item counters plus detailed action-application results; `bot_brain.*` applies validated attack/use decisions to `usercmd_t` while leaving weapon/inventory commands as pending intents.
- Reserved-mode lane: server smoke modes `20` through `23` now set deterministic scenario cvars, print begin markers, and wait for runtime-backed frame status before final capture when early frames report skipped runtime work.
- Team-objective lane: `bot_objectives.*` provides the first helper scaffold for flag/objective assignment facts, and `bot_brain.*` exposes compact frame status plus a dedicated `q3a_bot_objective_status` line. Route/reach/flag-event hooks are deliberately zero until real callers own them.
- Validation/perf lane: scenario promotion gap reporting now checks semantic metric values, not only metric names, and can parse raw reserved-mode diagnostics without promoting rows. The perf analyzer consumes optional source-counter fields when present, while import/adapter source-counter getters now exist for route, visibility, entity-trace, and static BSP trace counters. `bot_brain.*` now emits those non-timing source counters through a dedicated `q3a_bot_source_counter_status` marker so the primary frame-command line stays below the print-buffer ceiling. CPU timing remains pending.

## Validation Results

- Profile assets: `python tools\bot_profiles\validate_bot_profiles.py` passed with 5 character files, 5 profiles, 0 errors, and 0 warnings; package inspection found the bot companion scripts and `botfiles/chars.h`.
- Profile scenario: `profile_backed_spawn` passed after install refresh and the Q3-style botfile reshape; the loader saw 20 `.c` candidates, skipped 15 companions, loaded 5 active profiles, spawned `B|Smoke`, verified userinfo fields, and cleaned up to final count 0.
- Implemented scenarios: `run_bot_scenarios.py --scenario implemented` passed all five implemented scenarios after promotion-check work.
- Pending gap report without raw reserved logs: latest diagnostics reported 0 ready, 4 blocked, 4 missing rows, 42 missing status metrics, 28 missing marker metrics, and no failed metric checks because no source-backed rows exist yet for modes `20` through `23`.
- Pending gap report with raw reserved-mode logs and the mode `22` health/armor proof: latest diagnostics reported 0 ready, 4 blocked, 6 raw diagnostics parsed, 4 pending rows backed by raw diagnostics, 17 missing status metrics, 0 missing marker metrics, and 37 failed metric checks. The scenarios remain pending.
- Build/object checks recorded by lane docs: targeted bot item/action/brain/combat object builds passed; a later `sgame_x86_64.dll` relink and refresh-install passed for the action telemetry lane.
- Action application: `meson compile -C builddir-win sgame_x86_64` passed after the shared build lock cleared. `BotActions_ApplyDecisionDetailed()` now records accepted/rejected applications, button mutations, pending weapon/inventory intents, and last failure reasons.
- Health/armor routing: reserved mode `22` passed the focused routing proof with `pass=1`, `route_failures=0`, `item_focus=health_armor`, `item_goal_scans=15`, `item_goal_candidates=329`, `item_goal_assignments=15`, and final `last_item_goal_item=4` (`IT_ARMOR_SHARD`).
- Team objective scaffold: `meson compile -C builddir-win-bootstrap-hosted sgame_x86_64` passed on rerun for `bot_objectives.*` and status exposure. No scenario promotion or runtime objective smoke was run in that lane.
- Source-counter plumbing: `git diff --check` on the adapter/import files passed, `meson compile -C builddir-win sgame_x86_64` passed, and `refresh_install.py --package-q2aas-aas` passed. A short eight-bot soak printed `q3a_bot_source_counter_status` with route-build, visibility, entity-trace, and BSP-trace counters.
- Perf analyzer: `python -m py_compile` and unit tests passed for source-counter analyzer support, including legacy logs and synthetic CPU/visibility/trace fields. `python -B tools\bot_perf\analyze_bot_perf.py .install\basew\logs\logs\source_counter_soak.log` also parsed the split source-counter marker successfully. The default 540-second soak budget was not run for this short smoke log.
- Final plan count: `docs-dev/plans/q3a-botlib-aas-port.md` now has 483 of 659 total checkboxes complete (73.3%), or 483 of 647 phase checkboxes complete (74.7%).

## Pending Scenarios And Metrics

- `engage_enemy` / mode `20` remains pending. The blackboard can report acquisition/visibility/shootability, but promotion still needs attack intent, applied attack button, and attributed damage metrics from real bot behavior.
- `switch_weapons` / mode `21` remains pending. Request/completion/failure counters, weapon metadata, and pending-intent recording exist, but no validated command path applies and observes weapon switches yet.
- `health_armor_pickup` / mode `22` remains pending. Focused health/armor route assignment now passes, but deterministic low-health/low-armor setup and real pickup delta pass counters still need final validation.
- `team_objective` / mode `23` remains pending. The server can enable the smoke contract and `q3a_bot_objective_status` can expose helper counters, but route requests, route commands, reaches, flag pickups, captures, and deterministic map setup are still absent.
- Source-side performance counters are partially landed. Analyzer support, import/adapter non-timing getters, and `sgame` status emission are ready for route builds, PVS/PHS, visibility decompression, entity traces, and static BSP traces, but CPU timing fields and longer soak-budget validation have not landed.

## Outstanding Work

- Phase 4: complete broader `Entity_UpdateState` coverage, current-goal/route/stuck/reservation/team-role blackboard integration, item desirability staggering, and fairness controls for FOV, reaction time, aim error, and item-timer knowledge.
- Phase 6: promote action decisions into full command ownership, wire inventory and weapon switching, add real aim/firing behavior, attach damage telemetry to actual damage events, and complete pickup-delta validation.
- Phase 7: implement FFA/TDM/CTF/Duel tactical roles, objective behavior, match/vote/map-flow handling, and later coop follow/wait/door/resource behavior.
- Phase 9: add reference-map AAS coverage beyond `mm-rage`, CI platform builds, source-side CPU/visibility/trace counters, memory/AAS usage measurement, high-bot-count degradation policy, and final release packaging/license validation.

## Ledger Notes

- This summary is conservative by design: analyzer/getter support is not counted as final source-counter emission, focused routing is not counted as pickup completion, and reserved smoke modes are not counted as implemented scenarios.
- The round summary is based on the 2026-06-18 implementation documents and existing plan/roadmap state in the shared worktree.
- Completion count was measured directly from `docs-dev/plans/q3a-botlib-aas-port.md` after this round: 483/659 overall and 483/647 phase tasks.
