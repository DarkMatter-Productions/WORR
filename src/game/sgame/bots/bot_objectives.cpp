// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "botlib_adapter.hpp"
#include "bot_objectives.hpp"

namespace {
constexpr int BOT_OBJECTIVE_ENEMY_FLAG_PRIORITY = 900;
constexpr int BOT_OBJECTIVE_NEUTRAL_FLAG_PRIORITY = 880;
constexpr int BOT_OBJECTIVE_OWN_FLAG_RETURN_PRIORITY = 840;
constexpr int BOT_OBJECTIVE_BASE_DEFENSE_PRIORITY = 520;
constexpr int BOT_OBJECTIVE_RETURN_ROLE_BONUS = 100;
constexpr int BOT_OBJECTIVE_SUPPORT_ROLE_BONUS = 30;
constexpr int BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY = 40;

BotObjectiveStatus botObjectiveStatus;

static_assert(static_cast<int>(BotObjectiveType::EnemyFlagPickup) == 1);
static_assert(static_cast<int>(BotObjectiveType::OwnFlagReturn) == 2);
static_assert(static_cast<int>(BotObjectiveType::NeutralFlagPickup) == 3);
static_assert(static_cast<int>(BotObjectiveType::BaseDefense) == 4);
static_assert(static_cast<int>(BotObjectiveRole::None) == 0);
static_assert(static_cast<int>(BotObjectiveRole::Attacker) == 1);
static_assert(static_cast<int>(BotObjectiveRole::Defender) == 2);
static_assert(static_cast<int>(BotObjectiveRole::Returner) == 3);
static_assert(static_cast<int>(BotObjectiveRole::Support) == 4);
static_assert(static_cast<int>(BotObjectiveTargetSource::WorldFlagEntity) == 1);
static_assert(static_cast<int>(BotObjectiveTargetSource::DroppedFlagEntity) == 2);
static_assert(static_cast<int>(BotObjectiveTargetSource::FlagCarrier) == 3);
static_assert(static_cast<int>(BotObjectiveTargetSource::EnemyTeamAnchor) == 4);

bool BotObjectives_IsPrimaryTeam(int team) {
	return team == static_cast<int>(Team::Red) || team == static_cast<int>(Team::Blue);
}

bool BotObjectives_IsBotEntity(const gentity_t *bot) {
	return bot != nullptr &&
		bot->inUse &&
		bot->client != nullptr &&
		(((bot->svFlags & SVF_BOT) != 0) || bot->client->sess.is_a_bot);
}

bool BotObjectives_IsAliveBot(const gentity_t *bot) {
	return bot != nullptr &&
		bot->health > 0 &&
		!bot->deadFlag &&
		bot->client != nullptr &&
		!bot->client->eliminated;
}

bool BotObjectives_IsAlivePlayer(const gentity_t *ent) {
	return ent != nullptr &&
		ent->inUse &&
		ent->client != nullptr &&
		ent->health > 0 &&
		!ent->deadFlag &&
		!ent->client->eliminated;
}

bool BotObjectives_IsFlagItem(int item) {
	return item == IT_FLAG_RED || item == IT_FLAG_BLUE || item == IT_FLAG_NEUTRAL;
}

bool BotObjectives_HasSpawnFlag(const gentity_t *ent, SpawnFlags flag) {
	return ent != nullptr && ent->spawnFlags.has(flag);
}

bool BotObjectives_IsDroppedFlagEntity(const gentity_t *ent) {
	return BotObjectives_HasSpawnFlag(ent, SPAWNFLAG_ITEM_DROPPED) ||
		BotObjectives_HasSpawnFlag(ent, SPAWNFLAG_ITEM_DROPPED_PLAYER);
}

const char *BotObjectives_FlagClassNameForItem(int item) {
	switch (item) {
	case IT_FLAG_RED:
		return ITEM_CTF_FLAG_RED;
	case IT_FLAG_BLUE:
		return ITEM_CTF_FLAG_BLUE;
	case IT_FLAG_NEUTRAL:
		return ITEM_CTF_FLAG_NEUTRAL;
	default:
		return nullptr;
	}
}

int BotObjectives_EntityNumber(const gentity_t *ent) {
	if (ent == nullptr) {
		return -1;
	}
	if (ent->s.number >= 0) {
		return static_cast<int>(ent->s.number);
	}
	return static_cast<int>(ent - g_entities);
}

int BotObjectives_BotTeam(const gentity_t *bot) {
	return bot != nullptr && bot->client != nullptr
		? static_cast<int>(bot->client->sess.team)
		: static_cast<int>(Team::None);
}

int BotObjectives_TargetDistancePenalty(const gentity_t *bot, const float origin[3]) {
	if (bot == nullptr || origin == nullptr) {
		return 0;
	}

	const float dx = bot->s.origin.x - origin[0];
	const float dy = bot->s.origin.y - origin[1];
	return static_cast<int>((dx * dx + dy * dy) / (128.0f * 128.0f));
}

bool BotObjectives_ResolveAreaForOrigin(const float origin[3], int *area, float resolvedOrigin[3]) {
	if (origin == nullptr || area == nullptr || resolvedOrigin == nullptr) {
		botObjectiveStatus.targetAreaFailures++;
		return false;
	}

	botObjectiveStatus.targetAreaResolutions++;
	int resolvedArea = 0;
	float routeOrigin[3] = {};
	if (!BotLibAdapter_FindRouteAreaForPoint(origin, &resolvedArea, routeOrigin) || resolvedArea <= 0) {
		botObjectiveStatus.targetAreaFailures++;
		return false;
	}

	*area = resolvedArea;
	resolvedOrigin[0] = routeOrigin[0];
	resolvedOrigin[1] = routeOrigin[1];
	resolvedOrigin[2] = routeOrigin[2];
	return true;
}

bool BotObjectives_ResolveAreaForEntity(const gentity_t *ent, int *area, float resolvedOrigin[3]) {
	if (ent == nullptr) {
		botObjectiveStatus.targetAreaFailures++;
		return false;
	}

	const float origin[3] = {
		ent->s.origin.x,
		ent->s.origin.y,
		ent->s.origin.z
	};
	return BotObjectives_ResolveAreaForOrigin(origin, area, resolvedOrigin);
}

void BotObjectives_RecordTargetSource(BotObjectiveTargetSource source) {
	switch (source) {
	case BotObjectiveTargetSource::WorldFlagEntity:
		botObjectiveStatus.worldFlagTargets++;
		break;
	case BotObjectiveTargetSource::DroppedFlagEntity:
		botObjectiveStatus.droppedFlagTargets++;
		break;
	case BotObjectiveTargetSource::FlagCarrier:
		botObjectiveStatus.carrierTargets++;
		break;
	case BotObjectiveTargetSource::EnemyTeamAnchor:
		botObjectiveStatus.enemyTeamAnchorTargets++;
		break;
	default:
		break;
	}
}

const char *BotObjectives_RolePolicyReason(
	BotObjectiveRole role,
	BotObjectiveType type,
	BotObjectiveTargetSource source) {
	switch (role) {
	case BotObjectiveRole::Attacker:
		return type == BotObjectiveType::NeutralFlagPickup
			? "attack_neutral_flag"
			: "attack_enemy_flag";
	case BotObjectiveRole::Defender:
		return type == BotObjectiveType::OwnFlagReturn
			? "defend_return_lane"
			: "defend_base";
	case BotObjectiveRole::Returner:
		return "return_own_flag";
	case BotObjectiveRole::Support:
		return source == BotObjectiveTargetSource::FlagCarrier
			? "support_flag_carrier"
			: "support_objective";
	default:
		return "none";
	}
}

void BotObjectives_ApplyRolePolicyChoice(
	BotObjectiveRolePolicy *policy,
	const BotObjectiveTarget &target,
	BotObjectiveRole role,
	int priority,
	bool requestedRoleHonored) {
	if (policy == nullptr || role == BotObjectiveRole::None || priority <= 0) {
		return;
	}

	policy->valid = true;
	policy->requestedRoleHonored = requestedRoleHonored;
	policy->role = role;
	policy->assignmentPriority = priority;
	policy->rolePriority = priority;
	policy->reason = BotObjectives_RolePolicyReason(role, target.type, target.source);
}

void BotObjectives_ConsiderRolePolicyCandidate(
	BotObjectiveRolePolicy *policy,
	const BotObjectiveTarget &target,
	BotObjectiveRole role,
	int priority) {
	if (policy == nullptr || role == BotObjectiveRole::None || priority <= 0) {
		return;
	}

	if (!policy->valid || priority > policy->assignmentPriority) {
		policy->valid = true;
		policy->role = role;
		policy->assignmentPriority = priority;
		policy->rolePriority = priority;
		policy->reason = BotObjectives_RolePolicyReason(role, target.type, target.source);
	}
}

void BotObjectives_RecordRolePolicySelection(const BotObjectiveRolePolicy &policy) {
	if (!policy.valid) {
		botObjectiveStatus.rolePolicyNoSelection++;
		return;
	}

	botObjectiveStatus.rolePolicySelections++;
	switch (policy.role) {
	case BotObjectiveRole::Attacker:
		botObjectiveStatus.rolePolicyAttackSelections++;
		break;
	case BotObjectiveRole::Defender:
		botObjectiveStatus.rolePolicyDefendSelections++;
		break;
	case BotObjectiveRole::Returner:
		botObjectiveStatus.rolePolicyReturnSelections++;
		break;
	case BotObjectiveRole::Support:
		botObjectiveStatus.rolePolicySupportSelections++;
		break;
	default:
		break;
	}
}

void BotObjectives_RecordLast(const BotObjectiveAssignment &assignment) {
	botObjectiveStatus.lastObjectiveType = static_cast<int>(assignment.type);
	botObjectiveStatus.lastObjectiveRole = static_cast<int>(assignment.role);
	botObjectiveStatus.lastTargetSource = static_cast<int>(assignment.source);
	botObjectiveStatus.lastClient = assignment.clientIndex;
	botObjectiveStatus.lastTeam = assignment.team;
	botObjectiveStatus.lastTargetTeam = assignment.targetTeam;
	botObjectiveStatus.lastEntity = assignment.entity;
	botObjectiveStatus.lastSpawnCount = assignment.spawnCount;
	botObjectiveStatus.lastItem = assignment.item;
	botObjectiveStatus.lastArea = assignment.area;
	botObjectiveStatus.lastPriority = assignment.priority;
	botObjectiveStatus.lastRolePriority = assignment.rolePriority;
	botObjectiveStatus.lastAttackPriority = assignment.attackPriority;
	botObjectiveStatus.lastDefendPriority = assignment.defendPriority;
	botObjectiveStatus.lastReturnPriority = assignment.returnPriority;
	botObjectiveStatus.lastSupportPriority = assignment.supportPriority;
	botObjectiveStatus.lastCarrierClient = assignment.carrierClient;
	botObjectiveStatus.lastOriginX = static_cast<int>(assignment.origin[0]);
	botObjectiveStatus.lastOriginY = static_cast<int>(assignment.origin[1]);
	botObjectiveStatus.lastOriginZ = static_cast<int>(assignment.origin[2]);
}

void BotObjectives_RecordLastTarget(
	const BotObjectiveTarget &target,
	int clientIndex,
	int team,
	BotObjectiveRole role,
	int priority) {
	botObjectiveStatus.lastObjectiveType = static_cast<int>(target.type);
	botObjectiveStatus.lastObjectiveRole = static_cast<int>(role);
	botObjectiveStatus.lastTargetSource = static_cast<int>(target.source);
	botObjectiveStatus.lastClient = clientIndex;
	botObjectiveStatus.lastTeam = team;
	botObjectiveStatus.lastTargetTeam = target.ownerTeam;
	botObjectiveStatus.lastEntity = target.entity;
	botObjectiveStatus.lastSpawnCount = target.spawnCount;
	botObjectiveStatus.lastItem = target.item;
	botObjectiveStatus.lastArea = target.area;
	botObjectiveStatus.lastPriority = priority;
	botObjectiveStatus.lastRolePriority = priority;
	botObjectiveStatus.lastAttackPriority = 0;
	botObjectiveStatus.lastDefendPriority = 0;
	botObjectiveStatus.lastReturnPriority = 0;
	botObjectiveStatus.lastSupportPriority = 0;
	botObjectiveStatus.lastCarrierClient = target.carrierClient;
	botObjectiveStatus.lastOriginX = static_cast<int>(target.origin[0]);
	botObjectiveStatus.lastOriginY = static_cast<int>(target.origin[1]);
	botObjectiveStatus.lastOriginZ = static_cast<int>(target.origin[2]);
}

void BotObjectives_RecordLastEvent(
	BotObjectiveType type,
	BotObjectiveRole role,
	int clientIndex,
	int team,
	int targetTeam,
	int item,
	int entity,
	BotObjectiveTargetSource source,
	int originX,
	int originY,
	int originZ) {
	botObjectiveStatus.lastObjectiveType = static_cast<int>(type);
	botObjectiveStatus.lastObjectiveRole = static_cast<int>(role);
	botObjectiveStatus.lastTargetSource = static_cast<int>(source);
	botObjectiveStatus.lastClient = clientIndex;
	botObjectiveStatus.lastTeam = team;
	botObjectiveStatus.lastTargetTeam = targetTeam;
	botObjectiveStatus.lastEntity = entity;
	botObjectiveStatus.lastSpawnCount = 0;
	botObjectiveStatus.lastItem = item;
	botObjectiveStatus.lastArea = 0;
	botObjectiveStatus.lastPriority = 0;
	botObjectiveStatus.lastRolePriority = 0;
	botObjectiveStatus.lastAttackPriority = 0;
	botObjectiveStatus.lastDefendPriority = 0;
	botObjectiveStatus.lastReturnPriority = 0;
	botObjectiveStatus.lastSupportPriority = 0;
	botObjectiveStatus.lastCarrierClient = -1;
	botObjectiveStatus.lastOriginX = originX;
	botObjectiveStatus.lastOriginY = originY;
	botObjectiveStatus.lastOriginZ = originZ;
}

void BotObjectives_RecordAssignmentKind(BotObjectiveType type, BotObjectiveRole role) {
	switch (type) {
	case BotObjectiveType::EnemyFlagPickup:
		botObjectiveStatus.enemyFlagAssignments++;
		break;
	case BotObjectiveType::OwnFlagReturn:
		botObjectiveStatus.ownFlagReturnAssignments++;
		break;
	case BotObjectiveType::NeutralFlagPickup:
		botObjectiveStatus.neutralFlagAssignments++;
		break;
	case BotObjectiveType::BaseDefense:
		botObjectiveStatus.baseDefenseAssignments++;
		break;
	default:
		break;
	}

	switch (role) {
	case BotObjectiveRole::Attacker:
		botObjectiveStatus.roleAttacker++;
		break;
	case BotObjectiveRole::Defender:
		botObjectiveStatus.roleDefender++;
		break;
	case BotObjectiveRole::Returner:
		botObjectiveStatus.roleReturner++;
		break;
	case BotObjectiveRole::Support:
		botObjectiveStatus.roleSupport++;
		break;
	default:
		break;
	}
}

bool BotObjectives_AssignmentHasRouteTarget(const BotObjectiveAssignment &assignment) {
	return assignment.assigned &&
		assignment.wantsRoute &&
		assignment.clientIndex >= 0 &&
		assignment.entity >= 0 &&
		assignment.area > 0;
}

bool BotObjectives_RouteGoalMatchesAssignment(
	const BotObjectiveRouteGoal &goal,
	const BotObjectiveAssignment &assignment) {
	return goal.valid &&
		BotObjectives_AssignmentHasRouteTarget(assignment) &&
		goal.area == assignment.area &&
		goal.entity == assignment.entity &&
		goal.item == assignment.item;
}

BotObjectiveTarget BotObjectives_BuildTargetForEntity(
	const gentity_t *bot,
	const gentity_t *ent,
	int item,
	BotObjectiveTargetSource source,
	int carrierClient) {
	float routeOrigin[3] = {};
	int area = 0;
	const bool resolved = BotObjectives_ResolveAreaForEntity(ent, &area, routeOrigin);
	const int entityNumber = BotObjectives_EntityNumber(ent);
	return BotObjectives_BuildFlagTargetAt(
		BotObjectives_BotTeam(bot),
		entityNumber,
		ent != nullptr ? ent->spawn_count : 0,
		item,
		area,
		resolved,
		routeOrigin,
		source,
		carrierClient);
}

void BotObjectives_ConsiderTargetCandidate(
	const gentity_t *bot,
	const BotObjectiveTarget &candidate,
	BotObjectiveTarget *best,
	int *bestScore) {
	if (!candidate.available || !candidate.reachable || candidate.type == BotObjectiveType::None) {
		return;
	}

	botObjectiveStatus.targetCandidates++;
	const int sourceScore =
		candidate.source == BotObjectiveTargetSource::DroppedFlagEntity ? 3000 :
		candidate.source == BotObjectiveTargetSource::WorldFlagEntity ? 2400 :
		candidate.source == BotObjectiveTargetSource::FlagCarrier ? 1600 :
		candidate.source == BotObjectiveTargetSource::EnemyTeamAnchor ? 1000 :
		0;
	const int score = sourceScore - BotObjectives_TargetDistancePenalty(bot, candidate.origin);
	if (best != nullptr && bestScore != nullptr && score > *bestScore) {
		*best = candidate;
		*bestScore = score;
	}
}
} // namespace

