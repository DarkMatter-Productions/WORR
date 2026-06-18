// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "bot_combat.hpp"

#include "../g_local.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace {
constexpr int BOT_COMBAT_SWITCH_WEAPON_PRIORITY = 80;
constexpr int BOT_COMBAT_FIRE_PRIORITY = 70;
constexpr int BOT_COMBAT_CLOSE_RANGE_BONUS = 10;
constexpr int BOT_COMBAT_FIRE_RANGE_MATCH_BONUS = 5;
constexpr int BOT_COMBAT_MELEE_RANGE_DIST_SQUARED = 128 * 128;
constexpr int BOT_COMBAT_CLOSE_RANGE_DIST_SQUARED = 512 * 512;
constexpr int BOT_COMBAT_MEDIUM_RANGE_DIST_SQUARED = 1024 * 1024;
constexpr int BOT_COMBAT_WEAPON_SCORE_UNUSABLE = -100000;
constexpr int BOT_COMBAT_WEAPON_SWITCH_SCORE_MARGIN = 4;
constexpr int BOT_COMBAT_SPLASH_UNSAFE_PENALTY = 35;
constexpr int BOT_COMBAT_LOW_AMMO_PENALTY = 4;
constexpr int BOT_COMBAT_RANGE_MATCH_BONUS = 16;
constexpr int BOT_COMBAT_RANGE_USABLE_BONUS = 8;
constexpr int BOT_COMBAT_TOO_CLOSE_PENALTY = 24;
constexpr int BOT_COMBAT_TOO_FAR_PENALTY = 16;

BotCombatStatus botCombatStatus;

static_assert(static_cast<int>(BotCombatDecisionKind::None) == 0);

constexpr int Square(int value) {
	return value * value;
}

