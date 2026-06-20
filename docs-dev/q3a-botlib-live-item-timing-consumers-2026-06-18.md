# Q3A BotLib Live Item Timing Consumers

Date: 2026-06-18

Tasks: `FR-04-T02`, `FR-04-T03`, `FR-04-T07`

## Summary

This owned slice extends `src/game/sgame/bots/bot_items.*` with reusable
item-timing consumer helpers. The earlier timing policy helper could answer
whether a supplied timer was allowed, fuzzed, ready, or blocked, but normal item
selection did not yet have an API-shaped way to carry those facts into utility
scoring.

The new consumer layer keeps behavior conservative:

- live, visible pickups are selectable immediately;
- hidden respawn candidates need caller-supplied observed pickup facts before
  timer knowledge can influence selection;
- unobserved hidden respawn timers are blocked through the existing fairness
  policy instead of inferred from server internals;
- waiting or policy-blocked candidates return no `SeekCandidate` decision and
  increment timing-consumer deferral counters.

## API

`BotItems_BuildTimingConsumerFrameForEntity()` builds a reusable timing frame
from a bot/candidate entity pair. It records live-pickup state, entity identity,
spawn count, item id, current time, observed pickup time, and expected
availability time when the entity is in an item-respawn state.

`BotItems_EvaluateTimingConsumer()` turns that frame into a
`BotItemTimingConsumerResult`. The result reports whether the candidate is
selectable now, whether timing knowledge was usable, whether the candidate is
waiting for respawn, whether fairness policy blocked the timer, the effective
available time, remaining milliseconds, fuzz offset, and stable reason enums.

`BotItems_ApplyTimingConsumerResult()` maps a consumer result onto
`BotItemContext` fields so callers can build a normal utility context, then add
observed timing facts without duplicating policy logic.

`BotItems_BuildContextForTimedEntity()` is the convenience bridge for future
main-brain/nav integration. Existing `BotItems_BuildContextForEntity()` now
routes through it with no observed hidden-respawn facts, preserving active-item
behavior while letting live candidates carry status-friendly consumer metadata.

## Status Counters

`BotItemStatus` now tracks timing-consumer evaluations, invalid frames, live
pickup candidates, ready timers, waiting timers, fairness blocks, and normal
selection deferrals caused by timing. Last-consumer metadata records client,
entity, item, fuzz, effective available time, remaining time, policy reason, and
consumer reason.

These fields are intentionally local to `bot_items` in this slice. The current
owned write set excludes `bot_brain.cpp`, so compact status emission can be
added by the brain/status owner later without changing the item API.

## Integration Notes

For broad live item respawn planning, the future caller should keep the active
pickup path simple and use the consumer only when it has real observed pickup
knowledge:

1. Build or update an item context with `BotItems_BuildContextForTimedEntity()`,
   passing `pickupObserved=true` only for observations owned by the bot/team
   knowledge source.
2. Let `BotItems_Evaluate()` make the final seek/no-seek decision from the
   combined utility and timing facts.
3. Route only `SeekCandidate` decisions into nav ownership; waiting or
   fairness-blocked results should remain observations, not reservations.

## Validation

Command:

```powershell
ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_items.cpp.obj
```

Result: passed. Ninja also emitted the existing shared-build-dir warning
`premature end of file; recovering`.