void BotObjectives_ResetStatus() {
	botObjectiveStatus = {};
}

BotObjectiveRole BotObjectives_DefaultRoleForType(BotObjectiveType type) {
	switch (type) {
	case BotObjectiveType::OwnFlagReturn:
		return BotObjectiveRole::Returner;
	case BotObjectiveType::BaseDefense:
		return BotObjectiveRole::Defender;
	case BotObjectiveType::EnemyFlagPickup:
	case BotObjectiveType::NeutralFlagPickup:
		return BotObjectiveRole::Attacker;
	default:
		return BotObjectiveRole::None;
	}
}

BotObjectiveRole BotObjectives_DefaultRoleForTarget(const BotObjectiveTarget &target) {
	if ((target.type == BotObjectiveType::EnemyFlagPickup ||
			target.type == BotObjectiveType::NeutralFlagPickup) &&
		target.source == BotObjectiveTargetSource::FlagCarrier &&
		target.carrierClient >= 0) {
		return BotObjectiveRole::Support;
	}

	return BotObjectives_DefaultRoleForType(target.type);
}

int BotObjectives_PriorityForType(BotObjectiveType type) {
	switch (type) {
	case BotObjectiveType::EnemyFlagPickup:
		return BOT_OBJECTIVE_ENEMY_FLAG_PRIORITY;
	case BotObjectiveType::NeutralFlagPickup:
		return BOT_OBJECTIVE_NEUTRAL_FLAG_PRIORITY;
	case BotObjectiveType::OwnFlagReturn:
		return BOT_OBJECTIVE_OWN_FLAG_RETURN_PRIORITY;
	case BotObjectiveType::BaseDefense:
		return BOT_OBJECTIVE_BASE_DEFENSE_PRIORITY;
	default:
		return 0;
	}
}