constexpr std::array<BotWeaponMetadata, 23> BOT_WEAPON_METADATA = { {
	{
		.weaponItem = IT_WEAPON_GRAPPLE,
		.ammoItem = IT_NULL,
		.priority = 5,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Utility,
		.name = "grapple",
	},
	{
		.weaponItem = IT_WEAPON_BLASTER,
		.ammoItem = IT_NULL,
		.priority = 25,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.projectile = true,
		.name = "blaster",
	},
	{
		.weaponItem = IT_WEAPON_CHAINFIST,
		.ammoItem = IT_NULL,
		.priority = 54,
		.minimumRange = BotWeaponRangeBand::Melee,
		.idealRange = BotWeaponRangeBand::Melee,
		.maximumRange = BotWeaponRangeBand::Melee,
		.attackModel = BotWeaponAttackModel::Melee,
		.name = "chainfist",
	},
	{
		.weaponItem = IT_WEAPON_SHOTGUN,
		.ammoItem = IT_AMMO_SHELLS,
		.ammoPerShot = 1,
		.priority = 58,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Close,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Hitscan,
		.hitscan = true,
		.name = "shotgun",
	},
	{
		.weaponItem = IT_WEAPON_SSHOTGUN,
		.ammoItem = IT_AMMO_SHELLS,
		.ammoPerShot = 2,
		.priority = 74,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Close,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Hitscan,
		.hitscan = true,
		.name = "super_shotgun",
	},
	{
		.weaponItem = IT_WEAPON_MACHINEGUN,
		.ammoItem = IT_AMMO_BULLETS,
		.ammoPerShot = 1,
		.priority = 60,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Hitscan,
		.hitscan = true,
		.name = "machinegun",
	},
	{
		.weaponItem = IT_WEAPON_ETF_RIFLE,
		.ammoItem = IT_AMMO_FLECHETTES,
		.ammoPerShot = 1,
		.priority = 62,
		.minimumRange = BotWeaponRangeBand::Medium,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.projectile = true,
		.name = "etf_rifle",
	},
	{
		.weaponItem = IT_WEAPON_CHAINGUN,
		.ammoItem = IT_AMMO_BULLETS,
		.ammoPerShot = 1,
		.priority = 70,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Hitscan,
		.hitscan = true,
		.name = "chaingun",
	},
	{
		.weaponItem = IT_AMMO_GRENADES,
		.ammoItem = IT_AMMO_GRENADES,
		.ammoPerShot = 1,
		.priority = 50,
		.selfDamageSafetyDistanceSquared = Square(192),
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Projectile,
		.splashDamage = true,
		.selfDamageRisk = true,
		.projectile = true,
		.name = "hand_grenades",
	},
	{
		.weaponItem = IT_AMMO_TRAP,
		.ammoItem = IT_AMMO_TRAP,
		.ammoPerShot = 1,
		.priority = 34,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Close,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Deployable,
		.projectile = true,
		.name = "trap",
	},
	{
		.weaponItem = IT_AMMO_TESLA,
		.ammoItem = IT_AMMO_TESLA,
		.ammoPerShot = 1,
		.priority = 46,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Deployable,
		.projectile = true,
		.name = "tesla",
	},
	{
		.weaponItem = IT_WEAPON_GLAUNCHER,
		.ammoItem = IT_AMMO_GRENADES,
		.ammoPerShot = 1,
		.priority = 64,
		.selfDamageSafetyDistanceSquared = Square(192),
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Projectile,
		.splashDamage = true,
		.selfDamageRisk = true,
		.projectile = true,
		.name = "grenade_launcher",
	},
	{
		.weaponItem = IT_WEAPON_PROXLAUNCHER,
		.ammoItem = IT_AMMO_PROX,
		.ammoPerShot = 1,
		.priority = 52,
		.selfDamageSafetyDistanceSquared = Square(192),
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Deployable,
		.splashDamage = true,
		.selfDamageRisk = true,
		.projectile = true,
		.name = "prox_launcher",
	},
	{
		.weaponItem = IT_WEAPON_RLAUNCHER,
		.ammoItem = IT_AMMO_ROCKETS,
		.ammoPerShot = 1,
		.priority = 85,
		.selfDamageSafetyDistanceSquared = Square(256),
		.minimumRange = BotWeaponRangeBand::Medium,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.splashDamage = true,
		.selfDamageRisk = true,
		.projectile = true,
		.name = "rocket_launcher",
	},
	{
		.weaponItem = IT_WEAPON_HYPERBLASTER,
		.ammoItem = IT_AMMO_CELLS,
		.ammoPerShot = 1,
		.priority = 72,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.projectile = true,
		.name = "hyperblaster",
	},
	{
		.weaponItem = IT_WEAPON_IONRIPPER,
		.ammoItem = IT_AMMO_CELLS,
		.ammoPerShot = 10,
		.priority = 68,
		.minimumRange = BotWeaponRangeBand::Medium,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.projectile = true,
		.name = "ion_ripper",
	},
	{
		.weaponItem = IT_WEAPON_PLASMAGUN,
		.ammoItem = IT_AMMO_CELLS,
		.ammoPerShot = 1,
		.priority = 78,
		.selfDamageSafetyDistanceSquared = Square(192),
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.splashDamage = true,
		.selfDamageRisk = true,
		.projectile = true,
		.name = "plasma_gun",
	},
	{
		.weaponItem = IT_WEAPON_PLASMABEAM,
		.ammoItem = IT_AMMO_CELLS,
		.ammoPerShot = 1,
		.priority = 76,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Close,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Beam,
		.hitscan = true,
		.name = "plasma_beam",
	},
	{
		.weaponItem = IT_WEAPON_THUNDERBOLT,
		.ammoItem = IT_AMMO_CELLS,
		.ammoPerShot = 1,
		.priority = 78,
		.minimumRange = BotWeaponRangeBand::Close,
		.idealRange = BotWeaponRangeBand::Close,
		.maximumRange = BotWeaponRangeBand::Medium,
		.attackModel = BotWeaponAttackModel::Beam,
		.hitscan = true,
		.name = "thunderbolt",
	},
	{
		.weaponItem = IT_WEAPON_RAILGUN,
		.ammoItem = IT_AMMO_SLUGS,
		.ammoPerShot = 1,
		.priority = 88,
		.minimumRange = BotWeaponRangeBand::Medium,
		.idealRange = BotWeaponRangeBand::Long,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Hitscan,
		.hitscan = true,
		.name = "railgun",
	},
	{
		.weaponItem = IT_WEAPON_PHALANX,
		.ammoItem = IT_AMMO_MAGSLUG,
		.ammoPerShot = 1,
		.priority = 82,
		.selfDamageSafetyDistanceSquared = Square(192),
		.minimumRange = BotWeaponRangeBand::Medium,
		.idealRange = BotWeaponRangeBand::Medium,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.splashDamage = true,
		.selfDamageRisk = true,
		.projectile = true,
		.name = "phalanx",
	},
	{
		.weaponItem = IT_WEAPON_BFG,
		.ammoItem = IT_AMMO_CELLS,
		.ammoPerShot = 50,
		.priority = 92,
		.selfDamageSafetyDistanceSquared = Square(384),
		.minimumRange = BotWeaponRangeBand::Medium,
		.idealRange = BotWeaponRangeBand::Long,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.splashDamage = true,
		.selfDamageRisk = true,
		.projectile = true,
		.name = "bfg10k",
	},
	{
		.weaponItem = IT_WEAPON_DISRUPTOR,
		.ammoItem = IT_AMMO_ROUNDS,
		.ammoPerShot = 1,
		.priority = 90,
		.minimumRange = BotWeaponRangeBand::Medium,
		.idealRange = BotWeaponRangeBand::Long,
		.maximumRange = BotWeaponRangeBand::Long,
		.attackModel = BotWeaponAttackModel::Projectile,
		.projectile = true,
		.name = "disruptor",
	},
} };

