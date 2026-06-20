# Q3A BotLib Special Item Utility

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Summary

This slice broadens the WORR-native item utility helper beyond generic
powerups. The action layer can now distinguish combat boosts, protection,
invisibility, mobility, utility powerups, techs, and CTF objective pickups while
remaining an intent-only scoring helper.

## Implementation

- `BotItemSpecialKind` classifies special pickup intent separately from the
  broad `BotItemUtilityKind`.
- `BotItems_BuildContextForItem()` now records whether a candidate is a tech or
  CTF objective, detects active timed/count powerups, and avoids treating
  already-active powerups or already-held techs as ordinary useful pickups.
- `BotItems_Evaluate()` applies deterministic priority boosts for:
  - damage boosts such as quad, double damage, and power amp tech
  - protection such as battlesuit, empathy shield, spawn protection, defender
    sphere, and disruptor shield tech
  - invisibility
  - mobility such as haste, antigrav, and time-accel tech
  - utility powerups such as silencer, rebreather, envirosuit, regeneration,
    spheres, teleporter, doppelganger, and IR goggles
  - CTF flag/objective pickups
- `BotItemStatus` now tracks candidate and seek-decision buckets for these
  special item families, plus the last selected special kind.

This does not change route ownership, item reservation, inventory execution, or
weapon/inventory command dispatch. Those remain owned by the brain/nav/action
integration layers.

## Validation

Command:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_items.cpp.obj
```

Result: passed. Ninja emitted the existing shared-build-dir warning
`premature end of file; recovering`.

## Remaining Work

The new status buckets still need final `q3a_bot_action_status` exposure after
the parallel brain/action lanes settle. Scenario promotion can then assert that
powerup and tech utility are visible in runtime logs without reaching into item
internals.
