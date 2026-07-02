// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "bot_items.hpp"
#include "bot_nav.hpp"
#include "bot_objectives.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>

namespace {

constexpr uint32_t BOT_NAV_ROUTE_REFRESH_FRAMES = 4;
constexpr uint32_t BOT_NAV_ITEM_DESIRABILITY_STAGGER_FRAMES = 4;
constexpr uint32_t BOT_NAV_GOAL_BLACKLIST_FRAMES = 96;
constexpr uint32_t BOT_NAV_STUCK_REPATH_COOLDOWN_FRAMES = 4;
constexpr uint32_t BOT_NAV_STUCK_RECOVERY_FRAMES = 6;
constexpr uint32_t BOT_NAV_INTERACTION_RETRY_FRAMES = 12;
constexpr uint32_t BOT_NAV_INTERACTION_RETRY_COOLDOWN_FRAMES = 24;
constexpr int BOT_NAV_STUCK_FRAME_THRESHOLD = 8;
constexpr float BOT_NAV_STUCK_RECOVERY_LEGACY_FORWARD_MOVE = -80.0f;
constexpr float BOT_NAV_STUCK_RECOVERY_LEGACY_SIDE_MOVE = 140.0f;
constexpr float BOT_NAV_STUCK_RECOVERY_MOVE_SPEED = 160.0f;
constexpr float BOT_NAV_STUCK_RECOVERY_PROBE_DISTANCE = 72.0f;
constexpr float BOT_NAV_STUCK_RECOVERY_PROBE_MIN_FRACTION = 0.25f;
constexpr float BOT_NAV_STUCK_RECOVERY_AWAY_BONUS = 0.08f;
constexpr float BOT_NAV_STUCK_RECOVERY_SIDE_BONUS = 0.02f;
constexpr float BOT_NAV_TARGET_REACHED_DIST_SQUARED = 16.0f * 16.0f;
constexpr float BOT_NAV_GOAL_REACHED_DIST_SQUARED = 48.0f * 48.0f;
constexpr float BOT_NAV_PICKUP_RECORD_DIST_SQUARED = 96.0f * 96.0f;
constexpr float BOT_NAV_ROUTE_DRIFT_DIST_SQUARED = 96.0f * 96.0f;
constexpr float BOT_NAV_ROUTE_TARGET_STABILIZE_DIST_SQUARED = 24.0f * 24.0f;
constexpr float BOT_NAV_ROUTE_TARGET_STABLE_MIN_DIST_SQUARED = 64.0f * 64.0f;
constexpr float BOT_NAV_ROUTE_PROGRESS_TARGET_SHIFT_DIST_SQUARED = 32.0f * 32.0f;
constexpr float BOT_NAV_CORNER_CUT_MIN_DIST_SQUARED = 48.0f * 48.0f;
constexpr float BOT_NAV_CORNER_CUT_MAX_DIST_SQUARED = 256.0f * 256.0f;
constexpr float BOT_NAV_CORNER_CUT_GROUND_PROBE_DEPTH = STEPSIZE_BELOW + 8.0f;
constexpr float BOT_NAV_CORNER_CUT_MIN_GROUND_NORMAL_Z = 0.7f;
constexpr float BOT_NAV_CORNER_CUT_TRACE_FRACTION_SCALE = 1000.0f;
constexpr float BOT_NAV_STUCK_MIN_PROGRESS_DELTA_SQUARED = 16.0f;
constexpr float BOT_NAV_INTERACTION_NEAR_DIST_SQUARED = 192.0f * 192.0f;
constexpr float BOT_NAV_ELEVATOR_INTERACTION_DIST_SQUARED = 384.0f * 384.0f;
constexpr int BOT_NAV_INTERACTION_PROGRESSION_PREFERENCE_SLACK_SQUARED =
	96 * 96;
constexpr uint32_t BOT_NAV_INTERACTION_PROGRESSION_POST_FRAMES = 24;
constexpr uint32_t BOT_NAV_INTERACTION_PROGRESSION_SUPPRESS_FRAMES = 48;
constexpr float BOT_NAV_DEBUG_ROUTE_LIFETIME = 0.10f;
constexpr float BOT_NAV_DEBUG_CROSS_SIZE = 10.0f;
constexpr float BOT_NAV_DEBUG_LABEL_SIZE = 6.0f;

constexpr int BOT_NAV_TRAVEL_WALK = 2;
constexpr int BOT_NAV_TRAVEL_CROUCH = 3;
constexpr int BOT_NAV_TRAVEL_SWIM = 8;
constexpr int BOT_NAV_TRAVEL_WATER_JUMP = 9;
constexpr int BOT_NAV_TRAVEL_TELEPORT = 10;
constexpr int BOT_NAV_TRAVEL_ELEVATOR = 11;
constexpr int BOT_NAV_NATURAL_CROUCH_MASK = 1 << 0;
constexpr int BOT_NAV_NATURAL_SWIM_MASK = 1 << 1;
constexpr int BOT_NAV_NATURAL_WATER_JUMP_MASK = 1 << 2;

enum class BotNavRefreshReason {
	None,
	Invalid,
	Cadence,
	TargetReached,
	GoalReached,
	OriginDrift,
	PreferredGoal,
	Stuck,
};

enum class BotNavGoalClearReason {
	None = 0,
	Reached = 1,
	RouteFallback = 2,
	Reset = 3,
	ItemUnavailable = 4,
	Blacklisted = 5,
};

enum class BotNavStuckReason {
	None = 0,
	NoGoalProgress = 1,
};

enum class BotNavFailedGoalReason {
	None = 0,
	RouteFallback = 1,
	ItemUnavailable = 2,
	Blacklisted = 3,
};

enum class BotNavInteractionAction {
	None = 0,
	Wait = 1,
	Use = 2,
	WaitUse = 3,
};

enum class BotNavInteractionKind {
	None = 0,
	Door = 1,
	Button = 2,
	Platform = 3,
	Train = 4,
	Water = 5,
	Trigger = 6,
	Mover = 7,
	Teleporter = 8,
	Hazard = 9,
};

enum class BotNavInteractionArrivalSource {
	None = 0,
	DestinationOffset = 1,
	DestinationDirect = 2,
	MoverEndpoint = 3,
};

enum class BotNavNaturalMovementSupportReason {
	Unknown = 0,
	Supported = 1,
	AasNotLoaded = 2,
	NoRouteStart = 3,
	InvalidRouteAreas = 4,
};

enum class BotNavItemFocusMode {
	None,
	Health,
	Armor,
	Ammo,
	HealthArmor,
};

enum class BotNavBlackboardGoalType {
	None = 0,
	Item = 1,
	Position = 2,
	TravelType = 3,
	RouteGoal = 4,
};

enum class BotNavItemRoleScope {
	None,
	FreeForAll,
	CaptureTheFlag,
	Team,
};

enum class BotNavCornerCutSkipReason {
	None = 0,
	Invalid = 1,
	UnsupportedTravel = 2,
	NoCandidate = 3,
	NoSafeTrace = 4,
};

struct BotNavItemGoalCandidate {
	int entityNumber = -1;
	int spawnCount = 0;
	item_id_t item = IT_NULL;
	int area = 0;
	int score = 0;
	bool ffaItemRoleValid = false;
	int ffaItemRoleMode = 0;
	int ffaItemRoleRole = 0;
	int ffaItemRoleLane = 0;
	int ffaItemRoleCategory = 0;
	int ffaItemRoleItemRole = 0;
	int ffaItemRolePriority = 0;
	int ffaItemRoleScoreBoost = 0;
	int ffaItemRoleProfileItemBonus = 0;
	bool ctfItemRoleValid = false;
	int ctfItemRoleMode = 0;
	int ctfItemRoleRole = 0;
	int ctfItemRoleLane = 0;
	int ctfItemRoleCategory = 0;
	int ctfItemRoleItemRole = 0;
	int ctfItemRolePriority = 0;
	int ctfItemRoleScoreBoost = 0;
	int ctfItemRoleProfileItemBonus = 0;
	bool teamItemRoleValid = false;
	int teamItemRoleMode = 0;
	int teamItemRoleRole = 0;
	int teamItemRoleLane = 0;
	int teamItemRoleCategory = 0;
	int teamItemRoleItemRole = 0;
	int teamItemRolePriority = 0;
	int teamItemRoleScoreBoost = 0;
	int teamItemRoleProfileItemBonus = 0;
	bool teamResourceDenialValid = false;
	int teamResourceDenialMode = 0;
	int teamResourceDenialRole = 0;
	int teamResourceDenialLane = 0;
	int teamResourceDenialCategory = 0;
	int teamResourceDenialIntent = 0;
	int teamResourceDenialPriority = 0;
	int teamResourceDenialScoreBoost = 0;
	int teamResourceDenialProfileItemBonus = 0;
	Vector3 origin = vec3_origin;
};

struct BotNavRouteSlot {
	bool valid = false;
	uint32_t nextRefreshFrame = 0;
	uint32_t nextItemDesirabilityFrame = 0;
	uint32_t nextStuckRepathFrame = 0;
	uint32_t recoveryUntilFrame = 0;
	uint32_t interactionUntilFrame = 0;
	uint32_t nextInteractionFrame = 0;
	int recoverySideSign = 0;
	bool recoveryMoveValid = false;
	Vector3 recoveryMoveDirection = vec3_origin;
	int recoveryProbeCandidate = -1;
	int recoveryProbeFraction = 0;
	int interactionAction = 0;
	int interactionKind = 0;
	int interactionEntityNumber = -1;
	int interactionEntitySpawnCount = 0;
	int interactionProgressionScore = 0;
	int interactionProgressionPreferred = 0;
	int interactionTargetEntity = 0;
	int interactionProgressionTarget = 0;
	int interactionTargetLink = 0;
	int interactionNamedTarget = 0;
	int interactionKeyEntity = 0;
	int interactionKeyItem = 0;
	int interactionKeyLock = 0;
	int interactionKeyRequiredItem = 0;
	int interactionCommandFrames = 0;
	uint32_t postInteractionUntilFrame = 0;
	int postInteractionEntityNumber = -1;
	int postInteractionEntitySpawnCount = 0;
	int postInteractionProgressionScore = 0;
	uint32_t suppressedInteractionUntilFrame = 0;
	int suppressedInteractionEntityNumber = -1;
	int suppressedInteractionEntitySpawnCount = 0;
	int suppressedInteractionProgressionScore = 0;
	int completedProgressionEntityNumber = -1;
	int completedProgressionEntitySpawnCount = 0;
	int completedProgressionCount = 0;
	int completedProgressionDistinctCount = 0;
	int persistentGoalArea = 0;
	bool persistentGoalIsPosition = false;
	bool persistentGoalIsInteractionArrival = false;
	int persistentInteractionArrivalEntityNumber = -1;
	int persistentInteractionArrivalKind = 0;
	int persistentInteractionArrivalAction = 0;
	int persistentInteractionArrivalArea = 0;
	Vector3 persistentInteractionArrivalPosition = vec3_origin;
	int persistentGoalTravelType = 0;
	int persistentGoalEntityNumber = -1;
	int persistentGoalEntitySpawnCount = 0;
	item_id_t persistentGoalItem = IT_NULL;
	int persistentGoalHealthAtAssignment = 0;
	int persistentGoalArmorAtAssignment = 0;
	Vector3 persistentPositionGoal = vec3_origin;
	uint32_t blacklistedGoalUntilFrame = 0;
	int blacklistedGoalEntityNumber = -1;
	int blacklistedGoalEntitySpawnCount = 0;
	item_id_t blacklistedGoalItem = IT_NULL;
	int progressGoalArea = 0;
	float lastProgressDistanceSquared = -1.0f;
	float lastProgressTargetDistanceSquared = -1.0f;
	Vector3 progressRouteTarget = vec3_origin;
	int stagnantProgressFrames = 0;
	int lastStuckReason = 0;
	int lastStuckDistanceSq = 0;
	int lastStuckProgressDelta = 0;
	int lastFailedGoalReason = 0;
	int lastFailedGoalArea = 0;
	int lastFailedGoalEntityNumber = -1;
	item_id_t lastFailedGoalItem = IT_NULL;
	bool cachedItemGoalValid = false;
	BotNavItemGoalCandidate cachedItemGoal{};
	Vector3 origin = vec3_origin;
	BotLibAdapterRouteSteer route{};
};

gentity_t *BotNavClientEntity(int clientIndex);
bool BotNavInteractionArrivalKindHasMoverEndpoint(int kind);
bool BotNavRecordMoverRideState(
	const gentity_t *bot,
	int interactionEntityNumber,
	BotNavMoverRidePhase phase,
	const BotNavInteractionGoal *goal);

struct BotNavPositionGoalCandidate {
	int area = 0;
	int entityNumber = -1;
	int action = 0;
	int distanceSquared = 0;
	Vector3 origin = vec3_origin;
	bool interactionArrivalGoal = false;
	int interactionArrivalEntityNumber = -1;
	int interactionArrivalKind = 0;
	int interactionArrivalAction = 0;
};

struct BotNavInteractionCandidate {
	int entityNumber = -1;
	int action = 0;
	int kind = 0;
	int distanceSquared = 0;
	int progressionScore = 0;
	int progressionPreferred = 0;
	int targetEntity = 0;
	int progressionTarget = 0;
	int targetLink = 0;
	int namedTarget = 0;
	int keyEntity = 0;
	int keyItem = 0;
	int keyLock = 0;
	int keyRequiredItem = 0;
};

std::array<BotNavRouteSlot, MAX_CLIENTS> botNavRouteSlots{};
BotNavRouteStatus botNavRouteStatus;
bool botNavNaturalMovementSupportChecked = false;

void BotNavRecordInteractionArrivalRouteGoal(
	const BotNavPositionGoalCandidate &candidate,
	const gentity_t *bot);
void BotNavRecordPersistentInteractionArrivalRouteGoal(
	const BotNavRouteSlot &slot,
	const gentity_t *bot);

bool BotNavRocketJumpAllowed() {
	static cvar_t *allowRocketJump = nullptr;
	if (allowRocketJump == nullptr && gi.cvar != nullptr) {
		allowRocketJump = gi.cvar("bot_allow_rocketjump", "0", CVAR_NOFLAGS);
	}
	return allowRocketJump != nullptr && allowRocketJump->integer > 0;
}

bool BotNavBehaviorPolicyEnabled() {
	static cvar_t *behaviorEnable = nullptr;
	if (behaviorEnable == nullptr && gi.cvar != nullptr) {
		behaviorEnable = gi.cvar("bot_behavior_enable", "1", CVAR_NOFLAGS);
	}
	return behaviorEnable != nullptr && behaviorEnable->integer > 0;
}

bool BotNavCoopShareLoopEnabled() {
	static cvar_t *shareLoop = nullptr;
	if (shareLoop == nullptr && gi.cvar != nullptr) {
		shareLoop = gi.cvar("bot_coop_share_loop", "0", CVAR_NOFLAGS);
	}
	return shareLoop != nullptr && shareLoop->integer > 0;
}

bool BotNavCoopResourceShareEnabled() {
	static cvar_t *resourceShare = nullptr;
	if (resourceShare == nullptr && gi.cvar != nullptr) {
		resourceShare = gi.cvar("bot_coop_resource_share", "0", CVAR_NOFLAGS);
	}
	return BotNavBehaviorPolicyEnabled() ||
		BotNavCoopShareLoopEnabled() ||
		(resourceShare != nullptr && resourceShare->integer > 0);
}

bool BotNavMatchItemPolicyEnabled() {
	static cvar_t *matchItemPolicy = nullptr;
	if (matchItemPolicy == nullptr && gi.cvar != nullptr) {
		matchItemPolicy = gi.cvar("bot_match_item_policy", "0", CVAR_NOFLAGS);
	}
	return BotNavBehaviorPolicyEnabled() ||
		(matchItemPolicy != nullptr && matchItemPolicy->integer > 0);
}

bool BotNavDuelLivePacingEnabled() {
	static cvar_t *duelLivePacing = nullptr;
	if (duelLivePacing == nullptr && gi.cvar != nullptr) {
		duelLivePacing = gi.cvar("bot_duel_live_pacing", "0", CVAR_NOFLAGS);
	}
	return duelLivePacing != nullptr && duelLivePacing->integer > 0;
}

bool BotNavFfaItemRolesEnabled() {
	static cvar_t *ffaItemRoles = nullptr;
	if (ffaItemRoles == nullptr && gi.cvar != nullptr) {
		ffaItemRoles = gi.cvar("bot_ffa_item_roles", "0", CVAR_NOFLAGS);
	}
	return (ffaItemRoles != nullptr && ffaItemRoles->integer > 0) ||
		BotNavDuelLivePacingEnabled() ||
		BotNavMatchItemPolicyEnabled();
}

bool BotNavCtfItemRolesEnabled() {
	static cvar_t *ctfItemRoles = nullptr;
	if (ctfItemRoles == nullptr && gi.cvar != nullptr) {
		ctfItemRoles = gi.cvar("bot_ctf_item_roles", "0", CVAR_NOFLAGS);
	}
	return (ctfItemRoles != nullptr && ctfItemRoles->integer > 0) ||
		BotNavMatchItemPolicyEnabled();
}

bool BotNavTeamItemRolesEnabled() {
	static cvar_t *teamItemRoles = nullptr;
	if (teamItemRoles == nullptr && gi.cvar != nullptr) {
		teamItemRoles = gi.cvar("bot_team_item_roles", "0", CVAR_NOFLAGS);
	}
	return (teamItemRoles != nullptr && teamItemRoles->integer > 0) ||
		BotNavMatchItemPolicyEnabled();
}

bool BotNavTeamResourceDenialEnabled() {
	static cvar_t *teamResourceDenial = nullptr;
	if (teamResourceDenial == nullptr && gi.cvar != nullptr) {
		teamResourceDenial = gi.cvar("bot_team_resource_denial", "0", CVAR_NOFLAGS);
	}
	return (teamResourceDenial != nullptr && teamResourceDenial->integer > 0) ||
		BotNavMatchItemPolicyEnabled();
}

uint64_t BotNavRouteNowNs() {
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t BotNavRouteElapsedNs(uint64_t startNs) {
	const uint64_t endNs = BotNavRouteNowNs();
	return endNs >= startNs ? endNs - startNs : 0;
}

void BotNavRecordRouteQueryCpu(uint64_t elapsedNs, bool success) {
	botNavRouteStatus.routeQueryCpuNs += elapsedNs;
	botNavRouteStatus.routeQueryCpuSamples++;
	botNavRouteStatus.routeQueryCpuMaxNs =
		std::max(botNavRouteStatus.routeQueryCpuMaxNs, elapsedNs);
	if (!success) {
		botNavRouteStatus.routeQueryCpuFailNs += elapsedNs;
		botNavRouteStatus.routeQueryCpuFailSamples++;
	}
}

void BotNavRecordRouteReuseCpu(uint64_t elapsedNs) {
	botNavRouteStatus.routeReuseCpuNs += elapsedNs;
	botNavRouteStatus.routeReuseCpuSamples++;
}

bool BotNavCvarStringDisabled(const char *value) {
	return value == nullptr ||
		value[0] == '\0' ||
		Q_strcasecmp(value, "0") == 0 ||
		Q_strcasecmp(value, "false") == 0 ||
		Q_strcasecmp(value, "none") == 0 ||
		Q_strcasecmp(value, "off") == 0;
}

BotNavItemFocusMode BotNavSmokeItemFocusMode() {
	static cvar_t *itemFocus = nullptr;
	if (itemFocus == nullptr && gi.cvar != nullptr) {
		itemFocus = gi.cvar("bot_frame_command_smoke_item_focus", "0", CVAR_NOFLAGS);
	}
	if (itemFocus == nullptr) {
		return BotNavItemFocusMode::None;
	}

	const char *value = itemFocus->string;
	if (BotNavCvarStringDisabled(value)) {
		return BotNavItemFocusMode::None;
	}
	if (itemFocus->integer > 0 ||
		Q_strcasecmp(value, "health_armor") == 0 ||
		Q_strcasecmp(value, "healtharmor") == 0 ||
		Q_strcasecmp(value, "health+armor") == 0 ||
		Q_strcasecmp(value, "health,armor") == 0) {
		return BotNavItemFocusMode::HealthArmor;
	}

	switch (BotItems_FocusFromString(value)) {
	case BotItemFocus::Health:
		return BotNavItemFocusMode::Health;
	case BotItemFocus::Armor:
		return BotNavItemFocusMode::Armor;
	case BotItemFocus::Ammo:
		return BotNavItemFocusMode::Ammo;
	default:
		return BotNavItemFocusMode::None;
	}
}

bool BotNavItemFocusAllowsKind(BotNavItemFocusMode mode, BotItemUtilityKind kind) {
	switch (mode) {
	case BotNavItemFocusMode::Health:
		return kind == BotItemUtilityKind::Health;
	case BotNavItemFocusMode::Armor:
		return kind == BotItemUtilityKind::Armor;
	case BotNavItemFocusMode::Ammo:
		return kind == BotItemUtilityKind::Ammo;
	case BotNavItemFocusMode::HealthArmor:
		return kind == BotItemUtilityKind::Health || kind == BotItemUtilityKind::Armor;
	case BotNavItemFocusMode::None:
	default:
		return true;
	}
}

BotItemFocus BotNavItemFocusForKind(BotNavItemFocusMode mode, BotItemUtilityKind kind) {
	if ((mode == BotNavItemFocusMode::Health || mode == BotNavItemFocusMode::HealthArmor) &&
		kind == BotItemUtilityKind::Health) {
		return BotItemFocus::Health;
	}
	if ((mode == BotNavItemFocusMode::Armor || mode == BotNavItemFocusMode::HealthArmor) &&
		kind == BotItemUtilityKind::Armor) {
		return BotItemFocus::Armor;
	}
	if (mode == BotNavItemFocusMode::Ammo && kind == BotItemUtilityKind::Ammo) {
		return BotItemFocus::Ammo;
	}
	return BotItemFocus::None;
}

bool BotNavCoopResourceKindCanBeReserved(BotObjectiveItemCategory category) {
	return category == BotObjectiveItemCategory::Health ||
		category == BotObjectiveItemCategory::Armor ||
		category == BotObjectiveItemCategory::Ammo ||
		category == BotObjectiveItemCategory::Weapon ||
		category == BotObjectiveItemCategory::Utility;
}

bool BotNavTeamResourceKindCanDenyEnemy(BotObjectiveItemCategory category) {
	return category == BotObjectiveItemCategory::Weapon ||
		category == BotObjectiveItemCategory::Powerup ||
		category == BotObjectiveItemCategory::Tech ||
		category == BotObjectiveItemCategory::Utility;
}

BotItemContext BotNavApplyCoopResourceSharePolicy(
	const BotObjectiveMatchPolicy &matchPolicy,
	const BotObjectiveCoopPolicy &coopPolicy,
	BotObjectiveItemCategory category,
	BotItemContext context) {
	if (!BotNavCoopResourceShareEnabled() || !coopPolicy.valid || !coopPolicy.coopMode) {
		return context;
	}

	const bool teammateNeedsItem =
		coopPolicy.shareResources &&
		BotNavCoopResourceKindCanBeReserved(category);
	const BotObjectiveResourceContext resourceContext =
		BotObjectives_BuildResourceContext(
			matchPolicy,
			coopPolicy,
			category,
			context.candidateScore,
			context.candidateUseful,
			teammateNeedsItem,
			false);
	const BotObjectiveResourcePolicy resourcePolicy =
		BotObjectives_EvaluateResourcePolicy(resourceContext);
	if (resourcePolicy.valid &&
		resourcePolicy.shouldReserve &&
		!resourcePolicy.mayPickup) {
		context.candidateReserved = true;
	}

	return context;
}

BotItemContext BotNavApplyTeamResourceDenialPolicy(
	const BotObjectiveMatchPolicy &matchPolicy,
	BotObjectiveItemCategory category,
	BotItemContext context,
	BotObjectiveResourcePolicy *selectedPolicy) {
	if (selectedPolicy != nullptr) {
		*selectedPolicy = {};
	}
	if (!BotNavTeamResourceDenialEnabled() ||
		matchPolicy.mode != BotObjectiveMatchMode::TeamDeathmatch ||
		!BotNavTeamResourceKindCanDenyEnemy(category)) {
		return context;
	}

	botNavRouteStatus.teamResourceDenialEvaluations++;
	BotObjectiveCoopPolicy coopPolicy{};
	const BotObjectiveResourceContext resourceContext =
		BotObjectives_BuildResourceContext(
			matchPolicy,
			coopPolicy,
			category,
			context.candidateScore,
			context.candidateUseful,
			false,
			true);
	const BotObjectiveResourcePolicy resourcePolicy =
		BotObjectives_EvaluateResourcePolicy(resourceContext);
	if (selectedPolicy != nullptr) {
		*selectedPolicy = resourcePolicy;
	}
	if (!resourcePolicy.valid || !resourcePolicy.denyEnemyPickup) {
		botNavRouteStatus.teamResourceDenialInvalidSkips++;
		return context;
	}

	botNavRouteStatus.teamResourceDenialPolicyDenies++;
	const int scoreBoost = std::max(0, resourcePolicy.priority);
	if (scoreBoost > 0) {
		context.candidateScore += scoreBoost;
		botNavRouteStatus.teamResourceDenialScoreBoosts++;
	}

	return context;
}

BotItemContext BotNavApplyItemRolePolicy(
	const BotObjectiveMatchPolicy &matchPolicy,
	BotObjectiveItemCategory category,
	BotItemContext context,
	BotNavItemRoleScope scope,
	BotObjectiveItemRolePolicy *selectedPolicy) {
	if (selectedPolicy != nullptr) {
		*selectedPolicy = {};
	}
	if (scope == BotNavItemRoleScope::None) {
		return context;
	}

	if (scope == BotNavItemRoleScope::FreeForAll) {
		botNavRouteStatus.ffaItemRoleEvaluations++;
	} else if (scope == BotNavItemRoleScope::CaptureTheFlag) {
		botNavRouteStatus.ctfItemRoleEvaluations++;
	} else {
		botNavRouteStatus.teamItemRoleEvaluations++;
	}

	const BotObjectiveItemRolePolicy policy =
		BotObjectives_EvaluateItemRolePolicy(
			matchPolicy,
			category,
			context.candidateScore);
	if (selectedPolicy != nullptr) {
		*selectedPolicy = policy;
	}
	if (!policy.valid) {
		if (scope == BotNavItemRoleScope::FreeForAll) {
			botNavRouteStatus.ffaItemRoleInvalidSkips++;
		} else if (scope == BotNavItemRoleScope::CaptureTheFlag) {
			botNavRouteStatus.ctfItemRoleInvalidSkips++;
		} else {
			botNavRouteStatus.teamItemRoleInvalidSkips++;
		}
		return context;
	}

	if (scope == BotNavItemRoleScope::FreeForAll) {
		botNavRouteStatus.ffaItemRoleSelections++;
	} else if (scope == BotNavItemRoleScope::CaptureTheFlag) {
		botNavRouteStatus.ctfItemRoleSelections++;
	} else {
		botNavRouteStatus.teamItemRoleSelections++;
	}
	const int scoreBoost = std::max(0, policy.priority);
	if (scoreBoost > 0) {
		context.candidateScore += scoreBoost;
		if (scope == BotNavItemRoleScope::FreeForAll) {
			botNavRouteStatus.ffaItemRoleScoreBoosts++;
		} else if (scope == BotNavItemRoleScope::CaptureTheFlag) {
			botNavRouteStatus.ctfItemRoleScoreBoosts++;
		} else {
			botNavRouteStatus.teamItemRoleScoreBoosts++;
		}
	}

	return context;
}

void BotNavApplyRoutePolicy() {
	BotLibAdapter_SetRoutePolicy(BotNavRocketJumpAllowed());
}

int BotNavClientIndex(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr || bot->s.number <= 0) {
		return -1;
	}

	const int clientIndex = static_cast<int>(bot->s.number) - 1;
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botNavRouteSlots.size()) ||
		clientIndex >= static_cast<int>(game.maxClients)) {
		return -1;
	}

	return clientIndex;
}

