# Q3A BotLib Live Combat Policy Round

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This round advances the owned combat slice for less-scripted live aiming and
firing policy without editing brain, server, scenario, item, objective, plan,
roadmap, or credit files.

The implementation stays in:

- `src/game/sgame/bots/bot_combat.hpp`
- `src/game/sgame/bots/bot_combat.cpp`

It builds on the existing aim-policy and projectile-lead helpers by exposing a
reusable profile contract and status-friendly result fields for reaction, aim
settle, turn limits, burst cadence, aim error/noise, and projectile lead.

## API Additions

`BotCombat_AimProfileForSkill(int skill)` now exposes the combat-owned skill
profile table as `BotCombatAimProfile`. Callers can consume one source of truth
for:

- effective skill after clamping;
- reaction delay;
- aim-settle requirement;
- aim error and tracking noise in tenths of degrees;
- yaw and pitch turn limits per frame;
- burst shot limit, burst commit window, and burst cooldown;
- projectile lead percentage.

`BotCombatAimPolicyResult` now carries the profile snapshot plus ready/remaining
metadata:

- `reactionReady`, `reactionRemainingMilliseconds`;
- `aimSettled`, `aimSettleRemainingMilliseconds`;
- `burstReady`, `burstShotsFired`, `burstShotsRemaining`,
  `burstCooldownRemainingMilliseconds`;
- `maxYawTurnDegreesPerFrame`, `maxPitchTurnDegreesPerFrame`,
  `yawTurnOverageDegrees`, `pitchTurnOverageDegrees`;
- `projectileLeadPercent`.

The older fields remain in place, including `maxTurnDegreesPerFrame`, so current
callers do not need to move immediately.

## Projectile Lead

Projectile lead is now profile-aware through `leadScalePercent`.

`BotCombatProjectileLeadFrame::leadScalePercent` defaults to `-1`. Direct calls
normalize that to full lead (`100`). `BotCombat_BuildLiveAimDecision(...)`
replaces `-1` with the active aim profile's `projectileLeadPercent`, so lower
skills intentionally under-lead rather than always snapping to a perfect
intercept. Current profile lead percentages are:

- skill 0: `45`
- skill 1: `55`
- skill 2: `70`
- skill 3: `85`
- skill 4: `95`
- skill 5: `100`

`BotCombatProjectileLeadResult` also reports raw lead milliseconds, max lead
milliseconds, whether the lead was clamped, the scale percentage used, and raw
versus scaled lead offset distances.

## Status Surface

`BotCombatStatus` now records the last observed values for the new policy and
lead metadata, including turn overage, reaction/settle/burst remaining values,
lead clamp state, raw/clamped lead timing, lead scale, live-aim priority, and
live-aim projectile lead percentage.

These fields are available through `BotCombat_GetStatus()` but are not printed
by the brain-owned status line in this round.

## Brain Owner Follow-Up

The next `bot_brain.cpp` integration pass should:

- replace the duplicate burst limit/cooldown helper table with
  `BotCombat_AimProfileForSkill(...)`;
- pass an already evaluated `BotCombatAimPolicyResult` into
  `BotCombatLiveAimFrame` when action selection has evaluated the policy for
  the same frame, avoiding duplicate counters;
- use `reactionRemainingMilliseconds`, `aimSettleRemainingMilliseconds`, and
  turn overage fields to drive smoother track/settle behavior;
- apply `aimErrorTenthsDegrees` and `trackingNoiseTenthsDegrees` to command
  angles in the brain-owned aim path;
- decide which new `BotCombatStatus` fields belong on compact status lines and
  scenario assertions;
- eventually source skill/profile values from bot profile data instead of the
  temporary combat-owned table.

Deployable and ballistic projectile handling remains intentionally conservative:
the straight-line lead solver only applies to weapons with explicit projectile
speed metadata.

## Validation

Commands run:

- `git diff --check -- src/game/sgame/bots/bot_combat.hpp src/game/sgame/bots/bot_combat.cpp`
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_combat.cpp.obj`

Result: both passed. Ninja still prints the existing `premature end of file;
recovering` warning while compiling the object.
