// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "bot_combat.hpp"

#include "../g_local.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr int BOT_COMBAT_ESTIMATE_FINISHER_EFFECTIVE_HEALTH = 45;
constexpr int BOT_COMBAT_ESTIMATE_ARMORED_THRESHOLD = 50;
constexpr int BOT_COMBAT_ESTIMATE_DURABLE_EFFECTIVE_HEALTH = 100;
constexpr int BOT_COMBAT_ESTIMATE_FINISHER_BONUS = 7;
constexpr int BOT_COMBAT_ESTIMATE_ARMOR_PRESSURE_BONUS = 6;
constexpr int BOT_COMBAT_ESTIMATE_UNDERPOWERED_PENALTY = 6;
constexpr int BOT_COMBAT_ESTIMATE_PRESSURE_PRIORITY_THRESHOLD = 50;
constexpr int BOT_COMBAT_DEFAULT_FIELD_OF_VIEW_DEGREES = 110;
constexpr int BOT_COMBAT_MIN_FIELD_OF_VIEW_DEGREES = 30;
constexpr int BOT_COMBAT_MAX_FIELD_OF_VIEW_DEGREES = 180;
constexpr int BOT_COMBAT_DEFAULT_PROJECTILE_LEAD_MILLISECONDS = 1000;
constexpr int BOT_COMBAT_MAX_PROJECTILE_LEAD_MILLISECONDS = 1500;
constexpr float BOT_COMBAT_PROJECTILE_EPSILON = 0.001f;
constexpr float BOT_COMBAT_PROJECTILE_MIN_LEAD_OFFSET_SQUARED = 1.0f;

BotCombatStatus botCombatStatus;

static_assert(static_cast<int>(BotCombatDecisionKind::None) == 0);
static_assert(static_cast<int>(BotCombatAimPolicyFailure::None) == 0);

struct BotAimSkillProfile {
	int reactionDelayMilliseconds;
	int aimSettleMilliseconds;
	int aimErrorTenthsDegrees;
	int trackingNoiseTenthsDegrees;
	int maxTurnDegreesPerFrame;
	int burstShotLimit;
	int burstCommitMilliseconds;
	int burstCooldownMilliseconds;
	int projectileLeadPercent;
};

constexpr std::array<BotAimSkillProfile, 6> BOT_AIM_SKILL_PROFILES = { {
	{ 420, 160, 85, 70, 24, 2, 260, 520, 45 },
	{ 340, 130, 70, 55, 32, 3, 300, 460, 55 },
	{ 275, 105, 55, 42, 42, 4, 340, 390, 70 },
	{ 220, 85, 42, 32, 56, 5, 390, 330, 85 },
	{ 170, 65, 30, 22, 72, 6, 430, 270, 95 },
	{ 130, 45, 20, 14, 90, 8, 480, 220, 100 },
} };

constexpr int Square(int value) {
	return value * value;
}

Vector3 BotCombat_ToVector(const BotCombatVector3 &value) {
	return {
		value.x,
		value.y,
		value.z,
	};
}

BotCombatVector3 BotCombat_FromVector(const Vector3 &value) {
	return {
		.x = value.x,
		.y = value.y,
		.z = value.z,
	};
}

int BotCombat_AbsInt(int value) {
	return value < 0 ? -value : value;
}

int BotCombat_ClampInt(int value, int minimum, int maximum) {
	return std::max(minimum, std::min(value, maximum));
}

int BotCombat_ClampSkill(int skill) {
	return BotCombat_ClampInt(
		skill,
		0,
		static_cast<int>(BOT_AIM_SKILL_PROFILES.size()) - 1);
}

int BotCombat_NormalizeMilliseconds(int milliseconds) {
	return std::max(0, milliseconds);
}

int BotCombat_NormalizeFieldOfViewDegrees(int degrees) {
	if (degrees <= 0) {
		degrees = BOT_COMBAT_DEFAULT_FIELD_OF_VIEW_DEGREES;
	}
	return BotCombat_ClampInt(
		degrees,
		BOT_COMBAT_MIN_FIELD_OF_VIEW_DEGREES,
		BOT_COMBAT_MAX_FIELD_OF_VIEW_DEGREES);
}

int BotCombat_NormalizeProjectileLeadMilliseconds(int milliseconds) {
	if (milliseconds <= 0) {
		milliseconds = BOT_COMBAT_DEFAULT_PROJECTILE_LEAD_MILLISECONDS;
	}
	return BotCombat_ClampInt(
		milliseconds,
		0,
		BOT_COMBAT_MAX_PROJECTILE_LEAD_MILLISECONDS);
}

int BotCombat_NormalizeLeadScalePercent(int percent) {
	if (percent < 0) {
		percent = 100;
	}
	return BotCombat_ClampInt(percent, 0, 125);
}

int BotCombat_MillisecondsForSeconds(float seconds) {
	if (seconds <= 0.0f) {
		return 0;
	}
	return static_cast<int>((seconds * 1000.0f) + 0.5f);
}

float BotCombat_DirectProjectileTravelSeconds(const Vector3 &delta, float projectileSpeed) {
	if (projectileSpeed <= BOT_COMBAT_PROJECTILE_EPSILON) {
		return 0.0f;
	}
	const float distanceSquared = delta.lengthSquared();
	if (distanceSquared <= BOT_COMBAT_PROJECTILE_EPSILON) {
		return 0.0f;
	}
	return std::sqrt(distanceSquared) / projectileSpeed;
}

