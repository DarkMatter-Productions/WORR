// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

struct gentity_t;

enum class BotObjectiveType {
	None = 0,
	EnemyFlagPickup = 1,
	OwnFlagReturn = 2,
	NeutralFlagPickup = 3,
	BaseDefense = 4,
};

enum class BotObjectiveRole {
	None = 0,
	Attacker,
	Defender,
	Returner,
	Support,
};

enum class BotObjectiveTargetSource {
	None = 0,
	WorldFlagEntity = 1,
	DroppedFlagEntity = 2,
	FlagCarrier = 3,
	EnemyTeamAnchor = 4,
};

struct BotObjectiveTarget {
	bool available = false;
	bool reachable = false;
	int entity = -1;
	int spawnCount = 0;
	int item = 0;
	int area = 0;
	int ownerTeam = 0;
	int carrierClient = -1;
	float origin[3] = {};
	BotObjectiveType type = BotObjectiveType::None;
	BotObjectiveTargetSource source = BotObjectiveTargetSource::None;
};

struct BotObjectiveContext {
	bool smokeEnabled = false;
	bool valid = false;
	bool alive = false;
	int clientIndex = -1;
	int team = 0;
	BotObjectiveRole requestedRole = BotObjectiveRole::None;
	BotObjectiveTarget target{};
};

struct BotObjectiveRolePolicy {
	bool valid = false;
	bool hasRequestedRole = false;
	bool requestedRoleHonored = false;
	bool fallbackRole = false;
	BotObjectiveType type = BotObjectiveType::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	int assignmentPriority = 0;
	int rolePriority = 0;
	int attackPriority = 0;
	int defendPriority = 0;
	int returnPriority = 0;
	int supportPriority = 0;
	const char *reason = "none";
};

// Assignment data only. wantsRoute is a request for the future brain/nav bridge;
// this module does not mutate navigation state or claim scenario completion.
struct BotObjectiveAssignment {
	bool assigned = false;
	bool wantsRoute = false;
	BotObjectiveType type = BotObjectiveType::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveTargetSource source = BotObjectiveTargetSource::None;
	int priority = 0;
	int rolePriority = 0;
	int attackPriority = 0;
	int defendPriority = 0;
	int returnPriority = 0;
	int supportPriority = 0;
	int clientIndex = -1;
	int team = 0;
	int targetTeam = 0;
	int entity = -1;
	int spawnCount = 0;
	int item = 0;
	int area = 0;
	int carrierClient = -1;
	float origin[3] = {};
	const char *reason = "none";
};

struct BotObjectiveRouteGoal {
	bool valid = false;
	int area = 0;
	int entity = -1;
	int spawnCount = 0;
	int item = 0;
	float origin[3] = {};
};

// Process-local counters accumulate until BotObjectives_ResetStatus().
struct BotObjectiveStatus {
	int evaluations = 0;
	int disabledEvaluations = 0;
	int invalidContexts = 0;
	int deadContexts = 0;
	int missingTeams = 0;
	int missingObjectives = 0;
	int unreachableObjectives = 0;
	int targetSelections = 0;
	int targetSelectionFailures = 0;
	int targetCandidates = 0;
	int targetAreaResolutions = 0;
	int targetAreaFailures = 0;
	int worldFlagTargets = 0;
	int droppedFlagTargets = 0;
	int carrierTargets = 0;
	int enemyTeamAnchorTargets = 0;
	int assignments = 0;
	int routeRequests = 0;
	int routeCommands = 0;
	int reaches = 0;
	int flagPickups = 0;
	int flagCaptures = 0;
	int enemyFlagPickups = 0;
	int ownFlagReturns = 0;
	int neutralFlagPickups = 0;
	int enemyFlagCaptures = 0;
	int neutralFlagCaptures = 0;
	int invalidEventHooks = 0;
	int roleAttacker = 0;
	int roleDefender = 0;
	int roleReturner = 0;
	int roleSupport = 0;
	int rolePolicyEvaluations = 0;
	int rolePolicySelections = 0;
	int rolePolicyRequested = 0;
	int rolePolicyRequestedHonored = 0;
	int rolePolicyFallbacks = 0;
	int rolePolicyNoSelection = 0;
	int rolePolicyAttackSelections = 0;
	int rolePolicyDefendSelections = 0;
	int rolePolicyReturnSelections = 0;
	int rolePolicySupportSelections = 0;
	int enemyFlagAssignments = 0;
	int ownFlagReturnAssignments = 0;
	int neutralFlagAssignments = 0;
	int baseDefenseAssignments = 0;
	int lastObjectiveType = 0;
	int lastObjectiveRole = 0;
	int lastTargetSource = 0;
	int lastClient = -1;
	int lastTeam = 0;
	int lastTargetTeam = 0;
	int lastEntity = -1;
	int lastSpawnCount = 0;
	int lastItem = 0;
	int lastArea = 0;
	int lastPriority = 0;
	int lastRolePriority = 0;
	int lastAttackPriority = 0;
	int lastDefendPriority = 0;
	int lastReturnPriority = 0;
	int lastSupportPriority = 0;
	int lastCarrierClient = -1;
	int lastOriginX = 0;
	int lastOriginY = 0;
	int lastOriginZ = 0;
};