int BotObjectives_RolePriorityForTarget(BotObjectiveRole role, const BotObjectiveTarget &target) {
	const int basePriority = BotObjectives_PriorityForType(target.type);
	if (basePriority <= 0) {
		return 0;
	}

	switch (role) {
	case BotObjectiveRole::Attacker:
		return target.type == BotObjectiveType::EnemyFlagPickup ||
				target.type == BotObjectiveType::NeutralFlagPickup
			? basePriority
			: 0;
	case BotObjectiveRole::Defender:
		if (target.type == BotObjectiveType::BaseDefense) {
			return basePriority;
		}
		if (target.type == BotObjectiveType::OwnFlagReturn) {
			return basePriority - BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY;
		}
		return 0;
	case BotObjectiveRole::Returner:
		return target.type == BotObjectiveType::OwnFlagReturn
			? basePriority + BOT_OBJECTIVE_RETURN_ROLE_BONUS
			: 0;
	case BotObjectiveRole::Support:
		if ((target.type == BotObjectiveType::EnemyFlagPickup ||
				target.type == BotObjectiveType::NeutralFlagPickup) &&
			target.source == BotObjectiveTargetSource::FlagCarrier &&
			target.carrierClient >= 0) {
			return basePriority + BOT_OBJECTIVE_SUPPORT_ROLE_BONUS;
		}
		if (target.type == BotObjectiveType::BaseDefense) {
			return basePriority - BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY;
		}
		return 0;
	default:
		return 0;
	}
}

