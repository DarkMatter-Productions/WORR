# Q3A BotLib Health/Armor Pickup Proof - 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`

## Summary

This Worker C slice finishes the item-side proof helpers needed by reserved
scenario `health_armor_pickup` / smoke mode `22`. It does not edit
`bot_brain.cpp` or `src/server/main.c`.

The existing status fields remain the promotion contract:

- `item_low_health_boosts`
- `item_low_armor_boosts`
- `item_health_goal_assignments`
- `item_armor_goal_assignments`
- `item_health_pickups`
- `item_armor_pickups`
- `last_health_pickup_delta`
- `last_armor_pickup_delta`

These values are still driven by real item scoring, route-goal assignment, and
resource deltas. No helper writes pass counters directly.

## Implementation Notes

- `BotItems_ApplyHealthArmorProofSetup()` puts a live bot into a deterministic
  proof state: low health, synced persistent health, no armor inventory, and no
  health bonus. This gives mode `22` a repeatable reason to score both health
  and armor candidates.
- `BotItems_CapturePickupSnapshot()` captures the bot's pre-pickup health or
  armor value for a real health/armor item.
- `BotItems_RecordPickupObservation()` compares that snapshot to the live
  post-pickup bot state and records pickup counters only when the matching
  resource increased.
- `BotItems_CurrentArmor()` and `BotItems_ClassifyUtility()` expose the shared
  armor/classifier logic to later integration owners.
- The low-health priority boost was raised so a critically hurt bot in
  `health_armor` focus selects health before armor. Once health is recovered,
  the existing low-armor scoring can naturally take over.

## Main-Thread Integration Calls Needed

Mode `22` setup owner:

```cpp
BotItemHealthArmorProofSetup setup{};
BotItems_ApplyHealthArmorProofSetup(bot, &setup);
```

Pickup owner around the real item pickup call:

```cpp
const BotItemPickupSnapshot snapshot =
	BotItems_CapturePickupSnapshot(other, ent->item, ent->s.number);
const bool pickedUp = ent->item->pickup(ent, other);
if (pickedUp) {
	BotItems_RecordPickupObservation(snapshot, other);
}
```

The existing nav-side `BotItems_RecordPickup()` path can remain as a secondary
near-goal observation path, but the touch/pickup wrapper above is the most direct
proof that an actual item pickup changed the bot's resource state.

## Validation

Commands run:

```powershell
meson compile -C builddir-win sgame_x86_64
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_items.cpp.obj
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_objectives.cpp.obj
```

Results:

- `bot_items.cpp` compiled successfully via the direct Ninja object target.
- The full `sgame_x86_64` compile reached link after compiling the touched
  object, then failed because the unrelated `bot_objectives.cpp` object was not
  produced.
- Rebuilding `bot_objectives.cpp` directly showed the real blocker:
  `BotObjectives_RecordLastEvent()` currently expects eleven arguments while two
  call sites pass six. That file is outside this Worker C write scope.
