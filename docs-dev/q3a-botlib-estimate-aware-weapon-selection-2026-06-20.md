# Q3A BotLib Estimate-Aware Weapon Selection

Date: 2026-06-20

Tasks: `FR-04-T03`, `FR-04-T15`, `DV-07-T06`

## Summary

This pass consumes the WORR-owned enemy health and armor estimates added in the
previous round. The combat selector now uses those estimates as small, bounded
weapon-score adjustments:

- low effective enemy health gives direct, precise, or continuous weapons a
  finisher bonus;
- durable armor-heavy enemies give high-pressure combat weapons a modest bonus;
- weak utility/deployable/low-priority weapons receive a modest penalty against
  durable armored enemies;
- pending preferred-weapon switches now honor the selected score result instead
  of always pursuing the preferred weapon whenever it differs from the current
  weapon.

The score adjustments are intentionally smaller than range matching and splash
safety. Estimates can tip close choices, but they do not override the existing
range, ammo, readiness, or self-damage checks.

## Code Changes

- `bot_combat.*`
  - Adds estimate-aware scoring helpers for finisher, armor-pressure, and
    underpowered-weapon cases.
  - Extends `BotWeaponSelectionResult` with current/preferred/selected estimate
    adjustments and reason metadata.
  - Extends `BotCombatStatus` with estimate-use counters and last selected
    estimate values.
  - Changes preferred-weapon switching to require the scorer to choose the
    preferred weapon, making the scorer's output behavior-relevant.

- `bot_brain.cpp`
  - Extends compact `q3a_bot_action_status` output with
    `combat_weapon_selection_estimate_uses`,
    `combat_weapon_selection_finisher_bonuses`,
    `combat_weapon_selection_armor_pressure_bonuses`,
    `combat_weapon_selection_underpowered_penalties`,
    `last_combat_enemy_health_estimate`,
    `last_combat_enemy_armor_estimate`,
    `last_combat_enemy_effective_health_estimate`,
    `last_combat_weapon_estimate_adjustment`, and
    `last_combat_estimate_selection_reason`.

- `tools/bot_scenarios/run_bot_scenarios.py`
  - Registers the new combat weapon-selection estimate fields as optional
    live-combat telemetry so raw reserved-mode reports group them cleanly.

## Follow-Up

This is the first behavior consumer for enemy estimates. Remaining behavior
work should still add a broader inventory/arsenal policy that can choose among
all carried weapons and inventory items, rather than only comparing the current
weapon with the caller-provided preferred weapon.

## Validation

Initial focused object build:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_combat.cpp.obj sgame_x86_64.dll.p/src_game_sgame_bots_bot_brain.cpp.obj`

Result: passed, with Ninja printing the existing `premature end of file;
recovering` warning.