BotObjectiveRolePolicy BotObjectives_EvaluateRolePolicy(const BotObjectiveContext &context) {
	botObjectiveStatus.rolePolicyEvaluations++;

	BotObjectiveRolePolicy policy{};
	policy.type = context.target.type;
	policy.hasRequestedRole = context.requestedRole != BotObjectiveRole::None;
	policy.attackPriority = BotObjectives_RolePriorityForTarget(BotObjectiveRole::Attacker, context.target);
	policy.defendPriority = BotObjectives_RolePriorityForTarget(BotObjectiveRole::Defender, context.target);
	policy.returnPriority = BotObjectives_RolePriorityForTarget(BotObjectiveRole::Returner, context.target);
	policy.supportPriority = BotObjectives_RolePriorityForTarget(BotObjectiveRole::Support, context.target);

	if (!context.valid ||
		context.clientIndex < 0 ||
		!BotObjectives_IsPrimaryTeam(context.team) ||
		!context.target.available ||
		!context.target.reachable ||
		context.target.area <= 0 ||
		context.target.type == BotObjectiveType::None) {
		BotObjectives_RecordRolePolicySelection(policy);
		return policy;
	}

	bool requestedNeedsFallback = false;
	if (policy.hasRequestedRole) {
		botObjectiveStatus.rolePolicyRequested++;
		const int requestedPriority = BotObjectives_RolePriorityForTarget(context.requestedRole, context.target);
		if (requestedPriority > 0) {
			BotObjectives_ApplyRolePolicyChoice(
				&policy,
				context.target,
				context.requestedRole,
				requestedPriority,
				true);
			botObjectiveStatus.rolePolicyRequestedHonored++;
		} else {
			requestedNeedsFallback = true;
		}
	}

	if (!policy.valid) {
		const BotObjectiveRole defaultRole = BotObjectives_DefaultRoleForTarget(context.target);
		BotObjectives_ConsiderRolePolicyCandidate(
			&policy,
			context.target,
			defaultRole,
			BotObjectives_RolePriorityForTarget(defaultRole, context.target));
		BotObjectives_ConsiderRolePolicyCandidate(
			&policy,
			context.target,
			BotObjectiveRole::Returner,
			policy.returnPriority);
		BotObjectives_ConsiderRolePolicyCandidate(
			&policy,
			context.target,
			BotObjectiveRole::Support,
			policy.supportPriority);
		BotObjectives_ConsiderRolePolicyCandidate(
			&policy,
			context.target,
			BotObjectiveRole::Attacker,
			policy.attackPriority);
		BotObjectives_ConsiderRolePolicyCandidate(
			&policy,
			context.target,
			BotObjectiveRole::Defender,
			policy.defendPriority);
	}

	if (requestedNeedsFallback && policy.valid) {
		policy.fallbackRole = true;
		botObjectiveStatus.rolePolicyFallbacks++;
	}

	BotObjectives_RecordRolePolicySelection(policy);
	return policy;
}