Vector3 BotNavRouteTarget(const BotLibAdapterRouteSteer &route) {
	return { route.moveTarget[0], route.moveTarget[1], route.moveTarget[2] };
}

Vector3 BotNavRouteGoal(const BotLibAdapterRouteSteer &route) {
	return { route.goalOrigin[0], route.goalOrigin[1], route.goalOrigin[2] };
}

Vector3 BotNavRoutePoint(const BotLibAdapterRouteSteer &route, int pointIndex) {
	return {
		route.routePoints[pointIndex][0],
		route.routePoints[pointIndex][1],
		route.routePoints[pointIndex][2]
	};
}

int BotNavRoutePointCount(const BotLibAdapterRouteSteer &route) {
	return std::clamp(route.routePointCount, 0, BOTLIB_ADAPTER_MAX_ROUTE_POINTS);
}

Vector3 BotNavDebugPoint(const Vector3 &point) {
	return point + Vector3{ 0.0f, 0.0f, 8.0f };
}

float BotNavHorizontalDistanceSquared(const Vector3 &a, const Vector3 &b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

int BotNavStatusDistance(float distanceSquared) {
	return static_cast<int>(std::max(distanceSquared, 0.0f));
}

int BotNavStatusTraceFraction(float fraction) {
	const float scaledFraction =
		std::clamp(fraction, 0.0f, 1.0f) * BOT_NAV_CORNER_CUT_TRACE_FRACTION_SCALE;
	return static_cast<int>(std::round(scaledFraction));
}

void BotNavSetRouteMoveTarget(BotLibAdapterRouteSteer *route, const Vector3 &target) {
	if (route == nullptr) {
		return;
	}

	route->moveTarget[0] = target.x;
	route->moveTarget[1] = target.y;
	route->moveTarget[2] = target.z;
}

bool BotNavCornerCutTravelTypeSupported(int travelType) {
	return travelType == BOT_NAV_TRAVEL_WALK ||
		travelType == BOT_NAV_TRAVEL_CROUCH ||
		travelType == BOT_NAV_TRAVEL_SWIM;
}

bool BotNavCornerCutNeedsGroundTrace(int travelType) {
	return travelType == BOT_NAV_TRAVEL_WALK ||
		travelType == BOT_NAV_TRAVEL_CROUCH;
}

bool BotNavGroundTraceSupportsPoint(
	const gentity_t *bot,
	const Vector3 &point,
	const Vector3 &mins,
	const Vector3 &maxs,
	bool recordCornerCutStatus) {
	const Vector3 end = point - Vector3{ 0.0f, 0.0f, BOT_NAV_CORNER_CUT_GROUND_PROBE_DEPTH };
	if (recordCornerCutStatus) {
		botNavRouteStatus.cornerCutGroundTraceAttempts++;
	}
	const trace_t trace = gi.trace(point, mins, maxs, end, bot, MASK_NAV_SOLID);
	if (trace.startSolid ||
		trace.allSolid ||
		trace.fraction >= 1.0f ||
		trace.plane.normal.z < BOT_NAV_CORNER_CUT_MIN_GROUND_NORMAL_Z) {
		if (recordCornerCutStatus) {
			botNavRouteStatus.cornerCutGroundTraceFailures++;
		}
		return false;
	}
	return true;
}

bool BotNavShortcutGroundSupported(
	const gentity_t *bot,
	const Vector3 &target,
	const Vector3 &mins,
	const Vector3 &maxs,
	bool recordCornerCutStatus) {
	if (bot == nullptr) {
		return false;
	}

	const Vector3 origin = bot->s.origin;
	const Vector3 delta = target - origin;
	constexpr std::array<float, 3> sampleFractions = { 0.35f, 0.70f, 1.0f };
	for (const float sampleFraction : sampleFractions) {
		const Vector3 sample = origin + (delta * sampleFraction);
		if (!BotNavGroundTraceSupportsPoint(bot, sample, mins, maxs, recordCornerCutStatus)) {
			return false;
		}
	}
	return true;
}

bool BotNavRouteShortcutTraceCandidateClear(
	const gentity_t *bot,
	const Vector3 &target,
	int travelType,
	const Vector3 &mins,
	const Vector3 &maxs,
	bool recordCornerCutStatus) {
	if (bot == nullptr) {
		return false;
	}

	const trace_t trace = gi.trace(bot->s.origin, mins, maxs, target, bot, MASK_PLAYERSOLID);
	if (recordCornerCutStatus) {
		botNavRouteStatus.cornerCutTraceAttempts++;
		botNavRouteStatus.lastCornerCutTraceFraction =
			BotNavStatusTraceFraction(trace.fraction);
	}

	const bool clear = !trace.startSolid && !trace.allSolid && trace.fraction >= 1.0f;
	const bool supported = clear &&
		(!BotNavCornerCutNeedsGroundTrace(travelType) ||
		 BotNavShortcutGroundSupported(bot, target, mins, maxs, recordCornerCutStatus));
	if (!supported) {
		if (recordCornerCutStatus) {
			botNavRouteStatus.cornerCutTraceFailures++;
		}
		return false;
	}

	if (recordCornerCutStatus) {
		botNavRouteStatus.cornerCutTracePasses++;
	}
	return true;
}

bool BotNavRouteShortcutTraceClear(
	const gentity_t *bot,
	const Vector3 &target,
	int travelType,
	bool recordCornerCutStatus) {
	if (bot == nullptr) {
		return false;
	}

	const Vector3 mins = bot->mins;
	const Vector3 maxs = bot->maxs;
	const float hullFootOffset = std::max(-mins.z, 0.0f);
	const std::array<float, 4> zOffsets = {
		0.0f,
		std::min(8.0f, hullFootOffset),
		std::min(16.0f, hullFootOffset),
		hullFootOffset
	};
	float previousOffset = -1.0f;
	for (const float zOffset : zOffsets) {
		if (std::abs(zOffset - previousOffset) < 0.5f) {
			continue;
		}
		previousOffset = zOffset;

		Vector3 traceTarget = target;
		traceTarget.z += zOffset;
		if (BotNavRouteShortcutTraceCandidateClear(
				bot,
				traceTarget,
				travelType,
				mins,
				maxs,
				recordCornerCutStatus)) {
			return true;
		}
	}
	return false;
}

struct BotNavRecoveryProbeCandidate {
	float forwardMove = 0.0f;
	float sideMove = 0.0f;
};

Vector3 BotNavRecoveryViewAngles(const gentity_t *bot, const float viewAngles[3]) {
	Vector3 angles = vec3_origin;
	if (viewAngles != nullptr) {
		angles = { viewAngles[PITCH], viewAngles[YAW], viewAngles[ROLL] };
	} else if (bot != nullptr && bot->client != nullptr) {
		angles = bot->client->vAngle;
	} else if (bot != nullptr) {
		angles = bot->s.angles;
	}
	angles[PITCH] = 0.0f;
	angles[ROLL] = 0.0f;
	return angles;
}

Vector3 BotNavRecoveryCommandDirection(
	const Vector3 &viewAngles,
	float forwardMove,
	float sideMove) {
	Vector3 forward = vec3_origin;
	Vector3 right = vec3_origin;
	AngleVectors(viewAngles, &forward, &right, nullptr);
	forward.z = 0.0f;
	right.z = 0.0f;
	forward.normalize();
	right.normalize();

	Vector3 direction = (forward * forwardMove) + (right * sideMove);
	direction.z = 0.0f;
	const float length = direction.normalize();
	if (length < 1.0f) {
		return vec3_origin;
	}
	return direction;
}

bool BotNavRecoveryProjectDirection(
	const Vector3 &viewAngles,
	const Vector3 &direction,
	float *forwardMove,
	float *sideMove) {
	if (forwardMove != nullptr) {
		*forwardMove = 0.0f;
	}
	if (sideMove != nullptr) {
		*sideMove = 0.0f;
	}

	Vector3 flatDirection = direction;
	flatDirection.z = 0.0f;
	const float directionLength = flatDirection.normalize();
	if (directionLength < 0.1f) {
		return false;
	}

	Vector3 forward = vec3_origin;
	Vector3 right = vec3_origin;
	AngleVectors(viewAngles, &forward, &right, nullptr);
	forward.z = 0.0f;
	right.z = 0.0f;
	forward.normalize();
	right.normalize();

	float projectedForward = flatDirection.dot(forward) * BOT_NAV_STUCK_RECOVERY_MOVE_SPEED;
	float projectedSide = flatDirection.dot(right) * BOT_NAV_STUCK_RECOVERY_MOVE_SPEED;
	if (std::abs(projectedForward) < 1.0f) {
		projectedForward = 0.0f;
	}
	if (std::abs(projectedSide) < 1.0f) {
		projectedSide = 0.0f;
	}

	if (forwardMove != nullptr) {
		*forwardMove = projectedForward;
	}
	if (sideMove != nullptr) {
		*sideMove = projectedSide;
	}
	return true;
}

bool BotNavEnsureRecoveryMove(
	const gentity_t *bot,
	BotNavRouteSlot &slot,
	const float viewAngles[3]) {
	if (slot.recoveryMoveValid) {
		return true;
	}
	if (bot == nullptr) {
		return false;
	}

	botNavRouteStatus.stuckRecoveryProbeChecks++;
	const Vector3 view = BotNavRecoveryViewAngles(bot, viewAngles);
	const int sideSign = slot.recoverySideSign == 0 ? 1 : slot.recoverySideSign;
	const float side = static_cast<float>(sideSign);
	const std::array<BotNavRecoveryProbeCandidate, 9> candidates = { {
		{ BOT_NAV_STUCK_RECOVERY_LEGACY_FORWARD_MOVE,
		  BOT_NAV_STUCK_RECOVERY_LEGACY_SIDE_MOVE * side },
		{ -120.0f, 100.0f * side },
		{ 0.0f, 160.0f * side },
		{ 80.0f, 140.0f * side },
		{ -150.0f, 0.0f },
		{ BOT_NAV_STUCK_RECOVERY_LEGACY_FORWARD_MOVE,
		  -BOT_NAV_STUCK_RECOVERY_LEGACY_SIDE_MOVE * side },
		{ -120.0f, -100.0f * side },
		{ 0.0f, -160.0f * side },
		{ 80.0f, -140.0f * side },
	} };

	Vector3 targetDirection = BotNavRouteTarget(slot.route) - bot->s.origin;
	targetDirection.z = 0.0f;
	const bool hasTargetDirection = targetDirection.normalize() >= 1.0f;

	float bestScore = -1.0f;
	int bestCandidate = -1;
	int bestFraction = 0;
	Vector3 bestDirection = vec3_origin;
	for (size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
		const BotNavRecoveryProbeCandidate &candidate = candidates[candidateIndex];
		const Vector3 direction =
			BotNavRecoveryCommandDirection(view, candidate.forwardMove, candidate.sideMove);
		if (direction.is_zero()) {
			continue;
		}

		const Vector3 end = bot->s.origin + (direction * BOT_NAV_STUCK_RECOVERY_PROBE_DISTANCE);
		const trace_t trace = gi.trace(bot->s.origin, bot->mins, bot->maxs, end, bot, MASK_PLAYERSOLID);
		if (trace.startSolid || trace.allSolid) {
			continue;
		}

		const int traceFraction = BotNavStatusTraceFraction(trace.fraction);
		float score = trace.fraction;
		if (hasTargetDirection) {
			score += std::max(0.0f, -direction.dot(targetDirection)) *
				BOT_NAV_STUCK_RECOVERY_AWAY_BONUS;
		}
		if (candidate.sideMove * side > 0.0f) {
			score += BOT_NAV_STUCK_RECOVERY_SIDE_BONUS;
		}
		if (score > bestScore) {
			bestScore = score;
			bestCandidate = static_cast<int>(candidateIndex);
			bestFraction = traceFraction;
			bestDirection = direction;
		}
	}

	if (bestCandidate >= 0) {
		slot.recoveryMoveValid = true;
		slot.recoveryMoveDirection = bestDirection;
		slot.recoveryProbeCandidate = bestCandidate;
		slot.recoveryProbeFraction = bestFraction;
		botNavRouteStatus.stuckRecoveryProbeUses++;
		if (bestFraction <
			BotNavStatusTraceFraction(BOT_NAV_STUCK_RECOVERY_PROBE_MIN_FRACTION)) {
			botNavRouteStatus.stuckRecoveryProbeBlocks++;
		}
	} else {
		slot.recoveryMoveDirection = BotNavRecoveryCommandDirection(
			view,
			BOT_NAV_STUCK_RECOVERY_LEGACY_FORWARD_MOVE,
			BOT_NAV_STUCK_RECOVERY_LEGACY_SIDE_MOVE * side);
		slot.recoveryMoveValid = !slot.recoveryMoveDirection.is_zero();
		slot.recoveryProbeCandidate = -1;
		slot.recoveryProbeFraction = 0;
		botNavRouteStatus.stuckRecoveryProbeFallbacks++;
	}

	botNavRouteStatus.lastStuckRecoveryProbeCandidate = slot.recoveryProbeCandidate;
	botNavRouteStatus.lastStuckRecoveryProbeFraction = slot.recoveryProbeFraction;
	if (slot.recoveryMoveValid) {
		float forwardMove = 0.0f;
		float sideMove = 0.0f;
		if (BotNavRecoveryProjectDirection(
				view,
				slot.recoveryMoveDirection,
				&forwardMove,
				&sideMove)) {
			botNavRouteStatus.lastStuckRecoveryForwardMove =
				static_cast<int>(std::round(forwardMove));
			botNavRouteStatus.lastStuckRecoverySideMove =
				static_cast<int>(std::round(sideMove));
		}
	}
	return slot.recoveryMoveValid;
}

void BotNavResetProgress(BotNavRouteSlot &slot) {
	slot.progressGoalArea = 0;
	slot.lastProgressDistanceSquared = -1.0f;
	slot.lastProgressTargetDistanceSquared = -1.0f;
	slot.progressRouteTarget = vec3_origin;
	slot.stagnantProgressFrames = 0;
	slot.lastStuckDistanceSq = 0;
	slot.lastStuckProgressDelta = 0;
}

void BotNavClearRecovery(BotNavRouteSlot &slot) {
	slot.recoveryUntilFrame = 0;
	slot.recoverySideSign = 0;
	slot.recoveryMoveValid = false;
	slot.recoveryMoveDirection = vec3_origin;
	slot.recoveryProbeCandidate = -1;
	slot.recoveryProbeFraction = 0;
}

void BotNavClearInteraction(BotNavRouteSlot &slot) {
	slot.interactionUntilFrame = 0;
	slot.interactionAction = static_cast<int>(BotNavInteractionAction::None);
	slot.interactionKind = static_cast<int>(BotNavInteractionKind::None);
	slot.interactionEntityNumber = -1;
	slot.interactionEntitySpawnCount = 0;
	slot.interactionProgressionScore = 0;
	slot.interactionProgressionPreferred = 0;
	slot.interactionTargetEntity = 0;
	slot.interactionProgressionTarget = 0;
	slot.interactionTargetLink = 0;
	slot.interactionNamedTarget = 0;
	slot.interactionKeyEntity = 0;
	slot.interactionKeyItem = 0;
	slot.interactionKeyLock = 0;
	slot.interactionKeyRequiredItem = 0;
	slot.interactionCommandFrames = 0;
}

float BotNavDistanceSquaredToBounds(const Vector3 &point, const gentity_t *ent) {
	if (ent == nullptr) {
		return BOT_NAV_ELEVATOR_INTERACTION_DIST_SQUARED + 1.0f;
	}

	const Vector3 mins = ent->linked ? ent->absMin : ent->s.origin + ent->mins;
	const Vector3 maxs = ent->linked ? ent->absMax : ent->s.origin + ent->maxs;
	float distanceSquared = 0.0f;
	for (int axis = 0; axis < 3; ++axis) {
		if (point[axis] < mins[axis]) {
			const float delta = mins[axis] - point[axis];
			distanceSquared += delta * delta;
		} else if (point[axis] > maxs[axis]) {
			const float delta = point[axis] - maxs[axis];
			distanceSquared += delta * delta;
		}
	}
	return distanceSquared;
}

int BotNavStatusDistanceForPoints(const Vector3 &point, const gentity_t *ent) {
	return BotNavStatusDistance(BotNavDistanceSquaredToBounds(point, ent));
}

int BotNavStatusCoord(float value) {
	return static_cast<int>(std::round(value));
}

int BotNavArmorValue(const gclient_t *client) {
	if (client == nullptr) {
		return 0;
	}

	const auto &inventory = client->pers.inventory;
	return std::max(
		std::max(inventory[IT_ARMOR_BODY], inventory[IT_ARMOR_COMBAT]),
		std::max(inventory[IT_ARMOR_JACKET], inventory[IT_ARMOR_SHARD]));
}

const Item *BotNavItemForId(item_id_t item) {
	if (item <= IT_NULL || item >= IT_TOTAL) {
		return nullptr;
	}
	return &itemList[static_cast<size_t>(item)];
}

bool BotNavItemIsHealth(const Item *item) {
	return item != nullptr && (item->flags & IF_HEALTH);
}

bool BotNavItemIsArmor(const Item *item) {
	return item != nullptr &&
		((item->flags & (IF_ARMOR | IF_POWER_ARMOR)) ||
		 item->id == IT_POWER_SCREEN ||
		 item->id == IT_POWER_SHIELD);
}

bool BotNavNearRecordedPickupGoal(const BotNavRouteSlot &slot, const gentity_t *bot) {
	return bot != nullptr &&
		slot.valid &&
		BotNavHorizontalDistanceSquared(bot->s.origin, BotNavRouteGoal(slot.route)) <=
			BOT_NAV_PICKUP_RECORD_DIST_SQUARED;
}

void BotNavRecordPotentialPickup(const BotNavRouteSlot &slot, const gentity_t *bot) {
	if (slot.persistentGoalEntityNumber < 0 ||
		slot.persistentGoalItem <= IT_NULL ||
		bot == nullptr ||
		bot->client == nullptr ||
		!BotNavNearRecordedPickupGoal(slot, bot)) {
		return;
	}

	const Item *item = BotNavItemForId(slot.persistentGoalItem);
	if (BotNavItemIsHealth(item) && bot->health > slot.persistentGoalHealthAtAssignment) {
		BotItems_RecordPickup(item, slot.persistentGoalHealthAtAssignment, bot->health);
	} else if (BotNavItemIsArmor(item)) {
		const int armor = BotNavArmorValue(bot->client);
		if (armor > slot.persistentGoalArmorAtAssignment) {
			BotItems_RecordPickup(item, slot.persistentGoalArmorAtAssignment, armor);
		}
	}
}

bool BotNavClassIs(const gentity_t *ent, const char *className) {
	return ent != nullptr &&
		ent->className != nullptr &&
		className != nullptr &&
		Q_strcasecmp(ent->className, className) == 0;
}

bool BotNavClassStartsWith(const gentity_t *ent, const char *prefix, size_t prefixLength) {
	return ent != nullptr &&
		ent->className != nullptr &&
		prefix != nullptr &&
		Q_strncasecmp(ent->className, prefix, prefixLength) == 0;
}

bool BotNavStringPresent(const char *value) {
	return value != nullptr && value[0] != '\0';
}

bool BotNavClassIsTargetEntity(const gentity_t *ent) {
	return BotNavClassStartsWith(ent, "target_", 7);
}

bool BotNavClassIsProgressionTarget(const gentity_t *ent) {
	return BotNavClassIs(ent, "target_changelevel") ||
		BotNavClassIs(ent, "target_goal") ||
		BotNavClassIs(ent, "target_secret") ||
		BotNavClassIs(ent, "target_crosslevel_trigger") ||
		BotNavClassIs(ent, "target_crosslevel_target") ||
		BotNavClassIs(ent, "target_crossunit_trigger") ||
		BotNavClassIs(ent, "target_crossunit_target") ||
		BotNavClassIs(ent, "target_help") ||
		BotNavClassIs(ent, "trigger_key") ||
		BotNavClassIs(ent, "trigger_secret");
}

bool BotNavClassIsKeyEntity(const gentity_t *ent) {
	return BotNavClassStartsWith(ent, "key_", 4) ||
		(ent != nullptr && ent->item != nullptr && (ent->item->flags & IF_KEY));
}

bool BotNavClassIsKeyLock(const gentity_t *ent) {
	return BotNavClassIs(ent, "trigger_key");
}

int BotNavKeyRequiredItemId(const gentity_t *ent) {
	if (ent == nullptr || ent->item == nullptr || (ent->item->flags & IF_KEY) == 0) {
		return 0;
	}
	return static_cast<int>(ent->item->id);
}

int BotNavInteractionProgressionScore(
	const gentity_t *ent,
	BotNavInteractionCandidate *candidate) {
	const bool targetEntity = BotNavClassIsTargetEntity(ent);
	const bool progressionTarget = BotNavClassIsProgressionTarget(ent);
	const bool targetLink = BotNavStringPresent(ent != nullptr ? ent->target : nullptr);
	const bool namedTarget = BotNavStringPresent(ent != nullptr ? ent->targetName : nullptr);
	const bool keyItem = BotNavClassIsKeyEntity(ent);
	const bool keyLock = BotNavClassIsKeyLock(ent);
	const bool keyEntity = keyItem || keyLock;
	const int keyRequiredItem = BotNavKeyRequiredItemId(ent);

	if (candidate != nullptr) {
		candidate->targetEntity = targetEntity ? 1 : 0;
		candidate->progressionTarget = progressionTarget ? 1 : 0;
		candidate->targetLink = targetLink ? 1 : 0;
		candidate->namedTarget = namedTarget ? 1 : 0;
		candidate->keyEntity = keyEntity ? 1 : 0;
		candidate->keyItem = keyItem ? 1 : 0;
		candidate->keyLock = keyLock ? 1 : 0;
		candidate->keyRequiredItem = keyRequiredItem;
	}

	int score = 0;
	if (progressionTarget) {
		score += 8;
	}
	if (keyLock) {
		score += 8;
	}
	if (keyEntity) {
		score += 6;
	}
	if (targetLink) {
		score += 4;
	}
	if (namedTarget) {
		score += 2;
	}
	if (targetEntity) {
		score += 1;
	}
	return score;
}

bool BotNavInteractionCandidateBetter(
	const BotNavInteractionCandidate &candidate,
	const BotNavInteractionCandidate &best) {
	if (best.entityNumber < 0) {
		return true;
	}

	const int slack = BOT_NAV_INTERACTION_PROGRESSION_PREFERENCE_SLACK_SQUARED;
	if (candidate.progressionScore > best.progressionScore &&
		candidate.distanceSquared <= best.distanceSquared + slack) {
		return true;
	}
	if (candidate.progressionScore < best.progressionScore &&
		best.distanceSquared <= candidate.distanceSquared + slack) {
		return false;
	}
	if (candidate.distanceSquared != best.distanceSquared) {
		return candidate.distanceSquared < best.distanceSquared;
	}
	return candidate.progressionScore > best.progressionScore;
}

bool BotNavInteractionEntityMatches(
	const gentity_t *ent,
	int entityNumber,
	int targetEntityNumber,
	int spawnCount) {
	return ent != nullptr &&
		entityNumber >= 0 &&
		entityNumber == targetEntityNumber &&
		ent->spawn_count == spawnCount;
}

bool BotNavInteractionSuppressed(
	const BotNavRouteSlot &slot,
	const gentity_t *ent,
	int entityNumber,
	uint32_t frame) {
	return frame < slot.suppressedInteractionUntilFrame &&
		BotNavInteractionEntityMatches(
			ent,
			entityNumber,
			slot.suppressedInteractionEntityNumber,
			slot.suppressedInteractionEntitySpawnCount);
}

void BotNavRecordProgressionSuppression(
	const BotNavRouteSlot &slot,
	const gentity_t *ent,
	int entityNumber) {
	botNavRouteStatus.interactionProgressionRepeatSuppressions++;
	botNavRouteStatus.lastInteractionProgressionSuppressedEntity = entityNumber;
	botNavRouteStatus.lastInteractionProgressionSuppressedScore =
		slot.suppressedInteractionProgressionScore;
	(void)ent;
}

bool BotNavCompleteProgressionInteraction(
	BotNavRouteSlot &slot,
	int clientIndex,
	uint32_t frame) {
	if (slot.interactionAction == static_cast<int>(BotNavInteractionAction::None) ||
		slot.interactionProgressionScore <= 0 ||
		slot.interactionCommandFrames <= 0) {
		return false;
	}

	botNavRouteStatus.interactionProgressionCompletions++;
	botNavRouteStatus.lastClient = clientIndex;
	botNavRouteStatus.lastInteractionProgressionCompletedEntity =
		slot.interactionEntityNumber;
	botNavRouteStatus.lastInteractionProgressionCompletedScore =
		slot.interactionProgressionScore;
	if (slot.interactionKeyEntity) {
		botNavRouteStatus.interactionProgressionKeyPathCompletions++;
		botNavRouteStatus.lastInteractionProgressionKeyPathEntity =
			slot.interactionEntityNumber;
		botNavRouteStatus.lastInteractionProgressionKeyPathScore =
			slot.interactionProgressionScore;
		botNavRouteStatus.lastInteractionProgressionKeyPathKeyItem =
			slot.interactionKeyItem;
		botNavRouteStatus.lastInteractionProgressionKeyPathKeyLock =
			slot.interactionKeyLock;
		botNavRouteStatus.lastInteractionProgressionKeyPathRequiredItem =
			slot.interactionKeyRequiredItem;
	}

	const bool hasPreviousCompletion = slot.completedProgressionEntityNumber >= 0;
	const bool distinctCompletion =
		!hasPreviousCompletion ||
		slot.completedProgressionEntityNumber != slot.interactionEntityNumber ||
		slot.completedProgressionEntitySpawnCount != slot.interactionEntitySpawnCount;
	if (slot.completedProgressionCount == 0) {
		botNavRouteStatus.interactionProgressionCompletedClients++;
	}
	if (hasPreviousCompletion) {
		botNavRouteStatus.interactionProgressionCarryCompletions++;
		botNavRouteStatus.lastInteractionProgressionCarryPreviousEntity =
			slot.completedProgressionEntityNumber;
		botNavRouteStatus.lastInteractionProgressionCarryEntity =
			slot.interactionEntityNumber;
		botNavRouteStatus.lastInteractionProgressionCarryDistinct =
			distinctCompletion ? 1 : 0;
	}
	if (distinctCompletion) {
		if (slot.completedProgressionDistinctCount == 0) {
			botNavRouteStatus.interactionProgressionDistinctCompletedClients++;
		}
		if (hasPreviousCompletion) {
			botNavRouteStatus.interactionProgressionCarryDistinctCompletions++;
		}
		slot.completedProgressionDistinctCount++;
	}
	slot.completedProgressionCount++;
	slot.completedProgressionEntityNumber = slot.interactionEntityNumber;
	slot.completedProgressionEntitySpawnCount = slot.interactionEntitySpawnCount;
	botNavRouteStatus.lastInteractionProgressionCarryCount =
		slot.completedProgressionCount;
	botNavRouteStatus.lastInteractionProgressionCarryDistinctCount =
		slot.completedProgressionDistinctCount;

	slot.postInteractionUntilFrame =
		frame + BOT_NAV_INTERACTION_PROGRESSION_POST_FRAMES;
	slot.postInteractionEntityNumber = slot.interactionEntityNumber;
	slot.postInteractionEntitySpawnCount = slot.interactionEntitySpawnCount;
	slot.postInteractionProgressionScore = slot.interactionProgressionScore;

	slot.suppressedInteractionUntilFrame =
		frame + BOT_NAV_INTERACTION_PROGRESSION_SUPPRESS_FRAMES;
	slot.suppressedInteractionEntityNumber = slot.interactionEntityNumber;
	slot.suppressedInteractionEntitySpawnCount = slot.interactionEntitySpawnCount;
	slot.suppressedInteractionProgressionScore = slot.interactionProgressionScore;

	slot.valid = false;
	botNavRouteStatus.interactionProgressionPostRefreshes++;

	return true;
}

bool BotNavCompleteExpiredInteraction(
	BotNavRouteSlot &slot,
	int clientIndex,
	uint32_t frame) {
	if (slot.interactionAction == static_cast<int>(BotNavInteractionAction::None) ||
		frame < slot.interactionUntilFrame) {
		return false;
	}

	BotNavCompleteProgressionInteraction(slot, clientIndex, frame);
	if (slot.interactionCommandFrames > 0 &&
		BotNavInteractionArrivalKindHasMoverEndpoint(slot.interactionKind)) {
		gentity_t *bot = BotNavClientEntity(clientIndex);
		(void)BotNavRecordMoverRideState(
			bot,
			slot.interactionEntityNumber,
			BotNavMoverRidePhase::Leave,
			nullptr);
	}
	BotNavClearInteraction(slot);
	return true;
}

void BotNavRecordPostInteractionProgress(
	const BotNavRouteSlot &slot,
	uint32_t frame) {
	if (frame >= slot.postInteractionUntilFrame ||
		slot.postInteractionEntityNumber < 0) {
		return;
	}

	botNavRouteStatus.interactionProgressionPostFrames++;
	botNavRouteStatus.lastInteractionProgressionPostEntity =
		slot.postInteractionEntityNumber;
	botNavRouteStatus.lastInteractionProgressionPostFramesRemaining =
		static_cast<int>(slot.postInteractionUntilFrame - frame);
}

int BotNavInteractionKindForEntity(const gentity_t *ent) {
	if (BotNavClassIs(ent, "func_door") ||
		BotNavClassIs(ent, "func_door_rotating") ||
		BotNavClassIs(ent, "func_door_secret") ||
		BotNavClassIs(ent, "func_door_secret2")) {
		return static_cast<int>(BotNavInteractionKind::Door);
	}
	if (BotNavClassIs(ent, "func_button")) {
		return static_cast<int>(BotNavInteractionKind::Button);
	}
	if (BotNavClassIs(ent, "func_plat") ||
		BotNavClassIs(ent, "func_plat2")) {
		return static_cast<int>(BotNavInteractionKind::Platform);
	}
	if (BotNavClassIs(ent, "func_train") ||
		BotNavClassIs(ent, "func_rotate_train")) {
		return static_cast<int>(BotNavInteractionKind::Train);
	}
	if (BotNavClassIs(ent, "func_water") ||
		BotNavClassIs(ent, "func_bobbingwater")) {
		return static_cast<int>(BotNavInteractionKind::Water);
	}
	if (BotNavClassIs(ent, "misc_teleporter") ||
		BotNavClassIs(ent, "teleporter_touch") ||
		BotNavClassIs(ent, "misc_teleporter_dest") ||
		BotNavClassIs(ent, "target_teleporter") ||
		BotNavClassIs(ent, "trigger_teleport")) {
		return static_cast<int>(BotNavInteractionKind::Teleporter);
	}
	if (BotNavClassIs(ent, "trigger_hurt") ||
		BotNavClassIs(ent, "trigger_lava") ||
		BotNavClassIs(ent, "trigger_slime") ||
		BotNavClassIs(ent, "target_laser") ||
		BotNavClassIs(ent, "misc_lavaball")) {
		return static_cast<int>(BotNavInteractionKind::Hazard);
	}
	if (BotNavClassIs(ent, "trigger_once") ||
		BotNavClassIs(ent, "trigger_multiple") ||
		BotNavClassIsKeyLock(ent)) {
		return static_cast<int>(BotNavInteractionKind::Trigger);
	}
	if (ent != nullptr && ent->moveType == MoveType::Push && ent->solid == SOLID_BSP) {
		return static_cast<int>(BotNavInteractionKind::Mover);
	}
	return static_cast<int>(BotNavInteractionKind::None);
}

int BotNavInteractionActionForEntity(const gentity_t *ent) {
	if (ent == nullptr) {
		return static_cast<int>(BotNavInteractionAction::None);
	}
	if (ent->use) {
		return static_cast<int>(BotNavInteractionAction::WaitUse);
	}
	if (ent->touch) {
		return static_cast<int>(BotNavInteractionAction::Wait);
	}
	return static_cast<int>(BotNavInteractionAction::None);
}

void BotNavRecordInteractionEntityContext(const gentity_t *ent) {
	if (ent == nullptr) {
		return;
	}

	const Vector3 mins = ent->linked ? ent->absMin : ent->s.origin + ent->mins;
	const Vector3 maxs = ent->linked ? ent->absMax : ent->s.origin + ent->maxs;
	botNavRouteStatus.lastInteractionSpawnCount = ent->spawn_count;
	botNavRouteStatus.lastInteractionOriginX = BotNavStatusCoord(ent->s.origin.x);
	botNavRouteStatus.lastInteractionOriginY = BotNavStatusCoord(ent->s.origin.y);
	botNavRouteStatus.lastInteractionOriginZ = BotNavStatusCoord(ent->s.origin.z);
	botNavRouteStatus.lastInteractionBoundsMinX = BotNavStatusCoord(mins.x);
	botNavRouteStatus.lastInteractionBoundsMinY = BotNavStatusCoord(mins.y);
	botNavRouteStatus.lastInteractionBoundsMinZ = BotNavStatusCoord(mins.z);
	botNavRouteStatus.lastInteractionBoundsMaxX = BotNavStatusCoord(maxs.x);
	botNavRouteStatus.lastInteractionBoundsMaxY = BotNavStatusCoord(maxs.y);
	botNavRouteStatus.lastInteractionBoundsMaxZ = BotNavStatusCoord(maxs.z);
	botNavRouteStatus.lastInteractionUse = ent->use ? 1 : 0;
	botNavRouteStatus.lastInteractionTouch = ent->touch ? 1 : 0;
	botNavRouteStatus.lastInteractionSolid = static_cast<int>(ent->solid);
	botNavRouteStatus.lastInteractionMoveType = static_cast<int>(ent->moveType);
}

void BotNavIncrementInteractionKindCount(int kind) {
	switch (static_cast<BotNavInteractionKind>(kind)) {
	case BotNavInteractionKind::Door:
		botNavRouteStatus.interactionWorldDoors++;
		break;
	case BotNavInteractionKind::Button:
		botNavRouteStatus.interactionWorldButtons++;
		break;
	case BotNavInteractionKind::Platform:
		botNavRouteStatus.interactionWorldPlatforms++;
		break;
	case BotNavInteractionKind::Train:
		botNavRouteStatus.interactionWorldTrains++;
		break;
	case BotNavInteractionKind::Water:
		botNavRouteStatus.interactionWorldWaters++;
		break;
	case BotNavInteractionKind::Trigger:
		botNavRouteStatus.interactionWorldTriggers++;
		break;
	case BotNavInteractionKind::Mover:
		botNavRouteStatus.interactionWorldMovers++;
		break;
	case BotNavInteractionKind::Teleporter:
		botNavRouteStatus.interactionWorldTeleporters++;
		break;
	case BotNavInteractionKind::Hazard:
		botNavRouteStatus.interactionWorldHazards++;
		break;
	case BotNavInteractionKind::None:
	default:
		break;
	}
}

void BotNavUpdateInteractionWorldContextStatus() {
	botNavRouteStatus.interactionWorldEntities = 0;
	botNavRouteStatus.interactionWorldDoors = 0;
	botNavRouteStatus.interactionWorldButtons = 0;
	botNavRouteStatus.interactionWorldPlatforms = 0;
	botNavRouteStatus.interactionWorldTrains = 0;
	botNavRouteStatus.interactionWorldWaters = 0;
	botNavRouteStatus.interactionWorldTriggers = 0;
	botNavRouteStatus.interactionWorldMovers = 0;
	botNavRouteStatus.interactionWorldTeleporters = 0;
	botNavRouteStatus.interactionWorldHazards = 0;
	botNavRouteStatus.interactionWorldUseEntities = 0;
	botNavRouteStatus.interactionWorldTouchEntities = 0;
	botNavRouteStatus.interactionWorldTargetEntities = 0;
	botNavRouteStatus.interactionWorldProgressionTargets = 0;
	botNavRouteStatus.interactionWorldTargetLinks = 0;
	botNavRouteStatus.interactionWorldNamedTargets = 0;
	botNavRouteStatus.interactionWorldKeyEntities = 0;
	botNavRouteStatus.interactionWorldKeyItems = 0;
	botNavRouteStatus.interactionWorldKeyLocks = 0;
	botNavRouteStatus.interactionWorldKeyPathEntities = 0;
	botNavRouteStatus.interactionWorldProgressionEntities = 0;
	if (g_entities == nullptr) {
		return;
	}

	for (uint32_t entnum = game.maxClients + 1; entnum < globals.numEntities; ++entnum) {
		const gentity_t *ent = &g_entities[entnum];
		if (ent == nullptr ||
			!ent->inUse ||
			(ent->flags & FL_NO_BOTS) != 0) {
			continue;
		}

		const int kind = BotNavInteractionKindForEntity(ent);
		const bool interactionEntity = kind != static_cast<int>(BotNavInteractionKind::None);
		const bool targetEntity = BotNavClassIsTargetEntity(ent);
		const bool progressionTarget = BotNavClassIsProgressionTarget(ent);
		const bool targetLink = BotNavStringPresent(ent->target);
		const bool namedTarget = BotNavStringPresent(ent->targetName);
		const bool keyItem = BotNavClassIsKeyEntity(ent);
		const bool keyLock = BotNavClassIsKeyLock(ent);
		const bool keyEntity = keyItem || keyLock;
		if (targetEntity) {
			botNavRouteStatus.interactionWorldTargetEntities++;
		}
		if (progressionTarget) {
			botNavRouteStatus.interactionWorldProgressionTargets++;
		}
		if (targetLink) {
			botNavRouteStatus.interactionWorldTargetLinks++;
		}
		if (namedTarget) {
			botNavRouteStatus.interactionWorldNamedTargets++;
		}
		if (keyEntity) {
			botNavRouteStatus.interactionWorldKeyEntities++;
		}
		if (keyItem) {
			botNavRouteStatus.interactionWorldKeyItems++;
		}
		if (keyLock) {
			botNavRouteStatus.interactionWorldKeyLocks++;
		}
		if (keyEntity && (targetLink || progressionTarget || keyLock)) {
			botNavRouteStatus.interactionWorldKeyPathEntities++;
		}
		if (interactionEntity || targetEntity || progressionTarget || targetLink || namedTarget || keyEntity) {
			botNavRouteStatus.interactionWorldProgressionEntities++;
		}

		if (kind == static_cast<int>(BotNavInteractionKind::None)) {
			continue;
		}

		botNavRouteStatus.interactionWorldEntities++;
		BotNavIncrementInteractionKindCount(kind);
		const int action = BotNavInteractionActionForEntity(ent);
		if (action == static_cast<int>(BotNavInteractionAction::Use) ||
			action == static_cast<int>(BotNavInteractionAction::WaitUse)) {
			botNavRouteStatus.interactionWorldUseEntities++;
		}
		if (action == static_cast<int>(BotNavInteractionAction::Wait) ||
			action == static_cast<int>(BotNavInteractionAction::WaitUse)) {
			botNavRouteStatus.interactionWorldTouchEntities++;
		}
	}
}

bool BotNavInteractionKindMatchesRoute(int kind, const BotLibAdapterRouteSteer &route) {
	if (route.reachabilityTravelType == BOT_NAV_TRAVEL_ELEVATOR) {
		return kind == static_cast<int>(BotNavInteractionKind::Platform) ||
			kind == static_cast<int>(BotNavInteractionKind::Train) ||
			kind == static_cast<int>(BotNavInteractionKind::Mover);
	}
	return kind != static_cast<int>(BotNavInteractionKind::None);
}

bool BotNavFindInteractionCandidate(
	const gentity_t *bot,
	const BotLibAdapterRouteSteer &route,
	const BotNavRouteSlot &slot,
	uint32_t frame,
	int requiredKind,
	BotNavInteractionCandidate *candidate) {
	if (bot == nullptr || g_entities == nullptr) {
		return false;
	}

	const bool elevatorRoute = route.reachabilityTravelType == BOT_NAV_TRAVEL_ELEVATOR;
	const float maxDistanceSquared = elevatorRoute ?
		BOT_NAV_ELEVATOR_INTERACTION_DIST_SQUARED :
		BOT_NAV_INTERACTION_NEAR_DIST_SQUARED;
	const Vector3 routeTarget = BotNavRouteTarget(route);
	const Vector3 routeGoal = BotNavRouteGoal(route);
	const bool hasPersistentPositionGoal = slot.persistentGoalIsPosition;
	const Vector3 persistentPositionGoal = slot.persistentPositionGoal;
	BotNavInteractionCandidate best{};
	best.distanceSquared = BotNavStatusDistance(maxDistanceSquared) + 1;
	int nearestCandidateDistance = best.distanceSquared;

	botNavRouteStatus.interactionChecks++;
	for (uint32_t entnum = game.maxClients + 1; entnum < globals.numEntities; ++entnum) {
		const gentity_t *ent = &g_entities[entnum];
		if (ent == nullptr ||
			!ent->inUse ||
			(ent->flags & FL_NO_BOTS) != 0) {
			continue;
		}

		const int kind = BotNavInteractionKindForEntity(ent);
		const int action = BotNavInteractionActionForEntity(ent);
		if (kind == static_cast<int>(BotNavInteractionKind::None) ||
			(requiredKind > 0 && kind != requiredKind) ||
			action == static_cast<int>(BotNavInteractionAction::None) ||
			!BotNavInteractionKindMatchesRoute(kind, route)) {
			continue;
		}
		if (BotNavInteractionSuppressed(slot, ent, static_cast<int>(entnum), frame)) {
			BotNavRecordProgressionSuppression(slot, ent, static_cast<int>(entnum));
			continue;
		}

		const float botDistance = BotNavDistanceSquaredToBounds(bot->s.origin, ent);
		const float targetDistance = BotNavDistanceSquaredToBounds(routeTarget, ent);
		const float goalDistance = BotNavDistanceSquaredToBounds(routeGoal, ent);
		float distanceSquared = std::min(botDistance, std::min(targetDistance, goalDistance));
		if (hasPersistentPositionGoal) {
			distanceSquared = std::min(
				distanceSquared,
				BotNavDistanceSquaredToBounds(persistentPositionGoal, ent));
		}
		if (distanceSquared > maxDistanceSquared) {
			continue;
		}

		botNavRouteStatus.interactionCandidates++;
		const int statusDistance = BotNavStatusDistance(distanceSquared);
		nearestCandidateDistance = std::min(nearestCandidateDistance, statusDistance);

		BotNavInteractionCandidate current{};
		current.entityNumber = static_cast<int>(entnum);
		current.action = action;
		current.kind = kind;
		current.distanceSquared = statusDistance;
		current.progressionScore = BotNavInteractionProgressionScore(ent, &current);
		if (current.progressionScore > 0) {
			botNavRouteStatus.interactionProgressionCandidates++;
		}
		if (current.keyEntity) {
			botNavRouteStatus.interactionProgressionKeyPathCandidates++;
		}
		if (!BotNavInteractionCandidateBetter(current, best)) {
			continue;
		}

		best = current;
	}

	if (best.entityNumber < 0) {
		botNavRouteStatus.interactionMisses++;
		return false;
	}

	best.progressionPreferred =
		best.progressionScore > 0 &&
		best.distanceSquared > nearestCandidateDistance ? 1 : 0;

	if (candidate != nullptr) {
		*candidate = best;
	}
	return true;
}

bool BotNavActivateInteractionRetry(
	BotNavRouteSlot &slot,
	const gentity_t *bot,
	int clientIndex,
	uint32_t frame,
	const BotLibAdapterRouteSteer &route,
	bool fromStuck,
	int requiredKind = 0) {
	if (frame < slot.interactionUntilFrame || frame < slot.nextInteractionFrame) {
		return false;
	}

	BotNavInteractionCandidate candidate{};
	if (!BotNavFindInteractionCandidate(bot, route, slot, frame, requiredKind, &candidate)) {
		return false;
	}

	slot.interactionUntilFrame = frame + BOT_NAV_INTERACTION_RETRY_FRAMES;
	slot.nextInteractionFrame = frame + BOT_NAV_INTERACTION_RETRY_COOLDOWN_FRAMES;
	slot.interactionAction = candidate.action;
	slot.interactionKind = candidate.kind;
	slot.interactionEntityNumber = candidate.entityNumber;
	if (candidate.entityNumber >= 0 &&
		candidate.entityNumber < static_cast<int>(globals.numEntities)) {
		slot.interactionEntitySpawnCount = g_entities[candidate.entityNumber].spawn_count;
	} else {
		slot.interactionEntitySpawnCount = 0;
	}
	slot.interactionProgressionScore = candidate.progressionScore;
	slot.interactionProgressionPreferred = candidate.progressionPreferred;
	slot.interactionTargetEntity = candidate.targetEntity;
	slot.interactionProgressionTarget = candidate.progressionTarget;
	slot.interactionTargetLink = candidate.targetLink;
	slot.interactionNamedTarget = candidate.namedTarget;
	slot.interactionKeyEntity = candidate.keyEntity;
	slot.interactionKeyItem = candidate.keyItem;
	slot.interactionKeyLock = candidate.keyLock;
	slot.interactionKeyRequiredItem = candidate.keyRequiredItem;
	slot.interactionCommandFrames = 0;

	botNavRouteStatus.interactionActivations++;
	if (fromStuck) {
		botNavRouteStatus.interactionStuckActivations++;
	} else if (route.reachabilityTravelType == BOT_NAV_TRAVEL_ELEVATOR) {
		botNavRouteStatus.interactionElevatorActivations++;
	}
	botNavRouteStatus.lastInteractionAction = candidate.action;
	botNavRouteStatus.lastInteractionKind = candidate.kind;
	botNavRouteStatus.lastInteractionEntity = candidate.entityNumber;
	botNavRouteStatus.lastInteractionClient = clientIndex;
	botNavRouteStatus.lastInteractionDistanceSq = candidate.distanceSquared;
	botNavRouteStatus.lastInteractionTravelType = route.reachabilityTravelType;
	botNavRouteStatus.lastInteractionMoveState = 0;
	botNavRouteStatus.lastInteractionProgressionScore = candidate.progressionScore;
	botNavRouteStatus.lastInteractionProgressionPreferred = candidate.progressionPreferred;
	botNavRouteStatus.lastInteractionTargetEntity = candidate.targetEntity;
	botNavRouteStatus.lastInteractionProgressionTarget = candidate.progressionTarget;
	botNavRouteStatus.lastInteractionTargetLink = candidate.targetLink;
	botNavRouteStatus.lastInteractionNamedTarget = candidate.namedTarget;
	botNavRouteStatus.lastInteractionKeyEntity = candidate.keyEntity;
	botNavRouteStatus.lastInteractionKeyItem = candidate.keyItem;
	botNavRouteStatus.lastInteractionKeyLock = candidate.keyLock;
	botNavRouteStatus.lastInteractionKeyRequiredItem = candidate.keyRequiredItem;
	if (candidate.entityNumber >= 0 &&
		candidate.entityNumber < static_cast<int>(globals.numEntities)) {
		const gentity_t *ent = &g_entities[candidate.entityNumber];
		botNavRouteStatus.lastInteractionMoveState = static_cast<int>(ent->moveInfo.state);
		BotNavRecordInteractionEntityContext(ent);
	}
	if (candidate.progressionScore > 0) {
		botNavRouteStatus.interactionProgressionSelections++;
		if (candidate.targetEntity) {
			botNavRouteStatus.interactionProgressionTargetEntitySelections++;
		}
		if (candidate.progressionTarget) {
			botNavRouteStatus.interactionProgressionTargetSelections++;
		}
		if (candidate.targetLink) {
			botNavRouteStatus.interactionProgressionTargetLinkSelections++;
		}
		if (candidate.namedTarget) {
			botNavRouteStatus.interactionProgressionNamedTargetSelections++;
		}
		if (candidate.keyEntity) {
			botNavRouteStatus.interactionProgressionKeyEntitySelections++;
		}
		if (candidate.keyEntity) {
			botNavRouteStatus.interactionProgressionKeyPathSelections++;
			botNavRouteStatus.lastInteractionProgressionKeyPathEntity =
				candidate.entityNumber;
			botNavRouteStatus.lastInteractionProgressionKeyPathScore =
				candidate.progressionScore;
			botNavRouteStatus.lastInteractionProgressionKeyPathKeyItem =
				candidate.keyItem;
			botNavRouteStatus.lastInteractionProgressionKeyPathKeyLock =
				candidate.keyLock;
			botNavRouteStatus.lastInteractionProgressionKeyPathRequiredItem =
				candidate.keyRequiredItem;
		}
	}
	if (candidate.progressionPreferred) {
		botNavRouteStatus.interactionProgressionPreferenceSelections++;
	}
	botNavRouteStatus.lastInteractionFramesRemaining =
		static_cast<int>(BOT_NAV_INTERACTION_RETRY_FRAMES);
	return true;
}

void BotNavMaybeActivateRouteInteraction(
	BotNavRouteSlot &slot,
	const gentity_t *bot,
	int clientIndex,
	uint32_t frame,
	const BotLibAdapterRouteSteer &route) {
	if (route.reachabilityTravelType != BOT_NAV_TRAVEL_ELEVATOR) {
		return;
	}

	(void)BotNavActivateInteractionRetry(slot, bot, clientIndex, frame, route, false);
}

void BotNavRecordTravelTypeGoalSupport(int travelTypeGoal) {
	if (travelTypeGoal <= 0) {
		return;
	}

	float startOrigin[3] = {};
	int startArea = 0;
	int goalArea = 0;
	botNavRouteStatus.travelTypeGoalSupportChecks++;
	botNavRouteStatus.lastTravelTypeGoalSupportType = travelTypeGoal;
	if (BotLibAdapter_FindRouteStartForTravelType(travelTypeGoal, startOrigin, &startArea, &goalArea) &&
		startArea > 0 &&
		goalArea > 0) {
		botNavRouteStatus.travelTypeGoalSupported++;
		botNavRouteStatus.lastTravelTypeGoalSupportArea = startArea;
		botNavRouteStatus.lastTravelTypeGoalSupportGoalArea = goalArea;
		return;
	}

	botNavRouteStatus.travelTypeGoalUnsupported++;
	botNavRouteStatus.lastTravelTypeGoalSupportArea = 0;
	botNavRouteStatus.lastTravelTypeGoalSupportGoalArea = 0;
}

void BotNavRecordNaturalMovementSupportType(
	int travelType,
	int unsupportedMask,
	int *supported,
	int *unsupported,
	int *reason,
	int *area,
	int *goalArea,
	int *originX,
	int *originY,
	int *originZ) {
	float startOrigin[3] = {};
	int startArea = 0;
	int goalAreaValue = 0;

	botNavRouteStatus.naturalMovementSupportChecks++;
	if (!BotLibAdapter_FindRouteStartForTravelType(travelType, startOrigin, &startArea, &goalAreaValue)) {
		botNavRouteStatus.naturalMovementUnsupported++;
		botNavRouteStatus.naturalMovementUnsupportedMask |= unsupportedMask;
		if (supported != nullptr) {
			*supported = 0;
		}
		if (unsupported != nullptr) {
			*unsupported = 1;
		}
		if (reason != nullptr) {
			*reason = static_cast<int>(BotNavNaturalMovementSupportReason::NoRouteStart);
		}
		if (area != nullptr) {
			*area = 0;
		}
		if (goalArea != nullptr) {
			*goalArea = 0;
		}
		if (originX != nullptr) {
			*originX = 0;
		}
		if (originY != nullptr) {
			*originY = 0;
		}
		if (originZ != nullptr) {
			*originZ = 0;
		}
		return;
	}

	if (startArea > 0 && goalAreaValue > 0) {
		botNavRouteStatus.naturalMovementSupported++;
		if (supported != nullptr) {
			*supported = 1;
		}
		if (unsupported != nullptr) {
			*unsupported = 0;
		}
		if (reason != nullptr) {
			*reason = static_cast<int>(BotNavNaturalMovementSupportReason::Supported);
		}
		if (area != nullptr) {
			*area = startArea;
		}
		if (goalArea != nullptr) {
			*goalArea = goalAreaValue;
		}
		if (originX != nullptr) {
			*originX = BotNavStatusCoord(startOrigin[0]);
		}
		if (originY != nullptr) {
			*originY = BotNavStatusCoord(startOrigin[1]);
		}
		if (originZ != nullptr) {
			*originZ = BotNavStatusCoord(startOrigin[2]);
		}
		return;
	}

	botNavRouteStatus.naturalMovementUnsupported++;
	botNavRouteStatus.naturalMovementUnsupportedMask |= unsupportedMask;
	if (supported != nullptr) {
		*supported = 0;
	}
	if (unsupported != nullptr) {
		*unsupported = 1;
	}
	if (reason != nullptr) {
		*reason = static_cast<int>(BotNavNaturalMovementSupportReason::InvalidRouteAreas);
	}
	if (area != nullptr) {
		*area = 0;
	}
	if (goalArea != nullptr) {
		*goalArea = 0;
	}
	if (originX != nullptr) {
		*originX = 0;
	}
	if (originY != nullptr) {
		*originY = 0;
	}
	if (originZ != nullptr) {
		*originZ = 0;
	}
}

void BotNavUpdateNaturalMovementSupportStatus() {
	const BotLibAdapterStatus &adapterStatus = BotLibAdapter_GetStatus();
	botNavRouteStatus.naturalMovementSupportAasLoaded = adapterStatus.q3aAasLoaded ? 1 : 0;
	if (!adapterStatus.q3aAasLoaded) {
		botNavRouteStatus.naturalMovementSupportChecks = 0;
		botNavRouteStatus.naturalMovementSupported = 0;
		botNavRouteStatus.naturalMovementUnsupported = 3;
		botNavRouteStatus.naturalMovementUnsupportedMask =
			BOT_NAV_NATURAL_CROUCH_MASK |
			BOT_NAV_NATURAL_SWIM_MASK |
			BOT_NAV_NATURAL_WATER_JUMP_MASK;
		botNavRouteStatus.naturalMovementCrouchSupported = 0;
		botNavRouteStatus.naturalMovementCrouchUnsupported = 1;
		botNavRouteStatus.naturalMovementCrouchReason =
			static_cast<int>(BotNavNaturalMovementSupportReason::AasNotLoaded);
		botNavRouteStatus.naturalMovementSwimSupported = 0;
		botNavRouteStatus.naturalMovementSwimUnsupported = 1;
		botNavRouteStatus.naturalMovementSwimReason =
			static_cast<int>(BotNavNaturalMovementSupportReason::AasNotLoaded);
		botNavRouteStatus.naturalMovementWaterJumpSupported = 0;
		botNavRouteStatus.naturalMovementWaterJumpUnsupported = 1;
		botNavRouteStatus.naturalMovementWaterJumpReason =
			static_cast<int>(BotNavNaturalMovementSupportReason::AasNotLoaded);
		return;
	}

	if (botNavNaturalMovementSupportChecked) {
		return;
	}

	botNavRouteStatus.naturalMovementSupportChecks = 0;
	botNavRouteStatus.naturalMovementSupported = 0;
	botNavRouteStatus.naturalMovementUnsupported = 0;
	botNavRouteStatus.naturalMovementUnsupportedMask = 0;
	BotNavRecordNaturalMovementSupportType(
		BOT_NAV_TRAVEL_CROUCH,
		BOT_NAV_NATURAL_CROUCH_MASK,
		&botNavRouteStatus.naturalMovementCrouchSupported,
		&botNavRouteStatus.naturalMovementCrouchUnsupported,
		&botNavRouteStatus.naturalMovementCrouchReason,
		&botNavRouteStatus.naturalMovementCrouchArea,
		&botNavRouteStatus.naturalMovementCrouchGoalArea,
		&botNavRouteStatus.naturalMovementCrouchOriginX,
		&botNavRouteStatus.naturalMovementCrouchOriginY,
		&botNavRouteStatus.naturalMovementCrouchOriginZ);
	BotNavRecordNaturalMovementSupportType(
		BOT_NAV_TRAVEL_SWIM,
		BOT_NAV_NATURAL_SWIM_MASK,
		&botNavRouteStatus.naturalMovementSwimSupported,
		&botNavRouteStatus.naturalMovementSwimUnsupported,
		&botNavRouteStatus.naturalMovementSwimReason,
		&botNavRouteStatus.naturalMovementSwimArea,
		&botNavRouteStatus.naturalMovementSwimGoalArea,
		&botNavRouteStatus.naturalMovementSwimOriginX,
		&botNavRouteStatus.naturalMovementSwimOriginY,
		&botNavRouteStatus.naturalMovementSwimOriginZ);
	BotNavRecordNaturalMovementSupportType(
		BOT_NAV_TRAVEL_WATER_JUMP,
		BOT_NAV_NATURAL_WATER_JUMP_MASK,
		&botNavRouteStatus.naturalMovementWaterJumpSupported,
		&botNavRouteStatus.naturalMovementWaterJumpUnsupported,
		&botNavRouteStatus.naturalMovementWaterJumpReason,
		&botNavRouteStatus.naturalMovementWaterJumpArea,
		&botNavRouteStatus.naturalMovementWaterJumpGoalArea,
		&botNavRouteStatus.naturalMovementWaterJumpOriginX,
		&botNavRouteStatus.naturalMovementWaterJumpOriginY,
		&botNavRouteStatus.naturalMovementWaterJumpOriginZ);
	botNavNaturalMovementSupportChecked = true;
}

void BotNavActivateGoalBlacklist(BotNavRouteSlot &slot, int clientIndex, uint32_t frame);

void BotNavActivateRecovery(
	const gentity_t *bot,
	BotNavRouteSlot &slot,
	int clientIndex,
	uint32_t frame) {
	if (slot.recoverySideSign == 0) {
		slot.recoverySideSign = (clientIndex & 1) == 0 ? 1 : -1;
	} else {
		slot.recoverySideSign = -slot.recoverySideSign;
	}

	slot.recoveryUntilFrame = frame + BOT_NAV_STUCK_RECOVERY_FRAMES;
	slot.recoveryMoveValid = false;
	slot.recoveryMoveDirection = vec3_origin;
	slot.recoveryProbeCandidate = -1;
	slot.recoveryProbeFraction = 0;
	(void)BotNavEnsureRecoveryMove(bot, slot, nullptr);
	botNavRouteStatus.stuckRecoveryActivations++;
	botNavRouteStatus.lastStuckRecoveryClient = clientIndex;
	botNavRouteStatus.lastStuckRecoverySide = slot.recoverySideSign;
	botNavRouteStatus.lastStuckRecoveryFramesRemaining = static_cast<int>(BOT_NAV_STUCK_RECOVERY_FRAMES);
}

bool BotNavCheckStuckProgress(BotNavRouteSlot &slot, const gentity_t *bot, int clientIndex, uint32_t frame) {
	if (!slot.valid || slot.route.goalArea <= 0) {
		BotNavResetProgress(slot);
		BotNavClearRecovery(slot);
		return false;
	}

	const Vector3 routeTarget = BotNavRouteTarget(slot.route);
	const float distanceSquared = BotNavHorizontalDistanceSquared(bot->s.origin, BotNavRouteGoal(slot.route));
	const float targetDistanceSquared = BotNavHorizontalDistanceSquared(bot->s.origin, routeTarget);
	slot.lastStuckDistanceSq = BotNavStatusDistance(distanceSquared);
	botNavRouteStatus.stuckChecks++;
	botNavRouteStatus.lastStuckClient = clientIndex;
	botNavRouteStatus.lastStuckDistanceSq = slot.lastStuckDistanceSq;
	botNavRouteStatus.lastStuckTargetDistanceSq = BotNavStatusDistance(targetDistanceSquared);
	botNavRouteStatus.lastStuckConsumedTarget = 0;

	if (slot.progressGoalArea != slot.route.goalArea ||
		slot.lastProgressDistanceSquared < 0.0f ||
		slot.lastProgressTargetDistanceSquared < 0.0f) {
		slot.progressGoalArea = slot.route.goalArea;
		slot.lastProgressDistanceSquared = distanceSquared;
		slot.lastProgressTargetDistanceSquared = targetDistanceSquared;
		slot.progressRouteTarget = routeTarget;
		slot.stagnantProgressFrames = 0;
		slot.lastStuckProgressDelta = 0;
		botNavRouteStatus.lastStuckProgressDelta = 0;
		botNavRouteStatus.lastStuckFrames = 0;
		return false;
	}

	const float progressDelta = slot.lastProgressDistanceSquared - distanceSquared;
	float targetProgressDelta = 0.0f;
	const bool targetShifted =
		BotNavHorizontalDistanceSquared(slot.progressRouteTarget, routeTarget) >
			BOT_NAV_ROUTE_PROGRESS_TARGET_SHIFT_DIST_SQUARED;
	const bool targetReached =
		targetDistanceSquared <= BOT_NAV_TARGET_REACHED_DIST_SQUARED;
	const bool targetPreviouslyReached =
		!targetShifted &&
		slot.lastProgressTargetDistanceSquared >= 0.0f &&
		slot.lastProgressTargetDistanceSquared <= BOT_NAV_TARGET_REACHED_DIST_SQUARED;
	if (targetShifted) {
		slot.progressRouteTarget = routeTarget;
		slot.lastProgressTargetDistanceSquared = targetDistanceSquared;
	} else {
		targetProgressDelta =
			slot.lastProgressTargetDistanceSquared - targetDistanceSquared;
	}

	const float bestProgressDelta = std::max(progressDelta, targetProgressDelta);
	slot.lastStuckProgressDelta = static_cast<int>(bestProgressDelta);
	botNavRouteStatus.lastStuckProgressDelta = slot.lastStuckProgressDelta;
	const bool goalReached = distanceSquared <= BOT_NAV_GOAL_REACHED_DIST_SQUARED;
	const bool targetReachedIsProgress = targetReached && !targetPreviouslyReached;
	if (targetReached &&
		targetPreviouslyReached &&
		bestProgressDelta < BOT_NAV_STUCK_MIN_PROGRESS_DELTA_SQUARED &&
		!goalReached) {
		botNavRouteStatus.stuckConsumedTargetStalls++;
		botNavRouteStatus.lastStuckConsumedTarget = 1;
	}
	if (bestProgressDelta >= BOT_NAV_STUCK_MIN_PROGRESS_DELTA_SQUARED ||
		goalReached ||
		targetReachedIsProgress) {
		if (targetReachedIsProgress &&
			bestProgressDelta < BOT_NAV_STUCK_MIN_PROGRESS_DELTA_SQUARED &&
			!goalReached) {
			botNavRouteStatus.stuckTargetReachedProgresses++;
		}
		slot.lastProgressDistanceSquared = distanceSquared;
		slot.lastProgressTargetDistanceSquared = targetDistanceSquared;
		slot.progressRouteTarget = routeTarget;
		slot.stagnantProgressFrames = 0;
		slot.lastStuckProgressDelta = 0;
		botNavRouteStatus.lastStuckFrames = 0;
		return false;
	}

	slot.stagnantProgressFrames++;
	botNavRouteStatus.stuckStalls++;
	botNavRouteStatus.lastStuckFrames = slot.stagnantProgressFrames;
	if (slot.stagnantProgressFrames < BOT_NAV_STUCK_FRAME_THRESHOLD ||
		frame < slot.nextStuckRepathFrame) {
		return false;
	}

	slot.nextStuckRepathFrame = frame + BOT_NAV_STUCK_REPATH_COOLDOWN_FRAMES;
	slot.stagnantProgressFrames = 0;
	slot.lastProgressDistanceSquared = distanceSquared;
	slot.lastProgressTargetDistanceSquared = targetDistanceSquared;
	slot.progressRouteTarget = routeTarget;
	botNavRouteStatus.stuckDetections++;
	slot.lastStuckReason = static_cast<int>(BotNavStuckReason::NoGoalProgress);
	botNavRouteStatus.lastStuckReason = slot.lastStuckReason;
	if (!BotNavActivateInteractionRetry(slot, bot, clientIndex, frame, slot.route, true)) {
		BotNavActivateRecovery(bot, slot, clientIndex, frame);
		BotNavActivateGoalBlacklist(slot, clientIndex, frame);
	}
	return true;
}

bool BotNavIsActivePickup(const gentity_t *ent) {
	if (ent == nullptr || !ent->inUse || ent->item == nullptr || ent->item->pickup == nullptr) {
		return false;
	}
	if ((ent->svFlags & SVF_NOCLIENT) != 0 || (ent->flags & FL_NO_BOTS) != 0) {
		return false;
	}
	if (ent->solid != SOLID_TRIGGER) {
		return false;
	}
	if (ent->spawnFlags.has(SPAWNFLAG_ITEM_NO_TOUCH) ||
		ent->spawnFlags.has(SPAWNFLAG_ITEM_TRIGGER_SPAWN)) {
		return false;
	}

	return true;
}

bool BotNavItemGoalStillAvailable(const BotNavRouteSlot &slot) {
	if (slot.persistentGoalEntityNumber < 0 ||
		slot.persistentGoalEntityNumber >= static_cast<int>(globals.numEntities) ||
		g_entities == nullptr) {
		return false;
	}

	const gentity_t *ent = &g_entities[slot.persistentGoalEntityNumber];
	return ent->spawn_count == slot.persistentGoalEntitySpawnCount &&
		ent->item != nullptr &&
		ent->item->id == slot.persistentGoalItem &&
		BotNavIsActivePickup(ent);
}

bool BotNavItemGoalMatches(
	int entityNumber,
	int spawnCount,
	item_id_t item,
	int candidateEntityNumber,
	const gentity_t *candidate) {
	return candidate != nullptr &&
		candidate->item != nullptr &&
		entityNumber == candidateEntityNumber &&
		spawnCount == candidate->spawn_count &&
		item == candidate->item->id;
}

bool BotNavItemGoalBlacklistActive(const BotNavRouteSlot &slot, uint32_t frame) {
	return slot.blacklistedGoalEntityNumber >= 0 &&
		frame < slot.blacklistedGoalUntilFrame;
}

int BotNavBlacklistFramesRemaining(const BotNavRouteSlot &slot, uint32_t frame) {
	if (!BotNavItemGoalBlacklistActive(slot, frame)) {
		return 0;
	}

	return static_cast<int>(slot.blacklistedGoalUntilFrame - frame);
}

bool BotNavItemGoalIsBlacklisted(
	const BotNavRouteSlot &slot,
	int entityNumber,
	const gentity_t *ent,
	uint32_t frame) {
	return BotNavItemGoalBlacklistActive(slot, frame) &&
		BotNavItemGoalMatches(
			slot.blacklistedGoalEntityNumber,
			slot.blacklistedGoalEntitySpawnCount,
			slot.blacklistedGoalItem,
			entityNumber,
			ent);
}

bool BotNavCurrentItemGoalIsBlacklisted(const BotNavRouteSlot &slot, uint32_t frame) {
	if (slot.persistentGoalEntityNumber < 0 || !BotNavItemGoalBlacklistActive(slot, frame)) {
		return false;
	}

	return slot.blacklistedGoalEntityNumber == slot.persistentGoalEntityNumber &&
		slot.blacklistedGoalEntitySpawnCount == slot.persistentGoalEntitySpawnCount &&
		slot.blacklistedGoalItem == slot.persistentGoalItem;
}

void BotNavActivateGoalBlacklist(BotNavRouteSlot &slot, int clientIndex, uint32_t frame) {
	if (!BotNavItemGoalStillAvailable(slot)) {
		return;
	}

	slot.blacklistedGoalUntilFrame = frame + BOT_NAV_GOAL_BLACKLIST_FRAMES;
	slot.blacklistedGoalEntityNumber = slot.persistentGoalEntityNumber;
	slot.blacklistedGoalEntitySpawnCount = slot.persistentGoalEntitySpawnCount;
	slot.blacklistedGoalItem = slot.persistentGoalItem;

	botNavRouteStatus.itemGoalBlacklistActivations++;
	botNavRouteStatus.lastItemGoalBlacklistedEntity = slot.blacklistedGoalEntityNumber;
	botNavRouteStatus.lastItemGoalBlacklistedByClient = clientIndex;
	botNavRouteStatus.lastItemGoalBlacklistFramesRemaining = static_cast<int>(BOT_NAV_GOAL_BLACKLIST_FRAMES);
}

int BotNavItemReservationOwner(int clientIndex, int entityNumber) {
	if (entityNumber < 0) {
		return -1;
	}

	const int clientCount = std::min(
		static_cast<int>(botNavRouteSlots.size()),
		static_cast<int>(game.maxClients));
	for (int owner = 0; owner < clientCount; ++owner) {
		if (owner == clientIndex) {
			continue;
		}

		const BotNavRouteSlot &slot = botNavRouteSlots[owner];
		if (slot.persistentGoalEntityNumber == entityNumber &&
			BotNavItemGoalStillAvailable(slot)) {
			return owner;
		}
	}

	return -1;
}

void BotNavUpdateActiveGoalBlacklists() {
	int active = 0;
	const uint32_t frame = gi.ServerFrame();
	const int clientCount = std::min(
		static_cast<int>(botNavRouteSlots.size()),
		static_cast<int>(game.maxClients));
	for (int clientIndex = 0; clientIndex < clientCount; ++clientIndex) {
		if (BotNavItemGoalBlacklistActive(botNavRouteSlots[clientIndex], frame)) {
			active++;
		}
	}

	botNavRouteStatus.itemGoalBlacklistActive = active;
}

void BotNavUpdateActiveReservations() {
	int reservations = 0;
	const int clientCount = std::min(
		static_cast<int>(botNavRouteSlots.size()),
		static_cast<int>(game.maxClients));
	for (int clientIndex = 0; clientIndex < clientCount; ++clientIndex) {
		if (BotNavItemGoalStillAvailable(botNavRouteSlots[clientIndex])) {
			reservations++;
		}
	}

	botNavRouteStatus.itemGoalActiveReservations = reservations;
	botNavRouteStatus.itemGoalPeakActiveReservations =
		std::max(botNavRouteStatus.itemGoalPeakActiveReservations, reservations);
}

uint32_t BotNavNextItemDesirabilityFrame(int clientIndex, uint32_t frame) {
	return frame +
		BOT_NAV_ITEM_DESIRABILITY_STAGGER_FRAMES +
		static_cast<uint32_t>(std::max(0, clientIndex) % BOT_NAV_ITEM_DESIRABILITY_STAGGER_FRAMES);
}

bool BotNavCachedItemGoalStillAvailable(
	const BotNavRouteSlot &slot,
	int clientIndex,
	uint32_t frame,
	BotNavItemGoalCandidate *candidate) {
	if (!slot.cachedItemGoalValid ||
		slot.cachedItemGoal.entityNumber < 0 ||
		g_entities == nullptr ||
		slot.cachedItemGoal.entityNumber >= static_cast<int>(globals.numEntities)) {
		return false;
	}

	const gentity_t *ent = &g_entities[slot.cachedItemGoal.entityNumber];
	if (!BotNavIsActivePickup(ent) ||
		!BotNavItemGoalMatches(
			slot.cachedItemGoal.entityNumber,
			slot.cachedItemGoal.spawnCount,
			slot.cachedItemGoal.item,
			slot.cachedItemGoal.entityNumber,
			ent) ||
		BotNavItemGoalIsBlacklisted(slot, slot.cachedItemGoal.entityNumber, ent, frame) ||
		BotNavItemReservationOwner(clientIndex, slot.cachedItemGoal.entityNumber) >= 0) {
		return false;
	}

	if (candidate != nullptr) {
		*candidate = slot.cachedItemGoal;
	}
	return true;
}

bool BotNavUseCachedItemGoalIfFresh(
	BotNavRouteSlot &slot,
	int clientIndex,
	uint32_t frame,
	BotNavItemGoalCandidate *candidate) {
	if (frame >= slot.nextItemDesirabilityFrame) {
		return false;
	}
	if (!BotNavCachedItemGoalStillAvailable(slot, clientIndex, frame, candidate)) {
		slot.cachedItemGoalValid = false;
		return false;
	}

	botNavRouteStatus.itemGoalDesirabilityStaggerDeferrals++;
	botNavRouteStatus.itemGoalDesirabilityCacheReuses++;
	botNavRouteStatus.lastItemGoalDesirabilityClient = clientIndex;
	botNavRouteStatus.lastItemGoalDesirabilityNextFrame =
		static_cast<int>(slot.nextItemDesirabilityFrame);
	return true;
}

bool BotNavFindPickupGoal(const gentity_t *bot, int clientIndex, uint32_t frame, BotNavItemGoalCandidate *candidate) {
	if (bot == nullptr || bot->client == nullptr || g_entities == nullptr) {
		return false;
	}

	BotNavRouteSlot &routeSlot = botNavRouteSlots[clientIndex];
	if (BotNavUseCachedItemGoalIfFresh(routeSlot, clientIndex, frame, candidate)) {
		return true;
	}

	BotNavItemGoalCandidate best{};
	int bestScore = -1;
	const BotNavItemFocusMode focusMode = BotNavSmokeItemFocusMode();
	const bool coopResourceShare = BotNavCoopResourceShareEnabled();
	const bool ffaItemRoles = BotNavFfaItemRolesEnabled();
	const bool ctfItemRoles = BotNavCtfItemRolesEnabled();
	const bool teamItemRoles = BotNavTeamItemRolesEnabled();
	const bool teamResourceDenial = BotNavTeamResourceDenialEnabled();
	const bool itemRolePolicies = ffaItemRoles || ctfItemRoles || teamItemRoles;
	BotObjectiveMatchPolicy matchPolicy{};
	BotObjectiveCoopPolicy coopPolicy{};
	if (coopResourceShare || itemRolePolicies || teamResourceDenial) {
		const BotObjectiveMatchContext matchContext =
			BotObjectives_BuildMatchContext(bot, BotObjectiveRole::None);
		matchPolicy = BotObjectives_EvaluateMatchPolicy(matchContext);
	}
	BotNavItemRoleScope itemRoleScope = BotNavItemRoleScope::None;
	if (ffaItemRoles &&
		(matchPolicy.mode == BotObjectiveMatchMode::FreeForAll ||
		 matchPolicy.mode == BotObjectiveMatchMode::Duel)) {
		itemRoleScope = BotNavItemRoleScope::FreeForAll;
	} else if (ctfItemRoles &&
		matchPolicy.mode == BotObjectiveMatchMode::CaptureTheFlag) {
		itemRoleScope = BotNavItemRoleScope::CaptureTheFlag;
	} else if (teamItemRoles) {
		itemRoleScope = BotNavItemRoleScope::Team;
	}
	if (coopResourceShare) {
		const BotObjectiveCoopContext coopContext =
			BotObjectives_BuildCoopContext(
				bot,
				nullptr,
				false,
				BotObjectiveRole::None);
		coopPolicy = BotObjectives_EvaluateCoopPolicy(coopContext);
	}
	botNavRouteStatus.itemGoalDesirabilityUpdates++;
	botNavRouteStatus.lastItemGoalDesirabilityClient = clientIndex;
	botNavRouteStatus.itemGoalScans++;

	const uint32_t start = std::min<uint32_t>(game.maxClients + 1, globals.numEntities);
	for (uint32_t entnum = start; entnum < globals.numEntities; ++entnum) {
		const gentity_t *ent = &g_entities[entnum];
		if (!BotNavIsActivePickup(ent)) {
			continue;
		}

		const BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
		if (BotNavItemGoalIsBlacklisted(slot, static_cast<int>(entnum), ent, frame)) {
			botNavRouteStatus.itemGoalBlacklistSkips++;
			botNavRouteStatus.lastItemGoalBlacklistedEntity = static_cast<int>(entnum);
			botNavRouteStatus.lastItemGoalBlacklistedByClient = clientIndex;
			botNavRouteStatus.lastItemGoalBlacklistFramesRemaining = BotNavBlacklistFramesRemaining(slot, frame);
			continue;
		}

		const int reservationOwner = BotNavItemReservationOwner(clientIndex, static_cast<int>(entnum));
		if (reservationOwner >= 0) {
			botNavRouteStatus.itemGoalReservationSkips++;
			botNavRouteStatus.lastItemGoalReservedEntity = static_cast<int>(entnum);
			botNavRouteStatus.lastItemGoalReservedByClient = reservationOwner;
			BotItemContext reservedContext =
				BotItems_BuildContextForEntity(bot, ent, 0, true, BotItemFocus::None);
			if (BotNavItemFocusAllowsKind(focusMode, reservedContext.candidateKind)) {
				reservedContext.focus =
					BotNavItemFocusForKind(focusMode, reservedContext.candidateKind);
				(void)BotItems_Evaluate(reservedContext);
			}
			continue;
		}

		const float origin[3] = {
			ent->s.origin.x,
			ent->s.origin.y,
			ent->s.origin.z
		};
		float routeOrigin[3] = {};
		int area = 0;
		if (!BotLibAdapter_FindRouteAreaForPoint(origin, &area, routeOrigin) || area <= 0) {
			continue;
		}

		BotItemContext itemContext =
			BotItems_BuildContextForEntity(bot, ent, 0, false, BotItemFocus::None);
		if (!BotNavItemFocusAllowsKind(focusMode, itemContext.candidateKind)) {
			continue;
		}

		itemContext.focus = BotNavItemFocusForKind(focusMode, itemContext.candidateKind);
		const BotObjectiveItemCategory itemCategory =
			BotObjectives_ItemCategoryForItem(ent->item);
		if (coopResourceShare) {
			itemContext = BotNavApplyCoopResourceSharePolicy(
				matchPolicy,
				coopPolicy,
				itemCategory,
				itemContext);
		}
		BotObjectiveItemRolePolicy itemRolePolicy{};
		BotObjectiveResourcePolicy resourceDenialPolicy{};
		if (teamResourceDenial) {
			itemContext = BotNavApplyTeamResourceDenialPolicy(
				matchPolicy,
				itemCategory,
				itemContext,
				&resourceDenialPolicy);
		}
		if (itemRoleScope != BotNavItemRoleScope::None) {
			itemContext = BotNavApplyItemRolePolicy(
				matchPolicy,
				itemCategory,
				itemContext,
				itemRoleScope,
				&itemRolePolicy);
		}
		const BotItemDecision decision = BotItems_Evaluate(itemContext);
		if (decision.kind != BotItemDecisionKind::SeekCandidate || decision.priority <= 0) {
			continue;
		}

		const int distancePenalty = static_cast<int>(
			BotNavHorizontalDistanceSquared(bot->s.origin, ent->s.origin) / (128.0f * 128.0f));
		const int score = decision.priority - distancePenalty;
		botNavRouteStatus.itemGoalCandidates++;
		if (score <= bestScore) {
			continue;
		}

		bestScore = score;
		best.entityNumber = static_cast<int>(entnum);
		best.spawnCount = ent->spawn_count;
		best.item = static_cast<item_id_t>(decision.item);
		best.area = area;
		best.score = score;
		if (itemRolePolicy.valid) {
			if (itemRoleScope == BotNavItemRoleScope::FreeForAll) {
				best.ffaItemRoleValid = true;
				best.ffaItemRoleMode = static_cast<int>(itemRolePolicy.mode);
				best.ffaItemRoleRole = static_cast<int>(itemRolePolicy.role);
				best.ffaItemRoleLane = static_cast<int>(itemRolePolicy.lane);
				best.ffaItemRoleCategory = static_cast<int>(itemRolePolicy.category);
				best.ffaItemRoleItemRole = static_cast<int>(itemRolePolicy.itemRole);
				best.ffaItemRolePriority = itemRolePolicy.priority;
				best.ffaItemRoleScoreBoost = std::max(0, itemRolePolicy.priority);
				best.ffaItemRoleProfileItemBonus = itemRolePolicy.profileItemPolicyBonus;
			} else if (itemRoleScope == BotNavItemRoleScope::CaptureTheFlag) {
				best.ctfItemRoleValid = true;
				best.ctfItemRoleMode = static_cast<int>(itemRolePolicy.mode);
				best.ctfItemRoleRole = static_cast<int>(itemRolePolicy.role);
				best.ctfItemRoleLane = static_cast<int>(itemRolePolicy.lane);
				best.ctfItemRoleCategory = static_cast<int>(itemRolePolicy.category);
				best.ctfItemRoleItemRole = static_cast<int>(itemRolePolicy.itemRole);
				best.ctfItemRolePriority = itemRolePolicy.priority;
				best.ctfItemRoleScoreBoost = std::max(0, itemRolePolicy.priority);
				best.ctfItemRoleProfileItemBonus = itemRolePolicy.profileItemPolicyBonus;
			} else {
				best.teamItemRoleValid = true;
				best.teamItemRoleMode = static_cast<int>(itemRolePolicy.mode);
				best.teamItemRoleRole = static_cast<int>(itemRolePolicy.role);
				best.teamItemRoleLane = static_cast<int>(itemRolePolicy.lane);
				best.teamItemRoleCategory = static_cast<int>(itemRolePolicy.category);
				best.teamItemRoleItemRole = static_cast<int>(itemRolePolicy.itemRole);
				best.teamItemRolePriority = itemRolePolicy.priority;
				best.teamItemRoleScoreBoost = std::max(0, itemRolePolicy.priority);
				best.teamItemRoleProfileItemBonus = itemRolePolicy.profileItemPolicyBonus;
			}
		}
		if (resourceDenialPolicy.valid && resourceDenialPolicy.denyEnemyPickup) {
			best.teamResourceDenialValid = true;
			best.teamResourceDenialMode = static_cast<int>(resourceDenialPolicy.mode);
			best.teamResourceDenialRole = static_cast<int>(resourceDenialPolicy.role);
			best.teamResourceDenialLane = static_cast<int>(resourceDenialPolicy.lane);
			best.teamResourceDenialCategory = static_cast<int>(resourceDenialPolicy.category);
			best.teamResourceDenialIntent = static_cast<int>(resourceDenialPolicy.intent);
			best.teamResourceDenialPriority = resourceDenialPolicy.priority;
			best.teamResourceDenialScoreBoost = std::max(0, resourceDenialPolicy.priority);
			best.teamResourceDenialProfileItemBonus = resourceDenialPolicy.profileItemPolicyBonus;
		}
		best.origin = { routeOrigin[0], routeOrigin[1], routeOrigin[2] };
	}

	if (bestScore < 0) {
		routeSlot.cachedItemGoalValid = false;
		routeSlot.cachedItemGoal = {};
		routeSlot.nextItemDesirabilityFrame = BotNavNextItemDesirabilityFrame(clientIndex, frame);
		botNavRouteStatus.lastItemGoalDesirabilityNextFrame =
			static_cast<int>(routeSlot.nextItemDesirabilityFrame);
		return false;
	}

	if (candidate != nullptr) {
		*candidate = best;
	}
	routeSlot.cachedItemGoalValid = true;
	routeSlot.cachedItemGoal = best;
	routeSlot.nextItemDesirabilityFrame = BotNavNextItemDesirabilityFrame(clientIndex, frame);
	botNavRouteStatus.lastItemGoalDesirabilityNextFrame =
		static_cast<int>(routeSlot.nextItemDesirabilityFrame);
	if (best.ffaItemRoleValid) {
		botNavRouteStatus.ffaItemRoleSelectedGoals++;
		botNavRouteStatus.lastFfaItemRoleClient = clientIndex;
		botNavRouteStatus.lastFfaItemRoleMode = best.ffaItemRoleMode;
		botNavRouteStatus.lastFfaItemRoleRole = best.ffaItemRoleRole;
		botNavRouteStatus.lastFfaItemRoleLane = best.ffaItemRoleLane;
		botNavRouteStatus.lastFfaItemRoleCategory = best.ffaItemRoleCategory;
		botNavRouteStatus.lastFfaItemRoleItemRole = best.ffaItemRoleItemRole;
		botNavRouteStatus.lastFfaItemRolePriority = best.ffaItemRolePriority;
		botNavRouteStatus.lastFfaItemRoleScoreBoost = best.ffaItemRoleScoreBoost;
		botNavRouteStatus.lastFfaItemRoleProfileItemBonus =
			best.ffaItemRoleProfileItemBonus;
		botNavRouteStatus.lastFfaItemRoleEntity = best.entityNumber;
		botNavRouteStatus.lastFfaItemRoleItem = static_cast<int>(best.item);
		botNavRouteStatus.lastFfaItemRoleScore = best.score;
	}
	if (best.ctfItemRoleValid) {
		botNavRouteStatus.ctfItemRoleSelectedGoals++;
		botNavRouteStatus.lastCtfItemRoleClient = clientIndex;
		botNavRouteStatus.lastCtfItemRoleMode = best.ctfItemRoleMode;
		botNavRouteStatus.lastCtfItemRoleRole = best.ctfItemRoleRole;
		botNavRouteStatus.lastCtfItemRoleLane = best.ctfItemRoleLane;
		botNavRouteStatus.lastCtfItemRoleCategory = best.ctfItemRoleCategory;
		botNavRouteStatus.lastCtfItemRoleItemRole = best.ctfItemRoleItemRole;
		botNavRouteStatus.lastCtfItemRolePriority = best.ctfItemRolePriority;
		botNavRouteStatus.lastCtfItemRoleScoreBoost = best.ctfItemRoleScoreBoost;
		botNavRouteStatus.lastCtfItemRoleProfileItemBonus =
			best.ctfItemRoleProfileItemBonus;
		botNavRouteStatus.lastCtfItemRoleEntity = best.entityNumber;
		botNavRouteStatus.lastCtfItemRoleItem = static_cast<int>(best.item);
		botNavRouteStatus.lastCtfItemRoleScore = best.score;
	}
	if (best.teamItemRoleValid) {
		botNavRouteStatus.teamItemRoleSelectedGoals++;
		botNavRouteStatus.lastTeamItemRoleClient = clientIndex;
		botNavRouteStatus.lastTeamItemRoleMode = best.teamItemRoleMode;
		botNavRouteStatus.lastTeamItemRoleRole = best.teamItemRoleRole;
		botNavRouteStatus.lastTeamItemRoleLane = best.teamItemRoleLane;
		botNavRouteStatus.lastTeamItemRoleCategory = best.teamItemRoleCategory;
		botNavRouteStatus.lastTeamItemRoleItemRole = best.teamItemRoleItemRole;
		botNavRouteStatus.lastTeamItemRolePriority = best.teamItemRolePriority;
		botNavRouteStatus.lastTeamItemRoleScoreBoost = best.teamItemRoleScoreBoost;
		botNavRouteStatus.lastTeamItemRoleProfileItemBonus =
			best.teamItemRoleProfileItemBonus;
		botNavRouteStatus.lastTeamItemRoleEntity = best.entityNumber;
		botNavRouteStatus.lastTeamItemRoleItem = static_cast<int>(best.item);
		botNavRouteStatus.lastTeamItemRoleScore = best.score;
	}
	if (best.teamResourceDenialValid) {
		botNavRouteStatus.teamResourceDenialSelectedGoals++;
		botNavRouteStatus.lastTeamResourceDenialClient = clientIndex;
		botNavRouteStatus.lastTeamResourceDenialMode = best.teamResourceDenialMode;
		botNavRouteStatus.lastTeamResourceDenialRole = best.teamResourceDenialRole;
		botNavRouteStatus.lastTeamResourceDenialLane = best.teamResourceDenialLane;
		botNavRouteStatus.lastTeamResourceDenialCategory = best.teamResourceDenialCategory;
		botNavRouteStatus.lastTeamResourceDenialIntent = best.teamResourceDenialIntent;
		botNavRouteStatus.lastTeamResourceDenialPriority = best.teamResourceDenialPriority;
		botNavRouteStatus.lastTeamResourceDenialScoreBoost = best.teamResourceDenialScoreBoost;
		botNavRouteStatus.lastTeamResourceDenialProfileItemBonus =
			best.teamResourceDenialProfileItemBonus;
		botNavRouteStatus.lastTeamResourceDenialEntity = best.entityNumber;
		botNavRouteStatus.lastTeamResourceDenialItem = static_cast<int>(best.item);
		botNavRouteStatus.lastTeamResourceDenialScore = best.score;
	}
	return true;
}

bool BotNavFindPositionGoal(
	const gentity_t *bot,
	const BotNavRouteRequest *request,
	BotNavPositionGoalCandidate *candidate) {
	if (request == nullptr || !request->hasPositionGoal) {
		return false;
	}

	botNavRouteStatus.positionGoalRequests++;
	const bool hasInteractionArrivalGoal =
		request->hasInteractionArrivalGoal &&
		request->interactionArrivalEntityNumber >= 0 &&
		request->interactionArrivalKind > 0 &&
		request->interactionArrivalAction > 0;
	if (request->hasInteractionArrivalGoal) {
		botNavRouteStatus.interactionArrivalRouteRequests++;
		if (!hasInteractionArrivalGoal) {
			botNavRouteStatus.interactionArrivalRouteInvalidSkips++;
		}
	}

	float routeOrigin[3] = {};
	int area = 0;
	if (!BotLibAdapter_FindRouteAreaForPoint(request->positionGoal, &area, routeOrigin) || area <= 0) {
		if (request->hasInteractionArrivalGoal) {
			botNavRouteStatus.interactionArrivalRouteInvalidSkips++;
		}
		return false;
	}

	botNavRouteStatus.positionGoalResolved++;
	botNavRouteStatus.lastPositionGoalArea = area;
	botNavRouteStatus.lastPositionGoalX = static_cast<int>(request->positionGoal[0]);
	botNavRouteStatus.lastPositionGoalY = static_cast<int>(request->positionGoal[1]);
	botNavRouteStatus.lastPositionGoalZ = static_cast<int>(request->positionGoal[2]);

	if (candidate != nullptr) {
		candidate->area = area;
		candidate->origin = {
			request->positionGoal[0],
			request->positionGoal[1],
			request->positionGoal[2]
		};
		candidate->distanceSquared = bot != nullptr ?
			BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, candidate->origin)) :
			0;
		candidate->interactionArrivalGoal = hasInteractionArrivalGoal;
		candidate->interactionArrivalEntityNumber = request->interactionArrivalEntityNumber;
		candidate->interactionArrivalKind = request->interactionArrivalKind;
		candidate->interactionArrivalAction = request->interactionArrivalAction;
		BotNavRecordInteractionArrivalRouteGoal(*candidate, bot);
	}
	return true;
}

