// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "bot_items.hpp"

#include <algorithm>

namespace {
constexpr int BOT_ITEM_LOW_HEALTH_PRIORITY_BOOST = 350;
constexpr int BOT_ITEM_LOW_ARMOR_PRIORITY_BOOST = 65;
constexpr int BOT_ITEM_FOCUS_PRIORITY_BOOST = 120;
constexpr int BOT_ITEM_HIGH_VALUE_PRIORITY_BOOST = 250;
constexpr int BOT_ITEM_LOW_HEALTH_PERCENT = 45;
constexpr int BOT_ITEM_PROOF_HEALTH_PERCENT = 25;

constexpr int BOT_ITEM_GENERIC_PICKUP_SCORE = 180;
constexpr int BOT_ITEM_HEALTH_SCORE = 420;
constexpr int BOT_ITEM_ARMOR_SCORE = 520;
constexpr int BOT_ITEM_AMMO_SCORE = 320;
constexpr int BOT_ITEM_OWNED_WEAPON_SCORE = 360;
constexpr int BOT_ITEM_NEW_WEAPON_SCORE = 760;
constexpr int BOT_ITEM_POWERUP_SCORE = 850;

BotItemStatus botItemStatus;

static_assert(static_cast<int>(BotItemDecisionKind::None) == 0);
static_assert(static_cast<int>(BotItemUtilityKind::None) == 0);
static_assert(static_cast<int>(BotItemFocus::None) == 0);

char BotItems_LowerAscii(char value) {
	if (value >= 'A' && value <= 'Z') {
		return static_cast<char>(value - 'A' + 'a');
	}
	return value;
}

bool BotItems_StringEqualsNoCase(const char *value, const char *expected) {
	if (value == nullptr || expected == nullptr) {
		return false;
	}

	while (*value != '\0' && *expected != '\0') {
		if (BotItems_LowerAscii(*value) != BotItems_LowerAscii(*expected)) {
			return false;
		}
		value++;
		expected++;
	}

	return *value == '\0' && *expected == '\0';
}

item_id_t BotItems_CanonicalAmmoItem(item_id_t item) {
	switch (item) {
	case IT_AMMO_SHELLS_LARGE:
	case IT_AMMO_SHELLS_SMALL:
		return IT_AMMO_SHELLS;
	case IT_AMMO_BULLETS_LARGE:
	case IT_AMMO_BULLETS_SMALL:
		return IT_AMMO_BULLETS;
	case IT_AMMO_CELLS_LARGE:
	case IT_AMMO_CELLS_SMALL:
		return IT_AMMO_CELLS;
	case IT_AMMO_ROCKETS_SMALL:
		return IT_AMMO_ROCKETS;
	case IT_AMMO_SLUGS_LARGE:
	case IT_AMMO_SLUGS_SMALL:
		return IT_AMMO_SLUGS;
	default:
		return item;
	}
}

const Item *BotItems_ItemForId(int item) {
	if (item <= IT_NULL || item >= IT_TOTAL) {
		return nullptr;
	}
	return &itemList[static_cast<size_t>(item)];
}

bool BotItems_IsPowerArmorItem(const Item *item) {
	return item != nullptr &&
		(item->id == IT_POWER_SCREEN ||
		 item->id == IT_POWER_SHIELD ||
		 (item->flags & IF_POWER_ARMOR));
}

BotItemUtilityKind BotItems_ClassifyItem(const Item *item) {
	if (item == nullptr || item->id <= IT_NULL || item->id >= IT_TOTAL) {
		return BotItemUtilityKind::None;
	}

	const item_flags_t flags = item->flags;
	if (flags & IF_HEALTH) {
		return BotItemUtilityKind::Health;
	}
	if (BotItems_IsPowerArmorItem(item) || (flags & IF_ARMOR)) {
		return BotItemUtilityKind::Armor;
	}
	if (flags & IF_WEAPON) {
		return BotItemUtilityKind::Weapon;
	}
	if (flags & IF_AMMO) {
		return BotItemUtilityKind::Ammo;
	}
	if (flags & (IF_POWERUP | IF_SPHERE)) {
		return BotItemUtilityKind::Powerup;
	}
	if (flags & (IF_TIMED | IF_POWERUP_WHEEL | IF_TECH | IF_KEY)) {
		return BotItemUtilityKind::Pickup;
	}

	return BotItemUtilityKind::None;
}

int BotItems_ArmorValue(const gclient_t *client) {
	if (client == nullptr) {
		return 0;
	}

	const auto &inventory = client->pers.inventory;
	return std::max(
		std::max(inventory[IT_ARMOR_BODY], inventory[IT_ARMOR_COMBAT]),
		std::max(inventory[IT_ARMOR_JACKET], inventory[IT_ARMOR_SHARD]));
}

void BotItems_ClearArmorInventory(gclient_t *client) {
	if (client == nullptr) {
		return;
	}

	auto &inventory = client->pers.inventory;
	inventory[IT_ARMOR_BODY] = 0;
	inventory[IT_ARMOR_COMBAT] = 0;
	inventory[IT_ARMOR_JACKET] = 0;
	inventory[IT_ARMOR_SHARD] = 0;
	inventory[IT_POWER_SCREEN] = 0;
	inventory[IT_POWER_SHIELD] = 0;
}

int BotItems_ProofLowHealthValue(int maxHealth) {
	if (maxHealth <= 1) {
		return 1;
	}

	const int target = std::max(1, maxHealth * BOT_ITEM_PROOF_HEALTH_PERCENT / 100);
	const int lowHealthCeiling = std::max(1, maxHealth * BOT_ITEM_LOW_HEALTH_PERCENT / 100);
	return std::min(target, lowHealthCeiling);
}

bool BotItems_HealthIgnoresMax(const Item *item) {
	return item != nullptr && (item->tag & HEALTH_IGNORE_MAX) != 0;
}

int BotItems_HealthCap(const gentity_t *bot, const Item *item) {
	if (bot == nullptr) {
		return 0;
	}
	if (BotItems_HealthIgnoresMax(item)) {
		return std::max(250, bot->maxHealth + std::max(item != nullptr ? item->quantity : 0, 0));
	}
	return bot->maxHealth;
}

int BotItems_ArmorCap(const Item *item) {
	if (item == nullptr) {
		return 0;
	}
	if (BotItems_IsPowerArmorItem(item)) {
		return 1;
	}
	if (!(item->flags & IF_ARMOR) ||
		item->quantity < 0 ||
		item->quantity >= NUM_ARMOR_TYPES) {
		return 0;
	}

	return armor_stats[static_cast<int>(game.ruleset)][item->quantity].max_count;
}

int BotItems_CandidateQuantity(const Item *item, int candidateCount) {
	if (candidateCount > 0) {
		return candidateCount;
	}
	return item != nullptr ? item->quantity : 0;
}

void BotItems_RecordCandidateKind(BotItemUtilityKind kind) {
	switch (kind) {
	case BotItemUtilityKind::Health:
		botItemStatus.healthCandidates++;
		break;
	case BotItemUtilityKind::Armor:
		botItemStatus.armorCandidates++;
		break;
	case BotItemUtilityKind::Ammo:
		botItemStatus.ammoCandidates++;
		break;
	case BotItemUtilityKind::Weapon:
		botItemStatus.weaponCandidates++;
		break;
	case BotItemUtilityKind::Powerup:
		botItemStatus.powerupCandidates++;
		break;
	case BotItemUtilityKind::Pickup:
		botItemStatus.pickupCandidates++;
		break;
	default:
		break;
	}
}

void BotItems_RecordSeekKind(BotItemUtilityKind kind) {
	switch (kind) {
	case BotItemUtilityKind::Health:
		botItemStatus.healthSeekDecisions++;
		break;
	case BotItemUtilityKind::Armor:
		botItemStatus.armorSeekDecisions++;
		break;
	case BotItemUtilityKind::Ammo:
		botItemStatus.ammoSeekDecisions++;
		break;
	case BotItemUtilityKind::Weapon:
		botItemStatus.weaponSeekDecisions++;
		break;
	case BotItemUtilityKind::Powerup:
		botItemStatus.powerupSeekDecisions++;
		break;
	case BotItemUtilityKind::Pickup:
		botItemStatus.pickupSeekDecisions++;
		break;
	default:
		break;
	}
}

int BotItems_NeedScore(const BotItemContext &context) {
	if (context.maxAmount <= 0 || context.currentAmount < 0) {
		return 0;
	}

	const int missing = std::max(0, context.maxAmount - context.currentAmount);
	const int cappedMissing = std::min(missing, context.maxAmount);
	return std::min(180, cappedMissing * 180 / std::max(context.maxAmount, 1));
}

int BotItems_BaseUtilityScore(const BotItemContext &context) {
	switch (context.candidateKind) {
	case BotItemUtilityKind::Health:
		return BOT_ITEM_HEALTH_SCORE + BotItems_NeedScore(context);
	case BotItemUtilityKind::Armor:
		return BOT_ITEM_ARMOR_SCORE + BotItems_NeedScore(context);
	case BotItemUtilityKind::Ammo:
		return BOT_ITEM_AMMO_SCORE + BotItems_NeedScore(context);
	case BotItemUtilityKind::Weapon:
		return context.candidateAlreadyOwned ? BOT_ITEM_OWNED_WEAPON_SCORE : BOT_ITEM_NEW_WEAPON_SCORE;
	case BotItemUtilityKind::Powerup:
		return BOT_ITEM_POWERUP_SCORE;
	case BotItemUtilityKind::Pickup:
		return BOT_ITEM_GENERIC_PICKUP_SCORE;
	default:
		return context.candidateScore;
	}
}

const char *BotItems_BaseReason(BotItemUtilityKind kind) {
	switch (kind) {
	case BotItemUtilityKind::Health:
		return "health";
	case BotItemUtilityKind::Armor:
		return "armor";
	case BotItemUtilityKind::Ammo:
		return "ammo";
	case BotItemUtilityKind::Weapon:
		return "weapon";
	case BotItemUtilityKind::Powerup:
		return "powerup";
	case BotItemUtilityKind::Pickup:
		return "pickup";
	default:
		return "candidate";
	}
}

void BotItems_RecordGoalAssignmentKind(BotItemUtilityKind kind) {
	if (kind == BotItemUtilityKind::Health) {
		botItemStatus.itemHealthGoalAssignments++;
	} else if (kind == BotItemUtilityKind::Armor) {
		botItemStatus.itemArmorGoalAssignments++;
	}
}

void BotItems_RecordPickupKind(BotItemUtilityKind kind, int before, int after) {
	const int delta = after - before;
	if (kind == BotItemUtilityKind::Health) {
		botItemStatus.lastHealthBefore = before;
		botItemStatus.lastHealthAfter = after;
		botItemStatus.lastHealthPickupDelta = delta;
		if (delta > 0) {
			botItemStatus.itemHealthPickups++;
		}
	} else if (kind == BotItemUtilityKind::Armor) {
		botItemStatus.lastArmorBefore = before;
		botItemStatus.lastArmorAfter = after;
		botItemStatus.lastArmorPickupDelta = delta;
		if (delta > 0) {
			botItemStatus.itemArmorPickups++;
		}
	}
}
} // namespace

