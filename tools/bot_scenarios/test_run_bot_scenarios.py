#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import run_bot_scenarios as harness


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
LATEST_REPORT_FIXTURE = REPO_ROOT / ".tmp" / "bot_scenarios" / "latest_report.json"
RESERVED_MODE_BEGIN_LINES = {
    20: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=20 combat=engage_enemy "
        "weapon_switch=0 item_focus=0 team_objective=0 target=2 gametype=0"
    ),
    21: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=21 combat=switch_weapons "
        "weapon_switch=1 item_focus=0 team_objective=0 target=2 gametype=0"
    ),
    22: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=22 combat=0 "
        "weapon_switch=0 item_focus=health_armor team_objective=0 target=1 gametype=0"
    ),
    23: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=23 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=1 target=4 gametype=1"
    ),
    24: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=24 combat=engage_enemy "
        "weapon_switch=0 item_focus=0 team_objective=0 target=2 gametype=0 "
        "aim_fairness=1 item_timer=0 match_readiness=0"
    ),
    25: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=25 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=1 gametype=0 "
        "aim_fairness=0 item_timer=1 match_readiness=0"
    ),
    26: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=26 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=3 "
        "aim_fairness=0 item_timer=0 match_readiness=1"
    ),
    37: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=37 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=5 "
        "ctf_dropped_flag_route=1"
    ),
    38: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=38 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=5 "
        "ctf_carrier_support_route=1"
    ),
    39: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=39 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=5 "
        "ctf_base_return_route=1"
    ),
    40: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=40 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=5 "
        "ctf_objective_route=1"
    ),
    41: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=41 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=5 "
        "ctf_role_route=1 ctf_objective_route=1 "
        "ctf_objective_route_precedence=1"
    ),
    42: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=42 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=1 "
        "ffa_roam_route=1"
    ),
    43: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=43 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=3 "
        "team_role_combat=1"
    ),
    44: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=44 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=3 "
        "team_fire_avoidance=1 team_role_combat=1"
    ),
    45: (
        f"{harness.SCENARIO_BEGIN_MARKER} mode=45 combat=0 "
        "weapon_switch=0 item_focus=0 team_objective=0 target=4 gametype=1 "
        "ffa_roam_route=1 ffa_spawn_camp_avoidance=1"
    ),
}


def passing_raw_reserved_mode_lines(mode: int) -> list[str]:
    common = [
        RESERVED_MODE_BEGIN_LINES[mode],
        "q3a_bot_source_counter_status q3a_route_build_attempts=4 "
        "q3a_route_build_successes=4 bsp_trace_calls=2",
    ]
    if mode == 20:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0 "
            "combat_enemy_acquisitions=1 combat_enemy_visible=1 "
            "combat_enemy_shootable=1 last_combat_enemy_client=1",
            "q3a_bot_action_status combat_fire_decisions=1 action_attack_decisions=1 "
            "action_applied_attack_buttons=0 combat_damage_events=0 last_combat_damage=0",
            "q3a_bot_action_status combat_fire_decisions=1 action_attack_decisions=1 "
            "action_applied_attack_buttons=1 combat_damage_events=1 last_combat_damage=20",
            "q3a_bot_action_detail_status combat_evaluations=1 combat_fire_decisions=1 "
            "combat_withheld_fire=0 action_applied_cmds=1 "
            "action_applied_attack_buttons=1 action_last_intent_name=attack",
        ]
    if mode == 21:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_action_status combat_weapon_switch_decisions=1 "
            "action_weapon_switch_decisions=1 action_pending_weapon_switches=1 "
            "weapon_switch_requests=1 weapon_switch_completions=1 weapon_switch_failures=0 "
            "weapon_switch_expected_item=5 weapon_switch_actual_item=5 "
            "weapon_switch_expected_match=1",
            "q3a_bot_action_detail_status action_command_request_dispatch_attempts=1 "
            "action_weapon_command_requests=1 action_command_request_submitted=1 "
            "action_last_command_dispatch_outcome_name=submitted",
        ]
    if mode == 22:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_action_status item_low_health_boosts=1 item_low_armor_boosts=1 "
            "item_health_goal_assignments=1 item_armor_goal_assignments=1 "
            "item_health_pickups=1 item_armor_pickups=1 "
            "last_health_pickup_delta=25 last_armor_pickup_delta=50",
            "q3a_bot_action_detail_status item_evaluations=4 "
            "item_focus_health_boosts=1 item_focus_armor_boosts=1 "
            "item_health_seek_decisions=1 item_armor_seek_decisions=1",
        ]
    if mode == 23:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_objective_status team_objective_evaluations=1 "
            "team_objective_assignments=1 team_objective_route_requests=1 "
            "team_objective_route_commands=1 team_objective_reaches=1 "
            "team_objective_flag_pickups=1 "
            "team_objective_role_policy_evaluations=1 "
            "team_objective_role_policy_selections=1 "
            "team_objective_role_policy_lane_midfield_selections=1 "
            "team_objective_enemy_flag_assignments=1 "
            "team_objective_match_policy_evaluations=1 "
            "team_objective_match_policy_ffa=1 "
            "team_objective_match_policy_attack=1 "
            "last_team_objective_type=1 last_team_objective_role=1 "
            "last_team_objective_lane=3 last_team_objective_client=2 "
            "last_team_objective_item=9 last_team_objective_area=334",
            "q3a_bot_objective_detail_status "
            "team_objective_role_policy_requested_honored=1 "
            "team_objective_role_policy_attack_selections=1 "
            "team_objective_role_policy_lane_midfield_selections=1 "
            "team_objective_enemy_flag_assignments=1",
        ]
    if mode == 24:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_action_detail_status combat_evaluations=2 "
            "combat_fire_decisions=1 combat_withheld_fire=0 "
            "action_applied_cmds=1 action_applied_attack_buttons=1 "
            "aim_policy_evaluations=2 aim_policy_fire_allowed=1 "
            "last_aim_policy_failure_name=none "
            "projectile_lead_evaluations=1 projectile_lead_uses=1 "
            "last_projectile_lead_weapon=5 "
            "live_aim_evaluations=2 live_aim_fire_allowed=1 "
            "last_live_aim_weapon=5 last_live_aim_reason=projectile_lead",
        ]
    if mode == 25:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_action_detail_status item_timer_evaluations=2 "
            "item_timer_allowed_uses=1 item_timer_fairness_blocks=0 "
            "item_timer_fuzzed_offsets=0 last_item_timer_allowed=1 "
            "last_item_timer_reason=exact_timer "
            "item_timing_policy_evaluations=2 item_timing_policy_ready=1 "
            "item_last_timing_policy_reason_name=exact_timer "
            "item_timing_consumer_evaluations=1 "
            "item_timing_consumer_ready=1 "
            "item_timing_consumer_live_pickups=0 "
            "item_last_timing_consumer_reason_name=timer_ready",
        ]
    raise AssertionError(f"unexpected reserved mode: {mode}")


def passing_raw_reserved_mode_text(mode: int) -> str:
    return "\n".join(passing_raw_reserved_mode_lines(mode))


def passing_high_bot_soak_text() -> str:
    return "\n".join((
        f"{harness.SOAK_BEGIN_MARKER} target=8 duration_ms=600000 "
        "progress_ms=60000 count=8",
        f"{harness.SOAK_COMPLETE_MARKER} elapsed_ms=600001 duration_ms=600000 "
        "count=8 reports=9",
        "q3a_bot_frame_command_status frames=192036 commands=192036 "
        "route_commands=192036 route_failures=0 route_invalid_slots=0 "
        "route_debug_missing_frames=0 item_goal_active_reservations=1 "
        "item_goal_peak_active_reservations=2 skipped_inactive=0 "
        "expected_min_commands=8 pass=1",
    ))


def pending_promotion_scenario(name: str) -> harness.Scenario:
    promoted = harness.scenario_map()[name]
    proof_checks = tuple(
        harness.MetricCheck(check.metric, check.op, check.expected, check.note)
        for check in promoted.marker_checks
        if check.marker != harness.SCENARIO_BEGIN_MARKER
    )
    promotion_checks = (*promoted.checks, *proof_checks)
    promotion_metrics = tuple(dict.fromkeys(check.metric for check in promotion_checks))
    return harness.Scenario(
        name=promoted.name,
        title=promoted.title,
        smoke_mode=None,
        description=promoted.description,
        task_ids=promoted.task_ids,
        budget_seconds=0,
        pending_reason="Synthetic pre-promotion row used to test pending-gap diagnostics.",
        planned_smoke_mode=promoted.smoke_mode,
        promotion_metrics=promotion_metrics,
        promotion_checks=promotion_checks,
        promotion_marker_checks=tuple(
            check
            for check in promoted.marker_checks
            if check.marker == harness.SCENARIO_BEGIN_MARKER
        ),
    )


def passing_promotion_metrics(scenario: harness.Scenario) -> dict[str, object]:
    metrics: dict[str, object] = {}
    for check in scenario.promotion_checks:
        if check.op == "eq":
            metrics[check.metric] = check.expected
        elif isinstance(check.expected, int | float):
            metrics[check.metric] = check.expected + 1
        else:
            metrics[check.metric] = check.expected
    return metrics


