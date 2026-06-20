#!/usr/bin/env python3
"""Regression tests for the WORR bot perf analyzer."""

from __future__ import annotations

import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import analyze_bot_perf as perf


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
REAL_SOAK_FIXTURE = REPO_ROOT / ".tmp" / "q3a_bot_nav_soak_10min_final.stdout.txt"
DEFAULT_BUDGET = pathlib.Path(__file__).resolve().parent / "default_soak_budget.json"


def synthetic_log(
    *,
    elapsed_ms: int | None = 2000,
    progress_elapsed_ms: int = 1000,
    bots: int = 2,
    commands: int = 40,
    route_requests: int = 20,
    route_queries: int = 5,
    route_refreshes: int = 5,
    route_reuses: int = 15,
    route_failures: int = 0,
    recovery_command_uses: int = 4,
    stuck_detections: int = 1,
    source_counters: str = "",
) -> str:
    complete_line = ""
    if elapsed_ms is not None:
        complete_line = (
            "complete-prefix"
            f"{perf.SOAK_COMPLETE_MARKER} elapsed_ms={elapsed_ms} "
            f"duration_ms={elapsed_ms} count={bots} reports=1\n"
        )

    return (
        "server chatter before the smoke markers\n"
        f"noisy-prefix{perf.SOAK_BEGIN_MARKER} target={bots} duration_ms=2000 "
        f"progress_ms=1000 count={bots}\n"
        f"glued-output{perf.SOAK_PROGRESS_MARKER} elapsed_ms={progress_elapsed_ms} "
        f"duration_ms=2000 count={bots} reports=1\n"
        f"{complete_line}"
        "status-prefix"
        f"{perf.STATUS_MARKER} frames={commands} commands={commands} "
        f"route_requests={route_requests} route_queries={route_queries} "
        f"route_refreshes={route_refreshes} route_reuses={route_reuses} "
        f"route_commands={commands} route_failures={route_failures} "
        "route_invalid_slots=0 route_debug_routes=20 route_debug_goals=20 "
        "route_debug_missing_frames=0 route_debug_lines=10 route_debug_crosses=4 "
        "route_debug_arrows=2 route_debug_labels=2 "
        "route_debug_polyline_segments=12 "
        f"stuck_detections={stuck_detections} "
        f"stuck_recovery_activations={stuck_detections} "
        f"recovery_command_uses={recovery_command_uses} "
        "route_goal_assignments=3 item_goal_assignments=2 "
        "item_goal_reservation_skips=1 item_goal_peak_active_reservations=2 "
        f"{source_counters}"
        "skipped_inactive=0 "
        f"expected_min_frames={bots} expected_min_commands={bots} pass=1\n"
    )


SOURCE_COUNTERS = (
    "bot_frame_cpu_ns=8000000 bot_frame_cpu_samples=40 "
    "bot_frame_cpu_max_ns=1000000 bot_frame_cpu_success_ns=6000000 "
    "bot_frame_cpu_success_samples=30 "
    "route_query_cpu_ns=2000000 route_query_cpu_samples=5 "
    "route_query_cpu_max_ns=700000 route_query_cpu_fail_ns=200000 "
    "route_query_cpu_fail_samples=1 route_reuse_cpu_ns=500000 "
    "route_reuse_cpu_samples=15 "
    "q3a_route_cpu_ns=1500000 q3a_route_cpu_samples=5 "
    "q3a_route_cpu_max_ns=600000 q3a_route_cpu_fail_ns=100000 "
    "q3a_route_cpu_fail_samples=1 "
    "q3a_memory_zone_active=240000 q3a_memory_zone_peak=300000 "
    "q3a_memory_hunk_active=690000 q3a_memory_hunk_peak=720000 "
    "q3a_memory_total_active=930000 q3a_memory_total_peak=1020000 "
    "q3a_memory_failures=0 q3a_memory_available=66000000 "
    "aas_inpvs_checks=80 aas_inpvs_visible=60 aas_inpvs_misses=20 "
    "aas_inphs_checks=20 aas_inphs_visible=10 aas_inphs_misses=10 "
    "visibility_cluster_checks=100 visibility_cluster_same=15 "
    "visibility_cluster_invalid=1 visibility_decompress_calls=30 "
    "visibility_decompress_bytes=300 visibility_decompress_runs=12 "
    "visibility_decompress_failures=0 "
    "aas_trace_calls=120 bsp_trace_calls=100 bsp_trace_point_calls=60 "
    "bsp_trace_box_calls=40 bsp_trace_zero_length_calls=5 bsp_trace_hits=25 "
    "bsp_trace_misses=75 bsp_trace_startsolid=2 bsp_trace_allsolid=1 "
    "bsp_trace_hull_nodes=900 bsp_trace_brush_tests=400 "
    "bsp_trace_cpu_ns=10000000 bsp_trace_cpu_samples=100 "
    "bsp_trace_cpu_max_ns=900000 "
    "entity_trace_attempts=50 entity_trace_hits=12 entity_trace_misses=38 "
    "entity_trace_failures=0 entity_trace_clip_calls=40 "
    "entity_trace_clip_hits=10 entity_trace_clip_misses=30 "
    "entity_trace_clip_startsolid=1 entity_trace_clip_allsolid=0 "
    "entity_trace_clip_cpu_ns=4000000 entity_trace_clip_cpu_max_ns=500000 "
)