float BotCombat_ProjectileInterceptSeconds(
	const Vector3 &delta,
	const Vector3 &targetVelocity,
	float projectileSpeed) {
	if (projectileSpeed <= BOT_COMBAT_PROJECTILE_EPSILON) {
		return 0.0f;
	}

	const float fallbackSeconds = BotCombat_DirectProjectileTravelSeconds(delta, projectileSpeed);
	const float targetSpeedSquared = targetVelocity.lengthSquared();
	if (targetSpeedSquared <= BOT_COMBAT_PROJECTILE_EPSILON) {
		return fallbackSeconds;
	}

	const float a = targetSpeedSquared - (projectileSpeed * projectileSpeed);
	const float b = 2.0f * delta.dot(targetVelocity);
	const float c = delta.lengthSquared();

	if (c <= BOT_COMBAT_PROJECTILE_EPSILON) {
		return 0.0f;
	}
	if (std::fabs(a) <= BOT_COMBAT_PROJECTILE_EPSILON) {
		if (std::fabs(b) <= BOT_COMBAT_PROJECTILE_EPSILON) {
			return fallbackSeconds;
		}
		const float linearSeconds = -c / b;
		return linearSeconds > BOT_COMBAT_PROJECTILE_EPSILON ?
			linearSeconds :
			fallbackSeconds;
	}

	const float discriminant = (b * b) - (4.0f * a * c);
	if (discriminant < 0.0f) {
		return fallbackSeconds;
	}

	const float sqrtDiscriminant = std::sqrt(discriminant);
	const float rootA = (-b - sqrtDiscriminant) / (2.0f * a);
	const float rootB = (-b + sqrtDiscriminant) / (2.0f * a);
	float bestSeconds = std::numeric_limits<float>::max();
	if (rootA > BOT_COMBAT_PROJECTILE_EPSILON) {
		bestSeconds = rootA;
	}
	if (rootB > BOT_COMBAT_PROJECTILE_EPSILON && rootB < bestSeconds) {
		bestSeconds = rootB;
	}

	return bestSeconds != std::numeric_limits<float>::max() ?
		bestSeconds :
		fallbackSeconds;
}

int BotCombat_AimErrorDistanceAdjustmentTenths(int distanceSquared) {
	switch (BotCombat_RangeBandForDistanceSquared(distanceSquared)) {
	case BotWeaponRangeBand::Melee:
		return -8;
	case BotWeaponRangeBand::Close:
		return -4;
	case BotWeaponRangeBand::Medium:
		return 3;
	case BotWeaponRangeBand::Long:
		return 8;
	default:
		return 0;
	}
}

BotCombatAimProfile BotCombat_ProfileFromSkillData(int skill, const BotAimSkillProfile &profile) {
	const int maxYawTurnDegrees = std::max(1, profile.maxTurnDegreesPerFrame);
	return {
		.effectiveSkill = skill,
		.reactionDelayMilliseconds = profile.reactionDelayMilliseconds,
		.aimSettleMilliseconds = profile.aimSettleMilliseconds,
		.aimErrorTenthsDegrees = profile.aimErrorTenthsDegrees,
		.trackingNoiseTenthsDegrees = profile.trackingNoiseTenthsDegrees,
		.maxYawTurnDegreesPerFrame = maxYawTurnDegrees,
		.maxPitchTurnDegreesPerFrame = std::max(8, maxYawTurnDegrees / 2),
		.burstShotLimit = std::max(1, profile.burstShotLimit),
		.burstCommitMilliseconds = profile.burstCommitMilliseconds,
		.burstCooldownMilliseconds = profile.burstCooldownMilliseconds,
		.projectileLeadPercent = BotCombat_NormalizeLeadScalePercent(profile.projectileLeadPercent),
	};
}

int BotCombat_RemainingMilliseconds(int required, int actual) {
	return std::max(0, required - actual);
}

int BotCombat_AdjustTrackingNoiseTenths(
	const BotCombatAimProfile &profile,
	int trackedMilliseconds,
	int reactionDelayMilliseconds) {
	int noise = profile.trackingNoiseTenthsDegrees;
	if (trackedMilliseconds >= 1000) {
		noise = noise / 3;
	} else if (trackedMilliseconds >= 500) {
		noise = (noise * 2) / 3;
	} else if (trackedMilliseconds >= reactionDelayMilliseconds) {
		noise = (noise * 3) / 4;
	}
	return std::max(1, noise);
}