bool BotNavTeleporterEntityCanRouteTo(const gentity_t *ent, int *action) {
	if (action != nullptr) {
		*action = static_cast<int>(BotNavInteractionAction::None);
	}
	if (ent == nullptr ||
		!ent->inUse ||
		(ent->flags & FL_NO_BOTS) != 0 ||
		BotNavInteractionKindForEntity(ent) != static_cast<int>(BotNavInteractionKind::Teleporter) ||
		BotNavClassIs(ent, "misc_teleporter_dest") ||
		BotNavClassIs(ent, "target_teleporter")) {
		return false;
	}

	if (BotNavClassIs(ent, "teleporter_touch") ||
		BotNavClassIs(ent, "trigger_teleport")) {
		if (ent->target == nullptr || ent->target[0] == '\0' || !ent->touch) {
			return false;
		}
		if (BotNavClassIs(ent, "trigger_teleport") && ent->delay) {
			return false;
		}
		if (action != nullptr) {
			*action = BotNavInteractionActionForEntity(ent);
		}
		return true;
	}

	if (BotNavClassIs(ent, "misc_teleporter") &&
		ent->target != nullptr &&
		ent->target[0] != '\0' &&
		ent->solid != SOLID_NOT) {
		if (action != nullptr) {
			*action = static_cast<int>(BotNavInteractionAction::Wait);
		}
		return true;
	}

	return false;
}