void BotItems_ResetStatus() {
	botItemStatus = {};
}

int BotItems_CurrentArmor(const gclient_t *client) {
	return BotItems_ArmorValue(client);
}

BotItemUtilityKind BotItems_ClassifyUtility(const Item *item) {
	return BotItems_ClassifyItem(item);
}

BotItemContext BotItems_BuildContextForEntity(const gentity_t *bot, const gentity_t *candidate, int candidateScore, bool candidateReserved, BotItemFocus focus) {
	const bool candidateAvailable = candidate != nullptr &&
		candidate->inUse &&
		candidate->item != nullptr &&
		candidate->solid != SOLID_NOT &&
		(candidate->svFlags & SVF_NOCLIENT) == 0;
	const int candidateEntity = candidate != nullptr ? static_cast<int>(candidate->s.number) : -1;
	const int candidateCount = candidate != nullptr ? candidate->count : 0;
	return BotItems_BuildContextForItem(
		bot,
		candidateAvailable ? candidate->item : nullptr,
		candidateEntity,
		candidateScore,
		candidateAvailable,
		candidateReserved,
		candidateCount,
		focus);
}

BotItemContext BotItems_BuildContextForItem(const gentity_t *bot, const Item *candidateItem, int candidateEntity, int candidateScore, bool candidateAvailable, bool candidateReserved, int candidateCount, BotItemFocus focus) {
	BotItemContext context{};
	context.candidateAvailable = candidateAvailable && candidateItem != nullptr;
	context.candidateReserved = candidateReserved;
	context.candidateEntity = candidateEntity;
	context.candidateItem = candidateItem != nullptr ? candidateItem->id : IT_NULL;
	context.candidateScore = candidateScore;
	context.candidateQuantity = BotItems_CandidateQuantity(candidateItem, candidateCount);
	context.candidateKind = BotItems_ClassifyItem(candidateItem);
	context.candidateHighValue = candidateItem != nullptr && candidateItem->highValue != HighValueItems::None;
	context.focus = focus;

	if (bot == nullptr || bot->client == nullptr) {
		return context;
	}

	const gclient_t *client = bot->client;
	context.health = bot->health;
	context.maxHealth = bot->maxHealth;
	context.armor = BotItems_ArmorValue(client);
	context.lowHealth = context.maxHealth > 0 && context.health * 100 <= context.maxHealth * BOT_ITEM_LOW_HEALTH_PERCENT;
	context.lowArmor = context.armor < 25;

	if (candidateItem == nullptr) {
		return context;
	}

	const auto &inventory = client->pers.inventory;
	switch (context.candidateKind) {
	case BotItemUtilityKind::Health:
		context.currentAmount = context.health;
		context.maxAmount = BotItems_HealthCap(bot, candidateItem);
		context.candidateUseful = context.maxAmount <= 0 || context.currentAmount < context.maxAmount;
		break;
	case BotItemUtilityKind::Armor:
		context.currentAmount = BotItems_IsPowerArmorItem(candidateItem) ? inventory[candidateItem->id] : context.armor;
		context.maxAmount = BotItems_ArmorCap(candidateItem);
		context.candidateAlreadyOwned = BotItems_IsPowerArmorItem(candidateItem) && context.currentAmount > 0;
		context.candidateUseful = !context.candidateAlreadyOwned && (context.maxAmount <= 0 || context.currentAmount < context.maxAmount);
		break;
	case BotItemUtilityKind::Ammo: {
		const item_id_t ammoItem = BotItems_CanonicalAmmoItem(candidateItem->id);
		context.currentAmount = inventory[ammoItem];
		if (candidateItem->tag >= static_cast<int>(AmmoID::Bullets) &&
			candidateItem->tag < static_cast<int>(AmmoID::_Total)) {
			context.maxAmount = client->pers.ammoMax[static_cast<size_t>(candidateItem->tag)];
		}
		context.candidateUseful =
			context.currentAmount != AMMO_INFINITE &&
			(context.maxAmount <= 0 || context.currentAmount < context.maxAmount);
		break;
	}
	case BotItemUtilityKind::Weapon:
		context.currentAmount = inventory[candidateItem->id];
		context.maxAmount = 1;
		context.candidateAlreadyOwned = context.currentAmount > 0;
		if (!context.candidateAlreadyOwned) {
			context.candidateUseful = true;
		} else if (candidateItem->ammo > IT_NULL && candidateItem->ammo < IT_TOTAL) {
			const Item *ammoItem = BotItems_ItemForId(candidateItem->ammo);
			context.currentAmount = inventory[candidateItem->ammo];
			context.maxAmount = (ammoItem != nullptr &&
				ammoItem->tag >= static_cast<int>(AmmoID::Bullets) &&
				ammoItem->tag < static_cast<int>(AmmoID::_Total))
				? client->pers.ammoMax[static_cast<size_t>(ammoItem->tag)]
				: 0;
			context.candidateUseful = context.maxAmount <= 0 || context.currentAmount < context.maxAmount;
		} else {
			context.candidateUseful = false;
		}
		break;
	case BotItemUtilityKind::Pickup:
		context.currentAmount = inventory[candidateItem->id];
		context.maxAmount = 1;
		context.candidateAlreadyOwned = context.currentAmount > 0;
		context.candidateUseful = !context.candidateAlreadyOwned || context.candidateHighValue;
		break;
	case BotItemUtilityKind::Powerup:
		context.currentAmount = inventory[candidateItem->id];
		context.maxAmount = 1;
		context.candidateAlreadyOwned = context.currentAmount > 0;
		context.candidateUseful = true;
		break;
	default:
		context.candidateUseful = context.candidateScore > 0;
		break;
	}

	return context;
}

