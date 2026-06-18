# Q3A BotLib Weapon Switch Proof Helpers - 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This Worker B slice finishes the action-layer helper contract needed by reserved scenario `switch_weapons` mode 21. The work stays in `bot_actions.*` and does not dispatch weapon commands or mark synthetic completions.

The action layer now tracks validated weapon-switch requests as per-client pending proof state. A request is only a request; completion requires a later observed weapon item that matches the expected item.

## Helper Contract

`BotActions_ApplyDecisionDetailed()` now reports accepted non-command intents explicitly:

- `commandMutated` is only true when `BUTTON_ATTACK` or `BUTTON_USE` was written to `usercmd_t`.
- `pendingIntentAccepted` is true for accepted out-of-band intents.
- `weaponSwitchPending` plus `weaponSwitchItem` identifies an accepted switch intent.
- `inventoryUsePending` plus `inventoryUseItem` identifies an accepted inventory-use intent.
- A null `usercmd_t` only rejects decisions that need to mutate command buttons; switch intents can still validate as pending.

Weapon switch proof APIs:

- `BotActions_RecordWeaponSwitchRequestDetailed(decision, currentWeaponItem)` validates a real `SwitchWeapon` decision, stores the expected item for `decision.clientIndex`, and records the current weapon item as the previous/actual baseline.
- `BotActions_RecordWeaponSwitchObservation(clientIndex, actualWeaponItem)` is non-terminal. It completes the request only when `actualWeaponItem` equals the pending expected item; otherwise it leaves the request pending.
- `BotActions_RecordWeaponSwitchCompletionObserved(clientIndex, actualWeaponItem)` is terminal. A match records completion; a mismatch records failure and mismatch.
- `BotActions_RecordWeaponSwitchFailureObserved(clientIndex, actualWeaponItem)` is terminal failure proof for rejected or timed-out requests.
- The legacy direct expected/actual helpers remain available, but completion now means expected equals actual. Mismatched direct completions are counted as failures and mismatches, not completions.

## New Counters

`BotActionStatus` now includes request-validation and proof-state counters:

- `weaponSwitchValidatedRequests`
- `weaponSwitchRejectedRequests`
- `weaponSwitchDuplicateRequests`
- `weaponSwitchPendingRequests`
- `weaponSwitchNoPendingEvents`
- `weaponSwitchInvalidEvents`
- `weaponSwitchMismatches`
- `weaponSwitchPreviousItem`
- `weaponSwitchLastClientIndex`
- `lastWeaponSwitchEvent`

The existing scenario-facing counters are preserved:

- `weaponSwitchRequests`
- `weaponSwitchCompletions`
- `weaponSwitchFailures`
- `weaponSwitchExpectedItem`
- `weaponSwitchActualItem`
- `weaponSwitchExpectedMatch`

## Main-Thread Integration Needed

`bot_brain.cpp` should replace the current request-only call with proof-aware calls after it owns the actual weapon dispatch path:

1. Before dispatching the weapon switch, capture the bot's current weapon item.
2. After `BotActions_ApplyDecisionDetailed()` returns `weaponSwitchPending`, submit the real weapon switch request to the game weapon system.
3. If dispatch was accepted, call:

```cpp
BotActions_RecordWeaponSwitchRequestDetailed(actionDecision, currentWeaponItemBeforeDispatch);
```

4. On later frames, after observing the bot's actual current weapon item, call:

```cpp
BotActions_RecordWeaponSwitchObservation(actionDecision.clientIndex, actualCurrentWeaponItem);
```

5. If the switch request is rejected or times out before matching the expected item, call:

```cpp
BotActions_RecordWeaponSwitchFailureObserved(actionDecision.clientIndex, actualCurrentWeaponItem);
```

6. Surface the new validation/proof fields in the action status marker if mode 21 wants to gate on pending-request hygiene, duplicate requests, or mismatch failures.

The reserved scenario should only promote `switch_weapons` after `weapon_switch_requests > 0`, `weapon_switch_completions > 0`, `weapon_switch_expected_match = 1`, and `weapon_switch_failures = 0` are produced by observed weapon state.

## Validation

Commands run:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj
meson compile -C builddir-win sgame_x86_64
```

Results:

- The direct `bot_actions.cpp` object compile passed.
- The full `sgame_x86_64` target failed in `src/game/sgame/bots/bot_objectives.cpp`, outside this Worker B scope. The failure is a parallel-worker signature mismatch: `BotObjectives_RecordLastEvent()` now requires 11 arguments while two call sites still pass 6.
