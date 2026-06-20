# Q3A BotLib Aim Fairness Policy

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds a conservative aim/fairness helper layer to
`src/game/sgame/bots/bot_combat.*`. The new API gives later brain/perception
owners a deterministic gate for bot aiming and firing without making visibility
alone equivalent to perfect aim.

The helper remains inside the combat ownership boundary. It does not edit
`bot_brain.*`, does not submit commands, and does not implement full autonomous
firing. Existing combat decisions keep their previous behavior unless a caller
opts into the policy with `BotCombatContext::aimPolicyEnabled`.

## API Contract

`BotCombat_EvaluateAimPolicy(const BotCombatContext &, const BotCombatAimPolicyFrame &)`
returns `BotCombatAimPolicyResult`.

The input combines existing combat facts:

- `hasEnemy`
- `enemyVisible`
- `enemyShootable`
- `currentWeaponReady`
- `skillAllowsFire`
- `enemyDistanceSquared`

with caller-owned fairness facts:

- skill level, clamped to skill profiles `0` through `5`;
- target visible/tracked/aim-settled milliseconds;
- reaction-delay override or skill-derived reaction delay;
- FOV and absolute yaw/pitch deltas;
- burst shots already fired;
- burst cooldown remaining.

The result reports:

- `mayAim` and `mayFire`;
- a stable `BotCombatAimPolicyFailure`;
- effective skill, reaction delay, aim-settle requirement, FOV, yaw/pitch deltas,
  and turn limit;
- aim error and tracking noise in tenths of degrees;
- burst shot limit, burst commit window, and recommended cooldown.

## Fairness Rules

The policy is conservative when called directly:

- no enemy, no visibility, or outside-FOV targets cannot be aimed or fired at;
- visible but not shootable targets may be aimed at but not fired at;
- weapon readiness and `skillAllowsFire` remain explicit gates;
- target-visible time must satisfy the skill-derived or caller-provided
  reaction delay;
- yaw/pitch deltas must fit the skill profile's turn gate before firing, which
  blocks instant 180-degree perfect shots even if a caller marks the target
  visible;
- aim-settled time must satisfy the skill profile before firing;
- burst cooldown and burst-shot limits can withhold fire while still allowing
  aim.

Skill profiles intentionally bias lower skills toward slower reactions, larger
error/noise, tighter turn limits, shorter bursts, and longer cooldowns. Higher
skills improve those values without granting omniscient perception.

## Status Counters

`BotCombatStatus` now tracks policy-level counters:

- evaluations, aim-allowed, and fire-allowed counts;
- block counts for no enemy, visibility, FOV, shootability, weapon readiness,
  skill, burst cooldown, reaction, turn, aim-settle, and burst-limit failures;
- last failure, skill, reaction, settle, FOV, yaw/pitch, turn, error/noise, burst
  limit, and burst cooldown metadata.

`BotCombat_AimPolicyFailureName(...)` exposes stable snake_case names for future
status-line owners. This pass does not edit `q3a_bot_action_status`; brain/status
integration can add fields from `BotCombat_GetStatus()` without changing the
helper API.

## Integration Notes

`BotCombat_Evaluate()` consults the policy only when
`BotCombatContext::aimPolicyEnabled` is true. When enabled, a blocked policy
increments `withheldFire`, leaves attack application to the existing action
dispatcher, and stores the failure-name string in `lastSelectionReason`.

Future `bot_brain.*` or blackboard integration should populate:

- target-visible and target-tracked age from perception memory;
- yaw/pitch deltas from current view angles to target aim point;
- FOV from the active perception/profile model;
- aim-settled time from command/aim ownership state;
- burst shots and cooldown from a per-bot combat cadence state.

That integration should also decide which aim error/noise model applies to
actual command angles. This helper only returns metadata; it does not mutate view
angles.

## Validation

Command run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_combat.cpp.obj`

Result: passed with exit code `0`. Ninja printed `premature end of file;
recovering`, but the touched object compiled successfully.

No lightweight local C++ unit-test pattern exists for these bot helper structs,
so validation is compile-only for this slice.

## Remaining Work

- Wire policy inputs from the perception blackboard and command/aim state.
- Emit selected policy counters on the action or combat status line once the
  status owner chooses a compact field set.
- Apply returned aim error/noise to command angles in the brain-owned aiming
  path.
- Use bot profile fields for skill, reaction, and aim error once profile-to-brain
  ownership is settled.