void BotCombat_RecordAimPolicyResult(const BotCombatAimPolicyResult &result) {
	botCombatStatus.aimPolicyEvaluations++;
	botCombatStatus.lastAimPolicyFailure = result.failure;
	botCombatStatus.lastAimPolicySkill = result.effectiveSkill;
	botCombatStatus.lastAimPolicyReactionDelayMilliseconds = result.reactionDelayMilliseconds;
	botCombatStatus.lastAimPolicyAimSettleMilliseconds = result.requiredAimSettleMilliseconds;
	botCombatStatus.lastAimPolicyVisibleMilliseconds = result.targetVisibleMilliseconds;
	botCombatStatus.lastAimPolicyTrackedMilliseconds = result.targetTrackedMilliseconds;
	botCombatStatus.lastAimPolicyFovDegrees = result.fieldOfViewDegrees;
	botCombatStatus.lastAimPolicyYawDeltaDegrees = result.yawDeltaDegrees;
	botCombatStatus.lastAimPolicyPitchDeltaDegrees = result.pitchDeltaDegrees;
	botCombatStatus.lastAimPolicyMaxTurnDegrees = result.maxTurnDegreesPerFrame;
	botCombatStatus.lastAimPolicyAimErrorTenthsDegrees = result.aimErrorTenthsDegrees;
	botCombatStatus.lastAimPolicyTrackingNoiseTenthsDegrees = result.trackingNoiseTenthsDegrees;
	botCombatStatus.lastAimPolicyBurstShotLimit = result.burstShotLimit;
	botCombatStatus.lastAimPolicyBurstCooldownMilliseconds = result.burstCooldownMilliseconds;
	botCombatStatus.lastAimPolicyMaxYawTurnDegrees = result.maxYawTurnDegreesPerFrame;
	botCombatStatus.lastAimPolicyMaxPitchTurnDegrees = result.maxPitchTurnDegreesPerFrame;
	botCombatStatus.lastAimPolicyYawTurnOverageDegrees = result.yawTurnOverageDegrees;
	botCombatStatus.lastAimPolicyPitchTurnOverageDegrees = result.pitchTurnOverageDegrees;
	botCombatStatus.lastAimPolicyReactionRemainingMilliseconds =
		result.reactionRemainingMilliseconds;
	botCombatStatus.lastAimPolicyAimSettleRemainingMilliseconds =
		result.aimSettleRemainingMilliseconds;
	botCombatStatus.lastAimPolicyBurstShotsFired = result.burstShotsFired;
	botCombatStatus.lastAimPolicyBurstShotsRemaining = result.burstShotsRemaining;
	botCombatStatus.lastAimPolicyBurstCooldownRemainingMilliseconds =
		result.burstCooldownRemainingMilliseconds;
	botCombatStatus.lastAimPolicyProjectileLeadPercent = result.projectileLeadPercent;

	if (result.mayAim) {
		botCombatStatus.aimPolicyAimAllowed++;
	}
	if (result.mayFire) {
		botCombatStatus.aimPolicyFireAllowed++;
	}

	switch (result.failure) {
	case BotCombatAimPolicyFailure::NoEnemy:
		botCombatStatus.aimPolicyBlocksNoEnemy++;
		break;
	case BotCombatAimPolicyFailure::NotVisible:
		botCombatStatus.aimPolicyBlocksVisibility++;
		break;
	case BotCombatAimPolicyFailure::OutsideFieldOfView:
		botCombatStatus.aimPolicyBlocksFieldOfView++;
		break;
	case BotCombatAimPolicyFailure::NotShootable:
		botCombatStatus.aimPolicyBlocksShootability++;
		break;
	case BotCombatAimPolicyFailure::WeaponNotReady:
		botCombatStatus.aimPolicyBlocksWeaponReady++;
		break;
	case BotCombatAimPolicyFailure::SkillBlocked:
		botCombatStatus.aimPolicyBlocksSkill++;
		break;
	case BotCombatAimPolicyFailure::BurstCooldown:
		botCombatStatus.aimPolicyBlocksBurstCooldown++;
		break;
	case BotCombatAimPolicyFailure::ReactionPending:
		botCombatStatus.aimPolicyBlocksReaction++;
		break;
	case BotCombatAimPolicyFailure::TurnPending:
		botCombatStatus.aimPolicyBlocksTurn++;
		break;
	case BotCombatAimPolicyFailure::AimNotSettled:
		botCombatStatus.aimPolicyBlocksAimSettle++;
		break;
	case BotCombatAimPolicyFailure::BurstLimitReached:
		botCombatStatus.aimPolicyBlocksBurstLimit++;
		break;
	default:
		break;
	}
}

void BotCombat_RecordProjectileLeadResult(const BotCombatProjectileLeadResult &result) {
	botCombatStatus.projectileLeadEvaluations++;
	botCombatStatus.lastProjectileLeadWeaponItem = result.weaponItem;
	botCombatStatus.lastProjectileLeadSpeed = result.projectileSpeed;
	botCombatStatus.lastProjectileLeadTravelMilliseconds = result.travelMilliseconds;
	botCombatStatus.lastProjectileLeadMilliseconds = result.leadMilliseconds;
	botCombatStatus.lastProjectileLeadRawMilliseconds = result.rawLeadMilliseconds;
	botCombatStatus.lastProjectileLeadMaxMilliseconds = result.maxLeadMilliseconds;
	botCombatStatus.lastProjectileLeadScalePercent = result.leadScalePercent;
	botCombatStatus.lastProjectileLeadTargetSpeedSquared = result.targetSpeedSquared;
	botCombatStatus.lastProjectileLeadAimDistanceSquared = result.aimDistanceSquared;
	botCombatStatus.lastProjectileLeadOffsetSquared = result.leadOffsetSquared;
	botCombatStatus.lastProjectileLeadRawOffsetSquared = result.rawLeadOffsetSquared;
	botCombatStatus.lastProjectileLeadClamped = result.leadClamped;

	if (!result.projectileWeapon) {
		botCombatStatus.projectileLeadNoProjectile++;
	}
	if (result.projectileWeapon && !result.hasProjectileSpeed) {
		botCombatStatus.projectileLeadNoSpeed++;
	}
	if (result.projectileWeapon &&
		result.hasProjectileSpeed &&
		result.aimDistanceSquared <= 0) {
		botCombatStatus.projectileLeadInvalidDistance++;
	}
	if (result.usedLead) {
		botCombatStatus.projectileLeadUses++;
	}
}

