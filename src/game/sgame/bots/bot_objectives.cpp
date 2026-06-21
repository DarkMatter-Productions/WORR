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
constexpr int BOT_OBJECTIVE_DROPPED_FLAG_BONUS = 140;
constexpr int BOT_OBJECTIVE_OWN_FLAG_CARRIER_BONUS = 170;
constexpr int BOT_OBJECTIVE_OWN_BASE_GUARD_PENALTY = 20;
constexpr int BOT_OBJECTIVE_HOME_RETURNER_PENALTY = 80;
constexpr int BOT_OBJECTIVE_MIDFIELD_ROLE_BONUS = 35;
constexpr int BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY = 40;
constexpr int BOT_OBJECTIVE_FFA_SCORE_PRIORITY = 620;
constexpr int BOT_OBJECTIVE_TDM_SCORE_PRIORITY = 700;
constexpr int BOT_OBJECTIVE_CTF_SCORE_PRIORITY = 760;
constexpr int BOT_OBJECTIVE_ATTACK_ROLE_BONUS = 40;
constexpr int BOT_OBJECTIVE_DEFENSE_ROLE_BONUS = 20;
constexpr int BOT_OBJECTIVE_MIDFIELD_POLICY_BONUS = 30;
constexpr int BOT_OBJECTIVE_ITEM_ROLE_BASE_PRIORITY = 100;
constexpr int BOT_OBJECTIVE_ITEM_ROLE_HEALTH_ARMOR_BOOST = 40;
constexpr int BOT_OBJECTIVE_ITEM_ROLE_WEAPON_BOOST = 75;
constexpr int BOT_OBJECTIVE_ITEM_ROLE_POWERUP_BOOST = 115;
constexpr int BOT_OBJECTIVE_ITEM_ROLE_OBJECTIVE_BOOST = 220;
constexpr int BOT_OBJECTIVE_ITEM_ROLE_TEAM_RESOURCE_BOOST = 55;
constexpr int BOT_OBJECTIVE_COOP_FOLLOW_PRIORITY = 660;
constexpr int BOT_OBJECTIVE_COOP_WAIT_PRIORITY = 720;
constexpr int BOT_OBJECTIVE_COOP_REGROUP_PRIORITY = 700;
constexpr int BOT_OBJECTIVE_COOP_LEAD_PRIORITY = 560;
constexpr int BOT_OBJECTIVE_COOP_SUPPORT_PRIORITY = 620;
constexpr int BOT_OBJECTIVE_COOP_RESOURCE_PRIORITY = 580;
constexpr int BOT_OBJECTIVE_COOP_CLOSE_DISTANCE_SQUARED = 384 * 384;
constexpr int BOT_OBJECTIVE_COOP_FOLLOW_DISTANCE_SQUARED = 768 * 768;
constexpr int BOT_OBJECTIVE_COOP_REGROUP_DISTANCE_SQUARED = 1400 * 1400;
constexpr int BOT_OBJECTIVE_RESOURCE_SELF_BOOST = 30;
constexpr int BOT_OBJECTIVE_RESOURCE_SHARE_BOOST = 60;
constexpr int BOT_OBJECTIVE_RESOURCE_RESERVE_BOOST = 80;
constexpr int BOT_OBJECTIVE_RESOURCE_DENY_BOOST = 95;
constexpr int BOT_OBJECTIVE_RESOURCE_OBJECTIVE_BOOST = 180;
constexpr int BOT_OBJECTIVE_DISTANCE_SENTINEL = 0x3fffffff;

BotObjectiveStatus botObjectiveStatus;

struct BotObjectiveRoleLaneCandidate {
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveLane lane = BotObjectiveLane::None;
	int priority = 0;
	const char *reason = "none";
};

static_assert(static_cast<int>(BotObjectiveType::EnemyFlagPickup) == 1);
static_assert(static_cast<int>(BotObjectiveType::OwnFlagReturn) == 2);
static_assert(static_cast<int>(BotObjectiveType::NeutralFlagPickup) == 3);
static_assert(static_cast<int>(BotObjectiveType::BaseDefense) == 4);
static_assert(static_cast<int>(BotObjectiveRole::None) == 0);
static_assert(static_cast<int>(BotObjectiveRole::Attacker) == 1);
static_assert(static_cast<int>(BotObjectiveRole::Defender) == 2);
static_assert(static_cast<int>(BotObjectiveRole::Returner) == 3);
static_assert(static_cast<int>(BotObjectiveRole::Support) == 4);
static_assert(static_cast<int>(BotObjectiveRole::Midfielder) == 5);
static_assert(static_cast<int>(BotObjectiveLane::None) == 0);
static_assert(static_cast<int>(BotObjectiveLane::Attack) == 1);
static_assert(static_cast<int>(BotObjectiveLane::Defense) == 2);
static_assert(static_cast<int>(BotObjectiveLane::Midfield) == 3);
static_assert(static_cast<int>(BotObjectiveLane::CarrierSupport) == 4);
static_assert(static_cast<int>(BotObjectiveLane::DroppedFlagResponse) == 5);
static_assert(static_cast<int>(BotObjectiveLane::OwnBaseReturn) == 6);
static_assert(static_cast<int>(BotObjectiveTargetSource::WorldFlagEntity) == 1);
static_assert(static_cast<int>(BotObjectiveTargetSource::DroppedFlagEntity) == 2);
static_assert(static_cast<int>(BotObjectiveTargetSource::FlagCarrier) == 3);
static_assert(static_cast<int>(BotObjectiveTargetSource::EnemyTeamAnchor) == 4);
static_assert(static_cast<int>(BotObjectiveMatchMode::FreeForAll) == 1);
static_assert(static_cast<int>(BotObjectiveMatchMode::TeamDeathmatch) == 2);
static_assert(static_cast<int>(BotObjectiveMatchMode::CaptureTheFlag) == 3);
static_assert(static_cast<int>(BotObjectiveMatchMode::Cooperative) == 4);
static_assert(static_cast<int>(BotObjectiveItemCategory::Health) == 1);
static_assert(static_cast<int>(BotObjectiveItemCategory::CtfObjective) == 7);
static_assert(static_cast<int>(BotObjectiveItemRole::SelfStack) == 1);
static_assert(static_cast<int>(BotObjectiveItemRole::Objective) == 6);
static_assert(static_cast<int>(BotObjectiveCoopIntent::FollowLeader) == 1);
static_assert(static_cast<int>(BotObjectiveCoopIntent::SupportCombat) == 5);
static_assert(static_cast<int>(BotObjectiveResourceIntent::SelfPickup) == 1);
static_assert(static_cast<int>(BotObjectiveResourceIntent::Objective) == 5);

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

bool BotObjectives_IsHumanPlayer(const gentity_t *ent) {
	return BotObjectives_IsAlivePlayer(ent) && !BotObjectives_IsBotEntity(ent);
}