bool BotItems_ApplyHealthArmorProofSetup(gentity_t *bot, BotItemHealthArmorProofSetup *setup) {
	BotItemHealthArmorProofSetup result{};
	if (bot == nullptr || bot->client == nullptr) {
		if (setup != nullptr) {
			*setup = result;
		}
		return false;
	}

	gclient_t *client = bot->client;
	result.healthBefore = bot->health;
	result.armorBefore = BotItems_ArmorValue(client);

	const int maxHealth = std::max(1, std::max(bot->maxHealth, client->pers.maxHealth));
	bot->maxHealth = maxHealth;
	client->pers.maxHealth = maxHealth;
	bot->health = BotItems_ProofLowHealthValue(maxHealth);
	client->pers.health = bot->health;
	client->pers.healthBonus = 0;
	BotItems_ClearArmorInventory(client);

	result.healthAfter = bot->health;
	result.armorAfter = BotItems_ArmorValue(client);
	result.applied = true;

	botItemStatus.healthArmorProofSetups++;
	botItemStatus.lastProofHealthBefore = result.healthBefore;
	botItemStatus.lastProofHealthAfter = result.healthAfter;
	botItemStatus.lastProofArmorBefore = result.armorBefore;
	botItemStatus.lastProofArmorAfter = result.armorAfter;

	if (setup != nullptr) {
		*setup = result;
	}
	return true;
}