Vector3 BotNavTeleporterEntityRouteOrigin(const gentity_t *ent) {
	if (ent == nullptr) {
		return vec3_origin;
	}
	if (!ent->linked) {
		return ent->s.origin;
	}
	return (ent->absMin + ent->absMax) * 0.5f;
}

void BotNavRecordTeleporterEntityGoal(const BotNavPositionGoalCandidate &candidate) {
	botNavRouteStatus.lastTeleporterEntityGoalEntity = candidate.entityNumber;
	botNavRouteStatus.lastTeleporterEntityGoalArea = candidate.area;
	botNavRouteStatus.lastTeleporterEntityGoalX = static_cast<int>(candidate.origin.x);
	botNavRouteStatus.lastTeleporterEntityGoalY = static_cast<int>(candidate.origin.y);
	botNavRouteStatus.lastTeleporterEntityGoalZ = static_cast<int>(candidate.origin.z);
	botNavRouteStatus.lastTeleporterEntityGoalDistanceSq = candidate.distanceSquared;
	botNavRouteStatus.lastTeleporterEntityGoalAction = candidate.action;
}

Vector3 BotNavInteractionEntityRouteOrigin(const gentity_t *ent) {
	if (ent == nullptr) {
		return vec3_origin;
	}
	if (ent->s.origin.lengthSquared() > 1.0f) {
		return ent->s.origin;
	}
	if (ent->linked) {
		return (ent->absMin + ent->absMax) * 0.5f;
	}
	return ent->s.origin;
}

