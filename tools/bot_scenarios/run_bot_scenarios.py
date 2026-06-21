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
NAV_NATURAL_SUPPORT_STATUS_MARKER = "q3a_bot_nav_natural_support_status"
NAV_INTERACTION_CONTEXT_STATUS_MARKER = "q3a_bot_nav_interaction_context_status"
SOURCE_STATUS_MARKER = "q3a_bot_source_counter_status"
TEAM_POLICY_STATUS_MARKER = "q3a_bot_team_policy_status"
MATCH_READINESS_STATUS_MARKER = "q3a_bot_match_readiness_status"
COOP_READINESS_STATUS_MARKER = "q3a_bot_coop_readiness_status"
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
    NAV_NATURAL_SUPPORT_STATUS_MARKER,
    NAV_INTERACTION_CONTEXT_STATUS_MARKER,
    SOURCE_STATUS_MARKER,
)
RAW_RESERVED_OPTIONAL_ONLY_MARKERS = (
    TEAM_POLICY_STATUS_MARKER,
    MATCH_READINESS_STATUS_MARKER,
    COOP_READINESS_STATUS_MARKER,
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
    ("team_fire_avoidance_", (STATUS_MARKER,)),
    ("last_team_fire_avoidance_", (STATUS_MARKER,)),
    ("team_item_role_", (NAV_POLICY_STATUS_MARKER,)),
    ("last_team_item_role_", (NAV_POLICY_STATUS_MARKER,)),
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
            *marker_metric_checks(
                OBJECTIVE_DETAIL_STATUS_MARKER,
            MetricCheck(
                "team_objective_role_policy_requested_honored",
                "ge",
                1,
                "detail status must show requested role policy was honored",
            ),
            MetricCheck(
                "team_objective_role_policy_attack_selections",
                "ge",
                1,
                "detail status must show attack role selections",
            ),
            MetricCheck(
                "team_objective_role_policy_lane_midfield_selections",
                "ge",
                1,
                "detail status must show midfield lane selections",
            ),
            MetricCheck(
                "team_objective_enemy_flag_assignments",
                "ge",
                1,
                "detail status must show enemy-flag assignments",
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
                "last_timed_route_goal_kind",
                "eq",
                7,
                "FFA roam route smoke must end on the FFA timed route owner",
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
                "last_timed_route_goal_kind",
                "eq",
                7,
                "FFA anti-camp smoke must end on the FFA timed route owner",
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
                "last_timed_route_goal_kind",
                "eq",
                5,
                "team-role route smoke must end on the team-role timed route owner",
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
                "last_timed_route_goal_kind",
                "eq",
                6,
                "CTF-role route smoke must end on the CTF-role timed route owner",
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
            MarkerMetricCheck(
                OBJECTIVE_DETAIL_STATUS_MARKER,
                "team_objective_role_policy_dropped_flag_responses",
                "ge",
                1,
                "CTF dropped-flag route smoke must select dropped-flag response policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_DETAIL_STATUS_MARKER,
                "last_team_objective_lane",
                "eq",
                5,
                "CTF dropped-flag route objective must end on dropped-flag response lane",
            ),
            MarkerMetricCheck(
                OBJECTIVE_DETAIL_STATUS_MARKER,
                "last_team_objective_target_source",
                "eq",
                2,
                "CTF dropped-flag route objective must end on a dropped flag entity",
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
            MarkerMetricCheck(
                OBJECTIVE_DETAIL_STATUS_MARKER,
                "team_objective_role_policy_carrier_support_selections",
                "ge",
                1,
                "CTF carrier-support route smoke must select carrier-support policy",
            ),
            MarkerMetricCheck(
                OBJECTIVE_DETAIL_STATUS_MARKER,
                "last_team_objective_lane",
                "eq",
                4,
                "CTF carrier-support route objective must end on carrier-support lane",
            ),
            MarkerMetricCheck(
                OBJECTIVE_DETAIL_STATUS_MARKER,
                "last_team_objective_target_source",
                "eq",
                3,
                "CTF carrier-support route objective must end on a flag carrier",
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
            MarkerMetricCheck(
                OBJECTIVE_DETAIL_STATUS_MARKER,
                "team_objective_role_policy_own_base_return_selections",
                "ge",
                1,
                "CTF base-return route smoke must select own-base-return policy",
            ),
        ),
    ),
    Scenario(
        name="ctf_objective_route",
        title="CTF objective route policy",
        smoke_mode=40,
        description=(
            "Runs a four-bot CTF smoke with sg_bot_ctf_objective_route "
            "enabled and verifies the combined objective policy sees "
            "base-return plus lower-priority CTF candidates while recording "
            "base-return priority deferrals."
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
                STATUS_MARKER,
                "last_timed_route_goal_kind",
                "eq",
                4,
                "coop lead advance smoke must end on the lead-advance route owner",
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

    for status_line, metrics in reversed(status_rows):
        if metrics.get("expected_min_commands", 0) > 0:
            return status_line, metrics
    return status_rows[-1]


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
        if marker != STATUS_MARKER
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
    if not rows:
        return {}
    row = rows[-1]
    return row if isinstance(row, dict) else {}


def discover_optional_fields(
    status_metrics: dict[str, Any],
    marker_metrics: dict[str, list[dict[str, int | float | str]]],
) -> list[dict[str, Any]]:
    sources: list[tuple[str, dict[str, Any]]] = []
    if status_metrics:
        sources.append((STATUS_MARKER, status_metrics))
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
        map_name,
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
