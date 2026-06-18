# Q3A BotLib Action Item Utility Slice - 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This slice advances Phase 6 item/inventory behavior in the existing WORR bot action boundary without taking over navigation, command, weapon, or server-smoke orchestration ownership.

`bot_items.*` now exposes a Q2/WORR item utility surface that classifies and scores existing `Item`/pickup entities as health, armor, ammo, weapon, powerup, or generic pickup candidates. The scorer remains intent-only: it does not scan the map, reserve goals, clear route slots, press buttons, or mutate inventory.

`bot_actions.*` now carries bot health/armor snapshot data into the item context and exposes narrow weapon-switch request/result counters for future smoke orchestration. Weapon switching is still not executed by the action dispatcher.

## Item Utility

The new item context builders are:

- `BotItems_BuildContextForEntity(const gentity_t *bot, const gentity_t *candidate, ...)`
- `BotItems_BuildContextForItem(const gentity_t *bot, const Item *candidateItem, ...)`

They use the existing game structures instead of introducing a separate bot item table:

- `Item::flags` classifies health, armor, ammo, weapon, powerup, timed, tech, and key pickups.
- `Item::quantity`, `Item::ammo`, `Item::tag`, and `Item::highValue` feed utility scoring.
- `gclient_t::pers.inventory` and `gclient_t::pers.ammoMax` estimate whether ammo, weapons, power armor, and generic inventory pickups are useful.
- Bot `health`, `maxHealth`, and armor inventory determine health/armor urgency.

Scoring keeps the same rough scale as the current route item-goal utility: health and armor are practical mid/high priorities, new weapons and powerups rank higher, owned weapons remain useful only when they can add ammo, and high-value pickups get a separate boost.

## Smoke-Oriented Counters

`BotItemStatus` now includes counters for later scenario/smoke consumers:

- `itemHealthGoalAssignments`
- `itemArmorGoalAssignments`
- `itemHealthPickups`
- `itemArmorPickups`
- `lastHealthBefore`, `lastHealthAfter`, `lastHealthPickupDelta`
- `lastArmorBefore`, `lastArmorAfter`, `lastArmorPickupDelta`

The corresponding explicit hooks are:

- `BotItems_RecordGoalAssignment(const BotItemDecision &decision)`
- `BotItems_RecordGoalAssignment(int item)`
- `BotItems_RecordPickup(int item, int before, int after)`
- `BotItems_RecordPickup(const Item *item, int before, int after)`

These hooks intentionally record observed events only. `BotItems_Evaluate()` records candidate and seek-decision counters, but it does not pretend that a route goal was assigned or that a pickup completed.

For the planned smoke cvar `sg_bot_frame_command_smoke_item_focus=health/armor`, `BotItems_FocusFromString()` and `BotItems_FocusName()` provide a small parser/display helper. The item module does not read cvars directly.

## Action Dispatcher Counters

`BotActionStatus` now includes:

- `weaponSwitchRequests`
- `weaponSwitchCompletions`
- `weaponSwitchFailures`
- `weaponSwitchExpectedItem`
- `weaponSwitchActualItem`
- `weaponSwitchExpectedMatch`

The new hooks are:

- `BotActions_RecordWeaponSwitchRequest(int expectedWeaponItem)`
- `BotActions_RecordWeaponSwitchCompletion(int expectedWeaponItem, int actualWeaponItem)`
- `BotActions_RecordWeaponSwitchFailure(int expectedWeaponItem, int actualWeaponItem)`

These are intended for future `sg_bot_frame_command_smoke_weapon_switch=1` orchestration or a validated command owner. They do not issue weapon commands.

## Boundaries

This slice did not edit `src/server/main.c`, `bot_brain.*`, `bot_nav.*`, `bot_runtime.*`, `bot_combat.*`, or scenario/perf tools.

Current gaps left for other owners:

- Print or otherwise surface the new status fields in the smoke output path.
- Wire health/armor focused item-goal assignment and pickup observation into the server smoke modes.
- Wire weapon-switch request/completion/failure hooks into the validated weapon command path.
- Keep nav reservation and route-goal ownership in `bot_nav.*`.

## Validation

Commands run:

```powershell
meson compile -C builddir-win sgame_x86_64
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_actions.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_items.cpp.obj
```

Results:

- `meson compile -C builddir-win sgame_x86_64` compiled `bot_actions.cpp`, `bot_items.cpp`, and adjacent bot units, then stopped on a concurrent `bot_combat.cpp` pointer/member access error outside this lane.
- The targeted object compile for `bot_actions.cpp` and `bot_items.cpp` passed.
- Ninja still reports the existing `premature end of file; recovering` warning.
