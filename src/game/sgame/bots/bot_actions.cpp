// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "bot_actions.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace {
constexpr int BOT_ACTION_USE_WORLD_PRIORITY = 60;
constexpr int BOT_ACTION_USE_INVENTORY_PRIORITY = 55;
constexpr const char *BOT_ACTION_USE_INDEX_ONLY_COMMAND = "use_index_only";

BotActionStatus botActionStatus;

struct BotPendingWeaponSwitchRequest {
	bool active = false;
	int expectedWeaponItem = 0;
	int previousWeaponItem = 0;
};

std::array<BotPendingWeaponSwitchRequest, MAX_CLIENTS> botPendingWeaponSwitchRequests{};

static_assert(static_cast<int>(BotActionIntent::None) == 0);
static_assert(static_cast<int>(BotActionApplyFailure::None) == 0);
static_assert(static_cast<int>(BotActionCommandRequestKind::None) == 0);
static_assert(static_cast<int>(BotActionCommandRequestFailure::None) == 0);
static_assert(static_cast<int>(BotActionCommandDispatchOutcome::None) == 0);
static_assert(static_cast<int>(BotActionCommandDispatchFailure::None) == 0);
static_assert(static_cast<int>(BotItemDecisionKind::None) == 0);
static_assert(static_cast<int>(BotCombatDecisionKind::None) == 0);

int BotActions_ClampDistanceSquared(float distanceSquared) {
	if (distanceSquared <= 0.0f) {
		return 0;
	}
	if (distanceSquared >= static_cast<float>(std::numeric_limits<int>::max())) {
		return std::numeric_limits<int>::max();
	}
	return static_cast<int>(distanceSquared);
}

int BotActions_ArmorValue(const gclient_t *client) {
	if (client == nullptr) {
		return 0;
	}

	const auto &inventory = client->pers.inventory;
	return std::max(
		std::max(inventory[IT_ARMOR_BODY], inventory[IT_ARMOR_COMBAT]),
		std::max(inventory[IT_ARMOR_JACKET], inventory[IT_ARMOR_SHARD]));
}

int BotActions_WeaponAmmo(const gclient_t *client, const Item *weapon) {
	if (client == nullptr || weapon == nullptr || weapon->ammo == IT_NULL) {
		return 0;
	}
	return client->pers.inventory[weapon->ammo];
}

bool BotActions_WeaponReady(const gclient_t *client, const Item *weapon) {
	if (client == nullptr || weapon == nullptr) {
		return false;
	}
	if (weapon->ammo == IT_NULL) {
		return true;
	}
	const int requiredAmmo = std::max(weapon->quantity, 1);
	return client->pers.inventory[weapon->ammo] >= requiredAmmo;
}

bool BotActions_EnemyAlive(const gentity_t *enemy) {
	return enemy != nullptr && enemy->inUse && enemy->health > 0 && !enemy->deadFlag;
}

bool BotActions_HasAnyMutationFlag(const BotActionDecision &decision) {
	return decision.pressAttack ||
		decision.pressUse ||
		decision.wantsWeaponSwitch ||
		decision.wantsInventoryUse;
}

bool BotActions_DecisionHasIntent(const BotActionDecision &decision) {
	return decision.intent != BotActionIntent::None && decision.priority > 0;
}

BotActionApplyFailure BotActions_ValidateApplicationDecision(const BotActionDecision &decision) {
	if (decision.intent == BotActionIntent::None) {
		return BotActionApplyFailure::NoIntent;
	}
	if (decision.priority <= 0) {
		return BotActionApplyFailure::NonPositivePriority;
	}

	switch (decision.intent) {
	case BotActionIntent::Attack:
		return decision.pressAttack &&
			!decision.pressUse &&
			!decision.wantsWeaponSwitch &&
			!decision.wantsInventoryUse ?
			BotActionApplyFailure::None :
			BotActionApplyFailure::IntentFlagMismatch;
	case BotActionIntent::UseWorld:
		return decision.pressUse &&
			!decision.pressAttack &&
			!decision.wantsWeaponSwitch &&
			!decision.wantsInventoryUse ?
			BotActionApplyFailure::None :
			BotActionApplyFailure::IntentFlagMismatch;
	case BotActionIntent::SwitchWeapon:
		if (decision.pressAttack || decision.pressUse || decision.wantsInventoryUse) {
			return BotActionApplyFailure::IntentFlagMismatch;
		}
		if (!decision.wantsWeaponSwitch || decision.weaponItem <= 0) {
			return BotActionApplyFailure::MissingWeaponItem;
		}
		return BotActionApplyFailure::None;
	case BotActionIntent::UseInventory:
		if (decision.pressAttack || decision.pressUse || decision.wantsWeaponSwitch) {
			return BotActionApplyFailure::IntentFlagMismatch;
		}
		if (!decision.wantsInventoryUse || decision.item <= 0) {
			return BotActionApplyFailure::MissingInventoryItem;
		}
		return BotActionApplyFailure::None;
	case BotActionIntent::MoveToItem:
		return !BotActions_HasAnyMutationFlag(decision) ?
			BotActionApplyFailure::None :
			BotActionApplyFailure::IntentFlagMismatch;
	default:
		return BotActionApplyFailure::IntentFlagMismatch;
	}
}