void BotCombat_RecordLiveAimDecision(const BotCombatLiveAimDecision &decision) {
	botCombatStatus.liveAimEvaluations++;
	botCombatStatus.lastLiveAimWeaponItem = decision.weaponItem;
	botCombatStatus.lastLiveAimPriority = decision.priority;
	botCombatStatus.lastLiveAimReason = decision.reason;
	botCombatStatus.lastLiveAimReactionRemainingMilliseconds =
		decision.aimPolicy.reactionRemainingMilliseconds;
	botCombatStatus.lastLiveAimAimSettleRemainingMilliseconds =
		decision.aimPolicy.aimSettleRemainingMilliseconds;
	botCombatStatus.lastLiveAimProjectileLeadPercent =
		decision.profile.projectileLeadPercent;
	if (decision.mayAim) {
		botCombatStatus.liveAimAimAllowed++;
	}
	if (decision.mayFire) {
		botCombatStatus.liveAimFireAllowed++;
	}
	if (decision.usedAimPolicy &&
		decision.aimPolicy.failure != BotCombatAimPolicyFailure::None) {
		botCombatStatus.liveAimPolicyBlocks++;
	}
	if (decision.usedProjectileLead) {
		botCombatStatus.liveAimProjectileLeadUses++;
	}
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
		.projectileSpeed = 1500,
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
		.projectileSpeed = 1150,
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
		.projectileSpeed = 800,
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
		.projectileSpeed = 1000,
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
		.projectileSpeed = 2000,
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
		.projectileSpeed = 725,
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
		.projectileSpeed = 400,
		.maxProjectileLeadMilliseconds = 1200,
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
		.projectileSpeed = 1000,
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

int BotCombat_ArmorValue(const gclient_t *client) {
	if (client == nullptr) {
		return 0;
	}

	const auto &inventory = client->pers.inventory;
	return std::max(
		std::max(inventory[IT_ARMOR_BODY], inventory[IT_ARMOR_COMBAT]),
		std::max(inventory[IT_ARMOR_JACKET], inventory[IT_ARMOR_SHARD]));
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
	int estimateAdjustment = 0;
	bool usedEnemyEstimate = false;
	bool finisherEstimateBonus = false;
	bool armorPressureEstimateBonus = false;
	bool underpoweredEstimatePenalty = false;
	const char *estimateReason = "none";
	const char *reason = "unusable";
};

struct BotWeaponEstimateAdjustment {
	int scoreAdjustment = 0;
	bool usedEnemyEstimate = false;
	bool finisherBonus = false;
	bool armorPressureBonus = false;
	bool underpoweredPenalty = false;
	const char *reason = "none";
};

int BotCombat_EstimatedHealthValue(int value) {
	return std::max(0, value);
}

int BotCombat_EstimatedEffectiveHealth(const BotCombatContext &context) {
	return BotCombat_EstimatedHealthValue(context.enemyHealthEstimate) +
		BotCombat_EstimatedHealthValue(context.enemyArmorEstimate);
}

bool BotCombat_CanUseEnemyEstimate(const BotCombatContext &context) {
	return context.enemyEstimateKnown && context.enemyHealthEstimate > 0;
}

bool BotCombat_IsEstimateFinisherWeapon(
	const BotWeaponMetadata &metadata,
	BotWeaponRangeBand range) {
	if (metadata.attackModel == BotWeaponAttackModel::Hitscan ||
		metadata.attackModel == BotWeaponAttackModel::Beam) {
		return true;
	}
	if (metadata.attackModel == BotWeaponAttackModel::Melee) {
		return range == BotWeaponRangeBand::Melee ||
			range == BotWeaponRangeBand::Close;
	}
	return metadata.attackModel == BotWeaponAttackModel::Projectile &&
		!metadata.splashDamage;
}

bool BotCombat_IsEstimateArmorPressureWeapon(
	const BotWeaponMetadata &metadata,
	BotWeaponRangeBand range) {
	if (metadata.attackModel == BotWeaponAttackModel::Melee) {
		return range == BotWeaponRangeBand::Melee;
	}
	if (metadata.attackModel != BotWeaponAttackModel::Hitscan &&
		metadata.attackModel != BotWeaponAttackModel::Projectile &&
		metadata.attackModel != BotWeaponAttackModel::Beam) {
		return false;
	}
	return metadata.priority >= BOT_COMBAT_ESTIMATE_PRESSURE_PRIORITY_THRESHOLD;
}

bool BotCombat_IsEstimateUnderpoweredWeapon(const BotWeaponMetadata &metadata) {
	if (metadata.attackModel == BotWeaponAttackModel::Utility ||
		metadata.attackModel == BotWeaponAttackModel::Deployable) {
		return true;
	}
	return metadata.priority < BOT_COMBAT_ESTIMATE_PRESSURE_PRIORITY_THRESHOLD;
}

BotWeaponEstimateAdjustment BotCombat_BuildEstimateWeaponAdjustment(
	const BotWeaponMetadata &metadata,
	const BotCombatContext &context,
	BotWeaponRangeBand range) {
	if (!BotCombat_CanUseEnemyEstimate(context)) {
		return {};
	}

	const int armorEstimate = BotCombat_EstimatedHealthValue(context.enemyArmorEstimate);
	const int effectiveHealth = BotCombat_EstimatedEffectiveHealth(context);
	if (effectiveHealth <= BOT_COMBAT_ESTIMATE_FINISHER_EFFECTIVE_HEALTH &&
		BotCombat_IsEstimateFinisherWeapon(metadata, range)) {
		return {
			.scoreAdjustment = BOT_COMBAT_ESTIMATE_FINISHER_BONUS,
			.usedEnemyEstimate = true,
			.finisherBonus = true,
			.reason = "enemy_estimate_finisher",
		};
	}

	if (armorEstimate >= BOT_COMBAT_ESTIMATE_ARMORED_THRESHOLD &&
		effectiveHealth >= BOT_COMBAT_ESTIMATE_DURABLE_EFFECTIVE_HEALTH) {
		if (BotCombat_IsEstimateUnderpoweredWeapon(metadata)) {
			return {
				.scoreAdjustment = -BOT_COMBAT_ESTIMATE_UNDERPOWERED_PENALTY,
				.usedEnemyEstimate = true,
				.underpoweredPenalty = true,
				.reason = "enemy_estimate_underpowered",
			};
		}
		if (BotCombat_IsEstimateArmorPressureWeapon(metadata, range)) {
			return {
				.scoreAdjustment = BOT_COMBAT_ESTIMATE_ARMOR_PRESSURE_BONUS,
				.usedEnemyEstimate = true,
				.armorPressureBonus = true,
				.reason = "enemy_estimate_armor_pressure",
			};
		}
	}

	return {};
}

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

	const BotWeaponEstimateAdjustment estimateAdjustment =
		BotCombat_BuildEstimateWeaponAdjustment(*metadata, context, range);
	if (estimateAdjustment.scoreAdjustment != 0) {
		score += estimateAdjustment.scoreAdjustment;
		reason = estimateAdjustment.reason;
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
		.estimateAdjustment = estimateAdjustment.scoreAdjustment,
		.usedEnemyEstimate = estimateAdjustment.usedEnemyEstimate,
		.finisherEstimateBonus = estimateAdjustment.finisherBonus,
		.armorPressureEstimateBonus = estimateAdjustment.armorPressureBonus,
		.underpoweredEstimatePenalty = estimateAdjustment.underpoweredPenalty,
		.estimateReason = estimateAdjustment.reason,
		.reason = reason,
	};
}

