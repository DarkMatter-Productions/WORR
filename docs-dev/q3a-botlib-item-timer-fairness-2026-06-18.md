# Q3A BotLib Item Timer Fairness

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T07`, `FR-04-T15`, `DV-07-T06`

## Summary

This slice adds a deterministic item timing fairness helper inside
`src/game/sgame/bots/bot_items.*`. The helper lets bot item timing knowledge be
disabled or fuzzed without changing the existing active-pickup candidate scan or
route ownership boundaries.

The current runtime path only applies the helper where `bot_items` already had
timer knowledge: self-owned powerup timers used to decide whether another
matching powerup is already effectively owned. With default cvars, behavior is
preserved. When timing is disabled or fuzzed, that same already-owned decision
uses the policy result instead of an exact timer comparison.

## Policy API

`BotItems_EvaluatePickupTimingPolicy(const BotItemTimingPolicyFrame &)` returns
`BotItemTimingPolicyResult`.

The caller supplies:

- client, entity, spawn-count, and item identifiers for deterministic seeding;
- whether the pickup timing was actually observed by this bot/team knowledge
  source;
- observed pickup time, expected available time, and current time in
  milliseconds.

The result reports:

- whether timer knowledge may be used;
- whether the effective pickup window is open;
- the deterministic fuzz offset;
- effective available time and remaining milliseconds;
- a stable reason enum/name.

By default, `requireObservedPickup` is true in the policy config. Future world
respawn timing consumers should pass `pickupObserved=false` for unseen pickups;
the helper will block that timer knowledge instead of letting bots infer exact
server internals.

## Cvars

`bot_items` lazily reads these cvars when no explicit test/config override is
installed:

- `sg_bot_allow_item_timers` defaults to `1`. Set to `0` to suppress bot timer
  knowledge.
- `sg_bot_item_timer_fuzz_ms` defaults to `0`. Positive values apply a stable
  symmetric fuzz offset, clamped to at most 60000 ms.

The fuzz seed uses client/entity/spawn/item/timing facts, so results are
deterministic across identical runs while avoiding exact pickup timing.

## Status

`BotItemStatus` now tracks timing-policy evaluations, invalid inputs, disabled
timer blocks, unobserved-pickup blocks, exact/fuzzed uses, ready/waiting results,
and last timing-policy metadata. This pass does not edit `bot_brain.*`, so those
fields are not yet emitted on `q3a_bot_action_status`.

## Integration Notes

Main-thread integration can stay narrow:

- Keep the existing active-pickup scan unchanged for live entities.
- For any future respawn scheduler or BotLib item-goal timing bridge, call
  `BotItems_EvaluatePickupTimingPolicy()` before planning around a hidden item
  respawn. Skip timer-based planning when `mayUseTimer` is false, and use
  `effectiveAvailableMilliseconds` rather than the exact respawn time.
- If status output is desired, add compact fields from `BotItems_GetStatus()` in
  the current `q3a_bot_action_status` owner.

## Validation

Command:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_items.cpp.obj
```

Result: passed. Ninja emitted the existing shared-build-dir warning
`premature end of file; recovering`.

## Remaining Work

- Wire observed item pickup facts from the eventual nav/blackboard owner into
  the timing-policy frame.
- Decide which timing counters should be surfaced in runtime status logs.
- Add user-facing cvar documentation when the public bot cvar set is finalized.