BotItemPickupSnapshot BotItems_CapturePickupSnapshot(const gentity_t *bot, const Item *item, int entity) {
	BotItemPickupSnapshot snapshot{};
	if (bot == nullptr || bot->client == nullptr || item == nullptr) {
		return snapshot;
	}

	const BotItemUtilityKind kind = BotItems_ClassifyItem(item);
	if (kind != BotItemUtilityKind::Health && kind != BotItemUtilityKind::Armor) {
		return snapshot;
	}

	snapshot.valid = true;
	snapshot.utilityKind = kind;
	snapshot.item = item->id;
	snapshot.entity = entity;
	snapshot.health = bot->health;
	snapshot.armor = BotItems_ArmorValue(bot->client);
	return snapshot;
}

bool BotItems_RecordPickupObservation(const BotItemPickupSnapshot &snapshot, const gentity_t *bot) {
	botItemStatus.pickupObservationAttempts++;
	if (!snapshot.valid || bot == nullptr || bot->client == nullptr) {
		botItemStatus.pickupObservationNoDelta++;
		return false;
	}

	int before = 0;
	int after = 0;
	if (snapshot.utilityKind == BotItemUtilityKind::Health) {
		before = snapshot.health;
		after = bot->health;
	} else if (snapshot.utilityKind == BotItemUtilityKind::Armor) {
		before = snapshot.armor;
		after = BotItems_ArmorValue(bot->client);
	} else {
		botItemStatus.pickupObservationNoDelta++;
		return false;
	}

	if (after <= before) {
		botItemStatus.pickupObservationNoDelta++;
		return false;
	}

	BotItems_RecordPickupKind(snapshot.utilityKind, before, after);
	botItemStatus.pickupObservationRecords++;
	botItemStatus.lastItem = snapshot.item;
	botItemStatus.lastEntity = snapshot.entity;
	botItemStatus.lastUtilityKind = snapshot.utilityKind;
	return true;
}