int BotCombat_RangeRank(BotWeaponRangeBand band) {
	switch (band) {
	case BotWeaponRangeBand::Melee:
		return 0;
	case BotWeaponRangeBand::Close:
		return 1;
	case BotWeaponRangeBand::Medium:
		return 2;
	case BotWeaponRangeBand::Long:
		return 3;
	default:
		return -1;
	}
}

bool BotCombat_IsRangeWithin(BotWeaponRangeBand actual, const BotWeaponMetadata &metadata) {
	const int actualRank = BotCombat_RangeRank(actual);
	const int minimumRank = BotCombat_RangeRank(metadata.minimumRange);
	const int maximumRank = BotCombat_RangeRank(metadata.maximumRange);
	return actualRank >= 0 && minimumRank >= 0 && maximumRank >= 0 &&
		actualRank >= minimumRank && actualRank <= maximumRank;
}

bool BotCombat_IsSelfDamageUnsafe(const BotWeaponMetadata *metadata, const BotCombatContext &context) {
	return metadata != nullptr &&
		metadata->selfDamageRisk &&
		metadata->selfDamageSafetyDistanceSquared > 0 &&
		context.enemyDistanceSquared > 0 &&
		context.enemyDistanceSquared <= metadata->selfDamageSafetyDistanceSquared;
}

bool BotCombat_IsBotClient(const gentity_t *ent) {
	return ent != nullptr &&
		ent->inUse &&
		ent->client != nullptr &&
		(((ent->svFlags & SVF_BOT) != 0) || ent->client->sess.is_a_bot);
}

int BotCombat_ClientIndex(const gentity_t *ent) {
	if (ent == nullptr || ent->client == nullptr) {
		return -1;
	}

	const int clientIndex = static_cast<int>(ent->s.number) - 1;
	if (clientIndex < 0 || clientIndex >= static_cast<int>(game.maxClients)) {
		return -1;
	}

	return clientIndex;
}