int BotCombat_FirePriorityForContext(
	const BotCombatContext &context,
	const BotWeaponMetadata *metadata,
	const char **reasonOut) {
	int priority = BOT_COMBAT_FIRE_PRIORITY;
	const char *reason = "shootable_enemy";
	const BotWeaponRangeBand range = BotCombat_RangeBandForDistanceSquared(context.enemyDistanceSquared);
	if (range == BotWeaponRangeBand::Melee || range == BotWeaponRangeBand::Close) {
		priority += BOT_COMBAT_CLOSE_RANGE_BONUS;
		reason = "close_enemy";
	}
	if (metadata != nullptr && range == metadata->idealRange) {
		priority += BOT_COMBAT_FIRE_RANGE_MATCH_BONUS;
		reason = "weapon_range_match";
	}

	if (reasonOut != nullptr) {
		*reasonOut = reason;
	}
	return priority;
}

void BotCombat_RecordSelection(const BotCombatContext &context, const BotWeaponSelectionResult &selection) {
	botCombatStatus.weaponSelectionEvaluations++;
	botCombatStatus.lastCurrentWeaponScore = selection.currentWeaponScore;
	botCombatStatus.lastPreferredWeaponScore = selection.preferredWeaponScore;
	botCombatStatus.lastSelectedWeaponScore = selection.selectedWeaponScore;
	botCombatStatus.lastWeaponEstimateAdjustment = selection.selectedEstimateAdjustment;
	botCombatStatus.lastPreferredWeaponItem = context.preferredWeaponItem;
	botCombatStatus.lastSelectionReason = selection.reason;
	botCombatStatus.lastEstimateSelectionReason = selection.estimateReason;
	botCombatStatus.lastEnemyHealthEstimate = context.enemyEstimateKnown ?
		BotCombat_EstimatedHealthValue(context.enemyHealthEstimate) : 0;
	botCombatStatus.lastEnemyArmorEstimate = context.enemyEstimateKnown ?
		BotCombat_EstimatedHealthValue(context.enemyArmorEstimate) : 0;
	botCombatStatus.lastEnemyEffectiveHealthEstimate = context.enemyEstimateKnown ?
		BotCombat_EstimatedEffectiveHealth(context) : 0;
	botCombatStatus.lastEnemyRangeBand = BotCombat_RangeBandForDistanceSquared(context.enemyDistanceSquared);
	if (selection.usedEnemyEstimate) {
		botCombatStatus.weaponSelectionEstimateUses++;
	}
	if (selection.finisherEstimateBonus) {
		botCombatStatus.weaponSelectionFinisherBonuses++;
	}
	if (selection.armorPressureEstimateBonus) {
		botCombatStatus.weaponSelectionArmorPressureBonuses++;
	}
	if (selection.underpoweredEstimatePenalty) {
		botCombatStatus.weaponSelectionUnderpoweredPenalties++;
	}

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
		context.preferredWeaponItem != context.currentWeaponItem &&
		weaponSelection.weaponItem == context.preferredWeaponItem) {
		botCombatStatus.weaponSwitchDecisions++;
		botCombatStatus.lastWeaponItem = context.preferredWeaponItem;
		botCombatStatus.lastPriority = BOT_COMBAT_SWITCH_WEAPON_PRIORITY;
		return {
			.kind = BotCombatDecisionKind::SwitchWeapon,
			.priority = BOT_COMBAT_SWITCH_WEAPON_PRIORITY,
			.weaponItem = context.preferredWeaponItem,
			.reason = weaponSelection.reason,
		};
	}

	if (!context.enemyVisible || !context.enemyShootable) {
		botCombatStatus.blockedSight++;
		return {};
	}

	if (context.aimPolicyEnabled) {
		const BotCombatAimPolicyResult aimPolicy =
			BotCombat_EvaluateAimPolicy(context, context.aimPolicy);
		if (!aimPolicy.mayFire) {
			botCombatStatus.withheldFire++;
			botCombatStatus.lastWeaponItem = context.currentWeaponItem;
			botCombatStatus.lastPriority = 0;
			botCombatStatus.lastSelectionReason =
				BotCombat_AimPolicyFailureName(aimPolicy.failure);
			return {};
		}
	} else if (!context.currentWeaponReady || !context.skillAllowsFire) {
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

	const char *reason = "shootable_enemy";
	const int priority = BotCombat_FirePriorityForContext(
		context,
		currentWeaponMetadata,
		&reason);

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
	facts.enemyArmor = BotCombat_ArmorValue(enemy->client);
	botCombatStatus.lastEnemyDistanceSquared = facts.distanceSquared;
	botCombatStatus.lastEnemyHealth = facts.enemyHealth;
	botCombatStatus.lastEnemyArmor = facts.enemyArmor;
	botCombatStatus.lastEnemyEffectiveHealth = facts.enemyHealth + facts.enemyArmor;

	botCombatStatus.enemyFactVisibilityChecks++;
	facts.visible = visible(bot, enemy);
	if (facts.visible) {
		botCombatStatus.enemyHealthObservations++;
		if (facts.enemyArmor > 0) {
			botCombatStatus.enemyArmorObservations++;
		}
	}
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
	context.enemyEstimateKnown = facts.visible;
	context.enemyHealthEstimate = facts.visible ? facts.enemyHealth : 0;
	context.enemyArmorEstimate = facts.visible ? facts.enemyArmor : 0;
	return context;
}

