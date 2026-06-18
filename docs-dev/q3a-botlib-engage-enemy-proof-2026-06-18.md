# Q3A BotLib Engage Enemy Proof Helpers

Date: 2026-06-18

Tasks: `DV-03-T05`

## Summary

This Worker A pass keeps the reserved `engage_enemy` mode `20` combat proof work
inside `bot_combat.*` and the existing real damage hook. It does not edit
`src/game/sgame/bots/bot_brain.cpp` or `src/server/main.c`.

## Implementation

- Added `BotCombatEnemyFacts` as the combat-owned proof record for a bot/enemy
  pair.
- Added `BotCombat_BuildEnemyFacts(gentity_t *bot, gentity_t *enemy)` to check:
  - bot validity and live bot client state,
  - live enemy client state,
  - not-self and not-`FL_NOTARGET`,
  - team exclusion through `OnSameTeam`,
  - real visibility through `visible`,
  - real shootability through `CanDamage`,
  - enemy entity number, client index, spawn count, health, and distance.
- Added `BotCombat_FindNearestEnemy(gentity_t *bot, BotCombatEnemyFacts *facts)`
  to choose the nearest visible enemy, preferring shootable candidates when
  multiple players are visible.
- Added `BotCombat_WithEnemyFacts(...)` so callers can merge real entity facts
  into an existing weapon/ammo `BotCombatContext`.
- Extended `BotCombatStatus` with proof-side counters for enemy fact
  evaluations, invalid bot/enemy skips, team skips, searches, search hits,
  visibility checks, shootability checks, and last bot/enemy identifiers.
- Hardened `BotCombat_RecordDamageEvent(...)` so direct calls also filter
  non-bot attackers, self damage, teammate damage, non-client targets, invalid
  entity indices, and non-positive damage before incrementing
  `combat_damage_events`.
- Added last attacker/target entity and client fields for real attributed
  damage proof.

The existing `src/game/sgame/gameplay/g_combat.cpp` hook remains the source of
real bot-attributed damage events. It calls `BotCombat_RecordDamageEvent` only
after the normal WORR damage/protection path has produced positive, non-friendly
client damage.

## Integration Contract

The main-thread `bot_brain.cpp` integration should call one of these paths when
running combat smoke mode `20`:

1. Build weapon/ammo facts with the existing action context.
2. Call `BotCombat_FindNearestEnemy(bot, &facts)` or
   `BotCombat_BuildEnemyFacts(bot, suppliedEnemy)`.
3. Merge with `BotCombat_WithEnemyFacts(context.combat, facts)`.
4. Pass the enriched context through `BotActions_Decide`.
5. Continue applying `BotActions_ApplyDecisionDetailed` so an attack decision
   mutates `BUTTON_ATTACK`.
6. Let the normal weapon/damage pipeline call `Damage`, which in turn records
   real bot-attributed damage through `BotCombat_RecordDamageEvent`.

No counter in this pass fabricates scenario success. The helper counters only
advance when real entity, visibility, shootability, and damage checks are run.

## Validation

- `meson compile -C builddir-win sgame_x86_64` passed.

## Remaining Work

- `bot_brain.cpp` still needs to consume the helper API for mode `20`; this
  worker intentionally did not edit that file.
- The scenario still needs command-facing proof for
  `action_applied_attack_buttons >= 1`.
- The dedicated scenario should position two live clients in a visible,
  shootable lane and keep spawn protection/friendly-team state from suppressing
  the real damage event.