class BotScenarioHarnessTests(unittest.TestCase):
    def test_status_parsing_with_noisy_prefix_uses_last_status(self) -> None:
        text = "\n".join((
            "server chatter before status",
            "bot noise q3a_bot_frame_command_status frames=8 commands=8 route_failures=1 pass=0",
            "prefixed output q3a_bot_frame_command_status frames=92 commands=92 "
            "route_failures=0 last_debug_filter_client=-1 pass=1",
            "q3a_bot_frame_command_smoke_map_repeat_cycle_status_complete "
            "pass_source=q3a_bot_frame_command_status pass=1",
        ))

        line, metrics = harness.parse_status_line(text)

        self.assertIsNotNone(line)
        self.assertTrue(line.startswith("prefixed output"))
        self.assertEqual(metrics["frames"], 92)
        self.assertEqual(metrics["commands"], 92)
        self.assertEqual(metrics["route_failures"], 0)
        self.assertEqual(metrics["last_debug_filter_client"], -1)
        self.assertEqual(metrics["pass"], 1)

    def test_status_parsing_prefers_positive_command_proof_over_cleanup_status(self) -> None:
        text = "\n".join((
            "q3a_bot_frame_command_status frames=184 commands=183 route_commands=183 "
            "route_failures=0 item_goal_active_reservations=8 "
            "expected_min_commands=8 pass=1",
            "q3a_bot_frame_command_smoke_map_repeat_cleanup_status_requested "
            "cycle=2 phase=post_reload reason=final_cycle_complete count=0 status_line=next",
            "q3a_bot_frame_command_status frames=184 commands=183 route_commands=183 "
            "route_failures=0 item_goal_active_reservations=0 "
            "expected_min_commands=0 pass=1",
            "q3a_bot_frame_command_smoke_map_repeat_cleanup_status "
            "cycle=2 phase=post_reload reason=final_cycle_complete "
            "count=0 active_reservations=0 pass=1 status_line=previous",
        ))

        line, metrics = harness.parse_status_line(text)

        self.assertIsNotNone(line)
        self.assertEqual(metrics["expected_min_commands"], 8)
        self.assertEqual(metrics["item_goal_active_reservations"], 8)
        self.assertEqual(metrics["route_commands"], 183)
        self.assertEqual(metrics["pass"], 1)

    def test_mode_19_marker_metric_parsing(self) -> None:
        marker = "q3a_bot_frame_command_smoke_map_repeat=complete"
        text = "\n".join((
            "q3a_bot_frame_command_smoke_map_repeat_cycle=complete cycle=1 completed_cycles=1",
            "log prefix q3a_bot_frame_command_smoke_map_repeat=complete "
            "cycles=2 map_changes=1 final_spawncount=432101776 final_count=0",
        ))

        parsed = harness.parse_marker_metrics(text, {marker})

        self.assertIn(marker, parsed)
        self.assertEqual(len(parsed[marker]), 1)
        self.assertEqual(parsed[marker][0]["cycles"], 2)
        self.assertEqual(parsed[marker][0]["map_changes"], 1)
        self.assertEqual(parsed[marker][0]["final_count"], 0)

    def test_marker_metric_parsing_splits_embedded_status_markers(self) -> None:
        text = "\n".join((
            "q3a_bot_frame_command_status pass=1 route_failures=0 "
            "team_objective_route_commandsq3a_bot_objective_detail_status "
            "team_objective_role_policy_requested_honored=2 "
            "team_objective_enemy_flag_assignments=2",
            "cleanup pass_source=q3a_bot_frame_command_status pass=0",
        ))

        parsed = harness.parse_marker_metrics(
            text,
            {harness.STATUS_MARKER, harness.OBJECTIVE_DETAIL_STATUS_MARKER},
        )

        self.assertEqual(len(parsed[harness.STATUS_MARKER]), 1)
        self.assertEqual(parsed[harness.STATUS_MARKER][0]["pass"], 1)
        self.assertNotIn(
            "team_objective_role_policy_requested_honored",
            parsed[harness.STATUS_MARKER][0],
        )
        self.assertEqual(len(parsed[harness.OBJECTIVE_DETAIL_STATUS_MARKER]), 1)
        self.assertEqual(
            parsed[harness.OBJECTIVE_DETAIL_STATUS_MARKER][0][
                "team_objective_role_policy_requested_honored"
            ],
            2,
        )
        self.assertEqual(
            parsed[harness.OBJECTIVE_DETAIL_STATUS_MARKER][0][
                "team_objective_enemy_flag_assignments"
            ],
            2,
        )

    def test_status_parsing_splits_embedded_frame_command_proof_markers(self) -> None:
        text = "\n".join((
            "q3a_bot_frame_command_status pass=1 frames=184 commands=183 "
            "route_commands=183 item_goal_peak_active_reservations=8 "
            "last_stucq3a_bot_frame_command_status pass=1 frames=184 "
            "commands=183 route_commands=183 route_failures=0 "
            "expected_min_frames=8 expected_min_commands=8 "
            "item_goal_peak_active_reservations=8 recovery_command_uses=2",
            "q3a_bot_frame_command_status pass=1 frames=184 commands=183 "
            "route_commands=183 route_failures=0 expected_min_commands=0",
        ))

        _line, metrics = harness.parse_status_line(text)

        self.assertEqual(metrics["expected_min_commands"], 8)
        self.assertEqual(metrics["item_goal_peak_active_reservations"], 8)
        self.assertEqual(metrics["recovery_command_uses"], 2)
        self.assertEqual(metrics["route_commands"], 183)

    def test_marker_checks_use_latest_row_containing_metric(self) -> None:
        marker_metrics = {
            harness.ACTION_STATUS_MARKER: [
                {"combat_fire_decisions": 3, "combat_withheld_fire": 0},
                {"action_attack_decisions": 2},
            ],
        }

        fire = harness.evaluate_marker_check(
            harness.MarkerMetricCheck(
                harness.ACTION_STATUS_MARKER,
                "combat_fire_decisions",
                "ge",
                1,
            ),
            marker_metrics,
        )
        withheld = harness.evaluate_marker_check(
            harness.MarkerMetricCheck(
                harness.ACTION_STATUS_MARKER,
                "combat_withheld_fire",
                "eq",
                0,
            ),
            marker_metrics,
        )

        self.assertTrue(fire["passed"])
        self.assertEqual(fire["actual"], 3)
        self.assertTrue(withheld["passed"])
        self.assertEqual(withheld["actual"], 0)

    def test_profile_marker_field_parsing_and_exact_marker_matching(self) -> None:
        marker = "q3a_bot_profile_smoke_after_add"
        text = "\n".join((
            "q3a_bot_profile_smoke_after_add_request added=1 count=1",
            "q3a_bot_profile_smoke_after_add count=1 name=B|Smoke profile=smoke "
            "skin=male/grunt skill=4 reaction=250 aggression=0.65 aim_error=2.5 "
            "preferred_weapon=rocketlauncher chat=quiet role=attacker movement=strafe",
        ))

        parsed = harness.parse_marker_metrics(text, {marker})

        self.assertEqual(len(parsed[marker]), 1)
        fields = parsed[marker][0]
        self.assertEqual(fields["count"], 1)
        self.assertEqual(fields["name"], "B|Smoke")
        self.assertEqual(fields["profile"], "smoke")
        self.assertEqual(fields["skin"], "male/grunt")
        self.assertEqual(fields["skill"], 4)
        self.assertEqual(fields["reaction"], 250)
        self.assertEqual(fields["aggression"], 0.65)
        self.assertEqual(fields["aim_error"], 2.5)
        self.assertEqual(fields["preferred_weapon"], "rocketlauncher")
        self.assertEqual(fields["chat"], "quiet")
        self.assertEqual(fields["role"], "attacker")
        self.assertEqual(fields["movement"], "strafe")

    def test_check_evaluation_pass_fail_and_missing(self) -> None:
        passing = harness.evaluate_check(
            harness.MetricCheck("route_failures", "eq", 0),
            {"route_failures": 0},
        )
        failing = harness.evaluate_check(
            harness.MetricCheck("commands", "ge", 8),
            {"commands": 7},
        )
        missing = harness.evaluate_check(
            harness.MetricCheck("item_goal_assignments", "gt", 0),
            {},
        )

        self.assertTrue(passing["passed"])
        self.assertFalse(failing["passed"])
        self.assertEqual(failing["actual"], 7)
        self.assertFalse(missing["passed"])
        self.assertIsNone(missing["actual"])

    def test_promoted_reserved_scenario_catalog_output_shape(self) -> None:
        promoted_scenarios = [
            harness.scenario_map()[name]
            for name in (
                "engage_enemy",
                "switch_weapons",
                "health_armor_pickup",
                "team_objective",
            )
        ]
        report = harness.catalog_report(promoted_scenarios)

        self.assertEqual(report["summary"]["total"], 4)
        self.assertEqual(report["summary"]["implemented"], 4)
        self.assertEqual(report["summary"]["pending"], 0)

        engage_enemy = next(
            scenario for scenario in report["scenarios"]
            if scenario["name"] == "engage_enemy"
        )
        self.assertEqual(engage_enemy["status"], "implemented")
        self.assertEqual(engage_enemy["task_ids"], ["DV-03-T05"])
        self.assertEqual(engage_enemy["smoke_mode"], 20)
        self.assertIsNone(engage_enemy["planned_smoke_mode"])
        self.assertEqual(engage_enemy["runtime_budget_seconds"], 20)
        required_metrics = {
            (check["metric"], check["op"], check["expected"])
            for check in engage_enemy["required_metrics"]
        }
        self.assertIn(("pass", "eq", 1), required_metrics)
        self.assertIn(("route_failures", "eq", 0), required_metrics)
        required_marker_metrics = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in engage_enemy["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 20),
            required_marker_metrics,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "combat_damage_events", "ge", 1),
            required_marker_metrics,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "action_applied_attack_buttons", "ge", 1),
            required_marker_metrics,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "combat_withheld_fire", "eq", 0),
            required_marker_metrics,
        )
        self.assertEqual(engage_enemy["promotion_required_metrics"], [])
        self.assertEqual(engage_enemy["pending_blockers"], [])

        markdown = harness.build_markdown_report(report)
        self.assertIn("# Bot Scenario Catalog", markdown)
        self.assertIn("implemented | 20", markdown)
        self.assertIn("engage_enemy", markdown)

    def test_profile_backed_spawn_catalog_and_command(self) -> None:
        scenario = harness.scenario_map()["profile_backed_spawn"]
        report = harness.catalog_report([scenario])
        profile_spawn = report["scenarios"][0]

        self.assertEqual(profile_spawn["status"], "implemented")
        self.assertEqual(profile_spawn["task_ids"], ["FR-04-T13", "DV-03-T05"])
        self.assertEqual(profile_spawn["smoke_cvar"], "sv_bot_profile_smoke")
        self.assertEqual(profile_spawn["smoke_mode"], 2)
        self.assertEqual(profile_spawn["required_metrics"], [])
        required_marker_metrics = {
            (check["source"], check["metric"], check["expected"])
            for check in profile_spawn["required_marker_metrics"]
        }
        self.assertIn(
            ("q3a_bot_profile_smoke_after_add", "profile", "smoke"),
            required_marker_metrics,
        )
        self.assertIn(
            ("q3a_bot_profile_smoke_after_add", "aggression", 0.65),
            required_marker_metrics,
        )

        command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            scenario,
            "basew",
            "mm-rage",
            27970,
            "profile_smoke",
        )

        self.assertIn("sv_bot_profile_smoke", command)
        self.assertNotIn("sv_bot_frame_command_smoke", command)
        self.assertIn("mm-rage", command)

    def test_high_bot_degradation_catalog_and_selection_policy(self) -> None:
        scenario = harness.scenario_map()["high_bot_soak_degradation"]
        report = harness.catalog_report([scenario])
        row = report["scenarios"][0]

        self.assertTrue(row["manual_only"])
        self.assertEqual(row["selection_tags"], ["soak", "high_bot", "degradation"])
        self.assertEqual(report["summary"]["manual_only"], 1)
        self.assertEqual(report["summary"]["degradation_policies"], 1)
        self.assertEqual(row["degradation_policy"]["name"], "high_bot_long_soak")
        self.assertEqual(row["degradation_policy"]["bot_count"], 8)
        self.assertEqual(
            row["degradation_policy"]["budget_profile"],
            "tools/bot_perf/default_soak_budget.json",
        )
        self.assertIn(
            "final item_goal_active_reservations may fall below eight",
            row["degradation_policy"]["allowed_degradation"],
        )
        required_metrics = {
            (check["metric"], check["op"], check["expected"])
            for check in row["degradation_policy"]["required_metrics"]
        }
        self.assertIn(("commands", "ge", 120000), required_metrics)
        self.assertNotIn(("item_goal_peak_active_reservations", "ge", 8), required_metrics)

        implemented_names = {
            selected.name
            for selected in harness.select_scenarios(["implemented"])
        }
        self.assertNotIn("high_bot_soak_degradation", implemented_names)
        self.assertEqual(
            [selected.name for selected in harness.select_scenarios(["soak"])],
            ["high_bot_soak_degradation"],
        )
        self.assertEqual(
            [selected.name for selected in harness.select_scenarios(["manual"])],
            ["high_bot_soak_degradation"],
        )

        markdown = harness.build_markdown_report(report)
        self.assertIn("Degradation Policy", markdown)
        self.assertIn("high_bot_long_soak", markdown)
        self.assertIn("budget=tools/bot_perf/default_soak_budget.json", markdown)

    def test_high_bot_soak_policy_allows_reservation_decay_but_requires_throughput(self) -> None:
        scenario = harness.scenario_map()["high_bot_soak_degradation"]
        text = passing_high_bot_soak_text()
        _line, metrics = harness.parse_status_line(text)
        marker_metrics = harness.parse_marker_metrics(
            text,
            {check.marker for check in scenario.marker_checks},
        )

        policy_result = harness.evaluate_degradation_policy(
            scenario.degradation_policy,
            metrics,
            marker_metrics,
        )

        self.assertEqual(metrics["item_goal_active_reservations"], 1)
        self.assertEqual(metrics["item_goal_peak_active_reservations"], 2)
        self.assertEqual(policy_result["status"], "passed")
        self.assertEqual(policy_result["failed_metric_checks"], [])
        self.assertEqual(policy_result["failed_marker_checks"], [])

    def test_high_bot_soak_policy_fails_silent_route_and_duration_regressions(self) -> None:
        scenario = harness.scenario_map()["high_bot_soak_degradation"]
        text = "\n".join((
            f"{harness.SOAK_BEGIN_MARKER} target=8 duration_ms=600000 "
            "progress_ms=60000 count=8",
            f"{harness.SOAK_COMPLETE_MARKER} elapsed_ms=120000 duration_ms=600000 "
            "count=8 reports=2",
            "q3a_bot_frame_command_status frames=2000 commands=2000 "
            "route_commands=1999 route_failures=1 route_invalid_slots=0 "
            "route_debug_missing_frames=0 item_goal_active_reservations=0 "
            "item_goal_peak_active_reservations=1 skipped_inactive=0 "
            "expected_min_commands=8 pass=0",
        ))
        _line, metrics = harness.parse_status_line(text)
        marker_metrics = harness.parse_marker_metrics(
            text,
            {check.marker for check in scenario.marker_checks},
        )

        policy_result = harness.evaluate_degradation_policy(
            scenario.degradation_policy,
            metrics,
            marker_metrics,
        )
        failed_metrics = {
            check["metric"]: check["actual"]
            for check in policy_result["failed_metric_checks"]
        }
        failed_marker_metrics = {
            check["metric"]: check["actual"]
            for check in policy_result["failed_marker_checks"]
        }

        self.assertEqual(policy_result["status"], "failed")
        self.assertEqual(failed_metrics["commands"], 2000)
        self.assertEqual(failed_metrics["route_commands"], 1999)
        self.assertEqual(failed_metrics["route_failures"], 1)
        self.assertEqual(failed_marker_metrics["elapsed_ms"], 120000)
        self.assertEqual(failed_marker_metrics["reports"], 2)

    def test_team_policy_readiness_uses_any_marker_checks_for_pre_cleanup_status(self) -> None:
        scenario = harness.scenario_map()["team_policy_duel_readiness"]
        report = harness.catalog_report([scenario])
        row = report["scenarios"][0]

        self.assertEqual(row["status"], "implemented")
        self.assertEqual(row["smoke_cvar"], "sv_bot_team_policy_smoke")
        self.assertEqual(row["smoke_mode"], 2)
        required_marker_metrics = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in row["required_marker_metrics"]
        }
        self.assertIn(
            (harness.TEAM_POLICY_STATUS_MARKER, "bots", "any_eq", 3),
            required_marker_metrics,
        )
        self.assertIn(
            (harness.TEAM_POLICY_STATUS_MARKER, "bots", "eq", 0),
            required_marker_metrics,
        )
        self.assertIn(
            (harness.TEAM_POLICY_STATUS_MARKER, "queued", "any_eq", 0),
            required_marker_metrics,
        )

        marker_metrics = harness.parse_marker_metrics(
            "\n".join((
                "q3a_bot_team_policy_status bots=3 playing=2 spectators=1 queued=0 pass=1",
                "q3a_bot_team_policy_status bots=0 playing=0 spectators=0 queued=0 pass=1",
            )),
            {harness.TEAM_POLICY_STATUS_MARKER},
        )
        pre_cleanup = harness.evaluate_marker_check(
            harness.MarkerMetricCheck(
                harness.TEAM_POLICY_STATUS_MARKER,
                "playing",
                "any_eq",
                2,
            ),
            marker_metrics,
        )
        cleanup = harness.evaluate_marker_check(
            harness.MarkerMetricCheck(
                harness.TEAM_POLICY_STATUS_MARKER,
                "bots",
                "eq",
                0,
            ),
            marker_metrics,
        )

        self.assertTrue(pre_cleanup["passed"])
        self.assertEqual(pre_cleanup["actual"], [2, 0])
        self.assertTrue(cleanup["passed"])
        self.assertEqual(cleanup["actual"], 0)

    def test_duel_queue_spectator_requires_queued_policy_status(self) -> None:
        scenario = harness.scenario_map()["duel_queue_spectator"]
        report = harness.catalog_report([scenario])
        row = report["scenarios"][0]

        self.assertEqual(row["status"], "implemented")
        self.assertEqual(row["smoke_cvar"], "sv_bot_team_policy_smoke")
        self.assertEqual(row["smoke_mode"], 3)
        required_marker_metrics = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in row["required_marker_metrics"]
        }
        self.assertIn(
            ("q3a_bot_team_policy_smoke=begin", "queue_enabled", "eq", 1),
            required_marker_metrics,
        )
        self.assertIn(
            (harness.TEAM_POLICY_STATUS_MARKER, "queued", "any_eq", 1),
            required_marker_metrics,
        )
        self.assertIn(
            (harness.TEAM_POLICY_STATUS_MARKER, "expected_queued", "any_eq", 1),
            required_marker_metrics,
        )

        marker_metrics = harness.parse_marker_metrics(
            "\n".join((
                "q3a_bot_team_policy_smoke=begin queue_enabled=1",
                "q3a_bot_team_policy_status bots=3 playing=2 spectators=1 queued=1 expected_queued=1 pass=1",
                "q3a_bot_team_policy_status bots=0 playing=0 spectators=0 queued=0 expected_queued=0 pass=1",
            )),
            {
                "q3a_bot_team_policy_smoke=begin",
                harness.TEAM_POLICY_STATUS_MARKER,
            },
        )
        queued = harness.evaluate_marker_check(
            harness.MarkerMetricCheck(
                harness.TEAM_POLICY_STATUS_MARKER,
                "queued",
                "any_eq",
                1,
            ),
            marker_metrics,
        )
        cleanup = harness.evaluate_marker_check(
            harness.MarkerMetricCheck(
                harness.TEAM_POLICY_STATUS_MARKER,
                "queued",
                "eq",
                0,
            ),
            marker_metrics,
        )

        self.assertTrue(queued["passed"])
        self.assertEqual(queued["actual"], [1, 0])
        self.assertTrue(cleanup["passed"])
        self.assertEqual(cleanup["actual"], 0)

    def test_policy_trace_and_readiness_promotions_use_expected_smoke_rows(self) -> None:
        scenarios = harness.scenario_map()
        aim = scenarios["aim_fairness_policy_integration"]
        timers = scenarios["item_timer_fairness_signals"]
        team_objective = scenarios["team_objective"]
        trace = scenarios["trace_checked_corner_cutting"]
        match = scenarios["ffa_tdm_match_readiness"]
        duel_queue = scenarios["duel_queue_spectator"]
        coop = scenarios["coop_match_readiness"]
        leader_route = scenarios["coop_leader_route"]
        lead_advance = scenarios["coop_lead_advance"]
        resource_share = scenarios["coop_resource_share"]
        anti_block = scenarios["coop_anti_blocking"]
        target_share = scenarios["coop_target_share"]
        door_elevator = scenarios["coop_door_elevator"]
        ffa_roam_route = scenarios["ffa_roam_route"]
        ffa_spawn_camp_avoidance = scenarios["ffa_spawn_camp_avoidance"]
        team_role_route = scenarios["team_role_route"]
        team_role_combat = scenarios["team_role_combat"]
        team_role_combat_avoidance = scenarios["team_role_combat_avoidance"]
        ctf_role_combat = scenarios["ctf_role_combat"]
        ctf_dropped_flag_route = scenarios["ctf_dropped_flag_route"]
        ctf_carrier_support_route = scenarios["ctf_carrier_support_route"]
        ctf_base_return_route = scenarios["ctf_base_return_route"]
        ctf_objective_route = scenarios["ctf_objective_route"]
        ctf_objective_route_precedence = scenarios["ctf_objective_route_precedence"]
        team_item_roles = scenarios["team_item_roles"]
        progress_wait = scenarios["coop_progress_wait"]
        interaction_retry = scenarios["coop_interaction_retry"]
        report = harness.catalog_report([
            team_objective,
            aim,
            timers,
            trace,
            match,
            duel_queue,
            coop,
            leader_route,
            lead_advance,
            resource_share,
            anti_block,
            target_share,
            door_elevator,
            ffa_roam_route,
            ffa_spawn_camp_avoidance,
            team_role_route,
            team_role_combat,
            team_role_combat_avoidance,
            ctf_role_combat,
            ctf_dropped_flag_route,
            ctf_carrier_support_route,
            ctf_base_return_route,
            ctf_objective_route,
            ctf_objective_route_precedence,
            team_item_roles,
            progress_wait,
            interaction_retry,
        ])
        rows = {row["name"]: row for row in report["scenarios"]}

        self.assertEqual(report["summary"]["implemented"], 27)
        self.assertEqual(report["summary"]["pending"], 0)
        self.assertEqual(rows["team_objective"]["smoke_mode"], 23)
        self.assertEqual(rows["aim_fairness_policy_integration"]["smoke_mode"], 24)
        self.assertEqual(rows["item_timer_fairness_signals"]["smoke_mode"], 25)
        self.assertEqual(rows["trace_checked_corner_cutting"]["smoke_mode"], 21)
        self.assertEqual(rows["ffa_tdm_match_readiness"]["smoke_mode"], 26)
        self.assertEqual(rows["duel_queue_spectator"]["smoke_mode"], 3)
        self.assertEqual(rows["coop_match_readiness"]["smoke_mode"], 3)
        self.assertEqual(rows["coop_leader_route"]["smoke_mode"], 3)
        self.assertEqual(rows["coop_lead_advance"]["smoke_mode"], 27)
        self.assertEqual(rows["coop_resource_share"]["smoke_mode"], 28)
        self.assertEqual(rows["coop_anti_blocking"]["smoke_mode"], 29)
        self.assertEqual(rows["coop_target_share"]["smoke_mode"], 30)
        self.assertEqual(rows["coop_door_elevator"]["smoke_mode"], 31)
        self.assertEqual(rows["ffa_roam_route"]["smoke_mode"], 42)
        self.assertEqual(rows["ffa_spawn_camp_avoidance"]["smoke_mode"], 45)
        self.assertEqual(rows["team_role_route"]["smoke_mode"], 32)
        self.assertEqual(rows["team_role_combat"]["smoke_mode"], 43)
        self.assertEqual(rows["team_role_combat_avoidance"]["smoke_mode"], 44)
        self.assertEqual(rows["ctf_role_combat"]["smoke_mode"], 36)
        self.assertEqual(rows["ctf_dropped_flag_route"]["smoke_mode"], 37)
        self.assertEqual(rows["ctf_carrier_support_route"]["smoke_mode"], 38)
        self.assertEqual(rows["ctf_base_return_route"]["smoke_mode"], 39)
        self.assertEqual(rows["ctf_objective_route"]["smoke_mode"], 40)
        self.assertEqual(rows["ctf_objective_route_precedence"]["smoke_mode"], 41)
        self.assertEqual(rows["team_item_roles"]["smoke_mode"], 33)
        self.assertEqual(rows["coop_progress_wait"]["smoke_mode"], 3)
        self.assertEqual(rows["coop_interaction_retry"]["smoke_mode"], 12)
        self.assertEqual(
            rows["coop_match_readiness"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_leader_route"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_progress_wait"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
                {"name": "sg_bot_coop_progress_wait", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_lead_advance"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
                {"name": "sg_bot_coop_lead_advance", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_resource_share"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
                {"name": "sg_bot_coop_resource_share", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_anti_blocking"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
                {"name": "sg_bot_coop_anti_blocking", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_target_share"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
                {"name": "sg_bot_coop_target_share", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_door_elevator"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
                {"name": "sg_bot_coop_door_elevator", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ffa_roam_route"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "1"},
                {"name": "sg_bot_ffa_roam_route", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ffa_spawn_camp_avoidance"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "1"},
                {"name": "sg_bot_ffa_roam_route", "value": "1"},
                {"name": "sg_bot_ffa_spawn_camp_avoidance", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["team_role_route"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "3"},
                {"name": "sg_bot_team_role_route", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["team_role_combat"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "3"},
                {"name": "sg_bot_team_role_combat", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["team_role_combat_avoidance"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "3"},
                {"name": "sg_bot_team_role_combat", "value": "1"},
                {"name": "sg_bot_team_fire_avoidance", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ctf_role_combat"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "5"},
                {"name": "sg_bot_ctf_role_combat", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ctf_dropped_flag_route"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "5"},
                {"name": "sg_bot_ctf_dropped_flag_route", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ctf_carrier_support_route"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "5"},
                {"name": "sg_bot_ctf_carrier_support_route", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ctf_base_return_route"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "5"},
                {"name": "sg_bot_ctf_base_return_route", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ctf_objective_route"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "5"},
                {"name": "sg_bot_ctf_objective_route", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["ctf_objective_route_precedence"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "5"},
                {"name": "sg_bot_ctf_role_route", "value": "1"},
                {"name": "sg_bot_ctf_objective_route", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["team_item_roles"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "1"},
                {"name": "g_gametype", "value": "3"},
                {"name": "sg_bot_team_item_roles", "value": "1"},
            ],
        )
        self.assertEqual(
            rows["coop_interaction_retry"]["extra_cvars"],
            [
                {"name": "deathmatch", "value": "0"},
                {"name": "coop", "value": "1"},
                {"name": "sg_bot_coop_interaction_retry", "value": "1"},
            ],
        )

        aim_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["aim_fairness_policy_integration"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "aim_fairness", "eq", 1),
            aim_required,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "aim_policy_fire_allowed", "ge", 1),
            aim_required,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "live_aim_evaluations", "ge", 1),
            aim_required,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "live_aim_fire_allowed", "ge", 1),
            aim_required,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "last_live_aim_weapon", "ge", 1),
            aim_required,
        )

        timer_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["item_timer_fairness_signals"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "item_timer", "eq", 1),
            timer_required,
        )
        self.assertIn(
            (harness.ACTION_DETAIL_STATUS_MARKER, "last_item_timer_allowed", "eq", 1),
            timer_required,
        )
        self.assertIn(
            (harness.ACTION_DETAIL_STATUS_MARKER, "item_timing_consumer_evaluations", "ge", 1),
            timer_required,
        )
        self.assertIn(
            (
                harness.ACTION_DETAIL_STATUS_MARKER,
                harness.ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC,
                "ge",
                1,
            ),
            timer_required,
        )

        trace_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["trace_checked_corner_cutting"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.NAV_POLICY_STATUS_MARKER, "route_corner_cut_trace_checks", "ge", 1),
            trace_required,
        )
        self.assertIn(
            (harness.NAV_POLICY_STATUS_MARKER, "route_corner_cut_accepted", "ge", 1),
            trace_required,
        )
        self.assertIn(
            (harness.SOURCE_STATUS_MARKER, "bsp_trace_calls", "ge", 1),
            trace_required,
        )

        team_objective_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["team_objective"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_ffa", "ge", 1),
            team_objective_required,
        )

        match_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ffa_tdm_match_readiness"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "match_readiness", "eq", 1),
            match_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "proof", "eq", 1),
            match_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "tdm_pass", "eq", 1),
            match_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_tdm", "ge", 1),
            match_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_friendly_fire", "ge", 1),
            match_required,
        )

        coop_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_match_readiness"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.COOP_READINESS_STATUS_MARKER, "pass", "eq", 1),
            coop_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "deathmatch", "eq", 0),
            coop_required,
        )

        leader_route_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_leader_route"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.STATUS_MARKER, "last_timed_route_goal_kind", "eq", 3),
            leader_route_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_leader_route_activations", "ge", 1),
            leader_route_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_leader_route_refreshes", "ge", 1),
            leader_route_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_leader_route_spacing_sources", "ge", 1),
            leader_route_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "last_coop_leader_route_intent", "ge", 1),
            leader_route_marker_required,
        )

        lead_advance_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_lead_advance"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 27),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "target", "ge", 1),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_timed_route_goal_kind", "eq", 4),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_lead_advance_requests", "ge", 1),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_lead_advance_policy_leads", "ge", 1),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_lead_advance_activations", "ge", 1),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_lead_advance_route_requests", "ge", 1),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "last_coop_lead_advance_intent", "eq", 4),
            lead_advance_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "last_team_objective_coop_intent", "eq", 4),
            lead_advance_marker_required,
        )

        resource_share_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_resource_share"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 28),
            resource_share_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "target", "ge", 2),
            resource_share_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_coop_policy_resource_share", "ge", 1),
            resource_share_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_resource_policy_reserve", "ge", 1),
            resource_share_marker_required,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "item_reserved_deferrals", "ge", 1),
            resource_share_marker_required,
        )

        anti_block_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_anti_blocking"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 29),
            anti_block_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "target", "ge", 2),
            anti_block_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_anti_block_requests", "ge", 1),
            anti_block_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_anti_block_policy_close", "ge", 1),
            anti_block_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_anti_block_commands", "ge", 1),
            anti_block_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "last_coop_anti_block_intent", "eq", 5),
            anti_block_marker_required,
        )

        target_share_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_target_share"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 30),
            target_share_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "target_share", "eq", 1),
            target_share_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "target", "ge", 2),
            target_share_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_target_share_requests", "ge", 1),
            target_share_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_target_share_source_candidates", "ge", 1),
            target_share_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_target_share_adoptions", "ge", 1),
            target_share_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "last_coop_target_share_intent", "eq", 5),
            target_share_marker_required,
        )

        door_elevator_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_door_elevator"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 31),
            door_elevator_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "door_elevator", "eq", 1),
            door_elevator_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "target", "ge", 2),
            door_elevator_marker_required,
        )
        self.assertIn(
            (harness.NAV_POLICY_STATUS_MARKER, "nav_interaction_elevator_activations", "ge", 1),
            door_elevator_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_door_elevator_source_activations", "ge", 1),
            door_elevator_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_door_elevator_source_commands", "ge", 1),
            door_elevator_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_door_elevator_hold_commands", "ge", 1),
            door_elevator_marker_required,
        )

        ffa_roam_route_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ffa_roam_route"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 42),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ffa_roam_route", "eq", 1),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "ffa_pass", "eq", 1),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_ffa", "ge", 1),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_timed_route_goal_kind", "eq", 7),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ffa_roam_route_activations", "ge", 1),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ffa_roam_route_route_requests", "ge", 1),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ffa_roam_route_mode", "eq", 1),
            ffa_roam_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ffa_roam_route_goal_distance_sq", "gt", 0),
            ffa_roam_route_marker_required,
        )

        ffa_spawn_camp_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ffa_spawn_camp_avoidance"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 45),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ffa_roam_route", "eq", 1),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ffa_spawn_camp_avoidance", "eq", 1),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "ffa_pass", "eq", 1),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_ffa", "ge", 1),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_timed_route_goal_kind", "eq", 7),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ffa_spawn_camp_avoidance_source_selections", "ge", 1),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ffa_spawn_camp_avoidance_route_requests", "ge", 1),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ffa_spawn_camp_avoidance_source_distance_sq", "lt", 147456),
            ffa_spawn_camp_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ffa_spawn_camp_avoidance_policy_avoid", "eq", 1),
            ffa_spawn_camp_marker_required,
        )

        team_role_route_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["team_role_route"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 32),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "team_role_route", "eq", 1),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "tdm_pass", "eq", 1),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_tdm", "ge", 1),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_timed_route_goal_kind", "eq", 5),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "team_role_route_activations", "ge", 1),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "team_role_route_route_requests", "ge", 1),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_team_role_route_mode", "eq", 2),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_team_role_route_role", "ge", 1),
            team_role_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_team_role_route_lane", "ge", 1),
            team_role_route_marker_required,
        )

        team_role_combat_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["team_role_combat"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 43),
            team_role_combat_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "team_role_combat", "eq", 1),
            team_role_combat_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "tdm_pass", "eq", 1),
            team_role_combat_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_tdm", "ge", 1),
            team_role_combat_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "team_role_combat_attack_decisions", "ge", 1),
            team_role_combat_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_team_role_combat_target_visible", "eq", 1),
            team_role_combat_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_team_role_combat_target_shootable", "eq", 1),
            team_role_combat_marker_required,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "action_applied_attack_buttons", "ge", 1),
            team_role_combat_marker_required,
        )

        team_role_combat_avoidance_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["team_role_combat_avoidance"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 44),
            team_role_combat_avoidance_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "team_role_combat", "eq", 1),
            team_role_combat_avoidance_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "team_fire_avoidance", "eq", 1),
            team_role_combat_avoidance_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "team_role_combat_attack_decisions", "ge", 1),
            team_role_combat_avoidance_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "team_fire_avoidance_blocks", "ge", 1),
            team_role_combat_avoidance_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_team_fire_avoidance_blocked", "eq", 1),
            team_role_combat_avoidance_marker_required,
        )

        ctf_role_combat_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ctf_role_combat"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 36),
            ctf_role_combat_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ctf_role_combat", "eq", 1),
            ctf_role_combat_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "gametype", "eq", 5),
            ctf_role_combat_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_match_policy_ctf", "ge", 1),
            ctf_role_combat_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_role_combat_attack_decisions", "ge", 1),
            ctf_role_combat_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_role_combat_target_visible", "eq", 1),
            ctf_role_combat_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_role_combat_target_shootable", "eq", 1),
            ctf_role_combat_marker_required,
        )
        self.assertIn(
            (harness.ACTION_STATUS_MARKER, "action_applied_attack_buttons", "ge", 1),
            ctf_role_combat_marker_required,
        )

        ctf_dropped_flag_route_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ctf_dropped_flag_route"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 37),
            ctf_dropped_flag_route_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ctf_dropped_flag_route", "eq", 1),
            ctf_dropped_flag_route_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "gametype", "eq", 5),
            ctf_dropped_flag_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_dropped_flag_route_assignments", "ge", 1),
            ctf_dropped_flag_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_dropped_flag_route_route_commands", "ge", 1),
            ctf_dropped_flag_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_dropped_flag_route_lane", "eq", 5),
            ctf_dropped_flag_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_dropped_flag_route_source", "eq", 2),
            ctf_dropped_flag_route_marker_required,
        )
        self.assertIn(
            (
                harness.OBJECTIVE_DETAIL_STATUS_MARKER,
                "team_objective_role_policy_dropped_flag_responses",
                "ge",
                1,
            ),
            ctf_dropped_flag_route_marker_required,
        )

        ctf_carrier_support_route_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ctf_carrier_support_route"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 38),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ctf_carrier_support_route", "eq", 1),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "gametype", "eq", 5),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_carrier_support_route_assignments", "ge", 1),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_carrier_support_route_route_commands", "ge", 1),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_carrier_support_route_role", "eq", 4),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_carrier_support_route_lane", "eq", 4),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_carrier_support_route_source", "eq", 3),
            ctf_carrier_support_route_marker_required,
        )
        self.assertIn(
            (
                harness.OBJECTIVE_DETAIL_STATUS_MARKER,
                "team_objective_role_policy_carrier_support_selections",
                "ge",
                1,
            ),
            ctf_carrier_support_route_marker_required,
        )

        ctf_base_return_route_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ctf_base_return_route"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 39),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ctf_base_return_route", "eq", 1),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "gametype", "eq", 5),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_base_return_route_assignments", "ge", 1),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_base_return_route_route_commands", "ge", 1),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_base_return_route_role", "eq", 3),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_base_return_route_lane", "eq", 6),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_base_return_route_type", "eq", 2),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_base_return_route_source", "eq", 3),
            ctf_base_return_route_marker_required,
        )
        self.assertIn(
            (
                harness.OBJECTIVE_DETAIL_STATUS_MARKER,
                "team_objective_role_policy_own_base_return_selections",
                "ge",
                1,
            ),
            ctf_base_return_route_marker_required,
        )

        ctf_objective_route_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ctf_objective_route"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 40),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ctf_objective_route", "eq", 1),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "gametype", "eq", 5),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (
                harness.STATUS_MARKER,
                "ctf_objective_route_base_return_candidates",
                "ge",
                1,
            ),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (
                harness.STATUS_MARKER,
                "ctf_objective_route_carrier_support_candidates",
                "ge",
                1,
            ),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (
                harness.STATUS_MARKER,
                "ctf_objective_route_dropped_flag_candidates",
                "ge",
                1,
            ),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (
                harness.STATUS_MARKER,
                "ctf_objective_route_base_return_selections",
                "ge",
                1,
            ),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (
                harness.STATUS_MARKER,
                "ctf_objective_route_carrier_support_selections",
                "ge",
                1,
            ),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (
                harness.STATUS_MARKER,
                "ctf_objective_route_dropped_flag_deferrals",
                "ge",
                1,
            ),
            ctf_objective_route_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "last_ctf_objective_route_selection", "ge", 1),
            ctf_objective_route_marker_required,
        )

        ctf_objective_route_precedence_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["ctf_objective_route_precedence"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 41),
            ctf_objective_route_precedence_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ctf_role_route", "eq", 1),
            ctf_objective_route_precedence_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "ctf_objective_route", "eq", 1),
            ctf_objective_route_precedence_marker_required,
        )
        self.assertIn(
            (
                harness.SCENARIO_BEGIN_MARKER,
                "ctf_objective_route_precedence",
                "eq",
                1,
            ),
            ctf_objective_route_precedence_marker_required,
        )
        self.assertIn(
            (
                harness.STATUS_MARKER,
                "ctf_role_route_objective_deferrals",
                "ge",
                1,
            ),
            ctf_objective_route_precedence_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_role_route_activations", "eq", 0),
            ctf_objective_route_precedence_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_role_route_route_requests", "eq", 0),
            ctf_objective_route_precedence_marker_required,
        )
        self.assertIn(
            (harness.STATUS_MARKER, "ctf_objective_route_route_commands", "ge", 1),
            ctf_objective_route_precedence_marker_required,
        )

        team_item_roles_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["team_item_roles"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "mode", "eq", 33),
            team_item_roles_marker_required,
        )
        self.assertIn(
            (harness.SCENARIO_BEGIN_MARKER, "team_item_roles", "eq", 1),
            team_item_roles_marker_required,
        )
        self.assertIn(
            (harness.MATCH_READINESS_STATUS_MARKER, "tdm_pass", "eq", 1),
            team_item_roles_marker_required,
        )
        self.assertIn(
            (
                harness.OBJECTIVE_STATUS_MARKER,
                "team_objective_item_role_policy_selections",
                "ge",
                1,
            ),
            team_item_roles_marker_required,
        )
        self.assertIn(
            (harness.NAV_POLICY_STATUS_MARKER, "team_item_role_selected_goals", "ge", 1),
            team_item_roles_marker_required,
        )
        self.assertIn(
            (harness.NAV_POLICY_STATUS_MARKER, "last_team_item_role_mode", "eq", 2),
            team_item_roles_marker_required,
        )
        self.assertIn(
            (harness.NAV_POLICY_STATUS_MARKER, "last_team_item_role_item_role", "ge", 1),
            team_item_roles_marker_required,
        )

        progress_wait_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_progress_wait"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_progress_wait_requests", "ge", 1),
            progress_wait_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_progress_wait_policy_waits", "ge", 1),
            progress_wait_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_progress_wait_commands", "ge", 1),
            progress_wait_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "last_coop_progress_wait_intent", "eq", 2),
            progress_wait_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "team_objective_coop_policy_wait", "ge", 1),
            progress_wait_marker_required,
        )
        self.assertIn(
            (harness.OBJECTIVE_STATUS_MARKER, "last_team_objective_coop_intent", "eq", 2),
            progress_wait_marker_required,
        )

        interaction_retry_marker_required = {
            (check["source"], check["metric"], check["op"], check["expected"])
            for check in rows["coop_interaction_retry"]["required_marker_metrics"]
        }
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_interaction_retry_requests", "ge", 1),
            interaction_retry_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_interaction_retry_activations", "ge", 1),
            interaction_retry_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "coop_interaction_retry_commands", "ge", 1),
            interaction_retry_marker_required,
        )
        self.assertIn(
            (harness.COOP_COMMAND_STATUS_MARKER, "last_coop_interaction_retry_action", "eq", 3),
            interaction_retry_marker_required,
        )
        self.assertIn(
            (harness.NAV_INTERACTION_CONTEXT_STATUS_MARKER, "interaction_world_entities", "ge", 1),
            interaction_retry_marker_required,
        )

        command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            coop,
            "basew",
            "mm-rage",
            27971,
            "coop_readiness",
        )
        deathmatch_indices = [
            index
            for index, token in enumerate(command)
            if token == "deathmatch"
        ]
        self.assertGreaterEqual(len(deathmatch_indices), 2)
        self.assertEqual(command[deathmatch_indices[-1] + 1], "0")
        self.assertEqual(command[command.index("coop") + 1], "1")
        self.assertLess(command.index("coop"), command.index("sv_bot_frame_command_smoke"))

        leader_route_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            leader_route,
            "basew",
            "mm-rage",
            27972,
            "coop_leader_route",
        )
        self.assertEqual(leader_route_command[leader_route_command.index("coop") + 1], "1")
        self.assertLess(leader_route_command.index("coop"), leader_route_command.index("sv_bot_frame_command_smoke"))

        lead_advance_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            lead_advance,
            "basew",
            "mm-rage",
            27973,
            "coop_lead_advance",
        )
        self.assertEqual(lead_advance_command[lead_advance_command.index("coop") + 1], "1")
        self.assertEqual(
            lead_advance_command[lead_advance_command.index("sg_bot_coop_lead_advance") + 1],
            "1",
        )
        self.assertLess(
            lead_advance_command.index("sg_bot_coop_lead_advance"),
            lead_advance_command.index("sv_bot_frame_command_smoke"),
        )

        resource_share_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            resource_share,
            "basew",
            "mm-rage",
            27974,
            "coop_resource_share",
        )
        self.assertEqual(resource_share_command[resource_share_command.index("coop") + 1], "1")
        self.assertEqual(
            resource_share_command[resource_share_command.index("sg_bot_coop_resource_share") + 1],
            "1",
        )
        self.assertLess(
            resource_share_command.index("sg_bot_coop_resource_share"),
            resource_share_command.index("sv_bot_frame_command_smoke"),
        )

        anti_block_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            anti_block,
            "basew",
            "mm-rage",
            27975,
            "coop_anti_blocking",
        )
        self.assertEqual(anti_block_command[anti_block_command.index("coop") + 1], "1")
        self.assertEqual(
            anti_block_command[anti_block_command.index("sg_bot_coop_anti_blocking") + 1],
            "1",
        )
        self.assertLess(
            anti_block_command.index("sg_bot_coop_anti_blocking"),
            anti_block_command.index("sv_bot_frame_command_smoke"),
        )

        target_share_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            target_share,
            "basew",
            "mm-rage",
            27978,
            "coop_target_share",
        )
        self.assertEqual(target_share_command[target_share_command.index("coop") + 1], "1")
        self.assertEqual(
            target_share_command[target_share_command.index("sg_bot_coop_target_share") + 1],
            "1",
        )
        self.assertLess(
            target_share_command.index("sg_bot_coop_target_share"),
            target_share_command.index("sv_bot_frame_command_smoke"),
        )

        door_elevator_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            door_elevator,
            "basew",
            "mm-rage",
            27979,
            "coop_door_elevator",
        )
        self.assertEqual(door_elevator_command[door_elevator_command.index("coop") + 1], "1")
        self.assertEqual(
            door_elevator_command[door_elevator_command.index("sg_bot_coop_door_elevator") + 1],
            "1",
        )
        self.assertLess(
            door_elevator_command.index("sg_bot_coop_door_elevator"),
            door_elevator_command.index("sv_bot_frame_command_smoke"),
        )

        ffa_roam_route_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            ffa_roam_route,
            "basew",
            "mm-rage",
            27982,
            "ffa_roam_route",
        )
        self.assertEqual(ffa_roam_route_command[ffa_roam_route_command.index("deathmatch") + 1], "1")
        self.assertEqual(ffa_roam_route_command[ffa_roam_route_command.index("g_gametype") + 1], "1")
        self.assertEqual(
            ffa_roam_route_command[ffa_roam_route_command.index("sg_bot_ffa_roam_route") + 1],
            "1",
        )
        self.assertLess(
            ffa_roam_route_command.index("sg_bot_ffa_roam_route"),
            ffa_roam_route_command.index("sv_bot_frame_command_smoke"),
        )

        ffa_spawn_camp_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            ffa_spawn_camp_avoidance,
            "basew",
            "mm-rage",
            27982,
            "ffa_spawn_camp_avoidance",
        )
        self.assertEqual(ffa_spawn_camp_command[ffa_spawn_camp_command.index("deathmatch") + 1], "1")
        self.assertEqual(ffa_spawn_camp_command[ffa_spawn_camp_command.index("g_gametype") + 1], "1")
        self.assertEqual(
            ffa_spawn_camp_command[ffa_spawn_camp_command.index("sg_bot_ffa_roam_route") + 1],
            "1",
        )
        self.assertEqual(
            ffa_spawn_camp_command[
                ffa_spawn_camp_command.index("sg_bot_ffa_spawn_camp_avoidance") + 1
            ],
            "1",
        )
        self.assertLess(
            ffa_spawn_camp_command.index("sg_bot_ffa_spawn_camp_avoidance"),
            ffa_spawn_camp_command.index("sv_bot_frame_command_smoke"),
        )

        team_role_route_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            team_role_route,
            "basew",
            "mm-rage",
            27980,
            "team_role_route",
        )
        self.assertEqual(team_role_route_command[team_role_route_command.index("deathmatch") + 1], "1")
        self.assertEqual(team_role_route_command[team_role_route_command.index("g_gametype") + 1], "3")
        self.assertEqual(
            team_role_route_command[team_role_route_command.index("sg_bot_team_role_route") + 1],
            "1",
        )
        self.assertLess(
            team_role_route_command.index("sg_bot_team_role_route"),
            team_role_route_command.index("sv_bot_frame_command_smoke"),
        )

        team_role_combat_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            team_role_combat,
            "basew",
            "mm-rage",
            27986,
            "team_role_combat",
        )
        self.assertEqual(team_role_combat_command[team_role_combat_command.index("deathmatch") + 1], "1")
        self.assertEqual(team_role_combat_command[team_role_combat_command.index("g_gametype") + 1], "3")
        self.assertEqual(
            team_role_combat_command[team_role_combat_command.index("sg_bot_team_role_combat") + 1],
            "1",
        )
        self.assertLess(
            team_role_combat_command.index("sg_bot_team_role_combat"),
            team_role_combat_command.index("sv_bot_frame_command_smoke"),
        )

        team_role_combat_avoidance_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            team_role_combat_avoidance,
            "basew",
            "mm-rage",
            27987,
            "team_role_combat_avoidance",
        )
        self.assertEqual(
            team_role_combat_avoidance_command[
                team_role_combat_avoidance_command.index("deathmatch") + 1
            ],
            "1",
        )
        self.assertEqual(
            team_role_combat_avoidance_command[
                team_role_combat_avoidance_command.index("g_gametype") + 1
            ],
            "3",
        )
        self.assertEqual(
            team_role_combat_avoidance_command[
                team_role_combat_avoidance_command.index("sg_bot_team_role_combat") + 1
            ],
            "1",
        )
        self.assertEqual(
            team_role_combat_avoidance_command[
                team_role_combat_avoidance_command.index("sg_bot_team_fire_avoidance") + 1
            ],
            "1",
        )
        self.assertLess(
            team_role_combat_avoidance_command.index("sg_bot_team_role_combat"),
            team_role_combat_avoidance_command.index("sv_bot_frame_command_smoke"),
        )
        self.assertLess(
            team_role_combat_avoidance_command.index("sg_bot_team_fire_avoidance"),
            team_role_combat_avoidance_command.index("sv_bot_frame_command_smoke"),
        )

        team_item_roles_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            team_item_roles,
            "basew",
            "mm-rage",
            27983,
            "team_item_roles",
        )
        self.assertEqual(team_item_roles_command[team_item_roles_command.index("deathmatch") + 1], "1")
        self.assertEqual(team_item_roles_command[team_item_roles_command.index("g_gametype") + 1], "3")
        self.assertEqual(
            team_item_roles_command[team_item_roles_command.index("sg_bot_team_item_roles") + 1],
            "1",
        )
        self.assertLess(
            team_item_roles_command.index("sg_bot_team_item_roles"),
            team_item_roles_command.index("sv_bot_frame_command_smoke"),
        )

        progress_wait_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            progress_wait,
            "basew",
            "mm-rage",
            27981,
            "coop_progress_wait",
        )
        self.assertEqual(progress_wait_command[progress_wait_command.index("coop") + 1], "1")
        self.assertEqual(
            progress_wait_command[progress_wait_command.index("sg_bot_coop_progress_wait") + 1],
            "1",
        )
        self.assertLess(
            progress_wait_command.index("sg_bot_coop_progress_wait"),
            progress_wait_command.index("sv_bot_frame_command_smoke"),
        )

        interaction_retry_command = harness.build_command(
            pathlib.Path(".install/worr_ded_x86_64.exe"),
            pathlib.Path(".install"),
            interaction_retry,
            "basew",
            "mm-rage",
            27982,
            "coop_interaction_retry",
        )
        self.assertEqual(interaction_retry_command[interaction_retry_command.index("coop") + 1], "1")
        self.assertEqual(
            interaction_retry_command[interaction_retry_command.index("sg_bot_coop_interaction_retry") + 1],
            "1",
        )
        self.assertLess(
            interaction_retry_command.index("sg_bot_coop_interaction_retry"),
            interaction_retry_command.index("sv_bot_frame_command_smoke"),
        )

        pending_names = {
            selected.name
            for selected in harness.select_scenarios(["pending"])
        }
        self.assertEqual(pending_names, set())

    def test_trace_and_coop_marker_checks_accept_promoted_fixture_rows(self) -> None:
        scenarios = harness.scenario_map()
        trace = scenarios["trace_checked_corner_cutting"]
        coop = scenarios["coop_match_readiness"]
        leader_route = scenarios["coop_leader_route"]
        lead_advance = scenarios["coop_lead_advance"]
        resource_share = scenarios["coop_resource_share"]
        anti_block = scenarios["coop_anti_blocking"]
        target_share = scenarios["coop_target_share"]
        door_elevator = scenarios["coop_door_elevator"]
        ffa_roam_route = scenarios["ffa_roam_route"]
        ffa_spawn_camp_avoidance = scenarios["ffa_spawn_camp_avoidance"]
        team_role_route = scenarios["team_role_route"]
        team_role_combat = scenarios["team_role_combat"]
        team_role_combat_avoidance = scenarios["team_role_combat_avoidance"]
        team_item_roles = scenarios["team_item_roles"]
        interaction_retry = scenarios["coop_interaction_retry"]

        trace_text = "\n".join((
            RESERVED_MODE_BEGIN_LINES[21],
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_nav_policy_status route_corner_cut_candidates=35 "
            "route_corner_cut_trace_checks=38 route_corner_cut_trace_hits=9 "
            "route_corner_cut_ground_trace_checks=27 route_corner_cut_accepted=9 "
            "trace_checked_corner_cut_accepted=9",
            "q3a_bot_source_counter_status bsp_trace_calls=2",
        ))
        trace_marker_metrics = harness.parse_marker_metrics(
            trace_text,
            {check.marker for check in trace.marker_checks},
        )
        trace_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, trace_marker_metrics)
                for check in trace.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(trace_failed, [])

        coop_text = "\n".join((
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=2 "
            "playing=2 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=2 playing=2 "
            "spectators=0 queued=0 free=2 red=0 blue=0",
        ))
        coop_marker_metrics = harness.parse_marker_metrics(
            coop_text,
            {check.marker for check in coop.marker_checks},
        )
        coop_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, coop_marker_metrics)
                for check in coop.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(coop_failed, [])

        leader_route_text = "\n".join((
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 last_timed_route_goal_kind=3",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=2 "
            "playing=2 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=2 playing=2 "
            "spectators=0 queued=0 free=2 red=0 blue=0",
            "q3a_bot_coop_command_status "
            "coop_leader_route_activations=16 "
            "coop_leader_route_refreshes=14 "
            "coop_leader_route_spacing_sources=16 "
            "last_coop_leader_route_intent=5 "
            "last_coop_leader_route_intent_name=support_combat",
        ))
        leader_route_marker_metrics = harness.parse_marker_metrics(
            leader_route_text,
            {check.marker for check in leader_route.marker_checks},
        )
        leader_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, leader_route_marker_metrics)
                for check in leader_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(leader_route_failed, [])

        lead_advance_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=27 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=1 "
            "gametype=0",
            "q3a_bot_frame_command_status pass=1 route_commands=12 "
            "route_failures=0 last_timed_route_goal_kind=4",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=1 "
            "playing=1 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=1 playing=1 "
            "spectators=0 queued=0 free=1 red=0 blue=0",
            "q3a_bot_objective_status "
            "last_team_objective_coop_intent=4 "
            "last_team_objective_coop_intent_name=lead_advance",
            "q3a_bot_coop_command_status "
            "coop_lead_advance_requests=12 "
            "coop_lead_advance_policy_leads=12 "
            "coop_lead_advance_activations=12 "
            "coop_lead_advance_route_requests=12 "
            "last_coop_lead_advance_intent=4 "
            "last_coop_lead_advance_intent_name=lead_advance",
        ))
        lead_advance_marker_metrics = harness.parse_marker_metrics(
            lead_advance_text,
            {check.marker for check in lead_advance.marker_checks},
        )
        lead_advance_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, lead_advance_marker_metrics)
                for check in lead_advance.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(lead_advance_failed, [])

        resource_share_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=28 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=2 "
            "gametype=0",
            "q3a_bot_frame_command_status pass=1 route_commands=12 "
            "route_failures=0",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=2 "
            "playing=2 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=2 playing=2 "
            "spectators=0 queued=0 free=2 red=0 blue=0",
            "q3a_bot_objective_status "
            "team_objective_coop_policy_resource_share=8 "
            "team_objective_resource_policy_reserve=5 "
            "last_team_objective_resource_intent=3 "
            "last_team_objective_resource_intent_name=reserve_for_teammate",
            "q3a_bot_action_status item_reserved_deferrals=5",
        ))
        resource_share_marker_metrics = harness.parse_marker_metrics(
            resource_share_text,
            {check.marker for check in resource_share.marker_checks},
        )
        resource_share_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, resource_share_marker_metrics)
                for check in resource_share.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(resource_share_failed, [])

        anti_block_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=29 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=2 "
            "gametype=0",
            "q3a_bot_frame_command_status pass=1 route_commands=12 "
            "route_failures=0",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=2 "
            "playing=2 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=2 playing=2 "
            "spectators=0 queued=0 free=2 red=0 blue=0",
            "q3a_bot_coop_command_status "
            "coop_anti_block_requests=12 "
            "coop_anti_block_policy_close=8 "
            "coop_anti_block_commands=8 "
            "last_coop_anti_block_intent=5 "
            "last_coop_anti_block_intent_name=support_combat "
            "last_coop_anti_block_leader_distance_sq=1024",
        ))
        anti_block_marker_metrics = harness.parse_marker_metrics(
            anti_block_text,
            {check.marker for check in anti_block.marker_checks},
        )
        anti_block_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, anti_block_marker_metrics)
                for check in anti_block.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(anti_block_failed, [])

        target_share_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=30 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=2 "
            "gametype=0 target_share=1",
            "q3a_bot_frame_command_status pass=1 route_commands=12 "
            "route_failures=0",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=2 "
            "playing=2 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=2 playing=2 "
            "spectators=0 queued=0 free=2 red=0 blue=0",
            "q3a_bot_coop_command_status "
            "coop_target_share_requests=8 "
            "coop_target_share_policy_supports=6 "
            "coop_target_share_source_scans=6 "
            "coop_target_share_source_candidates=4 "
            "coop_target_share_adoptions=4 "
            "last_coop_target_share_client=1 "
            "last_coop_target_share_source_client=0 "
            "last_coop_target_share_target_entity=17 "
            "last_coop_target_share_target_client=-1 "
            "last_coop_target_share_target_distance_sq=4096 "
            "last_coop_target_share_intent=5 "
            "last_coop_target_share_intent_name=support_combat",
        ))
        target_share_marker_metrics = harness.parse_marker_metrics(
            target_share_text,
            {check.marker for check in target_share.marker_checks},
        )
        target_share_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, target_share_marker_metrics)
                for check in target_share.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(target_share_failed, [])

        door_elevator_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=31 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=2 "
            "gametype=0 door_elevator=1",
            "q3a_bot_frame_command_status pass=1 route_commands=12 "
            "route_failures=0",
            "q3a_bot_nav_policy_status nav_interaction_elevator_activations=1",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=2 "
            "playing=2 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=2 playing=2 "
            "spectators=0 queued=0 free=2 red=0 blue=0",
            "q3a_bot_coop_command_status "
            "coop_door_elevator_requests=8 "
            "coop_door_elevator_source_activations=1 "
            "coop_door_elevator_source_commands=4 "
            "coop_door_elevator_hold_commands=4 "
            "last_coop_door_elevator_client=1 "
            "last_coop_door_elevator_source_client=0 "
            "last_coop_door_elevator_action=3 "
            "last_coop_door_elevator_kind=3 "
            "last_coop_door_elevator_entity=17 "
            "last_coop_door_elevator_intent=5 "
            "last_coop_door_elevator_intent_name=support_combat",
        ))
        door_elevator_marker_metrics = harness.parse_marker_metrics(
            door_elevator_text,
            {check.marker for check in door_elevator.marker_checks},
        )
        door_elevator_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, door_elevator_marker_metrics)
                for check in door_elevator.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(door_elevator_failed, [])

        ffa_roam_route_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=42 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=1 ffa_roam_route=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 last_timed_route_goal_kind=7 "
            "ffa_roam_route_requests=16 "
            "ffa_roam_route_policy_selections=16 "
            "ffa_roam_route_activations=4 "
            "ffa_roam_route_route_requests=12 "
            "ffa_roam_route_invalid_skips=0 "
            "last_ffa_roam_route_client=2 "
            "last_ffa_roam_route_mode=1 "
            "last_ffa_roam_route_mode_name=free_for_all "
            "last_ffa_roam_route_role=1 "
            "last_ffa_roam_route_role_name=attacker "
            "last_ffa_roam_route_lane=3 "
            "last_ffa_roam_route_lane_name=midfield "
            "last_ffa_roam_route_priority=660 "
            "last_ffa_roam_route_goal_distance_sq=802816",
            "q3a_bot_match_readiness_status ffa_pass=1 tdm_pass=0 "
            "deathmatch=1 team_mode=0 gametype=1 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=0 blue=0",
            "q3a_bot_objective_status "
            "team_objective_match_policy_ffa=16",
        ))
        ffa_roam_route_marker_metrics = harness.parse_marker_metrics(
            ffa_roam_route_text,
            {check.marker for check in ffa_roam_route.marker_checks},
        )
        ffa_roam_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ffa_roam_route_marker_metrics)
                for check in ffa_roam_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ffa_roam_route_failed, [])

        ffa_spawn_camp_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=45 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=1 ffa_roam_route=1 ffa_spawn_camp_avoidance=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 last_timed_route_goal_kind=7 "
            "ffa_roam_route_activations=4 "
            "ffa_roam_route_route_requests=12 "
            "ffa_spawn_camp_avoidance_requests=16 "
            "ffa_spawn_camp_avoidance_policy_selections=16 "
            "ffa_spawn_camp_avoidance_source_selections=4 "
            "ffa_spawn_camp_avoidance_activations=4 "
            "ffa_spawn_camp_avoidance_route_requests=12 "
            "ffa_spawn_camp_avoidance_invalid_skips=0 "
            "last_ffa_spawn_camp_avoidance_client=2 "
            "last_ffa_spawn_camp_avoidance_source_client=1 "
            "last_ffa_spawn_camp_avoidance_source_entity=1 "
            "last_ffa_spawn_camp_avoidance_source_distance_sq=16384 "
            "last_ffa_spawn_camp_avoidance_policy_avoid=1 "
            "last_ffa_spawn_camp_avoidance_goal_distance_sq=802816 "
            "last_ffa_spawn_camp_avoidance_reason=nearby_source",
            "q3a_bot_match_readiness_status ffa_pass=1 tdm_pass=0 "
            "deathmatch=1 team_mode=0 gametype=1 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=0 blue=0",
            "q3a_bot_objective_status "
            "team_objective_match_policy_ffa=16",
        ))
        ffa_spawn_camp_marker_metrics = harness.parse_marker_metrics(
            ffa_spawn_camp_text,
            {check.marker for check in ffa_spawn_camp_avoidance.marker_checks},
        )
        ffa_spawn_camp_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ffa_spawn_camp_marker_metrics)
                for check in ffa_spawn_camp_avoidance.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ffa_spawn_camp_failed, [])

        team_role_route_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=32 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=3 team_role_route=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 last_timed_route_goal_kind=5 "
            "team_role_route_requests=16 "
            "team_role_route_policy_selections=16 "
            "team_role_route_activations=4 "
            "team_role_route_route_requests=12 "
            "last_team_role_route_client=2 "
            "last_team_role_route_mode=2 "
            "last_team_role_route_mode_name=team_deathmatch "
            "last_team_role_route_role=5 "
            "last_team_role_route_role_name=midfielder "
            "last_team_role_route_lane=3 "
            "last_team_role_route_lane_name=midfield "
            "last_team_role_route_priority=780 "
            "last_team_role_route_goal_distance_sq=802816",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=1 "
            "deathmatch=1 team_mode=1 gametype=3 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_objective_status "
            "team_objective_match_policy_tdm=16",
        ))
        team_role_route_marker_metrics = harness.parse_marker_metrics(
            team_role_route_text,
            {check.marker for check in team_role_route.marker_checks},
        )
        team_role_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, team_role_route_marker_metrics)
                for check in team_role_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(team_role_route_failed, [])

        team_role_combat_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=43 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=3 team_role_combat=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 team_role_combat_requests=16 "
            "team_role_combat_policy_selections=16 "
            "team_role_combat_target_selections=12 "
            "team_role_combat_attack_decisions=12 "
            "team_role_combat_decision_overrides=12 "
            "team_role_combat_invalid_skips=0 "
            "last_team_role_combat_client=2 "
            "last_team_role_combat_mode=2 "
            "last_team_role_combat_mode_name=team_deathmatch "
            "last_team_role_combat_role=5 "
            "last_team_role_combat_role_name=midfielder "
            "last_team_role_combat_lane=3 "
            "last_team_role_combat_lane_name=midfield "
            "last_team_role_combat_priority=795 "
            "last_team_role_combat_target_client=1 "
            "last_team_role_combat_target_entity=2 "
            "last_team_role_combat_target_distance_sq=16384 "
            "last_team_role_combat_target_visible=1 "
            "last_team_role_combat_target_shootable=1 "
            "last_team_role_combat_reason=team_role_combat_engage",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=1 "
            "deathmatch=1 team_mode=1 gametype=3 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_action_status action_applied_attack_buttons=12",
            "q3a_bot_objective_status "
            "team_objective_match_policy_tdm=16",
        ))
        team_role_combat_marker_metrics = harness.parse_marker_metrics(
            team_role_combat_text,
            {check.marker for check in team_role_combat.marker_checks},
        )
        team_role_combat_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, team_role_combat_marker_metrics)
                for check in team_role_combat.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(team_role_combat_failed, [])

        team_role_combat_avoidance_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=44 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=3 team_fire_avoidance=1 team_role_combat=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 team_role_combat_requests=16 "
            "team_role_combat_policy_selections=16 "
            "team_role_combat_target_selections=12 "
            "team_role_combat_attack_decisions=12 "
            "team_role_combat_decision_overrides=12 "
            "team_role_combat_invalid_skips=0 "
            "last_team_role_combat_client=2 "
            "last_team_role_combat_mode=2 "
            "last_team_role_combat_mode_name=team_deathmatch "
            "last_team_role_combat_role=5 "
            "last_team_role_combat_role_name=midfielder "
            "last_team_role_combat_lane=3 "
            "last_team_role_combat_lane_name=midfield "
            "last_team_role_combat_priority=795 "
            "last_team_role_combat_target_client=1 "
            "last_team_role_combat_target_entity=2 "
            "last_team_role_combat_target_distance_sq=16384 "
            "last_team_role_combat_target_visible=1 "
            "last_team_role_combat_target_shootable=1 "
            "last_team_role_combat_reason=team_role_combat_engage "
            "team_fire_avoidance_evaluations=12 "
            "team_fire_avoidance_blocks=12 "
            "team_fire_avoidance_line_blocks=12 "
            "last_team_fire_avoidance_client=2 "
            "last_team_fire_avoidance_target_client=1 "
            "last_team_fire_avoidance_friendly_line=1 "
            "last_team_fire_avoidance_target_allowed=1 "
            "last_team_fire_avoidance_blocked=1 "
            "last_team_fire_avoidance_reason=friendly_line_damage",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=1 "
            "deathmatch=1 team_mode=1 gametype=3 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_action_status action_applied_attack_buttons=3",
            "q3a_bot_objective_status "
            "team_objective_match_policy_tdm=16 "
            "team_objective_friendly_fire_policy_evaluations=12 "
            "team_objective_friendly_fire_avoidance=12",
        ))
        team_role_combat_avoidance_marker_metrics = harness.parse_marker_metrics(
            team_role_combat_avoidance_text,
            {check.marker for check in team_role_combat_avoidance.marker_checks},
        )
        team_role_combat_avoidance_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, team_role_combat_avoidance_marker_metrics)
                for check in team_role_combat_avoidance.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(team_role_combat_avoidance_failed, [])

        ctf_role_route = harness.scenario_map()["ctf_role_route"]
        ctf_role_route_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=35 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=5 ctf_role_route=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 last_timed_route_goal_kind=6 "
            "ctf_role_route_requests=16 "
            "ctf_role_route_policy_selections=16 "
            "ctf_role_route_activations=4 "
            "ctf_role_route_route_requests=12 "
            "last_ctf_role_route_client=2 "
            "last_ctf_role_route_mode=3 "
            "last_ctf_role_route_mode_name=capture_the_flag "
            "last_ctf_role_route_role=5 "
            "last_ctf_role_route_role_name=midfielder "
            "last_ctf_role_route_lane=3 "
            "last_ctf_role_route_lane_name=midfield "
            "last_ctf_role_route_priority=760 "
            "last_ctf_role_route_goal_distance_sq=802816",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=1 team_mode=1 gametype=5 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_objective_status "
            "team_objective_match_policy_ctf=16",
        ))
        ctf_role_route_marker_metrics = harness.parse_marker_metrics(
            ctf_role_route_text,
            {check.marker for check in ctf_role_route.marker_checks},
        )
        ctf_role_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ctf_role_route_marker_metrics)
                for check in ctf_role_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ctf_role_route_failed, [])

        ctf_role_combat = harness.scenario_map()["ctf_role_combat"]
        ctf_role_combat_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=36 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=5 ctf_role_combat=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 ctf_role_combat_requests=16 "
            "ctf_role_combat_policy_selections=16 "
            "ctf_role_combat_target_selections=12 "
            "ctf_role_combat_attack_decisions=12 "
            "ctf_role_combat_decision_overrides=12 "
            "ctf_role_combat_invalid_skips=0 "
            "last_ctf_role_combat_client=2 "
            "last_ctf_role_combat_mode=3 "
            "last_ctf_role_combat_mode_name=capture_the_flag "
            "last_ctf_role_combat_role=5 "
            "last_ctf_role_combat_role_name=midfielder "
            "last_ctf_role_combat_lane=3 "
            "last_ctf_role_combat_lane_name=midfield "
            "last_ctf_role_combat_priority=800 "
            "last_ctf_role_combat_target_client=1 "
            "last_ctf_role_combat_target_entity=2 "
            "last_ctf_role_combat_target_distance_sq=16384 "
            "last_ctf_role_combat_target_visible=1 "
            "last_ctf_role_combat_target_shootable=1 "
            "last_ctf_role_combat_reason=ctf_role_combat_engage",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=1 team_mode=1 gametype=5 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_action_status action_applied_attack_buttons=12",
            "q3a_bot_objective_status "
            "team_objective_match_policy_ctf=16",
        ))
        ctf_role_combat_marker_metrics = harness.parse_marker_metrics(
            ctf_role_combat_text,
            {check.marker for check in ctf_role_combat.marker_checks},
        )
        ctf_role_combat_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ctf_role_combat_marker_metrics)
                for check in ctf_role_combat.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ctf_role_combat_failed, [])

        ctf_dropped_flag_route = harness.scenario_map()["ctf_dropped_flag_route"]
        ctf_dropped_flag_route_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=37 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=5 ctf_dropped_flag_route=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 ctf_dropped_flag_route_requests=16 "
            "ctf_dropped_flag_route_assignments=16 "
            "ctf_dropped_flag_route_route_requests=16 "
            "ctf_dropped_flag_route_route_commands=16 "
            "ctf_dropped_flag_route_invalid_skips=0 "
            "last_ctf_dropped_flag_route_client=2 "
            "last_ctf_dropped_flag_route_role=1 "
            "last_ctf_dropped_flag_route_role_name=attacker "
            "last_ctf_dropped_flag_route_lane=5 "
            "last_ctf_dropped_flag_route_lane_name=dropped_flag_response "
            "last_ctf_dropped_flag_route_type=1 "
            "last_ctf_dropped_flag_route_type_name=enemy_flag_pickup "
            "last_ctf_dropped_flag_route_source=2 "
            "last_ctf_dropped_flag_route_source_name=dropped_flag_entity "
            "last_ctf_dropped_flag_route_entity=37 "
            "last_ctf_dropped_flag_route_item=10 "
            "last_ctf_dropped_flag_route_priority=2200 "
            "last_ctf_dropped_flag_route_goal_distance_sq=50176",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=1 team_mode=1 gametype=5 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_objective_detail_status "
            "team_objective_role_policy_dropped_flag_responses=16 "
            "last_team_objective_lane=5 "
            "last_team_objective_target_source=2",
        ))
        ctf_dropped_flag_route_marker_metrics = harness.parse_marker_metrics(
            ctf_dropped_flag_route_text,
            {check.marker for check in ctf_dropped_flag_route.marker_checks},
        )
        ctf_dropped_flag_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ctf_dropped_flag_route_marker_metrics)
                for check in ctf_dropped_flag_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ctf_dropped_flag_route_failed, [])

        ctf_carrier_support_route = harness.scenario_map()["ctf_carrier_support_route"]
        ctf_carrier_support_route_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=38 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=5 ctf_carrier_support_route=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 ctf_carrier_support_route_requests=16 "
            "ctf_carrier_support_route_assignments=16 "
            "ctf_carrier_support_route_route_requests=16 "
            "ctf_carrier_support_route_route_commands=16 "
            "ctf_carrier_support_route_invalid_skips=0 "
            "last_ctf_carrier_support_route_client=2 "
            "last_ctf_carrier_support_route_role=4 "
            "last_ctf_carrier_support_route_role_name=support "
            "last_ctf_carrier_support_route_lane=4 "
            "last_ctf_carrier_support_route_lane_name=carrier_support "
            "last_ctf_carrier_support_route_type=1 "
            "last_ctf_carrier_support_route_type_name=enemy_flag_pickup "
            "last_ctf_carrier_support_route_source=3 "
            "last_ctf_carrier_support_route_source_name=flag_carrier "
            "last_ctf_carrier_support_route_entity=3 "
            "last_ctf_carrier_support_route_carrier_client=2 "
            "last_ctf_carrier_support_route_item=10 "
            "last_ctf_carrier_support_route_priority=930 "
            "last_ctf_carrier_support_route_goal_distance_sq=36864",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=1 team_mode=1 gametype=5 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_objective_detail_status "
            "team_objective_role_policy_carrier_support_selections=16 "
            "last_team_objective_lane=4 "
            "last_team_objective_target_source=3",
        ))
        ctf_carrier_support_route_marker_metrics = harness.parse_marker_metrics(
            ctf_carrier_support_route_text,
            {check.marker for check in ctf_carrier_support_route.marker_checks},
        )
        ctf_carrier_support_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ctf_carrier_support_route_marker_metrics)
                for check in ctf_carrier_support_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ctf_carrier_support_route_failed, [])

        ctf_base_return_route = harness.scenario_map()["ctf_base_return_route"]
        ctf_base_return_route_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=39 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=5 ctf_base_return_route=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 ctf_base_return_route_requests=16 "
            "ctf_base_return_route_assignments=16 "
            "ctf_base_return_route_route_requests=16 "
            "ctf_base_return_route_route_commands=16 "
            "ctf_base_return_route_invalid_skips=0 "
            "last_ctf_base_return_route_client=2 "
            "last_ctf_base_return_route_role=3 "
            "last_ctf_base_return_route_role_name=returner "
            "last_ctf_base_return_route_lane=6 "
            "last_ctf_base_return_route_lane_name=own_base_return "
            "last_ctf_base_return_route_type=2 "
            "last_ctf_base_return_route_type_name=own_flag_return "
            "last_ctf_base_return_route_source=3 "
            "last_ctf_base_return_route_source_name=flag_carrier "
            "last_ctf_base_return_route_entity=3 "
            "last_ctf_base_return_route_carrier_client=2 "
            "last_ctf_base_return_route_item=9 "
            "last_ctf_base_return_route_priority=1120 "
            "last_ctf_base_return_route_goal_distance_sq=36864",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=1 team_mode=1 gametype=5 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_objective_detail_status "
            "team_objective_role_policy_own_base_return_selections=16 "
            "last_team_objective_lane=0 "
            "last_team_objective_target_source=2",
        ))
        ctf_base_return_route_marker_metrics = harness.parse_marker_metrics(
            ctf_base_return_route_text,
            {check.marker for check in ctf_base_return_route.marker_checks},
        )
        ctf_base_return_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ctf_base_return_route_marker_metrics)
                for check in ctf_base_return_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ctf_base_return_route_failed, [])

        ctf_objective_route = harness.scenario_map()["ctf_objective_route"]
        ctf_objective_route_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=40 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=5 ctf_objective_route=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 ctf_objective_route_requests=16 "
            "ctf_objective_route_assignments=16 "
            "ctf_objective_route_base_return_candidates=16 "
            "ctf_objective_route_carrier_support_candidates=16 "
            "ctf_objective_route_dropped_flag_candidates=16 "
            "ctf_objective_route_base_return_selections=12 "
            "ctf_objective_route_carrier_support_selections=2 "
            "ctf_objective_route_dropped_flag_selections=2 "
            "ctf_objective_route_carrier_support_deferrals=0 "
            "ctf_objective_route_dropped_flag_deferrals=14 "
            "ctf_objective_route_route_requests=16 "
            "ctf_objective_route_route_commands=16 "
            "ctf_objective_route_invalid_skips=0 "
            "last_ctf_objective_route_client=2 "
            "last_ctf_objective_route_selection=1 "
            "last_ctf_objective_route_selection_name=base_return "
            "last_ctf_objective_route_role=3 "
            "last_ctf_objective_route_role_name=returner "
            "last_ctf_objective_route_lane=6 "
            "last_ctf_objective_route_lane_name=own_base_return "
            "last_ctf_objective_route_type=2 "
            "last_ctf_objective_route_type_name=own_flag_return "
            "last_ctf_objective_route_source=3 "
            "last_ctf_objective_route_source_name=flag_carrier "
            "last_ctf_objective_route_entity=3 "
            "last_ctf_objective_route_carrier_client=2 "
            "last_ctf_objective_route_item=9 "
            "last_ctf_objective_route_priority=1120 "
            "last_ctf_objective_route_goal_distance_sq=36864",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=1 team_mode=1 gametype=5 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
        ))
        ctf_objective_route_marker_metrics = harness.parse_marker_metrics(
            ctf_objective_route_text,
            {check.marker for check in ctf_objective_route.marker_checks},
        )
        ctf_objective_route_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, ctf_objective_route_marker_metrics)
                for check in ctf_objective_route.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ctf_objective_route_failed, [])

        ctf_objective_route_precedence = harness.scenario_map()[
            "ctf_objective_route_precedence"
        ]
        ctf_objective_route_precedence_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=41 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=5 ctf_role_route=1 ctf_objective_route=1 "
            "ctf_objective_route_precedence=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 ctf_role_route_requests=16 "
            "ctf_role_route_policy_selections=16 "
            "ctf_role_route_activations=0 "
            "ctf_role_route_objective_deferrals=16 "
            "ctf_role_route_route_requests=0 "
            "ctf_role_route_invalid_skips=0 "
            "ctf_objective_route_requests=16 "
            "ctf_objective_route_assignments=16 "
            "ctf_objective_route_route_requests=16 "
            "ctf_objective_route_route_commands=16 "
            "ctf_objective_route_invalid_skips=0 "
            "last_ctf_objective_route_selection=1 "
            "last_ctf_objective_route_goal_distance_sq=36864",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=1 team_mode=1 gametype=5 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
        ))
        ctf_objective_route_precedence_marker_metrics = (
            harness.parse_marker_metrics(
                ctf_objective_route_precedence_text,
                {check.marker for check in ctf_objective_route_precedence.marker_checks},
            )
        )
        ctf_objective_route_precedence_failed = [
            result
            for result in (
                harness.evaluate_marker_check(
                    check,
                    ctf_objective_route_precedence_marker_metrics,
                )
                for check in ctf_objective_route_precedence.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(ctf_objective_route_precedence_failed, [])

        team_item_roles_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=33 combat=0 "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=3 team_item_roles=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 item_goal_assignments=4",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=1 "
            "deathmatch=1 team_mode=1 gametype=3 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_objective_status "
            "team_objective_match_policy_tdm=16 "
            "team_objective_item_role_policy_evaluations=64 "
            "team_objective_item_role_policy_selections=64",
            "q3a_bot_nav_policy_status "
            "team_item_role_evaluations=64 "
            "team_item_role_selections=64 "
            "team_item_role_score_boosts=64 "
            "team_item_role_selected_goals=4 "
            "last_team_item_role_client=2 "
            "last_team_item_role_mode=2 "
            "last_team_item_role_mode_name=team_deathmatch "
            "last_team_item_role_role=5 "
            "last_team_item_role_role_name=midfielder "
            "last_team_item_role_lane=3 "
            "last_team_item_role_lane_name=midfield "
            "last_team_item_role_category=4 "
            "last_team_item_role_category_name=weapon "
            "last_team_item_role_item_role=2 "
            "last_team_item_role_item_role_name=weapon_control "
            "last_team_item_role_priority=175 "
            "last_team_item_role_score_boost=175 "
            "last_team_item_role_entity=32 "
            "last_team_item_role_item=53 "
            "last_team_item_role_score=860",
        ))
        team_item_roles_marker_metrics = harness.parse_marker_metrics(
            team_item_roles_text,
            {check.marker for check in team_item_roles.marker_checks},
        )
        team_item_roles_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, team_item_roles_marker_metrics)
                for check in team_item_roles.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(team_item_roles_failed, [])

        team_fire_avoidance = harness.scenario_map()["team_fire_avoidance"]
        team_fire_avoidance_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=34 combat=engage_enemy "
            "weapon_switch=0 item_focus=0 team_objective=0 target=4 "
            "gametype=3 team_fire_avoidance=1",
            "q3a_bot_frame_command_status pass=1 route_commands=16 "
            "route_failures=0 team_fire_avoidance_evaluations=4 "
            "team_fire_avoidance_blocks=2 "
            "team_fire_avoidance_line_blocks=2 "
            "last_team_fire_avoidance_client=0 "
            "last_team_fire_avoidance_target_client=1 "
            "last_team_fire_avoidance_friendly_line=1 "
            "last_team_fire_avoidance_target_allowed=1 "
            "last_team_fire_avoidance_blocked=1 "
            "last_team_fire_avoidance_reason=friendly_line_damage",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=1 "
            "deathmatch=1 team_mode=1 gametype=3 bots=4 playing=4 "
            "spectators=0 queued=0 free=0 red=2 blue=2",
            "q3a_bot_action_status action_attack_decisions=4 "
            "action_applied_attack_buttons=2",
            "q3a_bot_objective_status "
            "team_objective_match_policy_tdm=16 "
            "team_objective_friendly_fire_policy_evaluations=8 "
            "team_objective_friendly_fire_avoidance=2",
        ))
        team_fire_avoidance_marker_metrics = harness.parse_marker_metrics(
            team_fire_avoidance_text,
            {check.marker for check in team_fire_avoidance.marker_checks},
        )
        team_fire_avoidance_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, team_fire_avoidance_marker_metrics)
                for check in team_fire_avoidance.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(team_fire_avoidance_failed, [])

        interaction_retry_text = "\n".join((
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_coop_readiness_status pass=1 coop=1 bots=1 "
            "playing=1 spectators=0 queued=0",
            "q3a_bot_match_readiness_status ffa_pass=0 tdm_pass=0 "
            "deathmatch=0 team_mode=0 gametype=0 bots=1 playing=1 "
            "spectators=0 queued=0 free=1 red=0 blue=0",
            "q3a_bot_nav_interaction_context_status "
            "interaction_world_entities=3 interaction_world_triggers=2",
            "q3a_bot_coop_command_status "
            "coop_interaction_retry_requests=4 "
            "coop_interaction_retry_activations=4 "
            "coop_interaction_retry_commands=3 "
            "last_coop_interaction_retry_action=3 "
            "last_coop_interaction_retry_kind=7",
        ))
        interaction_retry_marker_metrics = harness.parse_marker_metrics(
            interaction_retry_text,
            {check.marker for check in interaction_retry.marker_checks},
        )
        interaction_retry_failed = [
            result
            for result in (
                harness.evaluate_marker_check(check, interaction_retry_marker_metrics)
                for check in interaction_retry.marker_checks
            )
            if not result["passed"]
        ]
        self.assertEqual(interaction_retry_failed, [])

    def test_pending_gap_report_identifies_missing_rows_and_metrics(self) -> None:
        report = harness.pending_gap_report(
            [pending_promotion_scenario("engage_enemy")],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
        )

        self.assertEqual(report["summary"]["total"], 1)
        self.assertEqual(report["summary"]["ready"], 0)
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(report["summary"]["missing_rows"], 1)
        self.assertEqual(report["summary"]["overall"], "blocked")

        engage_enemy = report["scenarios"][0]
        self.assertEqual(engage_enemy["status"], "blocked")
        self.assertIn("fixture report has no scenario row named engage_enemy", engage_enemy["blockers"])
        self.assertIn("combat_damage_events", engage_enemy["missing_metrics"])
        self.assertIn(
            {"source": harness.SCENARIO_BEGIN_MARKER, "metric": "mode"},
            engage_enemy["missing_marker_metrics"],
        )

        markdown = harness.build_markdown_report(report)
        self.assertIn("# Bot Scenario Pending Gap Report", markdown)
        self.assertIn("combat_damage_events", markdown)
        self.assertIn("Missing Marker Metrics", markdown)

    def test_pending_gap_report_marks_ready_when_source_metrics_exist(self) -> None:
        scenario = pending_promotion_scenario("engage_enemy")
        metrics = passing_promotion_metrics(scenario)
        metrics["route_failures"] = 0
        fixture = {
            "scenarios": [
                {
                    "name": "engage_enemy",
                    "status": "passed",
                    "smoke_mode": 20,
                    "metrics": metrics,
                    "markers": {
                        harness.SCENARIO_BEGIN_MARKER: [
                            {
                                "mode": 20,
                                "combat": "engage_enemy",
                                "weapon_switch": 0,
                                "item_focus": 0,
                                "team_objective": 0,
                                "target": 2,
                                "gametype": 0,
                            },
                        ],
                    },
                },
            ],
        }

        report = harness.pending_gap_report([scenario], fixture, pathlib.Path("latest_report.json"))
        engage_enemy = report["scenarios"][0]

        self.assertEqual(report["summary"]["ready"], 1)
        self.assertEqual(report["summary"]["blocked"], 0)
        self.assertEqual(report["summary"]["overall"], "ready")
        self.assertEqual(engage_enemy["status"], "ready")
        self.assertEqual(engage_enemy["missing_metrics"], [])
        self.assertEqual(engage_enemy["missing_marker_metrics"], [])
        self.assertEqual(engage_enemy["blockers"], [])

    def test_pending_gap_report_blocks_when_promotion_checks_fail(self) -> None:
        scenario = pending_promotion_scenario("engage_enemy")
        metrics = passing_promotion_metrics(scenario)
        metrics["route_failures"] = 1
        metrics["last_combat_damage"] = 0
        fixture = {
            "scenarios": [
                {
                    "name": "engage_enemy",
                    "status": "passed",
                    "smoke_mode": 20,
                    "metrics": metrics,
                    "markers": {
                        harness.SCENARIO_BEGIN_MARKER: [
                            {
                                "mode": 20,
                                "combat": "engage_enemy",
                                "weapon_switch": 0,
                                "item_focus": 0,
                                "team_objective": 0,
                                "target": 2,
                                "gametype": 0,
                            },
                        ],
                    },
                },
            ],
        }

        report = harness.pending_gap_report([scenario], fixture, pathlib.Path("latest_report.json"))
        engage_enemy = report["scenarios"][0]
        failed_metrics = {
            check["metric"]: check
            for check in engage_enemy["failed_metric_checks"]
        }

        self.assertEqual(report["summary"]["ready"], 0)
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(report["summary"]["missing_status_metrics"], 0)
        self.assertEqual(report["summary"]["failed_metric_checks"], 2)
        self.assertEqual(engage_enemy["status"], "blocked")
        self.assertEqual(engage_enemy["missing_metrics"], [])
        self.assertEqual(failed_metrics["last_combat_damage"]["actual"], 0)
        self.assertEqual(failed_metrics["route_failures"]["actual"], 1)
        self.assertTrue(
            any(
                "last_combat_damage ge 1 failed, actual=0" in blocker
                for blocker in engage_enemy["blockers"]
            )
        )
        self.assertTrue(
            any(
                "route_failures eq 0 failed, actual=1" in blocker
                for blocker in engage_enemy["blockers"]
            )
        )

    def test_pending_gap_report_evaluates_marker_promotion_checks(self) -> None:
        scenario = harness.Scenario(
            name="marker_pending",
            title="Marker pending",
            smoke_mode=None,
            description="Synthetic marker-backed pending scenario.",
            task_ids=("DV-03-T05",),
            budget_seconds=0,
            planned_smoke_mode=99,
            promotion_marker_checks=(
                harness.MarkerMetricCheck(
                    "q3a_bot_marker_smoke=complete",
                    "events",
                    "ge",
                    1,
                    "completion marker must report at least one event",
                ),
            ),
        )
        fixture = {
            "scenarios": [
                {
                    "name": "marker_pending",
                    "status": "passed",
                    "smoke_mode": 99,
                    "metrics": {},
                    "markers": {
                        "q3a_bot_marker_smoke=complete": [
                            {"events": 0},
                        ],
                    },
                },
            ],
        }

        report = harness.pending_gap_report([scenario], fixture, pathlib.Path("latest_report.json"))
        marker_pending = report["scenarios"][0]

        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(report["summary"]["missing_marker_metrics"], 0)
        self.assertEqual(report["summary"]["failed_marker_checks"], 1)
        self.assertEqual(marker_pending["missing_marker_metrics"], [])
        self.assertEqual(marker_pending["failed_marker_checks"][0]["metric"], "events")
        self.assertTrue(
            any(
                "q3a_bot_marker_smoke=complete::events ge 1 failed, actual=0" in blocker
                for blocker in marker_pending["blockers"]
            )
        )

    def test_raw_reserved_mode_diagnostic_parsing(self) -> None:
        text = "\n".join((
            "noise before the reserved mode",
            f"{harness.SCENARIO_BEGIN_MARKER} mode=20 combat=engage_enemy "
            "weapon_switch=0 item_focus=0 team_objective=0 target=2 gametype=0",
            "q3a_bot_frame_command_status pass=0 route_failures=0 "
            "combat_enemy_acquisitions=1 combat_enemy_visible=1 combat_enemy_shootable=1 "
            "last_combat_enemy_client=3",
            "q3a_bot_blackboard_status blackboard_updates=2 combat_enemy_visible=1",
            "q3a_bot_action_status action_attack_decisions=1 "
            "action_applied_attack_buttons=0 combat_fire_decisions=1 "
            "combat_damage_events=0 last_combat_damage=0 action_last_intent_name=attack",
            "q3a_bot_source_counter_status q3a_route_build_attempts=3 "
            "bsp_trace_calls=2",
        ))

        diagnostics = harness.parse_raw_reserved_mode_diagnostics(text, "mode20.stdout.txt")

        self.assertEqual(len(diagnostics), 1)
        diagnostic = diagnostics[0]
        self.assertEqual(diagnostic["source_path"], "mode20.stdout.txt")
        self.assertEqual(diagnostic["mode"], 20)
        self.assertEqual(diagnostic["scenario"], "engage_enemy")
        self.assertEqual(diagnostic["status"], "failed")
        self.assertEqual(diagnostic["marker_counts"][harness.SCENARIO_BEGIN_MARKER], 1)
        self.assertEqual(diagnostic["marker_counts"][harness.STATUS_MARKER], 1)
        self.assertEqual(diagnostic["marker_counts"][harness.ACTION_STATUS_MARKER], 1)
        self.assertEqual(diagnostic["marker_counts"][harness.SOURCE_STATUS_MARKER], 1)
        self.assertEqual(diagnostic["metrics"]["combat_enemy_acquisitions"], 1)
        self.assertEqual(diagnostic["metrics"]["action_attack_decisions"], 1)
        self.assertEqual(diagnostic["metrics"]["action_applied_attack_buttons"], 0)
        self.assertEqual(diagnostic["metrics"]["action_last_intent_name"], "attack")
        self.assertEqual(diagnostic["metrics"]["q3a_route_build_attempts"], 3)
        self.assertIn(
            harness.ACTION_STATUS_MARKER,
            diagnostic["metric_sources"]["action_applied_attack_buttons"],
        )
        self.assertIn(
            harness.SOURCE_STATUS_MARKER,
            diagnostic["metric_sources"]["q3a_route_build_attempts"],
        )

    def test_raw_reserved_mode_latest_metric_event_wins_across_markers(self) -> None:
        text = "\n".join((
            RESERVED_MODE_BEGIN_LINES[20],
            "q3a_bot_action_status action_applied_attack_buttons=1 "
            "combat_damage_events=1",
            "q3a_bot_frame_command_status pass=1 route_failures=0 "
            "action_applied_attack_buttons=0",
        ))

        diagnostics = harness.parse_raw_reserved_mode_diagnostics(
            text,
            pathlib.Path(".tmp/bot_scenarios/raw_modes/mode20.stdout.txt"),
        )

        self.assertEqual(len(diagnostics), 1)
        diagnostic = diagnostics[0]
        self.assertEqual(diagnostic["metrics"]["action_applied_attack_buttons"], 0)
        self.assertEqual(
            diagnostic["metric_sources"]["action_applied_attack_buttons"],
            [harness.ACTION_STATUS_MARKER, harness.STATUS_MARKER],
        )
        self.assertEqual(
            diagnostic["metric_latest_sources"]["action_applied_attack_buttons"],
            harness.STATUS_MARKER,
        )
        self.assertEqual(diagnostic["metric_lines"]["action_applied_attack_buttons"], 3)

    def test_optional_field_discovery_parses_new_status_families(self) -> None:
        text = "\n".join((
            "q3a_bot_frame_command_status pass=1 route_failures=0 expected_min_commands=1 "
            "route_target_stabilization_checks=3 route_target_stabilizations=1 "
            "route_target_stabilization_skips=2 last_route_target_original_distance_sq=16 "
            "last_route_target_stable_distance_sq=128 last_route_target_stable_point_index=2 "
            "ffa_roam_route_requests=4 ffa_roam_route_policy_selections=4 "
            "ffa_roam_route_activations=2 ffa_roam_route_route_requests=3 "
            "last_ffa_roam_route_client=1 last_ffa_roam_route_mode=1 "
            "last_ffa_roam_route_mode_name=free_for_all "
            "last_ffa_roam_route_role=1 last_ffa_roam_route_role_name=attacker "
            "last_ffa_roam_route_lane=3 last_ffa_roam_route_lane_name=midfield "
            "last_ffa_roam_route_priority=660 "
            "last_ffa_roam_route_goal_distance_sq=802816 "
            "ffa_spawn_camp_avoidance_requests=4 "
            "ffa_spawn_camp_avoidance_policy_selections=4 "
            "ffa_spawn_camp_avoidance_source_selections=2 "
            "ffa_spawn_camp_avoidance_activations=2 "
            "ffa_spawn_camp_avoidance_route_requests=3 "
            "ffa_spawn_camp_avoidance_invalid_skips=0 "
            "last_ffa_spawn_camp_avoidance_client=1 "
            "last_ffa_spawn_camp_avoidance_source_client=2 "
            "last_ffa_spawn_camp_avoidance_source_entity=2 "
            "last_ffa_spawn_camp_avoidance_source_distance_sq=16384 "
            "last_ffa_spawn_camp_avoidance_policy_avoid=1 "
            "last_ffa_spawn_camp_avoidance_goal_distance_sq=802816 "
            "last_ffa_spawn_camp_avoidance_reason=nearby_source "
            "team_role_route_requests=4 team_role_route_policy_selections=4 "
            "team_role_route_activations=2 team_role_route_route_requests=3 "
            "last_team_role_route_client=1 last_team_role_route_mode=2 "
            "last_team_role_route_mode_name=team_deathmatch "
            "last_team_role_route_role=5 last_team_role_route_role_name=midfielder "
            "last_team_role_route_lane=3 last_team_role_route_lane_name=midfield "
            "last_team_role_route_priority=780 "
            "last_team_role_route_goal_distance_sq=802816 "
            "team_role_combat_requests=4 team_role_combat_policy_selections=4 "
            "team_role_combat_target_selections=3 "
            "team_role_combat_attack_decisions=3 "
            "team_role_combat_decision_overrides=3 "
            "team_role_combat_invalid_skips=0 "
            "last_team_role_combat_client=1 last_team_role_combat_mode=2 "
            "last_team_role_combat_mode_name=team_deathmatch "
            "last_team_role_combat_role=5 last_team_role_combat_role_name=midfielder "
            "last_team_role_combat_lane=3 last_team_role_combat_lane_name=midfield "
            "last_team_role_combat_priority=795 "
            "last_team_role_combat_target_client=2 "
            "last_team_role_combat_target_entity=3 "
            "last_team_role_combat_target_distance_sq=16384 "
            "last_team_role_combat_target_visible=1 "
            "last_team_role_combat_target_shootable=1 "
            "last_team_role_combat_reason=team_role_combat_engage "
            "ctf_role_route_requests=4 ctf_role_route_policy_selections=4 "
            "ctf_role_route_activations=2 "
            "ctf_role_route_objective_deferrals=1 "
            "ctf_role_route_route_requests=3 "
            "last_ctf_role_route_client=1 last_ctf_role_route_mode=3 "
            "last_ctf_role_route_mode_name=capture_the_flag "
            "last_ctf_role_route_role=5 last_ctf_role_route_role_name=midfielder "
            "last_ctf_role_route_lane=3 last_ctf_role_route_lane_name=midfield "
            "last_ctf_role_route_priority=760 "
            "last_ctf_role_route_goal_distance_sq=802816 "
            "ctf_role_combat_requests=4 ctf_role_combat_policy_selections=4 "
            "ctf_role_combat_target_selections=3 "
            "ctf_role_combat_attack_decisions=3 "
            "ctf_role_combat_decision_overrides=3 "
            "ctf_role_combat_invalid_skips=0 "
            "last_ctf_role_combat_client=1 last_ctf_role_combat_mode=3 "
            "last_ctf_role_combat_mode_name=capture_the_flag "
            "last_ctf_role_combat_role=5 last_ctf_role_combat_role_name=midfielder "
            "last_ctf_role_combat_lane=3 last_ctf_role_combat_lane_name=midfield "
            "last_ctf_role_combat_priority=800 "
            "last_ctf_role_combat_target_client=2 "
            "last_ctf_role_combat_target_entity=3 "
            "last_ctf_role_combat_target_distance_sq=16384 "
            "last_ctf_role_combat_target_visible=1 "
            "last_ctf_role_combat_target_shootable=1 "
            "last_ctf_role_combat_reason=ctf_role_combat_engage "
            "ctf_dropped_flag_route_requests=4 "
            "ctf_dropped_flag_route_assignments=4 "
            "ctf_dropped_flag_route_route_requests=4 "
            "ctf_dropped_flag_route_route_commands=4 "
            "ctf_dropped_flag_route_invalid_skips=0 "
            "last_ctf_dropped_flag_route_client=1 "
            "last_ctf_dropped_flag_route_role=1 "
            "last_ctf_dropped_flag_route_role_name=attacker "
            "last_ctf_dropped_flag_route_lane=5 "
            "last_ctf_dropped_flag_route_lane_name=dropped_flag_response "
            "last_ctf_dropped_flag_route_type=1 "
            "last_ctf_dropped_flag_route_type_name=enemy_flag_pickup "
            "last_ctf_dropped_flag_route_source=2 "
            "last_ctf_dropped_flag_route_source_name=dropped_flag_entity "
            "last_ctf_dropped_flag_route_entity=37 "
            "last_ctf_dropped_flag_route_item=10 "
            "last_ctf_dropped_flag_route_priority=2200 "
            "last_ctf_dropped_flag_route_goal_distance_sq=50176 "
            "ctf_carrier_support_route_requests=4 "
            "ctf_carrier_support_route_assignments=4 "
            "ctf_carrier_support_route_route_requests=4 "
            "ctf_carrier_support_route_route_commands=4 "
            "ctf_carrier_support_route_invalid_skips=0 "
            "last_ctf_carrier_support_route_client=1 "
            "last_ctf_carrier_support_route_role=4 "
            "last_ctf_carrier_support_route_role_name=support "
            "last_ctf_carrier_support_route_lane=4 "
            "last_ctf_carrier_support_route_lane_name=carrier_support "
            "last_ctf_carrier_support_route_type=1 "
            "last_ctf_carrier_support_route_type_name=enemy_flag_pickup "
            "last_ctf_carrier_support_route_source=3 "
            "last_ctf_carrier_support_route_source_name=flag_carrier "
            "last_ctf_carrier_support_route_entity=3 "
            "last_ctf_carrier_support_route_carrier_client=2 "
            "last_ctf_carrier_support_route_item=10 "
            "last_ctf_carrier_support_route_priority=930 "
            "last_ctf_carrier_support_route_goal_distance_sq=36864 "
            "ctf_base_return_route_requests=4 "
            "ctf_base_return_route_assignments=4 "
            "ctf_base_return_route_route_requests=4 "
            "ctf_base_return_route_route_commands=4 "
            "ctf_base_return_route_invalid_skips=0 "
            "last_ctf_base_return_route_client=1 "
            "last_ctf_base_return_route_role=3 "
            "last_ctf_base_return_route_role_name=returner "
            "last_ctf_base_return_route_lane=6 "
            "last_ctf_base_return_route_lane_name=own_base_return "
            "last_ctf_base_return_route_type=2 "
            "last_ctf_base_return_route_type_name=own_flag_return "
            "last_ctf_base_return_route_source=3 "
            "last_ctf_base_return_route_source_name=flag_carrier "
            "last_ctf_base_return_route_entity=3 "
            "last_ctf_base_return_route_carrier_client=2 "
            "last_ctf_base_return_route_item=9 "
            "last_ctf_base_return_route_priority=1120 "
            "last_ctf_base_return_route_goal_distance_sq=36864 "
            "ctf_objective_route_requests=4 "
            "ctf_objective_route_assignments=4 "
            "ctf_objective_route_base_return_candidates=4 "
            "ctf_objective_route_carrier_support_candidates=4 "
            "ctf_objective_route_dropped_flag_candidates=4 "
            "ctf_objective_route_base_return_selections=4 "
            "ctf_objective_route_carrier_support_selections=0 "
            "ctf_objective_route_dropped_flag_selections=0 "
            "ctf_objective_route_carrier_support_deferrals=4 "
            "ctf_objective_route_dropped_flag_deferrals=4 "
            "ctf_objective_route_route_requests=4 "
            "ctf_objective_route_route_commands=4 "
            "ctf_objective_route_invalid_skips=0 "
            "last_ctf_objective_route_client=1 "
            "last_ctf_objective_route_selection=1 "
            "last_ctf_objective_route_selection_name=base_return "
            "last_ctf_objective_route_role=3 "
            "last_ctf_objective_route_role_name=returner "
            "last_ctf_objective_route_lane=6 "
            "last_ctf_objective_route_lane_name=own_base_return "
            "last_ctf_objective_route_type=2 "
            "last_ctf_objective_route_type_name=own_flag_return "
            "last_ctf_objective_route_source=3 "
            "last_ctf_objective_route_source_name=flag_carrier "
            "last_ctf_objective_route_entity=3 "
            "last_ctf_objective_route_carrier_client=2 "
            "last_ctf_objective_route_item=9 "
            "last_ctf_objective_route_priority=1120 "
            "last_ctf_objective_route_goal_distance_sq=36864 "
            "team_fire_avoidance_evaluations=4 "
            "team_fire_avoidance_blocks=2 "
            "team_fire_avoidance_line_blocks=2 "
            "last_team_fire_avoidance_client=0 "
            "last_team_fire_avoidance_target_client=1 "
            "last_team_fire_avoidance_friendly_line=1 "
            "last_team_fire_avoidance_blocked=1 "
            "last_team_fire_avoidance_reason=friendly_line_damage",
            "q3a_bot_coop_command_status "
            "coop_leader_route_activations=5 "
            "coop_leader_route_refreshes=3 "
            "coop_leader_route_spacing_sources=4 "
            "last_coop_leader_route_intent=5 "
            "last_coop_leader_route_intent_name=support_combat "
            "coop_lead_advance_requests=6 "
            "coop_lead_advance_policy_leads=6 "
            "coop_lead_advance_activations=5 "
            "coop_lead_advance_route_requests=4 "
            "coop_lead_advance_owner_deferrals=1 "
            "last_coop_lead_advance_intent=4 "
            "last_coop_lead_advance_intent_name=lead_advance "
            "coop_progress_wait_commands=2 last_coop_progress_wait_intent=2 "
            "coop_anti_block_requests=4 "
            "coop_anti_block_policy_close=3 "
            "coop_anti_block_commands=3 "
            "last_coop_anti_block_intent=5 "
            "last_coop_anti_block_intent_name=support_combat "
            "last_coop_anti_block_forward_move=-90 "
            "last_coop_anti_block_side_move=130 "
            "coop_door_elevator_requests=5 "
            "coop_door_elevator_source_activations=1 "
            "coop_door_elevator_source_commands=3 "
            "coop_door_elevator_hold_commands=2 "
            "last_coop_door_elevator_client=1 "
            "last_coop_door_elevator_source_client=0 "
            "last_coop_door_elevator_action=3 "
            "last_coop_door_elevator_kind=3 "
            "last_coop_door_elevator_entity=17 "
            "last_coop_door_elevator_intent=5 "
            "last_coop_door_elevator_intent_name=support_combat "
            "coop_interaction_retry_commands=3 "
            "last_coop_interaction_retry_action=3 "
            "last_coop_interaction_retry_kind=7",
            "q3a_bot_action_status action_command_request_builds=2 "
            "action_command_request_accepted=2 action_command_request_dispatch_attempts=2 "
            "action_command_request_submitted=1 action_command_request_deferred=1 "
            "action_weapon_command_dispatches=1 action_inventory_command_dispatches=1 "
            "action_last_command_dispatch_outcome_name=submitted "
            "combat_fire_decisions=2 action_attack_decisions=2 "
            "action_applied_attack_buttons=2 "
            "aim_policy_evaluations=4 aim_policy_fire_allowed=2 "
            "aim_policy_blocks_reaction=1 last_aim_policy_failure_name=reaction_pending "
            "item_damage_boost_candidates=1 item_damage_boost_seek_decisions=1 "
            "item_tech_candidates=2 item_ctf_objective_seek_decisions=1 "
            "item_special_utility_boosts=4 item_last_special_kind_name=tech",
            "q3a_bot_action_detail_status combat_evaluations=4 combat_withheld_fire=1 "
            "action_applied_cmds=2 action_last_intent_name=attack "
            "last_aim_policy_skill=3 last_aim_policy_reaction_delay_ms=250 "
            "projectile_lead_evaluations=1 projectile_lead_uses=1 "
            "last_projectile_lead_weapon=5 live_aim_evaluations=2 "
            "live_aim_fire_allowed=1 last_live_aim_reason=projectile_lead "
            "item_timer_evaluations=2 item_timer_allowed_uses=1 "
            "last_item_timer_reason=fuzzed item_timing_policy_evaluations=2 "
            "item_timing_policy_ready=1 item_last_timing_policy_reason_name=fuzzed_timer "
            "item_timing_consumer_evaluations=1 item_timing_consumer_ready=1 "
            "item_timing_consumer_live_pickups=0 "
            "item_last_timing_consumer_reason_name=timer_ready",
            "q3a_bot_nav_policy_status route_corner_cut_trace_checks=3 "
            "route_corner_cut_accepted=1 route_corner_cut_rejected=2 "
            "team_item_role_evaluations=5 "
            "team_item_role_selections=4 "
            "team_item_role_score_boosts=4 "
            "team_item_role_selected_goals=2 "
            "last_team_item_role_mode=2 "
            "last_team_item_role_mode_name=team_deathmatch "
            "last_team_item_role_role=5 "
            "last_team_item_role_role_name=midfielder "
            "last_team_item_role_lane=3 "
            "last_team_item_role_lane_name=midfield "
            "last_team_item_role_category=4 "
            "last_team_item_role_category_name=weapon "
            "last_team_item_role_item_role=2 "
            "last_team_item_role_item_role_name=weapon_control "
            "last_team_item_role_priority=175 "
            "last_team_item_role_score_boost=175 "
            "last_team_item_role_entity=32 "
            "last_team_item_role_item=53 "
            "last_team_item_role_score=860",
            "q3a_bot_objective_detail_status team_objective_role_policy_evaluations=2 "
            "team_objective_role_policy_lane_midfield_selections=1 "
            "last_team_objective_lane_name=midfield",
            "q3a_bot_team_policy_status bots=3 playing=2 spectators=1 pass=1",
        ))
        _line, metrics = harness.parse_status_line(text)
        marker_metrics = harness.parse_marker_metrics(text, harness.optional_marker_names())

        optional_fields = harness.discover_optional_fields(metrics, marker_metrics)
        groups = {
            (group["family"], group["source"]): group["metrics"]
            for group in optional_fields
        }

        dispatch = groups[("action_dispatch_counters", harness.ACTION_STATUS_MARKER)]
        self.assertEqual(dispatch["action_command_request_builds"], 2)
        self.assertEqual(dispatch["action_command_request_submitted"], 1)
        self.assertEqual(dispatch["action_last_command_dispatch_outcome_name"], "submitted")

        aim_policy = groups[("aim_policy_counters", harness.ACTION_STATUS_MARKER)]
        self.assertEqual(aim_policy["aim_policy_evaluations"], 4)
        self.assertEqual(aim_policy["aim_policy_blocks_reaction"], 1)
        self.assertEqual(aim_policy["last_aim_policy_failure_name"], "reaction_pending")

        combat_firing = groups[("live_combat_firing_counters", harness.ACTION_DETAIL_STATUS_MARKER)]
        self.assertEqual(combat_firing["combat_evaluations"], 4)
        self.assertEqual(combat_firing["action_last_intent_name"], "attack")

        aim_detail = groups[("aim_policy_counters", harness.ACTION_DETAIL_STATUS_MARKER)]
        self.assertEqual(aim_detail["last_aim_policy_skill"], 3)
        self.assertEqual(aim_detail["last_aim_policy_reaction_delay_ms"], 250)
        self.assertEqual(aim_detail["projectile_lead_uses"], 1)
        self.assertEqual(aim_detail["last_projectile_lead_weapon"], 5)
        self.assertEqual(aim_detail["live_aim_evaluations"], 2)
        self.assertEqual(aim_detail["live_aim_fire_allowed"], 1)
        self.assertEqual(aim_detail["last_live_aim_reason"], "projectile_lead")

        special_items = groups[("special_item_utility_buckets", harness.ACTION_STATUS_MARKER)]
        self.assertEqual(special_items["item_damage_boost_candidates"], 1)
        self.assertEqual(special_items["item_ctf_objective_seek_decisions"], 1)
        self.assertEqual(special_items["item_last_special_kind_name"], "tech")

        timers = groups[("item_timer_fairness_signals", harness.ACTION_DETAIL_STATUS_MARKER)]
        self.assertEqual(timers["item_timer_evaluations"], 2)
        self.assertEqual(timers["last_item_timer_reason"], "fuzzed")
        self.assertEqual(timers["item_timing_policy_evaluations"], 2)
        self.assertEqual(timers["item_timing_consumer_evaluations"], 1)
        self.assertEqual(
            timers[harness.ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC],
            1,
        )
        self.assertEqual(timers["item_last_timing_consumer_reason_name"], "timer_ready")

        route_targets = groups[("route_target_stabilization_counters", harness.STATUS_MARKER)]
        self.assertEqual(route_targets["route_target_stabilization_checks"], 3)
        self.assertEqual(route_targets["route_target_stabilizations"], 1)
        self.assertEqual(route_targets["last_route_target_stable_point_index"], 2)

        ffa_roam_route = groups[("ffa_roam_route_counters", harness.STATUS_MARKER)]
        self.assertEqual(ffa_roam_route["ffa_roam_route_requests"], 4)
        self.assertEqual(ffa_roam_route["ffa_roam_route_policy_selections"], 4)
        self.assertEqual(ffa_roam_route["ffa_roam_route_activations"], 2)
        self.assertEqual(ffa_roam_route["ffa_roam_route_route_requests"], 3)
        self.assertEqual(ffa_roam_route["last_ffa_roam_route_mode"], 1)
        self.assertEqual(ffa_roam_route["last_ffa_roam_route_role"], 1)
        self.assertEqual(ffa_roam_route["last_ffa_roam_route_lane"], 3)
        self.assertEqual(ffa_roam_route["last_ffa_roam_route_goal_distance_sq"], 802816)

        ffa_spawn_camp = groups[("ffa_spawn_camp_avoidance_counters", harness.STATUS_MARKER)]
        self.assertEqual(ffa_spawn_camp["ffa_spawn_camp_avoidance_requests"], 4)
        self.assertEqual(ffa_spawn_camp["ffa_spawn_camp_avoidance_policy_selections"], 4)
        self.assertEqual(ffa_spawn_camp["ffa_spawn_camp_avoidance_source_selections"], 2)
        self.assertEqual(ffa_spawn_camp["ffa_spawn_camp_avoidance_activations"], 2)
        self.assertEqual(ffa_spawn_camp["ffa_spawn_camp_avoidance_route_requests"], 3)
        self.assertEqual(ffa_spawn_camp["last_ffa_spawn_camp_avoidance_source_client"], 2)
        self.assertEqual(ffa_spawn_camp["last_ffa_spawn_camp_avoidance_source_distance_sq"], 16384)
        self.assertEqual(ffa_spawn_camp["last_ffa_spawn_camp_avoidance_policy_avoid"], 1)

        team_role_route = groups[("team_role_route_counters", harness.STATUS_MARKER)]
        self.assertEqual(team_role_route["team_role_route_requests"], 4)
        self.assertEqual(team_role_route["team_role_route_policy_selections"], 4)
        self.assertEqual(team_role_route["team_role_route_activations"], 2)
        self.assertEqual(team_role_route["team_role_route_route_requests"], 3)
        self.assertEqual(team_role_route["last_team_role_route_mode"], 2)
        self.assertEqual(team_role_route["last_team_role_route_role"], 5)
        self.assertEqual(team_role_route["last_team_role_route_lane"], 3)
        self.assertEqual(team_role_route["last_team_role_route_goal_distance_sq"], 802816)

        team_role_combat = groups[("team_role_combat_counters", harness.STATUS_MARKER)]
        self.assertEqual(team_role_combat["team_role_combat_requests"], 4)
        self.assertEqual(team_role_combat["team_role_combat_policy_selections"], 4)
        self.assertEqual(team_role_combat["team_role_combat_target_selections"], 3)
        self.assertEqual(team_role_combat["team_role_combat_attack_decisions"], 3)
        self.assertEqual(team_role_combat["last_team_role_combat_mode"], 2)
        self.assertEqual(team_role_combat["last_team_role_combat_role"], 5)
        self.assertEqual(team_role_combat["last_team_role_combat_lane"], 3)
        self.assertEqual(team_role_combat["last_team_role_combat_target_visible"], 1)
        self.assertEqual(team_role_combat["last_team_role_combat_target_shootable"], 1)

        ctf_role_route = groups[("ctf_role_route_counters", harness.STATUS_MARKER)]
        self.assertEqual(ctf_role_route["ctf_role_route_requests"], 4)
        self.assertEqual(ctf_role_route["ctf_role_route_policy_selections"], 4)
        self.assertEqual(ctf_role_route["ctf_role_route_activations"], 2)
        self.assertEqual(ctf_role_route["ctf_role_route_objective_deferrals"], 1)
        self.assertEqual(ctf_role_route["ctf_role_route_route_requests"], 3)
        self.assertEqual(ctf_role_route["last_ctf_role_route_mode"], 3)
        self.assertEqual(ctf_role_route["last_ctf_role_route_role"], 5)
        self.assertEqual(ctf_role_route["last_ctf_role_route_lane"], 3)
        self.assertEqual(ctf_role_route["last_ctf_role_route_goal_distance_sq"], 802816)

        ctf_role_combat = groups[("ctf_role_combat_counters", harness.STATUS_MARKER)]
        self.assertEqual(ctf_role_combat["ctf_role_combat_requests"], 4)
        self.assertEqual(ctf_role_combat["ctf_role_combat_policy_selections"], 4)
        self.assertEqual(ctf_role_combat["ctf_role_combat_target_selections"], 3)
        self.assertEqual(ctf_role_combat["ctf_role_combat_attack_decisions"], 3)
        self.assertEqual(ctf_role_combat["last_ctf_role_combat_mode"], 3)
        self.assertEqual(ctf_role_combat["last_ctf_role_combat_role"], 5)
        self.assertEqual(ctf_role_combat["last_ctf_role_combat_lane"], 3)
        self.assertEqual(ctf_role_combat["last_ctf_role_combat_target_visible"], 1)
        self.assertEqual(ctf_role_combat["last_ctf_role_combat_target_shootable"], 1)

        ctf_dropped_flag_route = groups[
            ("ctf_dropped_flag_route_counters", harness.STATUS_MARKER)
        ]
        self.assertEqual(ctf_dropped_flag_route["ctf_dropped_flag_route_requests"], 4)
        self.assertEqual(ctf_dropped_flag_route["ctf_dropped_flag_route_assignments"], 4)
        self.assertEqual(ctf_dropped_flag_route["ctf_dropped_flag_route_route_commands"], 4)
        self.assertEqual(ctf_dropped_flag_route["last_ctf_dropped_flag_route_lane"], 5)
        self.assertEqual(ctf_dropped_flag_route["last_ctf_dropped_flag_route_source"], 2)
        self.assertEqual(ctf_dropped_flag_route["last_ctf_dropped_flag_route_item"], 10)

        ctf_carrier_support_route = groups[
            ("ctf_carrier_support_route_counters", harness.STATUS_MARKER)
        ]
        self.assertEqual(ctf_carrier_support_route["ctf_carrier_support_route_requests"], 4)
        self.assertEqual(ctf_carrier_support_route["ctf_carrier_support_route_assignments"], 4)
        self.assertEqual(ctf_carrier_support_route["ctf_carrier_support_route_route_commands"], 4)
        self.assertEqual(ctf_carrier_support_route["last_ctf_carrier_support_route_role"], 4)
        self.assertEqual(ctf_carrier_support_route["last_ctf_carrier_support_route_lane"], 4)
        self.assertEqual(ctf_carrier_support_route["last_ctf_carrier_support_route_source"], 3)
        self.assertEqual(ctf_carrier_support_route["last_ctf_carrier_support_route_carrier_client"], 2)

        ctf_base_return_route = groups[
            ("ctf_base_return_route_counters", harness.STATUS_MARKER)
        ]
        self.assertEqual(ctf_base_return_route["ctf_base_return_route_requests"], 4)
        self.assertEqual(ctf_base_return_route["ctf_base_return_route_assignments"], 4)
        self.assertEqual(ctf_base_return_route["ctf_base_return_route_route_commands"], 4)
        self.assertEqual(ctf_base_return_route["last_ctf_base_return_route_role"], 3)
        self.assertEqual(ctf_base_return_route["last_ctf_base_return_route_lane"], 6)
        self.assertEqual(ctf_base_return_route["last_ctf_base_return_route_type"], 2)
        self.assertEqual(ctf_base_return_route["last_ctf_base_return_route_source"], 3)
        self.assertEqual(ctf_base_return_route["last_ctf_base_return_route_carrier_client"], 2)

        ctf_objective_route = groups[
            ("ctf_objective_route_counters", harness.STATUS_MARKER)
        ]
        self.assertEqual(ctf_objective_route["ctf_objective_route_requests"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_assignments"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_base_return_candidates"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_carrier_support_candidates"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_dropped_flag_candidates"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_base_return_selections"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_carrier_support_selections"], 0)
        self.assertEqual(ctf_objective_route["ctf_objective_route_dropped_flag_selections"], 0)
        self.assertEqual(ctf_objective_route["ctf_objective_route_carrier_support_deferrals"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_dropped_flag_deferrals"], 4)
        self.assertEqual(ctf_objective_route["ctf_objective_route_route_commands"], 4)
        self.assertEqual(ctf_objective_route["last_ctf_objective_route_selection"], 1)
        self.assertEqual(ctf_objective_route["last_ctf_objective_route_lane"], 6)
        self.assertEqual(ctf_objective_route["last_ctf_objective_route_source"], 3)
        self.assertEqual(ctf_objective_route["last_ctf_objective_route_carrier_client"], 2)

        team_fire_avoidance = groups[("team_fire_avoidance_counters", harness.STATUS_MARKER)]
        self.assertEqual(team_fire_avoidance["team_fire_avoidance_evaluations"], 4)
        self.assertEqual(team_fire_avoidance["team_fire_avoidance_blocks"], 2)
        self.assertEqual(team_fire_avoidance["team_fire_avoidance_line_blocks"], 2)
        self.assertEqual(team_fire_avoidance["last_team_fire_avoidance_friendly_line"], 1)

        team_item_roles = groups[("team_item_role_counters", harness.NAV_POLICY_STATUS_MARKER)]
        self.assertEqual(team_item_roles["team_item_role_evaluations"], 5)
        self.assertEqual(team_item_roles["team_item_role_selections"], 4)
        self.assertEqual(team_item_roles["team_item_role_selected_goals"], 2)
        self.assertEqual(team_item_roles["last_team_item_role_mode"], 2)
        self.assertEqual(team_item_roles["last_team_item_role_item_role"], 2)
        self.assertEqual(team_item_roles["last_team_item_role_item_role_name"], "weapon_control")

        leader_route = groups[("coop_leader_route_counters", harness.COOP_COMMAND_STATUS_MARKER)]
        self.assertEqual(leader_route["coop_leader_route_activations"], 5)
        self.assertEqual(leader_route["coop_leader_route_refreshes"], 3)
        self.assertEqual(leader_route["last_coop_leader_route_intent_name"], "support_combat")

        lead_advance = groups[("coop_lead_advance_counters", harness.COOP_COMMAND_STATUS_MARKER)]
        self.assertEqual(lead_advance["coop_lead_advance_requests"], 6)
        self.assertEqual(lead_advance["coop_lead_advance_activations"], 5)
        self.assertEqual(lead_advance["coop_lead_advance_route_requests"], 4)
        self.assertEqual(lead_advance["last_coop_lead_advance_intent_name"], "lead_advance")

        progress_wait = groups[("coop_progress_wait_counters", harness.COOP_COMMAND_STATUS_MARKER)]
        self.assertEqual(progress_wait["coop_progress_wait_commands"], 2)
        self.assertEqual(progress_wait["last_coop_progress_wait_intent"], 2)

        anti_block = groups[("coop_anti_block_counters", harness.COOP_COMMAND_STATUS_MARKER)]
        self.assertEqual(anti_block["coop_anti_block_requests"], 4)
        self.assertEqual(anti_block["coop_anti_block_commands"], 3)
        self.assertEqual(anti_block["last_coop_anti_block_intent_name"], "support_combat")
        self.assertEqual(anti_block["last_coop_anti_block_forward_move"], -90)

        door_elevator = groups[("coop_door_elevator_counters", harness.COOP_COMMAND_STATUS_MARKER)]
        self.assertEqual(door_elevator["coop_door_elevator_requests"], 5)
        self.assertEqual(door_elevator["coop_door_elevator_source_activations"], 1)
        self.assertEqual(door_elevator["coop_door_elevator_source_commands"], 3)
        self.assertEqual(door_elevator["coop_door_elevator_hold_commands"], 2)
        self.assertEqual(door_elevator["last_coop_door_elevator_source_client"], 0)
        self.assertEqual(door_elevator["last_coop_door_elevator_action"], 3)
        self.assertEqual(door_elevator["last_coop_door_elevator_kind"], 3)
        self.assertEqual(door_elevator["last_coop_door_elevator_entity"], 17)
        self.assertEqual(door_elevator["last_coop_door_elevator_intent_name"], "support_combat")

        interaction_retry = groups[("coop_interaction_retry_counters", harness.COOP_COMMAND_STATUS_MARKER)]
        self.assertEqual(interaction_retry["coop_interaction_retry_commands"], 3)
        self.assertEqual(interaction_retry["last_coop_interaction_retry_action"], 3)
        self.assertEqual(interaction_retry["last_coop_interaction_retry_kind"], 7)

        corner_cutting = groups[("trace_checked_corner_cutting_signals", harness.NAV_POLICY_STATUS_MARKER)]
        self.assertEqual(corner_cutting["route_corner_cut_trace_checks"], 3)
        self.assertEqual(corner_cutting["route_corner_cut_accepted"], 1)

        team_readiness = groups[("team_mode_readiness_signals", harness.TEAM_POLICY_STATUS_MARKER)]
        self.assertEqual(team_readiness["bots"], 3)
        self.assertEqual(team_readiness["playing"], 2)

        text_report = harness.optional_field_text({"optional_fields": optional_fields})
        self.assertIn("action_dispatch_counters<q3a_bot_action_status>", text_report)
        self.assertIn("route_target_stabilization_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ffa_roam_route_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ffa_spawn_camp_avoidance_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("team_role_route_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("team_role_combat_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ctf_role_route_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ctf_role_combat_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ctf_dropped_flag_route_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ctf_carrier_support_route_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ctf_base_return_route_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("ctf_objective_route_counters<q3a_bot_frame_command_status>", text_report)
        self.assertIn("team_item_role_counters<q3a_bot_nav_policy_status>", text_report)
        self.assertIn("item_timer_fairness_signals<q3a_bot_action_detail_status>", text_report)
        self.assertIn("coop_leader_route_counters<q3a_bot_coop_command_status>", text_report)
        self.assertIn("coop_lead_advance_counters<q3a_bot_coop_command_status>", text_report)
        self.assertIn("coop_anti_block_counters<q3a_bot_coop_command_status>", text_report)
        self.assertIn("coop_door_elevator_counters<q3a_bot_coop_command_status>", text_report)
        self.assertIn("coop_interaction_retry_counters<q3a_bot_coop_command_status>", text_report)

    def test_raw_reserved_optional_fields_report_without_new_gates(self) -> None:
        raw_text = "\n".join((
            *passing_raw_reserved_mode_lines(21),
            "q3a_bot_frame_command_status pass=1 route_failures=0 "
            "route_target_stabilization_checks=5 route_target_stabilizations=2 "
            "route_target_stabilization_skips=3 last_route_target_stable_point_index=4",
            "q3a_bot_action_status action_command_request_builds=3 "
            "action_command_request_accepted=3 action_command_request_dispatch_attempts=3 "
            "action_command_request_submitted=2 action_command_request_deferred=1 "
            "action_last_command_dispatch_outcome_name=submitted "
            "aim_policy_evaluations=1 aim_policy_fire_allowed=1 "
            "item_utility_powerup_candidates=2 item_special_utility_boosts=2",
            "q3a_bot_team_policy_status bots=3 playing=2 spectators=1 pass=0",
            "q3a_bot_match_readiness_status ffa_pass=1 tdm_pass=0 pass=0 "
            "deathmatch=1 team_mode=0 gametype=0 bots=3 playing=3 "
            "spectators=0 queued=0 free=3 red=0 blue=0",
            "q3a_bot_coop_readiness_status pass=0 coop=0 bots=3 "
            "playing=3 spectators=0 queued=0",
        ))
        raw_diagnostics = harness.parse_raw_reserved_mode_diagnostics(
            raw_text,
            pathlib.Path(".tmp/bot_scenarios/raw_modes/mode21.stdout.txt"),
        )
        diagnostic = raw_diagnostics[0]
        optional_groups = {
            (group["family"], group["source"]): group["metrics"]
            for group in diagnostic["optional_fields"]
        }

        self.assertEqual(
            optional_groups[
                ("action_dispatch_counters", harness.ACTION_STATUS_MARKER)
            ]["action_command_request_submitted"],
            2,
        )
        self.assertEqual(
            optional_groups[
                ("route_target_stabilization_counters", harness.STATUS_MARKER)
            ]["route_target_stabilizations"],
            2,
        )
        self.assertEqual(diagnostic["metrics"]["pass"], 1)
        self.assertEqual(
            optional_groups[
                ("team_mode_readiness_signals", harness.TEAM_POLICY_STATUS_MARKER)
            ]["bots"],
            3,
        )
        self.assertEqual(
            optional_groups[
                ("team_mode_readiness_signals", harness.MATCH_READINESS_STATUS_MARKER)
            ]["ffa_pass"],
            1,
        )
        self.assertEqual(
            optional_groups[
                ("team_mode_readiness_signals", harness.COOP_READINESS_STATUS_MARKER)
            ]["coop"],
            0,
        )

        report = harness.pending_gap_report(
            [pending_promotion_scenario("switch_weapons")],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )
        switch_weapons = report["scenarios"][0]
        gap_groups = {
            (group["family"], group["source"]): group["metrics"]
            for group in switch_weapons["optional_fields"]
        }

        self.assertEqual(report["summary"]["ready"], 1)
        self.assertEqual(report["summary"]["failed_metric_checks"], 0)
        self.assertEqual(switch_weapons["status"], "ready")
        self.assertEqual(
            gap_groups[
                ("action_dispatch_counters", harness.ACTION_STATUS_MARKER)
            ]["action_command_request_builds"],
            3,
        )
        self.assertEqual(switch_weapons["blockers"], [])

    def test_raw_reserved_mode_promotion_passes_for_modes_20_to_25(self) -> None:
        raw_diagnostics = []
        for mode in (20, 21, 22, 23, 24, 25):
            raw_diagnostics.extend(harness.parse_raw_reserved_mode_diagnostics(
                passing_raw_reserved_mode_text(mode),
                pathlib.Path(f".tmp/bot_scenarios/raw_modes/mode{mode}.stdout.txt"),
            ))

        report = harness.pending_gap_report(
            [
                pending_promotion_scenario("engage_enemy"),
                pending_promotion_scenario("switch_weapons"),
                pending_promotion_scenario("health_armor_pickup"),
                pending_promotion_scenario("team_objective"),
                pending_promotion_scenario("aim_fairness_policy_integration"),
                pending_promotion_scenario("item_timer_fairness_signals"),
            ],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )

        self.assertEqual(report["summary"]["ready"], 6)
        self.assertEqual(report["summary"]["blocked"], 0)
        self.assertEqual(report["summary"]["missing_rows"], 0)
        self.assertEqual(report["summary"]["missing_status_metrics"], 0)
        self.assertEqual(report["summary"]["missing_marker_metrics"], 0)
        self.assertEqual(report["summary"]["missing_policy_consumer_fields"], 0)
        self.assertEqual(report["summary"]["failed_metric_checks"], 0)
        self.assertEqual(report["summary"]["failed_marker_checks"], 0)
        self.assertEqual(report["summary"]["overall"], "ready")

        by_name = {scenario["name"]: scenario for scenario in report["scenarios"]}
        for scenario in by_name.values():
            self.assertEqual(scenario["status"], "ready")
            self.assertEqual(scenario["fixture_source"], "raw_reserved_mode")
            self.assertEqual(scenario["missing_metrics"], [])
            self.assertEqual(scenario["missing_marker_metrics"], [])
            self.assertEqual(scenario["blockers"], [])

        team_objective = by_name["team_objective"]
        self.assertIn(
            harness.OBJECTIVE_STATUS_MARKER,
            team_objective["metric_sources"]["team_objective_reaches"],
        )
        self.assertEqual(team_objective["fixture_smoke_mode"], 23)
        self.assertEqual(by_name["aim_fairness_policy_integration"]["fixture_smoke_mode"], 24)
        self.assertEqual(by_name["item_timer_fairness_signals"]["fixture_smoke_mode"], 25)
        timer_groups = {
            (group["family"], group["source"]): group["metrics"]
            for group in by_name["item_timer_fairness_signals"]["optional_fields"]
        }
        timer_metrics = timer_groups[
            ("item_timer_fairness_signals", harness.ACTION_DETAIL_STATUS_MARKER)
        ]
        self.assertEqual(
            timer_metrics[harness.ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC],
            1,
        )

    def test_pending_gap_report_calls_out_missing_policy_consumer_fields(self) -> None:
        raw_text = "\n".join((
            RESERVED_MODE_BEGIN_LINES[24],
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_action_detail_status combat_evaluations=2 "
            "combat_fire_decisions=1 combat_withheld_fire=0 "
            "action_applied_cmds=1 action_applied_attack_buttons=1 "
            "aim_policy_evaluations=2 aim_policy_fire_allowed=1 "
            "last_aim_policy_failure_name=none",
            RESERVED_MODE_BEGIN_LINES[25],
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_action_detail_status item_timer_evaluations=2 "
            "item_timer_allowed_uses=1 item_timer_fairness_blocks=0 "
            "last_item_timer_allowed=1 last_item_timer_reason=exact_timer",
        ))
        raw_diagnostics = harness.parse_raw_reserved_mode_diagnostics(
            raw_text,
            pathlib.Path(".tmp/bot_scenarios/raw_modes/policy_consumers.stdout.txt"),
        )

        report = harness.pending_gap_report(
            [
                pending_promotion_scenario("aim_fairness_policy_integration"),
                pending_promotion_scenario("item_timer_fairness_signals"),
            ],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )
        by_name = {scenario["name"]: scenario for scenario in report["scenarios"]}
        aim = by_name["aim_fairness_policy_integration"]
        timers = by_name["item_timer_fairness_signals"]

        self.assertEqual(report["summary"]["ready"], 0)
        self.assertEqual(report["summary"]["blocked"], 2)
        self.assertEqual(report["summary"]["missing_policy_consumer_fields"], 4)
        self.assertEqual(
            {
                (item["source"], item["metric"])
                for item in aim["missing_policy_consumer_fields"]
            },
            {
                (harness.ACTION_STATUS_MARKER, "live_aim_evaluations"),
                (harness.ACTION_STATUS_MARKER, "live_aim_fire_allowed"),
            },
        )
        self.assertEqual(
            {
                (item["source"], item["metric"])
                for item in timers["missing_policy_consumer_fields"]
            },
            {
                (harness.ACTION_DETAIL_STATUS_MARKER, "item_timing_consumer_evaluations"),
                (
                    harness.ACTION_DETAIL_STATUS_MARKER,
                    harness.ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC,
                ),
            },
        )
        self.assertTrue(
            any("missing policy-consumer fields" in blocker for blocker in aim["blockers"])
        )
        self.assertTrue(
            any("missing policy-consumer fields" in blocker for blocker in timers["blockers"])
        )
        markdown = harness.build_markdown_report(report)
        self.assertIn("Missing Policy Consumers", markdown)
        self.assertIn(harness.ITEM_TIMING_CONSUMER_READY_OR_LIVE_METRIC, markdown)

    def test_pending_gap_report_uses_latest_raw_reserved_mode_per_mode(self) -> None:
        raw_text = "\n".join((
            *passing_raw_reserved_mode_lines(21),
            "noise between reserved runs",
            RESERVED_MODE_BEGIN_LINES[21],
            "q3a_bot_frame_command_status pass=0 route_failures=1",
            "q3a_bot_action_status combat_weapon_switch_decisions=1 "
            "action_weapon_switch_decisions=1 action_pending_weapon_switches=1 "
            "weapon_switch_requests=1 weapon_switch_completions=0 weapon_switch_failures=1 "
            "weapon_switch_expected_item=5 weapon_switch_actual_item=0 "
            "weapon_switch_expected_match=0",
        ))
        raw_diagnostics = harness.parse_raw_reserved_mode_diagnostics(
            raw_text,
            pathlib.Path(".tmp/bot_scenarios/raw_modes/mode21.stdout.txt"),
        )

        report = harness.pending_gap_report(
            [pending_promotion_scenario("switch_weapons")],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )
        switch_weapons = report["scenarios"][0]

        self.assertEqual(len(raw_diagnostics), 2)
        self.assertEqual(report["summary"]["ready"], 0)
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(switch_weapons["fixture_status"], "failed")
        self.assertEqual(switch_weapons["status"], "blocked")
        self.assertEqual(switch_weapons["raw_diagnostic"]["line"], 7)
        self.assertTrue(
            any(
                "raw reserved-mode diagnostics status is failed, expected passed" in blocker
                for blocker in switch_weapons["blockers"]
            )
        )

    def test_raw_reserved_mode_blocks_when_dedicated_marker_value_fails(self) -> None:
        raw_text = "\n".join((
            RESERVED_MODE_BEGIN_LINES[23],
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_objective_status team_objective_evaluations=1 "
            "team_objective_assignments=1 team_objective_route_requests=1 "
            "team_objective_route_commands=1 team_objective_reaches=0 "
            "team_objective_flag_pickups=1 "
            "team_objective_role_policy_evaluations=1 "
            "team_objective_role_policy_selections=1 "
            "team_objective_role_policy_lane_midfield_selections=1 "
            "team_objective_enemy_flag_assignments=1 "
            "team_objective_match_policy_evaluations=1 "
            "team_objective_match_policy_ffa=1 "
            "team_objective_match_policy_attack=1 "
            "last_team_objective_type=1 last_team_objective_role=1 "
            "last_team_objective_lane=3 last_team_objective_client=2 "
            "last_team_objective_item=9 last_team_objective_area=334",
            "q3a_bot_objective_detail_status "
            "team_objective_role_policy_requested_honored=1 "
            "team_objective_role_policy_attack_selections=1 "
            "team_objective_role_policy_lane_midfield_selections=1 "
            "team_objective_enemy_flag_assignments=1",
        ))
        raw_diagnostics = harness.parse_raw_reserved_mode_diagnostics(
            raw_text,
            pathlib.Path(".tmp/bot_scenarios/raw_modes/mode23.stdout.txt"),
        )

        report = harness.pending_gap_report(
            [pending_promotion_scenario("team_objective")],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )
        team_objective = report["scenarios"][0]
        failed_metrics = {
            check["metric"]: check
            for check in team_objective["failed_metric_checks"]
        }

        self.assertEqual(report["summary"]["ready"], 0)
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(report["summary"]["missing_status_metrics"], 0)
        self.assertEqual(report["summary"]["failed_metric_checks"], 1)
        self.assertEqual(team_objective["missing_metrics"], [])
        self.assertEqual(failed_metrics["team_objective_reaches"]["actual"], 0)
        self.assertIn(
            harness.OBJECTIVE_STATUS_MARKER,
            team_objective["metric_sources"]["team_objective_reaches"],
        )

    def test_pending_gap_report_uses_raw_reserved_mode_diagnostics(self) -> None:
        scenario = pending_promotion_scenario("engage_enemy")
        raw_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=20 combat=engage_enemy "
            "weapon_switch=0 item_focus=0 team_objective=0 target=2 gametype=0",
            "q3a_bot_frame_command_status pass=0 route_failures=0 "
            "combat_enemy_acquisitions=0 combat_enemy_visible=0 combat_enemy_shootable=0 "
            "last_combat_enemy_client=-1",
            "q3a_bot_action_status combat_fire_decisions=0 action_attack_decisions=0 "
            "action_applied_attack_buttons=0 combat_damage_events=0 last_combat_damage=0",
            "q3a_bot_action_detail_status combat_evaluations=1 combat_fire_decisions=0 "
            "combat_withheld_fire=0 action_applied_cmds=0 "
            "action_applied_attack_buttons=0 action_last_intent_name=attack",
        ))
        raw_diagnostics = harness.parse_raw_reserved_mode_diagnostics(
            raw_text,
            pathlib.Path(".tmp/bot_scenarios/raw_modes/mode20.stdout.txt"),
        )

        report = harness.pending_gap_report(
            [scenario],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )
        engage_enemy = report["scenarios"][0]

        self.assertEqual(report["summary"]["raw_diagnostics"], 1)
        self.assertEqual(report["summary"]["raw_diagnostic_rows"], 1)
        self.assertEqual(report["summary"]["missing_rows"], 0)
        self.assertEqual(engage_enemy["fixture_source"], "raw_reserved_mode")
        self.assertEqual(engage_enemy["fixture_status"], "failed")
        self.assertEqual(engage_enemy["fixture_smoke_mode"], 20)
        self.assertTrue(engage_enemy["raw_diagnostic_present"])
        self.assertEqual(engage_enemy["missing_metrics"], [])
        self.assertEqual(engage_enemy["missing_marker_metrics"], [])
        self.assertIn(
            harness.ACTION_STATUS_MARKER,
            engage_enemy["metric_sources"]["action_applied_attack_buttons"],
        )
        self.assertTrue(
            any(
                "raw reserved-mode diagnostics status is failed, expected passed" in blocker
                for blocker in engage_enemy["blockers"]
            )
        )
        self.assertTrue(
            any(
                "action_applied_attack_buttons ge 1 failed, actual=0" in blocker
                for blocker in engage_enemy["blockers"]
            )
        )

    def test_health_armor_raw_mode_blocks_without_pickup_proof(self) -> None:
        scenario = pending_promotion_scenario("health_armor_pickup")
        raw_text = "\n".join((
            f"{harness.SCENARIO_BEGIN_MARKER} mode=22 combat=0 "
            "weapon_switch=0 item_focus=health_armor team_objective=0 target=1 gametype=0",
            "q3a_bot_frame_command_status pass=1 route_failures=0 "
            "item_goal_assignments=15 item_goal_scans=15 item_goal_candidates=329 "
            "last_item_goal_item=4 last_item_goal_score=858 "
            "last_failed_goal_item=2",
            "q3a_bot_blackboard_status blackboard_updates=60",
        ))
        raw_diagnostics = harness.parse_raw_reserved_mode_diagnostics(
            raw_text,
            pathlib.Path(".tmp/bot_scenarios/raw_modes/mode22.stdout.txt"),
        )

        report = harness.pending_gap_report(
            [scenario],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )
        health_armor = report["scenarios"][0]
        failed_metrics = {
            check["metric"]: check
            for check in health_armor["failed_metric_checks"]
        }

        self.assertEqual(report["summary"]["ready"], 0)
        self.assertEqual(report["summary"]["blocked"], 1)
        self.assertEqual(report["summary"]["missing_status_metrics"], 13)
        self.assertEqual(report["summary"]["failed_metric_checks"], 13)
        self.assertEqual(report["summary"]["failed_marker_checks"], 0)
        self.assertEqual(health_armor["fixture_status"], "passed")
        self.assertEqual(health_armor["fixture_smoke_mode"], 22)
        self.assertEqual(health_armor["missing_marker_metrics"], [])
        self.assertEqual(
            health_armor["present_metrics"],
            ["pass", "route_failures"],
        )
        self.assertEqual(
            health_armor["related_present_metrics"]["item_goal_assignments"],
            15,
        )
        self.assertEqual(
            health_armor["related_present_metrics"]["last_item_goal_item"],
            4,
        )
        self.assertIn("item_health_pickups", health_armor["missing_metrics"])
        self.assertEqual(
            health_armor["missing_metric_sources"]["item_health_pickups"],
            [harness.ACTION_STATUS_MARKER],
        )
        self.assertIsNone(failed_metrics["item_health_pickups"]["actual"])
        self.assertTrue(
            any(
                "item_health_pickups (expected from q3a_bot_action_status)" in blocker
                for blocker in health_armor["blockers"]
            )
        )
        self.assertTrue(
            any(
                "health/armor-specific pickup proof is still missing" in note
                for note in health_armor["notes"]
            )
        )

    def test_comparison_metric_deltas(self) -> None:
        previous = {
            "scenarios": [
                {
                    "name": "spawn_route_to_item",
                    "status": "passed",
                    "metrics": {"commands": 8, "route_failures": 0, "pass": 1},
                    "duration_seconds": 1.5,
                },
                {
                    "name": "removed_case",
                    "status": "passed",
                    "metrics": {"commands": 1},
                },
            ],
        }
        current = {
            "scenarios": [
                {
                    "name": "spawn_route_to_item",
                    "status": "failed",
                    "metrics": {"commands": 10, "route_failures": 1, "pass": 0},
                    "duration_seconds": 2.0,
                },
                {
                    "name": "new_case",
                    "status": "pending",
                    "metrics": {},
                },
            ],
        }

        comparison = harness.compare_reports(
            current,
            previous,
            pathlib.Path("previous.json"),
        )

        self.assertEqual(comparison["summary"]["total"], 3)
        self.assertEqual(comparison["summary"]["matched"], 1)
        self.assertEqual(comparison["summary"]["added"], 1)
        self.assertEqual(comparison["summary"]["removed"], 1)
        self.assertEqual(comparison["summary"]["status_changed"], 3)
        self.assertEqual(comparison["summary"]["metric_changed"], 2)

        spawn = next(
            scenario for scenario in comparison["scenarios"]
            if scenario["name"] == "spawn_route_to_item"
        )
        self.assertEqual(spawn["previous_status"], "passed")
        self.assertEqual(spawn["current_status"], "failed")
        self.assertTrue(spawn["status_changed"])
        self.assertEqual(spawn["metric_changes"]["commands"]["delta"], 2)
        self.assertEqual(spawn["metric_changes"]["route_failures"]["delta"], 1)
        self.assertEqual(spawn["metric_changes"]["pass"]["delta"], -1)
        self.assertEqual(spawn["metric_changes"]["duration_seconds"]["delta"], 0.5)

    def test_latest_report_fixture_when_available(self) -> None:
        if not LATEST_REPORT_FIXTURE.is_file():
            self.skipTest(f"optional fixture missing: {LATEST_REPORT_FIXTURE}")

        report = json.loads(LATEST_REPORT_FIXTURE.read_text(encoding="utf-8"))
        scenarios = harness.report_scenario_map(report)
        required = {
            "spawn_route_to_item",
            "recover_from_stall",
            "multi_bot_reservation",
        }

        missing = sorted(required - set(scenarios))
        self.assertEqual(missing, [], f"latest report missing implemented scenario rows: {missing}")

        self.assert_passed_route_clean(scenarios["spawn_route_to_item"])
        self.assertGreaterEqual(scenarios["spawn_route_to_item"]["metrics"]["commands"], 1)
        self.assertGreaterEqual(scenarios["spawn_route_to_item"]["metrics"]["route_commands"], 1)
        self.assertGreaterEqual(scenarios["spawn_route_to_item"]["metrics"]["item_goal_assignments"], 1)

        self.assert_passed_route_clean(scenarios["recover_from_stall"])
        self.assertGreaterEqual(scenarios["recover_from_stall"]["metrics"]["stuck_detections"], 1)
        self.assertGreaterEqual(scenarios["recover_from_stall"]["metrics"]["recovery_command_uses"], 1)

        self.assert_passed_route_clean(scenarios["multi_bot_reservation"])
        self.assertGreaterEqual(
            scenarios["multi_bot_reservation"]["metrics"]["item_goal_peak_active_reservations"],
            8,
        )

        promoted_required = {
            "engage_enemy",
            "health_armor_pickup",
            "switch_weapons",
            "team_objective",
        }
        if promoted_required <= set(scenarios):
            for name in sorted(promoted_required):
                self.assert_passed_route_clean(scenarios[name])

        if "map_change_repeat" in scenarios:
            map_repeat = scenarios["map_change_repeat"]
            self.assert_passed_route_clean(map_repeat)
            key_metrics = harness.scenario_key_metrics(map_repeat)
            self.assertGreaterEqual(key_metrics["item_goal_peak_active_reservations"], 8)
            self.assertEqual(key_metrics["cycles"], 2)
            self.assertEqual(key_metrics["map_changes"], 1)
            self.assertEqual(key_metrics["final_count"], 0)

        if "profile_backed_spawn" in scenarios:
            profile_spawn = scenarios["profile_backed_spawn"]
            self.assertEqual(profile_spawn.get("smoke_cvar"), "sv_bot_profile_smoke")
            if profile_spawn["status"] == "passed":
                markers = profile_spawn.get("markers", {})
                after_add = markers["q3a_bot_profile_smoke_after_add"][-1]
                self.assertEqual(after_add["profile"], "smoke")
                self.assertEqual(after_add["skin"], "male/grunt")
                self.assertEqual(after_add["aggression"], 0.65)
                self.assertEqual(markers["q3a_bot_profile_smoke=end"][-1]["final_count"], 0)
            else:
                self.assertTrue(profile_spawn.get("failures"))

        gap_report = harness.pending_gap_report(
            harness.select_scenarios(["pending"]),
            report,
            LATEST_REPORT_FIXTURE,
        )
        pending_scenarios = harness.select_scenarios(["pending"])
        self.assertEqual(gap_report["summary"]["total"], len(pending_scenarios))
        if pending_scenarios:
            self.assertEqual(gap_report["summary"]["ready"], 0)
            self.assertEqual(gap_report["summary"]["blocked"], len(pending_scenarios))
            self.assertEqual(gap_report["summary"]["missing_rows"], len(pending_scenarios))
            self.assertEqual(gap_report["summary"]["overall"], "blocked")
        else:
            self.assertEqual(gap_report["summary"]["ready"], 0)
            self.assertEqual(gap_report["summary"]["blocked"], 0)
            self.assertEqual(gap_report["summary"]["missing_rows"], 0)
            self.assertEqual(gap_report["summary"]["overall"], "ready")

    def assert_passed_route_clean(self, scenario: dict) -> None:
        self.assertEqual(scenario["status"], "passed")
        self.assertEqual(scenario["metrics"]["pass"], 1)
        self.assertEqual(scenario["metrics"]["route_failures"], 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