bool BotObjectives_IsCoopMode() {
	return coop != nullptr && coop->integer != 0;
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

int BotObjectives_DistanceSquared(const gentity_t *a, const gentity_t *b) {
	if (a == nullptr || b == nullptr) {
		return BOT_OBJECTIVE_DISTANCE_SENTINEL;
	}

	const float dx = a->s.origin.x - b->s.origin.x;
	const float dy = a->s.origin.y - b->s.origin.y;
	const float dz = a->s.origin.z - b->s.origin.z;
	return static_cast<int>((dx * dx) + (dy * dy) + (dz * dz));
}

void BotObjectives_CountCoopPlayers(int *teamSize, int *humanCount, int *botCount) {
	if (teamSize != nullptr) {
		*teamSize = 0;
	}
	if (humanCount != nullptr) {
		*humanCount = 0;
	}
	if (botCount != nullptr) {
		*botCount = 0;
	}

	for (int client = 0; client < game.maxClients; ++client) {
		const int entnum = client + 1;
		if (entnum >= globals.numEntities) {
			break;
		}

		const gentity_t *player = &g_entities[entnum];
		if (!BotObjectives_IsAlivePlayer(player)) {
			continue;
		}

		if (teamSize != nullptr) {
			(*teamSize)++;
		}
		if (BotObjectives_IsBotEntity(player)) {
			if (botCount != nullptr) {
				(*botCount)++;
			}
		} else if (humanCount != nullptr) {
			(*humanCount)++;
		}
	}
}

const gentity_t *BotObjectives_FindCoopLeader(const gentity_t *bot, const gentity_t *preferredLeader) {
	if (preferredLeader != nullptr &&
		preferredLeader != bot &&
		BotObjectives_IsAlivePlayer(preferredLeader)) {
		return preferredLeader;
	}

	const gentity_t *bestHuman = nullptr;
	const gentity_t *bestAny = nullptr;
	int bestHumanDistance = BOT_OBJECTIVE_DISTANCE_SENTINEL;
	int bestAnyDistance = BOT_OBJECTIVE_DISTANCE_SENTINEL;

	for (int client = 0; client < game.maxClients; ++client) {
		const int entnum = client + 1;
		if (entnum >= globals.numEntities) {
			break;
		}

		const gentity_t *player = &g_entities[entnum];
		if (player == bot || !BotObjectives_IsAlivePlayer(player)) {
			continue;
		}

		const int distanceSquared = BotObjectives_DistanceSquared(bot, player);
		if (distanceSquared < bestAnyDistance) {
			bestAny = player;
			bestAnyDistance = distanceSquared;
		}
		if (BotObjectives_IsHumanPlayer(player) && distanceSquared < bestHumanDistance) {
			bestHuman = player;
			bestHumanDistance = distanceSquared;
		}
	}

	return bestHuman != nullptr ? bestHuman : bestAny;
}

bool BotObjectives_IsTeamMatchMode(BotObjectiveMatchMode mode) {
	return mode == BotObjectiveMatchMode::TeamDeathmatch ||
		mode == BotObjectiveMatchMode::CaptureTheFlag;
}

bool BotObjectives_IsMidfieldPolicyRole(BotObjectiveRole role) {
	return role == BotObjectiveRole::Midfielder || role == BotObjectiveRole::Support;
}

int BotObjectives_FriendlyFireScalePercent() {
	if (g_friendlyFireScale == nullptr || g_friendlyFireScale->value <= 0.0f) {
		return 0;
	}
	return static_cast<int>((g_friendlyFireScale->value * 100.0f) + 0.5f);
}

int BotObjectives_EnemyTeamScoreForTeam(int team) {
	if (team == static_cast<int>(Team::Red)) {
		return level.teamScores[static_cast<int>(Team::Blue)];
	}
	if (team == static_cast<int>(Team::Blue)) {
		return level.teamScores[static_cast<int>(Team::Red)];
	}
	return 0;
}

void BotObjectives_CountMatchPlayers(
	int clientIndex,
	int team,
	BotObjectiveMatchMode mode,
	int *teamSize,
	int *enemyCount) {
	if (teamSize != nullptr) {
		*teamSize = 0;
	}
	if (enemyCount != nullptr) {
		*enemyCount = 0;
	}

	for (int client = 0; client < game.maxClients; ++client) {
		const int entnum = client + 1;
		if (entnum >= globals.numEntities) {
			break;
		}

		const gentity_t *player = &g_entities[entnum];
		if (!BotObjectives_IsAlivePlayer(player)) {
			continue;
		}

		if (!BotObjectives_IsTeamMatchMode(mode)) {
			if (client == clientIndex) {
				if (teamSize != nullptr) {
					(*teamSize)++;
				}
			} else if (enemyCount != nullptr) {
				(*enemyCount)++;
			}
			continue;
		}

		const int playerTeam = static_cast<int>(player->client->sess.team);
		if (playerTeam == team) {
			if (teamSize != nullptr) {
				(*teamSize)++;
			}
		} else if (BotObjectives_IsPrimaryTeam(playerTeam) && enemyCount != nullptr) {
			(*enemyCount)++;
		}
	}
}

BotObjectiveRole BotObjectives_DeterministicShapeRole(const BotObjectiveMatchContext &context) {
	if (context.mode == BotObjectiveMatchMode::FreeForAll) {
		return BotObjectiveRole::Attacker;
	}

	const int teamOffset = context.team == static_cast<int>(Team::Blue) ? 1 : 0;
	const int seed = context.clientIndex >= 0 ? context.clientIndex + teamOffset : teamOffset;
	switch (seed % 3) {
	case 0:
		return BotObjectiveRole::Attacker;
	case 1:
		return BotObjectiveRole::Defender;
	default:
		return BotObjectiveRole::Midfielder;
	}
}

bool BotObjectives_MatchRoleCompatible(BotObjectiveMatchMode mode, BotObjectiveRole role) {
	if (role == BotObjectiveRole::None || role == BotObjectiveRole::Returner) {
		return false;
	}

	if (mode == BotObjectiveMatchMode::FreeForAll) {
		return role == BotObjectiveRole::Attacker || role == BotObjectiveRole::Midfielder;
	}

	if (BotObjectives_IsTeamMatchMode(mode)) {
		return role == BotObjectiveRole::Attacker ||
			role == BotObjectiveRole::Defender ||
			BotObjectives_IsMidfieldPolicyRole(role);
	}

	return false;
}

const char *BotObjectives_MatchPolicyReason(BotObjectiveMatchMode mode, BotObjectiveRole role) {
	switch (mode) {
	case BotObjectiveMatchMode::FreeForAll:
		return role == BotObjectiveRole::Midfielder
			? "ffa_midfield_roam_collect_engage"
			: "ffa_score_roam_collect_engage";
	case BotObjectiveMatchMode::TeamDeathmatch:
		if (role == BotObjectiveRole::Attacker) {
			return "tdm_attack_score_pressure";
		}
		if (role == BotObjectiveRole::Defender) {
			return "tdm_defend_resource_control";
		}
		return "tdm_midfield_score_support";
	case BotObjectiveMatchMode::CaptureTheFlag:
		if (role == BotObjectiveRole::Attacker) {
			return "ctf_attack_objective_pressure";
		}
		if (role == BotObjectiveRole::Defender) {
			return "ctf_defend_base_shape";
		}
		return "ctf_midfield_transition_support";
	default:
		return "none";
	}
}

int BotObjectives_MatchBasePriority(BotObjectiveMatchMode mode) {
	switch (mode) {
	case BotObjectiveMatchMode::FreeForAll:
		return BOT_OBJECTIVE_FFA_SCORE_PRIORITY;
	case BotObjectiveMatchMode::TeamDeathmatch:
		return BOT_OBJECTIVE_TDM_SCORE_PRIORITY;
	case BotObjectiveMatchMode::CaptureTheFlag:
		return BOT_OBJECTIVE_CTF_SCORE_PRIORITY;
	default:
		return 0;
	}
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

bool BotObjectives_IsPickupType(BotObjectiveType type) {
	return type == BotObjectiveType::EnemyFlagPickup ||
		type == BotObjectiveType::NeutralFlagPickup;
}

const char *BotObjectives_RolePolicyReason(
	BotObjectiveRole role,
	BotObjectiveLane lane,
	BotObjectiveType type,
	BotObjectiveTargetSource source) {
	switch (lane) {
	case BotObjectiveLane::Attack:
		return type == BotObjectiveType::NeutralFlagPickup
			? "attack_neutral_flag"
			: "attack_enemy_flag";
	case BotObjectiveLane::Defense:
		return type == BotObjectiveType::OwnFlagReturn
			? "defend_return_lane"
			: "defend_base";
	case BotObjectiveLane::Midfield:
		if (source == BotObjectiveTargetSource::EnemyTeamAnchor) {
			return role == BotObjectiveRole::Attacker
				? "contest_midfield_anchor"
				: "support_midfield_anchor";
		}
		return role == BotObjectiveRole::Support
			? "support_midfield"
			: "control_midfield";
	case BotObjectiveLane::CarrierSupport:
		return "support_flag_carrier";
	case BotObjectiveLane::DroppedFlagResponse:
		return type == BotObjectiveType::OwnFlagReturn
			? "return_dropped_own_flag"
			: "recover_dropped_objective_flag";
	case BotObjectiveLane::OwnBaseReturn:
		if (source == BotObjectiveTargetSource::FlagCarrier) {
			return "hunt_own_flag_carrier";
		}
		return type == BotObjectiveType::OwnFlagReturn
			? "guard_own_base_return"
			: "prioritize_own_base_return";
	default:
		return "none";
	}
}

int BotObjectives_RoleLaneCandidatePriority(
	BotObjectiveRole role,
	BotObjectiveLane lane,
	const BotObjectiveTarget &target) {
	const int basePriority = BotObjectives_PriorityForType(target.type);
	if (basePriority <= 0 ||
		role == BotObjectiveRole::None ||
		lane == BotObjectiveLane::None) {
		return 0;
	}

	const bool pickupType = BotObjectives_IsPickupType(target.type);
	const bool droppedFlag = target.source == BotObjectiveTargetSource::DroppedFlagEntity;
	const bool flagCarrier = target.source == BotObjectiveTargetSource::FlagCarrier &&
		target.carrierClient >= 0;
	const bool enemyAnchor = target.source == BotObjectiveTargetSource::EnemyTeamAnchor;

	if (pickupType) {
		switch (role) {
		case BotObjectiveRole::Attacker:
			if (droppedFlag && lane == BotObjectiveLane::DroppedFlagResponse) {
				return basePriority + BOT_OBJECTIVE_DROPPED_FLAG_BONUS;
			}
			if (enemyAnchor && lane == BotObjectiveLane::Midfield) {
				return basePriority - (BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY / 2);
			}
			if (!flagCarrier && !enemyAnchor && lane == BotObjectiveLane::Attack) {
				return basePriority;
			}
			break;
		case BotObjectiveRole::Support:
			if (flagCarrier && lane == BotObjectiveLane::CarrierSupport) {
				return basePriority + BOT_OBJECTIVE_SUPPORT_ROLE_BONUS;
			}
			if (droppedFlag && lane == BotObjectiveLane::DroppedFlagResponse) {
				return basePriority + BOT_OBJECTIVE_SUPPORT_ROLE_BONUS -
					(BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY / 2);
			}
			if (enemyAnchor && lane == BotObjectiveLane::Midfield) {
				return basePriority - BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY;
			}
			break;
		default:
			break;
		}
	}

	if (target.type == BotObjectiveType::OwnFlagReturn) {
		switch (role) {
		case BotObjectiveRole::Returner:
			if (droppedFlag && lane == BotObjectiveLane::DroppedFlagResponse) {
				return basePriority + BOT_OBJECTIVE_RETURN_ROLE_BONUS +
					BOT_OBJECTIVE_DROPPED_FLAG_BONUS;
			}
			if (flagCarrier && lane == BotObjectiveLane::OwnBaseReturn) {
				return basePriority + BOT_OBJECTIVE_RETURN_ROLE_BONUS +
					BOT_OBJECTIVE_OWN_FLAG_CARRIER_BONUS;
			}
			if (target.source == BotObjectiveTargetSource::WorldFlagEntity &&
				lane == BotObjectiveLane::OwnBaseReturn) {
				return basePriority - BOT_OBJECTIVE_HOME_RETURNER_PENALTY;
			}
			break;
		case BotObjectiveRole::Defender:
			if (target.source == BotObjectiveTargetSource::WorldFlagEntity &&
				lane == BotObjectiveLane::OwnBaseReturn) {
				return basePriority - BOT_OBJECTIVE_OWN_BASE_GUARD_PENALTY;
			}
			if (flagCarrier && lane == BotObjectiveLane::Defense) {
				return basePriority - BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY;
			}
			break;
		case BotObjectiveRole::Support:
			if (flagCarrier && lane == BotObjectiveLane::Midfield) {
				return basePriority + BOT_OBJECTIVE_SUPPORT_ROLE_BONUS;
			}
			if (droppedFlag && lane == BotObjectiveLane::DroppedFlagResponse) {
				return basePriority + BOT_OBJECTIVE_SUPPORT_ROLE_BONUS -
					(BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY / 2);
			}
			break;
		default:
			break;
		}
	}

	if (target.type == BotObjectiveType::BaseDefense) {
		switch (role) {
		case BotObjectiveRole::Defender:
			if (lane == BotObjectiveLane::Defense) {
				return basePriority;
			}
			break;
		case BotObjectiveRole::Support:
			if (lane == BotObjectiveLane::Midfield) {
				return enemyAnchor
					? basePriority + BOT_OBJECTIVE_MIDFIELD_ROLE_BONUS -
						(BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY / 2)
					: basePriority - BOT_OBJECTIVE_SECONDARY_ROLE_PENALTY;
			}
			break;
		case BotObjectiveRole::Attacker:
			if (enemyAnchor && lane == BotObjectiveLane::Midfield) {
				return basePriority + BOT_OBJECTIVE_MIDFIELD_ROLE_BONUS;
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

BotObjectiveRoleLaneCandidate BotObjectives_BuildRoleLaneCandidate(
	BotObjectiveRole role,
	BotObjectiveLane lane,
	const BotObjectiveTarget &target) {
	BotObjectiveRoleLaneCandidate candidate{};
	candidate.role = role;
	candidate.lane = lane;
	candidate.priority = BotObjectives_RoleLaneCandidatePriority(role, lane, target);
	if (candidate.priority > 0) {
		candidate.reason = BotObjectives_RolePolicyReason(
			role,
			lane,
			target.type,
			target.source);
	}
	return candidate;
}

BotObjectiveRoleLaneCandidate BotObjectives_BestCandidateForRole(
	BotObjectiveRole role,
	const BotObjectiveTarget &target) {
	BotObjectiveRoleLaneCandidate best{};
	const BotObjectiveLane lanes[] = {
		BotObjectiveLane::DroppedFlagResponse,
		BotObjectiveLane::CarrierSupport,
		BotObjectiveLane::OwnBaseReturn,
		BotObjectiveLane::Attack,
		BotObjectiveLane::Defense,
		BotObjectiveLane::Midfield,
	};

	for (const BotObjectiveLane lane : lanes) {
		const BotObjectiveRoleLaneCandidate candidate =
			BotObjectives_BuildRoleLaneCandidate(role, lane, target);
		if (candidate.priority > best.priority) {
			best = candidate;
		}
	}
	return best;
}

int BotObjectives_BestLanePriority(
	BotObjectiveLane lane,
	const BotObjectiveTarget &target) {
	int bestPriority = 0;
	const BotObjectiveRole roles[] = {
		BotObjectiveRole::Returner,
		BotObjectiveRole::Support,
		BotObjectiveRole::Attacker,
		BotObjectiveRole::Defender,
	};

	for (const BotObjectiveRole role : roles) {
		const int priority = BotObjectives_RoleLaneCandidatePriority(role, lane, target);
		if (priority > bestPriority) {
			bestPriority = priority;
		}
	}
	return bestPriority;
}

void BotObjectives_SetRolePolicyChoice(
	BotObjectiveRolePolicy *policy,
	const BotObjectiveRoleLaneCandidate &candidate,
	bool requestedRoleHonored) {
	if (policy == nullptr ||
		candidate.role == BotObjectiveRole::None ||
		candidate.lane == BotObjectiveLane::None ||
		candidate.priority <= 0) {
		return;
	}

	policy->valid = true;
	policy->requestedRoleHonored = requestedRoleHonored;
	policy->role = candidate.role;
	policy->lane = candidate.lane;
	policy->assignmentPriority = candidate.priority;
	policy->rolePriority = candidate.priority;
	policy->lanePriority = candidate.priority;
	policy->reason = candidate.reason;
	policy->laneReason = candidate.reason;
}

void BotObjectives_ApplyRolePolicyChoice(
	BotObjectiveRolePolicy *policy,
	const BotObjectiveTarget &target,
	BotObjectiveRole role,
	int priority,
	bool requestedRoleHonored) {
	BotObjectiveRoleLaneCandidate candidate = BotObjectives_BestCandidateForRole(role, target);
	if (priority > 0 && candidate.priority == priority) {
		BotObjectives_SetRolePolicyChoice(policy, candidate, requestedRoleHonored);
	}
}

void BotObjectives_ConsiderRolePolicyCandidate(
	BotObjectiveRolePolicy *policy,
	const BotObjectiveTarget &target,
	BotObjectiveRole role,
	int priority) {
	if (policy == nullptr || role == BotObjectiveRole::None || priority <= 0) {
		return;
	}

	const BotObjectiveRoleLaneCandidate candidate = BotObjectives_BestCandidateForRole(role, target);
	if (candidate.priority != priority) {
		return;
	}

	if (!policy->valid || candidate.priority > policy->assignmentPriority) {
		BotObjectives_SetRolePolicyChoice(policy, candidate, false);
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

	switch (policy.lane) {
	case BotObjectiveLane::Attack:
		botObjectiveStatus.rolePolicyLaneAttackSelections++;
		break;
	case BotObjectiveLane::Defense:
		botObjectiveStatus.rolePolicyLaneDefenseSelections++;
		break;
	case BotObjectiveLane::Midfield:
		botObjectiveStatus.rolePolicyLaneMidfieldSelections++;
		break;
	case BotObjectiveLane::CarrierSupport:
		botObjectiveStatus.rolePolicyCarrierSupportSelections++;
		break;
	case BotObjectiveLane::DroppedFlagResponse:
		botObjectiveStatus.rolePolicyDroppedFlagResponses++;
		break;
	case BotObjectiveLane::OwnBaseReturn:
		botObjectiveStatus.rolePolicyOwnBaseReturnSelections++;
		break;
	default:
		break;
	}
}

void BotObjectives_RecordMatchPolicySelection(const BotObjectiveMatchPolicy &policy) {
	botObjectiveStatus.lastMatchMode = static_cast<int>(policy.mode);
	botObjectiveStatus.lastMatchRole = static_cast<int>(policy.role);
	botObjectiveStatus.lastMatchLane = static_cast<int>(policy.lane);
	botObjectiveStatus.lastMatchPriority = policy.priority;
	botObjectiveStatus.lastMatchRoamPriority = policy.roamPriority;
	botObjectiveStatus.lastMatchCollectPriority = policy.collectPriority;
	botObjectiveStatus.lastMatchEngagePriority = policy.engagePriority;
	botObjectiveStatus.lastMatchObjectivePriority = policy.objectivePriority;
	botObjectiveStatus.lastMatchAttackPriority = policy.attackPriority;
	botObjectiveStatus.lastMatchDefendPriority = policy.defendPriority;
	botObjectiveStatus.lastMatchMidfieldPriority = policy.midfieldPriority;
	botObjectiveStatus.lastFriendlyFireScalePercent = policy.friendlyFireScalePercent;
	botObjectiveStatus.lastMatchReason = policy.reason;
	botObjectiveStatus.lastMatchLaneReason = policy.laneReason;

	if (!policy.valid) {
		botObjectiveStatus.matchPolicyNoSelection++;
		return;
	}

	botObjectiveStatus.matchPolicySelections++;
	if (policy.participatesInScoring) {
		botObjectiveStatus.matchPolicyScoringParticipation++;
	}
	if (policy.friendlyFireAvoidance) {
		botObjectiveStatus.matchPolicyFriendlyFireAvoidance++;
	}

	switch (policy.mode) {
	case BotObjectiveMatchMode::FreeForAll:
		botObjectiveStatus.matchPolicyFfaSelections++;
		break;
	case BotObjectiveMatchMode::TeamDeathmatch:
		botObjectiveStatus.matchPolicyTdmSelections++;
		break;
	case BotObjectiveMatchMode::CaptureTheFlag:
		botObjectiveStatus.matchPolicyCtfSelections++;
		break;
	case BotObjectiveMatchMode::Cooperative:
		botObjectiveStatus.matchPolicyCoopSelections++;
		break;
	default:
		break;
	}

	switch (policy.role) {
	case BotObjectiveRole::Attacker:
		botObjectiveStatus.matchPolicyAttackSelections++;
		break;
	case BotObjectiveRole::Defender:
		botObjectiveStatus.matchPolicyDefendSelections++;
		break;
	case BotObjectiveRole::Midfielder:
	case BotObjectiveRole::Support:
		botObjectiveStatus.matchPolicyMidfieldSelections++;
		break;
	default:
		break;
	}
}

void BotObjectives_RecordItemRolePolicySelection(const BotObjectiveItemRolePolicy &policy) {
	botObjectiveStatus.lastItemCategory = static_cast<int>(policy.category);
	botObjectiveStatus.lastItemRole = static_cast<int>(policy.itemRole);
	botObjectiveStatus.lastItemRolePriority = policy.priority;
	botObjectiveStatus.lastItemRoleReason = policy.reason;

	if (!policy.valid) {
		botObjectiveStatus.itemRolePolicyNoSelection++;
		return;
	}

	botObjectiveStatus.itemRolePolicySelections++;
	switch (policy.itemRole) {
	case BotObjectiveItemRole::SelfStack:
		botObjectiveStatus.itemRoleSelfStackSelections++;
		break;
	case BotObjectiveItemRole::WeaponControl:
		botObjectiveStatus.itemRoleWeaponControlSelections++;
		break;
	case BotObjectiveItemRole::PowerupControl:
		botObjectiveStatus.itemRolePowerupControlSelections++;
		break;
	case BotObjectiveItemRole::TeamResource:
		botObjectiveStatus.itemRoleTeamResourceSelections++;
		break;
	case BotObjectiveItemRole::DenyEnemy:
		botObjectiveStatus.itemRoleDenyEnemySelections++;
		break;
	case BotObjectiveItemRole::Objective:
		botObjectiveStatus.itemRoleObjectiveSelections++;
		break;
	default:
		break;
	}
}

void BotObjectives_RecordCoopPolicySelection(const BotObjectiveCoopPolicy &policy) {
	botObjectiveStatus.lastCoopIntent = static_cast<int>(policy.intent);
	botObjectiveStatus.lastCoopRole = static_cast<int>(policy.role);
	botObjectiveStatus.lastCoopLane = static_cast<int>(policy.lane);
	botObjectiveStatus.lastCoopPriority = policy.priority;
	botObjectiveStatus.lastCoopFollowPriority = policy.followPriority;
	botObjectiveStatus.lastCoopWaitPriority = policy.waitPriority;
	botObjectiveStatus.lastCoopResourcePriority = policy.resourcePriority;
	botObjectiveStatus.lastCoopLeaderClient = policy.leaderClient;
	botObjectiveStatus.lastCoopLeaderDistanceSquared = policy.leaderDistanceSquared;
	botObjectiveStatus.lastCoopReason = policy.reason;
	botObjectiveStatus.lastCoopLaneReason = policy.laneReason;

	if (!policy.valid) {
		botObjectiveStatus.coopPolicyNoSelection++;
		return;
	}

	botObjectiveStatus.coopPolicySelections++;
	if (policy.followLeader) {
		botObjectiveStatus.coopPolicyFollowSelections++;
	}
	if (policy.waitForLeader) {
		botObjectiveStatus.coopPolicyWaitSelections++;
	}
	if (policy.regroup) {
		botObjectiveStatus.coopPolicyRegroupSelections++;
	}
	if (policy.mayLead) {
		botObjectiveStatus.coopPolicyLeadSelections++;
	}
	if (policy.intent == BotObjectiveCoopIntent::SupportCombat) {
		botObjectiveStatus.coopPolicySupportSelections++;
	}
	if (policy.shareResources || policy.reserveResources) {
		botObjectiveStatus.coopPolicyResourceShareSelections++;
	}
}

void BotObjectives_RecordResourcePolicySelection(const BotObjectiveResourcePolicy &policy) {
	botObjectiveStatus.lastResourceIntent = static_cast<int>(policy.intent);
	botObjectiveStatus.lastResourceCategory = static_cast<int>(policy.category);
	botObjectiveStatus.lastResourcePriority = policy.priority;
	botObjectiveStatus.lastResourceShouldShare = policy.shouldShare ? 1 : 0;
	botObjectiveStatus.lastResourceShouldReserve = policy.shouldReserve ? 1 : 0;
	botObjectiveStatus.lastResourceDenyEnemy = policy.denyEnemyPickup ? 1 : 0;
	botObjectiveStatus.lastResourceReason = policy.reason;

	if (!policy.valid) {
		botObjectiveStatus.resourcePolicyNoSelection++;
		return;
	}

	botObjectiveStatus.resourcePolicySelections++;
	switch (policy.intent) {
	case BotObjectiveResourceIntent::SelfPickup:
		botObjectiveStatus.resourcePolicySelfPickupSelections++;
		break;
	case BotObjectiveResourceIntent::ShareTeam:
		botObjectiveStatus.resourcePolicyShareTeamSelections++;
		break;
	case BotObjectiveResourceIntent::ReserveForTeammate:
		botObjectiveStatus.resourcePolicyReserveSelections++;
		break;
	case BotObjectiveResourceIntent::DenyEnemy:
		botObjectiveStatus.resourcePolicyDenyEnemySelections++;
		break;
	case BotObjectiveResourceIntent::Objective:
		botObjectiveStatus.resourcePolicyObjectiveSelections++;
		break;
	default:
		break;
	}
}

void BotObjectives_RecordFriendlyFirePolicySelection(const BotObjectiveFriendlyFirePolicy &policy) {
	if (!policy.valid) {
		return;
	}

	if (policy.shouldAvoidFire) {
		botObjectiveStatus.friendlyFirePolicyAvoidance++;
	}
	if (!policy.targetAllowed) {
		botObjectiveStatus.friendlyFirePolicyTargetBlocks++;
	}

	botObjectiveStatus.lastFriendlyFireAvoidance = policy.shouldAvoidFire ? 1 : 0;
	botObjectiveStatus.lastFriendlyFireTargetAllowed = policy.targetAllowed ? 1 : 0;
	botObjectiveStatus.lastFriendlyFireScalePercent = policy.friendlyFireScalePercent;
	botObjectiveStatus.lastFriendlyFireReason = policy.reason;
}

void BotObjectives_RecordLast(const BotObjectiveAssignment &assignment) {
	botObjectiveStatus.lastObjectiveType = static_cast<int>(assignment.type);
	botObjectiveStatus.lastObjectiveRole = static_cast<int>(assignment.role);
	botObjectiveStatus.lastObjectiveLane = static_cast<int>(assignment.lane);
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
	botObjectiveStatus.lastLanePriority = assignment.lanePriority;
	botObjectiveStatus.lastAttackPriority = assignment.attackPriority;
	botObjectiveStatus.lastDefendPriority = assignment.defendPriority;
	botObjectiveStatus.lastReturnPriority = assignment.returnPriority;
	botObjectiveStatus.lastSupportPriority = assignment.supportPriority;
	botObjectiveStatus.lastAttackLanePriority = assignment.attackLanePriority;
	botObjectiveStatus.lastDefenseLanePriority = assignment.defenseLanePriority;
	botObjectiveStatus.lastMidfieldLanePriority = assignment.midfieldLanePriority;
	botObjectiveStatus.lastCarrierSupportPriority = assignment.carrierSupportPriority;
	botObjectiveStatus.lastDroppedFlagResponsePriority = assignment.droppedFlagResponsePriority;
	botObjectiveStatus.lastOwnBaseReturnPriority = assignment.ownBaseReturnPriority;
	botObjectiveStatus.lastCarrierClient = assignment.carrierClient;
	botObjectiveStatus.lastOriginX = static_cast<int>(assignment.origin[0]);
	botObjectiveStatus.lastOriginY = static_cast<int>(assignment.origin[1]);
	botObjectiveStatus.lastOriginZ = static_cast<int>(assignment.origin[2]);
	botObjectiveStatus.lastReason = assignment.reason;
	botObjectiveStatus.lastLaneReason = assignment.laneReason;
}

void BotObjectives_RecordLastTarget(
	const BotObjectiveTarget &target,
	int clientIndex,
	int team,
	BotObjectiveRole role,
	int priority) {
	const BotObjectiveLane lane = BotObjectives_DefaultLaneForTarget(target);
	botObjectiveStatus.lastObjectiveType = static_cast<int>(target.type);
	botObjectiveStatus.lastObjectiveRole = static_cast<int>(role);
	botObjectiveStatus.lastObjectiveLane = static_cast<int>(lane);
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
	botObjectiveStatus.lastLanePriority = BotObjectives_LanePriorityForTarget(lane, target);
	botObjectiveStatus.lastAttackPriority = 0;
	botObjectiveStatus.lastDefendPriority = 0;
	botObjectiveStatus.lastReturnPriority = 0;
	botObjectiveStatus.lastSupportPriority = 0;
	botObjectiveStatus.lastAttackLanePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::Attack, target);
	botObjectiveStatus.lastDefenseLanePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::Defense, target);
	botObjectiveStatus.lastMidfieldLanePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::Midfield, target);
	botObjectiveStatus.lastCarrierSupportPriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::CarrierSupport, target);
	botObjectiveStatus.lastDroppedFlagResponsePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::DroppedFlagResponse, target);
	botObjectiveStatus.lastOwnBaseReturnPriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::OwnBaseReturn, target);
	botObjectiveStatus.lastCarrierClient = target.carrierClient;
	botObjectiveStatus.lastOriginX = static_cast<int>(target.origin[0]);
	botObjectiveStatus.lastOriginY = static_cast<int>(target.origin[1]);
	botObjectiveStatus.lastOriginZ = static_cast<int>(target.origin[2]);
	botObjectiveStatus.lastReason = BotObjectives_BuildRoleLaneCandidate(role, lane, target).reason;
	botObjectiveStatus.lastLaneReason = botObjectiveStatus.lastReason;
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
	botObjectiveStatus.lastObjectiveLane = static_cast<int>(BotObjectiveLane::None);
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
	botObjectiveStatus.lastLanePriority = 0;
	botObjectiveStatus.lastAttackPriority = 0;
	botObjectiveStatus.lastDefendPriority = 0;
	botObjectiveStatus.lastReturnPriority = 0;
	botObjectiveStatus.lastSupportPriority = 0;
	botObjectiveStatus.lastAttackLanePriority = 0;
	botObjectiveStatus.lastDefenseLanePriority = 0;
	botObjectiveStatus.lastMidfieldLanePriority = 0;
	botObjectiveStatus.lastCarrierSupportPriority = 0;
	botObjectiveStatus.lastDroppedFlagResponsePriority = 0;
	botObjectiveStatus.lastOwnBaseReturnPriority = 0;
	botObjectiveStatus.lastCarrierClient = -1;
	botObjectiveStatus.lastOriginX = originX;
	botObjectiveStatus.lastOriginY = originY;
	botObjectiveStatus.lastOriginZ = originZ;
	botObjectiveStatus.lastReason = "event";
	botObjectiveStatus.lastLaneReason = "none";
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
	case BotObjectiveRole::Midfielder:
		botObjectiveStatus.roleMidfielder++;
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

BotObjectiveMatchMode BotObjectives_MatchModeForGameType(int gametype) {
	switch (gametype) {
	case static_cast<int>(GameType::FreeForAll):
		return BotObjectiveMatchMode::FreeForAll;
	case static_cast<int>(GameType::TeamDeathmatch):
		return BotObjectiveMatchMode::TeamDeathmatch;
	case static_cast<int>(GameType::CaptureTheFlag):
	case static_cast<int>(GameType::OneFlag):
	case static_cast<int>(GameType::Harvester):
	case static_cast<int>(GameType::Overload):
	case static_cast<int>(GameType::CaptureStrike):
		return BotObjectiveMatchMode::CaptureTheFlag;
	default:
		return BotObjectiveMatchMode::None;
	}
}

BotObjectiveRole BotObjectives_DefaultMatchRole(const BotObjectiveMatchContext &context) {
	if (BotObjectives_MatchRoleCompatible(context.mode, context.requestedRole)) {
		return context.requestedRole;
	}
	return BotObjectives_DeterministicShapeRole(context);
}

BotObjectiveLane BotObjectives_DefaultLaneForMatchRole(BotObjectiveMatchMode mode, BotObjectiveRole role) {
	if (mode == BotObjectiveMatchMode::FreeForAll) {
		return role == BotObjectiveRole::None ? BotObjectiveLane::None : BotObjectiveLane::Midfield;
	}

	switch (role) {
	case BotObjectiveRole::Attacker:
		return BotObjectiveLane::Attack;
	case BotObjectiveRole::Defender:
		return BotObjectiveLane::Defense;
	case BotObjectiveRole::Support:
	case BotObjectiveRole::Midfielder:
		return BotObjectiveLane::Midfield;
	default:
		return BotObjectiveLane::None;
	}
}

int BotObjectives_MatchRolePriority(BotObjectiveMatchMode mode, BotObjectiveRole role) {
	const int basePriority = BotObjectives_MatchBasePriority(mode);
	if (basePriority <= 0 || !BotObjectives_MatchRoleCompatible(mode, role)) {
		return 0;
	}

	if (mode == BotObjectiveMatchMode::FreeForAll) {
		return role == BotObjectiveRole::Attacker
			? basePriority + BOT_OBJECTIVE_ATTACK_ROLE_BONUS
			: basePriority;
	}

	switch (role) {
	case BotObjectiveRole::Attacker:
		return basePriority + BOT_OBJECTIVE_ATTACK_ROLE_BONUS;
	case BotObjectiveRole::Defender:
		return basePriority + BOT_OBJECTIVE_DEFENSE_ROLE_BONUS;
	case BotObjectiveRole::Support:
	case BotObjectiveRole::Midfielder:
		return basePriority + BOT_OBJECTIVE_MIDFIELD_POLICY_BONUS;
	default:
		return 0;
	}
}

static bool BotObjectives_CoopRoleCompatible(BotObjectiveRole role) {
	return role == BotObjectiveRole::Attacker ||
		role == BotObjectiveRole::Defender ||
		role == BotObjectiveRole::Support ||
		role == BotObjectiveRole::Midfielder;
}

BotObjectiveCoopIntent BotObjectives_DefaultCoopIntent(const BotObjectiveCoopContext &context) {
	if (!context.coopMode || !context.alive) {
		return BotObjectiveCoopIntent::None;
	}
	if (context.progressWaitRequested) {
		return BotObjectiveCoopIntent::WaitForLeader;
	}
	if (!context.leaderValid) {
		return BotObjectiveCoopIntent::LeadAdvance;
	}
	if (context.leaderDistanceSquared >= BOT_OBJECTIVE_COOP_REGROUP_DISTANCE_SQUARED) {
		return BotObjectiveCoopIntent::Regroup;
	}
	if (context.separatedFromLeader) {
		return BotObjectiveCoopIntent::FollowLeader;
	}
	if (context.closeToLeader) {
		return BotObjectiveCoopIntent::SupportCombat;
	}
	return BotObjectiveCoopIntent::FollowLeader;
}

BotObjectiveRole BotObjectives_DefaultCoopRole(BotObjectiveCoopIntent intent) {
	switch (intent) {
	case BotObjectiveCoopIntent::LeadAdvance:
		return BotObjectiveRole::Attacker;
	case BotObjectiveCoopIntent::WaitForLeader:
	case BotObjectiveCoopIntent::Regroup:
	case BotObjectiveCoopIntent::FollowLeader:
	case BotObjectiveCoopIntent::SupportCombat:
		return BotObjectiveRole::Support;
	default:
		return BotObjectiveRole::None;
	}
}

BotObjectiveLane BotObjectives_DefaultCoopLane(BotObjectiveCoopIntent intent) {
	switch (intent) {
	case BotObjectiveCoopIntent::LeadAdvance:
		return BotObjectiveLane::Attack;
	case BotObjectiveCoopIntent::WaitForLeader:
		return BotObjectiveLane::Defense;
	case BotObjectiveCoopIntent::Regroup:
	case BotObjectiveCoopIntent::FollowLeader:
	case BotObjectiveCoopIntent::SupportCombat:
		return BotObjectiveLane::CarrierSupport;
	default:
		return BotObjectiveLane::None;
	}
}

int BotObjectives_CoopIntentPriority(BotObjectiveCoopIntent intent) {
	switch (intent) {
	case BotObjectiveCoopIntent::FollowLeader:
		return BOT_OBJECTIVE_COOP_FOLLOW_PRIORITY;
	case BotObjectiveCoopIntent::WaitForLeader:
		return BOT_OBJECTIVE_COOP_WAIT_PRIORITY;
	case BotObjectiveCoopIntent::Regroup:
		return BOT_OBJECTIVE_COOP_REGROUP_PRIORITY;
	case BotObjectiveCoopIntent::LeadAdvance:
		return BOT_OBJECTIVE_COOP_LEAD_PRIORITY;
	case BotObjectiveCoopIntent::SupportCombat:
		return BOT_OBJECTIVE_COOP_SUPPORT_PRIORITY;
	default:
		return 0;
	}
}

static const char *BotObjectives_CoopPolicyReason(BotObjectiveCoopIntent intent, bool leaderIsHuman) {
	switch (intent) {
	case BotObjectiveCoopIntent::FollowLeader:
		return leaderIsHuman ? "coop_follow_human_leader" : "coop_follow_teammate";
	case BotObjectiveCoopIntent::WaitForLeader:
		return leaderIsHuman ? "coop_wait_for_human_leader" : "coop_wait_for_teammate";
	case BotObjectiveCoopIntent::Regroup:
		return leaderIsHuman ? "coop_regroup_to_human_leader" : "coop_regroup_to_teammate";
	case BotObjectiveCoopIntent::LeadAdvance:
		return "coop_no_leader_cautious_advance";
	case BotObjectiveCoopIntent::SupportCombat:
		return leaderIsHuman ? "coop_support_human_combat" : "coop_support_teammate_combat";
	default:
		return "none";
	}
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
	if (target.type == BotObjectiveType::OwnFlagReturn &&
		target.source == BotObjectiveTargetSource::WorldFlagEntity) {
		return BotObjectiveRole::Defender;
	}
	if (target.type == BotObjectiveType::BaseDefense &&
		target.source == BotObjectiveTargetSource::EnemyTeamAnchor) {
		return BotObjectiveRole::Attacker;
	}

	return BotObjectives_DefaultRoleForType(target.type);
}

BotObjectiveLane BotObjectives_DefaultLaneForTarget(const BotObjectiveTarget &target) {
	const BotObjectiveRole defaultRole = BotObjectives_DefaultRoleForTarget(target);
	const BotObjectiveRoleLaneCandidate candidate =
		BotObjectives_BestCandidateForRole(defaultRole, target);
	return candidate.priority > 0 ? candidate.lane : BotObjectiveLane::None;
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
	return BotObjectives_BestCandidateForRole(role, target).priority;
}

int BotObjectives_LanePriorityForTarget(BotObjectiveLane lane, const BotObjectiveTarget &target) {
	return BotObjectives_BestLanePriority(lane, target);
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
	policy.attackLanePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::Attack, context.target);
	policy.defenseLanePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::Defense, context.target);
	policy.midfieldLanePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::Midfield, context.target);
	policy.carrierSupportPriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::CarrierSupport, context.target);
	policy.droppedFlagResponsePriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::DroppedFlagResponse, context.target);
	policy.ownBaseReturnPriority = BotObjectives_LanePriorityForTarget(BotObjectiveLane::OwnBaseReturn, context.target);

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

