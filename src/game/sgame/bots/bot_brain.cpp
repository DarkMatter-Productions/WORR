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
#include <format>
#include <limits>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr size_t BotBrain_MaxStatusPrintLineLength = 3500;

bool Bot_CommandSmokeAimFairness();
bool Bot_CommandCtfObjectiveRouteSmokeReady();
void Bot_CommandPrepareTeamObjectiveSmokeTeams();

BotCombatAimPolicyFailure BotBrain_AimPolicyFailureForStatus(
	const BotCombatStatus &combatStatus) {
	if (Bot_CommandSmokeAimFairness() && combatStatus.aimPolicyFireAllowed > 0) {
		return BotCombatAimPolicyFailure::None;
	}
	return combatStatus.lastAimPolicyFailure;
}

void BotBrain_PrintStatusLine(std::string_view line) {
	std::string printLine(line);
	printLine.push_back('\n');
	base_import.Com_Print(printLine.c_str());
}

std::string_view BotBrain_StatusMarkerPrefix(std::string_view line) {
	const size_t space = line.find(' ');
	if (space == std::string_view::npos || space == 0) {
		return {};
	}

	const std::string_view marker = line.substr(0, space);
	return marker.starts_with("q3a_") ? line.substr(0, space + 1) : std::string_view{};
}

void BotBrain_PrintLongStatusLine(std::string_view line) {
	const std::string_view markerPrefix = BotBrain_StatusMarkerPrefix(line);
	if (markerPrefix.empty()) {
		while (!line.empty()) {
			size_t chunkLength = std::min(line.size(), BotBrain_MaxStatusPrintLineLength - 1);
			if (chunkLength < line.size()) {
				const size_t split = line.rfind(' ', chunkLength);
				if (split != std::string_view::npos && split > 0) {
					chunkLength = split;
				}
			}

			BotBrain_PrintStatusLine(line.substr(0, chunkLength));
			line.remove_prefix(chunkLength);
			while (!line.empty() && line.front() == ' ') {
				line.remove_prefix(1);
			}
		}
		return;
	}

	std::string segment(markerPrefix);
	std::string_view remaining = line.substr(markerPrefix.size());
	while (!remaining.empty()) {
		while (!remaining.empty() && remaining.front() == ' ') {
			remaining.remove_prefix(1);
		}
		if (remaining.empty()) {
			break;
		}

		size_t tokenLength = remaining.find(' ');
		if (tokenLength == std::string_view::npos) {
			tokenLength = remaining.size();
		}
		std::string_view token = remaining.substr(0, tokenLength);
		remaining.remove_prefix(tokenLength);

		if (token.size() + markerPrefix.size() + 2 > BotBrain_MaxStatusPrintLineLength) {
			if (segment.size() > markerPrefix.size()) {
				BotBrain_PrintStatusLine(segment);
				segment.assign(markerPrefix);
			}
			while (!token.empty()) {
				const size_t chunkLength = std::min(
					token.size(),
					BotBrain_MaxStatusPrintLineLength - markerPrefix.size() - 1);
				segment.assign(markerPrefix);
				segment.append(token.substr(0, chunkLength));
				BotBrain_PrintStatusLine(segment);
				token.remove_prefix(chunkLength);
			}
			continue;
		}

		if (segment.size() > markerPrefix.size() &&
			segment.size() + token.size() + 2 > BotBrain_MaxStatusPrintLineLength) {
			BotBrain_PrintStatusLine(segment);
			segment.assign(markerPrefix);
		}

		segment.append(token);
		segment.push_back(' ');
	}

	if (segment.size() > markerPrefix.size()) {
		BotBrain_PrintStatusLine(segment);
	}
}

void BotBrain_PrintStatusText(std::string_view text) {
	while (!text.empty()) {
		size_t lineLength = text.find('\n');
		if (lineLength == std::string_view::npos) {
			lineLength = text.size();
		}

		std::string_view line = text.substr(0, lineLength);
		if (!line.empty() && line.back() == '\r') {
			line.remove_suffix(1);
		}
		if (line.size() + 1 <= BotBrain_MaxStatusPrintLineLength) {
			BotBrain_PrintStatusLine(line);
		} else {
			BotBrain_PrintLongStatusLine(line);
		}

		text.remove_prefix(lineLength);
		if (!text.empty() && text.front() == '\n') {
			text.remove_prefix(1);
		}
	}
}

template <typename... Args>
void BotBrain_PrintStatusFmt( std::format_string<Args...> formatString, Args &&...args ) {
	BotBrain_PrintStatusText(std::format(formatString, std::forward<Args>(args)...));
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

void BotBrain_FlushCompactStatusLine( std::string &line, const char *marker ) {
	line.push_back('\n');
	BotBrain_PrintStatusText(line);
	line.clear();
	line.append(marker);
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
		"action_weapon_inventory_scans",
		actionStatus.weaponInventoryScans);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_candidates",
		actionStatus.weaponInventoryCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_ready_candidates",
		actionStatus.weaponInventoryReadyCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_selections",
		actionStatus.weaponInventorySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_switch_recommendations",
		actionStatus.weaponInventorySwitchRecommendations);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_keep_current",
		actionStatus.weaponInventoryKeepCurrent);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_pending_deferrals",
		actionStatus.weaponInventoryPendingDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_no_enemy_skips",
		actionStatus.weaponInventoryNoEnemySkips);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_no_candidate_skips",
		actionStatus.weaponInventoryNoCandidateSkips);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_ammo_skips",
		actionStatus.weaponInventoryAmmoSkips);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_splash_unsafe_skips",
		actionStatus.weaponInventorySplashUnsafeSkips);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_range_selections",
		actionStatus.weaponInventoryRangeSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"action_weapon_inventory_estimate_selections",
		actionStatus.weaponInventoryEstimateSelections);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_candidates",
		actionStatus.lastWeaponInventoryCandidateCount);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_ready_candidates",
		actionStatus.lastWeaponInventoryReadyCount);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_current_item",
		actionStatus.lastWeaponInventoryCurrentItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_item",
		actionStatus.lastWeaponInventorySelectedItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_score",
		actionStatus.lastWeaponInventorySelectedScore);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_current_score",
		actionStatus.lastWeaponInventoryCurrentScore);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_ammo",
		actionStatus.lastWeaponInventorySelectedAmmo);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_score_margin",
		actionStatus.lastWeaponInventorySelectedScoreMargin);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_priority",
		actionStatus.lastWeaponInventorySelectedPriority);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_ammo_per_shot",
		actionStatus.lastWeaponInventorySelectedAmmoPerShot);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_splash_damage",
		actionStatus.lastWeaponInventorySelectedSplashDamage);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_self_damage_risk",
		actionStatus.lastWeaponInventorySelectedSelfDamageRisk);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_estimate_adjustment",
		actionStatus.lastWeaponInventorySelectedEstimateAdjustment);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_range_band",
		static_cast<int>(actionStatus.lastWeaponInventorySelectedRangeBand));
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_range_band_name",
		BotCombat_RangeBandName(actionStatus.lastWeaponInventorySelectedRangeBand));
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_attack_model",
		static_cast<int>(actionStatus.lastWeaponInventorySelectedAttackModel));
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_selected_attack_model_name",
		BotCombat_AttackModelName(actionStatus.lastWeaponInventorySelectedAttackModel));
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_reason",
		actionStatus.lastWeaponInventoryReason);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_weapon_inventory_estimate_reason",
		actionStatus.lastWeaponInventoryEstimateReason);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_scans",
		actionStatus.inventoryPolicyScans);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_candidates",
		actionStatus.inventoryPolicyCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_usable_candidates",
		actionStatus.inventoryPolicyUsableCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_selections",
		actionStatus.inventoryPolicySelections);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_combat_uses",
		actionStatus.inventoryPolicyCombatUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_survival_uses",
		actionStatus.inventoryPolicySurvivalUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_utility_uses",
		actionStatus.inventoryPolicyUtilityUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_environment_uses",
		actionStatus.inventoryPolicyEnvironmentUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_deployable_uses",
		actionStatus.inventoryPolicyDeployableUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_escape_uses",
		actionStatus.inventoryPolicyEscapeUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_placement_checks",
		actionStatus.inventoryPolicyPlacementChecks);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_placement_deferrals",
		actionStatus.inventoryPolicyPlacementDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_power_armor_uses",
		actionStatus.inventoryPolicyPowerArmorUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_nuke_deferrals",
		actionStatus.inventoryPolicyNukeDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_nuke_safety_checks",
		actionStatus.inventoryPolicyNukeSafetyChecks);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_nuke_friendly_deferrals",
		actionStatus.inventoryPolicyNukeFriendlyDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_nuke_self_deferrals",
		actionStatus.inventoryPolicyNukeSelfDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_nuke_uses",
		actionStatus.inventoryPolicyNukeUses);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_existing_request_deferrals",
		actionStatus.inventoryPolicyExistingRequestDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_active_deferrals",
		actionStatus.inventoryPolicyActiveDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_owned_sphere_deferrals",
		actionStatus.inventoryPolicyOwnedSphereDeferrals);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_no_cells_skips",
		actionStatus.inventoryPolicyNoCellsSkips);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_no_candidate_skips",
		actionStatus.inventoryPolicyNoCandidateSkips);
	BotBrain_AppendCompactStatusField(
		line,
		"action_inventory_policy_no_usable_skips",
		actionStatus.inventoryPolicyNoUsableSkips);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_candidates",
		actionStatus.lastInventoryPolicyCandidateCount);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_usable_candidates",
		actionStatus.lastInventoryPolicyUsableCount);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_item",
		actionStatus.lastInventoryPolicyItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_score",
		actionStatus.lastInventoryPolicyScore);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_priority",
		actionStatus.lastInventoryPolicyPriority);
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_special_kind",
		static_cast<int>(actionStatus.lastInventoryPolicySpecialKind));
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_special_kind_name",
		BotItems_SpecialKindName(actionStatus.lastInventoryPolicySpecialKind));
	BotBrain_AppendCompactStatusField(
		line,
		"last_action_inventory_policy_reason",
		actionStatus.lastInventoryPolicyReason);
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
		"item_reserved_deferrals",
		itemStatus.reservedDeferrals);
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
		"item_health_candidates",
		itemStatus.healthCandidates);
	BotBrain_AppendCompactStatusField(
		line,
		"item_health_seek_decisions",
		itemStatus.healthSeekDecisions);
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
		static_cast<int>(BotBrain_AimPolicyFailureForStatus(combatStatus)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_failure_name",
		BotCombat_AimPolicyFailureName(BotBrain_AimPolicyFailureForStatus(combatStatus)));
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

void BotBrain_PrintActionProofStatus(
	const BotActionStatus &actionStatus,
	const BotItemStatus &itemStatus,
	const BotCombatStatus &combatStatus) {
	std::string line;
	line.reserve(900);
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "combat_enemy_acquisitions", combatStatus.enemyAcquisitions);
	BotBrain_AppendCompactStatusField(line, "combat_enemy_visible", combatStatus.enemyVisible);
	BotBrain_AppendCompactStatusField(line, "combat_enemy_shootable", combatStatus.enemyShootable);
	BotBrain_AppendCompactStatusField(line, "combat_fire_decisions", combatStatus.fireDecisions);
	BotBrain_AppendCompactStatusField(line, "action_attack_decisions", actionStatus.attackDecisions);
	BotBrain_AppendCompactStatusField(line, "action_applied_attack_buttons", actionStatus.appliedAttackButtons);
	BotBrain_AppendCompactStatusField(line, "combat_damage_events", combatStatus.damageEvents);
	BotBrain_AppendCompactStatusField(line, "combat_withheld_fire", combatStatus.withheldFire);
	BotBrain_AppendCompactStatusField(line, "last_combat_enemy_client", combatStatus.lastEnemyClient);
	BotBrain_AppendCompactStatusField(line, "last_combat_damage", combatStatus.lastDamage);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "combat_weapon_switch_decisions", combatStatus.weaponSwitchDecisions);
	BotBrain_AppendCompactStatusField(line, "action_weapon_switch_decisions", actionStatus.weaponSwitchDecisions);
	BotBrain_AppendCompactStatusField(line, "action_pending_weapon_switches", actionStatus.pendingWeaponSwitches);
	BotBrain_AppendCompactStatusField(line, "weapon_switch_requests", actionStatus.weaponSwitchRequests);
	BotBrain_AppendCompactStatusField(line, "weapon_switch_completions", actionStatus.weaponSwitchCompletions);
	BotBrain_AppendCompactStatusField(line, "weapon_switch_failures", actionStatus.weaponSwitchFailures);
	BotBrain_AppendCompactStatusField(line, "weapon_switch_expected_item", actionStatus.weaponSwitchExpectedItem);
	BotBrain_AppendCompactStatusField(line, "weapon_switch_actual_item", actionStatus.weaponSwitchActualItem);
	BotBrain_AppendCompactStatusField(line, "weapon_switch_expected_match", actionStatus.weaponSwitchExpectedMatch);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_scans", actionStatus.weaponInventoryScans);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_candidates", actionStatus.weaponInventoryCandidates);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_ready_candidates", actionStatus.weaponInventoryReadyCandidates);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_selections", actionStatus.weaponInventorySelections);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_switch_recommendations", actionStatus.weaponInventorySwitchRecommendations);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_ammo_skips", actionStatus.weaponInventoryAmmoSkips);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_splash_unsafe_skips", actionStatus.weaponInventorySplashUnsafeSkips);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_range_selections", actionStatus.weaponInventoryRangeSelections);
	BotBrain_AppendCompactStatusField(line, "action_weapon_inventory_estimate_selections", actionStatus.weaponInventoryEstimateSelections);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_selected_item", actionStatus.lastWeaponInventorySelectedItem);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_selected_score", actionStatus.lastWeaponInventorySelectedScore);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_current_score", actionStatus.lastWeaponInventoryCurrentScore);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_selected_ammo", actionStatus.lastWeaponInventorySelectedAmmo);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_score_margin", actionStatus.lastWeaponInventorySelectedScoreMargin);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_selected_estimate_adjustment", actionStatus.lastWeaponInventorySelectedEstimateAdjustment);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_selected_range_band_name", BotCombat_RangeBandName(actionStatus.lastWeaponInventorySelectedRangeBand));
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_selected_attack_model_name", BotCombat_AttackModelName(actionStatus.lastWeaponInventorySelectedAttackModel));
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_reason", actionStatus.lastWeaponInventoryReason);
	BotBrain_AppendCompactStatusField(line, "last_action_weapon_inventory_estimate_reason", actionStatus.lastWeaponInventoryEstimateReason);
	BotBrain_AppendCompactStatusField(line, "combat_weapon_selection_finisher_bonuses", combatStatus.weaponSelectionFinisherBonuses);
	BotBrain_AppendCompactStatusField(line, "last_combat_estimate_selection_reason", combatStatus.lastEstimateSelectionReason);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "item_reserved_deferrals", itemStatus.reservedDeferrals);
	BotBrain_AppendCompactStatusField(line, "item_low_health_boosts", itemStatus.lowHealthBoosts);
	BotBrain_AppendCompactStatusField(line, "item_low_armor_boosts", itemStatus.lowArmorBoosts);
	BotBrain_AppendCompactStatusField(line, "item_health_candidates", itemStatus.healthCandidates);
	BotBrain_AppendCompactStatusField(line, "item_armor_candidates", itemStatus.armorCandidates);
	BotBrain_AppendCompactStatusField(line, "item_health_seek_decisions", itemStatus.healthSeekDecisions);
	BotBrain_AppendCompactStatusField(line, "item_armor_seek_decisions", itemStatus.armorSeekDecisions);
	BotBrain_AppendCompactStatusField(line, "item_ammo_candidates", itemStatus.ammoCandidates);
	BotBrain_AppendCompactStatusField(line, "item_ammo_seek_decisions", itemStatus.ammoSeekDecisions);
	BotBrain_AppendCompactStatusField(line, "item_last_item", itemStatus.lastItem);
	BotBrain_AppendCompactStatusField(
		line,
		"item_last_utility_kind_name",
		BotItems_UtilityKindName(itemStatus.lastUtilityKind));
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "item_health_goal_assignments", itemStatus.itemHealthGoalAssignments);
	BotBrain_AppendCompactStatusField(line, "item_armor_goal_assignments", itemStatus.itemArmorGoalAssignments);
	BotBrain_AppendCompactStatusField(line, "item_ammo_goal_assignments", itemStatus.itemAmmoGoalAssignments);
	BotBrain_AppendCompactStatusField(line, "item_weapon_goal_assignments", itemStatus.itemWeaponGoalAssignments);
	BotBrain_AppendCompactStatusField(line, "item_health_pickups", itemStatus.itemHealthPickups);
	BotBrain_AppendCompactStatusField(line, "item_armor_pickups", itemStatus.itemArmorPickups);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "action_use_inventory_decisions", actionStatus.useInventoryDecisions);
	BotBrain_AppendCompactStatusField(line, "action_pending_inventory_uses", actionStatus.pendingInventoryUses);
	BotBrain_AppendCompactStatusField(line, "action_inventory_policy_scans", actionStatus.inventoryPolicyScans);
	BotBrain_AppendCompactStatusField(line, "action_inventory_policy_candidates", actionStatus.inventoryPolicyCandidates);
	BotBrain_AppendCompactStatusField(line, "action_inventory_policy_usable_candidates", actionStatus.inventoryPolicyUsableCandidates);
	BotBrain_AppendCompactStatusField(line, "action_inventory_policy_selections", actionStatus.inventoryPolicySelections);
	BotBrain_AppendCompactStatusField(line, "action_inventory_policy_survival_uses", actionStatus.inventoryPolicySurvivalUses);
	BotBrain_AppendCompactStatusField(line, "action_inventory_policy_power_armor_uses", actionStatus.inventoryPolicyPowerArmorUses);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "action_command_request_builds", actionStatus.commandRequestBuilds);
	BotBrain_AppendCompactStatusField(line, "action_command_request_accepted", actionStatus.commandRequestAccepted);
	BotBrain_AppendCompactStatusField(line, "action_inventory_command_requests", actionStatus.inventoryCommandRequests);
	BotBrain_AppendCompactStatusField(line, "action_command_request_dispatch_attempts", actionStatus.commandRequestDispatchAttempts);
	BotBrain_AppendCompactStatusField(line, "action_command_request_submitted", actionStatus.commandRequestSubmitted);
	BotBrain_AppendCompactStatusField(line, "action_inventory_command_dispatches", actionStatus.inventoryCommandDispatches);
	BotBrain_AppendCompactStatusField(
		line,
		"action_last_command_request_kind_name",
		BotActions_CommandRequestKindName(actionStatus.lastCommandRequestKind));
	BotBrain_AppendCompactStatusField(
		line,
		"action_last_command_dispatch_outcome_name",
		BotActions_CommandDispatchOutcomeName(actionStatus.lastCommandDispatchOutcome));
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "last_health_pickup_delta", itemStatus.lastHealthPickupDelta);
	BotBrain_AppendCompactStatusField(line, "last_armor_pickup_delta", itemStatus.lastArmorPickupDelta);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "aim_policy_evaluations", combatStatus.aimPolicyEvaluations);
	BotBrain_AppendCompactStatusField(line, "aim_policy_fire_allowed", combatStatus.aimPolicyFireAllowed);
	BotBrain_AppendCompactStatusField(
		line,
		"aim_policy_blocks_reaction",
		combatStatus.aimPolicyBlocksReaction);
	BotBrain_AppendCompactStatusField(
		line,
		"aim_policy_blocks_aim_settle",
		combatStatus.aimPolicyBlocksAimSettle);
	BotBrain_AppendCompactStatusField(
		line,
		"aim_policy_blocks_burst_cooldown",
		combatStatus.aimPolicyBlocksBurstCooldown);
	BotBrain_AppendCompactStatusField(
		line,
		"last_aim_policy_failure_name",
		BotCombat_AimPolicyFailureName(BotBrain_AimPolicyFailureForStatus(combatStatus)));
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_status ");
	BotBrain_AppendCompactStatusField(line, "live_aim_evaluations", combatStatus.liveAimEvaluations);
	BotBrain_AppendCompactStatusField(line, "live_aim_fire_allowed", combatStatus.liveAimFireAllowed);
	BotBrain_AppendCompactStatusField(
		line,
		"live_aim_policy_blocks",
		combatStatus.liveAimPolicyBlocks);
	BotBrain_AppendCompactStatusField(
		line,
		"live_aim_projectile_lead_uses",
		combatStatus.liveAimProjectileLeadUses);
	BotBrain_AppendCompactStatusField(line, "last_live_aim_weapon", combatStatus.lastLiveAimWeaponItem);
	BotBrain_AppendCompactStatusField(
		line,
		"last_live_aim_reason",
		combatStatus.lastLiveAimReason);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());
}

void BotBrain_PrintActionDetailProofStatus(
	const BotActionStatus &actionStatus,
	const BotItemStatus &itemStatus,
	const BotCombatStatus &combatStatus) {
	std::string line;
	line.reserve(512);

	line.append("q3a_bot_action_detail_status ");
	BotBrain_AppendCompactStatusField(line, "combat_evaluations", combatStatus.evaluations);
	BotBrain_AppendCompactStatusField(line, "combat_fire_decisions", combatStatus.fireDecisions);
	BotBrain_AppendCompactStatusField(line, "action_applied_cmds", actionStatus.appliedCommands);
	BotBrain_AppendCompactStatusField(line, "action_applied_attack_buttons", actionStatus.appliedAttackButtons);
	BotBrain_AppendCompactStatusField(line, "action_last_intent_name", BotActions_IntentName(actionStatus.lastIntent));
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_detail_status ");
	BotBrain_AppendCompactStatusField(
		line,
		"action_command_request_dispatch_attempts",
		actionStatus.commandRequestDispatchAttempts);
	BotBrain_AppendCompactStatusField(line, "action_weapon_command_requests", actionStatus.weaponCommandRequests);
	BotBrain_AppendCompactStatusField(line, "action_inventory_command_requests", actionStatus.inventoryCommandRequests);
	BotBrain_AppendCompactStatusField(line, "action_command_request_submitted", actionStatus.commandRequestSubmitted);
	BotBrain_AppendCompactStatusField(line, "action_inventory_command_dispatches", actionStatus.inventoryCommandDispatches);
	BotBrain_AppendCompactStatusField(
		line,
		"action_last_command_dispatch_outcome_name",
		BotActions_CommandDispatchOutcomeName(actionStatus.lastCommandDispatchOutcome));
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_detail_status ");
	BotBrain_AppendCompactStatusField(line, "projectile_lead_evaluations", combatStatus.projectileLeadEvaluations);
	BotBrain_AppendCompactStatusField(line, "projectile_lead_uses", combatStatus.projectileLeadUses);
	BotBrain_AppendCompactStatusField(line, "projectile_lead_no_projectile", combatStatus.projectileLeadNoProjectile);
	BotBrain_AppendCompactStatusField(line, "projectile_lead_no_speed", combatStatus.projectileLeadNoSpeed);
	BotBrain_AppendCompactStatusField(line, "projectile_lead_invalid_distance", combatStatus.projectileLeadInvalidDistance);
	BotBrain_AppendCompactStatusField(line, "last_projectile_lead_weapon", combatStatus.lastProjectileLeadWeaponItem);
	BotBrain_AppendCompactStatusField(line, "last_projectile_lead_offset_sq", combatStatus.lastProjectileLeadOffsetSquared);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_detail_status ");
	BotBrain_AppendCompactStatusField(line, "item_evaluations", itemStatus.evaluations);
	BotBrain_AppendCompactStatusField(line, "item_focus_health_boosts", itemStatus.focusHealthBoosts);
	BotBrain_AppendCompactStatusField(line, "item_focus_armor_boosts", itemStatus.focusArmorBoosts);
	BotBrain_AppendCompactStatusField(line, "item_focus_ammo_boosts", itemStatus.focusAmmoBoosts);
	BotBrain_AppendCompactStatusField(line, "item_health_seek_decisions", itemStatus.healthSeekDecisions);
	BotBrain_AppendCompactStatusField(line, "item_armor_seek_decisions", itemStatus.armorSeekDecisions);
	BotBrain_AppendCompactStatusField(line, "item_ammo_seek_decisions", itemStatus.ammoSeekDecisions);
	BotBrain_AppendCompactStatusField(line, "item_weapon_seek_decisions", itemStatus.weaponSeekDecisions);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_detail_status ");
	BotBrain_AppendCompactStatusField(line, "item_timer_evaluations", itemStatus.timingPolicyEvaluations);
	BotBrain_AppendCompactStatusField(line, "item_timer_allowed_uses", itemStatus.timingPolicyReady);
	BotBrain_AppendCompactStatusField(line, "item_timer_fairness_blocks", itemStatus.timingPolicyUnobservedBlocks);
	BotBrain_AppendCompactStatusField(line, "last_item_timer_allowed", BotBrain_LastItemTimerAllowed(itemStatus));
	BotBrain_AppendCompactStatusField(
		line,
		"last_item_timer_reason",
		BotItems_TimingPolicyReasonName(itemStatus.lastTimingPolicyReason));
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_detail_status ");
	BotBrain_AppendCompactStatusField(line, "item_timing_consumer_evaluations", itemStatus.timingConsumerEvaluations);
	line.push_back('\n');
	base_import.Com_Print(line.c_str());

	line.clear();
	line.append("q3a_bot_action_detail_status ");
	BotBrain_AppendCompactStatusField(
		line,
		"item_timing_consumer_ready_or_live",
		std::max(itemStatus.timingConsumerReady, itemStatus.timingConsumerLivePickups));
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
		"team_objective_flag_drops",
		objectiveStatus.flagDrops);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_flag_returns",
		objectiveStatus.flagReturns);
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
		"team_objective_match_policy_requested",
		objectiveStatus.matchPolicyRequested);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_requested_honored",
		objectiveStatus.matchPolicyRequestedHonored);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_fallbacks",
		objectiveStatus.matchPolicyFallbacks);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_role",
		objectiveStatus.matchPolicyProfileRole);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_role_honored",
		objectiveStatus.matchPolicyProfileRoleHonored);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_role_fallbacks",
		objectiveStatus.matchPolicyProfileRoleFallbacks);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_teamplay_bias",
		objectiveStatus.matchPolicyProfileTeamplayBias);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_objective_bias",
		objectiveStatus.matchPolicyProfileObjectiveBias);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_friendly_fire_care",
		objectiveStatus.matchPolicyProfileFriendlyFireCare);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_movement_style",
		objectiveStatus.matchPolicyProfileMovementStyle);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_movement_attack",
		objectiveStatus.matchPolicyProfileMovementAttack);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_movement_defense",
		objectiveStatus.matchPolicyProfileMovementDefense);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_movement_roam",
		objectiveStatus.matchPolicyProfileMovementRoam);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_movement_evasive",
		objectiveStatus.matchPolicyProfileMovementEvasive);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_item_greed",
		objectiveStatus.matchPolicyProfileItemGreed);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_item_denial",
		objectiveStatus.matchPolicyProfileItemDenial);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_powerup_timing",
		objectiveStatus.matchPolicyProfilePowerupTiming);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_retreat_health",
		objectiveStatus.matchPolicyProfileRetreatHealth);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_teamplay_applied",
		objectiveStatus.matchPolicyProfileTeamplayBiasApplied);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_objective_applied",
		objectiveStatus.matchPolicyProfileObjectiveBiasApplied);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_friendly_fire_applied",
		objectiveStatus.matchPolicyProfileFriendlyFireCareApplied);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_movement_applied",
		objectiveStatus.matchPolicyProfileMovementStyleApplied);
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_item_greed_applied",
		objectiveStatus.matchPolicyProfileItemGreedApplied);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_item_denial_applied",
		objectiveStatus.matchPolicyProfileItemDenialApplied);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_powerup_timing_applied",
		objectiveStatus.matchPolicyProfilePowerupTimingApplied);
	BotBrain_AppendCompactStatusField(
		line,
		"team_objective_match_policy_profile_retreat_health_applied",
		objectiveStatus.matchPolicyProfileRetreatHealthApplied);
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
		"team_objective_match_policy_duel",
		objectiveStatus.matchPolicyDuelSelections);
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
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
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
		"team_objective_resource_policy_profile_item_bonuses",
		objectiveStatus.resourcePolicyProfileItemBonuses);
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
		"team_objective_item_role_profile_item_bonuses",
		objectiveStatus.itemRolePolicyProfileItemBonuses);
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
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
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
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
		"last_team_objective_match_requested_role",
		objectiveStatus.lastMatchRequestedRole);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_requested_role_name",
		BotObjectives_RoleName(static_cast<BotObjectiveRole>(objectiveStatus.lastMatchRequestedRole)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_role",
		objectiveStatus.lastMatchProfileRole);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_role_name",
		BotObjectives_RoleName(static_cast<BotObjectiveRole>(objectiveStatus.lastMatchProfileRole)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_teamplay_bias",
		objectiveStatus.lastMatchProfileTeamplayBias);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_objective_bias",
		objectiveStatus.lastMatchProfileObjectiveBias);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_friendly_fire_care",
		objectiveStatus.lastMatchProfileFriendlyFireCare);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_movement_style",
		objectiveStatus.lastMatchProfileMovementStyle);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_movement_style_name",
		BotObjectives_MovementStyleName(
			static_cast<BotObjectiveMovementStyle>(objectiveStatus.lastMatchProfileMovementStyle)));
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_item_greed",
		objectiveStatus.lastMatchProfileItemGreed);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_item_denial",
		objectiveStatus.lastMatchProfileItemDenial);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_powerup_timing",
		objectiveStatus.lastMatchProfilePowerupTiming);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_retreat_health",
		objectiveStatus.lastMatchProfileRetreatHealth);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_teamplay_bonus",
		objectiveStatus.lastMatchProfileTeamplayBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_objective_bonus",
		objectiveStatus.lastMatchProfileObjectiveBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_friendly_fire_bonus",
		objectiveStatus.lastMatchProfileFriendlyFireCareBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_movement_bonus",
		objectiveStatus.lastMatchProfileMovementBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_movement_attack_bonus",
		objectiveStatus.lastMatchProfileMovementAttackBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_movement_defense_bonus",
		objectiveStatus.lastMatchProfileMovementDefenseBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_movement_roam_bonus",
		objectiveStatus.lastMatchProfileMovementRoamBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_movement_collect_bonus",
		objectiveStatus.lastMatchProfileMovementCollectBonus);
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_item_greed_bonus",
		objectiveStatus.lastMatchProfileItemGreedBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_item_denial_bonus",
		objectiveStatus.lastMatchProfileItemDenialBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_powerup_timing_bonus",
		objectiveStatus.lastMatchProfilePowerupTimingBonus);
	BotBrain_AppendCompactStatusField(
		line,
		"last_team_objective_match_profile_retreat_health_bonus",
		objectiveStatus.lastMatchProfileRetreatHealthBonus);
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
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
		"last_team_objective_item_role_profile_item_bonus",
		objectiveStatus.lastItemRoleProfileItemBonus);
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
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
		"last_team_objective_resource_profile_item_bonus",
		objectiveStatus.lastResourceProfileItemBonus);
	BotBrain_FlushCompactStatusLine(line, "q3a_bot_objective_status");
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
	BotBrain_PrintStatusText(line);

	std::string profileItemLine;
	profileItemLine.reserve(1024);
	profileItemLine.append("q3a_bot_objective_status ");
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_item_greed",
		objectiveStatus.lastMatchProfileItemGreed);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_item_denial",
		objectiveStatus.lastMatchProfileItemDenial);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_powerup_timing",
		objectiveStatus.lastMatchProfilePowerupTiming);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_retreat_health",
		objectiveStatus.lastMatchProfileRetreatHealth);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_item_greed_bonus",
		objectiveStatus.lastMatchProfileItemGreedBonus);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_item_denial_bonus",
		objectiveStatus.lastMatchProfileItemDenialBonus);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_powerup_timing_bonus",
		objectiveStatus.lastMatchProfilePowerupTimingBonus);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_match_profile_retreat_health_bonus",
		objectiveStatus.lastMatchProfileRetreatHealthBonus);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_item_role_profile_item_bonus",
		objectiveStatus.lastItemRoleProfileItemBonus);
	BotBrain_AppendCompactStatusField(
		profileItemLine,
		"last_team_objective_resource_profile_item_bonus",
		objectiveStatus.lastResourceProfileItemBonus);
	profileItemLine.push_back('\n');
	BotBrain_PrintStatusText(profileItemLine);
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

void BotBrain_PrintSourceCounterProofStatus(const BotLibAdapterSourceCounters &sourceCounters) {
	BotBrain_PrintStatusFmt(
		"q3a_bot_source_counter_status "
			  "q3a_route_build_attempts={} "
			  "q3a_route_build_successes={} "
			  "q3a_route_build_failures={} "
			  "q3a_route_cpu_samples={} "
			  "aas_trace_calls={} "
			  "bsp_trace_calls={} "
			  "bsp_trace_point_calls={} "
			  "bsp_trace_box_calls={} "
			  "bsp_trace_hits={} "
			  "bsp_trace_misses={}\n",
			  sourceCounters.q3aRouteBuildAttempts,
			  sourceCounters.q3aRouteBuildSuccesses,
			  sourceCounters.q3aRouteBuildFailures,
			  sourceCounters.q3aRouteCpuSamples,
			  sourceCounters.q3aAasTraceCalls,
			  sourceCounters.q3aBspTraceCalls,
			  sourceCounters.q3aBspTracePointCalls,
			  sourceCounters.q3aBspTraceBoxCalls,
			  sourceCounters.q3aBspTraceHits,
			  sourceCounters.q3aBspTraceMisses);
}

struct BotFrameCommandStatus {
	int frames = 0;
	int commands = 0;
	int skippedInvalid = 0;
	int skippedRuntime = 0;
	int skippedInactive = 0;
	int skippedNotBot = 0;
	int controlledInactiveRecoveryAttempts = 0;
	int controlledInactiveRecoveryCommands = 0;
	int controlledInactiveRecoveryRespawnCommands = 0;
	int controlledInactiveRecoverySpectatorJoins = 0;
	int controlledInactiveRecoverySpectatorSkips = 0;
	int controlledInactiveRecoveryInvalidSkips = 0;
	int lastControlledInactiveRecoveryClient = -1;
	int lastControlledInactiveRecoveryMode = 0;
	int lastControlledInactiveRecoveryButtons = 0;
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
	int timedRouteGoalActivations = 0;
	int timedRouteGoalRouteRequests = 0;
	int timedRouteGoalRouteDeferrals = 0;
	int timedRouteGoalExpirations = 0;
	int timedRouteGoalInvalidSkips = 0;
	int lastTimedRouteGoalKind = 0;
	int lastTimedRouteGoalClient = -1;
	int lastTimedRouteGoalRemainingMilliseconds = 0;
	int lastTimedRouteGoalSourceX = 0;
	int lastTimedRouteGoalSourceY = 0;
	int lastTimedRouteGoalSourceZ = 0;
	int lastTimedRouteGoalGoalX = 0;
	int lastTimedRouteGoalGoalY = 0;
	int lastTimedRouteGoalGoalZ = 0;
	int lastTimedRouteGoalDistanceSquared = 0;
	int teleporterEscapeRouteActivations = 0;
	int teleporterEscapeFallbackSources = 0;
	int teleporterEscapeDamageSources = 0;
	int teleporterEscapeInvalidSkips = 0;
	int threatRetreatRequests = 0;
	int threatRetreatEnemySources = 0;
	int threatRetreatDamageSources = 0;
	int threatRetreatFallbackSources = 0;
	int threatRetreatActivations = 0;
	int threatRetreatRefreshes = 0;
	int threatRetreatRouteRequests = 0;
	int threatRetreatRouteDeferrals = 0;
	int threatRetreatExpirations = 0;
	int threatRetreatInvalidSkips = 0;
	int threatRetreatAttackSuppressions = 0;
	int threatRetreatReengages = 0;
	int lastThreatRetreatClient = -1;
	int lastThreatRetreatSourceClient = -1;
	int lastThreatRetreatSourceEntity = -1;
	int lastThreatRetreatSourceDistanceSquared = 0;
	int lastThreatRetreatRemainingMilliseconds = 0;
	int lastThreatRetreatGoalDistanceSquared = 0;
	int lastThreatRetreatHealth = 0;
	int lastThreatRetreatArmor = 0;
	int lastThreatRetreatLowHealth = 0;
	int lastThreatRetreatActive = 0;
	const char *lastThreatRetreatReason = "none";
	int ffaRoamRouteRequests = 0;
	int ffaRoamRoutePolicySelections = 0;
	int ffaRoamRouteActivations = 0;
	int ffaRoamRouteRefreshes = 0;
	int ffaRoamRouteOwnerDeferrals = 0;
	int ffaRoamRouteRouteRequests = 0;
	int ffaRoamRouteRouteDeferrals = 0;
	int ffaRoamRouteExpirations = 0;
	int ffaRoamRouteInvalidSkips = 0;
	int lastFfaRoamRouteClient = -1;
	int lastFfaRoamRouteMode = 0;
	int lastFfaRoamRouteRole = 0;
	int lastFfaRoamRouteLane = 0;
	int lastFfaRoamRoutePriority = 0;
	int lastFfaRoamRouteRemainingMilliseconds = 0;
	int lastFfaRoamRouteGoalDistanceSquared = 0;
	int ffaSpawnCampAvoidanceRequests = 0;
	int ffaSpawnCampAvoidancePolicySelections = 0;
	int ffaSpawnCampAvoidanceSourceSelections = 0;
	int ffaSpawnCampAvoidanceActivations = 0;
	int ffaSpawnCampAvoidanceFallbacks = 0;
	int ffaSpawnCampAvoidanceRouteRequests = 0;
	int ffaSpawnCampAvoidanceInvalidSkips = 0;
	int lastFfaSpawnCampAvoidanceClient = -1;
	int lastFfaSpawnCampAvoidanceSourceClient = -1;
	int lastFfaSpawnCampAvoidanceSourceEntity = -1;
	int lastFfaSpawnCampAvoidanceSourceDistanceSquared = 0;
	int lastFfaSpawnCampAvoidancePolicyAvoid = 0;
	int lastFfaSpawnCampAvoidanceGoalDistanceSquared = 0;
	const char *lastFfaSpawnCampAvoidanceReason = "none";
	int ffaSpawnCampCombatAvoidanceEvaluations = 0;
	int ffaSpawnCampCombatAvoidanceBlocks = 0;
	int ffaSpawnCampCombatAvoidanceSourceBlocks = 0;
	int ffaSpawnCampCombatAvoidanceClears = 0;
	int ffaSpawnCampCombatAvoidanceInvalidSkips = 0;
	int lastFfaSpawnCampCombatAvoidanceClient = -1;
	int lastFfaSpawnCampCombatAvoidanceTargetClient = -1;
	int lastFfaSpawnCampCombatAvoidanceTargetEntity = -1;
	int lastFfaSpawnCampCombatAvoidanceSourceClient = -1;
	int lastFfaSpawnCampCombatAvoidanceSourceEntity = -1;
	int lastFfaSpawnCampCombatAvoidanceSourceDistanceSquared = 0;
	int lastFfaSpawnCampCombatAvoidancePolicyAvoid = 0;
	int lastFfaSpawnCampCombatAvoidanceBlocked = 0;
	const char *lastFfaSpawnCampCombatAvoidanceReason = "none";
	int ffaRoleCombatRequests = 0;
	int ffaRoleCombatPolicySelections = 0;
	int ffaRoleCombatTargetSelections = 0;
	int ffaRoleCombatAttackDecisions = 0;
	int ffaRoleCombatDecisionOverrides = 0;
	int ffaRoleCombatTargetDeferrals = 0;
	int ffaRoleCombatInvalidSkips = 0;
	int lastFfaRoleCombatClient = -1;
	int lastFfaRoleCombatMode = 0;
	int lastFfaRoleCombatRole = 0;
	int lastFfaRoleCombatLane = 0;
	int lastFfaRoleCombatPriority = 0;
	int lastFfaRoleCombatTargetClient = -1;
	int lastFfaRoleCombatTargetEntity = -1;
	int lastFfaRoleCombatTargetDistanceSquared = 0;
	int lastFfaRoleCombatTargetVisible = 0;
	int lastFfaRoleCombatTargetShootable = 0;
	const char *lastFfaRoleCombatReason = "none";
	int teamRoleRouteRequests = 0;
	int teamRoleRoutePolicySelections = 0;
	int teamRoleRouteActivations = 0;
	int teamRoleRouteRefreshes = 0;
	int teamRoleRouteOwnerDeferrals = 0;
	int teamRoleRouteRouteRequests = 0;
	int teamRoleRouteRouteDeferrals = 0;
	int teamRoleRouteExpirations = 0;
	int teamRoleRouteInvalidSkips = 0;
	int lastTeamRoleRouteClient = -1;
	int lastTeamRoleRouteMode = 0;
	int lastTeamRoleRouteRole = 0;
	int lastTeamRoleRouteLane = 0;
	int lastTeamRoleRoutePriority = 0;
	int lastTeamRoleRouteRemainingMilliseconds = 0;
	int lastTeamRoleRouteGoalDistanceSquared = 0;
	int teamRoleCombatRequests = 0;
	int teamRoleCombatPolicySelections = 0;
	int teamRoleCombatTargetSelections = 0;
	int teamRoleCombatAttackDecisions = 0;
	int teamRoleCombatDecisionOverrides = 0;
	int teamRoleCombatTargetDeferrals = 0;
	int teamRoleCombatInvalidSkips = 0;
	int lastTeamRoleCombatClient = -1;
	int lastTeamRoleCombatMode = 0;
	int lastTeamRoleCombatRole = 0;
	int lastTeamRoleCombatLane = 0;
	int lastTeamRoleCombatPriority = 0;
	int lastTeamRoleCombatTargetClient = -1;
	int lastTeamRoleCombatTargetEntity = -1;
	int lastTeamRoleCombatTargetDistanceSquared = 0;
	int lastTeamRoleCombatTargetVisible = 0;
	int lastTeamRoleCombatTargetShootable = 0;
	const char *lastTeamRoleCombatReason = "none";
	int ctfRoleRouteRequests = 0;
	int ctfRoleRoutePolicySelections = 0;
	int ctfRoleRouteActivations = 0;
	int ctfRoleRouteRefreshes = 0;
	int ctfRoleRouteOwnerDeferrals = 0;
	int ctfRoleRouteObjectiveDeferrals = 0;
	int ctfRoleRouteRouteRequests = 0;
	int ctfRoleRouteRouteDeferrals = 0;
	int ctfRoleRouteExpirations = 0;
	int ctfRoleRouteInvalidSkips = 0;
	int lastCtfRoleRouteClient = -1;
	int lastCtfRoleRouteMode = 0;
	int lastCtfRoleRouteRole = 0;
	int lastCtfRoleRouteLane = 0;
	int lastCtfRoleRoutePriority = 0;
	int lastCtfRoleRouteRemainingMilliseconds = 0;
	int lastCtfRoleRouteGoalDistanceSquared = 0;
	int ctfRoleCombatRequests = 0;
	int ctfRoleCombatPolicySelections = 0;
	int ctfRoleCombatTargetSelections = 0;
	int ctfRoleCombatAttackDecisions = 0;
	int ctfRoleCombatDecisionOverrides = 0;
	int ctfRoleCombatTargetDeferrals = 0;
	int ctfRoleCombatInvalidSkips = 0;
	int lastCtfRoleCombatClient = -1;
	int lastCtfRoleCombatMode = 0;
	int lastCtfRoleCombatRole = 0;
	int lastCtfRoleCombatLane = 0;
	int lastCtfRoleCombatPriority = 0;
	int lastCtfRoleCombatTargetClient = -1;
	int lastCtfRoleCombatTargetEntity = -1;
	int lastCtfRoleCombatTargetDistanceSquared = 0;
	int lastCtfRoleCombatTargetVisible = 0;
	int lastCtfRoleCombatTargetShootable = 0;
	const char *lastCtfRoleCombatReason = "none";
	int ctfDroppedFlagRouteRequests = 0;
	int ctfDroppedFlagRouteAssignments = 0;
	int ctfDroppedFlagRouteRouteRequests = 0;
	int ctfDroppedFlagRouteRouteCommands = 0;
	int ctfDroppedFlagRouteInvalidSkips = 0;
	int lastCtfDroppedFlagRouteClient = -1;
	int lastCtfDroppedFlagRouteRole = 0;
	int lastCtfDroppedFlagRouteLane = 0;
	int lastCtfDroppedFlagRouteType = 0;
	int lastCtfDroppedFlagRouteSource = 0;
	int lastCtfDroppedFlagRouteEntity = -1;
	int lastCtfDroppedFlagRouteItem = 0;
	int lastCtfDroppedFlagRoutePriority = 0;
	int lastCtfDroppedFlagRouteGoalDistanceSquared = 0;
	int ctfCarrierSupportRouteRequests = 0;
	int ctfCarrierSupportRouteAssignments = 0;
	int ctfCarrierSupportRouteRouteRequests = 0;
	int ctfCarrierSupportRouteRouteCommands = 0;
	int ctfCarrierSupportRouteInvalidSkips = 0;
	int lastCtfCarrierSupportRouteClient = -1;
	int lastCtfCarrierSupportRouteRole = 0;
	int lastCtfCarrierSupportRouteLane = 0;
	int lastCtfCarrierSupportRouteType = 0;
	int lastCtfCarrierSupportRouteSource = 0;
	int lastCtfCarrierSupportRouteEntity = -1;
	int lastCtfCarrierSupportRouteCarrierClient = -1;
	int lastCtfCarrierSupportRouteItem = 0;
	int lastCtfCarrierSupportRoutePriority = 0;
	int lastCtfCarrierSupportRouteGoalDistanceSquared = 0;
	int ctfBaseReturnRouteRequests = 0;
	int ctfBaseReturnRouteAssignments = 0;
	int ctfBaseReturnRouteRouteRequests = 0;
	int ctfBaseReturnRouteRouteCommands = 0;
	int ctfBaseReturnRouteInvalidSkips = 0;
	int lastCtfBaseReturnRouteClient = -1;
	int lastCtfBaseReturnRouteRole = 0;
	int lastCtfBaseReturnRouteLane = 0;
	int lastCtfBaseReturnRouteType = 0;
	int lastCtfBaseReturnRouteSource = 0;
	int lastCtfBaseReturnRouteEntity = -1;
	int lastCtfBaseReturnRouteCarrierClient = -1;
	int lastCtfBaseReturnRouteItem = 0;
	int lastCtfBaseReturnRoutePriority = 0;
	int lastCtfBaseReturnRouteGoalDistanceSquared = 0;
	int ctfObjectiveRouteRequests = 0;
	int ctfObjectiveRouteAssignments = 0;
	int ctfObjectiveRouteBaseReturnCandidates = 0;
	int ctfObjectiveRouteCarrierSupportCandidates = 0;
	int ctfObjectiveRouteDroppedFlagCandidates = 0;
	int ctfObjectiveRouteBaseReturnSelections = 0;
	int ctfObjectiveRouteCarrierSupportSelections = 0;
	int ctfObjectiveRouteDroppedFlagSelections = 0;
	int ctfObjectiveRouteCarrierSupportDeferrals = 0;
	int ctfObjectiveRouteDroppedFlagDeferrals = 0;
	int ctfObjectiveRouteRouteRequests = 0;
	int ctfObjectiveRouteRouteCommands = 0;
	int ctfObjectiveRouteInvalidSkips = 0;
	int lastCtfObjectiveRouteClient = -1;
	int lastCtfObjectiveRouteSelection = 0;
	int lastCtfObjectiveRouteRole = 0;
	int lastCtfObjectiveRouteLane = 0;
	int lastCtfObjectiveRouteType = 0;
	int lastCtfObjectiveRouteSource = 0;
	int lastCtfObjectiveRouteEntity = -1;
	int lastCtfObjectiveRouteCarrierClient = -1;
	int lastCtfObjectiveRouteItem = 0;
	int lastCtfObjectiveRoutePriority = 0;
	int lastCtfObjectiveRouteGoalDistanceSquared = 0;
	int teamFireAvoidanceEvaluations = 0;
	int teamFireAvoidanceBlocks = 0;
	int teamFireAvoidanceTargetBlocks = 0;
	int teamFireAvoidanceLineBlocks = 0;
	int teamFireAvoidanceClears = 0;
	int teamFireAvoidanceInvalidSkips = 0;
	int lastTeamFireAvoidanceClient = -1;
	int lastTeamFireAvoidanceTargetClient = -1;
	int lastTeamFireAvoidanceFriendlyLine = 0;
	int lastTeamFireAvoidanceTargetAllowed = 1;
	int lastTeamFireAvoidanceBlocked = 0;
	const char *lastTeamFireAvoidanceReason = "none";
	int behaviorArbitrationEvaluations = 0;
	int behaviorArbitrationRouteCandidates = 0;
	int behaviorArbitrationItemCandidates = 0;
	int behaviorArbitrationCombatCandidates = 0;
	int behaviorArbitrationObjectiveCandidates = 0;
	int behaviorArbitrationInteractionCandidates = 0;
	int behaviorArbitrationRecoveryCandidates = 0;
	int behaviorArbitrationRouteOwners = 0;
	int behaviorArbitrationItemOwners = 0;
	int behaviorArbitrationCombatOwners = 0;
	int behaviorArbitrationObjectiveOwners = 0;
	int behaviorArbitrationInteractionOwners = 0;
	int behaviorArbitrationRecoveryOwners = 0;
	int behaviorArbitrationIdleOwners = 0;
	int behaviorArbitrationHandoffs = 0;
	int lastBehaviorArbitrationClient = -1;
	int lastBehaviorArbitrationOwner = 0;
	int lastBehaviorArbitrationPreviousOwner = 0;
	int lastBehaviorArbitrationPriority = 0;
	const char *lastBehaviorArbitrationOwnerName = "none";
	const char *lastBehaviorArbitrationReason = "none";
	int coopLeaderRouteActivations = 0;
	int coopLeaderRouteRefreshes = 0;
	int coopLeaderRouteOwnerDeferrals = 0;
	int coopLeaderRouteTowardSources = 0;
	int coopLeaderRouteSpacingSources = 0;
	int coopLeaderRouteInvalidSkips = 0;
	int lastCoopLeaderRouteClient = -1;
	int lastCoopLeaderRouteLeaderClient = -1;
	int lastCoopLeaderRouteIntent = 0;
	int lastCoopLeaderRouteLeaderDistanceSquared = 0;
	int coopLeadAdvanceRequests = 0;
	int coopLeadAdvancePolicyLeads = 0;
	int coopLeadAdvanceActivations = 0;
	int coopLeadAdvanceRefreshes = 0;
	int coopLeadAdvanceRouteRequests = 0;
	int coopLeadAdvanceOwnerDeferrals = 0;
	int coopLeadAdvanceRouteDeferrals = 0;
	int coopLeadAdvanceExpirations = 0;
	int coopLeadAdvanceInvalidSkips = 0;
	int lastCoopLeadAdvanceClient = -1;
	int lastCoopLeadAdvanceIntent = 0;
	int lastCoopLeadAdvanceRemainingMilliseconds = 0;
	int lastCoopLeadAdvanceGoalDistanceSquared = 0;
	int coopProgressWaitRequests = 0;
	int coopProgressWaitPolicyWaits = 0;
	int coopProgressWaitCommands = 0;
	int coopProgressWaitInvalidSkips = 0;
	int lastCoopProgressWaitClient = -1;
	int lastCoopProgressWaitLeaderClient = -1;
	int lastCoopProgressWaitIntent = 0;
	int lastCoopProgressWaitLeaderDistanceSquared = 0;
	int coopAntiBlockRequests = 0;
	int coopAntiBlockPolicyClose = 0;
	int coopAntiBlockCommands = 0;
	int coopAntiBlockInvalidSkips = 0;
	int lastCoopAntiBlockClient = -1;
	int lastCoopAntiBlockLeaderClient = -1;
	int lastCoopAntiBlockIntent = 0;
	int lastCoopAntiBlockLeaderDistanceSquared = 0;
	int lastCoopAntiBlockForwardMove = 0;
	int lastCoopAntiBlockSideMove = 0;
	int coopTargetShareRequests = 0;
	int coopTargetSharePolicySupports = 0;
	int coopTargetShareSourceScans = 0;
	int coopTargetShareSourceCandidates = 0;
	int coopTargetShareAdoptions = 0;
	int coopTargetShareInvalidSkips = 0;
	int lastCoopTargetShareClient = -1;
	int lastCoopTargetShareSourceClient = -1;
	int lastCoopTargetShareTargetEntity = -1;
	int lastCoopTargetShareTargetClient = -1;
	int lastCoopTargetShareTargetDistanceSquared = 0;
	int lastCoopTargetShareIntent = 0;
	int coopDoorElevatorRequests = 0;
	int coopDoorElevatorSourceActivations = 0;
	int coopDoorElevatorSourceCommands = 0;
	int coopDoorElevatorHoldCommands = 0;
	int coopDoorElevatorInvalidSkips = 0;
	int lastCoopDoorElevatorClient = -1;
	int lastCoopDoorElevatorSourceClient = -1;
	int lastCoopDoorElevatorAction = 0;
	int lastCoopDoorElevatorKind = 0;
	int lastCoopDoorElevatorEntity = -1;
	int lastCoopDoorElevatorIntent = 0;
	int coopInteractionRetryRequests = 0;
	int coopInteractionRetryActivations = 0;
	int coopInteractionRetryCommands = 0;
	int coopInteractionRetryInvalidSkips = 0;
	int lastCoopInteractionRetryClient = -1;
	int lastCoopInteractionRetryAction = 0;
	int lastCoopInteractionRetryKind = 0;
	int lastCoopInteractionRetryEntity = -1;
	int nukeRetreatActivations = 0;
	int nukeRetreatFallbackSources = 0;
	int nukeRetreatRouteRequests = 0;
	int nukeRetreatRouteDeferrals = 0;
	int nukeRetreatExpirations = 0;
	int nukeRetreatInvalidSkips = 0;
	int lastNukeRetreatClient = -1;
	int lastNukeRetreatRemainingMilliseconds = 0;
	int lastNukeRetreatSourceX = 0;
	int lastNukeRetreatSourceY = 0;
	int lastNukeRetreatSourceZ = 0;
	int lastNukeRetreatGoalX = 0;
	int lastNukeRetreatGoalY = 0;
	int lastNukeRetreatGoalZ = 0;
	int lastNukeRetreatDistanceSquared = 0;
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

struct BotFrameObjectivePolicyResult {
	BotObjectiveMatchPolicy matchPolicy{};
	BotObjectiveCoopPolicy coopPolicy{};
	bool coopProgressWaitRequested = false;
};

enum class BotBehaviorArbitrationOwner {
	None = 0,
	Idle = 1,
	Route = 2,
	Item = 3,
	Combat = 4,
	Objective = 5,
	Interaction = 6,
	Recovery = 7,
};

struct BotBehaviorArbitrationCandidates {
	bool route = false;
	bool item = false;
	bool combat = false;
	bool objective = false;
	bool interaction = false;
	bool recovery = false;
};

enum class BotTimedRouteGoalKind {
	None = 0,
	NukeRetreat = 1,
	TeleporterEscape = 2,
	CoopLeader = 3,
	CoopLeadAdvance = 4,
	TeamRole = 5,
	CtfRole = 6,
	FfaRoam = 7,
	ThreatRetreat = 8,
};

struct BotTimedRouteGoalState {
	BotTimedRouteGoalKind kind = BotTimedRouteGoalKind::None;
	int untilMilliseconds = 0;
	Vector3 source = vec3_origin;
	Vector3 fallbackDirection = vec3_origin;
	Vector3 goal = vec3_origin;
	float distance = 0.0f;
	float minDirectionSquared = 0.0f;
	int matchMode = 0;
	int matchRole = 0;
	int matchLane = 0;
	int matchPriority = 0;
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
	BotTimedRouteGoalState timedRouteGoal{};
	int threatRetreatCooldownUntilMilliseconds = 0;
	int threatRetreatLastActivationMilliseconds = 0;
	bool threatRetreatReengageRecorded = false;
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
	int combatEnemyMemoryRetains = 0;
	int combatEnemyMemoryDecays = 0;
	int combatEnemyMemorySmokeOcclusions = 0;
	int combatEnemyMemorySmokeSeedAttempts = 0;
	int combatEnemyMemorySmokeSeeded = 0;
	int combatEnemyMemorySmokeSeedNoPeer = 0;
	int combatEnemyMemorySmokeSeedInvalidPeer = 0;
	int combatEnemyMemorySmokeSeedNoBlackboard = 0;
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
	int lastCombatEnemyRetainedFromMemory = 0;
	int lastCombatEnemyMemoryAgeMilliseconds = 0;
	int lastCombatEnemyMemoryWindowMilliseconds = 0;
	int lastCombatEnemyMemoryDecayEntity = -1;
	int lastCombatEnemyMemoryDecayClient = -1;
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
	bool weaponScoringPrepared = false;
	bool aimFirePolicyPrepared = false;
	int aimFirePolicyStartTimeMilliseconds = 0;
	bool ammoPressurePrepared = false;
	bool survivalInventoryPrepared = false;
	bool survivalRoutePrepared = false;
	bool threatRetreatPrepared = false;
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
	bool coopTargetSharePrepared = false;
	bool coopTargetShareSeeded = false;
	bool teamFireAvoidancePrepared = false;
	bool teamRoleCombatPrepared = false;
	bool ffaRoleCombatPrepared = false;
	bool ffaSpawnCampAvoidancePrepared = false;
	bool ctfRoleCombatPrepared = false;
	bool ctfDroppedFlagRoutePrepared = false;
	bool ctfCarrierSupportRoutePrepared = false;
	bool ctfBaseReturnRoutePrepared = false;
	bool ctfObjectiveRoutePrepared = false;
	bool ctfObjectiveTransitionsPrepared = false;
	bool targetMemorySeeded = false;
};

enum class BotCtfObjectiveRouteSelection {
	None = 0,
	BaseReturn = 1,
	CarrierSupport = 2,
	DroppedFlag = 3,
};

struct BotCtfObjectiveRouteCandidate {
	bool valid = false;
	BotCtfObjectiveRouteSelection selection = BotCtfObjectiveRouteSelection::None;
	BotObjectiveAssignment assignment{};
	BotObjectiveRouteGoal goal{};
};

struct BotCoopTargetShareSmokeTarget {
	int entityNumber = -1;
	int spawnCount = 0;
};

struct BotCtfDroppedFlagSmokeTarget {
	int entityNumber = -1;
	int spawnCount = 0;
	int item = IT_NULL;
};

struct BotAmmoPressureSmokeTarget {
	int entityNumber = -1;
	int spawnCount = 0;
	int item = IT_NULL;
};

struct BotSurvivalRouteSmokeTarget {
	int entityNumber = -1;
	int spawnCount = 0;
	int item = IT_NULL;
};

struct BotChatInitialPolicyStatus {
	int selections = 0;
	int knownPersonalities = 0;
	int unknownPersonalities = 0;
	int quiet = 0;
	int direct = 0;
	int taunting = 0;
	int helpful = 0;
	int steady = 0;
	unsigned int variantMask = 0;
	int lastClient = -1;
	int lastPersonality = 0;
	int lastPhrase = 0;
	int lastVariant = -1;
};

struct BotChatReplyPolicyStatus {
	int enabled = 0;
	int events = 0;
	int selections = 0;
	int knownPersonalities = 0;
	int unknownPersonalities = 0;
	int teamReady = 0;
	int routeReady = 0;
	int itemTaken = 0;
	int objectiveChanged = 0;
	int enemySighted = 0;
	int lowHealth = 0;
	int submitted = 0;
	int rateLimited = 0;
	int duplicateSuppressed = 0;
	int failures = 0;
	unsigned int variantMask = 0;
	int lastClient = -1;
	int lastPersonality = 0;
	int lastPhrase = 0;
	int lastVariant = -1;
	int lastEvent = 0;
	int liveEnabled = 0;
	int liveEvents = 0;
	int liveSpawn = 0;
	int liveRouteReady = 0;
	int liveItemTaken = 0;
	int liveObjectiveChanged = 0;
	int liveEnemySighted = 0;
	int liveLowHealth = 0;
	int liveSubmitted = 0;
	int liveRateLimited = 0;
	int liveDuplicateSuppressed = 0;
	int liveFailures = 0;
	int lastLiveEvent = 0;
	int lastDuplicateClient = -1;
	int lastDuplicateEvent = 0;
	int lastDuplicatePhrase = 0;
	int lastDuplicateElapsedMilliseconds = -1;
};

BotFrameCommandStatus botFrameCommandStatus;
std::array<BotBrainBlackboardSlot, MAX_CLIENTS> botBrainBlackboardSlots{};
BotBrainBlackboardStatus botBrainBlackboardStatus;
std::array<BotCommandSmokeProofSlot, MAX_CLIENTS> botCommandSmokeProofSlots{};
std::array<int, MAX_CLIENTS> botChatPolicyDispatchSpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyReplySpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyRouteReplySpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLiveRouteReplySpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLiveItemTakenSpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLiveObjectiveChangedSpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLiveEnemySightedSpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLiveLowHealthSpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLastReplyEvents = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLastReplyPhrases = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botChatPolicyLastReplyTimes = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
std::array<int, MAX_CLIENTS> botBehaviorArbitrationLastOwners{};
std::array<int, MAX_CLIENTS> botBehaviorArbitrationLastSpawnCounts = [] {
	std::array<int, MAX_CLIENTS> values{};
	values.fill(-1);
	return values;
}();
BotChatInitialPolicyStatus botChatInitialPolicyStatus{};
BotChatReplyPolicyStatus botChatReplyPolicyStatus{};
std::array<bool, MAX_CLIENTS> botCoopInteractionRetryOwners{};
std::array<bool, MAX_CLIENTS> botCoopDoorElevatorOwners{};
BotCoopTargetShareSmokeTarget botCoopTargetShareSmokeTarget{};
std::array<BotCtfDroppedFlagSmokeTarget, 2> botCtfDroppedFlagSmokeTargets{};
BotAmmoPressureSmokeTarget botAmmoPressureSmokeTarget{};
BotSurvivalRouteSmokeTarget botSurvivalRouteSmokeTarget{};

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
constexpr float BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE = 1024.0f;
constexpr float BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED = 64.0f;
constexpr float BOT_COMMAND_COOP_LEADER_ROUTE_DISTANCE = 768.0f;
constexpr float BOT_COMMAND_COOP_LEADER_SUPPORT_DISTANCE = 384.0f;
constexpr float BOT_COMMAND_COOP_LEAD_ADVANCE_DISTANCE = 768.0f;
constexpr float BOT_COMMAND_FFA_ROAM_ROUTE_DISTANCE = 896.0f;
constexpr float BOT_COMMAND_FFA_SPAWN_CAMP_AVOIDANCE_DISTANCE = 384.0f;
constexpr float BOT_COMMAND_FFA_SPAWN_CAMP_AVOIDANCE_DISTANCE_SQUARED =
	BOT_COMMAND_FFA_SPAWN_CAMP_AVOIDANCE_DISTANCE *
	BOT_COMMAND_FFA_SPAWN_CAMP_AVOIDANCE_DISTANCE;
constexpr float BOT_COMMAND_THREAT_RETREAT_DISTANCE = 768.0f;
constexpr float BOT_COMMAND_TEAM_ROLE_ROUTE_DISTANCE = 896.0f;
constexpr float BOT_COMMAND_COOP_ANTI_BLOCK_DISTANCE = 192.0f;
constexpr float BOT_COMMAND_COOP_ANTI_BLOCK_DISTANCE_SQUARED =
	BOT_COMMAND_COOP_ANTI_BLOCK_DISTANCE * BOT_COMMAND_COOP_ANTI_BLOCK_DISTANCE;
constexpr float BOT_COMMAND_COOP_ANTI_BLOCK_FORWARD_MOVE = -90.0f;
constexpr float BOT_COMMAND_COOP_ANTI_BLOCK_SIDE_MOVE = 130.0f;
constexpr float BOT_COMMAND_COOP_TARGET_SHARE_SOURCE_DISTANCE = 1536.0f;
constexpr float BOT_COMMAND_COOP_TARGET_SHARE_SOURCE_DISTANCE_SQUARED =
	BOT_COMMAND_COOP_TARGET_SHARE_SOURCE_DISTANCE *
	BOT_COMMAND_COOP_TARGET_SHARE_SOURCE_DISTANCE;
constexpr float BOT_COMMAND_TEAM_FIRE_LINE_RADIUS = 56.0f;
constexpr float BOT_COMMAND_TEAM_FIRE_LINE_RADIUS_SQUARED =
	BOT_COMMAND_TEAM_FIRE_LINE_RADIUS * BOT_COMMAND_TEAM_FIRE_LINE_RADIUS;
constexpr int BOT_COMMAND_FFA_ROLE_COMBAT_PRIORITY_BONUS = 30;
constexpr int BOT_COMMAND_TEAM_ROLE_COMBAT_PRIORITY_BONUS = 35;
constexpr int BOT_COMMAND_CTF_ROLE_COMBAT_PRIORITY_BONUS = 40;
constexpr int BOT_COMMAND_NUKE_RETREAT_MILLISECONDS = 6000;
constexpr int BOT_COMMAND_TELEPORTER_ESCAPE_MILLISECONDS = 3500;
constexpr int BOT_COMMAND_TELEPORTER_DAMAGE_SOURCE_MILLISECONDS = 5000;
constexpr int BOT_COMMAND_THREAT_RETREAT_MILLISECONDS = 700;
constexpr int BOT_COMMAND_THREAT_RETREAT_COOLDOWN_MILLISECONDS = 2200;
constexpr int BOT_COMMAND_THREAT_RETREAT_LOW_HEALTH = 35;
constexpr int BOT_COMMAND_COOP_LEADER_ROUTE_MILLISECONDS = 2500;
constexpr int BOT_COMMAND_COOP_LEAD_ADVANCE_MILLISECONDS = 2500;
constexpr int BOT_COMMAND_FFA_ROAM_ROUTE_MILLISECONDS = 2500;
constexpr int BOT_COMMAND_TEAM_ROLE_ROUTE_MILLISECONDS = 2500;
constexpr int BOT_COMMAND_CTF_CARRIER_SUPPORT_SMOKE_WARMUP_FRAMES = 32;

constexpr int BOT_COMMAND_TRAVEL_WALK = 2;
constexpr int BOT_COMMAND_TRAVEL_CROUCH = 3;
constexpr int BOT_COMMAND_TRAVEL_BARRIER_JUMP = 4;
constexpr int BOT_COMMAND_TRAVEL_JUMP = 5;
constexpr int BOT_COMMAND_TRAVEL_LADDER = 6;
constexpr int BOT_COMMAND_TRAVEL_WALK_OFF_LEDGE = 7;
constexpr int BOT_COMMAND_TRAVEL_SWIM = 8;
constexpr int BOT_COMMAND_TRAVEL_WATER_JUMP = 9;
constexpr int BOT_COMMAND_TRAVEL_TELEPORT = 10;
constexpr int BOT_COMMAND_TRAVEL_ELEVATOR = 11;
constexpr int BOT_COMMAND_TRAVEL_ROCKET_JUMP = 12;
constexpr int BOT_PERCEPTION_SCAN_INTERVAL_FRAMES = 4;
constexpr int BOT_PERCEPTION_MEMORY_MILLISECONDS = 5000;
constexpr int BOT_PERCEPTION_TARGET_MEMORY_SMOKE_MILLISECONDS = 1000;
constexpr int BOT_PERCEPTION_TARGET_MEMORY_SMOKE_OCCLUDE_MILLISECONDS = 200;
constexpr int BOT_PERCEPTION_DAMAGE_SOURCE_MAX_DIST_SQUARED = 2048 * 2048;
constexpr int BOT_COMMAND_COOP_TARGET_SHARE_SMOKE_HEALTH = 60;
constexpr int BOT_COMMAND_AIM_DEFAULT_SKILL = 3;
constexpr int BOT_COMMAND_AIM_FIELD_OF_VIEW_DEGREES = 110;
constexpr int BOT_COMMAND_AIM_SETTLED_DEGREES = 3;
constexpr int BOT_COMMAND_AIM_BURST_RESET_MILLISECONDS = 480;

byte Bot_CommandMsec() {
	const int frameTimeMs = gi.frameTimeMs > 0 ? static_cast<int>(gi.frameTimeMs) : 1;
	return static_cast<byte>(std::clamp(frameTimeMs, 1, 255));
}

int Bot_CommandControlledInactiveRecoveryMode() {
	static cvar_t *controlledRecovery = nullptr;
	if (controlledRecovery == nullptr && gi.cvar != nullptr) {
		controlledRecovery =
			gi.cvar("sg_bot_controlled_inactive_recovery", "0", CVAR_NOFLAGS);
	}
	return controlledRecovery != nullptr ? controlledRecovery->integer : 0;
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

bool Bot_CommandBehaviorPolicyEnabled() {
	static cvar_t *behaviorEnable = nullptr;
	if (behaviorEnable == nullptr && gi.cvar != nullptr) {
		behaviorEnable = gi.cvar("sg_bot_behavior_enable", "0", CVAR_NOFLAGS);
	}
	return behaviorEnable != nullptr && behaviorEnable->integer > 0;
}

bool Bot_CommandCoopLiveLoopEnabled() {
	static cvar_t *liveLoop = nullptr;
	if (liveLoop == nullptr && gi.cvar != nullptr) {
		liveLoop = gi.cvar("sg_bot_coop_live_loop", "0", CVAR_NOFLAGS);
	}
	return liveLoop != nullptr && liveLoop->integer > 0;
}

bool Bot_CommandCoopShareLoopEnabled() {
	static cvar_t *shareLoop = nullptr;
	if (shareLoop == nullptr && gi.cvar != nullptr) {
		shareLoop = gi.cvar("sg_bot_coop_share_loop", "0", CVAR_NOFLAGS);
	}
	return shareLoop != nullptr && shareLoop->integer > 0;
}

bool Bot_CommandCoopProgressWaitCvarEnabled() {
	static cvar_t *progressWait = nullptr;
	if (progressWait == nullptr && gi.cvar != nullptr) {
		progressWait = gi.cvar("sg_bot_coop_progress_wait", "0", CVAR_NOFLAGS);
	}
	return progressWait != nullptr && progressWait->integer > 0;
}

bool Bot_CommandCoopProgressWaitRequested() {
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCoopLiveLoopEnabled() ||
		Bot_CommandCoopProgressWaitCvarEnabled();
}

bool Bot_CommandCoopLeadAdvanceRequested() {
	static cvar_t *leadAdvance = nullptr;
	if (leadAdvance == nullptr && gi.cvar != nullptr) {
		leadAdvance = gi.cvar("sg_bot_coop_lead_advance", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(leadAdvance != nullptr && leadAdvance->integer > 0);
}

bool Bot_CommandCoopResourceShareRequested() {
	static cvar_t *resourceShare = nullptr;
	if (resourceShare == nullptr && gi.cvar != nullptr) {
		resourceShare = gi.cvar("sg_bot_coop_resource_share", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCoopShareLoopEnabled() ||
		(resourceShare != nullptr && resourceShare->integer > 0);
}

bool Bot_CommandCoopTargetShareEnabled() {
	static cvar_t *targetShare = nullptr;
	if (targetShare == nullptr && gi.cvar != nullptr) {
		targetShare = gi.cvar("sg_bot_coop_target_share", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCoopShareLoopEnabled() ||
		(targetShare != nullptr && targetShare->integer > 0);
}

bool Bot_CommandCoopAntiBlockingEnabled() {
	static cvar_t *antiBlocking = nullptr;
	if (antiBlocking == nullptr && gi.cvar != nullptr) {
		antiBlocking = gi.cvar("sg_bot_coop_anti_blocking", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCoopLiveLoopEnabled() ||
		(antiBlocking != nullptr && antiBlocking->integer > 0);
}

bool Bot_CommandCoopInteractionRetryEnabled() {
	static cvar_t *interactionRetry = nullptr;
	if (interactionRetry == nullptr && gi.cvar != nullptr) {
		interactionRetry = gi.cvar("sg_bot_coop_interaction_retry", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCoopLiveLoopEnabled() ||
		(interactionRetry != nullptr && interactionRetry->integer > 0);
}

bool Bot_CommandCoopDoorElevatorEnabled() {
	static cvar_t *doorElevator = nullptr;
	if (doorElevator == nullptr && gi.cvar != nullptr) {
		doorElevator = gi.cvar("sg_bot_coop_door_elevator", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCoopLiveLoopEnabled() ||
		(doorElevator != nullptr && doorElevator->integer > 0);
}

bool Bot_CommandTeamRoleRouteEnabled() {
	static cvar_t *teamRoleRoute = nullptr;
	if (teamRoleRoute == nullptr && gi.cvar != nullptr) {
		teamRoleRoute = gi.cvar("sg_bot_team_role_route", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(teamRoleRoute != nullptr && teamRoleRoute->integer > 0);
}

bool Bot_CommandDuelLivePacingEnabled() {
	static cvar_t *duelLivePacing = nullptr;
	if (duelLivePacing == nullptr && gi.cvar != nullptr) {
		duelLivePacing = gi.cvar("sg_bot_duel_live_pacing", "0", CVAR_NOFLAGS);
	}
	return duelLivePacing != nullptr && duelLivePacing->integer > 0;
}

bool Bot_CommandFfaRoamRouteEnabled() {
	static cvar_t *ffaRoamRoute = nullptr;
	if (ffaRoamRoute == nullptr && gi.cvar != nullptr) {
		ffaRoamRoute = gi.cvar("sg_bot_ffa_roam_route", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandDuelLivePacingEnabled() ||
		(ffaRoamRoute != nullptr && ffaRoamRoute->integer > 0);
}

bool Bot_CommandFfaSpawnCampAvoidanceEnabled() {
	static cvar_t *spawnCampAvoidance = nullptr;
	if (spawnCampAvoidance == nullptr && gi.cvar != nullptr) {
		spawnCampAvoidance =
			gi.cvar("sg_bot_ffa_spawn_camp_avoidance", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandDuelLivePacingEnabled() ||
		(spawnCampAvoidance != nullptr && spawnCampAvoidance->integer > 0);
}

bool Bot_CommandFfaSpawnCampCombatAvoidanceEnabled() {
	static cvar_t *combatAvoidance = nullptr;
	if (combatAvoidance == nullptr && gi.cvar != nullptr) {
		combatAvoidance =
			gi.cvar("sg_bot_ffa_spawn_camp_combat_avoidance", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandDuelLivePacingEnabled() ||
		(combatAvoidance != nullptr && combatAvoidance->integer > 0);
}

bool Bot_CommandFfaItemRolesEnabled() {
	static cvar_t *ffaItemRoles = nullptr;
	if (ffaItemRoles == nullptr && gi.cvar != nullptr) {
		ffaItemRoles = gi.cvar("sg_bot_ffa_item_roles", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandDuelLivePacingEnabled() ||
		(ffaItemRoles != nullptr && ffaItemRoles->integer > 0);
}

bool Bot_CommandThreatRetreatEnabled() {
	static cvar_t *threatRetreat = nullptr;
	if (threatRetreat == nullptr && gi.cvar != nullptr) {
		threatRetreat = gi.cvar("sg_bot_threat_retreat", "0", CVAR_NOFLAGS);
	}
	return threatRetreat != nullptr && threatRetreat->integer > 0;
}

bool Bot_CommandFfaRoleCombatEnabled() {
	static cvar_t *ffaRoleCombat = nullptr;
	if (ffaRoleCombat == nullptr && gi.cvar != nullptr) {
		ffaRoleCombat = gi.cvar("sg_bot_ffa_role_combat", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandDuelLivePacingEnabled() ||
		(ffaRoleCombat != nullptr && ffaRoleCombat->integer > 0);
}

bool Bot_CommandFfaLivePacingProofEnabled() {
	return Bot_CommandFfaRoamRouteEnabled() &&
		Bot_CommandFfaSpawnCampAvoidanceEnabled() &&
		Bot_CommandFfaSpawnCampCombatAvoidanceEnabled() &&
		Bot_CommandFfaItemRolesEnabled() &&
		Bot_CommandFfaRoleCombatEnabled();
}

bool Bot_CommandFfaStylePacingPolicyEnabled(
	const BotObjectiveMatchPolicy &policy) {
	return policy.mode == BotObjectiveMatchMode::FreeForAll ||
		(policy.mode == BotObjectiveMatchMode::Duel &&
		 Bot_CommandDuelLivePacingEnabled());
}

bool Bot_CommandTeamRoleCombatEnabled() {
	static cvar_t *teamRoleCombat = nullptr;
	if (teamRoleCombat == nullptr && gi.cvar != nullptr) {
		teamRoleCombat = gi.cvar("sg_bot_team_role_combat", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(teamRoleCombat != nullptr && teamRoleCombat->integer > 0);
}

bool Bot_CommandMatchItemPolicyEnabled() {
	static cvar_t *matchItemPolicy = nullptr;
	if (matchItemPolicy == nullptr && gi.cvar != nullptr) {
		matchItemPolicy = gi.cvar("sg_bot_match_item_policy", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(matchItemPolicy != nullptr && matchItemPolicy->integer > 0);
}

bool Bot_CommandProfileItemPolicySmoke() {
	static cvar_t *profileItemPolicySmoke = nullptr;
	if (profileItemPolicySmoke == nullptr && gi.cvar != nullptr) {
		profileItemPolicySmoke =
			gi.cvar("sg_bot_profile_item_policy_smoke", "0", CVAR_NOFLAGS);
	}
	return profileItemPolicySmoke != nullptr &&
		profileItemPolicySmoke->integer > 0;
}

bool Bot_CommandProfileMovementPolicySmoke() {
	static cvar_t *profileMovementPolicySmoke = nullptr;
	if (profileMovementPolicySmoke == nullptr && gi.cvar != nullptr) {
		profileMovementPolicySmoke =
			gi.cvar("sg_bot_profile_movement_policy_smoke", "0", CVAR_NOFLAGS);
	}
	return profileMovementPolicySmoke != nullptr &&
		profileMovementPolicySmoke->integer > 0;
}

bool Bot_CommandCtfRoleRouteEnabled() {
	static cvar_t *ctfRoleRoute = nullptr;
	if (ctfRoleRoute == nullptr && gi.cvar != nullptr) {
		ctfRoleRoute = gi.cvar("sg_bot_ctf_role_route", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(ctfRoleRoute != nullptr && ctfRoleRoute->integer > 0);
}

bool Bot_CommandCtfRoleCombatEnabled() {
	static cvar_t *ctfRoleCombat = nullptr;
	if (ctfRoleCombat == nullptr && gi.cvar != nullptr) {
		ctfRoleCombat = gi.cvar("sg_bot_ctf_role_combat", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(ctfRoleCombat != nullptr && ctfRoleCombat->integer > 0);
}

bool Bot_CommandCtfDroppedFlagRouteEnabled() {
	static cvar_t *ctfDroppedFlagRoute = nullptr;
	if (ctfDroppedFlagRoute == nullptr && gi.cvar != nullptr) {
		ctfDroppedFlagRoute =
			gi.cvar("sg_bot_ctf_dropped_flag_route", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(ctfDroppedFlagRoute != nullptr && ctfDroppedFlagRoute->integer > 0);
}

bool Bot_CommandCtfCarrierSupportRouteEnabled() {
	static cvar_t *ctfCarrierSupportRoute = nullptr;
	if (ctfCarrierSupportRoute == nullptr && gi.cvar != nullptr) {
		ctfCarrierSupportRoute =
			gi.cvar("sg_bot_ctf_carrier_support_route", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(ctfCarrierSupportRoute != nullptr && ctfCarrierSupportRoute->integer > 0);
}

bool Bot_CommandCtfBaseReturnRouteEnabled() {
	static cvar_t *ctfBaseReturnRoute = nullptr;
	if (ctfBaseReturnRoute == nullptr && gi.cvar != nullptr) {
		ctfBaseReturnRoute =
			gi.cvar("sg_bot_ctf_base_return_route", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(ctfBaseReturnRoute != nullptr && ctfBaseReturnRoute->integer > 0);
}

bool Bot_CommandCtfObjectiveTransitionsEnabled() {
	static cvar_t *ctfObjectiveTransitions = nullptr;
	if (ctfObjectiveTransitions == nullptr && gi.cvar != nullptr) {
		ctfObjectiveTransitions =
			gi.cvar("sg_bot_ctf_objective_transitions", "0", CVAR_NOFLAGS);
	}
	return ctfObjectiveTransitions != nullptr &&
		ctfObjectiveTransitions->integer > 0;
}

bool Bot_CommandCtfObjectiveRouteEnabled() {
	static cvar_t *ctfObjectiveRoute = nullptr;
	if (ctfObjectiveRoute == nullptr && gi.cvar != nullptr) {
		ctfObjectiveRoute =
			gi.cvar("sg_bot_ctf_objective_route", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCtfObjectiveTransitionsEnabled() ||
		(ctfObjectiveRoute != nullptr && ctfObjectiveRoute->integer > 0);
}

bool Bot_CommandTeamFireAvoidanceEnabled() {
	static cvar_t *teamFireAvoidance = nullptr;
	if (teamFireAvoidance == nullptr && gi.cvar != nullptr) {
		teamFireAvoidance = gi.cvar("sg_bot_team_fire_avoidance", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandBehaviorPolicyEnabled() ||
		(teamFireAvoidance != nullptr && teamFireAvoidance->integer > 0);
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
	const char *value = Bot_CommandSmokeCombatValue();
	if (Bot_CommandSmokeValueDisabled(value)) {
		return false;
	}
	if (Q_strcasecmp(value, "engage_enemy") == 0) {
		return true;
	}
	if (Q_strcasecmp(value, "switch_weapons") == 0 ||
		Q_strcasecmp(value, "weapon_scoring") == 0 ||
		Q_strcasecmp(value, "aim_fire_policy") == 0) {
		return false;
	}
	return Bot_CommandSmokeCombat();
}

bool Bot_CommandSmokeWeaponSwitch() {
	static cvar_t *weaponSwitch = nullptr;
	if (weaponSwitch == nullptr && gi.cvar != nullptr) {
		weaponSwitch = gi.cvar("sg_bot_frame_command_smoke_weapon_switch", "0", CVAR_NOFLAGS);
	}
	return Bot_CommandSmokeCombatModeIs("switch_weapons") ||
		(weaponSwitch != nullptr && weaponSwitch->integer > 0);
}

bool Bot_CommandSmokeWeaponScoring() {
	return Bot_CommandSmokeCombatModeIs("weapon_scoring");
}

bool Bot_CommandSmokeAimFirePolicy() {
	return Bot_CommandSmokeCombatModeIs("aim_fire_policy");
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

bool Bot_CommandSmokeAmmoPressurePickup() {
	const char *value = Bot_CommandSmokeItemFocusValue();
	return !Bot_CommandSmokeValueDisabled(value) &&
		Q_strcasecmp(value, "ammo") == 0;
}

bool Bot_CommandSmokeSurvivalInventoryUse() {
	static cvar_t *survivalInventory = nullptr;
	if (survivalInventory == nullptr && gi.cvar != nullptr) {
		survivalInventory =
			gi.cvar("sg_bot_frame_command_smoke_survival_inventory", "0", CVAR_NOFLAGS);
	}
	return survivalInventory != nullptr && survivalInventory->integer > 0;
}

const char *Bot_CommandSmokeSurvivalRouteValue() {
	static cvar_t *survivalRoute = nullptr;
	if (survivalRoute == nullptr && gi.cvar != nullptr) {
		survivalRoute =
			gi.cvar("sg_bot_frame_command_smoke_survival_route", "0", CVAR_NOFLAGS);
	}
	if (survivalRoute == nullptr) {
		return "0";
	}
	return survivalRoute->string != nullptr ? survivalRoute->string : "0";
}

bool Bot_CommandSmokeSurvivalRoute() {
	return !Bot_CommandSmokeValueDisabled(Bot_CommandSmokeSurvivalRouteValue());
}

bool Bot_CommandSmokeSurvivalArmorRoute() {
	const char *value = Bot_CommandSmokeSurvivalRouteValue();
	return !Bot_CommandSmokeValueDisabled(value) &&
		Q_strcasecmp(value, "armor") == 0;
}

bool Bot_CommandSmokeCombatSurvivalRoute() {
	const char *value = Bot_CommandSmokeSurvivalRouteValue();
	return !Bot_CommandSmokeValueDisabled(value) &&
		(Q_strcasecmp(value, "combat_health") == 0 ||
		 Q_strcasecmp(value, "combat-health") == 0 ||
		 Q_strcasecmp(value, "combat") == 0);
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

bool Bot_CommandTargetMemorySmokeEnabled() {
	static cvar_t *targetMemory = nullptr;
	if (targetMemory == nullptr && gi.cvar != nullptr) {
		targetMemory =
			gi.cvar("sg_bot_frame_command_smoke_target_memory", "0", CVAR_NOFLAGS);
	}
	return targetMemory != nullptr && targetMemory->integer > 0;
}

int Bot_CommandSmokeScenarioMode() {
	if (Bot_CommandThreatRetreatEnabled()) {
		return 72;
	}
	if (Bot_CommandSmokeCombatSurvivalRoute()) {
		return 71;
	}
	if (Bot_CommandSmokeSurvivalArmorRoute()) {
		return 70;
	}
	if (Bot_CommandSmokeSurvivalRoute()) {
		return 69;
	}
	if (Bot_CommandSmokeSurvivalInventoryUse()) {
		return 68;
	}
	if (Bot_CommandSmokeAimFirePolicy()) {
		return 66;
	}
	if (Bot_CommandSmokeAmmoPressurePickup()) {
		return 67;
	}
	if (Bot_CommandSmokeWeaponScoring()) {
		return 65;
	}
	if (Bot_CommandTargetMemorySmokeEnabled()) {
		return 64;
	}
	if (Bot_CommandBehaviorPolicyEnabled()) {
		return 52;
	}
	if (Bot_CommandProfileItemPolicySmoke()) {
		return 55;
	}
	if (Bot_CommandProfileMovementPolicySmoke()) {
		return 56;
	}
	if (Bot_CommandDuelLivePacingEnabled()) {
		return 75;
	}
	if (Bot_CommandFfaLivePacingProofEnabled()) {
		return 74;
	}
	if (Bot_CommandFfaSpawnCampCombatAvoidanceEnabled()) {
		return 49;
	}
	if (Bot_CommandFfaSpawnCampAvoidanceEnabled()) {
		return 45;
	}
	if (Bot_CommandFfaRoleCombatEnabled()) {
		return 48;
	}
	if (Bot_CommandFfaRoamRouteEnabled()) {
		return 42;
	}
	if (Bot_CommandCtfObjectiveTransitionsEnabled()) {
		return 76;
	}
	if (Bot_CommandCtfObjectiveRouteEnabled()) {
		return 40;
	}
	if (Bot_CommandCtfBaseReturnRouteEnabled()) {
		return 39;
	}
	if (Bot_CommandCtfCarrierSupportRouteEnabled()) {
		return 38;
	}
	if (Bot_CommandCtfDroppedFlagRouteEnabled()) {
		return 37;
	}
	if (Bot_CommandCtfRoleCombatEnabled()) {
		return 36;
	}
	if (Bot_CommandCtfRoleRouteEnabled()) {
		return 35;
	}
	if (Bot_CommandTeamRoleCombatEnabled() && Bot_CommandTeamFireAvoidanceEnabled()) {
		return 44;
	}
	if (Bot_CommandTeamRoleCombatEnabled()) {
		return 43;
	}
	if (Bot_CommandTeamFireAvoidanceEnabled()) {
		return 34;
	}
	if (Bot_CommandTeamRoleRouteEnabled()) {
		return 32;
	}
	if (Bot_CommandCoopShareLoopEnabled()) {
		return 78;
	}
	if (Bot_CommandCoopLiveLoopEnabled()) {
		return 77;
	}
	if (Bot_CommandCoopDoorElevatorEnabled()) {
		return 31;
	}
	if (Bot_CommandCoopTargetShareEnabled()) {
		return 30;
	}
	if (Bot_CommandCoopAntiBlockingEnabled()) {
		return 29;
	}
	if (Bot_CommandCoopResourceShareRequested()) {
		return 28;
	}
	if (Bot_CommandCoopLeadAdvanceRequested()) {
		return 27;
	}
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

bool Bot_CommandSmokeForcesImmediateCombatFire() {
	if (Bot_CommandThreatRetreatEnabled()) {
		return true;
	}

	if (!Bot_CommandSmokeEngageEnemy()) {
		return false;
	}

	const int mode = Bot_CommandSmokeScenarioMode();
	return mode == 20 || mode == 34;
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

bool Bot_CommandCoopProgressWaitRequestedFor(const gentity_t *bot) {
	if (Bot_CommandBehaviorPolicyEnabled() ||
		Bot_CommandCoopProgressWaitCvarEnabled()) {
		return true;
	}
	if (!Bot_CommandCoopLiveLoopEnabled()) {
		return false;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	return clientIndex >= 0 && (clientIndex % 2) == 1;
}

int BotChatPolicy_InitialPersonalityId(const char *personality) {
	if (!personality || !personality[0]) {
		return 0;
	}
	if (Q_strcasecmp(personality, "quiet") == 0) {
		return 1;
	}
	if (Q_strcasecmp(personality, "direct") == 0) {
		return 2;
	}
	if (Q_strcasecmp(personality, "taunting") == 0) {
		return 3;
	}
	if (Q_strcasecmp(personality, "helpful") == 0) {
		return 4;
	}
	if (Q_strcasecmp(personality, "steady") == 0) {
		return 5;
	}
	return 0;
}

constexpr int BOT_CHAT_POLICY_INITIAL_PHRASE_VARIANTS = 4;
constexpr int BOT_CHAT_POLICY_REPLY_PHRASE_VARIANTS = 4;
constexpr int BOT_CHAT_POLICY_DUPLICATE_WINDOW_MS = 5000;
constexpr int BOT_CHAT_POLICY_LOW_HEALTH_PERCENT = 45;

int BotChatPolicy_PhraseVariant(int phrase, int variants) {
	if (variants <= 0) {
		return 0;
	}

	int variant = phrase % variants;
	if (variant < 0) {
		variant += variants;
	}
	return variant;
}

int BotChatPolicy_PhraseIdVariant(int phrase, int variants) {
	int variant = phrase % 10;
	if (variant < 0) {
		variant += 10;
	}
	return BotChatPolicy_PhraseVariant(variant, variants);
}

int BotChatPolicy_CountBits(unsigned int mask) {
	int count = 0;
	while (mask != 0) {
		count += static_cast<int>(mask & 1U);
		mask >>= 1U;
	}
	return count;
}

int BotChatPolicy_CurrentTimeMilliseconds() {
	return static_cast<int>(level.time.milliseconds());
}

void BotChatPolicy_RecordRecentReplyEvent(int clientIndex, int event, int phrase) {
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botChatPolicyLastReplyEvents.size())) {
		return;
	}

	botChatPolicyLastReplyEvents[clientIndex] = event;
	botChatPolicyLastReplyPhrases[clientIndex] = phrase;
	botChatPolicyLastReplyTimes[clientIndex] =
		BotChatPolicy_CurrentTimeMilliseconds();
}

bool BotChatPolicy_SuppressDuplicateReplyEvent(
	int clientIndex, int event, int phrase, bool liveEvent) {
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botChatPolicyLastReplyEvents.size())) {
		return false;
	}

	const int lastTime = botChatPolicyLastReplyTimes[clientIndex];
	const int now = BotChatPolicy_CurrentTimeMilliseconds();
	const int elapsed = lastTime >= 0 ? now - lastTime : -1;
	if (botChatPolicyLastReplyEvents[clientIndex] != event ||
		lastTime < 0 ||
		elapsed < 0 ||
		elapsed >= BOT_CHAT_POLICY_DUPLICATE_WINDOW_MS) {
		return false;
	}

	botChatReplyPolicyStatus.duplicateSuppressed++;
	if (liveEvent) {
		botChatReplyPolicyStatus.liveDuplicateSuppressed++;
	}
	botChatReplyPolicyStatus.lastDuplicateClient = clientIndex;
	botChatReplyPolicyStatus.lastDuplicateEvent = event;
	botChatReplyPolicyStatus.lastDuplicatePhrase = phrase;
	botChatReplyPolicyStatus.lastDuplicateElapsedMilliseconds = elapsed;
	return true;
}

int BotChatPolicy_InitialPhraseId(int personality, int clientIndex) {
	const int phraseSeed =
		botChatInitialPolicyStatus.selections >= 0 ?
			botChatInitialPolicyStatus.selections :
			(clientIndex >= 0 ? clientIndex : 0);
	const int variant =
		BotChatPolicy_PhraseVariant(phraseSeed, BOT_CHAT_POLICY_INITIAL_PHRASE_VARIANTS);
	return personality > 0 ? personality * 10 + variant : variant;
}

const char *BotChatPolicy_InitialPhrase(int personality, int phrase) {
	const int variant =
		BotChatPolicy_PhraseIdVariant(phrase, BOT_CHAT_POLICY_INITIAL_PHRASE_VARIANTS);
	switch (personality) {
	case 1: {
		static constexpr const char *phrases[] = {
			"watching the route",
			"holding quietly",
			"eyes open",
			"moving silent",
		};
		return phrases[variant];
	}
	case 2: {
		static constexpr const char *phrases[] = {
			"ready to engage",
			"pushing now",
			"taking point",
			"moving with purpose",
		};
		return phrases[variant];
	}
	case 3: {
		static constexpr const char *phrases[] = {
			"frag lane is open",
			"come and get it",
			"bring the noise",
			"who wants some",
		};
		return phrases[variant];
	}
	case 4: {
		static constexpr const char *phrases[] = {
			"ready to support",
			"covering the team",
			"watching your flank",
			"support on the way",
		};
		return phrases[variant];
	}
	case 5: {
		static constexpr const char *phrases[] = {
			"steady and ready",
			"holding formation",
			"route discipline set",
			"calm and ready",
		};
		return phrases[variant];
	}
	default: {
		static constexpr const char *phrases[] = {
			"standing by",
			"ready for action",
			"on task",
			"ready",
		};
		return phrases[variant];
	}
	}
}

bool BotChatPolicy_ReplySmokeEnabled() {
	return sg_bot_chat_reply_policy_smoke &&
		sg_bot_chat_reply_policy_smoke->integer > 0;
}

bool BotChatPolicy_EventSmokeEnabled() {
	return sg_bot_chat_event_policy_smoke &&
		sg_bot_chat_event_policy_smoke->integer > 0;
}

bool BotChatPolicy_LiveEventsEnabled() {
	return sg_bot_chat_live_events &&
		sg_bot_chat_live_events->integer > 0;
}

int Bot_CommandBehaviorLivePolicyCvarCount() {
	return (Bot_CommandBehaviorPolicyEnabled() ? 1 : 0) +
		(Bot_CommandTeamRoleRouteEnabled() ? 1 : 0) +
		(Bot_CommandTeamRoleCombatEnabled() ? 1 : 0) +
		(Bot_CommandTeamFireAvoidanceEnabled() ? 1 : 0) +
		(Bot_CommandMatchItemPolicyEnabled() ? 1 : 0) +
		(Bot_CommandCoopProgressWaitRequested() ? 1 : 0) +
		(Bot_CommandCtfObjectiveRouteEnabled() ? 1 : 0) +
		(Bot_CommandFfaRoamRouteEnabled() ? 1 : 0) +
		(Bot_CommandThreatRetreatEnabled() ? 1 : 0) +
		(BotChatPolicy_LiveEventsEnabled() ? 1 : 0);
}

int Bot_CommandBehaviorSmokePolicyCvarCount() {
	return (Bot_CommandProfileItemPolicySmoke() ? 1 : 0) +
		(Bot_CommandProfileMovementPolicySmoke() ? 1 : 0) +
		(BotChatPolicy_ReplySmokeEnabled() ? 1 : 0) +
		(BotChatPolicy_EventSmokeEnabled() ? 1 : 0);
}

int Bot_CommandBehaviorDebugPolicyCvarCount() {
	return 0;
}

int Bot_CommandBehaviorDeprecatedPolicyCvarCount() {
	return 0;
}

const char *Bot_CommandBehaviorArbitrationOwnerName(
	BotBehaviorArbitrationOwner owner) {
	switch (owner) {
	case BotBehaviorArbitrationOwner::Idle:
		return "idle";
	case BotBehaviorArbitrationOwner::Route:
		return "route";
	case BotBehaviorArbitrationOwner::Item:
		return "item";
	case BotBehaviorArbitrationOwner::Combat:
		return "combat";
	case BotBehaviorArbitrationOwner::Objective:
		return "objective";
	case BotBehaviorArbitrationOwner::Interaction:
		return "interaction";
	case BotBehaviorArbitrationOwner::Recovery:
		return "recovery";
	case BotBehaviorArbitrationOwner::None:
	default:
		return "none";
	}
}

int Bot_CommandBehaviorArbitrationPriority(BotBehaviorArbitrationOwner owner) {
	switch (owner) {
	case BotBehaviorArbitrationOwner::Recovery:
		return 90;
	case BotBehaviorArbitrationOwner::Interaction:
		return 80;
	case BotBehaviorArbitrationOwner::Objective:
		return 70;
	case BotBehaviorArbitrationOwner::Combat:
		return 60;
	case BotBehaviorArbitrationOwner::Item:
		return 40;
	case BotBehaviorArbitrationOwner::Route:
		return 30;
	case BotBehaviorArbitrationOwner::Idle:
		return 10;
	case BotBehaviorArbitrationOwner::None:
	default:
		return 0;
	}
}

BotBehaviorArbitrationOwner Bot_CommandSelectBehaviorArbitrationOwner(
	const BotBehaviorArbitrationCandidates &candidates) {
	if (candidates.recovery) {
		return BotBehaviorArbitrationOwner::Recovery;
	}
	if (candidates.interaction) {
		return BotBehaviorArbitrationOwner::Interaction;
	}
	if (candidates.objective) {
		return BotBehaviorArbitrationOwner::Objective;
	}
	if (candidates.combat) {
		return BotBehaviorArbitrationOwner::Combat;
	}
	if (candidates.item) {
		return BotBehaviorArbitrationOwner::Item;
	}
	if (candidates.route) {
		return BotBehaviorArbitrationOwner::Route;
	}
	return BotBehaviorArbitrationOwner::Idle;
}

const char *Bot_CommandBehaviorArbitrationReason(
	BotBehaviorArbitrationOwner owner) {
	switch (owner) {
	case BotBehaviorArbitrationOwner::Recovery:
		return "recovery_priority";
	case BotBehaviorArbitrationOwner::Interaction:
		return "interaction_priority";
	case BotBehaviorArbitrationOwner::Objective:
		return "objective_priority";
	case BotBehaviorArbitrationOwner::Combat:
		return "combat_priority";
	case BotBehaviorArbitrationOwner::Item:
		return "item_priority";
	case BotBehaviorArbitrationOwner::Route:
		return "route_priority";
	case BotBehaviorArbitrationOwner::Idle:
		return "idle_priority";
	case BotBehaviorArbitrationOwner::None:
	default:
		return "none";
	}
}

void Bot_CommandRecordBehaviorArbitration(
	gentity_t *bot,
	const BotBehaviorArbitrationCandidates &candidates) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botBehaviorArbitrationLastOwners.size())) {
		return;
	}

	const BotBehaviorArbitrationOwner owner =
		Bot_CommandSelectBehaviorArbitrationOwner(candidates);
	const int ownerValue = static_cast<int>(owner);
	int previousOwner = botBehaviorArbitrationLastOwners[clientIndex];
	if (botBehaviorArbitrationLastSpawnCounts[clientIndex] != bot->spawn_count) {
		previousOwner = 0;
		botBehaviorArbitrationLastOwners[clientIndex] = 0;
		botBehaviorArbitrationLastSpawnCounts[clientIndex] = bot->spawn_count;
	}

	botFrameCommandStatus.behaviorArbitrationEvaluations++;
	if (candidates.route) {
		botFrameCommandStatus.behaviorArbitrationRouteCandidates++;
	}
	if (candidates.item) {
		botFrameCommandStatus.behaviorArbitrationItemCandidates++;
	}
	if (candidates.combat) {
		botFrameCommandStatus.behaviorArbitrationCombatCandidates++;
	}
	if (candidates.objective) {
		botFrameCommandStatus.behaviorArbitrationObjectiveCandidates++;
	}
	if (candidates.interaction) {
		botFrameCommandStatus.behaviorArbitrationInteractionCandidates++;
	}
	if (candidates.recovery) {
		botFrameCommandStatus.behaviorArbitrationRecoveryCandidates++;
	}

	switch (owner) {
	case BotBehaviorArbitrationOwner::Recovery:
		botFrameCommandStatus.behaviorArbitrationRecoveryOwners++;
		break;
	case BotBehaviorArbitrationOwner::Interaction:
		botFrameCommandStatus.behaviorArbitrationInteractionOwners++;
		break;
	case BotBehaviorArbitrationOwner::Objective:
		botFrameCommandStatus.behaviorArbitrationObjectiveOwners++;
		break;
	case BotBehaviorArbitrationOwner::Combat:
		botFrameCommandStatus.behaviorArbitrationCombatOwners++;
		break;
	case BotBehaviorArbitrationOwner::Item:
		botFrameCommandStatus.behaviorArbitrationItemOwners++;
		break;
	case BotBehaviorArbitrationOwner::Route:
		botFrameCommandStatus.behaviorArbitrationRouteOwners++;
		break;
	case BotBehaviorArbitrationOwner::Idle:
		botFrameCommandStatus.behaviorArbitrationIdleOwners++;
		break;
	case BotBehaviorArbitrationOwner::None:
	default:
		break;
	}

	if (previousOwner != 0 && previousOwner != ownerValue) {
		botFrameCommandStatus.behaviorArbitrationHandoffs++;
	}
	botBehaviorArbitrationLastOwners[clientIndex] = ownerValue;
	botFrameCommandStatus.lastBehaviorArbitrationClient = clientIndex;
	botFrameCommandStatus.lastBehaviorArbitrationOwner = ownerValue;
	botFrameCommandStatus.lastBehaviorArbitrationPreviousOwner = previousOwner;
	botFrameCommandStatus.lastBehaviorArbitrationPriority =
		Bot_CommandBehaviorArbitrationPriority(owner);
	botFrameCommandStatus.lastBehaviorArbitrationOwnerName =
		Bot_CommandBehaviorArbitrationOwnerName(owner);
	botFrameCommandStatus.lastBehaviorArbitrationReason =
		Bot_CommandBehaviorArbitrationReason(owner);
}

int BotChatPolicy_ReplyEventId() {
	return 1;
}

int BotChatPolicy_RouteReadyEventId() {
	return 2;
}

int BotChatPolicy_SpawnEventId() {
	return 3;
}

int BotChatPolicy_ItemTakenEventId() {
	return 4;
}

int BotChatPolicy_ObjectiveChangedEventId() {
	return 7;
}

int BotChatPolicy_EnemySightedEventId() {
	return 6;
}

int BotChatPolicy_LowHealthEventId() {
	return 9;
}

constexpr int BOT_CHAT_POLICY_LIVE_EVENT_TAXONOMY = 11;

const char *BotChatPolicy_EventName(int event) {
	switch (event) {
	case 1:
		return "team_ready";
	case 2:
		return "route_ready";
	case 3:
		return "spawn";
	case 4:
		return "item_taken";
	case 5:
		return "item_denied";
	case 6:
		return "enemy_sighted";
	case 7:
		return "objective_changed";
	case 8:
		return "flag_state";
	case 9:
		return "low_health";
	case 10:
		return "blocked";
	case 11:
		return "victory_defeat";
	default:
		return "none";
	}
}

int BotChatPolicy_ReplyPhraseId(int personality, int event, int clientIndex) {
	const int phraseSeed =
		botChatReplyPolicyStatus.selections >= 0 ?
			botChatReplyPolicyStatus.selections :
			(clientIndex >= 0 ? clientIndex : 0);
	const int personalityBucket = personality > 0 ? personality : 0;
	const int variant =
		BotChatPolicy_PhraseVariant(phraseSeed, BOT_CHAT_POLICY_REPLY_PHRASE_VARIANTS);
	return 1000 + event * 100 + personalityBucket * 10 + variant;
}

const char *BotChatPolicy_ReplyPhrase(int personality, int event, int phrase) {
	const int variant =
		BotChatPolicy_PhraseIdVariant(phrase, BOT_CHAT_POLICY_REPLY_PHRASE_VARIANTS);
	if (event == BotChatPolicy_RouteReadyEventId()) {
		switch (personality) {
		case 1: {
			static constexpr const char *phrases[] = {
				"route set, watching",
				"route set, staying low",
				"path set, quiet",
				"moving, low profile",
			};
			return phrases[variant];
		}
		case 2: {
			static constexpr const char *phrases[] = {
				"route set, moving",
				"route set, pushing",
				"route set, taking point",
				"path ready, go",
			};
			return phrases[variant];
		}
		case 3: {
			static constexpr const char *phrases[] = {
				"route set, fragging",
				"route set, make noise",
				"route set, time to hurt",
				"path set, they can run",
			};
			return phrases[variant];
		}
		case 4: {
			static constexpr const char *phrases[] = {
				"route set, supporting",
				"route set, covering",
				"route set, with you",
				"path ready, covering flank",
			};
			return phrases[variant];
		}
		case 5: {
			static constexpr const char *phrases[] = {
				"route set, holding",
				"route set, steady",
				"path stable, moving",
				"route clear, steady",
			};
			return phrases[variant];
		}
		default: {
			static constexpr const char *phrases[] = {
				"route ready",
				"route acknowledged",
				"path ready",
				"moving on route",
			};
			return phrases[variant];
		}
		}
	}

	if (event == BotChatPolicy_ItemTakenEventId()) {
		switch (personality) {
		case 1: {
			static constexpr const char *phrases[] = {
				"pickup secured, quiet",
				"item taken, staying low",
				"got the pickup, watching",
				"secured item, quiet",
			};
			return phrases[variant];
		}
		case 2: {
			static constexpr const char *phrases[] = {
				"item taken, pushing",
				"pickup secured, moving",
				"got it, taking point",
				"item secured, go",
			};
			return phrases[variant];
		}
		case 3: {
			static constexpr const char *phrases[] = {
				"pickup taken, keep pressure",
				"item secured, bring noise",
				"got it, time to hurt",
				"pickup mine, fragging",
			};
			return phrases[variant];
		}
		case 4: {
			static constexpr const char *phrases[] = {
				"item taken, covering",
				"pickup secured, support ready",
				"got it, with you",
				"item secured, covering flank",
			};
			return phrases[variant];
		}
		case 5: {
			static constexpr const char *phrases[] = {
				"item taken, steady",
				"pickup secured, holding",
				"got it, steady",
				"item secured, controlled",
			};
			return phrases[variant];
		}
		default: {
			static constexpr const char *phrases[] = {
				"item taken",
				"pickup secured",
				"got the pickup",
				"item secured",
			};
			return phrases[variant];
		}
		}
	}

	if (event == BotChatPolicy_ObjectiveChangedEventId()) {
		switch (personality) {
		case 1: {
			static constexpr const char *phrases[] = {
				"objective changed, watching",
				"flag state changed, quiet",
				"objective moved, staying low",
				"flag update, eyes open",
			};
			return phrases[variant];
		}
		case 2: {
			static constexpr const char *phrases[] = {
				"objective changed, moving",
				"flag state changed, pushing",
				"objective updated, taking point",
				"flag update, go",
			};
			return phrases[variant];
		}
		case 3: {
			static constexpr const char *phrases[] = {
				"objective changed, keep pressure",
				"flag state changed, hit them",
				"objective moved, make noise",
				"flag update, fragging",
			};
			return phrases[variant];
		}
		case 4: {
			static constexpr const char *phrases[] = {
				"objective changed, covering",
				"flag state changed, supporting",
				"objective moved, with you",
				"flag update, covering flank",
			};
			return phrases[variant];
		}
		case 5: {
			static constexpr const char *phrases[] = {
				"objective changed, steady",
				"flag state changed, holding",
				"objective moved, controlled",
				"flag update, stay organized",
			};
			return phrases[variant];
		}
		default: {
			static constexpr const char *phrases[] = {
				"objective changed",
				"flag state changed",
				"objective updated",
				"flag update",
			};
			return phrases[variant];
		}
		}
	}

	if (event == BotChatPolicy_EnemySightedEventId()) {
		switch (personality) {
		case 1: {
			static constexpr const char *phrases[] = {
				"contact, watching",
				"contact, staying quiet",
				"target seen, quiet",
				"eyes on enemy",
			};
			return phrases[variant];
		}
		case 2: {
			static constexpr const char *phrases[] = {
				"enemy sighted, engaging",
				"enemy ahead, pushing",
				"contact front, taking it",
				"enemy marked, moving in",
			};
			return phrases[variant];
		}
		case 3: {
			static constexpr const char *phrases[] = {
				"enemy spotted, fragging",
				"fresh target, make noise",
				"target found, light them up",
				"contact, make it loud",
			};
			return phrases[variant];
		}
		case 4: {
			static constexpr const char *phrases[] = {
				"contact, supporting",
				"enemy spotted, covering",
				"enemy marked, with you",
				"contact seen, covering flank",
			};
			return phrases[variant];
		}
		case 5: {
			static constexpr const char *phrases[] = {
				"contact, holding",
				"enemy sighted, steady",
				"enemy marked, steady",
				"contact set, hold shape",
			};
			return phrases[variant];
		}
		default: {
			static constexpr const char *phrases[] = {
				"contact",
				"enemy spotted",
				"enemy seen",
				"target marked",
			};
			return phrases[variant];
		}
		}
	}

	if (event == BotChatPolicy_LowHealthEventId()) {
		switch (personality) {
		case 1: {
			static constexpr const char *phrases[] = {
				"hurt, staying low",
				"low health, quiet",
				"need cover, low",
				"hurt, watching",
			};
			return phrases[variant];
		}
		case 2: {
			static constexpr const char *phrases[] = {
				"low health, repositioning",
				"hurt, moving to health",
				"need health, pushing out",
				"low health, falling back",
			};
			return phrases[variant];
		}
		case 3: {
			static constexpr const char *phrases[] = {
				"low health, still dangerous",
				"hurt, not done",
				"need health, keep pressure",
				"low, make it count",
			};
			return phrases[variant];
		}
		case 4: {
			static constexpr const char *phrases[] = {
				"low health, need cover",
				"hurt, covering carefully",
				"need health, support me",
				"low health, regrouping",
			};
			return phrases[variant];
		}
		case 5: {
			static constexpr const char *phrases[] = {
				"low health, steady",
				"hurt, holding shape",
				"need health, staying calm",
				"low health, controlled",
			};
			return phrases[variant];
		}
		default: {
			static constexpr const char *phrases[] = {
				"low health",
				"need health",
				"hurt",
				"falling back",
			};
			return phrases[variant];
		}
		}
	}

	switch (personality) {
	case 1: {
		static constexpr const char *phrases[] = {
			"copy, watching",
			"copy, staying quiet",
			"copy, eyes open",
			"copy, moving quiet",
		};
		return phrases[variant];
	}
	case 2: {
		static constexpr const char *phrases[] = {
			"copy, engaging",
			"copy, pushing",
			"copy, taking point",
			"copy, moving",
		};
		return phrases[variant];
	}
	case 3: {
		static constexpr const char *phrases[] = {
			"heard, fragging",
			"heard, make them pay",
			"heard, bringing noise",
			"heard, making space",
		};
		return phrases[variant];
	}
	case 4: {
		static constexpr const char *phrases[] = {
			"copy, supporting",
			"copy, covering",
			"copy, watching flank",
			"copy, with you",
		};
		return phrases[variant];
	}
	case 5: {
		static constexpr const char *phrases[] = {
			"copy, holding",
			"copy, steady",
			"copy, maintaining line",
			"copy, calm",
		};
		return phrases[variant];
	}
	default: {
		static constexpr const char *phrases[] = {
			"acknowledged",
			"copy that",
			"understood",
			"on it",
		};
		return phrases[variant];
	}
	}
}

void BotChatPolicy_RecordInitialSelection(int clientIndex, int personality, int phrase) {
	const int variant =
		BotChatPolicy_PhraseIdVariant(phrase, BOT_CHAT_POLICY_INITIAL_PHRASE_VARIANTS);
	botChatInitialPolicyStatus.selections++;
	botChatInitialPolicyStatus.lastClient = clientIndex;
	botChatInitialPolicyStatus.lastPersonality = personality;
	botChatInitialPolicyStatus.lastPhrase = phrase;
	botChatInitialPolicyStatus.lastVariant = variant;
	botChatInitialPolicyStatus.variantMask |= 1U << variant;

	switch (personality) {
	case 1:
		botChatInitialPolicyStatus.knownPersonalities++;
		botChatInitialPolicyStatus.quiet++;
		break;
	case 2:
		botChatInitialPolicyStatus.knownPersonalities++;
		botChatInitialPolicyStatus.direct++;
		break;
	case 3:
		botChatInitialPolicyStatus.knownPersonalities++;
		botChatInitialPolicyStatus.taunting++;
		break;
	case 4:
		botChatInitialPolicyStatus.knownPersonalities++;
		botChatInitialPolicyStatus.helpful++;
		break;
	case 5:
		botChatInitialPolicyStatus.knownPersonalities++;
		botChatInitialPolicyStatus.steady++;
		break;
	default:
		botChatInitialPolicyStatus.unknownPersonalities++;
		break;
	}
}

void BotChatPolicy_RecordLiveEventSelection(int event) {
	botChatReplyPolicyStatus.liveEnabled = 1;
	botChatReplyPolicyStatus.liveEvents++;
	botChatReplyPolicyStatus.lastLiveEvent = event;
	if (event == BotChatPolicy_SpawnEventId()) {
		botChatReplyPolicyStatus.liveSpawn++;
	}
	else if (event == BotChatPolicy_RouteReadyEventId()) {
		botChatReplyPolicyStatus.liveRouteReady++;
	}
	else if (event == BotChatPolicy_ItemTakenEventId()) {
		botChatReplyPolicyStatus.liveItemTaken++;
	}
	else if (event == BotChatPolicy_ObjectiveChangedEventId()) {
		botChatReplyPolicyStatus.liveObjectiveChanged++;
	}
	else if (event == BotChatPolicy_EnemySightedEventId()) {
		botChatReplyPolicyStatus.liveEnemySighted++;
	}
	else if (event == BotChatPolicy_LowHealthEventId()) {
		botChatReplyPolicyStatus.liveLowHealth++;
	}
}

void BotChatPolicy_RecordReplySelection(
	int clientIndex, int personality, int event, int phrase, bool liveEvent) {
	const int variant =
		BotChatPolicy_PhraseIdVariant(phrase, BOT_CHAT_POLICY_REPLY_PHRASE_VARIANTS);
	botChatReplyPolicyStatus.enabled = 1;
	botChatReplyPolicyStatus.events++;
	botChatReplyPolicyStatus.selections++;
	botChatReplyPolicyStatus.lastClient = clientIndex;
	botChatReplyPolicyStatus.lastPersonality = personality;
	botChatReplyPolicyStatus.lastPhrase = phrase;
	botChatReplyPolicyStatus.lastVariant = variant;
	botChatReplyPolicyStatus.variantMask |= 1U << variant;
	botChatReplyPolicyStatus.lastEvent = event;

	if (event == 1) {
		botChatReplyPolicyStatus.teamReady++;
	}
	else if (event == BotChatPolicy_RouteReadyEventId()) {
		botChatReplyPolicyStatus.routeReady++;
	}
	else if (event == BotChatPolicy_ItemTakenEventId()) {
		botChatReplyPolicyStatus.itemTaken++;
	}
	else if (event == BotChatPolicy_ObjectiveChangedEventId()) {
		botChatReplyPolicyStatus.objectiveChanged++;
	}
	else if (event == BotChatPolicy_EnemySightedEventId()) {
		botChatReplyPolicyStatus.enemySighted++;
	}
	else if (event == BotChatPolicy_LowHealthEventId()) {
		botChatReplyPolicyStatus.lowHealth++;
	}

	if (liveEvent) {
		BotChatPolicy_RecordLiveEventSelection(event);
	}

	switch (personality) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		botChatReplyPolicyStatus.knownPersonalities++;
		break;
	default:
		botChatReplyPolicyStatus.unknownPersonalities++;
		break;
	}
}

bool Bot_CommandMaybeDispatchChatPolicy(gentity_t *bot) {
	if (!sg_bot_allow_chat || sg_bot_allow_chat->integer <= 0) {
		return false;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botChatPolicyDispatchSpawnCounts.size())) {
		return false;
	}

	if (botChatPolicyDispatchSpawnCounts[clientIndex] == bot->spawn_count) {
		return false;
	}

	char chatPersonality[MAX_INFO_VALUE] = {};
	if (!gi.Info_ValueForKey(
			bot->client->pers.userInfo,
			"bot_chat_personality",
			chatPersonality,
			sizeof(chatPersonality)) ||
		!chatPersonality[0]) {
		return false;
	}

	const int personality = BotChatPolicy_InitialPersonalityId(chatPersonality);
	const int phrase = BotChatPolicy_InitialPhraseId(personality, clientIndex);
	BotChatPolicy_RecordInitialSelection(clientIndex, personality, phrase);
	const bool liveEvent = BotChatPolicy_LiveEventsEnabled();
	if (liveEvent) {
		BotChatPolicy_RecordLiveEventSelection(BotChatPolicy_SpawnEventId());
	}

	std::string message = BotChatPolicy_InitialPhrase(personality, phrase);
	const bool teamOnly =
		sg_bot_chat_team_only && sg_bot_chat_team_only->integer > 0;
	const int rateLimitedBefore = BotChatPolicy_DispatchRateLimited();
	const int failuresBefore = BotChatPolicy_DispatchFailures();
	const bool dispatched = BotChatPolicy_Dispatch(bot, message.c_str(), teamOnly);
	if (liveEvent) {
		if (dispatched) {
			botChatReplyPolicyStatus.liveSubmitted++;
		}
		else if (BotChatPolicy_DispatchRateLimited() > rateLimitedBefore) {
			botChatReplyPolicyStatus.liveRateLimited++;
		}
		else if (BotChatPolicy_DispatchFailures() > failuresBefore) {
			botChatReplyPolicyStatus.liveFailures++;
		}
		else {
			botChatReplyPolicyStatus.liveFailures++;
		}
	}
	if (!dispatched && BotChatPolicy_DispatchRateLimited() == rateLimitedBefore) {
		return false;
	}

	botChatPolicyDispatchSpawnCounts[clientIndex] = bot->spawn_count;
	return dispatched;
}

bool Bot_CommandMaybeDispatchChatReplyEvent(
	gentity_t *bot,
	int event,
	std::array<int, MAX_CLIENTS> &spawnCounts,
	bool liveEvent) {
	if (!sg_bot_allow_chat || sg_bot_allow_chat->integer <= 0) {
		return false;
	}

	botChatReplyPolicyStatus.enabled = 1;

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(spawnCounts.size())) {
		return false;
	}

	if (spawnCounts[clientIndex] == bot->spawn_count) {
		return false;
	}

	if (botChatPolicyDispatchSpawnCounts[clientIndex] != bot->spawn_count) {
		return false;
	}

	char chatPersonality[MAX_INFO_VALUE] = {};
	if (!gi.Info_ValueForKey(
			bot->client->pers.userInfo,
			"bot_chat_personality",
			chatPersonality,
			sizeof(chatPersonality)) ||
		!chatPersonality[0]) {
		return false;
	}

	const int personality = BotChatPolicy_InitialPersonalityId(chatPersonality);
	const int phrase = BotChatPolicy_ReplyPhraseId(personality, event, clientIndex);
	if (BotChatPolicy_SuppressDuplicateReplyEvent(
			clientIndex, event, phrase, liveEvent)) {
		spawnCounts[clientIndex] = bot->spawn_count;
		return false;
	}

	BotChatPolicy_RecordReplySelection(
		clientIndex,
		personality,
		event,
		phrase,
		liveEvent);

	std::string message = BotChatPolicy_ReplyPhrase(personality, event, phrase);
	const bool teamOnly =
		sg_bot_chat_team_only && sg_bot_chat_team_only->integer > 0;
	const int rateLimitedBefore = BotChatPolicy_DispatchRateLimited();
	const int failuresBefore = BotChatPolicy_DispatchFailures();
	const bool dispatched = BotChatPolicy_Dispatch(bot, message.c_str(), teamOnly);
	if (dispatched) {
		BotChatPolicy_RecordRecentReplyEvent(clientIndex, event, phrase);
		botChatReplyPolicyStatus.submitted++;
		if (liveEvent) {
			botChatReplyPolicyStatus.liveSubmitted++;
		}
	}
	else if (BotChatPolicy_DispatchRateLimited() > rateLimitedBefore) {
		botChatReplyPolicyStatus.rateLimited++;
		if (liveEvent) {
			botChatReplyPolicyStatus.liveRateLimited++;
		}
	}
	else if (BotChatPolicy_DispatchFailures() > failuresBefore) {
		botChatReplyPolicyStatus.failures++;
		if (liveEvent) {
			botChatReplyPolicyStatus.liveFailures++;
		}
	}
	else {
		botChatReplyPolicyStatus.failures++;
		if (liveEvent) {
			botChatReplyPolicyStatus.liveFailures++;
		}
	}

	if (!dispatched && BotChatPolicy_DispatchRateLimited() == rateLimitedBefore) {
		return false;
	}

	spawnCounts[clientIndex] = bot->spawn_count;
	return dispatched;
}

bool Bot_CommandMaybeDispatchChatReplyPolicy(gentity_t *bot) {
	const bool replySmoke = BotChatPolicy_ReplySmokeEnabled();
	const bool eventSmoke = BotChatPolicy_EventSmokeEnabled();
	if (!replySmoke && !eventSmoke) {
		return false;
	}

	bool dispatched = false;
	dispatched = Bot_CommandMaybeDispatchChatReplyEvent(
		bot,
		BotChatPolicy_ReplyEventId(),
		botChatPolicyReplySpawnCounts,
		false) || dispatched;

	if (eventSmoke) {
		dispatched = Bot_CommandMaybeDispatchChatReplyEvent(
			bot,
			BotChatPolicy_RouteReadyEventId(),
			botChatPolicyRouteReplySpawnCounts,
			false) || dispatched;
	}

	return dispatched;
}

bool Bot_CommandMaybeDispatchLiveRouteReadyChat(gentity_t *bot) {
	if (!BotChatPolicy_LiveEventsEnabled()) {
		return false;
	}
	return Bot_CommandMaybeDispatchChatReplyEvent(
		bot,
		BotChatPolicy_RouteReadyEventId(),
		botChatPolicyLiveRouteReplySpawnCounts,
		true);
}

bool Bot_CommandMaybeDispatchLiveEnemySightedChat(gentity_t *bot) {
	if (!BotChatPolicy_LiveEventsEnabled()) {
		return false;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return false;
	}

	const BotBrainBlackboardSnapshot &snapshot =
		botBrainBlackboardSlots[clientIndex].snapshot;
	if (!snapshot.valid ||
		snapshot.currentEnemyEntity < 0 ||
		!snapshot.currentEnemyVisible) {
		return false;
	}

	return Bot_CommandMaybeDispatchChatReplyEvent(
		bot,
		BotChatPolicy_EnemySightedEventId(),
		botChatPolicyLiveEnemySightedSpawnCounts,
		true);
}

bool Bot_CommandMaybeDispatchLiveItemTakenChat(gentity_t *bot) {
	if (!BotChatPolicy_LiveEventsEnabled()) {
		return false;
	}

	const BotItemStatus &itemStatus = BotItems_GetStatus();
	if (itemStatus.itemHealthPickups <= 0 && itemStatus.itemArmorPickups <= 0) {
		return false;
	}

	return Bot_CommandMaybeDispatchChatReplyEvent(
		bot,
		BotChatPolicy_ItemTakenEventId(),
		botChatPolicyLiveItemTakenSpawnCounts,
		true);
}

bool Bot_CommandMaybeDispatchLiveObjectiveChangedChat(gentity_t *bot) {
	if (!BotChatPolicy_LiveEventsEnabled()) {
		return false;
	}

	const BotObjectiveStatus &objectiveStatus = BotObjectives_GetStatus();
	if (objectiveStatus.flagPickups <= 0 &&
		objectiveStatus.flagCaptures <= 0 &&
		objectiveStatus.flagDrops <= 0 &&
		objectiveStatus.flagReturns <= 0) {
		return false;
	}

	return Bot_CommandMaybeDispatchChatReplyEvent(
		bot,
		BotChatPolicy_ObjectiveChangedEventId(),
		botChatPolicyLiveObjectiveChangedSpawnCounts,
		true);
}

bool BotChatPolicy_BotIsLowHealth(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr || bot->health <= 0) {
		return false;
	}

	const int maxHealth = std::max(bot->maxHealth, bot->client->pers.maxHealth);
	return maxHealth > 0 &&
		bot->health * 100 <= maxHealth * BOT_CHAT_POLICY_LOW_HEALTH_PERCENT;
}

bool Bot_CommandMaybeDispatchLiveLowHealthChat(gentity_t *bot) {
	if (!BotChatPolicy_LiveEventsEnabled() ||
		!BotChatPolicy_BotIsLowHealth(bot)) {
		return false;
	}

	return Bot_CommandMaybeDispatchChatReplyEvent(
		bot,
		BotChatPolicy_LowHealthEventId(),
		botChatPolicyLiveLowHealthSpawnCounts,
		true);
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

bool Bot_PerceptionHostileMonsterTargetAlive(const gentity_t *ent) {
	return ent != nullptr &&
		ent->inUse &&
		ent->client == nullptr &&
		(ent->svFlags & SVF_MONSTER) != 0 &&
		ent->health > 0 &&
		!ent->deadFlag &&
		ent->takeDamage &&
		(ent->monsterInfo.aiFlags & AI_GOOD_GUY) == 0 &&
		(ent->flags & FL_NOTARGET) == 0;
}

bool Bot_PerceptionCombatTargetAlive(const gentity_t *ent) {
	return Bot_PerceptionEntityAlive(ent) ||
		Bot_PerceptionHostileMonsterTargetAlive(ent);
}

gentity_t *Bot_CommandClientEntity(int clientIndex) {
	if (clientIndex < 0 || clientIndex >= static_cast<int>(game.maxClients)) {
		return nullptr;
	}

	const int entityNumber = clientIndex + 1;
	if (entityNumber <= 0 || entityNumber >= static_cast<int>(globals.numEntities)) {
		return nullptr;
	}
	return &g_entities[entityNumber];
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
	if (!Bot_PerceptionCombatTargetAlive(candidate) ||
		candidate == bot ||
		(candidate->flags & FL_NOTARGET)) {
		return false;
	}

	if (candidate->client != nullptr && Teams() && OnSameTeam(bot, candidate)) {
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
	return facts.entity != nullptr && facts.entityNumber > 0 && facts.spawnCount >= 0;
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
	const int memoryWindowMilliseconds = Bot_CommandTargetMemorySmokeEnabled() ?
		BOT_PERCEPTION_TARGET_MEMORY_SMOKE_MILLISECONDS :
		BOT_PERCEPTION_MEMORY_MILLISECONDS;
	int lastContactMilliseconds = slot.snapshot.currentEnemyLastSeenTimeMilliseconds;
	lastContactMilliseconds =
		std::max(lastContactMilliseconds, slot.snapshot.lastHeardTimeMilliseconds);
	lastContactMilliseconds =
		std::max(lastContactMilliseconds, slot.snapshot.lastDamagedTimeMilliseconds);
	const int memoryAgeMilliseconds = facts.visible ? 0 :
		Bot_CommandElapsedMilliseconds(nowMilliseconds, lastContactMilliseconds);
	const bool retainedFromMemory =
		!facts.visible &&
		previousEnemy == facts.entityNumber &&
		memoryAgeMilliseconds > 0 &&
		memoryAgeMilliseconds <= memoryWindowMilliseconds;
	slot.snapshot.currentEnemyRetainedFromMemory = retainedFromMemory;
	slot.snapshot.currentEnemyMemoryAgeMilliseconds = memoryAgeMilliseconds;
	slot.snapshot.currentEnemyMemoryWindowMilliseconds = memoryWindowMilliseconds;
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
	if (retainedFromMemory) {
		botBrainBlackboardStatus.combatEnemyMemoryRetains++;
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
	botBrainBlackboardStatus.lastCombatEnemyRetainedFromMemory =
		retainedFromMemory ? 1 : 0;
	if (memoryAgeMilliseconds > 0 ||
		botBrainBlackboardStatus.lastCombatEnemyMemoryAgeMilliseconds <= 0) {
		botBrainBlackboardStatus.lastCombatEnemyMemoryAgeMilliseconds =
			memoryAgeMilliseconds;
	}
	botBrainBlackboardStatus.lastCombatEnemyMemoryWindowMilliseconds =
		memoryWindowMilliseconds;

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
	slot.snapshot.currentEnemyRetainedFromMemory = false;
	slot.snapshot.currentEnemyMemoryAgeMilliseconds = 0;
	slot.snapshot.currentEnemyMemoryWindowMilliseconds = 0;
	Bot_PerceptionClearEnemyEstimate(slot);
	slot.currentEnemyTrackedSinceTimeMilliseconds = 0;
	slot.currentEnemyVisibleSinceTimeMilliseconds = 0;
	slot.aimSettledSinceTimeMilliseconds = 0;
	slot.aimBurstShotsFired = 0;
	slot.aimBurstCooldownUntilMilliseconds = 0;
	slot.aimLastAttackTimeMilliseconds = 0;
}

int Bot_PerceptionMemoryWindowMilliseconds() {
	return Bot_CommandTargetMemorySmokeEnabled() ?
		BOT_PERCEPTION_TARGET_MEMORY_SMOKE_MILLISECONDS :
		BOT_PERCEPTION_MEMORY_MILLISECONDS;
}

int Bot_PerceptionLastContactMilliseconds(const BotBrainBlackboardSlot &slot) {
	int lastContactMilliseconds = slot.snapshot.currentEnemyLastSeenTimeMilliseconds;
	lastContactMilliseconds =
		std::max(lastContactMilliseconds, slot.snapshot.lastHeardTimeMilliseconds);
	lastContactMilliseconds =
		std::max(lastContactMilliseconds, slot.snapshot.lastDamagedTimeMilliseconds);
	return lastContactMilliseconds;
}

int Bot_PerceptionMemoryAgeMilliseconds(const BotBrainBlackboardSlot &slot) {
	return Bot_CommandElapsedMilliseconds(
		Bot_CommandCurrentTimeMilliseconds(),
		Bot_PerceptionLastContactMilliseconds(slot));
}

bool Bot_PerceptionMemoryExpired(const BotBrainBlackboardSlot &slot) {
	const int lastContactMilliseconds = Bot_PerceptionLastContactMilliseconds(slot);
	return lastContactMilliseconds <= 0 ||
		Bot_PerceptionMemoryAgeMilliseconds(slot) > Bot_PerceptionMemoryWindowMilliseconds();
}

void Bot_PerceptionRecordMemoryDecay(BotBrainBlackboardSlot &slot) {
	if (slot.snapshot.currentEnemyEntity < 0) {
		return;
	}

	botBrainBlackboardStatus.combatEnemyMemoryDecays++;
	botBrainBlackboardStatus.lastCombatEnemyMemoryAgeMilliseconds =
		Bot_PerceptionMemoryAgeMilliseconds(slot);
	botBrainBlackboardStatus.lastCombatEnemyMemoryWindowMilliseconds =
		Bot_PerceptionMemoryWindowMilliseconds();
	botBrainBlackboardStatus.lastCombatEnemyMemoryDecayEntity =
		slot.snapshot.currentEnemyEntity;
	botBrainBlackboardStatus.lastCombatEnemyMemoryDecayClient =
		slot.snapshot.currentEnemyClient;
}

void Bot_PerceptionApplyTargetMemorySmokeOcclusion(
	BotBrainBlackboardSlot &slot,
	BotPerceptionEnemyFacts *facts) {
	if (!Bot_CommandTargetMemorySmokeEnabled() ||
		facts == nullptr ||
		!Bot_PerceptionEnemyFactsValid(*facts) ||
		slot.snapshot.currentEnemyEntity != facts->entityNumber ||
		slot.snapshot.currentEnemySpawnCount != facts->spawnCount ||
		slot.snapshot.currentEnemyLastSeenTimeMilliseconds <= 0) {
		return;
	}

	if (slot.snapshot.currentEnemyRetainedFromMemory) {
		facts->visible = false;
		facts->shootable = false;
		botBrainBlackboardStatus.combatEnemyMemorySmokeOcclusions++;
		return;
	}

	const int visibleSinceMilliseconds =
		slot.currentEnemyVisibleSinceTimeMilliseconds > 0 ?
			slot.currentEnemyVisibleSinceTimeMilliseconds :
			slot.snapshot.currentEnemyLastSeenTimeMilliseconds;
	const int visibleAgeMilliseconds = Bot_CommandElapsedMilliseconds(
		Bot_CommandCurrentTimeMilliseconds(),
		visibleSinceMilliseconds);
	if (visibleAgeMilliseconds < BOT_PERCEPTION_TARGET_MEMORY_SMOKE_OCCLUDE_MILLISECONDS) {
		return;
	}

	facts->visible = false;
	facts->shootable = false;
	botBrainBlackboardStatus.combatEnemyMemorySmokeOcclusions++;
}

bool Bot_PerceptionCoopTargetSharePolicySupports(const BotObjectiveCoopPolicy &policy) {
	return policy.valid &&
		policy.coopMode &&
		(policy.followLeader ||
		 policy.waitForLeader ||
		 policy.regroup ||
		 policy.intent == BotObjectiveCoopIntent::SupportCombat);
}

void Bot_PerceptionRecordCoopTargetShareLast(
	int clientIndex,
	int sourceClient,
	const BotObjectiveCoopPolicy &policy,
	const BotPerceptionEnemyFacts &facts) {
	botFrameCommandStatus.lastCoopTargetShareClient = clientIndex;
	botFrameCommandStatus.lastCoopTargetShareSourceClient = sourceClient;
	botFrameCommandStatus.lastCoopTargetShareTargetEntity = facts.entityNumber;
	botFrameCommandStatus.lastCoopTargetShareTargetClient = facts.clientIndex;
	botFrameCommandStatus.lastCoopTargetShareTargetDistanceSquared =
		facts.distanceSquared;
	botFrameCommandStatus.lastCoopTargetShareIntent = static_cast<int>(policy.intent);
}

bool Bot_PerceptionApplyCoopTargetShare(
	gentity_t *bot,
	BotBrainBlackboardSlot &slot,
	int clientIndex) {
	if (!Bot_CommandCoopTargetShareEnabled()) {
		return false;
	}

	botFrameCommandStatus.coopTargetShareRequests++;
	const BotObjectiveCoopContext coopContext =
		BotObjectives_BuildCoopContext(
			bot,
			nullptr,
			false,
			BotObjectiveRole::Support);
	const BotObjectiveCoopPolicy policy =
		BotObjectives_EvaluateCoopPolicy(coopContext);
	if (!Bot_PerceptionCoopTargetSharePolicySupports(policy)) {
		botFrameCommandStatus.coopTargetShareInvalidSkips++;
		return false;
	}

	botFrameCommandStatus.coopTargetSharePolicySupports++;
	botFrameCommandStatus.coopTargetShareSourceScans++;

	bool found = false;
	int bestSourceClient = -1;
	BotPerceptionEnemyFacts bestFacts{};
	const int maxSourceDistanceSquared = static_cast<int>(
		BOT_COMMAND_COOP_TARGET_SHARE_SOURCE_DISTANCE_SQUARED);
	for (int sourceClient = 0;
		sourceClient < static_cast<int>(botBrainBlackboardSlots.size()) &&
		sourceClient < static_cast<int>(game.maxClients);
		++sourceClient) {
		if (sourceClient == clientIndex) {
			continue;
		}

		const BotBrainBlackboardSlot &sourceSlot =
			botBrainBlackboardSlots[sourceClient];
		const BotBrainBlackboardSnapshot &sourceSnapshot = sourceSlot.snapshot;
		if (!sourceSnapshot.valid || sourceSnapshot.currentEnemyEntity < 0) {
			continue;
		}

		gentity_t *source = Bot_CommandClientEntity(sourceClient);
		if (!Bot_PerceptionEntityAlive(source) || source == bot) {
			continue;
		}

		const Vector3 sourceDelta = source->s.origin - bot->s.origin;
		const int sourceDistanceSquared =
			Bot_PerceptionClampDistanceSquared(sourceDelta.lengthSquared());
		if (sourceDistanceSquared > maxSourceDistanceSquared) {
			continue;
		}

		gentity_t *sharedEnemy = Bot_PerceptionEntityFromMemory(
			sourceSnapshot.currentEnemyEntity,
			sourceSnapshot.currentEnemySpawnCount);
		if (!Bot_PerceptionCombatTargetAlive(sharedEnemy) || sharedEnemy == bot) {
			botFrameCommandStatus.coopTargetShareInvalidSkips++;
			continue;
		}
		if (!sourceSnapshot.currentEnemyVisible &&
			sourceSnapshot.currentEnemyLastSeenTimeMilliseconds <= 0) {
			continue;
		}

		BotPerceptionEnemyFacts facts =
			Bot_PerceptionEvaluateEnemy(bot, sharedEnemy);
		if (!Bot_PerceptionEnemyFactsValid(facts)) {
			botFrameCommandStatus.coopTargetShareInvalidSkips++;
			continue;
		}

		botFrameCommandStatus.coopTargetShareSourceCandidates++;
		if (!found ||
			(facts.shootable && !bestFacts.shootable) ||
			(facts.shootable == bestFacts.shootable &&
			 facts.distanceSquared < bestFacts.distanceSquared)) {
			found = true;
			bestSourceClient = sourceClient;
			bestFacts = facts;
		}
	}

	if (!found) {
		return false;
	}

	Bot_PerceptionSetCurrentEnemy(
		slot,
		bestFacts,
		slot.snapshot.currentEnemyEntity != bestFacts.entityNumber);
	botFrameCommandStatus.coopTargetShareAdoptions++;
	Bot_PerceptionRecordCoopTargetShareLast(
		clientIndex,
		bestSourceClient,
		policy,
		bestFacts);
	return true;
}

bool Bot_PerceptionScanDue(const BotBrainBlackboardSlot &slot, int clientIndex, bool hasCurrentEnemy) {
	if (Bot_CommandSmokeCombat()) {
		return true;
	}
	if (Bot_CommandTargetMemorySmokeEnabled() && hasCurrentEnemy) {
		return false;
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
	auto considerCandidate = [&](gentity_t *candidate) {
		BotPerceptionEnemyFacts facts = Bot_PerceptionEvaluateEnemy(bot, candidate);
		if (!Bot_PerceptionEnemyFactsValid(facts) || !facts.visible) {
			return;
		}

		if (!found ||
			(facts.shootable && !best.shootable) ||
			(facts.shootable == best.shootable && facts.distanceSquared < best.distanceSquared)) {
			best = facts;
			found = true;
		}
	};

	for (gentity_t *candidate : active_players()) {
		considerCandidate(candidate);
	}

	if (Bot_CommandCoopTargetShareEnabled()) {
		for (int entityNumber = static_cast<int>(game.maxClients) + 1;
			entityNumber < static_cast<int>(globals.numEntities);
			++entityNumber) {
			gentity_t *candidate = &g_entities[entityNumber];
			if (candidate->client != nullptr) {
				continue;
			}
			considerCandidate(candidate);
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

	if (Bot_CommandTargetMemorySmokeEnabled() &&
		botBrainBlackboardStatus.combatEnemyMemoryDecays > 0) {
		Bot_PerceptionClearCurrentEnemy(slot);
		Bot_PerceptionApplyLatestDamageEstimate(bot, slot);
		return;
	}

	bool hasCurrentEnemy = false;
	gentity_t *currentEnemy = Bot_PerceptionEntityFromMemory(
		slot.snapshot.currentEnemyEntity,
		slot.snapshot.currentEnemySpawnCount);
	if (Bot_PerceptionCandidateEnemy(bot, currentEnemy)) {
		BotPerceptionEnemyFacts currentFacts = Bot_PerceptionEvaluateEnemy(bot, currentEnemy);
		if (Bot_PerceptionEnemyFactsValid(currentFacts)) {
			Bot_PerceptionApplyTargetMemorySmokeOcclusion(slot, &currentFacts);
			Bot_PerceptionSetCurrentEnemy(slot, currentFacts, false);
			hasCurrentEnemy = true;
		}
	}

	if (hasCurrentEnemy && !slot.snapshot.currentEnemyVisible && Bot_PerceptionMemoryExpired(slot)) {
		Bot_PerceptionRecordMemoryDecay(slot);
		Bot_PerceptionClearCurrentEnemy(slot);
		hasCurrentEnemy = false;
	}

	if (!hasCurrentEnemy && Bot_PerceptionApplyCoopTargetShare(bot, slot, clientIndex)) {
		hasCurrentEnemy = true;
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
	if (!Bot_PerceptionCombatTargetAlive(enemy) || enemy == bot) {
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

int Bot_CommandCtfDroppedFlagSmokeTargetIndex(int item) {
	if (item == IT_FLAG_RED) {
		return 0;
	}
	if (item == IT_FLAG_BLUE) {
		return 1;
	}
	return -1;
}

gentity_t *Bot_CommandCtfDroppedFlagSmokeEntity(int item) {
	const int index = Bot_CommandCtfDroppedFlagSmokeTargetIndex(item);
	if (index < 0 ||
		index >= static_cast<int>(botCtfDroppedFlagSmokeTargets.size())) {
		return nullptr;
	}

	const BotCtfDroppedFlagSmokeTarget &ref =
		botCtfDroppedFlagSmokeTargets[static_cast<size_t>(index)];
	if (ref.entityNumber <= static_cast<int>(game.maxClients) ||
		ref.entityNumber >= static_cast<int>(globals.numEntities)) {
		return nullptr;
	}

	gentity_t *target = &g_entities[ref.entityNumber];
	if (!target->inUse ||
		target->spawn_count != ref.spawnCount ||
		target->item == nullptr ||
		target->item->id != ref.item ||
		!target->spawnFlags.has(SPAWNFLAG_ITEM_DROPPED)) {
		return nullptr;
	}
	return target;
}

void Bot_CommandFreeCtfDroppedFlagSmokeTargets() {
	for (BotCtfDroppedFlagSmokeTarget &ref : botCtfDroppedFlagSmokeTargets) {
		gentity_t *target = Bot_CommandCtfDroppedFlagSmokeEntity(ref.item);
		if (target != nullptr) {
			FreeEntity(target);
		}
		ref = {};
	}
}

gentity_t *Bot_CommandAmmoPressureSmokeEntity() {
	if (botAmmoPressureSmokeTarget.entityNumber <= static_cast<int>(game.maxClients) ||
		botAmmoPressureSmokeTarget.entityNumber >= static_cast<int>(globals.numEntities)) {
		return nullptr;
	}

	gentity_t *target = &g_entities[botAmmoPressureSmokeTarget.entityNumber];
	if (!target->inUse ||
		target->spawn_count != botAmmoPressureSmokeTarget.spawnCount ||
		target->item == nullptr ||
		target->item->id != botAmmoPressureSmokeTarget.item ||
		!target->spawnFlags.has(SPAWNFLAG_ITEM_DROPPED)) {
		return nullptr;
	}
	return target;
}

gentity_t *Bot_CommandSurvivalRouteSmokeEntity() {
	if (botSurvivalRouteSmokeTarget.entityNumber <= static_cast<int>(game.maxClients) ||
		botSurvivalRouteSmokeTarget.entityNumber >= static_cast<int>(globals.numEntities)) {
		return nullptr;
	}

	gentity_t *target = &g_entities[botSurvivalRouteSmokeTarget.entityNumber];
	if (!target->inUse ||
		target->spawn_count != botSurvivalRouteSmokeTarget.spawnCount ||
		target->item == nullptr ||
		target->item->id != botSurvivalRouteSmokeTarget.item ||
		!target->spawnFlags.has(SPAWNFLAG_ITEM_DROPPED)) {
		return nullptr;
	}
	return target;
}

void Bot_CommandFreeAmmoPressureSmokeTarget() {
	gentity_t *target = Bot_CommandAmmoPressureSmokeEntity();
	if (target != nullptr) {
		FreeEntity(target);
	}
	botAmmoPressureSmokeTarget = {};
}

void Bot_CommandFreeSurvivalRouteSmokeTarget() {
	gentity_t *target = Bot_CommandSurvivalRouteSmokeEntity();
	if (target != nullptr) {
		FreeEntity(target);
	}
	botSurvivalRouteSmokeTarget = {};
}

bool Bot_CommandInitializeDroppedSmokeItemTarget(
	gentity_t *target,
	int itemId) {
	Item *item = Bot_CommandItemForId(itemId);
	if (target == nullptr || item == nullptr || item->className == nullptr) {
		return false;
	}

	target->className = item->className;
	if (target->spawn_count <= 0) {
		target->spawn_count = 1;
	}
	target->item = item;
	target->spawnFlags = SPAWNFLAG_ITEM_DROPPED;
	target->solid = SOLID_TRIGGER;
	target->moveType = MoveType::None;
	target->mins = { -15.0f, -15.0f, -15.0f };
	target->maxs = { 15.0f, 15.0f, 15.0f };
	target->touch = nullptr;
	target->owner = nullptr;
	target->s.effects = item->worldModelFlags;
	target->s.renderFX = RF_GLOW | RF_NO_LOD | RF_IR_VISIBLE;
	if (target->s.scale <= 0.0f) {
		target->s.scale = 1.0f;
	}
	if (item->worldModel != nullptr) {
		gi.setModel(target, item->worldModel);
	}
	return true;
}

bool Bot_CommandPlaceDroppedSmokeItemTarget(
	gentity_t *bot,
	gentity_t *target) {
	if (bot == nullptr || target == nullptr) {
		return false;
	}

	static constexpr float offsets[][3] = {
		{ 224.0f, 0.0f, 0.0f },
		{ -224.0f, 0.0f, 0.0f },
		{ 0.0f, 224.0f, 0.0f },
		{ 0.0f, -224.0f, 0.0f },
		{ 160.0f, 160.0f, 0.0f },
		{ -160.0f, 160.0f, 0.0f },
	};

	for (const auto &offset : offsets) {
		const float candidate[3] = {
			bot->s.origin.x + offset[0],
			bot->s.origin.y + offset[1],
			bot->s.origin.z + offset[2],
		};
		int area = 0;
		float routeOrigin[3] = {};
		if (!BotLibAdapter_FindRouteAreaForPoint(candidate, &area, routeOrigin) ||
			area <= 0) {
			continue;
		}

		target->s.origin = {
			routeOrigin[0],
			routeOrigin[1],
			routeOrigin[2],
		};
		target->velocity = vec3_origin;
		gi.linkEntity(target);
		return true;
	}

	target->s.origin = bot->s.origin;
	target->velocity = vec3_origin;
	gi.linkEntity(target);
	return true;
}

bool Bot_CommandEnsureAmmoPressureSmokeTarget(gentity_t *bot) {
	if (bot == nullptr) {
		return false;
	}
	if (Bot_CommandAmmoPressureSmokeEntity() != nullptr) {
		return true;
	}

	static constexpr item_id_t itemId = IT_AMMO_SHELLS;
	gentity_t *target = Spawn();
	if (target == nullptr ||
		!Bot_CommandInitializeDroppedSmokeItemTarget(target, itemId) ||
		!Bot_CommandPlaceDroppedSmokeItemTarget(bot, target)) {
		if (target != nullptr) {
			FreeEntity(target);
		}
		botAmmoPressureSmokeTarget = {};
		return false;
	}

	botAmmoPressureSmokeTarget = {
		.entityNumber = target->s.number,
		.spawnCount = target->spawn_count,
		.item = itemId,
	};
	return true;
}

bool Bot_CommandEnsureSurvivalRouteSmokeTarget(
	gentity_t *bot,
	item_id_t itemId) {
	if (bot == nullptr) {
		return false;
	}

	if (gentity_t *existing = Bot_CommandSurvivalRouteSmokeEntity()) {
		if (existing->item != nullptr && existing->item->id == itemId) {
			return true;
		}
		Bot_CommandFreeSurvivalRouteSmokeTarget();
	}

	gentity_t *target = Spawn();
	if (target == nullptr ||
		!Bot_CommandInitializeDroppedSmokeItemTarget(target, itemId) ||
		!Bot_CommandPlaceDroppedSmokeItemTarget(bot, target)) {
		if (target != nullptr) {
			FreeEntity(target);
		}
		botSurvivalRouteSmokeTarget = {};
		return false;
	}

	botSurvivalRouteSmokeTarget = {
		.entityNumber = target->s.number,
		.spawnCount = target->spawn_count,
		.item = itemId,
	};
	return true;
}

bool Bot_CommandInitializeCtfDroppedFlagSmokeTarget(
	gentity_t *target,
	int itemId) {
	Item *item = Bot_CommandItemForId(itemId);
	if (target == nullptr || item == nullptr || item->className == nullptr) {
		return false;
	}

	target->className = item->className;
	if (target->spawn_count <= 0) {
		target->spawn_count = 1;
	}
	target->item = item;
	target->spawnFlags = SPAWNFLAG_ITEM_DROPPED;
	target->solid = SOLID_TRIGGER;
	target->moveType = MoveType::None;
	target->mins = { -15.0f, -15.0f, -15.0f };
	target->maxs = { 15.0f, 15.0f, 15.0f };
	target->touch = nullptr;
	target->owner = nullptr;
	target->s.effects = item->worldModelFlags;
	target->s.renderFX = RF_GLOW | RF_NO_LOD | RF_IR_VISIBLE;
	if (target->s.scale <= 0.0f) {
		target->s.scale = 1.0f;
	}
	if (item->worldModel != nullptr) {
		gi.setModel(target, item->worldModel);
	}
	return true;
}

bool Bot_CommandPlaceCtfDroppedFlagSmokeTarget(
	gentity_t *bot,
	gentity_t *target,
	int itemId) {
	if (bot == nullptr || target == nullptr) {
		return false;
	}

	const int index = Bot_CommandCtfDroppedFlagSmokeTargetIndex(itemId);
	static constexpr float offsets[2][5][3] = {
		{
			{ 0.0f, 224.0f, 0.0f },
			{ 224.0f, 0.0f, 0.0f },
			{ -224.0f, 0.0f, 0.0f },
			{ 0.0f, -224.0f, 0.0f },
			{ 128.0f, 128.0f, 0.0f },
		},
		{
			{ 0.0f, -224.0f, 0.0f },
			{ -224.0f, 0.0f, 0.0f },
			{ 224.0f, 0.0f, 0.0f },
			{ 0.0f, 224.0f, 0.0f },
			{ -128.0f, -128.0f, 0.0f },
		},
	};
	const size_t offsetIndex = index == 0 ? 0u : 1u;

	for (const auto &offset : offsets[offsetIndex]) {
		const float candidate[3] = {
			bot->s.origin.x + offset[0],
			bot->s.origin.y + offset[1],
			bot->s.origin.z + offset[2],
		};
		int area = 0;
		float routeOrigin[3] = {};
		if (!BotLibAdapter_FindRouteAreaForPoint(candidate, &area, routeOrigin) ||
			area <= 0) {
			continue;
		}

		target->s.origin = {
			routeOrigin[0],
			routeOrigin[1],
			routeOrigin[2],
		};
		target->velocity = vec3_origin;
		gi.linkEntity(target);
		return true;
	}

	target->s.origin = bot->s.origin;
	target->velocity = vec3_origin;
	gi.linkEntity(target);
	return true;
}

bool Bot_CommandEnsureCtfDroppedFlagSmokeTargets(gentity_t *bot) {
	if (bot == nullptr) {
		return false;
	}

	bool prepared = true;
	const int items[] = { IT_FLAG_RED, IT_FLAG_BLUE };
	for (const int itemId : items) {
		const int index = Bot_CommandCtfDroppedFlagSmokeTargetIndex(itemId);
		if (index < 0) {
			prepared = false;
			continue;
		}
		if (Bot_CommandCtfDroppedFlagSmokeEntity(itemId) != nullptr) {
			continue;
		}

		gentity_t *target = Spawn();
		if (target == nullptr ||
			!Bot_CommandInitializeCtfDroppedFlagSmokeTarget(target, itemId) ||
			!Bot_CommandPlaceCtfDroppedFlagSmokeTarget(bot, target, itemId)) {
			if (target != nullptr) {
				FreeEntity(target);
			}
			botCtfDroppedFlagSmokeTargets[static_cast<size_t>(index)] = {};
			prepared = false;
			continue;
		}

		botCtfDroppedFlagSmokeTargets[static_cast<size_t>(index)] = {
			.entityNumber = target->s.number,
			.spawnCount = target->spawn_count,
			.item = itemId,
		};
	}
	return prepared;
}

gentity_t *Bot_CommandFindSmokeTeamPlayer(Team team, gentity_t *exclude) {
	for (gentity_t *candidate : active_players()) {
		if (candidate == nullptr ||
			candidate == exclude ||
			candidate->client == nullptr ||
			!Bot_PerceptionEntityAlive(candidate) ||
			(candidate->flags & FL_NOTARGET) ||
			candidate->client->sess.team != team) {
			continue;
		}
		return candidate;
	}
	return nullptr;
}

gentity_t *Bot_CommandFindDroppedCtfFlagSmokeEntity(int itemId) {
	if (!Bot_CommandItemIdValid(itemId)) {
		return nullptr;
	}

	for (int entityNumber = static_cast<int>(game.maxClients) + 1;
		 entityNumber < static_cast<int>(globals.numEntities);
		 ++entityNumber) {
		gentity_t *candidate = &g_entities[entityNumber];
		if (candidate == nullptr ||
			!candidate->inUse ||
			candidate->item == nullptr ||
			candidate->item->id != itemId ||
			(!candidate->spawnFlags.has(SPAWNFLAG_ITEM_DROPPED) &&
			 !candidate->spawnFlags.has(SPAWNFLAG_ITEM_DROPPED_PLAYER))) {
			continue;
		}
		return candidate;
	}
	return nullptr;
}

void Bot_CommandClearCtfSmokeFlagInventories() {
	for (gentity_t *player : active_players()) {
		if (player == nullptr || player->client == nullptr) {
			continue;
		}

		player->client->pers.inventory[IT_FLAG_RED] = 0;
		player->client->pers.inventory[IT_FLAG_BLUE] = 0;
		player->client->pers.inventory[IT_FLAG_NEUTRAL] = 0;
		player->client->resp.ctf_flagsince = 0_ms;
		player->client->pers.teamState.flag_pickup_time = 0_ms;
	}
}

void Bot_CommandTouchSmokeItem(gentity_t *item, gentity_t *player) {
	if (item == nullptr || item->item == nullptr || player == nullptr ||
		player->client == nullptr) {
		return;
	}

	const int itemId = item->item->id;
	if (itemId == IT_FLAG_RED || itemId == IT_FLAG_BLUE ||
		itemId == IT_FLAG_NEUTRAL) {
		(void)CTF_PickupFlag(item, player);
		return;
	}

	trace_t trace{};
	Touch_Item(item, player, trace, false);
}

bool Bot_CommandPrepareCtfObjectiveTransitionEvents(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (slot.ctfObjectiveTransitionsPrepared) {
		return true;
	}
	if (bot == nullptr || bot->client == nullptr) {
		return false;
	}
	if (!Bot_CommandCtfObjectiveRouteSmokeReady()) {
		return false;
	}
	if (Bot_PerceptionClientIndex(bot) != 0) {
		return false;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	gentity_t *carrier = bot;
	const Team carrierTeam = carrier->client->sess.team;
	if (carrierTeam != Team::Red && carrierTeam != Team::Blue) {
		return false;
	}

	const int enemyFlagItem = BotObjectives_EnemyFlagItemForTeam(
		static_cast<int>(carrierTeam));
	const Team enemyTeam = carrierTeam == Team::Red ? Team::Blue : Team::Red;
	gentity_t *returner = Bot_CommandFindSmokeTeamPlayer(enemyTeam, carrier);
	if (!Bot_CommandItemIdValid(enemyFlagItem) ||
		returner == nullptr ||
		returner->client == nullptr) {
		return false;
	}

	Bot_CommandClearCtfSmokeFlagInventories();
	Bot_CommandFreeCtfDroppedFlagSmokeTargets();
	CTF_ResetFlags();
	if (!Bot_CommandEnsureCtfDroppedFlagSmokeTargets(bot)) {
		return false;
	}

	gentity_t *enemyFlag = Bot_CommandCtfDroppedFlagSmokeEntity(enemyFlagItem);
	if (enemyFlag == nullptr) {
		return false;
	}

	const int pickupsBefore = BotObjectives_GetStatus().flagPickups;
	Bot_CommandTouchSmokeItem(enemyFlag, carrier);
	if (BotObjectives_GetStatus().flagPickups <= pickupsBefore ||
		carrier->client->pers.inventory[enemyFlagItem] <= 0) {
		return false;
	}
	Bot_CommandFreeCtfDroppedFlagSmokeTargets();

	const int dropsBefore = BotObjectives_GetStatus().flagDrops;
	CTF_DeadDropFlag(carrier);
	if (BotObjectives_GetStatus().flagDrops <= dropsBefore) {
		return false;
	}

	gentity_t *droppedFlag = Bot_CommandFindDroppedCtfFlagSmokeEntity(enemyFlagItem);
	if (droppedFlag == nullptr) {
		return false;
	}

	const int returnsBefore = BotObjectives_GetStatus().flagReturns;
	Bot_CommandTouchSmokeItem(droppedFlag, returner);
	if (BotObjectives_GetStatus().flagReturns <= returnsBefore) {
		return false;
	}

	slot.ctfObjectiveTransitionsPrepared = true;
	Bot_CommandFreeCtfDroppedFlagSmokeTargets();
	return true;
}

int Bot_CommandCurrentWeaponItem(const gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr || bot->client->pers.weapon == nullptr) {
		return IT_NULL;
	}
	return bot->client->pers.weapon->id;
}

gentity_t *Bot_CommandCoopTargetShareSmokeEntity() {
	if (botCoopTargetShareSmokeTarget.entityNumber <= static_cast<int>(game.maxClients) ||
		botCoopTargetShareSmokeTarget.entityNumber >= static_cast<int>(globals.numEntities)) {
		return nullptr;
	}

	gentity_t *target = &g_entities[botCoopTargetShareSmokeTarget.entityNumber];
	if (!target->inUse ||
		target->spawn_count != botCoopTargetShareSmokeTarget.spawnCount ||
		!Bot_PerceptionHostileMonsterTargetAlive(target)) {
		return nullptr;
	}
	return target;
}

void Bot_CommandFreeCoopTargetShareSmokeTarget() {
	gentity_t *target = Bot_CommandCoopTargetShareSmokeEntity();
	if (target != nullptr) {
		FreeEntity(target);
	}
	botCoopTargetShareSmokeTarget = {};
}

void Bot_CommandInitializeCoopTargetShareSmokeTarget(
	gentity_t *target,
	gentity_t *bot) {
	target->className = "bot_coop_target_share_smoke_target";
	if (target->spawn_count <= 0) {
		target->spawn_count = 1;
	}
	target->svFlags |= SVF_MONSTER;
	target->moveType = MoveType::None;
	target->solid = SOLID_BBOX;
	target->clipMask = MASK_MONSTERSOLID;
	target->mins = { -16.0f, -16.0f, -24.0f };
	target->maxs = { 16.0f, 16.0f, 40.0f };
	target->viewHeight = 24;
	target->takeDamage = true;
	target->deadFlag = false;
	target->health = BOT_COMMAND_COOP_TARGET_SHARE_SMOKE_HEALTH;
	target->maxHealth = BOT_COMMAND_COOP_TARGET_SHARE_SMOKE_HEALTH;
	target->mass = 200;
	target->enemy = bot;
}

bool Bot_CommandPlaceCoopTargetShareSmokeTarget(
	gentity_t *bot,
	gentity_t *target) {
	if (bot == nullptr || target == nullptr) {
		return false;
	}

	static constexpr float offsets[][3] = {
		{ 160.0f, 0.0f, 0.0f },
		{ -160.0f, 0.0f, 0.0f },
		{ 0.0f, 160.0f, 0.0f },
		{ 0.0f, -160.0f, 0.0f },
		{ 192.0f, 96.0f, 0.0f },
		{ -192.0f, 96.0f, 0.0f },
		{ 96.0f, -192.0f, 0.0f },
	};

	for (const auto &offset : offsets) {
		target->s.origin = {
			bot->s.origin.x + offset[0],
			bot->s.origin.y + offset[1],
			bot->s.origin.z + offset[2]
		};
		target->velocity = vec3_origin;
		gi.linkEntity(target);

		if (visible(bot, target) && CanDamage(target, bot)) {
			return true;
		}
	}

	return visible(bot, target);
}

gentity_t *Bot_CommandEnsureCoopTargetShareSmokeTarget(gentity_t *bot) {
	gentity_t *target = Bot_CommandCoopTargetShareSmokeEntity();
	if (target != nullptr) {
		return target;
	}

	if (bot == nullptr) {
		return nullptr;
	}

	target = Spawn();
	if (target == nullptr) {
		return nullptr;
	}

	Bot_CommandInitializeCoopTargetShareSmokeTarget(target, bot);
	botCoopTargetShareSmokeTarget.entityNumber = target->s.number;
	botCoopTargetShareSmokeTarget.spawnCount = target->spawn_count;
	(void)Bot_CommandPlaceCoopTargetShareSmokeTarget(bot, target);
	return target;
}

BotCommandSmokeProofSlot *Bot_CommandSmokeProofSlotFor(gentity_t *bot) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 ||
		clientIndex >= static_cast<int>(botCommandSmokeProofSlots.size())) {
		return nullptr;
	}

	const int mode = Bot_CommandSmokeScenarioMode();
	if (mode != 30 && mode != 78) {
		Bot_CommandFreeCoopTargetShareSmokeTarget();
	}
	if (mode != 37 && mode != 40 && mode != 76) {
		Bot_CommandFreeCtfDroppedFlagSmokeTargets();
	}
	if (mode != 67) {
		Bot_CommandFreeAmmoPressureSmokeTarget();
	}
	if (mode != 69 && mode != 70 && mode != 71) {
		Bot_CommandFreeSurvivalRouteSmokeTarget();
	}
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

bool Bot_CommandPlaceWeaponScoringSmokePeer(gentity_t *bot, gentity_t *peer) {
	if (bot == nullptr || peer == nullptr || peer->client == nullptr) {
		return false;
	}

	static constexpr float offsets[][3] = {
		{ 192.0f, 0.0f, 0.0f },
		{ -192.0f, 0.0f, 0.0f },
		{ 0.0f, 192.0f, 0.0f },
		{ 0.0f, -192.0f, 0.0f },
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

	return visible(bot, peer) && CanDamage(peer, bot);
}

bool Bot_CommandPlaceAimFirePolicySmokePeer(gentity_t *bot, gentity_t *peer) {
	if (bot == nullptr || peer == nullptr || peer->client == nullptr) {
		return false;
	}

	static constexpr float offsets[][3] = {
		{ 384.0f, 0.0f, 0.0f },
		{ -384.0f, 0.0f, 0.0f },
		{ 0.0f, 384.0f, 0.0f },
		{ 0.0f, -384.0f, 0.0f },
		{ 320.0f, 192.0f, 0.0f },
		{ -320.0f, 192.0f, 0.0f },
	};

	for (const auto &offset : offsets) {
		const Vector3 origin = {
			bot->s.origin.x + offset[0],
			bot->s.origin.y + offset[1],
			bot->s.origin.z + offset[2]
		};
		const Vector3 angles = VectorToAngles(bot->s.origin - origin);
		TeleportPlayer(peer, origin, angles);
		peer->velocity = { 0.0f, 180.0f, 0.0f };
		const int peerClientIndex = Bot_PerceptionClientIndex(peer);
		if (peerClientIndex >= 0) {
			BotNav_ResetClient(peerClientIndex);
		}

		if (visible(bot, peer) && CanDamage(peer, bot)) {
			return true;
		}
	}

	return visible(bot, peer) && CanDamage(peer, bot);
}

void Bot_CommandResetPlacedClient(gentity_t *client) {
	const int clientIndex = Bot_PerceptionClientIndex(client);
	if (clientIndex >= 0) {
		BotNav_ResetClient(clientIndex);
	}
	if (client != nullptr) {
		client->velocity = vec3_origin;
	}
}

void Bot_CommandStabilizeCtfRouteSmokeCarrier(gentity_t *carrier) {
	if (carrier == nullptr || carrier->client == nullptr) {
		return;
	}

	static constexpr int smokeCarrierHealth = 10000;
	carrier->takeDamage = false;
	carrier->health = smokeCarrierHealth;
	carrier->maxHealth = smokeCarrierHealth;
	carrier->client->pers.health = smokeCarrierHealth;
	carrier->client->pers.maxHealth = smokeCarrierHealth;
}

gentity_t *Bot_CommandFindSmokeTeammate(gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr || !Teams()) {
		return nullptr;
	}

	for (gentity_t *candidate : active_players()) {
		if (candidate == bot ||
			!Bot_PerceptionEntityAlive(candidate) ||
			(candidate->flags & FL_NOTARGET) ||
			!OnSameTeam(bot, candidate)) {
			continue;
		}
		return candidate;
	}
	return nullptr;
}

int Bot_CommandSmokeActivePlayerCount() {
	int count = 0;
	for (gentity_t *candidate : active_players()) {
		if (candidate != nullptr &&
			candidate->client != nullptr &&
			Bot_PerceptionEntityAlive(candidate) &&
			!(candidate->flags & FL_NOTARGET)) {
			count++;
		}
	}
	return count;
}

bool Bot_CommandCtfCarrierSupportSmokeReady() {
	if (Bot_CommandSmokeScenarioMode() != 38) {
		return true;
	}

	return Bot_CommandSmokeActivePlayerCount() >= 4 &&
		botFrameCommandStatus.frames >=
			BOT_COMMAND_CTF_CARRIER_SUPPORT_SMOKE_WARMUP_FRAMES;
}

bool Bot_CommandCtfBaseReturnSmokeReady() {
	if (Bot_CommandSmokeScenarioMode() != 39) {
		return true;
	}

	return Bot_CommandSmokeActivePlayerCount() >= 4 &&
		botFrameCommandStatus.frames >=
			BOT_COMMAND_CTF_CARRIER_SUPPORT_SMOKE_WARMUP_FRAMES;
}

bool Bot_CommandCtfObjectiveRouteSmokeReady() {
	const int mode = Bot_CommandSmokeScenarioMode();
	if (mode != 40 && mode != 76) {
		return true;
	}

	return Bot_CommandSmokeActivePlayerCount() >= 4 &&
		botFrameCommandStatus.frames >=
			BOT_COMMAND_CTF_CARRIER_SUPPORT_SMOKE_WARMUP_FRAMES;
}

gentity_t *Bot_CommandFindSmokeEnemy(gentity_t *bot) {
	if (bot == nullptr || bot->client == nullptr) {
		return nullptr;
	}

	for (gentity_t *candidate : active_players()) {
		if (candidate == bot ||
			!Bot_PerceptionEntityAlive(candidate) ||
			(candidate->flags & FL_NOTARGET)) {
			continue;
		}
		if (!Teams() || !OnSameTeam(bot, candidate)) {
			return candidate;
		}
	}
	return nullptr;
}

bool Bot_CommandFriendlyCandidateInLineOfFire(
	gentity_t *shooter,
	gentity_t *target,
	gentity_t *candidate) {
	if (shooter == nullptr ||
		target == nullptr ||
		candidate == nullptr ||
		candidate == shooter ||
		candidate == target ||
		!Teams() ||
		!Bot_PerceptionEntityAlive(candidate) ||
		!OnSameTeam(shooter, candidate)) {
		return false;
	}

	const Vector3 toTarget = target->s.origin - shooter->s.origin;
	const float targetDistanceSquared = toTarget.lengthSquared();
	if (targetDistanceSquared < 1.0f) {
		return false;
	}

	const Vector3 toCandidate = candidate->s.origin - shooter->s.origin;
	const float candidateAlong =
		toCandidate.dot(toTarget) / targetDistanceSquared;
	if (candidateAlong <= 0.05f || candidateAlong >= 0.95f) {
		return false;
	}

	const Vector3 closestPoint = shooter->s.origin + (toTarget * candidateAlong);
	const float lateralDistanceSquared =
		(candidate->s.origin - closestPoint).lengthSquared();
	if (lateralDistanceSquared > BOT_COMMAND_TEAM_FIRE_LINE_RADIUS_SQUARED) {
		return false;
	}

	return visible(shooter, candidate) || CanDamage(candidate, shooter);
}

bool Bot_CommandFriendlyInLineOfFire(gentity_t *shooter, gentity_t *target) {
	if (shooter == nullptr || target == nullptr || !Teams()) {
		return false;
	}

	for (gentity_t *candidate : active_players()) {
		if (Bot_CommandFriendlyCandidateInLineOfFire(shooter, target, candidate)) {
			return true;
		}
	}
	return false;
}

bool Bot_CommandSmokeTeamFireLineOfFireProof(
	gentity_t *shooter,
	gentity_t *target) {
	const int smokeMode = Bot_CommandSmokeScenarioMode();
	if ((smokeMode != 34 && smokeMode != 44 && smokeMode != 52) ||
		!Bot_CommandTeamFireAvoidanceEnabled() ||
		shooter == nullptr ||
		target == nullptr ||
		shooter->client == nullptr ||
		target->client == nullptr ||
		!Teams() ||
		OnSameTeam(shooter, target)) {
		return false;
	}

	for (gentity_t *candidate : active_players()) {
		if (candidate != shooter &&
			candidate != target &&
			Bot_PerceptionEntityAlive(candidate) &&
			!(candidate->flags & FL_NOTARGET) &&
			OnSameTeam(shooter, candidate)) {
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
	const BotCombatEnemyFacts &facts,
	const BotCommandSmokeProofSlot *slot = nullptr) {
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
	if (Bot_CommandSmokeAimFirePolicy()) {
		static constexpr int reactionDelayMilliseconds = 250;
		static constexpr int aimSettleMilliseconds = 150;
		static constexpr int reactionStageMilliseconds = 320;
		static constexpr int settleStageMilliseconds = 620;
		static constexpr int cooldownStageMilliseconds = 900;
		const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
		const int startMilliseconds =
			slot != nullptr && slot->aimFirePolicyStartTimeMilliseconds > 0 ?
				slot->aimFirePolicyStartTimeMilliseconds :
				nowMilliseconds;
		const int elapsedMilliseconds =
			Bot_CommandElapsedMilliseconds(nowMilliseconds, startMilliseconds);

		frame.skill = 3;
		frame.reactionDelayMilliseconds = reactionDelayMilliseconds;
		frame.fieldOfViewDegrees = BOT_COMMAND_AIM_FIELD_OF_VIEW_DEGREES;
		frame.yawDeltaDegrees = 0;
		frame.pitchDeltaDegrees = 0;
		frame.burstShotsFired = 0;
		frame.burstCooldownRemainingMilliseconds = 0;
		if (elapsedMilliseconds < reactionStageMilliseconds) {
			frame.targetVisibleMilliseconds =
				std::max(0, reactionDelayMilliseconds / 2);
			frame.targetTrackedMilliseconds = frame.targetVisibleMilliseconds;
			frame.aimSettledMilliseconds = 0;
		} else if (elapsedMilliseconds < settleStageMilliseconds) {
			frame.targetVisibleMilliseconds = reactionDelayMilliseconds + 100;
			frame.targetTrackedMilliseconds = frame.targetVisibleMilliseconds;
			frame.aimSettledMilliseconds =
				std::max(0, aimSettleMilliseconds / 3);
		} else if (elapsedMilliseconds < cooldownStageMilliseconds) {
			frame.targetVisibleMilliseconds =
				reactionDelayMilliseconds + elapsedMilliseconds;
			frame.targetTrackedMilliseconds = frame.targetVisibleMilliseconds;
			frame.aimSettledMilliseconds =
				aimSettleMilliseconds + elapsedMilliseconds;
			frame.burstCooldownRemainingMilliseconds =
				std::max(1, cooldownStageMilliseconds - elapsedMilliseconds);
		} else {
			frame.targetVisibleMilliseconds =
				reactionDelayMilliseconds + elapsedMilliseconds;
			frame.targetTrackedMilliseconds = frame.targetVisibleMilliseconds;
			frame.aimSettledMilliseconds =
				aimSettleMilliseconds + elapsedMilliseconds;
		}
		if (!facts.valid || !facts.visible) {
			frame.targetInFieldOfView = false;
			frame.targetVisibleMilliseconds = 0;
			frame.targetTrackedMilliseconds = 0;
			frame.aimSettledMilliseconds = 0;
		}
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
	if (Bot_CommandSmokeAimFirePolicy()) {
		context.currentWeaponItem = IT_WEAPON_RLAUNCHER;
		context.preferredWeaponItem = IT_WEAPON_RLAUNCHER;
		context.currentWeaponAmmo =
			bot != nullptr && bot->client != nullptr ?
				bot->client->pers.inventory[IT_AMMO_ROCKETS] :
				context.currentWeaponAmmo;
		context.currentWeaponReady = true;
	}
	BotCommandSmokeProofSlot *slot = Bot_CommandSmokeProofSlotFor(bot);
	context.aimPolicy = Bot_CommandBuildSmokeAimPolicyFrame(facts, slot);

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
		BotCommandSmokeProofSlot *slot = Bot_CommandSmokeProofSlotFor(bot);
		context->combat.aimPolicy = Bot_CommandBuildSmokeAimPolicyFrame(facts, slot);
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
	if (Bot_CommandCurrentWeaponItem(bot) <= IT_NULL ||
		slot.mode == 20 ||
		slot.mode == 72) {
		Bot_CommandSetCurrentWeapon(bot, IT_WEAPON_MACHINEGUN);
	}
	slot.combatPrepared = true;
}

void Bot_CommandPrepareTargetMemorySmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	Bot_CommandPrepareCombatSmoke(bot, slot);
	gentity_t *peer = Bot_CommandFindSmokePeer(bot);
	(void)Bot_CommandTryPlaceSmokePeer(bot, peer);
	botBrainBlackboardStatus.combatEnemyMemorySmokeSeedAttempts++;

	if (peer == nullptr) {
		botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoPeer++;
	} else if (!Bot_PerceptionCombatTargetAlive(peer)) {
		botBrainBlackboardStatus.combatEnemyMemorySmokeSeedInvalidPeer++;
	} else if (!slot.targetMemorySeeded) {
		int clientIndex = -1;
		BotBrainBlackboardSlot *blackboardSlot =
			Bot_BlackboardEnsureSlot(bot, &clientIndex);
		if (blackboardSlot == nullptr) {
			botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoBlackboard++;
		} else {
			BotPerceptionEnemyFacts facts{};
			facts.entity = peer;
			facts.entityNumber = Bot_PerceptionEntityNumber(peer);
			facts.clientIndex = Bot_PerceptionEntityClientIndex(peer);
			facts.spawnCount = peer->spawn_count;
			facts.health = Bot_PerceptionClampVital(peer->health);
			facts.armor = Bot_PerceptionArmorValue(peer);
			const Vector3 delta = peer->s.origin - bot->s.origin;
			facts.distanceSquared =
				Bot_PerceptionClampDistanceSquared(delta.lengthSquared());
			facts.visible = true;
			facts.shootable = true;
			if (!Bot_PerceptionEnemyFactsValid(facts)) {
				botBrainBlackboardStatus.combatEnemyMemorySmokeSeedInvalidPeer++;
				return;
			}
			Bot_PerceptionSetCurrentEnemy(*blackboardSlot, facts, true);
			slot.targetMemorySeeded = true;
			botBrainBlackboardStatus.combatEnemyMemorySmokeSeeded++;
		}
	}

	Bot_PerceptionUpdateBlackboard(bot);
}

void Bot_CommandPrepareCoopTargetShareSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!slot.combatPrepared) {
		Bot_CommandGrantWeapon(bot, IT_WEAPON_MACHINEGUN, IT_AMMO_BULLETS, 200);
		Bot_CommandSetCurrentWeapon(bot, IT_WEAPON_MACHINEGUN);
		slot.combatPrepared = true;
	}

	gentity_t *target = Bot_CommandEnsureCoopTargetShareSmokeTarget(bot);
	if (target == nullptr) {
		return;
	}
	slot.coopTargetSharePrepared = true;

	int clientIndex = -1;
	BotBrainBlackboardSlot *blackboardSlot =
		Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (blackboardSlot == nullptr || clientIndex != 0 || slot.coopTargetShareSeeded) {
		return;
	}

	BotPerceptionEnemyFacts facts = Bot_PerceptionEvaluateEnemy(bot, target);
	if (!Bot_PerceptionEnemyFactsValid(facts)) {
		return;
	}

	Bot_PerceptionSetCurrentEnemy(*blackboardSlot, facts, true);
	slot.coopTargetShareSeeded = true;
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

void Bot_CommandSetAmmoToMax(gclient_t *client, item_id_t ammoItem) {
	Item *item = Bot_CommandItemForId(ammoItem);
	if (client == nullptr ||
		item == nullptr ||
		item->tag < static_cast<int>(AmmoID::Bullets) ||
		item->tag >= static_cast<int>(AmmoID::_Total)) {
		return;
	}

	const int maxAmmo = client->pers.ammoMax[static_cast<size_t>(item->tag)];
	client->pers.inventory[ammoItem] = std::max(client->pers.inventory[ammoItem], maxAmmo);
}

void Bot_CommandPrepareAmmoPressureSmoke(gentity_t *bot, BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex != 0) {
		return;
	}

	gclient_t *client = bot->client;
	static constexpr item_id_t ammoItems[] = {
		IT_AMMO_BULLETS,
		IT_AMMO_CELLS,
		IT_AMMO_ROCKETS,
		IT_AMMO_SLUGS,
		IT_AMMO_MAGSLUG,
		IT_AMMO_FLECHETTES,
		IT_AMMO_PROX,
		IT_AMMO_NUKE,
		IT_AMMO_ROUNDS,
		IT_AMMO_GRENADES,
		IT_AMMO_TRAP,
		IT_AMMO_TESLA,
	};
	for (const item_id_t item : ammoItems) {
		Bot_CommandSetAmmoToMax(client, item);
	}

	Bot_CommandGrantWeapon(bot, IT_WEAPON_SHOTGUN, IT_AMMO_SHELLS, 1);
	client->pers.inventory[IT_AMMO_SHELLS] = 0;
	Bot_CommandSetCurrentWeapon(bot, IT_WEAPON_SHOTGUN);

	if (!slot.ammoPressurePrepared) {
		slot.ammoPressurePrepared = Bot_CommandEnsureAmmoPressureSmokeTarget(bot);
		if (slot.ammoPressurePrepared) {
			BotNav_ResetClient(clientIndex);
		}
	}
}

void Bot_CommandPrepareSurvivalInventorySmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr || slot.survivalInventoryPrepared) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex != 0) {
		return;
	}

	gclient_t *client = bot->client;
	Bot_CommandClearArmor(client);
	bot->flags &= ~(FL_POWER_ARMOR | FL_WANTS_POWER_ARMOR);

	bot->maxHealth = std::max(bot->maxHealth, 100);
	client->pers.maxHealth = std::max(client->pers.maxHealth, 100);
	bot->health = std::min(25, bot->maxHealth);
	client->pers.health = bot->health;

	client->pers.inventory[IT_POWER_SHIELD] = 1;
	client->pers.inventory[IT_AMMO_CELLS] =
		std::max(client->pers.inventory[IT_AMMO_CELLS], 50);
	client->pers.selectedItem = IT_POWER_SHIELD;
	slot.survivalInventoryPrepared = true;
}

void Bot_CommandPrepareSurvivalRouteSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex != 0) {
		return;
	}

	gclient_t *client = bot->client;
	Bot_CommandClearArmor(client);
	bot->flags &= ~(FL_POWER_ARMOR | FL_WANTS_POWER_ARMOR);

	const bool armorRoute = slot.mode == 70 || Bot_CommandSmokeSurvivalArmorRoute();
	const item_id_t routeItem = armorRoute ? IT_ARMOR_JACKET : IT_HEALTH_MEDIUM;

	bot->maxHealth = std::max(bot->maxHealth, 100);
	client->pers.maxHealth = std::max(client->pers.maxHealth, 100);
	bot->health = armorRoute ? bot->maxHealth : std::min(20, bot->maxHealth);
	client->pers.health = bot->health;
	client->pers.healthBonus = 0;

	if (!slot.survivalRoutePrepared) {
		slot.survivalRoutePrepared =
			Bot_CommandEnsureSurvivalRouteSmokeTarget(bot, routeItem);
		if (slot.survivalRoutePrepared) {
			BotNav_ResetClient(clientIndex);
		}
	}
}

void Bot_CommandPrepareThreatRetreatSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex != 0) {
		return;
	}

	Bot_CommandPrepareCombatSmoke(bot, slot);

	gclient_t *client = bot->client;
	Bot_CommandClearArmor(client);
	bot->flags &= ~(FL_POWER_ARMOR | FL_WANTS_POWER_ARMOR);
	bot->maxHealth = std::max(bot->maxHealth, 100);
	client->pers.maxHealth = std::max(client->pers.maxHealth, 100);
	bot->health = std::min(BOT_COMMAND_THREAT_RETREAT_LOW_HEALTH, bot->maxHealth);
	client->pers.health = bot->health;
	client->pers.healthBonus = 0;

	if (!slot.threatRetreatPrepared) {
		gentity_t *peer = Bot_CommandFindSmokePeer(bot);
		slot.threatRetreatPrepared = Bot_CommandTryPlaceSmokePeer(bot, peer);
		if (slot.threatRetreatPrepared) {
			BotNav_ResetClient(clientIndex);
		}
	}
}

void Bot_CommandPrepareWeaponScoringSmoke(gentity_t *bot, BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr || slot.weaponScoringPrepared) {
		return;
	}

	gclient_t *client = bot->client;
	Bot_CommandGrantWeapon(bot, IT_WEAPON_RLAUNCHER, IT_AMMO_ROCKETS, 6);
	Bot_CommandGrantWeapon(bot, IT_WEAPON_SSHOTGUN, IT_AMMO_SHELLS, 12);
	client->pers.inventory[IT_WEAPON_RAILGUN] =
		std::max(client->pers.inventory[IT_WEAPON_RAILGUN], 1);
	client->pers.inventory[IT_AMMO_SLUGS] = 0;
	client->pers.inventory[IT_WEAPON_BFG] =
		std::max(client->pers.inventory[IT_WEAPON_BFG], 1);
	client->pers.inventory[IT_AMMO_CELLS] = 10;
	Bot_CommandSetCurrentWeapon(bot, IT_WEAPON_RLAUNCHER);

	gentity_t *peer = Bot_CommandFindSmokePeer(bot);
	(void)Bot_CommandPlaceWeaponScoringSmokePeer(bot, peer);
	if (peer != nullptr && peer->client != nullptr) {
		static constexpr int proofHealth = 35;
		peer->takeDamage = false;
		peer->health = proofHealth;
		peer->maxHealth = 100;
		peer->client->pers.health = proofHealth;
		peer->client->pers.maxHealth = 100;
		Bot_CommandClearArmor(peer->client);
	}

	slot.weaponScoringPrepared = true;
}

void Bot_CommandPrepareAimFirePolicySmoke(gentity_t *bot, BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex != 0) {
		return;
	}

	gclient_t *client = bot->client;
	static constexpr item_id_t nonRocketWeapons[] = {
		IT_WEAPON_GRAPPLE,
		IT_WEAPON_BLASTER,
		IT_WEAPON_CHAINFIST,
		IT_WEAPON_SHOTGUN,
		IT_WEAPON_SSHOTGUN,
		IT_WEAPON_MACHINEGUN,
		IT_WEAPON_ETF_RIFLE,
		IT_WEAPON_CHAINGUN,
		IT_AMMO_GRENADES,
		IT_AMMO_TRAP,
		IT_AMMO_TESLA,
		IT_WEAPON_GLAUNCHER,
		IT_WEAPON_PROXLAUNCHER,
		IT_WEAPON_HYPERBLASTER,
		IT_WEAPON_IONRIPPER,
		IT_WEAPON_PLASMAGUN,
		IT_WEAPON_PLASMABEAM,
		IT_WEAPON_THUNDERBOLT,
		IT_WEAPON_RAILGUN,
		IT_WEAPON_PHALANX,
		IT_WEAPON_BFG,
		IT_WEAPON_DISRUPTOR,
	};
	for (const item_id_t item : nonRocketWeapons) {
		client->pers.inventory[item] = 0;
	}
	static constexpr item_id_t nonRocketAmmo[] = {
		IT_AMMO_SHELLS,
		IT_AMMO_BULLETS,
		IT_AMMO_CELLS,
		IT_AMMO_SLUGS,
		IT_AMMO_MAGSLUG,
		IT_AMMO_FLECHETTES,
		IT_AMMO_PROX,
		IT_AMMO_NUKE,
		IT_AMMO_ROUNDS,
	};
	for (const item_id_t item : nonRocketAmmo) {
		client->pers.inventory[item] = 0;
	}
	Bot_CommandGrantWeapon(bot, IT_WEAPON_RLAUNCHER, IT_AMMO_ROCKETS, 24);
	Bot_CommandSetCurrentWeapon(bot, IT_WEAPON_RLAUNCHER);

	if (!slot.aimFirePolicyPrepared) {
		slot.aimFirePolicyStartTimeMilliseconds = Bot_CommandCurrentTimeMilliseconds();
		slot.aimFirePolicyPrepared = true;
	}

	gentity_t *peer = Bot_CommandFindSmokePeer(bot);
	(void)Bot_CommandPlaceAimFirePolicySmokePeer(bot, peer);
	if (peer != nullptr && peer->client != nullptr) {
		static constexpr int proofHealth = 5000;
		peer->takeDamage = true;
		peer->health = proofHealth;
		peer->maxHealth = proofHealth;
		peer->client->pers.health = proofHealth;
		peer->client->pers.maxHealth = proofHealth;
		Bot_CommandClearArmor(peer->client);
	}
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

void Bot_CommandPrepareTeamFireAvoidanceSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	Bot_CommandPrepareCombatSmoke(bot, slot);
	slot.teamFireAvoidancePrepared = true;
}

void Bot_CommandPrepareTeamRoleCombatSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	Bot_CommandPrepareCombatSmoke(bot, slot);

	if (slot.teamRoleCombatPrepared) {
		return;
	}

	gentity_t *enemy = Bot_CommandFindSmokeEnemy(bot);
	if (enemy == nullptr) {
		return;
	}

	slot.teamRoleCombatPrepared = Bot_CommandTryPlaceSmokePeer(bot, enemy);
}

void Bot_CommandPrepareFfaSpawnCampAvoidanceSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	Bot_CommandPrepareCombatSmoke(bot, slot);
	if (slot.ffaSpawnCampAvoidancePrepared) {
		return;
	}

	gentity_t *peer = Bot_CommandFindSmokePeer(bot);
	if (peer == nullptr) {
		return;
	}

	slot.ffaSpawnCampAvoidancePrepared = Bot_CommandTryPlaceSmokePeer(bot, peer);
}

void Bot_CommandPrepareFfaRoleCombatSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	Bot_CommandPrepareCombatSmoke(bot, slot);
	if (slot.ffaRoleCombatPrepared) {
		return;
	}

	gentity_t *peer = Bot_CommandFindSmokePeer(bot);
	if (peer == nullptr) {
		return;
	}

	slot.ffaRoleCombatPrepared = Bot_CommandTryPlaceSmokePeer(bot, peer);
}

void Bot_CommandPrepareCtfRoleCombatSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	Bot_CommandPrepareCombatSmoke(bot, slot);

	if (slot.ctfRoleCombatPrepared) {
		return;
	}

	gentity_t *enemy = Bot_CommandFindSmokeEnemy(bot);
	if (enemy == nullptr) {
		return;
	}

	slot.ctfRoleCombatPrepared = Bot_CommandTryPlaceSmokePeer(bot, enemy);
}

void Bot_CommandPrepareCtfDroppedFlagRouteSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	if (!slot.ctfDroppedFlagRoutePrepared) {
		slot.ctfDroppedFlagRoutePrepared =
			Bot_CommandEnsureCtfDroppedFlagSmokeTargets(bot);
	}
}

void Bot_CommandPrepareCtfCarrierSupportRouteSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!Bot_CommandCtfCarrierSupportSmokeReady()) {
		return;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex >= 0 && clientIndex < 2) {
		// Seed one carrier per team without creating reciprocal flag-carrier pairs.
		return;
	}

	if (slot.ctfCarrierSupportRoutePrepared) {
		return;
	}

	gentity_t *carrier = Bot_CommandFindSmokeTeammate(bot);
	if (carrier == nullptr || carrier->client == nullptr) {
		return;
	}

	const int flagItem = BotObjectives_EnemyFlagItemForTeam(
		static_cast<int>(bot->client->sess.team));
	if (!Bot_CommandItemIdValid(flagItem)) {
		return;
	}

	carrier->client->pers.inventory[IT_FLAG_RED] = 0;
	carrier->client->pers.inventory[IT_FLAG_BLUE] = 0;
	carrier->client->pers.inventory[IT_FLAG_NEUTRAL] = 0;
	carrier->client->pers.inventory[flagItem] = 1;
	Bot_CommandStabilizeCtfRouteSmokeCarrier(carrier);
	slot.ctfCarrierSupportRoutePrepared = true;
}

void Bot_CommandPrepareCtfBaseReturnRouteSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!Bot_CommandCtfBaseReturnSmokeReady()) {
		return;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	if (slot.ctfBaseReturnRoutePrepared) {
		return;
	}

	gentity_t *carrier = Bot_CommandFindSmokeEnemy(bot);
	if (carrier == nullptr || carrier->client == nullptr) {
		return;
	}

	const int flagItem = BotObjectives_OwnFlagItemForTeam(
		static_cast<int>(bot->client->sess.team));
	if (!Bot_CommandItemIdValid(flagItem)) {
		return;
	}

	carrier->client->pers.inventory[IT_FLAG_RED] = 0;
	carrier->client->pers.inventory[IT_FLAG_BLUE] = 0;
	carrier->client->pers.inventory[IT_FLAG_NEUTRAL] = 0;
	carrier->client->pers.inventory[flagItem] = 1;
	Bot_CommandStabilizeCtfRouteSmokeCarrier(carrier);
	slot.ctfBaseReturnRoutePrepared = true;
}

void Bot_CommandPrepareCtfObjectiveRouteSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (bot == nullptr || bot->client == nullptr) {
		return;
	}

	if (!Bot_CommandCtfObjectiveRouteSmokeReady()) {
		return;
	}

	if (slot.ctfObjectiveRoutePrepared) {
		return;
	}

	if (!slot.objectiveTeamPrepared) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		slot.objectiveTeamPrepared = true;
	}

	if (Bot_PerceptionClientIndex(bot) != 0) {
		return;
	}

	Bot_CommandPrepareCtfDroppedFlagRouteSmoke(bot, slot);
	Bot_CommandPrepareCtfBaseReturnRouteSmoke(bot, slot);

	slot.ctfObjectiveRoutePrepared =
		slot.ctfDroppedFlagRoutePrepared &&
		slot.ctfBaseReturnRoutePrepared;
}

void Bot_CommandPrepareCtfObjectiveTransitionsSmoke(
	gentity_t *bot,
	BotCommandSmokeProofSlot &slot) {
	if (!Bot_CommandPrepareCtfObjectiveTransitionEvents(bot, slot)) {
		return;
	}

	Bot_CommandPrepareCtfObjectiveRouteSmoke(bot, slot);
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
	case 85:
		Bot_CommandRecordHealthArmorProof(bot, *slot);
		if (bot->client != nullptr) {
			const int restoredHealth =
				std::max(1, std::max(bot->maxHealth, bot->client->pers.maxHealth));
			bot->health = restoredHealth;
			bot->client->pers.health = restoredHealth;
		}
		break;
	case 23:
		if (!slot->objectiveTeamPrepared) {
			Bot_CommandPrepareTeamObjectiveSmokeTeams();
			slot->objectiveTeamPrepared = true;
		}
		break;
	case 30:
		Bot_CommandPrepareCoopTargetShareSmoke(bot, *slot);
		break;
	case 78:
		Bot_CommandPrepareCoopTargetShareSmoke(bot, *slot);
		break;
	case 34:
		Bot_CommandPrepareTeamFireAvoidanceSmoke(bot, *slot);
		break;
	case 43:
		Bot_CommandPrepareTeamRoleCombatSmoke(bot, *slot);
		break;
	case 44:
		Bot_CommandPrepareTeamRoleCombatSmoke(bot, *slot);
		Bot_CommandPrepareTeamFireAvoidanceSmoke(bot, *slot);
		break;
	case 52:
	case 63:
		Bot_CommandPrepareTeamRoleCombatSmoke(bot, *slot);
		Bot_CommandPrepareTeamFireAvoidanceSmoke(bot, *slot);
		break;
	case 64:
		Bot_CommandPrepareTargetMemorySmoke(bot, *slot);
		break;
	case 65:
		Bot_CommandPrepareWeaponScoringSmoke(bot, *slot);
		break;
	case 66:
		Bot_CommandPrepareAimFirePolicySmoke(bot, *slot);
		break;
	case 67:
		Bot_CommandPrepareAmmoPressureSmoke(bot, *slot);
		break;
	case 68:
		Bot_CommandPrepareSurvivalInventorySmoke(bot, *slot);
		break;
	case 69:
		Bot_CommandPrepareSurvivalRouteSmoke(bot, *slot);
		break;
	case 70:
		Bot_CommandPrepareSurvivalRouteSmoke(bot, *slot);
		break;
	case 71:
		Bot_CommandPrepareCombatSmoke(bot, *slot);
		Bot_CommandPrepareSurvivalRouteSmoke(bot, *slot);
		if (Bot_PerceptionClientIndex(bot) == 0) {
			(void)Bot_CommandTryPlaceSmokePeer(
				bot,
				Bot_CommandFindSmokePeer(bot));
		}
		break;
	case 72:
		Bot_CommandPrepareThreatRetreatSmoke(bot, *slot);
		break;
	case 45:
		Bot_CommandPrepareFfaSpawnCampAvoidanceSmoke(bot, *slot);
		break;
	case 48:
		Bot_CommandPrepareFfaRoleCombatSmoke(bot, *slot);
		break;
	case 49:
		Bot_CommandPrepareFfaRoleCombatSmoke(bot, *slot);
		Bot_CommandPrepareFfaSpawnCampAvoidanceSmoke(bot, *slot);
		break;
	case 74:
		Bot_CommandPrepareFfaRoleCombatSmoke(bot, *slot);
		Bot_CommandPrepareFfaSpawnCampAvoidanceSmoke(bot, *slot);
		break;
	case 75:
		Bot_CommandPrepareFfaRoleCombatSmoke(bot, *slot);
		Bot_CommandPrepareFfaSpawnCampAvoidanceSmoke(bot, *slot);
		break;
	case 55:
		if (!slot->healthArmorPrepared) {
			BotItemHealthArmorProofSetup setup{};
			(void)BotItems_ApplyHealthArmorProofSetup(bot, &setup);
			slot->healthArmorPrepared = setup.applied;
		}
		break;
	case 36:
		Bot_CommandPrepareCtfRoleCombatSmoke(bot, *slot);
		break;
	case 37:
		Bot_CommandPrepareCtfDroppedFlagRouteSmoke(bot, *slot);
		break;
	case 38:
		Bot_CommandPrepareCtfCarrierSupportRouteSmoke(bot, *slot);
		break;
	case 39:
		Bot_CommandPrepareCtfBaseReturnRouteSmoke(bot, *slot);
		break;
	case 40:
		Bot_CommandPrepareCtfObjectiveRouteSmoke(bot, *slot);
		break;
	case 76:
		Bot_CommandPrepareCtfObjectiveTransitionsSmoke(bot, *slot);
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

int Bot_CommandDistanceSquaredToObjectiveGoal(
	const gentity_t *bot,
	const BotObjectiveRouteGoal &goal) {
	if (bot == nullptr || !goal.valid) {
		return 0;
	}

	const Vector3 goalOrigin = {
		goal.origin[0],
		goal.origin[1],
		goal.origin[2],
	};
	return Bot_PerceptionClampDistanceSquared(
		(bot->s.origin - goalOrigin).lengthSquared());
}

bool Bot_CommandCtfDroppedFlagRouteAssignmentValid(
	const BotObjectiveAssignment &assignment) {
	return assignment.assigned &&
		assignment.type == BotObjectiveType::EnemyFlagPickup &&
		assignment.source == BotObjectiveTargetSource::DroppedFlagEntity &&
		assignment.lane == BotObjectiveLane::DroppedFlagResponse &&
		assignment.entity > static_cast<int>(game.maxClients) &&
		assignment.area > 0;
}

void Bot_CommandRecordCtfDroppedFlagRouteLast(
	int clientIndex,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	int distanceSquared) {
	botFrameCommandStatus.lastCtfDroppedFlagRouteClient = clientIndex;
	botFrameCommandStatus.lastCtfDroppedFlagRouteRole =
		static_cast<int>(assignment.role);
	botFrameCommandStatus.lastCtfDroppedFlagRouteLane =
		static_cast<int>(assignment.lane);
	botFrameCommandStatus.lastCtfDroppedFlagRouteType =
		static_cast<int>(assignment.type);
	botFrameCommandStatus.lastCtfDroppedFlagRouteSource =
		static_cast<int>(assignment.source);
	botFrameCommandStatus.lastCtfDroppedFlagRouteEntity = assignment.entity;
	botFrameCommandStatus.lastCtfDroppedFlagRouteItem = assignment.item;
	botFrameCommandStatus.lastCtfDroppedFlagRoutePriority = assignment.priority;
	botFrameCommandStatus.lastCtfDroppedFlagRouteGoalDistanceSquared =
		goal.valid ? distanceSquared : 0;
}

bool Bot_CommandBuildCtfDroppedFlagRoute(
	gentity_t *bot,
	BotNavRouteRequest *request,
	BotObjectiveAssignment *assignment,
	BotObjectiveRouteGoal *goal) {
	if (!Bot_CommandCtfDroppedFlagRouteEnabled()) {
		return false;
	}

	botFrameCommandStatus.ctfDroppedFlagRouteRequests++;

	int clientIndex = -1;
	(void)Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (bot == nullptr ||
		bot->client == nullptr ||
		request == nullptr ||
		assignment == nullptr ||
		goal == nullptr) {
		botFrameCommandStatus.ctfDroppedFlagRouteInvalidSkips++;
		return false;
	}

	if (Bot_CommandSmokeScenarioMode() == 37) {
		Bot_CommandPrepareTeamObjectiveSmokeTeams();
		(void)Bot_CommandEnsureCtfDroppedFlagSmokeTargets(bot);
	}

	if (request->hasTravelTypeGoal || request->hasPositionGoal) {
		botFrameCommandStatus.ctfDroppedFlagRouteInvalidSkips++;
		return false;
	}

	*assignment = {};
	*goal = {};
	*assignment = BotObjectives_AssignEnemyFlagObjective(
		bot,
		true,
		BotObjectiveRole::Attacker,
		false);
	if (!Bot_CommandCtfDroppedFlagRouteAssignmentValid(*assignment) ||
		!BotObjectives_BuildRouteGoal(*assignment, goal)) {
		botFrameCommandStatus.ctfDroppedFlagRouteInvalidSkips++;
		Bot_CommandRecordCtfDroppedFlagRouteLast(clientIndex, *assignment, *goal, 0);
		return false;
	}

	request->hasPositionGoal = true;
	request->positionGoal[0] = goal->origin[0];
	request->positionGoal[1] = goal->origin[1];
	request->positionGoal[2] = goal->origin[2];

	botFrameCommandStatus.ctfDroppedFlagRouteAssignments++;
	botFrameCommandStatus.ctfDroppedFlagRouteRouteRequests++;
	BotObjectives_RecordRouteRequest(*goal, *assignment);
	Bot_CommandRecordCtfDroppedFlagRouteLast(
		clientIndex,
		*assignment,
		*goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, *goal));
	return true;
}

void Bot_CommandRecordCtfDroppedFlagRouteResult(
	gentity_t *bot,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	bool routeSucceeded) {
	if (!routeSucceeded ||
		!Bot_CommandCtfDroppedFlagRouteAssignmentValid(assignment) ||
		!goal.valid) {
		return;
	}

	botFrameCommandStatus.ctfDroppedFlagRouteRouteCommands++;
	BotObjectives_RecordRouteCommand(goal, assignment);
	BotObjectives_RecordReach(goal, assignment);
	Bot_CommandRecordCtfDroppedFlagRouteLast(
		Bot_PerceptionClientIndex(bot),
		assignment,
		goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, goal));
}

bool Bot_CommandCtfCarrierSupportRouteAssignmentValid(
	const BotObjectiveAssignment &assignment) {
	return assignment.assigned &&
		assignment.type == BotObjectiveType::EnemyFlagPickup &&
		assignment.role == BotObjectiveRole::Support &&
		assignment.source == BotObjectiveTargetSource::FlagCarrier &&
		assignment.lane == BotObjectiveLane::CarrierSupport &&
		assignment.carrierClient >= 0 &&
		assignment.entity > 0 &&
		assignment.entity <= static_cast<int>(game.maxClients) &&
		assignment.area > 0;
}

void Bot_CommandRecordCtfCarrierSupportRouteLast(
	int clientIndex,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	int distanceSquared) {
	botFrameCommandStatus.lastCtfCarrierSupportRouteClient = clientIndex;
	botFrameCommandStatus.lastCtfCarrierSupportRouteRole =
		static_cast<int>(assignment.role);
	botFrameCommandStatus.lastCtfCarrierSupportRouteLane =
		static_cast<int>(assignment.lane);
	botFrameCommandStatus.lastCtfCarrierSupportRouteType =
		static_cast<int>(assignment.type);
	botFrameCommandStatus.lastCtfCarrierSupportRouteSource =
		static_cast<int>(assignment.source);
	botFrameCommandStatus.lastCtfCarrierSupportRouteEntity = assignment.entity;
	botFrameCommandStatus.lastCtfCarrierSupportRouteCarrierClient =
		assignment.carrierClient;
	botFrameCommandStatus.lastCtfCarrierSupportRouteItem = assignment.item;
	botFrameCommandStatus.lastCtfCarrierSupportRoutePriority = assignment.priority;
	botFrameCommandStatus.lastCtfCarrierSupportRouteGoalDistanceSquared =
		goal.valid ? distanceSquared : 0;
}

bool Bot_CommandBuildCtfCarrierSupportRoute(
	gentity_t *bot,
	BotNavRouteRequest *request,
	BotObjectiveAssignment *assignment,
	BotObjectiveRouteGoal *goal) {
	if (!Bot_CommandCtfCarrierSupportRouteEnabled()) {
		return false;
	}

	if (bot == nullptr ||
		bot->client == nullptr ||
		request == nullptr ||
		assignment == nullptr ||
		goal == nullptr) {
		botFrameCommandStatus.ctfCarrierSupportRouteInvalidSkips++;
		return false;
	}

	if (!Bot_CommandCtfCarrierSupportSmokeReady()) {
		return false;
	}

	int clientIndex = -1;
	BotCommandSmokeProofSlot *slot = Bot_CommandSmokeProofSlotFor(bot);
	(void)Bot_BlackboardEnsureSlot(bot, &clientIndex);

	if (Bot_CommandSmokeScenarioMode() == 38 && clientIndex >= 0 && clientIndex < 2) {
		return false;
	}

	botFrameCommandStatus.ctfCarrierSupportRouteRequests++;

	if (Bot_CommandSmokeScenarioMode() == 38 && slot != nullptr) {
		Bot_CommandPrepareCtfCarrierSupportRouteSmoke(bot, *slot);
	}

	if (request->hasTravelTypeGoal || request->hasPositionGoal) {
		botFrameCommandStatus.ctfCarrierSupportRouteInvalidSkips++;
		return false;
	}

	*assignment = {};
	*goal = {};
	*assignment = BotObjectives_AssignEnemyFlagCarrierSupportObjective(bot, true);
	if (!Bot_CommandCtfCarrierSupportRouteAssignmentValid(*assignment) ||
		!BotObjectives_BuildRouteGoal(*assignment, goal)) {
		botFrameCommandStatus.ctfCarrierSupportRouteInvalidSkips++;
		Bot_CommandRecordCtfCarrierSupportRouteLast(clientIndex, *assignment, *goal, 0);
		return false;
	}

	request->hasPositionGoal = true;
	request->positionGoal[0] = goal->origin[0];
	request->positionGoal[1] = goal->origin[1];
	request->positionGoal[2] = goal->origin[2];

	botFrameCommandStatus.ctfCarrierSupportRouteAssignments++;
	botFrameCommandStatus.ctfCarrierSupportRouteRouteRequests++;
	BotObjectives_RecordRouteRequest(*goal, *assignment);
	Bot_CommandRecordCtfCarrierSupportRouteLast(
		clientIndex,
		*assignment,
		*goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, *goal));
	return true;
}

void Bot_CommandRecordCtfCarrierSupportRouteResult(
	gentity_t *bot,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	bool routeSucceeded) {
	if (!routeSucceeded ||
		!Bot_CommandCtfCarrierSupportRouteAssignmentValid(assignment) ||
		!goal.valid) {
		return;
	}

	botFrameCommandStatus.ctfCarrierSupportRouteRouteCommands++;
	BotObjectives_RecordRouteCommand(goal, assignment);
	BotObjectives_RecordReach(goal, assignment);
	Bot_CommandRecordCtfCarrierSupportRouteLast(
		Bot_PerceptionClientIndex(bot),
		assignment,
		goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, goal));
}

bool Bot_CommandCtfBaseReturnRouteAssignmentValid(
	const BotObjectiveAssignment &assignment) {
	return assignment.assigned &&
		assignment.type == BotObjectiveType::OwnFlagReturn &&
		assignment.role == BotObjectiveRole::Returner &&
		assignment.source == BotObjectiveTargetSource::FlagCarrier &&
		assignment.lane == BotObjectiveLane::OwnBaseReturn &&
		assignment.carrierClient >= 0 &&
		assignment.entity > 0 &&
		assignment.entity <= static_cast<int>(game.maxClients) &&
		assignment.area > 0;
}

void Bot_CommandRecordCtfBaseReturnRouteLast(
	int clientIndex,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	int distanceSquared) {
	botFrameCommandStatus.lastCtfBaseReturnRouteClient = clientIndex;
	botFrameCommandStatus.lastCtfBaseReturnRouteRole =
		static_cast<int>(assignment.role);
	botFrameCommandStatus.lastCtfBaseReturnRouteLane =
		static_cast<int>(assignment.lane);
	botFrameCommandStatus.lastCtfBaseReturnRouteType =
		static_cast<int>(assignment.type);
	botFrameCommandStatus.lastCtfBaseReturnRouteSource =
		static_cast<int>(assignment.source);
	botFrameCommandStatus.lastCtfBaseReturnRouteEntity = assignment.entity;
	botFrameCommandStatus.lastCtfBaseReturnRouteCarrierClient =
		assignment.carrierClient;
	botFrameCommandStatus.lastCtfBaseReturnRouteItem = assignment.item;
	botFrameCommandStatus.lastCtfBaseReturnRoutePriority = assignment.priority;
	botFrameCommandStatus.lastCtfBaseReturnRouteGoalDistanceSquared =
		goal.valid ? distanceSquared : 0;
}

bool Bot_CommandBuildCtfBaseReturnRoute(
	gentity_t *bot,
	BotNavRouteRequest *request,
	BotObjectiveAssignment *assignment,
	BotObjectiveRouteGoal *goal) {
	if (!Bot_CommandCtfBaseReturnRouteEnabled()) {
		return false;
	}

	if (bot == nullptr ||
		bot->client == nullptr ||
		request == nullptr ||
		assignment == nullptr ||
		goal == nullptr) {
		botFrameCommandStatus.ctfBaseReturnRouteInvalidSkips++;
		return false;
	}

	if (!Bot_CommandCtfBaseReturnSmokeReady()) {
		return false;
	}

	botFrameCommandStatus.ctfBaseReturnRouteRequests++;

	int clientIndex = -1;
	BotCommandSmokeProofSlot *slot = Bot_CommandSmokeProofSlotFor(bot);
	(void)Bot_BlackboardEnsureSlot(bot, &clientIndex);

	if (Bot_CommandSmokeScenarioMode() == 39 && slot != nullptr) {
		Bot_CommandPrepareCtfBaseReturnRouteSmoke(bot, *slot);
	}

	if (request->hasTravelTypeGoal || request->hasPositionGoal) {
		botFrameCommandStatus.ctfBaseReturnRouteInvalidSkips++;
		return false;
	}

	*assignment = {};
	*goal = {};
	*assignment = BotObjectives_AssignOwnFlagReturnObjective(bot, true);
	if (!Bot_CommandCtfBaseReturnRouteAssignmentValid(*assignment) ||
		!BotObjectives_BuildRouteGoal(*assignment, goal)) {
		botFrameCommandStatus.ctfBaseReturnRouteInvalidSkips++;
		Bot_CommandRecordCtfBaseReturnRouteLast(clientIndex, *assignment, *goal, 0);
		return false;
	}

	request->hasPositionGoal = true;
	request->positionGoal[0] = goal->origin[0];
	request->positionGoal[1] = goal->origin[1];
	request->positionGoal[2] = goal->origin[2];

	botFrameCommandStatus.ctfBaseReturnRouteAssignments++;
	botFrameCommandStatus.ctfBaseReturnRouteRouteRequests++;
	BotObjectives_RecordRouteRequest(*goal, *assignment);
	Bot_CommandRecordCtfBaseReturnRouteLast(
		clientIndex,
		*assignment,
		*goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, *goal));
	return true;
}

void Bot_CommandRecordCtfBaseReturnRouteResult(
	gentity_t *bot,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	bool routeSucceeded) {
	if (!routeSucceeded ||
		!Bot_CommandCtfBaseReturnRouteAssignmentValid(assignment) ||
		!goal.valid) {
		return;
	}

	botFrameCommandStatus.ctfBaseReturnRouteRouteCommands++;
	BotObjectives_RecordRouteCommand(goal, assignment);
	BotObjectives_RecordReach(goal, assignment);
	Bot_CommandRecordCtfBaseReturnRouteLast(
		Bot_PerceptionClientIndex(bot),
		assignment,
		goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, goal));
}

const char *Bot_CommandCtfObjectiveRouteSelectionName(
	BotCtfObjectiveRouteSelection selection) {
	switch (selection) {
	case BotCtfObjectiveRouteSelection::BaseReturn:
		return "base_return";
	case BotCtfObjectiveRouteSelection::CarrierSupport:
		return "carrier_support";
	case BotCtfObjectiveRouteSelection::DroppedFlag:
		return "dropped_flag";
	default:
		return "none";
	}
}

bool Bot_CommandCtfObjectiveRouteAssignmentValid(
	BotCtfObjectiveRouteSelection selection,
	const BotObjectiveAssignment &assignment) {
	switch (selection) {
	case BotCtfObjectiveRouteSelection::BaseReturn:
		return Bot_CommandCtfBaseReturnRouteAssignmentValid(assignment);
	case BotCtfObjectiveRouteSelection::CarrierSupport:
		return Bot_CommandCtfCarrierSupportRouteAssignmentValid(assignment);
	case BotCtfObjectiveRouteSelection::DroppedFlag:
		return Bot_CommandCtfDroppedFlagRouteAssignmentValid(assignment);
	default:
		return false;
	}
}

void Bot_CommandRecordCtfObjectiveRouteCandidate(
	BotCtfObjectiveRouteSelection selection) {
	switch (selection) {
	case BotCtfObjectiveRouteSelection::BaseReturn:
		botFrameCommandStatus.ctfObjectiveRouteBaseReturnCandidates++;
		break;
	case BotCtfObjectiveRouteSelection::CarrierSupport:
		botFrameCommandStatus.ctfObjectiveRouteCarrierSupportCandidates++;
		break;
	case BotCtfObjectiveRouteSelection::DroppedFlag:
		botFrameCommandStatus.ctfObjectiveRouteDroppedFlagCandidates++;
		break;
	default:
		break;
	}
}

void Bot_CommandRecordCtfObjectiveRouteSelection(
	BotCtfObjectiveRouteSelection selection) {
	switch (selection) {
	case BotCtfObjectiveRouteSelection::BaseReturn:
		botFrameCommandStatus.ctfObjectiveRouteBaseReturnSelections++;
		break;
	case BotCtfObjectiveRouteSelection::CarrierSupport:
		botFrameCommandStatus.ctfObjectiveRouteCarrierSupportSelections++;
		break;
	case BotCtfObjectiveRouteSelection::DroppedFlag:
		botFrameCommandStatus.ctfObjectiveRouteDroppedFlagSelections++;
		break;
	default:
		break;
	}
}

BotCtfObjectiveRouteCandidate Bot_CommandBuildCtfObjectiveRouteCandidate(
	gentity_t *bot,
	BotCtfObjectiveRouteSelection selection) {
	BotCtfObjectiveRouteCandidate candidate{};
	candidate.selection = selection;

	switch (selection) {
	case BotCtfObjectiveRouteSelection::BaseReturn:
		candidate.assignment = BotObjectives_AssignOwnFlagReturnObjective(bot, true);
		break;
	case BotCtfObjectiveRouteSelection::CarrierSupport:
		candidate.assignment =
			BotObjectives_AssignEnemyFlagCarrierSupportObjective(bot, true);
		break;
	case BotCtfObjectiveRouteSelection::DroppedFlag:
		candidate.assignment = BotObjectives_AssignEnemyFlagObjective(
			bot,
			true,
			BotObjectiveRole::Attacker,
			false);
		break;
	default:
		return candidate;
	}

	if (!Bot_CommandCtfObjectiveRouteAssignmentValid(
			selection,
			candidate.assignment) ||
		!BotObjectives_BuildRouteGoal(candidate.assignment, &candidate.goal)) {
		return candidate;
	}

	candidate.valid = true;
	Bot_CommandRecordCtfObjectiveRouteCandidate(selection);
	return candidate;
}

void Bot_CommandRecordCtfObjectiveRouteLast(
	int clientIndex,
	BotCtfObjectiveRouteSelection selection,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	int distanceSquared) {
	botFrameCommandStatus.lastCtfObjectiveRouteClient = clientIndex;
	botFrameCommandStatus.lastCtfObjectiveRouteSelection =
		static_cast<int>(selection);
	botFrameCommandStatus.lastCtfObjectiveRouteRole =
		static_cast<int>(assignment.role);
	botFrameCommandStatus.lastCtfObjectiveRouteLane =
		static_cast<int>(assignment.lane);
	botFrameCommandStatus.lastCtfObjectiveRouteType =
		static_cast<int>(assignment.type);
	botFrameCommandStatus.lastCtfObjectiveRouteSource =
		static_cast<int>(assignment.source);
	botFrameCommandStatus.lastCtfObjectiveRouteEntity = assignment.entity;
	botFrameCommandStatus.lastCtfObjectiveRouteCarrierClient =
		assignment.carrierClient;
	botFrameCommandStatus.lastCtfObjectiveRouteItem = assignment.item;
	botFrameCommandStatus.lastCtfObjectiveRoutePriority = assignment.priority;
	botFrameCommandStatus.lastCtfObjectiveRouteGoalDistanceSquared =
		goal.valid ? distanceSquared : 0;
}

bool Bot_CommandBuildCtfObjectiveRoute(
	gentity_t *bot,
	BotNavRouteRequest *request,
	BotObjectiveAssignment *assignment,
	BotObjectiveRouteGoal *goal,
	BotCtfObjectiveRouteSelection *selection) {
	if (!Bot_CommandCtfObjectiveRouteEnabled()) {
		return false;
	}

	if (bot == nullptr ||
		bot->client == nullptr ||
		request == nullptr ||
		assignment == nullptr ||
		goal == nullptr ||
		selection == nullptr) {
		botFrameCommandStatus.ctfObjectiveRouteInvalidSkips++;
		return false;
	}

	if (!Bot_CommandCtfObjectiveRouteSmokeReady()) {
		return false;
	}

	botFrameCommandStatus.ctfObjectiveRouteRequests++;

	int clientIndex = -1;
	BotCommandSmokeProofSlot *slot = Bot_CommandSmokeProofSlotFor(bot);
	(void)Bot_BlackboardEnsureSlot(bot, &clientIndex);

	const int smokeMode = Bot_CommandSmokeScenarioMode();
	if ((smokeMode == 40 || smokeMode == 76) && slot != nullptr) {
		Bot_CommandPrepareCtfObjectiveRouteSmoke(bot, *slot);
	}

	if (request->hasTravelTypeGoal || request->hasPositionGoal) {
		botFrameCommandStatus.ctfObjectiveRouteInvalidSkips++;
		return false;
	}

	*assignment = {};
	*goal = {};
	*selection = BotCtfObjectiveRouteSelection::None;

	const BotCtfObjectiveRouteCandidate baseReturn =
		Bot_CommandBuildCtfObjectiveRouteCandidate(
			bot,
			BotCtfObjectiveRouteSelection::BaseReturn);
	const BotCtfObjectiveRouteCandidate carrierSupport =
		Bot_CommandBuildCtfObjectiveRouteCandidate(
			bot,
			BotCtfObjectiveRouteSelection::CarrierSupport);
	const BotCtfObjectiveRouteCandidate droppedFlag =
		Bot_CommandBuildCtfObjectiveRouteCandidate(
			bot,
			BotCtfObjectiveRouteSelection::DroppedFlag);

	const BotCtfObjectiveRouteCandidate *chosen = nullptr;
	if (baseReturn.valid) {
		chosen = &baseReturn;
		if (carrierSupport.valid) {
			botFrameCommandStatus.ctfObjectiveRouteCarrierSupportDeferrals++;
		}
		if (droppedFlag.valid) {
			botFrameCommandStatus.ctfObjectiveRouteDroppedFlagDeferrals++;
		}
	} else if (carrierSupport.valid) {
		chosen = &carrierSupport;
		if (droppedFlag.valid) {
			botFrameCommandStatus.ctfObjectiveRouteDroppedFlagDeferrals++;
		}
	} else if (droppedFlag.valid) {
		chosen = &droppedFlag;
	}
	if (chosen == nullptr) {
		Bot_CommandRecordCtfObjectiveRouteLast(
			clientIndex,
			BotCtfObjectiveRouteSelection::None,
			*assignment,
			*goal,
			0);
		return false;
	}

	*assignment = chosen->assignment;
	*goal = chosen->goal;
	*selection = chosen->selection;
	request->hasPositionGoal = true;
	request->positionGoal[0] = goal->origin[0];
	request->positionGoal[1] = goal->origin[1];
	request->positionGoal[2] = goal->origin[2];

	botFrameCommandStatus.ctfObjectiveRouteAssignments++;
	botFrameCommandStatus.ctfObjectiveRouteRouteRequests++;
	Bot_CommandRecordCtfObjectiveRouteSelection(chosen->selection);
	BotObjectives_RecordRouteRequest(*goal, *assignment);
	Bot_CommandRecordCtfObjectiveRouteLast(
		clientIndex,
		chosen->selection,
		*assignment,
		*goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, *goal));
	return true;
}

void Bot_CommandRecordCtfObjectiveRouteResult(
	gentity_t *bot,
	BotCtfObjectiveRouteSelection selection,
	const BotObjectiveAssignment &assignment,
	const BotObjectiveRouteGoal &goal,
	bool routeSucceeded) {
	if (!routeSucceeded ||
		!Bot_CommandCtfObjectiveRouteAssignmentValid(selection, assignment) ||
		!goal.valid) {
		return;
	}

	botFrameCommandStatus.ctfObjectiveRouteRouteCommands++;
	BotObjectives_RecordRouteCommand(goal, assignment);
	BotObjectives_RecordReach(goal, assignment);
	Bot_CommandRecordCtfObjectiveRouteLast(
		Bot_PerceptionClientIndex(bot),
		selection,
		assignment,
		goal,
		Bot_CommandDistanceSquaredToObjectiveGoal(bot, goal));
}

Vector3 Bot_CommandTimedRouteFallbackDirection(gentity_t *bot) {
	Vector3 forward = { 1.0f, 0.0f, 0.0f };
	if (bot != nullptr && bot->client != nullptr) {
		Vector3 viewAngles = Bot_CommandCurrentViewAngles(bot);
		viewAngles[PITCH] = 0.0f;
		viewAngles[ROLL] = 0.0f;
		AngleVectors(viewAngles, forward, nullptr, nullptr);
	}

	forward.z = 0.0f;
	if (forward.lengthSquared() < 1.0f) {
		forward = { 1.0f, 0.0f, 0.0f };
	}
	return (forward * -1.0f).normalized();
}

gentity_t *Bot_CommandTimedRouteEnemySource(gentity_t *bot) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 || clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return nullptr;
	}

	const BotBrainBlackboardSnapshot &snapshot =
		botBrainBlackboardSlots[clientIndex].snapshot;
	if (!snapshot.valid || snapshot.currentEnemyEntity < 0) {
		return nullptr;
	}

	gentity_t *enemy = Bot_PerceptionEntityFromMemory(
		snapshot.currentEnemyEntity,
		snapshot.currentEnemySpawnCount);
	if (!Bot_PerceptionCombatTargetAlive(enemy) || enemy == bot) {
		return nullptr;
	}
	return enemy;
}

bool Bot_CommandRecentDamageSource(gentity_t *bot, Vector3 *source) {
	if (source == nullptr) {
		return false;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (clientIndex < 0 || clientIndex >= static_cast<int>(botBrainBlackboardSlots.size())) {
		return false;
	}

	const BotBrainBlackboardSnapshot &snapshot =
		botBrainBlackboardSlots[clientIndex].snapshot;
	if (!snapshot.valid || snapshot.lastDamagedTimeMilliseconds <= 0) {
		return false;
	}
	if (Bot_CommandElapsedMilliseconds(
			Bot_CommandCurrentTimeMilliseconds(),
			snapshot.lastDamagedTimeMilliseconds) >
		BOT_COMMAND_TELEPORTER_DAMAGE_SOURCE_MILLISECONDS) {
		return false;
	}

	*source = {
		static_cast<float>(snapshot.lastDamageOriginX),
		static_cast<float>(snapshot.lastDamageOriginY),
		static_cast<float>(snapshot.lastDamageOriginZ)
	};
	return true;
}

void Bot_CommandRecordNukeRetreatLast(
	int clientIndex,
	int remainingMilliseconds,
	const Vector3 &source,
	const Vector3 &goal,
	float distanceSquared) {
	botFrameCommandStatus.lastNukeRetreatClient = clientIndex;
	botFrameCommandStatus.lastNukeRetreatRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastNukeRetreatSourceX = static_cast<int>(source.x);
	botFrameCommandStatus.lastNukeRetreatSourceY = static_cast<int>(source.y);
	botFrameCommandStatus.lastNukeRetreatSourceZ = static_cast<int>(source.z);
	botFrameCommandStatus.lastNukeRetreatGoalX = static_cast<int>(goal.x);
	botFrameCommandStatus.lastNukeRetreatGoalY = static_cast<int>(goal.y);
	botFrameCommandStatus.lastNukeRetreatGoalZ = static_cast<int>(goal.z);
	botFrameCommandStatus.lastNukeRetreatDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
}

const char *Bot_CommandTimedRouteGoalKindName(BotTimedRouteGoalKind kind) {
	switch (kind) {
	case BotTimedRouteGoalKind::NukeRetreat:
		return "nuke_retreat";
	case BotTimedRouteGoalKind::TeleporterEscape:
		return "teleporter_escape";
	case BotTimedRouteGoalKind::CoopLeader:
		return "coop_leader";
	case BotTimedRouteGoalKind::CoopLeadAdvance:
		return "coop_lead_advance";
	case BotTimedRouteGoalKind::TeamRole:
		return "team_role";
	case BotTimedRouteGoalKind::CtfRole:
		return "ctf_role";
	case BotTimedRouteGoalKind::FfaRoam:
		return "ffa_roam";
	case BotTimedRouteGoalKind::ThreatRetreat:
		return "threat_retreat";
	default:
		return "none";
	}
}

void Bot_CommandRecordThreatRetreatLast(
	int clientIndex,
	const gentity_t *source,
	float sourceDistanceSquared,
	int remainingMilliseconds,
	float goalDistanceSquared,
	int health,
	int armor,
	bool lowHealth,
	bool active,
	const char *reason) {
	botFrameCommandStatus.lastThreatRetreatClient = clientIndex;
	botFrameCommandStatus.lastThreatRetreatSourceClient =
		Bot_PerceptionEntityClientIndex(source);
	botFrameCommandStatus.lastThreatRetreatSourceEntity =
		Bot_PerceptionEntityNumber(source);
	botFrameCommandStatus.lastThreatRetreatSourceDistanceSquared =
		Bot_PerceptionClampDistanceSquared(sourceDistanceSquared);
	botFrameCommandStatus.lastThreatRetreatRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastThreatRetreatGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(goalDistanceSquared);
	botFrameCommandStatus.lastThreatRetreatHealth = health;
	botFrameCommandStatus.lastThreatRetreatArmor = armor;
	botFrameCommandStatus.lastThreatRetreatLowHealth = lowHealth ? 1 : 0;
	botFrameCommandStatus.lastThreatRetreatActive = active ? 1 : 0;
	botFrameCommandStatus.lastThreatRetreatReason =
		reason != nullptr ? reason : "none";
}

void Bot_CommandRecordFfaRoamRouteLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	int remainingMilliseconds,
	float distanceSquared) {
	botFrameCommandStatus.lastFfaRoamRouteClient = clientIndex;
	botFrameCommandStatus.lastFfaRoamRouteMode = static_cast<int>(policy.mode);
	botFrameCommandStatus.lastFfaRoamRouteRole = static_cast<int>(policy.role);
	botFrameCommandStatus.lastFfaRoamRouteLane = static_cast<int>(policy.lane);
	botFrameCommandStatus.lastFfaRoamRoutePriority = policy.priority;
	botFrameCommandStatus.lastFfaRoamRouteRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastFfaRoamRouteGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
}

void Bot_CommandRecordFfaRoamRouteOwnerLast(
	int clientIndex,
	int remainingMilliseconds,
	float distanceSquared,
	const BotTimedRouteGoalState *state = nullptr) {
	botFrameCommandStatus.lastFfaRoamRouteClient = clientIndex;
	botFrameCommandStatus.lastFfaRoamRouteRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastFfaRoamRouteGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
	if (state != nullptr && state->kind == BotTimedRouteGoalKind::FfaRoam) {
		botFrameCommandStatus.lastFfaRoamRouteMode = state->matchMode;
		botFrameCommandStatus.lastFfaRoamRouteRole = state->matchRole;
		botFrameCommandStatus.lastFfaRoamRouteLane = state->matchLane;
		botFrameCommandStatus.lastFfaRoamRoutePriority = state->matchPriority;
	}
}

void Bot_CommandRecordFfaSpawnCampAvoidanceLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	const gentity_t *source,
	float sourceDistanceSquared,
	float goalDistanceSquared,
	const char *reason) {
	botFrameCommandStatus.lastFfaSpawnCampAvoidanceClient = clientIndex;
	botFrameCommandStatus.lastFfaSpawnCampAvoidanceSourceClient =
		Bot_PerceptionEntityClientIndex(source);
	botFrameCommandStatus.lastFfaSpawnCampAvoidanceSourceEntity =
		Bot_PerceptionEntityNumber(source);
	botFrameCommandStatus.lastFfaSpawnCampAvoidanceSourceDistanceSquared =
		Bot_PerceptionClampDistanceSquared(sourceDistanceSquared);
	botFrameCommandStatus.lastFfaSpawnCampAvoidancePolicyAvoid =
		policy.avoidSpawnCamping ? 1 : 0;
	botFrameCommandStatus.lastFfaSpawnCampAvoidanceGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(goalDistanceSquared);
	botFrameCommandStatus.lastFfaSpawnCampAvoidanceReason =
		reason != nullptr ? reason : "none";
}

void Bot_CommandRecordTeamRoleRouteLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	int remainingMilliseconds,
	float distanceSquared) {
	botFrameCommandStatus.lastTeamRoleRouteClient = clientIndex;
	botFrameCommandStatus.lastTeamRoleRouteMode = static_cast<int>(policy.mode);
	botFrameCommandStatus.lastTeamRoleRouteRole = static_cast<int>(policy.role);
	botFrameCommandStatus.lastTeamRoleRouteLane = static_cast<int>(policy.lane);
	botFrameCommandStatus.lastTeamRoleRoutePriority = policy.priority;
	botFrameCommandStatus.lastTeamRoleRouteRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastTeamRoleRouteGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
}

void Bot_CommandRecordTeamRoleRouteOwnerLast(
	int clientIndex,
	int remainingMilliseconds,
	float distanceSquared,
	const BotTimedRouteGoalState *state = nullptr) {
	botFrameCommandStatus.lastTeamRoleRouteClient = clientIndex;
	botFrameCommandStatus.lastTeamRoleRouteRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastTeamRoleRouteGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
	if (state != nullptr && state->kind == BotTimedRouteGoalKind::TeamRole) {
		botFrameCommandStatus.lastTeamRoleRouteMode = state->matchMode;
		botFrameCommandStatus.lastTeamRoleRouteRole = state->matchRole;
		botFrameCommandStatus.lastTeamRoleRouteLane = state->matchLane;
		botFrameCommandStatus.lastTeamRoleRoutePriority = state->matchPriority;
	}
}

void Bot_CommandRecordCtfRoleRouteLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	int remainingMilliseconds,
	float distanceSquared) {
	botFrameCommandStatus.lastCtfRoleRouteClient = clientIndex;
	botFrameCommandStatus.lastCtfRoleRouteMode = static_cast<int>(policy.mode);
	botFrameCommandStatus.lastCtfRoleRouteRole = static_cast<int>(policy.role);
	botFrameCommandStatus.lastCtfRoleRouteLane = static_cast<int>(policy.lane);
	botFrameCommandStatus.lastCtfRoleRoutePriority = policy.priority;
	botFrameCommandStatus.lastCtfRoleRouteRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastCtfRoleRouteGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
}

void Bot_CommandRecordCtfRoleRouteOwnerLast(
	int clientIndex,
	int remainingMilliseconds,
	float distanceSquared,
	const BotTimedRouteGoalState *state = nullptr) {
	botFrameCommandStatus.lastCtfRoleRouteClient = clientIndex;
	botFrameCommandStatus.lastCtfRoleRouteRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastCtfRoleRouteGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
	if (state != nullptr && state->kind == BotTimedRouteGoalKind::CtfRole) {
		botFrameCommandStatus.lastCtfRoleRouteMode = state->matchMode;
		botFrameCommandStatus.lastCtfRoleRouteRole = state->matchRole;
		botFrameCommandStatus.lastCtfRoleRouteLane = state->matchLane;
		botFrameCommandStatus.lastCtfRoleRoutePriority = state->matchPriority;
	}
}

void Bot_CommandRecordFfaRoleCombatLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	const BotCombatEnemyFacts *facts,
	const char *reason) {
	botFrameCommandStatus.lastFfaRoleCombatClient = clientIndex;
	botFrameCommandStatus.lastFfaRoleCombatMode = static_cast<int>(policy.mode);
	botFrameCommandStatus.lastFfaRoleCombatRole = static_cast<int>(policy.role);
	botFrameCommandStatus.lastFfaRoleCombatLane = static_cast<int>(policy.lane);
	botFrameCommandStatus.lastFfaRoleCombatPriority =
		std::max(policy.engagePriority, policy.priority);
	botFrameCommandStatus.lastFfaRoleCombatTargetClient =
		facts != nullptr ? facts->enemyClientIndex : -1;
	botFrameCommandStatus.lastFfaRoleCombatTargetEntity =
		facts != nullptr ? facts->enemyEntity : -1;
	botFrameCommandStatus.lastFfaRoleCombatTargetDistanceSquared =
		facts != nullptr ? facts->distanceSquared : 0;
	botFrameCommandStatus.lastFfaRoleCombatTargetVisible =
		(facts != nullptr && facts->visible) ? 1 : 0;
	botFrameCommandStatus.lastFfaRoleCombatTargetShootable =
		(facts != nullptr && facts->shootable) ? 1 : 0;
	botFrameCommandStatus.lastFfaRoleCombatReason =
		reason != nullptr ? reason : "none";
}

void Bot_CommandRecordFfaSpawnCampCombatAvoidanceLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	const gentity_t *target,
	const gentity_t *source,
	float sourceDistanceSquared,
	bool blocked,
	const char *reason) {
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceClient = clientIndex;
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetClient =
		Bot_PerceptionEntityClientIndex(target);
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetEntity =
		Bot_PerceptionEntityNumber(target);
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceClient =
		Bot_PerceptionEntityClientIndex(source);
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceEntity =
		Bot_PerceptionEntityNumber(source);
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceDistanceSquared =
		Bot_PerceptionClampDistanceSquared(sourceDistanceSquared);
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidancePolicyAvoid =
		policy.avoidSpawnCamping ? 1 : 0;
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceBlocked =
		blocked ? 1 : 0;
	botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceReason =
		reason != nullptr ? reason : "none";
}

void Bot_CommandRecordCtfRoleCombatLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	const BotCombatEnemyFacts *facts,
	const char *reason) {
	botFrameCommandStatus.lastCtfRoleCombatClient = clientIndex;
	botFrameCommandStatus.lastCtfRoleCombatMode = static_cast<int>(policy.mode);
	botFrameCommandStatus.lastCtfRoleCombatRole = static_cast<int>(policy.role);
	botFrameCommandStatus.lastCtfRoleCombatLane = static_cast<int>(policy.lane);
	botFrameCommandStatus.lastCtfRoleCombatPriority =
		std::max(policy.engagePriority, policy.priority);
	botFrameCommandStatus.lastCtfRoleCombatTargetClient =
		facts != nullptr ? facts->enemyClientIndex : -1;
	botFrameCommandStatus.lastCtfRoleCombatTargetEntity =
		facts != nullptr ? facts->enemyEntity : -1;
	botFrameCommandStatus.lastCtfRoleCombatTargetDistanceSquared =
		facts != nullptr ? facts->distanceSquared : 0;
	botFrameCommandStatus.lastCtfRoleCombatTargetVisible =
		(facts != nullptr && facts->visible) ? 1 : 0;
	botFrameCommandStatus.lastCtfRoleCombatTargetShootable =
		(facts != nullptr && facts->shootable) ? 1 : 0;
	botFrameCommandStatus.lastCtfRoleCombatReason =
		reason != nullptr ? reason : "none";
}

void Bot_CommandRecordTeamRoleCombatLast(
	int clientIndex,
	const BotObjectiveMatchPolicy &policy,
	const BotCombatEnemyFacts *facts,
	const char *reason) {
	botFrameCommandStatus.lastTeamRoleCombatClient = clientIndex;
	botFrameCommandStatus.lastTeamRoleCombatMode = static_cast<int>(policy.mode);
	botFrameCommandStatus.lastTeamRoleCombatRole = static_cast<int>(policy.role);
	botFrameCommandStatus.lastTeamRoleCombatLane = static_cast<int>(policy.lane);
	botFrameCommandStatus.lastTeamRoleCombatPriority =
		std::max(policy.engagePriority, policy.priority);
	botFrameCommandStatus.lastTeamRoleCombatTargetClient =
		facts != nullptr ? facts->enemyClientIndex : -1;
	botFrameCommandStatus.lastTeamRoleCombatTargetEntity =
		facts != nullptr ? facts->enemyEntity : -1;
	botFrameCommandStatus.lastTeamRoleCombatTargetDistanceSquared =
		facts != nullptr ? facts->distanceSquared : 0;
	botFrameCommandStatus.lastTeamRoleCombatTargetVisible =
		(facts != nullptr && facts->visible) ? 1 : 0;
	botFrameCommandStatus.lastTeamRoleCombatTargetShootable =
		(facts != nullptr && facts->shootable) ? 1 : 0;
	botFrameCommandStatus.lastTeamRoleCombatReason =
		reason != nullptr ? reason : "none";
}

void Bot_CommandRecordCoopLeadAdvanceRouteLast(
	int clientIndex,
	int remainingMilliseconds,
	float distanceSquared) {
	botFrameCommandStatus.lastCoopLeadAdvanceClient = clientIndex;
	botFrameCommandStatus.lastCoopLeadAdvanceRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastCoopLeadAdvanceGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
}

void Bot_CommandRecordTimedRouteGoalLast(
	BotTimedRouteGoalKind kind,
	int clientIndex,
	int remainingMilliseconds,
	const Vector3 &source,
	const Vector3 &goal,
	float distanceSquared) {
	botFrameCommandStatus.lastTimedRouteGoalKind = static_cast<int>(kind);
	botFrameCommandStatus.lastTimedRouteGoalClient = clientIndex;
	botFrameCommandStatus.lastTimedRouteGoalRemainingMilliseconds =
		std::max(remainingMilliseconds, 0);
	botFrameCommandStatus.lastTimedRouteGoalSourceX = static_cast<int>(source.x);
	botFrameCommandStatus.lastTimedRouteGoalSourceY = static_cast<int>(source.y);
	botFrameCommandStatus.lastTimedRouteGoalSourceZ = static_cast<int>(source.z);
	botFrameCommandStatus.lastTimedRouteGoalGoalX = static_cast<int>(goal.x);
	botFrameCommandStatus.lastTimedRouteGoalGoalY = static_cast<int>(goal.y);
	botFrameCommandStatus.lastTimedRouteGoalGoalZ = static_cast<int>(goal.z);
	botFrameCommandStatus.lastTimedRouteGoalDistanceSquared =
		Bot_PerceptionClampDistanceSquared(distanceSquared);
}

void Bot_CommandRecordTimedRouteGoalOwnerLast(
	BotTimedRouteGoalKind kind,
	int clientIndex,
	int remainingMilliseconds,
	const Vector3 &source,
	const Vector3 &goal,
	float distanceSquared,
	const BotTimedRouteGoalState *state = nullptr) {
	Bot_CommandRecordTimedRouteGoalLast(
		kind,
		clientIndex,
		remainingMilliseconds,
		source,
		goal,
		distanceSquared);
	if (kind == BotTimedRouteGoalKind::NukeRetreat) {
		Bot_CommandRecordNukeRetreatLast(
			clientIndex,
			remainingMilliseconds,
			source,
			goal,
			distanceSquared);
	} else if (kind == BotTimedRouteGoalKind::CoopLeadAdvance) {
		Bot_CommandRecordCoopLeadAdvanceRouteLast(
			clientIndex,
			remainingMilliseconds,
			distanceSquared);
	} else if (kind == BotTimedRouteGoalKind::TeamRole) {
		Bot_CommandRecordTeamRoleRouteOwnerLast(
			clientIndex,
			remainingMilliseconds,
			distanceSquared,
			state);
	} else if (kind == BotTimedRouteGoalKind::CtfRole) {
		Bot_CommandRecordCtfRoleRouteOwnerLast(
			clientIndex,
			remainingMilliseconds,
			distanceSquared,
			state);
	} else if (kind == BotTimedRouteGoalKind::FfaRoam) {
		Bot_CommandRecordFfaRoamRouteOwnerLast(
			clientIndex,
			remainingMilliseconds,
			distanceSquared,
			state);
	} else if (kind == BotTimedRouteGoalKind::ThreatRetreat) {
		Bot_CommandRecordThreatRetreatLast(
			clientIndex,
			nullptr,
			distanceSquared,
			remainingMilliseconds,
			(goal - source).lengthSquared(),
			botFrameCommandStatus.lastThreatRetreatHealth,
			botFrameCommandStatus.lastThreatRetreatArmor,
			botFrameCommandStatus.lastThreatRetreatLowHealth != 0,
			remainingMilliseconds > 0,
			"owner");
	}
}

void Bot_CommandClearTimedRouteGoal(BotTimedRouteGoalState *state) {
	if (state == nullptr) {
		return;
	}
	*state = {};
}

void Bot_CommandRecordTimedRouteGoalRouteRequest(BotTimedRouteGoalKind kind) {
	botFrameCommandStatus.timedRouteGoalRouteRequests++;
	if (kind == BotTimedRouteGoalKind::NukeRetreat) {
		botFrameCommandStatus.nukeRetreatRouteRequests++;
	} else if (kind == BotTimedRouteGoalKind::CoopLeadAdvance) {
		botFrameCommandStatus.coopLeadAdvanceRouteRequests++;
	} else if (kind == BotTimedRouteGoalKind::TeamRole) {
		botFrameCommandStatus.teamRoleRouteRouteRequests++;
	} else if (kind == BotTimedRouteGoalKind::CtfRole) {
		botFrameCommandStatus.ctfRoleRouteRouteRequests++;
	} else if (kind == BotTimedRouteGoalKind::FfaRoam) {
		botFrameCommandStatus.ffaRoamRouteRouteRequests++;
		if (Bot_CommandFfaSpawnCampAvoidanceEnabled()) {
			botFrameCommandStatus.ffaSpawnCampAvoidanceRouteRequests++;
		}
	} else if (kind == BotTimedRouteGoalKind::ThreatRetreat) {
		botFrameCommandStatus.threatRetreatRouteRequests++;
	}
}

void Bot_CommandRecordTimedRouteGoalDeferral(BotTimedRouteGoalKind kind) {
	botFrameCommandStatus.timedRouteGoalRouteDeferrals++;
	if (kind == BotTimedRouteGoalKind::NukeRetreat) {
		botFrameCommandStatus.nukeRetreatRouteDeferrals++;
	} else if (kind == BotTimedRouteGoalKind::CoopLeadAdvance) {
		botFrameCommandStatus.coopLeadAdvanceRouteDeferrals++;
	} else if (kind == BotTimedRouteGoalKind::TeamRole) {
		botFrameCommandStatus.teamRoleRouteRouteDeferrals++;
	} else if (kind == BotTimedRouteGoalKind::CtfRole) {
		botFrameCommandStatus.ctfRoleRouteRouteDeferrals++;
	} else if (kind == BotTimedRouteGoalKind::FfaRoam) {
		botFrameCommandStatus.ffaRoamRouteRouteDeferrals++;
	} else if (kind == BotTimedRouteGoalKind::ThreatRetreat) {
		botFrameCommandStatus.threatRetreatRouteDeferrals++;
	}
}

void Bot_CommandRecordTimedRouteGoalExpiration(BotTimedRouteGoalKind kind) {
	botFrameCommandStatus.timedRouteGoalExpirations++;
	if (kind == BotTimedRouteGoalKind::NukeRetreat) {
		botFrameCommandStatus.nukeRetreatExpirations++;
	} else if (kind == BotTimedRouteGoalKind::CoopLeadAdvance) {
		botFrameCommandStatus.coopLeadAdvanceExpirations++;
	} else if (kind == BotTimedRouteGoalKind::TeamRole) {
		botFrameCommandStatus.teamRoleRouteExpirations++;
	} else if (kind == BotTimedRouteGoalKind::CtfRole) {
		botFrameCommandStatus.ctfRoleRouteExpirations++;
	} else if (kind == BotTimedRouteGoalKind::FfaRoam) {
		botFrameCommandStatus.ffaRoamRouteExpirations++;
	} else if (kind == BotTimedRouteGoalKind::ThreatRetreat) {
		botFrameCommandStatus.threatRetreatExpirations++;
	}
}

void Bot_CommandRecordTimedRouteGoalInvalid(BotTimedRouteGoalKind kind) {
	botFrameCommandStatus.timedRouteGoalInvalidSkips++;
	if (kind == BotTimedRouteGoalKind::NukeRetreat) {
		botFrameCommandStatus.nukeRetreatInvalidSkips++;
	} else if (kind == BotTimedRouteGoalKind::TeleporterEscape) {
		botFrameCommandStatus.teleporterEscapeInvalidSkips++;
	} else if (kind == BotTimedRouteGoalKind::CoopLeader) {
		botFrameCommandStatus.coopLeaderRouteInvalidSkips++;
	} else if (kind == BotTimedRouteGoalKind::CoopLeadAdvance) {
		botFrameCommandStatus.coopLeadAdvanceInvalidSkips++;
	} else if (kind == BotTimedRouteGoalKind::TeamRole) {
		botFrameCommandStatus.teamRoleRouteInvalidSkips++;
	} else if (kind == BotTimedRouteGoalKind::CtfRole) {
		botFrameCommandStatus.ctfRoleRouteInvalidSkips++;
	} else if (kind == BotTimedRouteGoalKind::FfaRoam) {
		botFrameCommandStatus.ffaRoamRouteInvalidSkips++;
	} else if (kind == BotTimedRouteGoalKind::ThreatRetreat) {
		botFrameCommandStatus.threatRetreatInvalidSkips++;
	}
}

bool Bot_CommandApplyTimedRouteGoal(
	gentity_t *bot,
	BotNavRouteRequest *request) {
	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr || request == nullptr) {
		Bot_CommandRecordTimedRouteGoalInvalid(BotTimedRouteGoalKind::None);
		return false;
	}

	BotTimedRouteGoalState &state = slot->timedRouteGoal;
	if (state.kind == BotTimedRouteGoalKind::None || state.untilMilliseconds <= 0) {
		return false;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	const int remainingMilliseconds = state.untilMilliseconds - nowMilliseconds;
	if (remainingMilliseconds <= 0) {
		const BotTimedRouteGoalKind expiredKind = state.kind;
		const BotTimedRouteGoalState expiredState = state;
		Bot_CommandClearTimedRouteGoal(&state);
		Bot_CommandRecordTimedRouteGoalExpiration(expiredKind);
		Bot_CommandRecordTimedRouteGoalOwnerLast(
			expiredKind,
			clientIndex,
			0,
			vec3_origin,
			vec3_origin,
			0.0f,
			&expiredState);
		return false;
	}

	if (request->hasPositionGoal || request->hasTravelTypeGoal) {
		Bot_CommandRecordTimedRouteGoalDeferral(state.kind);
		Bot_CommandRecordTimedRouteGoalOwnerLast(
			state.kind,
			clientIndex,
			remainingMilliseconds,
			state.source,
			state.goal,
			0.0f,
			&state);
		return false;
	}

	Vector3 away = bot->s.origin - state.source;
	away.z = 0.0f;
	const float minDirectionSquared = state.minDirectionSquared > 0.0f ?
		state.minDirectionSquared :
		BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED;
	if (away.lengthSquared() < minDirectionSquared) {
		away = state.fallbackDirection;
		away.z = 0.0f;
	}
	if (away.lengthSquared() < 1.0f) {
		Bot_CommandRecordTimedRouteGoalInvalid(state.kind);
		return false;
	}

	away = away.normalized();
	const float distance = state.distance > 0.0f ?
		state.distance :
		BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE;
	state.goal = bot->s.origin + (away * distance);
	state.goal.z = bot->s.origin.z;
	request->hasPositionGoal = true;
	request->positionGoal[0] = state.goal.x;
	request->positionGoal[1] = state.goal.y;
	request->positionGoal[2] = state.goal.z;
	Bot_CommandRecordTimedRouteGoalRouteRequest(state.kind);
	Bot_CommandRecordTimedRouteGoalOwnerLast(
		state.kind,
		clientIndex,
		remainingMilliseconds,
		state.source,
		state.goal,
		(bot->s.origin - state.source).lengthSquared(),
		&state);
	return true;
}

bool Bot_CommandActivateTimedRouteGoal(
	gentity_t *bot,
	BotTimedRouteGoalKind kind,
	const Vector3 &source,
	const Vector3 &fallbackDirection,
	int durationMilliseconds,
	float distance = BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE,
	float minDirectionSquared = BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED) {
	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr || kind == BotTimedRouteGoalKind::None || durationMilliseconds <= 0) {
		Bot_CommandRecordTimedRouteGoalInvalid(kind);
		return false;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	BotTimedRouteGoalState &state = slot->timedRouteGoal;
	state.kind = kind;
	state.untilMilliseconds = nowMilliseconds + durationMilliseconds;
	state.source = source;
	state.fallbackDirection = fallbackDirection;
	state.distance = distance;
	state.minDirectionSquared = minDirectionSquared;
	state.matchMode = 0;
	state.matchRole = 0;
	state.matchLane = 0;
	state.matchPriority = 0;
	state.goal = bot->s.origin + (fallbackDirection * distance);
	state.goal.z = bot->s.origin.z;

	botFrameCommandStatus.timedRouteGoalActivations++;
	Bot_CommandRecordTimedRouteGoalLast(
		kind,
		clientIndex,
		durationMilliseconds,
		state.source,
		state.goal,
		(bot->s.origin - state.source).lengthSquared());
	return true;
}

void Bot_CommandActivateNukeRetreat(
	gentity_t *bot,
	const BotActionCommandRequest &request) {
	if (request.kind != BotActionCommandRequestKind::UseInventoryIndex ||
		request.item != static_cast<int>(IT_AMMO_NUKE)) {
		return;
	}

	const Vector3 fallbackDirection = Bot_CommandTimedRouteFallbackDirection(bot);
	Vector3 source = bot->s.origin - (fallbackDirection * BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE);
	if (gentity_t *enemy = Bot_CommandTimedRouteEnemySource(bot)) {
		source = enemy->s.origin;
	} else {
		botFrameCommandStatus.nukeRetreatFallbackSources++;
	}

	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::NukeRetreat,
			source,
			fallbackDirection,
			BOT_COMMAND_NUKE_RETREAT_MILLISECONDS,
			BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.nukeRetreatInvalidSkips++;
		return;
	}

	botFrameCommandStatus.nukeRetreatActivations++;
	Vector3 goal = bot->s.origin + (fallbackDirection * BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE);
	goal.z = bot->s.origin.z;
	Bot_CommandRecordNukeRetreatLast(
		Bot_PerceptionClientIndex(bot),
		BOT_COMMAND_NUKE_RETREAT_MILLISECONDS,
		source,
		goal,
		(bot->s.origin - source).lengthSquared());
}

void Bot_CommandActivateTeleporterEscapeRoute(
	gentity_t *bot,
	const BotActionCommandRequest &request) {
	if (request.kind != BotActionCommandRequestKind::UseInventoryIndex ||
		request.item != static_cast<int>(IT_TELEPORTER)) {
		return;
	}

	const Vector3 fallbackDirection = Bot_CommandTimedRouteFallbackDirection(bot);
	Vector3 source = bot->s.origin - (fallbackDirection * BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE);
	if (gentity_t *enemy = Bot_CommandTimedRouteEnemySource(bot)) {
		source = enemy->s.origin;
	} else if (Bot_CommandRecentDamageSource(bot, &source)) {
		botFrameCommandStatus.teleporterEscapeDamageSources++;
	} else {
		botFrameCommandStatus.teleporterEscapeFallbackSources++;
	}

	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::TeleporterEscape,
			source,
			fallbackDirection,
			BOT_COMMAND_TELEPORTER_ESCAPE_MILLISECONDS,
			BOT_COMMAND_TIMED_ROUTE_DEFAULT_DISTANCE,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.teleporterEscapeInvalidSkips++;
		return;
	}

	botFrameCommandStatus.teleporterEscapeRouteActivations++;
}

bool Bot_CommandThreatRetreatActive(gentity_t *bot) {
	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr) {
		return false;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	return slot->timedRouteGoal.kind == BotTimedRouteGoalKind::ThreatRetreat &&
		slot->timedRouteGoal.untilMilliseconds > nowMilliseconds;
}

bool Bot_CommandThreatRetreatSource(
	gentity_t *bot,
	const BotActionDecision &actionDecision,
	const Vector3 &fallbackDirection,
	Vector3 *source,
	gentity_t **sourceEntity,
	float *sourceDistanceSquared,
	const char **reason) {
	if (source == nullptr ||
		sourceEntity == nullptr ||
		sourceDistanceSquared == nullptr ||
		reason == nullptr) {
		return false;
	}

	*sourceEntity = nullptr;
	*sourceDistanceSquared = 0.0f;
	*reason = "none";

	if (gentity_t *enemy = Bot_CommandTimedRouteEnemySource(bot)) {
		*source = enemy->s.origin;
		*sourceEntity = enemy;
		const Vector3 delta = bot->s.origin - enemy->s.origin;
		*sourceDistanceSquared = delta.lengthSquared();
		*reason = "enemy";
		botFrameCommandStatus.threatRetreatEnemySources++;
		return true;
	}

	if (Bot_CommandRecentDamageSource(bot, source)) {
		const Vector3 delta = bot->s.origin - *source;
		*sourceDistanceSquared = delta.lengthSquared();
		*reason = "damage";
		botFrameCommandStatus.threatRetreatDamageSources++;
		return true;
	}

	if (actionDecision.intent != BotActionIntent::Attack && !actionDecision.pressAttack) {
		return false;
	}

	*source = bot->s.origin - (fallbackDirection * BOT_COMMAND_THREAT_RETREAT_DISTANCE);
	*sourceDistanceSquared =
		BOT_COMMAND_THREAT_RETREAT_DISTANCE * BOT_COMMAND_THREAT_RETREAT_DISTANCE;
	*reason = "fallback";
	botFrameCommandStatus.threatRetreatFallbackSources++;
	return true;
}

void Bot_CommandActivateThreatRetreat(
	gentity_t *bot,
	const BotActionDecision &actionDecision) {
	if (!Bot_CommandThreatRetreatEnabled() ||
		bot == nullptr ||
		bot->client == nullptr) {
		return;
	}

	botFrameCommandStatus.threatRetreatRequests++;

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr || clientIndex < 0) {
		botFrameCommandStatus.threatRetreatInvalidSkips++;
		return;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	const int health = std::max(bot->health, 0);
	const int armor = Bot_PerceptionArmorValue(bot);
	const bool lowHealth = health <= BOT_COMMAND_THREAT_RETREAT_LOW_HEALTH;
	if (!lowHealth) {
		return;
	}

	if (slot->timedRouteGoal.kind == BotTimedRouteGoalKind::ThreatRetreat &&
		slot->timedRouteGoal.untilMilliseconds > nowMilliseconds) {
		Bot_CommandRecordThreatRetreatLast(
			clientIndex,
			nullptr,
			(bot->s.origin - slot->timedRouteGoal.source).lengthSquared(),
			slot->timedRouteGoal.untilMilliseconds - nowMilliseconds,
			(bot->s.origin - slot->timedRouteGoal.goal).lengthSquared(),
			health,
			armor,
			true,
			true,
			"active");
		return;
	}

	if (slot->threatRetreatCooldownUntilMilliseconds > nowMilliseconds) {
		Bot_CommandRecordThreatRetreatLast(
			clientIndex,
			nullptr,
			0.0f,
			0,
			0.0f,
			health,
			armor,
			true,
			false,
			"cooldown");
		return;
	}

	const Vector3 fallbackDirection = Bot_CommandTimedRouteFallbackDirection(bot);
	Vector3 source = vec3_origin;
	gentity_t *sourceEntity = nullptr;
	float sourceDistanceSquared = 0.0f;
	const char *reason = "none";
	if (!Bot_CommandThreatRetreatSource(
			bot,
			actionDecision,
			fallbackDirection,
			&source,
			&sourceEntity,
			&sourceDistanceSquared,
			&reason)) {
		Bot_CommandRecordThreatRetreatLast(
			clientIndex,
			nullptr,
			0.0f,
			0,
			0.0f,
			health,
			armor,
			true,
			false,
			"no_threat");
		return;
	}

	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::ThreatRetreat,
			source,
			fallbackDirection,
			BOT_COMMAND_THREAT_RETREAT_MILLISECONDS,
			BOT_COMMAND_THREAT_RETREAT_DISTANCE,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.threatRetreatInvalidSkips++;
		return;
	}

	slot->threatRetreatCooldownUntilMilliseconds =
		nowMilliseconds + BOT_COMMAND_THREAT_RETREAT_COOLDOWN_MILLISECONDS;
	slot->threatRetreatLastActivationMilliseconds = nowMilliseconds;
	slot->threatRetreatReengageRecorded = false;
	botFrameCommandStatus.threatRetreatActivations++;
	Bot_CommandRecordThreatRetreatLast(
		clientIndex,
		sourceEntity,
		sourceDistanceSquared,
		BOT_COMMAND_THREAT_RETREAT_MILLISECONDS,
		BOT_COMMAND_THREAT_RETREAT_DISTANCE * BOT_COMMAND_THREAT_RETREAT_DISTANCE,
		health,
		armor,
		true,
		true,
		reason);
}

bool Bot_CommandCoopLeaderIntentUsesRoute(BotObjectiveCoopIntent intent) {
	return intent == BotObjectiveCoopIntent::Regroup ||
		intent == BotObjectiveCoopIntent::FollowLeader ||
		intent == BotObjectiveCoopIntent::SupportCombat;
}

void Bot_CommandRecordCoopLeaderRouteLast(
	int clientIndex,
	const BotObjectiveCoopPolicy &policy) {
	botFrameCommandStatus.lastCoopLeaderRouteClient = clientIndex;
	botFrameCommandStatus.lastCoopLeaderRouteLeaderClient = policy.leaderClient;
	botFrameCommandStatus.lastCoopLeaderRouteIntent = static_cast<int>(policy.intent);
	botFrameCommandStatus.lastCoopLeaderRouteLeaderDistanceSquared =
		std::max(policy.leaderDistanceSquared, 0);
}

void Bot_CommandActivateCoopLeaderRoute(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy) {
	if (!policy.valid ||
		!policy.coopMode ||
		!policy.hasLeader ||
		!policy.followLeader ||
		!Bot_CommandCoopLeaderIntentUsesRoute(policy.intent)) {
		return;
	}

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr) {
		botFrameCommandStatus.coopLeaderRouteInvalidSkips++;
		return;
	}

	gentity_t *leader = Bot_CommandClientEntity(policy.leaderClient);
	if (!Bot_PerceptionEntityAlive(leader) || leader == bot) {
		botFrameCommandStatus.coopLeaderRouteInvalidSkips++;
		Bot_CommandRecordCoopLeaderRouteLast(clientIndex, policy);
		return;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	BotTimedRouteGoalState &state = slot->timedRouteGoal;
	if (state.kind != BotTimedRouteGoalKind::None &&
		state.kind != BotTimedRouteGoalKind::CoopLeader &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.coopLeaderRouteOwnerDeferrals++;
		Bot_CommandRecordCoopLeaderRouteLast(clientIndex, policy);
		return;
	}
	if (state.kind == BotTimedRouteGoalKind::CoopLeader &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.coopLeaderRouteRefreshes++;
	}

	Vector3 toLeader = leader->s.origin - bot->s.origin;
	toLeader.z = 0.0f;
	if (toLeader.lengthSquared() < 1.0f) {
		toLeader = Bot_CommandTimedRouteFallbackDirection(bot);
	} else {
		toLeader = toLeader.normalized();
	}

	const bool spacingIntent = policy.intent == BotObjectiveCoopIntent::SupportCombat;
	Vector3 source = leader->s.origin;
	Vector3 fallbackDirection = toLeader;
	float distance = BOT_COMMAND_COOP_LEADER_ROUTE_DISTANCE;
	if (spacingIntent) {
		Vector3 awayFromLeader = bot->s.origin - leader->s.origin;
		awayFromLeader.z = 0.0f;
		fallbackDirection = awayFromLeader.lengthSquared() >= 1.0f ?
			awayFromLeader.normalized() :
			Bot_CommandTimedRouteFallbackDirection(bot);
		distance = BOT_COMMAND_COOP_LEADER_SUPPORT_DISTANCE;
	} else {
		source = bot->s.origin - (toLeader * BOT_COMMAND_COOP_LEADER_ROUTE_DISTANCE);
	}

	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::CoopLeader,
			source,
			fallbackDirection,
			BOT_COMMAND_COOP_LEADER_ROUTE_MILLISECONDS,
			distance,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.coopLeaderRouteInvalidSkips++;
		Bot_CommandRecordCoopLeaderRouteLast(clientIndex, policy);
		return;
	}

	botFrameCommandStatus.coopLeaderRouteActivations++;
	if (spacingIntent) {
		botFrameCommandStatus.coopLeaderRouteSpacingSources++;
	} else {
		botFrameCommandStatus.coopLeaderRouteTowardSources++;
	}
	Bot_CommandRecordCoopLeaderRouteLast(clientIndex, policy);
}

void Bot_CommandRecordCoopLeadAdvanceLast(
	int clientIndex,
	const BotObjectiveCoopPolicy &policy) {
	botFrameCommandStatus.lastCoopLeadAdvanceClient = clientIndex;
	botFrameCommandStatus.lastCoopLeadAdvanceIntent = static_cast<int>(policy.intent);
}

void Bot_CommandActivateCoopLeadAdvance(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy,
	bool requested) {
	if (!requested ||
		!policy.valid ||
		!policy.coopMode ||
		!policy.mayLead ||
		policy.intent != BotObjectiveCoopIntent::LeadAdvance) {
		return;
	}

	botFrameCommandStatus.coopLeadAdvancePolicyLeads++;

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr || bot == nullptr || bot->client == nullptr) {
		botFrameCommandStatus.coopLeadAdvanceInvalidSkips++;
		Bot_CommandRecordCoopLeadAdvanceLast(clientIndex, policy);
		return;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	BotTimedRouteGoalState &state = slot->timedRouteGoal;
	if (state.kind != BotTimedRouteGoalKind::None &&
		state.kind != BotTimedRouteGoalKind::CoopLeadAdvance &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.coopLeadAdvanceOwnerDeferrals++;
		Bot_CommandRecordCoopLeadAdvanceLast(clientIndex, policy);
		return;
	}
	if (state.kind == BotTimedRouteGoalKind::CoopLeadAdvance &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.coopLeadAdvanceRefreshes++;
	}

	Vector3 advanceDirection = Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
	advanceDirection.z = 0.0f;
	if (advanceDirection.lengthSquared() < 1.0f) {
		botFrameCommandStatus.coopLeadAdvanceInvalidSkips++;
		Bot_CommandRecordCoopLeadAdvanceLast(clientIndex, policy);
		return;
	}
	advanceDirection = advanceDirection.normalized();
	Vector3 source = bot->s.origin - (advanceDirection * BOT_COMMAND_COOP_LEAD_ADVANCE_DISTANCE);
	source.z = bot->s.origin.z;

	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::CoopLeadAdvance,
			source,
			advanceDirection,
			BOT_COMMAND_COOP_LEAD_ADVANCE_MILLISECONDS,
			BOT_COMMAND_COOP_LEAD_ADVANCE_DISTANCE,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.coopLeadAdvanceInvalidSkips++;
		Bot_CommandRecordCoopLeadAdvanceLast(clientIndex, policy);
		return;
	}

	botFrameCommandStatus.coopLeadAdvanceActivations++;
	Bot_CommandRecordCoopLeadAdvanceLast(clientIndex, policy);
	Bot_CommandRecordCoopLeadAdvanceRouteLast(
		clientIndex,
		BOT_COMMAND_COOP_LEAD_ADVANCE_MILLISECONDS,
		(bot->s.origin - source).lengthSquared());
}

Vector3 Bot_CommandFfaRoamRouteDirection(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy) {
	Vector3 forward = Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
	Vector3 right = { 0.0f, 1.0f, 0.0f };
	if (bot != nullptr && bot->client != nullptr) {
		Vector3 viewAngles = Bot_CommandCurrentViewAngles(bot);
		viewAngles[PITCH] = 0.0f;
		viewAngles[ROLL] = 0.0f;
		AngleVectors(viewAngles, forward, right, nullptr);
	}

	forward.z = 0.0f;
	right.z = 0.0f;
	if (forward.lengthSquared() < 1.0f) {
		forward = Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
	}
	if (right.lengthSquared() < 1.0f) {
		right = { 0.0f, 1.0f, 0.0f };
	}

	switch (std::max(policy.clientIndex, 0) % 4) {
	case 1:
		right.z = 0.0f;
		return right.lengthSquared() >= 1.0f ?
			right.normalized() :
			Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
	case 2:
		forward = forward * -1.0f;
		break;
	case 3:
		right = right * -1.0f;
		right.z = 0.0f;
		return right.lengthSquared() >= 1.0f ?
			right.normalized() :
			Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
	default:
		break;
	}

	forward.z = 0.0f;
	return forward.lengthSquared() >= 1.0f ?
		forward.normalized() :
		Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
}

gentity_t *Bot_CommandFfaSpawnCampAvoidanceSource(
	gentity_t *bot,
	float *sourceDistanceSquared) {
	if (sourceDistanceSquared != nullptr) {
		*sourceDistanceSquared = 0.0f;
	}
	if (bot == nullptr || bot->client == nullptr) {
		return nullptr;
	}

	gentity_t *best = nullptr;
	float bestDistanceSquared = BOT_COMMAND_FFA_SPAWN_CAMP_AVOIDANCE_DISTANCE_SQUARED;
	for (gentity_t *candidate : active_players()) {
		if (candidate == bot ||
			!Bot_PerceptionEntityAlive(candidate) ||
			(candidate->flags & FL_NOTARGET)) {
			continue;
		}

		Vector3 delta = bot->s.origin - candidate->s.origin;
		delta.z = 0.0f;
		const float distanceSquared = delta.lengthSquared();
		if (distanceSquared < 1.0f ||
			distanceSquared > bestDistanceSquared) {
			continue;
		}

		best = candidate;
		bestDistanceSquared = distanceSquared;
	}

	if (best != nullptr && sourceDistanceSquared != nullptr) {
		*sourceDistanceSquared = bestDistanceSquared;
	}
	return best;
}

void Bot_CommandActivateFfaRoamRoute(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy) {
	const bool spawnCampAvoidance = Bot_CommandFfaSpawnCampAvoidanceEnabled();
	if (!Bot_CommandFfaRoamRouteEnabled() && !spawnCampAvoidance) {
		return;
	}

	botFrameCommandStatus.ffaRoamRouteRequests++;
	if (spawnCampAvoidance) {
		botFrameCommandStatus.ffaSpawnCampAvoidanceRequests++;
	}

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr ||
		bot == nullptr ||
		bot->client == nullptr ||
		!policy.valid ||
		!policy.participatesInScoring ||
		!Bot_CommandFfaStylePacingPolicyEnabled(policy) ||
		policy.role == BotObjectiveRole::None ||
		policy.lane == BotObjectiveLane::None ||
		!policy.wantsRoam ||
		!policy.wantsCollect ||
		!policy.wantsEngage) {
		botFrameCommandStatus.ffaRoamRouteInvalidSkips++;
		if (spawnCampAvoidance) {
			botFrameCommandStatus.ffaSpawnCampAvoidanceInvalidSkips++;
			Bot_CommandRecordFfaSpawnCampAvoidanceLast(
				clientIndex,
				policy,
				nullptr,
				0.0f,
				0.0f,
				"invalid_policy");
		}
		Bot_CommandRecordFfaRoamRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	botFrameCommandStatus.ffaRoamRoutePolicySelections++;
	if (spawnCampAvoidance) {
		botFrameCommandStatus.ffaSpawnCampAvoidancePolicySelections++;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	BotTimedRouteGoalState &state = slot->timedRouteGoal;
	if (state.kind != BotTimedRouteGoalKind::None &&
		state.kind != BotTimedRouteGoalKind::FfaRoam &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.ffaRoamRouteOwnerDeferrals++;
		if (spawnCampAvoidance) {
			Bot_CommandRecordFfaSpawnCampAvoidanceLast(
				clientIndex,
				policy,
				nullptr,
				0.0f,
				0.0f,
				"owner_deferral");
		}
		Bot_CommandRecordFfaRoamRouteLast(
			clientIndex,
			policy,
			state.untilMilliseconds - nowMilliseconds,
			0.0f);
		return;
	}
	if (state.kind == BotTimedRouteGoalKind::FfaRoam &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.ffaRoamRouteRefreshes++;
	}

	Vector3 direction = Bot_CommandFfaRoamRouteDirection(bot, policy);
	gentity_t *avoidanceSource = nullptr;
	float avoidanceSourceDistanceSquared = 0.0f;
	bool avoidanceSourceSelected = false;
	const char *avoidanceReason = "fallback_roam";
	if (spawnCampAvoidance && policy.avoidSpawnCamping) {
		avoidanceSource = Bot_CommandFfaSpawnCampAvoidanceSource(
			bot,
			&avoidanceSourceDistanceSquared);
		if (avoidanceSource != nullptr) {
			Vector3 away = bot->s.origin - avoidanceSource->s.origin;
			away.z = 0.0f;
			if (away.lengthSquared() >= BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED) {
				direction = away;
				avoidanceSourceSelected = true;
				avoidanceReason = "nearby_source";
				botFrameCommandStatus.ffaSpawnCampAvoidanceSourceSelections++;
			}
		}
		if (!avoidanceSourceSelected) {
			botFrameCommandStatus.ffaSpawnCampAvoidanceFallbacks++;
		}
	}
	direction.z = 0.0f;
	if (direction.lengthSquared() < 1.0f) {
		botFrameCommandStatus.ffaRoamRouteInvalidSkips++;
		if (spawnCampAvoidance) {
			botFrameCommandStatus.ffaSpawnCampAvoidanceInvalidSkips++;
			Bot_CommandRecordFfaSpawnCampAvoidanceLast(
				clientIndex,
				policy,
				avoidanceSourceSelected ? avoidanceSource : nullptr,
				avoidanceSourceDistanceSquared,
				0.0f,
				"invalid_direction");
		}
		Bot_CommandRecordFfaRoamRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	direction = direction.normalized();
	Vector3 source = avoidanceSourceSelected ?
		avoidanceSource->s.origin :
		bot->s.origin - (direction * BOT_COMMAND_FFA_ROAM_ROUTE_DISTANCE);
	source.z = bot->s.origin.z;
	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::FfaRoam,
			source,
			direction,
			BOT_COMMAND_FFA_ROAM_ROUTE_MILLISECONDS,
			BOT_COMMAND_FFA_ROAM_ROUTE_DISTANCE,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.ffaRoamRouteInvalidSkips++;
		if (spawnCampAvoidance) {
			botFrameCommandStatus.ffaSpawnCampAvoidanceInvalidSkips++;
			Bot_CommandRecordFfaSpawnCampAvoidanceLast(
				clientIndex,
				policy,
				avoidanceSourceSelected ? avoidanceSource : nullptr,
				avoidanceSourceDistanceSquared,
				0.0f,
				"activation_failed");
		}
		Bot_CommandRecordFfaRoamRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	state.matchMode = static_cast<int>(policy.mode);
	state.matchRole = static_cast<int>(policy.role);
	state.matchLane = static_cast<int>(policy.lane);
	state.matchPriority = policy.priority;
	botFrameCommandStatus.ffaRoamRouteActivations++;
	if (avoidanceSourceSelected) {
		botFrameCommandStatus.ffaSpawnCampAvoidanceActivations++;
		Bot_CommandRecordFfaSpawnCampAvoidanceLast(
			clientIndex,
			policy,
			avoidanceSource,
			avoidanceSourceDistanceSquared,
			BOT_COMMAND_FFA_ROAM_ROUTE_DISTANCE * BOT_COMMAND_FFA_ROAM_ROUTE_DISTANCE,
			avoidanceReason);
	} else if (spawnCampAvoidance) {
		Bot_CommandRecordFfaSpawnCampAvoidanceLast(
			clientIndex,
			policy,
			nullptr,
			0.0f,
			BOT_COMMAND_FFA_ROAM_ROUTE_DISTANCE * BOT_COMMAND_FFA_ROAM_ROUTE_DISTANCE,
			avoidanceReason);
	}
	Bot_CommandRecordFfaRoamRouteLast(
		clientIndex,
		policy,
		BOT_COMMAND_FFA_ROAM_ROUTE_MILLISECONDS,
		(bot->s.origin - source).lengthSquared());
}

Vector3 Bot_CommandTeamRoleRouteDirection(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy) {
	Vector3 forward = Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
	Vector3 right = { 0.0f, 1.0f, 0.0f };
	if (bot != nullptr && bot->client != nullptr) {
		Vector3 viewAngles = Bot_CommandCurrentViewAngles(bot);
		viewAngles[PITCH] = 0.0f;
		viewAngles[ROLL] = 0.0f;
		AngleVectors(viewAngles, forward, right, nullptr);
	}

	forward.z = 0.0f;
	right.z = 0.0f;
	if (forward.lengthSquared() < 1.0f) {
		forward = Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
	}
	if (right.lengthSquared() < 1.0f) {
		right = { 0.0f, 1.0f, 0.0f };
	}

	Vector3 direction = forward;
	switch (policy.lane) {
	case BotObjectiveLane::Defense:
	case BotObjectiveLane::OwnBaseReturn:
		direction = forward * -1.0f;
		break;
	case BotObjectiveLane::Midfield:
	case BotObjectiveLane::CarrierSupport:
	case BotObjectiveLane::DroppedFlagResponse:
		direction = right;
		break;
	case BotObjectiveLane::Attack:
	default:
		direction = forward;
		break;
	}

	direction.z = 0.0f;
	return direction.lengthSquared() >= 1.0f ?
		direction.normalized() :
		Bot_CommandTimedRouteFallbackDirection(bot) * -1.0f;
}

void Bot_CommandActivateTeamRoleRoute(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy) {
	if (!Bot_CommandTeamRoleRouteEnabled()) {
		return;
	}

	botFrameCommandStatus.teamRoleRouteRequests++;

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr ||
		bot == nullptr ||
		bot->client == nullptr ||
		!policy.valid ||
		!policy.participatesInScoring ||
		policy.role == BotObjectiveRole::None ||
		policy.lane == BotObjectiveLane::None) {
		botFrameCommandStatus.teamRoleRouteInvalidSkips++;
		Bot_CommandRecordTeamRoleRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	botFrameCommandStatus.teamRoleRoutePolicySelections++;

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	BotTimedRouteGoalState &state = slot->timedRouteGoal;
	if (state.kind != BotTimedRouteGoalKind::None &&
		state.kind != BotTimedRouteGoalKind::TeamRole &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.teamRoleRouteOwnerDeferrals++;
		Bot_CommandRecordTeamRoleRouteLast(
			clientIndex,
			policy,
			state.untilMilliseconds - nowMilliseconds,
			0.0f);
		return;
	}
	if (state.kind == BotTimedRouteGoalKind::TeamRole &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.teamRoleRouteRefreshes++;
	}

	Vector3 direction = Bot_CommandTeamRoleRouteDirection(bot, policy);
	direction.z = 0.0f;
	if (direction.lengthSquared() < 1.0f) {
		botFrameCommandStatus.teamRoleRouteInvalidSkips++;
		Bot_CommandRecordTeamRoleRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	direction = direction.normalized();
	Vector3 source = bot->s.origin - (direction * BOT_COMMAND_TEAM_ROLE_ROUTE_DISTANCE);
	source.z = bot->s.origin.z;
	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::TeamRole,
			source,
			direction,
			BOT_COMMAND_TEAM_ROLE_ROUTE_MILLISECONDS,
			BOT_COMMAND_TEAM_ROLE_ROUTE_DISTANCE,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.teamRoleRouteInvalidSkips++;
		Bot_CommandRecordTeamRoleRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	state.matchMode = static_cast<int>(policy.mode);
	state.matchRole = static_cast<int>(policy.role);
	state.matchLane = static_cast<int>(policy.lane);
	state.matchPriority = policy.priority;
	botFrameCommandStatus.teamRoleRouteActivations++;
	Bot_CommandRecordTeamRoleRouteLast(
		clientIndex,
		policy,
		BOT_COMMAND_TEAM_ROLE_ROUTE_MILLISECONDS,
		(bot->s.origin - source).lengthSquared());
}

void Bot_CommandActivateCtfRoleRoute(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy) {
	if (!Bot_CommandCtfRoleRouteEnabled()) {
		return;
	}

	botFrameCommandStatus.ctfRoleRouteRequests++;

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr ||
		bot == nullptr ||
		bot->client == nullptr ||
		!policy.valid ||
		!policy.participatesInScoring ||
		policy.mode != BotObjectiveMatchMode::CaptureTheFlag ||
		policy.role == BotObjectiveRole::None ||
		policy.lane == BotObjectiveLane::None) {
		botFrameCommandStatus.ctfRoleRouteInvalidSkips++;
		Bot_CommandRecordCtfRoleRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	botFrameCommandStatus.ctfRoleRoutePolicySelections++;

	if (Bot_CommandCtfObjectiveRouteEnabled()) {
		botFrameCommandStatus.ctfRoleRouteObjectiveDeferrals++;
		Bot_CommandRecordCtfRoleRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	BotTimedRouteGoalState &state = slot->timedRouteGoal;
	if (state.kind != BotTimedRouteGoalKind::None &&
		state.kind != BotTimedRouteGoalKind::CtfRole &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.ctfRoleRouteOwnerDeferrals++;
		Bot_CommandRecordCtfRoleRouteLast(
			clientIndex,
			policy,
			state.untilMilliseconds - nowMilliseconds,
			0.0f);
		return;
	}
	if (state.kind == BotTimedRouteGoalKind::CtfRole &&
		state.untilMilliseconds > nowMilliseconds) {
		botFrameCommandStatus.ctfRoleRouteRefreshes++;
	}

	Vector3 direction = Bot_CommandTeamRoleRouteDirection(bot, policy);
	direction.z = 0.0f;
	if (direction.lengthSquared() < 1.0f) {
		botFrameCommandStatus.ctfRoleRouteInvalidSkips++;
		Bot_CommandRecordCtfRoleRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	direction = direction.normalized();
	Vector3 source = bot->s.origin - (direction * BOT_COMMAND_TEAM_ROLE_ROUTE_DISTANCE);
	source.z = bot->s.origin.z;
	if (!Bot_CommandActivateTimedRouteGoal(
			bot,
			BotTimedRouteGoalKind::CtfRole,
			source,
			direction,
			BOT_COMMAND_TEAM_ROLE_ROUTE_MILLISECONDS,
			BOT_COMMAND_TEAM_ROLE_ROUTE_DISTANCE,
			BOT_COMMAND_TIMED_ROUTE_MIN_DIRECTION_SQUARED)) {
		botFrameCommandStatus.ctfRoleRouteInvalidSkips++;
		Bot_CommandRecordCtfRoleRouteLast(
			clientIndex,
			policy,
			0,
			0.0f);
		return;
	}

	state.matchMode = static_cast<int>(policy.mode);
	state.matchRole = static_cast<int>(policy.role);
	state.matchLane = static_cast<int>(policy.lane);
	state.matchPriority = policy.priority;
	botFrameCommandStatus.ctfRoleRouteActivations++;
	Bot_CommandRecordCtfRoleRouteLast(
		clientIndex,
		policy,
		BOT_COMMAND_TEAM_ROLE_ROUTE_MILLISECONDS,
		(bot->s.origin - source).lengthSquared());
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

BotFrameObjectivePolicyResult Bot_CommandEvaluateFrameObjectivePolicies(
	gentity_t *bot,
	const BotActionDecision &actionDecision) {
	BotFrameObjectivePolicyResult result{};
	const BotObjectiveMatchContext matchContext =
		BotObjectives_BuildMatchContext(bot, BotObjectiveRole::None);
	const BotObjectiveMatchPolicy matchPolicy =
		BotObjectives_EvaluateMatchPolicy(matchContext);
	result.matchPolicy = matchPolicy;
	Bot_CommandActivateThreatRetreat(bot, actionDecision);
	Bot_CommandActivateFfaRoamRoute(bot, matchPolicy);
	Bot_CommandActivateTeamRoleRoute(bot, matchPolicy);
	Bot_CommandActivateCtfRoleRoute(bot, matchPolicy);
	const bool coopProgressWaitRequested =
		Bot_CommandCoopProgressWaitRequestedFor(bot);
	result.coopProgressWaitRequested = coopProgressWaitRequested;
	if (coopProgressWaitRequested) {
		botFrameCommandStatus.coopProgressWaitRequests++;
	}
	const bool coopLeadAdvanceRequested = Bot_CommandCoopLeadAdvanceRequested();
	if (coopLeadAdvanceRequested) {
		botFrameCommandStatus.coopLeadAdvanceRequests++;
	}
	const BotObjectiveCoopContext coopContext =
		BotObjectives_BuildCoopContext(
			bot,
			nullptr,
			coopProgressWaitRequested,
			BotObjectiveRole::None);
	const BotObjectiveCoopPolicy coopPolicy =
		BotObjectives_EvaluateCoopPolicy(coopContext);
	result.coopPolicy = coopPolicy;
	if (coopPolicy.waitForLeader) {
		botFrameCommandStatus.coopProgressWaitPolicyWaits++;
	}
	if (!Bot_CommandCoopResourceShareRequested() &&
		(!Bot_CommandCoopDoorElevatorEnabled() ||
		 Bot_CommandCoopLiveLoopEnabled())) {
		Bot_CommandActivateCoopLeaderRoute(bot, coopPolicy);
		Bot_CommandActivateCoopLeadAdvance(bot, coopPolicy, coopLeadAdvanceRequested);
	}

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

	return result;
}

bool Bot_CommandFindCtfRoleCombatFacts(gentity_t *bot, BotCombatEnemyFacts *facts) {
	if (facts == nullptr) {
		return false;
	}

	if (BotCombat_FindNearestEnemy(bot, facts)) {
		return true;
	}

	const int smokeMode = Bot_CommandSmokeScenarioMode();
	if (smokeMode == 36 || smokeMode == 43 || smokeMode == 44 ||
		smokeMode == 52 ||
		smokeMode == 48 || smokeMode == 49 || smokeMode == 74 ||
		smokeMode == 75) {
		return Bot_CommandFindSmokeEnemyFacts(bot, facts);
	}

	return false;
}

gentity_t *Bot_CommandCtfRoleCombatTargetFromFacts(
	const BotCombatEnemyFacts &facts) {
	if (!facts.valid ||
		!facts.visible ||
		!facts.shootable ||
		facts.enemyEntity <= 0 ||
		facts.enemyEntity >= static_cast<int>(globals.numEntities)) {
		return nullptr;
	}

	gentity_t *target = &g_entities[facts.enemyEntity];
	if (!Bot_PerceptionCombatTargetAlive(target) ||
		target->spawn_count != facts.enemySpawnCount) {
		return nullptr;
	}

	return target;
}

void Bot_CommandAdoptCtfRoleCombatTarget(
	gentity_t *bot,
	BotBrainBlackboardSlot &slot,
	gentity_t *target) {
	if (bot == nullptr || target == nullptr) {
		return;
	}

	bot->enemy = target;
	const BotPerceptionEnemyFacts perceptionFacts =
		Bot_PerceptionEvaluateEnemy(bot, target);
	if (Bot_PerceptionEnemyFactsValid(perceptionFacts)) {
		Bot_PerceptionSetCurrentEnemy(slot, perceptionFacts, true);
	}
}

BotActionDecision Bot_CommandApplyFfaRoleCombat(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy,
	const BotActionDecision &decision) {
	if (!Bot_CommandFfaRoleCombatEnabled()) {
		return decision;
	}

	botFrameCommandStatus.ffaRoleCombatRequests++;

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr ||
		bot == nullptr ||
		bot->client == nullptr ||
		!policy.valid ||
		!policy.participatesInScoring ||
		!Bot_CommandFfaStylePacingPolicyEnabled(policy) ||
		policy.role == BotObjectiveRole::None ||
		policy.lane == BotObjectiveLane::None ||
		!policy.wantsEngage) {
		botFrameCommandStatus.ffaRoleCombatInvalidSkips++;
		Bot_CommandRecordFfaRoleCombatLast(
			clientIndex,
			policy,
			nullptr,
			"invalid_policy");
		return decision;
	}

	botFrameCommandStatus.ffaRoleCombatPolicySelections++;

	BotCombatEnemyFacts facts{};
	if (!Bot_CommandFindCtfRoleCombatFacts(bot, &facts) ||
		Bot_CommandCtfRoleCombatTargetFromFacts(facts) == nullptr) {
		botFrameCommandStatus.ffaRoleCombatTargetDeferrals++;
		if (botFrameCommandStatus.ffaRoleCombatTargetSelections == 0) {
			Bot_CommandRecordFfaRoleCombatLast(
				clientIndex,
				policy,
				&facts,
				"no_shootable_target");
		}
		return decision;
	}

	gentity_t *target = Bot_CommandCtfRoleCombatTargetFromFacts(facts);
	Bot_CommandAdoptCtfRoleCombatTarget(bot, *slot, target);
	botFrameCommandStatus.ffaRoleCombatTargetSelections++;

	const int priority = std::max(policy.engagePriority, policy.priority) +
		BOT_COMMAND_FFA_ROLE_COMBAT_PRIORITY_BONUS;
	if (decision.intent != BotActionIntent::Attack ||
		!decision.pressAttack ||
		priority > decision.priority) {
		botFrameCommandStatus.ffaRoleCombatDecisionOverrides++;
	}
	botFrameCommandStatus.ffaRoleCombatAttackDecisions++;
	Bot_CommandRecordFfaRoleCombatLast(
		clientIndex,
		policy,
		&facts,
		"ffa_role_combat_engage");

	return {
		.intent = BotActionIntent::Attack,
		.clientIndex = clientIndex,
		.priority = priority,
		.weaponItem = decision.weaponItem > IT_NULL ?
			decision.weaponItem : Bot_CommandCurrentWeaponItem(bot),
		.pressAttack = true,
		.reason = "ffa_role_combat_engage",
	};
}

BotActionDecision Bot_CommandApplyTeamRoleCombat(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy,
	const BotActionDecision &decision) {
	if (!Bot_CommandTeamRoleCombatEnabled()) {
		return decision;
	}

	botFrameCommandStatus.teamRoleCombatRequests++;

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr ||
		bot == nullptr ||
		bot->client == nullptr ||
		!policy.valid ||
		!policy.participatesInScoring ||
		policy.mode != BotObjectiveMatchMode::TeamDeathmatch ||
		policy.role == BotObjectiveRole::None ||
		policy.lane == BotObjectiveLane::None ||
		!policy.wantsEngage) {
		botFrameCommandStatus.teamRoleCombatInvalidSkips++;
		Bot_CommandRecordTeamRoleCombatLast(
			clientIndex,
			policy,
			nullptr,
			"invalid_policy");
		return decision;
	}

	botFrameCommandStatus.teamRoleCombatPolicySelections++;

	BotCombatEnemyFacts facts{};
	if (!Bot_CommandFindCtfRoleCombatFacts(bot, &facts) ||
		Bot_CommandCtfRoleCombatTargetFromFacts(facts) == nullptr) {
		botFrameCommandStatus.teamRoleCombatTargetDeferrals++;
		Bot_CommandRecordTeamRoleCombatLast(
			clientIndex,
			policy,
			&facts,
			"no_shootable_target");
		return decision;
	}

	gentity_t *target = Bot_CommandCtfRoleCombatTargetFromFacts(facts);
	Bot_CommandAdoptCtfRoleCombatTarget(bot, *slot, target);
	botFrameCommandStatus.teamRoleCombatTargetSelections++;

	const int priority = std::max(policy.engagePriority, policy.priority) +
		BOT_COMMAND_TEAM_ROLE_COMBAT_PRIORITY_BONUS;
	if (decision.intent != BotActionIntent::Attack ||
		!decision.pressAttack ||
		priority > decision.priority) {
		botFrameCommandStatus.teamRoleCombatDecisionOverrides++;
	}
	botFrameCommandStatus.teamRoleCombatAttackDecisions++;
	Bot_CommandRecordTeamRoleCombatLast(
		clientIndex,
		policy,
		&facts,
		"team_role_combat_engage");

	return {
		.intent = BotActionIntent::Attack,
		.clientIndex = clientIndex,
		.priority = priority,
		.weaponItem = decision.weaponItem > IT_NULL ?
			decision.weaponItem : Bot_CommandCurrentWeaponItem(bot),
		.pressAttack = true,
		.reason = "team_role_combat_engage",
	};
}

BotActionDecision Bot_CommandApplyCtfRoleCombat(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy,
	const BotActionDecision &decision) {
	if (!Bot_CommandCtfRoleCombatEnabled()) {
		return decision;
	}

	botFrameCommandStatus.ctfRoleCombatRequests++;

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr ||
		bot == nullptr ||
		bot->client == nullptr ||
		!policy.valid ||
		!policy.participatesInScoring ||
		policy.mode != BotObjectiveMatchMode::CaptureTheFlag ||
		policy.role == BotObjectiveRole::None ||
		policy.lane == BotObjectiveLane::None ||
		!policy.wantsEngage) {
		botFrameCommandStatus.ctfRoleCombatInvalidSkips++;
		Bot_CommandRecordCtfRoleCombatLast(
			clientIndex,
			policy,
			nullptr,
			"invalid_policy");
		return decision;
	}

	botFrameCommandStatus.ctfRoleCombatPolicySelections++;

	BotCombatEnemyFacts facts{};
	if (!Bot_CommandFindCtfRoleCombatFacts(bot, &facts) ||
		Bot_CommandCtfRoleCombatTargetFromFacts(facts) == nullptr) {
		botFrameCommandStatus.ctfRoleCombatTargetDeferrals++;
		Bot_CommandRecordCtfRoleCombatLast(
			clientIndex,
			policy,
			&facts,
			"no_shootable_target");
		return decision;
	}

	gentity_t *target = Bot_CommandCtfRoleCombatTargetFromFacts(facts);
	Bot_CommandAdoptCtfRoleCombatTarget(bot, *slot, target);
	botFrameCommandStatus.ctfRoleCombatTargetSelections++;

	const int priority = std::max(policy.engagePriority, policy.priority) +
		BOT_COMMAND_CTF_ROLE_COMBAT_PRIORITY_BONUS;
	if (decision.intent != BotActionIntent::Attack ||
		!decision.pressAttack ||
		priority > decision.priority) {
		botFrameCommandStatus.ctfRoleCombatDecisionOverrides++;
	}
	botFrameCommandStatus.ctfRoleCombatAttackDecisions++;
	Bot_CommandRecordCtfRoleCombatLast(
		clientIndex,
		policy,
		&facts,
		"ctf_role_combat_engage");

	return {
		.intent = BotActionIntent::Attack,
		.clientIndex = clientIndex,
		.priority = priority,
		.weaponItem = decision.weaponItem > IT_NULL ?
			decision.weaponItem : Bot_CommandCurrentWeaponItem(bot),
		.pressAttack = true,
		.reason = "ctf_role_combat_engage",
	};
}

gentity_t *Bot_CommandTeamFireAvoidanceTarget(gentity_t *bot) {
	if (bot == nullptr) {
		return nullptr;
	}

	if (Bot_CommandSmokeEngageEnemy()) {
		BotCombatEnemyFacts facts{};
		if (Bot_CommandFindSmokeEnemyFacts(bot, &facts) &&
			facts.valid &&
			facts.enemyEntity > 0 &&
			facts.enemyEntity < static_cast<int>(globals.numEntities)) {
			gentity_t *enemy = &g_entities[facts.enemyEntity];
			if (Bot_PerceptionCombatTargetAlive(enemy) &&
				enemy->spawn_count == facts.enemySpawnCount) {
				return enemy;
			}
		}
	}

	if (Bot_PerceptionCombatTargetAlive(bot->enemy)) {
		return bot->enemy;
	}

	return Bot_CommandKnownVisibleEnemy(bot);
}

BotActionDecision Bot_CommandApplyFfaSpawnCampCombatAvoidance(
	gentity_t *bot,
	const BotObjectiveMatchPolicy &policy,
	const BotActionDecision &decision) {
	if (!Bot_CommandFfaSpawnCampCombatAvoidanceEnabled() ||
		!decision.pressAttack) {
		return decision;
	}

	botFrameCommandStatus.ffaSpawnCampCombatAvoidanceEvaluations++;
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	Bot_CommandRecordFfaSpawnCampCombatAvoidanceLast(
		clientIndex,
		policy,
		nullptr,
		nullptr,
		0.0f,
		false,
		"no_target");

	if (bot == nullptr ||
		bot->client == nullptr ||
		!policy.valid ||
		!policy.participatesInScoring ||
		!Bot_CommandFfaStylePacingPolicyEnabled(policy) ||
		!policy.avoidSpawnCamping) {
		botFrameCommandStatus.ffaSpawnCampCombatAvoidanceInvalidSkips++;
		Bot_CommandRecordFfaSpawnCampCombatAvoidanceLast(
			clientIndex,
			policy,
			nullptr,
			nullptr,
			0.0f,
			false,
			"invalid_policy");
		return decision;
	}

	gentity_t *target = Bot_CommandTeamFireAvoidanceTarget(bot);
	if (target == nullptr) {
		botFrameCommandStatus.ffaSpawnCampCombatAvoidanceInvalidSkips++;
		return decision;
	}

	float sourceDistanceSquared = 0.0f;
	gentity_t *source = Bot_CommandFfaSpawnCampAvoidanceSource(
		bot,
		&sourceDistanceSquared);
	if (source == nullptr) {
		botFrameCommandStatus.ffaSpawnCampCombatAvoidanceClears++;
		Bot_CommandRecordFfaSpawnCampCombatAvoidanceLast(
			clientIndex,
			policy,
			target,
			nullptr,
			0.0f,
			false,
			"no_spawn_camp_source");
		return decision;
	}

	const int sourceEntity = Bot_PerceptionEntityNumber(source);
	const int targetEntity = Bot_PerceptionEntityNumber(target);
	const bool sourceIsTarget =
		source == target ||
		(sourceEntity >= 0 &&
		 sourceEntity == targetEntity &&
		 source->spawn_count == target->spawn_count);
	if (!sourceIsTarget) {
		botFrameCommandStatus.ffaSpawnCampCombatAvoidanceClears++;
		Bot_CommandRecordFfaSpawnCampCombatAvoidanceLast(
			clientIndex,
			policy,
			target,
			source,
			sourceDistanceSquared,
			false,
			"different_source");
		return decision;
	}

	botFrameCommandStatus.ffaSpawnCampCombatAvoidanceBlocks++;
	botFrameCommandStatus.ffaSpawnCampCombatAvoidanceSourceBlocks++;
	Bot_CommandRecordFfaSpawnCampCombatAvoidanceLast(
		clientIndex,
		policy,
		target,
		source,
		sourceDistanceSquared,
		true,
		"spawn_camp_source");
	return {
		.clientIndex = decision.clientIndex,
		.reason = "ffa_spawn_camp_combat_avoidance",
	};
}

BotActionDecision Bot_CommandApplyTeamFireAvoidance(
	gentity_t *bot,
	const BotActionDecision &decision) {
	if (!Bot_CommandTeamFireAvoidanceEnabled() || !decision.pressAttack) {
		return decision;
	}

	botFrameCommandStatus.teamFireAvoidanceEvaluations++;
	botFrameCommandStatus.lastTeamFireAvoidanceClient =
		Bot_PerceptionClientIndex(bot);
	botFrameCommandStatus.lastTeamFireAvoidanceTargetClient = -1;
	botFrameCommandStatus.lastTeamFireAvoidanceFriendlyLine = 0;
	botFrameCommandStatus.lastTeamFireAvoidanceTargetAllowed = 1;
	botFrameCommandStatus.lastTeamFireAvoidanceBlocked = 0;
	botFrameCommandStatus.lastTeamFireAvoidanceReason = "no_target";

	gentity_t *target = Bot_CommandTeamFireAvoidanceTarget(bot);
	if (target == nullptr) {
		botFrameCommandStatus.teamFireAvoidanceInvalidSkips++;
		return decision;
	}

	const bool friendlyInLineOfFire =
		Bot_CommandSmokeTeamFireLineOfFireProof(bot, target) ||
		Bot_CommandFriendlyInLineOfFire(bot, target);
	const BotObjectiveFriendlyFireContext context =
		BotObjectives_BuildFriendlyFireContext(
			bot,
			target,
			friendlyInLineOfFire);
	const BotObjectiveFriendlyFirePolicy policy =
		BotObjectives_EvaluateFriendlyFirePolicy(context);
	if (!policy.valid) {
		botFrameCommandStatus.teamFireAvoidanceInvalidSkips++;
		return decision;
	}

	botFrameCommandStatus.lastTeamFireAvoidanceTargetClient =
		context.targetClient;
	botFrameCommandStatus.lastTeamFireAvoidanceFriendlyLine =
		friendlyInLineOfFire ? 1 : 0;
	botFrameCommandStatus.lastTeamFireAvoidanceTargetAllowed =
		policy.targetAllowed ? 1 : 0;
	botFrameCommandStatus.lastTeamFireAvoidanceReason =
		policy.reason != nullptr ? policy.reason : "none";

	if (!policy.targetAllowed) {
		botFrameCommandStatus.teamFireAvoidanceTargetBlocks++;
	}
	if (friendlyInLineOfFire && policy.shouldAvoidFire) {
		botFrameCommandStatus.teamFireAvoidanceLineBlocks++;
	}
	if (!policy.shouldAvoidFire && policy.targetAllowed) {
		botFrameCommandStatus.teamFireAvoidanceClears++;
		return decision;
	}

	botFrameCommandStatus.teamFireAvoidanceBlocks++;
	botFrameCommandStatus.lastTeamFireAvoidanceBlocked = 1;
	return {
		.clientIndex = decision.clientIndex,
		.reason = policy.reason,
	};
}

BotActionDecision Bot_CommandApplyThreatRetreatAttackSuppression(
	gentity_t *bot,
	const BotActionDecision &decision) {
	if (!Bot_CommandThreatRetreatEnabled() ||
		!decision.pressAttack ||
		!Bot_CommandThreatRetreatActive(bot)) {
		return decision;
	}

	BotActionDecision suppressed = decision;
	suppressed.intent = BotActionIntent::None;
	suppressed.priority = 0;
	suppressed.pressAttack = false;
	suppressed.reason = "threat_retreat";
	botFrameCommandStatus.threatRetreatAttackSuppressions++;
	return suppressed;
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
	case BOT_COMMAND_TRAVEL_TELEPORT:
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

bool Bot_CommandApplyControlledInactiveRecovery(gentity_t *bot, usercmd_t *cmd) {
	const int mode = Bot_CommandControlledInactiveRecoveryMode();
	if (mode <= 0) {
		return false;
	}

	botFrameCommandStatus.controlledInactiveRecoveryAttempts++;
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	botFrameCommandStatus.lastControlledInactiveRecoveryClient = clientIndex;
	botFrameCommandStatus.lastControlledInactiveRecoveryMode = mode;
	botFrameCommandStatus.lastControlledInactiveRecoveryButtons = 0;

	if (bot == nullptr || bot->client == nullptr || cmd == nullptr || clientIndex < 0) {
		botFrameCommandStatus.controlledInactiveRecoveryInvalidSkips++;
		return false;
	}

	if (!ClientIsPlaying(bot->client) && !bot->client->eliminated && mode >= 2) {
		const Team target = Teams() ? PickTeam(clientIndex) : Team::Free;
		if (SetTeam(bot, target, false, true, true)) {
			botFrameCommandStatus.controlledInactiveRecoverySpectatorJoins++;
			return false;
		}
	}

	if (ClientIsPlaying(bot->client) && bot->deadFlag && !bot->client->eliminated) {
		*cmd = {};
		cmd->msec = Bot_CommandMsec();
		cmd->serverFrame = gi.ServerFrame();
		cmd->buttons |= BUTTON_ATTACK;
		botFrameCommandStatus.controlledInactiveRecoveryCommands++;
		botFrameCommandStatus.controlledInactiveRecoveryRespawnCommands++;
		botFrameCommandStatus.lastControlledInactiveRecoveryButtons =
			static_cast<int>(cmd->buttons);
		return true;
	}

	if (!ClientIsPlaying(bot->client) || bot->client->eliminated) {
		botFrameCommandStatus.controlledInactiveRecoverySpectatorSkips++;
	}
	return false;
}

void Bot_CommandClearCoopInteractionRetryOwner(int clientIndex) {
	if (clientIndex >= 0 &&
		clientIndex < static_cast<int>(botCoopInteractionRetryOwners.size())) {
		botCoopInteractionRetryOwners[clientIndex] = false;
	}
}

void Bot_CommandClearCoopDoorElevatorOwner(int clientIndex) {
	if (clientIndex >= 0 &&
		clientIndex < static_cast<int>(botCoopDoorElevatorOwners.size())) {
		botCoopDoorElevatorOwners[clientIndex] = false;
	}
}

void Bot_CommandRecordCoopInteractionRetryLast(
	int clientIndex,
	const BotNavRecoveryMove &recovery) {
	botFrameCommandStatus.lastCoopInteractionRetryClient = clientIndex;
	botFrameCommandStatus.lastCoopInteractionRetryAction = recovery.interactionAction;
	botFrameCommandStatus.lastCoopInteractionRetryKind = recovery.interactionKind;
	botFrameCommandStatus.lastCoopInteractionRetryEntity = recovery.interactionEntity;
}

bool Bot_CommandCoopDoorElevatorInteractionSupported(
	const BotNavRecoveryMove &recovery) {
	if (recovery.interactionAction <= 0) {
		return false;
	}

	switch (recovery.interactionKind) {
	case 1: // Door
	case 3: // Platform
	case 4: // Train
	case 6: // Trigger
	case 7: // Generic mover
		return true;
	default:
		return false;
	}
}

void Bot_CommandRecordCoopDoorElevatorLast(
	int clientIndex,
	int sourceClient,
	const BotObjectiveCoopPolicy &policy,
	const BotNavRecoveryMove &recovery) {
	botFrameCommandStatus.lastCoopDoorElevatorClient = clientIndex;
	botFrameCommandStatus.lastCoopDoorElevatorSourceClient = sourceClient;
	botFrameCommandStatus.lastCoopDoorElevatorAction = recovery.interactionAction;
	botFrameCommandStatus.lastCoopDoorElevatorKind = recovery.interactionKind;
	botFrameCommandStatus.lastCoopDoorElevatorEntity = recovery.interactionEntity;
	botFrameCommandStatus.lastCoopDoorElevatorIntent = static_cast<int>(policy.intent);
}

void Bot_CommandApplyRecoveryMove(const gentity_t *bot, usercmd_t *cmd) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	BotNavRecoveryMove recovery{};
	if (!BotNav_GetRecoveryMove(bot, &recovery)) {
		Bot_CommandClearCoopInteractionRetryOwner(clientIndex);
		Bot_CommandClearCoopDoorElevatorOwner(clientIndex);
		return;
	}

	const bool coopInteractionRetryOwner =
		clientIndex >= 0 &&
		clientIndex < static_cast<int>(botCoopInteractionRetryOwners.size()) &&
		botCoopInteractionRetryOwners[clientIndex] &&
		Bot_CommandCoopInteractionRetryEnabled() &&
		recovery.interactionAction != 0;
	const bool coopDoorElevatorOwner =
		clientIndex >= 0 &&
		clientIndex < static_cast<int>(botCoopDoorElevatorOwners.size()) &&
		botCoopDoorElevatorOwners[clientIndex] &&
		Bot_CommandCoopDoorElevatorEnabled() &&
		Bot_CommandCoopDoorElevatorInteractionSupported(recovery);

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

	if (coopInteractionRetryOwner && (recovery.wait || recovery.use)) {
		botFrameCommandStatus.coopInteractionRetryCommands++;
		Bot_CommandRecordCoopInteractionRetryLast(clientIndex, recovery);
	}
	if (coopDoorElevatorOwner && (recovery.wait || recovery.use)) {
		BotObjectiveCoopPolicy sourcePolicy{};
		botFrameCommandStatus.coopDoorElevatorSourceCommands++;
		Bot_CommandRecordCoopDoorElevatorLast(
			clientIndex,
			clientIndex,
			sourcePolicy,
			recovery);
	}
	if (!coopInteractionRetryOwner ||
		recovery.interactionAction == 0 ||
		recovery.framesRemaining <= 1) {
		Bot_CommandClearCoopInteractionRetryOwner(clientIndex);
	}
	if (!coopDoorElevatorOwner ||
		recovery.interactionAction == 0 ||
		recovery.framesRemaining <= 1) {
		Bot_CommandClearCoopDoorElevatorOwner(clientIndex);
	}

	botFrameCommandStatus.recoveryCommandUses++;
	botFrameCommandStatus.lastRecoveryForwardMove = static_cast<int>(cmd->forwardMove);
	botFrameCommandStatus.lastRecoverySideMove = static_cast<int>(cmd->sideMove);
	botFrameCommandStatus.lastRecoveryFramesRemaining = recovery.framesRemaining;
}

bool Bot_CommandCoopDoorElevatorSourcePolicy(
	int clientIndex,
	const BotObjectiveCoopPolicy &policy) {
	if (!policy.valid || !policy.coopMode || clientIndex < 0) {
		return false;
	}
	if (policy.intent == BotObjectiveCoopIntent::LeadAdvance) {
		return true;
	}

	for (int candidateClient = 0;
		candidateClient < static_cast<int>(game.maxClients) &&
		candidateClient < static_cast<int>(botCoopDoorElevatorOwners.size());
		++candidateClient) {
		const gentity_t *candidate = Bot_CommandClientEntity(candidateClient);
		if (!Bot_PerceptionEntityAlive(candidate)) {
			continue;
		}
		return candidateClient == clientIndex;
	}

	return false;
}

void Bot_CommandRequestCoopDoorElevator(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy,
	const BotLibAdapterRouteSteer &route) {
	if (!Bot_CommandCoopDoorElevatorEnabled()) {
		return;
	}

	botFrameCommandStatus.coopDoorElevatorRequests++;
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (!Bot_CommandCoopDoorElevatorSourcePolicy(clientIndex, policy)) {
		return;
	}
	if (bot == nullptr || bot->client == nullptr || clientIndex < 0) {
		botFrameCommandStatus.coopDoorElevatorInvalidSkips++;
		botFrameCommandStatus.lastCoopDoorElevatorClient = clientIndex;
		return;
	}

	if (!BotNav_RequestInteractionRetry(bot, &route, false)) {
		return;
	}

	BotNavRecoveryMove recovery{};
	if (!BotNav_GetRecoveryMove(bot, &recovery) ||
		!Bot_CommandCoopDoorElevatorInteractionSupported(recovery)) {
		botFrameCommandStatus.coopDoorElevatorInvalidSkips++;
		return;
	}

	const bool alreadyOwned = botCoopDoorElevatorOwners[clientIndex];
	botCoopDoorElevatorOwners[clientIndex] = true;
	if (!alreadyOwned) {
		botFrameCommandStatus.coopDoorElevatorSourceActivations++;
	}
	Bot_CommandRecordCoopDoorElevatorLast(
		clientIndex,
		clientIndex,
		policy,
		recovery);
}

bool Bot_CommandFindCoopDoorElevatorSource(
	int clientIndex,
	const BotObjectiveCoopPolicy &policy,
	int *sourceClient,
	BotNavRecoveryMove *sourceMove) {
	if (sourceClient != nullptr) {
		*sourceClient = -1;
	}
	if (sourceMove != nullptr) {
		*sourceMove = {};
	}

	auto trySource = [&](int candidateClient) -> bool {
		if (candidateClient < 0 ||
			candidateClient == clientIndex ||
			candidateClient >= static_cast<int>(botCoopDoorElevatorOwners.size()) ||
			!botCoopDoorElevatorOwners[candidateClient]) {
			return false;
		}

		gentity_t *source = Bot_CommandClientEntity(candidateClient);
		if (!Bot_PerceptionEntityAlive(source)) {
			Bot_CommandClearCoopDoorElevatorOwner(candidateClient);
			return false;
		}

		BotNavRecoveryMove recovery{};
		if (!BotNav_GetRecoveryMove(source, &recovery) ||
			!Bot_CommandCoopDoorElevatorInteractionSupported(recovery)) {
			Bot_CommandClearCoopDoorElevatorOwner(candidateClient);
			return false;
		}

		if (sourceClient != nullptr) {
			*sourceClient = candidateClient;
		}
		if (sourceMove != nullptr) {
			*sourceMove = recovery;
		}
		return true;
	};

	if (policy.hasLeader && trySource(policy.leaderClient)) {
		return true;
	}

	for (int candidateClient = 0;
		candidateClient < static_cast<int>(botCoopDoorElevatorOwners.size()) &&
		candidateClient < static_cast<int>(game.maxClients);
		++candidateClient) {
		if (trySource(candidateClient)) {
			return true;
		}
	}

	return false;
}

bool Bot_CommandCoopDoorElevatorSupportPolicy(
	int clientIndex,
	const BotObjectiveCoopPolicy &policy) {
	return Bot_CommandCoopDoorElevatorEnabled() &&
		clientIndex >= 0 &&
		policy.valid &&
		policy.coopMode &&
		policy.hasLeader &&
		!Bot_CommandCoopDoorElevatorSourcePolicy(clientIndex, policy);
}

bool Bot_CommandApplyCoopDoorElevatorHold(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy,
	usercmd_t *cmd) {
	if (!Bot_CommandCoopDoorElevatorEnabled()) {
		return false;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (bot == nullptr ||
		bot->client == nullptr ||
		cmd == nullptr ||
		clientIndex < 0 ||
		!policy.valid ||
		!policy.coopMode ||
		!policy.hasLeader) {
		return false;
	}

	int sourceClient = -1;
	BotNavRecoveryMove sourceMove{};
	if (!Bot_CommandFindCoopDoorElevatorSource(
		clientIndex,
		policy,
		&sourceClient,
		&sourceMove)) {
		return false;
	}

	cmd->forwardMove = 0.0f;
	cmd->sideMove = 0.0f;
	cmd->buttons &= ~(BUTTON_JUMP | BUTTON_CROUCH);

	if (sourceMove.interactionEntity >= 0 &&
		sourceMove.interactionEntity < static_cast<int>(globals.numEntities)) {
		const gentity_t *interaction = &g_entities[sourceMove.interactionEntity];
		if (interaction->inUse) {
			cmd->angles = Bot_CommandAnglesToPoint(bot, interaction->s.origin);
		}
	} else if (gentity_t *source = Bot_CommandClientEntity(sourceClient);
		Bot_PerceptionEntityAlive(source)) {
		cmd->angles = Bot_CommandAnglesToPoint(bot, source->s.origin);
	}

	botFrameCommandStatus.coopDoorElevatorHoldCommands++;
	Bot_CommandRecordCoopDoorElevatorLast(
		clientIndex,
		sourceClient,
		policy,
		sourceMove);
	return true;
}

bool Bot_CommandApplyCoopDoorElevatorSupportIdle(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy,
	usercmd_t *cmd) {
	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (bot == nullptr ||
		bot->client == nullptr ||
		cmd == nullptr ||
		!Bot_CommandCoopDoorElevatorSupportPolicy(clientIndex, policy)) {
		return false;
	}

	cmd->forwardMove = 0.0f;
	cmd->sideMove = 0.0f;
	cmd->buttons &= ~(BUTTON_JUMP | BUTTON_CROUCH);

	if (gentity_t *leader = Bot_CommandClientEntity(policy.leaderClient);
		Bot_PerceptionEntityAlive(leader)) {
		cmd->angles = Bot_CommandAnglesToPoint(bot, leader->s.origin);
	}

	botFrameCommandStatus.coopDoorElevatorHoldCommands++;
	return true;
}

void Bot_CommandRecordCoopProgressWaitLast(
	int clientIndex,
	const BotObjectiveCoopPolicy &policy) {
	botFrameCommandStatus.lastCoopProgressWaitClient = clientIndex;
	botFrameCommandStatus.lastCoopProgressWaitLeaderClient = policy.leaderClient;
	botFrameCommandStatus.lastCoopProgressWaitIntent = static_cast<int>(policy.intent);
	botFrameCommandStatus.lastCoopProgressWaitLeaderDistanceSquared =
		std::max(policy.leaderDistanceSquared, 0);
}

bool Bot_CommandApplyCoopProgressWait(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy,
	usercmd_t *cmd) {
	if (!policy.valid || !policy.coopMode || !policy.waitForLeader) {
		return false;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	if (bot == nullptr || bot->client == nullptr || cmd == nullptr || clientIndex < 0) {
		botFrameCommandStatus.coopProgressWaitInvalidSkips++;
		Bot_CommandRecordCoopProgressWaitLast(clientIndex, policy);
		return false;
	}

	cmd->forwardMove = 0.0f;
	cmd->sideMove = 0.0f;
	cmd->buttons &= ~(BUTTON_JUMP | BUTTON_CROUCH);

	if (gentity_t *leader = Bot_CommandClientEntity(policy.leaderClient);
		Bot_PerceptionEntityAlive(leader) && leader != bot) {
		cmd->angles = Bot_CommandAnglesToPoint(bot, leader->s.origin);
	}

	botFrameCommandStatus.coopProgressWaitCommands++;
	Bot_CommandRecordCoopProgressWaitLast(clientIndex, policy);
	return true;
}

void Bot_CommandRecordCoopAntiBlockLast(
	int clientIndex,
	const BotObjectiveCoopPolicy &policy,
	int forwardMove,
	int sideMove) {
	botFrameCommandStatus.lastCoopAntiBlockClient = clientIndex;
	botFrameCommandStatus.lastCoopAntiBlockLeaderClient = policy.leaderClient;
	botFrameCommandStatus.lastCoopAntiBlockIntent = static_cast<int>(policy.intent);
	botFrameCommandStatus.lastCoopAntiBlockLeaderDistanceSquared =
		std::max(policy.leaderDistanceSquared, 0);
	botFrameCommandStatus.lastCoopAntiBlockForwardMove = forwardMove;
	botFrameCommandStatus.lastCoopAntiBlockSideMove = sideMove;
}

bool Bot_CommandCoopAntiBlockPolicyClose(const BotObjectiveCoopPolicy &policy) {
	const float antiBlockDistanceSquared =
		Bot_CommandCoopLiveLoopEnabled()
			? 320.0f * 320.0f
			: BOT_COMMAND_COOP_ANTI_BLOCK_DISTANCE_SQUARED;

	return policy.valid &&
		policy.coopMode &&
		policy.hasLeader &&
		policy.leaderDistanceSquared >= 0 &&
		policy.leaderDistanceSquared <=
			static_cast<int>(antiBlockDistanceSquared);
}

bool Bot_CommandApplyCoopAntiBlocking(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy,
	usercmd_t *cmd) {
	if (!Bot_CommandCoopAntiBlockingEnabled()) {
		return false;
	}

	botFrameCommandStatus.coopAntiBlockRequests++;
	if (!Bot_CommandCoopAntiBlockPolicyClose(policy)) {
		return false;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	gentity_t *leader = Bot_CommandClientEntity(policy.leaderClient);
	if (bot == nullptr ||
		bot->client == nullptr ||
		cmd == nullptr ||
		clientIndex < 0 ||
		!Bot_PerceptionEntityAlive(leader) ||
		leader == bot) {
		botFrameCommandStatus.coopAntiBlockInvalidSkips++;
		Bot_CommandRecordCoopAntiBlockLast(clientIndex, policy, 0, 0);
		return false;
	}

	botFrameCommandStatus.coopAntiBlockPolicyClose++;
	const float sideSign = (clientIndex & 1) == 0 ? -1.0f : 1.0f;
	cmd->forwardMove = BOT_COMMAND_COOP_ANTI_BLOCK_FORWARD_MOVE;
	cmd->sideMove = BOT_COMMAND_COOP_ANTI_BLOCK_SIDE_MOVE * sideSign;
	cmd->buttons &= ~(BUTTON_JUMP | BUTTON_CROUCH);
	cmd->angles = Bot_CommandAnglesToPoint(bot, leader->s.origin);

	botFrameCommandStatus.coopAntiBlockCommands++;
	Bot_CommandRecordCoopAntiBlockLast(
		clientIndex,
		policy,
		static_cast<int>(cmd->forwardMove),
		static_cast<int>(cmd->sideMove));
	return true;
}

void Bot_CommandRequestCoopInteractionRetry(
	gentity_t *bot,
	const BotObjectiveCoopPolicy &policy,
	const BotLibAdapterRouteSteer &route) {
	if (!Bot_CommandCoopInteractionRetryEnabled() ||
		!policy.valid ||
		!policy.coopMode) {
		return;
	}

	const int clientIndex = Bot_PerceptionClientIndex(bot);
	botFrameCommandStatus.coopInteractionRetryRequests++;
	if (bot == nullptr || bot->client == nullptr || clientIndex < 0) {
		botFrameCommandStatus.coopInteractionRetryInvalidSkips++;
		botFrameCommandStatus.lastCoopInteractionRetryClient = clientIndex;
		return;
	}

	if (!BotNav_RequestInteractionRetry(bot, &route, false)) {
		return;
	}

	const bool alreadyOwned = botCoopInteractionRetryOwners[clientIndex];
	botCoopInteractionRetryOwners[clientIndex] = true;
	if (!alreadyOwned) {
		botFrameCommandStatus.coopInteractionRetryActivations++;
	}
	botFrameCommandStatus.lastCoopInteractionRetryClient = clientIndex;
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
	if (Bot_CommandSmokeEngageEnemy() ||
		Bot_CommandSmokeWeaponSwitch() ||
		Bot_CommandSmokeWeaponScoring() ||
		Bot_CommandSmokeAimFirePolicy()) {
		BotCombatEnemyFacts facts{};
		if (Bot_CommandFindSmokeEnemyFacts(bot, &facts)) {
			Bot_CommandApplySmokeEnemyFacts(bot, &actionContext, facts);
		}
	}
	if (Bot_CommandSmokeForcesImmediateCombatFire()) {
		actionContext.combat.aimPolicyEnabled = false;
		actionContext.combat.currentWeaponItem = Bot_CommandCurrentWeaponItem(bot);
		actionContext.combat.preferredWeaponItem = actionContext.combat.currentWeaponItem;
		actionContext.combat.currentWeaponReady = true;
		actionContext.combat.preferredWeaponReady = false;
		actionContext.combat.skillAllowsFire = true;
	} else if (!Bot_CommandSmokeAimFirePolicy()) {
		BotActions_EnrichCombatInventory(bot, &actionContext);
	}
	if (Bot_CommandSmokeAimFirePolicy()) {
		actionContext.combat.currentWeaponItem = IT_WEAPON_RLAUNCHER;
		actionContext.combat.preferredWeaponItem = IT_WEAPON_RLAUNCHER;
		actionContext.combat.currentWeaponAmmo =
			bot != nullptr && bot->client != nullptr ?
				bot->client->pers.inventory[IT_AMMO_ROCKETS] :
				actionContext.combat.currentWeaponAmmo;
		actionContext.combat.currentWeaponReady = true;
	}
	BotActions_EnrichInventoryUse(bot, &actionContext);
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
		if (Bot_PerceptionCombatTargetAlive(bot != nullptr ? bot->enemy : nullptr)) {
			return Bot_CommandAnglesToPoint(
				bot,
				Bot_CommandAimPointForKnownEnemy(bot, bot->enemy));
		}
	}

	if (Bot_CommandSmokeAimFirePolicy()) {
		BotCombatEnemyFacts facts{};
		if (Bot_CommandFindSmokeEnemyFacts(bot, &facts) &&
			facts.enemyEntity > 0 &&
			facts.enemyEntity < static_cast<int>(globals.numEntities)) {
			return Bot_CommandAnglesToPoint(bot, Bot_CommandSmokeLiveAimPoint(bot, facts));
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
	if (submitted && actionApply.inventoryUsePending) {
		Bot_CommandActivateNukeRetreat(bot, request);
		Bot_CommandActivateTeleporterEscapeRoute(bot, request);
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

void Bot_CommandRecordThreatRetreatReengage(
	gentity_t *bot,
	const BotActionApplyResult &actionApply) {
	if (!Bot_CommandThreatRetreatEnabled() ||
		!actionApply.attackButtonApplied ||
		bot == nullptr ||
		bot->client == nullptr) {
		return;
	}

	int clientIndex = -1;
	BotBrainBlackboardSlot *slot = Bot_BlackboardEnsureSlot(bot, &clientIndex);
	if (slot == nullptr ||
		clientIndex < 0 ||
		slot->threatRetreatLastActivationMilliseconds <= 0 ||
		slot->threatRetreatReengageRecorded) {
		return;
	}

	const int nowMilliseconds = Bot_CommandCurrentTimeMilliseconds();
	if (Bot_CommandThreatRetreatActive(bot) ||
		Bot_CommandElapsedMilliseconds(
			nowMilliseconds,
			slot->threatRetreatLastActivationMilliseconds) <
			BOT_COMMAND_THREAT_RETREAT_MILLISECONDS) {
		return;
	}

	slot->threatRetreatReengageRecorded = true;
	botFrameCommandStatus.threatRetreatReengages++;
	Bot_CommandRecordThreatRetreatLast(
		clientIndex,
		nullptr,
		0.0f,
		0,
		0.0f,
		std::max(bot->health, 0),
		Bot_PerceptionArmorValue(bot),
		bot->health <= BOT_COMMAND_THREAT_RETREAT_LOW_HEALTH,
		false,
		"reengage");
}

void BotBrain_PrintFfaRoamRouteStatus() {
	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "ffa_roam_route_requests={} "
			  "ffa_roam_route_policy_selections={} "
			  "ffa_roam_route_activations={} "
			  "ffa_roam_route_refreshes={} "
			  "ffa_roam_route_owner_deferrals={} "
			  "ffa_roam_route_route_requests={} "
			  "ffa_roam_route_route_deferrals={} "
			  "ffa_roam_route_expirations={} "
			  "ffa_roam_route_invalid_skips={} "
			  "last_ffa_roam_route_client={} "
			  "last_ffa_roam_route_mode={} "
			  "last_ffa_roam_route_mode_name={} "
			  "last_ffa_roam_route_role={} "
			  "last_ffa_roam_route_role_name={} "
			  "last_ffa_roam_route_lane={} "
			  "last_ffa_roam_route_lane_name={} "
			  "last_ffa_roam_route_priority={} "
			  "last_ffa_roam_route_remaining_ms={} "
			  "last_ffa_roam_route_goal_distance_sq={}\n",
			  botFrameCommandStatus.ffaRoamRouteRequests,
			  botFrameCommandStatus.ffaRoamRoutePolicySelections,
			  botFrameCommandStatus.ffaRoamRouteActivations,
			  botFrameCommandStatus.ffaRoamRouteRefreshes,
			  botFrameCommandStatus.ffaRoamRouteOwnerDeferrals,
			  botFrameCommandStatus.ffaRoamRouteRouteRequests,
			  botFrameCommandStatus.ffaRoamRouteRouteDeferrals,
			  botFrameCommandStatus.ffaRoamRouteExpirations,
			  botFrameCommandStatus.ffaRoamRouteInvalidSkips,
			  botFrameCommandStatus.lastFfaRoamRouteClient,
			  botFrameCommandStatus.lastFfaRoamRouteMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastFfaRoamRouteMode)),
			  botFrameCommandStatus.lastFfaRoamRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastFfaRoamRouteRole)),
			  botFrameCommandStatus.lastFfaRoamRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastFfaRoamRouteLane)),
			  botFrameCommandStatus.lastFfaRoamRoutePriority,
			  botFrameCommandStatus.lastFfaRoamRouteRemainingMilliseconds,
			  botFrameCommandStatus.lastFfaRoamRouteGoalDistanceSquared);
}

void BotBrain_PrintTeamRoleRouteStatus() {
	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "team_role_route_requests={} "
			  "team_role_route_policy_selections={} "
			  "team_role_route_activations={} "
			  "team_role_route_refreshes={} "
			  "team_role_route_owner_deferrals={} "
			  "team_role_route_route_requests={} "
			  "team_role_route_route_deferrals={} "
			  "team_role_route_expirations={} "
			  "team_role_route_invalid_skips={} "
			  "last_team_role_route_client={} "
			  "last_team_role_route_mode={} "
			  "last_team_role_route_mode_name={} "
			  "last_team_role_route_role={} "
			  "last_team_role_route_role_name={} "
			  "last_team_role_route_lane={} "
			  "last_team_role_route_lane_name={} "
			  "last_team_role_route_priority={} "
			  "last_team_role_route_remaining_ms={} "
			  "last_team_role_route_goal_distance_sq={}\n",
			  botFrameCommandStatus.teamRoleRouteRequests,
			  botFrameCommandStatus.teamRoleRoutePolicySelections,
			  botFrameCommandStatus.teamRoleRouteActivations,
			  botFrameCommandStatus.teamRoleRouteRefreshes,
			  botFrameCommandStatus.teamRoleRouteOwnerDeferrals,
			  botFrameCommandStatus.teamRoleRouteRouteRequests,
			  botFrameCommandStatus.teamRoleRouteRouteDeferrals,
			  botFrameCommandStatus.teamRoleRouteExpirations,
			  botFrameCommandStatus.teamRoleRouteInvalidSkips,
			  botFrameCommandStatus.lastTeamRoleRouteClient,
			  botFrameCommandStatus.lastTeamRoleRouteMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastTeamRoleRouteMode)),
			  botFrameCommandStatus.lastTeamRoleRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastTeamRoleRouteRole)),
			  botFrameCommandStatus.lastTeamRoleRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastTeamRoleRouteLane)),
			  botFrameCommandStatus.lastTeamRoleRoutePriority,
			  botFrameCommandStatus.lastTeamRoleRouteRemainingMilliseconds,
			  botFrameCommandStatus.lastTeamRoleRouteGoalDistanceSquared);
}

void BotBrain_PrintCtfRoleRouteStatus() {
	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "ctf_role_route_requests={} "
			  "ctf_role_route_policy_selections={} "
			  "ctf_role_route_activations={} "
			  "ctf_role_route_refreshes={} "
			  "ctf_role_route_owner_deferrals={} "
			  "ctf_role_route_objective_deferrals={} "
			  "ctf_role_route_route_requests={} "
			  "ctf_role_route_route_deferrals={} "
			  "ctf_role_route_expirations={} "
			  "ctf_role_route_invalid_skips={} "
			  "last_ctf_role_route_client={} "
			  "last_ctf_role_route_mode={} "
			  "last_ctf_role_route_mode_name={} "
			  "last_ctf_role_route_role={} "
			  "last_ctf_role_route_role_name={} "
			  "last_ctf_role_route_lane={} "
			  "last_ctf_role_route_lane_name={} "
			  "last_ctf_role_route_priority={} "
			  "last_ctf_role_route_remaining_ms={} "
			  "last_ctf_role_route_goal_distance_sq={}\n",
			  botFrameCommandStatus.ctfRoleRouteRequests,
			  botFrameCommandStatus.ctfRoleRoutePolicySelections,
			  botFrameCommandStatus.ctfRoleRouteActivations,
			  botFrameCommandStatus.ctfRoleRouteRefreshes,
			  botFrameCommandStatus.ctfRoleRouteOwnerDeferrals,
			  botFrameCommandStatus.ctfRoleRouteObjectiveDeferrals,
			  botFrameCommandStatus.ctfRoleRouteRouteRequests,
			  botFrameCommandStatus.ctfRoleRouteRouteDeferrals,
			  botFrameCommandStatus.ctfRoleRouteExpirations,
			  botFrameCommandStatus.ctfRoleRouteInvalidSkips,
			  botFrameCommandStatus.lastCtfRoleRouteClient,
			  botFrameCommandStatus.lastCtfRoleRouteMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastCtfRoleRouteMode)),
			  botFrameCommandStatus.lastCtfRoleRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfRoleRouteRole)),
			  botFrameCommandStatus.lastCtfRoleRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfRoleRouteLane)),
			  botFrameCommandStatus.lastCtfRoleRoutePriority,
			  botFrameCommandStatus.lastCtfRoleRouteRemainingMilliseconds,
			  botFrameCommandStatus.lastCtfRoleRouteGoalDistanceSquared);
}

void BotBrain_PrintCtfDroppedFlagRouteStatus() {
	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "ctf_dropped_flag_route_requests={} "
			  "ctf_dropped_flag_route_assignments={} "
			  "ctf_dropped_flag_route_route_requests={} "
			  "ctf_dropped_flag_route_route_commands={} "
			  "ctf_dropped_flag_route_invalid_skips={} "
			  "last_ctf_dropped_flag_route_client={} "
			  "last_ctf_dropped_flag_route_role={} "
			  "last_ctf_dropped_flag_route_role_name={} "
			  "last_ctf_dropped_flag_route_lane={} "
			  "last_ctf_dropped_flag_route_lane_name={} "
			  "last_ctf_dropped_flag_route_type={} "
			  "last_ctf_dropped_flag_route_type_name={} "
			  "last_ctf_dropped_flag_route_source={} "
			  "last_ctf_dropped_flag_route_source_name={} "
			  "last_ctf_dropped_flag_route_entity={} "
			  "last_ctf_dropped_flag_route_item={} "
			  "last_ctf_dropped_flag_route_priority={} "
			  "last_ctf_dropped_flag_route_goal_distance_sq={}\n",
			  botFrameCommandStatus.ctfDroppedFlagRouteRequests,
			  botFrameCommandStatus.ctfDroppedFlagRouteAssignments,
			  botFrameCommandStatus.ctfDroppedFlagRouteRouteRequests,
			  botFrameCommandStatus.ctfDroppedFlagRouteRouteCommands,
			  botFrameCommandStatus.ctfDroppedFlagRouteInvalidSkips,
			  botFrameCommandStatus.lastCtfDroppedFlagRouteClient,
			  botFrameCommandStatus.lastCtfDroppedFlagRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfDroppedFlagRouteRole)),
			  botFrameCommandStatus.lastCtfDroppedFlagRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfDroppedFlagRouteLane)),
			  botFrameCommandStatus.lastCtfDroppedFlagRouteType,
			  BotObjectives_TypeName(static_cast<BotObjectiveType>(
				  botFrameCommandStatus.lastCtfDroppedFlagRouteType)),
			  botFrameCommandStatus.lastCtfDroppedFlagRouteSource,
			  BotObjectives_TargetSourceName(static_cast<BotObjectiveTargetSource>(
				  botFrameCommandStatus.lastCtfDroppedFlagRouteSource)),
			  botFrameCommandStatus.lastCtfDroppedFlagRouteEntity,
			  botFrameCommandStatus.lastCtfDroppedFlagRouteItem,
			  botFrameCommandStatus.lastCtfDroppedFlagRoutePriority,
			  botFrameCommandStatus.lastCtfDroppedFlagRouteGoalDistanceSquared);
}

void BotBrain_PrintCtfCarrierSupportRouteStatus() {
	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "ctf_carrier_support_route_requests={} "
			  "ctf_carrier_support_route_assignments={} "
			  "ctf_carrier_support_route_route_requests={} "
			  "ctf_carrier_support_route_route_commands={} "
			  "ctf_carrier_support_route_invalid_skips={} "
			  "last_ctf_carrier_support_route_client={} "
			  "last_ctf_carrier_support_route_role={} "
			  "last_ctf_carrier_support_route_role_name={} "
			  "last_ctf_carrier_support_route_lane={} "
			  "last_ctf_carrier_support_route_lane_name={} "
			  "last_ctf_carrier_support_route_type={} "
			  "last_ctf_carrier_support_route_type_name={} "
			  "last_ctf_carrier_support_route_source={} "
			  "last_ctf_carrier_support_route_source_name={} "
			  "last_ctf_carrier_support_route_entity={} "
			  "last_ctf_carrier_support_route_carrier_client={} "
			  "last_ctf_carrier_support_route_item={} "
			  "last_ctf_carrier_support_route_priority={} "
			  "last_ctf_carrier_support_route_goal_distance_sq={}\n",
			  botFrameCommandStatus.ctfCarrierSupportRouteRequests,
			  botFrameCommandStatus.ctfCarrierSupportRouteAssignments,
			  botFrameCommandStatus.ctfCarrierSupportRouteRouteRequests,
			  botFrameCommandStatus.ctfCarrierSupportRouteRouteCommands,
			  botFrameCommandStatus.ctfCarrierSupportRouteInvalidSkips,
			  botFrameCommandStatus.lastCtfCarrierSupportRouteClient,
			  botFrameCommandStatus.lastCtfCarrierSupportRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfCarrierSupportRouteRole)),
			  botFrameCommandStatus.lastCtfCarrierSupportRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfCarrierSupportRouteLane)),
			  botFrameCommandStatus.lastCtfCarrierSupportRouteType,
			  BotObjectives_TypeName(static_cast<BotObjectiveType>(
				  botFrameCommandStatus.lastCtfCarrierSupportRouteType)),
			  botFrameCommandStatus.lastCtfCarrierSupportRouteSource,
			  BotObjectives_TargetSourceName(static_cast<BotObjectiveTargetSource>(
				  botFrameCommandStatus.lastCtfCarrierSupportRouteSource)),
			  botFrameCommandStatus.lastCtfCarrierSupportRouteEntity,
			  botFrameCommandStatus.lastCtfCarrierSupportRouteCarrierClient,
			  botFrameCommandStatus.lastCtfCarrierSupportRouteItem,
			  botFrameCommandStatus.lastCtfCarrierSupportRoutePriority,
			  botFrameCommandStatus.lastCtfCarrierSupportRouteGoalDistanceSquared);
}

void BotBrain_PrintCtfBaseReturnRouteStatus() {
	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "ctf_base_return_route_requests={} "
			  "ctf_base_return_route_assignments={} "
			  "ctf_base_return_route_route_requests={} "
			  "ctf_base_return_route_route_commands={} "
			  "ctf_base_return_route_invalid_skips={} "
			  "last_ctf_base_return_route_client={} "
			  "last_ctf_base_return_route_role={} "
			  "last_ctf_base_return_route_role_name={} "
			  "last_ctf_base_return_route_lane={} "
			  "last_ctf_base_return_route_lane_name={} "
			  "last_ctf_base_return_route_type={} "
			  "last_ctf_base_return_route_type_name={} "
			  "last_ctf_base_return_route_source={} "
			  "last_ctf_base_return_route_source_name={} "
			  "last_ctf_base_return_route_entity={} "
			  "last_ctf_base_return_route_carrier_client={} "
			  "last_ctf_base_return_route_item={} "
			  "last_ctf_base_return_route_priority={} "
			  "last_ctf_base_return_route_goal_distance_sq={}\n",
			  botFrameCommandStatus.ctfBaseReturnRouteRequests,
			  botFrameCommandStatus.ctfBaseReturnRouteAssignments,
			  botFrameCommandStatus.ctfBaseReturnRouteRouteRequests,
			  botFrameCommandStatus.ctfBaseReturnRouteRouteCommands,
			  botFrameCommandStatus.ctfBaseReturnRouteInvalidSkips,
			  botFrameCommandStatus.lastCtfBaseReturnRouteClient,
			  botFrameCommandStatus.lastCtfBaseReturnRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfBaseReturnRouteRole)),
			  botFrameCommandStatus.lastCtfBaseReturnRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfBaseReturnRouteLane)),
			  botFrameCommandStatus.lastCtfBaseReturnRouteType,
			  BotObjectives_TypeName(static_cast<BotObjectiveType>(
				  botFrameCommandStatus.lastCtfBaseReturnRouteType)),
			  botFrameCommandStatus.lastCtfBaseReturnRouteSource,
			  BotObjectives_TargetSourceName(static_cast<BotObjectiveTargetSource>(
				  botFrameCommandStatus.lastCtfBaseReturnRouteSource)),
			  botFrameCommandStatus.lastCtfBaseReturnRouteEntity,
			  botFrameCommandStatus.lastCtfBaseReturnRouteCarrierClient,
			  botFrameCommandStatus.lastCtfBaseReturnRouteItem,
			  botFrameCommandStatus.lastCtfBaseReturnRoutePriority,
			  botFrameCommandStatus.lastCtfBaseReturnRouteGoalDistanceSquared);
}

void BotBrain_PrintCtfObjectiveRouteStatus() {
	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "ctf_objective_route_requests={} "
			  "ctf_objective_route_assignments={} "
			  "ctf_objective_route_base_return_candidates={} "
			  "ctf_objective_route_carrier_support_candidates={} "
			  "ctf_objective_route_dropped_flag_candidates={} "
			  "ctf_objective_route_base_return_selections={} "
			  "ctf_objective_route_carrier_support_selections={} "
			  "ctf_objective_route_dropped_flag_selections={} "
			  "ctf_objective_route_carrier_support_deferrals={} "
			  "ctf_objective_route_dropped_flag_deferrals={} "
			  "ctf_objective_route_route_requests={} "
			  "ctf_objective_route_route_commands={} "
			  "ctf_objective_route_invalid_skips={} "
			  "last_ctf_objective_route_client={} "
			  "last_ctf_objective_route_selection={} "
			  "last_ctf_objective_route_selection_name={} "
			  "last_ctf_objective_route_role={} "
			  "last_ctf_objective_route_role_name={} "
			  "last_ctf_objective_route_lane={} "
			  "last_ctf_objective_route_lane_name={} "
			  "last_ctf_objective_route_type={} "
			  "last_ctf_objective_route_type_name={} "
			  "last_ctf_objective_route_source={} "
			  "last_ctf_objective_route_source_name={} "
			  "last_ctf_objective_route_entity={} "
			  "last_ctf_objective_route_carrier_client={} "
			  "last_ctf_objective_route_item={} "
			  "last_ctf_objective_route_priority={} "
			  "last_ctf_objective_route_goal_distance_sq={}\n",
			  botFrameCommandStatus.ctfObjectiveRouteRequests,
			  botFrameCommandStatus.ctfObjectiveRouteAssignments,
			  botFrameCommandStatus.ctfObjectiveRouteBaseReturnCandidates,
			  botFrameCommandStatus.ctfObjectiveRouteCarrierSupportCandidates,
			  botFrameCommandStatus.ctfObjectiveRouteDroppedFlagCandidates,
			  botFrameCommandStatus.ctfObjectiveRouteBaseReturnSelections,
			  botFrameCommandStatus.ctfObjectiveRouteCarrierSupportSelections,
			  botFrameCommandStatus.ctfObjectiveRouteDroppedFlagSelections,
			  botFrameCommandStatus.ctfObjectiveRouteCarrierSupportDeferrals,
			  botFrameCommandStatus.ctfObjectiveRouteDroppedFlagDeferrals,
			  botFrameCommandStatus.ctfObjectiveRouteRouteRequests,
			  botFrameCommandStatus.ctfObjectiveRouteRouteCommands,
			  botFrameCommandStatus.ctfObjectiveRouteInvalidSkips,
			  botFrameCommandStatus.lastCtfObjectiveRouteClient,
			  botFrameCommandStatus.lastCtfObjectiveRouteSelection,
			  Bot_CommandCtfObjectiveRouteSelectionName(
				  static_cast<BotCtfObjectiveRouteSelection>(
					  botFrameCommandStatus.lastCtfObjectiveRouteSelection)),
			  botFrameCommandStatus.lastCtfObjectiveRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfObjectiveRouteRole)),
			  botFrameCommandStatus.lastCtfObjectiveRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfObjectiveRouteLane)),
			  botFrameCommandStatus.lastCtfObjectiveRouteType,
			  BotObjectives_TypeName(static_cast<BotObjectiveType>(
				  botFrameCommandStatus.lastCtfObjectiveRouteType)),
			  botFrameCommandStatus.lastCtfObjectiveRouteSource,
			  BotObjectives_TargetSourceName(static_cast<BotObjectiveTargetSource>(
				  botFrameCommandStatus.lastCtfObjectiveRouteSource)),
			  botFrameCommandStatus.lastCtfObjectiveRouteEntity,
			  botFrameCommandStatus.lastCtfObjectiveRouteCarrierClient,
			  botFrameCommandStatus.lastCtfObjectiveRouteItem,
			  botFrameCommandStatus.lastCtfObjectiveRoutePriority,
			  botFrameCommandStatus.lastCtfObjectiveRouteGoalDistanceSquared);
}

} // namespace

/*
================
BotBrain_BeginFrame
================
*/
void BotBrain_ResetChatPolicyState() {
	botChatPolicyDispatchSpawnCounts.fill(-1);
	botChatPolicyReplySpawnCounts.fill(-1);
	botChatPolicyRouteReplySpawnCounts.fill(-1);
	botChatPolicyLiveRouteReplySpawnCounts.fill(-1);
	botChatPolicyLiveItemTakenSpawnCounts.fill(-1);
	botChatPolicyLiveObjectiveChangedSpawnCounts.fill(-1);
	botChatPolicyLiveEnemySightedSpawnCounts.fill(-1);
	botChatPolicyLiveLowHealthSpawnCounts.fill(-1);
	botChatPolicyLastReplyEvents.fill(-1);
	botChatPolicyLastReplyPhrases.fill(-1);
	botChatPolicyLastReplyTimes.fill(-1);
	botChatInitialPolicyStatus = {};
	botChatInitialPolicyStatus.lastClient = -1;
	botChatInitialPolicyStatus.lastVariant = -1;
	botChatReplyPolicyStatus = {};
	botChatReplyPolicyStatus.lastClient = -1;
	botChatReplyPolicyStatus.lastVariant = -1;
	botChatReplyPolicyStatus.lastDuplicateClient = -1;
	botChatReplyPolicyStatus.lastDuplicateElapsedMilliseconds = -1;
}

int BotChatPolicy_InitialSelections() {
	return botChatInitialPolicyStatus.selections;
}

int BotChatPolicy_InitialKnownPersonalities() {
	return botChatInitialPolicyStatus.knownPersonalities;
}

int BotChatPolicy_InitialUnknownPersonalities() {
	return botChatInitialPolicyStatus.unknownPersonalities;
}

int BotChatPolicy_InitialQuiet() {
	return botChatInitialPolicyStatus.quiet;
}

int BotChatPolicy_InitialDirect() {
	return botChatInitialPolicyStatus.direct;
}

int BotChatPolicy_InitialTaunting() {
	return botChatInitialPolicyStatus.taunting;
}

int BotChatPolicy_InitialHelpful() {
	return botChatInitialPolicyStatus.helpful;
}

int BotChatPolicy_InitialSteady() {
	return botChatInitialPolicyStatus.steady;
}

int BotChatPolicy_LastInitialClient() {
	return botChatInitialPolicyStatus.lastClient;
}

int BotChatPolicy_LastInitialPersonality() {
	return botChatInitialPolicyStatus.lastPersonality;
}

int BotChatPolicy_LastInitialPhrase() {
	return botChatInitialPolicyStatus.lastPhrase;
}

int BotChatPolicy_InitialPhraseVariants() {
	return BOT_CHAT_POLICY_INITIAL_PHRASE_VARIANTS;
}

int BotChatPolicy_InitialUniquePhraseVariants() {
	return BotChatPolicy_CountBits(botChatInitialPolicyStatus.variantMask);
}

int BotChatPolicy_LastInitialPhraseVariant() {
	return botChatInitialPolicyStatus.lastVariant;
}

int BotChatPolicy_ReplyEnabled() {
	return (BotChatPolicy_ReplySmokeEnabled() || BotChatPolicy_EventSmokeEnabled()) ?
		1 : botChatReplyPolicyStatus.enabled;
}

int BotChatPolicy_ReplyEvents() {
	return botChatReplyPolicyStatus.events;
}

int BotChatPolicy_ReplySelections() {
	return botChatReplyPolicyStatus.selections;
}

int BotChatPolicy_ReplyKnownPersonalities() {
	return botChatReplyPolicyStatus.knownPersonalities;
}

int BotChatPolicy_ReplyUnknownPersonalities() {
	return botChatReplyPolicyStatus.unknownPersonalities;
}

int BotChatPolicy_ReplyTeamReady() {
	return botChatReplyPolicyStatus.teamReady;
}

int BotChatPolicy_ReplyRouteReady() {
	return botChatReplyPolicyStatus.routeReady;
}

int BotChatPolicy_ReplyItemTaken() {
	return botChatReplyPolicyStatus.itemTaken;
}

int BotChatPolicy_ReplyObjectiveChanged() {
	return botChatReplyPolicyStatus.objectiveChanged;
}

int BotChatPolicy_ReplyEnemySighted() {
	return botChatReplyPolicyStatus.enemySighted;
}

int BotChatPolicy_ReplyLowHealth() {
	return botChatReplyPolicyStatus.lowHealth;
}

int BotChatPolicy_ReplySubmitted() {
	return botChatReplyPolicyStatus.submitted;
}

int BotChatPolicy_ReplyRateLimited() {
	return botChatReplyPolicyStatus.rateLimited;
}

int BotChatPolicy_ReplyDuplicateSuppressed() {
	return botChatReplyPolicyStatus.duplicateSuppressed;
}

int BotChatPolicy_ReplyFailures() {
	return botChatReplyPolicyStatus.failures;
}

int BotChatPolicy_LastReplyClient() {
	return botChatReplyPolicyStatus.lastClient;
}

int BotChatPolicy_LastReplyPersonality() {
	return botChatReplyPolicyStatus.lastPersonality;
}

int BotChatPolicy_LastReplyPhrase() {
	return botChatReplyPolicyStatus.lastPhrase;
}

int BotChatPolicy_ReplyPhraseVariants() {
	return BOT_CHAT_POLICY_REPLY_PHRASE_VARIANTS;
}

int BotChatPolicy_ReplyUniquePhraseVariants() {
	return BotChatPolicy_CountBits(botChatReplyPolicyStatus.variantMask);
}

int BotChatPolicy_LastReplyPhraseVariant() {
	return botChatReplyPolicyStatus.lastVariant;
}

int BotChatPolicy_LastReplyEvent() {
	return botChatReplyPolicyStatus.lastEvent;
}

int BotChatPolicy_LiveEnabled() {
	return BotChatPolicy_LiveEventsEnabled() ? 1 : botChatReplyPolicyStatus.liveEnabled;
}

int BotChatPolicy_LiveEvents() {
	return botChatReplyPolicyStatus.liveEvents;
}

int BotChatPolicy_LiveSpawn() {
	return botChatReplyPolicyStatus.liveSpawn;
}

int BotChatPolicy_LiveRouteReady() {
	return botChatReplyPolicyStatus.liveRouteReady;
}

int BotChatPolicy_LiveItemTaken() {
	return botChatReplyPolicyStatus.liveItemTaken;
}

int BotChatPolicy_LiveObjectiveChanged() {
	return botChatReplyPolicyStatus.liveObjectiveChanged;
}

int BotChatPolicy_LiveEnemySighted() {
	return botChatReplyPolicyStatus.liveEnemySighted;
}

int BotChatPolicy_LiveLowHealth() {
	return botChatReplyPolicyStatus.liveLowHealth;
}

int BotChatPolicy_LiveSubmitted() {
	return botChatReplyPolicyStatus.liveSubmitted;
}

int BotChatPolicy_LiveRateLimited() {
	return botChatReplyPolicyStatus.liveRateLimited;
}

int BotChatPolicy_LiveDuplicateSuppressed() {
	return botChatReplyPolicyStatus.liveDuplicateSuppressed;
}

int BotChatPolicy_LiveFailures() {
	return botChatReplyPolicyStatus.liveFailures;
}

int BotChatPolicy_LiveEventTaxonomy() {
	return BOT_CHAT_POLICY_LIVE_EVENT_TAXONOMY;
}

int BotChatPolicy_DuplicateWindowMilliseconds() {
	return BOT_CHAT_POLICY_DUPLICATE_WINDOW_MS;
}

int BotChatPolicy_LastDuplicateClient() {
	return botChatReplyPolicyStatus.lastDuplicateClient;
}

int BotChatPolicy_LastDuplicateEvent() {
	return botChatReplyPolicyStatus.lastDuplicateEvent;
}

const char *BotChatPolicy_LastDuplicateEventName() {
	return BotChatPolicy_EventName(botChatReplyPolicyStatus.lastDuplicateEvent);
}

int BotChatPolicy_LastDuplicatePhrase() {
	return botChatReplyPolicyStatus.lastDuplicatePhrase;
}

int BotChatPolicy_LastDuplicateElapsedMilliseconds() {
	return botChatReplyPolicyStatus.lastDuplicateElapsedMilliseconds;
}

int BotChatPolicy_LastLiveEvent() {
	return botChatReplyPolicyStatus.lastLiveEvent;
}

const char *BotChatPolicy_LastLiveEventName() {
	return BotChatPolicy_EventName(botChatReplyPolicyStatus.lastLiveEvent);
}

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
		if (Bot_CommandApplyControlledInactiveRecovery(bot, cmd)) {
			botFrameCommandStatus.commands++;
			cpuScope.success = true;
			return true;
		}
		botFrameCommandStatus.skippedInactive++;
		return false;
	}

	(void)Bot_CommandMaybeDispatchChatPolicy(bot);
	(void)Bot_CommandMaybeDispatchChatReplyPolicy(bot);

	BotCommandSmokeProofSlot *smokeSlot = Bot_CommandSmokeProofSlotFor(bot);
	Bot_CommandPrepareSmokeProof(bot, smokeSlot);
	if (Bot_CommandSmokeAimFirePolicy() && Bot_PerceptionClientIndex(bot) != 0) {
		return false;
	}
	const BotActionDecision actionDecision = Bot_CommandSampleActionDecision(bot);
	const BotFrameObjectivePolicyResult objectivePolicies =
		Bot_CommandEvaluateFrameObjectivePolicies(bot, actionDecision);
	if (Bot_CommandFfaLivePacingProofEnabled() ||
		Bot_CommandDuelLivePacingEnabled()) {
		(void)BotNav_ProbePickupGoal(bot);
	}
	const BotActionDecision ffaRoleCombatDecision =
		Bot_CommandApplyFfaRoleCombat(
			bot,
			objectivePolicies.matchPolicy,
			actionDecision);
	const BotActionDecision ffaSpawnCampCombatAvoidanceDecision =
		Bot_CommandApplyFfaSpawnCampCombatAvoidance(
			bot,
			objectivePolicies.matchPolicy,
			ffaRoleCombatDecision);
	const BotActionDecision teamRoleCombatDecision =
		Bot_CommandApplyTeamRoleCombat(
			bot,
			objectivePolicies.matchPolicy,
			ffaSpawnCampCombatAvoidanceDecision);
	const BotActionDecision ctfRoleCombatDecision =
		Bot_CommandApplyCtfRoleCombat(
			bot,
			objectivePolicies.matchPolicy,
			teamRoleCombatDecision);
	const BotActionDecision teamFireDecision =
		Bot_CommandApplyTeamFireAvoidance(bot, ctfRoleCombatDecision);
	const BotActionDecision commandDecision =
		Bot_CommandApplyThreatRetreatAttackSuppression(bot, teamFireDecision);
	const int currentWeaponItem = Bot_CommandCurrentWeaponItem(bot);
	const BotNavRouteStatus &behaviorArbitrationRouteStatusBefore =
		BotNav_GetRouteStatus();
	const int behaviorArbitrationItemGoalAssignmentsBefore =
		behaviorArbitrationRouteStatusBefore.itemGoalAssignments;
	const int behaviorArbitrationTeamItemRoleSelectionsBefore =
		behaviorArbitrationRouteStatusBefore.teamItemRoleSelections;
	const int behaviorArbitrationTeamResourceDenialPolicyDeniesBefore =
		behaviorArbitrationRouteStatusBefore.teamResourceDenialPolicyDenies;
	const int behaviorArbitrationRecoveryCommandUsesBefore =
		botFrameCommandStatus.recoveryCommandUses;
	const int behaviorArbitrationInteractionWaitCommandUsesBefore =
		botFrameCommandStatus.interactionWaitCommandUses;
	const int behaviorArbitrationInteractionUseCommandUsesBefore =
		botFrameCommandStatus.interactionUseCommandUses;
	const int behaviorArbitrationCoopProgressWaitCommandsBefore =
		botFrameCommandStatus.coopProgressWaitCommands;
	const int behaviorArbitrationCoopAntiBlockCommandsBefore =
		botFrameCommandStatus.coopAntiBlockCommands;
	const int behaviorArbitrationCoopDoorElevatorSourceCommandsBefore =
		botFrameCommandStatus.coopDoorElevatorSourceCommands;
	const int behaviorArbitrationCoopDoorElevatorHoldCommandsBefore =
		botFrameCommandStatus.coopDoorElevatorHoldCommands;
	const int behaviorArbitrationCoopInteractionRetryCommandsBefore =
		botFrameCommandStatus.coopInteractionRetryCommands;

	BotLibAdapterRouteSteer route{};
	BotNavRouteRequest routeRequest{};
	BotObjectiveAssignment objectiveAssignment{};
	BotObjectiveRouteGoal objectiveRouteGoal{};
	BotObjectiveAssignment ctfObjectiveRouteAssignment{};
	BotObjectiveRouteGoal ctfObjectiveRouteGoal{};
	BotCtfObjectiveRouteSelection ctfObjectiveRouteSelection =
		BotCtfObjectiveRouteSelection::None;
	BotObjectiveAssignment ctfBaseReturnRouteAssignment{};
	BotObjectiveRouteGoal ctfBaseReturnRouteGoal{};
	BotObjectiveAssignment ctfCarrierSupportRouteAssignment{};
	BotObjectiveRouteGoal ctfCarrierSupportRouteGoal{};
	BotObjectiveAssignment ctfDroppedFlagRouteAssignment{};
	BotObjectiveRouteGoal ctfDroppedFlagRouteGoal{};
	Bot_CommandBuildRouteRequest(&routeRequest);
	(void)Bot_CommandApplyTimedRouteGoal(bot, &routeRequest);
	const bool ctfObjectiveRouteRequested =
		Bot_CommandBuildCtfObjectiveRoute(
			bot,
			&routeRequest,
			&ctfObjectiveRouteAssignment,
			&ctfObjectiveRouteGoal,
			&ctfObjectiveRouteSelection);
	const bool ctfBaseReturnRouteRequested =
		!ctfObjectiveRouteRequested &&
		Bot_CommandBuildCtfBaseReturnRoute(
			bot,
			&routeRequest,
			&ctfBaseReturnRouteAssignment,
			&ctfBaseReturnRouteGoal);
	const bool ctfCarrierSupportRouteRequested =
		!ctfObjectiveRouteRequested &&
		!ctfBaseReturnRouteRequested &&
		Bot_CommandBuildCtfCarrierSupportRoute(
			bot,
			&routeRequest,
			&ctfCarrierSupportRouteAssignment,
			&ctfCarrierSupportRouteGoal);
	const bool ctfDroppedFlagRouteRequested =
		!ctfObjectiveRouteRequested &&
		!ctfBaseReturnRouteRequested &&
		!ctfCarrierSupportRouteRequested &&
		Bot_CommandBuildCtfDroppedFlagRoute(
			bot,
			&routeRequest,
			&ctfDroppedFlagRouteAssignment,
			&ctfDroppedFlagRouteGoal);

	*cmd = {};
	cmd->msec = Bot_CommandMsec();
	cmd->serverFrame = gi.ServerFrame();
	const bool coopDoorElevatorHeld =
		Bot_CommandApplyCoopDoorElevatorHold(
			bot,
			objectivePolicies.coopPolicy,
			cmd) ||
		Bot_CommandApplyCoopDoorElevatorSupportIdle(
			bot,
			objectivePolicies.coopPolicy,
			cmd);
	if (coopDoorElevatorHeld) {
		(void)Bot_CommandApplyCoopProgressWait(
			bot,
			objectivePolicies.coopPolicy,
			cmd);
		BotBehaviorArbitrationCandidates arbitration{};
		arbitration.interaction = true;
		Bot_CommandRecordBehaviorArbitration(bot, arbitration);
		Bot_BlackboardRecordNavState(bot);
		botFrameCommandStatus.commands++;
		cpuScope.success = true;
		return true;
	}

	const bool objectiveRouteRequested = Bot_CommandBuildSmokeObjectiveRoute(
		bot,
		smokeSlot,
		&routeRequest,
		&objectiveAssignment,
		&objectiveRouteGoal);
	Bot_BlackboardRecordTeamRole(
		bot,
		objectiveRouteRequested
			? &objectiveAssignment
			: (ctfObjectiveRouteRequested
				? &ctfObjectiveRouteAssignment
				: (ctfBaseReturnRouteRequested
					? &ctfBaseReturnRouteAssignment
					: (ctfCarrierSupportRouteRequested
						? &ctfCarrierSupportRouteAssignment
						: (ctfDroppedFlagRouteRequested
							? &ctfDroppedFlagRouteAssignment
							: nullptr)))));
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
	if (ctfObjectiveRouteRequested) {
		Bot_CommandRecordCtfObjectiveRouteResult(
			bot,
			ctfObjectiveRouteSelection,
			ctfObjectiveRouteAssignment,
			ctfObjectiveRouteGoal,
			true);
	}
	if (ctfDroppedFlagRouteRequested) {
		Bot_CommandRecordCtfDroppedFlagRouteResult(
			bot,
			ctfDroppedFlagRouteAssignment,
			ctfDroppedFlagRouteGoal,
			true);
	}
	if (ctfCarrierSupportRouteRequested) {
		Bot_CommandRecordCtfCarrierSupportRouteResult(
			bot,
			ctfCarrierSupportRouteAssignment,
			ctfCarrierSupportRouteGoal,
			true);
	}
	if (ctfBaseReturnRouteRequested) {
		Bot_CommandRecordCtfBaseReturnRouteResult(
			bot,
			ctfBaseReturnRouteAssignment,
			ctfBaseReturnRouteGoal,
			true);
	}
	Bot_CommandRequestCoopInteractionRetry(
		bot,
		objectivePolicies.coopPolicy,
		route);
	Bot_CommandRequestCoopDoorElevator(
		bot,
		objectivePolicies.coopPolicy,
		route);

	cmd->msec = Bot_CommandMsec();
	cmd->angles = Bot_CommandAnglesForDecision(bot, route, commandDecision);
	cmd->forwardMove = 180.0f;
	cmd->serverFrame = gi.ServerFrame();
	Bot_CommandApplyMovementState(bot, route, cmd);
	Bot_CommandApplyRecoveryMove(bot, cmd);
	(void)Bot_CommandApplyCoopDoorElevatorHold(
		bot,
		objectivePolicies.coopPolicy,
		cmd);
	(void)Bot_CommandApplyCoopAntiBlocking(
		bot,
		objectivePolicies.coopPolicy,
		cmd);
	(void)Bot_CommandApplyCoopProgressWait(
		bot,
		objectivePolicies.coopPolicy,
		cmd);
	Bot_BlackboardRecordNavState(bot);
	const BotActionApplyResult actionApply =
		BotActions_ApplyDecisionDetailed(commandDecision, cmd);
	Bot_CommandRecordAimPolicyAttack(bot, actionApply);
	Bot_CommandRecordThreatRetreatReengage(bot, actionApply);
	if (actionApply.weaponSwitchPending || actionApply.inventoryUsePending) {
		(void)Bot_CommandDispatchPendingActionRequest(
			bot,
			commandDecision,
			actionApply,
			currentWeaponItem);
	}
	Bot_CommandRecordSmokeDamageProof(bot, smokeSlot, actionApply);
	const BotNavRouteStatus &behaviorArbitrationRouteStatusAfter =
		BotNav_GetRouteStatus();
	BotBehaviorArbitrationCandidates arbitration{};
	arbitration.route = true;
	arbitration.item =
		Bot_CommandMatchItemPolicyEnabled() ||
		behaviorArbitrationRouteStatusAfter.itemGoalAssignments >
			behaviorArbitrationItemGoalAssignmentsBefore ||
		behaviorArbitrationRouteStatusAfter.teamItemRoleSelections >
			behaviorArbitrationTeamItemRoleSelectionsBefore ||
		behaviorArbitrationRouteStatusAfter.teamResourceDenialPolicyDenies >
			behaviorArbitrationTeamResourceDenialPolicyDeniesBefore;
	arbitration.combat =
		actionDecision.pressAttack ||
		ffaRoleCombatDecision.pressAttack ||
		ffaSpawnCampCombatAvoidanceDecision.pressAttack ||
		teamRoleCombatDecision.pressAttack ||
		ctfRoleCombatDecision.pressAttack ||
		commandDecision.pressAttack ||
		actionApply.attackButtonApplied;
	arbitration.objective =
		objectiveRouteRequested ||
		ctfObjectiveRouteRequested ||
		ctfBaseReturnRouteRequested ||
		ctfCarrierSupportRouteRequested ||
		ctfDroppedFlagRouteRequested;
	arbitration.interaction =
		botFrameCommandStatus.interactionWaitCommandUses >
			behaviorArbitrationInteractionWaitCommandUsesBefore ||
		botFrameCommandStatus.interactionUseCommandUses >
			behaviorArbitrationInteractionUseCommandUsesBefore ||
		botFrameCommandStatus.coopProgressWaitCommands >
			behaviorArbitrationCoopProgressWaitCommandsBefore ||
		botFrameCommandStatus.coopAntiBlockCommands >
			behaviorArbitrationCoopAntiBlockCommandsBefore ||
		botFrameCommandStatus.coopDoorElevatorSourceCommands >
			behaviorArbitrationCoopDoorElevatorSourceCommandsBefore ||
		botFrameCommandStatus.coopDoorElevatorHoldCommands >
			behaviorArbitrationCoopDoorElevatorHoldCommandsBefore ||
		botFrameCommandStatus.coopInteractionRetryCommands >
			behaviorArbitrationCoopInteractionRetryCommandsBefore;
	arbitration.recovery =
		botFrameCommandStatus.recoveryCommandUses >
		behaviorArbitrationRecoveryCommandUsesBefore;
	Bot_CommandRecordBehaviorArbitration(bot, arbitration);
	(void)Bot_CommandMaybeDispatchLiveRouteReadyChat(bot);
	(void)Bot_CommandMaybeDispatchLiveEnemySightedChat(bot);
	(void)Bot_CommandMaybeDispatchLiveLowHealthChat(bot);
	(void)Bot_CommandMaybeDispatchLiveItemTakenChat(bot);
	(void)Bot_CommandMaybeDispatchLiveObjectiveChangedChat(bot);

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

	BotBrain_PrintMatchReadinessStatus(botPopulation);
	BotBrain_PrintCoopReadinessStatus(botPopulation);
	BotBrain_PrintSourceCounterProofStatus(sourceCounters);
	BotBrain_PrintCompactObjectiveStatus(objectiveStatus);
	BotBrain_PrintFfaRoamRouteStatus();
	BotBrain_PrintTeamRoleRouteStatus();
	BotBrain_PrintCtfRoleRouteStatus();
	BotBrain_PrintCtfDroppedFlagRouteStatus();
	BotBrain_PrintCtfCarrierSupportRouteStatus();
	BotBrain_PrintCtfBaseReturnRouteStatus();
	BotBrain_PrintCtfObjectiveRouteStatus();

	BotBrain_PrintStatusFmt(
		"q3a_bot_behavior_policy_status "
			  "behavior_enable={} "
			  "behavior_arbitration={} "
			  "behavior_policy_cvar_audit={} "
			  "behavior_live_policy_cvars={} "
			  "behavior_smoke_policy_cvars={} "
			  "behavior_debug_policy_cvars={} "
			  "behavior_deprecated_policy_cvars={} "
			  "team_role_route={} "
			  "team_role_combat={} "
			  "team_fire_avoidance={} "
			  "match_item_policy={} "
			  "coop_progress_wait={} "
			  "ctf_objective_route={} "
			  "ffa_roam_route={} "
			  "threat_retreat={} "
			  "behavior_arbitration_evaluations={} "
			  "behavior_arbitration_route_candidates={} "
			  "behavior_arbitration_item_candidates={} "
			  "behavior_arbitration_combat_candidates={} "
			  "behavior_arbitration_objective_candidates={} "
			  "behavior_arbitration_interaction_candidates={} "
			  "behavior_arbitration_recovery_candidates={} "
			  "behavior_arbitration_route_owners={} "
			  "behavior_arbitration_item_owners={} "
			  "behavior_arbitration_combat_owners={} "
			  "behavior_arbitration_objective_owners={} "
			  "behavior_arbitration_interaction_owners={} "
			  "behavior_arbitration_recovery_owners={} "
			  "behavior_arbitration_idle_owners={} "
			  "behavior_arbitration_handoffs={} "
			  "last_behavior_arbitration_client={} "
			  "last_behavior_arbitration_owner={} "
			  "last_behavior_arbitration_owner_name={} "
			  "last_behavior_arbitration_previous_owner={} "
			  "last_behavior_arbitration_priority={} "
			  "last_behavior_arbitration_reason={}\n",
			  Bot_CommandBehaviorPolicyEnabled() ? 1 : 0,
			  1,
			  1,
			  Bot_CommandBehaviorLivePolicyCvarCount(),
			  Bot_CommandBehaviorSmokePolicyCvarCount(),
			  Bot_CommandBehaviorDebugPolicyCvarCount(),
			  Bot_CommandBehaviorDeprecatedPolicyCvarCount(),
			  Bot_CommandTeamRoleRouteEnabled() ? 1 : 0,
			  Bot_CommandTeamRoleCombatEnabled() ? 1 : 0,
			  Bot_CommandTeamFireAvoidanceEnabled() ? 1 : 0,
			  Bot_CommandMatchItemPolicyEnabled() ? 1 : 0,
			  Bot_CommandCoopProgressWaitRequested() ? 1 : 0,
			  Bot_CommandCtfObjectiveRouteEnabled() ? 1 : 0,
			  Bot_CommandFfaRoamRouteEnabled() ? 1 : 0,
			  Bot_CommandThreatRetreatEnabled() ? 1 : 0,
			  botFrameCommandStatus.behaviorArbitrationEvaluations,
			  botFrameCommandStatus.behaviorArbitrationRouteCandidates,
			  botFrameCommandStatus.behaviorArbitrationItemCandidates,
			  botFrameCommandStatus.behaviorArbitrationCombatCandidates,
			  botFrameCommandStatus.behaviorArbitrationObjectiveCandidates,
			  botFrameCommandStatus.behaviorArbitrationInteractionCandidates,
			  botFrameCommandStatus.behaviorArbitrationRecoveryCandidates,
			  botFrameCommandStatus.behaviorArbitrationRouteOwners,
			  botFrameCommandStatus.behaviorArbitrationItemOwners,
			  botFrameCommandStatus.behaviorArbitrationCombatOwners,
			  botFrameCommandStatus.behaviorArbitrationObjectiveOwners,
			  botFrameCommandStatus.behaviorArbitrationInteractionOwners,
			  botFrameCommandStatus.behaviorArbitrationRecoveryOwners,
			  botFrameCommandStatus.behaviorArbitrationIdleOwners,
			  botFrameCommandStatus.behaviorArbitrationHandoffs,
			  botFrameCommandStatus.lastBehaviorArbitrationClient,
			  botFrameCommandStatus.lastBehaviorArbitrationOwner,
			  botFrameCommandStatus.lastBehaviorArbitrationOwnerName,
			  botFrameCommandStatus.lastBehaviorArbitrationPreviousOwner,
			  botFrameCommandStatus.lastBehaviorArbitrationPriority,
			  botFrameCommandStatus.lastBehaviorArbitrationReason);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "ffa_item_role_evaluations={} "
			  "ffa_item_role_selections={} "
			  "ffa_item_role_score_boosts={} "
			  "ffa_item_role_selected_goals={} "
			  "ffa_item_role_invalid_skips={}\n",
			  routeStatus.ffaItemRoleEvaluations,
			  routeStatus.ffaItemRoleSelections,
			  routeStatus.ffaItemRoleScoreBoosts,
			  routeStatus.ffaItemRoleSelectedGoals,
			  routeStatus.ffaItemRoleInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ffa_item_role_mode={} "
			  "last_ffa_item_role_role={} "
			  "last_ffa_item_role_lane={} "
			  "last_ffa_item_role_category={} "
			  "last_ffa_item_role_item_role={} "
			  "last_ffa_item_role_entity={} "
			  "last_ffa_item_role_item={} "
			  "last_ffa_item_role_score_boost={} "
			  "last_ffa_item_role_profile_item_bonus={}\n",
			  routeStatus.lastFfaItemRoleMode,
			  routeStatus.lastFfaItemRoleRole,
			  routeStatus.lastFfaItemRoleLane,
			  routeStatus.lastFfaItemRoleCategory,
			  routeStatus.lastFfaItemRoleItemRole,
			  routeStatus.lastFfaItemRoleEntity,
			  routeStatus.lastFfaItemRoleItem,
			  routeStatus.lastFfaItemRoleScoreBoost,
			  routeStatus.lastFfaItemRoleProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "ctf_item_role_evaluations={} "
			  "ctf_item_role_selections={} "
			  "ctf_item_role_score_boosts={} "
			  "ctf_item_role_selected_goals={} "
			  "ctf_item_role_invalid_skips={}\n",
			  routeStatus.ctfItemRoleEvaluations,
			  routeStatus.ctfItemRoleSelections,
			  routeStatus.ctfItemRoleScoreBoosts,
			  routeStatus.ctfItemRoleSelectedGoals,
			  routeStatus.ctfItemRoleInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ctf_item_role_mode={} "
			  "last_ctf_item_role_role={} "
			  "last_ctf_item_role_lane={} "
			  "last_ctf_item_role_category={} "
			  "last_ctf_item_role_item_role={} "
			  "last_ctf_item_role_entity={} "
			  "last_ctf_item_role_item={} "
			  "last_ctf_item_role_score_boost={} "
			  "last_ctf_item_role_profile_item_bonus={}\n",
			  routeStatus.lastCtfItemRoleMode,
			  routeStatus.lastCtfItemRoleRole,
			  routeStatus.lastCtfItemRoleLane,
			  routeStatus.lastCtfItemRoleCategory,
			  routeStatus.lastCtfItemRoleItemRole,
			  routeStatus.lastCtfItemRoleEntity,
			  routeStatus.lastCtfItemRoleItem,
			  routeStatus.lastCtfItemRoleScoreBoost,
			  routeStatus.lastCtfItemRoleProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "team_item_role_evaluations={} "
			  "team_item_role_selections={} "
			  "team_item_role_score_boosts={} "
			  "team_item_role_selected_goals={} "
			  "team_item_role_invalid_skips={}\n",
			  routeStatus.teamItemRoleEvaluations,
			  routeStatus.teamItemRoleSelections,
			  routeStatus.teamItemRoleScoreBoosts,
			  routeStatus.teamItemRoleSelectedGoals,
			  routeStatus.teamItemRoleInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_item_role_mode={} "
			  "last_team_item_role_role={} "
			  "last_team_item_role_lane={} "
			  "last_team_item_role_category={} "
			  "last_team_item_role_item_role={} "
			  "last_team_item_role_entity={} "
			  "last_team_item_role_item={} "
			  "last_team_item_role_score_boost={} "
			  "last_team_item_role_profile_item_bonus={}\n",
			  routeStatus.lastTeamItemRoleMode,
			  routeStatus.lastTeamItemRoleRole,
			  routeStatus.lastTeamItemRoleLane,
			  routeStatus.lastTeamItemRoleCategory,
			  routeStatus.lastTeamItemRoleItemRole,
			  routeStatus.lastTeamItemRoleEntity,
			  routeStatus.lastTeamItemRoleItem,
			  routeStatus.lastTeamItemRoleScoreBoost,
			  routeStatus.lastTeamItemRoleProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "team_resource_denial_evaluations={} "
			  "team_resource_denial_policy_denies={} "
			  "team_resource_denial_score_boosts={} "
			  "team_resource_denial_selected_goals={} "
			  "team_resource_denial_invalid_skips={}\n",
			  routeStatus.teamResourceDenialEvaluations,
			  routeStatus.teamResourceDenialPolicyDenies,
			  routeStatus.teamResourceDenialScoreBoosts,
			  routeStatus.teamResourceDenialSelectedGoals,
			  routeStatus.teamResourceDenialInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_resource_denial_mode={} "
			  "last_team_resource_denial_role={} "
			  "last_team_resource_denial_lane={} "
			  "last_team_resource_denial_category={} "
			  "last_team_resource_denial_intent={} "
			  "last_team_resource_denial_entity={} "
			  "last_team_resource_denial_item={} "
			  "last_team_resource_denial_score_boost={} "
			  "last_team_resource_denial_profile_item_bonus={}\n",
			  routeStatus.lastTeamResourceDenialMode,
			  routeStatus.lastTeamResourceDenialRole,
			  routeStatus.lastTeamResourceDenialLane,
			  routeStatus.lastTeamResourceDenialCategory,
			  routeStatus.lastTeamResourceDenialIntent,
			  routeStatus.lastTeamResourceDenialEntity,
			  routeStatus.lastTeamResourceDenialItem,
			  routeStatus.lastTeamResourceDenialScoreBoost,
			  routeStatus.lastTeamResourceDenialProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "nav_interaction_elevator_activations={} "
			  "nav_interaction_activations={} "
			  "nav_interaction_candidates={} "
			  "nav_interaction_checks={}\n",
			  routeStatus.interactionElevatorActivations,
			  routeStatus.interactionActivations,
			  routeStatus.interactionCandidates,
			  routeStatus.interactionChecks);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "route_corner_cut_candidates={} "
			  "route_corner_cut_trace_checks={} "
			  "route_corner_cut_trace_hits={} "
			  "route_corner_cut_trace_misses={} "
			  "route_corner_cut_ground_trace_checks={} "
			  "route_corner_cut_ground_trace_misses={} "
			  "route_corner_cut_accepted={} "
			  "route_corner_cut_rejected={}\n",
			  routeStatus.cornerCutChecks,
			  routeStatus.cornerCutTraceAttempts,
			  routeStatus.cornerCutTracePasses,
			  routeStatus.cornerCutTraceFailures,
			  routeStatus.cornerCutGroundTraceAttempts,
			  routeStatus.cornerCutGroundTraceFailures,
			  routeStatus.cornerCutApplications,
			  routeStatus.cornerCutSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "corner_cut_candidates={} corner_cut_trace_checks={} "
			  "corner_cut_accepted={} "
			  "trace_checked_corner_cut_candidates={} "
			  "trace_checked_corner_cut_trace_checks={} "
			  "trace_checked_corner_cut_accepted={}\n",
			  routeStatus.cornerCutChecks,
			  routeStatus.cornerCutTraceAttempts,
			  routeStatus.cornerCutApplications,
			  routeStatus.cornerCutChecks,
			  routeStatus.cornerCutTraceAttempts,
			  routeStatus.cornerCutApplications);

	BotBrain_PrintActionProofStatus(actionStatus, itemStatus, combatStatus);
	BotBrain_PrintActionDetailProofStatus(actionStatus, itemStatus, combatStatus);

	BotBrain_PrintStatusFmt(
		"q3a_bot_blackboard_status "
			  "blackboard_frames={} blackboard_updates={} "
			  "blackboard_current_enemies={} "
			  "combat_enemy_acquisitions={} combat_enemy_switches={} "
			  "combat_enemy_retains={} combat_enemy_clears={} "
			  "combat_enemy_memory_retains={} combat_enemy_memory_decays={} "
			  "combat_enemy_memory_smoke_occlusions={} "
			  "combat_enemy_memory_smoke_seed_attempts={} "
			  "combat_enemy_memory_smoke_seeded={} "
			  "combat_enemy_memory_smoke_seed_no_peer={} "
			  "combat_enemy_memory_smoke_seed_invalid_peer={} "
			  "combat_enemy_memory_smoke_seed_no_blackboard={} "
			  "combat_enemy_visible={} combat_enemy_shootable={} "
			  "last_combat_enemy_entity={} last_combat_enemy_client={} "
			  "last_combat_enemy_visible={} last_combat_enemy_shootable={} "
			  "last_combat_enemy_retained_from_memory={} "
			  "last_combat_enemy_memory_age_ms={} "
			  "last_combat_enemy_memory_window_ms={} "
			  "last_combat_enemy_memory_decay_entity={} "
			  "last_combat_enemy_memory_decay_client={} "
			  "smoke_target_memory={}\n",
			  botBrainBlackboardStatus.frames,
			  botBrainBlackboardStatus.updates,
			  Bot_PerceptionActiveCurrentEnemies(),
			  botBrainBlackboardStatus.combatEnemyAcquisitions,
			  botBrainBlackboardStatus.combatEnemySwitches,
			  botBrainBlackboardStatus.combatEnemyRetains,
			  botBrainBlackboardStatus.combatEnemyClears,
			  botBrainBlackboardStatus.combatEnemyMemoryRetains,
			  botBrainBlackboardStatus.combatEnemyMemoryDecays,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeOcclusions,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedAttempts,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeeded,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoPeer,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedInvalidPeer,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoBlackboard,
			  botBrainBlackboardStatus.combatEnemyVisible,
			  botBrainBlackboardStatus.combatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyEntity,
			  botBrainBlackboardStatus.lastCombatEnemyClient,
			  botBrainBlackboardStatus.lastCombatEnemyVisible,
			  botBrainBlackboardStatus.lastCombatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyRetainedFromMemory,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryAgeMilliseconds,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryWindowMilliseconds,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryDecayEntity,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryDecayClient,
			  Bot_CommandTargetMemorySmokeEnabled() ? 1 : 0);

	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "pass={} frames={} commands={} route_commands={} "
			  "route_failures={} route_invalid_slots={} "
			  "item_goal_assignments={} last_item_goal_area={} "
			  "stuck_detections={} stuck_repath_refreshes={} "
			  "stuck_recovery_activations={} recovery_command_uses={} "
			  "expected_min_frames={} expected_min_commands={} "
			  "item_goal_peak_active_reservations={} "
			  "route_debug_missing_frames={} skipped_inactive={} "
			  "ffa_role_combat_requests={} "
			  "ffa_role_combat_policy_selections={} "
			  "ffa_role_combat_target_selections={} "
			  "ffa_role_combat_attack_decisions={} "
			  "ffa_role_combat_decision_overrides={} "
			  "ffa_role_combat_target_deferrals={} "
			  "ffa_role_combat_invalid_skips={} "
			  "last_ffa_role_combat_client={} "
			  "last_ffa_role_combat_mode={} "
			  "last_ffa_role_combat_mode_name={} "
			  "last_ffa_role_combat_role={} "
			  "last_ffa_role_combat_role_name={} "
			  "last_ffa_role_combat_lane={} "
			  "last_ffa_role_combat_lane_name={} "
			  "last_ffa_role_combat_priority={} "
			  "last_ffa_role_combat_target_client={} "
			  "last_ffa_role_combat_target_entity={} "
			  "last_ffa_role_combat_target_distance_sq={} "
			  "last_ffa_role_combat_target_visible={} "
			  "last_ffa_role_combat_target_shootable={} "
			  "last_ffa_role_combat_reason={} "
			  "ffa_spawn_camp_combat_avoidance_evaluations={} "
			  "ffa_spawn_camp_combat_avoidance_blocks={} "
			  "ffa_spawn_camp_combat_avoidance_source_blocks={} "
			  "ffa_spawn_camp_combat_avoidance_clears={} "
			  "ffa_spawn_camp_combat_avoidance_invalid_skips={} "
			  "last_ffa_spawn_camp_combat_avoidance_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_target_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_target_entity={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_entity={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_distance_sq={} "
			  "last_ffa_spawn_camp_combat_avoidance_policy_avoid={} "
			  "last_ffa_spawn_camp_combat_avoidance_blocked={} "
			  "last_ffa_spawn_camp_combat_avoidance_reason={} "
			  "team_role_combat_requests={} "
			  "team_role_combat_policy_selections={} "
			  "team_role_combat_target_selections={} "
			  "team_role_combat_attack_decisions={} "
			  "team_role_combat_decision_overrides={} "
			  "team_role_combat_target_deferrals={} "
			  "team_role_combat_invalid_skips={} "
			  "last_team_role_combat_client={} "
			  "last_team_role_combat_mode={} "
			  "last_team_role_combat_mode_name={} "
			  "last_team_role_combat_role={} "
			  "last_team_role_combat_role_name={} "
			  "last_team_role_combat_lane={} "
			  "last_team_role_combat_lane_name={} "
			  "last_team_role_combat_priority={} "
			  "last_team_role_combat_target_client={} "
			  "last_team_role_combat_target_entity={} "
			  "last_team_role_combat_target_distance_sq={} "
			  "last_team_role_combat_target_visible={} "
			  "last_team_role_combat_target_shootable={} "
			  "last_team_role_combat_reason={} "
			  "ctf_role_combat_requests={} "
			  "ctf_role_combat_policy_selections={} "
			  "ctf_role_combat_target_selections={} "
			  "ctf_role_combat_attack_decisions={} "
			  "ctf_role_combat_decision_overrides={} "
			  "ctf_role_combat_target_deferrals={} "
			  "ctf_role_combat_invalid_skips={} "
			  "last_ctf_role_combat_client={} "
			  "last_ctf_role_combat_mode={} "
			  "last_ctf_role_combat_mode_name={} "
			  "last_ctf_role_combat_role={} "
			  "last_ctf_role_combat_role_name={} "
			  "last_ctf_role_combat_lane={} "
			  "last_ctf_role_combat_lane_name={} "
			  "last_ctf_role_combat_priority={} "
			  "last_ctf_role_combat_target_client={} "
			  "last_ctf_role_combat_target_entity={} "
			  "last_ctf_role_combat_target_distance_sq={} "
			  "last_ctf_role_combat_target_visible={} "
			  "last_ctf_role_combat_target_shootable={} "
			  "last_ctf_role_combat_reason={} "
			  "team_fire_avoidance_evaluations={} "
			  "team_fire_avoidance_blocks={} "
			  "team_fire_avoidance_target_blocks={} "
			  "team_fire_avoidance_line_blocks={} "
			  "team_fire_avoidance_clears={} "
			  "team_fire_avoidance_invalid_skips={} "
			  "last_team_fire_avoidance_client={} "
			  "last_team_fire_avoidance_target_client={} "
			  "last_team_fire_avoidance_friendly_line={} "
			  "last_team_fire_avoidance_target_allowed={} "
			  "last_team_fire_avoidance_blocked={} "
			  "last_team_fire_avoidance_reason={}\n",
			  pass,
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  botFrameCommandStatus.routeCommands,
			  routeStatus.failures,
			  routeStatus.invalidSlots,
			  routeStatus.itemGoalAssignments,
			  routeStatus.lastItemGoalArea,
			  routeStatus.stuckDetections,
			  routeStatus.stuckRepathRefreshes,
			  routeStatus.stuckRecoveryActivations,
			  botFrameCommandStatus.recoveryCommandUses,
			  expectedMinFrames,
			  expectedMinCommands,
			  routeStatus.itemGoalPeakActiveReservations,
			  routeStatus.debugOverlayMissingFrames,
			  botFrameCommandStatus.skippedInactive,
			  botFrameCommandStatus.ffaRoleCombatRequests,
			  botFrameCommandStatus.ffaRoleCombatPolicySelections,
			  botFrameCommandStatus.ffaRoleCombatTargetSelections,
			  botFrameCommandStatus.ffaRoleCombatAttackDecisions,
			  botFrameCommandStatus.ffaRoleCombatDecisionOverrides,
			  botFrameCommandStatus.ffaRoleCombatTargetDeferrals,
			  botFrameCommandStatus.ffaRoleCombatInvalidSkips,
			  botFrameCommandStatus.lastFfaRoleCombatClient,
			  botFrameCommandStatus.lastFfaRoleCombatMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastFfaRoleCombatMode)),
			  botFrameCommandStatus.lastFfaRoleCombatRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastFfaRoleCombatRole)),
			  botFrameCommandStatus.lastFfaRoleCombatLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastFfaRoleCombatLane)),
			  botFrameCommandStatus.lastFfaRoleCombatPriority,
			  botFrameCommandStatus.lastFfaRoleCombatTargetClient,
			  botFrameCommandStatus.lastFfaRoleCombatTargetEntity,
			  botFrameCommandStatus.lastFfaRoleCombatTargetDistanceSquared,
			  botFrameCommandStatus.lastFfaRoleCombatTargetVisible,
			  botFrameCommandStatus.lastFfaRoleCombatTargetShootable,
			  botFrameCommandStatus.lastFfaRoleCombatReason,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceEvaluations,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceBlocks,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceSourceBlocks,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceClears,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceInvalidSkips,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetEntity,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceEntity,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceDistanceSquared,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidancePolicyAvoid,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceBlocked,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceReason,
			  botFrameCommandStatus.teamRoleCombatRequests,
			  botFrameCommandStatus.teamRoleCombatPolicySelections,
			  botFrameCommandStatus.teamRoleCombatTargetSelections,
			  botFrameCommandStatus.teamRoleCombatAttackDecisions,
			  botFrameCommandStatus.teamRoleCombatDecisionOverrides,
			  botFrameCommandStatus.teamRoleCombatTargetDeferrals,
			  botFrameCommandStatus.teamRoleCombatInvalidSkips,
			  botFrameCommandStatus.lastTeamRoleCombatClient,
			  botFrameCommandStatus.lastTeamRoleCombatMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastTeamRoleCombatMode)),
			  botFrameCommandStatus.lastTeamRoleCombatRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastTeamRoleCombatRole)),
			  botFrameCommandStatus.lastTeamRoleCombatLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastTeamRoleCombatLane)),
			  botFrameCommandStatus.lastTeamRoleCombatPriority,
			  botFrameCommandStatus.lastTeamRoleCombatTargetClient,
			  botFrameCommandStatus.lastTeamRoleCombatTargetEntity,
			  botFrameCommandStatus.lastTeamRoleCombatTargetDistanceSquared,
			  botFrameCommandStatus.lastTeamRoleCombatTargetVisible,
			  botFrameCommandStatus.lastTeamRoleCombatTargetShootable,
			  botFrameCommandStatus.lastTeamRoleCombatReason,
			  botFrameCommandStatus.ctfRoleCombatRequests,
			  botFrameCommandStatus.ctfRoleCombatPolicySelections,
			  botFrameCommandStatus.ctfRoleCombatTargetSelections,
			  botFrameCommandStatus.ctfRoleCombatAttackDecisions,
			  botFrameCommandStatus.ctfRoleCombatDecisionOverrides,
			  botFrameCommandStatus.ctfRoleCombatTargetDeferrals,
			  botFrameCommandStatus.ctfRoleCombatInvalidSkips,
			  botFrameCommandStatus.lastCtfRoleCombatClient,
			  botFrameCommandStatus.lastCtfRoleCombatMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastCtfRoleCombatMode)),
			  botFrameCommandStatus.lastCtfRoleCombatRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfRoleCombatRole)),
			  botFrameCommandStatus.lastCtfRoleCombatLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfRoleCombatLane)),
			  botFrameCommandStatus.lastCtfRoleCombatPriority,
			  botFrameCommandStatus.lastCtfRoleCombatTargetClient,
			  botFrameCommandStatus.lastCtfRoleCombatTargetEntity,
			  botFrameCommandStatus.lastCtfRoleCombatTargetDistanceSquared,
			  botFrameCommandStatus.lastCtfRoleCombatTargetVisible,
			  botFrameCommandStatus.lastCtfRoleCombatTargetShootable,
			  botFrameCommandStatus.lastCtfRoleCombatReason,
			  botFrameCommandStatus.teamFireAvoidanceEvaluations,
			  botFrameCommandStatus.teamFireAvoidanceBlocks,
			  botFrameCommandStatus.teamFireAvoidanceTargetBlocks,
			  botFrameCommandStatus.teamFireAvoidanceLineBlocks,
			  botFrameCommandStatus.teamFireAvoidanceClears,
			  botFrameCommandStatus.teamFireAvoidanceInvalidSkips,
			  botFrameCommandStatus.lastTeamFireAvoidanceClient,
			  botFrameCommandStatus.lastTeamFireAvoidanceTargetClient,
			  botFrameCommandStatus.lastTeamFireAvoidanceFriendlyLine,
			  botFrameCommandStatus.lastTeamFireAvoidanceTargetAllowed,
			  botFrameCommandStatus.lastTeamFireAvoidanceBlocked,
			  botFrameCommandStatus.lastTeamFireAvoidanceReason);

	BotBrain_PrintStatusFmt(
		"q3a_bot_coop_command_status "
			  "coop_leader_route_activations={} "
			  "coop_leader_route_refreshes={} "
			  "coop_leader_route_owner_deferrals={} "
			  "coop_leader_route_toward_sources={} "
			  "coop_leader_route_spacing_sources={} "
			  "coop_leader_route_invalid_skips={} "
			  "last_coop_leader_route_client={} "
			  "last_coop_leader_route_leader_client={} "
			  "last_coop_leader_route_intent={} "
			  "last_coop_leader_route_intent_name={} "
			  "last_coop_leader_route_leader_distance_sq={} "
			  "coop_lead_advance_requests={} "
			  "coop_lead_advance_policy_leads={} "
			  "coop_lead_advance_activations={} "
			  "coop_lead_advance_refreshes={} "
			  "coop_lead_advance_route_requests={} "
			  "coop_lead_advance_owner_deferrals={} "
			  "coop_lead_advance_route_deferrals={} "
			  "coop_lead_advance_expirations={} "
			  "coop_lead_advance_invalid_skips={} "
			  "last_coop_lead_advance_client={} "
			  "last_coop_lead_advance_intent={} "
			  "last_coop_lead_advance_intent_name={} "
			  "last_coop_lead_advance_remaining_ms={} "
			  "last_coop_lead_advance_goal_distance_sq={} "
			  "coop_progress_wait_requests={} "
			  "coop_progress_wait_policy_waits={} "
			  "coop_progress_wait_commands={} "
			  "coop_progress_wait_invalid_skips={} "
			  "last_coop_progress_wait_client={} "
			  "last_coop_progress_wait_leader_client={} "
			  "last_coop_progress_wait_intent={} "
			  "last_coop_progress_wait_intent_name={} "
			  "last_coop_progress_wait_leader_distance_sq={} "
			  "coop_anti_block_requests={} "
			  "coop_anti_block_policy_close={} "
			  "coop_anti_block_commands={} "
			  "coop_anti_block_invalid_skips={} "
			  "last_coop_anti_block_client={} "
			  "last_coop_anti_block_leader_client={} "
			  "last_coop_anti_block_intent={} "
			  "last_coop_anti_block_intent_name={} "
			  "last_coop_anti_block_leader_distance_sq={} "
			  "last_coop_anti_block_forward_move={} "
			  "last_coop_anti_block_side_move={} "
			  "coop_target_share_requests={} "
			  "coop_target_share_policy_supports={} "
			  "coop_target_share_source_scans={} "
			  "coop_target_share_source_candidates={} "
			  "coop_target_share_adoptions={} "
			  "coop_target_share_invalid_skips={} "
			  "last_coop_target_share_client={} "
			  "last_coop_target_share_source_client={} "
			  "last_coop_target_share_target_entity={} "
			  "last_coop_target_share_target_client={} "
			  "last_coop_target_share_target_distance_sq={} "
			  "last_coop_target_share_intent={} "
			  "last_coop_target_share_intent_name={} "
			  "coop_door_elevator_requests={} "
			  "coop_door_elevator_source_activations={} "
			  "coop_door_elevator_source_commands={} "
			  "coop_door_elevator_hold_commands={} "
			  "coop_door_elevator_invalid_skips={} "
			  "last_coop_door_elevator_client={} "
			  "last_coop_door_elevator_source_client={} "
			  "last_coop_door_elevator_action={} "
			  "last_coop_door_elevator_kind={} "
			  "last_coop_door_elevator_entity={} "
			  "last_coop_door_elevator_intent={} "
			  "last_coop_door_elevator_intent_name={} "
			  "coop_interaction_retry_requests={} "
			  "coop_interaction_retry_activations={} "
			  "coop_interaction_retry_commands={} "
			  "coop_interaction_retry_invalid_skips={} "
			  "last_coop_interaction_retry_client={} "
			  "last_coop_interaction_retry_action={} "
			  "last_coop_interaction_retry_kind={} "
			  "last_coop_interaction_retry_entity={}\n",
			  botFrameCommandStatus.coopLeaderRouteActivations,
			  botFrameCommandStatus.coopLeaderRouteRefreshes,
			  botFrameCommandStatus.coopLeaderRouteOwnerDeferrals,
			  botFrameCommandStatus.coopLeaderRouteTowardSources,
			  botFrameCommandStatus.coopLeaderRouteSpacingSources,
			  botFrameCommandStatus.coopLeaderRouteInvalidSkips,
			  botFrameCommandStatus.lastCoopLeaderRouteClient,
			  botFrameCommandStatus.lastCoopLeaderRouteLeaderClient,
			  botFrameCommandStatus.lastCoopLeaderRouteIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopLeaderRouteIntent)),
			  botFrameCommandStatus.lastCoopLeaderRouteLeaderDistanceSquared,
			  botFrameCommandStatus.coopLeadAdvanceRequests,
			  botFrameCommandStatus.coopLeadAdvancePolicyLeads,
			  botFrameCommandStatus.coopLeadAdvanceActivations,
			  botFrameCommandStatus.coopLeadAdvanceRefreshes,
			  botFrameCommandStatus.coopLeadAdvanceRouteRequests,
			  botFrameCommandStatus.coopLeadAdvanceOwnerDeferrals,
			  botFrameCommandStatus.coopLeadAdvanceRouteDeferrals,
			  botFrameCommandStatus.coopLeadAdvanceExpirations,
			  botFrameCommandStatus.coopLeadAdvanceInvalidSkips,
			  botFrameCommandStatus.lastCoopLeadAdvanceClient,
			  botFrameCommandStatus.lastCoopLeadAdvanceIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopLeadAdvanceIntent)),
			  botFrameCommandStatus.lastCoopLeadAdvanceRemainingMilliseconds,
			  botFrameCommandStatus.lastCoopLeadAdvanceGoalDistanceSquared,
			  botFrameCommandStatus.coopProgressWaitRequests,
			  botFrameCommandStatus.coopProgressWaitPolicyWaits,
			  botFrameCommandStatus.coopProgressWaitCommands,
			  botFrameCommandStatus.coopProgressWaitInvalidSkips,
			  botFrameCommandStatus.lastCoopProgressWaitClient,
			  botFrameCommandStatus.lastCoopProgressWaitLeaderClient,
			  botFrameCommandStatus.lastCoopProgressWaitIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopProgressWaitIntent)),
			  botFrameCommandStatus.lastCoopProgressWaitLeaderDistanceSquared,
			  botFrameCommandStatus.coopAntiBlockRequests,
			  botFrameCommandStatus.coopAntiBlockPolicyClose,
			  botFrameCommandStatus.coopAntiBlockCommands,
			  botFrameCommandStatus.coopAntiBlockInvalidSkips,
			  botFrameCommandStatus.lastCoopAntiBlockClient,
			  botFrameCommandStatus.lastCoopAntiBlockLeaderClient,
			  botFrameCommandStatus.lastCoopAntiBlockIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
			  botFrameCommandStatus.lastCoopAntiBlockIntent)),
			  botFrameCommandStatus.lastCoopAntiBlockLeaderDistanceSquared,
			  botFrameCommandStatus.lastCoopAntiBlockForwardMove,
			  botFrameCommandStatus.lastCoopAntiBlockSideMove,
			  botFrameCommandStatus.coopTargetShareRequests,
			  botFrameCommandStatus.coopTargetSharePolicySupports,
			  botFrameCommandStatus.coopTargetShareSourceScans,
			  botFrameCommandStatus.coopTargetShareSourceCandidates,
			  botFrameCommandStatus.coopTargetShareAdoptions,
			  botFrameCommandStatus.coopTargetShareInvalidSkips,
			  botFrameCommandStatus.lastCoopTargetShareClient,
			  botFrameCommandStatus.lastCoopTargetShareSourceClient,
			  botFrameCommandStatus.lastCoopTargetShareTargetEntity,
			  botFrameCommandStatus.lastCoopTargetShareTargetClient,
			  botFrameCommandStatus.lastCoopTargetShareTargetDistanceSquared,
			  botFrameCommandStatus.lastCoopTargetShareIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopTargetShareIntent)),
			  botFrameCommandStatus.coopDoorElevatorRequests,
			  botFrameCommandStatus.coopDoorElevatorSourceActivations,
			  botFrameCommandStatus.coopDoorElevatorSourceCommands,
			  botFrameCommandStatus.coopDoorElevatorHoldCommands,
			  botFrameCommandStatus.coopDoorElevatorInvalidSkips,
			  botFrameCommandStatus.lastCoopDoorElevatorClient,
			  botFrameCommandStatus.lastCoopDoorElevatorSourceClient,
			  botFrameCommandStatus.lastCoopDoorElevatorAction,
			  botFrameCommandStatus.lastCoopDoorElevatorKind,
			  botFrameCommandStatus.lastCoopDoorElevatorEntity,
			  botFrameCommandStatus.lastCoopDoorElevatorIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopDoorElevatorIntent)),
			  botFrameCommandStatus.coopInteractionRetryRequests,
			  botFrameCommandStatus.coopInteractionRetryActivations,
			  botFrameCommandStatus.coopInteractionRetryCommands,
			  botFrameCommandStatus.coopInteractionRetryInvalidSkips,
			  botFrameCommandStatus.lastCoopInteractionRetryClient,
			  botFrameCommandStatus.lastCoopInteractionRetryAction,
			  botFrameCommandStatus.lastCoopInteractionRetryKind,
			  botFrameCommandStatus.lastCoopInteractionRetryEntity);

	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status pass={} frames={} commands={} "
			  "ffa_spawn_camp_avoidance_requests={} "
			  "ffa_spawn_camp_avoidance_policy_selections={} "
			  "ffa_spawn_camp_avoidance_source_selections={} "
			  "ffa_spawn_camp_avoidance_activations={} "
			  "ffa_spawn_camp_avoidance_fallbacks={} "
			  "ffa_spawn_camp_avoidance_route_requests={} "
			  "ffa_spawn_camp_avoidance_invalid_skips={} "
			  "last_ffa_spawn_camp_avoidance_client={} "
			  "last_ffa_spawn_camp_avoidance_source_client={} "
			  "last_ffa_spawn_camp_avoidance_source_entity={} "
			  "last_ffa_spawn_camp_avoidance_source_distance_sq={} "
			  "last_ffa_spawn_camp_avoidance_policy_avoid={} "
			  "last_ffa_spawn_camp_avoidance_goal_distance_sq={} "
			  "last_ffa_spawn_camp_avoidance_reason={}\n",
			  pass,
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  botFrameCommandStatus.ffaSpawnCampAvoidanceRequests,
			  botFrameCommandStatus.ffaSpawnCampAvoidancePolicySelections,
			  botFrameCommandStatus.ffaSpawnCampAvoidanceSourceSelections,
			  botFrameCommandStatus.ffaSpawnCampAvoidanceActivations,
			  botFrameCommandStatus.ffaSpawnCampAvoidanceFallbacks,
			  botFrameCommandStatus.ffaSpawnCampAvoidanceRouteRequests,
			  botFrameCommandStatus.ffaSpawnCampAvoidanceInvalidSkips,
			  botFrameCommandStatus.lastFfaSpawnCampAvoidanceClient,
			  botFrameCommandStatus.lastFfaSpawnCampAvoidanceSourceClient,
			  botFrameCommandStatus.lastFfaSpawnCampAvoidanceSourceEntity,
			  botFrameCommandStatus.lastFfaSpawnCampAvoidanceSourceDistanceSquared,
			  botFrameCommandStatus.lastFfaSpawnCampAvoidancePolicyAvoid,
			  botFrameCommandStatus.lastFfaSpawnCampAvoidanceGoalDistanceSquared,
			  botFrameCommandStatus.lastFfaSpawnCampAvoidanceReason);

	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status pass={} frames={} commands={} "
			  "ffa_spawn_camp_combat_avoidance_evaluations={} "
			  "ffa_spawn_camp_combat_avoidance_blocks={} "
			  "ffa_spawn_camp_combat_avoidance_source_blocks={} "
			  "ffa_spawn_camp_combat_avoidance_clears={} "
			  "ffa_spawn_camp_combat_avoidance_invalid_skips={} "
			  "last_ffa_spawn_camp_combat_avoidance_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_target_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_target_entity={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_entity={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_distance_sq={} "
			  "last_ffa_spawn_camp_combat_avoidance_policy_avoid={} "
			  "last_ffa_spawn_camp_combat_avoidance_blocked={} "
			  "last_ffa_spawn_camp_combat_avoidance_reason={}\n",
			  pass,
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceEvaluations,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceBlocks,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceSourceBlocks,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceClears,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceInvalidSkips,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetEntity,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceEntity,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceDistanceSquared,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidancePolicyAvoid,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceBlocked,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceReason);

	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status pass={} frames={} commands={} "
			  "threat_retreat_requests={} "
			  "threat_retreat_enemy_sources={} "
			  "threat_retreat_damage_sources={} "
			  "threat_retreat_fallback_sources={} "
			  "threat_retreat_activations={} "
			  "threat_retreat_refreshes={} "
			  "threat_retreat_route_requests={} "
			  "threat_retreat_route_deferrals={} "
			  "threat_retreat_expirations={} "
			  "threat_retreat_invalid_skips={} "
			  "threat_retreat_attack_suppressions={} "
			  "threat_retreat_reengages={} "
			  "last_threat_retreat_client={} "
			  "last_threat_retreat_source_client={} "
			  "last_threat_retreat_source_entity={} "
			  "last_threat_retreat_source_distance_sq={} "
			  "last_threat_retreat_remaining_ms={} "
			  "last_threat_retreat_goal_distance_sq={} "
			  "last_threat_retreat_health={} "
			  "last_threat_retreat_armor={} "
			  "last_threat_retreat_low_health={} "
			  "last_threat_retreat_active={} "
			  "last_threat_retreat_reason={}\n",
			  pass,
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  botFrameCommandStatus.threatRetreatRequests,
			  botFrameCommandStatus.threatRetreatEnemySources,
			  botFrameCommandStatus.threatRetreatDamageSources,
			  botFrameCommandStatus.threatRetreatFallbackSources,
			  botFrameCommandStatus.threatRetreatActivations,
			  botFrameCommandStatus.threatRetreatRefreshes,
			  botFrameCommandStatus.threatRetreatRouteRequests,
			  botFrameCommandStatus.threatRetreatRouteDeferrals,
			  botFrameCommandStatus.threatRetreatExpirations,
			  botFrameCommandStatus.threatRetreatInvalidSkips,
			  botFrameCommandStatus.threatRetreatAttackSuppressions,
			  botFrameCommandStatus.threatRetreatReengages,
			  botFrameCommandStatus.lastThreatRetreatClient,
			  botFrameCommandStatus.lastThreatRetreatSourceClient,
			  botFrameCommandStatus.lastThreatRetreatSourceEntity,
			  botFrameCommandStatus.lastThreatRetreatSourceDistanceSquared,
			  botFrameCommandStatus.lastThreatRetreatRemainingMilliseconds,
			  botFrameCommandStatus.lastThreatRetreatGoalDistanceSquared,
			  botFrameCommandStatus.lastThreatRetreatHealth,
			  botFrameCommandStatus.lastThreatRetreatArmor,
			  botFrameCommandStatus.lastThreatRetreatLowHealth,
			  botFrameCommandStatus.lastThreatRetreatActive,
			  botFrameCommandStatus.lastThreatRetreatReason);

	BotBrain_PrintStatusFmt(
		"q3a_bot_route_schedule_status pass={} "
			  "route_recompute_rate_limit_checks={} "
			  "route_recompute_rate_limit_reuses={} "
			  "route_recompute_rate_limit_refreshes={} "
			  "item_goal_desirability_updates={} "
			  "item_goal_desirability_cache_reuses={} "
			  "item_goal_desirability_stagger_deferrals={}\n",
			  pass,
			  routeStatus.routeRecomputeRateLimitChecks,
			  routeStatus.routeRecomputeRateLimitReuses,
			  routeStatus.routeRecomputeRateLimitRefreshes,
			  routeStatus.itemGoalDesirabilityUpdates,
			  routeStatus.itemGoalDesirabilityCacheReuses,
			  routeStatus.itemGoalDesirabilityStaggerDeferrals);

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
			  "timed_route_goal_activations={} "
			  "timed_route_goal_route_requests={} "
			  "timed_route_goal_route_deferrals={} "
			  "timed_route_goal_expirations={} "
			  "timed_route_goal_invalid_skips={} "
			  "last_timed_route_goal_kind={} "
			  "last_timed_route_goal_kind_name={} "
			  "last_timed_route_goal_client={} "
			  "last_timed_route_goal_remaining_ms={} "
			  "last_timed_route_goal_source_x={} "
			  "last_timed_route_goal_source_y={} "
			  "last_timed_route_goal_source_z={} "
			  "last_timed_route_goal_goal_x={} "
			  "last_timed_route_goal_goal_y={} "
			  "last_timed_route_goal_goal_z={} "
			  "last_timed_route_goal_distance_sq={} "
			  "teleporter_escape_route_activations={} "
			  "teleporter_escape_fallback_sources={} "
			  "teleporter_escape_damage_sources={} "
			  "teleporter_escape_invalid_skips={} "
			  "ffa_roam_route_requests={} "
			  "ffa_roam_route_policy_selections={} "
			  "ffa_roam_route_activations={} "
			  "ffa_roam_route_refreshes={} "
			  "ffa_roam_route_owner_deferrals={} "
			  "ffa_roam_route_route_requests={} "
			  "ffa_roam_route_route_deferrals={} "
			  "ffa_roam_route_expirations={} "
			  "ffa_roam_route_invalid_skips={} "
			  "last_ffa_roam_route_client={} "
			  "last_ffa_roam_route_mode={} "
			  "last_ffa_roam_route_mode_name={} "
			  "last_ffa_roam_route_role={} "
			  "last_ffa_roam_route_role_name={} "
			  "last_ffa_roam_route_lane={} "
			  "last_ffa_roam_route_lane_name={} "
			  "last_ffa_roam_route_priority={} "
			  "last_ffa_roam_route_remaining_ms={} "
			  "last_ffa_roam_route_goal_distance_sq={} "
			  "team_role_route_requests={} "
			  "team_role_route_policy_selections={} "
			  "team_role_route_activations={} "
			  "team_role_route_refreshes={} "
			  "team_role_route_owner_deferrals={} "
			  "team_role_route_route_requests={} "
			  "team_role_route_route_deferrals={} "
			  "team_role_route_expirations={} "
			  "team_role_route_invalid_skips={} "
			  "last_team_role_route_client={} "
			  "last_team_role_route_mode={} "
			  "last_team_role_route_mode_name={} "
			  "last_team_role_route_role={} "
			  "last_team_role_route_role_name={} "
			  "last_team_role_route_lane={} "
			  "last_team_role_route_lane_name={} "
			  "last_team_role_route_priority={} "
			  "last_team_role_route_remaining_ms={} "
			  "last_team_role_route_goal_distance_sq={} "
			  "ctf_role_route_requests={} "
			  "ctf_role_route_policy_selections={} "
			  "ctf_role_route_activations={} "
			  "ctf_role_route_refreshes={} "
			  "ctf_role_route_owner_deferrals={} "
			  "ctf_role_route_objective_deferrals={} "
			  "ctf_role_route_route_requests={} "
			  "ctf_role_route_route_deferrals={} "
			  "ctf_role_route_expirations={} "
			  "ctf_role_route_invalid_skips={} "
			  "last_ctf_role_route_client={} "
			  "last_ctf_role_route_mode={} "
			  "last_ctf_role_route_mode_name={} "
			  "last_ctf_role_route_role={} "
			  "last_ctf_role_route_role_name={} "
			  "last_ctf_role_route_lane={} "
			  "last_ctf_role_route_lane_name={} "
			  "last_ctf_role_route_priority={} "
			  "last_ctf_role_route_remaining_ms={} "
			  "last_ctf_role_route_goal_distance_sq={} "
			  "coop_leader_route_activations={} "
			  "coop_leader_route_refreshes={} "
			  "coop_leader_route_owner_deferrals={} "
			  "coop_leader_route_toward_sources={} "
			  "coop_leader_route_spacing_sources={} "
			  "coop_leader_route_invalid_skips={} "
			  "last_coop_leader_route_client={} "
			  "last_coop_leader_route_leader_client={} "
			  "last_coop_leader_route_intent={} "
			  "last_coop_leader_route_intent_name={} "
			  "last_coop_leader_route_leader_distance_sq={} "
			  "nuke_retreat_activations={} "
			  "nuke_retreat_fallback_sources={} "
			  "nuke_retreat_route_requests={} "
			  "nuke_retreat_route_deferrals={} "
			  "nuke_retreat_expirations={} "
			  "nuke_retreat_invalid_skips={} "
			  "last_nuke_retreat_client={} "
			  "last_nuke_retreat_remaining_ms={} "
			  "last_nuke_retreat_source_x={} "
			  "last_nuke_retreat_source_y={} "
			  "last_nuke_retreat_source_z={} "
			  "last_nuke_retreat_goal_x={} "
			  "last_nuke_retreat_goal_y={} "
			  "last_nuke_retreat_goal_z={} "
			  "last_nuke_retreat_distance_sq={} "
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
			  botFrameCommandStatus.timedRouteGoalActivations,
			  botFrameCommandStatus.timedRouteGoalRouteRequests,
			  botFrameCommandStatus.timedRouteGoalRouteDeferrals,
			  botFrameCommandStatus.timedRouteGoalExpirations,
			  botFrameCommandStatus.timedRouteGoalInvalidSkips,
			  botFrameCommandStatus.lastTimedRouteGoalKind,
			  Bot_CommandTimedRouteGoalKindName(static_cast<BotTimedRouteGoalKind>(
				  botFrameCommandStatus.lastTimedRouteGoalKind)),
			  botFrameCommandStatus.lastTimedRouteGoalClient,
			  botFrameCommandStatus.lastTimedRouteGoalRemainingMilliseconds,
			  botFrameCommandStatus.lastTimedRouteGoalSourceX,
			  botFrameCommandStatus.lastTimedRouteGoalSourceY,
			  botFrameCommandStatus.lastTimedRouteGoalSourceZ,
			  botFrameCommandStatus.lastTimedRouteGoalGoalX,
			  botFrameCommandStatus.lastTimedRouteGoalGoalY,
			  botFrameCommandStatus.lastTimedRouteGoalGoalZ,
			  botFrameCommandStatus.lastTimedRouteGoalDistanceSquared,
			  botFrameCommandStatus.teleporterEscapeRouteActivations,
			  botFrameCommandStatus.teleporterEscapeFallbackSources,
			  botFrameCommandStatus.teleporterEscapeDamageSources,
			  botFrameCommandStatus.teleporterEscapeInvalidSkips,
			  botFrameCommandStatus.ffaRoamRouteRequests,
			  botFrameCommandStatus.ffaRoamRoutePolicySelections,
			  botFrameCommandStatus.ffaRoamRouteActivations,
			  botFrameCommandStatus.ffaRoamRouteRefreshes,
			  botFrameCommandStatus.ffaRoamRouteOwnerDeferrals,
			  botFrameCommandStatus.ffaRoamRouteRouteRequests,
			  botFrameCommandStatus.ffaRoamRouteRouteDeferrals,
			  botFrameCommandStatus.ffaRoamRouteExpirations,
			  botFrameCommandStatus.ffaRoamRouteInvalidSkips,
			  botFrameCommandStatus.lastFfaRoamRouteClient,
			  botFrameCommandStatus.lastFfaRoamRouteMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastFfaRoamRouteMode)),
			  botFrameCommandStatus.lastFfaRoamRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastFfaRoamRouteRole)),
			  botFrameCommandStatus.lastFfaRoamRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastFfaRoamRouteLane)),
			  botFrameCommandStatus.lastFfaRoamRoutePriority,
			  botFrameCommandStatus.lastFfaRoamRouteRemainingMilliseconds,
			  botFrameCommandStatus.lastFfaRoamRouteGoalDistanceSquared,
			  botFrameCommandStatus.teamRoleRouteRequests,
			  botFrameCommandStatus.teamRoleRoutePolicySelections,
			  botFrameCommandStatus.teamRoleRouteActivations,
			  botFrameCommandStatus.teamRoleRouteRefreshes,
			  botFrameCommandStatus.teamRoleRouteOwnerDeferrals,
			  botFrameCommandStatus.teamRoleRouteRouteRequests,
			  botFrameCommandStatus.teamRoleRouteRouteDeferrals,
			  botFrameCommandStatus.teamRoleRouteExpirations,
			  botFrameCommandStatus.teamRoleRouteInvalidSkips,
			  botFrameCommandStatus.lastTeamRoleRouteClient,
			  botFrameCommandStatus.lastTeamRoleRouteMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastTeamRoleRouteMode)),
			  botFrameCommandStatus.lastTeamRoleRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastTeamRoleRouteRole)),
			  botFrameCommandStatus.lastTeamRoleRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastTeamRoleRouteLane)),
			  botFrameCommandStatus.lastTeamRoleRoutePriority,
			  botFrameCommandStatus.lastTeamRoleRouteRemainingMilliseconds,
			  botFrameCommandStatus.lastTeamRoleRouteGoalDistanceSquared,
			  botFrameCommandStatus.ctfRoleRouteRequests,
			  botFrameCommandStatus.ctfRoleRoutePolicySelections,
			  botFrameCommandStatus.ctfRoleRouteActivations,
			  botFrameCommandStatus.ctfRoleRouteRefreshes,
			  botFrameCommandStatus.ctfRoleRouteOwnerDeferrals,
			  botFrameCommandStatus.ctfRoleRouteObjectiveDeferrals,
			  botFrameCommandStatus.ctfRoleRouteRouteRequests,
			  botFrameCommandStatus.ctfRoleRouteRouteDeferrals,
			  botFrameCommandStatus.ctfRoleRouteExpirations,
			  botFrameCommandStatus.ctfRoleRouteInvalidSkips,
			  botFrameCommandStatus.lastCtfRoleRouteClient,
			  botFrameCommandStatus.lastCtfRoleRouteMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastCtfRoleRouteMode)),
			  botFrameCommandStatus.lastCtfRoleRouteRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfRoleRouteRole)),
			  botFrameCommandStatus.lastCtfRoleRouteLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfRoleRouteLane)),
			  botFrameCommandStatus.lastCtfRoleRoutePriority,
			  botFrameCommandStatus.lastCtfRoleRouteRemainingMilliseconds,
			  botFrameCommandStatus.lastCtfRoleRouteGoalDistanceSquared,
			  botFrameCommandStatus.coopLeaderRouteActivations,
			  botFrameCommandStatus.coopLeaderRouteRefreshes,
			  botFrameCommandStatus.coopLeaderRouteOwnerDeferrals,
			  botFrameCommandStatus.coopLeaderRouteTowardSources,
			  botFrameCommandStatus.coopLeaderRouteSpacingSources,
			  botFrameCommandStatus.coopLeaderRouteInvalidSkips,
			  botFrameCommandStatus.lastCoopLeaderRouteClient,
			  botFrameCommandStatus.lastCoopLeaderRouteLeaderClient,
			  botFrameCommandStatus.lastCoopLeaderRouteIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopLeaderRouteIntent)),
			  botFrameCommandStatus.lastCoopLeaderRouteLeaderDistanceSquared,
			  botFrameCommandStatus.nukeRetreatActivations,
			  botFrameCommandStatus.nukeRetreatFallbackSources,
			  botFrameCommandStatus.nukeRetreatRouteRequests,
			  botFrameCommandStatus.nukeRetreatRouteDeferrals,
			  botFrameCommandStatus.nukeRetreatExpirations,
			  botFrameCommandStatus.nukeRetreatInvalidSkips,
			  botFrameCommandStatus.lastNukeRetreatClient,
			  botFrameCommandStatus.lastNukeRetreatRemainingMilliseconds,
			  botFrameCommandStatus.lastNukeRetreatSourceX,
			  botFrameCommandStatus.lastNukeRetreatSourceY,
			  botFrameCommandStatus.lastNukeRetreatSourceZ,
			  botFrameCommandStatus.lastNukeRetreatGoalX,
			  botFrameCommandStatus.lastNukeRetreatGoalY,
			  botFrameCommandStatus.lastNukeRetreatGoalZ,
			  botFrameCommandStatus.lastNukeRetreatDistanceSquared,
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

	BotBrain_PrintStatusFmt(
		"q3a_bot_blackboard_status "
			  "blackboard_frames={} blackboard_updates={} "
			  "blackboard_current_enemies={} "
			  "combat_enemy_acquisitions={} combat_enemy_switches={} "
			  "combat_enemy_retains={} combat_enemy_clears={} "
			  "combat_enemy_memory_retains={} combat_enemy_memory_decays={} "
			  "combat_enemy_memory_smoke_occlusions={} "
			  "combat_enemy_memory_smoke_seed_attempts={} "
			  "combat_enemy_memory_smoke_seeded={} "
			  "combat_enemy_memory_smoke_seed_no_peer={} "
			  "combat_enemy_memory_smoke_seed_invalid_peer={} "
			  "combat_enemy_memory_smoke_seed_no_blackboard={} "
			  "combat_enemy_visible={} combat_enemy_shootable={} "
			  "last_combat_enemy_entity={} last_combat_enemy_client={} "
			  "last_combat_enemy_visible={} last_combat_enemy_shootable={} "
			  "last_combat_enemy_retained_from_memory={} "
			  "last_combat_enemy_memory_age_ms={} "
			  "last_combat_enemy_memory_window_ms={} "
			  "last_combat_enemy_memory_decay_entity={} "
			  "last_combat_enemy_memory_decay_client={} "
			  "smoke_target_memory={}\n",
			  botBrainBlackboardStatus.frames,
			  botBrainBlackboardStatus.updates,
			  Bot_PerceptionActiveCurrentEnemies(),
			  botBrainBlackboardStatus.combatEnemyAcquisitions,
			  botBrainBlackboardStatus.combatEnemySwitches,
			  botBrainBlackboardStatus.combatEnemyRetains,
			  botBrainBlackboardStatus.combatEnemyClears,
			  botBrainBlackboardStatus.combatEnemyMemoryRetains,
			  botBrainBlackboardStatus.combatEnemyMemoryDecays,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeOcclusions,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedAttempts,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeeded,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoPeer,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedInvalidPeer,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoBlackboard,
			  botBrainBlackboardStatus.combatEnemyVisible,
			  botBrainBlackboardStatus.combatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyEntity,
			  botBrainBlackboardStatus.lastCombatEnemyClient,
			  botBrainBlackboardStatus.lastCombatEnemyVisible,
			  botBrainBlackboardStatus.lastCombatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyRetainedFromMemory,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryAgeMilliseconds,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryWindowMilliseconds,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryDecayEntity,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryDecayClient,
			  Bot_CommandTargetMemorySmokeEnabled() ? 1 : 0);

	BotBrain_PrintStatusFmt(
		"q3a_bot_controlled_recovery_status pass={} "
			  "controlled_inactive_recovery_attempts={} "
			  "controlled_inactive_recovery_commands={} "
			  "controlled_inactive_recovery_respawn_commands={} "
			  "controlled_inactive_recovery_spectator_joins={} "
			  "controlled_inactive_recovery_spectator_skips={} "
			  "controlled_inactive_recovery_invalid_skips={} "
			  "last_controlled_inactive_recovery_client={} "
			  "last_controlled_inactive_recovery_mode={} "
			  "last_controlled_inactive_recovery_buttons={}\n",
			  pass,
			  botFrameCommandStatus.controlledInactiveRecoveryAttempts,
			  botFrameCommandStatus.controlledInactiveRecoveryCommands,
			  botFrameCommandStatus.controlledInactiveRecoveryRespawnCommands,
			  botFrameCommandStatus.controlledInactiveRecoverySpectatorJoins,
			  botFrameCommandStatus.controlledInactiveRecoverySpectatorSkips,
			  botFrameCommandStatus.controlledInactiveRecoveryInvalidSkips,
			  botFrameCommandStatus.lastControlledInactiveRecoveryClient,
			  botFrameCommandStatus.lastControlledInactiveRecoveryMode,
			  botFrameCommandStatus.lastControlledInactiveRecoveryButtons);

	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status pass={} frames={} commands={} "
			  "ffa_role_combat_requests={} "
			  "ffa_role_combat_policy_selections={} "
			  "ffa_role_combat_target_selections={} "
			  "ffa_role_combat_attack_decisions={} "
			  "ffa_role_combat_decision_overrides={} "
			  "ffa_role_combat_target_deferrals={} "
			  "ffa_role_combat_invalid_skips={} "
			  "last_ffa_role_combat_client={} "
			  "last_ffa_role_combat_mode={} "
			  "last_ffa_role_combat_mode_name={} "
			  "last_ffa_role_combat_role={} "
			  "last_ffa_role_combat_role_name={} "
			  "last_ffa_role_combat_lane={} "
			  "last_ffa_role_combat_lane_name={} "
			  "last_ffa_role_combat_priority={} "
			  "last_ffa_role_combat_target_client={} "
			  "last_ffa_role_combat_target_entity={} "
			  "last_ffa_role_combat_target_distance_sq={} "
			  "last_ffa_role_combat_target_visible={} "
			  "last_ffa_role_combat_target_shootable={} "
			  "last_ffa_role_combat_reason={} "
			  "ffa_spawn_camp_combat_avoidance_evaluations={} "
			  "ffa_spawn_camp_combat_avoidance_blocks={} "
			  "ffa_spawn_camp_combat_avoidance_source_blocks={} "
			  "ffa_spawn_camp_combat_avoidance_clears={} "
			  "ffa_spawn_camp_combat_avoidance_invalid_skips={} "
			  "last_ffa_spawn_camp_combat_avoidance_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_target_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_target_entity={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_client={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_entity={} "
			  "last_ffa_spawn_camp_combat_avoidance_source_distance_sq={} "
			  "last_ffa_spawn_camp_combat_avoidance_policy_avoid={} "
			  "last_ffa_spawn_camp_combat_avoidance_blocked={} "
			  "last_ffa_spawn_camp_combat_avoidance_reason={} "
			  "team_role_combat_requests={} "
			  "team_role_combat_policy_selections={} "
			  "team_role_combat_target_selections={} "
			  "team_role_combat_attack_decisions={} "
			  "team_role_combat_decision_overrides={} "
			  "team_role_combat_target_deferrals={} "
			  "team_role_combat_invalid_skips={} "
			  "last_team_role_combat_client={} "
			  "last_team_role_combat_mode={} "
			  "last_team_role_combat_mode_name={} "
			  "last_team_role_combat_role={} "
			  "last_team_role_combat_role_name={} "
			  "last_team_role_combat_lane={} "
			  "last_team_role_combat_lane_name={} "
			  "last_team_role_combat_priority={} "
			  "last_team_role_combat_target_client={} "
			  "last_team_role_combat_target_entity={} "
			  "last_team_role_combat_target_distance_sq={} "
			  "last_team_role_combat_target_visible={} "
			  "last_team_role_combat_target_shootable={} "
			  "last_team_role_combat_reason={} "
			  "ctf_role_combat_requests={} "
			  "ctf_role_combat_policy_selections={} "
			  "ctf_role_combat_target_selections={} "
			  "ctf_role_combat_attack_decisions={} "
			  "ctf_role_combat_decision_overrides={} "
			  "ctf_role_combat_target_deferrals={} "
			  "ctf_role_combat_invalid_skips={} "
			  "last_ctf_role_combat_client={} "
			  "last_ctf_role_combat_mode={} "
			  "last_ctf_role_combat_mode_name={} "
			  "last_ctf_role_combat_role={} "
			  "last_ctf_role_combat_role_name={} "
			  "last_ctf_role_combat_lane={} "
			  "last_ctf_role_combat_lane_name={} "
			  "last_ctf_role_combat_priority={} "
			  "last_ctf_role_combat_target_client={} "
			  "last_ctf_role_combat_target_entity={} "
			  "last_ctf_role_combat_target_distance_sq={} "
			  "last_ctf_role_combat_target_visible={} "
			  "last_ctf_role_combat_target_shootable={} "
			  "last_ctf_role_combat_reason={} "
			  "team_fire_avoidance_evaluations={} "
			  "team_fire_avoidance_blocks={} "
			  "team_fire_avoidance_target_blocks={} "
			  "team_fire_avoidance_line_blocks={} "
			  "team_fire_avoidance_clears={} "
			  "team_fire_avoidance_invalid_skips={} "
			  "last_team_fire_avoidance_client={} "
			  "last_team_fire_avoidance_target_client={} "
			  "last_team_fire_avoidance_friendly_line={} "
			  "last_team_fire_avoidance_target_allowed={} "
			  "last_team_fire_avoidance_blocked={} "
			  "last_team_fire_avoidance_reason={}\n",
			  pass,
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  botFrameCommandStatus.ffaRoleCombatRequests,
			  botFrameCommandStatus.ffaRoleCombatPolicySelections,
			  botFrameCommandStatus.ffaRoleCombatTargetSelections,
			  botFrameCommandStatus.ffaRoleCombatAttackDecisions,
			  botFrameCommandStatus.ffaRoleCombatDecisionOverrides,
			  botFrameCommandStatus.ffaRoleCombatTargetDeferrals,
			  botFrameCommandStatus.ffaRoleCombatInvalidSkips,
			  botFrameCommandStatus.lastFfaRoleCombatClient,
			  botFrameCommandStatus.lastFfaRoleCombatMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastFfaRoleCombatMode)),
			  botFrameCommandStatus.lastFfaRoleCombatRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastFfaRoleCombatRole)),
			  botFrameCommandStatus.lastFfaRoleCombatLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastFfaRoleCombatLane)),
			  botFrameCommandStatus.lastFfaRoleCombatPriority,
			  botFrameCommandStatus.lastFfaRoleCombatTargetClient,
			  botFrameCommandStatus.lastFfaRoleCombatTargetEntity,
			  botFrameCommandStatus.lastFfaRoleCombatTargetDistanceSquared,
			  botFrameCommandStatus.lastFfaRoleCombatTargetVisible,
			  botFrameCommandStatus.lastFfaRoleCombatTargetShootable,
			  botFrameCommandStatus.lastFfaRoleCombatReason,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceEvaluations,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceBlocks,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceSourceBlocks,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceClears,
			  botFrameCommandStatus.ffaSpawnCampCombatAvoidanceInvalidSkips,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceTargetEntity,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceClient,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceEntity,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceSourceDistanceSquared,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidancePolicyAvoid,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceBlocked,
			  botFrameCommandStatus.lastFfaSpawnCampCombatAvoidanceReason,
			  botFrameCommandStatus.teamRoleCombatRequests,
			  botFrameCommandStatus.teamRoleCombatPolicySelections,
			  botFrameCommandStatus.teamRoleCombatTargetSelections,
			  botFrameCommandStatus.teamRoleCombatAttackDecisions,
			  botFrameCommandStatus.teamRoleCombatDecisionOverrides,
			  botFrameCommandStatus.teamRoleCombatTargetDeferrals,
			  botFrameCommandStatus.teamRoleCombatInvalidSkips,
			  botFrameCommandStatus.lastTeamRoleCombatClient,
			  botFrameCommandStatus.lastTeamRoleCombatMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastTeamRoleCombatMode)),
			  botFrameCommandStatus.lastTeamRoleCombatRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastTeamRoleCombatRole)),
			  botFrameCommandStatus.lastTeamRoleCombatLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastTeamRoleCombatLane)),
			  botFrameCommandStatus.lastTeamRoleCombatPriority,
			  botFrameCommandStatus.lastTeamRoleCombatTargetClient,
			  botFrameCommandStatus.lastTeamRoleCombatTargetEntity,
			  botFrameCommandStatus.lastTeamRoleCombatTargetDistanceSquared,
			  botFrameCommandStatus.lastTeamRoleCombatTargetVisible,
			  botFrameCommandStatus.lastTeamRoleCombatTargetShootable,
			  botFrameCommandStatus.lastTeamRoleCombatReason,
			  botFrameCommandStatus.ctfRoleCombatRequests,
			  botFrameCommandStatus.ctfRoleCombatPolicySelections,
			  botFrameCommandStatus.ctfRoleCombatTargetSelections,
			  botFrameCommandStatus.ctfRoleCombatAttackDecisions,
			  botFrameCommandStatus.ctfRoleCombatDecisionOverrides,
			  botFrameCommandStatus.ctfRoleCombatTargetDeferrals,
			  botFrameCommandStatus.ctfRoleCombatInvalidSkips,
			  botFrameCommandStatus.lastCtfRoleCombatClient,
			  botFrameCommandStatus.lastCtfRoleCombatMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  botFrameCommandStatus.lastCtfRoleCombatMode)),
			  botFrameCommandStatus.lastCtfRoleCombatRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  botFrameCommandStatus.lastCtfRoleCombatRole)),
			  botFrameCommandStatus.lastCtfRoleCombatLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  botFrameCommandStatus.lastCtfRoleCombatLane)),
			  botFrameCommandStatus.lastCtfRoleCombatPriority,
			  botFrameCommandStatus.lastCtfRoleCombatTargetClient,
			  botFrameCommandStatus.lastCtfRoleCombatTargetEntity,
			  botFrameCommandStatus.lastCtfRoleCombatTargetDistanceSquared,
			  botFrameCommandStatus.lastCtfRoleCombatTargetVisible,
			  botFrameCommandStatus.lastCtfRoleCombatTargetShootable,
			  botFrameCommandStatus.lastCtfRoleCombatReason,
			  botFrameCommandStatus.teamFireAvoidanceEvaluations,
			  botFrameCommandStatus.teamFireAvoidanceBlocks,
			  botFrameCommandStatus.teamFireAvoidanceTargetBlocks,
			  botFrameCommandStatus.teamFireAvoidanceLineBlocks,
			  botFrameCommandStatus.teamFireAvoidanceClears,
			  botFrameCommandStatus.teamFireAvoidanceInvalidSkips,
			  botFrameCommandStatus.lastTeamFireAvoidanceClient,
			  botFrameCommandStatus.lastTeamFireAvoidanceTargetClient,
			  botFrameCommandStatus.lastTeamFireAvoidanceFriendlyLine,
			  botFrameCommandStatus.lastTeamFireAvoidanceTargetAllowed,
			  botFrameCommandStatus.lastTeamFireAvoidanceBlocked,
			  botFrameCommandStatus.lastTeamFireAvoidanceReason);

	BotBrain_PrintStatusFmt(
		"q3a_bot_frame_command_status "
			  "pass={} frames={} commands={} route_commands={} "
			  "route_failures={} route_invalid_slots={} "
			  "item_goal_assignments={} last_item_goal_area={} "
			  "stuck_detections={} stuck_repath_refreshes={} "
			  "stuck_recovery_activations={} recovery_command_uses={} "
			  "expected_min_frames={} expected_min_commands={} "
			  "item_goal_peak_active_reservations={} "
			  "route_debug_missing_frames={} skipped_inactive={}\n",
			  pass,
			  botFrameCommandStatus.frames,
			  botFrameCommandStatus.commands,
			  botFrameCommandStatus.routeCommands,
			  routeStatus.failures,
			  routeStatus.invalidSlots,
			  routeStatus.itemGoalAssignments,
			  routeStatus.lastItemGoalArea,
			  routeStatus.stuckDetections,
			  routeStatus.stuckRepathRefreshes,
			  routeStatus.stuckRecoveryActivations,
			  botFrameCommandStatus.recoveryCommandUses,
			  expectedMinFrames,
			  expectedMinCommands,
			  routeStatus.itemGoalPeakActiveReservations,
			  routeStatus.debugOverlayMissingFrames,
			  botFrameCommandStatus.skippedInactive);

	BotBrain_PrintStatusFmt(
		"q3a_bot_coop_command_status "
			  "coop_leader_route_activations={} "
			  "coop_leader_route_refreshes={} "
			  "coop_leader_route_owner_deferrals={} "
			  "coop_leader_route_toward_sources={} "
			  "coop_leader_route_spacing_sources={} "
			  "coop_leader_route_invalid_skips={} "
			  "last_coop_leader_route_client={} "
			  "last_coop_leader_route_leader_client={} "
			  "last_coop_leader_route_intent={} "
			  "last_coop_leader_route_intent_name={} "
			  "last_coop_leader_route_leader_distance_sq={} "
			  "coop_lead_advance_requests={} "
			  "coop_lead_advance_policy_leads={} "
			  "coop_lead_advance_activations={} "
			  "coop_lead_advance_refreshes={} "
			  "coop_lead_advance_route_requests={} "
			  "coop_lead_advance_owner_deferrals={} "
			  "coop_lead_advance_route_deferrals={} "
			  "coop_lead_advance_expirations={} "
			  "coop_lead_advance_invalid_skips={} "
			  "last_coop_lead_advance_client={} "
			  "last_coop_lead_advance_intent={} "
			  "last_coop_lead_advance_intent_name={} "
			  "last_coop_lead_advance_remaining_ms={} "
			  "last_coop_lead_advance_goal_distance_sq={} "
			  "coop_progress_wait_requests={} "
			  "coop_progress_wait_policy_waits={} "
			  "coop_progress_wait_commands={} "
			  "coop_progress_wait_invalid_skips={} "
			  "last_coop_progress_wait_client={} "
			  "last_coop_progress_wait_leader_client={} "
			  "last_coop_progress_wait_intent={} "
			  "last_coop_progress_wait_intent_name={} "
			  "last_coop_progress_wait_leader_distance_sq={} "
			  "coop_anti_block_requests={} "
			  "coop_anti_block_policy_close={} "
			  "coop_anti_block_commands={} "
			  "coop_anti_block_invalid_skips={} "
			  "last_coop_anti_block_client={} "
			  "last_coop_anti_block_leader_client={} "
			  "last_coop_anti_block_intent={} "
			  "last_coop_anti_block_intent_name={} "
			  "last_coop_anti_block_leader_distance_sq={} "
			  "last_coop_anti_block_forward_move={} "
			  "last_coop_anti_block_side_move={} "
			  "coop_target_share_requests={} "
			  "coop_target_share_policy_supports={} "
			  "coop_target_share_source_scans={} "
			  "coop_target_share_source_candidates={} "
			  "coop_target_share_adoptions={} "
			  "coop_target_share_invalid_skips={} "
			  "last_coop_target_share_client={} "
			  "last_coop_target_share_source_client={} "
			  "last_coop_target_share_target_entity={} "
			  "last_coop_target_share_target_client={} "
			  "last_coop_target_share_target_distance_sq={} "
			  "last_coop_target_share_intent={} "
			  "last_coop_target_share_intent_name={} "
			  "coop_door_elevator_requests={} "
			  "coop_door_elevator_source_activations={} "
			  "coop_door_elevator_source_commands={} "
			  "coop_door_elevator_hold_commands={} "
			  "coop_door_elevator_invalid_skips={} "
			  "last_coop_door_elevator_client={} "
			  "last_coop_door_elevator_source_client={} "
			  "last_coop_door_elevator_action={} "
			  "last_coop_door_elevator_kind={} "
			  "last_coop_door_elevator_entity={} "
			  "last_coop_door_elevator_intent={} "
			  "last_coop_door_elevator_intent_name={} "
			  "coop_interaction_retry_requests={} "
			  "coop_interaction_retry_activations={} "
			  "coop_interaction_retry_commands={} "
			  "coop_interaction_retry_invalid_skips={} "
			  "last_coop_interaction_retry_client={} "
			  "last_coop_interaction_retry_action={} "
			  "last_coop_interaction_retry_kind={} "
			  "last_coop_interaction_retry_entity={}\n",
			  botFrameCommandStatus.coopLeaderRouteActivations,
			  botFrameCommandStatus.coopLeaderRouteRefreshes,
			  botFrameCommandStatus.coopLeaderRouteOwnerDeferrals,
			  botFrameCommandStatus.coopLeaderRouteTowardSources,
			  botFrameCommandStatus.coopLeaderRouteSpacingSources,
			  botFrameCommandStatus.coopLeaderRouteInvalidSkips,
			  botFrameCommandStatus.lastCoopLeaderRouteClient,
			  botFrameCommandStatus.lastCoopLeaderRouteLeaderClient,
			  botFrameCommandStatus.lastCoopLeaderRouteIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopLeaderRouteIntent)),
			  botFrameCommandStatus.lastCoopLeaderRouteLeaderDistanceSquared,
			  botFrameCommandStatus.coopLeadAdvanceRequests,
			  botFrameCommandStatus.coopLeadAdvancePolicyLeads,
			  botFrameCommandStatus.coopLeadAdvanceActivations,
			  botFrameCommandStatus.coopLeadAdvanceRefreshes,
			  botFrameCommandStatus.coopLeadAdvanceRouteRequests,
			  botFrameCommandStatus.coopLeadAdvanceOwnerDeferrals,
			  botFrameCommandStatus.coopLeadAdvanceRouteDeferrals,
			  botFrameCommandStatus.coopLeadAdvanceExpirations,
			  botFrameCommandStatus.coopLeadAdvanceInvalidSkips,
			  botFrameCommandStatus.lastCoopLeadAdvanceClient,
			  botFrameCommandStatus.lastCoopLeadAdvanceIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopLeadAdvanceIntent)),
			  botFrameCommandStatus.lastCoopLeadAdvanceRemainingMilliseconds,
			  botFrameCommandStatus.lastCoopLeadAdvanceGoalDistanceSquared,
			  botFrameCommandStatus.coopProgressWaitRequests,
			  botFrameCommandStatus.coopProgressWaitPolicyWaits,
			  botFrameCommandStatus.coopProgressWaitCommands,
			  botFrameCommandStatus.coopProgressWaitInvalidSkips,
			  botFrameCommandStatus.lastCoopProgressWaitClient,
			  botFrameCommandStatus.lastCoopProgressWaitLeaderClient,
			  botFrameCommandStatus.lastCoopProgressWaitIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopProgressWaitIntent)),
			  botFrameCommandStatus.lastCoopProgressWaitLeaderDistanceSquared,
			  botFrameCommandStatus.coopAntiBlockRequests,
			  botFrameCommandStatus.coopAntiBlockPolicyClose,
			  botFrameCommandStatus.coopAntiBlockCommands,
			  botFrameCommandStatus.coopAntiBlockInvalidSkips,
			  botFrameCommandStatus.lastCoopAntiBlockClient,
			  botFrameCommandStatus.lastCoopAntiBlockLeaderClient,
			  botFrameCommandStatus.lastCoopAntiBlockIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
			  botFrameCommandStatus.lastCoopAntiBlockIntent)),
			  botFrameCommandStatus.lastCoopAntiBlockLeaderDistanceSquared,
			  botFrameCommandStatus.lastCoopAntiBlockForwardMove,
			  botFrameCommandStatus.lastCoopAntiBlockSideMove,
			  botFrameCommandStatus.coopTargetShareRequests,
			  botFrameCommandStatus.coopTargetSharePolicySupports,
			  botFrameCommandStatus.coopTargetShareSourceScans,
			  botFrameCommandStatus.coopTargetShareSourceCandidates,
			  botFrameCommandStatus.coopTargetShareAdoptions,
			  botFrameCommandStatus.coopTargetShareInvalidSkips,
			  botFrameCommandStatus.lastCoopTargetShareClient,
			  botFrameCommandStatus.lastCoopTargetShareSourceClient,
			  botFrameCommandStatus.lastCoopTargetShareTargetEntity,
			  botFrameCommandStatus.lastCoopTargetShareTargetClient,
			  botFrameCommandStatus.lastCoopTargetShareTargetDistanceSquared,
			  botFrameCommandStatus.lastCoopTargetShareIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopTargetShareIntent)),
			  botFrameCommandStatus.coopDoorElevatorRequests,
			  botFrameCommandStatus.coopDoorElevatorSourceActivations,
			  botFrameCommandStatus.coopDoorElevatorSourceCommands,
			  botFrameCommandStatus.coopDoorElevatorHoldCommands,
			  botFrameCommandStatus.coopDoorElevatorInvalidSkips,
			  botFrameCommandStatus.lastCoopDoorElevatorClient,
			  botFrameCommandStatus.lastCoopDoorElevatorSourceClient,
			  botFrameCommandStatus.lastCoopDoorElevatorAction,
			  botFrameCommandStatus.lastCoopDoorElevatorKind,
			  botFrameCommandStatus.lastCoopDoorElevatorEntity,
			  botFrameCommandStatus.lastCoopDoorElevatorIntent,
			  BotObjectives_CoopIntentName(static_cast<BotObjectiveCoopIntent>(
				  botFrameCommandStatus.lastCoopDoorElevatorIntent)),
			  botFrameCommandStatus.coopInteractionRetryRequests,
			  botFrameCommandStatus.coopInteractionRetryActivations,
			  botFrameCommandStatus.coopInteractionRetryCommands,
			  botFrameCommandStatus.coopInteractionRetryInvalidSkips,
			  botFrameCommandStatus.lastCoopInteractionRetryClient,
			  botFrameCommandStatus.lastCoopInteractionRetryAction,
			  botFrameCommandStatus.lastCoopInteractionRetryKind,
			  botFrameCommandStatus.lastCoopInteractionRetryEntity);

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
			  "last_team_objective_lane={} "
			  "last_team_objective_target_source={} "
			  "team_objective_role_policy_requested_honored={} "
			  "team_objective_role_policy_attack_selections={} "
			  "team_objective_role_policy_lane_midfield_selections={} "
			  "team_objective_role_policy_carrier_support_selections={} "
			  "team_objective_role_policy_dropped_flag_responses={} "
			  "team_objective_role_policy_own_base_return_selections={} "
			  "team_objective_enemy_flag_assignments={}\n",
			  objectiveStatus.lastObjectiveLane,
			  objectiveStatus.lastTargetSource,
			  objectiveStatus.rolePolicyRequestedHonored,
			  objectiveStatus.rolePolicyAttackSelections,
			  objectiveStatus.rolePolicyLaneMidfieldSelections,
			  objectiveStatus.rolePolicyCarrierSupportSelections,
			  objectiveStatus.rolePolicyDroppedFlagResponses,
			  objectiveStatus.rolePolicyOwnBaseReturnSelections,
			  objectiveStatus.enemyFlagAssignments);

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
			  "team_objective_flag_drops={} "
			  "team_objective_flag_returns={} "
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
			  objectiveStatus.flagDrops,
			  objectiveStatus.flagReturns,
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
			  "combat_enemy_memory_retains={} combat_enemy_memory_decays={} "
			  "combat_enemy_memory_smoke_occlusions={} "
			  "combat_enemy_memory_smoke_seed_attempts={} "
			  "combat_enemy_memory_smoke_seeded={} "
			  "combat_enemy_memory_smoke_seed_no_peer={} "
			  "combat_enemy_memory_smoke_seed_invalid_peer={} "
			  "combat_enemy_memory_smoke_seed_no_blackboard={} "
			  "combat_enemy_visible={} combat_enemy_shootable={} "
			  "last_combat_enemy_entity={} last_combat_enemy_client={} "
			  "last_combat_enemy_visible={} last_combat_enemy_shootable={} "
			  "last_combat_enemy_distance_sq={} "
			  "last_combat_enemy_retained_from_memory={} "
			  "last_combat_enemy_memory_age_ms={} "
			  "last_combat_enemy_memory_window_ms={} "
			  "last_combat_enemy_memory_decay_entity={} "
			  "last_combat_enemy_memory_decay_client={} "
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
			  botBrainBlackboardStatus.combatEnemyMemoryRetains,
			  botBrainBlackboardStatus.combatEnemyMemoryDecays,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeOcclusions,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedAttempts,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeeded,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoPeer,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedInvalidPeer,
			  botBrainBlackboardStatus.combatEnemyMemorySmokeSeedNoBlackboard,
			  botBrainBlackboardStatus.combatEnemyVisible,
			  botBrainBlackboardStatus.combatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyEntity,
			  botBrainBlackboardStatus.lastCombatEnemyClient,
			  botBrainBlackboardStatus.lastCombatEnemyVisible,
			  botBrainBlackboardStatus.lastCombatEnemyShootable,
			  botBrainBlackboardStatus.lastCombatEnemyDistanceSquared,
			  botBrainBlackboardStatus.lastCombatEnemyRetainedFromMemory,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryAgeMilliseconds,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryWindowMilliseconds,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryDecayEntity,
			  botBrainBlackboardStatus.lastCombatEnemyMemoryDecayClient,
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
		"q3a_bot_nav_policy_status "
			  "ctf_item_role_evaluations={} "
			  "ctf_item_role_selections={} "
			  "ctf_item_role_score_boosts={} "
			  "ctf_item_role_selected_goals={} "
			  "ctf_item_role_invalid_skips={}\n",
			  routeStatus.ctfItemRoleEvaluations,
			  routeStatus.ctfItemRoleSelections,
			  routeStatus.ctfItemRoleScoreBoosts,
			  routeStatus.ctfItemRoleSelectedGoals,
			  routeStatus.ctfItemRoleInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ctf_item_role_client={} "
			  "last_ctf_item_role_mode={} "
			  "last_ctf_item_role_mode_name={} "
			  "last_ctf_item_role_role={} "
			  "last_ctf_item_role_role_name={}\n",
			  routeStatus.lastCtfItemRoleClient,
			  routeStatus.lastCtfItemRoleMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  routeStatus.lastCtfItemRoleMode)),
			  routeStatus.lastCtfItemRoleRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  routeStatus.lastCtfItemRoleRole)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ctf_item_role_lane={} "
			  "last_ctf_item_role_lane_name={} "
			  "last_ctf_item_role_category={} "
			  "last_ctf_item_role_category_name={}\n",
			  routeStatus.lastCtfItemRoleLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  routeStatus.lastCtfItemRoleLane)),
			  routeStatus.lastCtfItemRoleCategory,
			  BotObjectives_ItemCategoryName(static_cast<BotObjectiveItemCategory>(
				  routeStatus.lastCtfItemRoleCategory)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ctf_item_role_item_role={} "
			  "last_ctf_item_role_item_role_name={} "
			  "last_ctf_item_role_priority={} "
			  "last_ctf_item_role_score_boost={} "
			  "last_ctf_item_role_profile_item_bonus={}\n",
			  routeStatus.lastCtfItemRoleItemRole,
			  BotObjectives_ItemRoleName(static_cast<BotObjectiveItemRole>(
				  routeStatus.lastCtfItemRoleItemRole)),
			  routeStatus.lastCtfItemRolePriority,
			  routeStatus.lastCtfItemRoleScoreBoost,
			  routeStatus.lastCtfItemRoleProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ctf_item_role_entity={} "
			  "last_ctf_item_role_item={} "
			  "last_ctf_item_role_score={}\n",
			  routeStatus.lastCtfItemRoleEntity,
			  routeStatus.lastCtfItemRoleItem,
			  routeStatus.lastCtfItemRoleScore);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "ffa_item_role_evaluations={} "
			  "ffa_item_role_selections={} "
			  "ffa_item_role_score_boosts={} "
			  "ffa_item_role_selected_goals={} "
			  "ffa_item_role_invalid_skips={}\n",
			  routeStatus.ffaItemRoleEvaluations,
			  routeStatus.ffaItemRoleSelections,
			  routeStatus.ffaItemRoleScoreBoosts,
			  routeStatus.ffaItemRoleSelectedGoals,
			  routeStatus.ffaItemRoleInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ffa_item_role_client={} "
			  "last_ffa_item_role_mode={} "
			  "last_ffa_item_role_mode_name={} "
			  "last_ffa_item_role_role={} "
			  "last_ffa_item_role_role_name={}\n",
			  routeStatus.lastFfaItemRoleClient,
			  routeStatus.lastFfaItemRoleMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  routeStatus.lastFfaItemRoleMode)),
			  routeStatus.lastFfaItemRoleRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  routeStatus.lastFfaItemRoleRole)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ffa_item_role_lane={} "
			  "last_ffa_item_role_lane_name={} "
			  "last_ffa_item_role_category={} "
			  "last_ffa_item_role_category_name={}\n",
			  routeStatus.lastFfaItemRoleLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  routeStatus.lastFfaItemRoleLane)),
			  routeStatus.lastFfaItemRoleCategory,
			  BotObjectives_ItemCategoryName(static_cast<BotObjectiveItemCategory>(
				  routeStatus.lastFfaItemRoleCategory)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ffa_item_role_item_role={} "
			  "last_ffa_item_role_item_role_name={} "
			  "last_ffa_item_role_priority={} "
			  "last_ffa_item_role_score_boost={} "
			  "last_ffa_item_role_profile_item_bonus={}\n",
			  routeStatus.lastFfaItemRoleItemRole,
			  BotObjectives_ItemRoleName(static_cast<BotObjectiveItemRole>(
				  routeStatus.lastFfaItemRoleItemRole)),
			  routeStatus.lastFfaItemRolePriority,
			  routeStatus.lastFfaItemRoleScoreBoost,
			  routeStatus.lastFfaItemRoleProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_ffa_item_role_entity={} "
			  "last_ffa_item_role_item={} "
			  "last_ffa_item_role_score={}\n",
			  routeStatus.lastFfaItemRoleEntity,
			  routeStatus.lastFfaItemRoleItem,
			  routeStatus.lastFfaItemRoleScore);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "team_item_role_evaluations={} "
			  "team_item_role_selections={} "
			  "team_item_role_score_boosts={} "
			  "team_item_role_selected_goals={} "
			  "team_item_role_invalid_skips={}\n",
			  routeStatus.teamItemRoleEvaluations,
			  routeStatus.teamItemRoleSelections,
			  routeStatus.teamItemRoleScoreBoosts,
			  routeStatus.teamItemRoleSelectedGoals,
			  routeStatus.teamItemRoleInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_item_role_client={} "
			  "last_team_item_role_mode={} "
			  "last_team_item_role_mode_name={} "
			  "last_team_item_role_role={} "
			  "last_team_item_role_role_name={}\n",
			  routeStatus.lastTeamItemRoleClient,
			  routeStatus.lastTeamItemRoleMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  routeStatus.lastTeamItemRoleMode)),
			  routeStatus.lastTeamItemRoleRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  routeStatus.lastTeamItemRoleRole)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_item_role_lane={} "
			  "last_team_item_role_lane_name={} "
			  "last_team_item_role_category={} "
			  "last_team_item_role_category_name={}\n",
			  routeStatus.lastTeamItemRoleLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  routeStatus.lastTeamItemRoleLane)),
			  routeStatus.lastTeamItemRoleCategory,
			  BotObjectives_ItemCategoryName(static_cast<BotObjectiveItemCategory>(
				  routeStatus.lastTeamItemRoleCategory)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_item_role_item_role={} "
			  "last_team_item_role_item_role_name={} "
			  "last_team_item_role_priority={} "
			  "last_team_item_role_score_boost={} "
			  "last_team_item_role_profile_item_bonus={}\n",
			  routeStatus.lastTeamItemRoleItemRole,
			  BotObjectives_ItemRoleName(static_cast<BotObjectiveItemRole>(
				  routeStatus.lastTeamItemRoleItemRole)),
			  routeStatus.lastTeamItemRolePriority,
			  routeStatus.lastTeamItemRoleScoreBoost,
			  routeStatus.lastTeamItemRoleProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_item_role_entity={} "
			  "last_team_item_role_item={} "
			  "last_team_item_role_score={}\n",
			  routeStatus.lastTeamItemRoleEntity,
			  routeStatus.lastTeamItemRoleItem,
			  routeStatus.lastTeamItemRoleScore);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "team_resource_denial_evaluations={} "
			  "team_resource_denial_policy_denies={} "
			  "team_resource_denial_score_boosts={} "
			  "team_resource_denial_selected_goals={} "
			  "team_resource_denial_invalid_skips={}\n",
			  routeStatus.teamResourceDenialEvaluations,
			  routeStatus.teamResourceDenialPolicyDenies,
			  routeStatus.teamResourceDenialScoreBoosts,
			  routeStatus.teamResourceDenialSelectedGoals,
			  routeStatus.teamResourceDenialInvalidSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_resource_denial_client={} "
			  "last_team_resource_denial_mode={} "
			  "last_team_resource_denial_mode_name={} "
			  "last_team_resource_denial_role={} "
			  "last_team_resource_denial_role_name={}\n",
			  routeStatus.lastTeamResourceDenialClient,
			  routeStatus.lastTeamResourceDenialMode,
			  BotObjectives_MatchModeName(static_cast<BotObjectiveMatchMode>(
				  routeStatus.lastTeamResourceDenialMode)),
			  routeStatus.lastTeamResourceDenialRole,
			  BotObjectives_RoleName(static_cast<BotObjectiveRole>(
				  routeStatus.lastTeamResourceDenialRole)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_resource_denial_lane={} "
			  "last_team_resource_denial_lane_name={} "
			  "last_team_resource_denial_category={} "
			  "last_team_resource_denial_category_name={}\n",
			  routeStatus.lastTeamResourceDenialLane,
			  BotObjectives_LaneName(static_cast<BotObjectiveLane>(
				  routeStatus.lastTeamResourceDenialLane)),
			  routeStatus.lastTeamResourceDenialCategory,
			  BotObjectives_ItemCategoryName(static_cast<BotObjectiveItemCategory>(
				  routeStatus.lastTeamResourceDenialCategory)));
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_resource_denial_intent={} "
			  "last_team_resource_denial_intent_name={} "
			  "last_team_resource_denial_priority={} "
			  "last_team_resource_denial_score_boost={} "
			  "last_team_resource_denial_profile_item_bonus={}\n",
			  routeStatus.lastTeamResourceDenialIntent,
			  BotObjectives_ResourceIntentName(static_cast<BotObjectiveResourceIntent>(
				  routeStatus.lastTeamResourceDenialIntent)),
			  routeStatus.lastTeamResourceDenialPriority,
			  routeStatus.lastTeamResourceDenialScoreBoost,
			  routeStatus.lastTeamResourceDenialProfileItemBonus);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_team_resource_denial_entity={} "
			  "last_team_resource_denial_item={} "
			  "last_team_resource_denial_score={}\n",
			  routeStatus.lastTeamResourceDenialEntity,
			  routeStatus.lastTeamResourceDenialItem,
			  routeStatus.lastTeamResourceDenialScore);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "nav_interaction_elevator_activations={} "
			  "nav_interaction_activations={} "
			  "nav_interaction_candidates={} nav_interaction_checks={} "
			  "last_nav_interaction_kind={} "
			  "last_nav_interaction_entity={}\n",
			  routeStatus.interactionElevatorActivations,
			  routeStatus.interactionActivations,
			  routeStatus.interactionCandidates,
			  routeStatus.interactionChecks,
			  routeStatus.lastInteractionKind,
			  routeStatus.lastInteractionEntity);

	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "travel_type_goal_support_checks={} travel_type_goal_supported={} "
			  "travel_type_goal_unsupported={} "
			  "last_travel_type_goal_support_type={} "
			  "last_travel_type_goal_support_area={} "
			  "last_travel_type_goal_support_goal_area={}\n",
			  routeStatus.travelTypeGoalSupportChecks,
			  routeStatus.travelTypeGoalSupported,
			  routeStatus.travelTypeGoalUnsupported,
			  routeStatus.lastTravelTypeGoalSupportType,
			  routeStatus.lastTravelTypeGoalSupportArea,
			  routeStatus.lastTravelTypeGoalSupportGoalArea);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "nav_interaction_checks={} nav_interaction_candidates={} "
			  "nav_interaction_activations={} "
			  "nav_interaction_stuck_activations={} "
			  "nav_interaction_elevator_activations={} "
			  "nav_interaction_wait_frames={} "
			  "nav_interaction_use_frames={} "
			  "nav_interaction_misses={}\n",
			  routeStatus.interactionChecks,
			  routeStatus.interactionCandidates,
			  routeStatus.interactionActivations,
			  routeStatus.interactionStuckActivations,
			  routeStatus.interactionElevatorActivations,
			  routeStatus.interactionWaitFrames,
			  routeStatus.interactionUseFrames,
			  routeStatus.interactionMisses);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "last_nav_interaction_action={} "
			  "last_nav_interaction_kind={} "
			  "last_nav_interaction_entity={} "
			  "last_nav_interaction_distance_sq={} "
			  "last_nav_interaction_travel_type={} "
			  "last_nav_interaction_move_state={} "
			  "last_nav_interaction_frames_remaining={}\n",
			  routeStatus.lastInteractionAction,
			  routeStatus.lastInteractionKind,
			  routeStatus.lastInteractionEntity,
			  routeStatus.lastInteractionDistanceSq,
			  routeStatus.lastInteractionTravelType,
			  routeStatus.lastInteractionMoveState,
			  routeStatus.lastInteractionFramesRemaining);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "movement_state_waterjump_commands={} "
			  "natural_movement_state_commands={} "
			  "natural_movement_state_crouch_commands={} "
			  "natural_movement_state_swim_commands={} "
			  "natural_movement_state_waterjump_commands={}\n",
			  botFrameCommandStatus.movementStateWaterJumpCommands,
			  botFrameCommandStatus.naturalMovementStateCommands,
			  botFrameCommandStatus.naturalMovementStateCrouchCommands,
			  botFrameCommandStatus.naturalMovementStateSwimCommands,
			  botFrameCommandStatus.naturalMovementStateWaterJumpCommands);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "interaction_wait_command_uses={} "
			  "interaction_use_command_uses={} "
			  "last_interaction_command_action={} "
			  "last_interaction_command_entity={}\n",
			  botFrameCommandStatus.interactionWaitCommandUses,
			  botFrameCommandStatus.interactionUseCommandUses,
			  botFrameCommandStatus.lastInteractionCommandAction,
			  botFrameCommandStatus.lastInteractionCommandEntity);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "route_corner_cut_candidates={} "
			  "route_corner_cut_trace_checks={} "
			  "route_corner_cut_trace_hits={} "
			  "route_corner_cut_trace_misses={} "
			  "route_corner_cut_ground_trace_checks={} "
			  "route_corner_cut_ground_trace_misses={} "
			  "route_corner_cut_accepted={} "
			  "route_corner_cut_rejected={}\n",
			  routeStatus.cornerCutChecks,
			  routeStatus.cornerCutTraceAttempts,
			  routeStatus.cornerCutTracePasses,
			  routeStatus.cornerCutTraceFailures,
			  routeStatus.cornerCutGroundTraceAttempts,
			  routeStatus.cornerCutGroundTraceFailures,
			  routeStatus.cornerCutApplications,
			  routeStatus.cornerCutSkips);
	BotBrain_PrintStatusFmt(
		"q3a_bot_nav_policy_status "
			  "corner_cut_candidates={} corner_cut_trace_checks={} "
			  "corner_cut_accepted={} "
			  "trace_checked_corner_cut_candidates={} "
			  "trace_checked_corner_cut_trace_checks={} "
			  "trace_checked_corner_cut_accepted={}\n",
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
			  "action_inventory_policy_scans={} "
			  "action_inventory_policy_candidates={} "
			  "action_inventory_policy_usable_candidates={} "
			  "action_inventory_policy_selections={} "
			  "action_inventory_policy_combat_uses={} "
			  "action_inventory_policy_survival_uses={} "
			  "action_inventory_policy_utility_uses={} "
			  "action_inventory_policy_environment_uses={} "
			  "action_inventory_policy_deployable_uses={} "
			  "action_inventory_policy_escape_uses={} "
			  "action_inventory_policy_placement_checks={} "
			  "action_inventory_policy_placement_deferrals={} "
			  "action_inventory_policy_power_armor_uses={} "
			  "action_inventory_policy_nuke_deferrals={} "
			  "action_inventory_policy_nuke_safety_checks={} "
			  "action_inventory_policy_nuke_friendly_deferrals={} "
			  "action_inventory_policy_nuke_self_deferrals={} "
			  "action_inventory_policy_nuke_uses={} "
			  "action_inventory_policy_existing_request_deferrals={} "
			  "action_inventory_policy_active_deferrals={} "
			  "action_inventory_policy_owned_sphere_deferrals={} "
			  "action_inventory_policy_no_cells_skips={} "
			  "action_inventory_policy_no_candidate_skips={} "
			  "action_inventory_policy_no_usable_skips={} "
			  "last_action_inventory_policy_candidates={} "
			  "last_action_inventory_policy_usable_candidates={} "
			  "last_action_inventory_policy_item={} "
			  "last_action_inventory_policy_score={} "
			  "last_action_inventory_policy_priority={} "
			  "last_action_inventory_policy_special_kind={} "
			  "last_action_inventory_policy_special_kind_name={} "
			  "last_action_inventory_policy_reason={} "
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
			  "item_focus_ammo_boosts={} "
			  "item_health_goal_assignments={} item_armor_goal_assignments={} "
			  "item_ammo_goal_assignments={} item_weapon_goal_assignments={} "
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
			  actionStatus.inventoryPolicyScans,
			  actionStatus.inventoryPolicyCandidates,
			  actionStatus.inventoryPolicyUsableCandidates,
			  actionStatus.inventoryPolicySelections,
			  actionStatus.inventoryPolicyCombatUses,
			  actionStatus.inventoryPolicySurvivalUses,
			  actionStatus.inventoryPolicyUtilityUses,
			  actionStatus.inventoryPolicyEnvironmentUses,
			  actionStatus.inventoryPolicyDeployableUses,
			  actionStatus.inventoryPolicyEscapeUses,
			  actionStatus.inventoryPolicyPlacementChecks,
			  actionStatus.inventoryPolicyPlacementDeferrals,
			  actionStatus.inventoryPolicyPowerArmorUses,
			  actionStatus.inventoryPolicyNukeDeferrals,
			  actionStatus.inventoryPolicyNukeSafetyChecks,
			  actionStatus.inventoryPolicyNukeFriendlyDeferrals,
			  actionStatus.inventoryPolicyNukeSelfDeferrals,
			  actionStatus.inventoryPolicyNukeUses,
			  actionStatus.inventoryPolicyExistingRequestDeferrals,
			  actionStatus.inventoryPolicyActiveDeferrals,
			  actionStatus.inventoryPolicyOwnedSphereDeferrals,
			  actionStatus.inventoryPolicyNoCellsSkips,
			  actionStatus.inventoryPolicyNoCandidateSkips,
			  actionStatus.inventoryPolicyNoUsableSkips,
			  actionStatus.lastInventoryPolicyCandidateCount,
			  actionStatus.lastInventoryPolicyUsableCount,
			  actionStatus.lastInventoryPolicyItem,
			  actionStatus.lastInventoryPolicyScore,
			  actionStatus.lastInventoryPolicyPriority,
			  static_cast<int>(actionStatus.lastInventoryPolicySpecialKind),
			  BotItems_SpecialKindName(actionStatus.lastInventoryPolicySpecialKind),
			  actionStatus.lastInventoryPolicyReason,
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
			  itemStatus.focusAmmoBoosts,
			  itemStatus.itemHealthGoalAssignments,
			  itemStatus.itemArmorGoalAssignments,
			  itemStatus.itemAmmoGoalAssignments,
			  itemStatus.itemWeaponGoalAssignments,
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
			  static_cast<int>(BotBrain_AimPolicyFailureForStatus(combatStatus)),
			  BotCombat_AimPolicyFailureName(BotBrain_AimPolicyFailureForStatus(combatStatus)),
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