BotObjectiveTarget BotObjectives_BuildFlagTarget(int botTeam, int entityNumber, int item, int area, bool available) {
	const float origin[3] = {};
	return BotObjectives_BuildFlagTargetAt(
		botTeam,
		entityNumber,
		0,
		item,
		area,
		available,
		origin,
		BotObjectiveTargetSource::None,
		-1);
}

BotObjectiveTarget BotObjectives_BuildFlagTargetAt(
	int botTeam,
	int entityNumber,
	int spawnCount,
	int item,
	int area,
	bool available,
	const float origin[3],
	BotObjectiveTargetSource source,
	int carrierClient) {
	BotObjectiveTarget target{};
	target.available = available;
	target.reachable = available && area > 0;
	target.entity = entityNumber;
	target.spawnCount = spawnCount;
	target.item = item;
	target.area = area;
	target.ownerTeam = BotObjectives_FlagOwnerTeamForItem(item);
	target.carrierClient = carrierClient;
	if (origin != nullptr) {
		target.origin[0] = origin[0];
		target.origin[1] = origin[1];
		target.origin[2] = origin[2];
	}
	target.type = BotObjectives_FlagObjectiveTypeForTeam(botTeam, item);
	target.source = source;
	return target;
}

BotObjectiveTarget BotObjectives_BuildFlagTargetForEntity(const gentity_t *bot, const gentity_t *flag, int area) {
	const int botTeam = bot != nullptr && bot->client != nullptr
		? static_cast<int>(bot->client->sess.team)
		: static_cast<int>(Team::None);
	const bool available = flag != nullptr && flag->inUse && flag->item != nullptr;
	const int entityNumber = flag != nullptr ? static_cast<int>(flag->s.number) : -1;
	const int item = flag != nullptr && flag->item != nullptr ? flag->item->id : IT_NULL;
	const float origin[3] = {
		flag != nullptr ? flag->s.origin.x : 0.0f,
		flag != nullptr ? flag->s.origin.y : 0.0f,
		flag != nullptr ? flag->s.origin.z : 0.0f
	};
	return BotObjectives_BuildFlagTargetAt(
		botTeam,
		entityNumber,
		flag != nullptr ? flag->spawn_count : 0,
		item,
		area,
		available,
		origin,
		flag != nullptr && BotObjectives_IsDroppedFlagEntity(flag)
			? BotObjectiveTargetSource::DroppedFlagEntity
			: BotObjectiveTargetSource::WorldFlagEntity,
		-1);
}

