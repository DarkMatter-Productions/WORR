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
        ]
    if mode == 22:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_action_status item_low_health_boosts=1 item_low_armor_boosts=1 "
            "item_health_goal_assignments=1 item_armor_goal_assignments=1 "
            "item_health_pickups=1 item_armor_pickups=1 "
            "last_health_pickup_delta=25 last_armor_pickup_delta=50",
        ]
    if mode == 23:
        return [
            *common,
            "q3a_bot_frame_command_status pass=1 route_failures=0",
            "q3a_bot_objective_status team_objective_evaluations=1 "
            "team_objective_assignments=1 team_objective_route_requests=1 "
            "team_objective_route_commands=1 team_objective_reaches=1 "
            "team_objective_flag_pickups=1 last_team_objective_type=1 "
            "last_team_objective_client=2 last_team_objective_item=9",
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
        metrics = {metric: 1 for metric in scenario.promotion_metrics}
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
        metrics = {metric: 1 for metric in scenario.promotion_metrics}
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

    def test_raw_reserved_mode_promotion_passes_for_modes_20_to_23(self) -> None:
        raw_diagnostics = []
        for mode in (20, 21, 22, 23):
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
            ],
            {"scenarios": []},
            pathlib.Path("latest_report.json"),
            raw_diagnostics,
        )

        self.assertEqual(report["summary"]["ready"], 4)
        self.assertEqual(report["summary"]["blocked"], 0)
        self.assertEqual(report["summary"]["missing_rows"], 0)
        self.assertEqual(report["summary"]["missing_status_metrics"], 0)
        self.assertEqual(report["summary"]["missing_marker_metrics"], 0)
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
        self.assertEqual(
            team_objective["metric_sources"]["team_objective_reaches"],
            [harness.OBJECTIVE_STATUS_MARKER],
        )
        self.assertEqual(team_objective["fixture_smoke_mode"], 23)

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
        self.assertEqual(switch_weapons["raw_diagnostic"]["line"], 6)
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
            "team_objective_flag_pickups=1 last_team_objective_type=1 "
            "last_team_objective_client=2 last_team_objective_item=9",
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
        self.assertEqual(
            team_objective["metric_sources"]["team_objective_reaches"],
            [harness.OBJECTIVE_STATUS_MARKER],
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
        self.assertEqual(
            engage_enemy["metric_sources"]["action_applied_attack_buttons"],
            [harness.ACTION_STATUS_MARKER],
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
        self.assertEqual(report["summary"]["missing_status_metrics"], 8)
        self.assertEqual(report["summary"]["failed_metric_checks"], 8)
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
        self.assertEqual(gap_report["summary"]["total"], 0)
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