bool BotNavResolveInteractionEntityRouteArea(
	const gentity_t *ent,
	int *area,
	Vector3 *origin) {
	if (area != nullptr) {
		*area = 0;
	}
	if (origin != nullptr) {
		*origin = vec3_origin;
	}
	if (ent == nullptr) {
		return false;
	}

	std::array<Vector3, 2> candidates = {
		BotNavInteractionEntityRouteOrigin(ent),
		ent->linked ? (ent->absMin + ent->absMax) * 0.5f : ent->s.origin,
	};
	for (const Vector3 &candidate : candidates) {
		const float point[3] = { candidate.x, candidate.y, candidate.z };
		float routeOrigin[3] = {};
		int routeArea = 0;
		if (!BotLibAdapter_FindRouteAreaForPoint(point, &routeArea, routeOrigin) ||
			routeArea <= 0) {
			continue;
		}
		if (area != nullptr) {
			*area = routeArea;
		}
		if (origin != nullptr) {
			*origin = { routeOrigin[0], routeOrigin[1], routeOrigin[2] };
		}
		return true;
	}
	return false;
}

void BotNavRecordInteractionGoal(const BotNavInteractionGoal &goal) {
	botNavRouteStatus.lastInteractionGoalEntity = goal.entityNumber;
	botNavRouteStatus.lastInteractionGoalKind = goal.kind;
	botNavRouteStatus.lastInteractionGoalAction = goal.action;
	botNavRouteStatus.lastInteractionGoalArea = goal.area;
	botNavRouteStatus.lastInteractionGoalX = static_cast<int>(goal.position[0]);
	botNavRouteStatus.lastInteractionGoalY = static_cast<int>(goal.position[1]);
	botNavRouteStatus.lastInteractionGoalZ = static_cast<int>(goal.position[2]);
	botNavRouteStatus.lastInteractionGoalDistanceSq = goal.distanceSquared;
	botNavRouteStatus.lastInteractionGoalDestinationDistanceSq =
		goal.destinationDistanceSquared;
}

void BotNavRecordInteractionArrivalGoal(const BotNavInteractionGoal &goal) {
	botNavRouteStatus.lastInteractionArrivalGoalEntity = goal.entityNumber;
	botNavRouteStatus.lastInteractionArrivalGoalKind = goal.kind;
	botNavRouteStatus.lastInteractionArrivalGoalAction = goal.action;
	botNavRouteStatus.lastInteractionArrivalGoalArea = goal.area;
	botNavRouteStatus.lastInteractionArrivalGoalSource = goal.source;
	botNavRouteStatus.lastInteractionArrivalGoalX = static_cast<int>(goal.position[0]);
	botNavRouteStatus.lastInteractionArrivalGoalY = static_cast<int>(goal.position[1]);
	botNavRouteStatus.lastInteractionArrivalGoalZ = static_cast<int>(goal.position[2]);
	botNavRouteStatus.lastInteractionArrivalGoalDistanceSq = goal.distanceSquared;
	botNavRouteStatus.lastInteractionArrivalGoalDestinationDistanceSq =
		goal.destinationDistanceSquared;
}

void BotNavRecordInteractionArrivalMoverEndpoint(const BotNavInteractionGoal &goal) {
	botNavRouteStatus.lastInteractionArrivalMoverEndpointEntity = goal.entityNumber;
	botNavRouteStatus.lastInteractionArrivalMoverEndpointKind = goal.kind;
	botNavRouteStatus.lastInteractionArrivalMoverEndpointAction = goal.action;
	botNavRouteStatus.lastInteractionArrivalMoverEndpointArea = goal.area;
	botNavRouteStatus.lastInteractionArrivalMoverEndpointX =
		static_cast<int>(goal.position[0]);
	botNavRouteStatus.lastInteractionArrivalMoverEndpointY =
		static_cast<int>(goal.position[1]);
	botNavRouteStatus.lastInteractionArrivalMoverEndpointZ =
		static_cast<int>(goal.position[2]);
	botNavRouteStatus.lastInteractionArrivalMoverEndpointDistanceSq =
		goal.distanceSquared;
	botNavRouteStatus.lastInteractionArrivalMoverEndpointDestinationDistanceSq =
		goal.destinationDistanceSquared;
}

void BotNavRecordInteractionArrivalRouteGoal(
	int entityNumber,
	int kind,
	int action,
	int area,
	const Vector3 &origin,
	int distanceSquared) {
	botNavRouteStatus.lastInteractionArrivalRouteEntity = entityNumber;
	botNavRouteStatus.lastInteractionArrivalRouteKind = kind;
	botNavRouteStatus.lastInteractionArrivalRouteAction = action;
	botNavRouteStatus.lastInteractionArrivalRouteArea = area;
	botNavRouteStatus.lastInteractionArrivalRouteX = static_cast<int>(origin.x);
	botNavRouteStatus.lastInteractionArrivalRouteY = static_cast<int>(origin.y);
	botNavRouteStatus.lastInteractionArrivalRouteZ = static_cast<int>(origin.z);
	botNavRouteStatus.lastInteractionArrivalRouteDistanceSq = distanceSquared;
}

void BotNavRecordInteractionArrivalRouteGoal(
	const BotNavPositionGoalCandidate &candidate,
	const gentity_t *bot) {
	if (!candidate.interactionArrivalGoal) {
		return;
	}

	const int distanceSquared = bot != nullptr ?
		BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, candidate.origin)) :
		candidate.distanceSquared;
	BotNavRecordInteractionArrivalRouteGoal(
		candidate.interactionArrivalEntityNumber,
		candidate.interactionArrivalKind,
		candidate.interactionArrivalAction,
		candidate.area,
		candidate.origin,
		distanceSquared);
}

void BotNavRecordPersistentInteractionArrivalRouteGoal(
	const BotNavRouteSlot &slot,
	const gentity_t *bot) {
	if (!slot.persistentGoalIsInteractionArrival) {
		return;
	}

	const int distanceSquared = bot != nullptr ?
		BotNavStatusDistance(BotNavHorizontalDistanceSquared(
			bot->s.origin,
			slot.persistentInteractionArrivalPosition)) :
		botNavRouteStatus.lastInteractionArrivalRouteDistanceSq;
	BotNavRecordInteractionArrivalRouteGoal(
		slot.persistentInteractionArrivalEntityNumber,
		slot.persistentInteractionArrivalKind,
		slot.persistentInteractionArrivalAction,
		slot.persistentInteractionArrivalArea,
		slot.persistentInteractionArrivalPosition,
		distanceSquared);
}