BotObjectiveContext BotObjectives_BuildContextForTarget(const gentity_t *bot, const BotObjectiveTarget &target, bool smokeEnabled, BotObjectiveRole requestedRole) {
	BotObjectiveContext context{};
	context.smokeEnabled = smokeEnabled;
	context.target = target;
	context.requestedRole = requestedRole;

	if (!BotObjectives_IsBotEntity(bot)) {
		return context;
	}

	context.valid = true;
	context.alive = BotObjectives_IsAliveBot(bot);
	context.clientIndex = static_cast<int>(bot->s.number) - 1;
	context.team = static_cast<int>(bot->client->sess.team);
	return context;
}

BotObjectiveTarget BotObjectives_SelectEnemyFlagTarget(const gentity_t *bot, bool allowEnemyTeamAnchor) {
	botObjectiveStatus.targetSelections++;

	const int botTeam = BotObjectives_BotTeam(bot);
	if (!BotObjectives_IsPrimaryTeam(botTeam)) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	const int item = BotObjectives_EnemyFlagItemForTeam(botTeam);
	const char *className = BotObjectives_FlagClassNameForItem(item);
	if (className == nullptr) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	BotObjectiveTarget best{};
	int bestScore = -999999;
	gentity_t *flag = nullptr;
	while ((flag = G_FindByString<&gentity_t::className>(flag, className)) != nullptr) {
		if (flag->item == nullptr || flag->item->id != item) {
			continue;
		}

		const BotObjectiveTargetSource source = BotObjectives_IsDroppedFlagEntity(flag)
			? BotObjectiveTargetSource::DroppedFlagEntity
			: BotObjectiveTargetSource::WorldFlagEntity;
		BotObjectives_ConsiderTargetCandidate(
			bot,
			BotObjectives_BuildTargetForEntity(bot, flag, item, source, -1),
			&best,
			&bestScore);
	}

	for (int client = 0; client < game.maxClients; ++client) {
		const int entnum = client + 1;
		if (entnum >= globals.numEntities) {
			break;
		}

		const gentity_t *carrier = &g_entities[entnum];
		if (!BotObjectives_IsAlivePlayer(carrier) ||
			carrier->client->pers.inventory[item] <= 0) {
			continue;
		}

		BotObjectives_ConsiderTargetCandidate(
			bot,
			BotObjectives_BuildTargetForEntity(
				bot,
				carrier,
				item,
				BotObjectiveTargetSource::FlagCarrier,
				client),
			&best,
			&bestScore);
	}

	if (allowEnemyTeamAnchor && bestScore < 0) {
		const int enemyTeam = BotObjectives_FlagOwnerTeamForItem(item);
		for (int client = 0; client < game.maxClients; ++client) {
			const int entnum = client + 1;
			if (entnum >= globals.numEntities) {
				break;
			}

			const gentity_t *enemy = &g_entities[entnum];
			if (!BotObjectives_IsAlivePlayer(enemy) ||
				static_cast<int>(enemy->client->sess.team) != enemyTeam) {
				continue;
			}

			BotObjectives_ConsiderTargetCandidate(
				bot,
				BotObjectives_BuildTargetForEntity(
					bot,
					enemy,
					item,
					BotObjectiveTargetSource::EnemyTeamAnchor,
					client),
				&best,
				&bestScore);
		}
	}

	if (bestScore < 0) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	BotObjectives_RecordTargetSource(best.source);
	const BotObjectiveRole defaultRole = BotObjectives_DefaultRoleForTarget(best);
	BotObjectives_RecordLastTarget(
		best,
		BotObjectives_ClientIndexForEntity(bot),
		botTeam,
		defaultRole,
		BotObjectives_RolePriorityForTarget(defaultRole, best));
	return best;
}

BotObjectiveAssignment BotObjectives_Assign(const BotObjectiveContext &context) {
	botObjectiveStatus.evaluations++;

	if (!context.smokeEnabled) {
		botObjectiveStatus.disabledEvaluations++;
		return {};
	}
	if (!context.valid || context.clientIndex < 0) {
		botObjectiveStatus.invalidContexts++;
		return {};
	}
	if (!context.alive) {
		botObjectiveStatus.deadContexts++;
		return {};
	}
	if (!BotObjectives_IsPrimaryTeam(context.team)) {
		botObjectiveStatus.missingTeams++;
		return {};
	}
	if (!context.target.available || context.target.entity < 0 || context.target.type == BotObjectiveType::None) {
		botObjectiveStatus.missingObjectives++;
		return {};
	}
	if (!context.target.reachable || context.target.area <= 0) {
		botObjectiveStatus.unreachableObjectives++;
		return {};
	}

	const BotObjectiveRolePolicy rolePolicy = BotObjectives_EvaluateRolePolicy(context);
	if (!rolePolicy.valid ||
		rolePolicy.role == BotObjectiveRole::None ||
		rolePolicy.assignmentPriority <= 0) {
		botObjectiveStatus.missingObjectives++;
		return {};
	}

	BotObjectiveAssignment assignment{
		.assigned = true,
		.wantsRoute = true,
		.type = context.target.type,
		.role = rolePolicy.role,
		.source = context.target.source,
		.priority = rolePolicy.assignmentPriority,
		.rolePriority = rolePolicy.rolePriority,
		.attackPriority = rolePolicy.attackPriority,
		.defendPriority = rolePolicy.defendPriority,
		.returnPriority = rolePolicy.returnPriority,
		.supportPriority = rolePolicy.supportPriority,
		.clientIndex = context.clientIndex,
		.team = context.team,
		.targetTeam = context.target.ownerTeam,
		.entity = context.target.entity,
		.spawnCount = context.target.spawnCount,
		.item = context.target.item,
		.area = context.target.area,
		.carrierClient = context.target.carrierClient,
		.origin = {
			context.target.origin[0],
			context.target.origin[1],
			context.target.origin[2],
		},
		.reason = rolePolicy.reason,
	};

	botObjectiveStatus.assignments++;
	BotObjectives_RecordAssignmentKind(assignment.type, assignment.role);
	BotObjectives_RecordLast(assignment);
	return assignment;
}