void BotObjectives_ResetStatus();
BotObjectiveTarget BotObjectives_BuildFlagTarget(int botTeam, int entityNumber, int item, int area, bool available);
BotObjectiveTarget BotObjectives_BuildFlagTargetAt(
	int botTeam,
	int entityNumber,
	int spawnCount,
	int item,
	int area,
	bool available,
	const float origin[3],
	BotObjectiveTargetSource source,
	int carrierClient);
BotObjectiveTarget BotObjectives_BuildFlagTargetForEntity(const gentity_t *bot, const gentity_t *flag, int area);
BotObjectiveContext BotObjectives_BuildContextForTarget(const gentity_t *bot, const BotObjectiveTarget &target, bool smokeEnabled, BotObjectiveRole requestedRole);
BotObjectiveTarget BotObjectives_SelectEnemyFlagTarget(const gentity_t *bot, bool allowEnemyTeamAnchor);
BotObjectiveRolePolicy BotObjectives_EvaluateRolePolicy(const BotObjectiveContext &context);
BotObjectiveAssignment BotObjectives_Assign(const BotObjectiveContext &context);
BotObjectiveAssignment BotObjectives_AssignEnemyFlagObjective(
	const gentity_t *bot,
	bool smokeEnabled,
	BotObjectiveRole requestedRole,
	bool allowEnemyTeamAnchor);
bool BotObjectives_BuildRouteGoal(const BotObjectiveAssignment &assignment, BotObjectiveRouteGoal *goal);
void BotObjectives_RecordRouteRequest(const BotObjectiveAssignment &assignment);
void BotObjectives_RecordRouteCommand(const BotObjectiveAssignment &assignment);
void BotObjectives_RecordReach(const BotObjectiveAssignment &assignment);
void BotObjectives_RecordRouteRequest(const BotObjectiveRouteGoal &goal, const BotObjectiveAssignment &assignment);
void BotObjectives_RecordRouteCommand(const BotObjectiveRouteGoal &goal, const BotObjectiveAssignment &assignment);
void BotObjectives_RecordReach(const BotObjectiveRouteGoal &goal, const BotObjectiveAssignment &assignment);
void BotObjectives_RecordFlagPickup(int clientIndex, int team, int item);
void BotObjectives_RecordFlagCapture(int clientIndex, int team, int item);
void BotObjectives_RecordFlagPickup(const gentity_t *player, const gentity_t *flag);
void BotObjectives_RecordFlagCapture(const gentity_t *player, int item);
const BotObjectiveStatus &BotObjectives_GetStatus();
BotObjectiveType BotObjectives_FlagObjectiveTypeForTeam(int botTeam, int flagItem);
int BotObjectives_FlagOwnerTeamForItem(int flagItem);
int BotObjectives_EnemyFlagItemForTeam(int team);
int BotObjectives_OwnFlagItemForTeam(int team);
int BotObjectives_ClientIndexForEntity(const gentity_t *ent);
BotObjectiveRole BotObjectives_DefaultRoleForType(BotObjectiveType type);
BotObjectiveRole BotObjectives_DefaultRoleForTarget(const BotObjectiveTarget &target);
int BotObjectives_PriorityForType(BotObjectiveType type);
int BotObjectives_RolePriorityForTarget(BotObjectiveRole role, const BotObjectiveTarget &target);
const char *BotObjectives_TypeName(BotObjectiveType type);
const char *BotObjectives_RoleName(BotObjectiveRole role);
const char *BotObjectives_TargetSourceName(BotObjectiveTargetSource source);