BotItemDecision BotItems_Evaluate(const BotItemContext &context) {
	botItemStatus.evaluations++;

	if (!context.candidateAvailable || context.candidateEntity < 0 || context.candidateItem <= 0) {
		botItemStatus.invalidCandidates++;
		return {};
	}

	if (context.candidateReserved) {
		botItemStatus.reservedDeferrals++;
		return {};
	}

	BotItems_RecordCandidateKind(context.candidateKind);
	if (!context.candidateUseful) {
		botItemStatus.unneededCandidates++;
		return {};
	}
	botItemStatus.usefulCandidates++;

	int priority = BotItems_BaseUtilityScore(context);
	if (context.candidateKind != BotItemUtilityKind::None) {
		priority += std::max(0, context.candidateScore);
	}

	const char *reason = BotItems_BaseReason(context.candidateKind);
	if ((context.lowHealth && context.candidateKind == BotItemUtilityKind::Health) ||
		(context.lowHealth && context.candidateKind == BotItemUtilityKind::None)) {
		priority += BOT_ITEM_LOW_HEALTH_PRIORITY_BOOST;
		reason = "low_health";
		botItemStatus.lowHealthBoosts++;
	} else if ((context.lowArmor && context.candidateKind == BotItemUtilityKind::Armor) ||
		(context.lowArmor && context.candidateKind == BotItemUtilityKind::None)) {
		priority += BOT_ITEM_LOW_ARMOR_PRIORITY_BOOST;
		reason = "low_armor";
		botItemStatus.lowArmorBoosts++;
	}
	if (context.focus == BotItemFocus::Health && context.candidateKind == BotItemUtilityKind::Health) {
		priority += BOT_ITEM_FOCUS_PRIORITY_BOOST;
		reason = "focus_health";
		botItemStatus.focusHealthBoosts++;
	} else if (context.focus == BotItemFocus::Armor && context.candidateKind == BotItemUtilityKind::Armor) {
		priority += BOT_ITEM_FOCUS_PRIORITY_BOOST;
		reason = "focus_armor";
		botItemStatus.focusArmorBoosts++;
	}
	if (context.candidateHighValue) {
		priority += BOT_ITEM_HIGH_VALUE_PRIORITY_BOOST;
		botItemStatus.highValueBoosts++;
		if (context.focus == BotItemFocus::None) {
			reason = "high_value";
		}
	}

	if (priority <= 0) {
		botItemStatus.invalidCandidates++;
		return {};
	}

	botItemStatus.seekDecisions++;
	BotItems_RecordSeekKind(context.candidateKind);
	botItemStatus.lastItem = context.candidateItem;
	botItemStatus.lastEntity = context.candidateEntity;
	botItemStatus.lastPriority = priority;
	botItemStatus.lastUtilityKind = context.candidateKind;

	return {
		.kind = BotItemDecisionKind::SeekCandidate,
		.utilityKind = context.candidateKind,
		.priority = priority,
		.item = context.candidateItem,
		.entity = context.candidateEntity,
		.reason = reason,
	};
}

