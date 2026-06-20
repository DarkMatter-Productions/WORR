# Q3A BotLib Weapon and Inventory Dispatch Wiring

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

Worker A wired the validated bot action command request API into the
brain-owned frame command path for weapon and inventory intents.

The implementation keeps ownership conservative:

- `BotActions_BuildCommandRequest()` remains the single builder and validator
  for pending weapon/inventory action intents.
- `bot_brain.cpp` now consumes accepted pending action results, builds a
  concrete `use_index_only` command request, and records a dispatch outcome.
- Eligible exact weapon and inventory requests are submitted through the
  existing server-game item `use` callback path after the same inventory and
  item-shape checks used by the command layer.
- Weapon switches keep the existing proof flow by recording the pending switch
  request and then observing whether the submitted request reached the current
  or pending weapon slot.
- Unsupported future command shapes are deferred with an explicit reason
  instead of being silently ignored.

This does not restore or call the removed Q2R `Bot_UseItem` export callback.

## Implementation Details

Updated files:

- `src/game/sgame/bots/bot_actions.hpp`
- `src/game/sgame/bots/bot_actions.cpp`
- `src/game/sgame/bots/bot_brain.cpp`

`BotActionStatus` now tracks command request dispatch telemetry:

- `commandRequestDispatchAttempts`
- `commandRequestSubmitted`
- `commandRequestDeferred`
- `commandRequestDispatchFailures`
- `weaponCommandDispatches`
- `inventoryCommandDispatches`
- last dispatch kind, outcome, failure, client, and item

`q3a_bot_action_status` now emits the request build/accept/reject counters
plus dispatch/defer/failure counters and string names for the last request and
dispatch outcome. This gives scenario logs enough evidence to prove that a
pending weapon or inventory decision was built, validated, and either submitted
or deferred/failed with a specific reason.

The brain dispatcher currently accepts exact `use_index_only` requests for:

- `UseWeaponIndex`
- `UseInventoryIndex`

Before calling an item `use` callback it verifies:

- the request is valid and exact,
- the request client matches the bot entity,
- the target entity is still a live bot client,
- the item index resolves to the requested item,
- the item is still usable,
- the bot has the item in inventory.

For weapon requests, the dispatcher sets `noWeaponChains` to preserve
`use_index_only` exact-item behavior, calls the item use function, validates the
selected item, and records whether the requested weapon became current or
pending. Scenario smoke mode can still force `Change_Weapon()` for immediate
observation.

## Notes

The registered client command path for `use_index_only` is still driven by
engine-provided command arguments through `CommandArgs` and `gi.argv()`. The
brain path does not synthesize those engine argv slots. Instead, it uses the
same item use callback reached by that command after validating the request
boundary locally.

## Validation

Object build:

```text
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj
```

Result: passed. Both touched objects compiled. Ninja printed the existing
`premature end of file; recovering` warning.

Full server-game build:

```text
meson compile -C builddir-win sgame_x86_64
```

Result: passed. `sgame_x86_64.dll` linked successfully. Ninja again printed the
same non-fatal `premature end of file; recovering` warning.