bool BotActions_ClientIndexValid(int clientIndex) {
	return clientIndex >= 0 &&
		clientIndex < static_cast<int>(botPendingWeaponSwitchRequests.size()) &&
		(game.maxClients == 0 || clientIndex < static_cast<int>(game.maxClients));
}

BotActionCommandRequestKind BotActions_CommandRequestKindForDecision(
	const BotActionDecision &decision) {
	switch (decision.intent) {
	case BotActionIntent::SwitchWeapon:
		return BotActionCommandRequestKind::UseWeaponIndex;
	case BotActionIntent::UseInventory:
		return BotActionCommandRequestKind::UseInventoryIndex;
	default:
		return BotActionCommandRequestKind::None;
	}
}

int BotActions_CommandRequestItemForDecision(const BotActionDecision &decision) {
	switch (decision.intent) {
	case BotActionIntent::SwitchWeapon:
		return decision.weaponItem;
	case BotActionIntent::UseInventory:
		return decision.item;
	default:
		return 0;
	}
}

BotActionCommandRequestFailure BotActions_CommandFailureFromApplyFailure(
	BotActionApplyFailure failure) {
	switch (failure) {
	case BotActionApplyFailure::NoIntent:
		return BotActionCommandRequestFailure::NoIntent;
	case BotActionApplyFailure::NonPositivePriority:
		return BotActionCommandRequestFailure::NonPositivePriority;
	case BotActionApplyFailure::IntentFlagMismatch:
		return BotActionCommandRequestFailure::IntentFlagMismatch;
	case BotActionApplyFailure::MissingWeaponItem:
		return BotActionCommandRequestFailure::MissingWeaponItem;
	case BotActionApplyFailure::MissingInventoryItem:
		return BotActionCommandRequestFailure::MissingInventoryItem;
	default:
		return BotActionCommandRequestFailure::None;
	}
}

bool BotActions_ItemIndexValid(int item) {
	return item > static_cast<int>(IT_NULL) && item < static_cast<int>(IT_TOTAL);
}

const Item *BotActions_ItemForCommandRequest(int item) {
	if (!BotActions_ItemIndexValid(item)) {
		return nullptr;
	}
	const Item *itemDef = GetItemByIndex(static_cast<item_id_t>(item));
	if (itemDef == nullptr || static_cast<int>(itemDef->id) != item) {
		return nullptr;
	}
	return itemDef;
}

bool BotActions_ItemIsKnownWeaponCommandItem(int item, const Item *itemDef) {
	return BotCombat_GetWeaponMetadata(item) != nullptr ||
		(itemDef != nullptr && (itemDef->flags & IF_WEAPON));
}

BotActionCommandRequestFailure BotActions_ValidateCommandRequestItem(
	int item,
	bool weaponRequest) {
	if (!BotActions_ItemIndexValid(item)) {
		return BotActionCommandRequestFailure::InvalidItemIndex;
	}

	const Item *itemDef = BotActions_ItemForCommandRequest(item);
	if (itemDef == nullptr) {
		return BotActionCommandRequestFailure::UnknownItem;
	}
	if (itemDef->use == nullptr) {
		return BotActionCommandRequestFailure::ItemNotUsable;
	}

	const bool weaponItem = BotActions_ItemIsKnownWeaponCommandItem(item, itemDef);
	if (weaponRequest && !weaponItem) {
		return BotActionCommandRequestFailure::ItemNotWeapon;
	}
	if (!weaponRequest && weaponItem) {
		return BotActionCommandRequestFailure::InventoryItemIsWeapon;
	}

	return BotActionCommandRequestFailure::None;
}

BotActionCommandRequestFailure BotActions_ValidateCommandRequestDecision(
	const BotActionDecision &decision) {
	const BotActionApplyFailure applyFailure = BotActions_ValidateApplicationDecision(decision);
	const BotActionCommandRequestFailure commandFailure =
		BotActions_CommandFailureFromApplyFailure(applyFailure);
	if (commandFailure != BotActionCommandRequestFailure::None) {
		return commandFailure;
	}

	const BotActionCommandRequestKind kind = BotActions_CommandRequestKindForDecision(decision);
	if (kind == BotActionCommandRequestKind::None) {
		return BotActionCommandRequestFailure::NotPendingCommandIntent;
	}
	if (!BotActions_ClientIndexValid(decision.clientIndex)) {
		return BotActionCommandRequestFailure::InvalidClientIndex;
	}

	const bool weaponRequest = kind == BotActionCommandRequestKind::UseWeaponIndex;
	return BotActions_ValidateCommandRequestItem(
		BotActions_CommandRequestItemForDecision(decision),
		weaponRequest);
}

