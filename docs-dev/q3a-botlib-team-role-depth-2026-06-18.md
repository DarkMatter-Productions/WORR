# Q3A BotLib team role depth policy

Date: 2026-06-18

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This slice deepens the WORR-native team-objective role policy in
`src/game/sgame/bots/bot_objectives.*`. It keeps the helper deterministic and
integration-ready: callers provide target facts, and the objective module
returns a role, lane, priority breakdown, and reason strings without mutating
bot brain, navigation, or gameplay behavior.

No `bot_brain.cpp` integration was added in this worker lane.

## Implementation

Changed files:

- `src/game/sgame/bots/bot_objectives.hpp`
- `src/game/sgame/bots/bot_objectives.cpp`
- `docs-dev/q3a-botlib-team-role-depth-2026-06-18.md`

New policy vocabulary:

- `BotObjectiveLane::Attack`
- `BotObjectiveLane::Defense`
- `BotObjectiveLane::Midfield`
- `BotObjectiveLane::CarrierSupport`
- `BotObjectiveLane::DroppedFlagResponse`
- `BotObjectiveLane::OwnBaseReturn`

The existing objective target vocabulary is unchanged. Lanes sit beside
`BotObjectiveRole` so future CTF/TDM brain work can distinguish "what job is
this bot doing" from "where in the team shape should it operate."

## Policy Rules

The lane-aware policy is still a pure priority table:

- Enemy and neutral flag pickups use the attack lane by default.
- Dropped enemy or neutral flags become high-priority dropped-flag responses
  owned by attackers, with support as a compatible requested-role fallback.
- Dropped own flags become returner-owned emergency responses.
- Enemy carriers holding the bot team's flag become high-priority own-base
  return targets.
- The bot team's own flag at its world/base entity defaults to a defender in
  the own-base-return lane, avoiding a fake "return" task when the flag is
  already home.
- Friendly flag-carrier targets use support in the carrier-support lane.
- Base-defense targets retain the defense lane, while enemy-anchor base-defense
  targets expose a midfield contest/support lane for TDM-style integration.

Requested roles remain honored when compatible. If a requested role has no
compatible candidate for the target facts, the existing fallback flow selects
the deterministic best candidate and records a fallback.

## Integration Surface

`BotObjectiveRolePolicy` and `BotObjectiveAssignment` now expose:

- selected `lane`
- selected `lanePriority`
- per-lane priority breakdowns:
  `attackLanePriority`, `defenseLanePriority`, `midfieldLanePriority`,
  `carrierSupportPriority`, `droppedFlagResponsePriority`,
  `ownBaseReturnPriority`
- selected `laneReason`

New pure helpers:

- `BotObjectives_DefaultLaneForTarget(target)`
- `BotObjectives_LanePriorityForTarget(lane, target)`
- `BotObjectives_LaneName(lane)`

These helpers allow the main brain owner to consume lane facts without needing
to duplicate scoring in `bot_brain.cpp`.

## Status

`BotObjectiveStatus` now records lane-policy counters:

- `rolePolicyLaneAttackSelections`
- `rolePolicyLaneDefenseSelections`
- `rolePolicyLaneMidfieldSelections`
- `rolePolicyCarrierSupportSelections`
- `rolePolicyDroppedFlagResponses`
- `rolePolicyOwnBaseReturnSelections`

It also records latest lane facts:

- `lastObjectiveLane`
- `lastLanePriority`
- latest per-lane priority breakdowns
- `lastReason`
- `lastLaneReason`

The current brain status marker does not print the new fields yet. They are
available through `BotObjectives_GetStatus()` for the main integration lane to
wire into `q3a_bot_objective_status` when that file is safe to edit.

## Validation

Command:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result: passed. Ninja emitted the shared build-log recovery warning
`premature end of file; recovering`, and `sgame_x86_64.dll` linked
successfully.

Command:

```powershell
python tools\refresh_install.py --build-dir builddir-win
```

Result: passed. The local `.install` staging root was refreshed and
`basew/pak0.pkz` was repackaged.

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed, `23` tests.

Command:

```powershell
python tools\bot_scenarios\run_bot_scenarios.py --scenario team_objective --timeout 60 --base-port 28000 --format text --json-out .tmp\bot_scenarios\team_objective_role_depth_report.json
```

Result: passed. The focused runtime smoke reported `1` passed scenario, `0`
failures, and `0` pending rows. The `q3a_bot_objective_status` marker reported
`team_objective_evaluations=246`, `team_objective_assignments=245`,
`team_objective_route_requests=4`, `team_objective_route_commands=4`,
`team_objective_reaches=4`, and `team_objective_flag_pickups=4`.