BotObjectiveAssignment BotObjectives_AssignEnemyFlagObjective(
	const gentity_t *bot,
	bool smokeEnabled,
	BotObjectiveRole requestedRole,
	bool allowEnemyTeamAnchor) {
	const BotObjectiveTarget target = BotObjectives_SelectEnemyFlagTarget(bot, allowEnemyTeamAnchor);
	return BotObjectives_Assign(
		BotObjectives_BuildContextForTarget(bot, target, smokeEnabled, requestedRole));
}

bool BotObjectives_BuildRouteGoal(const BotObjectiveAssignment &assignment, BotObjectiveRouteGoal *goal) {
	if (goal == nullptr) {
		botObjectiveStatus.invalidEventHooks++;
		return false;
	}

	*goal = {};
	if (!BotObjectives_AssignmentHasRouteTarget(assignment)) {
		return false;
	}

	goal->valid = true;
	goal->area = assignment.area;
	goal->entity = assignment.entity;
	goal->spawnCount = assignment.spawnCount;
	goal->item = assignment.item;
	goal->origin[0] = assignment.origin[0];
	goal->origin[1] = assignment.origin[1];
	goal->origin[2] = assignment.origin[2];
	return true;
}

void BotObjectives_RecordRouteRequest(const BotObjectiveAssignment &assignment) {
	if (!BotObjectives_AssignmentHasRouteTarget(assignment)) {
		return;
	}

	botObjectiveStatus.routeRequests++;
	BotObjectives_RecordLast(assignment);
}

void BotObjectives_RecordRouteRequest(
	const BotObjectiveRouteGoal &goal,
	const BotObjectiveAssignment &assignment) {
	if (!BotObjectives_RouteGoalMatchesAssignment(goal, assignment)) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	BotObjectives_RecordRouteRequest(assignment);
}

void BotObjectives_RecordRouteCommand(const BotObjectiveAssignment &assignment) {
	if (!BotObjectives_AssignmentHasRouteTarget(assignment)) {
		return;
	}

	botObjectiveStatus.routeCommands++;
	BotObjectives_RecordLast(assignment);
}

void BotObjectives_RecordRouteCommand(
	const BotObjectiveRouteGoal &goal,
	const BotObjectiveAssignment &assignment) {
	if (!BotObjectives_RouteGoalMatchesAssignment(goal, assignment)) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	BotObjectives_RecordRouteCommand(assignment);
}

void BotObjectives_RecordReach(const BotObjectiveAssignment &assignment) {
	if (!BotObjectives_AssignmentHasRouteTarget(assignment)) {
		return;
	}

	botObjectiveStatus.reaches++;
	BotObjectives_RecordLast(assignment);
}

void BotObjectives_RecordReach(
	const BotObjectiveRouteGoal &goal,
	const BotObjectiveAssignment &assignment) {
	if (!BotObjectives_RouteGoalMatchesAssignment(goal, assignment)) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	BotObjectives_RecordReach(assignment);
}

void BotObjectives_RecordFlagPickup(int clientIndex, int team, int item) {
	if (clientIndex < 0 || item <= 0) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	const BotObjectiveType type = BotObjectives_FlagObjectiveTypeForTeam(team, item);
	if (type == BotObjectiveType::None) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	botObjectiveStatus.flagPickups++;
	switch (type) {
	case BotObjectiveType::EnemyFlagPickup:
		botObjectiveStatus.enemyFlagPickups++;
		break;
	case BotObjectiveType::OwnFlagReturn:
		botObjectiveStatus.ownFlagReturns++;
		break;
	case BotObjectiveType::NeutralFlagPickup:
		botObjectiveStatus.neutralFlagPickups++;
		break;
	default:
		break;
	}
	BotObjectives_RecordLastEvent(
		type,
		BotObjectives_DefaultRoleForType(type),
		clientIndex,
		team,
		BotObjectives_FlagOwnerTeamForItem(item),
		item,
		-1,
		BotObjectiveTargetSource::None,
		0,
		0,
		0);
}

void BotObjectives_RecordFlagCapture(int clientIndex, int team, int item) {
	if (clientIndex < 0 || item <= 0) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	const BotObjectiveType type = BotObjectives_FlagObjectiveTypeForTeam(team, item);
	if (type == BotObjectiveType::None) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	botObjectiveStatus.flagCaptures++;
	switch (type) {
	case BotObjectiveType::EnemyFlagPickup:
		botObjectiveStatus.enemyFlagCaptures++;
		break;
	case BotObjectiveType::NeutralFlagPickup:
		botObjectiveStatus.neutralFlagCaptures++;
		break;
	default:
		break;
	}
	BotObjectives_RecordLastEvent(
		type,
		BotObjectives_DefaultRoleForType(type),
		clientIndex,
		team,
		BotObjectives_FlagOwnerTeamForItem(item),
		item,
		-1,
		BotObjectiveTargetSource::None,
		0,
		0,
		0);
}

