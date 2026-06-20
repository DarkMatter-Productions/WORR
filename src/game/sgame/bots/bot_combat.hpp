// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

struct gentity_t;

struct BotCombatVector3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

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
	int projectileSpeed = 0;
	int maxProjectileLeadMilliseconds = 0;
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

enum class BotCombatAimPolicyFailure {
	None,
	NoEnemy,
	NotVisible,
	OutsideFieldOfView,
	NotShootable,
	WeaponNotReady,
	SkillBlocked,
	BurstCooldown,
	ReactionPending,
	TurnPending,
	AimNotSettled,
	BurstLimitReached,
};

struct BotCombatAimProfile {
	int effectiveSkill = 0;
	int reactionDelayMilliseconds = 0;
	int aimSettleMilliseconds = 0;
	int aimErrorTenthsDegrees = 0;
	int trackingNoiseTenthsDegrees = 0;
	int maxYawTurnDegreesPerFrame = 0;
	int maxPitchTurnDegreesPerFrame = 0;
	int burstShotLimit = 0;
	int burstCommitMilliseconds = 0;
	int burstCooldownMilliseconds = 0;
	int projectileLeadPercent = 100;
};

struct BotWeaponSelectionResult {
	int weaponItem = 0;
	int currentWeaponScore = 0;
	int preferredWeaponScore = 0;
	int selectedWeaponScore = 0;
	int currentEstimateAdjustment = 0;
	int preferredEstimateAdjustment = 0;
	int selectedEstimateAdjustment = 0;
	bool usedEnemyEstimate = false;
	bool finisherEstimateBonus = false;
	bool armorPressureEstimateBonus = false;
	bool underpoweredEstimatePenalty = false;
	bool hasKnownWeapon = false;
	bool shouldSwitch = false;
	bool preferredWeaponSafe = true;
	const BotWeaponMetadata *metadata = nullptr;
	const char *reason = "none";
	const char *estimateReason = "none";
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
	int enemyArmor = 0;
	int distanceSquared = 0;
};

// Caller-owned fairness facts. The policy is intentionally deterministic:
// callers provide timing/FOV deltas, then consume the returned error/noise
// metadata instead of granting perfect aim from raw visibility alone.
struct BotCombatAimPolicyFrame {
	bool targetInFieldOfView = true;
	int skill = 3;
	int targetVisibleMilliseconds = 0;
	int targetTrackedMilliseconds = 0;
	int aimSettledMilliseconds = 0;
	int reactionDelayMilliseconds = -1;
	int fieldOfViewDegrees = 110;
	int yawDeltaDegrees = 0;
	int pitchDeltaDegrees = 0;
	int burstShotsFired = 0;
	int burstCooldownRemainingMilliseconds = 0;
};

struct BotCombatAimPolicyResult {
	bool mayAim = false;
	bool mayFire = false;
	bool targetInFieldOfView = false;
	bool withinTurnLimit = false;
	BotCombatAimPolicyFailure failure = BotCombatAimPolicyFailure::None;
	int effectiveSkill = 0;
	int reactionDelayMilliseconds = 0;
	int requiredAimSettleMilliseconds = 0;
	int targetVisibleMilliseconds = 0;
	int targetTrackedMilliseconds = 0;
	int aimSettledMilliseconds = 0;
	int fieldOfViewDegrees = 0;
	int yawDeltaDegrees = 0;
	int pitchDeltaDegrees = 0;
	int maxTurnDegreesPerFrame = 0;
	int aimErrorTenthsDegrees = 0;
	int trackingNoiseTenthsDegrees = 0;
	int burstShotLimit = 0;
	int burstCommitMilliseconds = 0;
	int burstCooldownMilliseconds = 0;
	BotCombatAimProfile profile{};
	bool reactionReady = false;
	bool aimSettled = false;
	bool burstReady = false;
	int maxYawTurnDegreesPerFrame = 0;
	int maxPitchTurnDegreesPerFrame = 0;
	int yawTurnOverageDegrees = 0;
	int pitchTurnOverageDegrees = 0;
	int reactionRemainingMilliseconds = 0;
	int aimSettleRemainingMilliseconds = 0;
	int burstShotsFired = 0;
	int burstShotsRemaining = 0;
	int burstCooldownRemainingMilliseconds = 0;
	int projectileLeadPercent = 100;
};

// Straight-line projectile lead inputs. Ballistic/deployable weapons may still
// be projectile weapons, but only weapons with a projectile speed produce lead.
struct BotCombatProjectileLeadFrame {
	int weaponItem = 0;
	int projectileSpeed = 0;
	int maxLeadMilliseconds = 0;
	int leadScalePercent = -1;
	bool targetVelocityKnown = false;
	bool allowVerticalLead = true;
	BotCombatVector3 shooterOrigin = {};
	BotCombatVector3 targetOrigin = {};
	BotCombatVector3 targetVelocity = {};
};