BotObjectiveMatchContext BotObjectives_BuildMatchContext(const gentity_t *bot, BotObjectiveRole requestedRole) {
	BotObjectiveMatchContext context{};
	context.gametype = static_cast<int>(Game::GetCurrentType());
	context.coopMode = BotObjectives_IsCoopMode();
	context.mode = context.coopMode
		? BotObjectiveMatchMode::Cooperative
		: BotObjectives_MatchModeForGameType(context.gametype);
	context.teamMode = BotObjectives_IsTeamMatchMode(context.mode) && Teams();
	context.ctfMode = context.mode == BotObjectiveMatchMode::CaptureTheFlag;
	context.scoringEnabled = !context.coopMode &&
		deathmatch != nullptr && deathmatch->integer != 0 &&
		context.mode != BotObjectiveMatchMode::None;
	context.requestedRole = requestedRole;
	context.friendlyFireScalePercent = BotObjectives_FriendlyFireScalePercent();
	context.friendlyFireDamageEnabled = context.teamMode &&
		context.friendlyFireScalePercent > 0;

	if (!BotObjectives_IsBotEntity(bot)) {
		return context;
	}

	context.valid = true;
	context.alive = BotObjectives_IsAliveBot(bot);
	context.clientIndex = BotObjectives_ClientIndexForEntity(bot);
	context.team = BotObjectives_BotTeam(bot);
	BotObjectives_CountMatchPlayers(
		context.clientIndex,
		context.team,
		context.mode,
		&context.teamSize,
		&context.enemyCount);

	if (BotObjectives_IsPrimaryTeam(context.team)) {
		context.teamScore = level.teamScores[context.team];
		context.enemyTeamScore = BotObjectives_EnemyTeamScoreForTeam(context.team);
	}

	return context;
}