bool BotNavFindTeleporterEntityGoal(const gentity_t *bot, BotNavPositionGoalCandidate *candidate) {
	botNavRouteStatus.teleporterEntityGoalRequests++;
	if (bot == nullptr || g_entities == nullptr) {
		botNavRouteStatus.teleporterEntityGoalInvalidSkips++;
		return false;
	}

	BotNavApplyRoutePolicy();
	BotNavPositionGoalCandidate best{};
	best.distanceSquared = 0x7fffffff;
	for (uint32_t entnum = game.maxClients + 1; entnum < globals.numEntities; ++entnum) {
		const gentity_t *ent = &g_entities[entnum];
		int action = static_cast<int>(BotNavInteractionAction::None);
		if (!BotNavTeleporterEntityCanRouteTo(ent, &action)) {
			continue;
		}

		const Vector3 routePoint = BotNavTeleporterEntityRouteOrigin(ent);
		const float point[3] = { routePoint.x, routePoint.y, routePoint.z };
		float routeOrigin[3] = {};
		int area = 0;
		botNavRouteStatus.teleporterEntityGoalCandidates++;
		if (!BotLibAdapter_FindRouteAreaForPoint(point, &area, routeOrigin) || area <= 0) {
			botNavRouteStatus.teleporterEntityGoalInvalidSkips++;
			continue;
		}

		const Vector3 candidateOrigin = { routeOrigin[0], routeOrigin[1], routeOrigin[2] };
		const int distanceSquared =
			BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, candidateOrigin));
		if (distanceSquared >= best.distanceSquared) {
			continue;
		}

		best.area = area;
		best.entityNumber = static_cast<int>(entnum);
		best.action = action;
		best.distanceSquared = distanceSquared;
		best.origin = candidateOrigin;
	}

	if (best.area <= 0) {
		return false;
	}

	botNavRouteStatus.teleporterEntityGoalResolved++;
	BotNavRecordTeleporterEntityGoal(best);
	if (candidate != nullptr) {
		*candidate = best;
	}
	return true;
}

bool BotNavFindInteractionGoal(
	const gentity_t *bot,
	int requiredKind,
	BotNavInteractionGoal *goal) {
	botNavRouteStatus.interactionGoalRequests++;
	if (goal != nullptr) {
		*goal = {};
	}
	if (bot == nullptr || g_entities == nullptr || requiredKind <= 0) {
		botNavRouteStatus.interactionGoalInvalidSkips++;
		return false;
	}

	BotNavApplyRoutePolicy();
	BotNavInteractionGoal best{};
	best.distanceSquared = 0x7fffffff;
	for (uint32_t entnum = game.maxClients + 1; entnum < globals.numEntities; ++entnum) {
		const gentity_t *ent = &g_entities[entnum];
		if (ent == nullptr ||
			!ent->inUse ||
			(ent->flags & FL_NO_BOTS) != 0) {
			continue;
		}

		const int kind = BotNavInteractionKindForEntity(ent);
		const int action = BotNavInteractionActionForEntity(ent);
		if (kind != requiredKind ||
			action == static_cast<int>(BotNavInteractionAction::None)) {
			continue;
		}

		botNavRouteStatus.interactionGoalCandidates++;
		Vector3 routeOrigin = vec3_origin;
		int area = 0;
		if (!BotNavResolveInteractionEntityRouteArea(ent, &area, &routeOrigin) ||
			area <= 0) {
			botNavRouteStatus.interactionGoalInvalidSkips++;
			continue;
		}

		const int distanceSquared =
			BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, routeOrigin));
		if (distanceSquared >= best.distanceSquared) {
			continue;
		}

		best.entityNumber = static_cast<int>(entnum);
		best.kind = kind;
		best.action = action;
		best.area = area;
		best.distanceSquared = distanceSquared;
		best.position[0] = routeOrigin.x;
		best.position[1] = routeOrigin.y;
		best.position[2] = routeOrigin.z;
	}

	if (best.area <= 0) {
		return false;
	}

	botNavRouteStatus.interactionGoalResolved++;
	BotNavRecordInteractionGoal(best);
	if (goal != nullptr) {
		*goal = best;
	}
	return true;
}

bool BotNavInteractionArrivalKindHasMoverEndpoint(int kind) {
	return kind == static_cast<int>(BotNavInteractionKind::Platform) ||
		kind == static_cast<int>(BotNavInteractionKind::Train) ||
		kind == static_cast<int>(BotNavInteractionKind::Mover);
}

bool BotNavRecordMoverRideState(
	const gentity_t *bot,
	int interactionEntityNumber,
	BotNavMoverRidePhase phase,
	const BotNavInteractionGoal *goal) {
	botNavRouteStatus.interactionMoverRideChecks++;
	if (bot == nullptr ||
		g_entities == nullptr ||
		interactionEntityNumber < 0 ||
		interactionEntityNumber >= static_cast<int>(globals.numEntities) ||
		phase == BotNavMoverRidePhase::None) {
		botNavRouteStatus.interactionMoverRideInvalidSkips++;
		return false;
	}

	const gentity_t *ent = &g_entities[interactionEntityNumber];
	if (ent == nullptr || !ent->inUse) {
		botNavRouteStatus.interactionMoverRideInvalidSkips++;
		return false;
	}

	const bool goalMatches =
		goal != nullptr &&
		goal->entityNumber == interactionEntityNumber;
	const int kind =
		goalMatches && goal->kind > 0 ?
			goal->kind :
			BotNavInteractionKindForEntity(ent);
	if (!BotNavInteractionArrivalKindHasMoverEndpoint(kind)) {
		botNavRouteStatus.interactionMoverRideInvalidSkips++;
		return false;
	}

	const int action =
		goalMatches && goal->action > 0 ?
			goal->action :
			BotNavInteractionActionForEntity(ent);
	const int area = goalMatches ? goal->area : 0;
	const Vector3 position =
		goalMatches && goal->area > 0 ?
			Vector3{ goal->position[0], goal->position[1], goal->position[2] } :
			BotNavInteractionEntityRouteOrigin(ent);
	const int groundEntity =
		bot->groundEntity != nullptr ? bot->groundEntity->s.number : -1;
	const bool groundedOnMover = groundEntity == interactionEntityNumber;
	const bool moverMoving =
		ent->moveInfo.state == MoveState::Up ||
		ent->moveInfo.state == MoveState::Down;
	const int distanceSquared =
		BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, position));

	switch (phase) {
	case BotNavMoverRidePhase::Wait:
		botNavRouteStatus.interactionMoverRideWaitStates++;
		break;
	case BotNavMoverRidePhase::Board:
		botNavRouteStatus.interactionMoverRideBoardStates++;
		break;
	case BotNavMoverRidePhase::Ride:
		botNavRouteStatus.interactionMoverRideRideStates++;
		break;
	case BotNavMoverRidePhase::Leave:
		botNavRouteStatus.interactionMoverRideLeaveStates++;
		break;
	case BotNavMoverRidePhase::None:
	default:
		botNavRouteStatus.interactionMoverRideInvalidSkips++;
		return false;
	}

	if (groundedOnMover) {
		botNavRouteStatus.interactionMoverRideGroundStates++;
	}
	if (moverMoving) {
		botNavRouteStatus.interactionMoverRideMovingStates++;
	}

	const int clientIndex = BotNavClientIndex(bot);
	const bool preserveCompletedLeave =
		phase != BotNavMoverRidePhase::Leave &&
		botNavRouteStatus.lastInteractionMoverRidePhase ==
			static_cast<int>(BotNavMoverRidePhase::Leave) &&
		botNavRouteStatus.lastInteractionMoverRideEntity == interactionEntityNumber &&
		botNavRouteStatus.lastInteractionMoverRideClient == clientIndex;
	if (preserveCompletedLeave) {
		return true;
	}

	botNavRouteStatus.lastInteractionMoverRidePhase = static_cast<int>(phase);
	botNavRouteStatus.lastInteractionMoverRideEntity = interactionEntityNumber;
	botNavRouteStatus.lastInteractionMoverRideKind = kind;
	botNavRouteStatus.lastInteractionMoverRideAction = action;
	botNavRouteStatus.lastInteractionMoverRideArea = area;
	botNavRouteStatus.lastInteractionMoverRideClient = clientIndex;
	botNavRouteStatus.lastInteractionMoverRideMoveState =
		static_cast<int>(ent->moveInfo.state);
	botNavRouteStatus.lastInteractionMoverRideGroundEntity = groundEntity;
	botNavRouteStatus.lastInteractionMoverRideX = static_cast<int>(position.x);
	botNavRouteStatus.lastInteractionMoverRideY = static_cast<int>(position.y);
	botNavRouteStatus.lastInteractionMoverRideZ = static_cast<int>(position.z);
	botNavRouteStatus.lastInteractionMoverRideDistanceSq = distanceSquared;
	return true;
}

bool BotNavInteractionArrivalCandidateBetter(
	int source,
	int destinationDistanceSquared,
	int botDistanceSquared,
	const BotNavInteractionGoal &best) {
	if (best.area <= 0) {
		return true;
	}

	const bool candidateMoverEndpoint =
		source == static_cast<int>(BotNavInteractionArrivalSource::MoverEndpoint);
	const bool bestMoverEndpoint =
		best.source == static_cast<int>(BotNavInteractionArrivalSource::MoverEndpoint);
	if (candidateMoverEndpoint != bestMoverEndpoint) {
		static constexpr int moverEndpointPreferenceSlackSquared = 160 * 160;
		if (candidateMoverEndpoint &&
			destinationDistanceSquared <=
				best.destinationDistanceSquared + moverEndpointPreferenceSlackSquared) {
			return true;
		}
		if (bestMoverEndpoint &&
			best.destinationDistanceSquared <=
				destinationDistanceSquared + moverEndpointPreferenceSlackSquared) {
			return false;
		}
	}

	return destinationDistanceSquared < best.destinationDistanceSquared ||
		(destinationDistanceSquared == best.destinationDistanceSquared &&
		 botDistanceSquared < best.distanceSquared);
}

bool BotNavTryInteractionArrivalCandidate(
	const gentity_t *bot,
	const gentity_t *ent,
	int entityNumber,
	int kind,
	int action,
	const Vector3 &destination,
	const Vector3 &candidate,
	int source,
	BotNavInteractionGoal *best) {
	if (best == nullptr) {
		return false;
	}

	const bool moverEndpoint =
		source == static_cast<int>(BotNavInteractionArrivalSource::MoverEndpoint);
	if (moverEndpoint) {
		botNavRouteStatus.interactionArrivalMoverEndpointChecks++;
	}

	const float point[3] = { candidate.x, candidate.y, candidate.z };
	float routeOrigin[3] = {};
	int area = 0;
	botNavRouteStatus.interactionArrivalGoalCandidates++;
	if (!BotLibAdapter_FindRouteAreaForPoint(point, &area, routeOrigin) ||
		area <= 0) {
		botNavRouteStatus.interactionArrivalGoalInvalidSkips++;
		return false;
	}
	if (moverEndpoint) {
		botNavRouteStatus.interactionArrivalMoverEndpointCandidates++;
	}

	const Vector3 resolved = { routeOrigin[0], routeOrigin[1], routeOrigin[2] };
	const int destinationDistanceSquared =
		BotNavStatusDistance(BotNavHorizontalDistanceSquared(destination, resolved));
	const int botDistanceSquared =
		BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, resolved));
	if (!BotNavInteractionArrivalCandidateBetter(
			source,
			destinationDistanceSquared,
			botDistanceSquared,
			*best)) {
		return false;
	}

	best->entityNumber = entityNumber;
	best->kind = kind;
	best->action = action;
	best->area = area;
	best->source = source;
	best->distanceSquared = botDistanceSquared;
	best->destinationDistanceSquared = destinationDistanceSquared;
	best->position[0] = resolved.x;
	best->position[1] = resolved.y;
	best->position[2] = resolved.z;
	if (moverEndpoint) {
		botNavRouteStatus.interactionArrivalMoverEndpointSelections++;
		BotNavRecordInteractionArrivalMoverEndpoint(*best);
	}
	(void)ent;
	return true;
}

bool BotNavAddUniqueInteractionArrivalEndpoint(
	std::array<Vector3, 8> &endpoints,
	size_t *count,
	const Vector3 &candidate) {
	if (count == nullptr ||
		*count >= endpoints.size() ||
		candidate.lengthSquared() <= 1.0f) {
		return false;
	}

	for (size_t i = 0; i < *count; ++i) {
		if ((endpoints[i] - candidate).lengthSquared() <= 1.0f) {
			return false;
		}
	}

	endpoints[*count] = candidate;
	++(*count);
	return true;
}

bool BotNavCollectInteractionArrivalMoverEndpoints(
	const gentity_t *ent,
	std::array<Vector3, 8> &endpoints,
	size_t *count) {
	if (ent == nullptr || count == nullptr) {
		return false;
	}

	const size_t initialCount = *count;
	(void)BotNavAddUniqueInteractionArrivalEndpoint(
		endpoints,
		count,
		ent->moveInfo.endOrigin);
	(void)BotNavAddUniqueInteractionArrivalEndpoint(
		endpoints,
		count,
		ent->moveInfo.dest);
	(void)BotNavAddUniqueInteractionArrivalEndpoint(
		endpoints,
		count,
		ent->pos2);
	(void)BotNavAddUniqueInteractionArrivalEndpoint(
		endpoints,
		count,
		ent->pos1);

	if (ent->targetEnt != nullptr && ent->targetEnt->inUse) {
		(void)BotNavAddUniqueInteractionArrivalEndpoint(
			endpoints,
			count,
			ent->targetEnt->s.origin);
	}

	if (ent->target != nullptr && ent->target[0] != '\0') {
		gentity_t *target = nullptr;
		while ((target = G_FindByString<&gentity_t::targetName>(target, ent->target)) != nullptr) {
			if (!target->inUse) {
				continue;
			}
			(void)BotNavAddUniqueInteractionArrivalEndpoint(
				endpoints,
				count,
				target->s.origin);
		}
	}

	return *count > initialCount;
}

void BotNavTryInteractionArrivalMoverEndpointCandidates(
	const gentity_t *bot,
	const gentity_t *ent,
	int entityNumber,
	int kind,
	int action,
	const Vector3 &destination,
	BotNavInteractionGoal *best) {
	if (!BotNavInteractionArrivalKindHasMoverEndpoint(kind)) {
		return;
	}

	std::array<Vector3, 8> endpoints{};
	size_t endpointCount = 0;
	if (!BotNavCollectInteractionArrivalMoverEndpoints(ent, endpoints, &endpointCount)) {
		return;
	}

	for (size_t endpointIndex = 0; endpointIndex < endpointCount; ++endpointIndex) {
		const Vector3 endpoint = endpoints[endpointIndex];
		(void)BotNavTryInteractionArrivalCandidate(
			bot,
			ent,
			entityNumber,
			kind,
			action,
			destination,
			endpoint,
			static_cast<int>(BotNavInteractionArrivalSource::MoverEndpoint),
			best);
	}
}

bool BotNavFindInteractionArrivalGoal(
	const gentity_t *bot,
	int interactionEntityNumber,
	const float destinationPoint[3],
	BotNavInteractionGoal *goal) {
	botNavRouteStatus.interactionArrivalGoalRequests++;
	if (goal != nullptr) {
		*goal = {};
	}
	if (bot == nullptr ||
		g_entities == nullptr ||
		destinationPoint == nullptr ||
		interactionEntityNumber < 0 ||
		interactionEntityNumber >= static_cast<int>(globals.numEntities)) {
		botNavRouteStatus.interactionArrivalGoalInvalidSkips++;
		return false;
	}

	const gentity_t *ent = &g_entities[interactionEntityNumber];
	if (ent == nullptr ||
		!ent->inUse ||
		(ent->flags & FL_NO_BOTS) != 0) {
		botNavRouteStatus.interactionArrivalGoalInvalidSkips++;
		return false;
	}

	const int kind = BotNavInteractionKindForEntity(ent);
	const int action = BotNavInteractionActionForEntity(ent);
	if (kind == static_cast<int>(BotNavInteractionKind::None) ||
		action == static_cast<int>(BotNavInteractionAction::None)) {
		botNavRouteStatus.interactionArrivalGoalInvalidSkips++;
		return false;
	}

	BotNavApplyRoutePolicy();
	const Vector3 destination = {
		destinationPoint[0],
		destinationPoint[1],
		destinationPoint[2],
	};
	Vector3 origin = BotNavInteractionEntityRouteOrigin(ent);
	Vector3 forward = destination - origin;
	forward.z = 0.0f;
	if (forward.lengthSquared() <= 1.0f) {
		forward = { 1.0f, 0.0f, 0.0f };
	} else {
		forward = forward.normalized();
	}
	const Vector3 right = { -forward.y, forward.x, 0.0f };

	static constexpr float backOffsets[] = { 384.0f, 256.0f, 160.0f, 96.0f };
	static constexpr float sideOffsets[] = { 0.0f, 96.0f, -96.0f, 192.0f, -192.0f };
	static constexpr float zOffsets[] = { 0.0f, 48.0f, -48.0f };

	BotNavInteractionGoal best{};
	BotNavTryInteractionArrivalMoverEndpointCandidates(
		bot,
		ent,
		interactionEntityNumber,
		kind,
		action,
		destination,
		&best);
	for (const float backOffset : backOffsets) {
		for (const float sideOffset : sideOffsets) {
			for (const float zOffset : zOffsets) {
				Vector3 candidate =
					destination -
					forward * backOffset +
					right * sideOffset;
				candidate.z += zOffset;
				(void)BotNavTryInteractionArrivalCandidate(
					bot,
					ent,
					interactionEntityNumber,
					kind,
					action,
					destination,
					candidate,
					static_cast<int>(BotNavInteractionArrivalSource::DestinationOffset),
					&best);
			}
		}
	}

	if (best.area <= 0) {
		(void)BotNavTryInteractionArrivalCandidate(
			bot,
			ent,
			interactionEntityNumber,
			kind,
			action,
			destination,
			destination,
			static_cast<int>(BotNavInteractionArrivalSource::DestinationDirect),
			&best);
	}

	if (best.area <= 0) {
		return false;
	}

	botNavRouteStatus.interactionArrivalGoalResolved++;
	BotNavRecordInteractionArrivalGoal(best);
	if (goal != nullptr) {
		*goal = best;
	}
	return true;
}

BotNavRefreshReason BotNavRefreshReasonFor(
	BotNavRouteSlot &slot,
	const gentity_t *bot,
	int preferredGoalArea,
	bool positionGoalRequested,
	int travelTypeGoalRequested,
	int clientIndex,
	uint32_t frame) {
	if (!slot.valid) {
		return BotNavRefreshReason::Invalid;
	}
	if (slot.persistentGoalArea > 0 &&
		BotNavHorizontalDistanceSquared(bot->s.origin, BotNavRouteGoal(slot.route)) <=
			BOT_NAV_GOAL_REACHED_DIST_SQUARED) {
		return BotNavRefreshReason::GoalReached;
	}
	if (slot.persistentGoalArea != preferredGoalArea) {
		return BotNavRefreshReason::PreferredGoal;
	}
	if (positionGoalRequested && !slot.persistentGoalIsPosition) {
		return BotNavRefreshReason::PreferredGoal;
	}
	if (!positionGoalRequested && slot.persistentGoalIsPosition) {
		return BotNavRefreshReason::PreferredGoal;
	}
	if (travelTypeGoalRequested > 0 && slot.persistentGoalTravelType != travelTypeGoalRequested) {
		return BotNavRefreshReason::PreferredGoal;
	}
	if (travelTypeGoalRequested <= 0 && slot.persistentGoalTravelType > 0) {
		return BotNavRefreshReason::PreferredGoal;
	}
	if (BotNavCheckStuckProgress(slot, bot, clientIndex, frame)) {
		return BotNavRefreshReason::Stuck;
	}
	if (frame >= slot.nextRefreshFrame) {
		return BotNavRefreshReason::Cadence;
	}
	if (BotNavHorizontalDistanceSquared(bot->s.origin, BotNavRouteTarget(slot.route)) <=
		BOT_NAV_TARGET_REACHED_DIST_SQUARED) {
		return BotNavRefreshReason::TargetReached;
	}
	if (BotNavHorizontalDistanceSquared(bot->s.origin, slot.origin) >= BOT_NAV_ROUTE_DRIFT_DIST_SQUARED) {
		return BotNavRefreshReason::OriginDrift;
	}

	return BotNavRefreshReason::None;
}

void BotNavRecordRefresh(BotNavRefreshReason reason) {
	switch (reason) {
	case BotNavRefreshReason::Cadence:
		botNavRouteStatus.cadenceRefreshes++;
		break;
	case BotNavRefreshReason::TargetReached:
		botNavRouteStatus.targetRefreshes++;
		break;
	case BotNavRefreshReason::GoalReached:
		botNavRouteStatus.targetRefreshes++;
		break;
	case BotNavRefreshReason::OriginDrift:
		botNavRouteStatus.driftRefreshes++;
		break;
	case BotNavRefreshReason::PreferredGoal:
		botNavRouteStatus.preferredGoalRefreshes++;
		break;
	case BotNavRefreshReason::Stuck:
		botNavRouteStatus.stuckRepathRefreshes++;
		break;
	default:
		break;
	}
}

BotNavFailedGoalReason BotNavFailedGoalReasonForClear(BotNavGoalClearReason reason) {
	switch (reason) {
	case BotNavGoalClearReason::RouteFallback:
		return BotNavFailedGoalReason::RouteFallback;
	case BotNavGoalClearReason::ItemUnavailable:
		return BotNavFailedGoalReason::ItemUnavailable;
	case BotNavGoalClearReason::Blacklisted:
		return BotNavFailedGoalReason::Blacklisted;
	default:
		return BotNavFailedGoalReason::None;
	}
}

void BotNavRecordFailedGoal(BotNavRouteSlot &slot, int clientIndex, BotNavGoalClearReason clearReason) {
	const BotNavFailedGoalReason failedReason = BotNavFailedGoalReasonForClear(clearReason);
	if (failedReason == BotNavFailedGoalReason::None) {
		return;
	}

	slot.lastFailedGoalReason = static_cast<int>(failedReason);
	slot.lastFailedGoalArea = slot.persistentGoalArea;
	slot.lastFailedGoalEntityNumber = slot.persistentGoalEntityNumber;
	slot.lastFailedGoalItem = slot.persistentGoalItem;

	botNavRouteStatus.failedGoalEvents++;
	botNavRouteStatus.lastFailedGoalReason = slot.lastFailedGoalReason;
	botNavRouteStatus.lastFailedGoalClient = clientIndex;
	botNavRouteStatus.lastFailedGoalArea = slot.lastFailedGoalArea;
	botNavRouteStatus.lastFailedGoalEntity = slot.lastFailedGoalEntityNumber;
	botNavRouteStatus.lastFailedGoalItem = static_cast<int>(slot.lastFailedGoalItem);
}

void BotNavClearItemGoal(BotNavRouteSlot &slot) {
	if (slot.persistentGoalEntityNumber >= 0) {
		botNavRouteStatus.itemGoalClears++;
	}
	slot.persistentGoalEntityNumber = -1;
	slot.persistentGoalEntitySpawnCount = 0;
	slot.persistentGoalItem = IT_NULL;
	slot.persistentGoalHealthAtAssignment = 0;
	slot.persistentGoalArmorAtAssignment = 0;
	BotNavUpdateActiveReservations();
}

void BotNavClearPositionGoal(BotNavRouteSlot &slot) {
	if (slot.persistentGoalIsPosition) {
		botNavRouteStatus.positionGoalClears++;
	}
	slot.persistentGoalIsPosition = false;
	slot.persistentPositionGoal = vec3_origin;
}

void BotNavClearInteractionArrivalRouteGoal(BotNavRouteSlot &slot) {
	slot.persistentGoalIsInteractionArrival = false;
	slot.persistentInteractionArrivalEntityNumber = -1;
	slot.persistentInteractionArrivalKind = 0;
	slot.persistentInteractionArrivalAction = 0;
	slot.persistentInteractionArrivalArea = 0;
	slot.persistentInteractionArrivalPosition = vec3_origin;
}

void BotNavClearTravelTypeGoal(BotNavRouteSlot &slot) {
	if (slot.persistentGoalTravelType > 0) {
		botNavRouteStatus.travelTypeGoalClears++;
	}
	slot.persistentGoalTravelType = 0;
}

void BotNavClearPersistentGoal(BotNavRouteSlot &slot, BotNavGoalClearReason reason, int clientIndex) {
	const uint32_t frame = gi.ServerFrame();
	BotNavCompleteProgressionInteraction(slot, clientIndex, frame);
	BotNavRecordFailedGoal(slot, clientIndex, reason);
	if (slot.persistentGoalArea > 0) {
		botNavRouteStatus.persistentGoalClears++;
	}
	BotNavClearItemGoal(slot);
	BotNavClearPositionGoal(slot);
	BotNavClearInteractionArrivalRouteGoal(slot);
	BotNavClearTravelTypeGoal(slot);
	slot.persistentGoalArea = 0;
	BotNavResetProgress(slot);
	BotNavClearInteraction(slot);
	if (reason != BotNavGoalClearReason::Blacklisted) {
		BotNavClearRecovery(slot);
	}
	botNavRouteStatus.lastPersistentGoalArea = 0;
	botNavRouteStatus.lastGoalClearReason = static_cast<int>(reason);
}

void BotNavRecordRoute(int clientIndex, const BotLibAdapterRouteSteer &route) {
	botNavRouteStatus.lastClient = clientIndex;
	botNavRouteStatus.lastCurrentArea = route.startArea;
	botNavRouteStatus.lastStartArea = route.startArea;
	botNavRouteStatus.lastGoalArea = route.goalArea;
	botNavRouteStatus.lastRouteEndArea = route.routeEndArea;
	botNavRouteStatus.lastRoutePointCount = BotNavRoutePointCount(route);
	botNavRouteStatus.lastTravelTime = route.travelTime;
	botNavRouteStatus.lastReachability = route.reachability;
	botNavRouteStatus.lastReachabilityTravelType = route.reachabilityTravelType;
	botNavRouteStatus.lastReachabilityTravelFlags = route.reachabilityTravelFlags;
	botNavRouteStatus.lastReachabilityEndArea = route.reachabilityEndArea;
	botNavRouteStatus.lastStopEvent = route.stopEvent;
}