struct BotCombatProjectileLeadResult {
	bool valid = false;
	bool projectileWeapon = false;
	bool hasProjectileSpeed = false;
	bool targetVelocityKnown = false;
	bool usedLead = false;
	int weaponItem = 0;
	int projectileSpeed = 0;
	int travelMilliseconds = 0;
	int leadMilliseconds = 0;
	int targetSpeedSquared = 0;
	int aimDistanceSquared = 0;
	int leadOffsetSquared = 0;
	bool leadClamped = false;
	int rawLeadMilliseconds = 0;
	int maxLeadMilliseconds = 0;
	int leadScalePercent = 100;
	int rawLeadOffsetSquared = 0;
	BotCombatVector3 aimPoint = {};
	BotCombatVector3 rawLeadPoint = {};
};

// Brain-owned aiming can call this helper after it has perception, view-angle,
// settle, and burst facts. It does not mutate commands or view angles.
struct BotCombatLiveAimFrame {
	bool useAimPolicy = true;
	bool hasAimPolicyResult = false;
	bool useProjectileLead = true;
	BotCombatAimPolicyFrame aimPolicy{};
	BotCombatAimPolicyResult aimPolicyResult{};
	BotCombatProjectileLeadFrame projectileLead{};
};

struct BotCombatLiveAimDecision {
	bool mayAim = false;
	bool mayFire = false;
	bool pressAttack = false;
	bool usedAimPolicy = false;
	bool usedProjectileLead = false;
	int weaponItem = 0;
	int priority = 0;
	BotCombatVector3 aimPoint = {};
	BotCombatAimProfile profile{};
	BotCombatAimPolicyResult aimPolicy{};
	BotCombatProjectileLeadResult projectileLead{};
	const char *reason = "none";
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
	bool aimPolicyEnabled = false;
	bool enemyEstimateKnown = false;
	int currentWeaponItem = 0;
	int preferredWeaponItem = 0;
	int currentWeaponAmmo = 0;
	int preferredWeaponAmmo = 0;
	int enemyDistanceSquared = 0;
	int enemyClientIndex = -1;
	int enemyHealthEstimate = 0;
	int enemyArmorEstimate = 0;
	BotCombatAimPolicyFrame aimPolicy{};
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
	int enemyHealthObservations = 0;
	int enemyArmorObservations = 0;
	int enemyAcquisitions = 0;
	int enemyVisible = 0;
	int enemyShootable = 0;
	int blockedSight = 0;
	int weaponSwitchDecisions = 0;
	int fireDecisions = 0;
	int withheldFire = 0;
	int aimPolicyEvaluations = 0;
	int aimPolicyAimAllowed = 0;
	int aimPolicyFireAllowed = 0;
	int aimPolicyBlocksNoEnemy = 0;
	int aimPolicyBlocksVisibility = 0;
	int aimPolicyBlocksFieldOfView = 0;
	int aimPolicyBlocksShootability = 0;
	int aimPolicyBlocksWeaponReady = 0;
	int aimPolicyBlocksSkill = 0;
	int aimPolicyBlocksBurstCooldown = 0;
	int aimPolicyBlocksReaction = 0;
	int aimPolicyBlocksTurn = 0;
	int aimPolicyBlocksAimSettle = 0;
	int aimPolicyBlocksBurstLimit = 0;
	int projectileLeadEvaluations = 0;
	int projectileLeadUses = 0;
	int projectileLeadNoProjectile = 0;
	int projectileLeadNoSpeed = 0;
	int projectileLeadInvalidDistance = 0;
	int liveAimEvaluations = 0;
	int liveAimAimAllowed = 0;
	int liveAimFireAllowed = 0;
	int liveAimPolicyBlocks = 0;
	int liveAimProjectileLeadUses = 0;
	int weaponSelectionEvaluations = 0;
	int weaponSelectionEstimateUses = 0;
	int weaponSelectionFinisherBonuses = 0;
	int weaponSelectionArmorPressureBonuses = 0;
	int weaponSelectionUnderpoweredPenalties = 0;
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
	int damageSequence = 0;
	int lastDamageSequence = 0;
	int lastDamageHealth = 0;
	int lastDamageArmor = 0;
	int lastWeaponItem = 0;
	int lastPreferredWeaponItem = 0;
	int lastPriority = 0;
	int lastBotClient = -1;
	int lastEnemyEntity = -1;
	int lastEnemyDistanceSquared = 0;
	int lastEnemyClient = -1;
	int lastEnemyHealth = 0;
	int lastEnemyArmor = 0;
	int lastEnemyEffectiveHealth = 0;
	int lastEnemyHealthEstimate = 0;
	int lastEnemyArmorEstimate = 0;
	int lastEnemyEffectiveHealthEstimate = 0;
	int lastDamage = 0;
	int lastDamageAttackerClient = -1;
	int lastDamageTargetClient = -1;
	int lastDamageAttackerEntity = -1;
	int lastDamageTargetEntity = -1;
	int lastCurrentWeaponScore = 0;
	int lastPreferredWeaponScore = 0;
	int lastSelectedWeaponScore = 0;
	int lastWeaponEstimateAdjustment = 0;
	int lastWeaponMetadataPriority = 0;
	int lastWeaponAmmoPerShot = 0;
	BotCombatAimPolicyFailure lastAimPolicyFailure = BotCombatAimPolicyFailure::None;
	int lastAimPolicySkill = 0;
	int lastAimPolicyReactionDelayMilliseconds = 0;
	int lastAimPolicyAimSettleMilliseconds = 0;
	int lastAimPolicyVisibleMilliseconds = 0;
	int lastAimPolicyTrackedMilliseconds = 0;
	int lastAimPolicyFovDegrees = 0;
	int lastAimPolicyYawDeltaDegrees = 0;
	int lastAimPolicyPitchDeltaDegrees = 0;
	int lastAimPolicyMaxTurnDegrees = 0;
	int lastAimPolicyAimErrorTenthsDegrees = 0;
	int lastAimPolicyTrackingNoiseTenthsDegrees = 0;
	int lastAimPolicyBurstShotLimit = 0;
	int lastAimPolicyBurstCooldownMilliseconds = 0;
	int lastAimPolicyMaxYawTurnDegrees = 0;
	int lastAimPolicyMaxPitchTurnDegrees = 0;
	int lastAimPolicyYawTurnOverageDegrees = 0;
	int lastAimPolicyPitchTurnOverageDegrees = 0;
	int lastAimPolicyReactionRemainingMilliseconds = 0;
	int lastAimPolicyAimSettleRemainingMilliseconds = 0;
	int lastAimPolicyBurstShotsFired = 0;
	int lastAimPolicyBurstShotsRemaining = 0;
	int lastAimPolicyBurstCooldownRemainingMilliseconds = 0;
	int lastAimPolicyProjectileLeadPercent = 100;
	int lastProjectileLeadWeaponItem = 0;
	int lastProjectileLeadSpeed = 0;
	int lastProjectileLeadTravelMilliseconds = 0;
	int lastProjectileLeadMilliseconds = 0;
	int lastProjectileLeadRawMilliseconds = 0;
	int lastProjectileLeadMaxMilliseconds = 0;
	int lastProjectileLeadScalePercent = 100;
	int lastProjectileLeadTargetSpeedSquared = 0;
	int lastProjectileLeadAimDistanceSquared = 0;
	int lastProjectileLeadOffsetSquared = 0;
	int lastProjectileLeadRawOffsetSquared = 0;
	int lastLiveAimWeaponItem = 0;
	int lastLiveAimPriority = 0;
	int lastLiveAimReactionRemainingMilliseconds = 0;
	int lastLiveAimAimSettleRemainingMilliseconds = 0;
	int lastLiveAimProjectileLeadPercent = 100;
	BotWeaponRangeBand lastEnemyRangeBand = BotWeaponRangeBand::Unknown;
	BotWeaponAttackModel lastWeaponAttackModel = BotWeaponAttackModel::Unknown;
	bool lastWeaponSplashDamage = false;
	bool lastWeaponSelfDamageRisk = false;
	bool lastProjectileLeadClamped = false;
	const char *lastLiveAimReason = "none";
	const char *lastSelectionReason = "none";
	const char *lastEstimateSelectionReason = "none";
};

