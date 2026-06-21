// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

struct gentity_t;
struct Item;

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
	Midfielder,
};

enum class BotObjectiveLane {
	None = 0,
	Attack,
	Defense,
	Midfield,
	CarrierSupport,
	DroppedFlagResponse,
	OwnBaseReturn,
};

enum class BotObjectiveTargetSource {
	None = 0,
	WorldFlagEntity = 1,
	DroppedFlagEntity = 2,
	FlagCarrier = 3,
	EnemyTeamAnchor = 4,
};

enum class BotObjectiveMatchMode {
	None = 0,
	FreeForAll = 1,
	TeamDeathmatch = 2,
	CaptureTheFlag = 3,
	Cooperative = 4,
};

enum class BotObjectiveItemCategory {
	None = 0,
	Health = 1,
	Armor = 2,
	Ammo = 3,
	Weapon = 4,
	Powerup = 5,
	Tech = 6,
	CtfObjective = 7,
	Utility = 8,
};

enum class BotObjectiveItemRole {
	None = 0,
	SelfStack = 1,
	WeaponControl = 2,
	PowerupControl = 3,
	TeamResource = 4,
	DenyEnemy = 5,
	Objective = 6,
};

enum class BotObjectiveCoopIntent {
	None = 0,
	FollowLeader = 1,
	WaitForLeader = 2,
	Regroup = 3,
	LeadAdvance = 4,
	SupportCombat = 5,
};

enum class BotObjectiveResourceIntent {
	None = 0,
	SelfPickup = 1,
	ShareTeam = 2,
	ReserveForTeammate = 3,
	DenyEnemy = 4,
	Objective = 5,
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
	BotObjectiveLane lane = BotObjectiveLane::None;
	int assignmentPriority = 0;
	int rolePriority = 0;
	int lanePriority = 0;
	int attackPriority = 0;
	int defendPriority = 0;
	int returnPriority = 0;
	int supportPriority = 0;
	int attackLanePriority = 0;
	int defenseLanePriority = 0;
	int midfieldLanePriority = 0;
	int carrierSupportPriority = 0;
	int droppedFlagResponsePriority = 0;
	int ownBaseReturnPriority = 0;
	const char *reason = "none";
	const char *laneReason = "none";
};

// Assignment data only. wantsRoute is a request for the future brain/nav bridge;
// this module does not mutate navigation state or claim scenario completion.
struct BotObjectiveAssignment {
	bool assigned = false;
	bool wantsRoute = false;
	BotObjectiveType type = BotObjectiveType::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveLane lane = BotObjectiveLane::None;
	BotObjectiveTargetSource source = BotObjectiveTargetSource::None;
	int priority = 0;
	int rolePriority = 0;
	int lanePriority = 0;
	int attackPriority = 0;
	int defendPriority = 0;
	int returnPriority = 0;
	int supportPriority = 0;
	int attackLanePriority = 0;
	int defenseLanePriority = 0;
	int midfieldLanePriority = 0;
	int carrierSupportPriority = 0;
	int droppedFlagResponsePriority = 0;
	int ownBaseReturnPriority = 0;
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
	const char *laneReason = "none";
};

struct BotObjectiveRouteGoal {
	bool valid = false;
	int area = 0;
	int entity = -1;
	int spawnCount = 0;
	int item = 0;
	float origin[3] = {};
};

struct BotObjectiveMatchContext {
	bool valid = false;
	bool alive = false;
	bool scoringEnabled = false;
	bool teamMode = false;
	bool ctfMode = false;
	bool coopMode = false;
	bool friendlyFireDamageEnabled = false;
	int clientIndex = -1;
	int gametype = 0;
	int team = 0;
	int teamSize = 0;
	int enemyCount = 0;
	int teamScore = 0;
	int enemyTeamScore = 0;
	int friendlyFireScalePercent = 0;
	BotObjectiveMatchMode mode = BotObjectiveMatchMode::None;
	BotObjectiveRole requestedRole = BotObjectiveRole::None;
};

struct BotObjectiveMatchPolicy {
	bool valid = false;
	bool participatesInScoring = false;
	bool hasRequestedRole = false;
	bool requestedRoleHonored = false;
	bool fallbackRole = false;
	bool wantsRoam = false;
	bool wantsCollect = false;
	bool wantsEngage = false;
	bool wantsObjective = false;
	bool avoidSpawnCamping = false;
	bool requiresTeamTargetFilter = false;
	bool friendlyFireAvoidance = false;
	bool preferMajorItems = false;
	bool shareTeamResources = false;
	BotObjectiveMatchMode mode = BotObjectiveMatchMode::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveLane lane = BotObjectiveLane::None;
	int clientIndex = -1;
	int team = 0;
	int priority = 0;
	int roamPriority = 0;
	int collectPriority = 0;
	int engagePriority = 0;
	int objectivePriority = 0;
	int attackPriority = 0;
	int defendPriority = 0;
	int midfieldPriority = 0;
	int friendlyFireScalePercent = 0;
	const char *reason = "none";
	const char *laneReason = "none";
};