int BotCombat_EntityNumber(const gentity_t *ent) {
	return ent != nullptr ? static_cast<int>(ent->s.number) : -1;
}

bool BotCombat_AliveClient(const gentity_t *ent) {
	return ent != nullptr &&
		ent->inUse &&
		ent->client != nullptr &&
		ent->health > 0 &&
		!ent->deadFlag &&
		!ent->client->eliminated &&
		ClientIsPlaying(ent->client);
}

bool BotCombat_SameTeam(const gentity_t *a, const gentity_t *b) {
	if (a == nullptr || b == nullptr || a->client == nullptr || b->client == nullptr) {
		return false;
	}
	return OnSameTeam(const_cast<gentity_t *>(a), const_cast<gentity_t *>(b));
}

int BotCombat_ClampDistanceSquared(float distanceSquared) {
	if (distanceSquared <= 0.0f) {
		return 0;
	}
	if (distanceSquared >= static_cast<float>(std::numeric_limits<int>::max())) {
		return std::numeric_limits<int>::max();
	}
	return static_cast<int>(distanceSquared);
}

struct BotWeaponScore {
	int score = BOT_COMBAT_WEAPON_SCORE_UNUSABLE;
	bool usable = false;
	bool safe = true;
	const BotWeaponMetadata *metadata = nullptr;
	const char *reason = "unusable";
};

BotWeaponScore BotCombat_ScoreWeapon(
	int weaponItem,
	int ammo,
	bool ready,
	const BotCombatContext &context) {
	if (weaponItem <= 0) {
		return {};
	}

	if (!ready) {
		return {
			.reason = "weapon_not_ready",
		};
	}

	const BotWeaponMetadata *metadata = BotCombat_GetWeaponMetadata(weaponItem);
	if (metadata == nullptr) {
		return {
			.score = 1,
			.usable = true,
			.reason = "unknown_weapon",
		};
	}

	if (metadata->ammoPerShot > 0 && ammo < metadata->ammoPerShot) {
		return {
			.metadata = metadata,
			.reason = "insufficient_ammo",
		};
	}

	BotWeaponRangeBand range = BotCombat_RangeBandForDistanceSquared(context.enemyDistanceSquared);
	int score = metadata->priority;
	const char *reason = "weapon_priority";

	if (range != BotWeaponRangeBand::Unknown) {
		if (range == metadata->idealRange) {
			score += BOT_COMBAT_RANGE_MATCH_BONUS;
			reason = "range_match";
		} else if (BotCombat_IsRangeWithin(range, *metadata)) {
			score += BOT_COMBAT_RANGE_USABLE_BONUS;
			reason = "range_usable";
		} else if (BotCombat_RangeRank(range) < BotCombat_RangeRank(metadata->minimumRange)) {
			score -= BOT_COMBAT_TOO_CLOSE_PENALTY;
			reason = "too_close";
		} else if (BotCombat_RangeRank(range) > BotCombat_RangeRank(metadata->maximumRange)) {
			score -= BOT_COMBAT_TOO_FAR_PENALTY;
			reason = "too_far";
		}
	}

	if (metadata->ammoPerShot > 0 && ammo == metadata->ammoPerShot) {
		score -= BOT_COMBAT_LOW_AMMO_PENALTY;
		reason = "last_ammo";
	}

	bool safe = !BotCombat_IsSelfDamageUnsafe(metadata, context);
	if (!safe) {
		score -= BOT_COMBAT_SPLASH_UNSAFE_PENALTY;
		reason = "splash_unsafe";
	}

	return {
		.score = score,
		.usable = true,
		.safe = safe,
		.metadata = metadata,
		.reason = reason,
	};
}

