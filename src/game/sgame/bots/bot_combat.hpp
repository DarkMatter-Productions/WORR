// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

struct gentity_t;

enum class BotCombatDecisionKind {
	None,
	SwitchWeapon,
	FireWeapon,
};

enum class BotWeaponRangeBand {
	Unknown,
	Melee,
	Close,
	Medium,
	Long,
};

enum class BotWeaponAttackModel {
	Unknown,
	Utility,
	Melee,
	Hitscan,
	Projectile,
	Beam,
	Deployable,
};

struct BotWeaponMetadata {
	int weaponItem = 0;
	int ammoItem = 0;
	int ammoPerShot = 0;
	int priority = 0;
	int selfDamageSafetyDistanceSquared = 0;
	BotWeaponRangeBand minimumRange = BotWeaponRangeBand::Unknown;
	BotWeaponRangeBand idealRange = BotWeaponRangeBand::Unknown;
	BotWeaponRangeBand maximumRange = BotWeaponRangeBand::Unknown;
	BotWeaponAttackModel attackModel = BotWeaponAttackModel::Unknown;
	bool splashDamage = false;
	bool selfDamageRisk = false;
	bool projectile = false;
	bool hitscan = false;
	const char *name = "unknown";
};

struct BotWeaponSelectionResult {
	int weaponItem = 0;
	int currentWeaponScore = 0;
	int preferredWeaponScore = 0;
	int selectedWeaponScore = 0;
	bool hasKnownWeapon = false;
	bool shouldSwitch = false;
	bool preferredWeaponSafe = true;
	const BotWeaponMetadata *metadata = nullptr;
	const char *reason = "none";
};

struct BotCombatEnemyFacts {
	bool valid = false;
	bool botValid = false;
	bool enemyValid = false;
	bool teammate = false;
	bool visible = false;
	bool shootable = false;
	int botClientIndex = -1;
	int enemyEntity = -1;
	int enemyClientIndex = -1;
	int enemySpawnCount = 0;
	int enemyHealth = 0;
	int distanceSquared = 0;
};

// Caller owns perception facts. hasEnemy alone is not enough to fire; visible
// and shootable are separate so future aim/perception policy stays explicit.
struct BotCombatContext {
	bool hasEnemy = false;
	bool enemyVisible = false;
	bool enemyShootable = false;
	bool currentWeaponReady = false;
	bool preferredWeaponReady = false;
	bool skillAllowsFire = true;
	int currentWeaponItem = 0;
	int preferredWeaponItem = 0;
	int currentWeaponAmmo = 0;
	int preferredWeaponAmmo = 0;
	int enemyDistanceSquared = 0;
	int enemyClientIndex = -1;
};

// SwitchWeapon is intent-only. FireWeapon may request BUTTON_ATTACK through the
// action dispatcher, but does not aim or choose movement.
struct BotCombatDecision {
	BotCombatDecisionKind kind = BotCombatDecisionKind::None;
	int priority = 0;
	int weaponItem = 0;
	bool pressAttack = false;
	const char *reason = "none";
};

// Process-local counters accumulate until BotCombat_ResetStatus().
struct BotCombatStatus {
	int evaluations = 0;
	int noEnemy = 0;
	int enemyFactEvaluations = 0;
	int enemyFactInvalidBots = 0;
	int enemyFactInvalidEnemies = 0;
	int enemyFactTeamSkips = 0;
	int enemyFactSearches = 0;
	int enemyFactSearchHits = 0;
	int enemyFactVisibilityChecks = 0;
	int enemyFactShootabilityChecks = 0;
	int enemyAcquisitions = 0;
	int enemyVisible = 0;
	int enemyShootable = 0;
	int blockedSight = 0;
	int weaponSwitchDecisions = 0;
	int fireDecisions = 0;
	int withheldFire = 0;
	int weaponSelectionEvaluations = 0;
	int knownWeaponSelections = 0;
	int unknownWeaponSelections = 0;
	int splashSafetyDeferrals = 0;
	int damageEvents = 0;
	int damageInvalidEvents = 0;
	int damageNonBotAttackerSkips = 0;
	int damageSelfSkips = 0;
	int damageFriendlySkips = 0;
	int damageNonClientTargetSkips = 0;
	int damageZeroSkips = 0;
	int lastWeaponItem = 0;
	int lastPreferredWeaponItem = 0;
	int lastPriority = 0;
	int lastBotClient = -1;
	int lastEnemyEntity = -1;
	int lastEnemyDistanceSquared = 0;
	int lastEnemyClient = -1;
	int lastDamage = 0;
	int lastDamageAttackerClient = -1;
	int lastDamageTargetClient = -1;
	int lastDamageAttackerEntity = -1;
	int lastDamageTargetEntity = -1;
	int lastCurrentWeaponScore = 0;
	int lastPreferredWeaponScore = 0;
	int lastSelectedWeaponScore = 0;
	int lastWeaponMetadataPriority = 0;
	int lastWeaponAmmoPerShot = 0;
	BotWeaponRangeBand lastEnemyRangeBand = BotWeaponRangeBand::Unknown;
	BotWeaponAttackModel lastWeaponAttackModel = BotWeaponAttackModel::Unknown;
	bool lastWeaponSplashDamage = false;
	bool lastWeaponSelfDamageRisk = false;
	const char *lastSelectionReason = "none";
};

void BotCombat_ResetStatus();
BotCombatDecision BotCombat_Evaluate(const BotCombatContext &context);
BotCombatEnemyFacts BotCombat_BuildEnemyFacts(gentity_t *bot, gentity_t *enemy);
bool BotCombat_FindNearestEnemy(gentity_t *bot, BotCombatEnemyFacts *facts);
BotCombatContext BotCombat_WithEnemyFacts(BotCombatContext context, const BotCombatEnemyFacts &facts);
void BotCombat_RecordDamageEvent(const gentity_t *attacker, const gentity_t *target, int damage);
const BotCombatStatus &BotCombat_GetStatus();
const char *BotCombat_DecisionName(BotCombatDecisionKind kind);
BotWeaponRangeBand BotCombat_RangeBandForDistanceSquared(int distanceSquared);
const BotWeaponMetadata *BotCombat_GetWeaponMetadata(int weaponItem);
BotWeaponSelectionResult BotCombat_SelectPreferredWeapon(const BotCombatContext &context);
const char *BotCombat_RangeBandName(BotWeaponRangeBand band);
const char *BotCombat_AttackModelName(BotWeaponAttackModel model);
