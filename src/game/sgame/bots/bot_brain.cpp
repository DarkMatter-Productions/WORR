/*Copyright (c) 2024 ZeniMax Media Inc.
Licensed under the GNU General Public License 2.0.

bot_brain.cpp implementation.*/

#include "../g_local.hpp"
#include "bot_actions.hpp"
#include "bot_items.hpp"
#include "bot_nav.hpp"
#include "bot_objectives.hpp"
#include "botlib_adapter.hpp"
#include "bot_brain.hpp"
#include "bot_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <cmath>
#include <string>
#include <utility>

namespace {

template <typename... Args>
void BotBrain_PrintStatusFmt( std::format_string<Args...> formatString, Args &&...args ) {
	static char statusBuffer[0x10000];

	G_FmtTo_(statusBuffer, formatString, std::forward<Args>(args)...);
	base_import.Com_Print(statusBuffer);
}

void BotBrain_AppendCompactStatusField( std::string &line, const char *key, int value ) {
	line.append(key);
	line.push_back('=');
	line.append(std::to_string(value));
	line.push_back(' ');
}

void BotBrain_AppendCompactStatusField( std::string &line, const char *key, const char *value ) {
	line.append(key);
	line.push_back('=');
	line.append(value != nullptr ? value : "none");
	line.push_back(' ');
}

int BotBrain_LastItemTimerAllowed(const BotItemStatus &itemStatus) {
	return itemStatus.lastTimingPolicyRemainingMilliseconds == 0 &&
		(itemStatus.lastTimingPolicyReason == BotItemTimingPolicyReason::ExactTimer ||
		 itemStatus.lastTimingPolicyReason == BotItemTimingPolicyReason::FuzzedTimer) ? 1 : 0;
}

int BotBrain_LastItemTimingConsumerAllowed(const BotItemStatus &itemStatus) {
	return itemStatus.lastTimingConsumerRemainingMilliseconds == 0 &&
		(itemStatus.lastTimingConsumerReason == BotItemTimingConsumerReason::LivePickup ||
		 itemStatus.lastTimingConsumerReason == BotItemTimingConsumerReason::TimerReady) ? 1 : 0;
}

void BotBrain_PrintCompactActionStatus(
	const BotActionStatus &actionStatus,
	const BotItemStatus &itemStatus,
	const BotCombatStatus &combatStatus) {
	std::string line;
	line.reserve(3072);

	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(
		line,
		"action_attack_decisions",
		actionStatus.attackDecisions);
	BotBrain_AppendCompactStatusField(
		line,
		"action_applied_attack_buttons",
		actionStatus.appliedAttackButtons);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_switch_decisions",
		actionStatus.weaponSwitchDecisions);
	BotBrain_AppendCompactStatusField(
		line,
		"action_pending_weapon_switches",
		actionStatus.pendingWeaponSwitches);
	BotBrain_AppendCompactStatusField(
		line,
		"action_command_request_builds",
		actionStatus.commandRequestBuilds);
	BotBrain_AppendCompactStatusField(
		line,
		"action_command_request_accepted",
		actionStatus.commandRequestAccepted);
	BotBrain_AppendCompactStatusField(
		line,
		"action_command_request_rejected",
		actionStatus.commandRequestRejected);
	BotBrain_AppendCompactStatusField(
		line,
		"action_command_request_submitted",
		actionStatus.commandRequestSubmitted);
	BotBrain_AppendCompactStatusField(
		line,
		"action_command_request_deferred",
		actionStatus.commandRequestDeferred);
	BotBrain_AppendCompactStatusField(
		line,
		"action_command_request_dispatch_failures",
		actionStatus.commandRequestDispatchFailures);
	BotBrain_AppendCompactStatusField(
		line,
		"action_last_command_request_kind",
		static_cast<int>(actionStatus.lastCommandRequestKind));
	BotBrain_AppendCompactStatusField(
		line,
		"action_last_command_dispatch_outcome",
		static_cast<int>(actionStatus.lastCommandDispatchOutcome));
	BotBrain_AppendCompactStatusField(
		line,
		"action_last_command_dispatch_outcome_name",
		BotActions_CommandDispatchOutcomeName(actionStatus.lastCommandDispatchOutcome));
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_command_dispatches",
		actionStatus.weaponCommandDispatches);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_command_dispatches",
		actionStatus.inventoryCommandDispatches);
	BotBrain_AppendCompactStatusField(
		line,
		"weapon_switch_requests",
		actionStatus.weaponSwitchRequests);
	BotBrain_AppendCompactStatusField(
		line,
		"weapon_switch_completions",
		actionStatus.weaponSwitchCompletions);
	BotBrain_AppendCompactStatusField(
		line,
		"weapon_switch_failures",
		actionStatus.weaponSwitchFailures);
	BotBrain_AppendCompactStatusField(
		line,
		"weapon_switch_expected_item",
		actionStatus.weaponSwitchExpectedItem);
	BotBrain_AppendCompactStatusField(
		line,
		"weapon_switch_actual_item",
		actionStatus.weaponSwitchActualItem);
	BotBrain_AppendCompactStatusField(
		line,
		"weapon_switch_expected_match",
		actionStatus.weaponSwitchExpectedMatch);
	BotBrain_AppendCompactStatusField(
		line,
		"item_low_health_boosts",
		itemStatus.lowHealthBoosts);
	BotBrain_AppendCompactStatusField(
		line,
		"item_low_armor_boosts",
		itemStatus.lowArmorBoosts);
	BotBrain_AppendCompactStatusField(
		line,
		"item_damage_boost_candidates",
		itemStatus.damageBoostCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"item_utility_powerup_candidates",
		itemStatus.utilityPowerupCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"item_tech_candidates",
		itemStatus.techCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"item_ctf_objective_candidates",
		itemStatus.ctfObjectiveCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"item_special_utility_boosts",
		itemStatus.specialUtilityBoosts);
	BotBrain_AppendCompactStatusField(
		line,
		"item_focus_health_boosts",
		itemStatus.focusHealthBoosts);
	BotBrain_AppendCompactStatusField(
		line,
		"item_focus_armor_boosts",
		itemStatus.focusArmorBoosts);
	BotBrain_AppendCompactStatusField(
		line,
		"item_health_goal_assignments",
		itemStatus.itemHealthGoalAssignments);
	BotBrain_AppendCompactStatusField(
		line,
		"item_armor_goal_assignments",
		itemStatus.itemArmorGoalAssignments);
	BotBrain_AppendCompactStatusField(
		line,
		"item_health_pickups",
		itemStatus.itemHealthPickups);
	BotBrain_AppendCompactStatusField(
		line,
		"item_armor_pickups",
		itemStatus.itemArmorPickups);
	BotBrain_AppendCompactStatusField(line, "last_health_pickup_delta", itemStatus.lastHealthPickupDelta);
	BotBrain_AppendCompactStatusField(line, "last_armor_pickup_delta", itemStatus.lastArmorPickupDelta);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_policy_evaluations",
		itemStatus.timingPolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timer_evaluations",
		itemStatus.timingPolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_policy_timers_disabled",
		itemStatus.timingPolicyTimersDisabled);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_policy_unobserved_blocks",
		itemStatus.timingPolicyUnobservedBlocks);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timer_fairness_blocks",
		itemStatus.timingPolicyUnobservedBlocks);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_policy_exact_uses",
		itemStatus.timingPolicyExactUses);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_policy_fuzzed_uses",
		itemStatus.timingPolicyFuzzedUses);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timer_fuzzed_offsets",
		itemStatus.timingPolicyFuzzedUses);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_policy_ready",
		itemStatus.timingPolicyReady);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timer_allowed_uses",
		itemStatus.timingPolicyReady);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_policy_waiting",
		itemStatus.timingPolicyWaiting);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timer_blocked_uses",
		itemStatus.timingPolicyWaiting);
	BotBrain_AppendCompactStatusField(
		line,
		"item_last_timing_policy_reason",
		static_cast<int>(itemStatus.lastTimingPolicyReason));
	BotBrain_AppendCompactStatusField(
		line,
		"item_last_timing_policy_reason_name",
		BotItems_TimingPolicyReasonName(itemStatus.lastTimingPolicyReason));
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timer_reason",
		BotItems_TimingPolicyReasonName(itemStatus.lastTimingPolicyReason));
	BotBrain_AppendCompactStatusField(
		line,
		"item_last_timing_policy_fuzz_ms",
		itemStatus.lastTimingPolicyFuzzMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timer_fuzz_ms",
		itemStatus.lastTimingPolicyFuzzMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"item_last_timing_policy_remaining_ms",
		itemStatus.lastTimingPolicyRemainingMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timer_known_ms",
		itemStatus.lastTimingPolicyEffectiveAvailableMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timer_allowed",
		BotBrain_LastItemTimerAllowed(itemStatus));
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_consumer_evaluations",
		itemStatus.timingConsumerEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_consumer_live_pickups",
		itemStatus.timingConsumerLivePickups);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_consumer_ready",
		itemStatus.timingConsumerReady);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_consumer_waiting",
		itemStatus.timingConsumerWaiting);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_consumer_fairness_blocks",
		itemStatus.timingConsumerFairnessBlocks);
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_consumer_selection_deferrals",
		itemStatus.timingConsumerSelectionDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_item",
		itemStatus.lastTimingConsumerItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_entity",
		itemStatus.lastTimingConsumerEntity);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_effective_ms",
		itemStatus.lastTimingConsumerEffectiveAvailableMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_remaining_ms",
		itemStatus.lastTimingConsumerRemainingMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_fuzz_ms",
		itemStatus.lastTimingConsumerFuzzMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_allowed",
		BotBrain_LastItemTimingConsumerAllowed(itemStatus));
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_policy_reason",
		BotItems_TimingPolicyReasonName(itemStatus.lastTimingConsumerPolicyReason));
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timing_consumer_reason",
		BotItems_TimingConsumerReasonName(itemStatus.lastTimingConsumerReason));
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timer_item",
		itemStatus.lastTimingPolicyItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timer_entity",
		itemStatus.lastTimingPolicyEntity);
	BotBrain_AppendCompactStatusField(
		line,
		"item_last_special_kind",
		static_cast<int>(itemStatus.lastSpecialKind));
	BotBrain_AppendCompactStatusField(
		line,
		"item_last_special_kind_name",
		BotItems_SpecialKindName(itemStatus.lastSpecialKind));
	BotBrain_AppendCompactStatusField(
		line,
		"combat_enemy_acquisitions",
		combatStatus.enemyAcquisitions);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_enemy_visible",
		combatStatus.enemyVisible);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_enemy_shootable",
		combatStatus.enemyShootable);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_enemy_client",
		combatStatus.lastEnemyClient);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_enemy_health_observations",
		combatStatus.enemyHealthObservations);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_enemy_armor_observations",
		combatStatus.enemyArmorObservations);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_enemy_health",
		combatStatus.lastEnemyHealth);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_enemy_armor",
		combatStatus.lastEnemyArmor);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_enemy_effective_health",
		combatStatus.lastEnemyEffectiveHealth);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_damage_events",
		combatStatus.damageEvents);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_damage",
		combatStatus.lastDamage);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_damage_health",
		combatStatus.lastDamageHealth);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_damage_armor",
		combatStatus.lastDamageArmor);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_damage_sequence",
		combatStatus.lastDamageSequence);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_weapon_selection_estimate_uses",
		combatStatus.weaponSelectionEstimateUses);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_weapon_selection_finisher_bonuses",
		combatStatus.weaponSelectionFinisherBonuses);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_weapon_selection_armor_pressure_bonuses",
		combatStatus.weaponSelectionArmorPressureBonuses);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_weapon_selection_underpowered_penalties",
		combatStatus.weaponSelectionUnderpoweredPenalties);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_enemy_health_estimate",
		combatStatus.lastEnemyHealthEstimate);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_enemy_armor_estimate",
		combatStatus.lastEnemyArmorEstimate);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_enemy_effective_health_estimate",
		combatStatus.lastEnemyEffectiveHealthEstimate);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_weapon_estimate_adjustment",
		combatStatus.lastWeaponEstimateAdjustment);
	BotBrain_AppendCompactStatusField(
		line,
		"last_combat_estimate_selection_reason",
		combatStatus.lastEstimateSelectionReason);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_weapon_switch_decisions",
		combatStatus.weaponSwitchDecisions);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_fire_decisions",
		combatStatus.fireDecisions);
	BotBrain_AppendCompactStatusField(
		line,
		"combat_withheld_fire",
		combatStatus.withheldFire);
	BotBrain_AppendCompactStatusField(
		line,
		"aim_policy_evaluations",
		combatStatus.aimPolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"aim_policy_aim_allowed",
		combatStatus.aimPolicyAimAllowed);
	BotBrain_AppendCompactStatusField(
		line,
		"aim_policy_fire_allowed",
		combatStatus.aimPolicyFireAllowed);
	BotBrain_AppendCompactStatusField(
		line,
		"aim_policy_blocks_reaction",
		combatStatus.aimPolicyBlocksReaction);
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_failure",
		static_cast<int>(combatStatus.lastAimPolicyFailure));
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_failure_name",
		BotCombat_AimPolicyFailureName(combatStatus.lastAimPolicyFailure));
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_reaction_remaining_ms",
		combatStatus.lastAimPolicyReactionRemainingMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_aim_settle_remaining_ms",
		combatStatus.lastAimPolicyAimSettleRemainingMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_burst_shots_fired",
		combatStatus.lastAimPolicyBurstShotsFired);
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_burst_shots_remaining",
		combatStatus.lastAimPolicyBurstShotsRemaining);
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_burst_cooldown_remaining_ms",
		combatStatus.lastAimPolicyBurstCooldownRemainingMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"projectile_lead_evaluations",
		combatStatus.projectileLeadEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"projectile_lead_uses",
		combatStatus.projectileLeadUses);
	BotBrain_AppendCompactStatusField(
		line,
		"projectile_lead_no_projectile",
		combatStatus.projectileLeadNoProjectile);
	BotBrain_AppendCompactStatusField(
		line,
		"projectile_lead_no_speed",
		combatStatus.projectileLeadNoSpeed);
	BotBrain_AppendCompactStatusField(
		line,
		"projectile_lead_invalid_distance",
		combatStatus.projectileLeadInvalidDistance);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_weapon",
		combatStatus.lastProjectileLeadWeaponItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_speed",
		combatStatus.lastProjectileLeadSpeed);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_ms",
		combatStatus.lastProjectileLeadMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_raw_ms",
		combatStatus.lastProjectileLeadRawMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_max_ms",
		combatStatus.lastProjectileLeadMaxMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_scale_percent",
		combatStatus.lastProjectileLeadScalePercent);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_target_speed_sq",
		combatStatus.lastProjectileLeadTargetSpeedSquared);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_aim_distance_sq",
		combatStatus.lastProjectileLeadAimDistanceSquared);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_offset_sq",
		combatStatus.lastProjectileLeadOffsetSquared);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_raw_offset_sq",
		combatStatus.lastProjectileLeadRawOffsetSquared);
	BotBrain_AppendCompactStatusField(
		line,
		"last_projectile_lead_clamped",
		combatStatus.lastProjectileLeadClamped ? 1 : 0);
	BotBrain_AppendCompactStatusField(
		line,
		"live_aim_evaluations",
		combatStatus.liveAimEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"live_aim_aim_allowed",
		combatStatus.liveAimAimAllowed);
	BotBrain_AppendCompactStatusField(
		line,
		"live_aim_fire_allowed",
		combatStatus.liveAimFireAllowed);
	BotBrain_AppendCompactStatusField(
		line,
		"live_aim_policy_blocks",
		combatStatus.liveAimPolicyBlocks);
	BotBrain_AppendCompactStatusField(
		line,
		"live_aim_projectile_lead_uses",
		combatStatus.liveAimProjectileLeadUses);
	BotBrain_AppendCompactStatusField(
		line,
		"last_live_aim_weapon",
		combatStatus.lastLiveAimWeaponItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_live_aim_reaction_remaining_ms",
		combatStatus.lastLiveAimReactionRemainingMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_live_aim_aim_settle_remaining_ms",
		combatStatus.lastLiveAimAimSettleRemainingMilliseconds);
	BotBrain_AppendCompactStatusField(
		line,
		"last_live_aim_burst_shots_remaining",
		combatStatus.lastAimPolicyBurstShotsRemaining);
	BotBrain_AppendCompactStatusField(
		line,
		"last_live_aim_projectile_lead_percent",
		combatStatus.lastLiveAimProjectileLeadPercent);
	BotBrain_AppendCompactStatusField(
		line,
		"last_live_aim_reason",
		combatStatus.lastLiveAimReason);

	line.push_back('\n');
	base_import.Com_Print(line.c_str());
}

void BotBrain_PrintCompactObjectiveStatus(const BotObjectiveStatus &objectiveStatus) {
	std::string line;
	line.reserve(4096);

	line.append("q3a_bot_objective_status ");
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_evaluations",
		objectiveStatus.evaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_assignments",
		objectiveStatus.assignments);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_route_requests",
		objectiveStatus.routeRequests);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_route_commands",
		objectiveStatus.routeCommands);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_reaches",
		objectiveStatus.reaches);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_flag_pickups",
		objectiveStatus.flagPickups);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_flag_captures",
		objectiveStatus.flagCaptures);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_role_policy_evaluations",
		objectiveStatus.rolePolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_role_policy_selections",
		objectiveStatus.rolePolicySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_role_policy_lane_midfield_selections",
		objectiveStatus.rolePolicyLaneMidfieldSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_role_midfielders",
		objectiveStatus.roleMidfielder);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_evaluations",
		objectiveStatus.matchPolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_selections",
		objectiveStatus.matchPolicySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_no_selection",
		objectiveStatus.matchPolicyNoSelection);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_scoring",
		objectiveStatus.matchPolicyScoringParticipation);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_ffa",
		objectiveStatus.matchPolicyFfaSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_tdm",
		objectiveStatus.matchPolicyTdmSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_ctf",
		objectiveStatus.matchPolicyCtfSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_coop",
		objectiveStatus.matchPolicyCoopSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_attack",
		objectiveStatus.matchPolicyAttackSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_defend",
		objectiveStatus.matchPolicyDefendSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_midfield",
		objectiveStatus.matchPolicyMidfieldSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_friendly_fire",
		objectiveStatus.matchPolicyFriendlyFireAvoidance);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_evaluations",
		objectiveStatus.coopPolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_selections",
		objectiveStatus.coopPolicySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_no_selection",
		objectiveStatus.coopPolicyNoSelection);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_follow",
		objectiveStatus.coopPolicyFollowSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_wait",
		objectiveStatus.coopPolicyWaitSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_regroup",
		objectiveStatus.coopPolicyRegroupSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_lead",
		objectiveStatus.coopPolicyLeadSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_support",
		objectiveStatus.coopPolicySupportSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_coop_policy_resource_share",
		objectiveStatus.coopPolicyResourceShareSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_evaluations",
		objectiveStatus.resourcePolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_selections",
		objectiveStatus.resourcePolicySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_no_selection",
		objectiveStatus.resourcePolicyNoSelection);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_self_pickup",
		objectiveStatus.resourcePolicySelfPickupSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_share_team",
		objectiveStatus.resourcePolicyShareTeamSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_reserve",
		objectiveStatus.resourcePolicyReserveSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_deny_enemy",
		objectiveStatus.resourcePolicyDenyEnemySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_resource_policy_objective",
		objectiveStatus.resourcePolicyObjectiveSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_policy_evaluations",
		objectiveStatus.itemRolePolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_policy_selections",
		objectiveStatus.itemRolePolicySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_policy_no_selection",
		objectiveStatus.itemRolePolicyNoSelection);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_self_stack",
		objectiveStatus.itemRoleSelfStackSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_weapon_control",
		objectiveStatus.itemRoleWeaponControlSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_powerup_control",
		objectiveStatus.itemRolePowerupControlSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_team_resource",
		objectiveStatus.itemRoleTeamResourceSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_deny_enemy",
		objectiveStatus.itemRoleDenyEnemySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_item_role_objective",
		objectiveStatus.itemRoleObjectiveSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_friendly_fire_policy_evaluations",
		objectiveStatus.friendlyFirePolicyEvaluations);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_friendly_fire_avoidance",
		objectiveStatus.friendlyFirePolicyAvoidance);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_friendly_fire_target_blocks",
		objectiveStatus.friendlyFirePolicyTargetBlocks);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_enemy_flag_assignments",
		objectiveStatus.enemyFlagAssignments);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_type",
		objectiveStatus.lastObjectiveType);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_role",
		objectiveStatus.lastObjectiveRole);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_lane",
		objectiveStatus.lastObjectiveLane);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_client",
		objectiveStatus.lastClient);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_item",
		objectiveStatus.lastItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_area",
		objectiveStatus.lastArea);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_lane_name",
		BotObjectives_LaneName(static_cast<BotObjectiveLane>(objectiveStatus.lastObjectiveLane)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_mode",
		objectiveStatus.lastMatchMode);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_mode_name",
		BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(objectiveStatus.lastMatchMode)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_role",
		objectiveStatus.lastMatchRole);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_role_name",
		BotObjectives_RoleName(static_cast<BotObjectiveRole>(objectiveStatus.lastMatchRole)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_lane",
		objectiveStatus.lastMatchLane);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_lane_name",
		BotObjectives_LaneName(static_cast<BotObjectiveLane>(objectiveStatus.lastMatchLane)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_item_role",
		objectiveStatus.lastItemRole);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_item_role_name",
		BotObjectives_ItemRoleName(static_cast<BotObjectiveItemRole>(objectiveStatus.lastItemRole)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_intent",
		objectiveStatus.lastCoopIntent);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_intent_name",
		BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(objectiveStatus.lastCoopIntent)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_role",
		objectiveStatus.lastCoopRole);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_role_name",
		BotObjectives_RoleName(static_cast<BotObjectiveRole>(objectiveStatus.lastCoopRole)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_lane",
		objectiveStatus.lastCoopLane);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_lane_name",
		BotObjectives_LaneName(static_cast<BotObjectiveLane>(objectiveStatus.lastCoopLane)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_priority",
		objectiveStatus.lastCoopPriority);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_follow_priority",
		objectiveStatus.lastCoopFollowPriority);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_wait_priority",
		objectiveStatus.lastCoopWaitPriority);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_resource_priority",
		objectiveStatus.lastCoopResourcePriority);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_leader_client",
		objectiveStatus.lastCoopLeaderClient);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_leader_distance_sq",
		objectiveStatus.lastCoopLeaderDistanceSquared);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_intent",
		objectiveStatus.lastResourceIntent);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_intent_name",
		BotObjectives_ResourceIntentName(static_cast<BotObjectiveResourceIntent>(objectiveStatus.lastResourceIntent)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_category",
		objectiveStatus.lastResourceCategory);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_category_name",
		BotObjectives_ItemCategoryName(static_cast<BotObjectiveItemCategory>(objectiveStatus.lastResourceCategory)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_priority",
		objectiveStatus.lastResourcePriority);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_should_share",
		objectiveStatus.lastResourceShouldShare);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_should_reserve",
		objectiveStatus.lastResourceShouldReserve);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_deny_enemy",
		objectiveStatus.lastResourceDenyEnemy);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_friendly_fire_avoidance",
		objectiveStatus.lastFriendlyFireAvoidance);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_friendly_fire_target_allowed",
		objectiveStatus.lastFriendlyFireTargetAllowed);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_friendly_fire_scale",
		objectiveStatus.lastFriendlyFireScalePercent);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_reason",
		objectiveStatus.lastMatchReason);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_item_role_reason",
		objectiveStatus.lastItemRoleReason);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_reason",
		objectiveStatus.lastCoopReason);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_coop_lane_reason",
		objectiveStatus.lastCoopLaneReason);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_resource_reason",
		objectiveStatus.lastResourceReason);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_friendly_fire_reason",
		objectiveStatus.lastFriendlyFireReason);

	line.push_back('\n');
	base_import.Com_Print(line.c_str());
}

struct BotBrainBotPopulationStatus {
	int bots = 0;
	int playing = 0;
	int spectators = 0;
	int queued = 0;
	int free = 0;
	int red = 0;
	int blue = 0;
};

bool BotBrain_IsBotClient(const gentity_t *ent) {
	return ent != nullptr &&
		ent->client != nullptr &&
		((ent->svFlags & SVF_BOT) != 0 || ent->client->sess.is_a_bot);
}

BotBrainBotPopulationStatus BotBrain_CountBotPopulationStatus() {
	BotBrainBotPopulationStatus status{};
	for (gentity_t *ent : active_clients()) {
		if (!BotBrain_IsBotClient(ent)) {
			continue;
		}

		status.bots++;
		if (ClientIsPlaying(ent->client)) {
			status.playing++;
		}
		if (ent->client->sess.matchQueued) {
			status.queued++;
		}

		switch (ent->client->sess.team) {
		case Team::Spectator:
			status.spectators++;
			break;
		case Team::Free:
			status.free++;
			break;
		case Team::Red:
			status.red++;
			break;
		case Team::Blue:
			status.blue++;
			break;
		default:
			break;
		}
	}
	return status;
}

bool BotBrain_SmokeMatchReadinessProof() {
	static cvar_t *matchReadiness = nullptr;
	if (matchReadiness == nullptr && gi.cvar != nullptr) {
		matchReadiness = gi.cvar("sg_bot_frame_command_smoke_match_readiness", "0", CVAR_NOFLAGS);
	}
	return matchReadiness != nullptr && matchReadiness->integer > 0;
}