void BotActions_RecordCommandRequestResult(const BotActionCommandRequest &request) {
	botActionStatus.commandRequestBuilds++;
	botActionStatus.lastCommandRequestKind = request.kind;
	botActionStatus.lastCommandRequestFailure = request.failure;
	botActionStatus.lastCommandRequestClientIndex = request.clientIndex;
	botActionStatus.lastCommandRequestItem = request.item;

	if (request.valid) {
		botActionStatus.commandRequestAccepted++;
		if (request.kind == BotActionCommandRequestKind::UseWeaponIndex) {
			botActionStatus.weaponCommandRequests++;
		} else if (request.kind == BotActionCommandRequestKind::UseInventoryIndex) {
			botActionStatus.inventoryCommandRequests++;
		}
		return;
	}

	botActionStatus.commandRequestRejected++;
	switch (request.failure) {
	case BotActionCommandRequestFailure::InvalidClientIndex:
		botActionStatus.commandRequestInvalidClients++;
		break;
	case BotActionCommandRequestFailure::MissingWeaponItem:
	case BotActionCommandRequestFailure::MissingInventoryItem:
	case BotActionCommandRequestFailure::InvalidItemIndex:
		botActionStatus.commandRequestInvalidItems++;
		break;
	case BotActionCommandRequestFailure::UnknownItem:
		botActionStatus.commandRequestUnknownItems++;
		break;
	case BotActionCommandRequestFailure::ItemNotUsable:
		botActionStatus.commandRequestUnusableItems++;
		break;
	case BotActionCommandRequestFailure::ItemNotWeapon:
		botActionStatus.commandRequestWeaponRejects++;
		break;
	case BotActionCommandRequestFailure::InventoryItemIsWeapon:
		botActionStatus.commandRequestInventoryRejects++;
		break;
	default:
		break;
	}
}

void BotActions_RecordCommandDispatchResult(
	const BotActionCommandRequest &request,
	BotActionCommandDispatchOutcome outcome,
	BotActionCommandDispatchFailure failure) {
	botActionStatus.commandRequestDispatchAttempts++;
	botActionStatus.lastCommandDispatchKind = request.kind;
	botActionStatus.lastCommandDispatchOutcome = outcome;
	botActionStatus.lastCommandDispatchFailure = failure;
	botActionStatus.lastCommandDispatchClientIndex = request.clientIndex;
	botActionStatus.lastCommandDispatchItem = request.item;

	switch (outcome) {
	case BotActionCommandDispatchOutcome::Submitted:
		botActionStatus.commandRequestSubmitted++;
		if (request.kind == BotActionCommandRequestKind::UseWeaponIndex) {
			botActionStatus.weaponCommandDispatches++;
		} else if (request.kind == BotActionCommandRequestKind::UseInventoryIndex) {
			botActionStatus.inventoryCommandDispatches++;
		}
		break;
	case BotActionCommandDispatchOutcome::Deferred:
		botActionStatus.commandRequestDeferred++;
		break;
	case BotActionCommandDispatchOutcome::Failed:
		botActionStatus.commandRequestDispatchFailures++;
		break;
	default:
		break;
	}
}

int BotActions_CountPendingWeaponSwitchRequests() {
	int pending = 0;
	for (const BotPendingWeaponSwitchRequest &request : botPendingWeaponSwitchRequests) {
		if (request.active) {
			pending++;
		}
	}
	return pending;
}

void BotActions_UpdatePendingWeaponSwitchCount() {
	botActionStatus.weaponSwitchPendingRequests = BotActions_CountPendingWeaponSwitchRequests();
}

BotWeaponSwitchProofResult BotActions_MakeWeaponSwitchProofResult(
	BotWeaponSwitchProofEvent event,
	int clientIndex,
	int expectedWeaponItem,
	int actualWeaponItem) {
	BotWeaponSwitchProofResult result{};
	result.event = event;
	result.clientIndex = clientIndex;
	result.expectedWeaponItem = expectedWeaponItem;
	result.actualWeaponItem = actualWeaponItem;
	result.matchedExpected = expectedWeaponItem > 0 && expectedWeaponItem == actualWeaponItem;
	return result;
}

void BotActions_RecordWeaponSwitchProofResult(const BotWeaponSwitchProofResult &result) {
	botActionStatus.lastWeaponSwitchEvent = result.event;
	botActionStatus.weaponSwitchLastClientIndex = result.clientIndex;
	botActionStatus.weaponSwitchExpectedItem = result.expectedWeaponItem;
	botActionStatus.weaponSwitchActualItem = result.actualWeaponItem;
	botActionStatus.weaponSwitchExpectedMatch = result.matchedExpected ? 1 : 0;
	BotActions_UpdatePendingWeaponSwitchCount();
}

BotWeaponSwitchProofResult BotActions_RecordRejectedWeaponSwitchRequest(
	const BotActionDecision &decision,
	int currentWeaponItem) {
	botActionStatus.weaponSwitchRejectedRequests++;
	botActionStatus.weaponSwitchInvalidEvents++;
	BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
		BotWeaponSwitchProofEvent::RequestRejected,
		decision.clientIndex,
		decision.weaponItem,
		currentWeaponItem);
	BotActions_RecordWeaponSwitchProofResult(result);
	return result;
}

