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
constexpr int BOT_ACTION_INVENTORY_POLICY_PRIORITY = 79;
constexpr float BOT_ACTION_DOPPELGANGER_CREATE_DISTANCE = 48.0f;
constexpr float BOT_ACTION_DOPPELGANGER_SPAWN_CLEARANCE = 32.0f;
constexpr float BOT_ACTION_DOPPELGANGER_GROUND_HEIGHT = 64.0f;
constexpr int BOT_ACTION_DOPPELGANGER_MIN_DISTANCE_SQUARED = 128 * 128;
constexpr int BOT_ACTION_TELEPORTER_CLOSE_THREAT_DISTANCE_SQUARED = 512 * 512;
constexpr int BOT_ACTION_TELEPORTER_CRITICAL_HEALTH = 35;
constexpr int BOT_ACTION_NUKE_KILL_RADIUS = 512;
constexpr int BOT_ACTION_NUKE_FALLOFF_RADIUS = BOT_ACTION_NUKE_KILL_RADIUS * 2;
constexpr int BOT_ACTION_NUKE_MIN_TARGET_DISTANCE_SQUARED = 768 * 768;
constexpr int BOT_ACTION_NUKE_MAX_TARGET_DISTANCE_SQUARED = 2048 * 2048;
constexpr int BOT_ACTION_NUKE_LAUNCH_CLEAR_DISTANCE = 256;
constexpr int BOT_ACTION_NUKE_MIN_ENEMY_VALUE = 120;
constexpr const char *BOT_ACTION_USE_INDEX_ONLY_COMMAND = "use_index_only";

BotActionStatus botActionStatus;

struct BotPendingWeaponSwitchRequest {
	bool active = false;
	int expectedWeaponItem = 0;
	int previousWeaponItem = 0;
};

struct BotInventoryUseScore {
	int item = 0;
	int score = 0;
	int priority = 0;
	bool combatUse = false;
	bool survivalUse = false;
	bool utilityUse = false;
	bool environmentUse = false;
	bool deployableUse = false;
	bool escapeUse = false;
	bool powerArmorUse = false;
	bool alreadyActive = false;
	bool noCells = false;
	bool ownedSphere = false;
	bool placementChecked = false;
	bool placementBlocked = false;
	bool nukeUse = false;
	bool nukeSafetyChecked = false;
	bool nukeDeferred = false;
	bool nukeFriendlyBlocked = false;
	bool nukeSelfBlocked = false;
	BotItemSpecialKind specialKind = BotItemSpecialKind::None;
	const char *reason = "unsupported";
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

bool BotActions_CarriedWeaponCandidate(const gclient_t *client, const Item *item) {
	if (client == nullptr || item == nullptr) {
		return false;
	}
	const int itemId = static_cast<int>(item->id);
	if (itemId <= static_cast<int>(IT_NULL) || itemId >= static_cast<int>(IT_TOTAL)) {
		return false;
	}
	if (client->pers.inventory[item->id] <= 0) {
		return false;
	}
	return BotCombat_GetWeaponMetadata(itemId) != nullptr ||
		(item->flags & IF_WEAPON) != 0;
}

int BotActions_RangeRank(BotWeaponRangeBand band) {
	switch (band) {
	case BotWeaponRangeBand::Melee:
		return 0;
	case BotWeaponRangeBand::Close:
		return 1;
	case BotWeaponRangeBand::Medium:
		return 2;
	case BotWeaponRangeBand::Long:
		return 3;
	default:
		return -1;
	}
}

bool BotActions_RangeSupportsWeapon(
	BotWeaponRangeBand range,
	const BotWeaponMetadata &metadata) {
	const int actual = BotActions_RangeRank(range);
	const int minimum = BotActions_RangeRank(metadata.minimumRange);
	const int maximum = BotActions_RangeRank(metadata.maximumRange);
	return actual >= 0 && minimum >= 0 && maximum >= 0 &&
		actual >= minimum && actual <= maximum;
}

bool BotActions_WeaponSelfDamageUnsafe(
	const BotWeaponMetadata &metadata,
	const BotCombatContext &context) {
	return metadata.selfDamageRisk &&
		metadata.selfDamageSafetyDistanceSquared > 0 &&
		context.enemyDistanceSquared > 0 &&
		context.enemyDistanceSquared <= metadata.selfDamageSafetyDistanceSquared;
}

bool BotActions_WeaponAmmoInsufficient(
	const BotWeaponMetadata &metadata,
	int ammo) {
	return metadata.ammoPerShot > 0 && ammo < metadata.ammoPerShot;
}

bool BotActions_CarriedInventoryCandidate(const gclient_t *client, const Item *item) {
	if (client == nullptr || item == nullptr) {
		return false;
	}
	const int itemId = static_cast<int>(item->id);
	if (itemId <= static_cast<int>(IT_NULL) || itemId >= static_cast<int>(IT_TOTAL)) {
		return false;
	}
	if (client->pers.inventory[item->id] <= 0) {
		return false;
	}
	if (item->use == nullptr) {
		return false;
	}
	return BotCombat_GetWeaponMetadata(itemId) == nullptr &&
		(item->flags & IF_WEAPON) == 0;
}

bool BotActions_PowerupTimerActive(const gclient_t *client, const Item *item) {
	if (client == nullptr || item == nullptr) {
		return false;
	}
	const auto timer = PowerupTimerForItem(item->id);
	return timer && client->PowerupTimer(*timer) > level.time;
}

bool BotActions_PowerupCountActive(const gclient_t *client, const Item *item) {
	if (client == nullptr || item == nullptr) {
		return false;
	}
	const auto counter = PowerupCountForItem(item->id);
	return counter && client->PowerupCount(*counter) > 0;
}

bool BotActions_InventoryEffectActive(const gclient_t *client, const Item *item) {
	return BotActions_PowerupTimerActive(client, item) ||
		BotActions_PowerupCountActive(client, item);
}

bool BotActions_InHazardVolume(const gentity_t *bot) {
	return bot != nullptr &&
		bot->waterLevel > WATER_NONE &&
		(bot->waterType & (CONTENTS_LAVA | CONTENTS_SLIME));
}

bool BotActions_Underwater(const gentity_t *bot) {
	return bot != nullptr &&
		bot->waterLevel >= WATER_UNDER &&
		(bot->waterType & (CONTENTS_WATER | CONTENTS_LAVA | CONTENTS_SLIME));
}

bool BotActions_CarryingCtfObjective(const gclient_t *client) {
	return client != nullptr &&
		(client->pers.inventory[IT_FLAG_RED] > 0 ||
		 client->pers.inventory[IT_FLAG_BLUE] > 0 ||
		 client->pers.inventory[IT_FLAG_NEUTRAL] > 0);
}

bool BotActions_DoppelgangerPlacementReady(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr) {
		return false;
	}