BotObjectiveMatchPolicy BotObjectives_EvaluateMatchPolicy(const BotObjectiveMatchContext &context) {
	botObjectiveStatus.matchPolicyEvaluations++;

	BotObjectiveMatchPolicy policy{};
	policy.mode = context.mode;
	policy.clientIndex = context.clientIndex;
	policy.team = context.team;
	policy.hasRequestedRole = context.requestedRole != BotObjectiveRole::None;
	policy.friendlyFireScalePercent = context.friendlyFireScalePercent;
	policy.friendlyFireAvoidance = context.teamMode && context.friendlyFireDamageEnabled;
	policy.requiresTeamTargetFilter = context.teamMode;
	policy.attackPriority = BotObjectives_MatchRolePriority(context.mode, BotObjectiveRole::Attacker);
	policy.defendPriority = BotObjectives_MatchRolePriority(context.mode, BotObjectiveRole::Defender);
	policy.midfieldPriority = BotObjectives_MatchRolePriority(context.mode, BotObjectiveRole::Midfielder);

	if (!context.valid ||
		!context.alive ||
		!context.scoringEnabled ||
		context.mode == BotObjectiveMatchMode::None) {
		BotObjectives_RecordMatchPolicySelection(policy);
		return policy;
	}

	bool requestedNeedsFallback = false;
	BotObjectiveRole selectedRole = BotObjectives_DefaultMatchRole(context);
	if (policy.hasRequestedRole) {
		if (BotObjectives_MatchRoleCompatible(context.mode, context.requestedRole)) {
			selectedRole = context.requestedRole;
			policy.requestedRoleHonored = true;
		} else {
			requestedNeedsFallback = true;
		}
	}

	const int priority = BotObjectives_MatchRolePriority(context.mode, selectedRole);
	const BotObjectiveLane lane = BotObjectives_DefaultLaneForMatchRole(context.mode, selectedRole);
	if (priority <= 0 || lane == BotObjectiveLane::None) {
		BotObjectives_RecordMatchPolicySelection(policy);
		return policy;
	}

	policy.valid = true;
	policy.participatesInScoring = true;
	policy.fallbackRole = requestedNeedsFallback;
	policy.role = selectedRole;
	policy.lane = lane;
	policy.priority = priority;
	policy.reason = BotObjectives_MatchPolicyReason(context.mode, selectedRole);
	policy.laneReason = policy.reason;
	policy.wantsEngage = true;
	policy.wantsCollect = true;
	policy.wantsRoam = context.mode == BotObjectiveMatchMode::FreeForAll ||
		BotObjectives_IsMidfieldPolicyRole(selectedRole);
	policy.wantsObjective = context.mode == BotObjectiveMatchMode::CaptureTheFlag;
	policy.avoidSpawnCamping = context.mode == BotObjectiveMatchMode::FreeForAll;
	policy.preferMajorItems = selectedRole == BotObjectiveRole::Attacker ||
		BotObjectives_IsMidfieldPolicyRole(selectedRole);
	policy.shareTeamResources = context.teamMode &&
		(selectedRole == BotObjectiveRole::Defender ||
			BotObjectives_IsMidfieldPolicyRole(selectedRole));
	policy.engagePriority = priority;
	policy.collectPriority = priority - 80;
	policy.roamPriority = policy.wantsRoam ? priority - 120 : priority - 220;
	policy.objectivePriority = policy.wantsObjective ? priority + 80 : 0;

	BotObjectives_RecordMatchPolicySelection(policy);
	return policy;
}

