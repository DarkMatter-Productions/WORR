# Q3A BotLib Team Role Policy Helper

Date: 2026-06-18

Tasks: `FR-04-T04`, `FR-04-T15`, `DV-03-T05`

## Summary

This slice adds a deterministic team-objective role policy layer inside the
WORR-native bot objective helper. It does not touch the bot brain, gameplay
branches, navigation state, or smoke pass criteria. The new policy surface lets
future brain integration ask the objective module which role should own a
reachable target and why before turning that decision into route or combat
behavior.

## Implementation

Changed files:

- `src/game/sgame/bots/bot_objectives.hpp`
- `src/game/sgame/bots/bot_objectives.cpp`

New role vocabulary:

- `BotObjectiveRole::Attacker`
- `BotObjectiveRole::Defender`
- `BotObjectiveRole::Returner`
- `BotObjectiveRole::Support`

The enum values are pinned with `static_assert`s so status output remains
stable for follow-up telemetry work.

New helper/status API:

- `BotObjectiveRolePolicy`
- `BotObjectives_EvaluateRolePolicy(context)`
- `BotObjectives_DefaultRoleForType(type)`
- `BotObjectives_DefaultRoleForTarget(target)`
- `BotObjectives_PriorityForType(type)`
- `BotObjectives_RolePriorityForTarget(role, target)`

`BotObjectives_Assign()` now evaluates a role policy after the existing
smoke/context/team/target/reachability gates pass. The returned assignment keeps
the old `priority` field, but also carries the policy breakdown:
`rolePriority`, `attackPriority`, `defendPriority`, `returnPriority`, and
`supportPriority`.

## Deterministic Policy

The policy scores role lanes from existing objective vocabulary:

- Enemy and neutral flag targets default to attack.
- Own-flag-return targets default to returner.
- Base-defense targets default to defender.
- Enemy or neutral flag targets whose route target is a flag carrier can default
  to support, giving future brain work an escort/protect hook without changing
  current smoke wiring.

If a caller requests a compatible role, that role is honored even when another
role has a higher automatic score. If the requested role is incompatible, the
helper falls back to the deterministic best role and records a fallback counter.

## Counters

`BotObjectiveStatus` now tracks:

- assignment roles: `roleReturner`, `roleSupport`
- policy activity: `rolePolicyEvaluations`, `rolePolicySelections`,
  `rolePolicyRequested`, `rolePolicyRequestedHonored`,
  `rolePolicyFallbacks`, `rolePolicyNoSelection`
- policy selection buckets: `rolePolicyAttackSelections`,
  `rolePolicyDefendSelections`, `rolePolicyReturnSelections`,
  `rolePolicySupportSelections`
- latest policy priorities: `lastRolePriority`, `lastAttackPriority`,
  `lastDefendPriority`, `lastReturnPriority`, `lastSupportPriority`

Main-thread integration now emits these counters on `q3a_bot_objective_status`
alongside the existing objective smoke metrics. The marker includes returner and
support role counts, role-policy evaluation/selection/fallback counters, and the
latest role-priority breakdown.

## Validation

Command:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result: passed. Ninja emitted the shared-build warning
`premature end of file; recovering`, but `sgame_x86_64.dll` linked
successfully.

Command:

```powershell
python tools\bot_scenarios\test_run_bot_scenarios.py
```

Result: passed, `23` tests.

## Integration Risks

- No bot brain consumer is wired in this slice, so the new return/support
  policy data is visible in runtime status logs but not yet used for autonomous
  behavior.
- Existing mode `23` still requests `BotObjectiveRole::Attacker`, so current
  team-objective smoke behavior should remain stable until the brain owner
  chooses to consume automatic role selection.
- Support-role scoring assumes a flag-carrier target represents an escort or
  protect opportunity. Future integration should confirm carrier team identity
  before using support behavior for combat decisions.