	Vector3 forward;
	const Vector3 angles = { 0.0f, bot->client->vAngle[YAW], 0.0f };
	AngleVectors(angles, forward, nullptr, nullptr);

	const Vector3 createPoint =
		bot->s.origin + (forward * BOT_ACTION_DOPPELGANGER_CREATE_DISTANCE);
	Vector3 spawnPoint;
	if (!FindSpawnPoint(
		createPoint,
		bot->mins,
		bot->maxs,
		spawnPoint,
		BOT_ACTION_DOPPELGANGER_SPAWN_CLEARANCE)) {
		return false;
	}
	return CheckGroundSpawnPoint(
		spawnPoint,
		bot->mins,
		bot->maxs,
		BOT_ACTION_DOPPELGANGER_GROUND_HEIGHT,
		false);
}

bool BotActions_TeleporterAllowedForMode(const gentity_t *bot) {
	return bot != nullptr &&
		bot->client != nullptr &&
		deathmatch != nullptr &&
		deathmatch->integer != 0 &&
		!BotActions_CarryingCtfObjective(bot->client);
}

bool BotActions_TeleporterEscapePressure(
	const BotActionContext &context,
	bool hazardPressure) {
	const bool criticalHealth =
		context.health > 0 &&
		context.health <= BOT_ACTION_TELEPORTER_CRITICAL_HEALTH;
	const bool closeThreat =
		context.combat.hasEnemy &&
		context.combat.enemyDistanceSquared > 0 &&
		context.combat.enemyDistanceSquared <=
			BOT_ACTION_TELEPORTER_CLOSE_THREAT_DISTANCE_SQUARED;
	const bool immediateEnemyPressure =
		context.combat.enemyShootable ||
		(context.combat.enemyVisible && closeThreat);

	return (criticalHealth && immediateEnemyPressure) ||
		(criticalHealth && hazardPressure);
}

gentity_t *BotActions_ClientEntityForIndex(int clientIndex) {
	if (clientIndex < 0 || clientIndex >= static_cast<int>(game.maxClients)) {
		return nullptr;
	}
	return g_entities + clientIndex + 1;
}