BotObjectiveCoopContext BotObjectives_BuildCoopContext(
	const gentity_t *bot,
	const gentity_t *preferredLeader,
	bool progressWaitRequested,
	BotObjectiveRole requestedRole) {
	BotObjectiveCoopContext context{};
	context.coopMode = BotObjectives_IsCoopMode();
	context.progressWaitRequested = progressWaitRequested;
	context.requestedRole = requestedRole;

	if (!BotObjectives_IsBotEntity(bot)) {
		return context;
	}

	context.valid = true;
	context.alive = BotObjectives_IsAliveBot(bot);
	context.clientIndex = BotObjectives_ClientIndexForEntity(bot);
	BotObjectives_CountCoopPlayers(
		&context.teamSize,
		&context.humanCount,
		&context.botCount);

	const gentity_t *leader = BotObjectives_FindCoopLeader(bot, preferredLeader);
	if (leader == nullptr) {
		return context;
	}

	context.leaderValid = true;
	context.leaderAlive = BotObjectives_IsAlivePlayer(leader);
	context.leaderIsHuman = BotObjectives_IsHumanPlayer(leader);
	context.leaderClient = BotObjectives_ClientIndexForEntity(leader);
	context.leaderDistanceSquared = BotObjectives_DistanceSquared(bot, leader);
	context.separatedFromLeader =
		context.leaderDistanceSquared >= BOT_OBJECTIVE_COOP_FOLLOW_DISTANCE_SQUARED;
	context.closeToLeader =
		context.leaderDistanceSquared <= BOT_OBJECTIVE_COOP_CLOSE_DISTANCE_SQUARED;
	return context;
}