BotWeaponSwitchProofResult BotActions_RecordDirectWeaponSwitchCompletion(
	int expectedWeaponItem,
	int actualWeaponItem) {
	if (expectedWeaponItem <= 0 || actualWeaponItem <= 0) {
		botActionStatus.weaponSwitchInvalidEvents++;
		BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
			BotWeaponSwitchProofEvent::Mismatch,
			-1,
			expectedWeaponItem,
			actualWeaponItem);
		BotActions_RecordWeaponSwitchProofResult(result);
		return result;
	}

	BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
		expectedWeaponItem == actualWeaponItem ?
			BotWeaponSwitchProofEvent::Completion :
			BotWeaponSwitchProofEvent::Mismatch,
		-1,
		expectedWeaponItem,
		actualWeaponItem);
	result.valid = true;
	result.completed = expectedWeaponItem == actualWeaponItem;
	result.failed = expectedWeaponItem != actualWeaponItem;
	if (result.completed) {
		botActionStatus.weaponSwitchCompletions++;
	} else {
		botActionStatus.weaponSwitchFailures++;
		botActionStatus.weaponSwitchMismatches++;
	}
	BotActions_RecordWeaponSwitchProofResult(result);
	return result;
}

BotWeaponSwitchProofResult BotActions_RecordDirectWeaponSwitchFailure(
	int expectedWeaponItem,
	int actualWeaponItem) {
	if (expectedWeaponItem <= 0) {
		botActionStatus.weaponSwitchInvalidEvents++;
		BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
			BotWeaponSwitchProofEvent::Failure,
			-1,
			expectedWeaponItem,
			actualWeaponItem);
		BotActions_RecordWeaponSwitchProofResult(result);
		return result;
	}

	BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
		expectedWeaponItem == actualWeaponItem ?
			BotWeaponSwitchProofEvent::Completion :
			BotWeaponSwitchProofEvent::Failure,
		-1,
		expectedWeaponItem,
		actualWeaponItem);
	result.valid = true;
	result.completed = expectedWeaponItem == actualWeaponItem && actualWeaponItem > 0;
	result.failed = !result.completed;
	if (result.completed) {
		botActionStatus.weaponSwitchCompletions++;
	} else {
		botActionStatus.weaponSwitchFailures++;
		if (actualWeaponItem > 0 && expectedWeaponItem != actualWeaponItem) {
			botActionStatus.weaponSwitchMismatches++;
		}
	}
	BotActions_RecordWeaponSwitchProofResult(result);
	return result;
}

BotWeaponSwitchProofResult BotActions_RecordPendingWeaponSwitchTerminal(
	int clientIndex,
	int actualWeaponItem,
	bool mismatchIsFailure) {
	if (!BotActions_ClientIndexValid(clientIndex) ||
		(actualWeaponItem <= 0 && !mismatchIsFailure)) {
		botActionStatus.weaponSwitchInvalidEvents++;
		BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
			BotWeaponSwitchProofEvent::Failure,
			clientIndex,
			0,
			actualWeaponItem);
		BotActions_RecordWeaponSwitchProofResult(result);
		return result;
	}

	BotPendingWeaponSwitchRequest &request = botPendingWeaponSwitchRequests[clientIndex];
	if (!request.active) {
		botActionStatus.weaponSwitchNoPendingEvents++;
		BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
			BotWeaponSwitchProofEvent::NoPendingRequest,
			clientIndex,
			0,
			actualWeaponItem);
		BotActions_RecordWeaponSwitchProofResult(result);
		return result;
	}

	BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
		actualWeaponItem == request.expectedWeaponItem ?
			BotWeaponSwitchProofEvent::Completion :
			(mismatchIsFailure ?
				(actualWeaponItem > 0 ? BotWeaponSwitchProofEvent::Mismatch : BotWeaponSwitchProofEvent::Failure) :
				BotWeaponSwitchProofEvent::PendingObservation),
		clientIndex,
		request.expectedWeaponItem,
		actualWeaponItem);
	result.valid = true;
	result.pending = actualWeaponItem != request.expectedWeaponItem && !mismatchIsFailure;
	result.completed = actualWeaponItem == request.expectedWeaponItem;
	result.failed = actualWeaponItem != request.expectedWeaponItem && mismatchIsFailure;

	if (result.completed) {
		request = {};
		botActionStatus.weaponSwitchCompletions++;
	} else if (result.failed) {
		request = {};
		botActionStatus.weaponSwitchFailures++;
		if (actualWeaponItem > 0) {
			botActionStatus.weaponSwitchMismatches++;
		}
	}

	BotActions_RecordWeaponSwitchProofResult(result);
	return result;
}

void BotActions_RecordApplicationResult(const BotActionApplyResult &result) {
	botActionStatus.applyAttempts++;
	botActionStatus.lastApplyFailure = result.failure;

	if (!result.accepted) {
		botActionStatus.rejectedApplications++;
		return;
	}

	botActionStatus.acceptedApplications++;
}

