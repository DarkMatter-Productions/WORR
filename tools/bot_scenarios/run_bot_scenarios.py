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
BLACKBOARD_STATUS_MARKER = "q3a_bot_blackboard_status"
OBJECTIVE_STATUS_MARKER = "q3a_bot_objective_status"
SOURCE_STATUS_MARKER = "q3a_bot_source_counter_status"
SOAK_BEGIN_MARKER = "q3a_bot_frame_command_smoke_soak=begin"
SOAK_COMPLETE_MARKER = "q3a_bot_frame_command_smoke_soak=complete"
RAW_RESERVED_METRIC_MARKERS = (
    STATUS_MARKER,
    BLACKBOARD_STATUS_MARKER,
    ACTION_STATUS_MARKER,
    OBJECTIVE_STATUS_MARKER,
    SOURCE_STATUS_MARKER,
)
RAW_RESERVED_MODE_MARKERS = (
    SCENARIO_BEGIN_MARKER,
    *RAW_RESERVED_METRIC_MARKERS,
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
    ("team_objective_", (OBJECTIVE_STATUS_MARKER,)),
    ("last_team_objective_", (OBJECTIVE_STATUS_MARKER,)),
    ("combat_enemy_", (ACTION_STATUS_MARKER, BLACKBOARD_STATUS_MARKER, STATUS_MARKER)),
    ("last_combat_enemy_", (ACTION_STATUS_MARKER, BLACKBOARD_STATUS_MARKER, STATUS_MARKER)),
    ("q3a_", (SOURCE_STATUS_MARKER,)),
    ("bsp_", (SOURCE_STATUS_MARKER,)),
)
RESERVED_MODE_SCENARIOS = {
    20: "engage_enemy",
    21: "switch_weapons",
    22: "health_armor_pickup",
    23: "team_objective",
}
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
            MetricCheck("last_combat_enemy_client", "ge", 0, "enemy client index must be recorded"),
            MetricCheck("last_combat_damage", "ge", 1, "last attributed damage must be positive"),
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
            MetricCheck("last_team_objective_type", "eq", 1, "first promoted objective is enemy flag pickup"),
            MetricCheck("last_team_objective_client", "ge", 0, "objective client index must be recorded"),
            MetricCheck("last_team_objective_item", "ge", 1, "objective item id must be reported"),
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
        if STATUS_TOKEN_RE.search(line):
            status_line = line.strip()
            status_rows.append((
                status_line,
                {match.group(1): int(match.group(2)) for match in KEY_VALUE_RE.finditer(status_line)},
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


def evaluate_marker_check(
    check: MarkerMetricCheck,
    marker_metrics: dict[str, list[dict[str, int | float | str]]],
) -> dict[str, Any]:
    matches = marker_metrics.get(check.marker, [])
    metrics = matches[-1] if matches else {}
    actual = metrics.get(check.metric)
    passed = False
    if actual is not None:
        if check.op == "eq":
            passed = actual == check.expected
        elif check.op == "ge":
            passed = (
                isinstance(actual, int | float)
                and isinstance(check.expected, int | float)
                and actual >= check.expected
            )
        elif check.op == "gt":
            passed = (
                isinstance(actual, int | float)
                and isinstance(check.expected, int | float)
                and actual > check.expected
            )
        elif check.op == "le":
            passed = (
                isinstance(actual, int | float)
                and isinstance(check.expected, int | float)
                and actual <= check.expected
            )
        elif check.op == "lt":
            passed = (
                isinstance(actual, int | float)
                and isinstance(check.expected, int | float)
                and actual < check.expected
            )
        else:
            raise ValueError(f"unknown marker check operator: {check.op}")

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


def parse_marker_fields(line: str) -> dict[str, int | float | str]:
    return {
        match.group(1): parse_marker_value(match.group(2))
        for match in MARKER_FIELD_RE.finditer(line)
    }


def line_has_marker(line: str, marker: str) -> bool:
    pattern = rf"(?<![A-Za-z0-9_]){re.escape(marker)}(?![A-Za-z0-9_])"
    return re.search(pattern, line) is not None


def parse_marker_metrics(text: str, markers: set[str]) -> dict[str, list[dict[str, int | float | str]]]:
    marker_metrics: dict[str, list[dict[str, int | float | str]]] = {
        marker: []
        for marker in sorted(markers)
    }
    if not markers:
        return marker_metrics

    for line in text.splitlines():
        for marker in markers:
            if line_has_marker(line, marker):
                marker_metrics[marker].append(parse_marker_fields(line))

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


def parse_raw_reserved_mode_diagnostics(
    text: str,
    source_path: pathlib.Path | str | None = None,
) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    active: dict[str, Any] | None = None
    source_text = str(source_path) if source_path is not None else None

    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if line_has_marker(stripped, SCENARIO_BEGIN_MARKER):
            fields = parse_marker_fields(stripped)
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

        for marker in RAW_RESERVED_MODE_MARKERS:
            if marker == SCENARIO_BEGIN_MARKER:
                continue
            if line_has_marker(stripped, marker):
                fields = parse_marker_fields(stripped)
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


def pending_gap_scenario(
    scenario: Scenario,
    fixture_result: dict[str, Any] | None,
    raw_diagnostic: dict[str, Any] | None = None,
) -> dict[str, Any]:
    present_metrics: set[str] = set()
    present_marker_metrics: set[tuple[str, str]] = set()
    fixture_metrics: dict[str, Any] = {}
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
            metric_sources = raw_sources if isinstance(raw_sources, dict) else {}
            notes.append("using raw reserved-mode diagnostics because no scenario row exists")
    else:
        fixture_source = "scenario_row"
        fixture_status = fixture_result.get("status")
        fixture_smoke_mode = fixture_result.get("smoke_mode")
        raw_metrics = fixture_result.get("metrics", {})
        raw_markers = fixture_result.get("markers", {})
        fixture_metrics = raw_metrics if isinstance(raw_metrics, dict) else {}
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
    related_metrics = related_promotion_metrics(scenario, fixture_metrics)
    related_note = health_armor_related_note(scenario, related_metrics, missing_metrics)
    if related_note:
        notes.append(related_note)

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
        "missing_metrics": missing_metrics,
        "missing_metric_sources": missing_metric_sources,
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
        "| Scenario | Status | Source | Planned Smoke | Fixture Status | Missing Metrics | Missing Metric Sources | Missing Marker Metrics | Related Metrics | Blockers |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
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
        related = ", ".join(
            f"{metric}={value}"
            for metric, value in scenario.get("related_present_metrics", {}).items()
        )
        blockers = "; ".join(scenario.get("blockers", []))
        lines.append(
            "| {name} | {status} | {source} | {planned} | {fixture_status} | "
            "{missing} | {missing_sources} | {marker_missing} | {related} | {blockers} |".format(
                name=markdown_cell(scenario.get("name", "")),
                status=markdown_cell(scenario.get("status", "")),
                source=markdown_cell(scenario.get("fixture_source", "")),
                planned=markdown_cell(scenario.get("planned_smoke_mode")),
                fixture_status=markdown_cell(scenario.get("fixture_status")),
                missing=markdown_cell(missing),
                missing_sources=markdown_cell(missing_sources),
                marker_missing=markdown_cell(", ".join(marker_missing)),
                related=markdown_cell(related),
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
    lines.append("| Scenario | Status | Smoke | Tasks | Key Metrics | Degradation Policy | Pending Blockers | Artifacts |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for scenario in report.get("scenarios", []):
        tasks = ",".join(scenario.get("task_ids", []))
        smoke = smoke_display(scenario)
        artifacts = "<br>".join(f"`{artifact}`" for artifact in scenario_artifacts(scenario))
        lines.append(
            "| {name} | {status} | {smoke} | {tasks} | {metrics} | {policy} | {pending} | {artifacts} |".format(
                name=markdown_cell(scenario.get("name", "")),
                status=markdown_cell(scenario.get("status", "")),
                smoke=markdown_cell(smoke),
                tasks=markdown_cell(tasks),
                metrics=markdown_cell(scenario_metric_text(scenario)),
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
    if scenario.degradation_policy is not None:
        marker_names.update(
            check.marker
            for check in scenario.degradation_policy.required_marker_metrics
        )
    marker_metrics = parse_marker_metrics(combined_text, marker_names)
    result["status_line"] = status_line
    result["metrics"] = metrics
    result["markers"] = marker_metrics

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
        "{missing_marker_metrics} missing marker metrics, {failed_metric_checks} failed metric checks, "
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