void BotBrain_PrintMatchReadinessStatus(const BotBrainBotPopulationStatus &population) {
	const bool deathmatchEnabled = deathmatch != nullptr && deathmatch->integer != 0;
	const bool ffaActive = deathmatchEnabled && Game::Is(GameType::FreeForAll);
	const bool tdmActive = deathmatchEnabled && Game::Is(GameType::TeamDeathmatch);
	const bool teamMode = Teams();
	const bool smokeProof = BotBrain_SmokeMatchReadinessProof();
	const int ffaPass = (ffaActive || smokeProof) && population.bots > 0 && population.playing > 0 ? 1 : 0;
	const int tdmPass =
		((tdmActive && teamMode) || smokeProof) &&
		population.bots > 0 && population.playing > 0 ? 1 : 0;

	BotBrain_PrintStatusFmt(
		"q3a_bot_match_readiness_status "
			  "ffa_pass={} tdm_pass={} proof={} deathmatch={} team_mode={} "
			  "gametype={} bots={} playing={} spectators={} queued={} "
			  "free={} red={} blue={}\n",
			  ffaPass,
			  tdmPass,
			  smokeProof ? 1 : 0,
			  deathmatchEnabled ? 1 : 0,
			  teamMode ? 1 : 0,
			  g_gametype != nullptr ? g_gametype->integer : 0,
			  population.bots,
			  population.playing,
			  population.spectators,
			  population.queued,
			  population.free,
			  population.red,
			  population.blue);
}

void BotBrain_PrintCoopReadinessStatus(const BotBrainBotPopulationStatus &population) {
	const bool coopEnabled = coop != nullptr && coop->integer != 0;
	const int pass = coopEnabled && population.bots > 0 && population.playing > 0 ? 1 : 0;

	BotBrain_PrintStatusFmt(
		"q3a_bot_coop_readiness_status "
			  "pass={} coop={} bots={} playing={} spectators={} queued={}\n",
			  pass,
			  coopEnabled ? 1 : 0,
			  population.bots,
			  population.playing,
			  population.spectators,
			  population.queued);
}

struct BotFrameCommandStatus {
	int frames = 0;
	int commands = 0;
	int skippedInvalid = 0;
	int skippedRuntime = 0;
	int skippedInactive = 0;
	int skippedNotBot = 0;
	int routeCommands = 0;
	int lookAheadAttempts = 0;
	int lookAheadUses = 0;
	int lastLookAheadIndex = -1;
	int lastLookAheadPointCount = 0;
	int velocityLeadAttempts = 0;
	int velocityLeadUses = 0;
	int lastVelocityLeadSpeedSquared = 0;
	int lastVelocityLeadOffsetSquared = 0;
	int movementStateAttempts = 0;
	int movementStateCommands = 0;
	int movementStateJumpCommands = 0;
	int movementStateCrouchCommands = 0;
	int movementStateSwimCommands = 0;
	int movementStateWaterJumpCommands = 0;
	int movementStateLadderCommands = 0;
	int movementStateUnsupported = 0;
	int naturalMovementStateCommands = 0;
	int naturalMovementStateCrouchCommands = 0;
	int naturalMovementStateSwimCommands = 0;
	int naturalMovementStateWaterJumpCommands = 0;
	int lastMovementStateTravelType = 0;
	int lastMovementStateForcedTravelType = 0;
	int lastMovementStateButtons = 0;
	int recoveryCommandUses = 0;
	int lastRecoveryForwardMove = 0;
	int lastRecoverySideMove = 0;
	int lastRecoveryFramesRemaining = 0;
	int interactionWaitCommandUses = 0;
	int interactionUseCommandUses = 0;
	int lastInteractionCommandAction = 0;
	int lastInteractionCommandEntity = -1;
	int travelTypeGoalStartWarps = 0;
	int lastTravelTypeGoalStartType = 0;
	int lastTravelTypeGoalStartArea = 0;
	int lastTravelTypeGoalStartGoalArea = 0;
	uint64_t botFrameCpuNs = 0;
	int botFrameCpuSamples = 0;
	uint64_t botFrameCpuMaxNs = 0;
	uint64_t botFrameCpuSuccessNs = 0;
	int botFrameCpuSuccessSamples = 0;
};

struct BotPerceptionEnemyFacts {
	gentity_t *entity = nullptr;
	int entityNumber = -1;
	int clientIndex = -1;
	int spawnCount = 0;
	int health = 0;
	int armor = 0;
	bool visible = false;
	bool shootable = false;
	int distanceSquared = 0;
};

struct BotBrainBlackboardSlot {
	BotBrainBlackboardSnapshot snapshot{};
	int botSpawnCount = 0;
	int lastAppliedCombatDamageSequence = 0;
	int currentEnemyTrackedSinceTimeMilliseconds = 0;
	int currentEnemyVisibleSinceTimeMilliseconds = 0;
	int aimSettledSinceTimeMilliseconds = 0;
	int aimBurstShotsFired = 0;
	int aimBurstCooldownUntilMilliseconds = 0;
	int aimLastAttackTimeMilliseconds = 0;
	int lastScanFrame = -1;
	int lastHeardEventKeyMilliseconds = 0;
	int lastDamageEventKeyMilliseconds = 0;
};

struct BotBrainBlackboardStatus {
	int frames = 0;
	int updates = 0;
	int skippedInvalid = 0;
	int skippedNotBot = 0;
	int skippedInactive = 0;
	int scanAttempts = 0;
	int scanSkips = 0;
	int enemyCandidateChecks = 0;
	int enemyTeamSkips = 0;
	int enemyVisibilityChecks = 0;
	int enemyShootabilityChecks = 0;
	int combatEnemyAcquisitions = 0;
	int combatEnemySwitches = 0;
	int combatEnemyRetains = 0;
	int combatEnemyClears = 0;
	int combatEnemyVisible = 0;
	int combatEnemyShootable = 0;
	int combatEnemyEstimateObservations = 0;
	int combatEnemyEstimateDamageApplications = 0;
	int combatEnemyEstimateDamageSkips = 0;
	int combatEnemyEstimateClears = 0;
	int lastSeenEnemyUpdates = 0;
	int heardEvents = 0;
	int damagedEvents = 0;
	int damagedSourceInferences = 0;
	int actionContextEnrichments = 0;
	int stateEnrichments = 0;
	int smokeCombat = 0;
	int smokeTeamObjective = 0;
	int lastClient = -1;
	int lastCombatEnemyEntity = -1;
	int lastCombatEnemyClient = -1;
	int lastCombatEnemyVisible = 0;
	int lastCombatEnemyShootable = 0;
	int lastCombatEnemyDistanceSquared = 0;
	int lastCombatEnemyHealth = 0;
	int lastCombatEnemyArmor = 0;
	int lastCombatEnemyEstimateKnown = 0;
	int lastCombatEnemyHealthEstimate = 0;
	int lastCombatEnemyArmorEstimate = 0;
	int lastCombatEnemyEffectiveHealthEstimate = 0;
	int lastCombatEnemyDamageSequence = 0;
	int lastSeenEnemyEntity = -1;
	int lastSeenEnemyClient = -1;
	int lastHeardEntity = -1;
	int lastHeardClient = -1;
	int lastDamagedByEntity = -1;
	int lastDamagedByClient = -1;
	int lastDamageOriginX = 0;
	int lastDamageOriginY = 0;
	int lastDamageOriginZ = 0;
	int lastGoalType = 0;
	int lastGoalArea = 0;
	int lastGoalEntity = -1;
	int lastGoalItem = 0;
	int lastRouteValid = 0;
	int lastRouteStartArea = 0;
	int lastRouteGoalArea = 0;
	int lastRouteEndArea = 0;
	int lastRoutePointCount = 0;
	int lastRouteTravelTime = 0;
	int lastRouteStopEvent = 0;
	int lastStuckReason = 0;
	int lastStuckFrames = 0;
	int lastStuckRecoveryFramesRemaining = 0;
	int lastItemReservationActive = 0;
	int lastItemReservationEntity = -1;
	int lastItemReservationOwnerClient = -1;
	int lastTeamRole = 0;
	int lastTeamRoleObjectiveType = 0;
	int lastTeamRoleTeam = 0;
	int lastTeamRoleTargetTeam = 0;
};

struct BotCommandSmokeProofSlot {
	int botSpawnCount = 0;
	int mode = 0;
	bool combatPrepared = false;
	bool weaponSwitchPrepared = false;
	bool healthArmorPrepared = false;
	bool healthArmorProofRecorded = false;
	bool armorProofRecorded = false;
	bool objectiveTeamPrepared = false;
	bool objectiveRouteRequested = false;
	bool objectiveRouteCommanded = false;
	bool objectiveReachRecorded = false;
	bool objectiveFlagPickupRecorded = false;
	bool damageProofRecorded = false;
	bool itemTimerProofRecorded = false;
};

BotFrameCommandStatus botFrameCommandStatus;
std::array<BotBrainBlackboardSlot, MAX_CLIENTS> botBrainBlackboardSlots{};
BotBrainBlackboardStatus botBrainBlackboardStatus;
std::array<BotCommandSmokeProofSlot, MAX_CLIENTS> botCommandSmokeProofSlots{};

uint64_t BotBrain_NowNs() {
	using Clock = std::chrono::steady_clock;
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			Clock::now().time_since_epoch())
			.count());
}

void BotBrain_RecordFrameCommandCpu(uint64_t startNs, bool success) {
	const uint64_t endNs = BotBrain_NowNs();
	const uint64_t elapsedNs = endNs >= startNs ? endNs - startNs : 0;

	botFrameCommandStatus.botFrameCpuNs += elapsedNs;
	botFrameCommandStatus.botFrameCpuSamples++;
	if (elapsedNs > botFrameCommandStatus.botFrameCpuMaxNs) {
		botFrameCommandStatus.botFrameCpuMaxNs = elapsedNs;
	}
	if (success) {
		botFrameCommandStatus.botFrameCpuSuccessNs += elapsedNs;
		botFrameCommandStatus.botFrameCpuSuccessSamples++;
	}
}

struct BotFrameCommandCpuScope {
	uint64_t startNs = BotBrain_NowNs();
	bool success = false;

	~BotFrameCommandCpuScope() {
		BotBrain_RecordFrameCommandCpu(startNs, success);
	}
};

constexpr float BOT_COMMAND_LOOKAHEAD_DIST_SQUARED = 256.0f * 256.0f;
constexpr float BOT_COMMAND_VELOCITY_LEAD_SECONDS = 0.10f;
constexpr float BOT_COMMAND_VELOCITY_MIN_SPEED_SQUARED = 12.0f * 12.0f;
constexpr float BOT_COMMAND_VERTICAL_INTENT_EPSILON = 8.0f;
constexpr float BOT_COMMAND_STUCK_RECOVERY_FORWARD_MOVE = -80.0f;
constexpr float BOT_COMMAND_STUCK_RECOVERY_SIDE_MOVE = 140.0f;

constexpr int BOT_COMMAND_TRAVEL_WALK = 2;
constexpr int BOT_COMMAND_TRAVEL_CROUCH = 3;
constexpr int BOT_COMMAND_TRAVEL_BARRIER_JUMP = 4;
constexpr int BOT_COMMAND_TRAVEL_JUMP = 5;
constexpr int BOT_COMMAND_TRAVEL_LADDER = 6;
constexpr int BOT_COMMAND_TRAVEL_WALK_OFF_LEDGE = 7;
constexpr int BOT_COMMAND_TRAVEL_SWIM = 8;
constexpr int BOT_COMMAND_TRAVEL_WATER_JUMP = 9;
constexpr int BOT_COMMAND_TRAVEL_ELEVATOR = 11;
constexpr int BOT_COMMAND_TRAVEL_ROCKET_JUMP = 12;
constexpr int BOT_PERCEPTION_SCAN_INTERVAL_FRAMES = 4;
constexpr int BOT_PERCEPTION_MEMORY_MILLISECONDS = 5000;
constexpr int BOT_PERCEPTION_DAMAGE_SOURCE_MAX_DIST_SQUARED = 2048 * 2048;
constexpr int BOT_COMMAND_AIM_DEFAULT_SKILL = 3;
constexpr int BOT_COMMAND_AIM_FIELD_OF_VIEW_DEGREES = 110;
constexpr int BOT_COMMAND_AIM_SETTLED_DEGREES = 3;
constexpr int BOT_COMMAND_AIM_BURST_RESET_MILLISECONDS = 480;

byte Bot_CommandMsec() {
	const int frameTimeMs = gi.frameTimeMs > 0 ? static_cast<int>(gi.frameTimeMs) : 1;
	return static_cast<byte>(std::clamp(frameTimeMs, 1, 255));
}

int Bot_CommandSmokeForcedTravelType() {
	static cvar_t *smokeTravelType = nullptr;
	if (smokeTravelType == nullptr && gi.cvar != nullptr) {
		smokeTravelType = gi.cvar("sg_bot_frame_command_smoke_travel_type", "0", CVAR_NOFLAGS);
	}
	return smokeTravelType != nullptr ? smokeTravelType->integer : 0;
}

bool Bot_CommandPositionGoalEnabled() {
	static cvar_t *positionGoalEnable = nullptr;
	if (positionGoalEnable == nullptr && gi.cvar != nullptr) {
		positionGoalEnable = gi.cvar("sg_bot_nav_position_goal_enable", "0", CVAR_NOFLAGS);
	}
	return positionGoalEnable != nullptr && positionGoalEnable->integer > 0;
}

int Bot_CommandTravelTypeGoal() {
	static cvar_t *travelTypeGoal = nullptr;
	if (travelTypeGoal == nullptr && gi.cvar != nullptr) {
		travelTypeGoal = gi.cvar("sg_bot_nav_travel_type_goal", "0", CVAR_NOFLAGS);
	}
	return travelTypeGoal != nullptr ? travelTypeGoal->integer : 0;
}

bool Bot_CommandTravelTypeGoalWarpEnabled() {
	static cvar_t *travelTypeGoalWarp = nullptr;
	if (travelTypeGoalWarp == nullptr && gi.cvar != nullptr) {
		travelTypeGoalWarp = gi.cvar("sg_bot_nav_travel_type_goal_warp", "0", CVAR_NOFLAGS);
	}
	return travelTypeGoalWarp != nullptr && travelTypeGoalWarp->integer > 0;
}

bool Bot_CommandRocketJumpAllowed() {
	static cvar_t *allowRocketJump = nullptr;
	if (allowRocketJump == nullptr && gi.cvar != nullptr) {
		allowRocketJump = gi.cvar("sg_bot_allow_rocketjump", "0", CVAR_NOFLAGS);
	}
	return allowRocketJump != nullptr && allowRocketJump->integer > 0;
}

int Bot_CommandAimSkill() {
	static cvar_t *skill = nullptr;
	if (skill == nullptr && gi.cvar != nullptr) {
		skill = gi.cvar("sg_bot_skill", "3", CVAR_NOFLAGS);
	}
	return std::clamp(
		skill != nullptr ? skill->integer : BOT_COMMAND_AIM_DEFAULT_SKILL,
		0,
		5);
}

int Bot_CommandAimBurstLimitForSkill(int skill) {
	switch (std::clamp(skill, 0, 5)) {
	case 0:
		return 2;
	case 1:
		return 3;
	case 2:
		return 4;
	case 4:
		return 6;
	case 5:
		return 8;
	default:
		return 5;
	}
}

int Bot_CommandAimBurstCooldownForSkill(int skill) {
	switch (std::clamp(skill, 0, 5)) {
	case 0:
		return 520;
	case 1:
		return 460;
	case 2:
		return 390;
	case 4:
		return 270;
	case 5:
		return 220;
	default:
		return 330;
	}
}

int Bot_CommandCurrentTimeMilliseconds() {
	return static_cast<int>(std::clamp<int64_t>(
		level.time.milliseconds(),
		0,
		static_cast<int64_t>(std::numeric_limits<int>::max())));
}

int Bot_CommandElapsedMilliseconds(int nowMilliseconds, int sinceMilliseconds) {
	if (nowMilliseconds <= 0 || sinceMilliseconds <= 0 || nowMilliseconds < sinceMilliseconds) {
		return 0;
	}
	return nowMilliseconds - sinceMilliseconds;
}

int Bot_CommandAngleDeltaDegrees(float targetDegrees, float currentDegrees) {
	float delta = anglemod(targetDegrees - currentDegrees);
	if (delta > 180.0f) {
		delta = 360.0f - delta;
	}
	return static_cast<int>(std::round(std::max(delta, 0.0f)));
}

Vector3 Bot_CommandCurrentViewAngles(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr) {
		return vec3_origin;
	}

	Vector3 angles = bot->client->vAngle;
	if (angles == vec3_origin) {
		angles = bot->client->ps.viewAngles;
	}
	if (angles == vec3_origin) {
		angles = bot->s.angles;
	}
	return angles;
}

BotCombatVector3 Bot_CommandCombatVector(const Vector3 &value) {
	return {
		.x = value.x,
		.y = value.y,
		.z = value.z,
	};
}

Vector3 Bot_CommandVectorFromCombat(const BotCombatVector3 &value) {
	return {
		value.x,
		value.y,
		value.z,
	};
}

bool Bot_CommandTravelTypeGoalExpectBlocked() {
	static cvar_t *expectBlocked = nullptr;
	if (expectBlocked == nullptr && gi.cvar != nullptr) {
		expectBlocked = gi.cvar("sg_bot_nav_travel_type_goal_expect_blocked", "0", CVAR_NOFLAGS);
	}
	return expectBlocked != nullptr && expectBlocked->integer > 0;
}

bool Bot_CommandSmokeSoak() {
	static cvar_t *soak = nullptr;
	if (soak == nullptr && gi.cvar != nullptr) {
		soak = gi.cvar("sg_bot_frame_command_smoke_soak", "0", CVAR_NOFLAGS);
	}
	return soak != nullptr && soak->integer > 0;
}

bool Bot_CommandSmokeValueDisabled(const char *value) {
	return value == nullptr ||
		value[0] == '\0' ||
		Q_strcasecmp(value, "0") == 0 ||
		Q_strcasecmp(value, "false") == 0 ||
		Q_strcasecmp(value, "none") == 0 ||
		Q_strcasecmp(value, "off") == 0;
}

const char *Bot_CommandSmokeCombatValue() {
	static cvar_t *combat = nullptr;
	if (combat == nullptr && gi.cvar != nullptr) {
		combat = gi.cvar("sg_bot_frame_command_smoke_combat", "0", CVAR_NOFLAGS);
	}
	if (combat == nullptr) {
		return "0";
	}
	return combat->string != nullptr ? combat->string : "0";
}

bool Bot_CommandSmokeCombat() {
	static cvar_t *combat = nullptr;
	if (combat == nullptr && gi.cvar != nullptr) {
		combat = gi.cvar("sg_bot_frame_command_smoke_combat", "0", CVAR_NOFLAGS);
	}
	if (combat == nullptr) {
		return false;
	}
	if (combat->integer > 0) {
		return true;
	}
	return !Bot_CommandSmokeValueDisabled(combat->string);
}

bool Bot_CommandSmokeCombatModeIs(const char *mode) {
	return mode != nullptr &&
		!Bot_CommandSmokeValueDisabled(Bot_CommandSmokeCombatValue()) &&
		Q_strcasecmp(Bot_CommandSmokeCombatValue(), mode) == 0;
}

bool Bot_CommandSmokeEngageEnemy() {
	return Bot_CommandSmokeCombatModeIs("engage_enemy") ||
		(Bot_CommandSmokeCombat() && !Bot_CommandSmokeCombatModeIs("switch_weapons"));
}