void BotCombat_RecordSelection(const BotCombatContext &context, const BotWeaponSelectionResult &selection) {
	botCombatStatus.weaponSelectionEvaluations++;
	botCombatStatus.lastCurrentWeaponScore = selection.currentWeaponScore;
	botCombatStatus.lastPreferredWeaponScore = selection.preferredWeaponScore;
	botCombatStatus.lastSelectedWeaponScore = selection.selectedWeaponScore;
	botCombatStatus.lastPreferredWeaponItem = context.preferredWeaponItem;
	botCombatStatus.lastSelectionReason = selection.reason;
	botCombatStatus.lastEnemyRangeBand = BotCombat_RangeBandForDistanceSquared(context.enemyDistanceSquared);

	if (selection.metadata != nullptr) {
		botCombatStatus.knownWeaponSelections++;
		botCombatStatus.lastWeaponMetadataPriority = selection.metadata->priority;
		botCombatStatus.lastWeaponAmmoPerShot = selection.metadata->ammoPerShot;
		botCombatStatus.lastWeaponAttackModel = selection.metadata->attackModel;
		botCombatStatus.lastWeaponSplashDamage = selection.metadata->splashDamage;
		botCombatStatus.lastWeaponSelfDamageRisk = selection.metadata->selfDamageRisk;
	} else {
		botCombatStatus.unknownWeaponSelections++;
		botCombatStatus.lastWeaponMetadataPriority = 0;
		botCombatStatus.lastWeaponAmmoPerShot = 0;
		botCombatStatus.lastWeaponAttackModel = BotWeaponAttackModel::Unknown;
		botCombatStatus.lastWeaponSplashDamage = false;
		botCombatStatus.lastWeaponSelfDamageRisk = false;
	}
}
} // namespace

void BotCombat_ResetStatus() {
	botCombatStatus = {};
	botCombatStatus.lastBotClient = -1;
	botCombatStatus.lastEnemyEntity = -1;
	botCombatStatus.lastEnemyClient = -1;
	botCombatStatus.lastDamageAttackerClient = -1;
	botCombatStatus.lastDamageTargetClient = -1;
	botCombatStatus.lastDamageAttackerEntity = -1;
	botCombatStatus.lastDamageTargetEntity = -1;
}

BotCombatDecision BotCombat_Evaluate(const BotCombatContext &context) {
	botCombatStatus.evaluations++;
	botCombatStatus.lastEnemyDistanceSquared = context.enemyDistanceSquared;
	botCombatStatus.lastEnemyClient = context.enemyClientIndex;

	if (!context.hasEnemy) {
		botCombatStatus.noEnemy++;
		botCombatStatus.lastEnemyClient = -1;
		return {};
	}

	botCombatStatus.enemyAcquisitions++;
	if (context.enemyVisible) {
		botCombatStatus.enemyVisible++;
	}
	if (context.enemyShootable) {
		botCombatStatus.enemyShootable++;
	}

	const BotWeaponSelectionResult weaponSelection = BotCombat_SelectPreferredWeapon(context);
	BotCombat_RecordSelection(context, weaponSelection);

	if (context.preferredWeaponReady &&
		context.preferredWeaponItem > 0 &&
		context.currentWeaponItem > 0 &&
		context.preferredWeaponItem != context.currentWeaponItem) {
		const char *reason = weaponSelection.weaponItem == context.preferredWeaponItem ?
			weaponSelection.reason : "preferred_weapon_pending";
		botCombatStatus.weaponSwitchDecisions++;
		botCombatStatus.lastWeaponItem = context.preferredWeaponItem;
		botCombatStatus.lastPriority = BOT_COMBAT_SWITCH_WEAPON_PRIORITY;
		return {
			.kind = BotCombatDecisionKind::SwitchWeapon,
			.priority = BOT_COMBAT_SWITCH_WEAPON_PRIORITY,
			.weaponItem = context.preferredWeaponItem,
			.reason = reason,
		};
	}

	if (!context.enemyVisible || !context.enemyShootable) {
		botCombatStatus.blockedSight++;
		return {};
	}

	if (!context.currentWeaponReady || !context.skillAllowsFire) {
		botCombatStatus.withheldFire++;
		return {};
	}

	const BotWeaponMetadata *currentWeaponMetadata = BotCombat_GetWeaponMetadata(context.currentWeaponItem);
	if (BotCombat_IsSelfDamageUnsafe(currentWeaponMetadata, context)) {
		botCombatStatus.withheldFire++;
		botCombatStatus.splashSafetyDeferrals++;
		botCombatStatus.lastWeaponItem = context.currentWeaponItem;
		botCombatStatus.lastPriority = 0;
		botCombatStatus.lastSelectionReason = "splash_fire_unsafe";
		return {};
	}

	int priority = BOT_COMBAT_FIRE_PRIORITY;
	const char *reason = "shootable_enemy";
	const BotWeaponRangeBand range = BotCombat_RangeBandForDistanceSquared(context.enemyDistanceSquared);
	if (range == BotWeaponRangeBand::Melee || range == BotWeaponRangeBand::Close) {
		priority += BOT_COMBAT_CLOSE_RANGE_BONUS;
		reason = "close_enemy";
	}
	if (currentWeaponMetadata != nullptr && range == currentWeaponMetadata->idealRange) {
		priority += BOT_COMBAT_FIRE_RANGE_MATCH_BONUS;
		reason = "weapon_range_match";
	}

	botCombatStatus.fireDecisions++;
	botCombatStatus.lastWeaponItem = context.currentWeaponItem;
	botCombatStatus.lastPriority = priority;
	return {
		.kind = BotCombatDecisionKind::FireWeapon,
		.priority = priority,
		.weaponItem = context.currentWeaponItem,
		.pressAttack = true,
		.reason = reason,
	};
}

