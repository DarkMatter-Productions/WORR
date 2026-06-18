# Q3A BotLib Combat Weapon Metadata

Date: 2026-06-18

Project tasks: `FR-04-T03`, `FR-04-T15`

## Summary

Phase 6 now has a WORR-native combat metadata surface in `src/game/sgame/bots/bot_combat.*`. The combat module keeps Q2/WORR item IDs as the public currency and exposes weapon traits for the stock Quake II weapons plus locally discoverable expansion/rerelease weapons.

The metadata is intentionally narrow. It does not own inventory scanning, enemy selection, aiming, or damage accounting. It scores the current and caller-provided preferred weapon, records why a weapon was favored, and blocks obviously unsafe self-damage fire when the active splash weapon is too close to the enemy.

## Implemented Weapon Traits

Each metadata entry records:

- Weapon item ID and ammo item ID.
- Ammo needed per real shot, including special cases such as `IT_WEAPON_IONRIPPER` requiring 10 cells and `IT_WEAPON_BFG` requiring 50 cells in the Q2 path.
- Combat priority.
- Minimum, ideal, and maximum range bands.
- Attack model: utility, melee, hitscan, projectile, beam, or deployable.
- Splash/self-damage flags and a conservative unsafe self-damage distance.
- Projectile/hitscan booleans for future tactical consumers.

Covered item IDs:

- `IT_WEAPON_GRAPPLE`, `IT_WEAPON_BLASTER`, `IT_WEAPON_CHAINFIST`
- `IT_WEAPON_SHOTGUN`, `IT_WEAPON_SSHOTGUN`, `IT_WEAPON_MACHINEGUN`, `IT_WEAPON_CHAINGUN`
- `IT_AMMO_GRENADES`, `IT_WEAPON_GLAUNCHER`, `IT_WEAPON_RLAUNCHER`
- `IT_WEAPON_HYPERBLASTER`, `IT_WEAPON_RAILGUN`, `IT_WEAPON_BFG`
- `IT_WEAPON_ETF_RIFLE`, `IT_WEAPON_IONRIPPER`, `IT_WEAPON_PLASMAGUN`, `IT_WEAPON_PLASMABEAM`, `IT_WEAPON_THUNDERBOLT`, `IT_WEAPON_PHALANX`, `IT_WEAPON_DISRUPTOR`
- `IT_AMMO_TRAP`, `IT_AMMO_TESLA`, `IT_WEAPON_PROXLAUNCHER`

## Selection And Telemetry Contract

`BotCombat_SelectPreferredWeapon(const BotCombatContext &)` is a pure helper for selecting between `currentWeaponItem` and `preferredWeaponItem`. It returns the selected item, score details, metadata pointer, switch recommendation, safety flag, and a stable string reason such as `range_match`, `too_close`, `insufficient_ammo`, or `splash_unsafe`.

`BotCombat_Evaluate()` records selection telemetry in `BotCombatStatus`:

- `enemyAcquisitions`, `enemyVisible`, `enemyShootable`
- `fireDecisions`, `withheldFire`, `splashSafetyDeferrals`
- `damageEvents`, `lastDamage`
- `lastEnemyClient`, `lastEnemyDistanceSquared`
- `lastCurrentWeaponScore`, `lastPreferredWeaponScore`, `lastSelectedWeaponScore`
- `lastWeaponMetadataPriority`, `lastWeaponAmmoPerShot`
- `lastEnemyRangeBand`, `lastWeaponAttackModel`
- `lastWeaponSplashDamage`, `lastWeaponSelfDamageRisk`
- `lastSelectionReason`

Damage fields are present for status-line stability but remain zero in this lane because no real damage callback is owned by `bot_combat.*`. They should only be incremented by a future hook that sees actual `Damage()` or match hit events.

## Smoke Compatibility

`sg_bot_frame_command_smoke_combat=1` is compatible with this lane through `BotCombatContext`. Worker 1's blackboard/perception layer can populate:

- `hasEnemy`
- `enemyVisible`
- `enemyShootable`
- `enemyDistanceSquared`
- `enemyClientIndex`

The requested status fields can be printed by the status owner from `BotCombat_GetStatus()` without needing combat-module changes:

- `combat_enemy_acquisitions` -> `BotCombatStatus::enemyAcquisitions`
- `combat_enemy_visible` -> `BotCombatStatus::enemyVisible`
- `combat_enemy_shootable` -> `BotCombatStatus::enemyShootable`
- `combat_fire_decisions` -> `BotCombatStatus::fireDecisions`
- `combat_damage_events` -> `BotCombatStatus::damageEvents`
- `last_combat_enemy_client` -> `BotCombatStatus::lastEnemyClient`
- `last_combat_damage` -> `BotCombatStatus::lastDamage`

## Validation

Commands run:

- `ninja -C builddir-win sgame_x86_64.dll.p/src_game_sgame_bots_bot_combat.cpp.obj`
- `meson compile -C builddir-win sgame_x86_64`

Both commands completed successfully after fixing the pointer member access typo in the scoring path.

## Remaining Gaps

- Selection still compares only the current weapon and caller-supplied preferred weapon. Full inventory scanning belongs in a later action/brain integration pass.
- Damage telemetry is not wired to real damage events yet.
- Weapon traits are practical defaults, not final skill/profile-aware fight weights.
- The combat module does not own status-line printing; consumers should use the status contract above.