void BotNavRecordCornerCutSkip(BotNavCornerCutSkipReason reason) {
	botNavRouteStatus.cornerCutSkips++;
	botNavRouteStatus.lastCornerCutSkipReason = static_cast<int>(reason);
}

void BotNavPromoteCornerCutPoint(BotLibAdapterRouteSteer *route, int pointIndex) {
	if (route == nullptr || pointIndex < 0) {
		return;
	}

	const Vector3 target = BotNavRoutePoint(*route, pointIndex);
	BotNavSetRouteMoveTarget(route, target);
	route->routePoints[0][0] = target.x;
	route->routePoints[0][1] = target.y;
	route->routePoints[0][2] = target.z;
	route->routePointCount = 1;
}

void BotNavApplyTraceCheckedCornerCut(const gentity_t *bot, BotLibAdapterRouteSteer *route) {
	botNavRouteStatus.cornerCutChecks++;
	botNavRouteStatus.lastCornerCutPointIndex = -1;
	botNavRouteStatus.lastCornerCutOriginalPointCount = 0;
	botNavRouteStatus.lastCornerCutResultPointCount = 0;
	botNavRouteStatus.lastCornerCutDistanceSq = 0;
	botNavRouteStatus.lastCornerCutTraceFraction = 0;
	botNavRouteStatus.lastCornerCutTravelType = route != nullptr ? route->reachabilityTravelType : 0;
	botNavRouteStatus.lastCornerCutSkipReason = static_cast<int>(BotNavCornerCutSkipReason::None);

	if (bot == nullptr || route == nullptr || !route->success) {
		BotNavRecordCornerCutSkip(BotNavCornerCutSkipReason::Invalid);
		return;
	}

	if (!BotNavCornerCutTravelTypeSupported(route->reachabilityTravelType)) {
		BotNavRecordCornerCutSkip(BotNavCornerCutSkipReason::UnsupportedTravel);
		return;
	}

	const int pointCount = BotNavRoutePointCount(*route);
	botNavRouteStatus.lastCornerCutOriginalPointCount = pointCount;
	if (pointCount <= 1) {
		BotNavRecordCornerCutSkip(BotNavCornerCutSkipReason::NoCandidate);
		return;
	}

	int bestPointIndex = -1;
	float bestDistanceSquared = 0.0f;
	bool consideredCandidate = false;
	for (int pointIndex = 1; pointIndex < pointCount; ++pointIndex) {
		const Vector3 routePoint = BotNavRoutePoint(*route, pointIndex);
		const float distanceSquared = BotNavHorizontalDistanceSquared(bot->s.origin, routePoint);
		if (distanceSquared < BOT_NAV_CORNER_CUT_MIN_DIST_SQUARED) {
			continue;
		}
		if (distanceSquared > BOT_NAV_CORNER_CUT_MAX_DIST_SQUARED) {
			break;
		}

		consideredCandidate = true;
		if (BotNavRouteShortcutTraceClear(bot, routePoint, route->reachabilityTravelType, true)) {
			bestPointIndex = pointIndex;
			bestDistanceSquared = distanceSquared;
		}
	}

	if (bestPointIndex <= 0) {
		BotNavRecordCornerCutSkip(
			consideredCandidate ?
				BotNavCornerCutSkipReason::NoSafeTrace :
				BotNavCornerCutSkipReason::NoCandidate);
		return;
	}

	BotNavPromoteCornerCutPoint(route, bestPointIndex);
	botNavRouteStatus.cornerCutApplications++;
	botNavRouteStatus.lastCornerCutPointIndex = bestPointIndex;
	botNavRouteStatus.lastCornerCutResultPointCount = BotNavRoutePointCount(*route);
	botNavRouteStatus.lastCornerCutDistanceSq = BotNavStatusDistance(bestDistanceSquared);
}

void BotNavStabilizeRouteTarget(const gentity_t *bot, BotLibAdapterRouteSteer *route) {
	if (bot == nullptr || route == nullptr || !route->success) {
		return;
	}

	botNavRouteStatus.routeTargetStabilizationChecks++;
	botNavRouteStatus.lastRouteTargetStablePointIndex = -1;

	const Vector3 originalTarget = BotNavRouteTarget(*route);
	const float originalDistanceSquared =
		BotNavHorizontalDistanceSquared(bot->s.origin, originalTarget);
	botNavRouteStatus.lastRouteTargetOriginalDistanceSq =
		BotNavStatusDistance(originalDistanceSquared);
	botNavRouteStatus.lastRouteTargetStableDistanceSq = 0;

	if (originalDistanceSquared > BOT_NAV_ROUTE_TARGET_STABILIZE_DIST_SQUARED) {
		botNavRouteStatus.routeTargetStabilizationSkips++;
		return;
	}

	const int pointCount = BotNavRoutePointCount(*route);
	for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
		const Vector3 routePoint = BotNavRoutePoint(*route, pointIndex);
		const float stableDistanceSquared =
			BotNavHorizontalDistanceSquared(bot->s.origin, routePoint);
		if (stableDistanceSquared < BOT_NAV_ROUTE_TARGET_STABLE_MIN_DIST_SQUARED) {
			continue;
		}
		if (!BotNavRouteShortcutTraceClear(bot, routePoint, route->reachabilityTravelType, false)) {
			continue;
		}

		BotNavSetRouteMoveTarget(route, routePoint);
		botNavRouteStatus.routeTargetStabilizations++;
		botNavRouteStatus.lastRouteTargetStableDistanceSq =
			BotNavStatusDistance(stableDistanceSquared);
		botNavRouteStatus.lastRouteTargetStablePointIndex = pointIndex;
		return;
	}

	botNavRouteStatus.routeTargetStabilizationSkips++;
}

bool BotNavBuildRouteWithFallback(
	const float origin[3],
	int preferredGoalArea,
	const BotNavPositionGoalCandidate *positionGoal,
	int travelTypeGoal,
	BotLibAdapterRouteSteer *refreshedRoute) {
	BotNavApplyRoutePolicy();

	if (preferredGoalArea > 0) {
		botNavRouteStatus.persistentGoalRequests++;
	}

	bool routed = false;
	if (travelTypeGoal > 0) {
		botNavRouteStatus.travelTypeGoalRequests++;
		BotNavRecordTravelTypeGoalSupport(travelTypeGoal);
		routed = BotLibAdapter_BuildRouteSteerForTravelType(origin, travelTypeGoal, refreshedRoute);
		if (routed &&
			refreshedRoute != nullptr &&
			refreshedRoute->success &&
			refreshedRoute->reachabilityTravelType == travelTypeGoal) {
			botNavRouteStatus.travelTypeGoalResolved++;
			botNavRouteStatus.lastTravelTypeGoalType = travelTypeGoal;
			botNavRouteStatus.lastTravelTypeGoalArea = refreshedRoute->goalArea;
			return true;
		}
		if (travelTypeGoal == BOT_NAV_TRAVEL_TELEPORT &&
			positionGoal != nullptr &&
			preferredGoalArea > 0) {
			const float goalOrigin[3] = {
				positionGoal->origin.x,
				positionGoal->origin.y,
				positionGoal->origin.z
			};
			botNavRouteStatus.teleporterEntityGoalFallbacks++;
			routed = BotLibAdapter_BuildRouteSteerTowardGoal(
				origin,
				preferredGoalArea,
				goalOrigin,
				refreshedRoute);
			if (routed &&
				refreshedRoute != nullptr &&
				refreshedRoute->success) {
				return true;
			}
		}
		return false;
	} else if (positionGoal != nullptr && preferredGoalArea > 0) {
		const float goalOrigin[3] = {
			positionGoal->origin.x,
			positionGoal->origin.y,
			positionGoal->origin.z
		};
		routed = BotLibAdapter_BuildRouteSteerToGoal(origin, preferredGoalArea, goalOrigin, refreshedRoute);
	} else {
		routed = BotLibAdapter_BuildRouteSteer(origin, preferredGoalArea, refreshedRoute);
	}

	if (routed &&
		refreshedRoute != nullptr &&
		refreshedRoute->success) {
		if (preferredGoalArea > 0 && refreshedRoute->goalArea != preferredGoalArea) {
			botNavRouteStatus.persistentGoalFallbacks++;
		}
		return true;
	}

	if (preferredGoalArea <= 0) {
		return false;
	}

	botNavRouteStatus.persistentGoalFallbacks++;
	BotLibAdapterRouteSteer fallbackRoute{};
	if (!BotLibAdapter_BuildRouteSteer(origin, 0, &fallbackRoute) || !fallbackRoute.success) {
		return false;
	}

	if (refreshedRoute != nullptr) {
		*refreshedRoute = fallbackRoute;
	}
	return true;
}

void BotNavAssignPersistentGoal(BotNavRouteSlot &slot, const BotLibAdapterRouteSteer &route) {
	if (route.goalArea <= 0) {
		return;
	}

	if (slot.persistentGoalArea != route.goalArea) {
		botNavRouteStatus.persistentGoalAssignments++;
	}
	slot.persistentGoalArea = route.goalArea;
	botNavRouteStatus.lastPersistentGoalArea = slot.persistentGoalArea;
}

void BotNavAssignInteractionArrivalRouteGoal(
	BotNavRouteSlot &slot,
	const BotNavPositionGoalCandidate &candidate) {
	if (!candidate.interactionArrivalGoal || candidate.area <= 0) {
		return;
	}

	const bool newInteractionArrivalAssignment =
		!slot.persistentGoalIsInteractionArrival ||
		slot.persistentInteractionArrivalEntityNumber != candidate.interactionArrivalEntityNumber ||
		slot.persistentInteractionArrivalKind != candidate.interactionArrivalKind ||
		slot.persistentInteractionArrivalAction != candidate.interactionArrivalAction ||
		slot.persistentInteractionArrivalArea != candidate.area ||
		(slot.persistentInteractionArrivalPosition - candidate.origin).lengthSquared() > 1.0f;
	if (newInteractionArrivalAssignment) {
		botNavRouteStatus.interactionArrivalRouteAssignments++;
	}
	slot.persistentGoalIsInteractionArrival = true;
	slot.persistentInteractionArrivalEntityNumber = candidate.interactionArrivalEntityNumber;
	slot.persistentInteractionArrivalKind = candidate.interactionArrivalKind;
	slot.persistentInteractionArrivalAction = candidate.interactionArrivalAction;
	slot.persistentInteractionArrivalArea = candidate.area;
	slot.persistentInteractionArrivalPosition = candidate.origin;
	BotNavRecordInteractionArrivalRouteGoal(candidate, nullptr);
}

void BotNavAssignPositionGoal(BotNavRouteSlot &slot, const BotNavPositionGoalCandidate &candidate) {
	if (candidate.area <= 0) {
		return;
	}

	if (!slot.persistentGoalIsPosition || slot.persistentGoalArea != candidate.area) {
		botNavRouteStatus.positionGoalAssignments++;
	}
	if (candidate.interactionArrivalGoal) {
		BotNavAssignInteractionArrivalRouteGoal(slot, candidate);
	} else {
		BotNavClearInteractionArrivalRouteGoal(slot);
	}

	slot.persistentGoalIsPosition = true;
	slot.persistentGoalTravelType = 0;
	slot.persistentPositionGoal = candidate.origin;
	botNavRouteStatus.lastPositionGoalArea = candidate.area;
	botNavRouteStatus.lastPositionGoalX = static_cast<int>(candidate.origin.x);
	botNavRouteStatus.lastPositionGoalY = static_cast<int>(candidate.origin.y);
	botNavRouteStatus.lastPositionGoalZ = static_cast<int>(candidate.origin.z);
	BotNavClearItemGoal(slot);
}

void BotNavAssignTeleporterEntityGoal(BotNavRouteSlot &slot, const BotNavPositionGoalCandidate &candidate) {
	if (candidate.area <= 0 || candidate.entityNumber < 0) {
		return;
	}

	const bool newAssignment =
		!slot.persistentGoalIsPosition ||
		slot.persistentGoalTravelType != BOT_NAV_TRAVEL_TELEPORT ||
		slot.persistentGoalArea != candidate.area;
	BotNavAssignPositionGoal(slot, candidate);
	slot.persistentGoalTravelType = BOT_NAV_TRAVEL_TELEPORT;
	if (newAssignment) {
		botNavRouteStatus.teleporterEntityGoalAssignments++;
	}
	BotNavRecordTeleporterEntityGoal(candidate);
}

void BotNavAssignTravelTypeGoal(BotNavRouteSlot &slot, int travelTypeGoal, const BotLibAdapterRouteSteer &route) {
	if (travelTypeGoal <= 0 || route.goalArea <= 0) {
		return;
	}

	if (slot.persistentGoalTravelType != travelTypeGoal || slot.persistentGoalArea != route.goalArea) {
		botNavRouteStatus.travelTypeGoalAssignments++;
	}

	slot.persistentGoalTravelType = travelTypeGoal;
	slot.persistentGoalIsPosition = false;
	BotNavClearInteractionArrivalRouteGoal(slot);
	slot.persistentPositionGoal = vec3_origin;
	botNavRouteStatus.lastTravelTypeGoalType = travelTypeGoal;
	botNavRouteStatus.lastTravelTypeGoalArea = route.goalArea;
	BotNavClearItemGoal(slot);
}

void BotNavAssignItemGoal(BotNavRouteSlot &slot, const BotNavItemGoalCandidate &candidate, const gentity_t *bot) {
	if (candidate.entityNumber < 0 || candidate.area <= 0) {
		return;
	}

	const bool newAssignment =
		slot.persistentGoalEntityNumber != candidate.entityNumber ||
		slot.persistentGoalEntitySpawnCount != candidate.spawnCount ||
		slot.persistentGoalItem != candidate.item;
	if (newAssignment) {
		botNavRouteStatus.itemGoalAssignments++;
		BotItems_RecordGoalAssignment(static_cast<int>(candidate.item));
	}

	slot.persistentGoalEntityNumber = candidate.entityNumber;
	slot.persistentGoalEntitySpawnCount = candidate.spawnCount;
	slot.persistentGoalItem = candidate.item;
	slot.persistentGoalHealthAtAssignment = bot != nullptr ? bot->health : 0;
	slot.persistentGoalArmorAtAssignment =
		bot != nullptr && bot->client != nullptr ? BotNavArmorValue(bot->client) : 0;
	slot.persistentGoalIsPosition = false;
	BotNavClearInteractionArrivalRouteGoal(slot);
	slot.persistentGoalTravelType = 0;
	slot.persistentPositionGoal = vec3_origin;
	botNavRouteStatus.lastItemGoalEntity = candidate.entityNumber;
	botNavRouteStatus.lastItemGoalArea = candidate.area;
	botNavRouteStatus.lastItemGoalItem = static_cast<int>(candidate.item);
	botNavRouteStatus.lastItemGoalScore = candidate.score;
	BotNavUpdateActiveReservations();
}

const char *BotNavTravelTypeName(int travelType) {
	switch (travelType) {
	case 2:
		return "walk";
	case 3:
		return "crouch";
	case 4:
		return "barrier_jump";
	case 5:
		return "jump";
	case 6:
		return "ladder";
	case 7:
		return "walkoffledge";
	case 8:
		return "swim";
	case 9:
		return "waterjump";
	case 10:
		return "teleport";
	case 11:
		return "elevator";
	case 12:
		return "rocketjump";
	case 13:
		return "bfgjump";
	case 14:
		return "grapplehook";
	case 15:
		return "doublejump";
	case 16:
		return "rampjump";
	case 17:
		return "strafejump";
	case 18:
		return "jumppad";
	case 19:
		return "funcbob";
	default:
		return "unknown";
	}
}

gentity_t *BotNavClientEntity(int clientIndex) {
	if (g_entities == nullptr || clientIndex < 0 || clientIndex >= static_cast<int>(game.maxClients)) {
		return nullptr;
	}

	const int entnum = clientIndex + 1;
	if (entnum <= 0 || entnum >= static_cast<int>(game.maxEntities)) {
		return nullptr;
	}

	gentity_t *ent = &g_entities[entnum];
	if (!ent->inUse || ent->client == nullptr) {
		return nullptr;
	}
	if ((ent->svFlags & SVF_BOT) == 0 && !ent->client->sess.is_a_bot) {
		return nullptr;
	}

	return ent;
}

void BotNavDrawCross(const Vector3 &origin, float size, const rgba_t &color, float lifeTime) {
	const float halfSize = std::max(size, 1.0f);
	gi.Draw_Line(
		origin + Vector3{ -halfSize, 0.0f, 0.0f },
		origin + Vector3{ halfSize, 0.0f, 0.0f },
		color,
		lifeTime,
		false);
	gi.Draw_Line(
		origin + Vector3{ 0.0f, -halfSize, 0.0f },
		origin + Vector3{ 0.0f, halfSize, 0.0f },
		color,
		lifeTime,
		false);
	gi.Draw_Line(
		origin + Vector3{ 0.0f, 0.0f, -halfSize },
		origin + Vector3{ 0.0f, 0.0f, halfSize },
		color,
		lifeTime,
		false);
	botNavRouteStatus.debugOverlayCrosses++;
}

void BotNavDrawCachedRoute(int clientIndex, const gentity_t *bot, const BotLibAdapterRouteSteer &route, bool drawRoute, bool drawGoal) {
	const float lifeTime = std::max(gi.frameTimeSec * 2.0f, BOT_NAV_DEBUG_ROUTE_LIFETIME);
	const Vector3 botOrigin = BotNavDebugPoint(bot->s.origin);
	const Vector3 moveTarget = BotNavDebugPoint(BotNavRouteTarget(route));
	const Vector3 goalOrigin = BotNavDebugPoint(BotNavRouteGoal(route));
	const int routePointCount = BotNavRoutePointCount(route);

	if (drawRoute) {
		Vector3 previousPoint = botOrigin;
		if (routePointCount > 0) {
			for (int pointIndex = 0; pointIndex < routePointCount; ++pointIndex) {
				const Vector3 routePoint = BotNavDebugPoint(BotNavRoutePoint(route, pointIndex));
				if (pointIndex == 0) {
					gi.Draw_Arrow(previousPoint, routePoint, 8.0f, rgba_cyan, rgba_yellow, lifeTime, false);
					botNavRouteStatus.debugOverlayArrows++;
				} else {
					gi.Draw_Line(previousPoint, routePoint, rgba_cyan, lifeTime, false);
					botNavRouteStatus.debugOverlayLines++;
				}
				botNavRouteStatus.debugOverlayPolylineSegments++;
				botNavRouteStatus.debugOverlayPolylinePoints++;
				previousPoint = routePoint;
			}

			if ((goalOrigin - previousPoint).lengthSquared() > 1.0f) {
				gi.Draw_Line(previousPoint, goalOrigin, rgba_green, lifeTime, false);
				botNavRouteStatus.debugOverlayLines++;
				botNavRouteStatus.debugOverlayPolylineSegments++;
			}
		} else {
			gi.Draw_Arrow(botOrigin, moveTarget, 8.0f, rgba_cyan, rgba_yellow, lifeTime, false);
			gi.Draw_Line(moveTarget, goalOrigin, rgba_green, lifeTime, false);
			botNavRouteStatus.debugOverlayArrows++;
			botNavRouteStatus.debugOverlayLines++;
			botNavRouteStatus.debugOverlayPolylineSegments += 2;
		}

		BotNavDrawCross(moveTarget, BOT_NAV_DEBUG_CROSS_SIZE, rgba_yellow, lifeTime);
		botNavRouteStatus.debugOverlayRoutes++;

		const BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
		const auto label = G_Fmt(
			"area {} goal {} item {} reach {} {} -> {} pts {} stuck {}:{} fail {}",
			route.startArea,
			route.goalArea,
			slot.persistentGoalEntityNumber,
			route.reachability,
			BotNavTravelTypeName(route.reachabilityTravelType),
			route.reachabilityEndArea,
			routePointCount,
			botNavRouteStatus.lastStuckReason,
			slot.stagnantProgressFrames,
			slot.lastFailedGoalReason);
		gi.Draw_OrientedWorldText(
			botOrigin + Vector3{ 0.0f, 0.0f, 32.0f },
			label.data(),
			rgba_cyan,
			BOT_NAV_DEBUG_LABEL_SIZE,
			lifeTime,
			false);
		botNavRouteStatus.debugOverlayLabels++;
	}

	if (drawGoal) {
		BotNavDrawCross(goalOrigin, BOT_NAV_DEBUG_CROSS_SIZE + 2.0f, rgba_green, lifeTime);
		botNavRouteStatus.debugOverlayGoals++;
	}

	botNavRouteStatus.lastDebugClient = clientIndex;
}

bool BotNavRefreshRoute(
	const gentity_t *bot,
	int clientIndex,
	int preferredGoalArea,
	const BotNavItemGoalCandidate *requestedItemGoal,
	const BotNavPositionGoalCandidate *requestedPositionGoal,
	int requestedTravelTypeGoal,
	BotNavRefreshReason reason,
	BotLibAdapterRouteSteer *route) {
	BotLibAdapterRouteSteer refreshedRoute{};
	const float origin[3] = {
		bot->s.origin.x,
		bot->s.origin.y,
		bot->s.origin.z
	};

	botNavRouteStatus.queries++;
	botNavRouteStatus.routeRecomputeRateLimitRefreshes++;
	BotNavRecordRefresh(reason);

	const uint64_t routeQueryStartNs = BotNavRouteNowNs();
	const bool routed = BotNavBuildRouteWithFallback(
		origin,
		preferredGoalArea,
		requestedPositionGoal,
		requestedTravelTypeGoal,
		&refreshedRoute);
	BotNavRecordRouteQueryCpu(BotNavRouteElapsedNs(routeQueryStartNs), routed);
	if (!routed) {
		botNavRouteSlots[clientIndex].valid = false;
		botNavRouteStatus.failures++;
		BotNavRecordRoute(clientIndex, refreshedRoute);
		if (preferredGoalArea > 0) {
			BotNavClearPersistentGoal(botNavRouteSlots[clientIndex], BotNavGoalClearReason::RouteFallback, clientIndex);
		} else if (requestedTravelTypeGoal > 0 && botNavRouteSlots[clientIndex].persistentGoalArea > 0) {
			BotNavClearPersistentGoal(botNavRouteSlots[clientIndex], BotNavGoalClearReason::Reset, clientIndex);
		}
		return false;
	}

	BotNavStabilizeRouteTarget(bot, &refreshedRoute);
	BotNavApplyTraceCheckedCornerCut(bot, &refreshedRoute);

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const int previousProgressGoalArea = slot.progressGoalArea;
	slot.valid = true;
	slot.nextRefreshFrame = gi.ServerFrame() + BOT_NAV_ROUTE_REFRESH_FRAMES;
	slot.origin = bot->s.origin;
	slot.route = refreshedRoute;
	if (previousProgressGoalArea != 0 && previousProgressGoalArea != refreshedRoute.goalArea) {
		BotNavResetProgress(slot);
	}
	if (preferredGoalArea > 0 && refreshedRoute.goalArea != preferredGoalArea) {
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::RouteFallback, clientIndex);
	}
	BotNavAssignPersistentGoal(slot, refreshedRoute);
	if (requestedPositionGoal != nullptr && requestedPositionGoal->interactionArrivalGoal) {
		BotNavAssignInteractionArrivalRouteGoal(slot, *requestedPositionGoal);
	}
	if (requestedTravelTypeGoal > 0 && refreshedRoute.reachabilityTravelType == requestedTravelTypeGoal) {
		BotNavAssignTravelTypeGoal(slot, requestedTravelTypeGoal, refreshedRoute);
	} else if (requestedTravelTypeGoal == BOT_NAV_TRAVEL_TELEPORT &&
		requestedPositionGoal != nullptr &&
		refreshedRoute.goalArea == requestedPositionGoal->area) {
		BotNavAssignTeleporterEntityGoal(slot, *requestedPositionGoal);
	} else if (requestedTravelTypeGoal > 0) {
		BotNavClearTravelTypeGoal(slot);
	} else if (requestedPositionGoal != nullptr && refreshedRoute.goalArea == requestedPositionGoal->area) {
		BotNavAssignPositionGoal(slot, *requestedPositionGoal);
	} else if (requestedPositionGoal != nullptr) {
		BotNavClearPositionGoal(slot);
	} else if (requestedItemGoal != nullptr && refreshedRoute.goalArea == requestedItemGoal->area) {
		BotNavAssignItemGoal(slot, *requestedItemGoal, bot);
	} else if (requestedItemGoal != nullptr || refreshedRoute.goalArea != preferredGoalArea) {
		BotNavClearItemGoal(slot);
	}

	botNavRouteStatus.refreshes++;
	BotNavRecordRoute(clientIndex, refreshedRoute);
	BotNavMaybeActivateRouteInteraction(slot, bot, clientIndex, gi.ServerFrame(), refreshedRoute);
	if (route != nullptr) {
		*route = refreshedRoute;
	}
	return true;
}

int BotNavBlackboardGoalTypeForSlot(const BotNavRouteSlot &slot) {
	if (slot.persistentGoalEntityNumber >= 0) {
		return static_cast<int>(BotNavBlackboardGoalType::Item);
	}
	if (slot.persistentGoalIsPosition) {
		return static_cast<int>(BotNavBlackboardGoalType::Position);
	}
	if (slot.persistentGoalTravelType > 0) {
		return static_cast<int>(BotNavBlackboardGoalType::TravelType);
	}
	if (slot.persistentGoalArea > 0) {
		return static_cast<int>(BotNavBlackboardGoalType::RouteGoal);
	}
	return static_cast<int>(BotNavBlackboardGoalType::None);
}

} // namespace

