// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include "bot_combat.hpp"
#include "bot_items.hpp"

struct gentity_t;
struct usercmd_t;

// Decision vocabulary only. Navigation ownership stays with bot_nav.*, and
// weapon/inventory command dispatch must be provided by a later integration.
enum class BotActionIntent {
	None,
	MoveToItem,
	SwitchWeapon,
	Attack,
	UseWorld,
	UseInventory,
};

// Caller-owned frame facts. BotActions_BuildContext() fills the bot-local
// fields; future brain/perception/nav owners may enrich item/combat/use fields.
struct BotActionContext {
	bool valid = false;
	bool alive = false;
	bool useWorldRequested = false;
	bool inventoryUseRequested = false;
	int clientIndex = -1;
	int health = 0;
	int maxHealth = 0;
	int armor = 0;
	int inventoryItem = 0;
	BotItemContext item{};
	BotCombatContext combat{};
};

// ApplyDecision only mutates usercmd_t buttons for pressAttack/pressUse.
// wantsWeaponSwitch and wantsInventoryUse are accepted as pending intent today;
// callers that actually submit a switch/use request must record that separately.
struct BotActionDecision {
	BotActionIntent intent = BotActionIntent::None;
	int clientIndex = -1;
	int priority = 0;
	int item = 0;
	int entity = -1;
	int weaponItem = 0;
	bool pressAttack = false;
	bool pressUse = false;
	bool wantsWeaponSwitch = false;
	bool wantsInventoryUse = false;
	const char *reason = "none";
};

enum class BotActionApplyFailure {
	None,
	NullCommand,
	NoIntent,
	NonPositivePriority,
	IntentFlagMismatch,
	MissingWeaponItem,
	MissingInventoryItem,
};

enum class BotActionCommandRequestKind {
	None,
	UseWeaponIndex,
	UseInventoryIndex,
};

enum class BotActionCommandRequestFailure {
	None,
	NoIntent,
	NonPositivePriority,
	IntentFlagMismatch,
	NotPendingCommandIntent,
	InvalidClientIndex,
	MissingWeaponItem,
	MissingInventoryItem,
	InvalidItemIndex,
	UnknownItem,
	ItemNotUsable,
	ItemNotWeapon,
	InventoryItemIsWeapon,
};

struct BotActionApplyResult {
	bool accepted = false;
	bool commandMutated = false;
	bool pendingIntentAccepted = false;
	bool attackButtonApplied = false;
	bool useButtonApplied = false;
	bool weaponSwitchPending = false;
	bool inventoryUsePending = false;
	int weaponSwitchItem = 0;
	int inventoryUseItem = 0;
	BotActionApplyFailure failure = BotActionApplyFailure::None;
};

// A request describes the concrete game/client command a later integration can
// submit. Building it validates intent and item shape, but never executes it.
struct BotActionCommandRequest {
	bool valid = false;
	BotActionCommandRequestKind kind = BotActionCommandRequestKind::None;
	BotActionCommandRequestFailure failure = BotActionCommandRequestFailure::None;
	int clientIndex = -1;
	int item = 0;
	int argumentItem = 0;
	bool exactItem = false;
	const char *command = "";
	const char *reason = "none";
};

enum class BotWeaponSwitchProofEvent {
	None,
	RequestAccepted,
	RequestRejected,
	DuplicateRequest,
	PendingObservation,
	Completion,
	Failure,
	Mismatch,
	NoPendingRequest,
};

struct BotWeaponSwitchProofResult {
	bool valid = false;
	bool pending = false;
	bool completed = false;
	bool failed = false;
	bool matchedExpected = false;
	int clientIndex = -1;
	int expectedWeaponItem = 0;
	int actualWeaponItem = 0;
	BotWeaponSwitchProofEvent event = BotWeaponSwitchProofEvent::None;
};