void BotItems_RecordGoalAssignment(const BotItemDecision &decision) {
	if (decision.kind != BotItemDecisionKind::SeekCandidate) {
		return;
	}

	BotItems_RecordGoalAssignmentKind(decision.utilityKind != BotItemUtilityKind::None
		? decision.utilityKind
		: BotItems_ClassifyItem(BotItems_ItemForId(decision.item)));
}

void BotItems_RecordGoalAssignment(int item) {
	BotItems_RecordGoalAssignmentKind(BotItems_ClassifyItem(BotItems_ItemForId(item)));
}

void BotItems_RecordPickup(int item, int before, int after) {
	BotItems_RecordPickupKind(BotItems_ClassifyItem(BotItems_ItemForId(item)), before, after);
}

void BotItems_RecordPickup(const Item *item, int before, int after) {
	BotItems_RecordPickupKind(BotItems_ClassifyItem(item), before, after);
}

const BotItemStatus &BotItems_GetStatus() {
	return botItemStatus;
}

const char *BotItems_DecisionName(BotItemDecisionKind kind) {
	switch (kind) {
	case BotItemDecisionKind::SeekCandidate:
		return "seek_candidate";
	default:
		return "none";
	}
}

const char *BotItems_UtilityKindName(BotItemUtilityKind kind) {
	switch (kind) {
	case BotItemUtilityKind::Health:
		return "health";
	case BotItemUtilityKind::Armor:
		return "armor";
	case BotItemUtilityKind::Ammo:
		return "ammo";
	case BotItemUtilityKind::Weapon:
		return "weapon";
	case BotItemUtilityKind::Powerup:
		return "powerup";
	case BotItemUtilityKind::Pickup:
		return "pickup";
	default:
		return "none";
	}
}

BotItemFocus BotItems_FocusFromString(const char *focus) {
	if (BotItems_StringEqualsNoCase(focus, "health")) {
		return BotItemFocus::Health;
	}
	if (BotItems_StringEqualsNoCase(focus, "armor")) {
		return BotItemFocus::Armor;
	}
	return BotItemFocus::None;
}

const char *BotItems_FocusName(BotItemFocus focus) {
	switch (focus) {
	case BotItemFocus::Health:
		return "health";
	case BotItemFocus::Armor:
		return "armor";
	default:
		return "none";
	}
}