BotCombatEnemyFacts BotCombat_BuildEnemyFacts(gentity_t *bot, gentity_t *enemy) {
	botCombatStatus.enemyFactEvaluations++;

	BotCombatEnemyFacts facts{};
	facts.botClientIndex = BotCombat_ClientIndex(bot);
	facts.enemyEntity = BotCombat_EntityNumber(enemy);
	facts.enemyClientIndex = BotCombat_ClientIndex(enemy);
	facts.botValid = BotCombat_IsBotClient(bot) && BotCombat_AliveClient(bot);
	facts.enemyValid = BotCombat_AliveClient(enemy) && enemy != bot && ((enemy->flags & FL_NOTARGET) == 0);
	botCombatStatus.lastBotClient = facts.botClientIndex;
	botCombatStatus.lastEnemyEntity = facts.enemyEntity;
	botCombatStatus.lastEnemyClient = facts.enemyClientIndex;

	if (!facts.botValid) {
		botCombatStatus.enemyFactInvalidBots++;
		return facts;
	}
	if (!facts.enemyValid) {
		botCombatStatus.enemyFactInvalidEnemies++;
		return facts;
	}

	facts.teammate = BotCombat_SameTeam(bot, enemy);
	if (facts.teammate) {
		botCombatStatus.enemyFactTeamSkips++;
		return facts;
	}

	const Vector3 delta = enemy->s.origin - bot->s.origin;
	facts.distanceSquared = BotCombat_ClampDistanceSquared(delta.lengthSquared());
	facts.enemySpawnCount = enemy->spawn_count;
	facts.enemyHealth = enemy->health;
	botCombatStatus.lastEnemyDistanceSquared = facts.distanceSquared;

	botCombatStatus.enemyFactVisibilityChecks++;
	facts.visible = visible(bot, enemy);
	if (facts.visible) {
		botCombatStatus.enemyFactShootabilityChecks++;
		facts.shootable = CanDamage(enemy, bot);
	}

	facts.valid = true;
	return facts;
}

