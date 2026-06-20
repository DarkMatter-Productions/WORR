// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "../g_local.hpp"
#include "bot_items.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

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
constexpr int BOT_ITEM_DAMAGE_BOOST_PRIORITY_BOOST = 420;
constexpr int BOT_ITEM_PROTECTION_PRIORITY_BOOST = 320;
constexpr int BOT_ITEM_INVISIBILITY_PRIORITY_BOOST = 280;
constexpr int BOT_ITEM_MOBILITY_PRIORITY_BOOST = 210;
constexpr int BOT_ITEM_UTILITY_POWERUP_PRIORITY_BOOST = 160;
constexpr int BOT_ITEM_TECH_PRIORITY_BOOST = 260;
constexpr int BOT_ITEM_CTF_OBJECTIVE_PRIORITY_BOOST = 520;
constexpr int BOT_ITEM_TIMER_MAX_FUZZ_MILLISECONDS = 60000;

BotItemStatus botItemStatus;
BotItemTimingPolicyConfig botItemTimingPolicyConfig;
bool botItemTimingPolicyConfigOverride = false;

static_assert(static_cast<int>(BotItemDecisionKind::None) == 0);
static_assert(static_cast<int>(BotItemUtilityKind::None) == 0);
static_assert(static_cast<int>(BotItemSpecialKind::None) == 0);
static_assert(static_cast<int>(BotItemTimingPolicyReason::None) == 0);
static_assert(static_cast<int>(BotItemTimingConsumerReason::None) == 0);
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

BotItemTimingPolicyConfig BotItems_NormalizeTimingPolicyConfig(BotItemTimingPolicyConfig config) {
	config.timerFuzzMilliseconds = std::clamp(
		config.timerFuzzMilliseconds,
		0,
		BOT_ITEM_TIMER_MAX_FUZZ_MILLISECONDS);
	return config;
}

BotItemTimingPolicyConfig BotItems_TimingPolicyConfigFromCvars() {
	static cvar_t *allowItemTimers = nullptr;
	static cvar_t *timerFuzzMilliseconds = nullptr;
	if (allowItemTimers == nullptr && gi.cvar != nullptr) {
		allowItemTimers = gi.cvar("sg_bot_allow_item_timers", "1", CVAR_NOFLAGS);
	}
	if (timerFuzzMilliseconds == nullptr && gi.cvar != nullptr) {
		timerFuzzMilliseconds = gi.cvar("sg_bot_item_timer_fuzz_ms", "0", CVAR_NOFLAGS);
	}

	BotItemTimingPolicyConfig config{};
	config.allowItemTimers = allowItemTimers == nullptr || allowItemTimers->integer != 0;
	config.requireObservedPickup = true;
	config.timerFuzzMilliseconds =
		timerFuzzMilliseconds != nullptr ? timerFuzzMilliseconds->integer : 0;
	return BotItems_NormalizeTimingPolicyConfig(config);
}

BotItemTimingPolicyConfig BotItems_CurrentTimingPolicyConfig() {
	if (botItemTimingPolicyConfigOverride) {
		return botItemTimingPolicyConfig;
	}
	return BotItems_TimingPolicyConfigFromCvars();
}

uint32_t BotItems_MixTimingHash(uint32_t hash, int value) {
	uint32_t bits = static_cast<uint32_t>(value);
	for (int shift = 0; shift < 32; shift += 8) {
		hash ^= (bits >> shift) & 0xffu;
		hash *= 16777619u;
	}
	return hash;
}

uint32_t BotItems_TimingHash(const BotItemTimingPolicyFrame &frame) {
	uint32_t hash = 2166136261u;
	hash = BotItems_MixTimingHash(hash, frame.clientIndex);
	hash = BotItems_MixTimingHash(hash, frame.entity);
	hash = BotItems_MixTimingHash(hash, frame.spawnCount);
	hash = BotItems_MixTimingHash(hash, frame.item);
	hash = BotItems_MixTimingHash(hash, frame.observedPickupMilliseconds);
	hash = BotItems_MixTimingHash(hash, frame.expectedAvailableMilliseconds);
	return hash;
}

int BotItems_TimingFuzzMilliseconds(const BotItemTimingPolicyFrame &frame, int fuzzWindowMilliseconds) {
	if (fuzzWindowMilliseconds <= 0) {
		return 0;
	}

	const uint32_t range = static_cast<uint32_t>(fuzzWindowMilliseconds) * 2u + 1u;
	return static_cast<int>(BotItems_TimingHash(frame) % range) - fuzzWindowMilliseconds;
}