CPU_SOURCE_COUNTERS = (
    "bot_frame_cpu_ns=8000000 bot_frame_cpu_samples=40 "
    "bot_frame_cpu_max_ns=1000000 bot_frame_cpu_success_ns=6000000 "
    "bot_frame_cpu_success_samples=30 "
    "route_query_cpu_ns=2000000 route_query_cpu_samples=5 "
    "route_query_cpu_max_ns=700000 route_query_cpu_fail_ns=200000 "
    "route_query_cpu_fail_samples=1 "
    "q3a_route_cpu_ns=1500000 q3a_route_cpu_samples=5 "
    "q3a_route_cpu_max_ns=600000 q3a_route_cpu_fail_ns=100000 "
    "q3a_route_cpu_fail_samples=1 "
)


BSP_TRACE_CPU_SOURCE_COUNTERS = (
    "bsp_trace_calls=5 bsp_trace_cpu_ns=12000 "
    "bsp_trace_cpu_samples=6 bsp_trace_cpu_max_ns=5000 "
)


class BotPerfAnalyzerTests(unittest.TestCase):
    def write_text(self, root: pathlib.Path, name: str, text: str) -> pathlib.Path:
        path = root / name
        path.write_text(text, encoding="utf-8")
        return path

    def test_noisy_marker_prefixes_and_complete_duration(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(pathlib.Path(temp), "smoke.log", synthetic_log())

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertEqual(parsed.soak_begin["target"], 2)
            self.assertEqual(parsed.progress[0]["elapsed_ms"], 1000)
            self.assertEqual(parsed.soak_complete["elapsed_ms"], 2000)
            self.assertEqual(parsed.status_lines, 1)
            self.assertEqual(report["duration_sec"], 2.0)
            self.assertEqual(report["bot_count"], 2)
            self.assertEqual(report["commands_per_bot_sec"], 10.0)
            self.assertEqual(report["route_refresh_ratio"], 0.25)
            self.assertIsNone(report["bot_frame_cpu_ms_per_bot_sec"])
            self.assertEqual(report["source_counter_status"], "fail")
            self.assertFalse(report["source_counter_pass"])
            self.assertEqual(report["source_counter_pass_int"], 0)
            self.assertEqual(report["source_counter_groups_present"], [])
            self.assertEqual(report["source_counter_groups_missing_count"], 7)
            self.assertEqual(
                report["missing_current_counters"][0]["primary_counter"],
                "bot_frame_cpu_ns",
            )
            self.assertIn("bot_frame_cpu_ns", report["missing_instrumentation"])

    def test_source_counters_derive_cpu_visibility_and_trace_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(
                pathlib.Path(temp),
                "source-counters.log",
                synthetic_log(source_counters=SOURCE_COUNTERS),
            )

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertEqual(parsed.status["bot_frame_cpu_ns"], 8000000)
            self.assertEqual(report["source_counter_status"], "pass")
            self.assertTrue(report["source_counter_pass"])
            self.assertEqual(report["source_counter_pass_int"], 1)
            self.assertEqual(report["source_counter_groups_missing"], [])
            self.assertEqual(report["source_counter_groups_missing_count"], 0)
            self.assertEqual(
                report["source_counter_groups_present"],
                [
                    "bot_frame_cpu",
                    "route_query_cpu",
                    "q3a_route_cpu",
                    "q3a_memory",
                    "visibility",
                    "static_bsp_trace",
                    "entity_trace",
                ],
            )
            self.assertEqual(report["missing_instrumentation"], [])

            self.assertEqual(report["bot_frame_cpu_ms_per_sec"], 4.0)
            self.assertEqual(report["bot_frame_cpu_ms_per_bot_sec"], 2.0)
            self.assertEqual(report["bot_frame_cpu_avg_us"], 200.0)
            self.assertEqual(report["bot_frame_cpu_success_avg_us"], 200.0)
            self.assertEqual(report["bot_frame_cpu_max_us"], 1000.0)
            self.assertEqual(report["route_query_cpu_ms_per_bot_sec"], 0.5)
            self.assertEqual(report["route_query_cpu_avg_us"], 400.0)
            self.assertEqual(report["route_query_cpu_fail_avg_us"], 200.0)
            self.assertEqual(report["route_reuse_cpu_avg_us"], 33.333)
            self.assertEqual(report["q3a_route_cpu_ms_per_bot_sec"], 0.375)
            self.assertEqual(report["q3a_route_cpu_fail_avg_us"], 100.0)
            self.assertEqual(report["q3a_memory_total_active_bytes"], 930000)
            self.assertEqual(report["q3a_memory_failures"], 0)

            self.assertEqual(report["aas_inpvs_checks_per_bot_sec"], 20.0)
            self.assertEqual(report["aas_inpvs_visible_ratio"], 0.75)
            self.assertEqual(report["aas_inphs_checks_per_bot_sec"], 5.0)
            self.assertEqual(report["aas_inphs_visible_ratio"], 0.5)
            self.assertEqual(report["visibility_decompress_calls_per_sec"], 15.0)
            self.assertEqual(report["visibility_decompress_failures"], 0)

            self.assertEqual(report["bsp_trace_calls_per_bot_sec"], 25.0)
            self.assertEqual(report["bsp_trace_cpu_avg_us"], 100.0)
            self.assertEqual(report["bsp_trace_cpu_max_us"], 900.0)
            self.assertEqual(report["bsp_trace_hit_ratio"], 0.25)
            self.assertEqual(report["entity_trace_attempts_per_sec"], 25.0)
            self.assertEqual(report["entity_trace_clip_calls_per_bot_sec"], 10.0)
            self.assertEqual(report["entity_trace_clip_cpu_avg_us"], 100.0)
            self.assertEqual(report["entity_trace_clip_hit_ratio"], 0.25)

    def test_source_counter_marker_merges_with_frame_command_status(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(
                pathlib.Path(temp),
                "source-counter-marker.log",
                synthetic_log() + f"{perf.SOURCE_STATUS_MARKER} {SOURCE_COUNTERS}\n",
            )

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertEqual(parsed.status_lines, 1)
            self.assertEqual(parsed.status["aas_inpvs_checks"], 80)
            self.assertEqual(parsed.status["bsp_trace_calls"], 100)
            self.assertEqual(report["source_counter_groups_missing"], [])
            self.assertEqual(report["aas_inpvs_checks_per_bot_sec"], 20.0)

    def test_source_counter_marker_cpu_fields_report_without_false_missing_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(
                pathlib.Path(temp),
                "source-counter-cpu-marker.log",
                synthetic_log() + f"{perf.SOURCE_STATUS_MARKER} {CPU_SOURCE_COUNTERS}\n",
            )

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertEqual(parsed.status_lines, 1)
            self.assertEqual(parsed.status["commands"], 40)
            self.assertEqual(parsed.status["bot_frame_cpu_ns"], 8000000)
            self.assertEqual(parsed.status["route_query_cpu_ns"], 2000000)
            self.assertEqual(parsed.status["q3a_route_cpu_ns"], 1500000)
            self.assertEqual(
                report["source_counter_groups_present"],
                ["bot_frame_cpu", "route_query_cpu", "q3a_route_cpu"],
            )
            self.assertEqual(
                report["source_counter_groups_missing"],
                ["q3a_memory", "visibility", "static_bsp_trace", "entity_trace"],
            )
            self.assertEqual(report["source_counter_status"], "fail")
            self.assertEqual(report["source_counter_groups_present_count"], 3)
            self.assertEqual(report["source_counter_groups_missing_count"], 4)
            self.assertNotIn("bot_frame_cpu_ns", report["missing_instrumentation"])
            self.assertNotIn("route_query_cpu_ns", report["missing_instrumentation"])
            self.assertNotIn("q3a_route_cpu_ns", report["missing_instrumentation"])
            self.assertEqual(
                report["missing_instrumentation"],
                [
                    "q3a_memory_zone_active",
                    "aas_inpvs_checks",
                    "aas_trace_calls",
                    "entity_trace_attempts",
                ],
            )

            self.assertEqual(report["bot_frame_cpu_ms_per_sec"], 4.0)
            self.assertEqual(report["bot_frame_cpu_ms_per_bot_sec"], 2.0)
            self.assertEqual(report["bot_frame_cpu_avg_us"], 200.0)
            self.assertEqual(report["bot_frame_cpu_success_avg_us"], 200.0)
            self.assertEqual(report["route_query_cpu_ms_per_bot_sec"], 0.5)
            self.assertEqual(report["route_query_cpu_avg_us"], 400.0)
            self.assertEqual(report["route_query_cpu_fail_avg_us"], 200.0)
            self.assertEqual(report["q3a_route_cpu_ms_per_bot_sec"], 0.375)
            self.assertEqual(report["q3a_route_cpu_avg_us"], 300.0)
            self.assertEqual(report["q3a_route_cpu_fail_avg_us"], 100.0)

    def test_source_counter_marker_static_bsp_cpu_fields_are_grouped(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(
                pathlib.Path(temp),
                "source-counter-bsp-trace-cpu-marker.log",
                synthetic_log()
                + f"{perf.SOURCE_STATUS_MARKER} {BSP_TRACE_CPU_SOURCE_COUNTERS}\n",
            )

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertEqual(parsed.status["bsp_trace_cpu_ns"], 12000)
            self.assertEqual(parsed.status["bsp_trace_cpu_samples"], 6)
            self.assertEqual(report["source_counter_groups_present"], ["static_bsp_trace"])
            self.assertNotIn("aas_trace_calls", report["missing_instrumentation"])
            self.assertEqual(report["bsp_trace_calls_per_bot_sec"], 1.25)
            self.assertEqual(report["bsp_trace_cpu_avg_us"], 2.0)
            self.assertEqual(report["bsp_trace_cpu_max_us"], 5.0)

    def test_progress_duration_fallback_without_complete_line(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(
                pathlib.Path(temp),
                "progress-only.log",
                synthetic_log(elapsed_ms=None, progress_elapsed_ms=1500),
            )

            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            self.assertIsNone(parsed.soak_complete)
            self.assertEqual(perf.choose_duration_ms(parsed), 1500)
            self.assertEqual(report["duration_sec"], 1.5)

    def test_budget_pass_and_fail_function_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(pathlib.Path(temp), "smoke.log", synthetic_log())
            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)

            pass_budget = perf.Budget(
                path=pathlib.Path("pass-budget.json"),
                metrics={"commands_per_bot_sec": {"min": 5, "max": 20}},
                status={"route_failures": {"max": 0}},
            )
            fail_budget = perf.Budget(
                path=pathlib.Path("fail-budget.json"),
                metrics={"commands_per_bot_sec": {"min": 999}},
                status={},
            )

            self.assertTrue(perf.evaluate_budget(report, parsed.status, pass_budget)["pass"])

            failure = perf.evaluate_budget(report, parsed.status, fail_budget)
            self.assertFalse(failure["pass"])
            self.assertIn("commands_per_bot_sec", failure["failures"][0])

    def test_budget_missing_current_counter_diagnostics_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = self.write_text(pathlib.Path(temp), "smoke.log", synthetic_log())
            parsed = perf.parse_log(path)
            report = perf.analyze(parsed)
            budget = perf.Budget(
                path=pathlib.Path("source-budget.json"),
                metrics={
                    "bot_frame_cpu_ms_per_bot_sec": {"max": 5.0, "required": False},
                    "route_query_cpu_ms_per_bot_sec": {"max": 2.0},
                },
                status={
                    "visibility_decompress_failures": {"max": 0, "required": False},
                },
            )

            result = perf.evaluate_budget(report, parsed.status, budget)
            diagnostics = {
                item["metric"]: item
                for item in result["missing_current_counters"]
            }

            self.assertFalse(result["pass"])
            self.assertEqual(result["status"], "fail")
            self.assertEqual(result["pass_int"], 0)
            self.assertEqual(result["required_failed"], 1)
            self.assertEqual(result["optional_missing"], 2)
            self.assertEqual(result["missing_current_counter_count"], 3)
            self.assertEqual(
                diagnostics["bot_frame_cpu_ms_per_bot_sec"]["missing_current_counters"],
                ["bot_frame_cpu_ns", "bot_cpu_ns", "bot_frame_cpu_ms", "bot_cpu_ms"],
            )
            self.assertEqual(
                diagnostics["route_query_cpu_ms_per_bot_sec"]["group"],
                "route_query_cpu",
            )
            self.assertTrue(diagnostics["route_query_cpu_ms_per_bot_sec"]["required"])
            self.assertEqual(
                diagnostics["route_query_cpu_ms_per_bot_sec"]["missing_current_counters"],
                ["route_query_cpu_ns", "bot_route_cpu_ms"],
            )
            self.assertEqual(
                diagnostics["visibility_decompress_failures"]["missing_current_counters"],
                ["visibility_decompress_failures"],
            )

            perf.attach_budget_result(report, result)
            self.assertEqual(report["budget_status"], "fail")
            self.assertEqual(report["budget_pass_int"], 0)
            self.assertEqual(report["budget_required_failed"], 1)
            self.assertEqual(report["budget_optional_missing"], 2)
            self.assertEqual(report["budget_missing_current_counters"], 3)

    def test_budget_failure_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            log_path = self.write_text(root, "smoke.log", synthetic_log())
            budget_path = self.write_text(
                root,
                "budget.json",
                json.dumps({
                    "checks": {
                        "metrics": {
                            "commands_per_bot_sec": {"min": 999}
                        }
                    }
                }),
            )

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = perf.main(["--budget", str(budget_path), str(log_path)])

            self.assertEqual(exit_code, 1)
            self.assertIn("failure:", stdout.getvalue())

    def test_multi_run_comparison_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = perf.analyze(perf.parse_log(
                self.write_text(root, "first.log", synthetic_log(commands=40))
            ))
            second = perf.analyze(perf.parse_log(
                self.write_text(root, "second.log", synthetic_log(commands=20))
            ))

            first["budget"] = {"pass": True, "failures": [], "warnings": [], "checks": []}
            second["budget"] = {"pass": False, "failures": ["synthetic"], "warnings": [], "checks": []}

            comparison = perf.build_comparison([first, second])
            command_metric = next(
                metric for metric in comparison["metrics"]
                if metric["key"] == "commands_per_bot_sec"
            )

            self.assertEqual(comparison["run_count"], 2)
            self.assertEqual(comparison["latest_file"], second["file"])
            self.assertEqual(command_metric["best_run"], 1)
            self.assertEqual(command_metric["worst_run"], 2)
            self.assertEqual(comparison["budget"]["passed"], 1)
            self.assertEqual(comparison["budget"]["failed"], 1)
            self.assertFalse(comparison["budget"]["latest_pass"])

    def test_comparison_guards_mixed_scenarios(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = perf.analyze(
                perf.parse_log(self.write_text(root, "spawn.log", synthetic_log(bots=1))),
                {"name": "spawn_route_to_item", "duration_seconds": 1.0},
            )
            second = perf.analyze(
                perf.parse_log(self.write_text(root, "recover.log", synthetic_log(bots=2))),
                {"name": "recover_from_stall", "duration_seconds": 2.0},
            )

            comparison = perf.build_comparison([first, second])
            guards = {guard["code"]: guard for guard in comparison["guards"]}

            self.assertIn("mixed_scenarios", guards)
            self.assertIn("mixed_bot_counts", guards)
            self.assertEqual(
                guards["mixed_scenarios"]["values"],
                ["spawn_route_to_item", "recover_from_stall"],
            )
            self.assertEqual(guards["mixed_bot_counts"]["values"], ["1", "2"])

    def test_scenario_report_duration_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            log_path = self.write_text(root, "scenario.stdout.txt", synthetic_log())
            report_path = self.write_text(
                root,
                "latest_report.json",
                json.dumps({
                    "scenarios": [
                        {
                            "name": "synthetic_scenario",
                            "status": "passed",
                            "stdout_path": str(log_path),
                            "duration_seconds": 4.0,
                            "duration_budget_passed": True,
                            "returncode": 0,
                        }
                    ]
                }),
            )

            metadata = perf.load_scenario_report(report_path)
            parsed = perf.parse_log(log_path)
            analyzed = perf.analyze(parsed, metadata[perf.path_key(log_path)])

            self.assertEqual(analyzed["scenario_name"], "synthetic_scenario")
            self.assertEqual(analyzed["duration_source"], "scenario_report")
            self.assertEqual(analyzed["duration_sec"], 4.0)
            self.assertEqual(analyzed["commands_per_bot_sec"], 5.0)

    def test_real_soak_fixture_baseline_when_available(self) -> None:
        if not REAL_SOAK_FIXTURE.is_file():
            self.skipTest(f"optional soak fixture is missing: {REAL_SOAK_FIXTURE}")

        parsed = perf.parse_log(REAL_SOAK_FIXTURE)
        report = perf.analyze(parsed)
        budget = perf.load_budget(DEFAULT_BUDGET)
        budget_result = perf.evaluate_budget(report, parsed.status, budget)

        self.assertEqual(report["pass"], 1)
        self.assertEqual(report["route_failures"], 0)
        self.assertAlmostEqual(report["commands_per_bot_sec"], 40.0, delta=1.0)
        self.assertAlmostEqual(report["route_refresh_ratio"], 0.285, delta=0.02)
        self.assertTrue(budget_result["pass"])


if __name__ == "__main__":
    unittest.main()
