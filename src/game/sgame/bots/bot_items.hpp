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

enum class BotItemSpecialKind {
	None,
	DamageBoost,
	Protection,
	Invisibility,
	Mobility,
	Utility,
	Tech,
	CtfObjective,
};

enum class BotItemTimingPolicyReason {
	None,
	Invalid,
	TimersDisabled,
	UnobservedPickup,
	ExactTimer,
	FuzzedTimer,
};

enum class BotItemTimingConsumerReason {
	None,
	Invalid,
	LivePickup,
	TimerReady,
	TimerWaiting,
	TimerBlocked,
};

enum class BotItemFocus {
	None,
	Health,
	Armor,
	Ammo,
};

// Caller supplies candidate facts from the current item/nav owner. This module
// scores intent only; it does not discover, reserve, or clear item goals.
struct BotItemContext {
	bool candidateAvailable = false;
	bool candidateReserved = false;
	bool candidateUseful = true;
	bool candidateAlreadyOwned = false;
	bool candidateHighValue = false;
	bool candidateTech = false;
	bool candidateCtfObjective = false;
	bool lowHealth = false;
	bool lowArmor = false;
	BotItemUtilityKind candidateKind = BotItemUtilityKind::None;
	BotItemSpecialKind specialKind = BotItemSpecialKind::None;
	BotItemFocus focus = BotItemFocus::None;
	bool timingEvaluated = false;
	bool timingCandidateSelectable = false;
	bool timingKnown = false;
	bool timingWaitingForRespawn = false;
	bool timingFairnessBlocked = false;
	int timingEffectiveAvailableMilliseconds = 0;
	int timingRemainingMilliseconds = 0;
	int timingFuzzMilliseconds = 0;
	BotItemTimingPolicyReason timingPolicyReason = BotItemTimingPolicyReason::None;
	BotItemTimingConsumerReason timingConsumerReason = BotItemTimingConsumerReason::None;
	int candidateEntity = -1;
	int candidateSpawnCount = 0;
	int candidateItem = 0;
	int candidateScore = 0;
	int candidateQuantity = 0;
	int currentAmount = 0;
	int maxAmount = 0;
	int health = 0;
	int maxHealth = 0;
	int armor = 0;
};

struct BotItemTimingPolicyConfig {
	bool allowItemTimers = true;
	bool requireObservedPickup = true;
	int timerFuzzMilliseconds = 0;
};

struct BotItemTimingPolicyFrame {
	bool pickupObserved = false;
	int clientIndex = -1;
	int entity = -1;
	int spawnCount = 0;
	int item = 0;
	int observedPickupMilliseconds = 0;
	int expectedAvailableMilliseconds = 0;
	int currentMilliseconds = 0;
};

struct BotItemTimingPolicyResult {
	bool mayUseTimer = false;
	bool pickupWindowOpen = false;
	int effectiveAvailableMilliseconds = 0;
	int remainingMilliseconds = 0;
	int fuzzMilliseconds = 0;
	BotItemTimingPolicyReason reason = BotItemTimingPolicyReason::None;
};

struct BotItemTimingConsumerFrame {
	bool livePickup = false;
	bool pickupObserved = false;
	int clientIndex = -1;
	int entity = -1;
	int spawnCount = 0;
	int item = 0;
	int observedPickupMilliseconds = 0;
	int expectedAvailableMilliseconds = 0;
	int currentMilliseconds = 0;
};