int BotItems_ClampTimingMilliseconds(int64_t milliseconds) {
	return static_cast<int>(std::clamp<int64_t>(
		milliseconds,
		0,
		std::numeric_limits<int>::max()));
}

void BotItems_RecordTimingPolicyResult(
	const BotItemTimingPolicyFrame &frame,
	const BotItemTimingPolicyResult &result) {
	botItemStatus.timingPolicyEvaluations++;
	botItemStatus.lastTimingPolicyClient = frame.clientIndex;
	botItemStatus.lastTimingPolicyEntity = frame.entity;
	botItemStatus.lastTimingPolicyItem = frame.item;
	botItemStatus.lastTimingPolicyFuzzMilliseconds = result.fuzzMilliseconds;
	botItemStatus.lastTimingPolicyEffectiveAvailableMilliseconds =
		result.effectiveAvailableMilliseconds;
	botItemStatus.lastTimingPolicyRemainingMilliseconds = result.remainingMilliseconds;
	botItemStatus.lastTimingPolicyReason = result.reason;

	switch (result.reason) {
	case BotItemTimingPolicyReason::Invalid:
		botItemStatus.timingPolicyInvalid++;
		break;
	case BotItemTimingPolicyReason::TimersDisabled:
		botItemStatus.timingPolicyTimersDisabled++;
		break;
	case BotItemTimingPolicyReason::UnobservedPickup:
		botItemStatus.timingPolicyUnobservedBlocks++;
		break;
	case BotItemTimingPolicyReason::ExactTimer:
		botItemStatus.timingPolicyExactUses++;
		break;
	case BotItemTimingPolicyReason::FuzzedTimer:
		botItemStatus.timingPolicyFuzzedUses++;
		break;
	default:
		break;
	}

	if (result.mayUseTimer) {
		if (result.pickupWindowOpen) {
			botItemStatus.timingPolicyReady++;
		} else {
			botItemStatus.timingPolicyWaiting++;
		}
	}
}

bool BotItems_IsLivePickupEntity(const gentity_t *candidate) {
	return candidate != nullptr &&
		candidate->inUse &&
		candidate->item != nullptr &&
		candidate->solid != SOLID_NOT &&
		(candidate->svFlags & SVF_NOCLIENT) == 0;
}

bool BotItems_IsRespawningPickupEntity(const gentity_t *candidate) {
	return candidate != nullptr &&
		candidate->inUse &&
		candidate->item != nullptr &&
		(candidate->flags & FL_RESPAWN) != 0 &&
		candidate->nextThink > 0_ms;
}

