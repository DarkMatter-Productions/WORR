# Q3A Botlib Gameplay Item Hooks - 2026-06-18

Task refs: `DV-03-T05`, `FR-04-T02`, `FR-04-T04`

## Summary

Worker J wired the real item-touch success path in `src/game/sgame/gameplay/g_items.cpp` into the bot item proof counters. `Touch_Item()` now captures a `BotItemPickupSnapshot` before calling the item's `pickup` function and records the observation only when that pickup returns success.

This keeps the mode `22` health/armor pickup proof tied to actual game pickup state: failed touches, disabled pickups, already-taken instanced coop items, full-health health pickups, full-armor armor pickups, and other no-op cases do not increment pickup proof counters. The helper itself ignores non-health and non-armor items, so the central hook can stay at the shared success point without changing weapon, ammo, powerup, or flag behavior.

## CTF Objective Hook Status

The real CTF flag pickup, return, and capture branches are not in `g_items.cpp`; they live in `src/game/sgame/gameplay/g_capture.cpp` inside `CTF_PickupFlag()`. This Worker J lane did not edit that file.

Remaining exact hooks:

- Call `BotObjectives_RecordFlagPickup(other, ent)` from the successful carry branch in `CTF_PickupFlag()`, after `GiveFlagToPlayer(ent, other, flagTeam, flagItem)` and before returning `true`.
- Call `BotObjectives_RecordFlagCapture(other, enemyFlagItem)` from the two-flag capture branch immediately after `AwardFlagCapture(ent, other, flagTeam, pickupTime)`.
- Call `BotObjectives_RecordFlagCapture(other, IT_FLAG_NEUTRAL)` from the one-flag capture branch immediately after `AwardFlagCapture(ent, other, scoringTeam, pickupTime)`.

Those calls should be placed in `g_capture.cpp` so mode `23` can distinguish enemy flag pickup, own flag return, neutral flag pickup, and capture events from the authoritative CTF branches instead of inferring them from the generic item touch success path.
