# Q3A Botlib Brain Runtime Consumers Round - 2026-06-18

Task IDs: FR-04-T03, FR-04-T15, DV-07-T06

## Summary

This round wires the newly added botlib policy helper status into the main brain runtime surface without changing navigation ownership or broad item-selection behavior.

## Implementation

- Preserved the live aim policy/projectile-lead path in `Bot_CommandAimPointForKnownEnemy`.
- Added compact and detailed action telemetry for live aim remaining reaction/settle windows, burst shots remaining, projectile raw/scaled/clamped lead values, and item timing consumer decisions.
- Added compact objective telemetry for coop and resource policy counters, last policy intents, name helpers, priorities, and reasons.
- Evaluated coop policy once in the frame objective policy pass as read-only status data.
- Evaluated resource policy only when the current action is already a move-to-item decision, using conservative self-pickup defaults and no command or route mutation.

## Notes

No entity-backed item selection call inside `bot_brain.cpp` was suitable for conversion to `BotItems_BuildContextForTimedEntity`; the remaining `BotItems_BuildContextForItem` calls in this file are smoke/proof paths that build synthetic item contexts.

## Validation

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`
- `git diff --check -- src/game/sgame/bots/bot_brain.cpp src/game/sgame/bots/bot_brain.hpp docs-dev/q3a-botlib-brain-runtime-consumers-round-2026-06-18.md`
