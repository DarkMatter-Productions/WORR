# Q3A BotLib Live Aim Policy Integration

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds a deterministic combat helper layer for brain-owned live aim and
firing decisions without editing `bot_brain.cpp`.

The new `bot_combat.*` API combines the existing aim fairness policy with
straight-line projectile lead data. It gives the brain a single helper result
for:

- whether the bot may aim;
- whether the bot may fire and press attack;
- which aim point should be used this frame;
- whether projectile lead was used;
- which aim-policy failure blocked firing.

The helper is intentionally pure. It does not mutate `usercmd_t`, view angles,
blackboard slots, weapon state, or burst state.

## API Additions

`BotCombatVector3` is a small POD vector type in `bot_combat.hpp`. It keeps the
public combat header independent from `g_local.hpp` and `q_vec3.hpp` include
ordering.

`BotCombat_BuildProjectileLead(const BotCombatProjectileLeadFrame &)` returns a
`BotCombatProjectileLeadResult` with:

- direct target aim point fallback;
- projectile speed used;
- direct travel milliseconds;
- clamped lead milliseconds;
- target speed, aim distance, and lead offset counters;
- `usedLead` only when target velocity is known and the lead point differs from
  the target point.

`BotCombat_BuildLiveAimDecision(const BotCombatContext &, const BotCombatLiveAimFrame &)`
returns a `BotCombatLiveAimDecision` with:

- `mayAim`, `mayFire`, and `pressAttack`;
- the final aim point for the brain to turn toward;
- the embedded aim-policy and projectile-lead results;
- fire priority/reason matching the existing combat fire decision helper;
- optional `hasAimPolicyResult` support so a caller can pass a pre-evaluated
  policy result and avoid double-counting policy evaluations.

## Projectile Lead

Straight-line lead speed hints were added to `BotWeaponMetadata` for weapons
whose runtime fire paths launch direct projectiles:

- Blaster: `1500`
- ETF rifle: `1150`
- Rocket launcher: `800`
- HyperBlaster: `1000`
- Plasma gun: `2000`
- Phalanx: `725`
- BFG10K: `400`, with a `1200 ms` max lead hint
- Disruptor: `1000`

Grenades, traps, teslas, prox mines, and ion ripper remain projectile/deployable
metadata entries, but they do not get straight-line lead speeds in this pass.
Their throw arcs, arming behavior, or randomized projectile spread need a
different solver.

The solver uses the standard intercept quadratic, falls back to direct
distance/speed flight time when no positive intercept is available, and clamps
lead time to a conservative default of `1000 ms` with a hard cap of `1500 ms`.

## Status Counters

`BotCombatStatus` now records projectile/live-aim helper telemetry:

- projectile lead evaluations, uses, non-projectile skips, missing-speed skips,
  and zero-distance skips;
- last projectile weapon, speed, lead milliseconds, target speed, aim distance,
  and lead offset;
- live aim evaluations, aim/fire allowed counts, policy blocks, lead uses, last
  weapon, and last reason.

These counters are available through `BotCombat_GetStatus()` for a future
status-line or scenario smoke owner.

## Brain Integration Follow-Up

`bot_brain.cpp` already has most required inputs:

- `Bot_PerceptionEnrichActionContext(...)` at
  `src/game/sgame/bots/bot_brain.cpp:1405` enables the aim policy and fills
  `context->combat.aimPolicy` with `Bot_CommandBuildAimPolicyFrame(...)`.
- `Bot_CommandSampleActionDecision(...)` at
  `src/game/sgame/bots/bot_brain.cpp:2432` owns the combat action context.
- `Bot_CommandAnglesForDecision(...)` at
  `src/game/sgame/bots/bot_brain.cpp:2444` is the exact place currently aiming
  at raw enemy origins during attack decisions.
- `cmd->angles = Bot_CommandAnglesForDecision(...)` at
  `src/game/sgame/bots/bot_brain.cpp:2780` is the frame command handoff.

Recommended integration:

1. Build a `BotCombatLiveAimFrame` from the same enemy used by the perception
   action context.
2. Fill `projectileLead.weaponItem` from `actionContext.combat.currentWeaponItem`.
3. Fill `shooterOrigin`, `targetOrigin`, and `targetVelocity` from the bot and
   enemy entity state.
4. Pass the existing aim policy frame through `liveFrame.aimPolicy`.
5. Thread the returned `BotCombatLiveAimDecision` into
   `Bot_CommandAnglesForDecision(...)`.
6. Aim at `liveAimDecision.aimPoint` when `mayAim` or `pressAttack` is true,
   converting `BotCombatVector3` back to `Vector3`.

If the action path has already evaluated the policy for the same frame, pass it
through `liveFrame.aimPolicyResult` with `hasAimPolicyResult = true` to avoid
duplicating policy counters. If the main thread chooses to make the live helper
the sole owner of policy evaluation, then thread `liveAimDecision.pressAttack`
into the action decision path instead.

## Validation

Commands run:

- `git diff --check -- src/game/sgame/bots/bot_combat.hpp src/game/sgame/bots/bot_combat.cpp`
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_combat.cpp.obj`
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj`
- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`

Results: all passed. Ninja continued to print the existing `premature end of
file; recovering` warning.

I also tried `ninja -C builddir-win sgame_x86_64.dll`. That target is currently
blocked by a concurrent/unrelated `bot_nav.cpp` compile error:

- `src/game/sgame/bots/bot_nav.cpp:411`: call to
  `BotNavGroundTraceSupportsPoint(...)` supplies 4 arguments, but the current
  function definition requires 5, including `recordCornerCutStatus`.

No lightweight standalone bot combat C++ unit harness exists in this tree, so
validation for this slice is object-build focused.