void BotNav_ResetAll() {
	botNavRouteSlots = {};
	botNavRouteStatus = {};
	botNavNaturalMovementSupportChecked = false;
}

void BotNav_ResetClient(int clientIndex) {
	if (clientIndex < 0 || clientIndex >= static_cast<int>(botNavRouteSlots.size())) {
		return;
	}

	botNavRouteSlots[clientIndex] = {};
	BotNavUpdateActiveReservations();
}

bool BotNav_FindInteractionGoal(
	const gentity_t *bot,
	int requiredKind,
	BotNavInteractionGoal *goal) {
	return BotNavFindInteractionGoal(bot, requiredKind, goal);
}

bool BotNav_FindInteractionArrivalGoal(
	const gentity_t *bot,
	int interactionEntityNumber,
	const float destination[3],
	BotNavInteractionGoal *goal) {
	return BotNavFindInteractionArrivalGoal(
		bot,
		interactionEntityNumber,
		destination,
		goal);
}

bool BotNav_SetInteractionArrivalRouteRequest(
	const BotNavInteractionGoal &goal,
	BotNavRouteRequest *request) {
	if (request == nullptr ||
		goal.entityNumber < 0 ||
		goal.kind <= 0 ||
		goal.action <= 0 ||
		goal.area <= 0) {
		botNavRouteStatus.interactionArrivalRouteInvalidSkips++;
		return false;
	}

	request->hasPositionGoal = true;
	request->positionGoal[0] = goal.position[0];
	request->positionGoal[1] = goal.position[1];
	request->positionGoal[2] = goal.position[2];
	request->hasInteractionArrivalGoal = true;
	request->interactionArrivalEntityNumber = goal.entityNumber;
	request->interactionArrivalKind = goal.kind;
	request->interactionArrivalAction = goal.action;
	return true;
}

bool BotNav_BuildInteractionArrivalRouteRequest(
	const gentity_t *bot,
	int interactionEntityNumber,
	const float destination[3],
	BotNavRouteRequest *request,
	BotNavInteractionGoal *goal) {
	BotNavInteractionGoal resolved{};
	if (!BotNavFindInteractionArrivalGoal(
			bot,
			interactionEntityNumber,
			destination,
			&resolved)) {
		botNavRouteStatus.interactionArrivalRouteInvalidSkips++;
		return false;
	}

	if (goal != nullptr) {
		*goal = resolved;
	}
	return BotNav_SetInteractionArrivalRouteRequest(resolved, request);
}

bool BotNav_InteractionArrivalRouteReached(
	const gentity_t *bot,
	const BotNavInteractionGoal *goal,
	int distanceSquaredThreshold) {
	const int clientIndex = BotNavClientIndex(bot);
	if (clientIndex < 0 ||
		goal == nullptr ||
		goal->entityNumber < 0 ||
		goal->kind <= 0 ||
		goal->action <= 0 ||
		goal->area <= 0 ||
		distanceSquaredThreshold <= 0) {
		botNavRouteStatus.interactionArrivalRouteInvalidSkips++;
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const Vector3 goalOrigin = {
		goal->position[0],
		goal->position[1],
		goal->position[2],
	};
	const int directDistanceSquared =
		BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, goalOrigin));
	BotNavRecordInteractionArrivalRouteGoal(
		goal->entityNumber,
		goal->kind,
		goal->action,
		goal->area,
		goalOrigin,
		directDistanceSquared);

	if (!slot.persistentGoalIsInteractionArrival ||
		slot.persistentInteractionArrivalEntityNumber != goal->entityNumber ||
		slot.persistentInteractionArrivalKind != goal->kind ||
		slot.persistentInteractionArrivalAction != goal->action ||
		slot.persistentInteractionArrivalArea != goal->area ||
		(slot.persistentInteractionArrivalPosition - goalOrigin).lengthSquared() > 1.0f ||
		!slot.valid) {
		return false;
	}

	const Vector3 routeTarget = BotNavRouteTarget(slot.route);
	const int routeTargetDistanceSquared =
		BotNavStatusDistance(BotNavHorizontalDistanceSquared(bot->s.origin, routeTarget));
	const int bestDistanceSquared =
		std::min(directDistanceSquared, routeTargetDistanceSquared);
	BotNavRecordInteractionArrivalRouteGoal(
		goal->entityNumber,
		goal->kind,
		goal->action,
		goal->area,
		goalOrigin,
		bestDistanceSquared);

	if (slot.route.routeEndArea <= 0) {
		return false;
	}
	if (BotNavRoutePointCount(slot.route) > 1 &&
		directDistanceSquared > distanceSquaredThreshold &&
		routeTargetDistanceSquared > distanceSquaredThreshold) {
		return false;
	}
	if (bestDistanceSquared > distanceSquaredThreshold) {
		return false;
	}

	botNavRouteStatus.interactionArrivalRouteReached++;
	return true;
}

bool BotNav_RecordMoverRideState(
	const gentity_t *bot,
	int interactionEntityNumber,
	BotNavMoverRidePhase phase,
	const BotNavInteractionGoal *goal) {
	return BotNavRecordMoverRideState(
		bot,
		interactionEntityNumber,
		phase,
		goal);
}

bool BotNav_ProbePickupGoal(const gentity_t *bot) {
	const int clientIndex = BotNavClientIndex(bot);
	if (clientIndex < 0) {
		return false;
	}

	BotNavItemGoalCandidate selectedItemGoal{};
	return BotNavFindPickupGoal(
		bot,
		clientIndex,
		gi.ServerFrame(),
		&selectedItemGoal);
}

bool BotNav_RouteTargetTraceClear(
	const gentity_t *bot,
	const BotLibAdapterRouteSteer *route,
	const float target[3]) {
	if (bot == nullptr || route == nullptr || !route->success || target == nullptr) {
		return false;
	}

	const Vector3 targetPoint = { target[0], target[1], target[2] };
	return BotNavRouteShortcutTraceClear(
		bot,
		targetPoint,
		route->reachabilityTravelType,
		false);
}

bool BotNav_GetRouteSteer(const gentity_t *bot, const BotNavRouteRequest *request, BotLibAdapterRouteSteer *route) {
	const int clientIndex = BotNavClientIndex(bot);
	const uint32_t frame = gi.ServerFrame();

	botNavRouteStatus.requests++;
	if (clientIndex < 0) {
		botNavRouteStatus.invalidSlots++;
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	BotNavCompleteExpiredInteraction(slot, clientIndex, frame);
	BotNavRecordPostInteractionProgress(slot, frame);
	if (slot.persistentGoalEntityNumber >= 0 && !BotNavItemGoalStillAvailable(slot)) {
		BotNavRecordPotentialPickup(slot, bot);
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::ItemUnavailable, clientIndex);
		slot.valid = false;
	}

	int preferredGoalArea = slot.persistentGoalArea;
	BotNavItemGoalCandidate selectedItemGoal{};
	BotNavPositionGoalCandidate selectedPositionGoal{};
	BotNavPositionGoalCandidate selectedTeleporterEntityGoal{};
	bool hasSelectedItemGoal = false;
	const bool hasSelectedPositionGoal = BotNavFindPositionGoal(bot, request, &selectedPositionGoal);
	const int requestedTravelTypeGoal =
		request != nullptr && request->hasTravelTypeGoal && request->travelTypeGoal > 0 ?
			request->travelTypeGoal :
			0;
	const bool hasSelectedTeleporterEntityGoal =
		!hasSelectedPositionGoal &&
		requestedTravelTypeGoal == BOT_NAV_TRAVEL_TELEPORT &&
		BotNavFindTeleporterEntityGoal(bot, &selectedTeleporterEntityGoal);
	const bool hasSelectedRoutePositionGoal =
		hasSelectedPositionGoal || hasSelectedTeleporterEntityGoal;
	const BotNavPositionGoalCandidate *selectedRoutePositionGoal =
		hasSelectedPositionGoal ?
			&selectedPositionGoal :
			(hasSelectedTeleporterEntityGoal ? &selectedTeleporterEntityGoal : nullptr);
	const bool hasSelectedTravelTypeGoal = !hasSelectedPositionGoal && requestedTravelTypeGoal > 0;
	if (hasSelectedRoutePositionGoal && selectedRoutePositionGoal != nullptr) {
		preferredGoalArea = selectedRoutePositionGoal->area;
		hasSelectedItemGoal = false;
	} else if (slot.persistentGoalIsPosition) {
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Reset, clientIndex);
		slot.valid = false;
		preferredGoalArea = 0;
	}

	if (hasSelectedTravelTypeGoal) {
		if (hasSelectedTeleporterEntityGoal) {
			preferredGoalArea = selectedTeleporterEntityGoal.area;
		} else {
			preferredGoalArea = slot.persistentGoalTravelType == requestedTravelTypeGoal ?
				slot.persistentGoalArea :
				0;
		}
		hasSelectedItemGoal = false;
	} else if (slot.persistentGoalTravelType > 0) {
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Reset, clientIndex);
		slot.valid = false;
		preferredGoalArea = 0;
	}

	if (!hasSelectedRoutePositionGoal && !hasSelectedTravelTypeGoal && preferredGoalArea <= 0) {
		hasSelectedItemGoal = BotNavFindPickupGoal(bot, clientIndex, frame, &selectedItemGoal);
		if (hasSelectedItemGoal) {
			preferredGoalArea = selectedItemGoal.area;
		}
	} else if (!hasSelectedRoutePositionGoal && !hasSelectedTravelTypeGoal && slot.persistentGoalEntityNumber >= 0) {
		botNavRouteStatus.itemGoalReuses++;
	}

	const BotNavRefreshReason reason = BotNavRefreshReasonFor(
		slot,
		bot,
		preferredGoalArea,
		hasSelectedRoutePositionGoal,
		hasSelectedTravelTypeGoal ? requestedTravelTypeGoal : 0,
		clientIndex,
		frame);
	if (reason == BotNavRefreshReason::GoalReached) {
		BotNavRecordPotentialPickup(slot, bot);
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Reached, clientIndex);
		preferredGoalArea = 0;
		if (hasSelectedRoutePositionGoal && selectedRoutePositionGoal != nullptr) {
			preferredGoalArea = selectedRoutePositionGoal->area;
		} else if (!hasSelectedTravelTypeGoal) {
			hasSelectedItemGoal = BotNavFindPickupGoal(bot, clientIndex, frame, &selectedItemGoal);
		}
		if (!hasSelectedRoutePositionGoal && !hasSelectedTravelTypeGoal && hasSelectedItemGoal) {
			preferredGoalArea = selectedItemGoal.area;
		}
	} else if (reason == BotNavRefreshReason::Stuck && BotNavCurrentItemGoalIsBlacklisted(slot, frame)) {
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Blacklisted, clientIndex);
		preferredGoalArea = 0;
		hasSelectedItemGoal = !hasSelectedRoutePositionGoal &&
			!hasSelectedTravelTypeGoal &&
			BotNavFindPickupGoal(bot, clientIndex, frame, &selectedItemGoal);
		if (hasSelectedItemGoal) {
			preferredGoalArea = selectedItemGoal.area;
		}
	}

	if (reason == BotNavRefreshReason::None) {
		const uint64_t routeReuseStartNs = BotNavRouteNowNs();
		botNavRouteStatus.reuses++;
		botNavRouteStatus.routeRecomputeRateLimitChecks++;
		if (frame < slot.nextRefreshFrame) {
			botNavRouteStatus.routeRecomputeRateLimitReuses++;
		}
		if (slot.persistentGoalArea > 0) {
			botNavRouteStatus.persistentGoalCacheReuses++;
			botNavRouteStatus.lastPersistentGoalArea = slot.persistentGoalArea;
		}
		if (slot.persistentGoalIsPosition) {
			botNavRouteStatus.positionGoalCacheReuses++;
			botNavRouteStatus.lastPositionGoalArea = slot.persistentGoalArea;
			botNavRouteStatus.lastPositionGoalX = static_cast<int>(slot.persistentPositionGoal.x);
			botNavRouteStatus.lastPositionGoalY = static_cast<int>(slot.persistentPositionGoal.y);
			botNavRouteStatus.lastPositionGoalZ = static_cast<int>(slot.persistentPositionGoal.z);
		}
		if (slot.persistentGoalIsInteractionArrival) {
			botNavRouteStatus.interactionArrivalRouteCacheReuses++;
			BotNavRecordPersistentInteractionArrivalRouteGoal(slot, bot);
		}
		if (slot.persistentGoalTravelType > 0) {
			botNavRouteStatus.travelTypeGoalCacheReuses++;
			botNavRouteStatus.lastTravelTypeGoalType = slot.persistentGoalTravelType;
			botNavRouteStatus.lastTravelTypeGoalArea = slot.persistentGoalArea;
		}
		BotNavRecordRoute(clientIndex, slot.route);
		BotNavMaybeActivateRouteInteraction(slot, bot, clientIndex, frame, slot.route);
		if (route != nullptr) {
			*route = slot.route;
		}
		BotNavRecordRouteReuseCpu(BotNavRouteElapsedNs(routeReuseStartNs));
		return true;
	}

	return BotNavRefreshRoute(
		bot,
		clientIndex,
		preferredGoalArea,
		hasSelectedItemGoal ? &selectedItemGoal : nullptr,
		selectedRoutePositionGoal,
		hasSelectedTravelTypeGoal ? requestedTravelTypeGoal : 0,
		reason,
		route);
}

bool BotNav_RequestInteractionRetry(
	const gentity_t *bot,
	const BotLibAdapterRouteSteer *route,
	bool fromStuck) {
	const int clientIndex = BotNavClientIndex(bot);
	if (clientIndex < 0 || route == nullptr) {
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const uint32_t frame = gi.ServerFrame();
	BotNavCompleteExpiredInteraction(slot, clientIndex, frame);
	BotNavRecordPostInteractionProgress(slot, frame);
	return BotNavActivateInteractionRetry(
		slot,
		bot,
		clientIndex,
		frame,
		*route,
		fromStuck) ||
		(slot.interactionAction != static_cast<int>(BotNavInteractionAction::None) &&
		 frame < slot.interactionUntilFrame);
}

bool BotNav_ActivateInteractionNearPosition(
	const gentity_t *bot,
	const float position[3],
	int requiredKind,
	int forcedTravelType) {
	const int clientIndex = BotNavClientIndex(bot);
	if (clientIndex < 0 ||
		position == nullptr ||
		requiredKind <= 0 ||
		forcedTravelType <= 0) {
		return false;
	}

	BotLibAdapterRouteSteer route{};
	route.success = true;
	route.reachabilityTravelType = forcedTravelType;
	route.routePointCount = 1;
	for (int axis = 0; axis < 3; ++axis) {
		route.moveTarget[axis] = position[axis];
		route.goalOrigin[axis] = position[axis];
		route.routePoints[0][axis] = position[axis];
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const uint32_t frame = gi.ServerFrame();
	BotNavCompleteExpiredInteraction(slot, clientIndex, frame);
	BotNavRecordPostInteractionProgress(slot, frame);
	return BotNavActivateInteractionRetry(
		slot,
		bot,
		clientIndex,
		frame,
		route,
		false,
		requiredKind) ||
		(slot.interactionAction != static_cast<int>(BotNavInteractionAction::None) &&
		 slot.interactionKind == requiredKind &&
		 frame < slot.interactionUntilFrame);
}

bool BotNav_GetRecoveryMove(const gentity_t *bot, BotNavRecoveryMove *move) {
	return BotNav_GetRecoveryMove(bot, nullptr, move);
}

bool BotNav_GetRecoveryMove(const gentity_t *bot, const float viewAngles[3], BotNavRecoveryMove *move) {
	if (move != nullptr) {
		*move = {};
	}

	const int clientIndex = BotNavClientIndex(bot);
	if (clientIndex < 0) {
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const uint32_t frame = gi.ServerFrame();
	BotNavCompleteExpiredInteraction(slot, clientIndex, frame);
	BotNavRecordPostInteractionProgress(slot, frame);
	if (slot.interactionAction != static_cast<int>(BotNavInteractionAction::None) &&
		frame < slot.interactionUntilFrame) {
		const int framesRemaining = static_cast<int>(slot.interactionUntilFrame - frame);
		const bool use =
			slot.interactionAction == static_cast<int>(BotNavInteractionAction::Use) ||
			slot.interactionAction == static_cast<int>(BotNavInteractionAction::WaitUse);
		const bool wait =
			slot.interactionAction == static_cast<int>(BotNavInteractionAction::Wait) ||
			slot.interactionAction == static_cast<int>(BotNavInteractionAction::WaitUse);

		if (wait) {
			botNavRouteStatus.interactionWaitFrames++;
		}
		if (use) {
			botNavRouteStatus.interactionUseFrames++;
		}
		slot.interactionCommandFrames++;
		botNavRouteStatus.lastInteractionAction = slot.interactionAction;
		botNavRouteStatus.lastInteractionKind = slot.interactionKind;
		botNavRouteStatus.lastInteractionEntity = slot.interactionEntityNumber;
		botNavRouteStatus.lastInteractionClient = clientIndex;
		botNavRouteStatus.lastInteractionFramesRemaining = framesRemaining;
		botNavRouteStatus.lastInteractionProgressionScore = slot.interactionProgressionScore;
		botNavRouteStatus.lastInteractionProgressionPreferred = slot.interactionProgressionPreferred;
		botNavRouteStatus.lastInteractionTargetEntity = slot.interactionTargetEntity;
		botNavRouteStatus.lastInteractionProgressionTarget = slot.interactionProgressionTarget;
		botNavRouteStatus.lastInteractionTargetLink = slot.interactionTargetLink;
		botNavRouteStatus.lastInteractionNamedTarget = slot.interactionNamedTarget;
		botNavRouteStatus.lastInteractionKeyEntity = slot.interactionKeyEntity;
		botNavRouteStatus.lastInteractionKeyItem = slot.interactionKeyItem;
		botNavRouteStatus.lastInteractionKeyLock = slot.interactionKeyLock;
		botNavRouteStatus.lastInteractionKeyRequiredItem = slot.interactionKeyRequiredItem;
		if (slot.interactionEntityNumber >= 0 &&
			slot.interactionEntityNumber < static_cast<int>(globals.numEntities)) {
			const gentity_t *ent = &g_entities[slot.interactionEntityNumber];
			botNavRouteStatus.lastInteractionMoveState = static_cast<int>(ent->moveInfo.state);
			BotNavRecordInteractionEntityContext(ent);
			if (BotNavInteractionArrivalKindHasMoverEndpoint(slot.interactionKind)) {
				const bool moverMoving =
					ent->moveInfo.state == MoveState::Up ||
					ent->moveInfo.state == MoveState::Down;
				if (moverMoving) {
					(void)BotNavRecordMoverRideState(
						bot,
						slot.interactionEntityNumber,
						BotNavMoverRidePhase::Ride,
						nullptr);
				}
				if (wait) {
					(void)BotNavRecordMoverRideState(
						bot,
						slot.interactionEntityNumber,
						BotNavMoverRidePhase::Wait,
						nullptr);
				}
				if (use) {
					(void)BotNavRecordMoverRideState(
						bot,
						slot.interactionEntityNumber,
						BotNavMoverRidePhase::Board,
						nullptr);
				}
				if (!moverMoving && !wait && !use) {
					(void)BotNavRecordMoverRideState(
						bot,
						slot.interactionEntityNumber,
						BotNavMoverRidePhase::Wait,
						nullptr);
				}
				if (framesRemaining <= 1) {
					(void)BotNavRecordMoverRideState(
						bot,
						slot.interactionEntityNumber,
						BotNavMoverRidePhase::Leave,
						nullptr);
				}
			}
		}

		if (move != nullptr) {
			move->wait = wait;
			move->use = use;
			move->framesRemaining = framesRemaining;
			move->interactionAction = slot.interactionAction;
			move->interactionKind = slot.interactionKind;
			move->interactionEntity = slot.interactionEntityNumber;
		}
		return true;
	}

	if (slot.recoverySideSign == 0 || frame >= slot.recoveryUntilFrame) {
		return false;
	}

	const int framesRemaining = static_cast<int>(slot.recoveryUntilFrame - frame);
	botNavRouteStatus.stuckRecoveryFrames++;
	botNavRouteStatus.lastStuckRecoveryClient = clientIndex;
	botNavRouteStatus.lastStuckRecoverySide = slot.recoverySideSign;
	botNavRouteStatus.lastStuckRecoveryFramesRemaining = framesRemaining;

	if (move != nullptr) {
		move->sideSign = slot.recoverySideSign;
		move->framesRemaining = framesRemaining;
		if (BotNavEnsureRecoveryMove(bot, slot, viewAngles)) {
			const Vector3 view = BotNavRecoveryViewAngles(bot, viewAngles);
			if (BotNavRecoveryProjectDirection(
					view,
					slot.recoveryMoveDirection,
					&move->forwardMove,
					&move->sideMove)) {
				move->hasMovement = true;
				botNavRouteStatus.lastStuckRecoveryForwardMove =
					static_cast<int>(std::round(move->forwardMove));
				botNavRouteStatus.lastStuckRecoverySideMove =
					static_cast<int>(std::round(move->sideMove));
			}
		}
	}
	return true;
}

bool BotNav_DrawDebugOverlay(bool drawRoute, bool drawGoal, int debugClientIndex) {
	if (!drawRoute && !drawGoal) {
		return false;
	}

	botNavRouteStatus.debugOverlayFrames++;
	botNavRouteStatus.lastDebugFilterClient = debugClientIndex;

	bool drewAny = false;
	const int clientCount = std::min(
		static_cast<int>(botNavRouteSlots.size()),
		static_cast<int>(game.maxClients));
	for (int clientIndex = 0; clientIndex < clientCount; ++clientIndex) {
		const BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
		if (!slot.valid) {
			continue;
		}
		if (debugClientIndex >= 0 && clientIndex != debugClientIndex) {
			botNavRouteStatus.debugOverlayFilteredSlots++;
			continue;
		}

		gentity_t *bot = BotNavClientEntity(clientIndex);
		if (bot == nullptr) {
			continue;
		}

		BotNavDrawCachedRoute(clientIndex, bot, slot.route, drawRoute, drawGoal);
		drewAny = true;
	}

	if (!drewAny) {
		botNavRouteStatus.debugOverlayMissingFrames++;
		if (debugClientIndex >= 0) {
			botNavRouteStatus.debugOverlayFilterMissFrames++;
		}
	}

	return drewAny;
}

const BotNavRouteStatus &BotNav_GetRouteStatus() {
	BotNavUpdateNaturalMovementSupportStatus();
	BotNavUpdateInteractionWorldContextStatus();
	BotNavUpdateActiveReservations();
	BotNavUpdateActiveGoalBlacklists();
	return botNavRouteStatus;
}

bool BotNav_GetBlackboardSnapshot(int clientIndex, BotNavBlackboardSnapshot *snapshot) {
	if (snapshot == nullptr) {
		return false;
	}

	*snapshot = {};
	if (clientIndex < 0 || clientIndex >= static_cast<int>(botNavRouteSlots.size())) {
		return false;
	}

	const BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const uint32_t frame = gi.ServerFrame();

	snapshot->routeSlotValid = slot.valid;
	snapshot->clientIndex = clientIndex;
	snapshot->goalType = BotNavBlackboardGoalTypeForSlot(slot);
	snapshot->goalArea = slot.persistentGoalArea;
	snapshot->goalEntity = slot.persistentGoalEntityNumber;
	snapshot->goalSpawnCount = slot.persistentGoalEntitySpawnCount;
	snapshot->goalItem = static_cast<int>(slot.persistentGoalItem);
	snapshot->goalPositionX = static_cast<int>(slot.persistentPositionGoal.x);
	snapshot->goalPositionY = static_cast<int>(slot.persistentPositionGoal.y);
	snapshot->goalPositionZ = static_cast<int>(slot.persistentPositionGoal.z);
	snapshot->goalTravelType = slot.persistentGoalTravelType;
	if (slot.valid) {
		snapshot->routeStartArea = slot.route.startArea;
		snapshot->routeGoalArea = slot.route.goalArea;
		snapshot->routeEndArea = slot.route.routeEndArea;
		snapshot->routePointCount = BotNavRoutePointCount(slot.route);
		snapshot->routeTravelTime = slot.route.travelTime;
		snapshot->routeReachability = slot.route.reachability;
		snapshot->routeReachabilityTravelType = slot.route.reachabilityTravelType;
		snapshot->routeReachabilityTravelFlags = slot.route.reachabilityTravelFlags;
		snapshot->routeReachabilityEndArea = slot.route.reachabilityEndArea;
		snapshot->routeStopEvent = slot.route.stopEvent;
	}
	snapshot->stuckReason = slot.lastStuckReason;
	snapshot->stuckFrames = slot.stagnantProgressFrames;
	snapshot->stuckDistanceSq = slot.lastStuckDistanceSq;
	snapshot->stuckProgressDelta = slot.lastStuckProgressDelta;
	if (slot.recoverySideSign != 0 && frame < slot.recoveryUntilFrame) {
		snapshot->stuckRecoverySide = slot.recoverySideSign;
		snapshot->stuckRecoveryFramesRemaining =
			static_cast<int>(slot.recoveryUntilFrame - frame);
	}
	if (slot.persistentGoalEntityNumber >= 0 && slot.persistentGoalItem > IT_NULL) {
		snapshot->itemReservationActive = 1;
		snapshot->itemReservationEntity = slot.persistentGoalEntityNumber;
		snapshot->itemReservationOwnerClient = clientIndex;
		snapshot->itemReservationItem = static_cast<int>(slot.persistentGoalItem);
		snapshot->itemReservationArea = slot.persistentGoalArea;
	}
	return true;
}
