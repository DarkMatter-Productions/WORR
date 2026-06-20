# Q3A BotLib Blackboard State Enrichment - 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T14`, `FR-04-T15`, `DV-07-T06`

## Summary

This slice extends the WORR bot brain blackboard from perception-only facts to
the Phase 4 state surface needed by later behavior work: current goal, route
state, stuck timer/reason, item reservation, and team role facts.

The implementation is telemetry/state plumbing only. It does not change route
selection, item scoring, stuck recovery, action dispatch, aim, firing, or team
policy behavior.

## Implementation

- `BotBrainBlackboardSnapshot` now carries numeric current-goal, route,
  stuck, item-reservation, and team-role fields alongside the existing enemy
  memory fields.
- `bot_nav.*` exposes `BotNav_GetBlackboardSnapshot()` as a read-only
  per-client route-slot snapshot. The getter copies existing route-slot facts
  and records the per-slot stuck-distance/progress telemetry already maintained
  for global route status.
- `BotBrain_BuildFrameCommand()` records objective role facts after the current
  team-objective helper produces an assignment, then records nav facts after
  route steering and recovery-command lookup.
- Worker A's weapon/inventory dispatch path remains intact. Blackboard
  recording is layered around `Bot_CommandDispatchPendingActionRequest()` and
  does not rewrite the new `q3a_bot_action_status` command-dispatch fields.

Goal type status values are intentionally compact:

| Value | Meaning |
|---|---|
| `0` | no current goal |
| `1` | item goal |
| `2` | explicit position goal |
| `3` | travel-type goal |
| `4` | cached route goal |

## Status Output

`q3a_bot_blackboard_status` now includes aggregate counts:

- `blackboard_state_enrichments`
- `blackboard_current_goals`
- `blackboard_route_states`
- `blackboard_stuck_timers`
- `blackboard_item_reservations`
- `blackboard_team_roles`

It also includes compact latest-fact fields:

- `last_goal_type`, `last_goal_area`, `last_goal_entity`, `last_goal_item`
- `last_route_valid`, `last_route_start_area`, `last_route_goal_area`,
  `last_route_end_area`, `last_route_points`, `last_route_travel_time`,
  `last_route_stop_event`
- `last_stuck_reason`, `last_stuck_frames`,
  `last_stuck_recovery_frames`
- `item_reservation_active`, `item_reservation_entity`,
  `item_reservation_owner`, `item_reservation_count`
- `last_team_role`, `last_team_role_objective`, `last_team_role_team`,
  `last_team_role_target_team`

The primary `q3a_bot_frame_command_status` line was not expanded for this slice
because it is already large; blackboard-specific facts stay on the dedicated
blackboard marker.

## Coordination Notes

This work landed during a multi-worker round. Parallel edits were present in
`bot_actions.*`, `bot_brain.cpp`, `bot_nav.*`, `bot_combat.*`, `bot_items.*`,
`bot_objectives.*`, tools, and docs. This slice did not revert or rewrite those
changes. The final brain integration was re-read against Worker A's completed
weapon/inventory dispatch code before validation.

## Validation

Commands run:

```powershell
git diff --check
meson compile -C builddir-win sgame_x86_64
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Results:

- `git diff --check` passed, with only line-ending normalization warnings from
  the shared Windows worktree.
- `sgame_x86_64` built and linked successfully. Ninja emitted the existing
  shared-build warning `premature end of file; recovering`.
- Bot scenario parser tests passed, `23` tests.

## Remaining Gaps

- These fields expose state for later consumers but do not make bots use the
  blackboard for autonomous team-role selection, item timing, aim fairness, or
  dispatch policy.
- The route snapshot is read-only and per-client. Global reservation skip facts
  still come from existing `BotNavRouteStatus` counters.