// Process-local counters. They accumulate until BotActions_ResetStatus() and
// references returned by BotActions_GetStatus() are borrowed, not owned.
struct BotActionStatus {
	int evaluations = 0;
	int invalidContexts = 0;
	int deadContexts = 0;
	int itemEvaluations = 0;
	int combatEvaluations = 0;
	int moveToItemDecisions = 0;
	int weaponSwitchDecisions = 0;
	int attackDecisions = 0;
	int useWorldDecisions = 0;
	int useInventoryDecisions = 0;
	int noopDecisions = 0;
	int applyAttempts = 0;
	int acceptedApplications = 0;
	int rejectedApplications = 0;
	int appliedCommands = 0;
	int appliedAttackButtons = 0;
	int appliedUseButtons = 0;
	int pendingWeaponSwitches = 0;
	int pendingInventoryUses = 0;
	int commandRequestBuilds = 0;
	int commandRequestAccepted = 0;
	int commandRequestRejected = 0;
	int weaponCommandRequests = 0;
	int inventoryCommandRequests = 0;
	int commandRequestInvalidClients = 0;
	int commandRequestInvalidItems = 0;
	int commandRequestUnknownItems = 0;
	int commandRequestUnusableItems = 0;
	int commandRequestWeaponRejects = 0;
	int commandRequestInventoryRejects = 0;
	int lastCommandRequestClientIndex = -1;
	int lastCommandRequestItem = 0;
	BotActionCommandRequestKind lastCommandRequestKind = BotActionCommandRequestKind::None;
	BotActionCommandRequestFailure lastCommandRequestFailure = BotActionCommandRequestFailure::None;
	int weaponSwitchRequests = 0;
	int weaponSwitchValidatedRequests = 0;
	int weaponSwitchRejectedRequests = 0;
	int weaponSwitchDuplicateRequests = 0;
	int weaponSwitchPendingRequests = 0;
	int weaponSwitchCompletions = 0;
	int weaponSwitchFailures = 0;
	int weaponSwitchNoPendingEvents = 0;
	int weaponSwitchInvalidEvents = 0;
	int weaponSwitchMismatches = 0;
	int weaponSwitchExpectedItem = 0;
	int weaponSwitchActualItem = 0;
	int weaponSwitchPreviousItem = 0;
	int weaponSwitchLastClientIndex = -1;
	int weaponSwitchExpectedMatch = 0;
	int lastClientIndex = -1;
	int lastPriority = 0;
	int lastItem = 0;
	int lastEntity = -1;
	int lastWeaponItem = 0;
	BotActionIntent lastIntent = BotActionIntent::None;
	BotActionApplyFailure lastApplyFailure = BotActionApplyFailure::None;
	BotWeaponSwitchProofEvent lastWeaponSwitchEvent = BotWeaponSwitchProofEvent::None;
};

void BotActions_ResetStatus();
BotActionContext BotActions_BuildContext(const gentity_t *bot);
BotActionDecision BotActions_Decide(const BotActionContext &context);
BotActionApplyResult BotActions_ApplyDecisionDetailed(const BotActionDecision &decision, usercmd_t *cmd);
bool BotActions_ApplyDecision(const BotActionDecision &decision, usercmd_t *cmd);
BotActionApplyFailure BotActions_ValidateDecisionForApplication(const BotActionDecision &decision);
BotActionCommandRequest BotActions_BuildCommandRequest(const BotActionDecision &decision);
BotActionCommandRequestFailure BotActions_ValidateCommandRequest(const BotActionDecision &decision);
bool BotActions_IsWeaponSwitchDecision(const BotActionDecision &decision);
bool BotActions_RecordWeaponSwitchRequest(const BotActionDecision &decision);
void BotActions_RecordWeaponSwitchRequest(int expectedWeaponItem);
BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchRequestDetailed(
	const BotActionDecision &decision,
	int currentWeaponItem);
BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchObservation(
	int clientIndex,
	int actualWeaponItem);
bool BotActions_RecordWeaponSwitchCompletion(const BotActionDecision &decision, int actualWeaponItem);
void BotActions_RecordWeaponSwitchCompletion(int expectedWeaponItem, int actualWeaponItem);
BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchCompletionObserved(
	int clientIndex,
	int actualWeaponItem);
bool BotActions_RecordWeaponSwitchFailure(const BotActionDecision &decision, int actualWeaponItem);
void BotActions_RecordWeaponSwitchFailure(int expectedWeaponItem, int actualWeaponItem);
BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchFailureObserved(
	int clientIndex,
	int actualWeaponItem);
const BotActionStatus &BotActions_GetStatus();
const char *BotActions_IntentName(BotActionIntent intent);
const char *BotActions_ApplyFailureName(BotActionApplyFailure failure);
const char *BotActions_CommandRequestKindName(BotActionCommandRequestKind kind);
const char *BotActions_CommandRequestFailureName(BotActionCommandRequestFailure failure);
const char *BotActions_WeaponSwitchProofEventName(BotWeaponSwitchProofEvent event);