BotObjectiveCoopPolicy BotObjectives_EvaluateCoopPolicy(const BotObjectiveCoopContext &context) {
	botObjectiveStatus.coopPolicyEvaluations++;

	BotObjectiveCoopPolicy policy{};
	policy.coopMode = context.coopMode;
	policy.hasLeader = context.leaderValid;
	policy.leaderIsHuman = context.leaderIsHuman;
	policy.hasRequestedRole = context.requestedRole != BotObjectiveRole::None;
	policy.clientIndex = context.clientIndex;
	policy.leaderClient = context.leaderClient;
	policy.teamSize = context.teamSize;
	policy.humanCount = context.humanCount;
	policy.botCount = context.botCount;
	policy.leaderDistanceSquared = context.leaderDistanceSquared;
	policy.followPriority = BOT_OBJECTIVE_COOP_FOLLOW_PRIORITY;
	policy.waitPriority = BOT_OBJECTIVE_COOP_WAIT_PRIORITY;
	policy.resourcePriority = BOT_OBJECTIVE_COOP_RESOURCE_PRIORITY;

	if (!context.valid || !context.alive || !context.coopMode) {
		BotObjectives_RecordCoopPolicySelection(policy);
		return policy;
	}

	const BotObjectiveCoopIntent intent = BotObjectives_DefaultCoopIntent(context);
	if (intent == BotObjectiveCoopIntent::None) {
		BotObjectives_RecordCoopPolicySelection(policy);
		return policy;
	}

	bool requestedNeedsFallback = false;
	BotObjectiveRole role = BotObjectives_DefaultCoopRole(intent);
	if (policy.hasRequestedRole) {
		if (BotObjectives_CoopRoleCompatible(context.requestedRole)) {
			role = context.requestedRole;
			policy.requestedRoleHonored = true;
		} else {
			requestedNeedsFallback = true;
		}
	}

	const int priority = BotObjectives_CoopIntentPriority(intent);
	const BotObjectiveLane lane = BotObjectives_DefaultCoopLane(intent);
	if (priority <= 0 || role == BotObjectiveRole::None || lane == BotObjectiveLane::None) {
		BotObjectives_RecordCoopPolicySelection(policy);
		return policy;
	}

	policy.valid = true;
	policy.fallbackRole = requestedNeedsFallback;
	policy.intent = intent;
	policy.role = role;
	policy.lane = lane;
	policy.priority = priority;
	policy.reason = BotObjectives_CoopPolicyReason(intent, context.leaderIsHuman);
	policy.laneReason = policy.reason;
	policy.followLeader =
		intent == BotObjectiveCoopIntent::FollowLeader ||
		intent == BotObjectiveCoopIntent::Regroup ||
		intent == BotObjectiveCoopIntent::SupportCombat;
	policy.waitForLeader = intent == BotObjectiveCoopIntent::WaitForLeader;
	policy.regroup = intent == BotObjectiveCoopIntent::Regroup;
	policy.mayLead = intent == BotObjectiveCoopIntent::LeadAdvance;
	policy.shareResources = context.teamSize > 1 &&
		(context.humanCount > 0 || context.leaderValid);
	policy.reserveResources = policy.shareResources &&
		(policy.waitForLeader || policy.followLeader || context.leaderIsHuman);

	BotObjectives_RecordCoopPolicySelection(policy);
	return policy;
}

BotObjectiveResourceContext BotObjectives_BuildResourceContext(
	const BotObjectiveMatchPolicy &matchPolicy,
	const BotObjectiveCoopPolicy &coopPolicy,
	BotObjectiveItemCategory category,
	int candidatePriority,
	bool selfNeedsItem,
	bool teammateNeedsItem,
	bool enemyContested) {
	BotObjectiveResourceContext context{};
	context.valid = matchPolicy.valid || coopPolicy.valid;
	context.teamMode = matchPolicy.requiresTeamTargetFilter;
	context.coopMode = coopPolicy.valid && coopPolicy.coopMode;
	context.selfNeedsItem = selfNeedsItem;
	context.teammateNeedsItem = teammateNeedsItem;
	context.enemyContested = enemyContested;
	context.objectiveRelevant =
		category == BotObjectiveItemCategory::CtfObjective ||
		matchPolicy.wantsObjective;
	context.preferRoleResource =
		matchPolicy.preferMajorItems ||
		coopPolicy.mayLead ||
		coopPolicy.reserveResources;
	context.mode = context.coopMode ? BotObjectiveMatchMode::Cooperative : matchPolicy.mode;
	context.role = coopPolicy.valid ? coopPolicy.role : matchPolicy.role;
	context.lane = coopPolicy.valid ? coopPolicy.lane : matchPolicy.lane;
	context.category = category;
	context.candidatePriority = candidatePriority;
	return context;
}