struct BotObjectiveCoopContext {
	bool valid = false;
	bool alive = false;
	bool coopMode = false;
	bool leaderValid = false;
	bool leaderAlive = false;
	bool leaderIsHuman = false;
	bool progressWaitRequested = false;
	bool separatedFromLeader = false;
	bool closeToLeader = false;
	int clientIndex = -1;
	int leaderClient = -1;
	int teamSize = 0;
	int humanCount = 0;
	int botCount = 0;
	int leaderDistanceSquared = 0;
	BotObjectiveRole requestedRole = BotObjectiveRole::None;
};

struct BotObjectiveCoopPolicy {
	bool valid = false;
	bool coopMode = false;
	bool hasLeader = false;
	bool leaderIsHuman = false;
	bool hasRequestedRole = false;
	bool requestedRoleHonored = false;
	bool fallbackRole = false;
	bool followLeader = false;
	bool waitForLeader = false;
	bool regroup = false;
	bool mayLead = false;
	bool shareResources = false;
	bool reserveResources = false;
	BotObjectiveCoopIntent intent = BotObjectiveCoopIntent::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveLane lane = BotObjectiveLane::None;
	int clientIndex = -1;
	int leaderClient = -1;
	int teamSize = 0;
	int humanCount = 0;
	int botCount = 0;
	int priority = 0;
	int followPriority = 0;
	int waitPriority = 0;
	int resourcePriority = 0;
	int leaderDistanceSquared = 0;
	const char *reason = "none";
	const char *laneReason = "none";
};

struct BotObjectiveResourceContext {
	bool valid = false;
	bool teamMode = false;
	bool coopMode = false;
	bool selfNeedsItem = false;
	bool teammateNeedsItem = false;
	bool enemyContested = false;
	bool objectiveRelevant = false;
	bool preferRoleResource = false;
	BotObjectiveMatchMode mode = BotObjectiveMatchMode::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveLane lane = BotObjectiveLane::None;
	BotObjectiveItemCategory category = BotObjectiveItemCategory::None;
	int candidatePriority = 0;
};

struct BotObjectiveResourcePolicy {
	bool valid = false;
	bool mayPickup = false;
	bool shouldShare = false;
	bool shouldReserve = false;
	bool denyEnemyPickup = false;
	bool objectiveResource = false;
	BotObjectiveResourceIntent intent = BotObjectiveResourceIntent::None;
	BotObjectiveMatchMode mode = BotObjectiveMatchMode::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveLane lane = BotObjectiveLane::None;
	BotObjectiveItemCategory category = BotObjectiveItemCategory::None;
	int priority = 0;
	const char *reason = "none";
};

struct BotObjectiveItemRolePolicy {
	bool valid = false;
	bool denyEnemyPickup = false;
	bool shareWithTeam = false;
	bool reserveForRole = false;
	BotObjectiveMatchMode mode = BotObjectiveMatchMode::None;
	BotObjectiveRole role = BotObjectiveRole::None;
	BotObjectiveLane lane = BotObjectiveLane::None;
	BotObjectiveItemCategory category = BotObjectiveItemCategory::None;
	BotObjectiveItemRole itemRole = BotObjectiveItemRole::None;
	int priority = 0;
	const char *reason = "none";
};

struct BotObjectiveFriendlyFireContext {
	bool valid = false;
	bool teamMode = false;
	bool targetIsSelf = false;
	bool targetIsTeammate = false;
	bool friendlyInLineOfFire = false;
	bool friendlyFireDamageEnabled = false;
	int shooterClient = -1;
	int targetClient = -1;
	int shooterTeam = 0;
	int targetTeam = 0;
	int friendlyFireScalePercent = 0;
};

