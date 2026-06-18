# Q3A BotLib Weapon and Inventory Command API - 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This Worker E slice narrows the remaining weapon/inventory dispatch gap from "pending action intent" to a safe, inspectable command-request API in `bot_actions.*`.

The action layer can now translate a validated pending weapon-switch or inventory-use decision into a concrete client-command request without executing it or editing `bot_brain.cpp`. This keeps command ownership conservative while giving the future main-thread integration a testable object to submit through the existing client command path.

## API Contract

New public helpers:

- `BotActions_BuildCommandRequest(const BotActionDecision &decision)`
- `BotActions_ValidateCommandRequest(const BotActionDecision &decision)`
- `BotActions_CommandRequestKindName(BotActionCommandRequestKind kind)`
- `BotActions_CommandRequestFailureName(BotActionCommandRequestFailure failure)`

`BotActionCommandRequest` is valid only for accepted out-of-band action intents:

- `SwitchWeapon` becomes `UseWeaponIndex` with command `use_index_only` and `argumentItem = weaponItem`.
- `UseInventory` becomes `UseInventoryIndex` with command `use_index_only` and `argumentItem = item`.

Both request kinds set `exactItem = true`. The helper intentionally chooses `use_index_only` instead of a name-based command so later callers can avoid string lookup ambiguity and weapon-chain cycling when they submit the request.

## Safety Checks

The builder validates:

- non-`None` intent and positive priority through the existing action validation path;
- pending-command intent only, rejecting attack, world-use button, and movement decisions;
- valid bot client index;
- positive in-range item IDs;
- `GetItemByIndex()` returning the expected item;
- usable item callback presence;
- weapon requests target known weapon-command items;
- inventory requests do not accidentally target weapon-command items.

The helper does not validate live inventory count, UI blocking, intermission state, or selected-item state. Those remain authoritative in the later dispatcher/game command execution path.

## Status Additions

`BotActionStatus` now tracks command-request translation attempts:

- `commandRequestBuilds`
- `commandRequestAccepted`
- `commandRequestRejected`
- `weaponCommandRequests`
- `inventoryCommandRequests`
- failure buckets for invalid clients, invalid/unknown/unusable items, weapon rejects, and inventory rejects
- `lastCommandRequestKind`
- `lastCommandRequestFailure`
- `lastCommandRequestClientIndex`
- `lastCommandRequestItem`

These counters are process-local like the existing action status fields and reset through `BotActions_ResetStatus()`.

## Integration Notes

Future `bot_brain.cpp` or main-thread dispatch wiring can use this flow:

1. Build and apply the action decision as today.
2. When `BotActionApplyResult` reports `weaponSwitchPending` or `inventoryUsePending`, call `BotActions_BuildCommandRequest(decision)`.
3. If the request is valid, submit `request.command` with `request.argumentItem` through the authoritative command path.
4. Keep using the existing weapon-switch proof helpers to record accepted requests and observed completion/failure.

This slice does not claim full weapon/inventory dispatch integration, does not submit commands, and does not update scenario promotion gates.

## Validation

Commands run:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj
meson compile -C builddir-win sgame_x86_64
```

Results:

- Focused `bot_actions.cpp` object compile passed.
- The initial full `sgame_x86_64` build compiled the touched bot action code and exposed an unrelated team-role helper declaration issue in `bot_objectives.cpp`; the later team-role integration fixed that issue, and the final round build passed.
- Ninja still emits the pre-existing `premature end of file; recovering` warning.

## Residual Risks

- The new request object is not wired into `bot_brain.cpp` in this slice by design.
- Inventory-use requests are intentionally conservative and reject known weapon-command items; future policy may need a separate tactical throwable/weapon-use category.
- Live inventory count and game-state rejections are still deferred to the real command execution path.