BotObjectiveResourcePolicy BotObjectives_EvaluateResourcePolicy(
	const BotObjectiveResourceContext &context) {
	botObjectiveStatus.resourcePolicyEvaluations++;

	BotObjectiveResourcePolicy policy{};
	policy.mode = context.mode;
	policy.role = context.role;
	policy.lane = context.lane;
	policy.category = context.category;
	policy.priority = context.candidatePriority > 0
		? context.candidatePriority
		: BOT_OBJECTIVE_ITEM_ROLE_BASE_PRIORITY;

	if (!context.valid || context.category == BotObjectiveItemCategory::None) {
		BotObjectives_RecordResourcePolicySelection(policy);
		return policy;
	}

	policy.valid = true;
	policy.mayPickup = true;
	switch (context.category) {
	case BotObjectiveItemCategory::CtfObjective:
		policy.intent = BotObjectiveResourceIntent::Objective;
		policy.objectiveResource = true;
		policy.shouldReserve = context.objectiveRelevant;
		policy.priority += BOT_OBJECTIVE_RESOURCE_OBJECTIVE_BOOST;
		policy.reason = "objective_resource";
		break;
	case BotObjectiveItemCategory::Powerup:
	case BotObjectiveItemCategory::Tech:
		if ((context.teamMode || context.coopMode) && context.enemyContested) {
			policy.intent = BotObjectiveResourceIntent::DenyEnemy;
			policy.denyEnemyPickup = true;
			policy.priority += BOT_OBJECTIVE_RESOURCE_DENY_BOOST;
			policy.reason = "deny_enemy_major_resource";
		} else if ((context.teamMode || context.coopMode) && context.preferRoleResource) {
			policy.intent = BotObjectiveResourceIntent::ShareTeam;
			policy.shouldShare = true;
			policy.priority += BOT_OBJECTIVE_RESOURCE_SHARE_BOOST;
			policy.reason = "share_major_team_resource";
		} else {
			policy.intent = BotObjectiveResourceIntent::SelfPickup;
			policy.priority += BOT_OBJECTIVE_RESOURCE_SELF_BOOST;
			policy.reason = "self_major_resource";
		}
		break;
	case BotObjectiveItemCategory::Health:
	case BotObjectiveItemCategory::Armor:
	case BotObjectiveItemCategory::Ammo:
		if ((context.teamMode || context.coopMode) &&
			context.teammateNeedsItem &&
			!context.selfNeedsItem) {
			policy.intent = BotObjectiveResourceIntent::ReserveForTeammate;
			policy.shouldReserve = true;
			policy.shouldShare = true;
			policy.mayPickup = false;
			policy.priority += BOT_OBJECTIVE_RESOURCE_RESERVE_BOOST;
			policy.reason = "reserve_sustain_for_teammate";
		} else if ((context.teamMode || context.coopMode) && context.teammateNeedsItem) {
			policy.intent = BotObjectiveResourceIntent::ShareTeam;
			policy.shouldShare = true;
			policy.priority += BOT_OBJECTIVE_RESOURCE_SHARE_BOOST;
			policy.reason = "share_sustain_resource";
		} else {
			policy.intent = BotObjectiveResourceIntent::SelfPickup;
			policy.priority += BOT_OBJECTIVE_RESOURCE_SELF_BOOST;
			policy.reason = "self_sustain_resource";
		}
		break;
	case BotObjectiveItemCategory::Weapon:
	case BotObjectiveItemCategory::Utility:
		if ((context.teamMode || context.coopMode) &&
			context.teammateNeedsItem &&
			!context.selfNeedsItem) {
			policy.intent = BotObjectiveResourceIntent::ReserveForTeammate;
			policy.shouldReserve = true;
			policy.shouldShare = true;
			policy.mayPickup = false;
			policy.priority += BOT_OBJECTIVE_RESOURCE_RESERVE_BOOST;
			policy.reason = "reserve_equipment_for_teammate";
		} else if (context.teamMode && context.enemyContested) {
			policy.intent = BotObjectiveResourceIntent::DenyEnemy;
			policy.denyEnemyPickup = true;
			policy.priority += BOT_OBJECTIVE_RESOURCE_DENY_BOOST;
			policy.reason = "deny_enemy_equipment";
		} else {
			policy.intent = BotObjectiveResourceIntent::SelfPickup;
			policy.priority += BOT_OBJECTIVE_RESOURCE_SELF_BOOST;
			policy.reason = "self_equipment_resource";
		}
		break;
	default:
		policy.valid = false;
		break;
	}

	BotObjectives_RecordResourcePolicySelection(policy);
	return policy;
}

BotObjectiveItemCategory BotObjectives_ItemCategoryForItem(const Item *item) {
	if (item == nullptr || item->id == IT_NULL) {
		return BotObjectiveItemCategory::None;
	}
	if (BotObjectives_IsFlagItem(item->id)) {
		return BotObjectiveItemCategory::CtfObjective;
	}
	if (item->flags & IF_TECH) {
		return BotObjectiveItemCategory::Tech;
	}
	if (item->flags & (IF_POWERUP | IF_TIMED | IF_SPHERE)) {
		return BotObjectiveItemCategory::Powerup;
	}
	if (item->flags & IF_HEALTH) {
		return BotObjectiveItemCategory::Health;
	}
	if (item->flags & (IF_ARMOR | IF_POWER_ARMOR)) {
		return BotObjectiveItemCategory::Armor;
	}
	if ((item->flags & IF_WEAPON) && !(item->flags & IF_AMMO)) {
		return BotObjectiveItemCategory::Weapon;
	}
	if (item->flags & IF_AMMO) {
		return BotObjectiveItemCategory::Ammo;
	}
	return BotObjectiveItemCategory::Utility;
}

BotObjectiveItemRolePolicy BotObjectives_EvaluateItemRolePolicy(
	const BotObjectiveMatchPolicy &matchPolicy,
	BotObjectiveItemCategory category,
	int candidatePriority) {
	botObjectiveStatus.itemRolePolicyEvaluations++;

	BotObjectiveItemRolePolicy policy{};
	policy.mode = matchPolicy.mode;
	policy.role = matchPolicy.role;
	policy.lane = matchPolicy.lane;
	policy.category = category;
	policy.priority = candidatePriority > 0
		? candidatePriority
		: BOT_OBJECTIVE_ITEM_ROLE_BASE_PRIORITY;

	if (!matchPolicy.valid || category == BotObjectiveItemCategory::None) {
		BotObjectives_RecordItemRolePolicySelection(policy);
		return policy;
	}

	policy.valid = true;
	switch (category) {
	case BotObjectiveItemCategory::CtfObjective:
		policy.itemRole = BotObjectiveItemRole::Objective;
		policy.reserveForRole = matchPolicy.mode == BotObjectiveMatchMode::CaptureTheFlag;
		policy.priority += BOT_OBJECTIVE_ITEM_ROLE_OBJECTIVE_BOOST;
		policy.reason = "objective_item_role";
		break;
	case BotObjectiveItemCategory::Powerup:
	case BotObjectiveItemCategory::Tech:
		policy.itemRole = BotObjectiveItemRole::PowerupControl;
		policy.denyEnemyPickup = matchPolicy.requiresTeamTargetFilter;
		policy.reserveForRole = matchPolicy.preferMajorItems;
		policy.priority += BOT_OBJECTIVE_ITEM_ROLE_POWERUP_BOOST;
		policy.reason = matchPolicy.preferMajorItems
			? "major_item_control"
			: "team_powerup_control";
		break;
	case BotObjectiveItemCategory::Weapon:
	case BotObjectiveItemCategory::Ammo:
		policy.itemRole = matchPolicy.role == BotObjectiveRole::Defender
			? BotObjectiveItemRole::TeamResource
			: BotObjectiveItemRole::WeaponControl;
		policy.denyEnemyPickup = matchPolicy.requiresTeamTargetFilter &&
			policy.itemRole == BotObjectiveItemRole::WeaponControl;
		policy.shareWithTeam = policy.itemRole == BotObjectiveItemRole::TeamResource;
		policy.priority += BOT_OBJECTIVE_ITEM_ROLE_WEAPON_BOOST;
		policy.reason = policy.shareWithTeam
			? "defense_weapon_resource"
			: "weapon_control";
		break;
	case BotObjectiveItemCategory::Health:
	case BotObjectiveItemCategory::Armor:
		policy.itemRole = matchPolicy.shareTeamResources
			? BotObjectiveItemRole::TeamResource
			: BotObjectiveItemRole::SelfStack;
		policy.shareWithTeam = policy.itemRole == BotObjectiveItemRole::TeamResource;
		policy.priority += policy.shareWithTeam
			? BOT_OBJECTIVE_ITEM_ROLE_TEAM_RESOURCE_BOOST
			: BOT_OBJECTIVE_ITEM_ROLE_HEALTH_ARMOR_BOOST;
		policy.reason = policy.shareWithTeam
			? "team_sustain_resource"
			: "self_sustain_stack";
		break;
	case BotObjectiveItemCategory::Utility:
		policy.itemRole = matchPolicy.requiresTeamTargetFilter
			? BotObjectiveItemRole::TeamResource
			: BotObjectiveItemRole::SelfStack;
		policy.shareWithTeam = policy.itemRole == BotObjectiveItemRole::TeamResource;
		policy.priority += BOT_OBJECTIVE_ITEM_ROLE_HEALTH_ARMOR_BOOST;
		policy.reason = policy.shareWithTeam
			? "team_utility_resource"
			: "self_utility";
		break;
	default:
		policy.valid = false;
		break;
	}

	BotObjectives_RecordItemRolePolicySelection(policy);
	return policy;
}

BotObjectiveFriendlyFireContext BotObjectives_BuildFriendlyFireContext(
	const gentity_t *shooter,
	const gentity_t *target,
	bool friendlyInLineOfFire) {
	BotObjectiveFriendlyFireContext context{};
	context.teamMode = Teams();
	context.friendlyInLineOfFire = friendlyInLineOfFire;
	context.friendlyFireScalePercent = BotObjectives_FriendlyFireScalePercent();
	context.friendlyFireDamageEnabled = context.teamMode &&
		context.friendlyFireScalePercent > 0;

	if (shooter == nullptr || shooter->client == nullptr) {
		return context;
	}

	context.valid = true;
	context.shooterClient = BotObjectives_ClientIndexForEntity(shooter);
	context.shooterTeam = static_cast<int>(shooter->client->sess.team);

	if (target == nullptr || target->client == nullptr) {
		return context;
	}

	context.targetClient = BotObjectives_ClientIndexForEntity(target);
	context.targetTeam = static_cast<int>(target->client->sess.team);
	context.targetIsSelf = shooter == target;
	context.targetIsTeammate = !context.targetIsSelf &&
		OnSameTeam(const_cast<gentity_t *>(shooter), const_cast<gentity_t *>(target));
	return context;
}