struct BotObjectiveFriendlyFirePolicy {
	bool valid = false;
	bool targetAllowed = true;
	bool shouldAvoidFire = false;
	bool requiresFriendlyLineCheck = false;
	bool friendlyFireDamageEnabled = false;
	int friendlyFireScalePercent = 0;
	const char *reason = "none";
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
	int roleMidfielder = 0;
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
	int rolePolicyLaneAttackSelections = 0;
	int rolePolicyLaneDefenseSelections = 0;
	int rolePolicyLaneMidfieldSelections = 0;
	int rolePolicyCarrierSupportSelections = 0;
	int rolePolicyDroppedFlagResponses = 0;
	int rolePolicyOwnBaseReturnSelections = 0;
	int enemyFlagAssignments = 0;
	int ownFlagReturnAssignments = 0;
	int neutralFlagAssignments = 0;
	int baseDefenseAssignments = 0;
	int matchPolicyEvaluations = 0;
	int matchPolicySelections = 0;
	int matchPolicyNoSelection = 0;
	int matchPolicyScoringParticipation = 0;
	int matchPolicyFfaSelections = 0;
	int matchPolicyTdmSelections = 0;
	int matchPolicyCtfSelections = 0;
	int matchPolicyCoopSelections = 0;
	int matchPolicyAttackSelections = 0;
	int matchPolicyDefendSelections = 0;
	int matchPolicyMidfieldSelections = 0;
	int matchPolicyFriendlyFireAvoidance = 0;
	int coopPolicyEvaluations = 0;
	int coopPolicySelections = 0;
	int coopPolicyNoSelection = 0;
	int coopPolicyFollowSelections = 0;
	int coopPolicyWaitSelections = 0;
	int coopPolicyRegroupSelections = 0;
	int coopPolicyLeadSelections = 0;
	int coopPolicySupportSelections = 0;
	int coopPolicyResourceShareSelections = 0;
	int resourcePolicyEvaluations = 0;
	int resourcePolicySelections = 0;
	int resourcePolicyNoSelection = 0;
	int resourcePolicySelfPickupSelections = 0;
	int resourcePolicyShareTeamSelections = 0;
	int resourcePolicyReserveSelections = 0;
	int resourcePolicyDenyEnemySelections = 0;
	int resourcePolicyObjectiveSelections = 0;
	int itemRolePolicyEvaluations = 0;
	int itemRolePolicySelections = 0;
	int itemRolePolicyNoSelection = 0;
	int itemRoleSelfStackSelections = 0;
	int itemRoleWeaponControlSelections = 0;
	int itemRolePowerupControlSelections = 0;
	int itemRoleTeamResourceSelections = 0;
	int itemRoleDenyEnemySelections = 0;
	int itemRoleObjectiveSelections = 0;
	int friendlyFirePolicyEvaluations = 0;
	int friendlyFirePolicyAvoidance = 0;
	int friendlyFirePolicyTargetBlocks = 0;
	int lastObjectiveType = 0;
	int lastObjectiveRole = 0;
	int lastObjectiveLane = 0;
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
	int lastLanePriority = 0;
	int lastAttackPriority = 0;
	int lastDefendPriority = 0;
	int lastReturnPriority = 0;
	int lastSupportPriority = 0;
	int lastAttackLanePriority = 0;
	int lastDefenseLanePriority = 0;
	int lastMidfieldLanePriority = 0;
	int lastCarrierSupportPriority = 0;
	int lastDroppedFlagResponsePriority = 0;
	int lastOwnBaseReturnPriority = 0;
	int lastCarrierClient = -1;
	int lastOriginX = 0;
	int lastOriginY = 0;
	int lastOriginZ = 0;
	const char *lastReason = "none";
	const char *lastLaneReason = "none";
	int lastMatchMode = 0;
	int lastMatchRole = 0;
	int lastMatchLane = 0;
	int lastMatchPriority = 0;
	int lastMatchRoamPriority = 0;
	int lastMatchCollectPriority = 0;
	int lastMatchEngagePriority = 0;
	int lastMatchObjectivePriority = 0;
	int lastMatchAttackPriority = 0;
	int lastMatchDefendPriority = 0;
	int lastMatchMidfieldPriority = 0;
	int lastCoopIntent = 0;
	int lastCoopRole = 0;
	int lastCoopLane = 0;
	int lastCoopPriority = 0;
	int lastCoopFollowPriority = 0;
	int lastCoopWaitPriority = 0;
	int lastCoopResourcePriority = 0;
	int lastCoopLeaderClient = -1;
	int lastCoopLeaderDistanceSquared = 0;
	int lastResourceIntent = 0;
	int lastResourceCategory = 0;
	int lastResourcePriority = 0;
	int lastResourceShouldShare = 0;
	int lastResourceShouldReserve = 0;
	int lastResourceDenyEnemy = 0;
	int lastItemCategory = 0;
	int lastItemRole = 0;
	int lastItemRolePriority = 0;
	int lastFriendlyFireAvoidance = 0;
	int lastFriendlyFireTargetAllowed = 1;
	int lastFriendlyFireScalePercent = 0;
	const char *lastMatchReason = "none";
	const char *lastMatchLaneReason = "none";
	const char *lastCoopReason = "none";
	const char *lastCoopLaneReason = "none";
	const char *lastResourceReason = "none";
	const char *lastItemRoleReason = "none";
	const char *lastFriendlyFireReason = "none";
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
BotObjectiveMatchContext BotObjectives_BuildMatchContext(const gentity_t *bot, BotObjectiveRole requestedRole);
BotObjectiveMatchPolicy BotObjectives_EvaluateMatchPolicy(const BotObjectiveMatchContext &context);
BotObjectiveCoopContext BotObjectives_BuildCoopContext(
	const gentity_t *bot,
	const gentity_t *preferredLeader,
	bool progressWaitRequested,
	BotObjectiveRole requestedRole);
BotObjectiveCoopPolicy BotObjectives_EvaluateCoopPolicy(const BotObjectiveCoopContext &context);
BotObjectiveResourceContext BotObjectives_BuildResourceContext(
	const BotObjectiveMatchPolicy &matchPolicy,
	const BotObjectiveCoopPolicy &coopPolicy,
	BotObjectiveItemCategory category,
	int candidatePriority,
	bool selfNeedsItem,
	bool teammateNeedsItem,
	bool enemyContested);
BotObjectiveResourcePolicy BotObjectives_EvaluateResourcePolicy(const BotObjectiveResourceContext &context);
BotObjectiveItemCategory BotObjectives_ItemCategoryForItem(const Item *item);
BotObjectiveItemRolePolicy BotObjectives_EvaluateItemRolePolicy(
	const BotObjectiveMatchPolicy &matchPolicy,
	BotObjectiveItemCategory category,
	int candidatePriority);
BotObjectiveFriendlyFireContext BotObjectives_BuildFriendlyFireContext(
	const gentity_t *shooter,
	const gentity_t *target,
	bool friendlyInLineOfFire);
BotObjectiveFriendlyFirePolicy BotObjectives_EvaluateFriendlyFirePolicy(const BotObjectiveFriendlyFireContext &context);
BotObjectiveAssignment BotObjectives_Assign(const BotObjectiveContext &context);
BotObjectiveAssignment BotObjectives_AssignEnemyFlagObjective(
	const gentity_t *bot,
	bool smokeEnabled,
	BotObjectiveRole requestedRole,
	bool allowEnemyTeamAnchor);
BotObjectiveAssignment BotObjectives_AssignEnemyFlagCarrierSupportObjective(
	const gentity_t *bot,
	bool smokeEnabled);
BotObjectiveAssignment BotObjectives_AssignOwnFlagReturnObjective(
	const gentity_t *bot,
	bool smokeEnabled);
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
BotObjectiveLane BotObjectives_DefaultLaneForTarget(const BotObjectiveTarget &target);
int BotObjectives_PriorityForType(BotObjectiveType type);
int BotObjectives_RolePriorityForTarget(BotObjectiveRole role, const BotObjectiveTarget &target);
int BotObjectives_LanePriorityForTarget(BotObjectiveLane lane, const BotObjectiveTarget &target);
BotObjectiveMatchMode BotObjectives_MatchModeForGameType(int gametype);
BotObjectiveRole BotObjectives_DefaultMatchRole(const BotObjectiveMatchContext &context);
BotObjectiveLane BotObjectives_DefaultLaneForMatchRole(BotObjectiveMatchMode mode, BotObjectiveRole role);
int BotObjectives_MatchRolePriority(BotObjectiveMatchMode mode, BotObjectiveRole role);
BotObjectiveCoopIntent BotObjectives_DefaultCoopIntent(const BotObjectiveCoopContext &context);
BotObjectiveRole BotObjectives_DefaultCoopRole(BotObjectiveCoopIntent intent);
BotObjectiveLane BotObjectives_DefaultCoopLane(BotObjectiveCoopIntent intent);
int BotObjectives_CoopIntentPriority(BotObjectiveCoopIntent intent);
const char *BotObjectives_TypeName(BotObjectiveType type);
const char *BotObjectives_RoleName(BotObjectiveRole role);
const char *BotObjectives_LaneName(BotObjectiveLane lane);
const char *BotObjectives_TargetSourceName(BotObjectiveTargetSource source);
const char *BotObjectives_MatchModeName(BotObjectiveMatchMode mode);
const char *BotObjectives_ItemCategoryName(BotObjectiveItemCategory category);
const char *BotObjectives_ItemRoleName(BotObjectiveItemRole role);
const char *BotObjectives_CoopIntentName(BotObjectiveCoopIntent intent);
const char *BotObjectives_ResourceIntentName(BotObjectiveResourceIntent intent);