BotActionDecision BotActions_MakeMoveToItemDecision(const BotItemDecision &itemDecision) {
	if (itemDecision.kind != BotItemDecisionKind::SeekCandidate) {
		return {};
	}

	return {
		.intent = BotActionIntent::MoveToItem,
		.priority = itemDecision.priority,
		.item = itemDecision.item,
		.entity = itemDecision.entity,
		.reason = itemDecision.reason,
	};
}

BotActionDecision BotActions_MakeCombatDecision(const BotCombatDecision &combatDecision) {
	switch (combatDecision.kind) {
	case BotCombatDecisionKind::SwitchWeapon:
		return {
			.intent = BotActionIntent::SwitchWeapon,
			.priority = combatDecision.priority,
			.weaponItem = combatDecision.weaponItem,
			.wantsWeaponSwitch = true,
			.reason = combatDecision.reason,
		};
	case BotCombatDecisionKind::FireWeapon:
		return {
			.intent = BotActionIntent::Attack,
			.priority = combatDecision.priority,
			.weaponItem = combatDecision.weaponItem,
			.pressAttack = combatDecision.pressAttack,
			.reason = combatDecision.reason,
		};
	default:
		return {};
	}
}

BotActionDecision BotActions_HigherPriority(BotActionDecision current, const BotActionDecision &candidate) {
	if (BotActions_DecisionHasIntent(candidate) && candidate.priority > current.priority) {
		return candidate;
	}
	return current;
}

void BotActions_RecordDecision(const BotActionContext &context, const BotActionDecision &decision) {
	botActionStatus.lastClientIndex = context.clientIndex;
	botActionStatus.lastIntent = decision.intent;
	botActionStatus.lastPriority = decision.priority;
	botActionStatus.lastItem = decision.item;
	botActionStatus.lastEntity = decision.entity;
	botActionStatus.lastWeaponItem = decision.weaponItem;

	switch (decision.intent) {
	case BotActionIntent::MoveToItem:
		botActionStatus.moveToItemDecisions++;
		break;
	case BotActionIntent::SwitchWeapon:
		botActionStatus.weaponSwitchDecisions++;
		break;
	case BotActionIntent::Attack:
		botActionStatus.attackDecisions++;
		break;
	case BotActionIntent::UseWorld:
		botActionStatus.useWorldDecisions++;
		break;
	case BotActionIntent::UseInventory:
		botActionStatus.useInventoryDecisions++;
		break;
	default:
		botActionStatus.noopDecisions++;
		break;
	}
}
} // namespace

void BotActions_ResetStatus() {
	botActionStatus = {};
	botPendingWeaponSwitchRequests = {};
	BotItems_ResetStatus();
	BotCombat_ResetStatus();
}

BotActionContext BotActions_BuildContext(const gentity_t *bot) {
	BotActionContext context{};
	if (bot == nullptr || !bot->inUse || bot->client == nullptr) {
		return context;
	}

	const bool isBot = ((bot->svFlags & SVF_BOT) != 0) || bot->client->sess.is_a_bot;
	if (!isBot) {
		return context;
	}

	context.valid = true;
	context.alive = bot->health > 0 && !bot->deadFlag && !bot->client->eliminated;
	context.clientIndex = static_cast<int>(bot->s.number) - 1;
	context.health = bot->health;
	context.maxHealth = bot->maxHealth;
	context.armor = BotActions_ArmorValue(bot->client);

	context.item.health = context.health;
	context.item.maxHealth = context.maxHealth;
	context.item.armor = context.armor;
	context.item.lowHealth = context.maxHealth > 0 && context.health * 100 <= context.maxHealth * 45;
	context.item.lowArmor = context.armor < 25;

	const Item *currentWeapon = bot->client->pers.weapon;
	const Item *pendingWeapon = bot->client->weapon.pending;
	const Item *preferredWeapon = pendingWeapon != nullptr ? pendingWeapon : currentWeapon;

	context.combat.currentWeaponItem = currentWeapon != nullptr ? currentWeapon->id : IT_NULL;
	context.combat.currentWeaponAmmo = BotActions_WeaponAmmo(bot->client, currentWeapon);
	context.combat.currentWeaponReady = BotActions_WeaponReady(bot->client, currentWeapon);
	context.combat.preferredWeaponItem = preferredWeapon != nullptr ? preferredWeapon->id : IT_NULL;
	context.combat.preferredWeaponAmmo = BotActions_WeaponAmmo(bot->client, preferredWeapon);
	context.combat.preferredWeaponReady = BotActions_WeaponReady(bot->client, preferredWeapon);

	if (BotActions_EnemyAlive(bot->enemy)) {
		context.combat.hasEnemy = true;
		const Vector3 delta = bot->enemy->s.origin - bot->s.origin;
		context.combat.enemyDistanceSquared = BotActions_ClampDistanceSquared(delta.lengthSquared());
	}

	return context;
}