bool BotActions_LivePlayingClient(const gentity_t *ent) {
	return ent != nullptr &&
		ent->inUse &&
		ent->client != nullptr &&
		ClientIsPlaying(ent->client) &&
		!ent->client->eliminated &&
		!ent->deadFlag &&
		ent->health > 0;
}

bool BotActions_NukeAllowedForMode(const gentity_t *bot) {
	return bot != nullptr &&
		bot->client != nullptr &&
		deathmatch != nullptr &&
		deathmatch->integer != 0 &&
		!BotActions_CarryingCtfObjective(bot->client);
}

bool BotActions_NukeLaunchClear(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr) {
		return false;
	}

	Vector3 forward;
	AngleVectors(bot->client->vAngle, forward, nullptr, nullptr);
	const Vector3 end =
		bot->s.origin + (forward * BOT_ACTION_NUKE_LAUNCH_CLEAR_DISTANCE);
	const trace_t trace = gi.traceLine(
		bot->s.origin,
		end,
		const_cast<gentity_t *>(bot),
		MASK_PROJECTILE);
	return !trace.startSolid && !trace.allSolid && trace.fraction >= 0.75f;
}

bool BotActions_NukeFriendlyExposure(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr) {
		return true;
	}
	const int friendlyRadiusSquared =
		BOT_ACTION_NUKE_FALLOFF_RADIUS * BOT_ACTION_NUKE_FALLOFF_RADIUS;
	for (int clientIndex = 0; clientIndex < static_cast<int>(game.maxClients); ++clientIndex) {
		gentity_t *candidate = BotActions_ClientEntityForIndex(clientIndex);
		if (candidate == bot || !BotActions_LivePlayingClient(candidate)) {
			continue;
		}
		if (!OnSameTeam(const_cast<gentity_t *>(bot), candidate)) {
			continue;
		}
		const Vector3 delta = candidate->s.origin - bot->s.origin;
		if (BotActions_ClampDistanceSquared(delta.lengthSquared()) <= friendlyRadiusSquared) {
			return true;
		}
	}
	return false;
}

int BotActions_NukeEnemyValue(const BotActionContext &context) {
	if (context.combat.enemyEstimateKnown) {
		return context.combat.enemyHealthEstimate + context.combat.enemyArmorEstimate;
	}

	gentity_t *enemy =
		BotActions_ClientEntityForIndex(context.combat.enemyClientIndex);
	if (!BotActions_LivePlayingClient(enemy)) {
		return 0;
	}
	return std::max(0, enemy->health) + BotActions_ArmorValue(enemy->client);
}