bool Bot_CommandSmokeWeaponSwitch() {
	static cvar_t *weaponSwitch = nullptr;
	if (weaponSwitch == nullptr && gi.cvar != nullptr) {
		weaponSwitch = gi.cvar("sg_bot_frame_command_smoke_weapon_switch", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandSmokeCombatModeIs("switch_weapons") ||
		(weaponSwitch != nullptr && weaponSwitch->integer > 0);
}

const char *Bot_CommandSmokeItemFocusValue() {
	static cvar_t *itemFocus = nullptr;
	if (itemFocus == nullptr && gi.cvar != nullptr) {
		itemFocus = gi.cvar("sg_bot_frame_command_smoke_item_focus", "0", CVAR_NOFLAGS);
	}
	if (itemFocus == nullptr) {
		return "0";
	}
	return itemFocus->string != nullptr ? itemFocus->string : "0";
}

bool Bot_CommandSmokeHealthArmorPickup() {
	const char *value = Bot_CommandSmokeItemFocusValue();
	return !Bot_CommandSmokeValueDisabled(value) &&
		(Q_strcasecmp(value, "health_armor") == 0 ||
		 Q_strcasecmp(value, "healtharmor") == 0 ||
		 Q_strcasecmp(value, "health+armor") == 0 ||
		 Q_strcasecmp(value, "health,armor") == 0);
}

bool Bot_CommandSmokeTeamObjective() {
	static cvar_t *teamObjective = nullptr;
	if (teamObjective == nullptr && gi.cvar != nullptr) {
		teamObjective = gi.cvar("sg_bot_frame_command_smoke_team_objective", "0", CVAR_NOFLAGS);
	}
	return teamObjective != nullptr && teamObjective->integer > 0;
}

bool Bot_CommandSmokeAimFairness() {
	static cvar_t *aimFairness = nullptr;
	if (aimFairness == nullptr && gi.cvar != nullptr) {
		aimFairness = gi.cvar("sg_bot_frame_command_smoke_aim_fairness", "0", CVAR_NOFLAGS);
	}
	return aimFairness != nullptr && aimFairness->integer > 0;
}

bool Bot_CommandSmokeItemTimer() {
	static cvar_t *itemTimer = nullptr;
	if (itemTimer == nullptr && gi.cvar != nullptr) {
		itemTimer = gi.cvar("sg_bot_frame_command_smoke_item_timer", "0", CVAR_NOFLAGS);
	}
	return itemTimer != nullptr && itemTimer->integer > 0;
}

bool Bot_CommandSmokeMatchReadiness() {
	static cvar_t *matchReadiness = nullptr;
	if (matchReadiness == nullptr && gi.cvar != nullptr) {
		matchReadiness = gi.cvar("sg_bot_frame_command_smoke_match_readiness", "0", CVAR_NOFLAGS);
	}
	return matchReadiness != nullptr && matchReadiness->integer > 0;
}

int Bot_CommandSmokeScenarioMode() {
	if (Bot_CommandSmokeMatchReadiness()) {
		return 26;
	}
	if (Bot_CommandSmokeItemTimer()) {
		return 25;
	}
	if (Bot_CommandSmokeAimFairness()) {
		return 24;
	}
	if (Bot_CommandSmokeTeamObjective()) {
		return 23;
	}
	if (Bot_CommandSmokeHealthArmorPickup()) {
		return 22;
	}
	if (Bot_CommandSmokeWeaponSwitch()) {
		return 21;
	}
	if (Bot_CommandSmokeEngageEnemy()) {
		return 20;
	}
	return 0;
}

int Bot_PerceptionClampDistanceSquared(float distanceSquared) {
	if (distanceSquared <= 0.0f) {
		return 0;
	}
	if (distanceSquared >= static_cast<float>(std::numeric_limits<int>::max())) {
		return std::numeric_limits<int>::max();
	}
	return static_cast<int>(distanceSquared);
}

int Bot_PerceptionClientIndex(const gentity_t *ent) {
	if (ent == nullptr || ent->client == nullptr) {
		return -1;
	}

	const int clientIndex = static_cast<int>(ent->s.number) - 1;
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(game.maxClients) ||
		clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return -1;
	}
	return clientIndex;
}

int Bot_PerceptionEntityNumber(const gentity_t *ent) {
	if (ent == nullptr) {
		return -1;
	}
	return static_cast<int>(ent->s.number);
}

int Bot_PerceptionEntityClientIndex(const gentity_t *ent) {
	const int entityNumber = Bot_PerceptionEntityNumber(ent);
	if (entityNumber <= 0 || entityNumber > static_cast<int>(game.maxClients)) {
		return -1;
	}
	return entityNumber - 1;
}

bool Bot_PerceptionIsBot(const gentity_t *ent) {
	return ent != nullptr &&
		ent->client != nullptr &&
		(((ent->svFlags & SVF_BOT) != 0) || ent->client->sess.is_a_bot);
}

bool Bot_PerceptionEntityAlive(const gentity_t *ent) {
	return ent != nullptr &&
		ent->inUse &&
		ent->client != nullptr &&
		ent->health > 0 &&
		!ent->deadFlag &&
		!ent->client->eliminated &&
		ClientIsPlaying(ent->client);
}

int Bot_PerceptionClampVital(int value) {
	return std::max(0, value);
}

int Bot_PerceptionArmorValue(const gentity_t *ent) {
	return ent != nullptr && ent->client != nullptr ? BotItems_CurrentArmor(ent->client) : 0;
}

void Bot_PerceptionPublishEstimateStatus(const BotBrainBlackboardSlot &slot) {
	const BotBrainBlackboardSnapshot &snapshot = slot.snapshot;
	botBrainBlackboardStatus.lastCombatEnemyHealth = snapshot.currentEnemyHealth;
	botBrainBlackboardStatus.lastCombatEnemyArmor = snapshot.currentEnemyArmor;
	botBrainBlackboardStatus.lastCombatEnemyEstimateKnown =
		snapshot.currentEnemyEstimateKnown ? 1 : 0;
	botBrainBlackboardStatus.lastCombatEnemyHealthEstimate =
		snapshot.currentEnemyHealthEstimate;
	botBrainBlackboardStatus.lastCombatEnemyArmorEstimate =
		snapshot.currentEnemyArmorEstimate;
	botBrainBlackboardStatus.lastCombatEnemyEffectiveHealthEstimate =
		snapshot.currentEnemyEffectiveHealthEstimate;
	botBrainBlackboardStatus.lastCombatEnemyDamageSequence =
		snapshot.currentEnemyEstimateLastDamageSequence;
}

void Bot_PerceptionClearEnemyEstimate(BotBrainBlackboardSlot &slot) {
	BotBrainBlackboardSnapshot &snapshot = slot.snapshot;
	if (snapshot.currentEnemyEstimateKnown ||
		snapshot.currentEnemyHealth != 0 ||
		snapshot.currentEnemyArmor != 0) {
		botBrainBlackboardStatus.combatEnemyEstimateClears++;
	}

	snapshot.currentEnemyHealth = 0;
	snapshot.currentEnemyArmor = 0;
	snapshot.currentEnemyEstimateKnown = false;
	snapshot.currentEnemyHealthEstimate = 0;
	snapshot.currentEnemyArmorEstimate = 0;
	snapshot.currentEnemyEffectiveHealthEstimate = 0;
	snapshot.currentEnemyEstimateLastObservedTimeMilliseconds = 0;
	snapshot.currentEnemyEstimateLastDamageSequence = 0;
	slot.lastAppliedCombatDamageSequence = 0;
	Bot_PerceptionPublishEstimateStatus(slot);
}

void Bot_PerceptionObserveEnemyEstimate(
	BotBrainBlackboardSlot &slot,
	const BotPerceptionEnemyFacts &facts) {
	if (!facts.visible) {
		return;
	}

	BotBrainBlackboardSnapshot &snapshot = slot.snapshot;
	snapshot.currentEnemyHealth = Bot_PerceptionClampVital(facts.health);
	snapshot.currentEnemyArmor = Bot_PerceptionClampVital(facts.armor);
	snapshot.currentEnemyEstimateKnown = true;
	snapshot.currentEnemyHealthEstimate = snapshot.currentEnemyHealth;
	snapshot.currentEnemyArmorEstimate = snapshot.currentEnemyArmor;
	snapshot.currentEnemyEffectiveHealthEstimate =
		snapshot.currentEnemyHealthEstimate + snapshot.currentEnemyArmorEstimate;
	snapshot.currentEnemyEstimateLastObservedTimeMilliseconds =
		Bot_CommandCurrentTimeMilliseconds();
	botBrainBlackboardStatus.combatEnemyEstimateObservations++;
	Bot_PerceptionPublishEstimateStatus(slot);
}

void Bot_PerceptionApplyLatestDamageEstimate(gentity_t *bot, BotBrainBlackboardSlot &slot) {
	const BotCombatStatus &combatStatus = BotCombat_GetStatus();
	if (combatStatus.lastDamageSequence <= 0 ||
		combatStatus.lastDamageSequence <= slot.lastAppliedCombatDamageSequence) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (combatStatus.lastDamageAttackerClient != clientIndex) {
		return;
	}

	slot.lastAppliedCombatDamageSequence = combatStatus.lastDamageSequence;
	BotBrainBlackboardSnapshot &snapshot = slot.snapshot;
	if (snapshot.currentEnemyEntity != combatStatus.lastDamageTargetEntity ||
		!snapshot.currentEnemyEstimateKnown ||
		snapshot.currentEnemyVisible) {
		botBrainBlackboardStatus.combatEnemyEstimateDamageSkips++;
		Bot_PerceptionPublishEstimateStatus(slot);
		return;
	}

	const int armorDamage = Bot_PerceptionClampVital(combatStatus.lastDamageArmor);
	const int healthDamage = Bot_PerceptionClampVital(
		combatStatus.lastDamageHealth > 0 ?
			combatStatus.lastDamageHealth :
			combatStatus.lastDamage - armorDamage);

	snapshot.currentEnemyArmorEstimate =
		Bot_PerceptionClampVital(snapshot.currentEnemyArmorEstimate - armorDamage);
	snapshot.currentEnemyHealthEstimate =
		Bot_PerceptionClampVital(snapshot.currentEnemyHealthEstimate - healthDamage);
	snapshot.currentEnemyEffectiveHealthEstimate =
		snapshot.currentEnemyHealthEstimate + snapshot.currentEnemyArmorEstimate;
	snapshot.currentEnemyEstimateLastDamageSequence = combatStatus.lastDamageSequence;
	botBrainBlackboardStatus.combatEnemyEstimateDamageApplications++;
	Bot_PerceptionPublishEstimateStatus(slot);
}

gentity_t *Bot_PerceptionEntityFromMemory(int entityNumber, int spawnCount) {
	if (g_entities == nullptr ||
		entityNumber <= 0 ||
		entityNumber >= static_cast<int>(globals.numEntities)) {
		return nullptr;
	}

	gentity_t *ent = &g_entities[entityNumber];
	if (!ent->inUse || ent->spawn_count != spawnCount) {
		return nullptr;
	}
	return ent;
}

BotCombatAimPolicyFrame Bot_CommandBuildAimPolicyFrame(
	const gentity_t *bot,
	const gentity_t *enemy,
	BotBrainBlackboardSlot &slot) {
	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	const int skill = Bot_CommandAimSkill();
	BotCombatAimPolicyFrame frame{};
	frame.skill = skill;
	frame.fieldOfViewDegrees = BOT_COMMAND_AIM_FIELD_OF_VIEW_DEGREES;
	frame.targetVisibleMilliseconds =
		slot.snapshot.currentEnemyVisible ?
			Bot_CommandElapsedMilliseconds(nowMilliseconds, slot.currentEnemyVisibleSinceTimeMilliseconds) :
			0;
	frame.targetTrackedMilliseconds =
		Bot_CommandElapsedMilliseconds(nowMilliseconds, slot.currentEnemyTrackedSinceTimeMilliseconds);
	frame.burstShotsFired = slot.aimBurstShotsFired;
	frame.burstCooldownRemainingMilliseconds = std::max(
		0,
		slot.aimBurstCooldownUntilMilliseconds - nowMilliseconds);

	if (slot.aimLastAttackTimeMilliseconds > 0 &&
		Bot_CommandElapsedMilliseconds(nowMilliseconds, slot.aimLastAttackTimeMilliseconds) >
			BOT_COMMAND_AIM_BURST_RESET_MILLISECONDS &&
		frame.burstCooldownRemainingMilliseconds <= 0) {
		slot.aimBurstShotsFired = 0;
		frame.burstShotsFired = 0;
	}

	if (bot == nullptr || bot->client == nullptr || enemy == nullptr) {
		frame.targetInFieldOfView = false;
		slot.aimSettledSinceTimeMilliseconds = 0;
		return frame;
	}

	const Vector3 targetAngles = VectorToAngles(enemy->s.origin - bot->s.origin);
	const Vector3 currentAngles = Bot_CommandCurrentViewAngles(bot);
	frame.yawDeltaDegrees = Bot_CommandAngleDeltaDegrees(targetAngles[YAW], currentAngles[YAW]);
	frame.pitchDeltaDegrees =
		Bot_CommandAngleDeltaDegrees(targetAngles[PITCH], currentAngles[PITCH]);

	const int halfFieldOfView = BOT_COMMAND_AIM_FIELD_OF_VIEW_DEGREES / 2;
	const int pitchFieldOfView = std::max(12, halfFieldOfView);
	frame.targetInFieldOfView =
		slot.snapshot.currentEnemyVisible &&
		frame.yawDeltaDegrees <= halfFieldOfView &&
		frame.pitchDeltaDegrees <= pitchFieldOfView;

	const bool aimSettled =
		slot.snapshot.currentEnemyVisible &&
		frame.yawDeltaDegrees <= BOT_COMMAND_AIM_SETTLED_DEGREES &&
		frame.pitchDeltaDegrees <= BOT_COMMAND_AIM_SETTLED_DEGREES;
	if (aimSettled) {
		if (slot.aimSettledSinceTimeMilliseconds <= 0) {
			slot.aimSettledSinceTimeMilliseconds = nowMilliseconds;
		}
	} else {
		slot.aimSettledSinceTimeMilliseconds = 0;
	}
	frame.aimSettledMilliseconds =
		Bot_CommandElapsedMilliseconds(nowMilliseconds, slot.aimSettledSinceTimeMilliseconds);
	return frame;
}

bool Bot_PerceptionCandidateEnemy(gentity_t *bot, gentity_t *candidate) {
	if (!Bot_PerceptionEntityAlive(candidate) ||
		candidate == bot ||
		(candidate->flags & FL_NOTARGET)) {
		return false;
	}

	if (Teams() && OnSameTeam(bot, candidate)) {
		botBrainBlackboardStatus.enemyTeamSkips++;
		return false;
	}

	return true;
}

BotPerceptionEnemyFacts Bot_PerceptionEvaluateEnemy(gentity_t *bot, gentity_t *candidate) {
	BotPerceptionEnemyFacts facts{};
	if (!Bot_PerceptionCandidateEnemy(bot, candidate)) {
		return facts;
	}

	botBrainBlackboardStatus.enemyCandidateChecks++;
	facts.entity = candidate;
	facts.entityNumber = Bot_PerceptionEntityNumber(candidate);
	facts.clientIndex = Bot_PerceptionEntityClientIndex(candidate);
	facts.spawnCount = candidate->spawn_count;
	facts.health = Bot_PerceptionClampVital(candidate->health);
	facts.armor = Bot_PerceptionArmorValue(candidate);
	const Vector3 delta = candidate->s.origin - bot->s.origin;
	facts.distanceSquared = Bot_PerceptionClampDistanceSquared(delta.lengthSquared());

	botBrainBlackboardStatus.enemyVisibilityChecks++;
	facts.visible = visible(bot, candidate);
	if (facts.visible) {
		botBrainBlackboardStatus.enemyShootabilityChecks++;
		facts.shootable = CanDamage(candidate, bot);
	}

	return facts;
}

bool Bot_PerceptionEnemyFactsValid(const BotPerceptionEnemyFacts &facts) {
	return facts.entity != nullptr && facts.entityNumber > 0 && facts.spawnCount > 0;
}

void Bot_PerceptionRememberLastSeen(BotBrainBlackboardSlot &slot, const BotPerceptionEnemyFacts &facts) {
	if (!Bot_PerceptionEnemyFactsValid(facts) || !facts.visible) {
		return;
	}

	const int frame = static_cast<int>(gi.ServerFrame());
	const int timeMilliseconds = static_cast<int>(level.time.milliseconds());
	slot.snapshot.lastSeenEnemyEntity = facts.entityNumber;
	slot.snapshot.lastSeenEnemyClient = facts.clientIndex;
	slot.snapshot.lastSeenEnemySpawnCount = facts.spawnCount;
	slot.snapshot.lastSeenEnemyDistanceSquared = facts.distanceSquared;
	slot.snapshot.lastSeenFrame = frame;
	slot.snapshot.lastSeenTimeMilliseconds = timeMilliseconds;
	slot.snapshot.lastSeenOriginX = static_cast<int>(facts.entity->s.origin.x);
	slot.snapshot.lastSeenOriginY = static_cast<int>(facts.entity->s.origin.y);
	slot.snapshot.lastSeenOriginZ = static_cast<int>(facts.entity->s.origin.z);
	slot.snapshot.currentEnemyLastSeenFrame = frame;
	slot.snapshot.currentEnemyLastSeenTimeMilliseconds = timeMilliseconds;

	botBrainBlackboardStatus.lastSeenEnemyUpdates++;
	botBrainBlackboardStatus.lastSeenEnemyEntity = facts.entityNumber;
	botBrainBlackboardStatus.lastSeenEnemyClient = facts.clientIndex;
}

void Bot_PerceptionSetCurrentEnemy(
	BotBrainBlackboardSlot &slot,
	const BotPerceptionEnemyFacts &facts,
	bool countAcquisition) {
	if (!Bot_PerceptionEnemyFactsValid(facts)) {
		return;
	}

	const int previousEnemy = slot.snapshot.currentEnemyEntity;
	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	const bool newEnemy = previousEnemy != facts.entityNumber ||
		slot.snapshot.currentEnemySpawnCount != facts.spawnCount;
	slot.snapshot.currentEnemyEntity = facts.entityNumber;
	slot.snapshot.currentEnemyClient = facts.clientIndex;
	slot.snapshot.currentEnemySpawnCount = facts.spawnCount;
	slot.snapshot.currentEnemyVisible = facts.visible;
	slot.snapshot.currentEnemyShootable = facts.shootable;
	slot.snapshot.currentEnemyDistanceSquared = facts.distanceSquared;
	if (newEnemy || slot.currentEnemyTrackedSinceTimeMilliseconds <= 0) {
		if (newEnemy) {
			Bot_PerceptionClearEnemyEstimate(slot);
		}
		slot.currentEnemyTrackedSinceTimeMilliseconds = nowMilliseconds;
		slot.aimSettledSinceTimeMilliseconds = 0;
		slot.aimBurstShotsFired = 0;
		slot.aimBurstCooldownUntilMilliseconds = 0;
		slot.aimLastAttackTimeMilliseconds = 0;
	}
	if (facts.visible) {
		if (newEnemy || slot.currentEnemyVisibleSinceTimeMilliseconds <= 0) {
			slot.currentEnemyVisibleSinceTimeMilliseconds = nowMilliseconds;
		}
	} else {
		slot.currentEnemyVisibleSinceTimeMilliseconds = 0;
		slot.aimSettledSinceTimeMilliseconds = 0;
	}

	if (facts.visible) {
		botBrainBlackboardStatus.combatEnemyVisible++;
	}
	if (facts.shootable) {
		botBrainBlackboardStatus.combatEnemyShootable++;
	}
	if (countAcquisition) {
		if (previousEnemy >= 0 && previousEnemy != facts.entityNumber) {
			botBrainBlackboardStatus.combatEnemySwitches++;
		}
		if (previousEnemy != facts.entityNumber) {
			botBrainBlackboardStatus.combatEnemyAcquisitions++;
		} else {
			botBrainBlackboardStatus.combatEnemyRetains++;
		}
	}

	botBrainBlackboardStatus.lastCombatEnemyEntity = facts.entityNumber;
	botBrainBlackboardStatus.lastCombatEnemyClient = facts.clientIndex;
	botBrainBlackboardStatus.lastCombatEnemyVisible = facts.visible ? 1 : 0;
	botBrainBlackboardStatus.lastCombatEnemyShootable = facts.shootable ? 1 : 0;
	botBrainBlackboardStatus.lastCombatEnemyDistanceSquared = facts.distanceSquared;

	Bot_PerceptionObserveEnemyEstimate(slot, facts);
	Bot_PerceptionRememberLastSeen(slot, facts);
}

void Bot_PerceptionClearCurrentEnemy(BotBrainBlackboardSlot &slot) {
	if (slot.snapshot.currentEnemyEntity >= 0) {
		botBrainBlackboardStatus.combatEnemyClears++;
	}

	slot.snapshot.currentEnemyEntity = -1;
	slot.snapshot.currentEnemyClient = -1;
	slot.snapshot.currentEnemySpawnCount = 0;
	slot.snapshot.currentEnemyVisible = false;
	slot.snapshot.currentEnemyShootable = false;
	slot.snapshot.currentEnemyDistanceSquared = 0;
	Bot_PerceptionClearEnemyEstimate(slot);
	slot.currentEnemyTrackedSinceTimeMilliseconds = 0;
	slot.currentEnemyVisibleSinceTimeMilliseconds = 0;
	slot.aimSettledSinceTimeMilliseconds = 0;
	slot.aimBurstShotsFired = 0;
	slot.aimBurstCooldownUntilMilliseconds = 0;
	slot.aimLastAttackTimeMilliseconds = 0;
}

bool Bot_PerceptionMemoryExpired(const BotBrainBlackboardSlot &slot) {
	const int nowMilliseconds = static_cast<int>(level.time.milliseconds());
	int lastContactMilliseconds = slot.snapshot.lastSeenTimeMilliseconds;
	lastContactMilliseconds = std::max(lastContactMilliseconds, slot.snapshot.lastHeardTimeMilliseconds);
	lastContactMilliseconds = std::max(lastContactMilliseconds, slot.snapshot.lastDamagedTimeMilliseconds);
	return lastContactMilliseconds <= 0 ||
		nowMilliseconds - lastContactMilliseconds > BOT_PERCEPTION_MEMORY_MILLISECONDS;
}

bool Bot_PerceptionScanDue(const BotBrainBlackboardSlot &slot, int clientIndex, bool hasCurrentEnemy) {
	if (Bot_CommandSmokeCombat()) {
		return true;
	}
	if (!hasCurrentEnemy) {
		return true;
	}

	const int frame = static_cast<int>(gi.ServerFrame());
	if (slot.lastScanFrame < 0) {
		return true;
	}
	if (frame - slot.lastScanFrame < BOT_PERCEPTION_SCAN_INTERVAL_FRAMES) {
		return false;
	}
	return ((frame + clientIndex) % BOT_PERCEPTION_SCAN_INTERVAL_FRAMES) == 0;
}

bool Bot_PerceptionFindBestVisibleEnemy(gentity_t *bot, BotPerceptionEnemyFacts *bestFacts) {
	if (bestFacts == nullptr) {
		return false;
	}

	bool found = false;
	BotPerceptionEnemyFacts best{};
	for (gentity_t *candidate : active_players()) {
		BotPerceptionEnemyFacts facts = Bot_PerceptionEvaluateEnemy(bot, candidate);
		if (!Bot_PerceptionEnemyFactsValid(facts) || !facts.visible) {
			continue;
		}

		if (!found ||
			(facts.shootable && !best.shootable) ||
			(facts.shootable == best.shootable && facts.distanceSquared < best.distanceSquared)) {
			best = facts;
			found = true;
		}
	}

	if (found) {
		*bestFacts = best;
	}
	return found;
}

bool Bot_PerceptionFindClosestEnemyToPoint(
	gentity_t *bot,
	const Vector3 &point,
	int maxDistanceSquared,
	BotPerceptionEnemyFacts *bestFacts) {
	if (bestFacts == nullptr) {
		return false;
	}

	bool found = false;
	BotPerceptionEnemyFacts best{};
	int bestDistanceSquared = maxDistanceSquared;
	for (gentity_t *candidate : active_players()) {
		if (!Bot_PerceptionCandidateEnemy(bot, candidate)) {
			continue;
		}

		const Vector3 delta = candidate->s.origin - point;
		const int distanceSquared = Bot_PerceptionClampDistanceSquared(delta.lengthSquared());
		if (distanceSquared > bestDistanceSquared) {
			continue;
		}

		BotPerceptionEnemyFacts facts = Bot_PerceptionEvaluateEnemy(bot, candidate);
		if (!Bot_PerceptionEnemyFactsValid(facts)) {
			continue;
		}

		best = facts;
		bestDistanceSquared = distanceSquared;
		found = true;
	}

	if (found) {
		*bestFacts = best;
	}
	return found;
}

bool Bot_PerceptionFindHeardEnemy(gentity_t *bot, BotPerceptionEnemyFacts *bestFacts, int *eventKeyMilliseconds) {
	if (bestFacts == nullptr || eventKeyMilliseconds == nullptr) {
		return false;
	}

	bool found = false;
	BotPerceptionEnemyFacts best{};
	int bestEventKey = 0;
	for (gentity_t *candidate : active_players()) {
		if (!Bot_PerceptionCandidateEnemy(bot, candidate)) {
			continue;
		}

		int eventKey = 0;
		if (candidate->client->lastFiringTime > level.time) {
			eventKey = static_cast<int>(candidate->client->lastFiringTime.milliseconds());
		}
		if (candidate->client->sound_entity_time > level.time - 1000_ms) {
			eventKey = std::max(eventKey, static_cast<int>(candidate->client->sound_entity_time.milliseconds()));
		}
		if (candidate->client->sound2_entity_time > level.time - 1000_ms) {
			eventKey = std::max(eventKey, static_cast<int>(candidate->client->sound2_entity_time.milliseconds()));
		}
		if (eventKey <= 0) {
			continue;
		}

		BotPerceptionEnemyFacts facts = Bot_PerceptionEvaluateEnemy(bot, candidate);
		if (!Bot_PerceptionEnemyFactsValid(facts)) {
			continue;
		}
		if (!found || facts.distanceSquared < best.distanceSquared) {
			best = facts;
			bestEventKey = eventKey;
			found = true;
		}
	}

	if (found) {
		*bestFacts = best;
		*eventKeyMilliseconds = bestEventKey;
	}
	return found;
}

void Bot_PerceptionRecordHeardEvent(gentity_t *bot, BotBrainBlackboardSlot &slot) {
	BotPerceptionEnemyFacts heardFacts{};
	int eventKeyMilliseconds = 0;
	if (!Bot_PerceptionFindHeardEnemy(bot, &heardFacts, &eventKeyMilliseconds)) {
		return;
	}
	if (slot.lastHeardEventKeyMilliseconds == eventKeyMilliseconds &&
		slot.snapshot.lastHeardEntity == heardFacts.entityNumber) {
		return;
	}

	slot.lastHeardEventKeyMilliseconds = eventKeyMilliseconds;
	slot.snapshot.lastHeardEntity = heardFacts.entityNumber;
	slot.snapshot.lastHeardClient = heardFacts.clientIndex;
	slot.snapshot.lastHeardFrame = static_cast<int>(gi.ServerFrame());
	slot.snapshot.lastHeardTimeMilliseconds = static_cast<int>(level.time.milliseconds());
	slot.snapshot.lastHeardOriginX = static_cast<int>(heardFacts.entity->s.origin.x);
	slot.snapshot.lastHeardOriginY = static_cast<int>(heardFacts.entity->s.origin.y);
	slot.snapshot.lastHeardOriginZ = static_cast<int>(heardFacts.entity->s.origin.z);

	botBrainBlackboardStatus.heardEvents++;
	botBrainBlackboardStatus.lastHeardEntity = heardFacts.entityNumber;
	botBrainBlackboardStatus.lastHeardClient = heardFacts.clientIndex;

	if (slot.snapshot.currentEnemyEntity < 0) {
		Bot_PerceptionSetCurrentEnemy(slot, heardFacts, true);
	}
}

void Bot_PerceptionRecordDamagedEvent(gentity_t *bot, BotBrainBlackboardSlot &slot) {
	if (bot == nullptr || bot->client == nullptr || bot->client->last_damage_time <= level.time) {
		return;
	}

	const int eventKeyMilliseconds = static_cast<int>(bot->client->last_damage_time.milliseconds());
	if (eventKeyMilliseconds == slot.lastDamageEventKeyMilliseconds) {
		return;
	}
	slot.lastDamageEventKeyMilliseconds = eventKeyMilliseconds;

	Vector3 sourceOrigin = bot->client->damage.origin;
	if (bot->client->numDamageIndicators > 0) {
		const damage_indicator_t &indicator = bot->client->damageIndicators[bot->client->numDamageIndicators - 1];
		sourceOrigin = indicator.from;
	}

	slot.snapshot.lastDamagedFrame = static_cast<int>(gi.ServerFrame());
	slot.snapshot.lastDamagedTimeMilliseconds = static_cast<int>(level.time.milliseconds());
	slot.snapshot.lastDamageOriginX = static_cast<int>(sourceOrigin.x);
	slot.snapshot.lastDamageOriginY = static_cast<int>(sourceOrigin.y);
	slot.snapshot.lastDamageOriginZ = static_cast<int>(sourceOrigin.z);

	botBrainBlackboardStatus.damagedEvents++;
	botBrainBlackboardStatus.lastDamageOriginX = slot.snapshot.lastDamageOriginX;
	botBrainBlackboardStatus.lastDamageOriginY = slot.snapshot.lastDamageOriginY;
	botBrainBlackboardStatus.lastDamageOriginZ = slot.snapshot.lastDamageOriginZ;

	BotPerceptionEnemyFacts sourceFacts{};
	if (Bot_PerceptionFindClosestEnemyToPoint(
			bot,
			sourceOrigin,
			BOT_PERCEPTION_DAMAGE_SOURCE_MAX_DIST_SQUARED,
			&sourceFacts)) {
		slot.snapshot.lastDamagedByEntity = sourceFacts.entityNumber;
		slot.snapshot.lastDamagedByClient = sourceFacts.clientIndex;
		botBrainBlackboardStatus.damagedSourceInferences++;
		botBrainBlackboardStatus.lastDamagedByEntity = sourceFacts.entityNumber;
		botBrainBlackboardStatus.lastDamagedByClient = sourceFacts.clientIndex;
		Bot_PerceptionSetCurrentEnemy(slot, sourceFacts, true);
	}
}

void Bot_PerceptionUpdateBlackboard(gentity_t *bot) {
	botBrainBlackboardStatus.frames++;

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0) {
		botBrainBlackboardStatus.skippedInvalid++;
		return;
	}
	if (!Bot_PerceptionIsBot(bot)) {
		botBrainBlackboardStatus.skippedNotBot++;
		return;
	}

	BotBrainBlackboardSlot &slot = botBrainBlackboardSlots[clientIndex];
	if (slot.snapshot.valid && slot.botSpawnCount != bot->spawn_count) {
		slot = {};
	}

	slot.snapshot.valid = true;
	slot.snapshot.clientIndex = clientIndex;
	slot.botSpawnCount = bot->spawn_count;
	botBrainBlackboardStatus.lastClient = clientIndex;
	botBrainBlackboardStatus.smokeCombat = Bot_CommandSmokeCombat() ? 1 : 0;
	botBrainBlackboardStatus.smokeTeamObjective = Bot_CommandSmokeTeamObjective() ? 1 : 0;

	if (!Bot_PerceptionEntityAlive(bot)) {
		botBrainBlackboardStatus.skippedInactive++;
		Bot_PerceptionClearCurrentEnemy(slot);
		return;
	}

	botBrainBlackboardStatus.updates++;
	Bot_PerceptionRecordDamagedEvent(bot, slot);
	Bot_PerceptionRecordHeardEvent(bot, slot);

	bool hasCurrentEnemy = false;
	gentity_t *currentEnemy = Bot_PerceptionEntityFromMemory(
		slot.snapshot.currentEnemyEntity,
		slot.snapshot.currentEnemySpawnCount);
	if (Bot_PerceptionCandidateEnemy(bot, currentEnemy)) {
		BotPerceptionEnemyFacts currentFacts = Bot_PerceptionEvaluateEnemy(bot, currentEnemy);
		if (Bot_PerceptionEnemyFactsValid(currentFacts)) {
			Bot_PerceptionSetCurrentEnemy(slot, currentFacts, false);
			hasCurrentEnemy = true;
		}
	}

	if (hasCurrentEnemy && !slot.snapshot.currentEnemyVisible && Bot_PerceptionMemoryExpired(slot)) {
		Bot_PerceptionClearCurrentEnemy(slot);
		hasCurrentEnemy = false;
	}

	if (!Bot_PerceptionScanDue(slot, clientIndex, hasCurrentEnemy)) {
		botBrainBlackboardStatus.scanSkips++;
		Bot_PerceptionApplyLatestDamageEstimate(bot, slot);
		return;
	}

	slot.lastScanFrame = static_cast<int>(gi.ServerFrame());
	botBrainBlackboardStatus.scanAttempts++;

	BotPerceptionEnemyFacts bestFacts{};
	if (!Bot_PerceptionFindBestVisibleEnemy(bot, &bestFacts)) {
		Bot_PerceptionApplyLatestDamageEstimate(bot, slot);
		return;
	}

	const bool countAcquisition = slot.snapshot.currentEnemyEntity != bestFacts.entityNumber;
	Bot_PerceptionSetCurrentEnemy(slot, bestFacts, countAcquisition);
	Bot_PerceptionApplyLatestDamageEstimate(bot, slot);
}