BotActionDecision BotActions_Decide(const BotActionContext &context) {
	botActionStatus.evaluations++;
	if (!context.valid) {
		botActionStatus.invalidContexts++;
		return {};
	}
	if (!context.alive) {
		botActionStatus.deadContexts++;
		return {};
	}

	botActionStatus.itemEvaluations++;
	const BotItemDecision itemDecision = BotItems_Evaluate(context.item);

	botActionStatus.combatEvaluations++;
	const BotCombatDecision combatDecision = BotCombat_Evaluate(context.combat);

	BotActionDecision decision = BotActions_MakeMoveToItemDecision(itemDecision);
	decision = BotActions_HigherPriority(decision, BotActions_MakeCombatDecision(combatDecision));

	if (context.useWorldRequested) {
		decision = BotActions_HigherPriority(decision, {
			.intent = BotActionIntent::UseWorld,
			.priority = BOT_ACTION_USE_WORLD_PRIORITY,
			.pressUse = true,
			.reason = "world_use",
		});
	}

	if (context.inventoryUseRequested && context.inventoryItem > 0) {
		decision = BotActions_HigherPriority(decision, {
			.intent = BotActionIntent::UseInventory,
			.clientIndex = context.clientIndex,
			.priority = BOT_ACTION_USE_INVENTORY_PRIORITY,
			.item = context.inventoryItem,
			.wantsInventoryUse = true,
			.reason = "inventory_use",
		});
	}

	decision.clientIndex = context.clientIndex;
	BotActions_RecordDecision(context, decision);
	return decision;
}

BotActionApplyResult BotActions_ApplyDecisionDetailed(const BotActionDecision &decision, usercmd_t *cmd) {
	BotActionApplyResult result{};
	result.failure = BotActions_ValidateApplicationDecision(decision);
	if (result.failure != BotActionApplyFailure::None) {
		BotActions_RecordApplicationResult(result);
		return result;
	}

	const bool needsCommand = decision.pressAttack || decision.pressUse;
	if (needsCommand && cmd == nullptr) {
		result.failure = BotActionApplyFailure::NullCommand;
		BotActions_RecordApplicationResult(result);
		return result;
	}

	result.accepted = true;
	if (decision.pressAttack) {
		cmd->buttons |= BUTTON_ATTACK;
		botActionStatus.appliedAttackButtons++;
		result.attackButtonApplied = true;
		result.commandMutated = true;
	}
	if (decision.pressUse) {
		cmd->buttons |= BUTTON_USE;
		botActionStatus.appliedUseButtons++;
		result.useButtonApplied = true;
		result.commandMutated = true;
	}
	if (decision.wantsWeaponSwitch) {
		botActionStatus.pendingWeaponSwitches++;
		result.pendingIntentAccepted = true;
		result.weaponSwitchPending = true;
		result.weaponSwitchItem = decision.weaponItem;
	}
	if (decision.wantsInventoryUse) {
		botActionStatus.pendingInventoryUses++;
		result.pendingIntentAccepted = true;
		result.inventoryUsePending = true;
		result.inventoryUseItem = decision.item;
	}
	if (result.commandMutated) {
		botActionStatus.appliedCommands++;
	}

	BotActions_RecordApplicationResult(result);
	return result;
}

bool BotActions_ApplyDecision(const BotActionDecision &decision, usercmd_t *cmd) {
	const BotActionApplyResult result = BotActions_ApplyDecisionDetailed(decision, cmd);
	return result.commandMutated;
}

BotActionApplyFailure BotActions_ValidateDecisionForApplication(const BotActionDecision &decision) {
	return BotActions_ValidateApplicationDecision(decision);
}

BotActionCommandRequest BotActions_BuildCommandRequest(const BotActionDecision &decision) {
	BotActionCommandRequest request{};
	request.kind = BotActions_CommandRequestKindForDecision(decision);
	request.clientIndex = decision.clientIndex;
	request.item = BotActions_CommandRequestItemForDecision(decision);
	request.argumentItem = request.item;
	request.reason = decision.reason != nullptr ? decision.reason : "none";
	request.failure = BotActions_ValidateCommandRequestDecision(decision);

	if (request.failure == BotActionCommandRequestFailure::None) {
		request.valid = true;
		request.exactItem = true;
		request.command = BOT_ACTION_USE_INDEX_ONLY_COMMAND;
	}

	BotActions_RecordCommandRequestResult(request);
	return request;
}

BotActionCommandRequestFailure BotActions_ValidateCommandRequest(const BotActionDecision &decision) {
	return BotActions_ValidateCommandRequestDecision(decision);
}

void BotActions_RecordCommandDispatch(
	const BotActionCommandRequest &request,
	BotActionCommandDispatchOutcome outcome,
	BotActionCommandDispatchFailure failure) {
	BotActions_RecordCommandDispatchResult(request, outcome, failure);
}

bool BotActions_IsWeaponSwitchDecision(const BotActionDecision &decision) {
	return decision.intent == BotActionIntent::SwitchWeapon &&
		BotActions_ValidateApplicationDecision(decision) == BotActionApplyFailure::None;
}

bool BotActions_RecordWeaponSwitchRequest(const BotActionDecision &decision) {
	const BotWeaponSwitchProofResult result =
		BotActions_RecordWeaponSwitchRequestDetailed(decision, 0);
	return result.valid;
}

