# Q3A BotLib Perception Blackboard - 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T15`

## Summary

This slice adds a WORR-native per-bot perception blackboard inside `bot_brain.*`.
The blackboard tracks current enemy memory, last seen enemy facts, best-effort
heard/damaged event facts, and status counters that scenario smoke orchestration
can consume without editing server smoke modes.

The implementation stays in the bot brain ownership lane. It does not edit
`src/server/main.c`, `bot_combat.*`, `bot_items.*`, `bot_actions.*`, tools, or
`q2proto/`.

## Runtime Behavior

- `BotBrain_BeginFrame()` updates a per-client blackboard for active bots when
  the BotLib runtime is enabled and AAS is loaded.
- Visible enemy acquisition scans use active playing clients, reject self,
  dead/eliminated players, `FL_NOTARGET`, and same-team candidates in team modes.
- Current enemy visibility uses the existing game `visible()` helper.
- Shootability uses the existing game `CanDamage()` helper.
- Enemy memory persists briefly after sight is lost and then clears through a
  bounded memory window.
- Expensive visible-enemy scans are staggered by default. The server smoke cvar
  `sg_bot_frame_command_smoke_combat=1` forces full-rate scans for deterministic
  combat smoke.
- `sg_bot_frame_command_smoke_team_objective=1` is consumed and reported in
  blackboard status so team-objective smoke can confirm the brain saw the mode.
- Action telemetry is enriched from the blackboard before `BotActions_Decide()`
  runs, but no action decision is applied to `usercmd_t`.

## Public Query Surface

`bot_brain.hpp` now exposes `BotBrainBlackboardSnapshot` and
`BotBrain_GetBlackboardSnapshot(int clientIndex, BotBrainBlackboardSnapshot *)`.
The snapshot includes current enemy entity/client/spawn count, visible/shootable
facts, distance, last seen origin/time/frame, heard source fields, and damage
event/source fields.

## Status Output

`BotBrain_PrintFrameCommandStatus()` now includes compact blackboard fields on
the existing `q3a_bot_frame_command_status` line:

- `blackboard_updates`
- `blackboard_current_enemies`
- `combat_enemy_acquisitions`
- `combat_enemy_visible`
- `combat_enemy_shootable`
- `last_combat_enemy_client`

It also emits a dedicated `q3a_bot_blackboard_status` line with scan, candidate,
visibility, shootability, current-enemy, last-seen, heard, damaged, action
context enrichment, and smoke-cvar counters.

The existing `q3a_bot_action_status` line now also surfaces the available
action/item/combat counters needed by smoke orchestration:

- `weapon_switch_requests`, `weapon_switch_completions`,
  `weapon_switch_failures`, `weapon_switch_expected_item`,
  `weapon_switch_actual_item`, `weapon_switch_expected_match`
- `item_health_goal_assignments`, `item_armor_goal_assignments`,
  `item_health_pickups`, `item_armor_pickups`, `last_health_pickup_delta`,
  `last_armor_pickup_delta`, plus before/after health and armor fields
- `combat_enemy_acquisitions`, `combat_enemy_visible`,
  `combat_enemy_shootable`, `combat_damage_events`,
  `last_combat_enemy_client`, `last_combat_damage`

## Validation

Commands run:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj
git diff --check -- src/game/sgame/bots/bot_brain.cpp src/game/sgame/bots/bot_brain.hpp
```

Results:

- The targeted `bot_brain.cpp` object compile passed.
- `git diff --check` passed for the touched brain files, with only existing
  line-ending normalization warnings.
- A broader `ninja -C builddir-win sgame_x86_64.dll` attempt was not used as
  the final validation gate because the shared worktree had adjacent compile
  activity and an unrelated `bot_combat.cpp` pointer/member failure was under
  active ownership by Worker 2.

## Remaining Gaps

- Damage source inference remains best-effort because the available player
  damage feedback data is reset during end-frame view feedback. The blackboard
  reliably records damage event timing, but a future damage hook should preserve
  attacker/source details earlier if scenarios need exact damaged-by identity.
- The blackboard enriches action telemetry only. It does not make bots fire,
  switch weapons, chase enemies, or override route movement.
- FOV, reaction-time fairness, aim error, item timer fuzzing, and team-objective
  tactical choices remain future Phase 4/6/7 work.