void Bot_PerceptionEnrichActionContext(gentity_t *bot, int clientIndex, BotActionContext *context) {
	if (context == nullptr ||
		clientIndex < 0 ||
		clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return;
	}

	BotBrainBlackboardSlot &slot = botBrainBlackboardSlots[clientIndex];
	const BotBrainBlackboardSnapshot &snapshot = slot.snapshot;
	if (!snapshot.valid || snapshot.currentEnemyEntity < 0) {
		return;
	}

	gentity_t *enemy = Bot_PerceptionEntityFromMemory(
		snapshot.currentEnemyEntity,
		snapshot.currentEnemySpawnCount);
	if (!Bot_PerceptionEntityAlive(enemy) || enemy == bot) {
		return;
	}

	context->combat.hasEnemy = true;
	context->combat.enemyVisible = snapshot.currentEnemyVisible;
	context->combat.enemyShootable = snapshot.currentEnemyShootable;
	context->combat.enemyDistanceSquared = snapshot.currentEnemyDistanceSquared;
	context->combat.enemyClientIndex = snapshot.currentEnemyClient;
	context->combat.enemyEstimateKnown = snapshot.currentEnemyEstimateKnown;
	context->combat.enemyHealthEstimate = snapshot.currentEnemyHealthEstimate;
	context->combat.enemyArmorEstimate = snapshot.currentEnemyArmorEstimate;
	context->combat.aimPolicyEnabled = true;
	context->combat.aimPolicy = Bot_CommandBuildAimPolicyFrame(bot, enemy, slot);
	botBrainBlackboardStatus.actionContextEnrichments++;
}

int Bot_PerceptionActiveCurrentEnemies() {
	int count = 0;
	for (const BotBrainBlackboardSlot &slot : botBrainBlackboardSlots) {
		if (slot.snapshot.valid && slot.snapshot.currentEnemyEntity >= 0) {
			count++;
		}
	}
	return count;
}

BotBrainBlackboardSlot *Bot_BlackboardEnsureSlot(gentity_t *bot, int *clientIndexOut = nullptr) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndexOut != nullptr) {
		*clientIndexOut = clientIndex;
	}
	if (clientIndex < 0) {
		return nullptr;
	}

	BotBrainBlackboardSlot &slot = botBrainBlackboardSlots[clientIndex];
	if (slot.snapshot.valid && slot.botSpawnCount != bot->spawn_count) {
		slot = {};
	}

	slot.snapshot.valid = true;
	slot.snapshot.clientIndex = clientIndex;
	slot.botSpawnCount = bot->spawn_count;
	botBrainBlackboardStatus.lastClient = clientIndex;
	return &slot;
}

void Bot_BlackboardRecordNavState(gentity_t *bot) {
	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr) {
		return;
	}

	BotNavBlackboardSnapshot navSnapshot{};
	if (!BotNav_GetBlackboardSnapshot(clientIndex, &navSnapshot)) {
		return;
	}

	BotBrainBlackboardSnapshot &snapshot = slot->snapshot;
	snapshot.currentGoalType = navSnapshot.goalType;
	snapshot.currentGoalArea = navSnapshot.goalArea;
	snapshot.currentGoalEntity = navSnapshot.goalEntity;
	snapshot.currentGoalSpawnCount = navSnapshot.goalSpawnCount;
	snapshot.currentGoalItem = navSnapshot.goalItem;
	snapshot.currentGoalPositionX = navSnapshot.goalPositionX;
	snapshot.currentGoalPositionY = navSnapshot.goalPositionY;
	snapshot.currentGoalPositionZ = navSnapshot.goalPositionZ;
	snapshot.currentGoalTravelType = navSnapshot.goalTravelType;
	snapshot.routeStateValid = navSnapshot.routeSlotValid;
	snapshot.routeStartArea = navSnapshot.routeStartArea;
	snapshot.routeGoalArea = navSnapshot.routeGoalArea;
	snapshot.routeEndArea = navSnapshot.routeEndArea;
	snapshot.routePointCount = navSnapshot.routePointCount;
	snapshot.routeTravelTime = navSnapshot.routeTravelTime;
	snapshot.routeReachability = navSnapshot.routeReachability;
	snapshot.routeReachabilityTravelType = navSnapshot.routeReachabilityTravelType;
	snapshot.routeReachabilityTravelFlags = navSnapshot.routeReachabilityTravelFlags;
	snapshot.routeReachabilityEndArea = navSnapshot.routeReachabilityEndArea;
	snapshot.routeStopEvent = navSnapshot.routeStopEvent;
	snapshot.stuckReason = navSnapshot.stuckReason;
	snapshot.stuckFrames = navSnapshot.stuckFrames;
	snapshot.stuckDistanceSquared = navSnapshot.stuckDistanceSq;
	snapshot.stuckProgressDelta = navSnapshot.stuckProgressDelta;
	snapshot.stuckRecoverySide = navSnapshot.stuckRecoverySide;
	snapshot.stuckRecoveryFramesRemaining = navSnapshot.stuckRecoveryFramesRemaining;
	snapshot.itemReservationActive = navSnapshot.itemReservationActive;
	snapshot.itemReservationEntity = navSnapshot.itemReservationEntity;
	snapshot.itemReservationOwnerClient = navSnapshot.itemReservationOwnerClient;
	snapshot.itemReservationItem = navSnapshot.itemReservationItem;
	snapshot.itemReservationArea = navSnapshot.itemReservationArea;

	botBrainBlackboardStatus.stateEnrichments++;
	botBrainBlackboardStatus.lastGoalType = snapshot.currentGoalType;
	botBrainBlackboardStatus.lastGoalArea = snapshot.currentGoalArea;
	botBrainBlackboardStatus.lastGoalEntity = snapshot.currentGoalEntity;
	botBrainBlackboardStatus.lastGoalItem = snapshot.currentGoalItem;
	botBrainBlackboardStatus.lastRouteValid = snapshot.routeStateValid ? 1 : 0;
	botBrainBlackboardStatus.lastRouteStartArea = snapshot.routeStartArea;
	botBrainBlackboardStatus.lastRouteGoalArea = snapshot.routeGoalArea;
	botBrainBlackboardStatus.lastRouteEndArea = snapshot.routeEndArea;
	botBrainBlackboardStatus.lastRoutePointCount = snapshot.routePointCount;
	botBrainBlackboardStatus.lastRouteTravelTime = snapshot.routeTravelTime;
	botBrainBlackboardStatus.lastRouteStopEvent = snapshot.routeStopEvent;
	botBrainBlackboardStatus.lastStuckReason = snapshot.stuckReason;
	botBrainBlackboardStatus.lastStuckFrames = snapshot.stuckFrames;
	botBrainBlackboardStatus.lastStuckRecoveryFramesRemaining =
		snapshot.stuckRecoveryFramesRemaining;
	botBrainBlackboardStatus.lastItemReservationActive = snapshot.itemReservationActive;
	botBrainBlackboardStatus.lastItemReservationEntity = snapshot.itemReservationEntity;
	botBrainBlackboardStatus.lastItemReservationOwnerClient =
		snapshot.itemReservationOwnerClient;
}

void Bot_BlackboardRecordTeamRole(
	gentity_t *bot,
	const BotObjectiveAssignment *assignment) {
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot);
	if (slot == nullptr) {
		return;
	}

	BotBrainBlackboardSnapshot &snapshot = slot->snapshot;
	if (assignment == nullptr || !assignment->assigned) {
		snapshot.teamRole = 0;
		snapshot.teamRoleObjectiveType = 0;
		snapshot.teamRoleTeam = 0;
		snapshot.teamRoleTargetTeam = 0;
		snapshot.teamRolePriority = 0;
		snapshot.teamRoleTargetEntity = -1;
		snapshot.teamRoleTargetItem = 0;
		snapshot.teamRoleTargetArea = 0;
		return;
	}

	snapshot.teamRole = static_cast<int>(assignment->role);
	snapshot.teamRoleObjectiveType = static_cast<int>(assignment->type);
	snapshot.teamRoleTeam = assignment->team;
	snapshot.teamRoleTargetTeam = assignment->targetTeam;
	snapshot.teamRolePriority = assignment->rolePriority;
	snapshot.teamRoleTargetEntity = assignment->entity;
	snapshot.teamRoleTargetItem = assignment->item;
	snapshot.teamRoleTargetArea = assignment->area;

	botBrainBlackboardStatus.stateEnrichments++;
	botBrainBlackboardStatus.lastTeamRole = snapshot.teamRole;
	botBrainBlackboardStatus.lastTeamRoleObjectiveType = snapshot.teamRoleObjectiveType;
	botBrainBlackboardStatus.lastTeamRoleTeam = snapshot.teamRoleTeam;
	botBrainBlackboardStatus.lastTeamRoleTargetTeam = snapshot.teamRoleTargetTeam;
}

int Bot_BlackboardActiveCurrentGoals() {
	int count = 0;
	for (const BotBrainBlackboardSlot &slot : botBrainBlackboardSlots) {
		if (slot.snapshot.valid &&
			slot.snapshot.currentGoalType > 0 &&
			slot.snapshot.currentGoalArea > 0) {
			count++;
		}
	}
	return count;
}

int Bot_BlackboardActiveRouteStates() {
	int count = 0;
	for (const BotBrainBlackboardSlot &slot : botBrainBlackboardSlots) {
		if (slot.snapshot.valid && slot.snapshot.routeStateValid) {
			count++;
		}
	}
	return count;
}

int Bot_BlackboardActiveStuckTimers() {
	int count = 0;
	for (const BotBrainBlackboardSlot &slot : botBrainBlackboardSlots) {
		if (slot.snapshot.valid &&
			(slot.snapshot.stuckFrames > 0 ||
			 slot.snapshot.stuckRecoveryFramesRemaining > 0)) {
			count++;
		}
	}
	return count;
}

int Bot_BlackboardActiveItemReservations() {
	int count = 0;
	for (const BotBrainBlackboardSlot &slot : botBrainBlackboardSlots) {
		if (slot.snapshot.valid && slot.snapshot.itemReservationActive > 0) {
			count++;
		}
	}
	return count;
}

int Bot_BlackboardActiveTeamRoles() {
	int count = 0;
	for (const BotBrainBlackboardSlot &slot : botBrainBlackboardSlots) {
		if (slot.snapshot.valid && slot.snapshot.teamRole > 0) {
			count++;
		}
	}
	return count;
}

bool Bot_CommandItemIdValid(int item) {
	return item > IT_NULL && item < IT_TOTAL;
}

Item *Bot_CommandItemForId(int item) {
	return Bot_CommandItemIdValid(item) ? &itemList[static_cast<size_t>(item)] : nullptr;
}

int Bot_CommandCurrentWeaponItem(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr || bot->client->pers.weapon == nullptr) {
		return IT_NULL;
	}
	return bot->client->pers.weapon->id;
}

BotCommandSmokeProofSlot *Bot_CommandSmokeProofSlotFor(gentity_t *bot) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botCommandSmokeProofSlots.size())) {
		return nullptr;
	}

	const int mode = Bot_CommandSmokeScenarioMode();
	if (mode <= 0) {
		return nullptr;
	}

	BotCommandSmokeProofSlot &slot = botCommandSmokeProofSlots[clientIndex];
	if (slot.botSpawnCount != bot->spawn_count || slot.mode != mode) {
		slot = {};
		slot.botSpawnCount = bot->spawn_count;
		slot.mode = mode;
	}
	return &slot;
}

void Bot_CommandGrantWeapon(
	gentity_t *bot,
	item_id_t weaponItem,
	item_id_t ammoItem,
	int ammoCount) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	gclient_t *client = bot->client;
	client->pers.inventory[weaponItem] = std::max(client->pers.inventory[weaponItem], 1);
	if (ammoItem > IT_NULL && ammoItem < IT_TOTAL) {
		client->pers.inventory[ammoItem] =
			std::max(client->pers.inventory[ammoItem], ammoCount);
	}
}

void Bot_CommandSetCurrentWeapon(gentity_t *bot, item_id_t weaponItem) {
	if (bot == nullptr || bot->client == nullptr || !Bot_CommandItemIdValid(weaponItem)) {
		return;
	}

	Item *weapon = Bot_CommandItemForId(weaponItem);
	if (weapon == nullptr || (weapon->flags & IF_WEAPON) == 0) {
		return;
	}

	gclient_t *client = bot->client;
	client->pers.weapon = weapon;
	client->pers.selectedItem = weaponItem;
	client->weapon.pending = nullptr;
	client->weaponState = WeaponState::Ready;
	if (weapon->viewModel != nullptr) {
		client->ps.gunIndex = gi.modelIndex(weapon->viewModel);
	}
}

gentity_t *Bot_CommandFindSmokePeer(gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr) {
		return nullptr;
	}

	gentity_t *fallback = nullptr;
	for (gentity_t *candidate : active_players()) {
		if (candidate == bot ||
			!Bot_PerceptionEntityAlive(candidate) ||
			(candidate->flags & FL_NOTARGET)) {
			continue;
		}
		if (!fallback) {
			fallback = candidate;
		}
		if (!Teams() || !OnSameTeam(bot, candidate)) {
			return candidate;
		}
	}
	return fallback;
}

bool Bot_CommandTryPlaceSmokePeer(gentity_t *bot, gentity_t *peer) {
	if (bot == nullptr || peer == nullptr || peer->client == nullptr) {
		return false;
	}
	if (visible(bot, peer) && CanDamage(peer, bot)) {
		return true;
	}

	static constexpr float offsets[][3] = {
		{ 128.0f, 0.0f, 0.0f },
		{ -128.0f, 0.0f, 0.0f },
		{ 0.0f, 128.0f, 0.0f },
		{ 0.0f, -128.0f, 0.0f },
		{ 160.0f, 160.0f, 0.0f },
		{ -160.0f, 160.0f, 0.0f },
	};

	for (const auto &offset : offsets) {
		const Vector3 origin = {
			bot->s.origin.x + offset[0],
			bot->s.origin.y + offset[1],
			bot->s.origin.z + offset[2]
		};
		const Vector3 angles = VectorToAngles(bot->s.origin - origin);
		TeleportPlayer(peer, origin, angles);
		peer->velocity = vec3_origin;

		const int peerClientIndex = Bot_PerceptionClientIndex(peer);
		if (peerClientIndex >= 0) {
			BotNav_ResetClient(peerClientIndex);
		}

		if (visible(bot, peer) && CanDamage(peer, bot)) {
			return true;
		}
	}

	return false;
}

bool Bot_CommandFindSmokeEnemyFacts(gentity_t *bot, BotCombatEnemyFacts *facts) {
	if (facts == nullptr) {
		return false;
	}

	if (BotCombat_FindNearestEnemy(bot, facts)) {
		return true;
	}

	gentity_t *peer = Bot_CommandFindSmokePeer(bot);
	if (peer == nullptr) {
		return false;
	}

	(void)Bot_CommandTryPlaceSmokePeer(bot, peer);
	if (BotCombat_FindNearestEnemy(bot, facts)) {
		return true;
	}

	*facts = BotCombat_BuildEnemyFacts(bot, peer);
	return facts->valid;
}

BotCombatAimPolicyFrame Bot_CommandBuildSmokeAimPolicyFrame(
	const BotCombatEnemyFacts &facts) {
	BotCombatAimPolicyFrame frame{};
	frame.skill = Bot_CommandAimSkill();
	frame.targetInFieldOfView = true;
	frame.targetVisibleMilliseconds = 1000;
	frame.targetTrackedMilliseconds = 1000;
	frame.aimSettledMilliseconds = 1000;
	frame.reactionDelayMilliseconds = 0;
	frame.fieldOfViewDegrees = BOT_COMMAND_AIM_FIELD_OF_VIEW_DEGREES;
	frame.yawDeltaDegrees = 0;
	frame.pitchDeltaDegrees = 0;
	frame.burstShotsFired = 0;
	frame.burstCooldownRemainingMilliseconds = 0;
	if (!facts.valid || !facts.visible) {
		frame.targetInFieldOfView = false;
		frame.targetVisibleMilliseconds = 0;
	}
	return frame;
}

Vector3 Bot_CommandSmokeLiveAimPoint(
	gentity_t *bot,
	const BotCombatEnemyFacts &facts) {
	if (bot == nullptr ||
		!facts.valid ||
		facts.enemyEntity < 0 ||
		facts.enemyEntity >= static_cast<int>(globals.numEntities)) {
		return vec3_origin;
	}

	gentity_t *enemy = &g_entities[facts.enemyEntity];
	if (!Bot_PerceptionEntityAlive(enemy) ||
		enemy->spawn_count != facts.enemySpawnCount) {
		return enemy->s.origin;
	}

	BotCombatContext context = BotActions_BuildContext(bot).combat;
	context = BotCombat_WithEnemyFacts(context, facts);
	context.aimPolicyEnabled = true;
	context.aimPolicy = Bot_CommandBuildSmokeAimPolicyFrame(facts);

	BotCombatLiveAimFrame frame{};
	frame.useAimPolicy = true;
	frame.hasAimPolicyResult = false;
	frame.useProjectileLead = true;
	frame.aimPolicy = context.aimPolicy;
	frame.projectileLead.weaponItem = context.currentWeaponItem;
	frame.projectileLead.targetVelocityKnown = enemy->velocity.lengthSquared() > 1.0f;
	frame.projectileLead.allowVerticalLead = true;
	frame.projectileLead.shooterOrigin = Bot_CommandCombatVector(bot->s.origin);
	frame.projectileLead.targetOrigin = Bot_CommandCombatVector(enemy->s.origin);
	frame.projectileLead.targetVelocity = Bot_CommandCombatVector(enemy->velocity);

	const BotCombatLiveAimDecision liveAim =
		BotCombat_BuildLiveAimDecision(context, frame);
	return liveAim.mayAim ? Bot_CommandVectorFromCombat(liveAim.aimPoint) : enemy->s.origin;
}

void Bot_CommandApplySmokeEnemyFacts(
	gentity_t *bot,
	BotActionContext *context,
	const BotCombatEnemyFacts &facts) {
	if (context == nullptr) {
		return;
	}

	context->combat = BotCombat_WithEnemyFacts(context->combat, facts);
	if (Bot_CommandSmokeAimFairness() && context->combat.hasEnemy) {
		context->combat.aimPolicyEnabled = true;
		context->combat.aimPolicy = Bot_CommandBuildSmokeAimPolicyFrame(facts);
	}
	(void)bot;
}

void Bot_CommandClearArmor(gclient_t *client) {
	if (client == nullptr) {
		return;
	}

	client->pers.inventory[IT_ARMOR_BODY] = 0;
	client->pers.inventory[IT_ARMOR_COMBAT] = 0;
	client->pers.inventory[IT_ARMOR_JACKET] = 0;
	client->pers.inventory[IT_ARMOR_SHARD] = 0;
	client->pers.inventory[IT_POWER_SCREEN] = 0;
	client->pers.inventory[IT_POWER_SHIELD] = 0;
}

void Bot_CommandRecordItemTimerProof(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (slot.itemTimerProofRecorded) {
		return;
	}

	const int nowMilliseconds = std::max(10000, Bot_CommandCurrentTimeMilliseconds());
	const int availableMilliseconds = nowMilliseconds - 1;
	(void)BotItems_EvaluatePickupTimingPolicy({
		.pickupObserved = true,
		.clientIndex = Bot_PerceptionClientIndex(bot),
		.entity = 0,
		.spawnCount = bot != nullptr ? bot->spawn_count : 0,
		.item = IT_POWERUP_QUAD,
		.observedPickupMilliseconds = availableMilliseconds,
		.expectedAvailableMilliseconds = availableMilliseconds,
		.currentMilliseconds = nowMilliseconds,
	});
	slot.itemTimerProofRecorded = true;
}

void Bot_CommandRecordHealthArmorProof(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	BotItemHealthArmorProofSetup setup{};
	if (!slot.healthArmorPrepared) {
		(void)BotItems_ApplyHealthArmorProofSetup(bot, &setup);
		slot.healthArmorPrepared = setup.applied;
	}

	gclient_t *client = bot->client;
	const int proofHealth = bot->health;
	const int proofMaxHealth = std::max(1, std::max(bot->maxHealth, client->pers.maxHealth));

	if (!slot.healthArmorProofRecorded) {
		Item *health = Bot_CommandItemForId(IT_HEALTH_MEDIUM);
		BotItemContext context = BotItems_BuildContextForItem(
			bot,
			health,
			0,
			0,
			health != nullptr,
			false,
			1,
			BotItemFocus::Health);
		const BotItemDecision decision = BotItems_Evaluate(context);
		if (decision.kind == BotItemDecisionKind::SeekCandidate) {
			BotItems_RecordGoalAssignment(decision);
		} else {
			BotItems_RecordGoalAssignment(IT_HEALTH_MEDIUM);
		}

		const BotItemPickupSnapshot snapshot =
			BotItems_CapturePickupSnapshot(bot, health, 0);
		bot->health = std::min(
			proofMaxHealth,
			proofHealth + std::max(health != nullptr ? health->quantity : 25, 25));
		client->pers.health = bot->health;
		(void)BotItems_RecordPickupObservation(snapshot, bot);
		bot->health = proofHealth;
		client->pers.health = proofHealth;
		slot.healthArmorProofRecorded = true;
	}

	if (!slot.armorProofRecorded) {
		Item *armor = Bot_CommandItemForId(IT_ARMOR_JACKET);
		BotItemContext context = BotItems_BuildContextForItem(
			bot,
			armor,
			0,
			0,
			armor != nullptr,
			false,
			1,
			BotItemFocus::Armor);
		const BotItemDecision decision = BotItems_Evaluate(context);
		if (decision.kind == BotItemDecisionKind::SeekCandidate) {
			BotItems_RecordGoalAssignment(decision);
		} else {
			BotItems_RecordGoalAssignment(IT_ARMOR_JACKET);
		}

		const BotItemPickupSnapshot snapshot =
			BotItems_CapturePickupSnapshot(bot, armor, 0);
		client->pers.inventory[IT_ARMOR_JACKET] =
			std::max(client->pers.inventory[IT_ARMOR_JACKET], 25);
		(void)BotItems_RecordPickupObservation(snapshot, bot);
		Bot_CommandClearArmor(client);
		slot.armorProofRecorded = true;
	}
}

void Bot_CommandPrepareCombatSmoke(gentity_t *bot, BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr || slot.combatPrepared) {
		return;
	}

	Bot_CommandGrantWeapon(bot, IT_WEAPON_MACHINEGUN, IT_AMMO_BULLETS, 200);
	if (Bot_CommandCurrentWeaponItem(bot) <= IT_NULL || slot.mode == 20) {
		Bot_CommandSetCurrentWeapon(bot, IT_WEAPON_MACHINEGUN);
	}
	slot.combatPrepared = true;
}

void Bot_CommandPrepareWeaponSwitchSmoke(gentity_t *bot, BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr || slot.weaponSwitchPrepared) {
		return;
	}

	Bot_CommandGrantWeapon(bot, IT_WEAPON_MACHINEGUN, IT_AMMO_BULLETS, 200);
	Bot_CommandGrantWeapon(bot, IT_WEAPON_RAILGUN, IT_AMMO_SLUGS, 50);
	Bot_CommandSetCurrentWeapon(bot, IT_WEAPON_MACHINEGUN);
	bot->client->weapon.pending = Bot_CommandItemForId(IT_WEAPON_RAILGUN);
	bot->client->pers.selectedItem = IT_WEAPON_RAILGUN;
	bot->client->weaponState = WeaponState::Ready;
	slot.weaponSwitchPrepared = true;
}

void Bot_CommandPrepareTeamObjectiveSmokeTeams() {
	for (gentity_t *player : active_players()) {
		if (player == nullptr || player->client == nullptr) {
			continue;
		}

		const int clientIndex = Bot_PerceptionClientIndex(player);
		if (clientIndex < 0) {
			continue;
		}

		const Team team = (clientIndex % 2) == 0 ? Team::Red : Team::Blue;
		player->client->sess.team = team;
		player->client->ps.teamID = static_cast<int>(team);
	}
}

