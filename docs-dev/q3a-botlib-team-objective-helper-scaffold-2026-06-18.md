# Q3A BotLib team objective helper scaffold

Date: 2026-06-18

Tasks: `FR-04-T01`, `DV-03-T05`

## Summary

This slice adds the first WORR-native team-objective helper boundary for the
reserved `sg_bot_frame_command_smoke_team_objective=1` lane. It is deliberately
telemetry and assignment scaffolding only: it does not promote the
`team_objective` scenario, does not call the bot brain, does not mutate
navigation state, and does not synthesize route/reach/flag success metrics.

## Implementation

- Added `src/game/sgame/bots/bot_objectives.hpp`.
- Added `src/game/sgame/bots/bot_objectives.cpp`.
- Added `src/game/sgame/bots/bot_objectives.cpp` to `sgame_src` in
  `meson.build`.

The helper exposes stable objective type values matching the pending scenario
counter plan:

| Value | Type |
| ---: | --- |
| `1` | Enemy flag pickup |
| `2` | Own flag return |
| `3` | Neutral flag pickup |
| `4` | Base defense |

It also exposes deterministic flag helper functions:

- `BotObjectives_EnemyFlagItemForTeam()`
- `BotObjectives_OwnFlagItemForTeam()`
- `BotObjectives_FlagOwnerTeamForItem()`
- `BotObjectives_FlagObjectiveTypeForTeam()`
- `BotObjectives_BuildFlagTarget()`
- `BotObjectives_BuildFlagTargetForEntity()`

`BotObjectives_Assign()` accepts caller-supplied target facts and returns a
`BotObjectiveAssignment` with client/team/target/entity/item/area/role/priority
data. It only assigns when the smoke lane is enabled, the caller supplied a live
bot context, the bot is on a primary team, the target exists, and the target has
a positive AAS area. It sets `wantsRoute=true` for future brain/nav integration,
but does not record a route request by itself.

## Telemetry contract

The status helper currently tracks:

- evaluation gating: `evaluations`, `disabledEvaluations`, `invalidContexts`,
  `deadContexts`, `missingTeams`, `missingObjectives`,
  `unreachableObjectives`
- deterministic assignments: `assignments`, `roleAttacker`, `roleDefender`,
  `enemyFlagAssignments`, `ownFlagReturnAssignments`,
  `neutralFlagAssignments`, `baseDefenseAssignments`
- future integration hooks: `routeRequests`, `routeCommands`, `reaches`,
  `flagPickups`, `flagCaptures`
- latest objective facts: `lastObjectiveType`, `lastObjectiveRole`,
  `lastClient`, `lastTeam`, `lastTargetTeam`, `lastEntity`, `lastItem`,
  `lastArea`, `lastPriority`

The future integration hooks are intentionally explicit record functions:

- `BotObjectives_RecordRouteRequest()`
- `BotObjectives_RecordRouteCommand()`
- `BotObjectives_RecordReach()`
- `BotObjectives_RecordFlagPickup()`
- `BotObjectives_RecordFlagCapture()`

Those counters remain zero until the brain/nav/capture owners call them from
real events. This keeps the reserved smoke lane honest and avoids fake pass
metrics.

## Validation

Command:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result: blocked by the shared build directory being in use by another Meson
process:

```text
ERROR: Some other Meson process is already using this build directory. Exiting.
```

Command:

```powershell
meson compile -C builddir-win-bootstrap-hosted sgame_x86_64
```

Result: the first run exceeded the command timeout while compiling the
regenerated server-game target, but produced
`builddir-win-bootstrap-hosted/sgame_x86_64.dll.p/src_game_sgame_bots_bot_objectives.cpp.obj`.

Command:

```powershell
meson compile -C builddir-win-bootstrap-hosted sgame_x86_64
```

Result: passed on rerun. Captured output:

```text
[1/2] Compiling C++ object sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj
[2/2] Linking target sgame_x86_64.dll
```

No scenario harness promotion or runtime smoke was run in this lane.