void BotCombat_RecordDamageEvent(
	const gentity_t *attacker,
	const gentity_t *target,
	int damage,
	int healthDamage,
	int armorDamage) {
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
	const int resolvedHealthDamage = healthDamage >= 0 ? healthDamage : damage;
	const int resolvedArmorDamage = armorDamage >= 0 ? armorDamage : 0;
	botCombatStatus.damageSequence++;
	botCombatStatus.damageEvents++;
	botCombatStatus.lastDamageSequence = botCombatStatus.damageSequence;
	botCombatStatus.lastDamage = damage;
	botCombatStatus.lastDamageHealth = resolvedHealthDamage;
	botCombatStatus.lastDamageArmor = resolvedArmorDamage;
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

BotCombatAimProfile BotCombat_AimProfileForSkill(int skill) {
	const int effectiveSkill = BotCombat_ClampSkill(skill);
	return BotCombat_ProfileFromSkillData(
		effectiveSkill,
		BOT_AIM_SKILL_PROFILES[effectiveSkill]);
}

BotCombatAimPolicyResult BotCombat_EvaluateAimPolicy(
	const BotCombatContext &context,
	const BotCombatAimPolicyFrame &frame) {
	BotCombatAimProfile profile = BotCombat_AimProfileForSkill(frame.skill);
	const int reactionDelayMilliseconds = frame.reactionDelayMilliseconds >= 0 ?
		frame.reactionDelayMilliseconds :
		profile.reactionDelayMilliseconds;
	profile.reactionDelayMilliseconds =
		BotCombat_NormalizeMilliseconds(reactionDelayMilliseconds);
	const int fieldOfViewDegrees = BotCombat_NormalizeFieldOfViewDegrees(frame.fieldOfViewDegrees);
	const int yawDeltaDegrees = BotCombat_AbsInt(frame.yawDeltaDegrees);
	const int pitchDeltaDegrees = BotCombat_AbsInt(frame.pitchDeltaDegrees);
	const int targetVisibleMilliseconds =
		BotCombat_NormalizeMilliseconds(frame.targetVisibleMilliseconds);
	const int targetTrackedMilliseconds = std::max(
		BotCombat_NormalizeMilliseconds(frame.targetTrackedMilliseconds),
		targetVisibleMilliseconds);
	const int aimSettledMilliseconds =
		BotCombat_NormalizeMilliseconds(frame.aimSettledMilliseconds);
	const int halfFieldOfViewDegrees = fieldOfViewDegrees / 2;
	const int pitchFieldOfViewDegrees = std::max(12, halfFieldOfViewDegrees);
	const bool targetInFieldOfView = frame.targetInFieldOfView &&
		yawDeltaDegrees <= halfFieldOfViewDegrees &&
		pitchDeltaDegrees <= pitchFieldOfViewDegrees;
	const bool withinTurnLimit =
		yawDeltaDegrees <= profile.maxYawTurnDegreesPerFrame &&
		pitchDeltaDegrees <= profile.maxPitchTurnDegreesPerFrame;
	const int burstCooldownRemainingMilliseconds =
		BotCombat_NormalizeMilliseconds(frame.burstCooldownRemainingMilliseconds);
	const int burstShotsFired = std::max(0, frame.burstShotsFired);

	BotCombatAimPolicyResult result{};
	result.targetInFieldOfView = targetInFieldOfView;
	result.withinTurnLimit = withinTurnLimit;
	result.effectiveSkill = profile.effectiveSkill;
	result.reactionDelayMilliseconds = profile.reactionDelayMilliseconds;
	result.requiredAimSettleMilliseconds = profile.aimSettleMilliseconds;
	result.targetVisibleMilliseconds = targetVisibleMilliseconds;
	result.targetTrackedMilliseconds = targetTrackedMilliseconds;
	result.aimSettledMilliseconds = aimSettledMilliseconds;
	result.fieldOfViewDegrees = fieldOfViewDegrees;
	result.yawDeltaDegrees = yawDeltaDegrees;
	result.pitchDeltaDegrees = pitchDeltaDegrees;
	result.maxTurnDegreesPerFrame = profile.maxYawTurnDegreesPerFrame;
	result.trackingNoiseTenthsDegrees = BotCombat_AdjustTrackingNoiseTenths(
		profile,
		targetTrackedMilliseconds,
		result.reactionDelayMilliseconds);
	result.aimErrorTenthsDegrees = std::max(
		1,
		profile.aimErrorTenthsDegrees +
			BotCombat_AimErrorDistanceAdjustmentTenths(context.enemyDistanceSquared));
	result.burstShotLimit = profile.burstShotLimit;
	result.burstCommitMilliseconds = profile.burstCommitMilliseconds;
	result.burstCooldownMilliseconds = profile.burstCooldownMilliseconds;
	result.profile = profile;
	result.reactionReady = targetVisibleMilliseconds >= result.reactionDelayMilliseconds;
	result.aimSettled = aimSettledMilliseconds >= result.requiredAimSettleMilliseconds;
	result.burstReady =
		burstCooldownRemainingMilliseconds <= 0 &&
		burstShotsFired < result.burstShotLimit;
	result.maxYawTurnDegreesPerFrame = profile.maxYawTurnDegreesPerFrame;
	result.maxPitchTurnDegreesPerFrame = profile.maxPitchTurnDegreesPerFrame;
	result.yawTurnOverageDegrees =
		std::max(0, yawDeltaDegrees - result.maxYawTurnDegreesPerFrame);
	result.pitchTurnOverageDegrees =
		std::max(0, pitchDeltaDegrees - result.maxPitchTurnDegreesPerFrame);
	result.reactionRemainingMilliseconds = BotCombat_RemainingMilliseconds(
		result.reactionDelayMilliseconds,
		targetVisibleMilliseconds);
	result.aimSettleRemainingMilliseconds = BotCombat_RemainingMilliseconds(
		result.requiredAimSettleMilliseconds,
		aimSettledMilliseconds);
	result.burstShotsFired = burstShotsFired;
	result.burstShotsRemaining =
		std::max(0, result.burstShotLimit - burstShotsFired);
	result.burstCooldownRemainingMilliseconds = burstCooldownRemainingMilliseconds;
	result.projectileLeadPercent = profile.projectileLeadPercent;

	auto finish = [&result](BotCombatAimPolicyFailure failure, bool mayAim, bool mayFire) {
		result.failure = failure;
		result.mayAim = mayAim;
		result.mayFire = mayFire;
		BotCombat_RecordAimPolicyResult(result);
		return result;
	};

	if (!context.hasEnemy) {
		return finish(BotCombatAimPolicyFailure::NoEnemy, false, false);
	}
	if (!context.enemyVisible) {
		return finish(BotCombatAimPolicyFailure::NotVisible, false, false);
	}
	if (!targetInFieldOfView) {
		return finish(BotCombatAimPolicyFailure::OutsideFieldOfView, false, false);
	}
	if (!context.enemyShootable) {
		return finish(BotCombatAimPolicyFailure::NotShootable, true, false);
	}
	if (!context.currentWeaponReady) {
		return finish(BotCombatAimPolicyFailure::WeaponNotReady, true, false);
	}
	if (!context.skillAllowsFire) {
		return finish(BotCombatAimPolicyFailure::SkillBlocked, true, false);
	}
	if (burstCooldownRemainingMilliseconds > 0) {
		return finish(BotCombatAimPolicyFailure::BurstCooldown, true, false);
	}
	if (!result.reactionReady) {
		return finish(BotCombatAimPolicyFailure::ReactionPending, true, false);
	}
	if (!withinTurnLimit) {
		return finish(BotCombatAimPolicyFailure::TurnPending, true, false);
	}
	if (!result.aimSettled) {
		return finish(BotCombatAimPolicyFailure::AimNotSettled, true, false);
	}
	if (burstShotsFired >= result.burstShotLimit) {
		return finish(BotCombatAimPolicyFailure::BurstLimitReached, true, false);
	}

	return finish(BotCombatAimPolicyFailure::None, true, true);
}

const char *BotCombat_AimPolicyFailureName(BotCombatAimPolicyFailure failure) {
	switch (failure) {
	case BotCombatAimPolicyFailure::NoEnemy:
		return "no_enemy";
	case BotCombatAimPolicyFailure::NotVisible:
		return "not_visible";
	case BotCombatAimPolicyFailure::OutsideFieldOfView:
		return "outside_field_of_view";
	case BotCombatAimPolicyFailure::NotShootable:
		return "not_shootable";
	case BotCombatAimPolicyFailure::WeaponNotReady:
		return "weapon_not_ready";
	case BotCombatAimPolicyFailure::SkillBlocked:
		return "skill_blocked";
	case BotCombatAimPolicyFailure::BurstCooldown:
		return "burst_cooldown";
	case BotCombatAimPolicyFailure::ReactionPending:
		return "reaction_pending";
	case BotCombatAimPolicyFailure::TurnPending:
		return "turn_pending";
	case BotCombatAimPolicyFailure::AimNotSettled:
		return "aim_not_settled";
	case BotCombatAimPolicyFailure::BurstLimitReached:
		return "burst_limit_reached";
	default:
		return "none";
	}
}

BotCombatProjectileLeadResult BotCombat_BuildProjectileLead(
	const BotCombatProjectileLeadFrame &frame) {
	BotCombatProjectileLeadResult result{};
	result.weaponItem = frame.weaponItem;
	result.aimPoint = frame.targetOrigin;
	result.rawLeadPoint = frame.targetOrigin;
	result.targetVelocityKnown = frame.targetVelocityKnown;
	result.leadScalePercent = BotCombat_NormalizeLeadScalePercent(frame.leadScalePercent);

	const BotWeaponMetadata *metadata = BotCombat_GetWeaponMetadata(frame.weaponItem);
	result.projectileWeapon = metadata != nullptr && metadata->projectile;
	result.projectileSpeed = frame.projectileSpeed > 0 ?
		frame.projectileSpeed :
		BotCombat_ProjectileSpeedForWeapon(frame.weaponItem);
	result.hasProjectileSpeed = result.projectileSpeed > 0;

	const Vector3 shooterOrigin = BotCombat_ToVector(frame.shooterOrigin);
	const Vector3 targetOrigin = BotCombat_ToVector(frame.targetOrigin);
	const Vector3 delta = targetOrigin - shooterOrigin;
	result.aimDistanceSquared = BotCombat_ClampDistanceSquared(delta.lengthSquared());

	Vector3 leadVelocity = BotCombat_ToVector(frame.targetVelocity);
	if (!frame.allowVerticalLead) {
		leadVelocity.z = 0.0f;
	}
	result.targetSpeedSquared = BotCombat_ClampDistanceSquared(leadVelocity.lengthSquared());

	const int metadataMaxLeadMilliseconds =
		metadata != nullptr ? metadata->maxProjectileLeadMilliseconds : 0;
	result.maxLeadMilliseconds = BotCombat_NormalizeProjectileLeadMilliseconds(
		frame.maxLeadMilliseconds > 0 ?
			frame.maxLeadMilliseconds :
			metadataMaxLeadMilliseconds);

	if (!result.projectileWeapon || !result.hasProjectileSpeed || result.aimDistanceSquared <= 0) {
		BotCombat_RecordProjectileLeadResult(result);
		return result;
	}

	result.valid = true;
	const float maxLeadSeconds = static_cast<float>(result.maxLeadMilliseconds) / 1000.0f;
	const float rawLeadSeconds = frame.targetVelocityKnown ?
		BotCombat_ProjectileInterceptSeconds(
			delta,
			leadVelocity,
			static_cast<float>(result.projectileSpeed)) :
		BotCombat_DirectProjectileTravelSeconds(
			delta,
			static_cast<float>(result.projectileSpeed));
	float leadSeconds = rawLeadSeconds;
	leadSeconds = std::max(0.0f, std::min(leadSeconds, maxLeadSeconds));
	result.rawLeadMilliseconds = BotCombat_MillisecondsForSeconds(rawLeadSeconds);
	result.leadClamped = rawLeadSeconds > maxLeadSeconds + BOT_COMBAT_PROJECTILE_EPSILON;

	result.travelMilliseconds = BotCombat_MillisecondsForSeconds(
		BotCombat_DirectProjectileTravelSeconds(
			delta,
			static_cast<float>(result.projectileSpeed)));
	result.leadMilliseconds = BotCombat_MillisecondsForSeconds(leadSeconds);

	if (frame.targetVelocityKnown && result.targetSpeedSquared > 0 && leadSeconds > 0.0f) {
		const Vector3 rawLeadPoint = targetOrigin + (leadVelocity * leadSeconds);
		result.rawLeadPoint = BotCombat_FromVector(rawLeadPoint);
		const Vector3 rawLeadOffset = rawLeadPoint - targetOrigin;
		result.rawLeadOffsetSquared =
			BotCombat_ClampDistanceSquared(rawLeadOffset.lengthSquared());
		const Vector3 scaledLeadPoint =
			targetOrigin + (rawLeadOffset * (static_cast<float>(result.leadScalePercent) / 100.0f));
		const Vector3 leadOffset = scaledLeadPoint - targetOrigin;
		result.leadOffsetSquared =
			BotCombat_ClampDistanceSquared(leadOffset.lengthSquared());
		if (result.leadOffsetSquared >=
			static_cast<int>(BOT_COMBAT_PROJECTILE_MIN_LEAD_OFFSET_SQUARED)) {
			result.aimPoint = BotCombat_FromVector(scaledLeadPoint);
			result.usedLead = true;
		}
	}

	BotCombat_RecordProjectileLeadResult(result);
	return result;
}

BotCombatLiveAimDecision BotCombat_BuildLiveAimDecision(
	const BotCombatContext &context,
	const BotCombatLiveAimFrame &frame) {
	BotCombatLiveAimDecision decision{};
	decision.usedAimPolicy = frame.useAimPolicy;
	decision.weaponItem = frame.projectileLead.weaponItem > 0 ?
		frame.projectileLead.weaponItem :
		context.currentWeaponItem;
	decision.aimPoint = frame.projectileLead.targetOrigin;
	decision.profile = BotCombat_AimProfileForSkill(frame.aimPolicy.skill);

	if (frame.useAimPolicy) {
		decision.aimPolicy = frame.hasAimPolicyResult ?
			frame.aimPolicyResult :
			BotCombat_EvaluateAimPolicy(context, frame.aimPolicy);
		decision.profile = decision.aimPolicy.profile.projectileLeadPercent > 0 ?
			decision.aimPolicy.profile :
			BotCombat_AimProfileForSkill(decision.aimPolicy.effectiveSkill);
		decision.aimPolicy.profile = decision.profile;
		decision.aimPolicy.projectileLeadPercent = decision.profile.projectileLeadPercent;
		decision.mayAim = decision.aimPolicy.mayAim;
		decision.mayFire = decision.aimPolicy.mayFire;
		if (!decision.mayAim || !decision.mayFire) {
			decision.reason = BotCombat_AimPolicyFailureName(decision.aimPolicy.failure);
		}
	} else {
		decision.aimPolicy.profile = decision.profile;
		decision.aimPolicy.effectiveSkill = decision.profile.effectiveSkill;
		decision.aimPolicy.projectileLeadPercent = decision.profile.projectileLeadPercent;
		decision.mayAim = context.hasEnemy &&
			context.enemyVisible &&
			frame.aimPolicy.targetInFieldOfView;
		decision.mayFire = decision.mayAim &&
			context.enemyShootable &&
			context.currentWeaponReady &&
			context.skillAllowsFire;
		if (!context.hasEnemy) {
			decision.reason = "no_enemy";
		} else if (!context.enemyVisible) {
			decision.reason = "not_visible";
		} else if (!frame.aimPolicy.targetInFieldOfView) {
			decision.reason = "outside_field_of_view";
		} else if (!context.enemyShootable) {
			decision.reason = "not_shootable";
		} else if (!context.currentWeaponReady) {
			decision.reason = "weapon_not_ready";
		} else if (!context.skillAllowsFire) {
			decision.reason = "skill_blocked";
		}
	}

	if (decision.mayAim && frame.useProjectileLead) {
		BotCombatProjectileLeadFrame leadFrame = frame.projectileLead;
		if (leadFrame.weaponItem <= 0) {
			leadFrame.weaponItem = decision.weaponItem;
		}
		if (leadFrame.leadScalePercent < 0) {
			leadFrame.leadScalePercent = decision.profile.projectileLeadPercent;
		}
		decision.projectileLead = BotCombat_BuildProjectileLead(leadFrame);
		decision.usedProjectileLead = decision.projectileLead.usedLead;
		decision.aimPoint = decision.projectileLead.aimPoint;
	}

	if (decision.mayFire) {
		const BotWeaponMetadata *metadata = BotCombat_GetWeaponMetadata(decision.weaponItem);
		const char *fireReason = "shootable_enemy";
		decision.priority = BotCombat_FirePriorityForContext(context, metadata, &fireReason);
		decision.pressAttack = true;
		decision.reason = decision.usedProjectileLead ? "projectile_lead" : fireReason;
	}

	BotCombat_RecordLiveAimDecision(decision);
	return decision;
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

int BotCombat_ProjectileSpeedForWeapon(int weaponItem) {
	const BotWeaponMetadata *metadata = BotCombat_GetWeaponMetadata(weaponItem);
	return metadata != nullptr ? metadata->projectileSpeed : 0;
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
	const BotWeaponScore *estimateSource = selected.usedEnemyEstimate ? &selected : nullptr;
	if (estimateSource == nullptr && preferred.usedEnemyEstimate) {
		estimateSource = &preferred;
	}
	if (estimateSource == nullptr && current.usedEnemyEstimate) {
		estimateSource = &current;
	}

	return {
		.weaponItem = selectedWeaponItem,
		.currentWeaponScore = current.score,
		.preferredWeaponScore = preferred.score,
		.selectedWeaponScore = selected.score,
		.currentEstimateAdjustment = current.estimateAdjustment,
		.preferredEstimateAdjustment = preferred.estimateAdjustment,
		.selectedEstimateAdjustment = selected.estimateAdjustment,
		.usedEnemyEstimate = current.usedEnemyEstimate || preferred.usedEnemyEstimate,
		.finisherEstimateBonus = selected.finisherEstimateBonus,
		.armorPressureEstimateBonus = selected.armorPressureEstimateBonus,
		.underpoweredEstimatePenalty = current.underpoweredEstimatePenalty ||
			preferred.underpoweredEstimatePenalty,
		.hasKnownWeapon = selected.metadata != nullptr,
		.shouldSwitch = shouldSwitch,
		.preferredWeaponSafe = preferred.safe,
		.metadata = selected.metadata,
		.reason = selected.reason,
		.estimateReason = estimateSource != nullptr ?
			estimateSource->estimateReason : "none",
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
