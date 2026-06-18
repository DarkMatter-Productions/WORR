# Q3A BotLib team objective proof helpers

Date: 2026-06-18

Tasks: `FR-04-T04`, `DV-03-T05`

## Summary

This slice advances the reserved `team_objective` mode `23` proof boundary
without wiring the bot brain or server smoke harness. The team-objective module
now exposes real helper APIs for selecting an enemy-flag objective target from
live game state, assigning that objective, building a route-goal handoff, and
recording route request, route command, reach, flag pickup, and flag capture
events.

No pass counters are synthesized. Assignment and target-selection counters come
from helper evaluation. Route, reach, pickup, and capture counters still require
their owning systems to call the record hooks from real events.

## Implementation

Changed files:

- `src/game/sgame/bots/bot_objectives.hpp`
- `src/game/sgame/bots/bot_objectives.cpp`

New target facts:

- `BotObjectiveTargetSource` records whether a target came from an in-world
  flag entity, dropped flag entity, flag carrier, or smoke-friendly enemy-team
  anchor.
- `BotObjectiveTarget` and `BotObjectiveAssignment` now carry target source,
  spawn count, carrier client, and target origin.
- `BotObjectiveRouteGoal` gives the nav owner a compact, validated handoff
  object for position-goal routing.

New selection and assignment APIs:

- `BotObjectives_SelectEnemyFlagTarget(bot, allowEnemyTeamAnchor)`
- `BotObjectives_AssignEnemyFlagObjective(bot, smokeEnabled, requestedRole,
  allowEnemyTeamAnchor)`
- `BotObjectives_BuildRouteGoal(assignment, goal)`

Target selection is deterministic:

1. Prefer routeable live flag entities matching the bot team's enemy flag item.
2. Prefer dropped flags over base/world flags when both are routeable.
3. Consider live flag carriers when the flag item is already carried.
4. If explicitly allowed, synthesize an enemy-flag objective target from a live
   enemy player entity as a smoke-friendly anchor. This uses real entity state
   and AAS area resolution; it does not count as a pickup, reach, or pass by
   itself.

New event-friendly record overloads:

- `BotObjectives_RecordRouteRequest(goal, assignment)`
- `BotObjectives_RecordRouteCommand(goal, assignment)`
- `BotObjectives_RecordReach(goal, assignment)`
- `BotObjectives_RecordFlagPickup(player, flag)`
- `BotObjectives_RecordFlagCapture(player, item)`

The route overloads reject mismatched goals and increment `invalidEventHooks`.
The pickup/capture overloads derive client, team, item, entity, source, and
origin from live entities.

## Counters

The status struct now tracks target-selection proof fields:

- `targetSelections`
- `targetSelectionFailures`
- `targetCandidates`
- `targetAreaResolutions`
- `targetAreaFailures`
- `worldFlagTargets`
- `droppedFlagTargets`
- `carrierTargets`
- `enemyTeamAnchorTargets`

It also tracks event detail counters:

- `enemyFlagPickups`
- `ownFlagReturns`
- `neutralFlagPickups`
- `enemyFlagCaptures`
- `neutralFlagCaptures`
- `invalidEventHooks`

Latest-objective facts now include source, spawn count, carrier client, and
origin coordinates in addition to the existing type, role, client, team, item,
entity, area, and priority fields.

## Main-thread integration calls needed

The main brain/smoke owner can evaluate mode `23` with:

```cpp
BotObjectiveAssignment objective = BotObjectives_AssignEnemyFlagObjective(
	bot,
	smokeTeamObjectiveEnabled,
	BotObjectiveRole::Attacker,
	true);
BotObjectiveRouteGoal goal{};
if (BotObjectives_BuildRouteGoal(objective, &goal)) {
	BotObjectives_RecordRouteRequest(goal, objective);
	// Submit goal.origin / goal.area to BotNav.
}
```

The nav owner should call `BotObjectives_RecordRouteCommand(goal, assignment)`
only after a route command is actually produced, and
`BotObjectives_RecordReach(goal, assignment)` only after the bot reaches the
objective radius.

The CTF/capture owner should call:

```cpp
BotObjectives_RecordFlagPickup(player, flagEntity);
BotObjectives_RecordFlagCapture(player, capturedFlagItem);
```

from the real pickup and capture branches. Those calls are the remaining bridge
needed before the pending scenario can prove `team_objective_flag_pickups` and
capture-related counters honestly.

## Validation

Command:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result: passed. The shared build reported a Ninja recovery warning while
re-reading its build log, but `sgame_x86_64.dll` linked successfully.