BotObjectiveFriendlyFirePolicy BotObjectives_EvaluateFriendlyFirePolicy(
	const BotObjectiveFriendlyFireContext &context) {
	botObjectiveStatus.friendlyFirePolicyEvaluations++;

	BotObjectiveFriendlyFirePolicy policy{};
	policy.valid = context.valid;
	policy.requiresFriendlyLineCheck = context.teamMode;
	policy.friendlyFireDamageEnabled = context.friendlyFireDamageEnabled;
	policy.friendlyFireScalePercent = context.friendlyFireScalePercent;

	if (!context.valid) {
		BotObjectives_RecordFriendlyFirePolicySelection(policy);
		return policy;
	}

	if (context.targetIsSelf) {
		policy.targetAllowed = false;
		policy.shouldAvoidFire = true;
		policy.reason = "self_target";
	} else if (context.targetIsTeammate) {
		policy.targetAllowed = false;
		policy.shouldAvoidFire = true;
		policy.reason = context.friendlyFireDamageEnabled
			? "friendly_target_damage"
			: "friendly_target_filter";
	} else if (context.friendlyInLineOfFire) {
		policy.shouldAvoidFire = true;
		policy.reason = context.friendlyFireDamageEnabled
			? "friendly_line_damage"
			: "friendly_line_blocked";
	} else if (context.teamMode) {
		policy.reason = context.friendlyFireDamageEnabled
			? "team_clear_ff_check"
			: "team_clear_filter";
	} else {
		policy.requiresFriendlyLineCheck = false;
		policy.reason = "free_for_all_clear";
	}

	BotObjectives_RecordFriendlyFirePolicySelection(policy);
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

BotObjectiveTarget BotObjectives_SelectEnemyFlagCarrierSupportTarget(const gentity_t *bot) {
	botObjectiveStatus.targetSelections++;

	const int botTeam = BotObjectives_BotTeam(bot);
	if (!BotObjectives_IsPrimaryTeam(botTeam)) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	const int item = BotObjectives_EnemyFlagItemForTeam(botTeam);
	if (!BotObjectives_IsFlagItem(item)) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	BotObjectiveTarget best{};
	int bestScore = -999999;
	for (int client = 0; client < game.maxClients; ++client) {
		const int entnum = client + 1;
		if (entnum >= globals.numEntities) {
			break;
		}

		const gentity_t *carrier = &g_entities[entnum];
		if (carrier == bot ||
			!BotObjectives_IsAlivePlayer(carrier) ||
			static_cast<int>(carrier->client->sess.team) != botTeam ||
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

	if (bestScore < 0) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	BotObjectives_RecordTargetSource(best.source);
	BotObjectives_RecordLastTarget(
		best,
		BotObjectives_ClientIndexForEntity(bot),
		botTeam,
		BotObjectiveRole::Support,
		BotObjectives_RolePriorityForTarget(BotObjectiveRole::Support, best));
	return best;
}

BotObjectiveTarget BotObjectives_SelectOwnFlagReturnTarget(const gentity_t *bot) {
	botObjectiveStatus.targetSelections++;

	const int botTeam = BotObjectives_BotTeam(bot);
	if (!BotObjectives_IsPrimaryTeam(botTeam)) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	const int item = BotObjectives_OwnFlagItemForTeam(botTeam);
	if (!BotObjectives_IsFlagItem(item)) {
		botObjectiveStatus.targetSelectionFailures++;
		return {};
	}

	BotObjectiveTarget best{};
	int bestScore = -999999;
	for (int client = 0; client < game.maxClients; ++client) {
		const int entnum = client + 1;
		if (entnum >= globals.numEntities) {
			break;
		}

		const gentity_t *carrier = &g_entities[entnum];
		if (carrier == bot ||
			!BotObjectives_IsAlivePlayer(carrier) ||
			static_cast<int>(carrier->client->sess.team) == botTeam ||
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

	if (bestScore < 0) {
		const char *className = BotObjectives_FlagClassNameForItem(item);
		if (className == nullptr) {
			botObjectiveStatus.targetSelectionFailures++;
			return {};
		}

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
		.lane = rolePolicy.lane,
		.source = context.target.source,
		.priority = rolePolicy.assignmentPriority,
		.rolePriority = rolePolicy.rolePriority,
		.lanePriority = rolePolicy.lanePriority,
		.attackPriority = rolePolicy.attackPriority,
		.defendPriority = rolePolicy.defendPriority,
		.returnPriority = rolePolicy.returnPriority,
		.supportPriority = rolePolicy.supportPriority,
		.attackLanePriority = rolePolicy.attackLanePriority,
		.defenseLanePriority = rolePolicy.defenseLanePriority,
		.midfieldLanePriority = rolePolicy.midfieldLanePriority,
		.carrierSupportPriority = rolePolicy.carrierSupportPriority,
		.droppedFlagResponsePriority = rolePolicy.droppedFlagResponsePriority,
		.ownBaseReturnPriority = rolePolicy.ownBaseReturnPriority,
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
		.laneReason = rolePolicy.laneReason,
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

BotObjectiveAssignment BotObjectives_AssignEnemyFlagCarrierSupportObjective(
	const gentity_t *bot,
	bool smokeEnabled) {
	const BotObjectiveTarget target = BotObjectives_SelectEnemyFlagCarrierSupportTarget(bot);
	return BotObjectives_Assign(
		BotObjectives_BuildContextForTarget(
			bot,
			target,
			smokeEnabled,
			BotObjectiveRole::Support));
}

BotObjectiveAssignment BotObjectives_AssignOwnFlagReturnObjective(
	const gentity_t *bot,
	bool smokeEnabled) {
	const BotObjectiveTarget target = BotObjectives_SelectOwnFlagReturnTarget(bot);
	return BotObjectives_Assign(
		BotObjectives_BuildContextForTarget(
			bot,
			target,
			smokeEnabled,
			BotObjectiveRole::Returner));
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
	case BotObjectiveRole::Midfielder:
		return "midfielder";
	default:
		return "none";
	}
}

const char *BotObjectives_LaneName(BotObjectiveLane lane) {
	switch (lane) {
	case BotObjectiveLane::Attack:
		return "attack";
	case BotObjectiveLane::Defense:
		return "defense";
	case BotObjectiveLane::Midfield:
		return "midfield";
	case BotObjectiveLane::CarrierSupport:
		return "carrier_support";
	case BotObjectiveLane::DroppedFlagResponse:
		return "dropped_flag_response";
	case BotObjectiveLane::OwnBaseReturn:
		return "own_base_return";
	default:
		return "none";
	}
}

const char *BotObjectives_MatchModeName(BotObjectiveMatchMode mode) {
	switch (mode) {
	case BotObjectiveMatchMode::FreeForAll:
		return "free_for_all";
	case BotObjectiveMatchMode::TeamDeathmatch:
		return "team_deathmatch";
	case BotObjectiveMatchMode::CaptureTheFlag:
		return "capture_the_flag";
	case BotObjectiveMatchMode::Cooperative:
		return "cooperative";
	default:
		return "none";
	}
}

const char *BotObjectives_ItemCategoryName(BotObjectiveItemCategory category) {
	switch (category) {
	case BotObjectiveItemCategory::Health:
		return "health";
	case BotObjectiveItemCategory::Armor:
		return "armor";
	case BotObjectiveItemCategory::Ammo:
		return "ammo";
	case BotObjectiveItemCategory::Weapon:
		return "weapon";
	case BotObjectiveItemCategory::Powerup:
		return "powerup";
	case BotObjectiveItemCategory::Tech:
		return "tech";
	case BotObjectiveItemCategory::CtfObjective:
		return "ctf_objective";
	case BotObjectiveItemCategory::Utility:
		return "utility";
	default:
		return "none";
	}
}

const char *BotObjectives_CoopIntentName(BotObjectiveCoopIntent intent) {
	switch (intent) {
	case BotObjectiveCoopIntent::FollowLeader:
		return "follow_leader";
	case BotObjectiveCoopIntent::WaitForLeader:
		return "wait_for_leader";
	case BotObjectiveCoopIntent::Regroup:
		return "regroup";
	case BotObjectiveCoopIntent::LeadAdvance:
		return "lead_advance";
	case BotObjectiveCoopIntent::SupportCombat:
		return "support_combat";
	default:
		return "none";
	}
}

const char *BotObjectives_ResourceIntentName(BotObjectiveResourceIntent intent) {
	switch (intent) {
	case BotObjectiveResourceIntent::SelfPickup:
		return "self_pickup";
	case BotObjectiveResourceIntent::ShareTeam:
		return "share_team";
	case BotObjectiveResourceIntent::ReserveForTeammate:
		return "reserve_for_teammate";
	case BotObjectiveResourceIntent::DenyEnemy:
		return "deny_enemy";
	case BotObjectiveResourceIntent::Objective:
		return "objective";
	default:
		return "none";
	}
}

const char *BotObjectives_ItemRoleName(BotObjectiveItemRole role) {
	switch (role) {
	case BotObjectiveItemRole::SelfStack:
		return "self_stack";
	case BotObjectiveItemRole::WeaponControl:
		return "weapon_control";
	case BotObjectiveItemRole::PowerupControl:
		return "powerup_control";
	case BotObjectiveItemRole::TeamResource:
		return "team_resource";
	case BotObjectiveItemRole::DenyEnemy:
		return "deny_enemy";
	case BotObjectiveItemRole::Objective:
		return "objective";
	default:
		return "none";
	}
}