void BotActions_RecordWeaponSwitchRequest(int expectedWeaponItem) {
	if (expectedWeaponItem <= 0) {
		botActionStatus.weaponSwitchInvalidEvents++;
		return;
	}

	botActionStatus.weaponSwitchRequests++;
	botActionStatus.weaponSwitchExpectedItem = expectedWeaponItem;
	botActionStatus.weaponSwitchActualItem = 0;
	botActionStatus.weaponSwitchExpectedMatch = 0;
	botActionStatus.lastWeaponSwitchEvent = BotWeaponSwitchProofEvent::RequestAccepted;
}

BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchRequestDetailed(
	const BotActionDecision &decision,
	int currentWeaponItem) {
	if (!BotActions_IsWeaponSwitchDecision(decision) ||
		!BotActions_ClientIndexValid(decision.clientIndex) ||
		currentWeaponItem == decision.weaponItem) {
		return BotActions_RecordRejectedWeaponSwitchRequest(decision, currentWeaponItem);
	}

	botActionStatus.weaponSwitchValidatedRequests++;
	BotPendingWeaponSwitchRequest &request =
		botPendingWeaponSwitchRequests[decision.clientIndex];
	if (request.active && request.expectedWeaponItem == decision.weaponItem) {
		botActionStatus.weaponSwitchDuplicateRequests++;
		BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
			BotWeaponSwitchProofEvent::DuplicateRequest,
			decision.clientIndex,
			decision.weaponItem,
			currentWeaponItem);
		result.valid = true;
		result.pending = true;
		BotActions_RecordWeaponSwitchProofResult(result);
		return result;
	}

	if (request.active) {
		botActionStatus.weaponSwitchDuplicateRequests++;
	}

	request.active = true;
	request.expectedWeaponItem = decision.weaponItem;
	request.previousWeaponItem = currentWeaponItem;
	botActionStatus.weaponSwitchRequests++;
	botActionStatus.weaponSwitchPreviousItem = currentWeaponItem;

	BotWeaponSwitchProofResult result = BotActions_MakeWeaponSwitchProofResult(
		BotWeaponSwitchProofEvent::RequestAccepted,
		decision.clientIndex,
		decision.weaponItem,
		currentWeaponItem);
	result.valid = true;
	result.pending = true;
	BotActions_RecordWeaponSwitchProofResult(result);
	return result;
}

BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchObservation(
	int clientIndex,
	int actualWeaponItem) {
	return BotActions_RecordPendingWeaponSwitchTerminal(clientIndex, actualWeaponItem, false);
}

bool BotActions_RecordWeaponSwitchCompletion(const BotActionDecision &decision, int actualWeaponItem) {
	if (!BotActions_IsWeaponSwitchDecision(decision)) {
		botActionStatus.weaponSwitchInvalidEvents++;
		return false;
	}

	BotWeaponSwitchProofResult result{};
	if (BotActions_ClientIndexValid(decision.clientIndex) &&
		botPendingWeaponSwitchRequests[decision.clientIndex].active) {
		result = BotActions_RecordWeaponSwitchCompletionObserved(decision.clientIndex, actualWeaponItem);
	} else {
		result = BotActions_RecordDirectWeaponSwitchCompletion(decision.weaponItem, actualWeaponItem);
	}
	return result.completed;
}

void BotActions_RecordWeaponSwitchCompletion(int expectedWeaponItem, int actualWeaponItem) {
	(void)BotActions_RecordDirectWeaponSwitchCompletion(expectedWeaponItem, actualWeaponItem);
}

BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchCompletionObserved(
	int clientIndex,
	int actualWeaponItem) {
	return BotActions_RecordPendingWeaponSwitchTerminal(clientIndex, actualWeaponItem, true);
}

bool BotActions_RecordWeaponSwitchFailure(const BotActionDecision &decision, int actualWeaponItem) {
	if (!BotActions_IsWeaponSwitchDecision(decision)) {
		botActionStatus.weaponSwitchInvalidEvents++;
		return false;
	}

	BotWeaponSwitchProofResult result{};
	if (BotActions_ClientIndexValid(decision.clientIndex) &&
		botPendingWeaponSwitchRequests[decision.clientIndex].active) {
		result = BotActions_RecordWeaponSwitchFailureObserved(decision.clientIndex, actualWeaponItem);
	} else {
		result = BotActions_RecordDirectWeaponSwitchFailure(decision.weaponItem, actualWeaponItem);
	}
	return result.failed;
}

void BotActions_RecordWeaponSwitchFailure(int expectedWeaponItem, int actualWeaponItem) {
	(void)BotActions_RecordDirectWeaponSwitchFailure(expectedWeaponItem, actualWeaponItem);
}

BotWeaponSwitchProofResult BotActions_RecordWeaponSwitchFailureObserved(
	int clientIndex,
	int actualWeaponItem) {
	return BotActions_RecordPendingWeaponSwitchTerminal(clientIndex, actualWeaponItem, true);
}

const BotActionStatus &BotActions_GetStatus() {
	return botActionStatus;
}