void BotObjectives_RecordFlagPickup(const gentity_t *player, const gentity_t *flag) {
	if (player == nullptr || player->client == nullptr || flag == nullptr || flag->item == nullptr) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	const int clientIndex = BotObjectives_ClientIndexForEntity(player);
	const int team = static_cast<int>(player->client->sess.team);
	const int item = flag->item->id;
	if (!BotObjectives_IsFlagItem(item)) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	BotObjectives_RecordFlagPickup(clientIndex, team, item);
	botObjectiveStatus.lastEntity = BotObjectives_EntityNumber(flag);
	botObjectiveStatus.lastSpawnCount = flag->spawn_count;
	botObjectiveStatus.lastTargetSource = static_cast<int>(
		BotObjectives_IsDroppedFlagEntity(flag)
			? BotObjectiveTargetSource::DroppedFlagEntity
			: BotObjectiveTargetSource::WorldFlagEntity);
	botObjectiveStatus.lastOriginX = static_cast<int>(flag->s.origin.x);
	botObjectiveStatus.lastOriginY = static_cast<int>(flag->s.origin.y);
	botObjectiveStatus.lastOriginZ = static_cast<int>(flag->s.origin.z);
}

void BotObjectives_RecordFlagCapture(const gentity_t *player, int item) {
	if (player == nullptr || player->client == nullptr) {
		botObjectiveStatus.invalidEventHooks++;
		return;
	}

	BotObjectives_RecordFlagCapture(
		BotObjectives_ClientIndexForEntity(player),
		static_cast<int>(player->client->sess.team),
		item);
}

const BotObjectiveStatus &BotObjectives_GetStatus() {
	return botObjectiveStatus;
}

BotObjectiveType BotObjectives_FlagObjectiveTypeForTeam(int botTeam, int flagItem) {
	if (flagItem == IT_FLAG_NEUTRAL) {
		return BotObjectiveType::NeutralFlagPickup;
	}
	if (botTeam == static_cast<int>(Team::Red)) {
		if (flagItem == IT_FLAG_BLUE) {
			return BotObjectiveType::EnemyFlagPickup;
		}
		if (flagItem == IT_FLAG_RED) {
			return BotObjectiveType::OwnFlagReturn;
		}
	}
	if (botTeam == static_cast<int>(Team::Blue)) {
		if (flagItem == IT_FLAG_RED) {
			return BotObjectiveType::EnemyFlagPickup;
		}
		if (flagItem == IT_FLAG_BLUE) {
			return BotObjectiveType::OwnFlagReturn;
		}
	}
	return BotObjectiveType::None;
}

int BotObjectives_FlagOwnerTeamForItem(int flagItem) {
	switch (flagItem) {
	case IT_FLAG_RED:
		return static_cast<int>(Team::Red);
	case IT_FLAG_BLUE:
		return static_cast<int>(Team::Blue);
	case IT_FLAG_NEUTRAL:
		return static_cast<int>(Team::Free);
	default:
		return static_cast<int>(Team::None);
	}
}

int BotObjectives_EnemyFlagItemForTeam(int team) {
	if (team == static_cast<int>(Team::Red)) {
		return IT_FLAG_BLUE;
	}
	if (team == static_cast<int>(Team::Blue)) {
		return IT_FLAG_RED;
	}
	return IT_NULL;
}

int BotObjectives_OwnFlagItemForTeam(int team) {
	if (team == static_cast<int>(Team::Red)) {
		return IT_FLAG_RED;
	}
	if (team == static_cast<int>(Team::Blue)) {
		return IT_FLAG_BLUE;
	}
	return IT_NULL;
}

int BotObjectives_ClientIndexForEntity(const gentity_t *ent) {
	if (ent == nullptr || ent->client == nullptr) {
		return -1;
	}

	const int entnum = BotObjectives_EntityNumber(ent);
	if (entnum > 0) {
		return entnum - 1;
	}
	return static_cast<int>(ent->client - game.clients);
}

const char *BotObjectives_TypeName(BotObjectiveType type) {
	switch (type) {
	case BotObjectiveType::EnemyFlagPickup:
		return "enemy_flag_pickup";
	case BotObjectiveType::OwnFlagReturn:
		return "own_flag_return";
	case BotObjectiveType::NeutralFlagPickup:
		return "neutral_flag_pickup";
	case BotObjectiveType::BaseDefense:
		return "base_defense";
	default:
		return "none";
	}
}

const char *BotObjectives_TargetSourceName(BotObjectiveTargetSource source) {
	switch (source) {
	case BotObjectiveTargetSource::WorldFlagEntity:
		return "world_flag_entity";
	case BotObjectiveTargetSource::DroppedFlagEntity:
		return "dropped_flag_entity";
	case BotObjectiveTargetSource::FlagCarrier:
		return "flag_carrier";
	case BotObjectiveTargetSource::EnemyTeamAnchor:
		return "enemy_team_anchor";
	default:
		return "none";
	}
}

const char *BotObjectives_RoleName(BotObjectiveRole role) {
	switch (role) {
	case BotObjectiveRole::Attacker:
		return "attacker";
	case BotObjectiveRole::Defender:
		return "defender";
	case BotObjectiveRole::Returner:
		return "returner";
	case BotObjectiveRole::Support:
		return "support";
	default:
		return "none";
	}
}