bool BotCombat_FindNearestEnemy(gentity_t *bot, BotCombatEnemyFacts *facts) {
	if (facts == nullptr) {
		return false;
	}

	botCombatStatus.enemyFactSearches++;

	bool found = false;
	BotCombatEnemyFacts best{};
	for (gentity_t *candidate : active_players()) {
		BotCombatEnemyFacts candidateFacts = BotCombat_BuildEnemyFacts(bot, candidate);
		if (!candidateFacts.valid || !candidateFacts.visible) {
			continue;
		}

		if (!found ||
			(candidateFacts.shootable && !best.shootable) ||
			(candidateFacts.shootable == best.shootable &&
			 candidateFacts.distanceSquared < best.distanceSquared)) {
			best = candidateFacts;
			found = true;
		}
	}

	if (!found) {
		return false;
	}

	botCombatStatus.enemyFactSearchHits++;
	botCombatStatus.lastBotClient = best.botClientIndex;
	botCombatStatus.lastEnemyEntity = best.enemyEntity;
	botCombatStatus.lastEnemyClient = best.enemyClientIndex;
	botCombatStatus.lastEnemyDistanceSquared = best.distanceSquared;
	*facts = best;
	return true;
}

BotCombatContext BotCombat_WithEnemyFacts(BotCombatContext context, const BotCombatEnemyFacts &facts) {
	if (!facts.valid) {
		context.hasEnemy = false;
		context.enemyVisible = false;
		context.enemyShootable = false;
		context.enemyDistanceSquared = 0;
		context.enemyClientIndex = -1;
		return context;
	}

	context.hasEnemy = true;
	context.enemyVisible = facts.visible;
	context.enemyShootable = facts.shootable;
	context.enemyDistanceSquared = facts.distanceSquared;
	context.enemyClientIndex = facts.enemyClientIndex;
	return context;
}

void BotCombat_RecordDamageEvent(const gentity_t *attacker, const gentity_t *target, int damage) {
	if (attacker == nullptr || target == nullptr) {
		botCombatStatus.damageInvalidEvents++;
		return;
	}
	if (!BotCombat_IsBotClient(attacker)) {
		botCombatStatus.damageNonBotAttackerSkips++;
		return;
	}
	if (target == attacker) {
		botCombatStatus.damageSelfSkips++;
		return;
	}
	if (target->client == nullptr) {
		botCombatStatus.damageNonClientTargetSkips++;
		return;
	}
	if (BotCombat_SameTeam(attacker, target)) {
		botCombatStatus.damageFriendlySkips++;
		return;
	}
	if (damage <= 0) {
		botCombatStatus.damageZeroSkips++;
		return;
	}

	const int attackerClientIndex = BotCombat_ClientIndex(attacker);
	const int targetClientIndex = BotCombat_ClientIndex(target);
	if (attackerClientIndex < 0 || targetClientIndex < 0) {
		botCombatStatus.damageInvalidEvents++;
		return;
	}

	const Vector3 delta = target->s.origin - attacker->s.origin;
	botCombatStatus.damageEvents++;
	botCombatStatus.lastDamage = damage;
	botCombatStatus.lastDamageAttackerClient = attackerClientIndex;
	botCombatStatus.lastDamageTargetClient = targetClientIndex;
	botCombatStatus.lastDamageAttackerEntity = BotCombat_EntityNumber(attacker);
	botCombatStatus.lastDamageTargetEntity = BotCombat_EntityNumber(target);
	botCombatStatus.lastBotClient = attackerClientIndex;
	botCombatStatus.lastEnemyClient = targetClientIndex;
	botCombatStatus.lastEnemyEntity = BotCombat_EntityNumber(target);
	botCombatStatus.lastEnemyDistanceSquared = BotCombat_ClampDistanceSquared(delta.lengthSquared());
}

const BotCombatStatus &BotCombat_GetStatus() {
	return botCombatStatus;
}

