#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Any


STATUS_MARKER = "q3a_bot_frame_command_status"
SCENARIO_BEGIN_MARKER = "q3a_bot_frame_command_smoke_scenario=begin"
ACTION_STATUS_MARKER = "q3a_bot_action_status"
ACTION_DETAIL_STATUS_MARKER = "q3a_bot_action_detail_status"
BLACKBOARD_STATUS_MARKER = "q3a_bot_blackboard_status"
OBJECTIVE_STATUS_MARKER = "q3a_bot_objective_status"
OBJECTIVE_DETAIL_STATUS_MARKER = "q3a_bot_objective_detail_status"
COOP_COMMAND_STATUS_MARKER = "q3a_bot_coop_command_status"
NAV_POLICY_STATUS_MARKER = "q3a_bot_nav_policy_status"
BEHAVIOR_POLICY_STATUS_MARKER = "q3a_bot_behavior_policy_status"
NAV_NATURAL_SUPPORT_STATUS_MARKER = "q3a_bot_nav_natural_support_status"
NAV_INTERACTION_CONTEXT_STATUS_MARKER = "q3a_bot_nav_interaction_context_status"
SOURCE_STATUS_MARKER = "q3a_bot_source_counter_status"
TEAM_POLICY_STATUS_MARKER = "q3a_bot_team_policy_status"
WARMUP_STATUS_MARKER = "q3a_bot_warmup_status"
VOTE_STATUS_MARKER = "q3a_bot_vote_status"
ADMIN_AUDIT_STATUS_MARKER = "q3a_bot_admin_audit_status"
TOURNAMENT_STATUS_MARKER = "q3a_bot_tournament_status"
MAPVOTE_STATUS_MARKER = "q3a_bot_mapvote_status"
MYMAP_STATUS_MARKER = "q3a_bot_mymap_status"
SCOREBOARD_STATUS_MARKER = "q3a_bot_scoreboard_status"
INTERMISSION_STATUS_MARKER = "q3a_bot_intermission_status"
NEXTMAP_STATUS_MARKER = "q3a_bot_nextmap_status"
CHAT_POLICY_STATUS_MARKER = "q3a_bot_chat_policy_status"
MATCH_READINESS_STATUS_MARKER = "q3a_bot_match_readiness_status"
COOP_READINESS_STATUS_MARKER = "q3a_bot_coop_readiness_status"
MATCH_LOGGING_SCHEMA_MARKER = "q3a_match_logging_schema"
MATCH_LOGGING_CATALOG_MARKER = "q3a_match_logging_catalog"
SOAK_BEGIN_MARKER = "q3a_bot_frame_command_smoke_soak=begin"
SOAK_COMPLETE_MARKER = "q3a_bot_frame_command_smoke_soak=complete"
RAW_RESERVED_METRIC_MARKERS = (
    STATUS_MARKER,
    BLACKBOARD_STATUS_MARKER,
    ACTION_STATUS_MARKER,
    ACTION_DETAIL_STATUS_MARKER,
    OBJECTIVE_STATUS_MARKER,
    OBJECTIVE_DETAIL_STATUS_MARKER,
    COOP_COMMAND_STATUS_MARKER,
    NAV_POLICY_STATUS_MARKER,
    BEHAVIOR_POLICY_STATUS_MARKER,
    NAV_NATURAL_SUPPORT_STATUS_MARKER,
    NAV_INTERACTION_CONTEXT_STATUS_MARKER,
    SOURCE_STATUS_MARKER,
)
RAW_RESERVED_OPTIONAL_ONLY_MARKERS = (
    TEAM_POLICY_STATUS_MARKER,
    WARMUP_STATUS_MARKER,
    VOTE_STATUS_MARKER,
    ADMIN_AUDIT_STATUS_MARKER,
    TOURNAMENT_STATUS_MARKER,
    MAPVOTE_STATUS_MARKER,
    MYMAP_STATUS_MARKER,
    SCOREBOARD_STATUS_MARKER,
    INTERMISSION_STATUS_MARKER,
    NEXTMAP_STATUS_MARKER,
    CHAT_POLICY_STATUS_MARKER,
    MATCH_READINESS_STATUS_MARKER,
    COOP_READINESS_STATUS_MARKER,
    MATCH_LOGGING_SCHEMA_MARKER,
    MATCH_LOGGING_CATALOG_MARKER,
)
RAW_RESERVED_MODE_MARKERS = (
    SCENARIO_BEGIN_MARKER,
    *RAW_RESERVED_METRIC_MARKERS,
    *RAW_RESERVED_OPTIONAL_ONLY_MARKERS,
)
RAW_RESERVED_METRIC_SOURCE_HINTS = {
    "pass": (STATUS_MARKER,),
    "route_failures": (STATUS_MARKER,),
    "combat_fire_decisions": (ACTION_STATUS_MARKER,),
    "combat_weapon_switch_decisions": (ACTION_STATUS_MARKER,),
    "combat_damage_events": (ACTION_STATUS_MARKER,),
    "last_combat_damage": (ACTION_STATUS_MARKER,),
    "action_attack_decisions": (ACTION_STATUS_MARKER,),
    "action_applied_attack_buttons": (ACTION_STATUS_MARKER,),
    "action_weapon_switch_decisions": (ACTION_STATUS_MARKER,),
    "action_pending_weapon_switches": (ACTION_STATUS_MARKER,),
    "action_weapon_inventory_selections": (ACTION_STATUS_MARKER,),
    "action_inventory_policy_selections": (ACTION_STATUS_MARKER,),
    "weapon_switch_requests": (ACTION_STATUS_MARKER,),
    "weapon_switch_completions": (ACTION_STATUS_MARKER,),
    "weapon_switch_failures": (ACTION_STATUS_MARKER,),
    "weapon_switch_expected_item": (ACTION_STATUS_MARKER,),
    "weapon_switch_actual_item": (ACTION_STATUS_MARKER,),
    "weapon_switch_expected_match": (ACTION_STATUS_MARKER,),
    "item_low_health_boosts": (ACTION_STATUS_MARKER,),
    "item_low_armor_boosts": (ACTION_STATUS_MARKER,),
    "item_health_goal_assignments": (ACTION_STATUS_MARKER,),
    "item_armor_goal_assignments": (ACTION_STATUS_MARKER,),
    "item_ammo_goal_assignments": (ACTION_STATUS_MARKER,),
    "item_weapon_goal_assignments": (ACTION_STATUS_MARKER,),
    "item_focus_ammo_boosts": (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER),
    "item_health_pickups": (ACTION_STATUS_MARKER,),
    "item_armor_pickups": (ACTION_STATUS_MARKER,),
    "last_health_pickup_delta": (ACTION_STATUS_MARKER,),
    "last_armor_pickup_delta": (ACTION_STATUS_MARKER,),
}
RAW_RESERVED_METRIC_PREFIX_SOURCE_HINTS = (
    ("team_objective_", (OBJECTIVE_STATUS_MARKER, OBJECTIVE_DETAIL_STATUS_MARKER)),
    ("last_team_objective_", (OBJECTIVE_STATUS_MARKER, OBJECTIVE_DETAIL_STATUS_MARKER)),
    ("aim_policy_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER, BLACKBOARD_STATUS_MARKER)),
    ("last_aim_policy_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER, BLACKBOARD_STATUS_MARKER)),
    ("live_aim_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("last_live_aim_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("action_inventory_policy_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("last_action_inventory_policy_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("projectile_lead_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("last_projectile_lead_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("item_timer_", (ACTION_DETAIL_STATUS_MARKER,)),
    ("last_item_timer_", (ACTION_DETAIL_STATUS_MARKER,)),
    ("item_timing_policy_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("item_last_timing_policy_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("item_timing_consumer_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("item_last_timing_consumer_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("route_corner_cut_", (STATUS_MARKER, NAV_POLICY_STATUS_MARKER)),
    ("corner_cut_", (NAV_POLICY_STATUS_MARKER,)),
    ("trace_checked_corner_", (NAV_POLICY_STATUS_MARKER,)),
    ("combat_enemy_", (ACTION_STATUS_MARKER, BLACKBOARD_STATUS_MARKER, STATUS_MARKER)),
    ("combat_weapon_selection_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("last_combat_enemy_", (ACTION_STATUS_MARKER, BLACKBOARD_STATUS_MARKER, STATUS_MARKER)),
    ("last_combat_estimate_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("last_combat_weapon_estimate_", (ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER)),
    ("coop_leader_route_", (STATUS_MARKER, COOP_COMMAND_STATUS_MARKER)),
    ("last_coop_leader_route_", (STATUS_MARKER, COOP_COMMAND_STATUS_MARKER)),
    ("coop_lead_advance_", (COOP_COMMAND_STATUS_MARKER,)),
    ("last_coop_lead_advance_", (COOP_COMMAND_STATUS_MARKER,)),
    ("coop_progress_wait_", (COOP_COMMAND_STATUS_MARKER,)),
    ("last_coop_progress_wait_", (COOP_COMMAND_STATUS_MARKER,)),
    ("coop_anti_block_", (COOP_COMMAND_STATUS_MARKER,)),
    ("last_coop_anti_block_", (COOP_COMMAND_STATUS_MARKER,)),
    ("coop_target_share_", (COOP_COMMAND_STATUS_MARKER,)),
    ("last_coop_target_share_", (COOP_COMMAND_STATUS_MARKER,)),
    ("coop_door_elevator_", (COOP_COMMAND_STATUS_MARKER,)),
    ("last_coop_door_elevator_", (COOP_COMMAND_STATUS_MARKER,)),
    ("coop_interaction_retry_", (COOP_COMMAND_STATUS_MARKER,)),
    ("last_coop_interaction_retry_", (COOP_COMMAND_STATUS_MARKER,)),
    ("ffa_roam_route_", (STATUS_MARKER,)),
    ("last_ffa_roam_route_", (STATUS_MARKER,)),
    ("ffa_spawn_camp_avoidance_", (STATUS_MARKER,)),
    ("last_ffa_spawn_camp_avoidance_", (STATUS_MARKER,)),
    ("ffa_spawn_camp_combat_avoidance_", (STATUS_MARKER,)),
    ("last_ffa_spawn_camp_combat_avoidance_", (STATUS_MARKER,)),
    ("ffa_item_role_", (NAV_POLICY_STATUS_MARKER,)),
    ("last_ffa_item_role_", (NAV_POLICY_STATUS_MARKER,)),
    ("ffa_role_combat_", (STATUS_MARKER,)),
    ("last_ffa_role_combat_", (STATUS_MARKER,)),
    ("team_role_route_", (STATUS_MARKER,)),
    ("last_team_role_route_", (STATUS_MARKER,)),
    ("team_role_combat_", (STATUS_MARKER,)),
    ("last_team_role_combat_", (STATUS_MARKER,)),
    ("ctf_role_route_", (STATUS_MARKER,)),
    ("last_ctf_role_route_", (STATUS_MARKER,)),
    ("ctf_role_combat_", (STATUS_MARKER,)),
    ("last_ctf_role_combat_", (STATUS_MARKER,)),
    ("ctf_dropped_flag_route_", (STATUS_MARKER,)),
    ("last_ctf_dropped_flag_route_", (STATUS_MARKER,)),
    ("ctf_carrier_support_route_", (STATUS_MARKER,)),
    ("last_ctf_carrier_support_route_", (STATUS_MARKER,)),
    ("ctf_base_return_route_", (STATUS_MARKER,)),
    ("last_ctf_base_return_route_", (STATUS_MARKER,)),
    ("ctf_objective_route_", (STATUS_MARKER,)),
    ("last_ctf_objective_route_", (STATUS_MARKER,)),
    ("ctf_item_role_", (NAV_POLICY_STATUS_MARKER,)),
    ("last_ctf_item_role_", (NAV_POLICY_STATUS_MARKER,)),
    ("team_fire_avoidance_", (STATUS_MARKER,)),
    ("last_team_fire_avoidance_", (STATUS_MARKER,)),
    ("threat_retreat_", (STATUS_MARKER,)),
    ("last_threat_retreat_", (STATUS_MARKER,)),
    ("team_item_role_", (NAV_POLICY_STATUS_MARKER,)),
    ("last_team_item_role_", (NAV_POLICY_STATUS_MARKER,)),
    ("team_resource_denial_", (NAV_POLICY_STATUS_MARKER,)),
    ("last_team_resource_denial_", (NAV_POLICY_STATUS_MARKER,)),
    ("behavior_", (BEHAVIOR_POLICY_STATUS_MARKER,)),
    ("q3a_", (SOURCE_STATUS_MARKER,)),
    ("bsp_", (SOURCE_STATUS_MARKER,)),
)
RESERVED_MODE_SCENARIOS = {
    20: "engage_enemy",
    21: "switch_weapons",
    22: "health_armor_pickup",
    23: "team_objective",
    24: "aim_fairness_policy_integration",
    25: "item_timer_fairness_signals",
    26: "ffa_tdm_match_readiness",
    27: "coop_lead_advance",
    28: "coop_resource_share",
    29: "coop_anti_blocking",
    30: "coop_target_share",
    31: "coop_door_elevator",
    32: "team_role_route",
    33: "team_item_roles",
    34: "team_fire_avoidance",
    35: "ctf_role_route",
    36: "ctf_role_combat",
    37: "ctf_dropped_flag_route",
    38: "ctf_carrier_support_route",
    39: "ctf_base_return_route",
    40: "ctf_objective_route",
    41: "ctf_objective_route_precedence",
    42: "ffa_roam_route",
    43: "team_role_combat",
    44: "team_role_combat_avoidance",
    45: "ffa_spawn_camp_avoidance",
    46: "ffa_item_roles",
    47: "ctf_item_roles",
    48: "ffa_role_combat",
    49: "ffa_spawn_camp_combat_avoidance",
    50: "team_resource_denial",
    51: "match_item_policy",
    52: "behavior_policy_umbrella",
    53: "profile_role_policy",
    54: "profile_team_policy",
    55: "profile_item_policy",
    56: "profile_movement_policy",
    57: "bot_chat_policy",
    58: "bot_chat_team_policy",
    59: "bot_chat_rate_policy",
    60: "bot_chat_initial_policy",
    61: "bot_chat_reply_policy",
    62: "bot_chat_event_policy",
    63: "behavior_arbitration",
    64: "target_memory_decay",
    65: "weapon_scoring_arsenal",
    66: "aim_fire_policy_depth",
    67: "ammo_pressure_pickup",
    68: "survival_inventory_use",
    69: "survival_health_route",
    70: "survival_armor_route",
    71: "combat_survival_regression",
    72: "threat_retreat_avoidance",
    73: "tdm_role_spawn_stability",
    74: "ffa_live_pacing",
    75: "duel_live_pacing",
    76: "ctf_objective_transitions",
    77: "coop_live_loop",
    78: "coop_share_loop",
    79: "bot_chat_live_events",
    80: "bot_chat_live_event_cooldown",
    81: "bot_chat_live_enemy_sighted",
    82: "bot_chat_phrase_library",
    83: "bot_chat_duplicate_suppression",
    84: "bot_chat_live_low_health",
    85: "bot_chat_live_item_taken",
    86: "bot_chat_live_objective_changed",
}
ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC = "item_timing_consumer_ready_or_live"
PROMOTION_RELATED_METRIC_PREFIXES = {
    "health_armor_pickup": (
        "item_goal_",
        "last_item_goal_",
        "last_failed_goal_",
    ),
}
KEY_VALUE_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)=(-?\d+)\b")
MARKER_FIELD_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
INTEGER_RE = re.compile(r"-?\d+")
FLOAT_RE = re.compile(r"-?(?:\d+\.\d+|\d+\.|\.\d+)")
STATUS_TOKEN_RE = re.compile(rf"(?:^|\s){re.escape(STATUS_MARKER)}(?:\s|$)")
FORBIDDEN_PATTERNS = (
    "commandMsec underflow",
)
KEY_METRICS = (
    "expected_min_commands",
    "elapsed_ms",
    "reports",
    "frames",
    "commands",
    "route_commands",
    "route_failures",
    "route_invalid_slots",
    "route_debug_missing_frames",
    "stuck_detections",
    "recovery_command_uses",
    "item_goal_assignments",
    "item_goal_active_reservations",
    "item_goal_peak_active_reservations",
    "skipped_inactive",
    "cycles",
    "map_changes",
    "final_count",
    "duration_seconds",
    "pass",
)


@dataclass(frozen=True)
class MetricCheck:
    metric: str
    op: str
    expected: int
    note: str = ""


@dataclass(frozen=True)
class MarkerMetricCheck:
    marker: str
    metric: str
    op: str
    expected: int | float | str
    note: str = ""


@dataclass(frozen=True)
class DegradationPolicy:
    name: str
    tier: str
    bot_count: int
    budget_profile: str
    preserved_behavior: tuple[str, ...]
    allowed_degradation: tuple[str, ...]
    required_metrics: tuple[MetricCheck, ...] = field(default_factory=tuple)
    required_marker_metrics: tuple[MarkerMetricCheck, ...] = field(default_factory=tuple)
    notes: tuple[str, ...] = field(default_factory=tuple)


@dataclass(frozen=True)
class Scenario:
    name: str
    title: str
    smoke_mode: int | None
    description: str
    task_ids: tuple[str, ...]
    budget_seconds: int
    smoke_cvar: str = "sv_bot_frame_command_smoke"
    checks: tuple[MetricCheck, ...] = field(default_factory=tuple)
    marker_checks: tuple[MarkerMetricCheck, ...] = field(default_factory=tuple)
    pending_reason: str = ""
    extra_cvars: tuple[tuple[str, str], ...] = field(default_factory=tuple)
    map_name: str | None = None
    manual_only: bool = False
    selection_tags: tuple[str, ...] = field(default_factory=tuple)
    degradation_policy: DegradationPolicy | None = None
    planned_smoke_mode: int | None = None
    promotion_metrics: tuple[str, ...] = field(default_factory=tuple)
    promotion_marker_metrics: tuple[tuple[str, str], ...] = field(default_factory=tuple)
    promotion_checks: tuple[MetricCheck, ...] = field(default_factory=tuple)
    promotion_marker_checks: tuple[MarkerMetricCheck, ...] = field(default_factory=tuple)

    @property
    def implemented(self) -> bool:
        return self.smoke_mode is not None


@dataclass(frozen=True)
class OptionalFieldFamily:
    name: str
    title: str
    description: str
    markers: tuple[str, ...]
    metric_names: tuple[str, ...] = field(default_factory=tuple)
    metric_prefixes: tuple[str, ...] = field(default_factory=tuple)


def reserved_mode_marker_checks(
    mode: int,
    *,
    combat: int | str,
    weapon_switch: int,
    item_focus: int | str,
    team_objective: int,
    target: int,
    gametype: int,
) -> tuple[MarkerMetricCheck, ...]:
    return (
        MarkerMetricCheck(
            SCENARIO_BEGIN_MARKER,
            "mode",
            "eq",
            mode,
            "reserved smoke mode must match the pending scenario",
        ),
        MarkerMetricCheck(
            SCENARIO_BEGIN_MARKER,
            "combat",
            "eq",
            combat,
            "reserved smoke combat cvar must match the pending scenario",
        ),
        MarkerMetricCheck(
            SCENARIO_BEGIN_MARKER,
            "weapon_switch",
            "eq",
            weapon_switch,
            "reserved smoke weapon-switch cvar must match the pending scenario",
        ),
        MarkerMetricCheck(
            SCENARIO_BEGIN_MARKER,
            "item_focus",
            "eq",
            item_focus,
            "reserved smoke item-focus cvar must match the pending scenario",
        ),
        MarkerMetricCheck(
            SCENARIO_BEGIN_MARKER,
            "team_objective",
            "eq",
            team_objective,
            "reserved smoke team-objective cvar must match the pending scenario",
        ),
        MarkerMetricCheck(
            SCENARIO_BEGIN_MARKER,
            "target",
            "ge",
            target,
            "reserved smoke must request enough bots for the scenario setup",
        ),
        MarkerMetricCheck(
            SCENARIO_BEGIN_MARKER,
            "gametype",
            "eq",
            gametype,
            "reserved smoke gametype flag must match the scenario setup",
        ),
    )


def marker_metric_checks(
    marker: str,
    *checks: MetricCheck,
) -> tuple[MarkerMetricCheck, ...]:
    return tuple(
        MarkerMetricCheck(
            marker,
            check.metric,
            check.op,
            check.expected,
            check.note,
        )
        for check in checks
    )


POLICY_CONSUMER_MARKER_FIELDS: dict[str, tuple[tuple[str, str], ...]] = {
    "aim_fairness_policy_integration": (
        (ACTION_STATUS_MARKER, "live_aim_evaluations"),
        (ACTION_STATUS_MARKER, "live_aim_fire_allowed"),
    ),
    "item_timer_fairness_signals": (
        (ACTION_DETAIL_STATUS_MARKER, "item_timing_consumer_evaluations"),
        (ACTION_DETAIL_STATUS_MARKER, ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC),
    ),
}
POLICY_CONSUMER_FIELD_NOTES = {
    (ACTION_DETAIL_STATUS_MARKER, ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC): (
        "derived from item_timing_consumer_ready or item_timing_consumer_live_pickups"
    ),
}


OPTIONAL_FIELD_FAMILIES: tuple[OptionalFieldFamily, ...] = (
    OptionalFieldFamily(
        name="behavior_arbitration_counters",
        title="Behavior arbitration counters",
        description=(
            "Central bot-brain behavior owner arbitration, candidate counts, "
            "handoffs, and proof-cvar classification emitted by the behavior "
            "policy status marker."
        ),
        markers=(BEHAVIOR_POLICY_STATUS_MARKER,),
        metric_names=(
            "behavior_arbitration",
            "behavior_policy_cvar_audit",
            "behavior_live_policy_cvars",
            "behavior_smoke_policy_cvars",
            "behavior_debug_policy_cvars",
            "behavior_deprecated_policy_cvars",
            "behavior_arbitration_evaluations",
            "behavior_arbitration_handoffs",
            "last_behavior_arbitration_client",
            "last_behavior_arbitration_owner",
            "last_behavior_arbitration_previous_owner",
            "last_behavior_arbitration_priority",
            "last_behavior_arbitration_owner_name",
            "last_behavior_arbitration_reason",
        ),
        metric_prefixes=(
            "behavior_arbitration_",
            "last_behavior_arbitration_",
        ),
    ),
    OptionalFieldFamily(
        name="target_memory_counters",
        title="Target memory counters",
        description=(
            "Combat blackboard counters for retaining a temporarily unseen "
            "enemy from memory and clearing that target after the memory window "
            "decays."
        ),
        markers=(BLACKBOARD_STATUS_MARKER,),
        metric_names=(
            "combat_enemy_memory_retains",
            "combat_enemy_memory_decays",
            "combat_enemy_memory_smoke_occlusions",
            "combat_enemy_memory_smoke_seed_attempts",
            "combat_enemy_memory_smoke_seeded",
            "combat_enemy_memory_smoke_seed_no_peer",
            "combat_enemy_memory_smoke_seed_invalid_peer",
            "combat_enemy_memory_smoke_seed_no_blackboard",
            "last_combat_enemy_retained_from_memory",
            "last_combat_enemy_memory_age_ms",
            "last_combat_enemy_memory_window_ms",
            "last_combat_enemy_memory_decay_entity",
            "last_combat_enemy_memory_decay_client",
        ),
        metric_prefixes=(
            "combat_enemy_memory_",
            "last_combat_enemy_memory_",
        ),
    ),
    OptionalFieldFamily(
        name="action_dispatch_counters",
        title="Action dispatch counters",
        description=(
            "Weapon/inventory command-request build, validation, dispatch, defer, "
            "and failure telemetry emitted by the action layer."
        ),
        markers=(ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER),
        metric_names=(
            "action_pending_inventory_uses",
            "action_use_inventory_decisions",
            "action_applied_use_buttons",
            "action_command_request_builds",
            "action_command_request_accepted",
            "action_command_request_rejected",
            "action_weapon_command_requests",
            "action_weapon_inventory_scans",
            "action_weapon_inventory_candidates",
            "action_weapon_inventory_ready_candidates",
            "action_weapon_inventory_selections",
            "action_weapon_inventory_switch_recommendations",
            "action_weapon_inventory_ammo_skips",
            "action_weapon_inventory_splash_unsafe_skips",
            "action_weapon_inventory_range_selections",
            "action_weapon_inventory_estimate_selections",
            "last_action_weapon_inventory_selected_ammo",
            "last_action_weapon_inventory_score_margin",
            "last_action_weapon_inventory_selected_priority",
            "last_action_weapon_inventory_selected_ammo_per_shot",
            "last_action_weapon_inventory_selected_splash_damage",
            "last_action_weapon_inventory_selected_self_damage_risk",
            "last_action_weapon_inventory_selected_estimate_adjustment",
            "last_action_weapon_inventory_selected_range_band",
            "last_action_weapon_inventory_selected_range_band_name",
            "last_action_weapon_inventory_selected_attack_model",
            "last_action_weapon_inventory_selected_attack_model_name",
            "last_action_weapon_inventory_estimate_reason",
            "action_inventory_command_requests",
            "action_command_request_dispatch_attempts",
            "action_command_request_submitted",
            "action_command_request_deferred",
            "action_command_request_dispatch_failures",
            "action_weapon_command_dispatches",
            "action_inventory_command_dispatches",
            "action_last_command_request_kind",
            "action_last_command_request_failure",
            "action_last_command_dispatch_outcome",
            "action_last_command_dispatch_failure",
            "action_last_command_request_kind_name",
            "action_last_command_request_failure_name",
            "action_last_command_dispatch_outcome_name",
            "action_last_command_dispatch_failure_name",
        ),
        metric_prefixes=(
            "action_command_request_",
            "action_weapon_command_",
            "action_weapon_inventory_",
            "last_action_weapon_inventory_",
            "action_inventory_command_",
            "action_last_command_request_",
            "action_last_command_dispatch_",
        ),
    ),
    OptionalFieldFamily(
        name="inventory_policy_counters",
        title="Inventory policy counters",
        description=(
            "Conservative carried non-weapon inventory/powerup scan, deferral, "
            "selection, priority, and last selected item telemetry emitted by "
            "the action layer."
        ),
        markers=(ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER),
        metric_names=(
            "action_inventory_policy_scans",
            "action_inventory_policy_candidates",
            "action_inventory_policy_usable_candidates",
            "action_inventory_policy_selections",
            "action_inventory_policy_combat_uses",
            "action_inventory_policy_survival_uses",
            "action_inventory_policy_utility_uses",
            "action_inventory_policy_environment_uses",
            "action_inventory_policy_deployable_uses",
            "action_inventory_policy_escape_uses",
            "action_inventory_policy_placement_checks",
            "action_inventory_policy_placement_deferrals",
            "action_inventory_policy_power_armor_uses",
            "action_inventory_policy_nuke_deferrals",
            "action_inventory_policy_nuke_safety_checks",
            "action_inventory_policy_nuke_friendly_deferrals",
            "action_inventory_policy_nuke_self_deferrals",
            "action_inventory_policy_nuke_uses",
            "action_inventory_policy_existing_request_deferrals",
            "action_inventory_policy_active_deferrals",
            "action_inventory_policy_owned_sphere_deferrals",
            "action_inventory_policy_no_cells_skips",
            "action_inventory_policy_no_candidate_skips",
            "action_inventory_policy_no_usable_skips",
            "last_action_inventory_policy_candidates",
            "last_action_inventory_policy_usable_candidates",
            "last_action_inventory_policy_item",
            "last_action_inventory_policy_score",
            "last_action_inventory_policy_priority",
            "last_action_inventory_policy_special_kind",
            "last_action_inventory_policy_special_kind_name",
            "last_action_inventory_policy_reason",
        ),
        metric_prefixes=(
            "action_inventory_policy_",
            "last_action_inventory_policy_",
        ),
    ),
    OptionalFieldFamily(
        name="timed_route_goal_counters",
        title="Timed route goal counters",
        description=(
            "Generic brain-owned timed route-goal activations, route ownership, "
            "deferrals, expiration, invalid skips, and last owner metadata."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "timed_route_goal_activations",
            "timed_route_goal_route_requests",
            "timed_route_goal_route_deferrals",
            "timed_route_goal_expirations",
            "timed_route_goal_invalid_skips",
            "last_timed_route_goal_kind",
            "last_timed_route_goal_kind_name",
            "last_timed_route_goal_client",
            "last_timed_route_goal_remaining_ms",
            "last_timed_route_goal_source_x",
            "last_timed_route_goal_source_y",
            "last_timed_route_goal_source_z",
            "last_timed_route_goal_goal_x",
            "last_timed_route_goal_goal_y",
            "last_timed_route_goal_goal_z",
            "last_timed_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "timed_route_goal_",
            "last_timed_route_goal_",
        ),
    ),
    OptionalFieldFamily(
        name="nuke_retreat_route_counters",
        title="Nuke retreat route counters",
        description=(
            "Command-owned retreat activations, route goal ownership, fallback "
            "source use, expiration, and last source/goal metadata after safe "
            "nuke inventory dispatch."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "nuke_retreat_activations",
            "nuke_retreat_fallback_sources",
            "nuke_retreat_route_requests",
            "nuke_retreat_route_deferrals",
            "nuke_retreat_expirations",
            "nuke_retreat_invalid_skips",
            "last_nuke_retreat_client",
            "last_nuke_retreat_remaining_ms",
            "last_nuke_retreat_source_x",
            "last_nuke_retreat_source_y",
            "last_nuke_retreat_source_z",
            "last_nuke_retreat_goal_x",
            "last_nuke_retreat_goal_y",
            "last_nuke_retreat_goal_z",
            "last_nuke_retreat_distance_sq",
        ),
        metric_prefixes=(
            "nuke_retreat_",
            "last_nuke_retreat_",
        ),
    ),
    OptionalFieldFamily(
        name="teleporter_escape_route_counters",
        title="Teleporter escape route counters",
        description=(
            "Timed route-goal activation and source-selection metadata after "
            "last-resort personal teleporter escape inventory use."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "teleporter_escape_route_activations",
            "teleporter_escape_fallback_sources",
            "teleporter_escape_damage_sources",
            "teleporter_escape_invalid_skips",
        ),
        metric_prefixes=(
            "teleporter_escape_",
        ),
    ),
    OptionalFieldFamily(
        name="threat_retreat_route_counters",
        title="Threat retreat route counters",
        description=(
            "Low-health threat-retreat requests, source selection, timed "
            "route-goal ownership, attack suppression, re-engagement, and "
            "last source/health metadata."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "threat_retreat_requests",
            "threat_retreat_enemy_sources",
            "threat_retreat_damage_sources",
            "threat_retreat_fallback_sources",
            "threat_retreat_activations",
            "threat_retreat_refreshes",
            "threat_retreat_route_requests",
            "threat_retreat_route_deferrals",
            "threat_retreat_expirations",
            "threat_retreat_invalid_skips",
            "threat_retreat_attack_suppressions",
            "threat_retreat_reengages",
            "last_threat_retreat_client",
            "last_threat_retreat_source_client",
            "last_threat_retreat_source_entity",
            "last_threat_retreat_source_distance_sq",
            "last_threat_retreat_remaining_ms",
            "last_threat_retreat_goal_distance_sq",
            "last_threat_retreat_health",
            "last_threat_retreat_armor",
            "last_threat_retreat_low_health",
            "last_threat_retreat_active",
            "last_threat_retreat_reason",
        ),
        metric_prefixes=(
            "threat_retreat_",
            "last_threat_retreat_",
        ),
    ),
    OptionalFieldFamily(
        name="coop_leader_route_counters",
        title="Coop leader route counters",
        description=(
            "Timed route-goal activation and leader-source metadata for "
            "coop follow, regroup, and support-spacing policy consumption."
        ),
        markers=(STATUS_MARKER, COOP_COMMAND_STATUS_MARKER),
        metric_names=(
            "coop_leader_route_activations",
            "coop_leader_route_refreshes",
            "coop_leader_route_owner_deferrals",
            "coop_leader_route_toward_sources",
            "coop_leader_route_spacing_sources",
            "coop_leader_route_invalid_skips",
            "last_coop_leader_route_client",
            "last_coop_leader_route_leader_client",
            "last_coop_leader_route_intent",
            "last_coop_leader_route_intent_name",
            "last_coop_leader_route_leader_distance_sq",
        ),
        metric_prefixes=(
            "coop_leader_route_",
            "last_coop_leader_route_",
        ),
    ),
    OptionalFieldFamily(
        name="coop_progress_wait_counters",
        title="Coop progress wait counters",
        description=(
            "Cvar-gated coop WaitForLeader policy requests, command-owner "
            "applications, and last leader intent metadata."
        ),
        markers=(COOP_COMMAND_STATUS_MARKER,),
        metric_names=(
            "coop_progress_wait_requests",
            "coop_progress_wait_policy_waits",
            "coop_progress_wait_commands",
            "coop_progress_wait_invalid_skips",
            "last_coop_progress_wait_client",
            "last_coop_progress_wait_leader_client",
            "last_coop_progress_wait_intent",
            "last_coop_progress_wait_intent_name",
            "last_coop_progress_wait_leader_distance_sq",
        ),
        metric_prefixes=(
            "coop_progress_wait_",
            "last_coop_progress_wait_",
        ),
    ),
    OptionalFieldFamily(
        name="coop_lead_advance_counters",
        title="Coop lead advance counters",
        description=(
            "Cvar-gated coop LeadAdvance policy requests, timed route-goal "
            "ownership, route requests, deferrals, and last intent metadata."
        ),
        markers=(COOP_COMMAND_STATUS_MARKER,),
        metric_names=(
            "coop_lead_advance_requests",
            "coop_lead_advance_policy_leads",
            "coop_lead_advance_activations",
            "coop_lead_advance_refreshes",
            "coop_lead_advance_route_requests",
            "coop_lead_advance_owner_deferrals",
            "coop_lead_advance_route_deferrals",
            "coop_lead_advance_expirations",
            "coop_lead_advance_invalid_skips",
            "last_coop_lead_advance_client",
            "last_coop_lead_advance_intent",
            "last_coop_lead_advance_intent_name",
            "last_coop_lead_advance_remaining_ms",
            "last_coop_lead_advance_goal_distance_sq",
        ),
        metric_prefixes=(
            "coop_lead_advance_",
            "last_coop_lead_advance_",
        ),
    ),
    OptionalFieldFamily(
        name="coop_anti_block_counters",
        title="Coop anti-block counters",
        description=(
            "Cvar-gated coop close-to-leader anti-blocking requests, "
            "command-owner applications, and last leader intent metadata."
        ),
        markers=(COOP_COMMAND_STATUS_MARKER,),
        metric_names=(
            "coop_anti_block_requests",
            "coop_anti_block_policy_close",
            "coop_anti_block_commands",
            "coop_anti_block_invalid_skips",
            "last_coop_anti_block_client",
            "last_coop_anti_block_leader_client",
            "last_coop_anti_block_intent",
            "last_coop_anti_block_intent_name",
            "last_coop_anti_block_leader_distance_sq",
            "last_coop_anti_block_forward_move",
            "last_coop_anti_block_side_move",
        ),
        metric_prefixes=(
            "coop_anti_block_",
            "last_coop_anti_block_",
        ),
    ),
    OptionalFieldFamily(
        name="coop_target_share_counters",
        title="Coop target share counters",
        description=(
            "Cvar-gated coop target sharing requests, source scans, "
            "blackboard adoptions, and last shared target/source metadata."
        ),
        markers=(COOP_COMMAND_STATUS_MARKER,),
        metric_names=(
            "coop_target_share_requests",
            "coop_target_share_policy_supports",
            "coop_target_share_source_scans",
            "coop_target_share_source_candidates",
            "coop_target_share_adoptions",
            "coop_target_share_invalid_skips",
            "last_coop_target_share_client",
            "last_coop_target_share_source_client",
            "last_coop_target_share_target_entity",
            "last_coop_target_share_target_client",
            "last_coop_target_share_target_distance_sq",
            "last_coop_target_share_intent",
            "last_coop_target_share_intent_name",
        ),
        metric_prefixes=(
            "coop_target_share_",
            "last_coop_target_share_",
        ),
    ),
    OptionalFieldFamily(
        name="coop_door_elevator_counters",
        title="Coop door/elevator counters",
        description=(
            "Cvar-gated coop door/elevator cooperation requests, source "
            "interaction ownership, teammate hold commands, and last "
            "interaction metadata."
        ),
        markers=(COOP_COMMAND_STATUS_MARKER,),
        metric_names=(
            "coop_door_elevator_requests",
            "coop_door_elevator_source_activations",
            "coop_door_elevator_source_commands",
            "coop_door_elevator_hold_commands",
            "coop_door_elevator_invalid_skips",
            "last_coop_door_elevator_client",
            "last_coop_door_elevator_source_client",
            "last_coop_door_elevator_action",
            "last_coop_door_elevator_kind",
            "last_coop_door_elevator_entity",
            "last_coop_door_elevator_intent",
            "last_coop_door_elevator_intent_name",
        ),
        metric_prefixes=(
            "coop_door_elevator_",
            "last_coop_door_elevator_",
        ),
    ),
    OptionalFieldFamily(
        name="coop_interaction_retry_counters",
        title="Coop interaction retry counters",
        description=(
            "Cvar-gated coop route interaction retry requests, activations, "
            "command-owner applications, and last detected interaction metadata."
        ),
        markers=(COOP_COMMAND_STATUS_MARKER,),
        metric_names=(
            "coop_interaction_retry_requests",
            "coop_interaction_retry_activations",
            "coop_interaction_retry_commands",
            "coop_interaction_retry_invalid_skips",
            "last_coop_interaction_retry_client",
            "last_coop_interaction_retry_action",
            "last_coop_interaction_retry_kind",
            "last_coop_interaction_retry_entity",
        ),
        metric_prefixes=(
            "coop_interaction_retry_",
            "last_coop_interaction_retry_",
        ),
    ),
    OptionalFieldFamily(
        name="live_combat_firing_counters",
        title="Live combat/firing counters",
        description=(
            "Enemy acquisition, combat evaluation, firing, attack-button, "
            "withheld-fire, and damage proof counters for live combat smokes."
        ),
        markers=(STATUS_MARKER, ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER, BLACKBOARD_STATUS_MARKER),
        metric_names=(
            "combat_evaluations",
            "combat_no_enemy",
            "combat_enemy_acquisitions",
            "combat_enemy_visible",
            "combat_enemy_shootable",
            "combat_blocked_sight",
            "combat_fire_decisions",
            "combat_withheld_fire",
            "combat_damage_events",
            "last_combat_damage",
            "last_combat_enemy_client",
            "action_attack_decisions",
            "action_applied_cmds",
            "action_applied_attack_buttons",
            "action_last_intent",
            "action_last_intent_name",
        ),
        metric_prefixes=(
            "combat_enemy_",
            "combat_weapon_selection_",
            "last_combat_enemy_",
            "last_combat_estimate_",
            "last_combat_weapon_estimate_",
            "combat_last_",
        ),
    ),
    OptionalFieldFamily(
        name="aim_policy_counters",
        title="Aim-policy counters",
        description=(
            "Fairness/aim policy evaluation, allowance, block-bucket, live-aim "
            "consumer, projectile lead, and last policy metadata when combat "
            "status owners expose it."
        ),
        markers=(STATUS_MARKER, ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER, BLACKBOARD_STATUS_MARKER),
        metric_names=(
            "aim_policy_evaluations",
            "aim_policy_aim_allowed",
            "aim_policy_fire_allowed",
            "aim_policy_blocks_no_enemy",
            "aim_policy_blocks_visibility",
            "aim_policy_blocks_field_of_view",
            "aim_policy_blocks_shootability",
            "aim_policy_blocks_weapon_ready",
            "aim_policy_blocks_skill",
            "aim_policy_blocks_burst_cooldown",
            "aim_policy_blocks_reaction",
            "aim_policy_blocks_turn",
            "aim_policy_blocks_aim_settle",
            "aim_policy_blocks_burst_limit",
            "last_aim_policy_failure",
            "last_aim_policy_failure_name",
            "last_aim_policy_skill",
            "last_aim_policy_reaction_delay_ms",
            "last_aim_policy_aim_settle_ms",
            "last_aim_policy_visible_ms",
            "last_aim_policy_tracked_ms",
            "last_aim_policy_fov_degrees",
            "last_aim_policy_yaw_delta_degrees",
            "last_aim_policy_pitch_delta_degrees",
            "last_aim_policy_max_turn_degrees",
            "last_aim_policy_aim_error_tenths_degrees",
            "last_aim_policy_tracking_noise_tenths_degrees",
            "last_aim_policy_burst_shot_limit",
            "last_aim_policy_burst_cooldown_ms",
            "projectile_lead_evaluations",
            "projectile_lead_uses",
            "projectile_lead_no_projectile",
            "projectile_lead_no_speed",
            "projectile_lead_invalid_distance",
            "last_projectile_lead_weapon",
            "last_projectile_lead_speed",
            "last_projectile_lead_ms",
            "last_projectile_lead_target_speed_sq",
            "last_projectile_lead_aim_distance_sq",
            "last_projectile_lead_offset_sq",
            "live_aim_evaluations",
            "live_aim_aim_allowed",
            "live_aim_fire_allowed",
            "live_aim_policy_blocks",
            "live_aim_projectile_lead_uses",
            "last_live_aim_weapon",
            "last_live_aim_reason",
        ),
        metric_prefixes=(
            "aim_policy_",
            "combat_aim_policy_",
            "last_aim_policy_",
            "combat_last_aim_policy_",
            "projectile_lead_",
            "last_projectile_lead_",
            "live_aim_",
            "last_live_aim_",
        ),
    ),
    OptionalFieldFamily(
        name="special_item_utility_buckets",
        title="Special item utility buckets",
        description=(
            "Special pickup candidate, seek-decision, boost, and last-kind "
            "telemetry for powerups, techs, mobility, protection, and objectives."
        ),
        markers=(STATUS_MARKER, ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER),
        metric_names=(
            "item_special_utility_boosts",
            "item_high_value_boosts",
            "item_focus_health_boosts",
            "item_focus_armor_boosts",
            "item_focus_ammo_boosts",
            "item_ammo_goal_assignments",
            "item_weapon_goal_assignments",
            "item_last_utility_kind",
            "item_last_utility_kind_name",
            "item_last_special_kind",
            "item_last_special_kind_name",
        ),
        metric_prefixes=(
            "item_damage_boost_",
            "item_protection_",
            "item_invisibility_",
            "item_mobility_",
            "item_utility_powerup_",
            "item_tech_",
            "item_ctf_objective_",
            "item_special_",
            "last_item_special_",
        ),
    ),
    OptionalFieldFamily(
        name="item_timer_fairness_signals",
        title="Item timer fairness signals",
        description=(
            "Optional item-timer observation, allowance, fuzzing, cooldown, "
            "live timing-consumer, and last-timer metadata for fair pickup "
            "timing policy."
        ),
        markers=(STATUS_MARKER, ACTION_STATUS_MARKER, ACTION_DETAIL_STATUS_MARKER),
        metric_names=(
            "item_timer_evaluations",
            "item_timer_known_pickups",
            "item_timer_unknown_pickups",
            "item_timer_allowed_uses",
            "item_timer_blocked_uses",
            "item_timer_fairness_blocks",
            "item_timer_fuzzed_offsets",
            "item_timer_cooldown_blocks",
            "last_item_timer_item",
            "last_item_timer_entity",
            "last_item_timer_known_ms",
            "last_item_timer_fuzz_ms",
            "last_item_timer_allowed",
            "last_item_timer_reason",
            "item_timing_policy_evaluations",
            "item_timing_policy_invalid",
            "item_timing_policy_timers_disabled",
            "item_timing_policy_unobserved_blocks",
            "item_timing_policy_exact_uses",
            "item_timing_policy_fuzzed_uses",
            "item_timing_policy_ready",
            "item_timing_policy_waiting",
            "item_last_timing_policy_reason",
            "item_last_timing_policy_reason_name",
            "item_last_timing_policy_fuzz_ms",
            "item_last_timing_policy_remaining_ms",
            "item_timing_consumer_evaluations",
            "item_timing_consumer_invalid",
            "item_timing_consumer_live_pickups",
            "item_timing_consumer_ready",
            "item_timing_consumer_waiting",
            "item_timing_consumer_fairness_blocks",
            "item_timing_consumer_selection_deferrals",
            ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC,
            "item_last_timing_consumer_reason",
            "item_last_timing_consumer_reason_name",
            "item_last_timing_consumer_policy_reason",
            "item_last_timing_consumer_policy_reason_name",
            "item_last_timing_consumer_fuzz_ms",
            "item_last_timing_consumer_remaining_ms",
        ),
        metric_prefixes=(
            "item_timer_",
            "last_item_timer_",
            "item_timing_policy_",
            "item_last_timing_policy_",
            "item_timing_consumer_",
            "item_last_timing_consumer_",
        ),
    ),
    OptionalFieldFamily(
        name="route_target_stabilization_counters",
        title="Route-target stabilization counters",
        description=(
            "Route target stabilization checks, applications, skips, and last "
            "sample metadata emitted by frame-command route status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "route_target_stabilization_checks",
            "route_target_stabilizations",
            "route_target_stabilization_skips",
            "last_route_target_original_distance_sq",
            "last_route_target_stable_distance_sq",
            "last_route_target_stable_point_index",
        ),
        metric_prefixes=(
            "route_target_stabilization_",
            "last_route_target_",
        ),
    ),
    OptionalFieldFamily(
        name="trace_checked_corner_cutting_signals",
        title="Trace-checked corner-cutting signals",
        description=(
            "Route-corner candidate, trace-check, acceptance, rejection, and "
            "last-corner metadata emitted by trace-checked corner cutting."
        ),
        markers=(STATUS_MARKER, NAV_POLICY_STATUS_MARKER, SOURCE_STATUS_MARKER),
        metric_names=(
            "route_corner_cut_candidates",
            "route_corner_cut_trace_checks",
            "route_corner_cut_trace_hits",
            "route_corner_cut_trace_misses",
            "route_corner_cut_accepted",
            "route_corner_cut_rejected",
            "last_route_corner_cut_index",
            "last_route_corner_cut_distance_sq",
            "last_route_corner_cut_clearance",
            "corner_cut_candidates",
            "corner_cut_trace_checks",
            "corner_cut_accepted",
            "trace_checked_corner_cut_candidates",
            "trace_checked_corner_cut_trace_checks",
            "trace_checked_corner_cut_accepted",
        ),
        metric_prefixes=(
            "route_corner_cut_",
            "last_route_corner_cut_",
            "corner_cut_",
            "trace_checked_corner_",
        ),
    ),
    OptionalFieldFamily(
        name="team_mode_readiness_signals",
        title="Team-mode readiness signals",
        description=(
            "Team policy, CTF objective role/lane, and blackboard team-role "
            "signals used to stage FFA/TDM/CTF/coop readiness validation."
        ),
        markers=(
            OBJECTIVE_STATUS_MARKER,
            OBJECTIVE_DETAIL_STATUS_MARKER,
            BLACKBOARD_STATUS_MARKER,
            TEAM_POLICY_STATUS_MARKER,
            WARMUP_STATUS_MARKER,
            MATCH_READINESS_STATUS_MARKER,
            COOP_READINESS_STATUS_MARKER,
        ),
        metric_names=(
            "ffa_pass",
            "tdm_pass",
            "pass",
            "bots",
            "playing",
            "spectators",
            "queued",
            "humans",
            "bot_playing",
            "human_playing",
            "ready_humans",
            "ready_bots",
            "minplayers",
            "minplayers_met",
            "warmup_enabled",
            "ready_up",
            "start_no_humans",
            "bot_only_start",
            "no_players_ready",
            "ready_percentage",
            "required_ready_percentage",
            "can_start",
            "match_state",
            "match_state_name",
            "warmup_state",
            "warmup_state_name",
            "free",
            "red",
            "blue",
            "deathmatch",
            "team_mode",
            "coop",
            "gametype",
            "expected_playing",
            "expected_spectators",
            "expected_bots",
            "expected_queued",
            "team_objective_role_policy_evaluations",
            "team_objective_role_policy_selections",
            "team_objective_role_policy_requested",
            "team_objective_role_policy_requested_honored",
            "team_objective_role_policy_fallbacks",
            "team_objective_role_policy_no_selection",
            "team_objective_role_policy_attack_selections",
            "team_objective_role_policy_defend_selections",
            "team_objective_role_policy_return_selections",
            "team_objective_role_policy_support_selections",
            "team_objective_role_policy_lane_attack_selections",
            "team_objective_role_policy_lane_defense_selections",
            "team_objective_role_policy_lane_midfield_selections",
            "team_objective_role_policy_carrier_support_selections",
            "team_objective_role_policy_dropped_flag_responses",
            "team_objective_role_policy_own_base_return_selections",
            "last_team_objective_role",
            "last_team_objective_lane",
            "last_team_objective_lane_name",
            "last_team_role",
            "last_team_role_objective",
            "last_team_role_team",
            "last_team_role_target_team",
        ),
        metric_prefixes=(
            "team_objective_role_",
            "team_objective_role_policy_",
            "last_team_objective_",
            "last_team_role_",
        ),
    ),
    OptionalFieldFamily(
        name="vote_match_flow_signals",
        title="Vote match-flow signals",
        description=(
            "Bot vote-population, active vote, and bot-origin launch rejection "
            "status emitted by the match-flow vote smoke."
        ),
        markers=(VOTE_STATUS_MARKER,),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "voting_clients",
            "active_vote",
            "vote_open",
            "execute_pending",
            "caller_bot",
            "caller_human",
            "vote_yes",
            "vote_no",
            "bot_yes",
            "bot_no",
            "human_yes",
            "human_no",
            "allow_voting",
            "allow_spec_vote",
            "vote_flags",
            "last_launch_attempted",
            "last_launch_bot_found",
            "last_launch_client",
            "last_launch_success",
            "last_launch_blocked",
            "last_launch_reason",
        ),
        metric_prefixes=(
            "vote_",
            "bot_",
            "human_",
            "last_launch_",
        ),
    ),
    OptionalFieldFamily(
        name="admin_audit_match_flow_signals",
        title="Admin audit match-flow signals",
        description=(
            "Bot admin-session, command permission, and team-lock status "
            "emitted by the match-flow admin audit smoke."
        ),
        markers=(ADMIN_AUDIT_STATUS_MARKER,),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "admin_bots",
            "admin_humans",
            "red_locked",
            "blue_locked",
            "allow_admin",
            "last_attempted",
            "last_bot_found",
            "last_client",
            "last_forced_admin",
            "last_admin_session",
            "last_command",
            "last_command_found",
            "last_admin_only",
            "last_allowed",
            "last_executed",
            "last_blocked",
            "last_reason",
            "last_red_locked_before",
            "last_red_locked_after",
        ),
        metric_prefixes=(
            "admin_",
            "last_",
        ),
    ),
    OptionalFieldFamily(
        name="tournament_match_flow_signals",
        title="Tournament match-flow signals",
        description=(
            "Tournament veto setup, bot veto exclusion, pick/ban counts, "
            "replay rewind/reset state, and cleanup status emitted by the "
            "match-flow tournament smoke."
        ),
        markers=(
            TOURNAMENT_STATUS_MARKER,
            "q3a_bot_tournament_replay_setup",
            "q3a_bot_tournament_replay",
        ),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "active",
            "veto_started",
            "veto_complete",
            "home_turn",
            "team_based",
            "pool",
            "picks",
            "bans",
            "order",
            "picks_needed",
            "games_played",
            "series_complete",
            "match_winners",
            "match_ids",
            "match_maps",
            "player0_wins",
            "player1_wins",
            "change_map_set",
            "change_map",
            "home_id",
            "away_id",
            "first_map",
            "last_setup_attempted",
            "last_setup_configured",
            "last_setup_bot_is_home",
            "last_veto_attempted",
            "last_veto_bot_found",
            "last_veto_client",
            "last_veto_map",
            "last_veto_allowed",
            "last_veto_blocked",
            "last_veto_reason",
            "last_veto_picks_before",
            "last_veto_picks_after",
            "last_veto_bans_before",
            "last_veto_bans_after",
            "last_replay_setup_attempted",
            "last_replay_setup_configured",
            "last_replay_setup_order",
            "last_replay_setup_history",
            "last_replay_attempted",
            "last_replay_game",
            "last_replay_success",
            "last_replay_rejected",
            "last_replay_reason",
            "last_replay_target_map",
            "last_replay_games_before",
            "last_replay_games_after",
            "last_replay_winners_before",
            "last_replay_winners_after",
            "last_replay_series_complete_before",
            "last_replay_series_complete_after",
            "last_replay_preserved",
            "last_replay_reset_applied",
            "configured",
            "history",
            "game",
            "success",
            "rejected",
            "reason",
            "target_map",
            "games_before",
            "games_after",
            "winners_before",
            "winners_after",
            "ids_before",
            "ids_after",
            "maps_before",
            "maps_after",
            "player0_wins_before",
            "player0_wins_after",
            "player1_wins_before",
            "player1_wins_after",
            "series_complete_before",
            "series_complete_after",
            "preserved",
            "reset_applied",
        ),
        metric_prefixes=(
            "veto_",
            "last_setup_",
            "last_veto_",
            "last_replay_",
            "replay_",
        ),
    ),
    OptionalFieldFamily(
        name="match_logging_schema_signals",
        title="Match logging schema signals",
        description=(
            "Match-stats and tournament-series schema/version metadata "
            "emitted by the match logging schema smoke."
        ),
        markers=(MATCH_LOGGING_SCHEMA_MARKER,),
        metric_names=(
            "attempted",
            "match_schema_name",
            "match_schema_version",
            "match_artifact_type",
            "match_artifact_version",
            "match_has_players_array",
            "match_has_event_log_array",
            "series_schema_name",
            "series_schema_version",
            "series_artifact_type",
            "series_artifact_version",
            "series_has_matches_array",
            "series_match_schema_version",
            "pass",
        ),
        metric_prefixes=(
            "match_",
            "series_",
        ),
    ),
    OptionalFieldFamily(
        name="match_logging_catalog_signals",
        title="Match logging catalog signals",
        description=(
            "Match catalog schema/version metadata, latest-artifact pointers, "
            "and indexed artifact paths emitted by the match logging smoke."
        ),
        markers=(MATCH_LOGGING_CATALOG_MARKER,),
        metric_names=(
            "attempted",
            "catalog_schema_name",
            "catalog_schema_version",
            "catalog_artifact_type",
            "catalog_artifact_version",
            "catalog_artifact_count",
            "latest_match_stats",
            "latest_tournament_series",
            "first_artifact_type",
            "first_json_path",
            "second_artifact_type",
            "second_json_path",
            "catalog_write_pass",
            "catalog_write_artifact_count",
            "pass",
        ),
        metric_prefixes=(
            "catalog_",
            "latest_",
            "first_",
            "second_",
        ),
    ),
    OptionalFieldFamily(
        name="mapvote_match_flow_signals",
        title="Map-vote match-flow signals",
        description=(
            "Map selector activity, candidate, bot-vote exclusion, finalize, "
            "and transition status emitted by the match-flow map-vote smoke."
        ),
        markers=(MAPVOTE_STATUS_MARKER,),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "connected_clients",
            "active",
            "force_exit",
            "post_intermission",
            "exit_requested",
            "candidates",
            "candidate0",
            "candidate1",
            "candidate2",
            "vote_count0",
            "vote_count1",
            "vote_count2",
            "total_counted_votes",
            "bot_votes",
            "human_votes",
            "change_map_set",
            "change_map",
            "current_map",
            "last_bot_vote_attempted",
            "last_bot_vote_found",
            "last_bot_vote_client",
            "last_bot_vote_index",
            "last_bot_vote_active",
            "last_bot_vote_blocked",
            "last_bot_vote_counted",
            "last_bot_vote_stored",
            "last_bot_vote_reason",
            "last_finalize_attempted",
            "last_finalize_success",
            "last_finalize_reason",
            "last_finalize_target_map",
            "last_finalize_current_map",
            "last_finalize_selected_index",
            "last_finalize_selected_votes",
            "last_finalize_candidates",
            "last_finalize_exit_requested",
            "last_finalize_change_map_set",
        ),
        metric_prefixes=(
            "candidate",
            "vote_count",
            "last_bot_vote_",
            "last_finalize_",
        ),
    ),
    OptionalFieldFamily(
        name="mymap_match_flow_signals",
        title="MyMap match-flow signals",
        description=(
            "Bot-attributed MyMap queue request, play/mymap queue counts, "
            "and queued-map consumption status emitted by the match-flow "
            "MyMap smoke."
        ),
        markers=(MYMAP_STATUS_MARKER,),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "play_queue",
            "mymap_queue",
            "allow_mymap",
            "maps_mymap",
            "queue_limit",
            "front_map",
            "front_social",
            "front_enable_flags",
            "front_disable_flags",
            "mymap_front_map",
            "mymap_front_social",
            "last_queue_attempted",
            "last_queue_bot_found",
            "last_queue_client",
            "last_queue_social_assigned",
            "last_queue_map_seeded",
            "last_queue_success",
            "last_queue_rejected",
            "last_queue_reason",
            "last_queue_map",
            "last_queue_social",
            "last_consume_attempted",
            "last_consume_success",
            "last_consume_reason",
            "last_consume_map",
            "last_consume_social",
        ),
        metric_prefixes=(
            "last_queue_",
            "last_consume_",
            "front_",
            "mymap_",
        ),
    ),
    OptionalFieldFamily(
        name="scoreboard_match_flow_signals",
        title="Scoreboard match-flow signals",
        description=(
            "Bot/human standings classification, sorted-client ranks, and "
            "test score application status emitted by the scoreboard smoke."
        ),
        markers=(SCOREBOARD_STATUS_MARKER,),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "voting_clients",
            "connected_clients",
            "sorted_clients",
            "sorted_bots",
            "sorted_humans",
            "sorted_spectators",
            "leader_bot",
            "runner_bot",
            "score_ordered",
            "rank_ordered",
            "top_client",
            "top_bot",
            "top_human",
            "top_playing",
            "top_score",
            "top_rank",
            "top_rank_tied",
            "second_client",
            "second_bot",
            "second_human",
            "second_playing",
            "second_score",
            "second_rank",
            "second_rank_tied",
            "last_score_attempted",
            "last_score_bot_count",
            "last_score_applied",
            "last_score_leader_client",
            "last_score_runner_client",
            "last_score_leader_score",
            "last_score_runner_score",
            "last_score_reason",
        ),
        metric_prefixes=(
            "top_",
            "second_",
            "last_score_",
            "sorted_",
        ),
    ),
    OptionalFieldFamily(
        name="intermission_match_flow_signals",
        title="Intermission match-flow signals",
        description=(
            "Bot-only intermission transition, frozen/freecam client state, "
            "change-map target, and cleanup status emitted by the intermission "
            "smoke."
        ),
        markers=(INTERMISSION_STATUS_MARKER,),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "connected_clients",
            "sorted_clients",
            "sorted_bots",
            "sorted_humans",
            "intermission",
            "intermission_queued",
            "post_intermission",
            "ready_to_exit",
            "change_map_set",
            "change_map_current",
            "change_map",
            "current_map",
            "intermission_bots",
            "pm_freeze_bots",
            "freecam_bots",
            "solid_not_bots",
            "last_begin_attempted",
            "last_begin_success",
            "last_begin_bot_count",
            "last_begin_reason",
            "last_begin_map",
        ),
        metric_prefixes=(
            "intermission_",
            "change_map_",
            "last_begin_",
            "pm_freeze_",
            "freecam_",
            "solid_not_",
            "sorted_",
        ),
    ),
    OptionalFieldFamily(
        name="nextmap_match_flow_signals",
        title="Nextmap match-flow signals",
        description=(
            "Queued nextmap target, queue consumption, map-transition request, "
            "and post-reload cleanup status emitted by the nextmap smoke."
        ),
        markers=(NEXTMAP_STATUS_MARKER,),
        metric_names=(
            "bots",
            "humans",
            "playing",
            "bot_playing",
            "human_playing",
            "spectators",
            "connected_clients",
            "play_queue",
            "mymap_queue",
            "front_map",
            "front_social",
            "change_map_set",
            "change_map",
            "current_map",
            "last_transition_attempted",
            "last_transition_success",
            "last_transition_consumed",
            "last_transition_reason",
            "last_transition_target_map",
            "last_transition_current_map",
            "last_transition_play_queue_before",
            "last_transition_mymap_queue_before",
            "last_transition_play_queue_after",
            "last_transition_mymap_queue_after",
            "last_transition_override_enable_flags",
            "last_transition_override_disable_flags",
        ),
        metric_prefixes=(
            "last_transition_",
            "front_",
            "change_map",
        ),
    ),
    OptionalFieldFamily(
        name="ffa_roam_route_counters",
        title="FFA roam route counters",
        description=(
            "Default-off FFA roam/collect/engage route-owner requests, "
            "activations, route requests, and latest role metadata emitted "
            "by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ffa_roam_route_requests",
            "ffa_roam_route_policy_selections",
            "ffa_roam_route_activations",
            "ffa_roam_route_refreshes",
            "ffa_roam_route_owner_deferrals",
            "ffa_roam_route_route_requests",
            "ffa_roam_route_route_deferrals",
            "ffa_roam_route_expirations",
            "ffa_roam_route_invalid_skips",
            "last_ffa_roam_route_client",
            "last_ffa_roam_route_mode",
            "last_ffa_roam_route_mode_name",
            "last_ffa_roam_route_role",
            "last_ffa_roam_route_role_name",
            "last_ffa_roam_route_lane",
            "last_ffa_roam_route_lane_name",
            "last_ffa_roam_route_priority",
            "last_ffa_roam_route_remaining_ms",
            "last_ffa_roam_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "ffa_roam_route_",
            "last_ffa_roam_route_",
        ),
    ),
    OptionalFieldFamily(
        name="ffa_spawn_camp_avoidance_counters",
        title="FFA spawn-camp avoidance counters",
        description=(
            "Default-off FFA anti-camping route-owner requests, nearby-source "
            "selections, activations, route requests, and latest source metadata "
            "emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ffa_spawn_camp_avoidance_requests",
            "ffa_spawn_camp_avoidance_policy_selections",
            "ffa_spawn_camp_avoidance_source_selections",
            "ffa_spawn_camp_avoidance_activations",
            "ffa_spawn_camp_avoidance_fallbacks",
            "ffa_spawn_camp_avoidance_route_requests",
            "ffa_spawn_camp_avoidance_invalid_skips",
            "last_ffa_spawn_camp_avoidance_client",
            "last_ffa_spawn_camp_avoidance_source_client",
            "last_ffa_spawn_camp_avoidance_source_entity",
            "last_ffa_spawn_camp_avoidance_source_distance_sq",
            "last_ffa_spawn_camp_avoidance_policy_avoid",
            "last_ffa_spawn_camp_avoidance_goal_distance_sq",
            "last_ffa_spawn_camp_avoidance_reason",
        ),
        metric_prefixes=(
            "ffa_spawn_camp_avoidance_",
            "last_ffa_spawn_camp_avoidance_",
        ),
    ),
    OptionalFieldFamily(
        name="ffa_spawn_camp_combat_avoidance_counters",
        title="FFA spawn-camp combat avoidance counters",
        description=(
            "Default-off FFA anti-camping combat suppression evaluations, "
            "source-target blocks, clears, and latest source/target metadata "
            "emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ffa_spawn_camp_combat_avoidance_evaluations",
            "ffa_spawn_camp_combat_avoidance_blocks",
            "ffa_spawn_camp_combat_avoidance_source_blocks",
            "ffa_spawn_camp_combat_avoidance_clears",
            "ffa_spawn_camp_combat_avoidance_invalid_skips",
            "last_ffa_spawn_camp_combat_avoidance_client",
            "last_ffa_spawn_camp_combat_avoidance_target_client",
            "last_ffa_spawn_camp_combat_avoidance_target_entity",
            "last_ffa_spawn_camp_combat_avoidance_source_client",
            "last_ffa_spawn_camp_combat_avoidance_source_entity",
            "last_ffa_spawn_camp_combat_avoidance_source_distance_sq",
            "last_ffa_spawn_camp_combat_avoidance_policy_avoid",
            "last_ffa_spawn_camp_combat_avoidance_blocked",
            "last_ffa_spawn_camp_combat_avoidance_reason",
        ),
        metric_prefixes=(
            "ffa_spawn_camp_combat_avoidance_",
            "last_ffa_spawn_camp_combat_avoidance_",
        ),
    ),
    OptionalFieldFamily(
        name="team_role_route_counters",
        title="Team role route counters",
        description=(
            "Default-off match role/lane route-owner requests, activations, "
            "route requests, and latest role metadata emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "team_role_route_requests",
            "team_role_route_policy_selections",
            "team_role_route_activations",
            "team_role_route_refreshes",
            "team_role_route_owner_deferrals",
            "team_role_route_route_requests",
            "team_role_route_route_deferrals",
            "team_role_route_expirations",
            "team_role_route_invalid_skips",
            "last_team_role_route_client",
            "last_team_role_route_mode",
            "last_team_role_route_mode_name",
            "last_team_role_route_role",
            "last_team_role_route_role_name",
            "last_team_role_route_lane",
            "last_team_role_route_lane_name",
            "last_team_role_route_priority",
            "last_team_role_route_remaining_ms",
            "last_team_role_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "team_role_route_",
            "last_team_role_route_",
        ),
    ),
    OptionalFieldFamily(
        name="ctf_role_route_counters",
        title="CTF role route counters",
        description=(
            "Default-off CTF match role/lane route-owner requests, "
            "activations, route requests, and latest role metadata emitted "
            "by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ctf_role_route_requests",
            "ctf_role_route_policy_selections",
            "ctf_role_route_activations",
            "ctf_role_route_refreshes",
            "ctf_role_route_owner_deferrals",
            "ctf_role_route_objective_deferrals",
            "ctf_role_route_route_requests",
            "ctf_role_route_route_deferrals",
            "ctf_role_route_expirations",
            "ctf_role_route_invalid_skips",
            "last_ctf_role_route_client",
            "last_ctf_role_route_mode",
            "last_ctf_role_route_mode_name",
            "last_ctf_role_route_role",
            "last_ctf_role_route_role_name",
            "last_ctf_role_route_lane",
            "last_ctf_role_route_lane_name",
            "last_ctf_role_route_priority",
            "last_ctf_role_route_remaining_ms",
            "last_ctf_role_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "ctf_role_route_",
            "last_ctf_role_route_",
        ),
    ),
    OptionalFieldFamily(
        name="ctf_role_combat_counters",
        title="CTF role combat counters",
        description=(
            "Default-off CTF match role/lane combat-owner requests, target "
            "selection, attack decisions, and latest visible/shootable target "
            "metadata emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ctf_role_combat_requests",
            "ctf_role_combat_policy_selections",
            "ctf_role_combat_target_selections",
            "ctf_role_combat_attack_decisions",
            "ctf_role_combat_decision_overrides",
            "ctf_role_combat_target_deferrals",
            "ctf_role_combat_invalid_skips",
            "last_ctf_role_combat_client",
            "last_ctf_role_combat_mode",
            "last_ctf_role_combat_mode_name",
            "last_ctf_role_combat_role",
            "last_ctf_role_combat_role_name",
            "last_ctf_role_combat_lane",
            "last_ctf_role_combat_lane_name",
            "last_ctf_role_combat_priority",
            "last_ctf_role_combat_target_client",
            "last_ctf_role_combat_target_entity",
            "last_ctf_role_combat_target_distance_sq",
            "last_ctf_role_combat_target_visible",
            "last_ctf_role_combat_target_shootable",
            "last_ctf_role_combat_reason",
        ),
        metric_prefixes=(
            "ctf_role_combat_",
            "last_ctf_role_combat_",
        ),
    ),
    OptionalFieldFamily(
        name="ffa_role_combat_counters",
        title="FFA role combat counters",
        description=(
            "Default-off FFA match role/lane combat-owner requests, target "
            "selection, attack decisions, and latest visible/shootable target "
            "metadata emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ffa_role_combat_requests",
            "ffa_role_combat_policy_selections",
            "ffa_role_combat_target_selections",
            "ffa_role_combat_attack_decisions",
            "ffa_role_combat_decision_overrides",
            "ffa_role_combat_target_deferrals",
            "ffa_role_combat_invalid_skips",
            "last_ffa_role_combat_client",
            "last_ffa_role_combat_mode",
            "last_ffa_role_combat_mode_name",
            "last_ffa_role_combat_role",
            "last_ffa_role_combat_role_name",
            "last_ffa_role_combat_lane",
            "last_ffa_role_combat_lane_name",
            "last_ffa_role_combat_priority",
            "last_ffa_role_combat_target_client",
            "last_ffa_role_combat_target_entity",
            "last_ffa_role_combat_target_distance_sq",
            "last_ffa_role_combat_target_visible",
            "last_ffa_role_combat_target_shootable",
            "last_ffa_role_combat_reason",
        ),
        metric_prefixes=(
            "ffa_role_combat_",
            "last_ffa_role_combat_",
        ),
    ),
    OptionalFieldFamily(
        name="team_role_combat_counters",
        title="Team role combat counters",
        description=(
            "Default-off TDM match role/lane combat-owner requests, target "
            "selection, attack decisions, and latest visible/shootable target "
            "metadata emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "team_role_combat_requests",
            "team_role_combat_policy_selections",
            "team_role_combat_target_selections",
            "team_role_combat_attack_decisions",
            "team_role_combat_decision_overrides",
            "team_role_combat_target_deferrals",
            "team_role_combat_invalid_skips",
            "last_team_role_combat_client",
            "last_team_role_combat_mode",
            "last_team_role_combat_mode_name",
            "last_team_role_combat_role",
            "last_team_role_combat_role_name",
            "last_team_role_combat_lane",
            "last_team_role_combat_lane_name",
            "last_team_role_combat_priority",
            "last_team_role_combat_target_client",
            "last_team_role_combat_target_entity",
            "last_team_role_combat_target_distance_sq",
            "last_team_role_combat_target_visible",
            "last_team_role_combat_target_shootable",
            "last_team_role_combat_reason",
        ),
        metric_prefixes=(
            "team_role_combat_",
            "last_team_role_combat_",
        ),
    ),
    OptionalFieldFamily(
        name="ctf_dropped_flag_route_counters",
        title="CTF dropped-flag route counters",
        description=(
            "Default-off CTF dropped-flag response route-owner requests, "
            "assignments, route commands, and latest dropped flag target "
            "metadata emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ctf_dropped_flag_route_requests",
            "ctf_dropped_flag_route_assignments",
            "ctf_dropped_flag_route_route_requests",
            "ctf_dropped_flag_route_route_commands",
            "ctf_dropped_flag_route_invalid_skips",
            "last_ctf_dropped_flag_route_client",
            "last_ctf_dropped_flag_route_role",
            "last_ctf_dropped_flag_route_role_name",
            "last_ctf_dropped_flag_route_lane",
            "last_ctf_dropped_flag_route_lane_name",
            "last_ctf_dropped_flag_route_type",
            "last_ctf_dropped_flag_route_type_name",
            "last_ctf_dropped_flag_route_source",
            "last_ctf_dropped_flag_route_source_name",
            "last_ctf_dropped_flag_route_entity",
            "last_ctf_dropped_flag_route_item",
            "last_ctf_dropped_flag_route_priority",
            "last_ctf_dropped_flag_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "ctf_dropped_flag_route_",
            "last_ctf_dropped_flag_route_",
        ),
    ),
    OptionalFieldFamily(
        name="ctf_carrier_support_route_counters",
        title="CTF carrier-support route counters",
        description=(
            "Default-off CTF carrier-support route-owner requests, "
            "assignments, route commands, and latest flag-carrier target "
            "metadata emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ctf_carrier_support_route_requests",
            "ctf_carrier_support_route_assignments",
            "ctf_carrier_support_route_route_requests",
            "ctf_carrier_support_route_route_commands",
            "ctf_carrier_support_route_invalid_skips",
            "last_ctf_carrier_support_route_client",
            "last_ctf_carrier_support_route_role",
            "last_ctf_carrier_support_route_role_name",
            "last_ctf_carrier_support_route_lane",
            "last_ctf_carrier_support_route_lane_name",
            "last_ctf_carrier_support_route_type",
            "last_ctf_carrier_support_route_type_name",
            "last_ctf_carrier_support_route_source",
            "last_ctf_carrier_support_route_source_name",
            "last_ctf_carrier_support_route_entity",
            "last_ctf_carrier_support_route_carrier_client",
            "last_ctf_carrier_support_route_item",
            "last_ctf_carrier_support_route_priority",
            "last_ctf_carrier_support_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "ctf_carrier_support_route_",
            "last_ctf_carrier_support_route_",
        ),
    ),
    OptionalFieldFamily(
        name="ctf_base_return_route_counters",
        title="CTF base-return route counters",
        description=(
            "Default-off CTF base-return route-owner requests, assignments, "
            "route commands, and latest own-flag carrier target metadata "
            "emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ctf_base_return_route_requests",
            "ctf_base_return_route_assignments",
            "ctf_base_return_route_route_requests",
            "ctf_base_return_route_route_commands",
            "ctf_base_return_route_invalid_skips",
            "last_ctf_base_return_route_client",
            "last_ctf_base_return_route_role",
            "last_ctf_base_return_route_role_name",
            "last_ctf_base_return_route_lane",
            "last_ctf_base_return_route_lane_name",
            "last_ctf_base_return_route_type",
            "last_ctf_base_return_route_type_name",
            "last_ctf_base_return_route_source",
            "last_ctf_base_return_route_source_name",
            "last_ctf_base_return_route_entity",
            "last_ctf_base_return_route_carrier_client",
            "last_ctf_base_return_route_item",
            "last_ctf_base_return_route_priority",
            "last_ctf_base_return_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "ctf_base_return_route_",
            "last_ctf_base_return_route_",
        ),
    ),
    OptionalFieldFamily(
        name="ctf_objective_route_counters",
        title="CTF objective-route policy counters",
        description=(
            "Default-off CTF objective-route policy requests, candidate "
            "availability, priority selections, lower-priority deferrals, "
            "route commands, and latest selected CTF objective metadata "
            "emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "ctf_objective_route_requests",
            "ctf_objective_route_assignments",
            "ctf_objective_route_base_return_candidates",
            "ctf_objective_route_carrier_support_candidates",
            "ctf_objective_route_dropped_flag_candidates",
            "ctf_objective_route_base_return_selections",
            "ctf_objective_route_carrier_support_selections",
            "ctf_objective_route_dropped_flag_selections",
            "ctf_objective_route_carrier_support_deferrals",
            "ctf_objective_route_dropped_flag_deferrals",
            "ctf_objective_route_route_requests",
            "ctf_objective_route_route_commands",
            "ctf_objective_route_invalid_skips",
            "last_ctf_objective_route_client",
            "last_ctf_objective_route_selection",
            "last_ctf_objective_route_selection_name",
            "last_ctf_objective_route_role",
            "last_ctf_objective_route_role_name",
            "last_ctf_objective_route_lane",
            "last_ctf_objective_route_lane_name",
            "last_ctf_objective_route_type",
            "last_ctf_objective_route_type_name",
            "last_ctf_objective_route_source",
            "last_ctf_objective_route_source_name",
            "last_ctf_objective_route_entity",
            "last_ctf_objective_route_carrier_client",
            "last_ctf_objective_route_item",
            "last_ctf_objective_route_priority",
            "last_ctf_objective_route_goal_distance_sq",
        ),
        metric_prefixes=(
            "ctf_objective_route_",
            "last_ctf_objective_route_",
        ),
    ),
    OptionalFieldFamily(
        name="ffa_item_role_counters",
        title="FFA item role counters",
        description=(
            "Default-off FFA item-role scoring bridge evaluations, selected "
            "pickup goals, and latest role/category metadata emitted by nav "
            "policy status."
        ),
        markers=(NAV_POLICY_STATUS_MARKER,),
        metric_names=(
            "ffa_item_role_evaluations",
            "ffa_item_role_selections",
            "ffa_item_role_score_boosts",
            "ffa_item_role_selected_goals",
            "ffa_item_role_invalid_skips",
            "last_ffa_item_role_client",
            "last_ffa_item_role_mode",
            "last_ffa_item_role_mode_name",
            "last_ffa_item_role_role",
            "last_ffa_item_role_role_name",
            "last_ffa_item_role_lane",
            "last_ffa_item_role_lane_name",
            "last_ffa_item_role_category",
            "last_ffa_item_role_category_name",
            "last_ffa_item_role_item_role",
            "last_ffa_item_role_item_role_name",
            "last_ffa_item_role_priority",
            "last_ffa_item_role_score_boost",
            "last_ffa_item_role_entity",
            "last_ffa_item_role_item",
            "last_ffa_item_role_score",
        ),
        metric_prefixes=(
            "ffa_item_role_",
            "last_ffa_item_role_",
        ),
    ),
    OptionalFieldFamily(
        name="ctf_item_role_counters",
        title="CTF item role counters",
        description=(
            "Default-off CTF item-role scoring bridge evaluations, selected "
            "pickup goals, and latest role/category metadata emitted by nav "
            "policy status."
        ),
        markers=(NAV_POLICY_STATUS_MARKER,),
        metric_names=(
            "ctf_item_role_evaluations",
            "ctf_item_role_selections",
            "ctf_item_role_score_boosts",
            "ctf_item_role_selected_goals",
            "ctf_item_role_invalid_skips",
            "last_ctf_item_role_client",
            "last_ctf_item_role_mode",
            "last_ctf_item_role_mode_name",
            "last_ctf_item_role_role",
            "last_ctf_item_role_role_name",
            "last_ctf_item_role_lane",
            "last_ctf_item_role_lane_name",
            "last_ctf_item_role_category",
            "last_ctf_item_role_category_name",
            "last_ctf_item_role_item_role",
            "last_ctf_item_role_item_role_name",
            "last_ctf_item_role_priority",
            "last_ctf_item_role_score_boost",
            "last_ctf_item_role_entity",
            "last_ctf_item_role_item",
            "last_ctf_item_role_score",
        ),
        metric_prefixes=(
            "ctf_item_role_",
            "last_ctf_item_role_",
        ),
    ),
    OptionalFieldFamily(
        name="team_item_role_counters",
        title="Team item role counters",
        description=(
            "Default-off TDM item-role scoring bridge evaluations, selected "
            "pickup goals, and latest role/category metadata emitted by nav "
            "policy status."
        ),
        markers=(NAV_POLICY_STATUS_MARKER,),
        metric_names=(
            "team_item_role_evaluations",
            "team_item_role_selections",
            "team_item_role_score_boosts",
            "team_item_role_selected_goals",
            "team_item_role_invalid_skips",
            "last_team_item_role_client",
            "last_team_item_role_mode",
            "last_team_item_role_mode_name",
            "last_team_item_role_role",
            "last_team_item_role_role_name",
            "last_team_item_role_lane",
            "last_team_item_role_lane_name",
            "last_team_item_role_category",
            "last_team_item_role_category_name",
            "last_team_item_role_item_role",
            "last_team_item_role_item_role_name",
            "last_team_item_role_priority",
            "last_team_item_role_score_boost",
            "last_team_item_role_entity",
            "last_team_item_role_item",
            "last_team_item_role_score",
        ),
        metric_prefixes=(
            "team_item_role_",
            "last_team_item_role_",
        ),
    ),
    OptionalFieldFamily(
        name="team_resource_denial_counters",
        title="Team resource denial counters",
        description=(
            "Default-off TDM resource-denial scoring bridge evaluations, "
            "deny-enemy resource policy selections, selected pickup goals, "
            "and latest role/category/intent metadata emitted by nav policy "
            "status."
        ),
        markers=(NAV_POLICY_STATUS_MARKER,),
        metric_names=(
            "team_resource_denial_evaluations",
            "team_resource_denial_policy_denies",
            "team_resource_denial_score_boosts",
            "team_resource_denial_selected_goals",
            "team_resource_denial_invalid_skips",
            "last_team_resource_denial_client",
            "last_team_resource_denial_mode",
            "last_team_resource_denial_mode_name",
            "last_team_resource_denial_role",
            "last_team_resource_denial_role_name",
            "last_team_resource_denial_lane",
            "last_team_resource_denial_lane_name",
            "last_team_resource_denial_category",
            "last_team_resource_denial_category_name",
            "last_team_resource_denial_intent",
            "last_team_resource_denial_intent_name",
            "last_team_resource_denial_priority",
            "last_team_resource_denial_score_boost",
            "last_team_resource_denial_entity",
            "last_team_resource_denial_item",
            "last_team_resource_denial_score",
        ),
        metric_prefixes=(
            "team_resource_denial_",
            "last_team_resource_denial_",
        ),
    ),
    OptionalFieldFamily(
        name="team_fire_avoidance_counters",
        title="Team fire avoidance counters",
        description=(
            "Default-off TDM friendly-fire policy bridge evaluations, "
            "live attack suppressions, and latest blocked target/line metadata "
            "emitted by frame-command status."
        ),
        markers=(STATUS_MARKER,),
        metric_names=(
            "team_fire_avoidance_evaluations",
            "team_fire_avoidance_blocks",
            "team_fire_avoidance_target_blocks",
            "team_fire_avoidance_line_blocks",
            "team_fire_avoidance_clears",
            "team_fire_avoidance_invalid_skips",
            "last_team_fire_avoidance_client",
            "last_team_fire_avoidance_target_client",
            "last_team_fire_avoidance_friendly_line",
            "last_team_fire_avoidance_target_allowed",
            "last_team_fire_avoidance_blocked",
            "last_team_fire_avoidance_reason",
        ),
        metric_prefixes=(
            "team_fire_avoidance_",
            "last_team_fire_avoidance_",
        ),
    ),
)


SCENARIOS: tuple[Scenario, ...] = (
    Scenario(
        name="spawn_route_to_item",
        title="Spawn and route to item",
        smoke_mode=2,
        description="One bot spawns and receives an item-backed AAS route command.",
        task_ids=("DV-03-T05", "FR-04-T14"),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "smoke status must pass"),
            MetricCheck("commands", "ge", 1, "bot command builder must emit commands"),
            MetricCheck("route_commands", "ge", 1, "route steering must drive commands"),
            MetricCheck("route_failures", "eq", 0, "item route must stay valid"),
            MetricCheck("item_goal_assignments", "ge", 1, "item goal must be selected"),
            MetricCheck("last_item_goal_area", "gt", 0, "item goal must resolve to an AAS area"),
        ),
    ),
    Scenario(
        name="recover_from_stall",
        title="Recover from stalled command",
        smoke_mode=4,
        description="Two bots build commands without applying movement, forcing stuck recovery.",
        task_ids=("DV-03-T05", "FR-04-T14"),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "smoke status must pass"),
            MetricCheck("commands", "ge", 1, "command path must remain active"),
            MetricCheck("route_failures", "eq", 0, "recovery must not turn into route failures"),
            MetricCheck("stuck_detections", "ge", 1, "stalled movement must be detected"),
            MetricCheck("stuck_repath_refreshes", "ge", 1, "stuck detection must repath"),
            MetricCheck("stuck_recovery_activations", "ge", 1, "recovery policy must activate"),
            MetricCheck("recovery_command_uses", "ge", 1, "recovery commands must be emitted"),
        ),
    ),
    Scenario(
        name="multi_bot_reservation",
        title="Multi-bot route-command reservation",
        smoke_mode=17,
        description="Eight bots route concurrently while item reservations avoid duplicated goals.",
        task_ids=("DV-03-T05", "FR-04-T16"),
        budget_seconds=30,
        checks=(
            MetricCheck("pass", "eq", 1, "smoke status must pass"),
            MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must be active"),
            MetricCheck("commands", "ge", 8, "all target bots must emit commands"),
            MetricCheck("route_commands", "ge", 8, "all target bots must route"),
            MetricCheck("route_failures", "eq", 0, "multi-bot route pressure must stay clean"),
            MetricCheck(
                "item_goal_peak_active_reservations",
                "ge",
                8,
                "reservation pressure proof must reach all eight bots",
            ),
        ),
        degradation_policy=DegradationPolicy(
            name="high_bot_short_pressure",
            tier="short_pressure",
            bot_count=8,
            budget_profile="scenario_runtime_budget",
            preserved_behavior=(
                "all requested bots emit commands",
                "all requested bots emit route commands",
                "route failures remain zero",
                "item reservation pressure reaches all eight bots",
            ),
            allowed_degradation=(
                "none for the short reservation-pressure proof",
            ),
            required_metrics=(
                MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must be active"),
                MetricCheck("commands", "ge", 8, "all target bots must emit commands"),
                MetricCheck("route_commands", "ge", 8, "all target bots must route"),
                MetricCheck("route_failures", "eq", 0, "routes must stay clean under pressure"),
                MetricCheck(
                    "item_goal_peak_active_reservations",
                    "ge",
                    8,
                    "short pressure must still prove all eight reservation slots",
                ),
            ),
            notes=(
                "This is the fast high-bot guard used by the default implemented scenario suite.",
            ),
        ),
    ),
    Scenario(
        name="high_bot_soak_degradation",
        title="High-bot soak degradation",
        smoke_mode=18,
        description=(
            "Eight-bot long soak verifies that high-count degradation preserves command "
            "throughput and route cleanliness while allowing transient item-reservation "
            "occupancy to decay over time."
        ),
        task_ids=("DV-03-T05", "FR-04-T16"),
        budget_seconds=660,
        checks=(
            MetricCheck("pass", "eq", 1, "soak status must pass"),
            MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must remain active"),
            MetricCheck("commands", "ge", 120000, "ten-minute soak must sustain command throughput"),
            MetricCheck("route_commands", "ge", 120000, "ten-minute soak must sustain route commands"),
            MetricCheck("route_failures", "eq", 0, "high-bot soak must stay route-clean"),
            MetricCheck("route_invalid_slots", "eq", 0, "route slots must stay valid"),
            MetricCheck("route_debug_missing_frames", "eq", 0, "route debug output must keep up"),
            MetricCheck("skipped_inactive", "eq", 0, "no target bot may become inactive"),
        ),
        marker_checks=(
            MarkerMetricCheck(
                SOAK_BEGIN_MARKER,
                "target",
                "ge",
                8,
                "soak must start with the eight-bot target",
            ),
            MarkerMetricCheck(
                SOAK_COMPLETE_MARKER,
                "elapsed_ms",
                "ge",
                540000,
                "soak must cover a near-ten-minute window with timing slack",
            ),
            MarkerMetricCheck(
                SOAK_COMPLETE_MARKER,
                "count",
                "ge",
                8,
                "all target bots must still be present when the soak completes",
            ),
            MarkerMetricCheck(
                SOAK_COMPLETE_MARKER,
                "reports",
                "ge",
                8,
                "long soak should emit regular progress reports",
            ),
        ),
        extra_cvars=(("sv_bot_frame_command_smoke_soak_ms", "600000"),),
        manual_only=True,
        selection_tags=("soak", "high_bot", "degradation"),
        degradation_policy=DegradationPolicy(
            name="high_bot_long_soak",
            tier="long_soak",
            bot_count=8,
            budget_profile="tools/bot_perf/default_soak_budget.json",
            preserved_behavior=(
                "all requested bots remain active",
                "command and route-command throughput stay sustained",
                "route failures and invalid route slots remain zero",
                "route debug output does not drop frames",
            ),
            allowed_degradation=(
                "final item_goal_active_reservations may fall below eight",
                "item_goal_peak_active_reservations may be lower than the short pressure proof",
                "stuck recovery may engage while command throughput remains intact",
            ),
            required_metrics=(
                MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must remain active"),
                MetricCheck("commands", "ge", 120000, "ten-minute soak must sustain command throughput"),
                MetricCheck("route_commands", "ge", 120000, "ten-minute soak must sustain route commands"),
                MetricCheck("route_failures", "eq", 0, "routes must stay clean under high-bot soak"),
                MetricCheck("route_invalid_slots", "eq", 0, "route slots must stay valid"),
                MetricCheck("skipped_inactive", "eq", 0, "all target bots must remain active"),
            ),
            required_marker_metrics=(
                MarkerMetricCheck(
                    SOAK_COMPLETE_MARKER,
                    "elapsed_ms",
                    "ge",
                    540000,
                    "soak must cover a near-ten-minute window with timing slack",
                ),
                MarkerMetricCheck(
                    SOAK_COMPLETE_MARKER,
                    "reports",
                    "ge",
                    8,
                    "soak must retain progress-report visibility",
                ),
            ),
            notes=(
                "Use the bot perf default soak budget for derived per-bot/sec thresholds.",
            ),
        ),
    ),
    Scenario(
        name="map_change_repeat",
        title="Map-change repeat",
        smoke_mode=19,
        description="Eight bots route, unload/reload the active map, then repeat the route proof.",
        task_ids=("DV-03-T05", "FR-04-T16"),
        budget_seconds=45,
        checks=(
            MetricCheck("pass", "eq", 1, "final repeated smoke status must pass"),
            MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must be active after reload"),
            MetricCheck("commands", "ge", 8, "post-reload bots must emit commands"),
            MetricCheck("route_commands", "ge", 8, "post-reload bots must route"),
            MetricCheck("route_failures", "eq", 0, "post-reload route proof must stay clean"),
            MetricCheck(
                "item_goal_peak_active_reservations",
                "ge",
                8,
                "post-reload reservation pressure proof must reach all eight bots",
            ),
        ),
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "cycles",
                "eq",
                2,
                "default repeat smoke must complete two proof cycles",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "map_changes",
                "eq",
                1,
                "two proof cycles must include one map reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "final_count",
                "eq",
                0,
                "bots must be removed after the final repeat cycle",
            ),
        ),
        extra_cvars=(("sv_bot_frame_command_smoke_map_repeat_cycles", "2"),),
    ),
    Scenario(
        name="map_restart_cleanup",
        title="Map-restart cleanup",
        smoke_mode=19,
        description=(
            "Eight bots route, restart the active map through the map force path, "
            "then repeat the route proof and verify cleanup after the restart."
        ),
        task_ids=("FR-04-T06", "DV-03-T05", "FR-04-T16"),
        budget_seconds=45,
        checks=(
            MetricCheck("pass", "eq", 1, "final repeated restart smoke status must pass"),
            MetricCheck("expected_min_commands", "ge", 8, "eight-bot target must be active after restart"),
            MetricCheck("commands", "ge", 8, "post-restart bots must emit commands"),
            MetricCheck("route_commands", "ge", 8, "post-restart bots must route"),
            MetricCheck("route_failures", "eq", 0, "post-restart route proof must stay clean"),
            MetricCheck(
                "item_goal_peak_active_reservations",
                "ge",
                8,
                "post-restart reservation pressure proof must reach all eight bots",
            ),
        ),
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle=begin",
                "command",
                "eq",
                "map_force",
                "restart proof must run the map force reload path",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle=begin",
                "restart",
                "eq",
                1,
                "restart proof must advertise restart mode at cycle start",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=queued",
                "command",
                "eq",
                "map_force",
                "restart proof must queue a map force reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=queued",
                "restart",
                "eq",
                1,
                "queued reload must be marked as restart-driven",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=observed",
                "command",
                "eq",
                "map_force",
                "restart proof must observe the map force reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=observed",
                "restart",
                "eq",
                1,
                "observed reload must be marked as restart-driven",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=observed",
                "completed_cycles",
                "eq",
                1,
                "restart reload must occur after the first completed proof cycle",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cleanup_status",
                "pass",
                "eq",
                1,
                "cleanup status after restart must pass",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cleanup_status",
                "count",
                "eq",
                0,
                "cleanup status after restart must see no bots left behind",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "cycles",
                "eq",
                2,
                "restart repeat smoke must complete two proof cycles",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "map_changes",
                "eq",
                1,
                "two restart proof cycles must include one map restart",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "final_count",
                "eq",
                0,
                "bots must be removed after the final restart cycle",
            ),
        ),
        extra_cvars=(
            ("sv_bot_frame_command_smoke_map_repeat_cycles", "2"),
            ("sv_bot_frame_command_smoke_map_repeat_restart", "1"),
        ),
        selection_tags=("match", "restart"),
    ),
    Scenario(
        name="warmup_bot_start_readiness",
        title="Warmup bot-start readiness",
        smoke_mode=2,
        description=(
            "Two bot-only warmup participants satisfy minplayers and prove "
            "match_start_no_humans can start ready-up without human ready flags, "
            "then cleanup returns the bot population to zero."
        ),
        task_ids=("FR-04-T06", "DV-03-T05"),
        budget_seconds=20,
        smoke_cvar="sv_bot_warmup_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke=begin",
                "target",
                "eq",
                2,
                "warmup smoke must request two bot participants",
            ),
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke=begin",
                "ready_up",
                "eq",
                1,
                "warmup smoke must exercise ready-up mode",
            ),
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke=begin",
                "start_no_humans",
                "eq",
                1,
                "warmup smoke must enable bot-only start",
            ),
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke_after_add_requests",
                "added_alpha",
                "eq",
                1,
                "first warmup bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke_after_add_requests",
                "added_bravo",
                "eq",
                1,
                "second warmup bot add request must be queued or accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke_after_add_requests",
                "count",
                "ge",
                1,
                "at least one warmup bot should materialize before queued add processing finishes",
            ),
            MarkerMetricCheck(
                WARMUP_STATUS_MARKER,
                "bots",
                "any_eq",
                2,
                "live warmup status must see both bots",
            ),
            MarkerMetricCheck(
                WARMUP_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "warmup proof must stay bot-only",
            ),
            MarkerMetricCheck(
                WARMUP_STATUS_MARKER,
                "playing",
                "any_eq",
                2,
                "both bots must be playing for the readiness proof",
            ),
            MarkerMetricCheck(
                WARMUP_STATUS_MARKER,
                "minplayers_met",
                "any_eq",
                1,
                "bot-only readiness must satisfy minplayers",
            ),
            MarkerMetricCheck(
                WARMUP_STATUS_MARKER,
                "bot_only_start",
                "any_eq",
                1,
                "warmup status must identify the bot-only start path",
            ),
            MarkerMetricCheck(
                WARMUP_STATUS_MARKER,
                "can_start",
                "any_eq",
                1,
                "bot-only warmup should be allowed to start",
            ),
            MarkerMetricCheck(
                WARMUP_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "warmup status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke_removed_all",
                "count",
                "eq",
                0,
                "warmup smoke must remove all bots before final status",
            ),
            MarkerMetricCheck(
                "q3a_bot_warmup_smoke=end",
                "final_count",
                "eq",
                0,
                "warmup smoke must finish with zero bots",
            ),
        ),
        selection_tags=("match", "warmup"),
    ),
    Scenario(
        name="vote_bot_exclusion",
        title="Vote bot exclusion",
        smoke_mode=2,
        description=(
            "Two bot-only FFA participants remain excluded from the voting "
            "population, a bot-origin vote launch is rejected by the game "
            "vote layer, and cleanup leaves no active vote or bot clients."
        ),
        task_ids=("FR-04-T06", "FR-07-T01", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_vote_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_vote_smoke=begin",
                "target",
                "eq",
                2,
                "vote smoke must request two bot participants",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke=begin",
                "allow_voting",
                "eq",
                1,
                "vote smoke must leave voting enabled while testing bot exclusion",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke=begin",
                "bot_vote_block",
                "eq",
                1,
                "vote smoke must exercise the bot-origin vote block",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke_after_add_requests",
                "added_alpha",
                "eq",
                1,
                "first vote-smoke bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke_after_add_requests",
                "added_bravo",
                "eq",
                1,
                "second vote-smoke bot add request must be queued or accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke_after_add_requests",
                "count",
                "ge",
                1,
                "at least one vote-smoke bot should materialize before queued add processing finishes",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke_status_requested",
                "count",
                "ge",
                2,
                "vote status must run after both bots are present",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "bots",
                "any_eq",
                2,
                "pre-cleanup vote status must count both bots",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "vote exclusion proof must stay bot-only",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "playing",
                "any_eq",
                2,
                "both bots must be playing for vote population accounting",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "voting_clients",
                "any_eq",
                0,
                "bot-only participants must not inflate the voting population",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "active_vote",
                "any_eq",
                0,
                "vote status must show no active vote before or after rejection",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "last_launch_attempted",
                "any_eq",
                0,
                "initial vote status must be reset before the launch attempt",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_launch",
                "bot_found",
                "eq",
                1,
                "vote smoke must find a playing bot to test the launch guard",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_launch",
                "success",
                "eq",
                0,
                "bot-origin vote launch must be rejected",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_launch",
                "blocked",
                "eq",
                1,
                "bot-origin vote launch must hit the explicit bot block",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_launch",
                "reason",
                "eq",
                "bot_blocked",
                "bot-origin vote launch must report the bot-block reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_launch",
                "active_vote",
                "eq",
                0,
                "rejected bot-origin vote must not leave an active vote",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke_launch_requested",
                "success",
                "eq",
                0,
                "server smoke must observe the rejected launch result",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "last_launch_attempted",
                "any_eq",
                1,
                "post-launch vote status must record the launch attempt",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "last_launch_success",
                "any_eq",
                0,
                "post-launch vote status must record the failed launch",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "last_launch_blocked",
                "any_eq",
                1,
                "post-launch vote status must record the bot block",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "vote status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke_removed_all",
                "count",
                "eq",
                0,
                "vote smoke must remove all bots before final status",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest vote status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "active_vote",
                "eq",
                0,
                "latest vote status must report no active vote after cleanup",
            ),
            MarkerMetricCheck(
                VOTE_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest vote status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_vote_smoke=end",
                "final_count",
                "eq",
                0,
                "vote smoke must finish with zero bots",
            ),
        ),
        selection_tags=("match", "votes"),
    ),
    Scenario(
        name="admin_bot_privilege_audit",
        title="Admin bot privilege audit",
        smoke_mode=2,
        description=(
            "A bot-only FFA participant is temporarily given an admin session "
            "bit, attempts the registered admin-only lock_team command, and "
            "is still rejected before the red team lock can change."
        ),
        task_ids=("FR-04-T06", "FR-07-T04", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_admin_audit_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke=begin",
                "target",
                "eq",
                1,
                "admin audit smoke must request one bot participant",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke=begin",
                "admin_command",
                "eq",
                "lock_team",
                "admin audit smoke must exercise the lock_team command",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke=begin",
                "bot_admin_block",
                "eq",
                1,
                "admin audit smoke must exercise the bot admin-command block",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke_after_add_requests",
                "added",
                "eq",
                1,
                "admin audit bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke_after_add_requests",
                "count",
                "ge",
                1,
                "admin audit bot should materialize before command testing",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke_status_requested",
                "count",
                "ge",
                1,
                "admin audit status must run after the bot is present",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "bots",
                "any_eq",
                1,
                "pre-cleanup admin audit status must count the bot",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "admin audit proof must stay bot-only",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "playing",
                "any_eq",
                1,
                "admin audit bot must be playing before command testing",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "admin_bots",
                "any_eq",
                0,
                "admin audit status must not leave a persistent admin bot",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "allow_admin",
                "any_eq",
                1,
                "admin audit must run while admin commands are globally enabled",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "red_locked",
                "any_eq",
                0,
                "red team must begin unlocked and remain unlocked",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "last_attempted",
                "any_eq",
                0,
                "initial admin audit status must be reset before the attempt",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "bot_found",
                "eq",
                1,
                "admin audit must find a playing bot to test the command guard",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "forced_admin",
                "eq",
                1,
                "admin audit must temporarily force the bot admin session bit",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "admin_session",
                "eq",
                1,
                "command audit must observe the forced admin session",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "command",
                "eq",
                "lock_team",
                "admin audit must attempt the registered lock_team command",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "command_found",
                "eq",
                1,
                "admin audit must resolve the registered command",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "admin_only",
                "eq",
                1,
                "admin audit must exercise an admin-only command",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "allowed",
                "eq",
                0,
                "forced-admin bot must still be denied permission",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "executed",
                "eq",
                0,
                "denied bot admin command must not execute",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "blocked",
                "eq",
                1,
                "denied bot admin command must hit the bot-specific block",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "reason",
                "eq",
                "bot_admin_blocked",
                "admin audit must report the bot-admin block reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "red_locked_before",
                "eq",
                0,
                "red team must be unlocked before the denied command",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "red_locked_after",
                "eq",
                0,
                "denied command must not lock the red team",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_attempt",
                "admin_bots",
                "eq",
                0,
                "admin audit must restore the bot admin bit before status count",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke_command_requested",
                "blocked",
                "eq",
                1,
                "server smoke must observe the blocked command result",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "last_attempted",
                "any_eq",
                1,
                "post-command status must record the command attempt",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "last_blocked",
                "any_eq",
                1,
                "post-command status must retain the bot-admin block",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "last_executed",
                "any_eq",
                0,
                "post-command status must show the command did not execute",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "last_red_locked_after",
                "any_eq",
                0,
                "post-command status must show the red team remained unlocked",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "admin audit status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke_removed_all",
                "count",
                "eq",
                0,
                "admin audit smoke must remove all bots before final status",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest admin audit status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "last_blocked",
                "eq",
                1,
                "latest admin audit status must retain the blocked result",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "red_locked",
                "eq",
                0,
                "latest admin audit status must keep red team unlocked",
            ),
            MarkerMetricCheck(
                ADMIN_AUDIT_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest admin audit status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke=end",
                "final_count",
                "eq",
                0,
                "admin audit smoke must finish with zero bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_admin_audit_smoke=end",
                "pass",
                "eq",
                1,
                "admin audit smoke must finish successfully",
            ),
        ),
        selection_tags=("match", "admin"),
    ),
    Scenario(
        name="tournament_bot_veto_exclusion",
        title="Tournament bot veto exclusion",
        smoke_mode=2,
        description=(
            "A bot-only tournament participant is assigned the active home "
            "side identity, attempts a veto pick, and is still rejected "
            "without adding a pick or ban."
        ),
        task_ids=("FR-04-T06", "FR-07-T02", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_tournament_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=begin",
                "target",
                "eq",
                1,
                "tournament smoke must request one bot participant",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=begin",
                "bot_veto_block",
                "eq",
                1,
                "tournament smoke must exercise the bot veto block",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=begin",
                "action",
                "eq",
                "pick",
                "tournament smoke must attempt a pick action",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_after_add_requests",
                "added",
                "eq",
                1,
                "tournament veto bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_after_add_requests",
                "count",
                "ge",
                1,
                "tournament veto bot should materialize before setup",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_setup",
                "bot_found",
                "eq",
                1,
                "tournament setup must find a playing bot",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_setup",
                "configured",
                "eq",
                1,
                "tournament setup must activate a tournament state",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_setup",
                "veto_started",
                "eq",
                1,
                "tournament setup must start the veto phase",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_setup",
                "bot_is_home",
                "eq",
                1,
                "tournament setup must give the bot the active home identity",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_setup",
                "pool",
                "ge",
                3,
                "tournament setup must seed a veto-capable map pool",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_setup",
                "picks_needed",
                "eq",
                2,
                "tournament setup must require multiple picks",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_setup_requested",
                "configured",
                "eq",
                1,
                "server smoke must observe the configured tournament state",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "bots",
                "any_eq",
                1,
                "pre-cleanup tournament status must count the bot",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "tournament veto proof must stay bot-only",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "active",
                "any_eq",
                1,
                "tournament status must be active during the proof",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "veto_started",
                "any_eq",
                1,
                "tournament status must show the veto phase started",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "picks",
                "any_eq",
                0,
                "tournament status must start with zero picks",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "bans",
                "any_eq",
                0,
                "tournament status must start with zero bans",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_setup_bot_is_home",
                "any_eq",
                1,
                "status must retain that the bot held the active identity",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "bot_found",
                "eq",
                1,
                "tournament veto attempt must find the bot",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "active_before",
                "eq",
                1,
                "tournament veto attempt must run while tournament is active",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "veto_started_before",
                "eq",
                1,
                "tournament veto attempt must run during the veto phase",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "allowed",
                "eq",
                0,
                "bot-held tournament identity must still be denied veto permission",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "blocked",
                "eq",
                1,
                "tournament veto attempt must hit the bot-specific block",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "reason",
                "eq",
                "bot_blocked",
                "tournament veto attempt must report the bot block reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "picks_before",
                "eq",
                0,
                "tournament veto attempt must begin with zero picks",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "picks_after",
                "eq",
                0,
                "denied bot veto must not add a pick",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "bans_before",
                "eq",
                0,
                "tournament veto attempt must begin with zero bans",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_veto",
                "bans_after",
                "eq",
                0,
                "denied bot veto must not add a ban",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_veto_requested",
                "blocked",
                "eq",
                1,
                "server smoke must observe the blocked veto result",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_veto_attempted",
                "any_eq",
                1,
                "post-veto status must record the veto attempt",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_veto_blocked",
                "any_eq",
                1,
                "post-veto status must retain the bot veto block",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_veto_allowed",
                "any_eq",
                0,
                "post-veto status must show the veto was denied",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_veto_picks_after",
                "any_eq",
                0,
                "post-veto status must show zero picks after denial",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_veto_bans_after",
                "any_eq",
                0,
                "post-veto status must show zero bans after denial",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "tournament status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_removed_all",
                "count",
                "eq",
                0,
                "tournament smoke must remove all bots before final status",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest tournament status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "active",
                "eq",
                1,
                "latest tournament status must retain the configured proof state",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_veto_blocked",
                "eq",
                1,
                "latest tournament status must retain the blocked result",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "picks",
                "eq",
                0,
                "latest tournament status must keep zero picks after cleanup",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "bans",
                "eq",
                0,
                "latest tournament status must keep zero bans after cleanup",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest tournament status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=end",
                "final_count",
                "eq",
                0,
                "tournament smoke must finish with zero bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=end",
                "pass",
                "eq",
                1,
                "tournament smoke must finish successfully",
            ),
        ),
        selection_tags=("match", "tournament"),
    ),
    Scenario(
        name="tournament_replay_reset",
        title="Tournament replay reset",
        smoke_mode=3,
        description=(
            "A completed best-of-three tournament history rejects an invalid "
            "replay request without mutation, then replays game 2 and rewinds "
            "series wins and match history to the first game."
        ),
        task_ids=("FR-04-T06", "FR-07-T02", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_tournament_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=begin",
                "target",
                "eq",
                0,
                "tournament replay smoke should not need bot participants",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=begin",
                "replay_reset",
                "eq",
                1,
                "tournament replay smoke must exercise replay reset handling",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=begin",
                "invalid_game",
                "eq",
                99,
                "tournament replay smoke must try an out-of-range replay",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=begin",
                "replay_game",
                "eq",
                2,
                "tournament replay smoke must then replay game 2",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay_setup",
                "configured",
                "eq",
                1,
                "replay setup must configure an active tournament state",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay_setup",
                "order",
                "eq",
                3,
                "replay setup must seed a best-of-three map order",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay_setup",
                "history",
                "eq",
                3,
                "replay setup must seed three completed game records",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay_setup",
                "games_played",
                "eq",
                3,
                "replay setup must start from three games played",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay_setup",
                "player0_wins",
                "eq",
                2,
                "replay setup must start with the home participant at match point",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay_setup",
                "player1_wins",
                "eq",
                1,
                "replay setup must preserve the away participant's prior win",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay_setup",
                "series_complete",
                "eq",
                1,
                "replay setup must prove completed-series replay is covered",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_replay_setup_requested",
                "configured",
                "eq",
                1,
                "server smoke must observe replay setup success",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "games_played",
                "any_eq",
                3,
                "pre-replay tournament status must report the completed history",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "series_complete",
                "any_eq",
                1,
                "pre-replay tournament status must show a completed series",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "match_winners",
                "any_eq",
                3,
                "pre-replay tournament status must retain all winners",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "game",
                "any_eq",
                99,
                "invalid replay attempt must target the out-of-range game",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "success",
                "any_eq",
                0,
                "invalid replay attempt must fail cleanly",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "reason",
                "any_eq",
                "range_error",
                "invalid replay attempt must report a range error",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "preserved",
                "any_eq",
                1,
                "invalid replay attempt must preserve series state",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_replay_invalid_requested",
                "preserved",
                "eq",
                1,
                "server smoke must observe invalid replay preservation",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "game",
                "eq",
                2,
                "latest replay attempt must target game 2",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "success",
                "eq",
                1,
                "valid replay attempt must succeed",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "reason",
                "eq",
                "queued_replay",
                "valid replay attempt must queue the replay map",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "games_before",
                "eq",
                3,
                "valid replay must start from the seeded completed history",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "games_after",
                "eq",
                1,
                "replaying game 2 must rewind games played to game 1",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "winners_after",
                "eq",
                1,
                "replaying game 2 must truncate winners after game 1",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "ids_after",
                "eq",
                1,
                "replaying game 2 must truncate match IDs after game 1",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "maps_after",
                "eq",
                1,
                "replaying game 2 must truncate match maps after game 1",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "player0_wins_after",
                "eq",
                1,
                "replaying game 2 must retain only the first home win",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "player1_wins_after",
                "eq",
                0,
                "replaying game 2 must remove the later away win",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "series_complete_after",
                "eq",
                0,
                "replaying game 2 must reopen the series",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_replay",
                "reset_applied",
                "eq",
                1,
                "valid replay attempt must report reset application",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_replay_valid_requested",
                "reset",
                "eq",
                1,
                "server smoke must observe valid replay reset",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "games_played",
                "eq",
                1,
                "latest tournament status must report the rewound game count",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "series_complete",
                "eq",
                0,
                "latest tournament status must report the reopened series",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "match_winners",
                "eq",
                1,
                "latest tournament status must retain one winner",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_replay_success",
                "eq",
                1,
                "latest tournament status must retain replay success",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "last_replay_reset_applied",
                "eq",
                1,
                "latest tournament status must retain reset proof",
            ),
            MarkerMetricCheck(
                TOURNAMENT_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest tournament status must satisfy replay expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke_removed_all",
                "count",
                "eq",
                0,
                "tournament replay smoke must finish with no bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=end",
                "final_count",
                "eq",
                0,
                "tournament replay smoke must finish with zero bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_tournament_smoke=end",
                "pass",
                "eq",
                1,
                "tournament replay smoke must finish successfully",
            ),
        ),
        selection_tags=("match", "tournament", "replay"),
    ),
    Scenario(
        name="match_logging_schema",
        title="Match logging schema",
        smoke_mode=2,
        description=(
            "Builds sample match-stats and tournament-series artifacts through "
            "the native JSON exporters and verifies stable schema/version "
            "metadata for downstream tooling."
        ),
        task_ids=("FR-07-T03", "DV-03-T05", "FR-04-T16"),
        budget_seconds=15,
        smoke_cvar="sv_bot_matchlog_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_matchlog_smoke=begin",
                "target",
                "eq",
                0,
                "match logging schema smoke should not need bot participants",
            ),
            MarkerMetricCheck(
                "q3a_bot_matchlog_smoke=begin",
                "schema",
                "eq",
                1,
                "match logging smoke must exercise schema validation",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "attempted",
                "eq",
                1,
                "schema status must run",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "match_schema_name",
                "eq",
                "worr.match_stats",
                "match stats artifacts must advertise their schema name",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "match_schema_version",
                "eq",
                1,
                "match stats schema version must be stable",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "match_artifact_type",
                "eq",
                "match_stats",
                "match stats artifacts must advertise their artifact type",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "match_artifact_version",
                "eq",
                1,
                "match stats artifact version must be stable",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "match_has_players_array",
                "eq",
                1,
                "match stats artifacts must keep the players array",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "match_has_event_log_array",
                "eq",
                1,
                "match stats artifacts must retain event-log shape when events exist",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "series_schema_name",
                "eq",
                "worr.tournament_series",
                "tournament series artifacts must advertise their schema name",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "series_schema_version",
                "eq",
                1,
                "tournament series schema version must be stable",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "series_artifact_type",
                "eq",
                "tournament_series",
                "tournament series artifacts must advertise their artifact type",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "series_artifact_version",
                "eq",
                1,
                "tournament series artifact version must be stable",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "series_has_matches_array",
                "eq",
                1,
                "tournament series artifacts must keep the matches array",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "series_match_schema_version",
                "eq",
                1,
                "embedded match artifacts must retain match schema metadata",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_SCHEMA_MARKER,
                "pass",
                "eq",
                1,
                "schema status must pass",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "attempted",
                "eq",
                1,
                "catalog status must run",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "catalog_schema_name",
                "eq",
                "worr.match_catalog",
                "match catalog must advertise its schema name",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "catalog_schema_version",
                "eq",
                1,
                "match catalog schema version must be stable",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "catalog_artifact_type",
                "eq",
                "match_catalog",
                "match catalog must advertise its artifact type",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "catalog_artifact_version",
                "eq",
                1,
                "match catalog artifact version must be stable",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "catalog_artifact_count",
                "eq",
                2,
                "catalog smoke must index match and series artifacts",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "latest_match_stats",
                "eq",
                "schema-smoke-match",
                "catalog must expose the latest match-stats artifact id",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "latest_tournament_series",
                "eq",
                "schema-smoke-series",
                "catalog must expose the latest tournament-series artifact id",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "first_artifact_type",
                "eq",
                "match_stats",
                "catalog first smoke artifact must be match stats",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "first_json_path",
                "eq",
                "schema-smoke-match.json",
                "catalog must keep a relative match JSON path",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "second_artifact_type",
                "eq",
                "tournament_series",
                "catalog second smoke artifact must be tournament series",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "second_json_path",
                "eq",
                "series_schema-smoke-series.json",
                "catalog must keep a relative series JSON path",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "catalog_write_pass",
                "eq",
                1,
                "catalog smoke must exercise the disk update path",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "catalog_write_artifact_count",
                "ge",
                2,
                "written scratch catalog must retain both artifact entries",
            ),
            MarkerMetricCheck(
                MATCH_LOGGING_CATALOG_MARKER,
                "pass",
                "eq",
                1,
                "catalog status must pass",
            ),
            MarkerMetricCheck(
                "q3a_bot_matchlog_smoke_schema_requested",
                "pass",
                "eq",
                1,
                "server smoke must observe schema validation success",
            ),
            MarkerMetricCheck(
                "q3a_bot_matchlog_smoke=end",
                "final_count",
                "eq",
                0,
                "match logging schema smoke must finish with zero bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_matchlog_smoke=end",
                "pass",
                "eq",
                1,
                "match logging schema smoke must finish successfully",
            ),
        ),
        selection_tags=("match", "logging", "schema"),
    ),
    Scenario(
        name="mapvote_bot_exclusion_transition",
        title="Map-vote bot exclusion transition",
        smoke_mode=2,
        description=(
            "A bot-only FFA map selector vote excludes bot ballots, finalizes "
            "a deterministic current-map candidate, observes the reload, and "
            "cleans up retained fake-client state."
        ),
        task_ids=("FR-04-T06", "FR-07-T01", "DV-03-T05", "FR-04-T16"),
        budget_seconds=25,
        smoke_cvar="sv_bot_mapvote_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke=begin",
                "target",
                "eq",
                2,
                "map-vote smoke must request two bot participants",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke=begin",
                "selector",
                "eq",
                1,
                "map-vote smoke must enable the map selector path",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke=begin",
                "bot_vote_block",
                "eq",
                1,
                "map-vote smoke must exercise bot selector-vote exclusion",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_after_add_requests",
                "added_alpha",
                "eq",
                1,
                "first map-vote bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_after_add_requests",
                "added_bravo",
                "eq",
                1,
                "second map-vote bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_after_add_requests",
                "count",
                "ge",
                1,
                "map-vote bots should materialize before selector setup",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "bots",
                "any_eq",
                2,
                "pre-reload map-vote status must count both bots",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "map-vote proof must stay bot-only",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "active",
                "any_eq",
                1,
                "map selector status must become active after setup",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "candidates",
                "any_eq",
                1,
                "map selector status must expose the deterministic candidate",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_begin",
                "success",
                "eq",
                1,
                "game extension must begin the map selector proof",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_begin",
                "active",
                "eq",
                1,
                "begin marker must report an active selector",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_begin",
                "candidates",
                "eq",
                1,
                "begin marker must report one selector candidate",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_bot_vote",
                "bot_found",
                "eq",
                1,
                "map-vote smoke must find a playing bot for exclusion proof",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_bot_vote",
                "blocked",
                "eq",
                1,
                "bot selector vote must be blocked",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_bot_vote",
                "counted",
                "eq",
                0,
                "blocked bot selector vote must not be counted",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_bot_vote",
                "stored_vote",
                "eq",
                -1,
                "blocked bot selector vote must not remain stored",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_bot_vote",
                "reason",
                "eq",
                "bot_blocked",
                "bot selector vote must report the bot-block reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_bot_vote",
                "vote_count0",
                "eq",
                0,
                "bot selector vote must leave candidate zero uncounted",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_bot_vote_requested",
                "blocked",
                "eq",
                1,
                "server smoke must observe the blocked selector vote",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "last_bot_vote_blocked",
                "any_eq",
                1,
                "map-vote status must retain bot vote exclusion",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "bot_votes",
                "any_eq",
                0,
                "map-vote status must keep bot ballots out of counts",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_finalize",
                "success",
                "eq",
                1,
                "map selector finalization must choose a transition target",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_finalize",
                "reason",
                "eq",
                "selected_exit",
                "map selector finalization must report selected-exit reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_finalize",
                "selected_index",
                "eq",
                0,
                "single-candidate map selector proof must select index zero",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_finalize",
                "selected_votes",
                "eq",
                0,
                "bot-only selector proof must finalize with zero counted votes",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_finalize",
                "exit_requested",
                "eq",
                1,
                "map selector finalization must request map exit",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_finalize_requested",
                "success",
                "eq",
                1,
                "server smoke must observe successful map-vote finalization",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "last_finalize_success",
                "any_eq",
                1,
                "map-vote status must retain finalize success",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "last_finalize_reason",
                "any_eq",
                "selected_exit",
                "map-vote status must retain selected-exit reason",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "map-vote status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_reload=queued",
                "timeout_ms",
                "ge",
                1000,
                "map-vote smoke must queue a bounded reload wait",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_reload=observed",
                "new_spawncount",
                "gt",
                0,
                "map-vote smoke must observe the map reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_reload=observed",
                "elapsed_ms",
                "lt",
                10000,
                "map-vote reload should occur within the smoke timeout",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke_removed_all",
                "count",
                "eq",
                0,
                "map-vote smoke must remove all retained bots after reload",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest map-vote status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "active",
                "eq",
                0,
                "latest map-vote status must report inactive selector after cleanup",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "last_bot_vote_blocked",
                "eq",
                1,
                "latest map-vote status must retain bot vote exclusion",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "last_finalize_success",
                "eq",
                1,
                "latest map-vote status must retain finalization success",
            ),
            MarkerMetricCheck(
                MAPVOTE_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest map-vote status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke=end",
                "final_count",
                "eq",
                0,
                "map-vote smoke must finish with zero bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_mapvote_smoke=end",
                "pass",
                "eq",
                1,
                "map-vote smoke must finish successfully",
            ),
        ),
        selection_tags=("match", "votes", "mapvote"),
    ),
    Scenario(
        name="mymap_queue_bot_request",
        title="MyMap bot queue request",
        smoke_mode=2,
        description=(
            "A bot-only FFA participant receives a deterministic test social "
            "ID, queues the active map through the normal MyMap helper, and "
            "then consumes the queued entry so playQueue and myMapQueue cleanup "
            "are both verified."
        ),
        task_ids=("FR-04-T06", "FR-07-T01", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_mymap_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke=begin",
                "target",
                "eq",
                1,
                "mymap smoke must request one bot participant",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke=begin",
                "maps_mymap",
                "eq",
                1,
                "mymap queue smoke must enable map-pool MyMap requests",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke=begin",
                "allow_mymap",
                "eq",
                1,
                "mymap queue smoke must enable the MyMap command path",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke=begin",
                "queue_limit",
                "eq",
                2,
                "mymap queue smoke must run with a bounded queue limit",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke_after_add_request",
                "added",
                "eq",
                1,
                "mymap queue bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke_after_add_request",
                "count",
                "ge",
                1,
                "mymap queue bot should materialize before queueing",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke_status_requested",
                "count",
                "ge",
                1,
                "mymap status must run after the bot is present",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "bots",
                "any_eq",
                1,
                "pre-cleanup MyMap status must count the bot",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "mymap queue proof must stay bot-only",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "playing",
                "any_eq",
                1,
                "the MyMap queue bot must be playing",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "allow_mymap",
                "any_eq",
                1,
                "MyMap status must show the command path enabled",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "maps_mymap",
                "any_eq",
                1,
                "MyMap status must show map-pool MyMap enabled",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "queue_limit",
                "any_eq",
                2,
                "MyMap status must report the smoke queue limit",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "play_queue",
                "any_eq",
                0,
                "initial or cleanup MyMap status must show no play queue",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "mymap_queue",
                "any_eq",
                0,
                "initial or cleanup MyMap status must show no MyMap queue",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_queue",
                "bot_found",
                "eq",
                1,
                "mymap queue smoke must find a playing bot",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_queue",
                "social_assigned",
                "eq",
                1,
                "mymap queue smoke must assign a deterministic bot social ID",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_queue",
                "success",
                "eq",
                1,
                "bot-attributed MyMap request must be queued",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_queue",
                "rejected",
                "eq",
                0,
                "bot-attributed MyMap request must not be rejected",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_queue",
                "reason",
                "eq",
                "queued",
                "MyMap queue marker must report the queued reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_queue",
                "play_queue",
                "eq",
                1,
                "queue marker must show one play queue entry",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_queue",
                "mymap_queue",
                "eq",
                1,
                "queue marker must show one MyMap request entry",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke_queue_requested",
                "success",
                "eq",
                1,
                "server smoke must observe the queued request",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "play_queue",
                "any_eq",
                1,
                "post-queue MyMap status must show one play queue entry",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "mymap_queue",
                "any_eq",
                1,
                "post-queue MyMap status must show one MyMap queue entry",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "last_queue_success",
                "any_eq",
                1,
                "post-queue MyMap status must record success",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_consume",
                "success",
                "eq",
                1,
                "queued MyMap entry must be consumable",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_consume",
                "reason",
                "eq",
                "consumed",
                "consume marker must report the consumed reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_consume",
                "play_queue",
                "eq",
                0,
                "consume marker must empty the play queue",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_consume",
                "mymap_queue",
                "eq",
                0,
                "consume marker must empty the MyMap queue",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke_consume_requested",
                "success",
                "eq",
                1,
                "server smoke must observe consumed queue state",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "last_consume_success",
                "any_eq",
                1,
                "post-consume MyMap status must record success",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "MyMap status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke_removed_all",
                "count",
                "eq",
                0,
                "mymap queue smoke must remove all bots before final status",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest MyMap status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "play_queue",
                "eq",
                0,
                "latest MyMap status must report an empty play queue",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "mymap_queue",
                "eq",
                0,
                "latest MyMap status must report an empty MyMap queue",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest MyMap status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_mymap_smoke=end",
                "final_count",
                "eq",
                0,
                "mymap queue smoke must finish with zero bots",
            ),
        ),
        selection_tags=("match", "mymap"),
    ),
    Scenario(
        name="scoreboard_bot_classification",
        title="Scoreboard bot classification",
        smoke_mode=2,
        description=(
            "Two bot-only FFA participants are classified through the normal "
            "sorted standings path, receive deterministic test scores, and "
            "prove the scoreboard leader and runner-up rows remain bot-owned."
        ),
        task_ids=("FR-04-T06", "FR-07-T01", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_scoreboard_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke=begin",
                "target",
                "eq",
                2,
                "scoreboard smoke must request two bot participants",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke=begin",
                "leader_score",
                "eq",
                7,
                "scoreboard smoke must declare the leader test score",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke=begin",
                "runner_score",
                "eq",
                3,
                "scoreboard smoke must declare the runner-up test score",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke_after_add_requests",
                "added_alpha",
                "eq",
                1,
                "first scoreboard bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke_after_add_requests",
                "added_bravo",
                "eq",
                1,
                "second scoreboard bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke_after_add_requests",
                "count",
                "ge",
                2,
                "both scoreboard bots should materialize before scoring",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "bots",
                "any_eq",
                2,
                "pre-cleanup scoreboard status must count both bots",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "scoreboard proof must stay bot-only",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "playing",
                "any_eq",
                2,
                "scoreboard proof must classify both bots as playing",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "voting_clients",
                "any_eq",
                0,
                "bot-only scoreboard proof must not create voting clients",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "sorted_bots",
                "any_eq",
                2,
                "sorted standings must include both bot rows",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "leader_bot",
                "any_eq",
                1,
                "top scoreboard row must be bot-classified",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_scores",
                "applied",
                "eq",
                1,
                "scoreboard test scores must be applied",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_scores",
                "bot_count",
                "eq",
                2,
                "scoreboard scoring must find two playing bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_scores",
                "leader_score",
                "eq",
                7,
                "scoreboard scoring must set the leader score",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_scores",
                "runner_score",
                "eq",
                3,
                "scoreboard scoring must set the runner-up score",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_scores",
                "reason",
                "eq",
                "applied",
                "scoreboard scoring marker must report the applied reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke_scores_requested",
                "success",
                "eq",
                1,
                "server smoke must observe successful score application",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "top_score",
                "any_eq",
                7,
                "post-score status must show the leader score first",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "second_score",
                "any_eq",
                3,
                "post-score status must show the runner-up score second",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "top_bot",
                "any_eq",
                1,
                "post-score leader row must stay bot-classified",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "second_bot",
                "any_eq",
                1,
                "post-score runner-up row must stay bot-classified",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "score_ordered",
                "any_eq",
                1,
                "scoreboard status must report descending score order",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "rank_ordered",
                "any_eq",
                1,
                "scoreboard status must report ordered FFA ranks",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "last_score_applied",
                "any_eq",
                1,
                "post-score status must record score application",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "scoreboard status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke_removed_all",
                "count",
                "eq",
                0,
                "scoreboard smoke must remove all bots before final status",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest scoreboard status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "sorted_clients",
                "eq",
                0,
                "latest scoreboard status must report no sorted clients",
            ),
            MarkerMetricCheck(
                SCOREBOARD_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest scoreboard status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_scoreboard_smoke=end",
                "final_count",
                "eq",
                0,
                "scoreboard smoke must finish with zero bots",
            ),
        ),
        selection_tags=("match", "scoreboard"),
    ),
    Scenario(
        name="intermission_bot_cleanup",
        title="Intermission bot cleanup",
        smoke_mode=2,
        description=(
            "Two bot-only FFA participants enter the native intermission path, "
            "prove MoveClientToIntermission-owned frozen/freecam state, then "
            "are removed without leaving sorted-client residue."
        ),
        task_ids=("FR-04-T06", "FR-07-T01", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_intermission_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_intermission_smoke=begin",
                "target",
                "eq",
                2,
                "intermission smoke must request two bot participants",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_smoke_after_add_requests",
                "added_alpha",
                "eq",
                1,
                "first intermission bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_smoke_after_add_requests",
                "added_bravo",
                "eq",
                1,
                "second intermission bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_smoke_after_add_requests",
                "count",
                "ge",
                2,
                "both intermission bots should materialize before transition",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "bots",
                "any_eq",
                2,
                "pre-cleanup intermission status must count both bots",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "humans",
                "any_eq",
                0,
                "intermission proof must stay bot-only",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "playing",
                "any_eq",
                2,
                "intermission proof must classify both bots as playing",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "sorted_bots",
                "any_eq",
                2,
                "pre-cleanup sorted standings must include both bot rows",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "intermission",
                "any_eq",
                0,
                "pre-transition status must show intermission is not active",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_begin",
                "success",
                "eq",
                1,
                "game extension must successfully begin intermission",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_begin",
                "reason",
                "eq",
                "begun",
                "intermission begin marker must report the begun reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_begin",
                "intermission",
                "eq",
                1,
                "intermission begin marker must observe active intermission",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_begin",
                "change_map_current",
                "eq",
                1,
                "intermission begin marker must target the current map",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_begin",
                "intermission_bots",
                "eq",
                2,
                "begin marker must show both bots moved to intermission state",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_smoke_begin_requested",
                "success",
                "eq",
                1,
                "server smoke must observe successful intermission begin",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "intermission",
                "any_eq",
                1,
                "post-transition status must show active intermission",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "intermission_bots",
                "any_eq",
                2,
                "post-transition status must count both bots in intermission",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "pm_freeze_bots",
                "any_eq",
                2,
                "post-transition bots must be frozen",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "freecam_bots",
                "any_eq",
                2,
                "post-transition bots must be in freecam movement",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "solid_not_bots",
                "any_eq",
                2,
                "post-transition bots must be non-solid",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "change_map_current",
                "any_eq",
                1,
                "post-transition status must record a current-map target",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "last_begin_success",
                "any_eq",
                1,
                "post-transition status must retain successful begin result",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "last_begin_reason",
                "any_eq",
                "begun",
                "post-transition status must retain begun reason",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "intermission status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_smoke_removed_all",
                "count",
                "eq",
                0,
                "intermission smoke must remove all bots before final status",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest intermission status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "connected_clients",
                "eq",
                0,
                "latest intermission status must report zero connected clients",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "sorted_clients",
                "eq",
                0,
                "latest intermission status must report no sorted clients",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "pm_freeze_bots",
                "eq",
                0,
                "latest intermission status must report no frozen bot clients",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "intermission",
                "eq",
                1,
                "latest intermission status should leave the map in intermission",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "post_intermission",
                "eq",
                0,
                "cleanup must happen before post-intermission map exit",
            ),
            MarkerMetricCheck(
                INTERMISSION_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest intermission status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_intermission_smoke=end",
                "final_count",
                "eq",
                0,
                "intermission smoke must finish with zero bots",
            ),
        ),
        selection_tags=("match", "intermission"),
    ),
    Scenario(
        name="queued_nextmap_transition",
        title="Queued nextmap transition",
        smoke_mode=2,
        description=(
            "A bot-attributed MyMap request is consumed by the nextmap "
            "transition path, the dedicated server observes the resulting map "
            "reload, and any retained fake-client state is cleaned up."
        ),
        task_ids=("FR-04-T06", "FR-07-T01", "DV-03-T05", "FR-04-T16"),
        budget_seconds=25,
        smoke_cvar="sv_bot_nextmap_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke=begin",
                "target",
                "eq",
                1,
                "nextmap smoke must request one bot participant",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke=begin",
                "maps_mymap",
                "eq",
                1,
                "nextmap smoke must enable MyMap map selection",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_after_add_request",
                "added",
                "eq",
                1,
                "nextmap bot add request must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_after_add_request",
                "count",
                "ge",
                1,
                "nextmap bot should materialize before queueing",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_queue_requested",
                "success",
                "eq",
                1,
                "nextmap smoke must queue a bot-attributed map request",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "play_queue",
                "any_eq",
                1,
                "MyMap status must show the queued play entry before transition",
            ),
            MarkerMetricCheck(
                MYMAP_STATUS_MARKER,
                "mymap_queue",
                "any_eq",
                1,
                "MyMap status must show the queued MyMap entry before transition",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "play_queue",
                "any_eq",
                1,
                "nextmap status must see the queued play entry before transition",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_transition",
                "success",
                "eq",
                1,
                "game extension must successfully request the nextmap transition",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_transition",
                "consumed",
                "eq",
                1,
                "nextmap transition must consume the queued map",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_transition",
                "reason",
                "eq",
                "queued_exit",
                "nextmap transition marker must report the queued-exit reason",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_transition",
                "play_queue_before",
                "eq",
                1,
                "nextmap transition must start with one play-queue entry",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_transition",
                "play_queue_after",
                "eq",
                0,
                "nextmap transition must clear the play queue",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_transition",
                "mymap_queue_after",
                "eq",
                0,
                "nextmap transition must clear the MyMap queue",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_transition_requested",
                "success",
                "eq",
                1,
                "server smoke must observe successful transition request",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "last_transition_success",
                "any_eq",
                1,
                "nextmap status must retain transition success",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "last_transition_consumed",
                "any_eq",
                1,
                "nextmap status must retain queue consumption",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "last_transition_reason",
                "any_eq",
                "queued_exit",
                "nextmap status must retain queued-exit reason",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "play_queue",
                "any_eq",
                0,
                "nextmap status must report an empty play queue after transition",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "mymap_queue",
                "any_eq",
                0,
                "nextmap status must report an empty MyMap queue after transition",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "pass",
                "any_eq",
                1,
                "nextmap status rows must satisfy their expectations",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_reload=queued",
                "timeout_ms",
                "ge",
                1000,
                "nextmap smoke must queue a bounded reload wait",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_reload=observed",
                "new_spawncount",
                "gt",
                0,
                "nextmap smoke must observe the map reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_reload=observed",
                "elapsed_ms",
                "lt",
                10000,
                "nextmap reload should occur within the smoke timeout",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke_removed_all",
                "count",
                "eq",
                0,
                "nextmap smoke must remove all retained bots after reload",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest nextmap status must report zero bots after cleanup",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "play_queue",
                "eq",
                0,
                "latest nextmap status must report an empty play queue",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "mymap_queue",
                "eq",
                0,
                "latest nextmap status must report an empty MyMap queue",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "last_transition_success",
                "eq",
                1,
                "latest nextmap status must retain transition success",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "last_transition_consumed",
                "eq",
                1,
                "latest nextmap status must retain queue consumption",
            ),
            MarkerMetricCheck(
                NEXTMAP_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest nextmap status must pass after cleanup",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke=end",
                "final_count",
                "eq",
                0,
                "nextmap smoke must finish with zero bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_nextmap_smoke=end",
                "pass",
                "eq",
                1,
                "nextmap smoke must finish successfully",
            ),
        ),
        selection_tags=("match", "nextmap"),
    ),
    Scenario(
        name="profile_backed_spawn",
        title="Profile-backed bot spawn",
        smoke_mode=2,
        description=(
            "Loads the staged smoke profile, spawns it through sg_bot_add, "
            "and verifies the bridged profile/userinfo fields."
        ),
        task_ids=("FR-04-T13", "DV-03-T05"),
        budget_seconds=20,
        smoke_cvar="sv_bot_profile_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_profile_smoke=begin",
                "profiles",
                "ge",
                1,
                "at least one profile must load from the staged install",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke=begin",
                "found",
                "eq",
                1,
                "the smoke profile asset must resolve",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add_request",
                "added",
                "eq",
                1,
                "profile-backed bot add must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add_request",
                "count",
                "eq",
                1,
                "the accepted add request must create one bot",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "count",
                "eq",
                1,
                "exactly one profile-backed bot should be present after spawn",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "name",
                "eq",
                "B|Smoke",
                "profile display name must bridge into the bot slot",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "profile",
                "eq",
                "smoke",
                "bot userinfo must retain the source profile id",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "skin",
                "eq",
                "male/grunt",
                "profile skin must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "skill",
                "eq",
                4,
                "profile skill must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "reaction",
                "eq",
                250,
                "reaction metadata must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "aggression",
                "eq",
                0.65,
                "aggression metadata must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "aim_error",
                "eq",
                2.5,
                "aim-error metadata must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "preferred_weapon",
                "eq",
                "rocketlauncher",
                "preferred-weapon metadata must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "chat",
                "eq",
                "quiet",
                "chat metadata must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "role",
                "eq",
                "attacker",
                "role metadata must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_add",
                "movement",
                "eq",
                "strafe",
                "movement metadata must bridge into userinfo",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke_after_remove_all",
                "count",
                "eq",
                0,
                "profile smoke cleanup must remove all bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_profile_smoke=end",
                "final_count",
                "eq",
                0,
                "profile smoke must end with no bots left behind",
            ),
        ),
    ),
    Scenario(
        name="team_policy_duel_readiness",
        title="Team-policy duel readiness",
        smoke_mode=2,
        description=(
            "Runs the existing team-policy smoke and verifies bot active/spectator "
            "accounting before and after cleanup."
        ),
        task_ids=("FR-04-T04", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_team_policy_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_after_add_requests",
                "added_alpha",
                "eq",
                1,
                "first bot add must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_after_add_requests",
                "added_bravo",
                "eq",
                1,
                "second bot add must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_after_add_requests",
                "added_charlie",
                "eq",
                1,
                "queued third bot add must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_status_requested",
                "count",
                "ge",
                3,
                "team-policy status must run after all three bots are present",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "bots",
                "any_eq",
                3,
                "pre-cleanup team policy status must count all three bots",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "playing",
                "any_eq",
                2,
                "duel policy must keep two bots playing",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "spectators",
                "any_eq",
                1,
                "duel policy must move one surplus bot to spectators",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "queued",
                "any_eq",
                0,
                "queue-disabled duel readiness must leave the surplus spectator unqueued",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest cleanup status must report zero bots",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "queued",
                "eq",
                0,
                "latest cleanup status must report zero queued bots",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest team policy status must pass",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_removed_all",
                "count",
                "eq",
                0,
                "team-policy smoke cleanup must remove all bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke=end",
                "final_count",
                "eq",
                0,
                "team-policy smoke must end with no bots left behind",
            ),
        ),
    ),
    Scenario(
        name="duel_queue_spectator",
        title="Duel queue spectator",
        smoke_mode=3,
        description=(
            "Runs the queue-enabled team-policy smoke and verifies the surplus "
            "duel bot remains spectator-owned while entering the match queue."
        ),
        task_ids=("FR-04-T04", "DV-03-T05", "FR-04-T16"),
        budget_seconds=20,
        smoke_cvar="sv_bot_team_policy_smoke",
        marker_checks=(
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke=begin",
                "queue_enabled",
                "eq",
                1,
                "queue-enabled smoke mode must enable duel queueing",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_after_add_requests",
                "added_alpha",
                "eq",
                1,
                "first bot add must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_after_add_requests",
                "added_bravo",
                "eq",
                1,
                "second bot add must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_after_add_requests",
                "added_charlie",
                "eq",
                1,
                "queued third bot add must be accepted",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_status_requested",
                "count",
                "ge",
                3,
                "duel queue status must run after all three bots are present",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "bots",
                "any_eq",
                3,
                "pre-cleanup duel queue status must count all three bots",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "playing",
                "any_eq",
                2,
                "duel queue policy must keep two bots playing",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "spectators",
                "any_eq",
                1,
                "duel queue policy must move one surplus bot to spectator",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "queued",
                "any_eq",
                1,
                "duel queue policy must queue the surplus spectator bot",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "expected_queued",
                "any_eq",
                1,
                "duel queue status must assert the expected queued count",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                0,
                "latest cleanup status must report zero bots",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "queued",
                "eq",
                0,
                "latest cleanup status must report zero queued bots",
            ),
            MarkerMetricCheck(
                TEAM_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "latest duel queue policy status must pass",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke_removed_all",
                "count",
                "eq",
                0,
                "duel queue smoke cleanup must remove all bots",
            ),
            MarkerMetricCheck(
                "q3a_bot_team_policy_smoke=end",
                "final_count",
                "eq",
                0,
                "duel queue smoke must end with no bots left behind",
            ),
        ),
    ),
    Scenario(
        name="engage_enemy",
        title="Engage enemy",
        smoke_mode=20,
        description="Bot selects an enemy target and emits attack intent.",
        task_ids=("DV-03-T05",),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "combat smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                20,
                combat="engage_enemy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            *marker_metric_checks(
                ACTION_STATUS_MARKER,
            MetricCheck("combat_enemy_acquisitions", "ge", 1, "bot must select a live enemy"),
            MetricCheck("combat_enemy_visible", "ge", 1, "enemy must pass visibility"),
            MetricCheck("combat_enemy_shootable", "ge", 1, "enemy must pass shootability"),
            MetricCheck("combat_fire_decisions", "ge", 1, "combat policy must decide to fire"),
            MetricCheck("action_attack_decisions", "ge", 1, "action layer must select attack intent"),
            MetricCheck("action_applied_attack_buttons", "ge", 1, "command must apply BUTTON_ATTACK"),
            MetricCheck("combat_damage_events", "ge", 1, "target must take attributed bot damage"),
            MetricCheck("combat_withheld_fire", "eq", 0, "live firing proof must not withhold fire"),
            MetricCheck("last_combat_enemy_client", "ge", 0, "enemy client index must be recorded"),
            MetricCheck("last_combat_damage", "ge", 1, "last attributed damage must be positive"),
            ),
            *marker_metric_checks(
                ACTION_DETAIL_STATUS_MARKER,
            MetricCheck("combat_evaluations", "ge", 1, "combat detail status must evaluate"),
            MetricCheck("combat_fire_decisions", "ge", 1, "combat detail status must decide to fire"),
            MetricCheck("action_applied_cmds", "ge", 1, "action detail must apply commands"),
            MetricCheck("action_applied_attack_buttons", "ge", 1, "action detail must apply attack"),
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "action_last_intent_name",
                "eq",
                "attack",
                "last detailed action intent must be attack",
            ),
        ),
    ),
    Scenario(
        name="switch_weapons",
        title="Switch weapons",
        smoke_mode=21,
        description="Bot evaluates weapon inventory and switches to a preferred weapon.",
        task_ids=("DV-03-T05",),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                21,
                combat="switch_weapons",
                weapon_switch=1,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            *marker_metric_checks(
                ACTION_STATUS_MARKER,
            MetricCheck(
                "combat_weapon_switch_decisions",
                "ge",
                1,
                "combat policy must choose a better weapon",
            ),
            MetricCheck(
                "action_weapon_switch_decisions",
                "ge",
                1,
                "action layer must select weapon-switch intent",
            ),
            MetricCheck(
                "action_pending_weapon_switches",
                "ge",
                1,
                "action application must request a pending weapon switch",
            ),
            MetricCheck("weapon_switch_requests", "ge", 1, "weapon switch request must be submitted"),
            MetricCheck("weapon_switch_completions", "ge", 1, "weapon system must complete the switch"),
            MetricCheck("weapon_switch_failures", "eq", 0, "weapon switch must not fail"),
            MetricCheck("weapon_switch_expected_item", "ge", 1, "expected weapon item id must be reported"),
            MetricCheck("weapon_switch_actual_item", "ge", 1, "actual weapon item id must be reported"),
            MetricCheck("weapon_switch_expected_match", "eq", 1, "actual weapon must match expected weapon"),
            ),
            *marker_metric_checks(
                ACTION_DETAIL_STATUS_MARKER,
            MetricCheck(
                "action_command_request_dispatch_attempts",
                "ge",
                1,
                "weapon-switch detail must attempt command dispatch",
            ),
            MetricCheck(
                "action_weapon_command_requests",
                "ge",
                1,
                "weapon-switch detail must build weapon command requests",
            ),
            MetricCheck(
                "action_command_request_submitted",
                "ge",
                1,
                "weapon-switch detail must submit command requests",
            ),
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "action_last_command_dispatch_outcome_name",
                "eq",
                "submitted",
                "last weapon-switch dispatch outcome must be submitted",
            ),
        ),
    ),
    Scenario(
        name="health_armor_pickup",
        title="Health/armor pickup",
        smoke_mode=22,
        description="Bot prioritizes health or armor after taking damage.",
        task_ids=("DV-03-T05",),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "pickup smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                22,
                combat=0,
                weapon_switch=0,
                item_focus="health_armor",
                team_objective=0,
                target=1,
                gametype=0,
            ),
            *marker_metric_checks(
                ACTION_STATUS_MARKER,
            MetricCheck("item_low_health_boosts", "ge", 1, "item scoring must boost low health"),
            MetricCheck("item_low_armor_boosts", "ge", 1, "item scoring must boost low armor"),
            MetricCheck("item_health_goal_assignments", "ge", 1, "health item goal must be assigned"),
            MetricCheck("item_armor_goal_assignments", "ge", 1, "armor item goal must be assigned"),
            MetricCheck("item_health_pickups", "ge", 1, "health pickup must complete"),
            MetricCheck("item_armor_pickups", "ge", 1, "armor pickup must complete"),
            MetricCheck("last_health_pickup_delta", "ge", 1, "health pickup delta must be positive"),
            MetricCheck("last_armor_pickup_delta", "ge", 1, "armor pickup delta must be positive"),
            ),
            *marker_metric_checks(
                ACTION_DETAIL_STATUS_MARKER,
            MetricCheck("item_evaluations", "ge", 1, "item detail status must evaluate candidates"),
            MetricCheck("item_focus_health_boosts", "ge", 1, "health focus must be visible in detail status"),
            MetricCheck("item_focus_armor_boosts", "ge", 1, "armor focus must be visible in detail status"),
            MetricCheck("item_health_seek_decisions", "ge", 1, "health seek decisions must be visible"),
            MetricCheck("item_armor_seek_decisions", "ge", 1, "armor seek decisions must be visible"),
            ),
        ),
    ),
    Scenario(
        name="team_objective",
        title="Team objective",
        smoke_mode=23,
        description="Bot chooses and pursues a team objective.",
        task_ids=("DV-03-T05",),
        budget_seconds=20,
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "team objective smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                23,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=1,
                target=4,
                gametype=1,
            ),
            *marker_metric_checks(
                OBJECTIVE_STATUS_MARKER,
            MetricCheck("team_objective_evaluations", "ge", 1, "team objective policy must evaluate"),
            MetricCheck("team_objective_assignments", "ge", 1, "bot must receive an objective"),
            MetricCheck("team_objective_route_requests", "ge", 1, "objective must request route planning"),
            MetricCheck("team_objective_route_commands", "ge", 1, "objective route must emit commands"),
            MetricCheck("team_objective_reaches", "ge", 1, "bot must reach the objective"),
            MetricCheck("team_objective_flag_pickups", "ge", 1, "bot must pick up the objective flag"),
            MetricCheck(
                "team_objective_role_policy_evaluations",
                "ge",
                1,
                "team role policy must evaluate for the CTF smoke",
            ),
            MetricCheck(
                "team_objective_role_policy_selections",
                "ge",
                1,
                "team role policy must select a role",
            ),
            MetricCheck(
                "team_objective_role_policy_lane_midfield_selections",
                "ge",
                1,
                "CTF smoke should expose the current midfield lane selection",
            ),
            MetricCheck(
                "team_objective_enemy_flag_assignments",
                "ge",
                1,
                "CTF smoke must assign the enemy-flag objective",
            ),
            MetricCheck("last_team_objective_type", "eq", 1, "first promoted objective is enemy flag pickup"),
            MetricCheck("last_team_objective_role", "ge", 1, "last objective role must be recorded"),
            MetricCheck("last_team_objective_lane", "ge", 1, "last objective lane must be recorded"),
            MetricCheck("last_team_objective_client", "ge", 0, "objective client index must be recorded"),
            MetricCheck("last_team_objective_item", "ge", 1, "objective item id must be reported"),
            MetricCheck("last_team_objective_area", "gt", 0, "objective area must be reported"),
            MetricCheck(
                "team_objective_match_policy_evaluations",
                "ge",
                1,
                "team-objective smoke must exercise match policy evaluation",
            ),
            MetricCheck(
                "team_objective_match_policy_ffa",
                "ge",
                1,
                "team-objective smoke must exercise the FFA match policy lane",
            ),
            MetricCheck(
                "team_objective_match_policy_attack",
                "ge",
                1,
                "team-objective smoke must choose an attack-side match policy",
            ),
            ),
        ),
    ),
    Scenario(
        name="aim_fairness_policy_integration",
        title="Aim fairness policy integration",
        smoke_mode=24,
        description=(
            "Bot engages a live enemy through the aim/fairness policy lane and "
            "proves firing is allowed by policy, not raw visibility alone."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-07-T06"),
        budget_seconds=20,
        selection_tags=("policy", "combat", "aim"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "aim fairness proof must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                24,
                combat="engage_enemy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "aim_fairness",
                "eq",
                1,
                "reserved smoke must enable the aim/fairness proof lane",
            ),
            *marker_metric_checks(
                ACTION_STATUS_MARKER,
                MetricCheck(
                    "aim_policy_evaluations",
                    "ge",
                    1,
                    "live firing must evaluate the aim policy",
                ),
                MetricCheck(
                    "aim_policy_fire_allowed",
                    "ge",
                    1,
                    "at least one live fire decision must pass fairness policy",
                ),
                MetricCheck(
                    "combat_withheld_fire",
                    "eq",
                    0,
                    "aim fairness proof must not end by withholding fire",
                ),
                MetricCheck(
                    "action_applied_attack_buttons",
                    "ge",
                    1,
                    "aim fairness proof must still apply attack input",
                ),
                MetricCheck(
                    "live_aim_evaluations",
                    "ge",
                    1,
                    "brain-owned view aiming must consume the live aim helper",
                ),
                MetricCheck(
                    "live_aim_fire_allowed",
                    "ge",
                    1,
                    "live aim policy must eventually allow firing",
                ),
                MetricCheck(
                    "last_live_aim_weapon",
                    "ge",
                    1,
                    "live aim proof must record the weapon used for aiming",
                ),
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_aim_policy_failure_name",
                "eq",
                "none",
                "successful live firing proof should end with no aim-policy block",
            ),
        ),
    ),
    Scenario(
        name="item_timer_fairness_signals",
        title="Item timer fairness signals",
        smoke_mode=25,
        description=(
            "Runs a deterministic observed-pickup timer proof and verifies the "
            "item timing policy emits fairness-aware allowance telemetry."
        ),
        task_ids=("FR-04-T15", "DV-07-T06"),
        budget_seconds=20,
        selection_tags=("policy", "items", "fairness"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "item-timer proof must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                25,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=1,
                gametype=0,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "item_timer",
                "eq",
                1,
                "reserved smoke must enable the item-timer proof lane",
            ),
            *marker_metric_checks(
                ACTION_DETAIL_STATUS_MARKER,
                MetricCheck(
                    "item_timer_evaluations",
                    "ge",
                    1,
                    "item timer policy must evaluate at least one pickup",
                ),
                MetricCheck(
                    "item_timer_allowed_uses",
                    "ge",
                    1,
                    "item timer policy must allow at least one fair timer use",
                ),
                MetricCheck(
                    "item_timing_consumer_evaluations",
                    "ge",
                    1,
                    "item timing consumer must evaluate at least one pickup",
                ),
                MetricCheck(
                    ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC,
                    "ge",
                    1,
                    "item timing consumer must report a ready timer or live pickup",
                ),
                MetricCheck(
                    "item_timer_fairness_blocks",
                    "ge",
                    0,
                    "item timer fairness block counter must be present",
                ),
                MetricCheck(
                    "last_item_timer_allowed",
                    "eq",
                    1,
                    "last timer decision should be an allowed observed timer",
                ),
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "last_item_timer_reason",
                "eq",
                "exact_timer",
                "deterministic timer proof should use the exact-timer path",
            ),
        ),
    ),
    Scenario(
        name="trace_checked_corner_cutting",
        title="Trace-checked corner cutting",
        smoke_mode=21,
        description=(
            "Runs a route-rich reserved smoke and verifies corner-cut candidates, "
            "trace checks, accepted shortcuts, and backing BSP trace telemetry."
        ),
        task_ids=("FR-04-T14", "FR-04-T16", "DV-07-T06"),
        budget_seconds=20,
        selection_tags=("nav", "corner_cutting", "trace"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "corner-cut proof must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                21,
                combat="switch_weapons",
                weapon_switch=1,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            *marker_metric_checks(
                NAV_POLICY_STATUS_MARKER,
                MetricCheck(
                    "route_corner_cut_candidates",
                    "ge",
                    1,
                    "corner-cut policy must inspect at least one candidate",
                ),
                MetricCheck(
                    "route_corner_cut_trace_checks",
                    "ge",
                    1,
                    "corner-cut policy must perform clearance traces",
                ),
                MetricCheck(
                    "route_corner_cut_trace_hits",
                    "ge",
                    1,
                    "corner-cut policy must observe at least one clear trace",
                ),
                MetricCheck(
                    "route_corner_cut_ground_trace_checks",
                    "ge",
                    1,
                    "accepted walk shortcuts must be backed by ground probes",
                ),
                MetricCheck(
                    "route_corner_cut_accepted",
                    "ge",
                    1,
                    "at least one corner cut must be accepted after tracing",
                ),
                MetricCheck(
                    "trace_checked_corner_cut_accepted",
                    "ge",
                    1,
                    "trace-checked alias must report accepted corner cuts",
                ),
            ),
            MarkerMetricCheck(
                SOURCE_STATUS_MARKER,
                "bsp_trace_calls",
                "ge",
                1,
                "corner-cut proof should be backed by BSP trace counters",
            ),
        ),
    ),
    Scenario(
        name="ffa_tdm_match_readiness",
        title="FFA/TDM match readiness",
        smoke_mode=26,
        description=(
            "Runs a multi-bot match-readiness smoke and verifies the dedicated "
            "source proof reports both FFA and TDM readiness gates."
        ),
        task_ids=("FR-04-T04", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        selection_tags=("match", "ffa", "tdm"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "match-readiness proof must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                26,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "match_readiness",
                "eq",
                1,
                "reserved smoke must enable the match-readiness proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "proof",
                "eq",
                1,
                "match readiness status must come from the dedicated proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "ffa_pass",
                "eq",
                1,
                "FFA readiness status must pass",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "TDM readiness status must pass",
            ),
            *marker_metric_checks(
                OBJECTIVE_STATUS_MARKER,
                MetricCheck(
                    "team_objective_match_policy_evaluations",
                    "ge",
                    1,
                    "TDM readiness smoke must exercise match policy evaluation",
                ),
                MetricCheck(
                    "team_objective_match_policy_tdm",
                    "ge",
                    1,
                    "TDM readiness smoke must choose the team-deathmatch lane",
                ),
                MetricCheck(
                    "team_objective_match_policy_midfield",
                    "ge",
                    1,
                    "TDM readiness smoke must distribute midfield policy roles",
                ),
                MetricCheck(
                    "team_objective_match_policy_friendly_fire",
                    "ge",
                    1,
                    "TDM readiness smoke must evaluate friendly-fire policy",
                ),
            ),
        ),
    ),
    Scenario(
        name="ffa_roam_route",
        title="FFA roam route",
        smoke_mode=42,
        description=(
            "Runs a four-bot FFA smoke with sg_bot_ffa_roam_route enabled "
            "and verifies live FFA roam/collect/engage policy can own timed "
            "route commands."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_ffa_roam_route", "1"),
        ),
        selection_tags=("match", "ffa", "roles", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "FFA roam route smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "FFA roam route smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                42,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_roam_route",
                "eq",
                1,
                "reserved smoke must enable the FFA roam-route proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "ffa_pass",
                "eq",
                1,
                "FFA roam route smoke must run in an FFA-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                0,
                "FFA roam route smoke must not enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ffa",
                "ge",
                1,
                "FFA roam route smoke must select FFA match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_remaining_ms",
                "gt",
                0,
                "FFA roam route smoke must end on an active FFA route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_requests",
                "ge",
                1,
                "FFA roam route cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_policy_selections",
                "ge",
                1,
                "FFA roam route smoke must observe a valid FFA match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_activations",
                "ge",
                1,
                "FFA roam route smoke must activate the timed route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_route_requests",
                "ge",
                1,
                "FFA roam route smoke must route through the timed owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_invalid_skips",
                "eq",
                0,
                "FFA roam route smoke must not record invalid policy skips",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_mode",
                "eq",
                1,
                "FFA roam route smoke must record FFA mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_role",
                "ge",
                1,
                "FFA roam route smoke must record a selected role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_lane",
                "ge",
                1,
                "FFA roam route smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_goal_distance_sq",
                "gt",
                0,
                "FFA roam route smoke must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="ffa_spawn_camp_avoidance",
        title="FFA spawn-camp avoidance",
        smoke_mode=45,
        description=(
            "Runs a four-bot FFA smoke with sg_bot_ffa_roam_route and "
            "sg_bot_ffa_spawn_camp_avoidance enabled, then verifies the FFA "
            "anti-camping policy can choose a nearby player as the timed "
            "route source and route away from that source."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_ffa_roam_route", "1"),
            ("sg_bot_ffa_spawn_camp_avoidance", "1"),
        ),
        selection_tags=("match", "ffa", "roles", "routing", "anti-camp"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "FFA anti-camp smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "FFA anti-camp smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                45,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_roam_route",
                "eq",
                1,
                "reserved smoke must enable the FFA roam-route owner",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_spawn_camp_avoidance",
                "eq",
                1,
                "reserved smoke must enable the FFA anti-camp proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "ffa_pass",
                "eq",
                1,
                "FFA anti-camp smoke must run in an FFA-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                0,
                "FFA anti-camp smoke must not enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ffa",
                "ge",
                1,
                "FFA anti-camp smoke must select FFA match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_policy_avoid",
                "eq",
                1,
                "FFA anti-camp smoke must end on an active avoidance policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_activations",
                "ge",
                1,
                "FFA anti-camp smoke must still activate the FFA route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_route_requests",
                "ge",
                1,
                "FFA anti-camp smoke must route through the FFA timed owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_requests",
                "ge",
                1,
                "FFA anti-camp cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_policy_selections",
                "ge",
                1,
                "FFA anti-camp smoke must observe valid FFA policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_source_selections",
                "ge",
                1,
                "FFA anti-camp smoke must choose a nearby source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_activations",
                "ge",
                1,
                "FFA anti-camp smoke must activate from the nearby source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_route_requests",
                "ge",
                1,
                "FFA anti-camp smoke must produce route requests",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_invalid_skips",
                "eq",
                0,
                "FFA anti-camp smoke must not reject valid FFA policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_source_client",
                "ge",
                0,
                "FFA anti-camp smoke must record a client source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_source_entity",
                "ge",
                0,
                "FFA anti-camp smoke must record a source entity",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_source_distance_sq",
                "gt",
                0,
                "FFA anti-camp smoke must record source distance",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_source_distance_sq",
                "lt",
                147456,
                "FFA anti-camp smoke must source from inside the 384-unit camp radius",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_policy_avoid",
                "eq",
                1,
                "FFA anti-camp smoke must record the FFA avoid-spawn-camping policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_goal_distance_sq",
                "gt",
                0,
                "FFA anti-camp smoke must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="team_role_route",
        title="Team role route",
        smoke_mode=32,
        description=(
            "Runs a four-bot TDM smoke with sg_bot_team_role_route enabled "
            "and verifies live match role/lane policy can own timed route "
            "commands."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_team_role_route", "1"),
        ),
        selection_tags=("match", "tdm", "roles", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "team-role route smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "team-role route smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                32,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_route",
                "eq",
                1,
                "reserved smoke must enable the team-role route proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "team-role route smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "team-role route smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "team-role route smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_remaining_ms",
                "gt",
                0,
                "team-role route smoke must end on an active team-role route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_requests",
                "ge",
                1,
                "team-role route cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_policy_selections",
                "ge",
                1,
                "team-role route smoke must observe a valid match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_activations",
                "ge",
                1,
                "team-role route smoke must activate the timed route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_route_requests",
                "ge",
                1,
                "team-role route smoke must route through the timed owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_mode",
                "eq",
                2,
                "team-role route smoke must record TDM mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_role",
                "ge",
                1,
                "team-role route smoke must record a selected role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_lane",
                "ge",
                1,
                "team-role route smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_goal_distance_sq",
                "gt",
                0,
                "team-role route smoke must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="ctf_role_route",
        title="CTF role route",
        smoke_mode=35,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_role_route enabled "
            "and verifies CTF match role/lane policy can own timed route "
            "commands."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_role_route", "1"),
        ),
        selection_tags=("match", "ctf", "roles", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF-role route smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF-role route smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                35,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_role_route",
                "eq",
                1,
                "reserved smoke must enable the CTF role-route proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF-role route smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF-role route smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ctf",
                "ge",
                1,
                "CTF-role route smoke must select CTF match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_route_remaining_ms",
                "gt",
                0,
                "CTF-role route smoke must end on an active CTF-role route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_requests",
                "ge",
                1,
                "CTF-role route cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_policy_selections",
                "ge",
                1,
                "CTF-role route smoke must observe a valid CTF match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_activations",
                "ge",
                1,
                "CTF-role route smoke must activate the timed route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_route_requests",
                "ge",
                1,
                "CTF-role route smoke must route through the timed owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_route_mode",
                "eq",
                3,
                "CTF-role route smoke must record CTF mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_route_role",
                "ge",
                1,
                "CTF-role route smoke must record a selected role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_route_lane",
                "ge",
                1,
                "CTF-role route smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_route_goal_distance_sq",
                "gt",
                0,
                "CTF-role route smoke must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="team_role_combat",
        title="Team role combat",
        smoke_mode=43,
        description=(
            "Runs a four-bot TDM smoke with sg_bot_team_role_combat enabled "
            "and verifies TDM match role/lane policy can own a live attack "
            "decision from visible, shootable enemy facts."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_team_role_combat", "1"),
        ),
        selection_tags=("match", "tdm", "roles", "combat"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "team-role combat smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "team-role combat smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                43,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_combat",
                "eq",
                1,
                "reserved smoke must enable the team role-combat proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "team-role combat smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "team-role combat smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "team-role combat smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_requests",
                "ge",
                1,
                "team-role combat cvar must request combat ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_policy_selections",
                "ge",
                1,
                "team-role combat smoke must observe a valid TDM match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_target_selections",
                "ge",
                1,
                "team-role combat smoke must select a visible target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_attack_decisions",
                "ge",
                1,
                "team-role combat smoke must own an attack decision",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_invalid_skips",
                "eq",
                0,
                "team-role combat smoke must not reject valid TDM policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_mode",
                "eq",
                2,
                "team-role combat smoke must record TDM mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_role",
                "ge",
                1,
                "team-role combat smoke must record a selected role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_lane",
                "ge",
                1,
                "team-role combat smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_target_client",
                "ge",
                0,
                "team-role combat smoke must record a client target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_target_visible",
                "eq",
                1,
                "team-role combat smoke must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_target_shootable",
                "eq",
                1,
                "team-role combat smoke must record shootable target facts",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_applied_attack_buttons",
                "ge",
                1,
                "team-role combat smoke must apply attack input",
            ),
        ),
    ),
    Scenario(
        name="team_role_combat_avoidance",
        title="Team role combat avoidance",
        smoke_mode=44,
        description=(
            "Runs a four-bot TDM smoke with sg_bot_team_role_combat and "
            "sg_bot_team_fire_avoidance enabled together, verifies role/lane "
            "policy owns a live attack decision, and verifies friendly-fire "
            "avoidance can suppress blocked BUTTON_ATTACK application."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_team_role_combat", "1"),
            ("sg_bot_team_fire_avoidance", "1"),
        ),
        selection_tags=("match", "tdm", "roles", "combat", "friendly-fire"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "team-role combat avoidance smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "team-role combat avoidance smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                44,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_combat",
                "eq",
                1,
                "reserved smoke must enable the team role-combat proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_fire_avoidance",
                "eq",
                1,
                "reserved smoke must enable the team-fire avoidance proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "team-role combat avoidance smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "team-role combat avoidance smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "team-role combat avoidance smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_friendly_fire_policy_evaluations",
                "ge",
                1,
                "team-role combat avoidance smoke must evaluate friendly-fire policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_friendly_fire_avoidance",
                "ge",
                1,
                "team-role combat avoidance smoke must record friendly-fire avoidance",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_requests",
                "ge",
                1,
                "team-role combat cvar must request combat ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_policy_selections",
                "ge",
                1,
                "team-role combat avoidance smoke must observe a valid TDM match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_target_selections",
                "ge",
                1,
                "team-role combat avoidance smoke must select a visible target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_attack_decisions",
                "ge",
                1,
                "team-role combat avoidance smoke must own an attack decision before suppression",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_invalid_skips",
                "eq",
                0,
                "team-role combat avoidance smoke must not reject valid TDM policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_mode",
                "eq",
                2,
                "team-role combat avoidance smoke must record TDM mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_target_visible",
                "eq",
                1,
                "team-role combat avoidance smoke must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_target_shootable",
                "eq",
                1,
                "team-role combat avoidance smoke must record shootable target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_fire_avoidance_evaluations",
                "ge",
                1,
                "team-fire cvar must evaluate role-combat attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_fire_avoidance_blocks",
                "ge",
                1,
                "team-fire cvar must block role-combat attack input",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_fire_avoidance_line_blocks",
                "ge",
                1,
                "team-fire cvar must block for a friendly line-of-fire",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_fire_avoidance_friendly_line",
                "eq",
                1,
                "team-role combat avoidance smoke must record a friendly line-of-fire",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_fire_avoidance_blocked",
                "eq",
                1,
                "team-role combat avoidance smoke must record final attack suppression",
            ),
        ),
    ),
    Scenario(
        name="tdm_role_spawn_stability",
        title="TDM role spawn stability",
        smoke_mode=73,
        description=(
            "Runs a four-bot TDM role route/combat proof across a forced "
            "same-map restart. The scenario verifies role-route owners, "
            "role-combat target ownership, post-restart status, and cleanup "
            "survive the spawncount transition."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=45,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sv_bot_frame_command_smoke_map_repeat_cycles", "2"),
            ("sv_bot_frame_command_smoke_map_repeat_restart", "1"),
        ),
        selection_tags=("match", "tdm", "roles", "routing", "combat", "restart", "stability"),
        checks=(
            MetricCheck("pass", "eq", 1, "post-restart TDM role status must pass"),
            MetricCheck("expected_min_commands", "ge", 4, "four-bot TDM target must be active after restart"),
            MetricCheck("commands", "ge", 4, "post-restart TDM role bots must emit commands"),
            MetricCheck("route_commands", "ge", 4, "post-restart TDM role bots must route"),
            MetricCheck("route_failures", "eq", 0, "post-restart TDM role proof must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                73,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "tdm_role_spawn_stability",
                "eq",
                1,
                "reserved smoke must identify the TDM role spawn-stability lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_route",
                "eq",
                1,
                "TDM role stability smoke must enable route ownership",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_combat",
                "eq",
                1,
                "TDM role stability smoke must enable combat ownership",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle=begin",
                "command",
                "eq",
                "map_force",
                "TDM role stability smoke must run the map force reload path",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle=begin",
                "restart",
                "eq",
                1,
                "TDM role stability smoke must advertise restart mode at cycle start",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=queued",
                "restart",
                "eq",
                1,
                "TDM role stability smoke must queue a restart-driven reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=observed",
                "completed_cycles",
                "eq",
                1,
                "TDM role stability smoke must observe reload after the first role proof",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_reload=observed",
                "restart",
                "eq",
                1,
                "observed role-stability reload must be restart-driven",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete",
                "cycle",
                "eq",
                2,
                "final role-stability status must come from the post-restart cycle",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete",
                "phase",
                "eq",
                "post_reload",
                "final role-stability status must be marked post-reload",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete",
                "pass",
                "eq",
                1,
                "post-restart role-stability cycle status must pass",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete",
                "official_pass",
                "eq",
                1,
                "post-restart role-stability status must use the game status line",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cleanup_status",
                "pass",
                "eq",
                1,
                "cleanup status after TDM role restart must pass",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat_cleanup_status",
                "count",
                "eq",
                0,
                "TDM role restart cleanup must leave no bots behind",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "cycles",
                "eq",
                2,
                "TDM role stability smoke must complete two proof cycles",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "map_changes",
                "eq",
                1,
                "two TDM role stability cycles must include one map restart",
            ),
            MarkerMetricCheck(
                "q3a_bot_frame_command_smoke_map_repeat=complete",
                "final_count",
                "eq",
                0,
                "TDM role stability smoke must remove bots after the final cycle",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "any_eq",
                1,
                "TDM role stability smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "TDM role stability smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "TDM role stability smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_activations",
                "ge",
                1,
                "TDM role stability smoke must activate route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_route_requests",
                "ge",
                1,
                "TDM role stability smoke must route through the team-role owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_remaining_ms",
                "gt",
                0,
                "TDM role stability smoke must end with an active route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_mode",
                "eq",
                2,
                "TDM role stability route status must record TDM mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_role",
                "ge",
                1,
                "TDM role stability route status must record a selected role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_route_lane",
                "ge",
                1,
                "TDM role stability route status must record a selected lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_target_selections",
                "ge",
                1,
                "TDM role stability smoke must select combat targets",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_attack_decisions",
                "ge",
                1,
                "TDM role stability smoke must own attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_mode",
                "eq",
                2,
                "TDM role stability combat status must record TDM mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_target_visible",
                "eq",
                1,
                "TDM role stability smoke must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_team_role_combat_target_shootable",
                "eq",
                1,
                "TDM role stability smoke must record shootable target facts",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_applied_attack_buttons",
                "ge",
                1,
                "TDM role stability smoke must apply attack input",
            ),
        ),
    ),
    Scenario(
        name="ctf_role_combat",
        title="CTF role combat",
        smoke_mode=36,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_role_combat enabled "
            "and verifies CTF match role/lane policy can own a live attack "
            "decision from visible, shootable enemy facts."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_role_combat", "1"),
        ),
        selection_tags=("match", "ctf", "roles", "combat"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF-role combat smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF-role combat smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                36,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_role_combat",
                "eq",
                1,
                "reserved smoke must enable the CTF role-combat proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF-role combat smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF-role combat smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ctf",
                "ge",
                1,
                "CTF-role combat smoke must select CTF match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_combat_requests",
                "ge",
                1,
                "CTF-role combat cvar must request combat ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_combat_policy_selections",
                "ge",
                1,
                "CTF-role combat smoke must observe a valid CTF match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_combat_target_selections",
                "ge",
                1,
                "CTF-role combat smoke must select a visible target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_combat_attack_decisions",
                "ge",
                1,
                "CTF-role combat smoke must own an attack decision",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_combat_invalid_skips",
                "eq",
                0,
                "CTF-role combat smoke must not reject valid CTF policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_combat_mode",
                "eq",
                3,
                "CTF-role combat smoke must record CTF mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_combat_role",
                "ge",
                1,
                "CTF-role combat smoke must record a selected role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_combat_lane",
                "ge",
                1,
                "CTF-role combat smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_combat_target_client",
                "ge",
                0,
                "CTF-role combat smoke must record a client target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_combat_target_visible",
                "eq",
                1,
                "CTF-role combat smoke must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_role_combat_target_shootable",
                "eq",
                1,
                "CTF-role combat smoke must record shootable target facts",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_applied_attack_buttons",
                "ge",
                1,
                "CTF-role combat smoke must apply attack input",
            ),
        ),
    ),
    Scenario(
        name="ctf_dropped_flag_route",
        title="CTF dropped flag route",
        smoke_mode=37,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_dropped_flag_route "
            "enabled and verifies the live objective bridge routes toward "
            "dropped enemy flags through the dropped-flag response lane."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_dropped_flag_route", "1"),
        ),
        selection_tags=("match", "ctf", "objectives", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF dropped-flag smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF dropped-flag smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                37,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_dropped_flag_route",
                "eq",
                1,
                "reserved smoke must enable the CTF dropped-flag route proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF dropped-flag route smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF dropped-flag route smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_dropped_flag_route_requests",
                "ge",
                1,
                "CTF dropped-flag route cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_dropped_flag_route_assignments",
                "ge",
                1,
                "CTF dropped-flag route smoke must assign a dropped flag objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_dropped_flag_route_route_requests",
                "ge",
                1,
                "CTF dropped-flag route smoke must request route planning",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_dropped_flag_route_route_commands",
                "ge",
                1,
                "CTF dropped-flag route smoke must command a dropped flag route",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_dropped_flag_route_invalid_skips",
                "eq",
                0,
                "CTF dropped-flag route smoke must not reject seeded dropped flags",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_dropped_flag_route_lane",
                "eq",
                5,
                "CTF dropped-flag route smoke must record dropped-flag response lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_dropped_flag_route_type",
                "eq",
                1,
                "CTF dropped-flag route smoke must target enemy flag pickup",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_dropped_flag_route_source",
                "eq",
                2,
                "CTF dropped-flag route smoke must target a dropped flag entity",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_dropped_flag_route_goal_distance_sq",
                "gt",
                0,
                "CTF dropped-flag route smoke must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="ctf_carrier_support_route",
        title="CTF carrier support route",
        smoke_mode=38,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_carrier_support_route "
            "enabled and verifies the live objective bridge routes toward a "
            "same-team flag carrier through the carrier-support lane."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_carrier_support_route", "1"),
        ),
        selection_tags=("match", "ctf", "objectives", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF carrier-support smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF carrier-support smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                38,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_carrier_support_route",
                "eq",
                1,
                "reserved smoke must enable the CTF carrier-support route proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF carrier-support route smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF carrier-support route smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_carrier_support_route_requests",
                "ge",
                1,
                "CTF carrier-support route cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_carrier_support_route_assignments",
                "ge",
                1,
                "CTF carrier-support route smoke must assign a carrier objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_carrier_support_route_route_requests",
                "ge",
                1,
                "CTF carrier-support route smoke must request route planning",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_carrier_support_route_route_commands",
                "ge",
                1,
                "CTF carrier-support route smoke must command a carrier-support route",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_carrier_support_route_invalid_skips",
                "eq",
                0,
                "CTF carrier-support route smoke must not reject seeded carriers",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_carrier_support_route_role",
                "eq",
                4,
                "CTF carrier-support route smoke must record support role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_carrier_support_route_lane",
                "eq",
                4,
                "CTF carrier-support route smoke must record carrier-support lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_carrier_support_route_type",
                "eq",
                1,
                "CTF carrier-support route smoke must target enemy flag pickup",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_carrier_support_route_source",
                "eq",
                3,
                "CTF carrier-support route smoke must target a flag carrier",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_carrier_support_route_carrier_client",
                "ge",
                0,
                "CTF carrier-support route smoke must record the carrier client",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_carrier_support_route_goal_distance_sq",
                "gt",
                0,
                "CTF carrier-support route smoke must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="ctf_base_return_route",
        title="CTF base return route",
        smoke_mode=39,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_base_return_route "
            "enabled and verifies the live objective bridge routes toward an "
            "enemy carrier holding the bot team's own flag through the "
            "own-base-return lane."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_base_return_route", "1"),
        ),
        selection_tags=("match", "ctf", "objectives", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF base-return smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF base-return smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                39,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_base_return_route",
                "eq",
                1,
                "reserved smoke must enable the CTF base-return route proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF base-return route smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF base-return route smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_base_return_route_requests",
                "ge",
                1,
                "CTF base-return route cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_base_return_route_assignments",
                "ge",
                1,
                "CTF base-return route smoke must assign an own-flag return objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_base_return_route_route_requests",
                "ge",
                1,
                "CTF base-return route smoke must request route planning",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_base_return_route_route_commands",
                "ge",
                1,
                "CTF base-return route smoke must command an own-base-return route",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_base_return_route_invalid_skips",
                "eq",
                0,
                "CTF base-return route smoke must not reject seeded carriers",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_base_return_route_role",
                "eq",
                3,
                "CTF base-return route smoke must record returner role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_base_return_route_lane",
                "eq",
                6,
                "CTF base-return route smoke must record own-base-return lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_base_return_route_type",
                "eq",
                2,
                "CTF base-return route smoke must target own flag return",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_base_return_route_source",
                "eq",
                3,
                "CTF base-return route smoke must target a flag carrier",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_base_return_route_carrier_client",
                "ge",
                0,
                "CTF base-return route smoke must record the carrier client",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_base_return_route_goal_distance_sq",
                "gt",
                0,
                "CTF base-return route smoke must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="ctf_objective_route",
        title="CTF objective route policy",
        smoke_mode=40,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_objective_route "
            "enabled and verifies the combined objective policy reacts to "
            "multiple live flag-state sources by selecting base-return, "
            "carrier-support, and dropped-flag objectives in one run."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_objective_route", "1"),
        ),
        selection_tags=("match", "ctf", "objectives", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF objective policy smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF objective policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                40,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_objective_route",
                "eq",
                1,
                "reserved smoke must enable the CTF objective-route policy lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF objective policy smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF objective policy smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_requests",
                "ge",
                1,
                "CTF objective policy cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_assignments",
                "ge",
                1,
                "CTF objective policy smoke must assign a selected objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_base_return_candidates",
                "ge",
                1,
                "CTF objective policy smoke must see a base-return candidate",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_carrier_support_candidates",
                "ge",
                1,
                "CTF objective policy smoke must see a carrier-support candidate",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_dropped_flag_candidates",
                "ge",
                1,
                "CTF objective policy smoke must see a dropped-flag candidate",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_base_return_selections",
                "ge",
                1,
                "CTF objective policy must prioritize base return",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_carrier_support_selections",
                "ge",
                1,
                "CTF objective policy must choose carrier support when no base return is available",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_dropped_flag_selections",
                "ge",
                1,
                "CTF objective policy must choose dropped flags when higher-priority flag states are unavailable",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_dropped_flag_deferrals",
                "ge",
                1,
                "CTF objective policy must defer dropped flags when base return is available",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_route_requests",
                "ge",
                1,
                "CTF objective policy smoke must request route planning",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_route_commands",
                "ge",
                1,
                "CTF objective policy smoke must command the selected objective route",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_invalid_skips",
                "eq",
                0,
                "CTF objective policy smoke must not reject seeded objective candidates",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_objective_route_selection",
                "ge",
                1,
                "CTF objective policy smoke must record a selected objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_objective_route_goal_distance_sq",
                "gt",
                0,
                "CTF objective policy smoke must record a non-zero route goal distance",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_objective_candidates",
                "ge",
                1,
                "CTF objective policy must enter objective arbitration",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_objective_owners",
                "ge",
                1,
                "CTF objective policy must win objective ownership",
            ),
        ),
    ),
    Scenario(
        name="ctf_objective_transitions",
        title="CTF objective pickup/drop transitions",
        smoke_mode=76,
        description=(
            "Runs a four-bot CTF smoke with the objective route policy and "
            "transition proof enabled, then verifies real CTF pickup, death "
            "drop, and same-team dropped-flag return hooks feed objective "
            "telemetry before the objective route owner commands the flag loop."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_objective_route", "1"),
            ("sg_bot_ctf_objective_transitions", "1"),
        ),
        selection_tags=("match", "ctf", "objectives", "routing", "transitions"),
        checks=(
            MetricCheck("pass", "eq", 1, "CTF transition smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF transition smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF transition smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                76,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_objective_route",
                "eq",
                1,
                "reserved smoke must enable the CTF objective-route policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_objective_transitions",
                "eq",
                1,
                "reserved smoke must enable the CTF objective transition proof",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF transition smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF transition smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_flag_pickups",
                "ge",
                1,
                "CTF transition smoke must record a real flag pickup hook",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_flag_drops",
                "ge",
                1,
                "CTF transition smoke must record a real flag drop hook",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_flag_returns",
                "ge",
                1,
                "CTF transition smoke must record a real dropped-flag return hook",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_requests",
                "ge",
                1,
                "CTF transition smoke must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_assignments",
                "ge",
                1,
                "CTF transition smoke must assign a selected objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_base_return_candidates",
                "ge",
                1,
                "CTF transition smoke must see a base-return candidate after transitions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_dropped_flag_candidates",
                "ge",
                1,
                "CTF transition smoke must see a dropped-flag candidate after transitions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_route_commands",
                "ge",
                1,
                "CTF transition smoke must command the selected objective route",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_invalid_skips",
                "eq",
                0,
                "CTF transition smoke must not reject seeded objective candidates",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_objective_candidates",
                "ge",
                1,
                "CTF transition smoke must enter objective arbitration",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_objective_owners",
                "ge",
                1,
                "CTF transition smoke must win objective ownership",
            ),
        ),
    ),
    Scenario(
        name="ctf_objective_route_precedence",
        title="CTF objective route precedence",
        smoke_mode=41,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_role_route and "
            "sg_bot_ctf_objective_route enabled together, verifying the "
            "generic CTF role route defers while the objective route policy "
            "owns the selected flag route."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_role_route", "1"),
            ("sg_bot_ctf_objective_route", "1"),
        ),
        selection_tags=("match", "ctf", "objectives", "routing"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF objective precedence smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF objective precedence smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                41,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_role_route",
                "eq",
                1,
                "precedence smoke must enable the CTF role-route lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_objective_route",
                "eq",
                1,
                "precedence smoke must enable the CTF objective-route policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_objective_route_precedence",
                "eq",
                1,
                "reserved smoke must mark the objective-route precedence proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF objective precedence smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF objective precedence smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_requests",
                "ge",
                1,
                "precedence smoke must still evaluate the CTF role-route cvar",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_policy_selections",
                "ge",
                1,
                "precedence smoke must observe valid CTF role policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_objective_deferrals",
                "ge",
                1,
                "CTF role route must defer to the objective route policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_activations",
                "eq",
                0,
                "CTF role route must not activate while objective route owns routing",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_route_requests",
                "eq",
                0,
                "CTF role route must not submit timed route requests while deferred",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_role_route_invalid_skips",
                "eq",
                0,
                "CTF role route precedence deferral must not count as invalid",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_requests",
                "ge",
                1,
                "CTF objective policy cvar must request route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_assignments",
                "ge",
                1,
                "CTF objective policy must assign the selected objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_route_requests",
                "ge",
                1,
                "CTF objective policy must request route planning",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_route_commands",
                "ge",
                1,
                "CTF objective policy must command the selected objective route",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_invalid_skips",
                "eq",
                0,
                "CTF objective policy must not inherit route-owner conflicts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_objective_route_selection",
                "ge",
                1,
                "CTF objective policy must record a selected objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ctf_objective_route_goal_distance_sq",
                "gt",
                0,
                "CTF objective policy must record a non-zero route goal distance",
            ),
        ),
    ),
    Scenario(
        name="ffa_item_roles",
        title="FFA item roles",
        smoke_mode=46,
        description=(
            "Runs a four-bot FFA smoke with sg_bot_ffa_item_roles enabled "
            "and verifies free-for-all item-role policy shapes live "
            "pickup-goal selection."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_ffa_item_roles", "1"),
        ),
        selection_tags=("match", "ffa", "items", "roles"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "FFA item-role smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "FFA item-role smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "FFA item-role smoke must assign an item goal"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                46,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_item_roles",
                "eq",
                1,
                "reserved smoke must enable the FFA item-role proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "ffa_pass",
                "eq",
                1,
                "FFA item-role smoke must run in an FFA-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                0,
                "FFA item-role smoke must not enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ffa",
                "ge",
                1,
                "FFA item-role smoke must select FFA match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_evaluations",
                "ge",
                1,
                "FFA item-role smoke must evaluate objective item-role policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_selections",
                "ge",
                1,
                "FFA item-role smoke must select an objective item-role policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_evaluations",
                "ge",
                1,
                "FFA item-role cvar must evaluate nav item candidates",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_selections",
                "ge",
                1,
                "FFA item-role smoke must observe valid nav item-role policies",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_score_boosts",
                "ge",
                1,
                "FFA item-role smoke must boost item candidate scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_selected_goals",
                "ge",
                1,
                "FFA item-role smoke must select a role-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_invalid_skips",
                "eq",
                0,
                "FFA item-role smoke must not record invalid policy skips",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_mode",
                "eq",
                1,
                "FFA item-role smoke must record FFA mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_role",
                "ge",
                1,
                "FFA item-role smoke must record a selected role",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_lane",
                "ge",
                1,
                "FFA item-role smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_category",
                "ge",
                1,
                "FFA item-role smoke must record a selected item category",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_item_role",
                "ge",
                1,
                "FFA item-role smoke must record a selected item role",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_entity",
                "ge",
                0,
                "FFA item-role smoke must record the selected item entity",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_item",
                "gt",
                0,
                "FFA item-role smoke must record the selected item id",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_score_boost",
                "gt",
                0,
                "FFA item-role smoke must record a positive score boost",
            ),
        ),
    ),
    Scenario(
        name="ctf_item_roles",
        title="CTF item roles",
        smoke_mode=47,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_item_roles enabled "
            "and verifies capture-the-flag item-role policy shapes live "
            "pickup-goal selection."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_ctf_item_roles", "1"),
        ),
        selection_tags=("match", "ctf", "items", "roles"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "CTF item-role smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "CTF item-role smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "CTF item-role smoke must assign an item goal"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                47,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_item_roles",
                "eq",
                1,
                "reserved smoke must enable the CTF item-role proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "CTF item-role smoke must run in a CTF match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "CTF item-role smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ctf",
                "ge",
                1,
                "CTF item-role smoke must select CTF match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_evaluations",
                "ge",
                1,
                "CTF item-role smoke must evaluate objective item-role policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_selections",
                "ge",
                1,
                "CTF item-role smoke must select an objective item-role policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ctf_item_role_evaluations",
                "ge",
                1,
                "CTF item-role cvar must evaluate nav item candidates",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ctf_item_role_selections",
                "ge",
                1,
                "CTF item-role smoke must observe valid nav item-role policies",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ctf_item_role_score_boosts",
                "ge",
                1,
                "CTF item-role smoke must boost item candidate scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ctf_item_role_selected_goals",
                "ge",
                1,
                "CTF item-role smoke must select a role-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ctf_item_role_invalid_skips",
                "eq",
                0,
                "CTF item-role smoke must not record invalid policy skips",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_mode",
                "eq",
                3,
                "CTF item-role smoke must record CTF mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_role",
                "ge",
                1,
                "CTF item-role smoke must record a selected role",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_lane",
                "ge",
                1,
                "CTF item-role smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_category",
                "ge",
                1,
                "CTF item-role smoke must record a selected item category",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_item_role",
                "ge",
                1,
                "CTF item-role smoke must record a selected item role",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_entity",
                "ge",
                0,
                "CTF item-role smoke must record the selected item entity",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_item",
                "gt",
                0,
                "CTF item-role smoke must record the selected item id",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ctf_item_role_score_boost",
                "gt",
                0,
                "CTF item-role smoke must record a positive score boost",
            ),
        ),
    ),
    Scenario(
        name="ffa_role_combat",
        title="FFA role combat",
        smoke_mode=48,
        description=(
            "Runs a four-bot FFA smoke with sg_bot_ffa_role_combat enabled "
            "and verifies free-for-all match role/lane policy can own a live "
            "attack decision from visible, shootable enemy facts."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_ffa_role_combat", "1"),
        ),
        selection_tags=("match", "ffa", "roles", "combat"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "FFA-role combat smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "FFA-role combat smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                48,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_role_combat",
                "eq",
                1,
                "reserved smoke must enable the FFA role-combat proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "ffa_pass",
                "eq",
                1,
                "FFA-role combat smoke must run in an FFA-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                0,
                "FFA-role combat smoke must not enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ffa",
                "ge",
                1,
                "FFA-role combat smoke must select FFA match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_requests",
                "ge",
                1,
                "FFA-role combat cvar must request combat ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_policy_selections",
                "ge",
                1,
                "FFA-role combat smoke must observe a valid FFA match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_target_selections",
                "ge",
                1,
                "FFA-role combat smoke must select a visible target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_attack_decisions",
                "ge",
                1,
                "FFA-role combat smoke must own an attack decision",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_invalid_skips",
                "eq",
                0,
                "FFA-role combat smoke must not reject valid FFA policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_mode",
                "eq",
                1,
                "FFA-role combat smoke must record FFA mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_role",
                "ge",
                1,
                "FFA-role combat smoke must record a selected role",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_lane",
                "ge",
                1,
                "FFA-role combat smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_client",
                "ge",
                0,
                "FFA-role combat smoke must record a client target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_visible",
                "eq",
                1,
                "FFA-role combat smoke must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_shootable",
                "eq",
                1,
                "FFA-role combat smoke must record shootable target facts",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_applied_attack_buttons",
                "ge",
                1,
                "FFA-role combat smoke must apply attack input",
            ),
        ),
    ),
    Scenario(
        name="ffa_spawn_camp_combat_avoidance",
        title="FFA spawn-camp combat avoidance",
        smoke_mode=49,
        description=(
            "Runs a four-bot FFA smoke with sg_bot_ffa_role_combat, "
            "sg_bot_ffa_spawn_camp_avoidance, and "
            "sg_bot_ffa_spawn_camp_combat_avoidance enabled together, "
            "then verifies role/lane combat can select a live attack target "
            "and FFA anti-camp policy can suppress the shot when that target "
            "is the nearby spawn-camp source."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_ffa_role_combat", "1"),
            ("sg_bot_ffa_spawn_camp_avoidance", "1"),
            ("sg_bot_ffa_spawn_camp_combat_avoidance", "1"),
        ),
        selection_tags=("match", "ffa", "roles", "combat", "anti-camp"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "FFA camp-combat avoidance smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "FFA camp-combat avoidance smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                49,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_role_combat",
                "eq",
                1,
                "reserved smoke must enable the FFA role-combat proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_spawn_camp_avoidance",
                "eq",
                1,
                "reserved smoke must enable FFA anti-camp policy",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_spawn_camp_combat_avoidance",
                "eq",
                1,
                "reserved smoke must enable the FFA anti-camp combat veto",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "ffa_pass",
                "eq",
                1,
                "FFA camp-combat avoidance smoke must run in an FFA-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                0,
                "FFA camp-combat avoidance smoke must not enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ffa",
                "ge",
                1,
                "FFA camp-combat avoidance smoke must select FFA match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_requests",
                "ge",
                1,
                "FFA role-combat cvar must request combat ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_policy_selections",
                "ge",
                1,
                "FFA camp-combat avoidance smoke must observe valid FFA role policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_target_selections",
                "ge",
                1,
                "FFA camp-combat avoidance smoke must select a visible target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_attack_decisions",
                "ge",
                1,
                "FFA camp-combat avoidance smoke must own an attack decision before suppression",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_mode",
                "eq",
                1,
                "FFA camp-combat avoidance smoke must record FFA role-combat mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_visible",
                "eq",
                1,
                "FFA camp-combat avoidance smoke must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_shootable",
                "eq",
                1,
                "FFA camp-combat avoidance smoke must record shootable target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_evaluations",
                "ge",
                1,
                "FFA camp-combat cvar must evaluate role-combat attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_blocks",
                "ge",
                1,
                "FFA camp-combat cvar must block role-combat attack input",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_source_blocks",
                "ge",
                1,
                "FFA camp-combat cvar must block because the target is the camp source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_invalid_skips",
                "eq",
                0,
                "FFA camp-combat smoke must not reject valid FFA policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_target_client",
                "ge",
                0,
                "FFA camp-combat smoke must record a target client",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_source_client",
                "ge",
                0,
                "FFA camp-combat smoke must record a source client",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_source_distance_sq",
                "gt",
                0,
                "FFA camp-combat smoke must record source distance",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_source_distance_sq",
                "lt",
                147456,
                "FFA camp-combat smoke must source from inside the 384-unit camp radius",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_policy_avoid",
                "eq",
                1,
                "FFA camp-combat smoke must record avoid-spawn-camping policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_blocked",
                "eq",
                1,
                "FFA camp-combat smoke must record final attack suppression",
            ),
        ),
    ),
    Scenario(
        name="ffa_live_pacing",
        title="FFA live pacing",
        smoke_mode=74,
        description=(
            "Runs a four-bot FFA smoke with roam-route, spawn-camp "
            "avoidance, item-role scoring, role-combat, and spawn-camp "
            "combat avoidance enabled together. The scenario verifies the "
            "normal FFA pacing loop can route, value contested items, select "
            "a live combat target, and suppress a spawn-camp shot in one run."
        ),
        task_ids=("FR-04-T04", "FR-04-T06", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=40,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_ffa_roam_route", "1"),
            ("sg_bot_ffa_spawn_camp_avoidance", "1"),
            ("sg_bot_ffa_item_roles", "1"),
            ("sg_bot_ffa_role_combat", "1"),
            ("sg_bot_ffa_spawn_camp_combat_avoidance", "1"),
        ),
        selection_tags=("match", "ffa", "roles", "routing", "items", "combat", "anti-camp", "pacing"),
        checks=(
            MetricCheck("pass", "eq", 1, "FFA live pacing status must pass"),
            MetricCheck("expected_min_commands", "ge", 4, "four-bot FFA target must be active"),
            MetricCheck("commands", "ge", 4, "FFA live pacing bots must emit commands"),
            MetricCheck("route_commands", "ge", 4, "FFA live pacing bots must retain route output"),
            MetricCheck("route_failures", "eq", 0, "FFA live pacing proof must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                74,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_live_pacing",
                "eq",
                1,
                "reserved smoke must identify the FFA live-pacing lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_roam_route",
                "eq",
                1,
                "FFA live pacing must enable roam-route ownership",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_spawn_camp_avoidance",
                "eq",
                1,
                "FFA live pacing must enable spawn-camp route pressure",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_item_roles",
                "eq",
                1,
                "FFA live pacing must enable item-role scoring",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_role_combat",
                "eq",
                1,
                "FFA live pacing must enable role-combat ownership",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_spawn_camp_combat_avoidance",
                "eq",
                1,
                "FFA live pacing must enable spawn-camp combat avoidance",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "ffa_pass",
                "eq",
                1,
                "FFA live pacing must run in an FFA-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                0,
                "FFA live pacing must remain a non-team match",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ffa",
                "ge",
                1,
                "FFA live pacing must select FFA match policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_activations",
                "ge",
                1,
                "FFA live pacing must activate the roam-route owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_route_requests",
                "ge",
                1,
                "FFA live pacing must route through the roam owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_mode",
                "eq",
                1,
                "FFA live pacing must record FFA route mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_remaining_ms",
                "gt",
                0,
                "FFA live pacing must leave an active route window",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_source_selections",
                "ge",
                1,
                "FFA live pacing must select a spawn-camp pressure source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_route_requests",
                "ge",
                1,
                "FFA live pacing must request route pressure away from the source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_policy_avoid",
                "eq",
                1,
                "FFA live pacing must retain avoid-spawn-camping policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_source_distance_sq",
                "lt",
                147456,
                "FFA live pacing must source pressure inside the 384-unit camp radius",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_evaluations",
                "ge",
                1,
                "FFA live pacing must evaluate item-role scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_selections",
                "ge",
                1,
                "FFA live pacing must select item-role policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_selected_goals",
                "ge",
                1,
                "FFA live pacing must assign an item-role goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_invalid_skips",
                "eq",
                0,
                "FFA live pacing must not reject valid item-role state",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_mode",
                "eq",
                1,
                "FFA live pacing must record FFA item-role mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_score_boost",
                "gt",
                0,
                "FFA live pacing must boost at least one contested item",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_target_selections",
                "ge",
                1,
                "FFA live pacing must select a combat target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_attack_decisions",
                "ge",
                1,
                "FFA live pacing must own combat attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_invalid_skips",
                "eq",
                0,
                "FFA live pacing must not reject valid combat policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_mode",
                "eq",
                1,
                "FFA live pacing must record FFA combat mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_visible",
                "eq",
                1,
                "FFA live pacing must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_shootable",
                "eq",
                1,
                "FFA live pacing must record shootable target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_evaluations",
                "ge",
                1,
                "FFA live pacing must evaluate combat avoidance",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_source_blocks",
                "ge",
                1,
                "FFA live pacing must suppress at least one spawn-camp-source shot",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_policy_avoid",
                "eq",
                1,
                "FFA live pacing must retain combat avoid policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_blocked",
                "eq",
                1,
                "FFA live pacing must record final attack suppression",
            ),
        ),
    ),
    Scenario(
        name="duel_live_pacing",
        title="Duel live pacing",
        smoke_mode=75,
        description=(
            "Runs a two-bot Duel smoke with sg_bot_duel_live_pacing enabled "
            "and verifies Duel match policy can drive item denial, route "
            "pressure, live combat arbitration, and spawn-camp shot "
            "suppression without enabling the FFA proof cvars."
        ),
        task_ids=("FR-04-T04", "FR-04-T06", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=40,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "2"),
            ("sg_bot_duel_live_pacing", "1"),
        ),
        selection_tags=("match", "duel", "roles", "routing", "items", "combat", "anti-camp", "pacing"),
        checks=(
            MetricCheck("pass", "eq", 1, "Duel live pacing status must pass"),
            MetricCheck("expected_min_commands", "ge", 2, "two-bot Duel target must be active"),
            MetricCheck("commands", "ge", 2, "Duel live pacing bots must emit commands"),
            MetricCheck("route_commands", "ge", 2, "Duel live pacing bots must retain route output"),
            MetricCheck("route_failures", "eq", 0, "Duel live pacing proof must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                75,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=2,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "duel_live_pacing",
                "eq",
                1,
                "reserved smoke must identify the Duel live-pacing lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_live_pacing",
                "eq",
                0,
                "Duel live pacing must not be reported as the FFA aggregate lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_roam_route",
                "eq",
                0,
                "Duel live pacing must not enable the FFA roam-route proof cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ffa_item_roles",
                "eq",
                0,
                "Duel live pacing must not enable the FFA item-role proof cvar",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                2,
                "Duel live pacing must run as Duel",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                0,
                "Duel live pacing must remain a non-team match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "playing",
                "eq",
                2,
                "Duel live pacing must keep exactly two bots playing",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "spectators",
                "eq",
                0,
                "Duel live pacing must not move either target bot to spectators",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "queued",
                "eq",
                0,
                "Duel live pacing must not rely on spectator queue flow",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_duel",
                "ge",
                1,
                "Duel live pacing must select Duel match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_mode",
                "eq",
                5,
                "Duel live pacing must record Duel as the last match mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_mode_name",
                "eq",
                "duel",
                "Duel live pacing must name the last match mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_deny_enemy",
                "ge",
                1,
                "Duel live pacing must produce item-denial policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_activations",
                "ge",
                1,
                "Duel live pacing must activate the FFA-style roam owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_roam_route_route_requests",
                "ge",
                1,
                "Duel live pacing must route through the roam owner",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_mode",
                "eq",
                5,
                "Duel live pacing must record Duel route mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_roam_route_remaining_ms",
                "gt",
                0,
                "Duel live pacing must leave an active route window",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_source_selections",
                "ge",
                1,
                "Duel live pacing must select a spawn-pressure source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_avoidance_route_requests",
                "ge",
                1,
                "Duel live pacing must request route pressure away from the source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_avoidance_policy_avoid",
                "eq",
                1,
                "Duel live pacing must retain avoid-spawn-camping policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_evaluations",
                "ge",
                1,
                "Duel live pacing must evaluate item-role scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_selections",
                "ge",
                1,
                "Duel live pacing must select item-role policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_selected_goals",
                "ge",
                1,
                "Duel live pacing must assign an item-role goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "ffa_item_role_invalid_skips",
                "eq",
                0,
                "Duel live pacing must not reject valid item-role state",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_mode",
                "eq",
                5,
                "Duel live pacing must record Duel item-role mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_ffa_item_role_score_boost",
                "gt",
                0,
                "Duel live pacing must boost at least one contested item",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_target_selections",
                "ge",
                1,
                "Duel live pacing must select a combat target",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_attack_decisions",
                "ge",
                1,
                "Duel live pacing must own combat attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_role_combat_invalid_skips",
                "eq",
                0,
                "Duel live pacing must not reject valid combat policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_mode",
                "eq",
                5,
                "Duel live pacing must record Duel combat mode",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_visible",
                "eq",
                1,
                "Duel live pacing must record visible target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_role_combat_target_shootable",
                "eq",
                1,
                "Duel live pacing must record shootable target facts",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_evaluations",
                "ge",
                1,
                "Duel live pacing must evaluate combat avoidance",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ffa_spawn_camp_combat_avoidance_source_blocks",
                "ge",
                1,
                "Duel live pacing must suppress at least one spawn-source shot",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_policy_avoid",
                "eq",
                1,
                "Duel live pacing must retain combat avoid policy",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_ffa_spawn_camp_combat_avoidance_blocked",
                "eq",
                1,
                "Duel live pacing must record final attack suppression",
            ),
        ),
    ),
    Scenario(
        name="team_item_roles",
        title="Team item roles",
        smoke_mode=33,
        description=(
            "Runs a four-bot TDM smoke with sg_bot_team_item_roles enabled "
            "and verifies match item-role policy shapes live pickup-goal "
            "selection."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_team_item_roles", "1"),
        ),
        selection_tags=("match", "tdm", "items", "roles"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "team-item role smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "team-item role smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "team-item role smoke must assign an item goal"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                33,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_item_roles",
                "eq",
                1,
                "reserved smoke must enable the team item-role proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "team-item role smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "team-item role smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "team-item role smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_evaluations",
                "ge",
                1,
                "team-item role smoke must evaluate objective item-role policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_selections",
                "ge",
                1,
                "team-item role smoke must select an objective item-role policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_evaluations",
                "ge",
                1,
                "team-item role cvar must evaluate nav item candidates",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_selections",
                "ge",
                1,
                "team-item role smoke must observe valid nav item-role policies",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_score_boosts",
                "ge",
                1,
                "team-item role smoke must boost item candidate scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_selected_goals",
                "ge",
                1,
                "team-item role smoke must select a role-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_mode",
                "eq",
                2,
                "team-item role smoke must record TDM mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_role",
                "ge",
                1,
                "team-item role smoke must record a selected role",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_lane",
                "ge",
                1,
                "team-item role smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_category",
                "ge",
                1,
                "team-item role smoke must record a selected item category",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_item_role",
                "ge",
                1,
                "team-item role smoke must record a selected item role",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_entity",
                "ge",
                0,
                "team-item role smoke must record the selected item entity",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_item",
                "gt",
                0,
                "team-item role smoke must record the selected item id",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_score_boost",
                "gt",
                0,
                "team-item role smoke must record a positive score boost",
            ),
        ),
    ),
    Scenario(
        name="team_resource_denial",
        title="Team resource denial",
        smoke_mode=50,
        description=(
            "Runs a four-bot TDM smoke with sg_bot_team_resource_denial "
            "enabled and verifies deny-enemy resource policy boosts live "
            "pickup-goal scoring for contested weapons, powerups, tech, or "
            "utility items."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_team_resource_denial", "1"),
        ),
        selection_tags=("match", "tdm", "items", "resources"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "team-resource denial smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "team-resource denial smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "team-resource denial smoke must assign an item goal"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                50,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_resource_denial",
                "eq",
                1,
                "reserved smoke must enable the team resource-denial proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "team-resource denial smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "team-resource denial smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "team-resource denial smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_resource_policy_evaluations",
                "ge",
                1,
                "team-resource denial smoke must evaluate objective resource policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_resource_policy_deny_enemy",
                "ge",
                1,
                "team-resource denial smoke must select deny-enemy resource policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_evaluations",
                "ge",
                1,
                "team-resource denial cvar must evaluate nav item candidates",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_policy_denies",
                "ge",
                1,
                "team-resource denial smoke must observe deny-enemy resource policies",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_score_boosts",
                "ge",
                1,
                "team-resource denial smoke must boost item candidate scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_selected_goals",
                "ge",
                1,
                "team-resource denial smoke must select a denial-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_invalid_skips",
                "eq",
                0,
                "team-resource denial smoke must not produce invalid contested policies",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_mode",
                "eq",
                2,
                "team-resource denial smoke must record TDM mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_role",
                "ge",
                1,
                "team-resource denial smoke must record a selected role",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_lane",
                "ge",
                1,
                "team-resource denial smoke must record a selected lane",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_category",
                "ge",
                4,
                "team-resource denial smoke must record a contestable item category",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_intent",
                "eq",
                4,
                "team-resource denial smoke must record deny-enemy intent",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_entity",
                "ge",
                0,
                "team-resource denial smoke must record the selected item entity",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_item",
                "gt",
                0,
                "team-resource denial smoke must record the selected item id",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_score_boost",
                "gt",
                0,
                "team-resource denial smoke must record a positive score boost",
            ),
        ),
    ),
    Scenario(
        name="match_item_policy",
        title="Match item policy",
        smoke_mode=51,
        description=(
            "Runs a four-bot TDM smoke with sg_bot_match_item_policy enabled "
            "and verifies the umbrella policy activates both match item-role "
            "pickup scoring and deny-enemy resource scoring without setting "
            "the individual team item-role or resource-denial proof cvars."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_match_item_policy", "1"),
        ),
        selection_tags=("match", "tdm", "items", "resources", "roles"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "match item-policy smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "match item-policy smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "match item-policy smoke must assign an item goal"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                51,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "match_item_policy",
                "eq",
                1,
                "reserved smoke must enable the umbrella match item-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_item_roles",
                "eq",
                0,
                "umbrella match item-policy smoke must not set the individual team item-role cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_resource_denial",
                "eq",
                0,
                "umbrella match item-policy smoke must not set the individual team resource-denial cvar",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "match item-policy smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "match item-policy smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "match item-policy smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_selections",
                "ge",
                1,
                "match item-policy smoke must select objective item-role policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_resource_policy_deny_enemy",
                "ge",
                1,
                "match item-policy smoke must select deny-enemy resource policy",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_evaluations",
                "ge",
                1,
                "umbrella match item-policy cvar must evaluate team item-role candidates",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_score_boosts",
                "ge",
                1,
                "umbrella match item-policy smoke must boost item-role candidate scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_selected_goals",
                "ge",
                1,
                "umbrella match item-policy smoke must select a role-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_evaluations",
                "ge",
                1,
                "umbrella match item-policy cvar must evaluate resource-denial candidates",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_policy_denies",
                "ge",
                1,
                "umbrella match item-policy smoke must observe deny-enemy resource policies",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_score_boosts",
                "ge",
                1,
                "umbrella match item-policy smoke must boost resource-denial candidate scoring",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_selected_goals",
                "ge",
                1,
                "umbrella match item-policy smoke must select a denial-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_invalid_skips",
                "eq",
                0,
                "umbrella match item-policy smoke must not produce invalid contested policies",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_mode",
                "eq",
                2,
                "umbrella match item-policy smoke must record TDM item-role mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_score_boost",
                "gt",
                0,
                "umbrella match item-policy smoke must record a positive item-role score boost",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_mode",
                "eq",
                2,
                "umbrella match item-policy smoke must record TDM resource-denial mode",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_intent",
                "eq",
                4,
                "umbrella match item-policy smoke must record deny-enemy intent",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_score_boost",
                "gt",
                0,
                "umbrella match item-policy smoke must record a positive resource-denial score boost",
            ),
        ),
    ),
    Scenario(
        name="behavior_policy_umbrella",
        title="Behavior policy umbrella",
        smoke_mode=52,
        description=(
            "Runs a four-bot TDM smoke with sg_bot_behavior_enable enabled "
            "and verifies the integrated behavior switch composes role "
            "routing, role combat, friendly-fire avoidance, and match "
            "item-policy activation without setting the individual proof "
            "cvars. The dedicated match_item_policy smoke owns the live "
            "item-goal selection proof because role-route ownership can "
            "legitimately preempt pickup-goal routing in the combined lane."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_behavior_enable", "1"),
        ),
        selection_tags=("match", "tdm", "behavior", "combat", "items", "resources", "roles"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "behavior umbrella smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "behavior umbrella smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                52,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                1,
                "reserved smoke must enable the integrated behavior umbrella",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "match_item_policy",
                "eq",
                0,
                "behavior umbrella smoke must not set the narrower item-policy cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_route",
                "eq",
                0,
                "behavior umbrella smoke must not set the individual team role-route cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_combat",
                "eq",
                0,
                "behavior umbrella smoke must not set the individual team role-combat cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_fire_avoidance",
                "eq",
                0,
                "behavior umbrella smoke must not set the individual friendly-fire cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_item_roles",
                "eq",
                0,
                "behavior umbrella smoke must not set the individual team item-role cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_resource_denial",
                "eq",
                0,
                "behavior umbrella smoke must not set the individual resource-denial cvar",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_enable",
                "eq",
                1,
                "runtime status must report the behavior umbrella enabled",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "team_role_route",
                "eq",
                1,
                "behavior umbrella must activate team role-route policy",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "team_role_combat",
                "eq",
                1,
                "behavior umbrella must activate team role-combat policy",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "team_fire_avoidance",
                "eq",
                1,
                "behavior umbrella must activate friendly-fire policy",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "match_item_policy",
                "eq",
                1,
                "behavior umbrella must activate match item-policy scoring",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "behavior umbrella smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_activations",
                "ge",
                1,
                "behavior umbrella must drive team role route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_attack_decisions",
                "ge",
                1,
                "behavior umbrella must produce team role-combat attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_fire_avoidance_blocks",
                "ge",
                1,
                "behavior umbrella must let friendly-fire avoidance suppress attacks",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "behavior umbrella smoke must evaluate TDM match policy",
            ),
        ),
    ),
    Scenario(
        name="behavior_arbitration",
        title="Behavior arbitration",
        smoke_mode=63,
        description=(
            "Runs a four-bot TDM smoke with the live sg_bot_behavior_enable "
            "umbrella and verifies the bot brain records a single ordered "
            "behavior owner per frame while separately exposing route, item, "
            "combat, and objective candidates. The row also hard-gates the "
            "proof-cvar audit so live policy cvars are distinct from smoke, "
            "debug, and deprecated gates."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_behavior_enable", "1"),
        ),
        selection_tags=("match", "tdm", "behavior", "arbitration", "combat", "items", "roles"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "behavior arbitration smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "behavior arbitration smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                63,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                1,
                "reserved smoke must enable the integrated behavior umbrella",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_arbitration",
                "eq",
                1,
                "reserved smoke must identify the behavior arbitration proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "match_item_policy",
                "eq",
                0,
                "behavior arbitration smoke must not set the narrower item-policy cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_route",
                "eq",
                0,
                "behavior arbitration smoke must not set the individual team role-route cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_role_combat",
                "eq",
                0,
                "behavior arbitration smoke must not set the individual team role-combat cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_fire_avoidance",
                "eq",
                0,
                "behavior arbitration smoke must not set the individual friendly-fire cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_item_roles",
                "eq",
                0,
                "behavior arbitration smoke must not set the individual team item-role cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_resource_denial",
                "eq",
                0,
                "behavior arbitration smoke must not set the individual resource-denial cvar",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_enable",
                "eq",
                1,
                "runtime status must report the behavior umbrella enabled",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration",
                "eq",
                1,
                "runtime status must expose the central arbitration model",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_policy_cvar_audit",
                "eq",
                1,
                "runtime status must expose proof-cvar classification",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_live_policy_cvars",
                "ge",
                8,
                "behavior arbitration must classify the active live policy cvars",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_smoke_policy_cvars",
                "eq",
                0,
                "behavior arbitration smoke must not rely on smoke-only policy cvars",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_debug_policy_cvars",
                "eq",
                0,
                "behavior arbitration smoke must not rely on debug-only policy cvars",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_deprecated_policy_cvars",
                "eq",
                0,
                "behavior arbitration smoke must not rely on deprecated policy cvars",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_evaluations",
                "ge",
                4,
                "behavior arbitration must evaluate staged bot command frames",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_route_candidates",
                "ge",
                1,
                "behavior arbitration must observe route candidates",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_item_candidates",
                "ge",
                1,
                "behavior arbitration must observe item-policy candidates",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_combat_candidates",
                "ge",
                1,
                "behavior arbitration must observe combat candidates",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_combat_owners",
                "ge",
                1,
                "behavior arbitration must select combat ownership when combat wins",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_handoffs",
                "ge",
                1,
                "behavior arbitration must record a frame-to-frame owner handoff",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "last_behavior_arbitration_owner",
                "gt",
                0,
                "behavior arbitration must record the latest owner id",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "last_behavior_arbitration_priority",
                "gt",
                0,
                "behavior arbitration must record the latest owner priority",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_route_activations",
                "ge",
                1,
                "behavior arbitration must drive team role route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_role_combat_attack_decisions",
                "ge",
                1,
                "behavior arbitration must produce team role-combat attack decisions",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "behavior arbitration smoke must evaluate TDM match policy",
            ),
        ),
    ),
    Scenario(
        name="target_memory_decay",
        title="Target memory decay",
        smoke_mode=64,
        description=(
            "Runs a two-bot FFA smoke that first lets perception acquire a "
            "visible enemy, then gates that same enemy through the blackboard "
            "memory path until the target-memory window expires. The proof "
            "hard-gates acquisition, memory-backed retention, deterministic "
            "smoke occlusion, and memory-decay clearing."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("combat", "blackboard", "memory", "target_memory", "decay"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 2, "target memory smoke must emit bot commands"),
            MetricCheck("route_failures", "eq", 0, "target memory smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                64,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "target_memory",
                "eq",
                1,
                "reserved smoke must identify the target-memory proof lane",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "target memory smoke must acquire a live enemy before retention",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_memory_retains",
                "ge",
                1,
                "blackboard must retain the unseen current enemy from memory",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_memory_decays",
                "ge",
                1,
                "blackboard must clear the retained target after memory decay",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_memory_smoke_occlusions",
                "ge",
                1,
                "smoke proof must drive the deterministic occlusion path",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_clears",
                "ge",
                1,
                "memory decay must clear the current enemy slot",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "last_combat_enemy_memory_age_ms",
                "gt",
                0,
                "target memory status must expose the last retained/decayed age",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "last_combat_enemy_memory_window_ms",
                "eq",
                1000,
                "mode 64 must use the short deterministic proof memory window",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "last_combat_enemy_memory_decay_entity",
                "ge",
                0,
                "memory decay must report the cleared enemy entity",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "last_combat_enemy_memory_decay_client",
                "ge",
                0,
                "memory decay must report the cleared enemy client",
            ),
        ),
    ),
    Scenario(
        name="weapon_scoring_arsenal",
        title="Weapon scoring arsenal",
        smoke_mode=65,
        description=(
            "Runs a two-bot FFA combat smoke that starts the bot on a close-range "
            "rocket launcher, grants a ready super shotgun, and carries railgun "
            "and BFG options without enough ammo. The proof hard-gates carried "
            "arsenal scanning, ammo rejection, splash safety pressure, close-range "
            "selection, enemy-estimate finisher scoring, and switch dispatch."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("combat", "weapons", "inventory", "weapon_scoring", "arsenal"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 2, "weapon scoring smoke must emit bot commands"),
            MetricCheck("route_failures", "eq", 0, "weapon scoring smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                65,
                combat="weapon_scoring",
                weapon_switch=1,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "weapon_scoring",
                "eq",
                1,
                "reserved smoke must identify the weapon-scoring proof lane",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_scans",
                "ge",
                1,
                "weapon scoring smoke must scan carried weapons",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_candidates",
                "ge",
                4,
                "weapon scoring smoke must evaluate a multi-weapon arsenal",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_ready_candidates",
                "ge",
                2,
                "weapon scoring smoke must have multiple ready candidates",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_selections",
                "ge",
                1,
                "weapon scoring smoke must select a carried weapon",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_switch_recommendations",
                "ge",
                1,
                "weapon scoring smoke must recommend a weapon switch",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_ammo_skips",
                "ge",
                1,
                "weapon scoring smoke must reject an insufficient-ammo weapon",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_splash_unsafe_skips",
                "ge",
                1,
                "weapon scoring smoke must record close-range splash safety pressure",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_range_selections",
                "ge",
                1,
                "weapon scoring smoke must select from a matching range band",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_weapon_inventory_estimate_selections",
                "ge",
                1,
                "weapon scoring smoke must use enemy-estimate scoring for selection",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_action_weapon_inventory_selected_item",
                "eq",
                11,
                "weapon scoring smoke must settle on the super shotgun",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_action_weapon_inventory_selected_range_band_name",
                "any_eq",
                "close",
                "selected weapon status must report close range",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_action_weapon_inventory_selected_attack_model_name",
                "any_eq",
                "hitscan",
                "selected weapon status must report the super shotgun attack model",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_action_weapon_inventory_selected_estimate_adjustment",
                "gt",
                0,
                "selected weapon status must expose the finisher estimate bonus",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_action_weapon_inventory_estimate_reason",
                "any_eq",
                "enemy_estimate_finisher",
                "selected weapon status must name the estimate finisher reason",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "weapon_switch_requests",
                "ge",
                1,
                "weapon scoring smoke must request the selected weapon switch",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "weapon_switch_completions",
                "ge",
                1,
                "weapon scoring smoke must prove the weapon switch completed",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "weapon_switch_failures",
                "eq",
                0,
                "weapon scoring smoke must not fail the switch dispatch",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "weapon_switch_expected_item",
                "eq",
                11,
                "weapon scoring smoke must request the super shotgun item",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_weapon_selection_finisher_bonuses",
                "ge",
                1,
                "combat scorer must record enemy-estimate finisher bonuses",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_combat_estimate_selection_reason",
                "any_eq",
                "enemy_estimate_finisher",
                "combat scorer must report the finisher estimate reason",
            ),
        ),
    ),
    Scenario(
        name="aim_fire_policy_depth",
        title="Aim/fire policy depth",
        smoke_mode=66,
        description=(
            "Runs a two-bot FFA combat smoke that starts the bot on a rocket "
            "launcher against a moving, shootable peer. The proof hard-gates "
            "reaction-delay withholding, aim-settle withholding, burst cooldown "
            "pacing, projectile lead, live-aim policy blocks, and eventual "
            "attack-button application."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("combat", "aim", "firing", "projectile_lead", "policy"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 2, "aim/fire smoke must emit bot commands"),
            MetricCheck("route_failures", "eq", 0, "aim/fire smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                66,
                combat="aim_fire_policy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "aim_fairness",
                "eq",
                1,
                "mode 66 must enable the aim/fairness policy cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "aim_fire_policy",
                "eq",
                1,
                "reserved smoke must identify the aim/fire policy proof lane",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "aim/fire smoke must acquire a combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "aim/fire smoke target must be visible",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "aim/fire smoke target must be shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_withheld_fire",
                "ge",
                1,
                "aim/fire policy must withhold fire before the staged aim is ready",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_fire_decisions",
                "ge",
                1,
                "aim/fire policy must eventually allow firing",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_attack_decisions",
                "ge",
                1,
                "action layer must eventually select attack intent",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_applied_attack_buttons",
                "ge",
                1,
                "command layer must eventually apply BUTTON_ATTACK",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "aim_policy_evaluations",
                "ge",
                4,
                "aim/fire smoke must evaluate the aim policy over multiple stages",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "aim_policy_fire_allowed",
                "ge",
                1,
                "aim/fire smoke must allow fire after reaction and settle windows",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "aim_policy_blocks_reaction",
                "ge",
                1,
                "aim/fire smoke must record reaction-delay withholding",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "aim_policy_blocks_aim_settle",
                "ge",
                1,
                "aim/fire smoke must record aim-settle withholding",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "aim_policy_blocks_burst_cooldown",
                "ge",
                1,
                "aim/fire smoke must record burst-cooldown pacing",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "live_aim_evaluations",
                "ge",
                1,
                "live aim consumer must run",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "live_aim_fire_allowed",
                "ge",
                1,
                "live aim consumer must allow fire once ready",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "live_aim_policy_blocks",
                "ge",
                1,
                "live aim consumer must expose policy-blocked aim stages",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "live_aim_projectile_lead_uses",
                "ge",
                1,
                "live aim consumer must use projectile lead for the rocket proof",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_live_aim_weapon",
                "eq",
                20,
                "live aim consumer must report the rocket launcher weapon",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_live_aim_reason",
                "any_eq",
                "projectile_lead",
                "live aim consumer must report projectile lead as the latest aim reason",
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "projectile_lead_uses",
                "ge",
                1,
                "projectile lead helper must report at least one used lead",
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "last_projectile_lead_weapon",
                "eq",
                20,
                "projectile lead helper must report the rocket launcher weapon",
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "last_projectile_lead_offset_sq",
                "gt",
                0,
                "projectile lead helper must report a nonzero lead offset",
            ),
        ),
    ),
    Scenario(
        name="ammo_pressure_pickup",
        title="Ammo pressure pickup",
        smoke_mode=67,
        description=(
            "Runs a one-bot FFA item-routing smoke that starts the bot with a "
            "shotgun and empty shells while staging a routeable shells pickup. "
            "The proof hard-gates ammo item focus, ammo candidate usefulness, "
            "ammo seek decisions, and ammo goal assignment telemetry."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("items", "ammo", "pickup", "resource_pressure", "inventory"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 1, "ammo pressure smoke must emit bot commands"),
            MetricCheck("route_failures", "eq", 0, "ammo pressure smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "ammo pressure smoke must assign an item goal"),
            MetricCheck("last_item_goal_area", "gt", 0, "ammo pressure smoke goal must resolve to an AAS area"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                67,
                combat=0,
                weapon_switch=0,
                item_focus="ammo",
                team_objective=0,
                target=1,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ammo_pressure",
                "eq",
                1,
                "reserved smoke must identify the ammo-pressure proof lane",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_ammo_candidates",
                "ge",
                1,
                "ammo pressure smoke must see an ammo candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_ammo_seek_decisions",
                "ge",
                1,
                "ammo pressure smoke must choose an ammo seek decision",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_ammo_goal_assignments",
                "ge",
                1,
                "ammo pressure smoke must record an ammo goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_item",
                "eq",
                30,
                "ammo pressure smoke must settle on shells as the utility item",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_utility_kind_name",
                "any_eq",
                "ammo",
                "ammo pressure smoke must classify the selected utility as ammo",
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "item_focus_ammo_boosts",
                "ge",
                1,
                "ammo pressure smoke must expose focused ammo utility pressure",
            ),
            MarkerMetricCheck(
                ACTION_DETAIL_STATUS_MARKER,
                "item_ammo_seek_decisions",
                "ge",
                1,
                "detail status must echo ammo seek decisions",
            ),
        ),
    ),
    Scenario(
        name="survival_inventory_use",
        title="Survival inventory use",
        smoke_mode=68,
        description=(
            "Runs a one-bot FFA action smoke that stages low health, no "
            "armor, a carried power shield, and cells. The proof hard-gates "
            "carried inventory scanning, survival power-armor selection, "
            "pending inventory intent, command-request validation, and the "
            "live use_index_only inventory dispatch path."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("actions", "inventory", "survival", "power_armor"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 1, "survival inventory smoke must emit bot commands"),
            MetricCheck("route_failures", "eq", 0, "survival inventory smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                68,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=1,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_inventory",
                "eq",
                1,
                "reserved smoke must identify the survival-inventory proof lane",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_policy_scans",
                "ge",
                1,
                "survival inventory smoke must scan carried inventory",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_policy_candidates",
                "ge",
                1,
                "survival inventory smoke must see a carried inventory candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_policy_usable_candidates",
                "ge",
                1,
                "survival inventory smoke must find a usable survival candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_policy_selections",
                "ge",
                1,
                "survival inventory smoke must select a carried inventory use",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_policy_survival_uses",
                "ge",
                1,
                "survival inventory smoke must classify the use as survival pressure",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_policy_power_armor_uses",
                "ge",
                1,
                "survival inventory smoke must select power armor",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_use_inventory_decisions",
                "ge",
                1,
                "action layer must choose UseInventory intent",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_pending_inventory_uses",
                "ge",
                1,
                "action application must accept a pending inventory use",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_command_requests",
                "ge",
                1,
                "action command builder must validate an inventory request",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_command_request_submitted",
                "ge",
                1,
                "action command dispatch must submit the inventory request",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_inventory_command_dispatches",
                "ge",
                1,
                "action command dispatch must count the inventory use dispatch",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_last_command_request_kind_name",
                "any_eq",
                "use_inventory_index",
                "latest command request kind must be inventory use",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_last_command_dispatch_outcome_name",
                "any_eq",
                "submitted",
                "latest inventory dispatch must submit successfully",
            ),
        ),
    ),
    Scenario(
        name="survival_health_route",
        title="Survival health route",
        smoke_mode=69,
        description=(
            "Runs a one-bot FFA item-routing smoke that stages low health, "
            "no armor, and a routeable medium health pickup without item "
            "focus. The proof hard-gates natural low-health utility boosts, "
            "health candidate/seek telemetry, health goal assignment, and "
            "route-clean AAS item-goal creation."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("items", "survival", "health", "route"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 1, "survival health smoke must emit bot commands"),
            MetricCheck("route_failures", "eq", 0, "survival health smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "survival health smoke must assign an item goal"),
            MetricCheck("last_item_goal_area", "gt", 0, "survival health smoke goal must resolve to an AAS area"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                69,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=1,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route",
                "eq",
                1,
                "reserved smoke must identify the survival-route proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route_kind",
                "eq",
                "health",
                "reserved smoke must identify the health survival-route flavor",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_low_health_boosts",
                "ge",
                1,
                "survival health smoke must boost item utility from low health",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_goal_assignments",
                "ge",
                1,
                "survival health smoke must record a health goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_candidates",
                "ge",
                1,
                "survival health smoke must evaluate a health candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_seek_decisions",
                "ge",
                1,
                "survival health smoke must choose a health seek decision",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_utility_kind_name",
                "any_eq",
                "health",
                "survival health smoke must classify a selected utility as health",
            ),
        ),
    ),
    Scenario(
        name="survival_armor_route",
        title="Survival armor route",
        smoke_mode=70,
        description=(
            "Runs a one-bot FFA item-routing smoke that stages full health, "
            "no armor, and a routeable jacket armor pickup without item "
            "focus. The proof hard-gates natural low-armor utility boosts, "
            "armor candidate/seek telemetry, armor goal assignment, and "
            "route-clean AAS item-goal creation."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("items", "survival", "armor", "route"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 1, "survival armor smoke must emit bot commands"),
            MetricCheck("route_failures", "eq", 0, "survival armor smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "survival armor smoke must assign an item goal"),
            MetricCheck("last_item_goal_area", "gt", 0, "survival armor smoke goal must resolve to an AAS area"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                70,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=1,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route",
                "eq",
                1,
                "reserved smoke must identify the shared survival-route proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route_kind",
                "eq",
                "armor",
                "reserved smoke must identify the armor survival-route flavor",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_low_armor_boosts",
                "ge",
                1,
                "survival armor smoke must boost item utility from low armor",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_armor_goal_assignments",
                "ge",
                1,
                "survival armor smoke must record an armor goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_armor_candidates",
                "ge",
                1,
                "survival armor smoke must evaluate an armor candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_armor_seek_decisions",
                "ge",
                1,
                "survival armor smoke must choose an armor seek decision",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_utility_kind_name",
                "any_eq",
                "armor",
                "survival armor smoke must classify a selected utility as armor",
            ),
        ),
    ),
    Scenario(
        name="combat_survival_regression",
        title="Combat survival regression",
        smoke_mode=71,
        description=(
            "Runs a two-bot FFA regression smoke that stages the lead bot at "
            "low health with a routeable medium health pickup while a visible, "
            "shootable enemy is placed nearby. The proof hard-gates that "
            "combat target facts remain visible to the blackboard/action layer "
            "while natural survival item pressure and recovery ownership can "
            "safely suppress attack input."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("combat", "survival", "items", "regression"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("commands", "ge", 2, "combined combat/survival smoke must emit commands"),
            MetricCheck("route_failures", "eq", 0, "combined combat/survival smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "combined smoke must assign a survival item goal"),
            MetricCheck("last_item_goal_area", "gt", 0, "survival item goal must resolve to an AAS area"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                71,
                combat="engage_enemy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route",
                "eq",
                1,
                "combined smoke must enable survival-route pressure",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route_kind",
                "eq",
                "combat_health",
                "combined smoke must identify the combat/survival route flavor",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "combined smoke must acquire a combat target",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "combined smoke target must be visible",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "combined smoke target must be shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "combined smoke action layer must retain a combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "combined smoke action layer must see the combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "combined smoke action layer must consider the target shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_withheld_fire",
                "ge",
                1,
                "combined smoke must prove survival pressure can suppress fire",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_low_health_boosts",
                "ge",
                1,
                "combined smoke must boost item utility from low health",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_candidates",
                "ge",
                1,
                "combined smoke must evaluate a health candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_seek_decisions",
                "ge",
                1,
                "combined smoke must choose a health seek decision",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_goal_assignments",
                "ge",
                1,
                "combined smoke must record a health goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_utility_kind_name",
                "any_eq",
                "health",
                "combined smoke must classify the selected utility as health",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_item_candidates",
                "ge",
                1,
                "behavior arbitration must see the survival item candidate",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_recovery_candidates",
                "ge",
                1,
                "behavior arbitration must see recovery pressure under threat",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_item_owners",
                "ge",
                1,
                "behavior arbitration must allow the survival item route to own frames",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_recovery_owners",
                "ge",
                1,
                "behavior arbitration must allow recovery to preempt attack under pressure",
            ),
        ),
    ),
    Scenario(
        name="combat_survival_regression_q2dm2",
        title="Combat survival regression on q2dm2",
        smoke_mode=71,
        description=(
            "Runs the compact two-bot FFA combat/survival regression on the "
            "q2dm2 reference map. This reuses the mode 71 live setup away "
            "from the default smoke map and hard-gates that visible enemy "
            "facts, withheld fire, low-health item routing, item ownership, "
            "and recovery ownership remain route-clean on a larger DM layout."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        map_name="q2dm2",
        selection_tags=("combat", "survival", "items", "regression", "maps", "q2dm2"),
        checks=(
            MetricCheck("pass", "eq", 1, "q2dm2 combat/survival smoke status must pass"),
            MetricCheck("commands", "ge", 2, "q2dm2 combined smoke must emit commands"),
            MetricCheck("route_failures", "eq", 0, "q2dm2 combined smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "q2dm2 combined smoke must assign a survival item goal"),
            MetricCheck("last_item_goal_area", "gt", 0, "q2dm2 survival item goal must resolve to an AAS area"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                71,
                combat="engage_enemy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "map",
                "eq",
                "q2dm2",
                "q2dm2 regression must run on the second reference map",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route",
                "eq",
                1,
                "q2dm2 combined smoke must enable survival-route pressure",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route_kind",
                "eq",
                "combat_health",
                "q2dm2 combined smoke must identify the combat/survival route flavor",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "q2dm2 combined smoke must acquire a combat target",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "q2dm2 combined smoke target must be visible",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "q2dm2 combined smoke target must be shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "q2dm2 combined smoke action layer must retain a combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "q2dm2 combined smoke action layer must see the combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "q2dm2 combined smoke action layer must consider the target shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_withheld_fire",
                "ge",
                1,
                "q2dm2 combined smoke must prove survival pressure can suppress fire",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_low_health_boosts",
                "ge",
                1,
                "q2dm2 combined smoke must boost item utility from low health",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_candidates",
                "ge",
                1,
                "q2dm2 combined smoke must evaluate a health candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_seek_decisions",
                "ge",
                1,
                "q2dm2 combined smoke must choose a health seek decision",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_goal_assignments",
                "ge",
                1,
                "q2dm2 combined smoke must record a health goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_utility_kind_name",
                "any_eq",
                "health",
                "q2dm2 combined smoke must classify the selected utility as health",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_item_candidates",
                "ge",
                1,
                "q2dm2 behavior arbitration must see the survival item candidate",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_recovery_candidates",
                "ge",
                1,
                "q2dm2 behavior arbitration must see recovery pressure under threat",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_item_owners",
                "ge",
                1,
                "q2dm2 behavior arbitration must allow the survival item route to own frames",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_recovery_owners",
                "ge",
                1,
                "q2dm2 behavior arbitration must allow recovery to preempt attack under pressure",
            ),
        ),
    ),
    Scenario(
        name="threat_retreat_avoidance",
        title="Threat retreat avoidance",
        smoke_mode=72,
        description=(
            "Runs a two-bot FFA live-threat smoke that stages the lead bot at "
            "low health with a nearby enemy but leaves the older smoke combat "
            "cvar disabled. The proof hard-gates low-health threat retreat "
            "activation, short timed route-goal ownership, attack suppression "
            "during the retreat window, and a later live re-engagement."
        ),
        task_ids=("FR-04-T03", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        selection_tags=("combat", "survival", "retreat", "avoidance", "regression"),
        checks=(
            MetricCheck("pass", "eq", 1, "threat-retreat smoke status must pass"),
            MetricCheck("commands", "ge", 2, "threat-retreat smoke must emit commands"),
            MetricCheck("route_commands", "ge", 2, "threat-retreat smoke must keep route ownership active"),
            MetricCheck("route_failures", "eq", 0, "threat-retreat smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                72,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "threat_retreat",
                "eq",
                1,
                "reserved smoke must enable the threat-retreat proof lane",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "threat-retreat smoke must acquire a live target",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "threat-retreat smoke target must be visible",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "threat-retreat smoke target must be shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "threat-retreat action layer must retain a combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "threat-retreat action layer must see the combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "threat-retreat action layer must consider the target shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_applied_attack_buttons",
                "ge",
                1,
                "threat-retreat smoke must re-engage with applied attack input",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_requests",
                "ge",
                1,
                "threat-retreat policy must evaluate requests",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_enemy_sources",
                "ge",
                1,
                "threat-retreat policy must use the live enemy as a source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_activations",
                "ge",
                1,
                "threat-retreat policy must activate a retreat window",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_route_requests",
                "ge",
                1,
                "threat-retreat policy must own at least one route request",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_attack_suppressions",
                "ge",
                1,
                "threat-retreat policy must suppress attack while retreating",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_reengages",
                "ge",
                1,
                "threat-retreat policy must record post-retreat re-engagement",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_threat_retreat_low_health",
                "eq",
                1,
                "threat-retreat status must record the low-health trigger",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_route_owners",
                "ge",
                1,
                "behavior arbitration must record route ownership during retreat",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_combat_owners",
                "ge",
                1,
                "behavior arbitration must record combat ownership after re-engagement",
            ),
        ),
    ),
    Scenario(
        name="combat_survival_regression_q2dm8",
        title="Combat survival regression on q2dm8",
        smoke_mode=71,
        description=(
            "Runs the compact two-bot FFA combat/survival regression on the "
            "q2dm8 reference map. This widens the map-matrix proof beyond "
            "mm-rage and q2dm2 while hard-gating visible enemy facts, withheld "
            "fire, low-health item routing, item ownership, and recovery "
            "ownership on another stocked DM layout."
        ),
        task_ids=("FR-04-T03", "FR-04-T11", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        map_name="q2dm8",
        selection_tags=("combat", "survival", "items", "regression", "maps", "q2dm8"),
        checks=(
            MetricCheck("pass", "eq", 1, "q2dm8 combat/survival smoke status must pass"),
            MetricCheck("commands", "ge", 2, "q2dm8 combined smoke must emit commands"),
            MetricCheck("route_failures", "eq", 0, "q2dm8 combined smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "q2dm8 combined smoke must assign a survival item goal"),
            MetricCheck("last_item_goal_area", "gt", 0, "q2dm8 survival item goal must resolve to an AAS area"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                71,
                combat="engage_enemy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "map",
                "eq",
                "q2dm8",
                "q2dm8 regression must run on the third reference DM map",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route",
                "eq",
                1,
                "q2dm8 combined smoke must enable survival-route pressure",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route_kind",
                "eq",
                "combat_health",
                "q2dm8 combined smoke must identify the combat/survival route flavor",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "q2dm8 combined smoke must acquire a combat target",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "q2dm8 combined smoke target must be visible",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "q2dm8 combined smoke target must be shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "q2dm8 combined smoke action layer must retain a combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "q2dm8 combined smoke action layer must see the combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "q2dm8 combined smoke action layer must consider the target shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_withheld_fire",
                "ge",
                1,
                "q2dm8 combined smoke must prove survival pressure can suppress fire",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_low_health_boosts",
                "ge",
                1,
                "q2dm8 combined smoke must boost item utility from low health",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_candidates",
                "ge",
                1,
                "q2dm8 combined smoke must evaluate a health candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_seek_decisions",
                "ge",
                1,
                "q2dm8 combined smoke must choose a health seek decision",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_goal_assignments",
                "ge",
                1,
                "q2dm8 combined smoke must record a health goal assignment",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_item_candidates",
                "ge",
                1,
                "q2dm8 behavior arbitration must see the survival item candidate",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_recovery_candidates",
                "ge",
                1,
                "q2dm8 behavior arbitration must see recovery pressure under threat",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_item_owners",
                "ge",
                1,
                "q2dm8 behavior arbitration must allow the survival item route to own frames",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_recovery_owners",
                "ge",
                1,
                "q2dm8 behavior arbitration must allow recovery to preempt attack under pressure",
            ),
        ),
    ),
    Scenario(
        name="threat_retreat_avoidance_q2dm8",
        title="Threat retreat avoidance on q2dm8",
        smoke_mode=72,
        description=(
            "Runs the low-health live-threat retreat proof on q2dm8. This "
            "widens the threat-retreat regression beyond the default smoke "
            "map while hard-gating the same live threat source, timed retreat "
            "route, attack suppression, and post-retreat re-engagement "
            "contract."
        ),
        task_ids=("FR-04-T03", "FR-04-T11", "FR-04-T15", "DV-03-T05"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
        ),
        map_name="q2dm8",
        selection_tags=("combat", "survival", "retreat", "avoidance", "regression", "maps", "q2dm8"),
        checks=(
            MetricCheck("pass", "eq", 1, "q2dm8 threat-retreat smoke status must pass"),
            MetricCheck("commands", "ge", 2, "q2dm8 threat-retreat smoke must emit commands"),
            MetricCheck("route_commands", "ge", 2, "q2dm8 threat-retreat smoke must keep route ownership active"),
            MetricCheck("route_failures", "eq", 0, "q2dm8 threat-retreat smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                72,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "map",
                "eq",
                "q2dm8",
                "q2dm8 threat-retreat regression must run on the third reference DM map",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "threat_retreat",
                "eq",
                1,
                "q2dm8 reserved smoke must enable the threat-retreat proof lane",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "q2dm8 threat-retreat smoke must acquire a live target",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "q2dm8 threat-retreat smoke target must be visible",
            ),
            MarkerMetricCheck(
                BLACKBOARD_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "q2dm8 threat-retreat smoke target must be shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_acquisitions",
                "ge",
                1,
                "q2dm8 threat-retreat action layer must retain a combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_visible",
                "ge",
                1,
                "q2dm8 threat-retreat action layer must see the combat target",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "combat_enemy_shootable",
                "ge",
                1,
                "q2dm8 threat-retreat action layer must consider the target shootable",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_applied_attack_buttons",
                "ge",
                1,
                "q2dm8 threat-retreat smoke must re-engage with applied attack input",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_requests",
                "ge",
                1,
                "q2dm8 threat-retreat policy must evaluate requests",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_enemy_sources",
                "ge",
                1,
                "q2dm8 threat-retreat policy must use the live enemy as a source",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_activations",
                "ge",
                1,
                "q2dm8 threat-retreat policy must activate a retreat window",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_route_requests",
                "ge",
                1,
                "q2dm8 threat-retreat policy must own at least one route request",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_attack_suppressions",
                "ge",
                1,
                "q2dm8 threat-retreat policy must suppress attack while retreating",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "threat_retreat_reengages",
                "ge",
                1,
                "q2dm8 threat-retreat policy must record post-retreat re-engagement",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_threat_retreat_low_health",
                "eq",
                1,
                "q2dm8 threat-retreat status must record the low-health trigger",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_route_owners",
                "ge",
                1,
                "q2dm8 behavior arbitration must record route ownership during retreat",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_combat_owners",
                "ge",
                1,
                "q2dm8 behavior arbitration must record combat ownership after re-engagement",
            ),
        ),
    ),
    Scenario(
        name="profile_role_policy",
        title="Profile role policy",
        smoke_mode=53,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs so "
            "WORR_ROLE metadata travels through server bot userinfo into the "
            "match-policy requested-role path. The status checks verify "
            "attacker, defender, and support/midfield selections are honored "
            "without enabling a separate proof cvar."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
        ),
        selection_tags=("profiles", "match", "tdm", "behavior", "roles"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "profile role smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "profile role smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                53,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "profile_role_policy",
                "eq",
                1,
                "reserved smoke must identify the profile-role proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "profile role smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "profile role smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "profile role smoke must evaluate TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_requested",
                "ge",
                3,
                "profile role smoke must turn profile roles into match requested roles",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_requested_honored",
                "ge",
                3,
                "profile role smoke must honor profile-derived requested roles",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_role",
                "ge",
                3,
                "profile role smoke must record profile role inputs",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_role_honored",
                "ge",
                3,
                "profile role smoke must honor profile role inputs",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_role_fallbacks",
                "eq",
                0,
                "supported profile roles should not require match-policy fallback",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_attack",
                "ge",
                1,
                "attacker profiles must produce attack match policy selections",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_defend",
                "ge",
                1,
                "defender profiles must produce defense match policy selections",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_midfield",
                "ge",
                1,
                "support profiles must produce midfield/support match policy selections",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_role",
                "ge",
                1,
                "profile role smoke must expose the last profile-derived role",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_requested_role",
                "ge",
                1,
                "profile role smoke must expose the last requested role",
            ),
        ),
    ),
    Scenario(
        name="profile_team_policy",
        title="Profile team policy",
        smoke_mode=54,
        description=(
            "Runs a four-bot CTF smoke with first-party bot profile IDs so "
            "WORR_TEAMPLAY_BIAS, WORR_OBJECTIVE_BIAS, and "
            "WORR_FRIENDLY_FIRE_CARE metadata travel through server bot "
            "userinfo into match-policy priority, resource-sharing, objective, "
            "and teammate-care decisions."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
        ),
        selection_tags=("profiles", "match", "ctf", "behavior", "team-policy"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "profile team-policy smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "profile team-policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                54,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "profile_team_policy",
                "eq",
                1,
                "reserved smoke must identify the profile team-policy proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "profile team-policy smoke must run in a CTF match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "profile team-policy smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_ctf",
                "ge",
                1,
                "profile team-policy smoke must evaluate CTF match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_teamplay_bias",
                "ge",
                3,
                "profile team-policy smoke must record teamplay profile hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_objective_bias",
                "ge",
                3,
                "profile team-policy smoke must record objective profile hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_friendly_fire_care",
                "ge",
                3,
                "profile team-policy smoke must record friendly-fire-care profile hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_teamplay_applied",
                "ge",
                3,
                "profile team-policy smoke must apply teamplay profile hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_objective_applied",
                "ge",
                3,
                "profile team-policy smoke must apply objective profile hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_friendly_fire_applied",
                "ge",
                3,
                "profile team-policy smoke must apply friendly-fire-care profile hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_friendly_fire",
                "ge",
                1,
                "profile friendly-fire care must request teammate-aware match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_teamplay_bias",
                "ge",
                500,
                "profile team-policy smoke must expose the last teamplay bias value",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_objective_bias",
                "ge",
                500,
                "profile team-policy smoke must expose the last objective bias value",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_friendly_fire_care",
                "ge",
                500,
                "profile team-policy smoke must expose the last friendly-fire-care value",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_teamplay_bonus",
                "gt",
                0,
                "profile teamplay bias must contribute a positive policy bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_objective_bonus",
                "gt",
                0,
                "profile objective bias must contribute a positive policy bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_friendly_fire_bonus",
                "gt",
                0,
                "profile friendly-fire care must contribute a positive policy bonus",
            ),
        ),
    ),
    Scenario(
        name="profile_item_policy",
        title="Profile item policy",
        smoke_mode=55,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs so "
            "WORR_ITEM_GREED, WORR_ITEM_DENIAL, WORR_POWERUP_TIMING, and "
            "WORR_RETREAT_HEALTH metadata travel through server bot userinfo "
            "into match item-policy priority, item-role scoring, and "
            "resource-denial scoring."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_match_item_policy", "1"),
            ("sg_bot_profile_item_policy_smoke", "1"),
        ),
        selection_tags=("profiles", "match", "tdm", "behavior", "items", "resources"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "profile item-policy smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "profile item-policy smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "profile item-policy smoke must assign an item goal"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                55,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "profile_item_policy",
                "eq",
                1,
                "reserved smoke must identify the profile item-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "match_item_policy",
                "eq",
                1,
                "profile item-policy smoke must enable match item-policy scoring",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_item_roles",
                "eq",
                0,
                "profile item-policy smoke must not set the individual team item-role cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_resource_denial",
                "eq",
                0,
                "profile item-policy smoke must not set the individual resource-denial cvar",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "profile item-policy smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "profile item-policy smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "profile item-policy smoke must evaluate TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_item_greed",
                "ge",
                3,
                "profile item-policy smoke must record item-greed hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_item_denial",
                "ge",
                3,
                "profile item-policy smoke must record item-denial hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_powerup_timing",
                "ge",
                3,
                "profile item-policy smoke must record powerup-timing hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_retreat_health",
                "ge",
                3,
                "profile item-policy smoke must record retreat-health hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_item_greed_applied",
                "ge",
                3,
                "profile item-policy smoke must apply item-greed hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_item_denial_applied",
                "ge",
                3,
                "profile item-policy smoke must apply item-denial hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_powerup_timing_applied",
                "ge",
                3,
                "profile item-policy smoke must apply powerup-timing hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_retreat_health_applied",
                "ge",
                1,
                "profile item-policy smoke must exercise retreat-health hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_profile_item_bonuses",
                "ge",
                1,
                "profile item-policy smoke must add profile bonuses to item-role policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_resource_policy_profile_item_bonuses",
                "ge",
                1,
                "profile item-policy smoke must add profile bonuses to resource policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_item_greed",
                "ge",
                500,
                "profile item-policy smoke must expose the last item-greed value",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_item_denial",
                "ge",
                500,
                "profile item-policy smoke must expose the last item-denial value",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_powerup_timing",
                "ge",
                500,
                "profile item-policy smoke must expose the last powerup-timing value",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_retreat_health",
                "gt",
                0,
                "profile item-policy smoke must expose the last retreat-health value",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_item_greed_bonus",
                "gt",
                0,
                "profile item greed must contribute a positive policy bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_item_denial_bonus",
                "gt",
                0,
                "profile item denial must contribute a positive policy bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_powerup_timing_bonus",
                "gt",
                0,
                "profile powerup timing must contribute a positive policy bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_retreat_health_bonus",
                "gt",
                0,
                "profile retreat-health threshold must contribute a positive policy bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_item_role_profile_item_bonus",
                "gt",
                0,
                "profile item-policy smoke must expose the last item-role profile bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_resource_profile_item_bonus",
                "gt",
                0,
                "profile item-policy smoke must expose the last resource profile bonus",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_item_role_selected_goals",
                "ge",
                1,
                "profile item-policy smoke must select a role-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "team_resource_denial_selected_goals",
                "ge",
                1,
                "profile item-policy smoke must select a denial-shaped pickup goal",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_item_role_profile_item_bonus",
                "gt",
                0,
                "profile item-policy smoke must expose nav item-role profile bonus",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "last_team_resource_denial_profile_item_bonus",
                "gt",
                0,
                "profile item-policy smoke must expose nav resource-denial profile bonus",
            ),
        ),
    ),
    Scenario(
        name="profile_movement_policy",
        title="Profile movement policy",
        smoke_mode=56,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs so "
            "WORR_MOVEMENT_STYLE metadata travels through server bot userinfo "
            "into match-policy attack, defense, roam, collect, and selected "
            "role priority bonuses."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_profile_movement_policy_smoke", "1"),
        ),
        selection_tags=("profiles", "match", "tdm", "behavior", "movement"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "profile movement-policy smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "profile movement-policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                56,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "profile_movement_policy",
                "eq",
                1,
                "reserved smoke must identify the profile movement-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "match_item_policy",
                "eq",
                0,
                "profile movement-policy smoke must not rely on match item-policy scoring",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "profile movement-policy smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "profile movement-policy smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "profile movement-policy smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "profile movement-policy smoke must evaluate TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_movement_style",
                "ge",
                3,
                "profile movement-policy smoke must record movement-style hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_movement_attack",
                "ge",
                2,
                "profile movement-policy smoke must record attack movement styles",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_movement_defense",
                "ge",
                1,
                "profile movement-policy smoke must record defense movement styles",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_movement_roam",
                "ge",
                1,
                "profile movement-policy smoke must record roam movement styles",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_profile_movement_applied",
                "ge",
                3,
                "profile movement-policy smoke must apply movement-style hints",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_attack",
                "ge",
                1,
                "profile movement-policy smoke must retain attacker selections",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_defend",
                "ge",
                1,
                "profile movement-policy smoke must retain defender selections",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_midfield",
                "ge",
                1,
                "profile movement-policy smoke must retain midfield selections",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_movement_style",
                "gt",
                0,
                "profile movement-policy smoke must expose the last movement style",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_movement_bonus",
                "gt",
                0,
                "profile movement style must contribute a selected-role bonus",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_match_profile_movement_attack_bonus",
                "gt",
                0,
                "profile movement style must expose the attack bonus",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_policy",
        title="Bot chat policy live dispatch",
        smoke_mode=57,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs and "
            "sg_bot_allow_chat enabled, then verifies chat metadata is visible "
            "while the first conservative live chat consumer submits a "
            "rate-limited policy-gated utterance."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "chat policy smoke must retain route commands"),
            MetricCheck("route_failures", "eq", 0, "chat policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                57,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "reserved smoke must identify the bot chat-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "reserved smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "bot chat-policy smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "chat policy status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "chat policy status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "chat policy status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "chat policy status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "chat policy status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                1,
                "chat policy smoke must attempt at least one bot chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                1,
                "chat policy smoke must submit at least one bot chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "chat policy smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled chat gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_team_policy",
        title="Bot chat team-only policy",
        smoke_mode=58,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs, "
            "sg_bot_allow_chat enabled, and sg_bot_chat_team_only enabled, "
            "then verifies the conservative live chat consumer dispatches "
            "through the team-only audience path."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_team_only", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "chat team-policy smoke must retain route commands"),
            MetricCheck("route_failures", "eq", 0, "chat team-policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                58,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "reserved smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_team_policy",
                "eq",
                1,
                "reserved smoke must identify the team-only chat-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "reserved smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "chat_team_only",
                "eq",
                1,
                "reserved smoke must enable the team-only chat audience gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "bot chat team-policy smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "chat team-policy status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "chat team-policy status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "chat team-policy status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "chat team-policy status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "team_only",
                "eq",
                1,
                "chat team-policy status must echo the team-only audience gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "chat team-policy status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                1,
                "chat team-policy smoke must attempt at least one bot chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                1,
                "chat team-policy smoke must submit at least one bot chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "chat team-policy smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_dispatch_team",
                "eq",
                1,
                "team-only chat policy must dispatch through the team audience path",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled chat gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_rate_policy",
        title="Bot chat rate-limit policy",
        smoke_mode=59,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs, "
            "sg_bot_allow_chat enabled, and sg_bot_chat_min_interval_ms set "
            "to a high global interval, then verifies the conservative live "
            "chat consumer submits the first dispatch and rate-limits later "
            "bot attempts."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_min_interval_ms", "60000"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "chat rate-policy smoke must retain route commands"),
            MetricCheck("route_failures", "eq", 0, "chat rate-policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                59,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "reserved smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_rate_policy",
                "eq",
                1,
                "reserved smoke must identify the rate-limit chat-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "reserved smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "chat_min_interval_ms",
                "eq",
                60000,
                "reserved smoke must enable the global bot chat rate interval",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "bot chat rate-policy smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "chat rate-policy status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "chat rate-policy status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "chat rate-policy status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "chat rate-policy status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "team_only",
                "eq",
                0,
                "chat rate-policy status must leave the team-only audience gate disabled",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "rate_limit_ms",
                "eq",
                60000,
                "chat rate-policy status must echo the configured global interval",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "chat rate-policy status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                4,
                "chat rate-policy smoke must attempt dispatch from the staged bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                1,
                "chat rate-policy smoke must submit the first bot chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "ge",
                1,
                "chat rate-policy smoke must rate-limit later bot chat attempts",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "chat rate-policy smoke must not count rate limits as dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled chat gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_initial_policy",
        title="Bot chat initial personality policy",
        smoke_mode=60,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs and "
            "sg_bot_allow_chat enabled, then verifies profile chat "
            "personalities select deterministic initial utterance buckets "
            "before live dispatch."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "chat initial-policy smoke must retain route commands"),
            MetricCheck("route_failures", "eq", 0, "chat initial-policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                60,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "reserved smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_initial_policy",
                "eq",
                1,
                "reserved smoke must identify the initial chat-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "reserved smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "bot chat initial-policy smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "chat initial-policy status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "chat initial-policy status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "chat initial-policy status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "chat initial-policy status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "chat initial-policy status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                1,
                "chat initial-policy smoke must submit at least one bot chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "chat initial-policy smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                4,
                "chat initial-policy smoke must select one initial utterance for each staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_known_personalities",
                "eq",
                4,
                "chat initial-policy smoke must recognize all staged profile personalities",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_unknown_personalities",
                "eq",
                0,
                "chat initial-policy smoke must not fall back for staged profile personalities",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_quiet",
                "eq",
                1,
                "chat initial-policy smoke must count the quiet profile bucket",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_direct",
                "eq",
                1,
                "chat initial-policy smoke must count the direct profile bucket",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_helpful",
                "eq",
                1,
                "chat initial-policy smoke must count the helpful profile bucket",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_steady",
                "eq",
                1,
                "chat initial-policy smoke must count the steady profile bucket",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_initial_chat_personality",
                "gt",
                0,
                "chat initial-policy smoke must record the latest personality bucket",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_initial_chat_phrase",
                "gt",
                0,
                "chat initial-policy smoke must record the latest selected phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled chat gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_reply_policy",
        title="Bot chat reply policy",
        smoke_mode=61,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs, "
            "sg_bot_allow_chat enabled, and the reply-policy smoke gate set, "
            "then verifies profile chat personalities select deterministic "
            "reply utterances for the first team-ready proof event."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_reply_policy_smoke", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "chat reply-policy smoke must retain route commands"),
            MetricCheck("route_failures", "eq", 0, "chat reply-policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                61,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "reserved smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_reply_policy",
                "eq",
                1,
                "reserved smoke must identify the reply chat-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "reserved smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "bot chat reply-policy smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "chat reply-policy status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "chat reply-policy status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "chat reply-policy status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "chat reply-policy status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "chat reply-policy status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                8,
                "chat reply-policy smoke must submit initial and reply dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "chat reply-policy smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                4,
                "chat reply-policy smoke must still select initial utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "chat reply-policy smoke must enable the reply selector",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "eq",
                4,
                "chat reply-policy smoke must observe one reply event per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_selections",
                "eq",
                4,
                "chat reply-policy smoke must select one reply utterance per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_known_personalities",
                "eq",
                4,
                "chat reply-policy smoke must recognize all staged reply personalities",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_unknown_personalities",
                "eq",
                0,
                "chat reply-policy smoke must not fall back for staged reply personalities",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_team_ready",
                "eq",
                4,
                "chat reply-policy smoke must record the team-ready reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "eq",
                4,
                "chat reply-policy smoke must submit all reply utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "eq",
                0,
                "chat reply-policy smoke must not rate-limit replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "chat reply-policy smoke must not record reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "eq",
                1,
                "chat reply-policy smoke must record the latest reply event kind",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "chat reply-policy smoke must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled chat gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_event_policy",
        title="Bot chat event policy",
        smoke_mode=62,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs, "
            "sg_bot_allow_chat enabled, and the event-policy smoke gate set, "
            "then verifies profile chat personalities select deterministic "
            "reply utterances for both team-ready and route-ready proof events."
        ),
        task_ids=("FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_event_policy_smoke", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "chat event-policy smoke must retain route commands"),
            MetricCheck("route_failures", "eq", 0, "chat event-policy smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                62,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "reserved smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                1,
                "reserved smoke must identify the event chat-policy proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "reserved smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "bot chat event-policy smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "chat event-policy status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "chat event-policy status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "chat event-policy status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "chat event-policy status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "chat event-policy status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live chat dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                12,
                "chat event-policy smoke must submit initial and both event reply dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "chat event-policy smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                4,
                "chat event-policy smoke must still select initial utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "chat event-policy smoke must enable the reply selector",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "eq",
                8,
                "chat event-policy smoke must observe two reply events per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_selections",
                "eq",
                8,
                "chat event-policy smoke must select two reply utterances per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_known_personalities",
                "eq",
                8,
                "chat event-policy smoke must recognize all staged reply personalities per event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_unknown_personalities",
                "eq",
                0,
                "chat event-policy smoke must not fall back for staged reply personalities",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_team_ready",
                "eq",
                4,
                "chat event-policy smoke must record the team-ready reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                4,
                "chat event-policy smoke must record the route-ready reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "eq",
                8,
                "chat event-policy smoke must submit all event reply utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "eq",
                0,
                "chat event-policy smoke must not rate-limit event replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "chat event-policy smoke must not record reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "eq",
                2,
                "chat event-policy smoke must record the latest route-ready reply event kind",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "chat event-policy smoke must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled chat gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_live_events",
        title="Bot chat live event taxonomy",
        smoke_mode=79,
        description=(
            "Runs a four-bot TDM smoke with first-party bot profile IDs, "
            "sg_bot_allow_chat enabled, and sg_bot_chat_live_events enabled, "
            "then verifies live spawn and route-ready frame-command outcomes "
            "feed the safe chat event pipeline without the smoke-only event "
            "gate."
        ),
        task_ids=("FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_live_events", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior", "live"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "live chat event smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "live chat event smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                79,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "live event smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                0,
                "live event smoke must not rely on the smoke-only event-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "live event smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                0,
                "live event smoke must leave cooldown stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "live event smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "live event smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "live event chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "live event chat status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "live event chat status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "live event chat status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "live event chat status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live event dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                8,
                "live event smoke must attempt at least initial and route-ready dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                8,
                "live event smoke must submit initial and live route-ready utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "eq",
                0,
                "live event smoke must not rate-limit the uncapped proof",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "live event smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "rate_limit_ms",
                "eq",
                0,
                "live event smoke must leave the global chat cooldown disabled",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                4,
                "live event smoke must still select initial utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "live event smoke must enable reply/event selection",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                4,
                "live event smoke must observe at least one live route-ready event per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_team_ready",
                "eq",
                0,
                "live event smoke must not synthesize team-ready smoke events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                4,
                "live event smoke must record live route-ready events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "ge",
                4,
                "live event smoke must submit at least all live route-ready utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "eq",
                0,
                "live event smoke must not rate-limit live route-ready events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "live event smoke must not record live reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enabled",
                "eq",
                1,
                "live event status must echo the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_events",
                "ge",
                8,
                "live event status must count at least spawn and route-ready events per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "eq",
                4,
                "live event status must isolate spawn live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "eq",
                4,
                "live event status must isolate route-ready live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_submitted",
                "ge",
                8,
                "live event status must submit at least initial and route-ready live utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "live event status must not record live-event dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_event_taxonomy",
                "eq",
                11,
                "live event taxonomy must expose the supported event set size",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event",
                "ge",
                2,
                "live event status must record a gameplay live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "ge",
                2,
                "live event status must record a gameplay reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "live event status must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled live event gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_live_event_cooldown",
        title="Bot chat live event cooldown",
        smoke_mode=80,
        description=(
            "Runs the four-bot TDM live chat event smoke with the global "
            "chat cooldown enabled, then verifies spawn and route-ready event "
            "selection still happens while the dispatch layer submits only "
            "the first utterance and rate-limits the rest without failures."
        ),
        task_ids=("FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_live_events", "1"),
            ("sg_bot_chat_min_interval_ms", "60000"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior", "live", "cooldown"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "live chat cooldown smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "live chat cooldown smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                80,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "live cooldown smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                0,
                "live cooldown smoke must not rely on the smoke-only event-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "live cooldown smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                1,
                "live cooldown smoke must identify the cooldown stress lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "live cooldown smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "chat_min_interval_ms",
                "eq",
                60000,
                "live cooldown smoke must configure the global chat cooldown",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "live cooldown smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "live cooldown chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "live cooldown chat status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "live cooldown chat status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "live cooldown chat status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "live cooldown chat status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live cooldown dispatch attempts",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                8,
                "live cooldown smoke must attempt at least initial and route-ready dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "eq",
                1,
                "live cooldown smoke must submit only the first live utterance",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "ge",
                7,
                "live cooldown smoke must rate-limit at least the remaining live utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "live cooldown smoke must not record dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "rate_limit_ms",
                "eq",
                60000,
                "live cooldown status must echo the configured global cooldown",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                4,
                "live cooldown smoke must still select initial utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "live cooldown smoke must enable reply/event selection",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                4,
                "live cooldown smoke must observe at least one live route-ready event per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_team_ready",
                "eq",
                0,
                "live cooldown smoke must not synthesize team-ready smoke events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                4,
                "live cooldown smoke must record live route-ready events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "eq",
                0,
                "live cooldown smoke must rate-limit all route-ready reply submissions",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "ge",
                4,
                "live cooldown smoke must count at least rate-limited route-ready replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "live cooldown smoke must not record live reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enabled",
                "eq",
                1,
                "live cooldown status must echo the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_events",
                "ge",
                8,
                "live cooldown status must count at least spawn and route-ready events per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "eq",
                4,
                "live cooldown status must isolate spawn live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "eq",
                4,
                "live cooldown status must isolate route-ready live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_submitted",
                "eq",
                1,
                "live cooldown status must submit only the first live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_rate_limited",
                "ge",
                7,
                "live cooldown status must count at least the base rate-limited live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "live cooldown status must not record live-event dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_event_taxonomy",
                "eq",
                11,
                "live cooldown taxonomy must expose the supported event set size",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event",
                "ge",
                2,
                "live cooldown status must record a gameplay live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "ge",
                2,
                "live cooldown status must record a gameplay reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "live cooldown status must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled live cooldown gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_live_enemy_sighted",
        title="Bot chat live enemy-sighted event",
        smoke_mode=81,
        description=(
            "Runs the live chat event smoke with combat staging enabled, then "
            "verifies a blackboard-visible enemy produces an enemy_sighted "
            "live chat event through the safe dispatch pipeline."
        ),
        task_ids=("FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_live_events", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior", "live", "combat"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "live enemy-sighted smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "live enemy-sighted smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                81,
                combat="engage_enemy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=3,
            ),
            *marker_metric_checks(
                ACTION_STATUS_MARKER,
                MetricCheck("combat_enemy_acquisitions", "ge", 1, "enemy-sighted chat smoke must acquire a live enemy"),
                MetricCheck("combat_enemy_visible", "ge", 1, "enemy-sighted chat smoke must see a visible enemy"),
                MetricCheck("combat_enemy_shootable", "ge", 1, "enemy-sighted chat smoke must keep the enemy shootable"),
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "live enemy-sighted smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                0,
                "live enemy-sighted smoke must not rely on the smoke-only event-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "live enemy-sighted smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                0,
                "live enemy-sighted smoke must leave cooldown stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_enemy_sighted",
                "eq",
                1,
                "live enemy-sighted smoke must identify the gameplay-event breadth lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "live enemy-sighted smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "live enemy-sighted smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "live enemy-sighted chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                2,
                "live enemy-sighted chat status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                2,
                "live enemy-sighted chat status must observe profile chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "live enemy-sighted chat status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "live enemy-sighted chat status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live enemy-sighted dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                5,
                "live enemy-sighted smoke must attempt spawn, route-ready, and enemy-sighted dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                5,
                "live enemy-sighted smoke must submit the uncapped live utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "eq",
                0,
                "live enemy-sighted smoke must not rate-limit the uncapped proof",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "live enemy-sighted smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "rate_limit_ms",
                "eq",
                0,
                "live enemy-sighted smoke must leave the global chat cooldown disabled",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                2,
                "live enemy-sighted smoke must still select initial utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "live enemy-sighted smoke must enable reply/event selection",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                3,
                "live enemy-sighted smoke must observe route-ready plus enemy-sighted events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_team_ready",
                "eq",
                0,
                "live enemy-sighted smoke must not synthesize team-ready smoke events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                2,
                "live enemy-sighted smoke must still record live route-ready events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enemy_sighted",
                "ge",
                1,
                "live enemy-sighted smoke must record a gameplay-derived enemy event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "ge",
                3,
                "live enemy-sighted smoke must submit route-ready plus enemy-sighted replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "eq",
                0,
                "live enemy-sighted smoke must not rate-limit live replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "live enemy-sighted smoke must not record live reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enabled",
                "eq",
                1,
                "live enemy-sighted status must echo the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_events",
                "ge",
                5,
                "live enemy-sighted status must count spawn, route-ready, and enemy events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "eq",
                2,
                "live enemy-sighted status must isolate spawn live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "eq",
                2,
                "live enemy-sighted status must isolate route-ready live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enemy_sighted",
                "ge",
                1,
                "live enemy-sighted status must isolate gameplay-derived enemy events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_submitted",
                "ge",
                5,
                "live enemy-sighted status must submit all uncapped live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_rate_limited",
                "eq",
                0,
                "live enemy-sighted status must not rate-limit the proof events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "live enemy-sighted status must not record live-event dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_event_taxonomy",
                "eq",
                11,
                "live enemy-sighted taxonomy must preserve the supported event set size",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event",
                "eq",
                6,
                "live enemy-sighted status must record enemy_sighted as the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event_name",
                "eq",
                "enemy_sighted",
                "live enemy-sighted status must name the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "eq",
                6,
                "live enemy-sighted status must record enemy_sighted as the latest reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "live enemy-sighted status must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled live enemy-sighted gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_phrase_library",
        title="Bot chat phrase library breadth",
        smoke_mode=82,
        description=(
            "Runs the four-bot TDM live chat event smoke with the expanded "
            "phrase library, then verifies initial and route-ready live "
            "utterance selection exercise all four phrase variants without "
            "falling back to the smoke-only event gate."
        ),
        task_ids=("FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_live_events", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior", "live", "phrase"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "phrase-library smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "phrase-library smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                82,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "phrase-library smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                0,
                "phrase-library smoke must not rely on the smoke-only event-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "phrase-library smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                0,
                "phrase-library smoke must leave cooldown stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_enemy_sighted",
                "eq",
                0,
                "phrase-library smoke must stay in the route-ready live event lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_phrase_library",
                "eq",
                1,
                "phrase-library smoke must identify the phrase breadth lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "phrase-library smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "phrase-library smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "phrase-library chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "phrase-library chat status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "phrase-library chat status must observe chat metadata on every staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                8,
                "phrase-library smoke must attempt at least initial and route-ready dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                8,
                "phrase-library smoke must submit all uncapped utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "eq",
                0,
                "phrase-library smoke must not rate-limit the uncapped proof",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "phrase-library smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                4,
                "phrase-library smoke must select one initial utterance per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_phrase_variants",
                "eq",
                4,
                "initial phrase library must expose four variants",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_unique_variants",
                "eq",
                4,
                "initial phrase selection must exercise all four variants",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_initial_chat_variant",
                "eq",
                3,
                "initial phrase telemetry must record the latest variant",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "phrase-library smoke must enable reply/event selection",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                4,
                "phrase-library smoke must observe at least one live route-ready event per staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                4,
                "phrase-library smoke must record live route-ready events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enemy_sighted",
                "ge",
                0,
                "phrase-library smoke may also record live enemy-sighted events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "ge",
                4,
                "phrase-library smoke must submit at least all live route-ready utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "phrase-library smoke must not record live reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_phrase_variants",
                "eq",
                4,
                "reply phrase library must expose four variants",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_unique_variants",
                "eq",
                4,
                "reply phrase selection must exercise all four variants",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_variant",
                "ge",
                0,
                "reply phrase telemetry must record the latest variant",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_events",
                "ge",
                8,
                "phrase-library status must count at least spawn and route-ready live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "eq",
                4,
                "phrase-library status must isolate spawn live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "eq",
                4,
                "phrase-library status must isolate route-ready live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enemy_sighted",
                "ge",
                0,
                "phrase-library status may also count enemy-sighted live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_submitted",
                "ge",
                8,
                "phrase-library status must submit at least initial and route-ready live utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "phrase-library status must not record live-event dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_event_taxonomy",
                "eq",
                11,
                "phrase-library status must preserve the supported event set size",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event",
                "ge",
                2,
                "phrase-library status must record a gameplay live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled phrase-library gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_duplicate_suppression",
        title="Bot chat duplicate suppression",
        smoke_mode=83,
        description=(
            "Runs the four-bot TDM chat event smoke with both the smoke-only "
            "route-ready event gate and the live event gate enabled, then "
            "verifies the second same-bot route-ready attempt is suppressed "
            "as a duplicate instead of reaching the public chat dispatcher."
        ),
        task_ids=("FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_event_policy_smoke", "1"),
            ("sg_bot_chat_live_events", "1"),
        ),
        selection_tags=("profiles", "chat", "match", "tdm", "behavior", "live", "duplicate"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "duplicate-suppression smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "duplicate-suppression smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                83,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "duplicate-suppression smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                1,
                "duplicate-suppression smoke must enable the smoke-only event gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "duplicate-suppression smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                0,
                "duplicate-suppression smoke must leave global cooldown stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_duplicate_suppression",
                "eq",
                1,
                "duplicate-suppression smoke must identify the duplicate lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "duplicate-suppression smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "duplicate-suppression chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "duplicate-suppression chat status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                12,
                "duplicate-suppression smoke must submit initial, team-ready, and first route-ready utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                12,
                "duplicate-suppression smoke must submit non-duplicate utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "eq",
                0,
                "duplicate-suppression smoke must not use the global cooldown path",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "duplicate-suppression smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                8,
                "duplicate-suppression smoke must keep the first team-ready and route-ready selections",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_team_ready",
                "eq",
                4,
                "duplicate-suppression smoke must preserve team-ready replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                4,
                "duplicate-suppression smoke must preserve the first route-ready replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_duplicate_suppressed",
                "ge",
                4,
                "duplicate-suppression smoke must suppress the second route-ready attempt per bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "duplicate-suppression smoke must not treat duplicates as reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enabled",
                "eq",
                1,
                "duplicate-suppression smoke must keep live events enabled",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "eq",
                4,
                "duplicate-suppression smoke must preserve spawn live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "eq",
                0,
                "duplicate live route-ready attempts must be suppressed before live route accounting",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_duplicate_suppressed",
                "ge",
                4,
                "duplicate-suppression smoke must count live duplicate suppressions",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "duplicate-suppression smoke must not treat live duplicates as failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "chat_duplicate_window_ms",
                "eq",
                5000,
                "duplicate-suppression status must expose the core duplicate window",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_duplicate_chat_event",
                "eq",
                2,
                "duplicate-suppression status must record route_ready as the latest duplicate event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_duplicate_chat_event_name",
                "eq",
                "route_ready",
                "duplicate-suppression status must name the latest duplicate event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_duplicate_chat_phrase",
                "gt",
                0,
                "duplicate-suppression status must record the suppressed phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_duplicate_chat_elapsed_ms",
                "ge",
                0,
                "duplicate-suppression status must record the duplicate elapsed time",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled duplicate-suppression gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_live_low_health",
        title="Bot chat live low-health event",
        smoke_mode=84,
        description=(
            "Runs a one-bot FFA survival-health route with live bot chat "
            "enabled, then verifies real low-health item pressure emits the "
            "low_health gameplay chat event through the safe reply pipeline."
        ),
        task_ids=("FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_live_events", "1"),
        ),
        selection_tags=("profiles", "chat", "survival", "health", "route", "live"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "live low-health smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "live low-health smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "live low-health smoke must assign a health item goal"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                84,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=1,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route",
                "eq",
                1,
                "live low-health smoke must identify the survival-route proof lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "survival_route_kind",
                "eq",
                "health",
                "live low-health smoke must identify the health survival-route flavor",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "live low-health smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                0,
                "live low-health smoke must not rely on the smoke-only event-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "live low-health smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                0,
                "live low-health smoke must leave cooldown stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_enemy_sighted",
                "eq",
                0,
                "live low-health smoke must not stage an enemy-sighted event",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_low_health",
                "eq",
                1,
                "live low-health smoke must identify the low-health event lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_phrase_library",
                "eq",
                0,
                "live low-health smoke must not use the phrase-library stress lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_duplicate_suppression",
                "eq",
                0,
                "live low-health smoke must leave duplicate suppression stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "live low-health smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "live low-health smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_low_health_boosts",
                "ge",
                1,
                "live low-health smoke must boost item utility from low health",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_goal_assignments",
                "ge",
                1,
                "live low-health smoke must record a health goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_candidates",
                "ge",
                1,
                "live low-health smoke must evaluate a health candidate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_seek_decisions",
                "ge",
                1,
                "live low-health smoke must choose a health seek decision",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_utility_kind_name",
                "any_eq",
                "health",
                "live low-health smoke must classify a selected utility as health",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "live low-health chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                1,
                "live low-health chat status must see the staged profile bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                1,
                "live low-health chat status must observe profile chat metadata on the staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "live low-health chat status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "live low-health chat status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live low-health dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                3,
                "live low-health smoke must attempt initial, route-ready, and low-health dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                3,
                "live low-health smoke must submit initial, route-ready, and low-health utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "eq",
                0,
                "live low-health smoke must not rate-limit the uncapped proof",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "live low-health smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "rate_limit_ms",
                "eq",
                0,
                "live low-health smoke must leave the global chat cooldown disabled",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                1,
                "live low-health smoke must still select an initial utterance",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "live low-health smoke must enable reply/event selection",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                2,
                "live low-health smoke must observe route-ready and low-health events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                1,
                "live low-health smoke must preserve the route-ready live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enemy_sighted",
                "eq",
                0,
                "live low-health smoke must not count enemy-sighted replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_low_health",
                "eq",
                1,
                "live low-health smoke must record the low-health reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "ge",
                2,
                "live low-health smoke must submit route-ready and low-health replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "eq",
                0,
                "live low-health smoke must not rate-limit live replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_duplicate_suppressed",
                "eq",
                0,
                "live low-health smoke must not suppress unique live replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "live low-health smoke must not record live reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enabled",
                "eq",
                1,
                "live low-health status must echo the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_events",
                "ge",
                3,
                "live low-health status must count spawn, route-ready, and low-health events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "eq",
                1,
                "live low-health status must isolate the spawn event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "eq",
                1,
                "live low-health status must isolate the route-ready event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enemy_sighted",
                "eq",
                0,
                "live low-health status must not count enemy-sighted live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_low_health",
                "eq",
                1,
                "live low-health status must isolate the low-health live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_submitted",
                "ge",
                3,
                "live low-health status must submit spawn, route-ready, and low-health utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_rate_limited",
                "eq",
                0,
                "live low-health status must not rate-limit live-event dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_duplicate_suppressed",
                "eq",
                0,
                "live low-health status must not suppress unique live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "live low-health status must not record live-event dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_event_taxonomy",
                "eq",
                11,
                "live low-health taxonomy must preserve the supported event set size",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event",
                "eq",
                9,
                "live low-health status must record low_health as the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event_name",
                "eq",
                "low_health",
                "live low-health status must name the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "eq",
                9,
                "live low-health status must record low_health as the latest reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "live low-health status must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled live low-health gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_live_item_taken",
        title="Bot chat live item-taken event",
        smoke_mode=85,
        description=(
            "Runs a one-bot FFA health/armor pickup proof with live bot chat "
            "enabled, then verifies recorded pickup observations emit the "
            "item_taken gameplay chat event through the safe reply pipeline."
        ),
        task_ids=("FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "1"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_live_events", "1"),
        ),
        selection_tags=("profiles", "chat", "items", "pickup", "live"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "live item-taken smoke must issue route commands"),
            MetricCheck("route_failures", "eq", 0, "live item-taken smoke must remain route-clean"),
            MetricCheck("item_goal_assignments", "ge", 1, "live item-taken smoke must assign item goals"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                85,
                combat=0,
                weapon_switch=0,
                item_focus="health_armor",
                team_objective=0,
                target=1,
                gametype=1,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "live item-taken smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                0,
                "live item-taken smoke must not rely on the smoke-only event-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "live item-taken smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                0,
                "live item-taken smoke must leave cooldown stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_enemy_sighted",
                "eq",
                0,
                "live item-taken smoke must not stage an enemy-sighted event",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_low_health",
                "eq",
                0,
                "live item-taken smoke must not identify the low-health event lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_item_taken",
                "eq",
                1,
                "live item-taken smoke must identify the item-taken event lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_phrase_library",
                "eq",
                0,
                "live item-taken smoke must not use the phrase-library stress lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_duplicate_suppression",
                "eq",
                0,
                "live item-taken smoke must leave duplicate suppression stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "live item-taken smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "live item-taken smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_goal_assignments",
                "ge",
                1,
                "live item-taken smoke must record a health goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_armor_goal_assignments",
                "ge",
                1,
                "live item-taken smoke must record an armor goal assignment",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_health_pickups",
                "ge",
                1,
                "live item-taken smoke must record a health pickup observation",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_armor_pickups",
                "ge",
                1,
                "live item-taken smoke must record an armor pickup observation",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_health_pickup_delta",
                "gt",
                0,
                "live item-taken smoke must expose a positive health pickup delta",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "last_armor_pickup_delta",
                "gt",
                0,
                "live item-taken smoke must expose a positive armor pickup delta",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_last_utility_kind_name",
                "any_eq",
                "health",
                "live item-taken smoke must leave the latest utility classification on the health/armor proof lane",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "live item-taken chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                1,
                "live item-taken chat status must see the staged profile bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                1,
                "live item-taken chat status must observe profile chat metadata on the staged bot",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "live item-taken chat status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "live item-taken chat status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live item-taken dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                4,
                "live item-taken smoke must attempt initial, route-ready, low-health, and item-taken dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                4,
                "live item-taken smoke must submit initial, route-ready, low-health, and item-taken utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "eq",
                0,
                "live item-taken smoke must not rate-limit the uncapped proof",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "live item-taken smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "eq",
                1,
                "live item-taken smoke must still select an initial utterance",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "live item-taken smoke must enable reply/event selection",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                3,
                "live item-taken smoke must observe route-ready, low-health, and item-taken events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "eq",
                1,
                "live item-taken smoke must preserve the route-ready live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_item_taken",
                "eq",
                1,
                "live item-taken smoke must record the item-taken reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enemy_sighted",
                "eq",
                0,
                "live item-taken smoke must not count enemy-sighted replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_low_health",
                "ge",
                1,
                "live item-taken smoke may count the health/armor proof's low-health precursor before item_taken",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "ge",
                3,
                "live item-taken smoke must submit route-ready, low-health, and item-taken replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "eq",
                0,
                "live item-taken smoke must not rate-limit live replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_duplicate_suppressed",
                "eq",
                0,
                "live item-taken smoke must not suppress unique live replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "live item-taken smoke must not record live reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enabled",
                "eq",
                1,
                "live item-taken status must echo the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_events",
                "ge",
                4,
                "live item-taken status must count spawn, route-ready, low-health, and item-taken events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "eq",
                1,
                "live item-taken status must isolate the spawn event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "eq",
                1,
                "live item-taken status must isolate the route-ready event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_item_taken",
                "eq",
                1,
                "live item-taken status must isolate the item-taken live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enemy_sighted",
                "eq",
                0,
                "live item-taken status must not count enemy-sighted live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_low_health",
                "ge",
                1,
                "live item-taken status may count the health/armor proof's low-health precursor before item_taken",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_submitted",
                "ge",
                4,
                "live item-taken status must submit spawn, route-ready, low-health, and item-taken utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_rate_limited",
                "eq",
                0,
                "live item-taken status must not rate-limit live-event dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_duplicate_suppressed",
                "eq",
                0,
                "live item-taken status must not suppress unique live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "live item-taken status must not record live-event dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_event_taxonomy",
                "eq",
                11,
                "live item-taken taxonomy must preserve the supported event set size",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event",
                "eq",
                4,
                "live item-taken status must record item_taken as the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event_name",
                "eq",
                "item_taken",
                "live item-taken status must name the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "eq",
                4,
                "live item-taken status must record item_taken as the latest reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "live item-taken status must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled live item-taken gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="bot_chat_live_objective_changed",
        title="Bot chat live objective-changed event",
        smoke_mode=86,
        description=(
            "Runs the four-bot CTF objective transition proof with live bot "
            "chat enabled, then verifies real flag pickup/drop/return hooks "
            "emit the objective_changed gameplay chat event through the safe "
            "reply pipeline."
        ),
        task_ids=("FR-04-T04", "FR-04-T07", "FR-04-T13", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "5"),
            ("sg_bot_allow_chat", "1"),
            ("sg_bot_chat_live_events", "1"),
            ("sg_bot_ctf_objective_route", "1"),
            ("sg_bot_ctf_objective_transitions", "1"),
        ),
        selection_tags=("profiles", "chat", "objectives", "ctf", "live"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "live objective-changed smoke must issue route commands"),
            MetricCheck("route_failures", "eq", 0, "live objective-changed smoke must remain route-clean"),
            MetricCheck("ctf_objective_route_assignments", "ge", 1, "live objective-changed smoke must assign CTF objectives"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                86,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=5,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_policy",
                "eq",
                1,
                "live objective-changed smoke must identify the shared bot chat-policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_event_policy",
                "eq",
                0,
                "live objective-changed smoke must not rely on the smoke-only event-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_events",
                "eq",
                1,
                "live objective-changed smoke must enable the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_event_cooldown",
                "eq",
                0,
                "live objective-changed smoke must leave cooldown stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_enemy_sighted",
                "eq",
                0,
                "live objective-changed smoke must not stage an enemy-sighted event",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_low_health",
                "eq",
                0,
                "live objective-changed smoke must not identify the low-health event lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_item_taken",
                "eq",
                0,
                "live objective-changed smoke must not identify the item-taken event lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_live_objective_changed",
                "eq",
                1,
                "live objective-changed smoke must identify the objective-changed event lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_phrase_library",
                "eq",
                0,
                "live objective-changed smoke must not use the phrase-library stress lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "bot_chat_duplicate_suppression",
                "eq",
                0,
                "live objective-changed smoke must leave duplicate suppression stress disabled",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "allow_chat",
                "eq",
                1,
                "live objective-changed smoke must enable the public chat-policy gate",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "behavior_enable",
                "eq",
                0,
                "live objective-changed smoke must not rely on the umbrella behavior cvar",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_objective_route",
                "eq",
                1,
                "live objective-changed smoke must enable the CTF objective-route policy lane",
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "ctf_objective_transitions",
                "eq",
                1,
                "live objective-changed smoke must enable real CTF objective transition hooks",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "live objective-changed smoke must enable team mode",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "gametype",
                "eq",
                5,
                "live objective-changed smoke must run as capture the flag",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_flag_pickups",
                "ge",
                1,
                "live objective-changed smoke must record a real flag pickup hook",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_flag_drops",
                "ge",
                1,
                "live objective-changed smoke must record a real flag drop hook",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_flag_returns",
                "ge",
                1,
                "live objective-changed smoke must record a real dropped-flag return hook",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_requests",
                "ge",
                1,
                "live objective-changed smoke must request CTF objective route ownership",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_assignments",
                "ge",
                1,
                "live objective-changed smoke must assign a selected CTF objective",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_route_commands",
                "ge",
                1,
                "live objective-changed smoke must command the selected CTF objective route",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "ctf_objective_route_invalid_skips",
                "eq",
                0,
                "live objective-changed smoke must not reject seeded CTF objective candidates",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_objective_candidates",
                "ge",
                1,
                "live objective-changed smoke must enter objective arbitration",
            ),
            MarkerMetricCheck(
                BEHAVIOR_POLICY_STATUS_MARKER,
                "behavior_arbitration_objective_owners",
                "ge",
                1,
                "live objective-changed smoke must win objective ownership",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "live objective-changed chat status must pass its expected count checks",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                4,
                "live objective-changed chat status must see the staged profile bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "profile_chat_metadata",
                "eq",
                4,
                "live objective-changed chat status must observe profile chat metadata on all staged bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "allow_chat",
                "eq",
                1,
                "live objective-changed chat status must echo the enabled public gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "consumer_ready",
                "eq",
                1,
                "live objective-changed chat status must report the live consumer",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_enabled",
                "eq",
                1,
                "enabled chat gate must enable live objective-changed dispatch",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_attempts",
                "ge",
                12,
                "live objective-changed smoke must attempt initial, route-ready, and objective-changed dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_submitted",
                "ge",
                12,
                "live objective-changed smoke must submit initial, route-ready, and objective-changed utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_rate_limited",
                "eq",
                0,
                "live objective-changed smoke must not rate-limit the uncapped proof",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "dispatch_failures",
                "eq",
                0,
                "live objective-changed smoke must not record chat dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "initial_chat_selections",
                "ge",
                4,
                "live objective-changed smoke must select initial utterances for the staged bots",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enabled",
                "eq",
                1,
                "live objective-changed smoke must enable reply/event selection",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_events",
                "ge",
                8,
                "live objective-changed smoke must observe route-ready and objective-changed events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_route_ready",
                "ge",
                4,
                "live objective-changed smoke must preserve route-ready live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_item_taken",
                "eq",
                0,
                "live objective-changed smoke must not count item-taken replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_objective_changed",
                "ge",
                4,
                "live objective-changed smoke must record objective-changed reply events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_enemy_sighted",
                "eq",
                0,
                "live objective-changed smoke must not count enemy-sighted replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_low_health",
                "eq",
                0,
                "live objective-changed smoke must not count low-health replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_submitted",
                "ge",
                8,
                "live objective-changed smoke must submit route-ready and objective-changed replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_rate_limited",
                "eq",
                0,
                "live objective-changed smoke must not rate-limit live replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_duplicate_suppressed",
                "eq",
                0,
                "live objective-changed smoke must not suppress unique live replies",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "reply_chat_failures",
                "eq",
                0,
                "live objective-changed smoke must not record live reply failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enabled",
                "eq",
                1,
                "live objective-changed status must echo the live event taxonomy gate",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_events",
                "ge",
                12,
                "live objective-changed status must count spawn, route-ready, and objective-changed events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_spawn",
                "ge",
                4,
                "live objective-changed status must isolate spawn events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_route_ready",
                "ge",
                4,
                "live objective-changed status must isolate route-ready events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_item_taken",
                "eq",
                0,
                "live objective-changed status must not count item-taken live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_objective_changed",
                "ge",
                4,
                "live objective-changed status must isolate the objective-changed live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_enemy_sighted",
                "eq",
                0,
                "live objective-changed status must not count enemy-sighted live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_low_health",
                "eq",
                0,
                "live objective-changed status must not count low-health live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_submitted",
                "ge",
                12,
                "live objective-changed status must submit spawn, route-ready, and objective-changed utterances",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_rate_limited",
                "eq",
                0,
                "live objective-changed status must not rate-limit live-event dispatches",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_duplicate_suppressed",
                "eq",
                0,
                "live objective-changed status must not suppress unique live events",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_failures",
                "eq",
                0,
                "live objective-changed status must not record live-event dispatch failures",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "live_chat_event_taxonomy",
                "eq",
                11,
                "live objective-changed taxonomy must preserve the supported event set size",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event",
                "eq",
                7,
                "live objective-changed status must record objective_changed as the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_live_chat_event_name",
                "eq",
                "objective_changed",
                "live objective-changed status must name the latest live event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_event",
                "eq",
                7,
                "live objective-changed status must record objective_changed as the latest reply event",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "last_reply_chat_phrase",
                "gt",
                0,
                "live objective-changed status must record the latest selected reply phrase id",
            ),
            MarkerMetricCheck(
                CHAT_POLICY_STATUS_MARKER,
                "blocked_until_consumer",
                "eq",
                0,
                "enabled live objective-changed gate must not remain blocked once the consumer exists",
            ),
        ),
    ),
    Scenario(
        name="team_fire_avoidance",
        title="Team friendly-fire avoidance",
        smoke_mode=34,
        description=(
            "Runs a four-bot TDM combat smoke with "
            "sg_bot_team_fire_avoidance enabled and verifies friendly-line "
            "policy can suppress live attack input before BUTTON_ATTACK is "
            "applied."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "1"),
            ("g_gametype", "3"),
            ("sg_bot_team_fire_avoidance", "1"),
        ),
        selection_tags=("match", "tdm", "combat", "friendly-fire"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "team-fire smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                34,
                combat="engage_enemy",
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=4,
                gametype=3,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "team_fire_avoidance",
                "eq",
                1,
                "reserved smoke must enable the team-fire avoidance proof lane",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "tdm_pass",
                "eq",
                1,
                "team-fire smoke must run in a TDM-ready match",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "team_mode",
                "eq",
                1,
                "team-fire smoke must enable team mode",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_match_policy_tdm",
                "ge",
                1,
                "team-fire smoke must select TDM match policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_friendly_fire_policy_evaluations",
                "ge",
                1,
                "team-fire smoke must evaluate objective friendly-fire policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_friendly_fire_avoidance",
                "ge",
                1,
                "team-fire smoke must record friendly-fire avoidance",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "action_attack_decisions",
                "ge",
                1,
                "team-fire smoke must produce live attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_fire_avoidance_evaluations",
                "ge",
                1,
                "team-fire cvar must evaluate live attack decisions",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_fire_avoidance_blocks",
                "ge",
                1,
                "team-fire cvar must block live attack input",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "team_fire_avoidance_line_blocks",
                "ge",
                1,
                "team-fire cvar must block for a friendly line-of-fire",
            ),
        ),
    ),
    Scenario(
        name="coop_match_readiness",
        title="Coop match readiness",
        smoke_mode=3,
        description=(
            "Runs the frame-command smoke under cooperative cvars and verifies "
            "coop readiness reports active, playing bots."
        ),
        task_ids=("FR-04-T04", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
        ),
        selection_tags=("match", "coop"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "coop smoke must remain route-clean"),
        ),
        marker_checks=(
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "ge",
                1,
                "coop readiness status must include at least one bot",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "playing",
                "ge",
                1,
                "coop readiness status must include at least one playing bot",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "coop",
                "eq",
                1,
                "coop readiness smoke must run with coop enabled",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop readiness smoke must disable deathmatch",
            ),
        ),
    ),
    Scenario(
        name="coop_leader_route",
        title="Coop leader route",
        smoke_mode=3,
        description=(
            "Runs the frame-command smoke under cooperative cvars and verifies "
            "coop follow/regroup/support policy reaches the timed route-goal "
            "owner and compact coop command status."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
        ),
        selection_tags=("match", "coop", "leader"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "coop leader route smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "coop leader route smoke must remain route-clean"),
        ),
        marker_checks=(
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop leader route smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                STATUS_MARKER,
                "last_timed_route_goal_kind",
                "eq",
                3,
                "coop leader route smoke must end on the coop leader route owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_leader_route_activations",
                "ge",
                1,
                "coop leader route smoke must activate leader routing",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_leader_route_refreshes",
                "ge",
                1,
                "coop leader route smoke must refresh an active leader route owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_leader_route_spacing_sources",
                "ge",
                1,
                "coop leader route smoke must build at least one support-spacing source",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_leader_route_intent",
                "ge",
                1,
                "coop leader route smoke must record a concrete coop intent",
            ),
        ),
    ),
    Scenario(
        name="coop_lead_advance",
        title="Coop lead advance",
        smoke_mode=27,
        description=(
            "Runs a one-bot cooperative frame-command smoke with "
            "sg_bot_coop_lead_advance enabled and verifies no-leader "
            "LeadAdvance policy reaches timed route-goal ownership."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_lead_advance", "1"),
        ),
        selection_tags=("match", "coop", "leader", "progression"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "coop lead advance smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "coop lead advance smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                27,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=1,
                gametype=0,
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "eq",
                1,
                "lead advance smoke must run with a single bot",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop lead advance smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_lead_advance_remaining_ms",
                "gt",
                0,
                "coop lead advance smoke must end on an active lead-advance route owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_lead_advance_requests",
                "ge",
                1,
                "lead advance smoke must request coop LeadAdvance ownership",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_lead_advance_policy_leads",
                "ge",
                1,
                "lead advance smoke must observe LeadAdvance policy",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_lead_advance_activations",
                "ge",
                1,
                "lead advance smoke must activate the timed route owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_lead_advance_route_requests",
                "ge",
                1,
                "lead advance smoke must route through the timed owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_lead_advance_intent",
                "eq",
                4,
                "lead advance smoke must record LeadAdvance intent",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_coop_intent",
                "eq",
                4,
                "lead advance smoke must record LeadAdvance objective intent",
            ),
        ),
    ),
    Scenario(
        name="coop_resource_share",
        title="Coop resource share",
        smoke_mode=28,
        description=(
            "Runs a two-bot cooperative frame-command smoke with "
            "sg_bot_coop_resource_share enabled and verifies item routing "
            "consumes reserve-for-teammate resource policy."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_resource_share", "1"),
        ),
        selection_tags=("match", "coop", "resources"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "coop resource smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "coop resource smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                28,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "ge",
                2,
                "resource share smoke must run with at least two bots",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop resource share smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_coop_policy_resource_share",
                "ge",
                1,
                "coop policy must advertise resource sharing",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_resource_policy_reserve",
                "ge",
                1,
                "resource policy must reserve at least one candidate for a teammate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_reserved_deferrals",
                "ge",
                1,
                "item scoring must defer at least one resource-reserved candidate",
            ),
        ),
    ),
    Scenario(
        name="coop_anti_blocking",
        title="Coop anti-blocking",
        smoke_mode=29,
        description=(
            "Runs a two-bot cooperative frame-command smoke with "
            "sg_bot_coop_anti_blocking enabled and verifies close-to-leader "
            "coop policy can own an anti-blocking movement command."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_anti_blocking", "1"),
        ),
        selection_tags=("match", "coop", "movement"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "coop anti-block smoke must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "coop anti-block smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                29,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "ge",
                2,
                "anti-block smoke must run with at least two bots",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop anti-block smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_anti_block_requests",
                "ge",
                1,
                "anti-block cvar must request the coop command owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_anti_block_policy_close",
                "ge",
                1,
                "coop policy must report close leader spacing for anti-blocking",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_anti_block_commands",
                "ge",
                1,
                "anti-blocking must own at least one movement command",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_anti_block_intent",
                "eq",
                5,
                "anti-blocking must be driven by close support-combat coop intent",
            ),
        ),
    ),
    Scenario(
        name="coop_target_share",
        title="Coop target share",
        smoke_mode=30,
        description=(
            "Runs a two-bot cooperative frame-command smoke with "
            "sg_bot_coop_target_share enabled and verifies a bot can adopt "
            "a teammate's hostile non-client target from the blackboard."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_target_share", "1"),
        ),
        selection_tags=("match", "coop", "combat", "targeting"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "coop target-share smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                30,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "target_share",
                "eq",
                1,
                "reserved smoke must enable the coop target-share proof lane",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "ge",
                2,
                "target-share smoke must run with at least two bots",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop target-share smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_target_share_requests",
                "ge",
                1,
                "target-share cvar must request coop target sharing",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_target_share_source_candidates",
                "ge",
                1,
                "target-share smoke must find at least one teammate source target",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_target_share_adoptions",
                "ge",
                1,
                "target-share smoke must adopt at least one teammate target",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_target_share_source_client",
                "ge",
                0,
                "target-share smoke must record the source teammate client",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_target_share_target_entity",
                "gt",
                0,
                "target-share smoke must record the shared target entity",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_target_share_intent",
                "eq",
                5,
                "target sharing must be driven by close support-combat coop intent",
            ),
        ),
    ),
    Scenario(
        name="coop_door_elevator",
        title="Coop door/elevator",
        smoke_mode=31,
        description=(
            "Runs a two-bot cooperative elevator travel-type smoke with "
            "sg_bot_coop_door_elevator enabled and verifies one bot can "
            "own the interaction while a teammate holds for it."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_door_elevator", "1"),
        ),
        selection_tags=("match", "coop", "interaction", "movement"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "coop door/elevator smoke must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                31,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "door_elevator",
                "eq",
                1,
                "reserved smoke must enable the coop door/elevator proof lane",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "ge",
                2,
                "door/elevator smoke must run with at least two bots",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop door/elevator smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "nav_interaction_elevator_activations",
                "ge",
                1,
                "door/elevator smoke must activate an elevator interaction window",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_requests",
                "ge",
                1,
                "door/elevator cvar must request coop interaction cooperation",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_source_activations",
                "ge",
                1,
                "door/elevator smoke must select a source interaction owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_source_commands",
                "ge",
                1,
                "door/elevator source must own wait/use commands",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_hold_commands",
                "ge",
                1,
                "door/elevator teammate must hold for the source interaction",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_door_elevator_source_client",
                "ge",
                0,
                "door/elevator smoke must record the source teammate client",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_door_elevator_action",
                "eq",
                3,
                "door/elevator smoke must record a wait/use action",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_door_elevator_kind",
                "ge",
                3,
                "door/elevator smoke must record a mover-like interaction kind",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_door_elevator_entity",
                "gt",
                0,
                "door/elevator smoke must record the interacted entity",
            ),
        ),
    ),
    Scenario(
        name="coop_live_loop",
        title="Coop live loop",
        smoke_mode=77,
        description=(
            "Runs a two-bot cooperative elevator travel-type smoke with "
            "sg_bot_coop_live_loop enabled and verifies leader routing, "
            "progress waiting, anti-blocking, route-interaction retry, and "
            "door/elevator source-hold cooperation in one live loop."
        ),
        task_ids=("FR-04-T04", "FR-04-T05", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_live_loop", "1"),
        ),
        selection_tags=("match", "coop", "interaction", "movement", "live"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "coop live loop must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "coop live loop must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                77,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "coop_live_loop",
                "eq",
                1,
                "reserved smoke must enable the coop live-loop proof lane",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "ge",
                2,
                "coop live loop must run with at least two bots",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop live loop must disable deathmatch",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_leader_route_activations",
                "ge",
                1,
                "coop live loop must activate the leader-route owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_leader_route_spacing_sources",
                "ge",
                1,
                "leader routing must use spacing support evidence",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_progress_wait_requests",
                "ge",
                1,
                "coop live loop must request progression waiting",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_progress_wait_policy_waits",
                "ge",
                1,
                "coop policy must produce WaitForLeader in the live loop",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_progress_wait_commands",
                "ge",
                1,
                "WaitForLeader must own at least one command",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_progress_wait_intent",
                "eq",
                2,
                "progress wait must record WaitForLeader intent",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_anti_block_requests",
                "ge",
                1,
                "coop live loop must request anti-blocking",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_anti_block_policy_close",
                "ge",
                1,
                "anti-blocking must see close leader spacing",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_anti_block_commands",
                "ge",
                1,
                "anti-blocking must own at least one movement command",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_interaction_retry_requests",
                "ge",
                1,
                "coop live loop must request route interaction retries",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_interaction_retry_activations",
                "ge",
                1,
                "route interaction retry must activate",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_interaction_retry_commands",
                "ge",
                1,
                "route interaction retry must own wait/use commands",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_requests",
                "ge",
                1,
                "coop live loop must request door/elevator cooperation",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_source_activations",
                "ge",
                1,
                "door/elevator cooperation must select a source owner",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_source_commands",
                "ge",
                1,
                "door/elevator source must own wait/use commands",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_door_elevator_hold_commands",
                "ge",
                1,
                "door/elevator teammate must hold for the source",
            ),
            MarkerMetricCheck(
                NAV_POLICY_STATUS_MARKER,
                "nav_interaction_elevator_activations",
                "ge",
                1,
                "coop live loop must activate an elevator interaction window",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_coop_policy_wait",
                "ge",
                1,
                "objective policy must record a WaitForLeader decision",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_coop_policy_support",
                "ge",
                1,
                "objective policy must also record support/following behavior",
            ),
        ),
    ),
    Scenario(
        name="coop_share_loop",
        title="Coop share loop",
        smoke_mode=78,
        description=(
            "Runs a two-bot cooperative frame-command smoke with "
            "sg_bot_coop_share_loop enabled and verifies coop target sharing "
            "and reserve-for-teammate resource sharing compose in one run."
        ),
        task_ids=("FR-04-T04", "FR-04-T05", "FR-04-T15", "DV-03-T05", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_share_loop", "1"),
        ),
        selection_tags=("match", "coop", "combat", "resources", "targeting", "live"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_commands", "ge", 1, "coop share loop must emit route commands"),
            MetricCheck("route_failures", "eq", 0, "coop share loop must remain route-clean"),
        ),
        marker_checks=(
            *reserved_mode_marker_checks(
                78,
                combat=0,
                weapon_switch=0,
                item_focus=0,
                team_objective=0,
                target=2,
                gametype=0,
            ),
            MarkerMetricCheck(
                SCENARIO_BEGIN_MARKER,
                "coop_share_loop",
                "eq",
                1,
                "reserved smoke must enable the coop target/resource share proof lane",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "bots",
                "ge",
                2,
                "coop share loop must run with at least two bots",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop share loop must disable deathmatch",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_coop_policy_resource_share",
                "ge",
                1,
                "coop share loop must advertise resource sharing",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_resource_policy_reserve",
                "ge",
                1,
                "coop share loop must reserve at least one candidate for a teammate",
            ),
            MarkerMetricCheck(
                ACTION_STATUS_MARKER,
                "item_reserved_deferrals",
                "ge",
                1,
                "coop share loop item scoring must defer a resource-reserved candidate",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_target_share_requests",
                "ge",
                1,
                "coop share loop must request coop target sharing",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_target_share_source_candidates",
                "ge",
                1,
                "coop share loop must find at least one teammate source target",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_target_share_adoptions",
                "ge",
                1,
                "coop share loop must adopt at least one teammate target",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_target_share_source_client",
                "ge",
                0,
                "coop share loop must record the source teammate client",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_target_share_target_entity",
                "gt",
                0,
                "coop share loop must record the shared target entity",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_target_share_intent",
                "eq",
                5,
                "coop share loop target sharing must be driven by support-combat intent",
            ),
        ),
    ),
    Scenario(
        name="coop_progress_wait",
        title="Coop progress wait",
        smoke_mode=3,
        description=(
            "Runs the frame-command smoke under cooperative cvars with "
            "sg_bot_coop_progress_wait enabled and verifies WaitForLeader "
            "policy consumption reaches command ownership."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_progress_wait", "1"),
        ),
        selection_tags=("match", "coop", "progression"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "coop progress wait smoke must remain route-clean"),
        ),
        marker_checks=(
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop progress wait smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_progress_wait_requests",
                "ge",
                1,
                "progress wait smoke must request coop progression waiting",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_progress_wait_policy_waits",
                "ge",
                1,
                "progress wait smoke must produce WaitForLeader policy",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_progress_wait_commands",
                "ge",
                1,
                "progress wait smoke must apply wait command ownership",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_progress_wait_intent",
                "eq",
                2,
                "progress wait smoke must record WaitForLeader intent",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "team_objective_coop_policy_wait",
                "ge",
                1,
                "progress wait smoke must evaluate WaitForLeader policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_STATUS_MARKER,
                "last_team_objective_coop_intent",
                "eq",
                2,
                "progress wait smoke must record WaitForLeader objective intent",
            ),
        ),
    ),
    Scenario(
        name="coop_interaction_retry",
        title="Coop interaction retry",
        smoke_mode=12,
        description=(
            "Runs the elevator travel-type smoke under cooperative cvars with "
            "sg_bot_coop_interaction_retry enabled and verifies detected route "
            "interactions can own wait/use command retry windows."
        ),
        task_ids=("FR-04-T04", "FR-04-T15", "DV-07-T06"),
        budget_seconds=30,
        extra_cvars=(
            ("deathmatch", "0"),
            ("coop", "1"),
            ("sg_bot_coop_interaction_retry", "1"),
        ),
        selection_tags=("match", "coop", "interaction"),
        checks=(
            MetricCheck("pass", "eq", 1, "source smoke status must pass"),
            MetricCheck("route_failures", "eq", 0, "coop interaction retry smoke must remain route-clean"),
        ),
        marker_checks=(
            MarkerMetricCheck(
                COOP_READINESS_STATUS_MARKER,
                "pass",
                "eq",
                1,
                "coop readiness status must pass",
            ),
            MarkerMetricCheck(
                MATCH_READINESS_STATUS_MARKER,
                "deathmatch",
                "eq",
                0,
                "coop interaction retry smoke must disable deathmatch",
            ),
            MarkerMetricCheck(
                NAV_INTERACTION_CONTEXT_STATUS_MARKER,
                "interaction_world_entities",
                "ge",
                1,
                "interaction retry smoke must see world interaction entities",
            ),
            MarkerMetricCheck(
                NAV_INTERACTION_CONTEXT_STATUS_MARKER,
                "interaction_world_triggers",
                "ge",
                1,
                "interaction retry smoke must see trigger-backed interactions",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_interaction_retry_requests",
                "ge",
                1,
                "interaction retry smoke must request coop route interaction retries",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_interaction_retry_activations",
                "ge",
                1,
                "interaction retry smoke must activate a retry window",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "coop_interaction_retry_commands",
                "ge",
                1,
                "interaction retry smoke must apply wait/use command ownership",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_interaction_retry_action",
                "eq",
                3,
                "interaction retry smoke must record a wait/use action",
            ),
            MarkerMetricCheck(
                COOP_COMMAND_STATUS_MARKER,
                "last_coop_interaction_retry_kind",
                "ge",
                3,
                "interaction retry smoke must record a mover-like interaction kind",
            ),
        ),
    ),
)


def unique_strings(values: list[str]) -> list[str]:
    seen: set[str] = set()
    unique: list[str] = []
    for value in values:
        if value in seen:
            continue
        unique.append(value)
        seen.add(value)
    return unique


def unique_marker_metrics(values: list[tuple[str, str]]) -> list[tuple[str, str]]:
    seen: set[tuple[str, str]] = set()
    unique: list[tuple[str, str]] = []
    for value in values:
        if value in seen:
            continue
        unique.append(value)
        seen.add(value)
    return unique


def promotion_required_metrics(scenario: Scenario) -> list[str]:
    return unique_strings([
        *scenario.promotion_metrics,
        *(check.metric for check in scenario.promotion_checks),
    ])


def promotion_required_marker_metrics(scenario: Scenario) -> list[tuple[str, str]]:
    return unique_marker_metrics([
        *scenario.promotion_marker_metrics,
        *((check.marker, check.metric) for check in scenario.promotion_marker_checks),
    ])


def scenario_map() -> dict[str, Scenario]:
    return {scenario.name: scenario for scenario in SCENARIOS}


def utc_timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_status_line(text: str) -> tuple[str | None, dict[str, int]]:
    status_rows: list[tuple[str, dict[str, int]]] = []
    for line in text.splitlines():
        stripped = line.strip()
        for _marker, segment in marker_line_segments(stripped, {STATUS_MARKER}):
            status_rows.append((
                stripped,
                {match.group(1): int(match.group(2)) for match in KEY_VALUE_RE.finditer(segment)},
            ))

    if not status_rows:
        return None, {}

    selected_line, selected_metrics = status_rows[-1]
    for status_line, metrics in reversed(status_rows):
        if metrics.get("expected_min_commands", 0) > 0:
            selected_line, selected_metrics = status_line, metrics
            break

    merged_metrics: dict[str, int] = {}
    for _status_line, metrics in status_rows:
        merged_metrics.update(metrics)
    merged_metrics.update(selected_metrics)
    return selected_line, merged_metrics


def evaluate_check(check: MetricCheck, metrics: dict[str, int]) -> dict[str, Any]:
    actual = metrics.get(check.metric)
    passed = False
    if actual is not None:
        if check.op == "eq":
            passed = actual == check.expected
        elif check.op == "ge":
            passed = actual >= check.expected
        elif check.op == "gt":
            passed = actual > check.expected
        elif check.op == "le":
            passed = actual <= check.expected
        elif check.op == "lt":
            passed = actual < check.expected
        else:
            raise ValueError(f"unknown check operator: {check.op}")

    return {
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "actual": actual,
        "passed": passed,
        "note": check.note,
    }


def marker_value_passes(
    op: str,
    actual: int | float | str,
    expected: int | float | str,
) -> bool:
    if op == "eq":
        return actual == expected
    if op == "ge":
        return (
            isinstance(actual, int | float)
            and isinstance(expected, int | float)
            and actual >= expected
        )
    if op == "gt":
        return (
            isinstance(actual, int | float)
            and isinstance(expected, int | float)
            and actual > expected
        )
    if op == "le":
        return (
            isinstance(actual, int | float)
            and isinstance(expected, int | float)
            and actual <= expected
        )
    if op == "lt":
        return (
            isinstance(actual, int | float)
            and isinstance(expected, int | float)
            and actual < expected
        )
    raise ValueError(f"unknown marker check operator: {op}")


def evaluate_marker_check(
    check: MarkerMetricCheck,
    marker_metrics: dict[str, list[dict[str, int | float | str]]],
) -> dict[str, Any]:
    matches = marker_metrics.get(check.marker, [])
    any_prefix = "any_"
    if check.op.startswith(any_prefix):
        base_op = check.op[len(any_prefix):]
        actual_values = [
            row[check.metric]
            for row in matches
            if check.metric in row
        ]
        passed = any(
            marker_value_passes(base_op, value, check.expected)
            for value in actual_values
        )
        return {
            "marker": check.marker,
            "metric": check.metric,
            "op": check.op,
            "expected": check.expected,
            "actual": actual_values if actual_values else None,
            "passed": passed,
            "note": check.note,
        }

    actual = None
    for metrics in reversed(matches):
        if check.metric in metrics:
            actual = metrics[check.metric]
            break
    passed = False
    if actual is not None:
        passed = marker_value_passes(check.op, actual, check.expected)

    return {
        "marker": check.marker,
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "actual": actual,
        "passed": passed,
        "note": check.note,
    }


def check_result_failure_text(check: dict[str, Any]) -> str:
    text = (
        f"{check['metric']} {check['op']} {check['expected']} "
        f"failed, actual={display_value(check['actual'])}"
    )
    if check.get("note"):
        text += f" ({check['note']})"
    return text


def marker_check_result_failure_text(check: dict[str, Any]) -> str:
    text = (
        f"{check['marker']}::{check['metric']} {check['op']} {check['expected']} "
        f"failed, actual={display_value(check['actual'])}"
    )
    if check.get("note"):
        text += f" ({check['note']})"
    return text


def parse_marker_value(value: str) -> int | float | str:
    if INTEGER_RE.fullmatch(value):
        return int(value)
    if FLOAT_RE.fullmatch(value):
        return float(value)
    return value


def numeric_marker_value(
    fields: dict[str, int | float | str],
    metric: str,
) -> int | float | None:
    value = fields.get(metric)
    return value if isinstance(value, int | float) else None


def normalize_marker_fields(
    fields: dict[str, int | float | str],
) -> dict[str, int | float | str]:
    if ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC not in fields:
        ready_or_live_values = [
            value
            for value in (
                numeric_marker_value(fields, "item_timing_consumer_ready"),
                numeric_marker_value(fields, "item_timing_consumer_live_pickups"),
            )
            if value is not None
        ]
        if ready_or_live_values:
            fields[ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC] = max(ready_or_live_values)
    return fields


def parse_marker_fields(line: str) -> dict[str, int | float | str]:
    fields = {
        match.group(1): parse_marker_value(match.group(2))
        for match in MARKER_FIELD_RE.finditer(line)
    }
    return normalize_marker_fields(fields)


def line_has_marker(line: str, marker: str) -> bool:
    return bool(marker_line_events(line, {marker}))


def marker_occurrence_is_event(line: str, start: int, marker: str) -> bool:
    end = start + len(marker)
    if end < len(line) and not line[end].isspace():
        return False
    if start == 0 or line[start - 1].isspace():
        return True
    return line[start - 1] != "="


def marker_line_events(line: str, markers: set[str]) -> list[tuple[int, str]]:
    events: list[tuple[int, str]] = []
    for marker in markers:
        search_from = 0
        while True:
            start = line.find(marker, search_from)
            if start == -1:
                break
            if marker_occurrence_is_event(line, start, marker):
                events.append((start, marker))
            search_from = start + 1

    events.sort(key=lambda event: (event[0], -len(event[1])))
    deduped: list[tuple[int, str]] = []
    seen_starts: set[int] = set()
    for start, marker in events:
        if start in seen_starts:
            continue
        deduped.append((start, marker))
        seen_starts.add(start)
    return deduped


def marker_line_segments(line: str, markers: set[str]) -> list[tuple[str, str]]:
    events = marker_line_events(line, markers)
    segments: list[tuple[str, str]] = []
    for index, (start, marker) in enumerate(events):
        end = events[index + 1][0] if index + 1 < len(events) else len(line)
        segments.append((marker, line[start:end]))
    return segments


def parse_marker_metrics(text: str, markers: set[str]) -> dict[str, list[dict[str, int | float | str]]]:
    marker_metrics: dict[str, list[dict[str, int | float | str]]] = {
        marker: []
        for marker in sorted(markers)
    }
    if not markers:
        return marker_metrics

    for line in text.splitlines():
        for marker, segment in marker_line_segments(line, markers):
            marker_metrics[marker].append(parse_marker_fields(segment))

    return marker_metrics


def check_catalog(check: MetricCheck) -> dict[str, Any]:
    return {
        "source": STATUS_MARKER,
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "note": check.note,
    }


def marker_check_catalog(check: MarkerMetricCheck) -> dict[str, Any]:
    return {
        "source": check.marker,
        "metric": check.metric,
        "op": check.op,
        "expected": check.expected,
        "note": check.note,
    }


def marker_metric_catalog(marker_metric: tuple[str, str]) -> dict[str, str]:
    marker, metric = marker_metric
    return {
        "source": marker,
        "metric": metric,
    }


def degradation_policy_catalog(policy: DegradationPolicy | None) -> dict[str, Any] | None:
    if policy is None:
        return None
    return {
        "name": policy.name,
        "tier": policy.tier,
        "bot_count": policy.bot_count,
        "budget_profile": policy.budget_profile,
        "preserved_behavior": list(policy.preserved_behavior),
        "allowed_degradation": list(policy.allowed_degradation),
        "required_metrics": [check_catalog(check) for check in policy.required_metrics],
        "required_marker_metrics": [
            marker_check_catalog(check)
            for check in policy.required_marker_metrics
        ],
        "notes": list(policy.notes),
    }


def evaluate_degradation_policy(
    policy: DegradationPolicy | None,
    metrics: dict[str, int],
    marker_metrics: dict[str, list[dict[str, int | float | str]]],
) -> dict[str, Any] | None:
    if policy is None:
        return None

    metric_results = [
        evaluate_check(check, metrics)
        for check in policy.required_metrics
    ]
    marker_results = [
        evaluate_marker_check(check, marker_metrics)
        for check in policy.required_marker_metrics
    ]
    failed_metrics = [
        check
        for check in metric_results
        if not check["passed"]
    ]
    failed_marker_metrics = [
        check
        for check in marker_results
        if not check["passed"]
    ]
    return {
        "name": policy.name,
        "tier": policy.tier,
        "bot_count": policy.bot_count,
        "budget_profile": policy.budget_profile,
        "status": "passed" if not failed_metrics and not failed_marker_metrics else "failed",
        "metric_checks": metric_results,
        "marker_checks": marker_results,
        "failed_metric_checks": failed_metrics,
        "failed_marker_checks": failed_marker_metrics,
    }


def expected_metric_sources(metric: str) -> list[str]:
    exact_sources = RAW_RESERVED_METRIC_SOURCE_HINTS.get(metric)
    if exact_sources:
        return list(exact_sources)

    for prefix, sources in RAW_RESERVED_METRIC_PREFIX_SOURCE_HINTS:
        if metric.startswith(prefix):
            return list(sources)

    return list(RAW_RESERVED_METRIC_MARKERS)


def source_list_text(sources: list[str]) -> str:
    if not sources:
        return "unknown raw status marker"
    return "+".join(sources)


def optional_field_family_catalog(family: OptionalFieldFamily) -> dict[str, Any]:
    return {
        "name": family.name,
        "title": family.title,
        "description": family.description,
        "markers": list(family.markers),
        "metric_names": list(family.metric_names),
        "metric_prefixes": list(family.metric_prefixes),
    }


def optional_field_family_catalogs() -> list[dict[str, Any]]:
    return [
        optional_field_family_catalog(family)
        for family in OPTIONAL_FIELD_FAMILIES
    ]


def optional_marker_names() -> set[str]:
    return {
        marker
        for family in OPTIONAL_FIELD_FAMILIES
        for marker in family.markers
    }


def optional_field_matches(metric: str, family: OptionalFieldFamily) -> bool:
    return metric in family.metric_names or any(
        metric.startswith(prefix)
        for prefix in family.metric_prefixes
    )


def latest_marker_row(
    marker_metrics: dict[str, list[dict[str, int | float | str]]],
    marker: str,
) -> dict[str, int | float | str]:
    rows = marker_metrics.get(marker, [])
    merged: dict[str, int | float | str] = {}
    for row in rows:
        if isinstance(row, dict):
            merged.update(row)
    return merged


def discover_optional_fields(
    status_metrics: dict[str, Any],
    marker_metrics: dict[str, list[dict[str, int | float | str]]],
) -> list[dict[str, Any]]:
    sources: list[tuple[str, dict[str, Any]]] = []
    status_source_metrics = dict(status_metrics) if status_metrics else {}
    status_source_metrics.update(latest_marker_row(marker_metrics, STATUS_MARKER))
    if status_source_metrics:
        sources.append((STATUS_MARKER, status_source_metrics))
    for marker in sorted(marker_metrics):
        if marker == STATUS_MARKER:
            continue
        row = latest_marker_row(marker_metrics, marker)
        if row:
            sources.append((marker, row))

    discovered: list[dict[str, Any]] = []
    seen: set[tuple[str, str]] = set()
    for family in OPTIONAL_FIELD_FAMILIES:
        for source, metrics in sources:
            if source not in family.markers:
                continue
            family_metrics = {
                metric: value
                for metric, value in sorted(metrics.items())
                if optional_field_matches(metric, family)
            }
            if not family_metrics:
                continue
            key = (family.name, source)
            if key in seen:
                continue
            seen.add(key)
            discovered.append({
                "family": family.name,
                "title": family.title,
                "source": source,
                "metrics": family_metrics,
            })
    return discovered


def scenario_catalog(scenario: Scenario) -> dict[str, Any]:
    return {
        "name": scenario.name,
        "title": scenario.title,
        "status": "implemented" if scenario.implemented else "pending",
        "task_ids": list(scenario.task_ids),
        "smoke_cvar": scenario.smoke_cvar,
        "smoke_mode": scenario.smoke_mode,
        "planned_smoke_mode": scenario.planned_smoke_mode,
        "description": scenario.description,
        "runtime_budget_seconds": scenario.budget_seconds,
        "manual_only": scenario.manual_only,
        "map_name": scenario.map_name,
        "selection_tags": list(scenario.selection_tags),
        "degradation_policy": degradation_policy_catalog(scenario.degradation_policy),
        "required_metrics": [check_catalog(check) for check in scenario.checks],
        "required_marker_metrics": [marker_check_catalog(check) for check in scenario.marker_checks],
        "promotion_required_metrics": promotion_required_metrics(scenario),
        "promotion_required_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in promotion_required_marker_metrics(scenario)
        ],
        "promotion_metric_checks": [check_catalog(check) for check in scenario.promotion_checks],
        "promotion_marker_checks": [
            marker_check_catalog(check)
            for check in scenario.promotion_marker_checks
        ],
        "extra_cvars": [
            {"name": name, "value": value}
            for name, value in scenario.extra_cvars
        ],
        "pending_blockers": [scenario.pending_reason] if scenario.pending_reason else [],
    }


def catalog_report(scenarios: list[Scenario]) -> dict[str, Any]:
    implemented = sum(1 for scenario in scenarios if scenario.implemented)
    pending = len(scenarios) - implemented
    manual_only = sum(1 for scenario in scenarios if scenario.manual_only)
    degradation_policies = sum(1 for scenario in scenarios if scenario.degradation_policy is not None)
    return {
        "schema_version": 1,
        "generated_utc": utc_timestamp(),
        "optional_field_families": optional_field_family_catalogs(),
        "summary": {
            "total": len(scenarios),
            "implemented": implemented,
            "pending": pending,
            "manual_only": manual_only,
            "degradation_policies": degradation_policies,
        },
        "scenarios": [scenario_catalog(scenario) for scenario in scenarios],
    }


def load_report(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def markdown_cell(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def display_value(value: Any) -> str:
    return "-" if value is None else str(value)


def smoke_display(row: dict[str, Any]) -> str:
    mode = row.get("smoke_mode")
    if mode is None:
        return "-"
    cvar = row.get("smoke_cvar")
    if cvar and cvar != "sv_bot_frame_command_smoke":
        return f"{cvar}={mode}"
    return str(mode)


def scenario_artifacts(scenario_result: dict[str, Any]) -> list[str]:
    artifacts: list[str] = []
    for key in ("stdout_path", "stderr_path"):
        path = scenario_result.get(key)
        if path:
            artifacts.append(str(path))
    return artifacts


def marker_summary_metrics(scenario_result: dict[str, Any]) -> dict[str, int]:
    metrics: dict[str, int] = {}
    for marker_rows in scenario_result.get("markers", {}).values():
        if not marker_rows:
            continue
        for key, value in marker_rows[-1].items():
            if key in KEY_METRICS:
                metrics[key] = value
    return metrics


def scenario_key_metrics(scenario_result: dict[str, Any]) -> dict[str, int | float]:
    metrics: dict[str, int | float] = {}
    status_metrics = scenario_result.get("metrics", {})
    for key in KEY_METRICS:
        if key in status_metrics:
            metrics[key] = status_metrics[key]

    metrics.update(marker_summary_metrics(scenario_result))
    duration = scenario_result.get("duration_seconds")
    if isinstance(duration, int | float):
        metrics["duration_seconds"] = duration
    return metrics


def report_scenario_map(report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        scenario.get("name"): scenario
        for scenario in report.get("scenarios", [])
        if scenario.get("name")
    }


def compare_reports(current: dict[str, Any], previous: dict[str, Any], previous_path: pathlib.Path) -> dict[str, Any]:
    current_scenarios = report_scenario_map(current)
    previous_scenarios = report_scenario_map(previous)
    scenario_names = sorted(set(current_scenarios) | set(previous_scenarios))
    scenario_results: list[dict[str, Any]] = []

    for name in scenario_names:
        current_result = current_scenarios.get(name)
        previous_result = previous_scenarios.get(name)
        current_metrics = scenario_key_metrics(current_result or {})
        previous_metrics = scenario_key_metrics(previous_result or {})
        metric_changes: dict[str, dict[str, Any]] = {}

        for metric in sorted(set(current_metrics) | set(previous_metrics)):
            current_value = current_metrics.get(metric)
            previous_value = previous_metrics.get(metric)
            if current_value == previous_value:
                continue
            delta = None
            if isinstance(current_value, int | float) and isinstance(previous_value, int | float):
                delta = current_value - previous_value
            metric_changes[metric] = {
                "previous": previous_value,
                "current": current_value,
                "delta": delta,
            }

        previous_status = previous_result.get("status") if previous_result else None
        current_status = current_result.get("status") if current_result else None
        scenario_results.append({
            "name": name,
            "previous_status": previous_status,
            "current_status": current_status,
            "added": previous_result is None,
            "removed": current_result is None,
            "status_changed": previous_status != current_status,
            "metric_changes": metric_changes,
        })

    summary = {
        "total": len(scenario_results),
        "matched": sum(1 for item in scenario_results if not item["added"] and not item["removed"]),
        "added": sum(1 for item in scenario_results if item["added"]),
        "removed": sum(1 for item in scenario_results if item["removed"]),
        "status_changed": sum(1 for item in scenario_results if item["status_changed"]),
        "metric_changed": sum(1 for item in scenario_results if item["metric_changes"]),
    }

    return {
        "previous_path": str(previous_path),
        "summary": summary,
        "scenarios": scenario_results,
    }


def attach_comparison(report: dict[str, Any], previous_path: pathlib.Path | None) -> None:
    if previous_path is None:
        return
    previous = load_report(previous_path)
    report["comparison"] = compare_reports(report, previous, previous_path)


def raw_reserved_status(metrics: dict[str, Any]) -> str:
    pass_value = metrics.get("pass")
    if pass_value == 1:
        return "passed"
    if pass_value is not None:
        return "failed"
    return "unknown"


def add_raw_metric_sources(
    metrics: dict[str, int | float | str],
    metric_sources: dict[str, list[str]],
    marker: str,
    row: dict[str, int | float | str],
    metric_latest_sources: dict[str, str] | None = None,
    metric_lines: dict[str, int] | None = None,
    line_number: int | None = None,
) -> None:
    for metric, value in row.items():
        metrics[metric] = value
        metric_sources.setdefault(metric, [])
        if marker not in metric_sources[metric]:
            metric_sources[metric].append(marker)
        if metric_latest_sources is not None:
            metric_latest_sources[metric] = marker
        if metric_lines is not None and line_number is not None:
            metric_lines[metric] = line_number


def finalize_raw_reserved_diagnostic(diagnostic: dict[str, Any]) -> None:
    markers = diagnostic["markers"]
    metrics: dict[str, int | float | str] = {}
    metric_sources: dict[str, list[str]] = {}
    metric_latest_sources: dict[str, str] = {}
    metric_lines: dict[str, int] = {}
    marker_events = diagnostic.get("marker_events", [])
    if isinstance(marker_events, list) and marker_events:
        for event in marker_events:
            if not isinstance(event, dict):
                continue
            marker = event.get("marker")
            row = event.get("metrics")
            line_number = event.get("line")
            if marker not in RAW_RESERVED_METRIC_MARKERS or not isinstance(row, dict):
                continue
            add_raw_metric_sources(
                metrics,
                metric_sources,
                marker,
                row,
                metric_latest_sources,
                metric_lines,
                line_number if isinstance(line_number, int) else None,
            )
    else:
        for marker in RAW_RESERVED_METRIC_MARKERS:
            rows = markers.get(marker, [])
            if rows:
                add_raw_metric_sources(
                    metrics,
                    metric_sources,
                    marker,
                    rows[-1],
                    metric_latest_sources,
                    None,
                    None,
                )

    diagnostic["metrics"] = metrics
    diagnostic["metric_sources"] = metric_sources
    diagnostic["metric_latest_sources"] = metric_latest_sources
    diagnostic["metric_lines"] = metric_lines
    diagnostic["status"] = raw_reserved_status(metrics)
    diagnostic["marker_counts"] = {
        marker: len(rows)
        for marker, rows in markers.items()
        if rows
    }
    diagnostic["optional_fields"] = discover_optional_fields(
        latest_marker_row(markers, STATUS_MARKER),
        markers,
    )


def parse_raw_reserved_mode_diagnostics(
    text: str,
    source_path: pathlib.Path | str | None = None,
) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    active: dict[str, Any] | None = None
    source_text = str(source_path) if source_path is not None else None

    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        marker_segments = marker_line_segments(stripped, set(RAW_RESERVED_MODE_MARKERS))
        begin_segment = next((
            segment
            for marker, segment in marker_segments
            if marker == SCENARIO_BEGIN_MARKER
        ), None)
        if begin_segment is not None:
            fields = parse_marker_fields(begin_segment)
            mode = fields.get("mode")
            scenario_name = RESERVED_MODE_SCENARIOS.get(mode) if isinstance(mode, int) else None
            active = None
            if scenario_name is None:
                continue

            active = {
                "source_path": source_text,
                "line": line_number,
                "mode": mode,
                "scenario": scenario_name,
                "status": "unknown",
                "markers": {marker: [] for marker in RAW_RESERVED_MODE_MARKERS},
                "metrics": {},
                "metric_sources": {},
                "metric_latest_sources": {},
                "metric_lines": {},
                "marker_counts": {},
                "marker_events": [],
            }
            active["markers"][SCENARIO_BEGIN_MARKER].append(fields)
            diagnostics.append(active)
            continue

        if active is None:
            continue

        for marker, segment in marker_segments:
            if marker == SCENARIO_BEGIN_MARKER:
                continue
            fields = parse_marker_fields(segment)
            active["markers"][marker].append(fields)
            active["marker_events"].append({
                "line": line_number,
                "marker": marker,
                "metrics": fields,
            })

    for diagnostic in diagnostics:
        finalize_raw_reserved_diagnostic(diagnostic)

    return diagnostics


def raw_diagnostic_summary(diagnostic: dict[str, Any]) -> dict[str, Any]:
    return {
        "source_path": diagnostic.get("source_path"),
        "line": diagnostic.get("line"),
        "mode": diagnostic.get("mode"),
        "scenario": diagnostic.get("scenario"),
        "status": diagnostic.get("status"),
        "metric_count": len(diagnostic.get("metrics", {})),
        "marker_counts": diagnostic.get("marker_counts", {}),
        "optional_fields": diagnostic.get("optional_fields", []),
    }


def latest_raw_diagnostics_by_scenario(raw_diagnostics: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    latest_by_mode: dict[int, dict[str, Any]] = {}
    for diagnostic in raw_diagnostics:
        mode = diagnostic.get("mode")
        if not isinstance(mode, int) or mode not in RESERVED_MODE_SCENARIOS:
            continue
        latest_by_mode[mode] = diagnostic

    return {
        RESERVED_MODE_SCENARIOS[mode]: diagnostic
        for mode, diagnostic in latest_by_mode.items()
    }


def merge_marker_metrics(
    first: dict[str, list[dict[str, int | float | str]]],
    second: dict[str, list[dict[str, int | float | str]]],
) -> dict[str, list[dict[str, int | float | str]]]:
    merged: dict[str, list[dict[str, int | float | str]]] = {}
    for marker, rows in first.items():
        if isinstance(rows, list):
            merged[marker] = [
                row
                for row in rows
                if isinstance(row, dict)
            ]
    for marker, rows in second.items():
        if not isinstance(rows, list):
            continue
        merged.setdefault(marker, [])
        merged[marker].extend(row for row in rows if isinstance(row, dict))
    return merged


def raw_log_paths(root: pathlib.Path, values: list[str]) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    for value in values:
        path = resolve_path(root, value)
        if path.is_file():
            paths.append(path)
            continue
        if path.is_dir():
            for candidate in sorted(path.rglob("*")):
                if candidate.is_file() and candidate.suffix.lower() in {".log", ".txt"}:
                    paths.append(candidate)
            continue
        raise SystemExit(f"Raw pending-gap diagnostic path not found: {path}")
    return paths


def load_raw_reserved_mode_diagnostics(root: pathlib.Path, values: list[str]) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    for path in raw_log_paths(root, values):
        text = path.read_text(encoding="utf-8", errors="replace")
        diagnostics.extend(parse_raw_reserved_mode_diagnostics(text, path))
    return diagnostics


def scenario_marker_metric_pairs(scenario_result: dict[str, Any]) -> set[tuple[str, str]]:
    pairs: set[tuple[str, str]] = set()
    markers = scenario_result.get("markers", {})
    if not isinstance(markers, dict):
        return pairs
    for marker, rows in markers.items():
        if not isinstance(rows, list):
            continue
        for row in rows:
            if not isinstance(row, dict):
                continue
            for metric in row:
                pairs.add((marker, metric))
    return pairs


def related_promotion_metrics(
    scenario: Scenario,
    fixture_metrics: dict[str, Any],
) -> dict[str, int | float | str]:
    prefixes = PROMOTION_RELATED_METRIC_PREFIXES.get(scenario.name, ())
    if not prefixes:
        return {}

    return {
        metric: value
        for metric, value in sorted(fixture_metrics.items())
        if (
            isinstance(value, int | float | str)
            and any(metric.startswith(prefix) for prefix in prefixes)
        )
    }


def health_armor_related_note(
    scenario: Scenario,
    related_metrics: dict[str, int | float | str],
    missing_metrics: list[str],
) -> str | None:
    if scenario.name != "health_armor_pickup":
        return None
    if not related_metrics or not missing_metrics:
        return None
    return (
        "generic item-goal telemetry is present, but health/armor-specific "
        "pickup proof is still missing"
    )


def policy_consumer_marker_fields(scenario: Scenario) -> tuple[tuple[str, str], ...]:
    return POLICY_CONSUMER_MARKER_FIELDS.get(scenario.name, ())


def policy_consumer_field_text(marker_metric: tuple[str, str]) -> str:
    marker, metric = marker_metric
    text = f"{marker}::{metric}"
    note = POLICY_CONSUMER_FIELD_NOTES.get(marker_metric)
    if note:
        text += f" ({note})"
    return text


def missing_policy_consumer_fields(
    scenario: Scenario,
    missing_metrics: list[str],
    missing_marker_metrics: list[tuple[str, str]],
) -> list[tuple[str, str]]:
    expected = set(policy_consumer_marker_fields(scenario))
    missing_metric_names = set(missing_metrics)
    return [
        marker_metric
        for marker_metric in expected
        if marker_metric in missing_marker_metrics or marker_metric[1] in missing_metric_names
    ]


def pending_gap_scenario(
    scenario: Scenario,
    fixture_result: dict[str, Any] | None,
    raw_diagnostic: dict[str, Any] | None = None,
) -> dict[str, Any]:
    present_metrics: set[str] = set()
    present_marker_metrics: set[tuple[str, str]] = set()
    fixture_metrics: dict[str, Any] = {}
    fixture_status_metrics: dict[str, Any] = {}
    fixture_markers: dict[str, list[dict[str, int | float | str]]] = {}
    metric_sources: dict[str, list[str]] = {}
    fixture_status = None
    fixture_smoke_mode = None
    fixture_source = "missing"
    fixture_row_present = fixture_result is not None
    raw_diagnostic_present = raw_diagnostic is not None
    blockers: list[str] = []
    notes: list[str] = []
    required_metrics = promotion_required_metrics(scenario)
    required_marker_metrics = promotion_required_marker_metrics(scenario)

    if fixture_result is None:
        if raw_diagnostic is None:
            blockers.append(f"fixture report has no scenario row named {scenario.name}")
        else:
            fixture_source = "raw_reserved_mode"
            fixture_status = raw_diagnostic.get("status")
            fixture_smoke_mode = raw_diagnostic.get("mode")
            raw_metrics = raw_diagnostic.get("metrics", {})
            raw_markers = raw_diagnostic.get("markers", {})
            raw_sources = raw_diagnostic.get("metric_sources", {})
            fixture_metrics = raw_metrics if isinstance(raw_metrics, dict) else {}
            fixture_markers = raw_markers if isinstance(raw_markers, dict) else {}
            fixture_status_metrics = latest_marker_row(fixture_markers, STATUS_MARKER)
            metric_sources = raw_sources if isinstance(raw_sources, dict) else {}
            notes.append("using raw reserved-mode diagnostics because no scenario row exists")
    else:
        fixture_source = "scenario_row"
        fixture_status = fixture_result.get("status")
        fixture_smoke_mode = fixture_result.get("smoke_mode")
        raw_metrics = fixture_result.get("metrics", {})
        raw_markers = fixture_result.get("markers", {})
        fixture_metrics = raw_metrics if isinstance(raw_metrics, dict) else {}
        fixture_status_metrics = dict(fixture_metrics)
        fixture_markers = raw_markers if isinstance(raw_markers, dict) else {}
        metric_sources = {
            metric: [STATUS_MARKER]
            for metric in fixture_metrics
        }
        if raw_diagnostic is not None:
            fixture_source = "scenario_row+raw_reserved_mode"
            raw_metrics = raw_diagnostic.get("metrics", {})
            raw_markers = raw_diagnostic.get("markers", {})
            raw_sources = raw_diagnostic.get("metric_sources", {})
            if isinstance(raw_metrics, dict):
                for metric, value in raw_metrics.items():
                    if metric not in fixture_metrics:
                        fixture_metrics[metric] = value
            if isinstance(raw_markers, dict):
                for metric, value in latest_marker_row(raw_markers, STATUS_MARKER).items():
                    if metric not in fixture_status_metrics:
                        fixture_status_metrics[metric] = value
                fixture_markers = merge_marker_metrics(fixture_markers, raw_markers)
            if isinstance(raw_sources, dict):
                for metric, sources in raw_sources.items():
                    if metric not in metric_sources and isinstance(sources, list):
                        metric_sources[metric] = sources

        if fixture_status == "pending":
            blockers.append("fixture row is still pending, not source-backed")
        elif fixture_status != "passed":
            blockers.append(
                f"fixture row status is {display_value(fixture_status)}, expected passed"
            )

    if fixture_result is None and raw_diagnostic is not None:
        if fixture_status != "passed":
            blockers.append(
                "raw reserved-mode diagnostics status is "
                f"{display_value(fixture_status)}, expected passed"
            )

    if (
        (fixture_result is not None or raw_diagnostic is not None)
        and scenario.planned_smoke_mode is not None
        and fixture_smoke_mode != scenario.planned_smoke_mode
    ):
        source_label = "fixture smoke_mode" if fixture_row_present else "raw reserved-mode mode"
        blockers.append(
            f"{source_label} is {display_value(fixture_smoke_mode)}, "
            f"expected {scenario.planned_smoke_mode}"
        )

    present_metrics = set(fixture_metrics)
    present_marker_metrics = scenario_marker_metric_pairs({"markers": fixture_markers})

    missing_metrics = [
        metric
        for metric in required_metrics
        if metric not in present_metrics
    ]
    missing_metric_sources = {
        metric: expected_metric_sources(metric)
        for metric in missing_metrics
    }
    missing_marker_metrics = [
        marker_metric
        for marker_metric in required_marker_metrics
        if marker_metric not in present_marker_metrics
    ]
    missing_policy_fields = missing_policy_consumer_fields(
        scenario,
        missing_metrics,
        missing_marker_metrics,
    )
    related_metrics = related_promotion_metrics(scenario, fixture_metrics)
    related_note = health_armor_related_note(scenario, related_metrics, missing_metrics)
    if related_note:
        notes.append(related_note)
    optional_fields = discover_optional_fields(fixture_status_metrics, fixture_markers)

    check_results = [
        evaluate_check(check, fixture_metrics)
        for check in scenario.promotion_checks
        if fixture_result is not None or raw_diagnostic is not None
    ]
    marker_check_results = [
        evaluate_marker_check(check, fixture_markers)
        for check in scenario.promotion_marker_checks
        if fixture_result is not None or raw_diagnostic is not None
    ]
    failed_checks = [check for check in check_results if not check["passed"]]
    failed_marker_checks = [check for check in marker_check_results if not check["passed"]]

    if missing_metrics:
        missing_details = [
            f"{metric} (expected from {source_list_text(missing_metric_sources[metric])})"
            for metric in missing_metrics
        ]
        blockers.append(f"missing status metrics: {', '.join(missing_details)}")
    if missing_marker_metrics:
        blockers.append(
            "missing marker metrics: "
            + ", ".join(f"{marker}::{metric}" for marker, metric in missing_marker_metrics)
        )
    if missing_policy_fields:
        blockers.append(
            "missing policy-consumer fields: "
            + ", ".join(policy_consumer_field_text(field) for field in missing_policy_fields)
        )
    if failed_checks:
        blockers.append(
            "promotion metric checks failed: "
            + "; ".join(check_result_failure_text(check) for check in failed_checks)
        )
    if failed_marker_checks:
        blockers.append(
            "promotion marker checks failed: "
            + "; ".join(marker_check_result_failure_text(check) for check in failed_marker_checks)
        )

    return {
        "name": scenario.name,
        "title": scenario.title,
        "status": "blocked" if blockers else "ready",
        "task_ids": list(scenario.task_ids),
        "smoke_mode": None,
        "planned_smoke_mode": scenario.planned_smoke_mode,
        "description": scenario.description,
        "pending_reason": scenario.pending_reason,
        "fixture_status": fixture_status,
        "fixture_smoke_mode": fixture_smoke_mode,
        "fixture_source": fixture_source,
        "fixture_row_present": fixture_row_present,
        "raw_diagnostic_present": raw_diagnostic_present,
        "raw_diagnostic": raw_diagnostic_summary(raw_diagnostic) if raw_diagnostic else None,
        "promotion_required_metrics": required_metrics,
        "promotion_required_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in required_marker_metrics
        ],
        "promotion_metric_checks": [check_catalog(check) for check in scenario.promotion_checks],
        "promotion_marker_checks": [
            marker_check_catalog(check)
            for check in scenario.promotion_marker_checks
        ],
        "promotion_metric_check_results": check_results,
        "promotion_marker_check_results": marker_check_results,
        "failed_metric_checks": failed_checks,
        "failed_marker_checks": failed_marker_checks,
        "present_metrics": sorted(metric for metric in required_metrics if metric in present_metrics),
        "related_present_metrics": related_metrics,
        "optional_fields": optional_fields,
        "missing_metrics": missing_metrics,
        "missing_metric_sources": missing_metric_sources,
        "missing_policy_consumer_fields": [
            marker_metric_catalog(marker_metric)
            for marker_metric in missing_policy_fields
        ],
        "metric_sources": {
            metric: metric_sources.get(metric, [])
            for metric in required_metrics
            if metric in present_metrics
        },
        "present_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in required_marker_metrics
            if marker_metric in present_marker_metrics
        ],
        "missing_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in missing_marker_metrics
        ],
        "blockers": blockers,
        "notes": notes,
    }


def pending_gap_report(
    scenarios: list[Scenario],
    fixture_report: dict[str, Any],
    fixture_path: pathlib.Path,
    raw_diagnostics: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    pending_scenarios = [scenario for scenario in scenarios if not scenario.implemented]
    fixture_scenarios = report_scenario_map(fixture_report)
    raw_diagnostics = raw_diagnostics or []
    raw_by_scenario = latest_raw_diagnostics_by_scenario(raw_diagnostics)
    gap_rows = [
        pending_gap_scenario(
            scenario,
            fixture_scenarios.get(scenario.name),
            raw_by_scenario.get(scenario.name),
        )
        for scenario in pending_scenarios
    ]
    summary = {
        "total": len(gap_rows),
        "ready": sum(1 for row in gap_rows if row["status"] == "ready"),
        "blocked": sum(1 for row in gap_rows if row["status"] == "blocked"),
        "missing_rows": sum(1 for row in gap_rows if row["fixture_status"] is None),
        "raw_diagnostics": len(raw_diagnostics),
        "raw_diagnostic_rows": sum(1 for row in gap_rows if row["raw_diagnostic_present"]),
        "pending_rows": sum(1 for row in gap_rows if row["fixture_status"] == "pending"),
        "missing_status_metrics": sum(len(row["missing_metrics"]) for row in gap_rows),
        "missing_marker_metrics": sum(len(row["missing_marker_metrics"]) for row in gap_rows),
        "missing_policy_consumer_fields": sum(
            len(row.get("missing_policy_consumer_fields", []))
            for row in gap_rows
        ),
        "failed_metric_checks": sum(len(row["failed_metric_checks"]) for row in gap_rows),
        "failed_marker_checks": sum(len(row["failed_marker_checks"]) for row in gap_rows),
        "overall": "ready" if all(row["status"] == "ready" for row in gap_rows) else "blocked",
    }
    return {
        "schema_version": 1,
        "report_type": "pending_gap",
        "generated_utc": utc_timestamp(),
        "fixture_path": str(fixture_path),
        "fixture_summary": fixture_report.get("summary", {}),
        "optional_field_families": optional_field_family_catalogs(),
        "raw_diagnostics": [
            raw_diagnostic_summary(diagnostic)
            for diagnostic in raw_diagnostics
        ],
        "summary": summary,
        "scenarios": gap_rows,
    }


def scenario_metric_text(scenario_result: dict[str, Any]) -> str:
    metrics = scenario_key_metrics(scenario_result)
    if not metrics:
        return ""
    return ", ".join(f"{key}={metrics[key]}" for key in KEY_METRICS if key in metrics)


def scenario_pending_text(scenario_result: dict[str, Any]) -> str:
    if scenario_result.get("pending_reason"):
        return scenario_result["pending_reason"]
    blockers = scenario_result.get("pending_blockers", [])
    return "; ".join(blockers)


def optional_field_text(scenario_result: dict[str, Any]) -> str:
    groups: list[str] = []
    for group in scenario_result.get("optional_fields", []):
        metrics = group.get("metrics", {})
        if not isinstance(metrics, dict) or not metrics:
            continue
        metric_text = " ".join(
            f"{metric}={display_value(value)}"
            for metric, value in metrics.items()
        )
        family = group.get("family", "")
        source = group.get("source", "")
        groups.append(f"{family}<{source}> {metric_text}")
    return "; ".join(groups)


def degradation_policy_text(scenario_result: dict[str, Any]) -> str:
    policy = scenario_result.get("degradation_policy")
    if not policy:
        return ""
    parts = [
        str(policy.get("name", "")),
        f"tier={policy.get('tier', '')}",
        f"bots={policy.get('bot_count', '')}",
    ]
    budget = policy.get("budget_profile")
    if budget:
        parts.append(f"budget={budget}")
    policy_result = scenario_result.get("degradation_policy_result")
    if policy_result:
        parts.append(f"status={policy_result.get('status', '')}")
    return " ".join(part for part in parts if part)


def build_pending_gap_markdown_report(report: dict[str, Any]) -> str:
    lines: list[str] = [
        "# Bot Scenario Pending Gap Report",
        "",
        f"- Generated UTC: `{report.get('generated_utc', '')}`",
        f"- Fixture: `{report.get('fixture_path', '')}`",
    ]
    summary = report.get("summary", {})
    if summary:
        summary_text = ", ".join(f"{key}={value}" for key, value in summary.items())
        lines.append(f"- Summary: `{summary_text}`")
    lines.extend((
        "",
        "## Scenarios",
        "",
        "| Scenario | Status | Source | Planned Smoke | Fixture Status | Missing Metrics | Missing Metric Sources | Missing Marker Metrics | Missing Policy Consumers | Related Metrics | Optional Fields | Blockers |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ))

    for scenario in report.get("scenarios", []):
        missing = ", ".join(scenario.get("missing_metrics", []))
        missing_sources = ", ".join(
            f"{metric}<-{source_list_text(sources)}"
            for metric, sources in scenario.get("missing_metric_sources", {}).items()
        )
        marker_missing = [
            f"{item['source']}::{item['metric']}"
            for item in scenario.get("missing_marker_metrics", [])
        ]
        policy_missing = [
            policy_consumer_field_text((item["source"], item["metric"]))
            for item in scenario.get("missing_policy_consumer_fields", [])
        ]
        related = ", ".join(
            f"{metric}={value}"
            for metric, value in scenario.get("related_present_metrics", {}).items()
        )
        optional = optional_field_text(scenario)
        blockers = "; ".join(scenario.get("blockers", []))
        lines.append(
            "| {name} | {status} | {source} | {planned} | {fixture_status} | "
            "{missing} | {missing_sources} | {marker_missing} | {policy_missing} | "
            "{related} | {optional} | {blockers} |".format(
                name=markdown_cell(scenario.get("name", "")),
                status=markdown_cell(scenario.get("status", "")),
                source=markdown_cell(scenario.get("fixture_source", "")),
                planned=markdown_cell(scenario.get("planned_smoke_mode")),
                fixture_status=markdown_cell(scenario.get("fixture_status")),
                missing=markdown_cell(missing),
                missing_sources=markdown_cell(missing_sources),
                marker_missing=markdown_cell(", ".join(marker_missing)),
                policy_missing=markdown_cell(", ".join(policy_missing)),
                related=markdown_cell(related),
                optional=markdown_cell(optional),
                blockers=markdown_cell(blockers),
            )
        )

    lines.append("")
    return "\n".join(lines)


def build_markdown_report(report: dict[str, Any]) -> str:
    if report.get("report_type") == "pending_gap":
        return build_pending_gap_markdown_report(report)

    lines: list[str] = []
    is_catalog = "generated_utc" in report and "started_utc" not in report
    title = "Bot Scenario Catalog" if is_catalog else "Bot Scenario Smoke Report"
    lines.append(f"# {title}")
    lines.append("")
    if report.get("repo_root"):
        lines.append(f"- Repo: `{report['repo_root']}`")
    if report.get("started_utc"):
        lines.append(f"- Started UTC: `{report['started_utc']}`")
    if report.get("generated_utc"):
        lines.append(f"- Generated UTC: `{report['generated_utc']}`")
    if report.get("artifact_dir"):
        lines.append(f"- Artifact dir: `{report['artifact_dir']}`")

    summary = report.get("summary", {})
    if summary:
        summary_text = ", ".join(f"{key}={value}" for key, value in summary.items())
        lines.append(f"- Summary: `{summary_text}`")
    lines.append("")

    lines.append("## Scenarios")
    lines.append("")
    lines.append("| Scenario | Status | Smoke | Tasks | Key Metrics | Optional Fields | Degradation Policy | Pending Blockers | Artifacts |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for scenario in report.get("scenarios", []):
        tasks = ",".join(scenario.get("task_ids", []))
        smoke = smoke_display(scenario)
        artifacts = "<br>".join(f"`{artifact}`" for artifact in scenario_artifacts(scenario))
        lines.append(
            "| {name} | {status} | {smoke} | {tasks} | {metrics} | {optional} | {policy} | {pending} | {artifacts} |".format(
                name=markdown_cell(scenario.get("name", "")),
                status=markdown_cell(scenario.get("status", "")),
                smoke=markdown_cell(smoke),
                tasks=markdown_cell(tasks),
                metrics=markdown_cell(scenario_metric_text(scenario)),
                optional=markdown_cell(optional_field_text(scenario)),
                policy=markdown_cell(degradation_policy_text(scenario)),
                pending=markdown_cell(scenario_pending_text(scenario)),
                artifacts=artifacts,
            )
        )

    comparison = report.get("comparison")
    if comparison:
        lines.append("")
        lines.append("## Comparison")
        lines.append("")
        lines.append(f"- Previous report: `{comparison['previous_path']}`")
        comparison_summary = ", ".join(
            f"{key}={value}" for key, value in comparison.get("summary", {}).items()
        )
        lines.append(f"- Summary: `{comparison_summary}`")
        lines.append("")
        lines.append("| Scenario | Previous | Current | Status Changed | Metric Changes |")
        lines.append("| --- | --- | --- | --- | --- |")
        for scenario in comparison.get("scenarios", []):
            changes = []
            for metric, change in scenario.get("metric_changes", {}).items():
                delta = change.get("delta")
                if delta is None:
                    changes.append(
                        f"{metric}: {display_value(change.get('previous'))} -> "
                        f"{display_value(change.get('current'))}"
                    )
                else:
                    changes.append(
                        f"{metric}: {display_value(change.get('previous'))} -> "
                        f"{display_value(change.get('current'))} ({delta:+})"
                    )
            lines.append(
                "| {name} | {previous} | {current} | {status_changed} | {changes} |".format(
                    name=markdown_cell(scenario.get("name", "")),
                    previous=markdown_cell(scenario.get("previous_status")),
                    current=markdown_cell(scenario.get("current_status")),
                    status_changed=markdown_cell(scenario.get("status_changed")),
                    changes=markdown_cell("; ".join(changes)),
                )
            )

    lines.append("")
    return "\n".join(lines)


def write_report_outputs(
    report: dict[str, Any],
    repo_root: pathlib.Path,
    json_out: str | None,
    markdown_out: str | None,
) -> None:
    if json_out:
        json_path = resolve_path(repo_root, json_out)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if markdown_out:
        markdown_path = resolve_path(repo_root, markdown_out)
        markdown_path.parent.mkdir(parents=True, exist_ok=True)
        markdown_path.write_text(build_markdown_report(report), encoding="utf-8")


def resolve_path(root: pathlib.Path, value: str) -> pathlib.Path:
    path = pathlib.Path(value)
    if not path.is_absolute():
        path = root / path
    return path.resolve()


def build_command(
    binary: pathlib.Path,
    install_dir: pathlib.Path,
    scenario: Scenario,
    game: str,
    map_name: str,
    port: int,
    log_name: str,
) -> list[str]:
    effective_map_name = scenario.map_name or map_name
    command = [
        str(binary),
        "+set",
        "game",
        game,
        "+set",
        "basedir",
        str(install_dir),
        "+set",
        "net_port",
        str(port),
        "+set",
        "logfile",
        "1",
        "+set",
        "logfile_name",
        log_name,
        "+set",
        "logfile_flush",
        "1",
        "+set",
        "developer",
        "1",
        "+set",
        "deathmatch",
        "1",
        "+set",
        "sg_bot_enable",
        "1",
        "+set",
        "sg_bot_debug_route",
        "1",
        "+set",
        "sg_bot_debug_goal",
        "1",
    ]

    for name, value in scenario.extra_cvars:
        command.extend(("+set", name, value))

    command.extend((
        "+set",
        scenario.smoke_cvar,
        str(scenario.smoke_mode),
        "+map",
        effective_map_name,
    ))
    return command


def run_implemented_scenario(
    root: pathlib.Path,
    binary: pathlib.Path,
    install_dir: pathlib.Path,
    scenario: Scenario,
    artifact_dir: pathlib.Path,
    game: str,
    map_name: str,
    port: int,
    timeout: int,
) -> dict[str, Any]:
    started = time.monotonic()
    log_stem = f"bot_scenario_{scenario.name}_{utc_timestamp()}"
    stdout_path = artifact_dir / f"{scenario.name}.stdout.txt"
    stderr_path = artifact_dir / f"{scenario.name}.stderr.txt"
    command = build_command(binary, install_dir, scenario, game, map_name, port, log_stem)

    result: dict[str, Any] = {
        "name": scenario.name,
        "title": scenario.title,
        "status": "error",
        "implemented": True,
        "smoke_cvar": scenario.smoke_cvar,
        "smoke_mode": scenario.smoke_mode,
        "task_ids": list(scenario.task_ids),
        "description": scenario.description,
        "runtime_budget_seconds": scenario.budget_seconds,
        "manual_only": scenario.manual_only,
        "map_name": scenario.map_name or map_name,
        "selection_tags": list(scenario.selection_tags),
        "degradation_policy": degradation_policy_catalog(scenario.degradation_policy),
        "degradation_policy_result": None,
        "port": port,
        "command": command,
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "required_metrics": [check_catalog(check) for check in scenario.checks],
        "required_marker_metrics": [marker_check_catalog(check) for check in scenario.marker_checks],
        "checks": [],
        "marker_checks": [],
        "markers": {},
        "metrics": {},
        "optional_fields": [],
        "status_line": None,
        "returncode": None,
        "duration_seconds": None,
        "duration_budget_passed": None,
        "failures": [],
    }

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, \
            stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
        process = subprocess.Popen(
            command,
            cwd=root,
            stdout=stdout_file,
            stderr=stderr_file,
            text=True,
            creationflags=creationflags,
        )
        try:
            result["returncode"] = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            result["returncode"] = process.wait(timeout=10)
            result["duration_seconds"] = round(time.monotonic() - started, 3)
            result["duration_budget_passed"] = False
            result["status"] = "timeout"
            result["failures"].append(f"timed out after {timeout} seconds")
            return result

    result["duration_seconds"] = round(time.monotonic() - started, 3)
    result["duration_budget_passed"] = (
        scenario.budget_seconds <= 0 or result["duration_seconds"] <= scenario.budget_seconds
    )
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    combined_text = stdout_text + "\n" + stderr_text
    status_line, metrics = parse_status_line(combined_text)
    marker_names = {check.marker for check in scenario.marker_checks}
    marker_names.update(optional_marker_names())
    if scenario.degradation_policy is not None:
        marker_names.update(
            check.marker
            for check in scenario.degradation_policy.required_marker_metrics
        )
    marker_metrics = parse_marker_metrics(combined_text, marker_names)
    result["status_line"] = status_line
    result["metrics"] = metrics
    result["markers"] = marker_metrics
    result["optional_fields"] = discover_optional_fields(metrics, marker_metrics)

    if status_line is None and scenario.checks:
        result["failures"].append(f"missing {STATUS_MARKER} line")

    check_results = [evaluate_check(check, metrics) for check in scenario.checks]
    marker_check_results = [
        evaluate_marker_check(check, marker_metrics)
        for check in scenario.marker_checks
    ]
    degradation_policy_result = evaluate_degradation_policy(
        scenario.degradation_policy,
        metrics,
        marker_metrics,
    )
    result["checks"] = check_results
    result["marker_checks"] = marker_check_results
    result["degradation_policy_result"] = degradation_policy_result
    result["failures"].extend(
        check_result_failure_text(check)
        for check in check_results
        if not check["passed"]
    )
    result["failures"].extend(
        marker_check_result_failure_text(check)
        for check in marker_check_results
        if not check["passed"]
    )
    if degradation_policy_result:
        result["failures"].extend(
            "degradation policy: " + check_result_failure_text(check)
            for check in degradation_policy_result["failed_metric_checks"]
        )
        result["failures"].extend(
            "degradation policy: " + marker_check_result_failure_text(check)
            for check in degradation_policy_result["failed_marker_checks"]
        )

    forbidden_hits = [
        pattern
        for pattern in FORBIDDEN_PATTERNS
        if pattern in stdout_text or pattern in stderr_text
    ]
    if forbidden_hits:
        result["failures"].extend(f"forbidden output matched: {pattern}" for pattern in forbidden_hits)

    result["status"] = "passed" if not result["failures"] else "failed"
    return result


def pending_result(scenario: Scenario) -> dict[str, Any]:
    return {
        "name": scenario.name,
        "title": scenario.title,
        "status": "pending",
        "implemented": False,
        "smoke_cvar": scenario.smoke_cvar,
        "smoke_mode": None,
        "planned_smoke_mode": scenario.planned_smoke_mode,
        "task_ids": list(scenario.task_ids),
        "description": scenario.description,
        "runtime_budget_seconds": scenario.budget_seconds,
        "manual_only": scenario.manual_only,
        "selection_tags": list(scenario.selection_tags),
        "degradation_policy": degradation_policy_catalog(scenario.degradation_policy),
        "degradation_policy_result": None,
        "pending_reason": scenario.pending_reason,
        "required_metrics": [check_catalog(check) for check in scenario.checks],
        "required_marker_metrics": [marker_check_catalog(check) for check in scenario.marker_checks],
        "promotion_required_metrics": promotion_required_metrics(scenario),
        "promotion_required_marker_metrics": [
            marker_metric_catalog(marker_metric)
            for marker_metric in promotion_required_marker_metrics(scenario)
        ],
        "promotion_metric_checks": [check_catalog(check) for check in scenario.promotion_checks],
        "promotion_marker_checks": [
            marker_check_catalog(check)
            for check in scenario.promotion_marker_checks
        ],
        "checks": [],
        "marker_checks": [],
        "markers": {},
        "metrics": {},
        "optional_fields": [],
        "failures": [],
    }


def select_scenarios(tokens: list[str]) -> list[Scenario]:
    names = scenario_map()
    tags = sorted({
        tag
        for scenario in SCENARIOS
        for tag in scenario.selection_tags
    })
    expanded: list[str] = []

    if not tokens:
        tokens = ["all"]

    for token in tokens:
        for part in token.split(","):
            part = part.strip()
            if part:
                expanded.append(part)

    selected: list[Scenario] = []
    seen: set[str] = set()
    for token in expanded:
        if token == "all":
            candidates = [scenario for scenario in SCENARIOS if not scenario.manual_only]
        elif token == "implemented":
            candidates = [
                scenario
                for scenario in SCENARIOS
                if scenario.implemented and not scenario.manual_only
            ]
        elif token == "implemented-with-manual":
            candidates = [scenario for scenario in SCENARIOS if scenario.implemented]
        elif token == "pending":
            candidates = [scenario for scenario in SCENARIOS if not scenario.implemented]
        elif token in {"manual", "manual-only"}:
            candidates = [scenario for scenario in SCENARIOS if scenario.manual_only]
        elif token in tags:
            candidates = [
                scenario
                for scenario in SCENARIOS
                if token in scenario.selection_tags
            ]
        elif token in names:
            candidates = [names[token]]
        else:
            choices = ", ".join(sorted([
                *names.keys(),
                *tags,
                "all",
                "implemented",
                "implemented-with-manual",
                "manual",
                "manual-only",
                "pending",
            ]))
            raise SystemExit(f"Unknown scenario '{token}'. Choices: {choices}")

        for scenario in candidates:
            if scenario.name not in seen:
                selected.append(scenario)
                seen.add(scenario.name)

    return selected


def summarize(results: list[dict[str, Any]]) -> dict[str, Any]:
    counts = {
        "passed": 0,
        "failed": 0,
        "timeout": 0,
        "error": 0,
        "pending": 0,
    }
    for result in results:
        status = result["status"]
        counts[status] = counts.get(status, 0) + 1

    blocking = counts["failed"] + counts["timeout"] + counts["error"]
    return {
        "total": len(results),
        **counts,
        "overall": "pass" if blocking == 0 else "fail",
    }


def print_text_report(report: dict[str, Any]) -> None:
    summary = report["summary"]
    print("Bot scenario smoke summary")
    print(f"Repo: {report['repo_root']}")
    print(f"Binary: {report['binary']}")
    print(f"Install: {report['install_dir']}")
    print(f"Artifacts: {report['artifact_dir']}")
    print(
        "Overall: {overall} ({passed} passed, {failed} failed, {timeout} timeout, "
        "{error} error, {pending} pending)".format(**summary)
    )
    print("")

    for result in report["scenarios"]:
        status = result["status"].upper()
        mode_text = (
            f"mode={smoke_display(result)}"
            if result.get("smoke_mode") is not None
            else "mode=pending"
        )
        print(f"[{status}] {result['name']} ({mode_text}) - {result['title']}")
        if result["status"] == "passed":
            metrics = result.get("metrics", {})
            interesting = [
                "expected_min_commands",
                "elapsed_ms",
                "reports",
                "frames",
                "commands",
                "route_commands",
                "route_failures",
                "route_invalid_slots",
                "route_debug_missing_frames",
                "stuck_detections",
                "recovery_command_uses",
                "item_goal_assignments",
                "item_goal_active_reservations",
                "item_goal_peak_active_reservations",
                "skipped_inactive",
                "pass",
            ]
            parts = [f"{key}={metrics[key]}" for key in interesting if key in metrics]
            for marker, marker_rows in result.get("markers", {}).items():
                if not marker_rows:
                    continue
                marker_metrics = marker_rows[-1]
                for key in ("elapsed_ms", "reports", "cycles", "map_changes", "final_count"):
                    if key in marker_metrics:
                        parts.append(f"{key}={marker_metrics[key]}")
            if parts:
                print(f"  metrics: {' '.join(parts)}")
            optional = optional_field_text(result)
            if optional:
                print(f"  optional_fields: {optional}")
            policy = degradation_policy_text(result)
            if policy:
                print(f"  degradation_policy: {policy}")
            budget = result.get("runtime_budget_seconds", 0)
            if budget:
                duration = result.get("duration_seconds")
                budget_passed = result.get("duration_budget_passed")
                print(f"  runtime: {duration}s budget={budget}s budget_passed={budget_passed}")
        elif result["status"] == "pending":
            print(f"  pending: {result['pending_reason']}")
        else:
            optional = optional_field_text(result)
            if optional:
                print(f"  optional_fields: {optional}")
            for failure in result.get("failures", []):
                print(f"  failure: {failure}")
            policy = degradation_policy_text(result)
            if policy:
                print(f"  degradation_policy: {policy}")
            if result.get("stdout_path"):
                print(f"  stdout: {result['stdout_path']}")
            if result.get("stderr_path"):
                print(f"  stderr: {result['stderr_path']}")


def print_catalog_report(report: dict[str, Any]) -> None:
    summary = report["summary"]
    print("Bot scenario catalog")
    print(
        "Scenarios: {total} ({implemented} implemented, {pending} pending)".format(**summary)
    )
    print("")
    for scenario in report["scenarios"]:
        status = scenario["status"]
        mode = smoke_display(scenario)
        task_ids = ",".join(scenario["task_ids"])
        print(f"[{status.upper()}] {scenario['name']} mode={mode} tasks={task_ids}")
        print(f"  budget_seconds: {scenario['runtime_budget_seconds']}")
        if scenario.get("manual_only"):
            tags = ",".join(scenario.get("selection_tags", []))
            print(f"  manual_only: true tags={tags}")
        if scenario["planned_smoke_mode"] is not None:
            print(f"  planned_smoke_mode: {scenario['planned_smoke_mode']}")
        policy = degradation_policy_text(scenario)
        if policy:
            print(f"  degradation_policy: {policy}")
        if scenario["required_metrics"]:
            metrics = [
                f"{check['metric']} {check['op']} {check['expected']}"
                for check in scenario["required_metrics"]
            ]
            print(f"  required_metrics: {'; '.join(metrics)}")
        if scenario["required_marker_metrics"]:
            marker_metrics = [
                f"{check['source']}::{check['metric']} {check['op']} {check['expected']}"
                for check in scenario["required_marker_metrics"]
            ]
            print(f"  required_marker_metrics: {'; '.join(marker_metrics)}")
        if scenario["promotion_required_metrics"]:
            print(f"  promotion_required_metrics: {', '.join(scenario['promotion_required_metrics'])}")
        if scenario["promotion_required_marker_metrics"]:
            marker_metrics = [
                f"{check['source']}::{check['metric']}"
                for check in scenario["promotion_required_marker_metrics"]
            ]
            print(f"  promotion_required_marker_metrics: {', '.join(marker_metrics)}")
        for blocker in scenario["pending_blockers"]:
            print(f"  pending: {blocker}")


def print_pending_gap_report(report: dict[str, Any]) -> None:
    summary = report["summary"]
    print("Bot scenario pending gap report")
    print(f"Fixture: {report['fixture_path']}")
    print(
        "Scenarios: {total} ({ready} ready, {blocked} blocked, {missing_rows} missing rows, "
        "{raw_diagnostics} raw diagnostics, {raw_diagnostic_rows} raw diagnostic rows, "
        "{pending_rows} pending rows, {missing_status_metrics} missing status metrics, "
        "{missing_marker_metrics} missing marker metrics, "
        "{missing_policy_consumer_fields} missing policy-consumer fields, "
        "{failed_metric_checks} failed metric checks, "
        "{failed_marker_checks} failed marker checks)".format(**summary)
    )
    print(f"Overall: {summary['overall']}")
    print("")

    for scenario in report["scenarios"]:
        planned = scenario["planned_smoke_mode"] if scenario["planned_smoke_mode"] is not None else "-"
        fixture_status = scenario["fixture_status"] if scenario["fixture_status"] is not None else "missing"
        fixture_mode = scenario["fixture_smoke_mode"] if scenario["fixture_smoke_mode"] is not None else "-"
        print(
            f"[{scenario['status'].upper()}] {scenario['name']} "
            f"source={scenario.get('fixture_source', 'missing')} "
            f"planned_mode={planned} fixture_status={fixture_status} fixture_mode={fixture_mode}"
        )
        raw_diagnostic = scenario.get("raw_diagnostic")
        if raw_diagnostic:
            marker_counts = raw_diagnostic.get("marker_counts", {})
            marker_text = ", ".join(f"{marker}:{count}" for marker, count in marker_counts.items())
            print(
                "  raw_diagnostic: "
                f"mode={raw_diagnostic.get('mode')} status={raw_diagnostic.get('status')} "
                f"source={raw_diagnostic.get('source_path')} markers={marker_text}"
            )
        if scenario["missing_metrics"]:
            print(f"  missing_metrics: {', '.join(scenario['missing_metrics'])}")
        if scenario.get("missing_metric_sources"):
            missing_source_parts = [
                f"{metric}<-{source_list_text(sources)}"
                for metric, sources in scenario["missing_metric_sources"].items()
            ]
            print(f"  missing_metric_sources: {', '.join(missing_source_parts)}")
        if scenario["missing_marker_metrics"]:
            missing_marker_metrics = [
                f"{item['source']}::{item['metric']}"
                for item in scenario["missing_marker_metrics"]
            ]
            print(f"  missing_marker_metrics: {', '.join(missing_marker_metrics)}")
        if scenario.get("missing_policy_consumer_fields"):
            missing_policy_fields = [
                policy_consumer_field_text((item["source"], item["metric"]))
                for item in scenario["missing_policy_consumer_fields"]
            ]
            print(f"  missing_policy_consumer_fields: {', '.join(missing_policy_fields)}")
        if scenario.get("metric_sources"):
            metric_source_parts = [
                f"{metric}<-{'+'.join(sources)}"
                for metric, sources in scenario["metric_sources"].items()
            ]
            print(f"  metric_sources: {', '.join(metric_source_parts)}")
        if scenario.get("related_present_metrics"):
            related_metrics = [
                f"{metric}={value}"
                for metric, value in scenario["related_present_metrics"].items()
            ]
            print(f"  related_present_metrics: {', '.join(related_metrics)}")
        optional = optional_field_text(scenario)
        if optional:
            print(f"  optional_fields: {optional}")
        for note in scenario.get("notes", []):
            print(f"  note: {note}")
        for blocker in scenario["blockers"]:
            print(f"  blocker: {blocker}")


def list_scenarios() -> None:
    for scenario in SCENARIOS:
        status = "implemented" if scenario.implemented else "pending"
        mode = smoke_display(scenario_catalog(scenario))
        suffix = " manual" if scenario.manual_only else ""
        print(f"{scenario.name:28} {status:11} mode={mode}  {scenario.title}{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run WORR Q3A BotLib scenario smokes through dedicated-server smoke modes."
    )
    parser.add_argument("--repo-root", default=".", help="WORR repository root")
    parser.add_argument("--binary", default=".install/worr_ded_x86_64.exe", help="Dedicated server binary")
    parser.add_argument("--install-dir", default=".install", help="Prepared install root")
    parser.add_argument("--game", default="basew", help="Game directory to launch")
    parser.add_argument("--map", default="mm-rage", help="Map used by scenario smoke modes")
    parser.add_argument("--scenario", action="append", help="Scenario name, comma list, or all/implemented/pending")
    parser.add_argument("--timeout", type=int, default=60, help="Per-scenario timeout in seconds")
    parser.add_argument("--base-port", type=int, default=27970, help="First net_port to use")
    parser.add_argument("--artifact-dir", default=".tmp/bot_scenarios", help="Output directory for stdout/stderr")
    parser.add_argument("--format", choices=("text", "json", "both"), default="text", help="Console output format")
    parser.add_argument("--json-out", help="Optional machine-readable JSON report path")
    parser.add_argument("--markdown-out", help="Optional Markdown scenario report path")
    parser.add_argument("--compare", help="Compare this report with one previous JSON report")
    parser.add_argument(
        "--pending-gap-report",
        help="Analyze one JSON report for pending scenario promotion gaps and exit without launching the game",
    )
    parser.add_argument(
        "--pending-gap-raw-log",
        action="append",
        default=[],
        help=(
            "Additional raw reserved-mode stdout/stderr file or directory to parse for "
            "pending scenario diagnostics; may be repeated"
        ),
    )
    parser.add_argument("--fail-on-pending", action="store_true", help="Treat pending placeholders as suite failures")
    parser.add_argument("--catalog", action="store_true", help="Emit the declarative scenario catalog and exit")
    parser.add_argument("--list", action="store_true", help="List known scenarios and exit")
    args = parser.parse_args()

    if args.list:
        list_scenarios()
        return 0

    started_utc = utc_timestamp()
    repo_root = pathlib.Path(args.repo_root).resolve()
    if not repo_root.is_dir():
        raise SystemExit(f"Repository root not found: {repo_root}")

    selected = select_scenarios(args.scenario or [])

    if args.catalog:
        report = catalog_report(selected)
        report["repo_root"] = str(repo_root)
        compare_path = resolve_path(repo_root, args.compare) if args.compare else None
        attach_comparison(report, compare_path)
        write_report_outputs(report, repo_root, args.json_out, args.markdown_out)
        if args.format in ("text", "both"):
            print_catalog_report(report)
        if args.format in ("json", "both"):
            print(json.dumps(report, indent=2))
        return 0

    if args.pending_gap_report:
        gap_path = resolve_path(repo_root, args.pending_gap_report)
        if not gap_path.is_file():
            raise SystemExit(f"Pending gap fixture report not found: {gap_path}")
        fixture_report = load_report(gap_path)
        raw_diagnostics = load_raw_reserved_mode_diagnostics(repo_root, args.pending_gap_raw_log)
        report = pending_gap_report(selected, fixture_report, gap_path, raw_diagnostics)
        report["repo_root"] = str(repo_root)
        write_report_outputs(report, repo_root, args.json_out, args.markdown_out)
        if args.format in ("text", "both"):
            print_pending_gap_report(report)
        if args.format in ("json", "both"):
            print(json.dumps(report, indent=2))
        return 0

    binary = resolve_path(repo_root, args.binary)
    install_dir = resolve_path(repo_root, args.install_dir)
    if not binary.is_file():
        raise SystemExit(f"Dedicated server binary not found: {binary}")
    if not install_dir.is_dir():
        raise SystemExit(f"Install dir not found: {install_dir}")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    artifact_root = resolve_path(repo_root, args.artifact_dir)
    artifact_dir = artifact_root / started_utc
    artifact_dir.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, Any]] = []
    run_index = 0
    for scenario in selected:
        if not scenario.implemented:
            results.append(pending_result(scenario))
            continue

        port = args.base_port + run_index
        run_index += 1
        results.append(
            run_implemented_scenario(
                repo_root,
                binary,
                install_dir,
                scenario,
                artifact_dir,
                args.game,
                args.map,
                port,
                args.timeout,
            )
        )

    summary = summarize(results)
    if args.fail_on_pending and summary["pending"] > 0 and summary["overall"] == "pass":
        summary["overall"] = "fail"

    report = {
        "schema_version": 1,
        "started_utc": started_utc,
        "repo_root": str(repo_root),
        "binary": str(binary),
        "install_dir": str(install_dir),
        "artifact_dir": str(artifact_dir),
        "map": args.map,
        "game": args.game,
        "timeout_seconds": args.timeout,
        "catalog": [scenario_catalog(scenario) for scenario in selected],
        "optional_field_families": optional_field_family_catalogs(),
        "summary": summary,
        "scenarios": results,
    }

    compare_path = resolve_path(repo_root, args.compare) if args.compare else None
    attach_comparison(report, compare_path)
    write_report_outputs(report, repo_root, args.json_out, args.markdown_out)

    if args.format in ("text", "both"):
        print_text_report(report)
    if args.format in ("json", "both"):
        print(json.dumps(report, indent=2))

    return 0 if summary["overall"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