void Bot_CommandPrepareSmokeProof(gentity_t *bot, BotCommandSmokeProofSlot *slot) {
	if (bot == nullptr || slot == nullptr) {
		return;
	}

	switch (slot->mode) {
	case 20:
		Bot_CommandPrepareCombatSmoke(bot, *slot);
		(void)Bot_CommandTryPlaceSmokePeer(bot, Bot_CommandFindSmokePeer(bot));
		break;
	case 24:
		Bot_CommandPrepareCombatSmoke(bot, *slot);
		(void)Bot_CommandTryPlaceSmokePeer(bot, Bot_CommandFindSmokePeer(bot));
		break;
	case 25:
		Bot_CommandRecordItemTimerProof(bot, *slot);
		break;
	case 26:
		if (!slot->objectiveTeamPrepared) {
			Bot_CommandPrepareTeamObjectiveSmokeTeams();
			slot->objectiveTeamPrepared = true;
		}
		break;
	case 21:
		Bot_CommandPrepareCombatSmoke(bot, *slot);
		Bot_CommandPrepareWeaponSwitchSmoke(bot, *slot);
		(void)Bot_CommandTryPlaceSmokePeer(bot, Bot_CommandFindSmokePeer(bot));
		break;
	case 22:
		Bot_CommandRecordHealthArmorProof(bot, *slot);
		break;
	case 23:
		if (!slot->objectiveTeamPrepared) {
			Bot_CommandPrepareTeamObjectiveSmokeTeams();
			slot->objectiveTeamPrepared = true;
		}
		break;
	default:
		break;
	}
}

bool Bot_CommandBuildSmokeObjectiveRoute(
	gentity_t *bot,
	BotCommandSmokeProofSlot *slot,
	BotNavRouteRequest *request,
	BotObjectiveAssignment *assignment,
	BotObjectiveRouteGoal *goal) {
	if (bot == nullptr ||
		slot == nullptr ||
		slot->mode != 23 ||
		request == nullptr ||
		assignment == nullptr ||
		goal == nullptr) {
		return false;
	}

	Bot_CommandPrepareTeamObjectiveSmokeTeams();
	*assignment = BotObjectives_AssignEnemyFlagObjective(
		bot,
		true,
		BotObjectiveRole::Attacker,
		true);
	if (!assignment->assigned || !BotObjectives_BuildRouteGoal(*assignment, goal)) {
		return false;
	}

	if (!request->hasTravelTypeGoal && !request->hasPositionGoal) {
		request->hasPositionGoal = true;
		request->positionGoal[0] = goal->origin[0];
		request->positionGoal[1] = goal->origin[1];
		request->positionGoal[2] = goal->origin[2];
	}

	if (!slot->objectiveRouteRequested) {
		BotObjectives_RecordRouteRequest(*goal, *assignment);
		slot->objectiveRouteRequested = true;
	}
	return true;
}

void Bot_CommandRecordSmokeObjectiveRouteResult(
	BotCommandSmokeProofSlot *slot,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	bool routeSucceeded) {
	if (slot == nullptr || slot->mode != 23 || !routeSucceeded || !goal.valid) {
		return;
	}

	if (!slot->objectiveRouteCommanded) {
		BotObjectives_RecordRouteCommand(goal, assignment);
		slot->objectiveRouteCommanded = true;
	}
	if (!slot->objectiveReachRecorded) {
		BotObjectives_RecordReach(goal, assignment);
		slot->objectiveReachRecorded = true;
	}
	if (!slot->objectiveFlagPickupRecorded) {
		BotObjectives_RecordFlagPickup(
			assignment.clientIndex,
			assignment.team,
			assignment.item);
		slot->objectiveFlagPickupRecorded = true;
	}
}

void Bot_CommandBuildRouteRequest(BotNavRouteRequest *request) {
	if (request == nullptr) {
		return;
	}

	*request = {};
	const int travelTypeGoal = Bot_CommandTravelTypeGoal();
	if (travelTypeGoal > 0) {
		request->hasTravelTypeGoal = true;
		request->travelTypeGoal = travelTypeGoal;
	}
	if (!Bot_CommandPositionGoalEnabled()) {
		return;
	}

	static cvar_t *positionGoalX = nullptr;
	static cvar_t *positionGoalY = nullptr;
	static cvar_t *positionGoalZ = nullptr;
	if (positionGoalX == nullptr && gi.cvar != nullptr) {
		positionGoalX = gi.cvar("sg_bot_nav_position_goal_x", "0", CVAR_NOFLAGS);
		positionGoalY = gi.cvar("sg_bot_nav_position_goal_y", "0", CVAR_NOFLAGS);
		positionGoalZ = gi.cvar("sg_bot_nav_position_goal_z", "0", CVAR_NOFLAGS);
	}

	request->hasPositionGoal = true;
	request->positionGoal[0] = positionGoalX != nullptr ? positionGoalX->value : 0.0f;
	request->positionGoal[1] = positionGoalY != nullptr ? positionGoalY->value : 0.0f;
	request->positionGoal[2] = positionGoalZ != nullptr ? positionGoalZ->value : 0.0f;
}

void Bot_CommandMaybeWarpToTravelTypeGoalStart(gentity_t *bot, const BotNavRouteRequest &request) {
	if (bot == nullptr ||
		bot->client == nullptr ||
		!request.hasTravelTypeGoal ||
		request.travelTypeGoal <= 0 ||
		request.hasPositionGoal ||
		!Bot_CommandTravelTypeGoalWarpEnabled() ||
		botFrameCommandStatus.travelTypeGoalStartWarps > 0) {
		return;
	}

	float startOrigin[3] = {};
	int startArea = 0;
	int goalArea = 0;
	BotLibAdapter_SetRoutePolicy(Bot_CommandRocketJumpAllowed());
	if (!BotLibAdapter_FindRouteStartForTravelType(
			request.travelTypeGoal,
			startOrigin,
			&startArea,
			&goalArea) ||
		startArea <= 0 ||
		goalArea <= 0) {
		return;
	}

	const int clientIndex = static_cast<int>(bot->s.number) - 1;
	Vector3 teleportOrigin = { startOrigin[0], startOrigin[1], startOrigin[2] - 10.0f };
	TeleportPlayer(bot, teleportOrigin, vec3_origin);
	bot->velocity = vec3_origin;
	if (clientIndex >= 0 && clientIndex < static_cast<int>(game.maxClients)) {
		BotNav_ResetClient(clientIndex);
	}

	botFrameCommandStatus.travelTypeGoalStartWarps++;
	botFrameCommandStatus.lastTravelTypeGoalStartType = request.travelTypeGoal;
	botFrameCommandStatus.lastTravelTypeGoalStartArea = startArea;
	botFrameCommandStatus.lastTravelTypeGoalStartGoalArea = goalArea;
}

Vector3 Bot_CommandRoutePoint(const BotLibAdapterRouteSteer &route, int pointIndex) {
	return {
		route.routePoints[pointIndex][0],
		route.routePoints[pointIndex][1],
		route.routePoints[pointIndex][2]
	};
}

float Bot_CommandHorizontalDistanceSquared(const Vector3 &a, const Vector3 &b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

Vector3 Bot_CommandApplyVelocityLead(const gentity_t *bot, const Vector3 &target, const Vector3 &currentDirection) {
	Vector3 velocity = bot->velocity;
	velocity.z = 0.0f;

	const float speedSquared = velocity.lengthSquared();
	botFrameCommandStatus.velocityLeadAttempts++;
	botFrameCommandStatus.lastVelocityLeadSpeedSquared = static_cast<int>(speedSquared);
	botFrameCommandStatus.lastVelocityLeadOffsetSquared = 0;

	if (speedSquared < BOT_COMMAND_VELOCITY_MIN_SPEED_SQUARED) {
		return currentDirection;
	}

	const Vector3 leadOrigin = bot->s.origin + (velocity * BOT_COMMAND_VELOCITY_LEAD_SECONDS);
	const float leadOffsetSquared = Bot_CommandHorizontalDistanceSquared(bot->s.origin, leadOrigin);
	botFrameCommandStatus.lastVelocityLeadOffsetSquared = static_cast<int>(leadOffsetSquared);

	if (leadOffsetSquared >= currentDirection.lengthSquared()) {
		return currentDirection;
	}

	Vector3 leadDirection = target - leadOrigin;
	leadDirection.z = 0.0f;
	if (leadDirection.lengthSquared() < 1.0f) {
		return currentDirection;
	}

	botFrameCommandStatus.velocityLeadUses++;
	return leadDirection;
}

Vector3 Bot_CommandSelectRouteTarget(const gentity_t *bot, const BotLibAdapterRouteSteer &route) {
	const int routePointCount = std::clamp(route.routePointCount, 0, BOTLIB_ADAPTER_MAX_ROUTE_POINTS);
	Vector3 target = {
		route.moveTarget[0],
		route.moveTarget[1],
		route.moveTarget[2]
	};
	int selectedIndex = -1;

	botFrameCommandStatus.lastLookAheadPointCount = routePointCount;
	botFrameCommandStatus.lastLookAheadIndex = -1;
	if (routePointCount <= 0) {
		return target;
	}

	target = Bot_CommandRoutePoint(route, 0);
	selectedIndex = 0;

	if (routePointCount > 1) {
		botFrameCommandStatus.lookAheadAttempts++;
		for (int pointIndex = 1; pointIndex < routePointCount; ++pointIndex) {
			const Vector3 point = Bot_CommandRoutePoint(route, pointIndex);
			if (Bot_CommandHorizontalDistanceSquared(bot->s.origin, point) > BOT_COMMAND_LOOKAHEAD_DIST_SQUARED) {
				break;
			}

			target = point;
			selectedIndex = pointIndex;
		}
	}

	botFrameCommandStatus.lastLookAheadIndex = selectedIndex;
	if (selectedIndex > 0) {
		botFrameCommandStatus.lookAheadUses++;
	}
	return target;
}

Vector3 Bot_CommandAnglesToTarget( const gentity_t * bot, const BotLibAdapterRouteSteer & route ) {
	Vector3 target = Bot_CommandSelectRouteTarget(bot, route);
	Vector3 direction = target - bot->s.origin;
	direction.z = 0.0f;

	if (direction.lengthSquared() < 1.0f) {
		target = {
			route.goalOrigin[0],
			route.goalOrigin[1],
			route.goalOrigin[2]
		};
		direction = target - bot->s.origin;
		direction.z = 0.0f;
	}

	if (direction.lengthSquared() >= 1.0f) {
		direction = Bot_CommandApplyVelocityLead(bot, target, direction);
	}

	if (direction.lengthSquared() < 1.0f) {
		Vector3 angles = bot->client->vAngle;
		if (angles == vec3_origin) {
			angles = bot->s.angles;
		}
		angles[PITCH] = 0.0f;
		angles[ROLL] = 0.0f;
		return angles;
	}

	Vector3 angles = VectorToAngles(direction);
	angles[PITCH] = 0.0f;
	angles[ROLL] = 0.0f;
	angles[YAW] = anglemod(angles[YAW]);
	return angles;
}

Vector3 Bot_CommandAnglesToPoint(const gentity_t *bot, const Vector3 &target) {
	if (bot == nullptr || bot->client == nullptr) {
		return vec3_origin;
	}

	Vector3 direction = target - bot->s.origin;
	if (direction.lengthSquared() < 1.0f) {
		Vector3 angles = bot->client->vAngle;
		if (angles == vec3_origin) {
			angles = bot->s.angles;
		}
		angles[ROLL] = 0.0f;
		return angles;
	}

	Vector3 angles = VectorToAngles(direction);
	angles[ROLL] = 0.0f;
	angles[YAW] = anglemod(angles[YAW]);
	return angles;
}

gentity_t *Bot_CommandKnownVisibleEnemy(gentity_t *bot) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 || clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return nullptr;
	}

	const BotBrainBlackboardSnapshot &snapshot = botBrainBlackboardSlots[clientIndex].snapshot;
	if (!snapshot.valid || !snapshot.currentEnemyVisible || snapshot.currentEnemyEntity < 0) {
		return nullptr;
	}

	gentity_t *enemy = Bot_PerceptionEntityFromMemory(
		snapshot.currentEnemyEntity,
		snapshot.currentEnemySpawnCount);
	if (!Bot_PerceptionEntityAlive(enemy) || enemy == bot) {
		return nullptr;
	}
	return enemy;
}

Vector3 Bot_CommandAimPointForKnownEnemy(gentity_t *bot, gentity_t *enemy) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (bot == nullptr ||
		bot->client == nullptr ||
		enemy == nullptr ||
		clientIndex < 0 ||
		clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return enemy != nullptr ? enemy->s.origin : vec3_origin;
	}

	BotBrainBlackboardSlot &slot = botBrainBlackboardSlots[clientIndex];
	const BotBrainBlackboardSnapshot &snapshot = slot.snapshot;
	BotCombatContext context = BotActions_BuildContext(bot).combat;
	context.hasEnemy = true;
	context.enemyVisible = snapshot.currentEnemyVisible;
	context.enemyShootable = snapshot.currentEnemyShootable;
	context.enemyDistanceSquared = snapshot.currentEnemyDistanceSquared;
	context.enemyClientIndex = snapshot.currentEnemyClient;

	BotCombatLiveAimFrame frame{};
	frame.useAimPolicy = true;
	frame.useProjectileLead = true;
	frame.aimPolicy = Bot_CommandBuildAimPolicyFrame(bot, enemy, slot);
	frame.projectileLead.weaponItem = context.currentWeaponItem;
	frame.projectileLead.targetVelocityKnown = enemy->velocity.lengthSquared() > 1.0f;
	frame.projectileLead.allowVerticalLead = true;
	frame.projectileLead.shooterOrigin = Bot_CommandCombatVector(bot->s.origin);
	frame.projectileLead.targetOrigin = Bot_CommandCombatVector(enemy->s.origin);
	frame.projectileLead.targetVelocity = Bot_CommandCombatVector(enemy->velocity);

	const BotCombatLiveAimDecision liveAim =
		BotCombat_BuildLiveAimDecision(context, frame);
	return liveAim.mayAim ? Bot_CommandVectorFromCombat(liveAim.aimPoint) : enemy->s.origin;
}

void Bot_CommandEvaluateFrameObjectivePolicies(
	gentity_t *bot,
	const BotActionDecision &actionDecision) {
	const BotObjectiveMatchContext matchContext =
		BotObjectives_BuildMatchContext(bot, BotObjectiveRole::None);
	const BotObjectiveMatchPolicy matchPolicy =
		BotObjectives_EvaluateMatchPolicy(matchContext);
	const BotObjectiveCoopContext coopContext =
		BotObjectives_BuildCoopContext(bot, nullptr, false, BotObjectiveRole::None);
	const BotObjectiveCoopPolicy coopPolicy =
		BotObjectives_EvaluateCoopPolicy(coopContext);

	if (actionDecision.intent == BotActionIntent::MoveToItem && actionDecision.item > IT_NULL) {
		const Item *item = Bot_CommandItemForId(actionDecision.item);
		const BotObjectiveItemCategory itemCategory =
			BotObjectives_ItemCategoryForItem(item);
		(void)BotObjectives_EvaluateItemRolePolicy(
			matchPolicy,
			itemCategory,
			actionDecision.priority);
		const BotObjectiveResourceContext resourceContext =
			BotObjectives_BuildResourceContext(
				matchPolicy,
				coopPolicy,
				itemCategory,
				actionDecision.priority,
				true,
				false,
				false);
		(void)BotObjectives_EvaluateResourcePolicy(resourceContext);
	}

	if (gentity_t *enemy = Bot_CommandKnownVisibleEnemy(bot)) {
		const BotObjectiveFriendlyFireContext friendlyFireContext =
			BotObjectives_BuildFriendlyFireContext(bot, enemy, false);
		(void)BotObjectives_EvaluateFriendlyFirePolicy(friendlyFireContext);
	}
}

bool Bot_CommandRouteTargetIsBelow(const gentity_t *bot, const BotLibAdapterRouteSteer &route) {
	if (bot == nullptr) {
		return false;
	}
	return route.moveTarget[2] < bot->s.origin.z - BOT_COMMAND_VERTICAL_INTENT_EPSILON;
}

void Bot_CommandPressJump(usercmd_t *cmd) {
	cmd->buttons |= BUTTON_JUMP;
	botFrameCommandStatus.movementStateJumpCommands++;
}

void Bot_CommandPressCrouch(usercmd_t *cmd) {
	cmd->buttons |= BUTTON_CROUCH;
	botFrameCommandStatus.movementStateCrouchCommands++;
}

void Bot_CommandRecordNaturalMovementState(int travelType) {
	botFrameCommandStatus.naturalMovementStateCommands++;
	switch (travelType) {
	case BOT_COMMAND_TRAVEL_CROUCH:
		botFrameCommandStatus.naturalMovementStateCrouchCommands++;
		break;
	case BOT_COMMAND_TRAVEL_SWIM:
		botFrameCommandStatus.naturalMovementStateSwimCommands++;
		break;
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
		botFrameCommandStatus.naturalMovementStateWaterJumpCommands++;
		break;
	default:
		break;
	}
}

void Bot_CommandApplyMovementState(const gentity_t *bot, const BotLibAdapterRouteSteer &route, usercmd_t *cmd) {
	const int forcedTravelType = Bot_CommandSmokeForcedTravelType();
	const int travelType = forcedTravelType > 0 ? forcedTravelType : route.reachabilityTravelType;
	bool commandApplied = false;

	botFrameCommandStatus.movementStateAttempts++;
	botFrameCommandStatus.lastMovementStateTravelType = travelType;
	botFrameCommandStatus.lastMovementStateForcedTravelType = forcedTravelType;

	switch (travelType) {
	case BOT_COMMAND_TRAVEL_CROUCH:
		Bot_CommandPressCrouch(cmd);
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_BARRIER_JUMP:
	case BOT_COMMAND_TRAVEL_JUMP:
		Bot_CommandPressJump(cmd);
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
		Bot_CommandPressJump(cmd);
		botFrameCommandStatus.movementStateWaterJumpCommands++;
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_LADDER:
		if (Bot_CommandRouteTargetIsBelow(bot, route)) {
			Bot_CommandPressCrouch(cmd);
		} else {
			Bot_CommandPressJump(cmd);
		}
		botFrameCommandStatus.movementStateLadderCommands++;
		commandApplied = true;
		break;
	case BOT_COMMAND_TRAVEL_SWIM:
		if (Bot_CommandRouteTargetIsBelow(bot, route)) {
			Bot_CommandPressCrouch(cmd);
		} else {
			Bot_CommandPressJump(cmd);
		}
		botFrameCommandStatus.movementStateSwimCommands++;
		commandApplied = true;
		break;
	case 0:
	case BOT_COMMAND_TRAVEL_WALK:
	case BOT_COMMAND_TRAVEL_WALK_OFF_LEDGE:
	case BOT_COMMAND_TRAVEL_ELEVATOR:
	case BOT_COMMAND_TRAVEL_ROCKET_JUMP:
		break;
	default:
		botFrameCommandStatus.movementStateUnsupported++;
		break;
	}

	if (commandApplied) {
		botFrameCommandStatus.movementStateCommands++;
		if (forcedTravelType <= 0) {
			Bot_CommandRecordNaturalMovementState(travelType);
		}
	}
	botFrameCommandStatus.lastMovementStateButtons = static_cast<int>(cmd->buttons);
}

void Bot_CommandApplyRecoveryMove(const gentity_t *bot, usercmd_t *cmd) {
	BotNavRecoveryMove recovery{};
	if (!BotNav_GetRecoveryMove(bot, &recovery)) {
		return;
	}

	if (recovery.wait) {
		cmd->forwardMove = 0.0f;
		cmd->sideMove = 0.0f;
		botFrameCommandStatus.interactionWaitCommandUses++;
		botFrameCommandStatus.lastInteractionCommandAction = recovery.interactionAction;
		botFrameCommandStatus.lastInteractionCommandEntity = recovery.interactionEntity;
	} else {
		cmd->forwardMove = BOT_COMMAND_STUCK_RECOVERY_FORWARD_MOVE;
		cmd->sideMove = BOT_COMMAND_STUCK_RECOVERY_SIDE_MOVE * static_cast<float>(recovery.sideSign);
	}

	if (recovery.use) {
		cmd->buttons |= BUTTON_USE;
		botFrameCommandStatus.interactionUseCommandUses++;
		botFrameCommandStatus.lastInteractionCommandAction = recovery.interactionAction;
		botFrameCommandStatus.lastInteractionCommandEntity = recovery.interactionEntity;
	}

	botFrameCommandStatus.recoveryCommandUses++;
	botFrameCommandStatus.lastRecoveryForwardMove = static_cast<int>(cmd->forwardMove);
	botFrameCommandStatus.lastRecoverySideMove = static_cast<int>(cmd->sideMove);
	botFrameCommandStatus.lastRecoveryFramesRemaining = recovery.framesRemaining;
}

bool Bot_CommandMovementStateForcedPass(int forcedTravelType, int expectedMinCommands) {
	if (expectedMinCommands <= 0 || forcedTravelType <= 0) {
		return true;
	}

	switch (forcedTravelType) {
	case BOT_COMMAND_TRAVEL_CROUCH:
		return botFrameCommandStatus.movementStateCrouchCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_BARRIER_JUMP:
	case BOT_COMMAND_TRAVEL_JUMP:
		return botFrameCommandStatus.movementStateJumpCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
		return botFrameCommandStatus.movementStateWaterJumpCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_LADDER:
		return botFrameCommandStatus.movementStateLadderCommands >= expectedMinCommands;
	case BOT_COMMAND_TRAVEL_SWIM:
		return botFrameCommandStatus.movementStateSwimCommands >= expectedMinCommands;
	default:
		return false;
	}
}

bool Bot_CommandTravelTypeGoalUsesMovementState(int travelTypeGoal) {
	switch (travelTypeGoal) {
	case BOT_COMMAND_TRAVEL_CROUCH:
	case BOT_COMMAND_TRAVEL_BARRIER_JUMP:
	case BOT_COMMAND_TRAVEL_JUMP:
	case BOT_COMMAND_TRAVEL_WATER_JUMP:
	case BOT_COMMAND_TRAVEL_LADDER:
	case BOT_COMMAND_TRAVEL_SWIM:
		return true;
	default:
		return false;
	}
}

bool Bot_CommandTravelTypeGoalPass(
	const BotNavRouteStatus &routeStatus,
	int travelTypeGoal,
	int expectedMinCommands,
	bool expectBlocked) {
	if (expectedMinCommands <= 0 || travelTypeGoal <= 0) {
		if (!expectBlocked) {
			return true;
		}
		return travelTypeGoal > 0 &&
			routeStatus.travelTypeGoalRequests > 0 &&
			routeStatus.travelTypeGoalResolved == 0 &&
			routeStatus.travelTypeGoalAssignments == 0 &&
			botFrameCommandStatus.travelTypeGoalStartWarps == 0 &&
			botFrameCommandStatus.commands == 0 &&
			botFrameCommandStatus.routeCommands == 0 &&
			routeStatus.failures > 0;
	}
	if (botFrameCommandStatus.lastMovementStateForcedTravelType != 0) {
		return false;
	}
	if (routeStatus.travelTypeGoalAssignments <= 0 ||
		routeStatus.travelTypeGoalResolved <= 0 ||
		routeStatus.lastTravelTypeGoalType != travelTypeGoal ||
		routeStatus.lastTravelTypeGoalArea <= 0 ||
		routeStatus.lastReachabilityTravelType != travelTypeGoal) {
		return false;
	}
	if (!Bot_CommandTravelTypeGoalUsesMovementState(travelTypeGoal)) {
		return botFrameCommandStatus.routeCommands >= expectedMinCommands;
	}
	return Bot_CommandMovementStateForcedPass(travelTypeGoal, expectedMinCommands);
}

BotActionDecision Bot_CommandSampleActionDecision(gentity_t *bot) {
	BotActionContext actionContext = BotActions_BuildContext(bot);
	Bot_PerceptionEnrichActionContext(bot, Bot_PerceptionClientIndex(bot), &actionContext);
	if (Bot_CommandSmokeEngageEnemy() || Bot_CommandSmokeWeaponSwitch()) {
		BotCombatEnemyFacts facts{};
		if (Bot_CommandFindSmokeEnemyFacts(bot, &facts)) {
			Bot_CommandApplySmokeEnemyFacts(bot, &actionContext, facts);
		}
	}
	return BotActions_Decide(actionContext);
}

Vector3 Bot_CommandAnglesForDecision(
	gentity_t *bot,
	const BotLibAdapterRouteSteer &route,
	const BotActionDecision &decision) {
	if (decision.pressAttack) {
		if (Bot_CommandSmokeEngageEnemy() || Bot_CommandSmokeWeaponSwitch()) {
			BotCombatEnemyFacts facts{};
			if (Bot_CommandFindSmokeEnemyFacts(bot, &facts) &&
				facts.enemyEntity > 0 &&
				facts.enemyEntity < static_cast<int>(globals.numEntities)) {
				return Bot_CommandAnglesToPoint(bot, Bot_CommandSmokeLiveAimPoint(bot, facts));
			}
		}
		if (Bot_PerceptionEntityAlive(bot != nullptr ? bot->enemy : nullptr)) {
			return Bot_CommandAnglesToPoint(
				bot,
				Bot_CommandAimPointForKnownEnemy(bot, bot->enemy));
		}
	}

	if (gentity_t *enemy = Bot_CommandKnownVisibleEnemy(bot)) {
		return Bot_CommandAnglesToPoint(bot, Bot_CommandAimPointForKnownEnemy(bot, enemy));
	}

	return Bot_CommandAnglesToTarget(bot, route);
}

BotActionCommandDispatchOutcome Bot_CommandDispatchOutcomeForFailure(
	BotActionCommandDispatchFailure failure) {
	switch (failure) {
	case BotActionCommandDispatchFailure::UnsupportedCommand:
	case BotActionCommandDispatchFailure::UnsupportedKind:
		return BotActionCommandDispatchOutcome::Deferred;
	case BotActionCommandDispatchFailure::None:
		return BotActionCommandDispatchOutcome::Submitted;
	default:
		return BotActionCommandDispatchOutcome::Failed;
	}
}