const char *BotCombat_DecisionName(BotCombatDecisionKind kind) {
	switch (kind) {
	case BotCombatDecisionKind::SwitchWeapon:
		return "switch_weapon";
	case BotCombatDecisionKind::FireWeapon:
		return "fire_weapon";
	default:
		return "none";
	}
}

BotWeaponRangeBand BotCombat_RangeBandForDistanceSquared(int distanceSquared) {
	if (distanceSquared <= 0) {
		return BotWeaponRangeBand::Unknown;
	}
	if (distanceSquared <= BOT_COMBAT_MELEE_RANGE_DIST_SQUARED) {
		return BotWeaponRangeBand::Melee;
	}
	if (distanceSquared <= BOT_COMBAT_CLOSE_RANGE_DIST_SQUARED) {
		return BotWeaponRangeBand::Close;
	}
	if (distanceSquared <= BOT_COMBAT_MEDIUM_RANGE_DIST_SQUARED) {
		return BotWeaponRangeBand::Medium;
	}
	return BotWeaponRangeBand::Long;
}

const BotWeaponMetadata *BotCombat_GetWeaponMetadata(int weaponItem) {
	const auto found = std::find_if(
		BOT_WEAPON_METADATA.begin(),
		BOT_WEAPON_METADATA.end(),
		[weaponItem](const BotWeaponMetadata &metadata) {
			return metadata.weaponItem == weaponItem;
		});
	return found != BOT_WEAPON_METADATA.end() ? &(*found) : nullptr;
}

BotWeaponSelectionResult BotCombat_SelectPreferredWeapon(const BotCombatContext &context) {
	const BotWeaponScore current = BotCombat_ScoreWeapon(
		context.currentWeaponItem,
		context.currentWeaponAmmo,
		context.currentWeaponReady,
		context);
	const BotWeaponScore preferred = BotCombat_ScoreWeapon(
		context.preferredWeaponItem,
		context.preferredWeaponAmmo,
		context.preferredWeaponReady,
		context);

	const bool canPrefer = preferred.usable &&
		context.preferredWeaponItem > 0 &&
		context.preferredWeaponItem != context.currentWeaponItem;
	const bool shouldSwitch = canPrefer &&
		(!current.usable ||
			preferred.score >= current.score + BOT_COMBAT_WEAPON_SWITCH_SCORE_MARGIN);
	const bool selectPreferred = shouldSwitch || (!current.usable && preferred.usable);
	const BotWeaponScore &selected = selectPreferred ? preferred : current;
	const int selectedWeaponItem = selectPreferred ? context.preferredWeaponItem : context.currentWeaponItem;

	return {
		.weaponItem = selectedWeaponItem,
		.currentWeaponScore = current.score,
		.preferredWeaponScore = preferred.score,
		.selectedWeaponScore = selected.score,
		.hasKnownWeapon = selected.metadata != nullptr,
		.shouldSwitch = shouldSwitch,
		.preferredWeaponSafe = preferred.safe,
		.metadata = selected.metadata,
		.reason = selected.reason,
	};
}

const char *BotCombat_RangeBandName(BotWeaponRangeBand band) {
	switch (band) {
	case BotWeaponRangeBand::Melee:
		return "melee";
	case BotWeaponRangeBand::Close:
		return "close";
	case BotWeaponRangeBand::Medium:
		return "medium";
	case BotWeaponRangeBand::Long:
		return "long";
	default:
		return "unknown";
	}
}

const char *BotCombat_AttackModelName(BotWeaponAttackModel model) {
	switch (model) {
	case BotWeaponAttackModel::Utility:
		return "utility";
	case BotWeaponAttackModel::Melee:
		return "melee";
	case BotWeaponAttackModel::Hitscan:
		return "hitscan";
	case BotWeaponAttackModel::Projectile:
		return "projectile";
	case BotWeaponAttackModel::Beam:
		return "beam";
	case BotWeaponAttackModel::Deployable:
		return "deployable";
	default:
		return "unknown";
	}
}