void BotCombat_ResetStatus();
BotCombatDecision BotCombat_Evaluate(const BotCombatContext &context);
BotCombatEnemyFacts BotCombat_BuildEnemyFacts(gentity_t *bot, gentity_t *enemy);
bool BotCombat_FindNearestEnemy(gentity_t *bot, BotCombatEnemyFacts *facts);
BotCombatContext BotCombat_WithEnemyFacts(BotCombatContext context, const BotCombatEnemyFacts &facts);
void BotCombat_RecordDamageEvent(
	const gentity_t *attacker,
	const gentity_t *target,
	int damage,
	int healthDamage = -1,
	int armorDamage = -1);
const BotCombatStatus &BotCombat_GetStatus();
const char *BotCombat_DecisionName(BotCombatDecisionKind kind);
BotCombatAimProfile BotCombat_AimProfileForSkill(int skill);
BotCombatAimPolicyResult BotCombat_EvaluateAimPolicy(
	const BotCombatContext &context,
	const BotCombatAimPolicyFrame &frame);
const char *BotCombat_AimPolicyFailureName(BotCombatAimPolicyFailure failure);
BotCombatProjectileLeadResult BotCombat_BuildProjectileLead(
	const BotCombatProjectileLeadFrame &frame);
BotCombatLiveAimDecision BotCombat_BuildLiveAimDecision(
	const BotCombatContext &context,
	const BotCombatLiveAimFrame &frame);
BotWeaponRangeBand BotCombat_RangeBandForDistanceSquared(int distanceSquared);
const BotWeaponMetadata *BotCombat_GetWeaponMetadata(int weaponItem);
int BotCombat_ProjectileSpeedForWeapon(int weaponItem);
BotWeaponSelectionResult BotCombat_SelectPreferredWeapon(const BotCombatContext &context);
const char *BotCombat_RangeBandName(BotWeaponRangeBand band);
const char *BotCombat_AttackModelName(BotWeaponAttackModel model);