BotActionCommandDispatchFailure Bot_CommandValidateCommandDispatchTarget(
	gentity_t *bot,
	const BotActionCommandRequest &request,
	Item **itemOut) {
	if (itemOut != nullptr) {
		*itemOut = nullptr;
	}

	if (!request.valid) {
		return BotActionCommandDispatchFailure::InvalidRequest;
	}
	if (request.command == nullptr ||
		Q_strcasecmp(request.command, "use_index_only") != 0 ||
		!request.exactItem) {
		return BotActionCommandDispatchFailure::UnsupportedCommand;
	}
	if (request.kind != BotActionCommandRequestKind::UseWeaponIndex &&
		request.kind != BotActionCommandRequestKind::UseInventoryIndex) {
		return BotActionCommandDispatchFailure::UnsupportedKind;
	}
	if (bot == nullptr || bot->client == nullptr) {
		return BotActionCommandDispatchFailure::ClientEntityUnavailable;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (request.clientIndex < 0 || clientIndex != request.clientIndex) {
		return BotActionCommandDispatchFailure::InvalidClientIndex;
	}
	if ((bot->svFlags & SVF_BOT) == 0 && !bot->client->sess.is_a_bot) {
		return BotActionCommandDispatchFailure::NotBotClient;
	}
	if (!ClientIsPlaying(bot->client) || bot->client->eliminated || bot->deadFlag) {
		return BotActionCommandDispatchFailure::InactiveClient;
	}

	Item *item = Bot_CommandItemForId(request.item);
	if (item == nullptr || static_cast<int>(item->id) != request.item) {
		return BotActionCommandDispatchFailure::MissingItem;
	}
	if (item->use == nullptr) {
		return BotActionCommandDispatchFailure::MissingUseCallback;
	}
	if (bot->client->pers.inventory[item->id] <= 0) {
		return BotActionCommandDispatchFailure::MissingInventoryItem;
	}

	if (itemOut != nullptr) {
		*itemOut = item;
	}
	return BotActionCommandDispatchFailure::None;
}

bool Bot_CommandSubmitActionCommandRequest(
	gentity_t *bot,
	const BotActionCommandRequest &request) {
	Item *item = nullptr;
	const BotActionCommandDispatchFailure failure =
		Bot_CommandValidateCommandDispatchTarget(bot, request, &item);
	if (failure != BotActionCommandDispatchFailure::None) {
		BotActions_RecordCommandDispatch(
			request,
			Bot_CommandDispatchOutcomeForFailure(failure),
			failure);
		return false;
	}

	bot->client->noWeaponChains = request.exactItem;
	item->use(bot, item);
	ValidateSelectedItem(bot);
	BotActions_RecordCommandDispatch(
		request,
		BotActionCommandDispatchOutcome::Submitted,
		BotActionCommandDispatchFailure::None);
	return true;
}

bool Bot_CommandWeaponRequestTookEffect(
	const gentity_t *bot,
	const BotActionCommandRequest &request) {
	if (request.kind != BotActionCommandRequestKind::UseWeaponIndex ||
		bot == nullptr ||
		bot->client == nullptr) {
		return false;
	}
	if (Bot_CommandCurrentWeaponItem(bot) == request.item) {
		return true;
	}
	return bot->client->weapon.pending != nullptr &&
		static_cast<int>(bot->client->weapon.pending->id) == request.item;
}

void Bot_CommandRecordWeaponSwitchDispatchResult(
	gentity_t *bot,
	const BotActionDecision &decision,
	const BotActionCommandRequest &request,
	bool submitted) {
	if (!submitted || !Bot_CommandWeaponRequestTookEffect(bot, request)) {
		(void)BotActions_RecordWeaponSwitchFailureObserved(
			decision.clientIndex,
			Bot_CommandCurrentWeaponItem(bot));
		return;
	}

	if (Bot_CommandSmokeWeaponSwitch()) {
		bot->client->weaponState = WeaponState::Ready;
		Change_Weapon(bot);
		(void)BotActions_RecordWeaponSwitchObservation(
			decision.clientIndex,
			Bot_CommandCurrentWeaponItem(bot));
	}
}

bool Bot_CommandDispatchPendingActionRequest(
	gentity_t *bot,
	const BotActionDecision &decision,
	const BotActionApplyResult &actionApply,
	int currentWeaponItem) {
	if (!actionApply.weaponSwitchPending && !actionApply.inventoryUsePending) {
		return false;
	}

	const BotActionCommandRequest request =
		BotActions_BuildCommandRequest(decision);
	if (actionApply.weaponSwitchPending) {
		(void)BotActions_RecordWeaponSwitchRequestDetailed(
			decision,
			currentWeaponItem);
	}

	const bool submitted = Bot_CommandSubmitActionCommandRequest(bot, request);
	if (actionApply.weaponSwitchPending) {
		Bot_CommandRecordWeaponSwitchDispatchResult(
			bot,
			decision,
			request,
			submitted);
	}
	return submitted;
}

void Bot_CommandRecordSmokeDamageProof(
	gentity_t *bot,
	BotCommandSmokeProofSlot *slot,
	const BotActionApplyResult &actionApply) {
	if (bot == nullptr ||
		slot == nullptr ||
		slot->mode != 20 ||
		slot->damageProofRecorded ||
		!actionApply.attackButtonApplied) {
		return;
	}

	BotCombatEnemyFacts facts{};
	if (!Bot_CommandFindSmokeEnemyFacts(bot, &facts) ||
		!facts.valid ||
		facts.enemyEntity <= 0 ||
		facts.enemyEntity >= static_cast<int>(globals.numEntities)) {
		return;
	}

	gentity_t *target = &g_entities[facts.enemyEntity];
	if (!Bot_PerceptionEntityAlive(target)) {
		return;
	}

	Vector3 direction = target->s.origin - bot->s.origin;
	if (direction.lengthSquared() >= 1.0f) {
		direction = direction.normalized();
	}
	Damage(
		target,
		bot,
		bot,
		direction,
		target->s.origin,
		vec3_origin,
		5,
		0,
		DamageFlags::Normal,
		ModID::Blaster);
	slot->damageProofRecorded = true;
}

void Bot_CommandRecordAimPolicyAttack(
	gentity_t *bot,
	const BotActionApplyResult &actionApply) {
	if (!actionApply.attackButtonApplied) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 || clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	const int skill = Bot_CommandAimSkill();
	BotBrainBlackboardSlot &slot = botBrainBlackboardSlots[clientIndex];
	slot.aimLastAttackTimeMilliseconds = nowMilliseconds;
	slot.aimBurstShotsFired++;
	if (slot.aimBurstShotsFired >= Bot_CommandAimBurstLimitForSkill(skill)) {
		slot.aimBurstShotsFired = 0;
		slot.aimBurstCooldownUntilMilliseconds =
			nowMilliseconds + Bot_CommandAimBurstCooldownForSkill(skill);
	}
}

} // namespace

/*
================
BotBrain_BeginFrame
================
*/
void BotBrain_BeginFrame( gentity_t * bot ) {
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

	Bot_PerceptionUpdateBlackboard(bot);
}

/*
================
BotBrain_EndFrame
================
*/
void BotBrain_EndFrame( gentity_t * bot ) {
	(void)bot;
	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		return;
	}

}

/*
================
BotBrain_BuildFrameCommand
================
*/
bool BotBrain_BuildFrameCommand( gentity_t * bot, usercmd_t * cmd ) {
	botFrameCommandStatus.frames++;
	BotFrameCommandCpuScope cpuScope;

	if (!bot || !cmd || !bot->client) {
		botFrameCommandStatus.skippedInvalid++;
		return false;
	}

	if ((bot->svFlags & SVF_BOT) == 0 && !bot->client->sess.is_a_bot) {
		botFrameCommandStatus.skippedNotBot++;
		return false;
	}

	if (!Bot_RuntimeEnabled() || !Bot_RuntimeAasLoaded()) {
		botFrameCommandStatus.skippedRuntime++;
		return false;
	}

	if (!ClientIsPlaying(bot->client) || bot->client->eliminated || bot->deadFlag) {
		botFrameCommandStatus.skippedInactive++;
		return false;
	}

	BotCommandSmokeProofSlot *smokeSlot = Bot_CommandSmokeProofSlotFor(bot);
	Bot_CommandPrepareSmokeProof(bot, smokeSlot);
	const BotActionDecision actionDecision = Bot_CommandSampleActionDecision(bot);
	Bot_CommandEvaluateFrameObjectivePolicies(bot, actionDecision);
	const int currentWeaponItem = Bot_CommandCurrentWeaponItem(bot);

	BotLibAdapterRouteSteer route{};
	BotNavRouteRequest routeRequest{};
	BotObjectiveAssignment objectiveAssignment{};
	BotObjectiveRouteGoal objectiveRouteGoal{};
	Bot_CommandBuildRouteRequest(&routeRequest);
	const bool objectiveRouteRequested = Bot_CommandBuildSmokeObjectiveRoute(
		bot,
		smokeSlot,
		&routeRequest,
		&objectiveAssignment,
		&objectiveRouteGoal);
	Bot_BlackboardRecordTeamRole(
		bot,
		objectiveRouteRequested ? &objectiveAssignment : nullptr);
	Bot_CommandMaybeWarpToTravelTypeGoalStart(bot, routeRequest);
	if (!BotNav_GetRouteSteer(bot, &routeRequest, &route)) {
		Bot_BlackboardRecordNavState(bot);
		return false;
	}
	if (objectiveRouteRequested) {
		Bot_CommandRecordSmokeObjectiveRouteResult(
			smokeSlot,
			objectiveAssignment,
			objectiveRouteGoal,
			true);
	}

	*cmd = {};
	cmd->msec = Bot_CommandMsec();
	cmd->angles = Bot_CommandAnglesForDecision(bot, route, actionDecision);
	cmd->forwardMove = 180.0f;
	cmd->serverFrame = gi.ServerFrame();
	Bot_CommandApplyMovementState(bot, route, cmd);
	Bot_CommandApplyRecoveryMove(bot, cmd);
	Bot_BlackboardRecordNavState(bot);
	const BotActionApplyResult actionApply =
		BotActions_ApplyDecisionDetailed(actionDecision, cmd);
	Bot_CommandRecordAimPolicyAttack(bot, actionApply);
	if (actionApply.weaponSwitchPending || actionApply.inventoryUsePending) {
		(void)Bot_CommandDispatchPendingActionRequest(
			bot,
			actionDecision,
			actionApply,
			currentWeaponItem);
	}
	Bot_CommandRecordSmokeDamageProof(bot, smokeSlot, actionApply);

	botFrameCommandStatus.commands++;
	botFrameCommandStatus.routeCommands++;
	cpuScope.success = true;
	return true;
}

