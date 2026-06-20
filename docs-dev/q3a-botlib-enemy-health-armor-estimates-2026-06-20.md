# Q3A BotLib Enemy Health and Armor Estimates

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass adds WORR-owned enemy health and armor estimate plumbing to the bot
combat blackboard. It does not import Q3A behavior code and does not change
weapon-selection policy yet.

The new path lets bots carry a fair, per-bot estimate of the current enemy's
vitals:

- visible enemy observations snap the estimate to current server health/armor;
- bot-attributed damage events expose split health/armor damage and a sequence
  number;
- each blackboard slot applies a damage sequence at most once, and only to a
  remembered non-visible enemy estimate;
- visible observations win over damage deltas, avoiding double-subtraction after
  the next scan already sees post-hit health.

## Code Changes

- `bot_combat.*`
  - `BotCombatEnemyFacts` now carries `enemyArmor` alongside `enemyHealth`.
  - `BotCombatContext` now carries `enemyEstimateKnown`,
    `enemyHealthEstimate`, and `enemyArmorEstimate` for later weapon/inventory
    consumers.
  - `BotCombatStatus` now reports visible health/armor observations plus split
    bot damage fields: `lastDamageHealth`, `lastDamageArmor`, and
    `lastDamageSequence`.
  - `BotCombat_RecordDamageEvent(...)` accepts optional health/armor damage
    splits while preserving the old total-damage path through default
    arguments.

- `g_combat.cpp`
  - The existing bot-attributed damage hook now passes `statTake` as health
    damage and the regular/power armor contribution as armor damage.

- `bot_brain.*`
  - `BotBrainBlackboardSnapshot` now exposes current observed enemy health/armor,
    estimate-known state, health/armor estimates, effective estimated health,
    last observation time, and last applied damage sequence.
  - `q3a_bot_blackboard_status` now prints estimate counters and last estimate
    fields.
  - The compact `q3a_bot_action_status` marker now exposes combat-side raw
    health/armor observation and split-damage fields for scenario tooling.

## Follow-Up

The estimate data is now available to behavior owners, but this slice keeps
selection behavior unchanged. Follow-up work should use these fields for:

- finisher/low-health weapon pressure without perfect omniscience;
- armor-aware weapon and item-role choices;
- richer inventory policy, especially powerup and sustain decisions;
- scenario assertions that prove estimates come from visible observations or
  bot-attributed damage rather than synthetic setup.

## Validation

Commands run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_combat.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj sgame_x86_64.dll.p/src_game_sgame_gameplay_g_combat.cpp.obj`
- `python tools/bot_scenarios/test_run_bot_scenarios.py`

Results: the focused object build passed, with Ninja printing the existing
`premature end of file; recovering` warning. The scenario parser test suite
passed 30 tests.