void BotItems_RecordTimingConsumerResult(
	const BotItemTimingConsumerFrame &frame,
	const BotItemTimingConsumerResult &result) {
	botItemStatus.timingConsumerEvaluations++;
	botItemStatus.lastTimingConsumerClient = frame.clientIndex;
	botItemStatus.lastTimingConsumerEntity = frame.entity;
	botItemStatus.lastTimingConsumerItem = frame.item;
	botItemStatus.lastTimingConsumerFuzzMilliseconds = result.fuzzMilliseconds;
	botItemStatus.lastTimingConsumerEffectiveAvailableMilliseconds =
		result.effectiveAvailableMilliseconds;
	botItemStatus.lastTimingConsumerRemainingMilliseconds = result.remainingMilliseconds;
	botItemStatus.lastTimingConsumerPolicyReason = result.policyReason;
	botItemStatus.lastTimingConsumerReason = result.reason;

	switch (result.reason) {
	case BotItemTimingConsumerReason::Invalid:
		botItemStatus.timingConsumerInvalid++;
		break;
	case BotItemTimingConsumerReason::LivePickup:
		botItemStatus.timingConsumerLivePickups++;
		break;
	case BotItemTimingConsumerReason::TimerReady:
		botItemStatus.timingConsumerReady++;
		break;
	case BotItemTimingConsumerReason::TimerWaiting:
		botItemStatus.timingConsumerWaiting++;
		break;
	case BotItemTimingConsumerReason::TimerBlocked:
		botItemStatus.timingConsumerFairnessBlocks++;
		break;
	default:
		break;
	}
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

bool BotItems_IsTechItem(const Item *item) {
	if (item == nullptr) {
		return false;
	}
	if (item->flags & IF_TECH) {
		return true;
	}
	for (const item_id_t techId : tech_ids) {
		if (item->id == techId) {
			return true;
		}
	}
	return false;
}

bool BotItems_ClientHasAnyTech(const gclient_t *client) {
	if (client == nullptr) {
		return false;
	}
	for (const item_id_t techId : tech_ids) {
		if (client->pers.inventory[techId] > 0) {
			return true;
		}
	}
	return false;
}

bool BotItems_IsCtfObjectiveItem(const Item *item) {
	return item != nullptr &&
		(item->id == IT_FLAG_RED ||
		 item->id == IT_FLAG_BLUE ||
		 item->id == IT_FLAG_NEUTRAL);
}

BotItemSpecialKind BotItems_ClassifySpecialItem(const Item *item) {
	if (item == nullptr) {
		return BotItemSpecialKind::None;
	}

	switch (item->id) {
	case IT_POWERUP_QUAD:
	case IT_POWERUP_DOUBLE:
	case IT_TECH_POWER_AMP:
		return BotItemSpecialKind::DamageBoost;
	case IT_POWERUP_BATTLESUIT:
	case IT_POWERUP_EMPATHY_SHIELD:
	case IT_POWERUP_SPAWN_PROTECTION:
	case IT_POWERUP_SPHERE_DEFENDER:
	case IT_TECH_DISRUPTOR_SHIELD:
		return BotItemSpecialKind::Protection;
	case IT_POWERUP_INVISIBILITY:
		return BotItemSpecialKind::Invisibility;
	case IT_POWERUP_HASTE:
	case IT_POWERUP_ANTIGRAV_BELT:
	case IT_TECH_TIME_ACCEL:
		return BotItemSpecialKind::Mobility;
	case IT_TECH_AUTODOC:
		return BotItemSpecialKind::Tech;
	case IT_FLAG_RED:
	case IT_FLAG_BLUE:
	case IT_FLAG_NEUTRAL:
		return BotItemSpecialKind::CtfObjective;
	case IT_POWERUP_SILENCER:
	case IT_POWERUP_REBREATHER:
	case IT_POWERUP_ENVIROSUIT:
	case IT_POWERUP_REGEN:
	case IT_POWERUP_SPHERE_VENGEANCE:
	case IT_POWERUP_SPHERE_HUNTER:
	case IT_TELEPORTER:
	case IT_DOPPELGANGER:
	case IT_IR_GOGGLES:
		return BotItemSpecialKind::Utility;
	default:
		break;
	}

	if (BotItems_IsTechItem(item)) {
		return BotItemSpecialKind::Tech;
	}
	if (BotItems_IsCtfObjectiveItem(item)) {
		return BotItemSpecialKind::CtfObjective;
	}
	return BotItemSpecialKind::None;
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

void BotItems_RecordSpecialCandidate(const BotItemContext &context) {
	switch (context.specialKind) {
	case BotItemSpecialKind::DamageBoost:
		botItemStatus.damageBoostCandidates++;
		break;
	case BotItemSpecialKind::Protection:
		botItemStatus.protectionCandidates++;
		break;
	case BotItemSpecialKind::Invisibility:
		botItemStatus.invisibilityCandidates++;
		break;
	case BotItemSpecialKind::Mobility:
		botItemStatus.mobilityCandidates++;
		break;
	case BotItemSpecialKind::Utility:
		botItemStatus.utilityPowerupCandidates++;
		break;
	case BotItemSpecialKind::Tech:
		break;
	case BotItemSpecialKind::CtfObjective:
		break;
	default:
		break;
	}
	if (context.candidateTech) {
		botItemStatus.techCandidates++;
	}
	if (context.candidateCtfObjective) {
		botItemStatus.ctfObjectiveCandidates++;
	}
}

void BotItems_RecordSpecialSeek(const BotItemContext &context) {
	switch (context.specialKind) {
	case BotItemSpecialKind::DamageBoost:
		botItemStatus.damageBoostSeekDecisions++;
		break;
	case BotItemSpecialKind::Protection:
		botItemStatus.protectionSeekDecisions++;
		break;
	case BotItemSpecialKind::Invisibility:
		botItemStatus.invisibilitySeekDecisions++;
		break;
	case BotItemSpecialKind::Mobility:
		botItemStatus.mobilitySeekDecisions++;
		break;
	case BotItemSpecialKind::Utility:
		botItemStatus.utilityPowerupSeekDecisions++;
		break;
	case BotItemSpecialKind::Tech:
		break;
	case BotItemSpecialKind::CtfObjective:
		break;
	default:
		break;
	}
	if (context.candidateTech) {
		botItemStatus.techSeekDecisions++;
	}
	if (context.candidateCtfObjective) {
		botItemStatus.ctfObjectiveSeekDecisions++;
	}
}

int BotItems_SpecialPriorityBoost(BotItemSpecialKind kind) {
	switch (kind) {
	case BotItemSpecialKind::DamageBoost:
		return BOT_ITEM_DAMAGE_BOOST_PRIORITY_BOOST;
	case BotItemSpecialKind::Protection:
		return BOT_ITEM_PROTECTION_PRIORITY_BOOST;
	case BotItemSpecialKind::Invisibility:
		return BOT_ITEM_INVISIBILITY_PRIORITY_BOOST;
	case BotItemSpecialKind::Mobility:
		return BOT_ITEM_MOBILITY_PRIORITY_BOOST;
	case BotItemSpecialKind::Utility:
		return BOT_ITEM_UTILITY_POWERUP_PRIORITY_BOOST;
	case BotItemSpecialKind::Tech:
		return BOT_ITEM_TECH_PRIORITY_BOOST;
	case BotItemSpecialKind::CtfObjective:
		return BOT_ITEM_CTF_OBJECTIVE_PRIORITY_BOOST;
	default:
		return 0;
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
		return BOT_ITEM_POWERUP_SCORE + BotItems_SpecialPriorityBoost(context.specialKind);
	case BotItemUtilityKind::Pickup:
		return BOT_ITEM_GENERIC_PICKUP_SCORE + BotItems_SpecialPriorityBoost(context.specialKind);
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

void BotItems_SetTimingPolicyConfig(const BotItemTimingPolicyConfig &config) {
	botItemTimingPolicyConfig = BotItems_NormalizeTimingPolicyConfig(config);
	botItemTimingPolicyConfigOverride = true;
}

void BotItems_ResetTimingPolicyConfig() {
	botItemTimingPolicyConfig = {};
	botItemTimingPolicyConfigOverride = false;
}

BotItemTimingPolicyConfig BotItems_GetTimingPolicyConfig() {
	return BotItems_CurrentTimingPolicyConfig();
}

BotItemTimingPolicyResult BotItems_EvaluatePickupTimingPolicy(const BotItemTimingPolicyFrame &frame) {
	const BotItemTimingPolicyConfig config = BotItems_CurrentTimingPolicyConfig();
	BotItemTimingPolicyResult result{};
	result.effectiveAvailableMilliseconds =
		BotItems_ClampTimingMilliseconds(frame.expectedAvailableMilliseconds);
	result.remainingMilliseconds = std::max(
		0,
		result.effectiveAvailableMilliseconds - frame.currentMilliseconds);

	auto finish = [&frame, &result](BotItemTimingPolicyReason reason) {
		result.reason = reason;
		BotItems_RecordTimingPolicyResult(frame, result);
		return result;
	};

	if (frame.item <= IT_NULL || frame.expectedAvailableMilliseconds <= 0) {
		return finish(BotItemTimingPolicyReason::Invalid);
	}
	if (!config.allowItemTimers) {
		return finish(BotItemTimingPolicyReason::TimersDisabled);
	}
	if (config.requireObservedPickup && !frame.pickupObserved) {
		return finish(BotItemTimingPolicyReason::UnobservedPickup);
	}

	result.fuzzMilliseconds = BotItems_TimingFuzzMilliseconds(
		frame,
		config.timerFuzzMilliseconds);
	result.effectiveAvailableMilliseconds = BotItems_ClampTimingMilliseconds(
		static_cast<int64_t>(frame.expectedAvailableMilliseconds) + result.fuzzMilliseconds);
	if (frame.observedPickupMilliseconds > 0) {
		result.effectiveAvailableMilliseconds =
			std::max(result.effectiveAvailableMilliseconds, frame.observedPickupMilliseconds);
	}
	result.remainingMilliseconds = std::max(
		0,
		result.effectiveAvailableMilliseconds - frame.currentMilliseconds);
	result.mayUseTimer = true;
	result.pickupWindowOpen = frame.currentMilliseconds >= result.effectiveAvailableMilliseconds;

	return finish(config.timerFuzzMilliseconds > 0 ?
		BotItemTimingPolicyReason::FuzzedTimer :
		BotItemTimingPolicyReason::ExactTimer);
}

BotItemTimingConsumerFrame BotItems_BuildTimingConsumerFrameForEntity(
	const gentity_t *bot,
	const gentity_t *candidate,
	bool pickupObserved,
	int observedPickupMilliseconds) {
	const int currentMilliseconds =
		BotItems_ClampTimingMilliseconds(level.time.milliseconds());
	BotItemTimingConsumerFrame frame{};
	frame.livePickup = BotItems_IsLivePickupEntity(candidate);
	frame.pickupObserved = pickupObserved;
	frame.clientIndex = bot != nullptr ? static_cast<int>(bot->s.number) : -1;
	frame.entity = candidate != nullptr ? static_cast<int>(candidate->s.number) : -1;
	frame.spawnCount = candidate != nullptr ? candidate->spawn_count : 0;
	frame.item = candidate != nullptr && candidate->item != nullptr ? candidate->item->id : IT_NULL;
	frame.observedPickupMilliseconds =
		BotItems_ClampTimingMilliseconds(observedPickupMilliseconds);
	frame.currentMilliseconds = currentMilliseconds;

	if (frame.livePickup) {
		frame.expectedAvailableMilliseconds = currentMilliseconds;
	} else if (BotItems_IsRespawningPickupEntity(candidate)) {
		frame.expectedAvailableMilliseconds =
			BotItems_ClampTimingMilliseconds(candidate->nextThink.milliseconds());
	}

	return frame;
}

BotItemTimingConsumerResult BotItems_EvaluateTimingConsumer(
	const BotItemTimingConsumerFrame &frame) {
	BotItemTimingConsumerResult result{};
	result.evaluated = true;

	auto finish = [&frame, &result](BotItemTimingConsumerReason reason) {
		result.reason = reason;
		BotItems_RecordTimingConsumerResult(frame, result);
		return result;
	};

	if (frame.item <= IT_NULL || frame.item >= IT_TOTAL || frame.entity < 0) {
		return finish(BotItemTimingConsumerReason::Invalid);
	}

	if (frame.livePickup) {
		result.candidateSelectable = true;
		result.livePickup = true;
		result.effectiveAvailableMilliseconds =
			BotItems_ClampTimingMilliseconds(frame.currentMilliseconds);
		return finish(BotItemTimingConsumerReason::LivePickup);
	}

	if (frame.expectedAvailableMilliseconds <= 0) {
		return finish(BotItemTimingConsumerReason::Invalid);
	}

	const BotItemTimingPolicyResult policy = BotItems_EvaluatePickupTimingPolicy({
		.pickupObserved = frame.pickupObserved,
		.clientIndex = frame.clientIndex,
		.entity = frame.entity,
		.spawnCount = frame.spawnCount,
		.item = frame.item,
		.observedPickupMilliseconds = frame.observedPickupMilliseconds,
		.expectedAvailableMilliseconds = frame.expectedAvailableMilliseconds,
		.currentMilliseconds = frame.currentMilliseconds,
	});
	result.timingKnown = policy.mayUseTimer;
	result.effectiveAvailableMilliseconds = policy.effectiveAvailableMilliseconds;
	result.remainingMilliseconds = policy.remainingMilliseconds;
	result.fuzzMilliseconds = policy.fuzzMilliseconds;
	result.policyReason = policy.reason;

	if (!policy.mayUseTimer) {
		result.fairnessBlocked = true;
		return finish(BotItemTimingConsumerReason::TimerBlocked);
	}

	result.candidateSelectable = policy.pickupWindowOpen;
	result.waitingForRespawn = !policy.pickupWindowOpen;
	return finish(policy.pickupWindowOpen ?
		BotItemTimingConsumerReason::TimerReady :
		BotItemTimingConsumerReason::TimerWaiting);
}

BotItemContext BotItems_ApplyTimingConsumerResult(
	BotItemContext context,
	const BotItemTimingConsumerResult &timing) {
	context.timingEvaluated = timing.evaluated;
	context.timingCandidateSelectable = timing.candidateSelectable;
	context.timingKnown = timing.timingKnown;
	context.timingWaitingForRespawn = timing.waitingForRespawn;
	context.timingFairnessBlocked = timing.fairnessBlocked;
	context.timingEffectiveAvailableMilliseconds =
		timing.effectiveAvailableMilliseconds;
	context.timingRemainingMilliseconds = timing.remainingMilliseconds;
	context.timingFuzzMilliseconds = timing.fuzzMilliseconds;
	context.timingPolicyReason = timing.policyReason;
	context.timingConsumerReason = timing.reason;

	if (timing.evaluated) {
		context.candidateAvailable = timing.candidateSelectable;
	}
	return context;
}

int BotItems_CurrentArmor(const gclient_t *client) {
	return BotItems_ArmorValue(client);
}

BotItemUtilityKind BotItems_ClassifyUtility(const Item *item) {
	return BotItems_ClassifyItem(item);
}

BotItemContext BotItems_BuildContextForEntity(const gentity_t *bot, const gentity_t *candidate, int candidateScore, bool candidateReserved, BotItemFocus focus) {
	return BotItems_BuildContextForTimedEntity(
		bot,
		candidate,
		candidateScore,
		candidateReserved,
		focus,
		false);
}

BotItemContext BotItems_BuildContextForTimedEntity(
	const gentity_t *bot,
	const gentity_t *candidate,
	int candidateScore,
	bool candidateReserved,
	BotItemFocus focus,
	bool pickupObserved,
	int observedPickupMilliseconds) {
	const int candidateEntity = candidate != nullptr ? static_cast<int>(candidate->s.number) : -1;
	const int candidateCount = candidate != nullptr ? candidate->count : 0;
	const int candidateSpawnCount = candidate != nullptr ? candidate->spawn_count : 0;
	const Item *candidateItem = candidate != nullptr && candidate->inUse ? candidate->item : nullptr;
	if (candidateItem == nullptr) {
		return BotItems_BuildContextForItem(
			bot,
			nullptr,
			candidateEntity,
			candidateScore,
			false,
			candidateReserved,
			candidateCount,
			focus,
			candidateSpawnCount);
	}

	const BotItemTimingConsumerResult timing = BotItems_EvaluateTimingConsumer(
		BotItems_BuildTimingConsumerFrameForEntity(
			bot,
			candidate,
			pickupObserved,
			observedPickupMilliseconds));
	BotItemContext context = BotItems_BuildContextForItem(
		bot,
		candidateItem,
		candidateEntity,
		candidateScore,
		timing.candidateSelectable,
		candidateReserved,
		candidateCount,
		focus,
		candidateSpawnCount);
	return BotItems_ApplyTimingConsumerResult(context, timing);
}

BotItemContext BotItems_BuildContextForItem(const gentity_t *bot, const Item *candidateItem, int candidateEntity, int candidateScore, bool candidateAvailable, bool candidateReserved, int candidateCount, BotItemFocus focus, int candidateSpawnCount) {
	BotItemContext context{};
	context.candidateAvailable = candidateAvailable && candidateItem != nullptr;
	context.candidateReserved = candidateReserved;
	context.candidateEntity = candidateEntity;
	context.candidateSpawnCount = candidateSpawnCount;
	context.candidateItem = candidateItem != nullptr ? candidateItem->id : IT_NULL;
	context.candidateScore = candidateScore;
	context.candidateQuantity = BotItems_CandidateQuantity(candidateItem, candidateCount);
	context.candidateKind = BotItems_ClassifyItem(candidateItem);
	context.specialKind = BotItems_ClassifySpecialItem(candidateItem);
	context.candidateHighValue = candidateItem != nullptr && candidateItem->highValue != HighValueItems::None;
	context.candidateTech = BotItems_IsTechItem(candidateItem);
	context.candidateCtfObjective = BotItems_IsCtfObjectiveItem(candidateItem);
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
		context.candidateAlreadyOwned =
			context.currentAmount > 0 ||
			(context.candidateTech && BotItems_ClientHasAnyTech(client));
		context.candidateUseful =
			context.candidateCtfObjective ||
			!context.candidateAlreadyOwned ||
			context.candidateHighValue;
		break;
	case BotItemUtilityKind::Powerup:
		context.currentAmount = inventory[candidateItem->id];
		context.maxAmount = 1;
		context.candidateAlreadyOwned = context.currentAmount > 0;
		if (const std::optional<PowerupTimer> timer = PowerupTimerForItem(candidateItem->id)) {
			const GameTime powerupAvailableTime = client->PowerupTimer(*timer);
			if (powerupAvailableTime > 0_ms) {
				const BotItemTimingPolicyResult timingPolicy = BotItems_EvaluatePickupTimingPolicy({
					.pickupObserved = true,
					.clientIndex = bot != nullptr ? static_cast<int>(bot->s.number) : -1,
					.entity = candidateEntity,
					.spawnCount = candidateSpawnCount,
					.item = candidateItem->id,
					.observedPickupMilliseconds = 0,
					.expectedAvailableMilliseconds =
						BotItems_ClampTimingMilliseconds(powerupAvailableTime.milliseconds()),
					.currentMilliseconds =
						BotItems_ClampTimingMilliseconds(level.time.milliseconds()),
				});
				context.candidateAlreadyOwned =
					context.candidateAlreadyOwned ||
					(timingPolicy.mayUseTimer && !timingPolicy.pickupWindowOpen);
			}
		}
		if (const std::optional<PowerupCount> counter = PowerupCountForItem(candidateItem->id)) {
			context.candidateAlreadyOwned = context.candidateAlreadyOwned || client->PowerupCount(*counter) > 0;
		}
		context.candidateUseful = !context.candidateAlreadyOwned || context.candidateHighValue;
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

	if (context.timingEvaluated && !context.timingCandidateSelectable) {
		botItemStatus.timingConsumerSelectionDeferrals++;
		return {};
	}

	const bool candidateAvailable =
		context.candidateAvailable ||
		(context.timingEvaluated && context.timingCandidateSelectable);
	if (!candidateAvailable || context.candidateEntity < 0 || context.candidateItem <= 0) {
		botItemStatus.invalidCandidates++;
		return {};
	}

	if (context.candidateReserved) {
		botItemStatus.reservedDeferrals++;
		return {};
	}

	BotItems_RecordCandidateKind(context.candidateKind);
	BotItems_RecordSpecialCandidate(context);
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
	if (context.specialKind != BotItemSpecialKind::None) {
		botItemStatus.specialUtilityBoosts++;
		if (context.focus == BotItemFocus::None && !context.candidateHighValue) {
			reason = BotItems_SpecialKindName(context.specialKind);
		}
	}

	if (priority <= 0) {
		botItemStatus.invalidCandidates++;
		return {};
	}

	botItemStatus.seekDecisions++;
	BotItems_RecordSeekKind(context.candidateKind);
	BotItems_RecordSpecialSeek(context);
	botItemStatus.lastItem = context.candidateItem;
	botItemStatus.lastEntity = context.candidateEntity;
	botItemStatus.lastPriority = priority;
	botItemStatus.lastUtilityKind = context.candidateKind;
	botItemStatus.lastSpecialKind = context.specialKind;

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

const char *BotItems_SpecialKindName(BotItemSpecialKind kind) {
	switch (kind) {
	case BotItemSpecialKind::DamageBoost:
		return "damage_boost";
	case BotItemSpecialKind::Protection:
		return "protection";
	case BotItemSpecialKind::Invisibility:
		return "invisibility";
	case BotItemSpecialKind::Mobility:
		return "mobility";
	case BotItemSpecialKind::Utility:
		return "utility";
	case BotItemSpecialKind::Tech:
		return "tech";
	case BotItemSpecialKind::CtfObjective:
		return "ctf_objective";
	default:
		return "none";
	}
}

const char *BotItems_TimingPolicyReasonName(BotItemTimingPolicyReason reason) {
	switch (reason) {
	case BotItemTimingPolicyReason::Invalid:
		return "invalid";
	case BotItemTimingPolicyReason::TimersDisabled:
		return "timers_disabled";
	case BotItemTimingPolicyReason::UnobservedPickup:
		return "unobserved_pickup";
	case BotItemTimingPolicyReason::ExactTimer:
		return "exact_timer";
	case BotItemTimingPolicyReason::FuzzedTimer:
		return "fuzzed_timer";
	default:
		return "none";
	}
}

const char *BotItems_TimingConsumerReasonName(BotItemTimingConsumerReason reason) {
	switch (reason) {
	case BotItemTimingConsumerReason::Invalid:
		return "invalid";
	case BotItemTimingConsumerReason::LivePickup:
		return "live_pickup";
	case BotItemTimingConsumerReason::TimerReady:
		return "timer_ready";
	case BotItemTimingConsumerReason::TimerWaiting:
		return "timer_waiting";
	case BotItemTimingConsumerReason::TimerBlocked:
		return "timer_blocked";
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
