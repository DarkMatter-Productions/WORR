// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

struct gclient_t;
struct gentity_t;
struct Item;

enum class BotItemDecisionKind {
	None,
	SeekCandidate,
};

enum class BotItemUtilityKind {
	None,
	Health,
	Armor,
	Ammo,
	Weapon,
	Powerup,
	Pickup,
};

enum class BotItemFocus {
	None,
	Health,
	Armor,
};

// Caller supplies candidate facts from the current item/nav owner. This module
// scores intent only; it does not discover, reserve, or clear item goals.
struct BotItemContext {
	bool candidateAvailable = false;
	bool candidateReserved = false;
	bool candidateUseful = true;
	bool candidateAlreadyOwned = false;
	bool candidateHighValue = false;
	bool lowHealth = false;
	bool lowArmor = false;
	BotItemUtilityKind candidateKind = BotItemUtilityKind::None;
	BotItemFocus focus = BotItemFocus::None;
	int candidateEntity = -1;
	int candidateItem = 0;
	int candidateScore = 0;
	int candidateQuantity = 0;
	int currentAmount = 0;
	int maxAmount = 0;
	int health = 0;
	int maxHealth = 0;
	int armor = 0;
};

// SeekCandidate means "prefer moving toward this pickup" and does not mutate
// route ownership. The caller decides whether/how to translate it to nav input.
struct BotItemDecision {
	BotItemDecisionKind kind = BotItemDecisionKind::None;
	BotItemUtilityKind utilityKind = BotItemUtilityKind::None;
	int priority = 0;
	int item = 0;
	int entity = -1;
	const char *reason = "none";
};

struct BotItemHealthArmorProofSetup {
	bool applied = false;
	int healthBefore = 0;
	int healthAfter = 0;
	int armorBefore = 0;
	int armorAfter = 0;
};

struct BotItemPickupSnapshot {
	bool valid = false;
	BotItemUtilityKind utilityKind = BotItemUtilityKind::None;
	int item = 0;
	int entity = -1;
	int health = 0;
	int armor = 0;
};

// Process-local counters accumulate until BotItems_ResetStatus().
struct BotItemStatus {
	int evaluations = 0;
	int invalidCandidates = 0;
	int reservedDeferrals = 0;
	int seekDecisions = 0;
	int lowHealthBoosts = 0;
	int lowArmorBoosts = 0;
	int healthCandidates = 0;
	int armorCandidates = 0;
	int ammoCandidates = 0;
	int weaponCandidates = 0;
	int powerupCandidates = 0;
	int pickupCandidates = 0;
	int usefulCandidates = 0;
	int unneededCandidates = 0;
	int healthSeekDecisions = 0;
	int armorSeekDecisions = 0;
	int ammoSeekDecisions = 0;
	int weaponSeekDecisions = 0;
	int powerupSeekDecisions = 0;
	int pickupSeekDecisions = 0;
	int highValueBoosts = 0;
	int focusHealthBoosts = 0;
	int focusArmorBoosts = 0;
	int itemHealthGoalAssignments = 0;
	int itemArmorGoalAssignments = 0;
	int itemHealthPickups = 0;
	int itemArmorPickups = 0;
	int healthArmorProofSetups = 0;
	int pickupObservationAttempts = 0;
	int pickupObservationRecords = 0;
	int pickupObservationNoDelta = 0;
	int lastHealthBefore = 0;
	int lastHealthAfter = 0;
	int lastHealthPickupDelta = 0;
	int lastArmorBefore = 0;
	int lastArmorAfter = 0;
	int lastArmorPickupDelta = 0;
	int lastProofHealthBefore = 0;
	int lastProofHealthAfter = 0;
	int lastProofArmorBefore = 0;
	int lastProofArmorAfter = 0;
	int lastItem = 0;
	int lastEntity = -1;
	int lastPriority = 0;
	BotItemUtilityKind lastUtilityKind = BotItemUtilityKind::None;
};

void BotItems_ResetStatus();
int BotItems_CurrentArmor(const gclient_t *client);
BotItemUtilityKind BotItems_ClassifyUtility(const Item *item);
BotItemContext BotItems_BuildContextForEntity(const gentity_t *bot, const gentity_t *candidate, int candidateScore, bool candidateReserved, BotItemFocus focus);
BotItemContext BotItems_BuildContextForItem(const gentity_t *bot, const Item *candidateItem, int candidateEntity, int candidateScore, bool candidateAvailable, bool candidateReserved, int candidateCount, BotItemFocus focus);
BotItemDecision BotItems_Evaluate(const BotItemContext &context);
bool BotItems_ApplyHealthArmorProofSetup(gentity_t *bot, BotItemHealthArmorProofSetup *setup = nullptr);
BotItemPickupSnapshot BotItems_CapturePickupSnapshot(const gentity_t *bot, const Item *item, int entity = -1);
bool BotItems_RecordPickupObservation(const BotItemPickupSnapshot &snapshot, const gentity_t *bot);
void BotItems_RecordGoalAssignment(const BotItemDecision &decision);
void BotItems_RecordGoalAssignment(int item);
void BotItems_RecordPickup(int item, int before, int after);
void BotItems_RecordPickup(const Item *item, int before, int after);
const BotItemStatus &BotItems_GetStatus();
const char *BotItems_DecisionName(BotItemDecisionKind kind);
const char *BotItems_UtilityKindName(BotItemUtilityKind kind);
BotItemFocus BotItems_FocusFromString(const char *focus);
const char *BotItems_FocusName(BotItemFocus focus);