BotInventoryUseScore BotActions_ScoreInventoryUse(
	const gentity_t *bot,
	const BotActionContext &context,
	const Item *item) {
	BotInventoryUseScore result{};
	if (bot == nullptr || bot->client == nullptr || item == nullptr) {
		result.reason = "invalid";
		return result;
	}

	result.item = item->id;
	result.specialKind = BotItems_ClassifySpecial(item);
	result.priority = BOT_ACTION_INVENTORY_POLICY_PRIORITY;

	const bool hasEnemy = context.combat.hasEnemy;
	const bool enemyActionable =
		hasEnemy && (context.combat.enemyVisible || context.combat.enemyShootable);
	const bool survivalPressure = context.item.lowHealth || context.item.lowArmor;
	const bool hurt = context.maxHealth > 0 && context.health < context.maxHealth;
	const bool hazardPressure = BotActions_InHazardVolume(bot);
	const bool underwaterPressure = BotActions_Underwater(bot);

	if (item->id == IT_AMMO_NUKE) {
		result.utilityUse = true;
		result.nukeSafetyChecked = true;
		if (!BotActions_NukeAllowedForMode(bot)) {
			result.nukeDeferred = true;
			result.nukeFriendlyBlocked = BotActions_CarryingCtfObjective(bot->client);
			result.reason = result.nukeFriendlyBlocked ?
				"nuke_objective_carrier_deferred" :
				"nuke_mode_deferred";
		} else if (!enemyActionable) {
			result.nukeDeferred = true;
			result.reason = "nuke_no_actionable_target";
		} else if (context.combat.enemyDistanceSquared <
				BOT_ACTION_NUKE_MIN_TARGET_DISTANCE_SQUARED) {
			result.nukeDeferred = true;
			result.nukeSelfBlocked = true;
			result.reason = "nuke_target_too_close";
		} else if (context.combat.enemyDistanceSquared >
				BOT_ACTION_NUKE_MAX_TARGET_DISTANCE_SQUARED) {
			result.nukeDeferred = true;
			result.reason = "nuke_target_too_far";
		} else if (survivalPressure || hazardPressure || underwaterPressure) {
			result.nukeDeferred = true;
			result.nukeSelfBlocked = true;
			result.reason = "nuke_self_pressure";
		} else if (!BotActions_NukeLaunchClear(bot)) {
			result.nukeDeferred = true;
			result.nukeSelfBlocked = true;
			result.reason = "nuke_launch_blocked";
		} else if (BotActions_NukeFriendlyExposure(bot)) {
			result.nukeDeferred = true;
			result.nukeFriendlyBlocked = true;
			result.reason = "nuke_friendly_exposure";
		} else if (BotActions_NukeEnemyValue(context) < BOT_ACTION_NUKE_MIN_ENEMY_VALUE) {
			result.nukeDeferred = true;
			result.reason = "nuke_low_enemy_value";
		} else {
			result.score = 82;
			result.combatUse = true;
			result.nukeUse = true;
			result.reason = "nuke_safe_combat";
		}
		return result;
	}

	if (BotItems_IsPowerArmorUtility(item)) {
		result.specialKind = BotItemSpecialKind::Protection;
		if (bot->flags & FL_POWER_ARMOR) {
			result.alreadyActive = true;
			result.reason = "power_armor_active";
			return result;
		}
		if (bot->client->pers.inventory[IT_AMMO_CELLS] <= 0) {
			result.noCells = true;
			result.reason = "power_armor_no_cells";
			return result;
		}
		if (!hasEnemy && !survivalPressure) {
			result.reason = "power_armor_no_pressure";
			return result;
		}

		result.score = survivalPressure ? 87 : 70;
		result.combatUse = hasEnemy;
		result.survivalUse = survivalPressure;
		result.powerArmorUse = true;
		result.reason = survivalPressure ? "power_armor_survival" : "power_armor_combat";
		return result;
	}

	if (item->flags & IF_SPHERE) {
		result.deployableUse = true;
		if (bot->client->ownedSphere != nullptr) {
			result.alreadyActive = true;
			result.ownedSphere = true;
			result.reason = "sphere_active";
			return result;
		}
		if (!hasEnemy && !survivalPressure) {
			result.reason = "sphere_no_pressure";
			return result;
		}

		switch (item->id) {
		case IT_POWERUP_SPHERE_DEFENDER:
			result.score = survivalPressure ? 84 : 74;
			result.combatUse = hasEnemy;
			result.survivalUse = survivalPressure;
			result.specialKind = BotItemSpecialKind::Protection;
			result.reason = survivalPressure ? "defender_sphere_survival" : "defender_sphere_combat";
			break;
		case IT_POWERUP_SPHERE_HUNTER:
			if (enemyActionable) {
				result.score = 73;
				result.combatUse = true;
				result.reason = "hunter_sphere_combat";
			} else {
				result.reason = "hunter_sphere_no_target";
			}
			break;
		case IT_POWERUP_SPHERE_VENGEANCE:
			if (hasEnemy && survivalPressure) {
				result.score = 78;
				result.combatUse = true;
				result.survivalUse = true;
				result.reason = "vengeance_sphere_survival";
			} else {
				result.reason = "vengeance_sphere_no_pressure";
			}
			break;
		default:
			result.reason = "sphere_unsupported";
			break;
		}
		return result;
	}
	if (BotActions_InventoryEffectActive(bot->client, item)) {
		result.alreadyActive = true;
		result.reason = "powerup_active";
		return result;
	}

	switch (result.specialKind) {
	case BotItemSpecialKind::DamageBoost:
		if (enemyActionable) {
			result.score = context.combat.enemyShootable ? 100 : 90;
			result.combatUse = true;
			result.reason = "damage_boost_combat";
		} else {
			result.reason = "damage_boost_no_target";
		}
		break;
	case BotItemSpecialKind::Protection:
		if (hasEnemy && survivalPressure) {
			result.score = 95;
			result.combatUse = true;
			result.survivalUse = true;
			result.reason = "protection_survival";
		} else if (enemyActionable) {
			result.score = 76;
			result.combatUse = true;
			result.reason = "protection_combat";
		} else {
			result.reason = "protection_no_pressure";
		}
		break;
	case BotItemSpecialKind::Invisibility:
		if (enemyActionable) {
			result.score = 72;
			result.combatUse = true;
			result.reason = "invisibility_combat";
		} else {
			result.reason = "invisibility_no_target";
		}
		break;
	case BotItemSpecialKind::Mobility:
		if (enemyActionable) {
			result.score = 68;
			result.combatUse = true;
			result.reason = "mobility_combat";
		} else {
			result.reason = "mobility_no_target";
		}
		break;
	case BotItemSpecialKind::Utility:
		if (item->id == IT_POWERUP_REGEN && context.item.lowHealth) {
			result.score = 90;
			result.survivalUse = true;
			result.reason = "regeneration_survival";
		} else if (item->id == IT_POWERUP_REGEN && enemyActionable && hurt) {
			result.score = 70;
			result.combatUse = true;
			result.reason = "regeneration_combat";
		} else if (item->id == IT_POWERUP_ENVIROSUIT && hazardPressure) {
			result.score = 110;
			result.survivalUse = true;
			result.environmentUse = true;
			result.reason = "envirosuit_hazard";
		} else if (item->id == IT_POWERUP_REBREATHER && underwaterPressure) {
			result.score = 102;
			result.survivalUse = true;
			result.environmentUse = true;
			result.reason = "rebreather_underwater";
		} else if (item->id == IT_IR_GOGGLES && hasEnemy && !context.combat.enemyVisible) {
			result.score = 54;
			result.combatUse = true;
			result.utilityUse = true;
			result.reason = "ir_goggles_tracking";
		} else if (item->id == IT_POWERUP_SILENCER && enemyActionable) {
			result.score = 52;
			result.combatUse = true;
			result.utilityUse = true;
			result.reason = "silencer_combat";
		} else if (item->id == IT_DOPPELGANGER) {
			result.deployableUse = true;
			result.placementChecked = true;
			if (!enemyActionable && !survivalPressure) {
				result.reason = "doppelganger_no_pressure";
			} else if (context.combat.enemyDistanceSquared > 0 &&
				context.combat.enemyDistanceSquared <
					BOT_ACTION_DOPPELGANGER_MIN_DISTANCE_SQUARED &&
				!survivalPressure) {
				result.reason = "doppelganger_target_too_close";
			} else if (!BotActions_DoppelgangerPlacementReady(bot)) {
				result.placementBlocked = true;
				result.reason = "doppelganger_no_space";
			} else {
				result.score = survivalPressure ? 83 : 66;
				result.combatUse = enemyActionable;
				result.survivalUse = survivalPressure;
				result.utilityUse = true;
				result.reason = survivalPressure ?
					"doppelganger_survival" :
					"doppelganger_combat";
			}
		} else if (item->id == IT_TELEPORTER) {
			result.utilityUse = true;
			result.escapeUse = true;
			if (!BotActions_TeleporterAllowedForMode(bot)) {
				result.reason = BotActions_CarryingCtfObjective(bot->client) ?
					"teleporter_objective_carrier_deferred" :
					"teleporter_mode_deferred";
			} else if (!BotActions_TeleporterEscapePressure(context, hazardPressure)) {
				result.reason = "teleporter_no_escape_pressure";
			} else {
				result.score = hazardPressure ? 91 : 88;
				result.survivalUse = true;
				result.environmentUse = hazardPressure;
				result.reason = hazardPressure ?
					"teleporter_hazard_escape" :
					"teleporter_combat_escape";
			}
		} else {
			result.reason = "utility_deferred";
		}
		break;
	default:
		result.reason = "unsupported_kind";
		break;
	}

	return result;
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

void BotActions_EnrichCombatInventory(const gentity_t *bot, BotActionContext *context) {
	botActionStatus.weaponInventoryScans++;
	botActionStatus.lastWeaponInventoryClientIndex =
		context != nullptr ? context->clientIndex : -1;
	botActionStatus.lastWeaponInventoryCurrentItem = 0;
	botActionStatus.lastWeaponInventorySelectedItem = 0;
	botActionStatus.lastWeaponInventoryCandidateCount = 0;
	botActionStatus.lastWeaponInventoryReadyCount = 0;
	botActionStatus.lastWeaponInventoryCurrentScore = 0;
	botActionStatus.lastWeaponInventorySelectedScore = 0;
	botActionStatus.lastWeaponInventorySelectedAmmo = 0;
	botActionStatus.lastWeaponInventorySelectedScoreMargin = 0;
	botActionStatus.lastWeaponInventorySelectedPriority = 0;
	botActionStatus.lastWeaponInventorySelectedAmmoPerShot = 0;
	botActionStatus.lastWeaponInventorySelectedSplashDamage = 0;
	botActionStatus.lastWeaponInventorySelectedSelfDamageRisk = 0;
	botActionStatus.lastWeaponInventorySelectedEstimateAdjustment = 0;
	botActionStatus.lastWeaponInventorySelectedRangeBand = BotWeaponRangeBand::Unknown;
	botActionStatus.lastWeaponInventorySelectedAttackModel = BotWeaponAttackModel::Unknown;
	botActionStatus.lastWeaponInventoryReason = "invalid_context";
	botActionStatus.lastWeaponInventoryEstimateReason = "none";

	if (bot == nullptr || bot->client == nullptr ||
		context == nullptr || !context->valid || !context->alive) {
		return;
	}

	const BotCombatContext baseCombat = context->combat;
	botActionStatus.lastWeaponInventoryCurrentItem = baseCombat.currentWeaponItem;
	botActionStatus.lastWeaponInventorySelectedItem = baseCombat.currentWeaponItem;
	if (!baseCombat.hasEnemy) {
		botActionStatus.weaponInventoryNoEnemySkips++;
		botActionStatus.lastWeaponInventoryReason = "no_enemy";
		return;
	}

	const Item *currentWeapon = bot->client->pers.weapon;
	const Item *pendingWeapon = bot->client->weapon.pending;
	if (pendingWeapon != nullptr && pendingWeapon != currentWeapon) {
		botActionStatus.weaponInventoryPendingDeferrals++;
		botActionStatus.lastWeaponInventorySelectedItem =
			context->combat.preferredWeaponItem;
		botActionStatus.lastWeaponInventoryReason = "pending_weapon";
		return;
	}

	const BotWeaponSelectionResult baseSelection =
		BotCombat_SelectPreferredWeapon(baseCombat);
	int candidateCount = 0;
	int readyCount = 0;
	int ammoSkipCount = 0;
	int splashUnsafeCount = 0;
	int selectedWeaponItem = baseCombat.currentWeaponItem;
	int selectedWeaponAmmo = baseCombat.currentWeaponAmmo;
	int selectedWeaponScore = baseSelection.currentWeaponScore;
	int selectedEstimateAdjustment = baseSelection.currentEstimateAdjustment;
	const char *selectedReason = baseSelection.reason;
	const char *selectedEstimateReason = baseSelection.estimateReason;
	bool selectedInventoryWeapon = false;

	for (int itemId = static_cast<int>(IT_NULL) + 1;
		itemId < static_cast<int>(IT_TOTAL);
		++itemId) {
		const Item *item = GetItemByIndex(static_cast<item_id_t>(itemId));
		if (!BotActions_CarriedWeaponCandidate(bot->client, item)) {
			continue;
		}

		candidateCount++;
		const int ammo = BotActions_WeaponAmmo(bot->client, item);
		const bool ready = BotActions_WeaponReady(bot->client, item);
		if (ready) {
			readyCount++;
		}
		const BotWeaponMetadata *candidateMetadata =
			BotCombat_GetWeaponMetadata(itemId);
		if (candidateMetadata != nullptr) {
			if (BotActions_WeaponAmmoInsufficient(*candidateMetadata, ammo)) {
				ammoSkipCount++;
			}
			if (BotActions_WeaponSelfDamageUnsafe(*candidateMetadata, baseCombat)) {
				splashUnsafeCount++;
			}
		}
		if (itemId == baseCombat.currentWeaponItem ||
			candidateMetadata == nullptr) {
			continue;
		}

		BotCombatContext candidateCombat = baseCombat;
		candidateCombat.preferredWeaponItem = itemId;
		candidateCombat.preferredWeaponAmmo = ammo;
		candidateCombat.preferredWeaponReady = ready;
		const BotWeaponSelectionResult candidateSelection =
			BotCombat_SelectPreferredWeapon(candidateCombat);
		if (!candidateSelection.shouldSwitch ||
			candidateSelection.weaponItem != itemId) {
			continue;
		}
		if (!selectedInventoryWeapon ||
			candidateSelection.selectedWeaponScore > selectedWeaponScore) {
			selectedInventoryWeapon = true;
			selectedWeaponItem = itemId;
			selectedWeaponAmmo = ammo;
			selectedWeaponScore = candidateSelection.selectedWeaponScore;
			selectedEstimateAdjustment =
				candidateSelection.selectedEstimateAdjustment;
			selectedReason = candidateSelection.reason;
			selectedEstimateReason = candidateSelection.estimateReason;
		}
	}

	botActionStatus.weaponInventoryCandidates += candidateCount;
	botActionStatus.weaponInventoryReadyCandidates += readyCount;
	botActionStatus.weaponInventoryAmmoSkips += ammoSkipCount;
	botActionStatus.weaponInventorySplashUnsafeSkips += splashUnsafeCount;
	botActionStatus.lastWeaponInventoryCandidateCount = candidateCount;
	botActionStatus.lastWeaponInventoryReadyCount = readyCount;
	botActionStatus.lastWeaponInventoryCurrentScore = baseSelection.currentWeaponScore;
	botActionStatus.lastWeaponInventorySelectedScore = selectedWeaponScore;
	botActionStatus.lastWeaponInventorySelectedItem = selectedWeaponItem;
	botActionStatus.lastWeaponInventorySelectedAmmo = selectedWeaponAmmo;
	botActionStatus.lastWeaponInventorySelectedScoreMargin =
		selectedWeaponScore - baseSelection.currentWeaponScore;
	botActionStatus.lastWeaponInventorySelectedEstimateAdjustment =
		selectedEstimateAdjustment;
	botActionStatus.lastWeaponInventoryEstimateReason = selectedEstimateReason;
	const BotWeaponMetadata *selectedMetadata =
		BotCombat_GetWeaponMetadata(selectedWeaponItem);
	if (selectedMetadata != nullptr) {
		const BotWeaponRangeBand selectedRange =
			BotCombat_RangeBandForDistanceSquared(baseCombat.enemyDistanceSquared);
		botActionStatus.lastWeaponInventorySelectedPriority =
			selectedMetadata->priority;
		botActionStatus.lastWeaponInventorySelectedAmmoPerShot =
			selectedMetadata->ammoPerShot;
		botActionStatus.lastWeaponInventorySelectedSplashDamage =
			selectedMetadata->splashDamage ? 1 : 0;
		botActionStatus.lastWeaponInventorySelectedSelfDamageRisk =
			selectedMetadata->selfDamageRisk ? 1 : 0;
		botActionStatus.lastWeaponInventorySelectedRangeBand = selectedRange;
		botActionStatus.lastWeaponInventorySelectedAttackModel =
			selectedMetadata->attackModel;
		if (selectedInventoryWeapon &&
			(selectedRange == selectedMetadata->idealRange ||
			 BotActions_RangeSupportsWeapon(selectedRange, *selectedMetadata))) {
			botActionStatus.weaponInventoryRangeSelections++;
		}
	}
	if (selectedInventoryWeapon && selectedEstimateAdjustment != 0) {
		botActionStatus.weaponInventoryEstimateSelections++;
	}

	if (candidateCount <= 0) {
		botActionStatus.weaponInventoryNoCandidateSkips++;
		botActionStatus.lastWeaponInventoryReason = "no_candidates";
		return;
	}
	if (!selectedInventoryWeapon) {
		botActionStatus.weaponInventoryKeepCurrent++;
		botActionStatus.lastWeaponInventoryReason = selectedReason;
		return;
	}

	context->combat.preferredWeaponItem = selectedWeaponItem;
	context->combat.preferredWeaponAmmo = selectedWeaponAmmo;
	context->combat.preferredWeaponReady = true;
	botActionStatus.weaponInventorySelections++;
	botActionStatus.weaponInventorySwitchRecommendations++;
	botActionStatus.lastWeaponInventoryReason = selectedReason;
}

void BotActions_EnrichInventoryUse(const gentity_t *bot, BotActionContext *context) {
	botActionStatus.inventoryPolicyScans++;
	botActionStatus.lastInventoryPolicyClientIndex =
		context != nullptr ? context->clientIndex : -1;
	botActionStatus.lastInventoryPolicyItem = 0;
	botActionStatus.lastInventoryPolicyCandidateCount = 0;
	botActionStatus.lastInventoryPolicyUsableCount = 0;
	botActionStatus.lastInventoryPolicyScore = 0;
	botActionStatus.lastInventoryPolicyPriority = 0;
	botActionStatus.lastInventoryPolicySpecialKind = BotItemSpecialKind::None;
	botActionStatus.lastInventoryPolicyReason = "invalid_context";

	if (bot == nullptr || bot->client == nullptr ||
		context == nullptr || !context->valid || !context->alive) {
		return;
	}

	if (context->inventoryUseRequested || context->inventoryItem > 0) {
		botActionStatus.inventoryPolicyExistingRequestDeferrals++;
		botActionStatus.lastInventoryPolicyItem = context->inventoryItem;
		botActionStatus.lastInventoryPolicyPriority = context->inventoryUsePriority;
		botActionStatus.lastInventoryPolicyReason = "existing_request";
		return;
	}

	BotInventoryUseScore selected{};
	int candidateCount = 0;
	int usableCount = 0;

	for (int itemId = static_cast<int>(IT_NULL) + 1;
		itemId < static_cast<int>(IT_TOTAL);
		++itemId) {
		const Item *item = GetItemByIndex(static_cast<item_id_t>(itemId));
		if (!BotActions_CarriedInventoryCandidate(bot->client, item)) {
			continue;
		}

		candidateCount++;
		const BotInventoryUseScore score =
			BotActions_ScoreInventoryUse(bot, *context, item);
		if (score.alreadyActive) {
			botActionStatus.inventoryPolicyActiveDeferrals++;
		}
		if (score.ownedSphere) {
			botActionStatus.inventoryPolicyOwnedSphereDeferrals++;
		}
		if (score.placementChecked) {
			botActionStatus.inventoryPolicyPlacementChecks++;
		}
		if (score.placementBlocked) {
			botActionStatus.inventoryPolicyPlacementDeferrals++;
		}
		if (score.nukeSafetyChecked) {
			botActionStatus.inventoryPolicyNukeSafetyChecks++;
		}
		if (score.nukeDeferred) {
			botActionStatus.inventoryPolicyNukeDeferrals++;
		}
		if (score.nukeFriendlyBlocked) {
			botActionStatus.inventoryPolicyNukeFriendlyDeferrals++;
		}
		if (score.nukeSelfBlocked) {
			botActionStatus.inventoryPolicyNukeSelfDeferrals++;
		}
		if (score.noCells) {
			botActionStatus.inventoryPolicyNoCellsSkips++;
		}
		if (score.score <= 0) {
			continue;
		}

		usableCount++;
		if (score.score > selected.score) {
			selected = score;
		}
	}

	botActionStatus.inventoryPolicyCandidates += candidateCount;
	botActionStatus.inventoryPolicyUsableCandidates += usableCount;
	botActionStatus.lastInventoryPolicyCandidateCount = candidateCount;
	botActionStatus.lastInventoryPolicyUsableCount = usableCount;

	if (candidateCount <= 0) {
		botActionStatus.inventoryPolicyNoCandidateSkips++;
		botActionStatus.lastInventoryPolicyReason = "no_candidates";
		return;
	}
	if (usableCount <= 0 || selected.item <= 0) {
		botActionStatus.inventoryPolicyNoUsableSkips++;
		botActionStatus.lastInventoryPolicyReason = "no_usable_candidate";
		return;
	}

	context->inventoryUseRequested = true;
	context->inventoryItem = selected.item;
	context->inventoryUsePriority = selected.priority;
	context->inventoryUseReason = selected.reason;

	botActionStatus.inventoryPolicySelections++;
	if (selected.combatUse) {
		botActionStatus.inventoryPolicyCombatUses++;
	}
	if (selected.survivalUse) {
		botActionStatus.inventoryPolicySurvivalUses++;
	}
	if (selected.utilityUse) {
		botActionStatus.inventoryPolicyUtilityUses++;
	}
	if (selected.environmentUse) {
		botActionStatus.inventoryPolicyEnvironmentUses++;
	}
	if (selected.deployableUse) {
		botActionStatus.inventoryPolicyDeployableUses++;
	}
	if (selected.escapeUse) {
		botActionStatus.inventoryPolicyEscapeUses++;
	}
	if (selected.powerArmorUse) {
		botActionStatus.inventoryPolicyPowerArmorUses++;
	}
	if (selected.nukeUse) {
		botActionStatus.inventoryPolicyNukeUses++;
	}
	botActionStatus.lastInventoryPolicyItem = selected.item;
	botActionStatus.lastInventoryPolicyScore = selected.score;
	botActionStatus.lastInventoryPolicyPriority = selected.priority;
	botActionStatus.lastInventoryPolicySpecialKind = selected.specialKind;
	botActionStatus.lastInventoryPolicyReason = selected.reason;
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
		const int inventoryPriority = context.inventoryUsePriority > 0 ?
			context.inventoryUsePriority :
			BOT_ACTION_USE_INVENTORY_PRIORITY;
		decision = BotActions_HigherPriority(decision, {
			.intent = BotActionIntent::UseInventory,
			.clientIndex = context.clientIndex,
			.priority = inventoryPriority,
			.item = context.inventoryItem,
			.wantsInventoryUse = true,
			.reason = context.inventoryUseReason != nullptr ?
				context.inventoryUseReason :
				"inventory_use",
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