struct BotItemTimingConsumerResult {
	bool evaluated = false;
	bool candidateSelectable = false;
	bool livePickup = false;
	bool timingKnown = false;
	bool waitingForRespawn = false;
	bool fairnessBlocked = false;
	int effectiveAvailableMilliseconds = 0;
	int remainingMilliseconds = 0;
	int fuzzMilliseconds = 0;
	BotItemTimingPolicyReason policyReason = BotItemTimingPolicyReason::None;
	BotItemTimingConsumerReason reason = BotItemTimingConsumerReason::None;
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
	int damageBoostCandidates = 0;
	int protectionCandidates = 0;
	int invisibilityCandidates = 0;
	int mobilityCandidates = 0;
	int utilityPowerupCandidates = 0;
	int techCandidates = 0;
	int ctfObjectiveCandidates = 0;
	int usefulCandidates = 0;
	int unneededCandidates = 0;
	int healthSeekDecisions = 0;
	int armorSeekDecisions = 0;
	int ammoSeekDecisions = 0;
	int weaponSeekDecisions = 0;
	int powerupSeekDecisions = 0;
	int pickupSeekDecisions = 0;
	int damageBoostSeekDecisions = 0;
	int protectionSeekDecisions = 0;
	int invisibilitySeekDecisions = 0;
	int mobilitySeekDecisions = 0;
	int utilityPowerupSeekDecisions = 0;
	int techSeekDecisions = 0;
	int ctfObjectiveSeekDecisions = 0;
	int specialUtilityBoosts = 0;
	int highValueBoosts = 0;
	int focusHealthBoosts = 0;
	int focusArmorBoosts = 0;
	int focusAmmoBoosts = 0;
	int itemHealthGoalAssignments = 0;
	int itemArmorGoalAssignments = 0;
	int itemAmmoGoalAssignments = 0;
	int itemWeaponGoalAssignments = 0;
	int itemHealthPickups = 0;
	int itemArmorPickups = 0;
	int healthArmorProofSetups = 0;
	int pickupObservationAttempts = 0;
	int pickupObservationRecords = 0;
	int pickupObservationNoDelta = 0;
	int timingPolicyEvaluations = 0;
	int timingPolicyInvalid = 0;
	int timingPolicyTimersDisabled = 0;
	int timingPolicyUnobservedBlocks = 0;
	int timingPolicyExactUses = 0;
	int timingPolicyFuzzedUses = 0;
	int timingPolicyReady = 0;
	int timingPolicyWaiting = 0;
	int timingConsumerEvaluations = 0;
	int timingConsumerInvalid = 0;
	int timingConsumerLivePickups = 0;
	int timingConsumerReady = 0;
	int timingConsumerWaiting = 0;
	int timingConsumerFairnessBlocks = 0;
	int timingConsumerSelectionDeferrals = 0;
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
	int lastTimingPolicyClient = -1;
	int lastTimingPolicyEntity = -1;
	int lastTimingPolicyItem = 0;
	int lastTimingPolicyFuzzMilliseconds = 0;
	int lastTimingPolicyEffectiveAvailableMilliseconds = 0;
	int lastTimingPolicyRemainingMilliseconds = 0;
	int lastTimingConsumerClient = -1;
	int lastTimingConsumerEntity = -1;
	int lastTimingConsumerItem = 0;
	int lastTimingConsumerFuzzMilliseconds = 0;
	int lastTimingConsumerEffectiveAvailableMilliseconds = 0;
	int lastTimingConsumerRemainingMilliseconds = 0;
	BotItemUtilityKind lastUtilityKind = BotItemUtilityKind::None;
	BotItemSpecialKind lastSpecialKind = BotItemSpecialKind::None;
	BotItemTimingPolicyReason lastTimingPolicyReason = BotItemTimingPolicyReason::None;
	BotItemTimingPolicyReason lastTimingConsumerPolicyReason = BotItemTimingPolicyReason::None;
	BotItemTimingConsumerReason lastTimingConsumerReason = BotItemTimingConsumerReason::None;
};

void BotItems_ResetStatus();
void BotItems_SetTimingPolicyConfig(const BotItemTimingPolicyConfig &config);
void BotItems_ResetTimingPolicyConfig();
BotItemTimingPolicyConfig BotItems_GetTimingPolicyConfig();
BotItemTimingPolicyResult BotItems_EvaluatePickupTimingPolicy(const BotItemTimingPolicyFrame &frame);
BotItemTimingConsumerFrame BotItems_BuildTimingConsumerFrameForEntity(const gentity_t *bot, const gentity_t *candidate, bool pickupObserved, int observedPickupMilliseconds = 0);
BotItemTimingConsumerResult BotItems_EvaluateTimingConsumer(const BotItemTimingConsumerFrame &frame);
BotItemContext BotItems_ApplyTimingConsumerResult(BotItemContext context, const BotItemTimingConsumerResult &timing);
int BotItems_CurrentArmor(const gclient_t *client);
BotItemUtilityKind BotItems_ClassifyUtility(const Item *item);
BotItemSpecialKind BotItems_ClassifySpecial(const Item *item);
bool BotItems_IsPowerArmorUtility(const Item *item);
BotItemContext BotItems_BuildContextForEntity(const gentity_t *bot, const gentity_t *candidate, int candidateScore, bool candidateReserved, BotItemFocus focus);
BotItemContext BotItems_BuildContextForTimedEntity(const gentity_t *bot, const gentity_t *candidate, int candidateScore, bool candidateReserved, BotItemFocus focus, bool pickupObserved, int observedPickupMilliseconds = 0);
BotItemContext BotItems_BuildContextForItem(const gentity_t *bot, const Item *candidateItem, int candidateEntity, int candidateScore, bool candidateAvailable, bool candidateReserved, int candidateCount, BotItemFocus focus, int candidateSpawnCount = 0);
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
const char *BotItems_SpecialKindName(BotItemSpecialKind kind);
const char *BotItems_TimingPolicyReasonName(BotItemTimingPolicyReason reason);
const char *BotItems_TimingConsumerReasonName(BotItemTimingConsumerReason reason);
BotItemFocus BotItems_FocusFromString(const char *focus);
const char *BotItems_FocusName(BotItemFocus focus);