const char *BotActions_IntentName(BotActionIntent intent) {
	switch (intent) {
	case BotActionIntent::MoveToItem:
		return "move_to_item";
	case BotActionIntent::SwitchWeapon:
		return "switch_weapon";
	case BotActionIntent::Attack:
		return "attack";
	case BotActionIntent::UseWorld:
		return "use_world";
	case BotActionIntent::UseInventory:
		return "use_inventory";
	default:
		return "none";
	}
}

const char *BotActions_ApplyFailureName(BotActionApplyFailure failure) {
	switch (failure) {
	case BotActionApplyFailure::NullCommand:
		return "null_command";
	case BotActionApplyFailure::NoIntent:
		return "no_intent";
	case BotActionApplyFailure::NonPositivePriority:
		return "non_positive_priority";
	case BotActionApplyFailure::IntentFlagMismatch:
		return "intent_flag_mismatch";
	case BotActionApplyFailure::MissingWeaponItem:
		return "missing_weapon_item";
	case BotActionApplyFailure::MissingInventoryItem:
		return "missing_inventory_item";
	default:
		return "none";
	}
}

const char *BotActions_CommandRequestKindName(BotActionCommandRequestKind kind) {
	switch (kind) {
	case BotActionCommandRequestKind::UseWeaponIndex:
		return "use_weapon_index";
	case BotActionCommandRequestKind::UseInventoryIndex:
		return "use_inventory_index";
	default:
		return "none";
	}
}

const char *BotActions_CommandRequestFailureName(BotActionCommandRequestFailure failure) {
	switch (failure) {
	case BotActionCommandRequestFailure::NoIntent:
		return "no_intent";
	case BotActionCommandRequestFailure::NonPositivePriority:
		return "non_positive_priority";
	case BotActionCommandRequestFailure::IntentFlagMismatch:
		return "intent_flag_mismatch";
	case BotActionCommandRequestFailure::NotPendingCommandIntent:
		return "not_pending_command_intent";
	case BotActionCommandRequestFailure::InvalidClientIndex:
		return "invalid_client_index";
	case BotActionCommandRequestFailure::MissingWeaponItem:
		return "missing_weapon_item";
	case BotActionCommandRequestFailure::MissingInventoryItem:
		return "missing_inventory_item";
	case BotActionCommandRequestFailure::InvalidItemIndex:
		return "invalid_item_index";
	case BotActionCommandRequestFailure::UnknownItem:
		return "unknown_item";
	case BotActionCommandRequestFailure::ItemNotUsable:
		return "item_not_usable";
	case BotActionCommandRequestFailure::ItemNotWeapon:
		return "item_not_weapon";
	case BotActionCommandRequestFailure::InventoryItemIsWeapon:
		return "inventory_item_is_weapon";
	default:
		return "none";
	}
}

const char *BotActions_CommandDispatchOutcomeName(BotActionCommandDispatchOutcome outcome) {
	switch (outcome) {
	case BotActionCommandDispatchOutcome::Submitted:
		return "submitted";
	case BotActionCommandDispatchOutcome::Deferred:
		return "deferred";
	case BotActionCommandDispatchOutcome::Failed:
		return "failed";
	default:
		return "none";
	}
}

const char *BotActions_CommandDispatchFailureName(BotActionCommandDispatchFailure failure) {
	switch (failure) {
	case BotActionCommandDispatchFailure::InvalidRequest:
		return "invalid_request";
	case BotActionCommandDispatchFailure::InvalidClientIndex:
		return "invalid_client_index";
	case BotActionCommandDispatchFailure::ClientEntityUnavailable:
		return "client_entity_unavailable";
	case BotActionCommandDispatchFailure::NotBotClient:
		return "not_bot_client";
	case BotActionCommandDispatchFailure::InactiveClient:
		return "inactive_client";
	case BotActionCommandDispatchFailure::MissingItem:
		return "missing_item";
	case BotActionCommandDispatchFailure::MissingInventoryItem:
		return "missing_inventory_item";
	case BotActionCommandDispatchFailure::MissingUseCallback:
		return "missing_use_callback";
	case BotActionCommandDispatchFailure::UnsupportedCommand:
		return "unsupported_command";
	case BotActionCommandDispatchFailure::UnsupportedKind:
		return "unsupported_kind";
	default:
		return "none";
	}
}

const char *BotActions_WeaponSwitchProofEventName(BotWeaponSwitchProofEvent event) {
	switch (event) {
	case BotWeaponSwitchProofEvent::RequestAccepted:
		return "request_accepted";
	case BotWeaponSwitchProofEvent::RequestRejected:
		return "request_rejected";
	case BotWeaponSwitchProofEvent::DuplicateRequest:
		return "duplicate_request";
	case BotWeaponSwitchProofEvent::PendingObservation:
		return "pending_observation";
	case BotWeaponSwitchProofEvent::Completion:
		return "completion";
	case BotWeaponSwitchProofEvent::Failure:
		return "failure";
	case BotWeaponSwitchProofEvent::Mismatch:
		return "mismatch";
	case BotWeaponSwitchProofEvent::NoPendingRequest:
		return "no_pending_request";
	default:
		return "none";
	}
}
