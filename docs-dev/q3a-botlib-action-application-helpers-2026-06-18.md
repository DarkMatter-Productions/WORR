# Q3A BotLib Action Application Helpers - 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This slice hardens the WORR-native bot action application helper without taking ownership of `bot_brain.*`, `bot_nav.*`, scenario harnesses, or server command dispatch.

`bot_actions.*` now separates the legacy "did this mutate usercmd buttons" answer from a richer action-application result. The old `BotActions_ApplyDecision()` API is preserved and still returns `true` only when `BUTTON_ATTACK` or `BUTTON_USE` is written to the supplied `usercmd_t`. New callers can use `BotActions_ApplyDecisionDetailed()` to distinguish accepted non-button intents such as a pending weapon switch or inventory use from malformed decisions.

## Helper Contract

`BotActions_ApplyDecisionDetailed(const BotActionDecision &, usercmd_t *)` validates an action before touching a command:

- `Attack` may only set `pressAttack` and applies `BUTTON_ATTACK`.
- `UseWorld` may only set `pressUse` and applies `BUTTON_USE`.
- `SwitchWeapon` must carry `wantsWeaponSwitch` plus a positive `weaponItem`; it is accepted as pending intent but does not issue a weapon command.
- `UseInventory` must carry `wantsInventoryUse` plus a positive `item`; it is accepted as pending intent but does not issue an inventory command.
- `MoveToItem` is accepted only when it has no command or pending-use flags; navigation ownership remains outside this file.

Malformed or null-command applications increment rejection counters and store a `BotActionApplyFailure` reason. This gives the later brain integration a way to report bad action wiring without guessing from a false bool return.

## Weapon Switch Telemetry

The existing integer telemetry hooks remain available:

- `BotActions_RecordWeaponSwitchRequest(int expectedWeaponItem)`
- `BotActions_RecordWeaponSwitchCompletion(int expectedWeaponItem, int actualWeaponItem)`
- `BotActions_RecordWeaponSwitchFailure(int expectedWeaponItem, int actualWeaponItem)`

This slice adds decision-based overloads for future brain-side call sites:

- `BotActions_RecordWeaponSwitchRequest(const BotActionDecision &decision)`
- `BotActions_RecordWeaponSwitchCompletion(const BotActionDecision &decision, int actualWeaponItem)`
- `BotActions_RecordWeaponSwitchFailure(const BotActionDecision &decision, int actualWeaponItem)`

The overloads validate that the decision is a real `SwitchWeapon` request before recording. They do not dispatch commands and do not synthesize scenario pass metrics. Completion/failure should only be recorded by a caller that observes the weapon system state or knows that the submitted request failed.

## Status Additions

`BotActionStatus` now records:

- Action application attempts, accepted applications, rejected applications, and the last apply failure reason.
- Invalid weapon-switch telemetry events and expected/actual mismatches.

The previously surfaced request/completion/failure counters are unchanged and continue to be the primary scenario-facing fields.

## Validation

Commands run:

```powershell
meson compile -C builddir-win sgame_x86_64
```

Result:

- First broad build invocation timed out in the tool after 124 seconds while the shared build kept running in the background.
- A targeted retry initially reported that another Meson process was using `builddir-win`, so no build process was killed or overwritten.
- After the builddir lock cleared, `meson compile -C builddir-win sgame_x86_64` completed successfully, including `src_game_sgame_bots_bot_actions.cpp.obj`, `src_game_sgame_bots_bot_brain.cpp.obj`, and the final `sgame_x86_64.dll` link.
- Ninja still emitted the pre-existing `premature end of file; recovering` warning.
