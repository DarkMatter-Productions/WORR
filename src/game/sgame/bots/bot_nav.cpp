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

namespace {

constexpr uint32_t BOT_NAV_ROUTE_REFRESH_FRAMES = 4;
constexpr uint32_t BOT_NAV_ITEM_DESIRABILITY_STAGGER_FRAMES = 4;
constexpr uint32_t BOT_NAV_GOAL_BLACKLIST_FRAMES = 96;
constexpr uint32_t BOT_NAV_STUCK_REPATH_COOLDOWN_FRAMES = 4;
constexpr uint32_t BOT_NAV_STUCK_RECOVERY_FRAMES = 6;
constexpr uint32_t BOT_NAV_INTERACTION_RETRY_FRAMES = 12;
constexpr uint32_t BOT_NAV_INTERACTION_RETRY_COOLDOWN_FRAMES = 24;
constexpr int BOT_NAV_STUCK_FRAME_THRESHOLD = 8;
constexpr float BOT_NAV_TARGET_REACHED_DIST_SQUARED = 16.0f * 16.0f;
constexpr float BOT_NAV_GOAL_REACHED_DIST_SQUARED = 48.0f * 48.0f;
constexpr float BOT_NAV_PICKUP_RECORD_DIST_SQUARED = 96.0f * 96.0f;
constexpr float BOT_NAV_ROUTE_DRIFT_DIST_SQUARED = 96.0f * 96.0f;
constexpr float BOT_NAV_ROUTE_TARGET_STABILIZE_DIST_SQUARED = 24.0f * 24.0f;
constexpr float BOT_NAV_ROUTE_TARGET_STABLE_MIN_DIST_SQUARED = 64.0f * 64.0f;
constexpr float BOT_NAV_CORNER_CUT_MIN_DIST_SQUARED = 48.0f * 48.0f;
constexpr float BOT_NAV_CORNER_CUT_MAX_DIST_SQUARED = 256.0f * 256.0f;
constexpr float BOT_NAV_CORNER_CUT_GROUND_PROBE_DEPTH = STEPSIZE_BELOW + 8.0f;
constexpr float BOT_NAV_CORNER_CUT_MIN_GROUND_NORMAL_Z = 0.7f;
constexpr float BOT_NAV_CORNER_CUT_TRACE_FRACTION_SCALE = 1000.0f;
constexpr float BOT_NAV_STUCK_MIN_PROGRESS_DELTA_SQUARED = 16.0f;
constexpr float BOT_NAV_INTERACTION_NEAR_DIST_SQUARED = 192.0f * 192.0f;
constexpr float BOT_NAV_ELEVATOR_INTERACTION_DIST_SQUARED = 384.0f * 384.0f;
constexpr float BOT_NAV_DEBUG_ROUTE_LIFETIME = 0.10f;
constexpr float BOT_NAV_DEBUG_CROSS_SIZE = 10.0f;
constexpr float BOT_NAV_DEBUG_LABEL_SIZE = 6.0f;

constexpr int BOT_NAV_TRAVEL_WALK = 2;
constexpr int BOT_NAV_TRAVEL_CROUCH = 3;
constexpr int BOT_NAV_TRAVEL_SWIM = 8;
constexpr int BOT_NAV_TRAVEL_WATER_JUMP = 9;
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
	int interactionAction = 0;
	int interactionKind = 0;
	int interactionEntityNumber = -1;
	int persistentGoalArea = 0;
	bool persistentGoalIsPosition = false;
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

struct BotNavPositionGoalCandidate {
	int area = 0;
	Vector3 origin = vec3_origin;
};

struct BotNavInteractionCandidate {
	int entityNumber = -1;
	int action = 0;
	int kind = 0;
	int distanceSquared = 0;
};

std::array<BotNavRouteSlot, MAX_CLIENTS> botNavRouteSlots{};
BotNavRouteStatus botNavRouteStatus;
bool botNavNaturalMovementSupportChecked = false;

bool BotNavRocketJumpAllowed() {
	static cvar_t *allowRocketJump = nullptr;
	if (allowRocketJump == nullptr && gi.cvar != nullptr) {
		allowRocketJump = gi.cvar("sg_bot_allow_rocketjump", "0", CVAR_NOFLAGS);
	}
	return allowRocketJump != nullptr && allowRocketJump->integer > 0;
}

bool BotNavBehaviorPolicyEnabled() {
	static cvar_t *behaviorEnable = nullptr;
	if (behaviorEnable == nullptr && gi.cvar != nullptr) {
		behaviorEnable = gi.cvar("sg_bot_behavior_enable", "0", CVAR_NOFLAGS);
	}
	return behaviorEnable != nullptr && behaviorEnable->integer > 0;
}

bool BotNavCoopResourceShareEnabled() {
	static cvar_t *resourceShare = nullptr;
	if (resourceShare == nullptr && gi.cvar != nullptr) {
		resourceShare = gi.cvar("sg_bot_coop_resource_share", "0", CVAR_NOFLAGS);
	}
	return BotNavBehaviorPolicyEnabled() ||
		(resourceShare != nullptr && resourceShare->integer > 0);
}

bool BotNavMatchItemPolicyEnabled() {
	static cvar_t *matchItemPolicy = nullptr;
	if (matchItemPolicy == nullptr && gi.cvar != nullptr) {
		matchItemPolicy = gi.cvar("sg_bot_match_item_policy", "0", CVAR_NOFLAGS);
	}
	return BotNavBehaviorPolicyEnabled() ||
		(matchItemPolicy != nullptr && matchItemPolicy->integer > 0);
}

bool BotNavFfaItemRolesEnabled() {
	static cvar_t *ffaItemRoles = nullptr;
	if (ffaItemRoles == nullptr && gi.cvar != nullptr) {
		ffaItemRoles = gi.cvar("sg_bot_ffa_item_roles", "0", CVAR_NOFLAGS);
	}
	return (ffaItemRoles != nullptr && ffaItemRoles->integer > 0) ||
		BotNavMatchItemPolicyEnabled();
}

bool BotNavCtfItemRolesEnabled() {
	static cvar_t *ctfItemRoles = nullptr;
	if (ctfItemRoles == nullptr && gi.cvar != nullptr) {
		ctfItemRoles = gi.cvar("sg_bot_ctf_item_roles", "0", CVAR_NOFLAGS);
	}
	return (ctfItemRoles != nullptr && ctfItemRoles->integer > 0) ||
		BotNavMatchItemPolicyEnabled();
}

bool BotNavTeamItemRolesEnabled() {
	static cvar_t *teamItemRoles = nullptr;
	if (teamItemRoles == nullptr && gi.cvar != nullptr) {
		teamItemRoles = gi.cvar("sg_bot_team_item_roles", "0", CVAR_NOFLAGS);
	}
	return (teamItemRoles != nullptr && teamItemRoles->integer > 0) ||
		BotNavMatchItemPolicyEnabled();
}

bool BotNavTeamResourceDenialEnabled() {
	static cvar_t *teamResourceDenial = nullptr;
	if (teamResourceDenial == nullptr && gi.cvar != nullptr) {
		teamResourceDenial = gi.cvar("sg_bot_team_resource_denial", "0", CVAR_NOFLAGS);
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
		itemFocus = gi.cvar("sg_bot_frame_command_smoke_item_focus", "0", CVAR_NOFLAGS);
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

void BotNavResetProgress(BotNavRouteSlot &slot) {
	slot.progressGoalArea = 0;
	slot.lastProgressDistanceSquared = -1.0f;
	slot.stagnantProgressFrames = 0;
	slot.lastStuckDistanceSq = 0;
	slot.lastStuckProgressDelta = 0;
}

void BotNavClearRecovery(BotNavRouteSlot &slot) {
	slot.recoveryUntilFrame = 0;
	slot.recoverySideSign = 0;
}

void BotNavClearInteraction(BotNavRouteSlot &slot) {
	slot.interactionUntilFrame = 0;
	slot.interactionAction = static_cast<int>(BotNavInteractionAction::None);
	slot.interactionKind = static_cast<int>(BotNavInteractionKind::None);
	slot.interactionEntityNumber = -1;
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
	if (BotNavClassIs(ent, "trigger_once") ||
		BotNavClassIs(ent, "trigger_multiple")) {
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
	botNavRouteStatus.interactionWorldUseEntities = 0;
	botNavRouteStatus.interactionWorldTouchEntities = 0;
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
			kind == static_cast<int>(BotNavInteractionKind::Mover);
	}
	return kind != static_cast<int>(BotNavInteractionKind::None);
}

bool BotNavFindInteractionCandidate(
	const gentity_t *bot,
	const BotLibAdapterRouteSteer &route,
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
	BotNavInteractionCandidate best{};
	best.distanceSquared = BotNavStatusDistance(maxDistanceSquared) + 1;

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
			action == static_cast<int>(BotNavInteractionAction::None) ||
			!BotNavInteractionKindMatchesRoute(kind, route)) {
			continue;
		}

		const float botDistance = BotNavDistanceSquaredToBounds(bot->s.origin, ent);
		const float targetDistance = BotNavDistanceSquaredToBounds(routeTarget, ent);
		const float goalDistance = BotNavDistanceSquaredToBounds(routeGoal, ent);
		const float distanceSquared = std::min(botDistance, std::min(targetDistance, goalDistance));
		if (distanceSquared > maxDistanceSquared) {
			continue;
		}

		botNavRouteStatus.interactionCandidates++;
		const int statusDistance = BotNavStatusDistance(distanceSquared);
		if (statusDistance >= best.distanceSquared) {
			continue;
		}

		best.entityNumber = static_cast<int>(entnum);
		best.action = action;
		best.kind = kind;
		best.distanceSquared = statusDistance;
	}

	if (best.entityNumber < 0) {
		botNavRouteStatus.interactionMisses++;
		return false;
	}

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
	bool fromStuck) {
	if (frame < slot.interactionUntilFrame || frame < slot.nextInteractionFrame) {
		return false;
	}

	BotNavInteractionCandidate candidate{};
	if (!BotNavFindInteractionCandidate(bot, route, &candidate)) {
		return false;
	}

	slot.interactionUntilFrame = frame + BOT_NAV_INTERACTION_RETRY_FRAMES;
	slot.nextInteractionFrame = frame + BOT_NAV_INTERACTION_RETRY_COOLDOWN_FRAMES;
	slot.interactionAction = candidate.action;
	slot.interactionKind = candidate.kind;
	slot.interactionEntityNumber = candidate.entityNumber;

	botNavRouteStatus.interactionActivations++;
	if (fromStuck) {
		botNavRouteStatus.interactionStuckActivations++;
	} else if (route.reachabilityTravelType == BOT_NAV_TRAVEL_ELEVATOR) {
		botNavRouteStatus.interactionElevatorActivations++;
	}
	botNavRouteStatus.lastInteractionAction = candidate.action;
	botNavRouteStatus.lastInteractionKind = candidate.kind;
	botNavRouteStatus.lastInteractionEntity = candidate.entityNumber;
	botNavRouteStatus.lastInteractionDistanceSq = candidate.distanceSquared;
	botNavRouteStatus.lastInteractionTravelType = route.reachabilityTravelType;
	botNavRouteStatus.lastInteractionMoveState = 0;
	if (candidate.entityNumber >= 0 &&
		candidate.entityNumber < static_cast<int>(globals.numEntities)) {
		const gentity_t *ent = &g_entities[candidate.entityNumber];
		botNavRouteStatus.lastInteractionMoveState = static_cast<int>(ent->moveInfo.state);
		BotNavRecordInteractionEntityContext(ent);
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

void BotNavActivateRecovery(BotNavRouteSlot &slot, int clientIndex, uint32_t frame) {
	if (slot.recoverySideSign == 0) {
		slot.recoverySideSign = (clientIndex & 1) == 0 ? 1 : -1;
	} else {
		slot.recoverySideSign = -slot.recoverySideSign;
	}

	slot.recoveryUntilFrame = frame + BOT_NAV_STUCK_RECOVERY_FRAMES;
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

	const float distanceSquared = BotNavHorizontalDistanceSquared(bot->s.origin, BotNavRouteGoal(slot.route));
	slot.lastStuckDistanceSq = BotNavStatusDistance(distanceSquared);
	botNavRouteStatus.stuckChecks++;
	botNavRouteStatus.lastStuckClient = clientIndex;
	botNavRouteStatus.lastStuckDistanceSq = slot.lastStuckDistanceSq;

	if (slot.progressGoalArea != slot.route.goalArea || slot.lastProgressDistanceSquared < 0.0f) {
		slot.progressGoalArea = slot.route.goalArea;
		slot.lastProgressDistanceSquared = distanceSquared;
		slot.stagnantProgressFrames = 0;
		slot.lastStuckProgressDelta = 0;
		botNavRouteStatus.lastStuckProgressDelta = 0;
		botNavRouteStatus.lastStuckFrames = 0;
		return false;
	}

	const float progressDelta = slot.lastProgressDistanceSquared - distanceSquared;
	slot.lastStuckProgressDelta = static_cast<int>(progressDelta);
	botNavRouteStatus.lastStuckProgressDelta = slot.lastStuckProgressDelta;
	if (progressDelta >= BOT_NAV_STUCK_MIN_PROGRESS_DELTA_SQUARED ||
		distanceSquared <= BOT_NAV_GOAL_REACHED_DIST_SQUARED) {
		slot.lastProgressDistanceSquared = distanceSquared;
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
	botNavRouteStatus.stuckDetections++;
	slot.lastStuckReason = static_cast<int>(BotNavStuckReason::NoGoalProgress);
	botNavRouteStatus.lastStuckReason = slot.lastStuckReason;
	if (!BotNavActivateInteractionRetry(slot, bot, clientIndex, frame, slot.route, true)) {
		BotNavActivateRecovery(slot, clientIndex, frame);
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
		matchPolicy.mode == BotObjectiveMatchMode::FreeForAll) {
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

bool BotNavFindPositionGoal(const BotNavRouteRequest *request, BotNavPositionGoalCandidate *candidate) {
	if (request == nullptr || !request->hasPositionGoal) {
		return false;
	}

	botNavRouteStatus.positionGoalRequests++;

	float routeOrigin[3] = {};
	int area = 0;
	if (!BotLibAdapter_FindRouteAreaForPoint(request->positionGoal, &area, routeOrigin) || area <= 0) {
		return false;
	}

	botNavRouteStatus.positionGoalResolved++;
	botNavRouteStatus.lastPositionGoalArea = area;
	botNavRouteStatus.lastPositionGoalX = static_cast<int>(request->positionGoal[0]);
	botNavRouteStatus.lastPositionGoalY = static_cast<int>(request->positionGoal[1]);
	botNavRouteStatus.lastPositionGoalZ = static_cast<int>(request->positionGoal[2]);

	if (candidate != nullptr) {
		candidate->area = area;
		candidate->origin = { routeOrigin[0], routeOrigin[1], routeOrigin[2] };
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
	botNavRouteStatus.lastItemGoalEntity = -1;
	botNavRouteStatus.lastItemGoalArea = 0;
	botNavRouteStatus.lastItemGoalItem = 0;
	botNavRouteStatus.lastItemGoalScore = 0;
	BotNavUpdateActiveReservations();
}

void BotNavClearPositionGoal(BotNavRouteSlot &slot) {
	if (slot.persistentGoalIsPosition) {
		botNavRouteStatus.positionGoalClears++;
	}
	slot.persistentGoalIsPosition = false;
	slot.persistentPositionGoal = vec3_origin;
}

void BotNavClearTravelTypeGoal(BotNavRouteSlot &slot) {
	if (slot.persistentGoalTravelType > 0) {
		botNavRouteStatus.travelTypeGoalClears++;
	}
	slot.persistentGoalTravelType = 0;
}

void BotNavClearPersistentGoal(BotNavRouteSlot &slot, BotNavGoalClearReason reason, int clientIndex) {
	BotNavRecordFailedGoal(slot, clientIndex, reason);
	if (slot.persistentGoalArea > 0) {
		botNavRouteStatus.persistentGoalClears++;
	}
	BotNavClearItemGoal(slot);
	BotNavClearPositionGoal(slot);
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

void BotNavAssignPositionGoal(BotNavRouteSlot &slot, const BotNavPositionGoalCandidate &candidate) {
	if (candidate.area <= 0) {
		return;
	}

	if (!slot.persistentGoalIsPosition || slot.persistentGoalArea != candidate.area) {
		botNavRouteStatus.positionGoalAssignments++;
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

void BotNavAssignTravelTypeGoal(BotNavRouteSlot &slot, int travelTypeGoal, const BotLibAdapterRouteSteer &route) {
	if (travelTypeGoal <= 0 || route.goalArea <= 0) {
		return;
	}

	if (slot.persistentGoalTravelType != travelTypeGoal || slot.persistentGoalArea != route.goalArea) {
		botNavRouteStatus.travelTypeGoalAssignments++;
	}

	slot.persistentGoalTravelType = travelTypeGoal;
	slot.persistentGoalIsPosition = false;
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
	if (requestedTravelTypeGoal > 0 && refreshedRoute.reachabilityTravelType == requestedTravelTypeGoal) {
		BotNavAssignTravelTypeGoal(slot, requestedTravelTypeGoal, refreshedRoute);
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

bool BotNav_GetRouteSteer(const gentity_t *bot, const BotNavRouteRequest *request, BotLibAdapterRouteSteer *route) {
	const int clientIndex = BotNavClientIndex(bot);
	const uint32_t frame = gi.ServerFrame();

	botNavRouteStatus.requests++;
	if (clientIndex < 0) {
		botNavRouteStatus.invalidSlots++;
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	if (slot.persistentGoalEntityNumber >= 0 && !BotNavItemGoalStillAvailable(slot)) {
		BotNavRecordPotentialPickup(slot, bot);
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::ItemUnavailable, clientIndex);
		slot.valid = false;
	}

	int preferredGoalArea = slot.persistentGoalArea;
	BotNavItemGoalCandidate selectedItemGoal{};
	BotNavPositionGoalCandidate selectedPositionGoal{};
	bool hasSelectedItemGoal = false;
	const bool hasSelectedPositionGoal = BotNavFindPositionGoal(request, &selectedPositionGoal);
	const int requestedTravelTypeGoal =
		request != nullptr && request->hasTravelTypeGoal && request->travelTypeGoal > 0 ?
			request->travelTypeGoal :
			0;
	const bool hasSelectedTravelTypeGoal = !hasSelectedPositionGoal && requestedTravelTypeGoal > 0;
	if (hasSelectedPositionGoal) {
		preferredGoalArea = selectedPositionGoal.area;
		hasSelectedItemGoal = false;
	} else if (slot.persistentGoalIsPosition) {
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Reset, clientIndex);
		slot.valid = false;
		preferredGoalArea = 0;
	}

	if (hasSelectedTravelTypeGoal) {
		preferredGoalArea = slot.persistentGoalTravelType == requestedTravelTypeGoal ?
			slot.persistentGoalArea :
			0;
		hasSelectedItemGoal = false;
	} else if (slot.persistentGoalTravelType > 0) {
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Reset, clientIndex);
		slot.valid = false;
		preferredGoalArea = 0;
	}

	if (!hasSelectedPositionGoal && !hasSelectedTravelTypeGoal && preferredGoalArea <= 0) {
		hasSelectedItemGoal = BotNavFindPickupGoal(bot, clientIndex, frame, &selectedItemGoal);
		if (hasSelectedItemGoal) {
			preferredGoalArea = selectedItemGoal.area;
		}
	} else if (!hasSelectedPositionGoal && !hasSelectedTravelTypeGoal && slot.persistentGoalEntityNumber >= 0) {
		botNavRouteStatus.itemGoalReuses++;
	}

	const BotNavRefreshReason reason = BotNavRefreshReasonFor(
		slot,
		bot,
		preferredGoalArea,
		hasSelectedPositionGoal,
		hasSelectedTravelTypeGoal ? requestedTravelTypeGoal : 0,
		clientIndex,
		frame);
	if (reason == BotNavRefreshReason::GoalReached) {
		BotNavRecordPotentialPickup(slot, bot);
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Reached, clientIndex);
		preferredGoalArea = 0;
		if (hasSelectedPositionGoal) {
			preferredGoalArea = selectedPositionGoal.area;
		} else if (!hasSelectedTravelTypeGoal) {
			hasSelectedItemGoal = BotNavFindPickupGoal(bot, clientIndex, frame, &selectedItemGoal);
		}
		if (!hasSelectedPositionGoal && !hasSelectedTravelTypeGoal && hasSelectedItemGoal) {
			preferredGoalArea = selectedItemGoal.area;
		}
	} else if (reason == BotNavRefreshReason::Stuck && BotNavCurrentItemGoalIsBlacklisted(slot, frame)) {
		BotNavClearPersistentGoal(slot, BotNavGoalClearReason::Blacklisted, clientIndex);
		preferredGoalArea = 0;
		hasSelectedItemGoal = !hasSelectedTravelTypeGoal &&
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
		hasSelectedPositionGoal ? &selectedPositionGoal : nullptr,
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

bool BotNav_GetRecoveryMove(const gentity_t *bot, BotNavRecoveryMove *move) {
	if (move != nullptr) {
		*move = {};
	}

	const int clientIndex = BotNavClientIndex(bot);
	if (clientIndex < 0) {
		return false;
	}

	BotNavRouteSlot &slot = botNavRouteSlots[clientIndex];
	const uint32_t frame = gi.ServerFrame();
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
		botNavRouteStatus.lastInteractionAction = slot.interactionAction;
		botNavRouteStatus.lastInteractionKind = slot.interactionKind;
		botNavRouteStatus.lastInteractionEntity = slot.interactionEntityNumber;
		botNavRouteStatus.lastInteractionFramesRemaining = framesRemaining;
		if (slot.interactionEntityNumber >= 0 &&
			slot.interactionEntityNumber < static_cast<int>(globals.numEntities)) {
			const gentity_t *ent = &g_entities[slot.interactionEntityNumber];
			botNavRouteStatus.lastInteractionMoveState = static_cast<int>(ent->moveInfo.state);
			BotNavRecordInteractionEntityContext(ent);
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