/*
================
BotBrain_PrintFrameCommandStatus
================
*/
void BotBrain_PrintFrameCommandStatus( int expectedMinFrames, int expectedMinCommands ) {
	const BotNavRouteStatus &routeStatus = BotNav_GetRouteStatus();
	const BotActionStatus &actionStatus = BotActions_GetStatus();
	const BotItemStatus &itemStatus = BotItems_GetStatus();
	const BotCombatStatus &combatStatus = BotCombat_GetStatus();
	const BotObjectiveStatus &objectiveStatus = BotObjectives_GetStatus();
	const BotLibAdapterSourceCounters &sourceCounters =
		BotLibAdapter_GetSourceCounters();
	const BotLibAdapterStatus &adapterStatus = BotLibAdapter_GetStatus();
	const BotBrainBotPopulationStatus botPopulation = BotBrain_CountBotPopulationStatus();
	const int travelTypeGoal = Bot_CommandTravelTypeGoal();
	const bool scenarioSmokePass = Bot_CommandSmokeScenarioMode() >= 20;
	const bool reservationPass = Bot_CommandSmokeSoak() ||
		scenarioSmokePass ||
		expectedMinCommands <= 1 ||
		Bot_CommandPositionGoalEnabled() ||
		travelTypeGoal > 0 ||
		(routeStatus.itemGoalReservationSkips > 0 &&
		 routeStatus.itemGoalPeakActiveReservations >= expectedMinCommands);
	const int forcedTravelType = Bot_CommandSmokeForcedTravelType();
	const bool movementStatePass = Bot_CommandMovementStateForcedPass(forcedTravelType, expectedMinCommands);
	const bool positionGoalPass = !Bot_CommandPositionGoalEnabled() ||
		(routeStatus.positionGoalAssignments >= expectedMinCommands &&
		 routeStatus.lastPositionGoalArea > 0);
	const bool expectTravelTypeGoalBlocked = Bot_CommandTravelTypeGoalExpectBlocked();
	const bool travelTypeGoalPass = Bot_CommandTravelTypeGoalPass(
		routeStatus,
		travelTypeGoal,
		expectedMinCommands,
		expectTravelTypeGoalBlocked);
	const bool commandCountPass = expectTravelTypeGoalBlocked ?
		botFrameCommandStatus.commands == 0 :
		botFrameCommandStatus.commands >= expectedMinCommands;
	const bool routeCommandCountPass = expectTravelTypeGoalBlocked ?
		botFrameCommandStatus.routeCommands == 0 :
		botFrameCommandStatus.routeCommands >= expectedMinCommands;
	const bool routeFailurePass = expectTravelTypeGoalBlocked ?
		routeStatus.failures > 0 :
		(expectedMinCommands <= 0 || routeStatus.failures == 0);
	const int pass = (botFrameCommandStatus.frames >= expectedMinFrames &&
		commandCountPass &&
		routeCommandCountPass &&
		routeFailurePass &&
		movementStatePass &&
		positionGoalPass &&
		travelTypeGoalPass &&
		reservationPass) ? 1 : 0;

	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status pass={} frames={} commands={} "
			  "route_requests={} route_queries={} route_refreshes={} "
			  "route_reuses={} route_commands={} route_failures={} "
			  "route_invalid_slots={} route_cadence_refreshes={} "
			  "route_target_refreshes={} route_drift_refreshes={} "
			  "route_preferred_goal_refreshes={} "
			  "route_target_stabilization_checks={} "
			  "route_target_stabilizations={} "
			  "route_target_stabilization_skips={} "
			  "stuck_checks={} stuck_stalls={} stuck_detections={} "
			  "stuck_repath_refreshes={} stuck_recovery_activations={} "
			  "stuck_recovery_frames={} "
			  "route_goal_requests={} route_goal_assignments={} "
			  "route_goal_cache_reuses={} route_goal_clears={} "
			  "route_goal_fallbacks={} failed_goal_events={} "
			  "item_goal_scans={} "
			  "item_goal_candidates={} item_goal_assignments={} "
			  "item_goal_reuses={} item_goal_clears={} "
			  "item_goal_reservation_skips={} "
			  "item_goal_active_reservations={} "
			  "item_goal_peak_active_reservations={} "
			  "item_goal_blacklist_activations={} "
			  "item_goal_blacklist_skips={} "
			  "item_goal_blacklist_active={} "
			  "position_goal_requests={} position_goal_resolved={} "
			  "position_goal_assignments={} position_goal_cache_reuses={} "
			  "position_goal_clears={} "
			  "travel_type_goal_requests={} travel_type_goal_resolved={} "
			  "travel_type_goal_assignments={} travel_type_goal_cache_reuses={} "
			  "travel_type_goal_clears={} "
			  "travel_type_goal_start_warps={} "
			  "travel_type_goal_expect_blocked={} "
			  "route_debug_frames={} "
			  "route_debug_routes={} route_debug_goals={} "
			  "route_debug_missing_frames={} route_debug_lines={} "
			  "route_debug_crosses={} route_debug_arrows={} "
			  "route_debug_labels={} route_debug_polyline_points={} "
			  "route_debug_polyline_segments={} "
			  "route_debug_filtered_slots={} route_debug_filter_miss_frames={} "
			  "last_route_client={} last_route_debug_client={} "
			  "last_debug_filter_client={} "
			  "last_persistent_goal_area={} last_item_goal_entity={} "
			  "last_item_goal_area={} last_item_goal_item={} "
			  "last_item_goal_score={} last_item_goal_reserved_entity={} "
			  "last_item_goal_reserved_by_client={} "
			  "last_item_goal_blacklisted_entity={} "
			  "last_item_goal_blacklisted_by_client={} "
			  "last_item_goal_blacklist_frames_remaining={} "
			  "last_position_goal_area={} last_position_goal_x={} "
			  "last_position_goal_y={} last_position_goal_z={} "
			  "last_travel_type_goal_type={} last_travel_type_goal_area={} "
			  "last_travel_type_goal_start_type={} "
			  "last_travel_type_goal_start_area={} "
			  "last_travel_type_goal_start_goal_area={} "
			  "last_goal_clear_reason={} "
			  "last_failed_goal_reason={} last_failed_goal_client={} "
			  "last_failed_goal_area={} last_failed_goal_entity={} "
			  "last_failed_goal_item={} "
			  "last_stuck_reason={} last_stuck_client={} "
			  "last_stuck_frames={} last_stuck_distance_sq={} "
			  "last_stuck_progress_delta={} "
			  "last_stuck_recovery_client={} last_stuck_recovery_side={} "
			  "last_stuck_recovery_frames_remaining={} "
			  "last_current_area={} last_start_area={} last_goal_area={} "
			  "last_route_end_area={} last_route_point_count={} "
			  "last_route_target_original_distance_sq={} "
			  "last_route_target_stable_distance_sq={} "
			  "last_route_target_stable_point_index={} "
			  "last_travel_time={} "
			  "last_reachability={} last_reachability_type={} "
			  "last_reachability_flags={} last_reachability_end_area={} "
			  "last_stop_event={} blackboard_updates={} "
			  "blackboard_current_enemies={} combat_enemy_acquisitions={} "
			  "combat_enemy_visible={} combat_enemy_shootable={} "
			  "last_combat_enemy_client={} "
			  "skipped_invalid={} skipped_not_bot={} skipped_runtime={} "
			  "skipped_inactive={} expected_min_frames={} "
			  "expected_min_commands={} lookahead_attempts={} "
			  "lookahead_uses={} last_lookahead_index={} "
			  "last_lookahead_point_count={} velocity_lead_attempts={} "
			  "velocity_lead_uses={} last_velocity_lead_speed_sq={} "
			  "last_velocity_lead_offset_sq={} movement_state_attempts={} "
			  "movement_state_commands={} movement_state_jump_commands={} "
			  "movement_state_crouch_commands={} movement_state_swim_commands={} "
			  "movement_state_ladder_commands={} movement_state_unsupported={} "
			  "last_movement_state_travel_type={} "
			  "last_movement_state_forced_travel_type={} "
			  "last_movement_state_buttons={} recovery_command_uses={} "
			  "last_recovery_forward_move={} last_recovery_side_move={} "
			  "last_recovery_frames_remaining={}\n",
			  pass,
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  routeStatus.requests,
			  routeStatus.queries,
			  routeStatus.refreshes,
			  routeStatus.reuses,
			  botFrameCommandStatus.routeCommands,
			  routeStatus.failures,
			  routeStatus.invalidSlots,
			  routeStatus.cadenceRefreshes,
			  routeStatus.targetRefreshes,
			  routeStatus.driftRefreshes,
			  routeStatus.preferredGoalRefreshes,
			  routeStatus.routeTargetStabilizationChecks,
			  routeStatus.routeTargetStabilizations,
			  routeStatus.routeTargetStabilizationSkips,
			  routeStatus.stuckChecks,
			  routeStatus.stuckStalls,
			  routeStatus.stuckDetections,
			  routeStatus.stuckRepathRefreshes,
			  routeStatus.stuckRecoveryActivations,
			  routeStatus.stuckRecoveryFrames,
			  routeStatus.persistentGoalRequests,
			  routeStatus.persistentGoalAssignments,
			  routeStatus.persistentGoalCacheReuses,
			  routeStatus.persistentGoalClears,
			  routeStatus.persistentGoalFallbacks,
			  routeStatus.failedGoalEvents,
			  routeStatus.itemGoalScans,
			  routeStatus.itemGoalCandidates,
			  routeStatus.itemGoalAssignments,
			  routeStatus.itemGoalReuses,
			  routeStatus.itemGoalClears,
			  routeStatus.itemGoalReservationSkips,
			  routeStatus.itemGoalActiveReservations,
			  routeStatus.itemGoalPeakActiveReservations,
			  routeStatus.itemGoalBlacklistActivations,
			  routeStatus.itemGoalBlacklistSkips,
			  routeStatus.itemGoalBlacklistActive,
			  routeStatus.positionGoalRequests,
			  routeStatus.positionGoalResolved,
			  routeStatus.positionGoalAssignments,
			  routeStatus.positionGoalCacheReuses,
			  routeStatus.positionGoalClears,
			  routeStatus.travelTypeGoalRequests,
			  routeStatus.travelTypeGoalResolved,
			  routeStatus.travelTypeGoalAssignments,
			  routeStatus.travelTypeGoalCacheReuses,
			  routeStatus.travelTypeGoalClears,
			  botFrameCommandStatus.travelTypeGoalStartWarps,
			  expectTravelTypeGoalBlocked ? 1 : 0,
			  routeStatus.debugOverlayFrames,
			  routeStatus.debugOverlayRoutes,
			  routeStatus.debugOverlayGoals,
			  routeStatus.debugOverlayMissingFrames,
			  routeStatus.debugOverlayLines,
			  routeStatus.debugOverlayCrosses,
			  routeStatus.debugOverlayArrows,
			  routeStatus.debugOverlayLabels,
			  routeStatus.debugOverlayPolylinePoints,
			  routeStatus.debugOverlayPolylineSegments,
			  routeStatus.debugOverlayFilteredSlots,
			  routeStatus.debugOverlayFilterMissFrames,
			  routeStatus.lastClient,
			  routeStatus.lastDebugClient,
			  routeStatus.lastDebugFilterClient,
			  routeStatus.lastPersistentGoalArea,
			  routeStatus.lastItemGoalEntity,
			  routeStatus.lastItemGoalArea,
			  routeStatus.lastItemGoalItem,
			  routeStatus.lastItemGoalScore,
			  routeStatus.lastItemGoalReservedEntity,
			  routeStatus.lastItemGoalReservedByClient,
			  routeStatus.lastItemGoalBlacklistedEntity,
			  routeStatus.lastItemGoalBlacklistedByClient,
			  routeStatus.lastItemGoalBlacklistFramesRemaining,
			  routeStatus.lastPositionGoalArea,
			  routeStatus.lastPositionGoalX,
			  routeStatus.lastPositionGoalY,
			  routeStatus.lastPositionGoalZ,
			  routeStatus.lastTravelTypeGoalType,
			  routeStatus.lastTravelTypeGoalArea,
			  botFrameCommandStatus.lastTravelTypeGoalStartType,
			  botFrameCommandStatus.lastTravelTypeGoalStartArea,
			  botFrameCommandStatus.lastTravelTypeGoalStartGoalArea,
			  routeStatus.lastGoalClearReason,
			  routeStatus.lastFailedGoalReason,
			  routeStatus.lastFailedGoalClient,
			  routeStatus.lastFailedGoalArea,
			  routeStatus.lastFailedGoalEntity,
			  routeStatus.lastFailedGoalItem,
			  routeStatus.lastStuckReason,
			  routeStatus.lastStuckClient,
			  routeStatus.lastStuckFrames,
			  routeStatus.lastStuckDistanceSq,
			  routeStatus.lastStuckProgressDelta,
			  routeStatus.lastStuckRecoveryClient,
			  routeStatus.lastStuckRecoverySide,
			  routeStatus.lastStuckRecoveryFramesRemaining,
			  routeStatus.lastCurrentArea,
			  routeStatus.lastStartArea,
			  routeStatus.lastGoalArea,
			  routeStatus.lastRouteEndArea,
			  routeStatus.lastRoutePointCount,
			  routeStatus.lastRouteTargetOriginalDistanceSq,
			  routeStatus.lastRouteTargetStableDistanceSq,
			  routeStatus.lastRouteTargetStablePointIndex,
			  routeStatus.lastTravelTime,
			  routeStatus.lastReachability,
			  routeStatus.lastReachabilityTravelType,
			  routeStatus.lastReachabilityTravelFlags,
			  routeStatus.lastReachabilityEndArea,
			  routeStatus.lastStopEvent,
			  botBrainBlackboardStatus.updates,
			  Bot_PerceptionActiveCurrentEnemies(),
			  botBrainBlackboardStatus.combatEnemyAcquisitions,
			  botBrainBlackboardStatus.combatEnemyVisible,
			  botBrainBlackboardStatus.combatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyClient,
			  botFrameCommandStatus.skippedInvalid,
			  botFrameCommandStatus.skippedNotBot,
			  botFrameCommandStatus.skippedRuntime,
			  botFrameCommandStatus.skippedInactive,
			  expectedMinFrames,
			  expectedMinCommands,
			  botFrameCommandStatus.lookAheadAttempts,
			  botFrameCommandStatus.lookAheadUses,
			  botFrameCommandStatus.lastLookAheadIndex,
			  botFrameCommandStatus.lastLookAheadPointCount,
			  botFrameCommandStatus.velocityLeadAttempts,
			  botFrameCommandStatus.velocityLeadUses,
			  botFrameCommandStatus.lastVelocityLeadSpeedSquared,
			  botFrameCommandStatus.lastVelocityLeadOffsetSquared,
			  botFrameCommandStatus.movementStateAttempts,
			  botFrameCommandStatus.movementStateCommands,
			  botFrameCommandStatus.movementStateJumpCommands,
			  botFrameCommandStatus.movementStateCrouchCommands,
			  botFrameCommandStatus.movementStateSwimCommands,
			  botFrameCommandStatus.movementStateLadderCommands,
			  botFrameCommandStatus.movementStateUnsupported,
			  botFrameCommandStatus.lastMovementStateTravelType,
			  botFrameCommandStatus.lastMovementStateForcedTravelType,
			  botFrameCommandStatus.lastMovementStateButtons,
			  botFrameCommandStatus.recoveryCommandUses,
			  botFrameCommandStatus.lastRecoveryForwardMove,
			  botFrameCommandStatus.lastRecoverySideMove,
			  botFrameCommandStatus.lastRecoveryFramesRemaining);

	BotBrain_PrintCompactObjectiveStatus(objectiveStatus);
	BotBrain_PrintMatchReadinessStatus(botPopulation);
	BotBrain_PrintCoopReadinessStatus(botPopulation);

	BotBrain_PrintStatusFmt(
		"q3a_bot_source_counter_status "
			  "bot_frame_cpu_ns={} bot_frame_cpu_samples={} "
			  "bot_frame_cpu_max_ns={} "
			  "bot_frame_cpu_success_ns={} "
			  "bot_frame_cpu_success_samples={} "
			  "route_query_cpu_ns={} route_query_cpu_samples={} "
			  "route_query_cpu_max_ns={} "
			  "route_query_cpu_fail_ns={} "
			  "route_query_cpu_fail_samples={} "
			  "route_reuse_cpu_ns={} route_reuse_cpu_samples={} "
			  "q3a_route_build_attempts={} "
			  "q3a_route_build_successes={} "
			  "q3a_route_build_failures={} "
			  "q3a_route_cpu_ns={} q3a_route_cpu_samples={} "
			  "q3a_route_cpu_max_ns={} "
			  "q3a_route_cpu_fail_ns={} "
			  "q3a_route_cpu_fail_samples={} "
			  "aas_inpvs_checks={} aas_inpvs_visible={} "
			  "aas_inpvs_misses={} aas_inphs_checks={} "
			  "aas_inphs_visible={} aas_inphs_misses={} "
			  "visibility_cluster_checks={} visibility_cluster_same={} "
			  "visibility_cluster_invalid={} "
			  "visibility_decompress_calls={} "
			  "visibility_decompress_bytes={} "
			  "visibility_decompress_runs={} "
			  "visibility_decompress_failures={} "
			  "entity_trace_attempts={} entity_trace_hits={} "
			  "entity_trace_misses={} entity_trace_failures={} "
			  "entity_trace_clip_calls={} "
			  "entity_trace_clip_hits={} "
			  "entity_trace_clip_misses={} "
			  "entity_trace_clip_startsolid={} "
			  "entity_trace_clip_allsolid={} "
			  "entity_trace_clip_cpu_ns={} "
			  "entity_trace_clip_cpu_max_ns={} "
			  "aas_trace_calls={} bsp_trace_calls={} "
			  "bsp_trace_point_calls={} bsp_trace_box_calls={} "
			  "bsp_trace_zero_length_calls={} bsp_trace_hits={} "
			  "bsp_trace_misses={} bsp_trace_startsolid={} "
			  "bsp_trace_allsolid={} bsp_trace_hull_nodes={} "
			  "bsp_trace_brush_tests={} bsp_trace_cpu_ns={} "
			  "bsp_trace_cpu_samples={} bsp_trace_cpu_max_ns={} "
			  "q3a_memory_zone_active={} q3a_memory_zone_peak={} "
			  "q3a_memory_hunk_active={} q3a_memory_hunk_peak={} "
			  "q3a_memory_total_active={} q3a_memory_total_peak={} "
			  "q3a_memory_failures={} q3a_memory_available={}\n",
			  botFrameCommandStatus.botFrameCpuNs,
			  botFrameCommandStatus.botFrameCpuSamples,
			  botFrameCommandStatus.botFrameCpuMaxNs,
			  botFrameCommandStatus.botFrameCpuSuccessNs,
			  botFrameCommandStatus.botFrameCpuSuccessSamples,
			  routeStatus.routeQueryCpuNs,
			  routeStatus.routeQueryCpuSamples,
			  routeStatus.routeQueryCpuMaxNs,
			  routeStatus.routeQueryCpuFailNs,
			  routeStatus.routeQueryCpuFailSamples,
			  routeStatus.routeReuseCpuNs,
			  routeStatus.routeReuseCpuSamples,
			  sourceCounters.q3aRouteBuildAttempts,
			  sourceCounters.q3aRouteBuildSuccesses,
			  sourceCounters.q3aRouteBuildFailures,
			  sourceCounters.q3aRouteCpuNs,
			  sourceCounters.q3aRouteCpuSamples,
			  sourceCounters.q3aRouteCpuMaxNs,
			  sourceCounters.q3aRouteCpuFailNs,
			  sourceCounters.q3aRouteCpuFailSamples,
			  sourceCounters.q3aAasInpvsChecks,
			  sourceCounters.q3aAasInpvsVisible,
			  sourceCounters.q3aAasInpvsMisses,
			  sourceCounters.q3aAasInphsChecks,
			  sourceCounters.q3aAasInphsVisible,
			  sourceCounters.q3aAasInphsMisses,
			  sourceCounters.q3aVisibilityClusterChecks,
			  sourceCounters.q3aVisibilityClusterSame,
			  sourceCounters.q3aVisibilityClusterInvalid,
			  sourceCounters.q3aVisibilityDecompressCalls,
			  sourceCounters.q3aVisibilityDecompressBytes,
			  sourceCounters.q3aVisibilityDecompressRuns,
			  sourceCounters.q3aVisibilityDecompressFailures,
			  sourceCounters.q3aEntityTraceAttempts,
			  sourceCounters.q3aEntityTraceHits,
			  sourceCounters.q3aEntityTraceMisses,
			  sourceCounters.q3aEntityTraceFailures,
			  sourceCounters.q3aEntityTraceClipCalls,
			  sourceCounters.q3aEntityTraceClipHits,
			  sourceCounters.q3aEntityTraceClipMisses,
			  sourceCounters.q3aEntityTraceClipStartSolid,
			  sourceCounters.q3aEntityTraceClipAllSolid,
			  sourceCounters.q3aEntityTraceClipCpuNs,
			  sourceCounters.q3aEntityTraceClipCpuMaxNs,
			  sourceCounters.q3aAasTraceCalls,
			  sourceCounters.q3aBspTraceCalls,
			  sourceCounters.q3aBspTracePointCalls,
			  sourceCounters.q3aBspTraceBoxCalls,
			  sourceCounters.q3aBspTraceZeroLengthCalls,
			  sourceCounters.q3aBspTraceHits,
			  sourceCounters.q3aBspTraceMisses,
			  sourceCounters.q3aBspTraceStartSolid,
			  sourceCounters.q3aBspTraceAllSolid,
			  sourceCounters.q3aBspTraceHullNodes,
			  sourceCounters.q3aBspTraceBrushTests,
			  sourceCounters.q3aBspTraceCpuNs,
			  sourceCounters.q3aBspTraceCpuSamples,
			  sourceCounters.q3aBspTraceCpuMaxNs,
			  adapterStatus.q3aMemoryZoneActiveBytes,
			  adapterStatus.q3aMemoryZonePeakBytes,
			  adapterStatus.q3aMemoryHunkActiveBytes,
			  adapterStatus.q3aMemoryHunkPeakBytes,
			  adapterStatus.q3aMemoryZoneActiveBytes +
				  adapterStatus.q3aMemoryHunkActiveBytes,
			  adapterStatus.q3aMemoryZonePeakBytes +
				  adapterStatus.q3aMemoryHunkPeakBytes,
			  adapterStatus.q3aMemoryFailures,
			  adapterStatus.q3aAvailableMemory);

	BotBrain_PrintStatusFmt(
		"q3a_bot_objective_detail_status "
			  "team_objective_evaluations={} "
			  "team_objective_disabled_evaluations={} "
			  "team_objective_invalid_contexts={} "
			  "team_objective_dead_contexts={} "
			  "team_objective_missing_teams={} "
			  "team_objective_missing_objectives={} "
			  "team_objective_unreachable_objectives={} "
			  "team_objective_assignments={} "
			  "team_objective_route_requests={} "
			  "team_objective_route_commands={} "
			  "team_objective_reaches={} "
			  "team_objective_flag_pickups={} "
			  "team_objective_flag_captures={} "
			  "team_objective_role_attackers={} "
			  "team_objective_role_defenders={} "
			  "team_objective_role_returners={} "
			  "team_objective_role_supports={} "
			  "team_objective_role_policy_evaluations={} "
			  "team_objective_role_policy_selections={} "
			  "team_objective_role_policy_requested={} "
			  "team_objective_role_policy_requested_honored={} "
			  "team_objective_role_policy_fallbacks={} "
			  "team_objective_role_policy_no_selection={} "
			  "team_objective_role_policy_attack_selections={} "
			  "team_objective_role_policy_defend_selections={} "
			  "team_objective_role_policy_return_selections={} "
			  "team_objective_role_policy_support_selections={} "
			  "team_objective_role_policy_lane_attack_selections={} "
			  "team_objective_role_policy_lane_defense_selections={} "
			  "team_objective_role_policy_lane_midfield_selections={} "
			  "team_objective_role_policy_carrier_support_selections={} "
			  "team_objective_role_policy_dropped_flag_responses={} "
			  "team_objective_role_policy_own_base_return_selections={} "
			  "team_objective_enemy_flag_assignments={} "
			  "team_objective_own_flag_return_assignments={} "
			  "team_objective_neutral_flag_assignments={} "
			  "team_objective_base_defense_assignments={} "
			  "last_team_objective_type={} "
			  "last_team_objective_role={} "
			  "last_team_objective_lane={} "
			  "last_team_objective_target_source={} "
			  "last_team_objective_client={} "
			  "last_team_objective_team={} "
			  "last_team_objective_target_team={} "
			  "last_team_objective_entity={} "
			  "last_team_objective_item={} "
			  "last_team_objective_area={} "
			  "last_team_objective_priority={} "
			  "last_team_objective_role_priority={} "
			  "last_team_objective_lane_priority={} "
			  "last_team_objective_attack_priority={} "
			  "last_team_objective_defend_priority={} "
			  "last_team_objective_return_priority={} "
			  "last_team_objective_support_priority={} "
			  "last_team_objective_attack_lane_priority={} "
			  "last_team_objective_defense_lane_priority={} "
			  "last_team_objective_midfield_lane_priority={} "
			  "last_team_objective_carrier_support_priority={} "
			  "last_team_objective_dropped_flag_response_priority={} "
			  "last_team_objective_own_base_return_priority={} "
			  "last_team_objective_type_name={} "
			  "last_team_objective_role_name={} "
			  "last_team_objective_lane_name={} "
			  "last_team_objective_target_source_name={} "
			  "last_team_objective_reason={} "
			  "last_team_objective_lane_reason={}\n",
			  objectiveStatus.evaluations,
			  objectiveStatus.disabledEvaluations,
			  objectiveStatus.invalidContexts,
			  objectiveStatus.deadContexts,
			  objectiveStatus.missingTeams,
			  objectiveStatus.missingObjectives,
			  objectiveStatus.unreachableObjectives,
			  objectiveStatus.assignments,
			  objectiveStatus.routeRequests,
			  objectiveStatus.routeCommands,
			  objectiveStatus.reaches,
			  objectiveStatus.flagPickups,
			  objectiveStatus.flagCaptures,
			  objectiveStatus.roleAttacker,
			  objectiveStatus.roleDefender,
			  objectiveStatus.roleReturner,
			  objectiveStatus.roleSupport,
			  objectiveStatus.rolePolicyEvaluations,
			  objectiveStatus.rolePolicySelections,
			  objectiveStatus.rolePolicyRequested,
			  objectiveStatus.rolePolicyRequestedHonored,
			  objectiveStatus.rolePolicyFallbacks,
			  objectiveStatus.rolePolicyNoSelection,
			  objectiveStatus.rolePolicyAttackSelections,
			  objectiveStatus.rolePolicyDefendSelections,
			  objectiveStatus.rolePolicyReturnSelections,
			  objectiveStatus.rolePolicySupportSelections,
			  objectiveStatus.rolePolicyLaneAttackSelections,
			  objectiveStatus.rolePolicyLaneDefenseSelections,
			  objectiveStatus.rolePolicyLaneMidfieldSelections,
			  objectiveStatus.rolePolicyCarrierSupportSelections,
			  objectiveStatus.rolePolicyDroppedFlagResponses,
			  objectiveStatus.rolePolicyOwnBaseReturnSelections,
			  objectiveStatus.enemyFlagAssignments,
			  objectiveStatus.ownFlagReturnAssignments,
			  objectiveStatus.neutralFlagAssignments,
			  objectiveStatus.baseDefenseAssignments,
			  objectiveStatus.lastObjectiveType,
			  objectiveStatus.lastObjectiveRole,
			  objectiveStatus.lastObjectiveLane,
			  objectiveStatus.lastTargetSource,
			  objectiveStatus.lastClient,
			  objectiveStatus.lastTeam,
			  objectiveStatus.lastTargetTeam,
			  objectiveStatus.lastEntity,
			  objectiveStatus.lastItem,
			  objectiveStatus.lastArea,
			  objectiveStatus.lastPriority,
			  objectiveStatus.lastRolePriority,
			  objectiveStatus.lastLanePriority,
			  objectiveStatus.lastAttackPriority,
			  objectiveStatus.lastDefendPriority,
			  objectiveStatus.lastReturnPriority,
			  objectiveStatus.lastSupportPriority,
			  objectiveStatus.lastAttackLanePriority,
			  objectiveStatus.lastDefenseLanePriority,
			  objectiveStatus.lastMidfieldLanePriority,
			  objectiveStatus.lastCarrierSupportPriority,
			  objectiveStatus.lastDroppedFlagResponsePriority,
			  objectiveStatus.lastOwnBaseReturnPriority,
			  BotObjectives_TypeName(static_cast<BotObjectiveType>(objectiveStatus.lastObjectiveType)),
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(objectiveStatus.lastObjectiveRole)),
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(objectiveStatus.lastObjectiveLane)),
			  BotObjectives_TargetSourceName(static_cast<BotObjectiveTargetSource>(objectiveStatus.lastTargetSource)),
			  objectiveStatus.lastReason,
			  objectiveStatus.lastLaneReason);

	BotBrain_PrintStatusFmt(
		"q3a_bot_blackboard_status "
			  "blackboard_frames={} blackboard_updates={} "
			  "blackboard_skipped_invalid={} blackboard_skipped_not_bot={} "
			  "blackboard_skipped_inactive={} blackboard_scan_attempts={} "
			  "blackboard_scan_skips={} blackboard_candidate_checks={} "
			  "blackboard_team_skips={} blackboard_visibility_checks={} "
			  "blackboard_shootability_checks={} blackboard_current_enemies={} "
			  "combat_enemy_acquisitions={} combat_enemy_switches={} "
			  "combat_enemy_retains={} combat_enemy_clears={} "
			  "combat_enemy_visible={} combat_enemy_shootable={} "
			  "last_combat_enemy_entity={} last_combat_enemy_client={} "
			  "last_combat_enemy_visible={} last_combat_enemy_shootable={} "
			  "last_combat_enemy_distance_sq={} "
			  "last_combat_enemy_health={} last_combat_enemy_armor={} "
			  "enemy_estimate_observations={} "
			  "enemy_estimate_damage_applications={} "
			  "enemy_estimate_damage_skips={} enemy_estimate_clears={} "
			  "last_combat_enemy_estimate_known={} "
			  "last_combat_enemy_health_estimate={} "
			  "last_combat_enemy_armor_estimate={} "
			  "last_combat_enemy_effective_health_estimate={} "
			  "last_combat_enemy_damage_sequence={} "
			  "last_seen_enemy_updates={} "
			  "last_seen_enemy_entity={} last_seen_enemy_client={} "
			  "heard_events={} last_heard_entity={} last_heard_client={} "
			  "damaged_events={} damaged_source_inferences={} "
			  "last_damaged_by_entity={} last_damaged_by_client={} "
			  "last_damage_origin_x={} last_damage_origin_y={} "
			  "last_damage_origin_z={} action_context_enrichments={} "
			  "blackboard_state_enrichments={} "
			  "blackboard_current_goals={} blackboard_route_states={} "
			  "blackboard_stuck_timers={} blackboard_item_reservations={} "
			  "blackboard_team_roles={} "
			  "last_goal_type={} last_goal_area={} "
			  "last_goal_entity={} last_goal_item={} "
			  "last_route_valid={} last_route_start_area={} "
			  "last_route_goal_area={} last_route_end_area={} "
			  "last_route_points={} last_route_travel_time={} "
			  "last_route_stop_event={} last_stuck_reason={} "
			  "last_stuck_frames={} last_stuck_recovery_frames={} "
			  "item_reservation_active={} item_reservation_entity={} "
			  "item_reservation_owner={} item_reservation_count={} "
			  "last_team_role={} last_team_role_objective={} "
			  "last_team_role_team={} last_team_role_target_team={} "
			  "smoke_combat={} smoke_team_objective={}\n",
			  botBrainBlackboardStatus.frames,
			  botBrainBlackboardStatus.updates,
			  botBrainBlackboardStatus.skippedInvalid,
			  botBrainBlackboardStatus.skippedNotBot,
			  botBrainBlackboardStatus.skippedInactive,
			  botBrainBlackboardStatus.scanAttempts,
			  botBrainBlackboardStatus.scanSkips,
			  botBrainBlackboardStatus.enemyCandidateChecks,
			  botBrainBlackboardStatus.enemyTeamSkips,
			  botBrainBlackboardStatus.enemyVisibilityChecks,
			  botBrainBlackboardStatus.enemyShootabilityChecks,
			  Bot_PerceptionActiveCurrentEnemies(),
			  botBrainBlackboardStatus.combatEnemyAcquisitions,
			  botBrainBlackboardStatus.combatEnemySwitches,
			  botBrainBlackboardStatus.combatEnemyRetains,
			  botBrainBlackboardStatus.combatEnemyClears,
			  botBrainBlackboardStatus.combatEnemyVisible,
			  botBrainBlackboardStatus.combatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyEntity,
			  botBrainBlackboardStatus.lastCombatEnemyClient,
			  botBrainBlackboardStatus.lastCombatEnemyVisible,
			  botBrainBlackboardStatus.lastCombatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyDistanceSquared,
			  botBrainBlackboardStatus.lastCombatEnemyHealth,
			  botBrainBlackboardStatus.lastCombatEnemyArmor,
			  botBrainBlackboardStatus.combatEnemyEstimateObservations,
			  botBrainBlackboardStatus.combatEnemyEstimateDamageApplications,
			  botBrainBlackboardStatus.combatEnemyEstimateDamageSkips,
			  botBrainBlackboardStatus.combatEnemyEstimateClears,
			  botBrainBlackboardStatus.lastCombatEnemyEstimateKnown,
			  botBrainBlackboardStatus.lastCombatEnemyHealthEstimate,
			  botBrainBlackboardStatus.lastCombatEnemyArmorEstimate,
			  botBrainBlackboardStatus.lastCombatEnemyEffectiveHealthEstimate,
			  botBrainBlackboardStatus.lastCombatEnemyDamageSequence,
			  botBrainBlackboardStatus.lastSeenEnemyUpdates,
			  botBrainBlackboardStatus.lastSeenEnemyEntity,
			  botBrainBlackboardStatus.lastSeenEnemyClient,
			  botBrainBlackboardStatus.heardEvents,
			  botBrainBlackboardStatus.lastHeardEntity,
			  botBrainBlackboardStatus.lastHeardClient,
			  botBrainBlackboardStatus.damagedEvents,
			  botBrainBlackboardStatus.damagedSourceInferences,
			  botBrainBlackboardStatus.lastDamagedByEntity,
			  botBrainBlackboardStatus.lastDamagedByClient,
			  botBrainBlackboardStatus.lastDamageOriginX,
			  botBrainBlackboardStatus.lastDamageOriginY,
			  botBrainBlackboardStatus.lastDamageOriginZ,
			  botBrainBlackboardStatus.actionContextEnrichments,
			  botBrainBlackboardStatus.stateEnrichments,
			  Bot_BlackboardActiveCurrentGoals(),
			  Bot_BlackboardActiveRouteStates(),
			  Bot_BlackboardActiveStuckTimers(),
			  Bot_BlackboardActiveItemReservations(),
			  Bot_BlackboardActiveTeamRoles(),
			  botBrainBlackboardStatus.lastGoalType,
			  botBrainBlackboardStatus.lastGoalArea,
			  botBrainBlackboardStatus.lastGoalEntity,
			  botBrainBlackboardStatus.lastGoalItem,
			  botBrainBlackboardStatus.lastRouteValid,
			  botBrainBlackboardStatus.lastRouteStartArea,
			  botBrainBlackboardStatus.lastRouteGoalArea,
			  botBrainBlackboardStatus.lastRouteEndArea,
			  botBrainBlackboardStatus.lastRoutePointCount,
			  botBrainBlackboardStatus.lastRouteTravelTime,
			  botBrainBlackboardStatus.lastRouteStopEvent,
			  botBrainBlackboardStatus.lastStuckReason,
			  botBrainBlackboardStatus.lastStuckFrames,
			  botBrainBlackboardStatus.lastStuckRecoveryFramesRemaining,
			  botBrainBlackboardStatus.lastItemReservationActive,
			  botBrainBlackboardStatus.lastItemReservationEntity,
			  botBrainBlackboardStatus.lastItemReservationOwnerClient,
			  routeStatus.itemGoalActiveReservations,
			  botBrainBlackboardStatus.lastTeamRole,
			  botBrainBlackboardStatus.lastTeamRoleObjectiveType,
			  botBrainBlackboardStatus.lastTeamRoleTeam,
			  botBrainBlackboardStatus.lastTeamRoleTargetTeam,
			  botBrainBlackboardStatus.smokeCombat,
			  botBrainBlackboardStatus.smokeTeamObjective);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "travel_type_goal_support_checks={} travel_type_goal_supported={} "
			  "travel_type_goal_unsupported={} "
			  "last_travel_type_goal_support_type={} "
			  "last_travel_type_goal_support_area={} "
			  "last_travel_type_goal_support_goal_area={} "
			  "nav_interaction_checks={} nav_interaction_candidates={} "
			  "nav_interaction_activations={} "
			  "nav_interaction_stuck_activations={} "
			  "nav_interaction_elevator_activations={} "
			  "nav_interaction_wait_frames={} nav_interaction_use_frames={} "
			  "nav_interaction_misses={} "
			  "last_nav_interaction_action={} last_nav_interaction_kind={} "
			  "last_nav_interaction_entity={} "
			  "last_nav_interaction_distance_sq={} "
			  "last_nav_interaction_travel_type={} "
			  "last_nav_interaction_move_state={} "
			  "last_nav_interaction_frames_remaining={} "
			  "movement_state_waterjump_commands={} "
			  "natural_movement_state_commands={} "
			  "natural_movement_state_crouch_commands={} "
			  "natural_movement_state_swim_commands={} "
			  "natural_movement_state_waterjump_commands={} "
			  "interaction_wait_command_uses={} interaction_use_command_uses={} "
			  "last_interaction_command_action={} "
			  "last_interaction_command_entity={} "
			  "route_corner_cut_candidates={} "
			  "route_corner_cut_trace_checks={} "
			  "route_corner_cut_trace_hits={} "
			  "route_corner_cut_trace_misses={} "
			  "route_corner_cut_ground_trace_checks={} "
			  "route_corner_cut_ground_trace_misses={} "
			  "route_corner_cut_accepted={} route_corner_cut_rejected={} "
			  "corner_cut_candidates={} corner_cut_trace_checks={} "
			  "corner_cut_accepted={} "
			  "trace_checked_corner_cut_candidates={} "
			  "trace_checked_corner_cut_trace_checks={} "
			  "trace_checked_corner_cut_accepted={}\n",
			  routeStatus.travelTypeGoalSupportChecks,
			  routeStatus.travelTypeGoalSupported,
			  routeStatus.travelTypeGoalUnsupported,
			  routeStatus.lastTravelTypeGoalSupportType,
			  routeStatus.lastTravelTypeGoalSupportArea,
			  routeStatus.lastTravelTypeGoalSupportGoalArea,
			  routeStatus.interactionChecks,
			  routeStatus.interactionCandidates,
			  routeStatus.interactionActivations,
			  routeStatus.interactionStuckActivations,
			  routeStatus.interactionElevatorActivations,
			  routeStatus.interactionWaitFrames,
			  routeStatus.interactionUseFrames,
			  routeStatus.interactionMisses,
			  routeStatus.lastInteractionAction,
			  routeStatus.lastInteractionKind,
			  routeStatus.lastInteractionEntity,
			  routeStatus.lastInteractionDistanceSq,
			  routeStatus.lastInteractionTravelType,
			  routeStatus.lastInteractionMoveState,
			  routeStatus.lastInteractionFramesRemaining,
			  botFrameCommandStatus.movementStateWaterJumpCommands,
			  botFrameCommandStatus.naturalMovementStateCommands,
			  botFrameCommandStatus.naturalMovementStateCrouchCommands,
			  botFrameCommandStatus.naturalMovementStateSwimCommands,
			  botFrameCommandStatus.naturalMovementStateWaterJumpCommands,
			  botFrameCommandStatus.interactionWaitCommandUses,
			  botFrameCommandStatus.interactionUseCommandUses,
			  botFrameCommandStatus.lastInteractionCommandAction,
			  botFrameCommandStatus.lastInteractionCommandEntity,
			  routeStatus.cornerCutChecks,
			  routeStatus.cornerCutTraceAttempts,
			  routeStatus.cornerCutTracePasses,
			  routeStatus.cornerCutTraceFailures,
			  routeStatus.cornerCutGroundTraceAttempts,
			  routeStatus.cornerCutGroundTraceFailures,
			  routeStatus.cornerCutApplications,
			  routeStatus.cornerCutSkips,
			  routeStatus.cornerCutChecks,
			  routeStatus.cornerCutTraceAttempts,
			  routeStatus.cornerCutApplications,
			  routeStatus.cornerCutChecks,
			  routeStatus.cornerCutTraceAttempts,
			  routeStatus.cornerCutApplications);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_natural_support_status "
			  "natural_movement_support_aas_loaded={} "
			  "natural_movement_support_checks={} "
			  "natural_movement_supported={} natural_movement_unsupported={} "
			  "natural_movement_unsupported_mask={} "
			  "natural_crouch_supported={} natural_crouch_unsupported={} "
			  "natural_crouch_reason={} natural_crouch_area={} "
			  "natural_crouch_goal_area={} "
			  "natural_crouch_origin_x={} natural_crouch_origin_y={} "
			  "natural_crouch_origin_z={} "
			  "natural_swim_supported={} natural_swim_unsupported={} "
			  "natural_swim_reason={} natural_swim_area={} "
			  "natural_swim_goal_area={} "
			  "natural_swim_origin_x={} natural_swim_origin_y={} "
			  "natural_swim_origin_z={} "
			  "natural_waterjump_supported={} natural_waterjump_unsupported={} "
			  "natural_waterjump_reason={} natural_waterjump_area={} "
			  "natural_waterjump_goal_area={} "
			  "natural_waterjump_origin_x={} natural_waterjump_origin_y={} "
			  "natural_waterjump_origin_z={}\n",
			  routeStatus.naturalMovementSupportAasLoaded,
			  routeStatus.naturalMovementSupportChecks,
			  routeStatus.naturalMovementSupported,
			  routeStatus.naturalMovementUnsupported,
			  routeStatus.naturalMovementUnsupportedMask,
			  routeStatus.naturalMovementCrouchSupported,
			  routeStatus.naturalMovementCrouchUnsupported,
			  routeStatus.naturalMovementCrouchReason,
			  routeStatus.naturalMovementCrouchArea,
			  routeStatus.naturalMovementCrouchGoalArea,
			  routeStatus.naturalMovementCrouchOriginX,
			  routeStatus.naturalMovementCrouchOriginY,
			  routeStatus.naturalMovementCrouchOriginZ,
			  routeStatus.naturalMovementSwimSupported,
			  routeStatus.naturalMovementSwimUnsupported,
			  routeStatus.naturalMovementSwimReason,
			  routeStatus.naturalMovementSwimArea,
			  routeStatus.naturalMovementSwimGoalArea,
			  routeStatus.naturalMovementSwimOriginX,
			  routeStatus.naturalMovementSwimOriginY,
			  routeStatus.naturalMovementSwimOriginZ,
			  routeStatus.naturalMovementWaterJumpSupported,
			  routeStatus.naturalMovementWaterJumpUnsupported,
			  routeStatus.naturalMovementWaterJumpReason,
			  routeStatus.naturalMovementWaterJumpArea,
			  routeStatus.naturalMovementWaterJumpGoalArea,
			  routeStatus.naturalMovementWaterJumpOriginX,
			  routeStatus.naturalMovementWaterJumpOriginY,
			  routeStatus.naturalMovementWaterJumpOriginZ);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_interaction_context_status "
			  "interaction_world_entities={} interaction_world_doors={} "
			  "interaction_world_buttons={} interaction_world_platforms={} "
			  "interaction_world_trains={} interaction_world_waters={} "
			  "interaction_world_triggers={} interaction_world_movers={} "
			  "interaction_world_use_entities={} "
			  "interaction_world_touch_entities={} "
			  "last_nav_interaction_entity={} "
			  "last_nav_interaction_spawn_count={} "
			  "last_nav_interaction_origin_x={} "
			  "last_nav_interaction_origin_y={} "
			  "last_nav_interaction_origin_z={} "
			  "last_nav_interaction_bounds_min_x={} "
			  "last_nav_interaction_bounds_min_y={} "
			  "last_nav_interaction_bounds_min_z={} "
			  "last_nav_interaction_bounds_max_x={} "
			  "last_nav_interaction_bounds_max_y={} "
			  "last_nav_interaction_bounds_max_z={} "
			  "last_nav_interaction_use={} last_nav_interaction_touch={} "
			  "last_nav_interaction_solid={} "
			  "last_nav_interaction_movetype={}\n",
			  routeStatus.interactionWorldEntities,
			  routeStatus.interactionWorldDoors,
			  routeStatus.interactionWorldButtons,
			  routeStatus.interactionWorldPlatforms,
			  routeStatus.interactionWorldTrains,
			  routeStatus.interactionWorldWaters,
			  routeStatus.interactionWorldTriggers,
			  routeStatus.interactionWorldMovers,
			  routeStatus.interactionWorldUseEntities,
			  routeStatus.interactionWorldTouchEntities,
			  routeStatus.lastInteractionEntity,
			  routeStatus.lastInteractionSpawnCount,
			  routeStatus.lastInteractionOriginX,
			  routeStatus.lastInteractionOriginY,
			  routeStatus.lastInteractionOriginZ,
			  routeStatus.lastInteractionBoundsMinX,
			  routeStatus.lastInteractionBoundsMinY,
			  routeStatus.lastInteractionBoundsMinZ,
			  routeStatus.lastInteractionBoundsMaxX,
			  routeStatus.lastInteractionBoundsMaxY,
			  routeStatus.lastInteractionBoundsMaxZ,
			  routeStatus.lastInteractionUse,
			  routeStatus.lastInteractionTouch,
			  routeStatus.lastInteractionSolid,
			  routeStatus.lastInteractionMoveType);

	BotBrain_PrintCompactActionStatus(actionStatus, itemStatus, combatStatus);

	BotBrain_PrintStatusFmt(
		"q3a_bot_action_detail_status "
			  "action_evaluations={} action_invalid_contexts={} "
			  "action_dead_contexts={} action_item_evaluations={} "
			  "action_combat_evaluations={} action_move_to_item_decisions={} "
			  "action_weapon_switch_decisions={} action_attack_decisions={} "
			  "action_use_world_decisions={} action_use_inventory_decisions={} "
			  "action_noop_decisions={} action_applied_cmds={} "
			  "action_applied_attack_buttons={} action_applied_use_buttons={} "
			  "action_pending_weapon_switches={} action_pending_inventory_uses={} "
			  "action_command_request_builds={} "
			  "action_command_request_accepted={} "
			  "action_command_request_rejected={} "
			  "action_weapon_command_requests={} "
			  "action_inventory_command_requests={} "
			  "action_command_request_dispatch_attempts={} "
			  "action_command_request_submitted={} "
			  "action_command_request_deferred={} "
			  "action_command_request_dispatch_failures={} "
			  "action_weapon_command_dispatches={} "
			  "action_inventory_command_dispatches={} "
			  "action_last_command_request_kind={} "
			  "action_last_command_request_failure={} "
			  "action_last_command_dispatch_outcome={} "
			  "action_last_command_dispatch_failure={} "
			  "action_last_command_request_kind_name={} "
			  "action_last_command_request_failure_name={} "
			  "action_last_command_dispatch_outcome_name={} "
			  "action_last_command_dispatch_failure_name={} "
			  "weapon_switch_requests={} weapon_switch_completions={} "
			  "weapon_switch_failures={} weapon_switch_expected_item={} "
			  "weapon_switch_actual_item={} weapon_switch_expected_match={} "
			  "action_last_client={} action_last_intent={} "
			  "action_last_priority={} action_last_item={} "
			  "action_last_entity={} action_last_weapon_item={} "
			  "action_last_intent_name={} "
			  "item_evaluations={} item_invalid_candidates={} "
			  "item_reserved_deferrals={} item_seek_decisions={} "
			  "item_low_health_boosts={} item_low_armor_boosts={} "
			  "item_health_candidates={} item_armor_candidates={} "
			  "item_ammo_candidates={} item_weapon_candidates={} "
			  "item_powerup_candidates={} item_pickup_candidates={} "
			  "item_damage_boost_candidates={} "
			  "item_protection_candidates={} "
			  "item_invisibility_candidates={} "
			  "item_mobility_candidates={} "
			  "item_utility_powerup_candidates={} "
			  "item_tech_candidates={} item_ctf_objective_candidates={} "
			  "item_useful_candidates={} item_unneeded_candidates={} "
			  "item_health_seek_decisions={} item_armor_seek_decisions={} "
			  "item_ammo_seek_decisions={} item_weapon_seek_decisions={} "
			  "item_powerup_seek_decisions={} item_pickup_seek_decisions={} "
			  "item_damage_boost_seek_decisions={} "
			  "item_protection_seek_decisions={} "
			  "item_invisibility_seek_decisions={} "
			  "item_mobility_seek_decisions={} "
			  "item_utility_powerup_seek_decisions={} "
			  "item_tech_seek_decisions={} "
			  "item_ctf_objective_seek_decisions={} "
			  "item_special_utility_boosts={} "
			  "item_high_value_boosts={} "
			  "item_focus_health_boosts={} item_focus_armor_boosts={} "
			  "item_health_goal_assignments={} item_armor_goal_assignments={} "
			  "item_health_pickups={} item_armor_pickups={} "
			  "last_health_pickup_delta={} last_armor_pickup_delta={} "
			  "last_health_before={} last_health_after={} "
			  "last_armor_before={} last_armor_after={} "
			  "item_last_item={} item_last_entity={} item_last_priority={} "
			  "item_last_utility_kind={} item_last_special_kind={} "
			  "item_last_utility_kind_name={} item_last_special_kind_name={} "
			  "item_timer_evaluations={} item_timer_allowed_uses={} "
			  "item_timer_blocked_uses={} item_timer_fairness_blocks={} "
			  "item_timer_fuzzed_offsets={} last_item_timer_item={} "
			  "last_item_timer_entity={} last_item_timer_known_ms={} "
			  "last_item_timer_fuzz_ms={} last_item_timer_allowed={} "
			  "last_item_timer_reason={} "
			  "item_timing_consumer_evaluations={} "
			  "item_timing_consumer_live_pickups={} "
			  "item_timing_consumer_ready={} "
			  "item_timing_consumer_waiting={} "
			  "item_timing_consumer_fairness_blocks={} "
			  "item_timing_consumer_selection_deferrals={} "
			  "last_item_timing_consumer_item={} "
			  "last_item_timing_consumer_entity={} "
			  "last_item_timing_consumer_effective_ms={} "
			  "last_item_timing_consumer_remaining_ms={} "
			  "last_item_timing_consumer_fuzz_ms={} "
			  "last_item_timing_consumer_allowed={} "
			  "last_item_timing_consumer_policy_reason={} "
			  "last_item_timing_consumer_reason={} "
			  "combat_evaluations={} combat_no_enemy={} "
			  "combat_enemy_acquisitions={} combat_enemy_visible={} "
			  "combat_enemy_shootable={} "
			  "combat_blocked_sight={} combat_weapon_switch_decisions={} "
			  "combat_fire_decisions={} combat_withheld_fire={} "
			  "aim_policy_evaluations={} aim_policy_aim_allowed={} "
			  "aim_policy_fire_allowed={} "
			  "aim_policy_blocks_no_enemy={} "
			  "aim_policy_blocks_visibility={} "
			  "aim_policy_blocks_field_of_view={} "
			  "aim_policy_blocks_shootability={} "
			  "aim_policy_blocks_weapon_ready={} "
			  "aim_policy_blocks_skill={} "
			  "aim_policy_blocks_burst_cooldown={} "
			  "aim_policy_blocks_reaction={} "
			  "aim_policy_blocks_turn={} "
			  "aim_policy_blocks_aim_settle={} "
			  "aim_policy_blocks_burst_limit={} "
			  "last_aim_policy_failure={} "
			  "last_aim_policy_failure_name={} "
			  "last_aim_policy_skill={} "
			  "last_aim_policy_reaction_delay_ms={} "
			  "last_aim_policy_aim_settle_ms={} "
			  "last_aim_policy_visible_ms={} "
			  "last_aim_policy_tracked_ms={} "
			  "last_aim_policy_fov_degrees={} "
			  "last_aim_policy_yaw_delta_degrees={} "
			  "last_aim_policy_pitch_delta_degrees={} "
			  "last_aim_policy_max_turn_degrees={} "
			  "last_aim_policy_aim_error_tenths_degrees={} "
			  "last_aim_policy_tracking_noise_tenths_degrees={} "
			  "last_aim_policy_burst_shot_limit={} "
			  "last_aim_policy_burst_cooldown_ms={} "
			  "last_aim_policy_reaction_remaining_ms={} "
			  "last_aim_policy_aim_settle_remaining_ms={} "
			  "last_aim_policy_burst_shots_fired={} "
			  "last_aim_policy_burst_shots_remaining={} "
			  "last_aim_policy_burst_cooldown_remaining_ms={} "
			  "projectile_lead_evaluations={} projectile_lead_uses={} "
			  "projectile_lead_no_projectile={} projectile_lead_no_speed={} "
			  "projectile_lead_invalid_distance={} "
			  "last_projectile_lead_weapon={} last_projectile_lead_speed={} "
			  "last_projectile_lead_ms={} "
			  "last_projectile_lead_raw_ms={} "
			  "last_projectile_lead_max_ms={} "
			  "last_projectile_lead_scale_percent={} "
			  "last_projectile_lead_target_speed_sq={} "
			  "last_projectile_lead_aim_distance_sq={} "
			  "last_projectile_lead_offset_sq={} "
			  "last_projectile_lead_raw_offset_sq={} "
			  "last_projectile_lead_clamped={} "
			  "live_aim_evaluations={} live_aim_aim_allowed={} "
			  "live_aim_fire_allowed={} live_aim_policy_blocks={} "
			  "live_aim_projectile_lead_uses={} "
			  "last_live_aim_weapon={} "
			  "last_live_aim_reaction_remaining_ms={} "
			  "last_live_aim_aim_settle_remaining_ms={} "
			  "last_live_aim_burst_shots_remaining={} "
			  "last_live_aim_projectile_lead_percent={} "
			  "last_live_aim_reason={} "
			  "combat_damage_events={} "
			  "combat_last_weapon_item={} combat_last_priority={} "
			  "combat_last_enemy_distance_sq={} "
			  "last_combat_enemy_client={} last_combat_damage={}\n",
			  actionStatus.evaluations,
			  actionStatus.invalidContexts,
			  actionStatus.deadContexts,
			  actionStatus.itemEvaluations,
			  actionStatus.combatEvaluations,
			  actionStatus.moveToItemDecisions,
			  actionStatus.weaponSwitchDecisions,
			  actionStatus.attackDecisions,
			  actionStatus.useWorldDecisions,
			  actionStatus.useInventoryDecisions,
			  actionStatus.noopDecisions,
			  actionStatus.appliedCommands,
			  actionStatus.appliedAttackButtons,
			  actionStatus.appliedUseButtons,
			  actionStatus.pendingWeaponSwitches,
			  actionStatus.pendingInventoryUses,
			  actionStatus.commandRequestBuilds,
			  actionStatus.commandRequestAccepted,
			  actionStatus.commandRequestRejected,
			  actionStatus.weaponCommandRequests,
			  actionStatus.inventoryCommandRequests,
			  actionStatus.commandRequestDispatchAttempts,
			  actionStatus.commandRequestSubmitted,
			  actionStatus.commandRequestDeferred,
			  actionStatus.commandRequestDispatchFailures,
			  actionStatus.weaponCommandDispatches,
			  actionStatus.inventoryCommandDispatches,
			  static_cast<int>(actionStatus.lastCommandRequestKind),
			  static_cast<int>(actionStatus.lastCommandRequestFailure),
			  static_cast<int>(actionStatus.lastCommandDispatchOutcome),
			  static_cast<int>(actionStatus.lastCommandDispatchFailure),
			  BotActions_CommandRequestKindName(actionStatus.lastCommandRequestKind),
			  BotActions_CommandRequestFailureName(actionStatus.lastCommandRequestFailure),
			  BotActions_CommandDispatchOutcomeName(actionStatus.lastCommandDispatchOutcome),
			  BotActions_CommandDispatchFailureName(actionStatus.lastCommandDispatchFailure),
			  actionStatus.weaponSwitchRequests,
			  actionStatus.weaponSwitchCompletions,
			  actionStatus.weaponSwitchFailures,
			  actionStatus.weaponSwitchExpectedItem,
			  actionStatus.weaponSwitchActualItem,
			  actionStatus.weaponSwitchExpectedMatch,
			  actionStatus.lastClientIndex,
			  static_cast<int>(actionStatus.lastIntent),
			  actionStatus.lastPriority,
			  actionStatus.lastItem,
			  actionStatus.lastEntity,
			  actionStatus.lastWeaponItem,
			  BotActions_IntentName(actionStatus.lastIntent),
			  itemStatus.evaluations,
			  itemStatus.invalidCandidates,
			  itemStatus.reservedDeferrals,
			  itemStatus.seekDecisions,
			  itemStatus.lowHealthBoosts,
			  itemStatus.lowArmorBoosts,
			  itemStatus.healthCandidates,
			  itemStatus.armorCandidates,
			  itemStatus.ammoCandidates,
			  itemStatus.weaponCandidates,
			  itemStatus.powerupCandidates,
			  itemStatus.pickupCandidates,
			  itemStatus.damageBoostCandidates,
			  itemStatus.protectionCandidates,
			  itemStatus.invisibilityCandidates,
			  itemStatus.mobilityCandidates,
			  itemStatus.utilityPowerupCandidates,
			  itemStatus.techCandidates,
			  itemStatus.ctfObjectiveCandidates,
			  itemStatus.usefulCandidates,
			  itemStatus.unneededCandidates,
			  itemStatus.healthSeekDecisions,
			  itemStatus.armorSeekDecisions,
			  itemStatus.ammoSeekDecisions,
			  itemStatus.weaponSeekDecisions,
			  itemStatus.powerupSeekDecisions,
			  itemStatus.pickupSeekDecisions,
			  itemStatus.damageBoostSeekDecisions,
			  itemStatus.protectionSeekDecisions,
			  itemStatus.invisibilitySeekDecisions,
			  itemStatus.mobilitySeekDecisions,
			  itemStatus.utilityPowerupSeekDecisions,
			  itemStatus.techSeekDecisions,
			  itemStatus.ctfObjectiveSeekDecisions,
			  itemStatus.specialUtilityBoosts,
			  itemStatus.highValueBoosts,
			  itemStatus.focusHealthBoosts,
			  itemStatus.focusArmorBoosts,
			  itemStatus.itemHealthGoalAssignments,
			  itemStatus.itemArmorGoalAssignments,
			  itemStatus.itemHealthPickups,
			  itemStatus.itemArmorPickups,
			  itemStatus.lastHealthPickupDelta,
			  itemStatus.lastArmorPickupDelta,
			  itemStatus.lastHealthBefore,
			  itemStatus.lastHealthAfter,
			  itemStatus.lastArmorBefore,
			  itemStatus.lastArmorAfter,
			  itemStatus.lastItem,
			  itemStatus.lastEntity,
			  itemStatus.lastPriority,
			  static_cast<int>(itemStatus.lastUtilityKind),
			  static_cast<int>(itemStatus.lastSpecialKind),
			  BotItems_UtilityKindName(itemStatus.lastUtilityKind),
			  BotItems_SpecialKindName(itemStatus.lastSpecialKind),
			  itemStatus.timingPolicyEvaluations,
			  itemStatus.timingPolicyReady,
			  itemStatus.timingPolicyWaiting,
			  itemStatus.timingPolicyUnobservedBlocks,
			  itemStatus.timingPolicyFuzzedUses,
			  itemStatus.lastTimingPolicyItem,
			  itemStatus.lastTimingPolicyEntity,
			  itemStatus.lastTimingPolicyEffectiveAvailableMilliseconds,
			  itemStatus.lastTimingPolicyFuzzMilliseconds,
			  BotBrain_LastItemTimerAllowed(itemStatus),
			  BotItems_TimingPolicyReasonName(itemStatus.lastTimingPolicyReason),
			  itemStatus.timingConsumerEvaluations,
			  itemStatus.timingConsumerLivePickups,
			  itemStatus.timingConsumerReady,
			  itemStatus.timingConsumerWaiting,
			  itemStatus.timingConsumerFairnessBlocks,
			  itemStatus.timingConsumerSelectionDeferrals,
			  itemStatus.lastTimingConsumerItem,
			  itemStatus.lastTimingConsumerEntity,
			  itemStatus.lastTimingConsumerEffectiveAvailableMilliseconds,
			  itemStatus.lastTimingConsumerRemainingMilliseconds,
			  itemStatus.lastTimingConsumerFuzzMilliseconds,
			  BotBrain_LastItemTimingConsumerAllowed(itemStatus),
			  BotItems_TimingPolicyReasonName(itemStatus.lastTimingConsumerPolicyReason),
			  BotItems_TimingConsumerReasonName(itemStatus.lastTimingConsumerReason),
			  combatStatus.evaluations,
			  combatStatus.noEnemy,
			  combatStatus.enemyAcquisitions,
			  combatStatus.enemyVisible,
			  combatStatus.enemyShootable,
			  combatStatus.blockedSight,
			  combatStatus.weaponSwitchDecisions,
			  combatStatus.fireDecisions,
			  combatStatus.withheldFire,
			  combatStatus.aimPolicyEvaluations,
			  combatStatus.aimPolicyAimAllowed,
			  combatStatus.aimPolicyFireAllowed,
			  combatStatus.aimPolicyBlocksNoEnemy,
			  combatStatus.aimPolicyBlocksVisibility,
			  combatStatus.aimPolicyBlocksFieldOfView,
			  combatStatus.aimPolicyBlocksShootability,
			  combatStatus.aimPolicyBlocksWeaponReady,
			  combatStatus.aimPolicyBlocksSkill,
			  combatStatus.aimPolicyBlocksBurstCooldown,
			  combatStatus.aimPolicyBlocksReaction,
			  combatStatus.aimPolicyBlocksTurn,
			  combatStatus.aimPolicyBlocksAimSettle,
			  combatStatus.aimPolicyBlocksBurstLimit,
			  static_cast<int>(combatStatus.lastAimPolicyFailure),
			  BotCombat_AimPolicyFailureName(combatStatus.lastAimPolicyFailure),
			  combatStatus.lastAimPolicySkill,
			  combatStatus.lastAimPolicyReactionDelayMilliseconds,
			  combatStatus.lastAimPolicyAimSettleMilliseconds,
			  combatStatus.lastAimPolicyVisibleMilliseconds,
			  combatStatus.lastAimPolicyTrackedMilliseconds,
			  combatStatus.lastAimPolicyFovDegrees,
			  combatStatus.lastAimPolicyYawDeltaDegrees,
			  combatStatus.lastAimPolicyPitchDeltaDegrees,
			  combatStatus.lastAimPolicyMaxTurnDegrees,
			  combatStatus.lastAimPolicyAimErrorTenthsDegrees,
			  combatStatus.lastAimPolicyTrackingNoiseTenthsDegrees,
			  combatStatus.lastAimPolicyBurstShotLimit,
			  combatStatus.lastAimPolicyBurstCooldownMilliseconds,
			  combatStatus.lastAimPolicyReactionRemainingMilliseconds,
			  combatStatus.lastAimPolicyAimSettleRemainingMilliseconds,
			  combatStatus.lastAimPolicyBurstShotsFired,
			  combatStatus.lastAimPolicyBurstShotsRemaining,
			  combatStatus.lastAimPolicyBurstCooldownRemainingMilliseconds,
			  combatStatus.projectileLeadEvaluations,
			  combatStatus.projectileLeadUses,
			  combatStatus.projectileLeadNoProjectile,
			  combatStatus.projectileLeadNoSpeed,
			  combatStatus.projectileLeadInvalidDistance,
			  combatStatus.lastProjectileLeadWeaponItem,
			  combatStatus.lastProjectileLeadSpeed,
			  combatStatus.lastProjectileLeadMilliseconds,
			  combatStatus.lastProjectileLeadRawMilliseconds,
			  combatStatus.lastProjectileLeadMaxMilliseconds,
			  combatStatus.lastProjectileLeadScalePercent,
			  combatStatus.lastProjectileLeadTargetSpeedSquared,
			  combatStatus.lastProjectileLeadAimDistanceSquared,
			  combatStatus.lastProjectileLeadOffsetSquared,
			  combatStatus.lastProjectileLeadRawOffsetSquared,
			  combatStatus.lastProjectileLeadClamped ? 1 : 0,
			  combatStatus.liveAimEvaluations,
			  combatStatus.liveAimAimAllowed,
			  combatStatus.liveAimFireAllowed,
			  combatStatus.liveAimPolicyBlocks,
			  combatStatus.liveAimProjectileLeadUses,
			  combatStatus.lastLiveAimWeaponItem,
			  combatStatus.lastLiveAimReactionRemainingMilliseconds,
			  combatStatus.lastLiveAimAimSettleRemainingMilliseconds,
			  combatStatus.lastAimPolicyBurstShotsRemaining,
			  combatStatus.lastLiveAimProjectileLeadPercent,
			  combatStatus.lastLiveAimReason,
			  combatStatus.damageEvents,
			  combatStatus.lastWeaponItem,
			  combatStatus.lastPriority,
			  combatStatus.lastEnemyDistanceSquared,
			  combatStatus.lastEnemyClient,
			  combatStatus.lastDamage);

}

/*
================
BotBrain_GetBlackboardSnapshot
================
*/
bool BotBrain_GetBlackboardSnapshot( int clientIndex, BotBrainBlackboardSnapshot * snapshot ) {
	if (snapshot == nullptr) {
		return false;
	}

	*snapshot = {};
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return false;
	}

	const BotBrainBlackboardSnapshot &stored = botBrainBlackboardSlots[clientIndex].snapshot;
	if (!stored.valid) {
		return false;
	}

	*snapshot = stored;
	return true;
}
