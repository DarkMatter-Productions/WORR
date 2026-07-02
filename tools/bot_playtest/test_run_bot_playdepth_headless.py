#!/usr/bin/env python3
"""Tests for the headless Duel/CTF play-depth runner."""

from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

import generate_bot_playtest
import run_bot_playdepth_headless as headless


def build_plan() -> dict:
    return generate_bot_playtest.build_payload(
        generate_bot_playtest.default_playtest_cases(),
        repo_root=pathlib.Path.cwd(),
        output_dir=pathlib.Path(".tmp") / "bot_playtest",
        base_game="basew",
        generated_at="2026-06-30T00:00:00Z",
    )


def case_by_id(plan: dict, case_id: str) -> dict:
    return next(case for case in plan["cases"] if case["id"] == case_id)


class BotPlayDepthHeadlessTests(unittest.TestCase):
    def test_build_command_runs_case_cvars_then_botlist_probe(self) -> None:
        plan = build_plan()
        case = case_by_id(plan, "duel_rotation")

        command = headless.build_command(
            binary=pathlib.Path(".install") / "worr_ded_x86_64.exe",
            install_dir=pathlib.Path(".install"),
            base_game="basew",
            port=28100,
            case=case,
            log_name="duel-proof",
            startup_wait=12,
            run_wait=34,
        )

        self.assertEqual(str(pathlib.Path(".install") / "worr_ded_x86_64.exe"), command[0])
        self.assertIn("+map", command)
        self.assertEqual("q2dm1", command[command.index("+map") + 1])
        self.assertIn("bot_min_players", command)
        self.assertIn("bot_duel_live_pacing", command)
        self.assertEqual(2, command.count("+botlist"))
        self.assertEqual("+quit", command[-1])

    def test_output_parsers_extract_botlist_names_and_profiles(self) -> None:
        text = "\n".join(
            [
                "Added bot B|Bulwark in slot 2.",
                "num state     name",
                "  0 spawned   B|Vanguard      ",
                "  1 spawned   B|Vector        ",
                "B|Vanguard^7 vs B|Vector^7",
            ]
        )

        self.assertEqual(["B|Bulwark", "B|Vanguard", "B|Vector"], headless.extract_bot_names(text))
        self.assertEqual(["vanguard", "vector", "bulwark"], headless.extract_profiles(text))
        self.assertGreaterEqual(len(headless.extract_botlist_lines(text)), 3)

    def test_clean_headless_results_create_pending_review_notes(self) -> None:
        plan = build_plan()
        result = {
            "id": "ctf_objectives",
            "failures": [],
            "botlist_lines": ["0: B|Vanguard profile=vanguard", "1: B|Relay profile=relay"],
            "bot_names": ["B|Vanguard", "B|Relay"],
            "profiles_observed": ["vanguard", "relay"],
            "stdout_path": ".tmp/bot_playtest/headless/ctf.stdout.txt",
            "stderr_path": ".tmp/bot_playtest/headless/ctf.stderr.txt",
        }

        notes = headless.build_notes_from_results(
            plan,
            [result],
            pathlib.Path(".tmp") / "bot_playtest" / "headless" / "bot_playdepth_headless_runs.json",
        )
        ctf_note = next(case for case in notes["cases"] if case["id"] == "ctf_objectives")

        self.assertEqual("pending", ctf_note["outcome"])
        self.assertIn("B|Vanguard", ctf_note["botlist"])
        self.assertEqual(["vanguard", "relay"], ctf_note["profiles_observed"])
        self.assertTrue(any("Headless command artifact" in step for step in ctf_note["repro_steps"]))

    def test_ctf_profile_coverage_requires_all_expected_profiles_when_target_allows(self) -> None:
        plan = build_plan()
        case = case_by_id(plan, "ctf_objectives")

        coverage = headless.case_profile_coverage(
            case,
            ["vanguard", "vector", "bulwark", "relay"],
        )

        self.assertFalse(coverage["passed"])
        self.assertTrue(coverage["required_all_expected"])
        self.assertEqual(["smoke"], coverage["missing_expected_profiles"])

    def test_duel_profile_coverage_allows_subset_because_target_is_two(self) -> None:
        plan = build_plan()
        case = case_by_id(plan, "duel_rotation")

        coverage = headless.case_profile_coverage(case, ["bulwark", "relay"])

        self.assertTrue(coverage["passed"])
        self.assertFalse(coverage["required_all_expected"])
        self.assertEqual(2, coverage["distinct_first_party_observed"])

    def test_failed_headless_results_block_review_notes(self) -> None:
        plan = build_plan()
        result = {
            "id": "duel_rotation",
            "failures": ["headless run returned 1"],
            "botlist_lines": [],
            "bot_names": [],
            "profiles_observed": [],
            "stdout_path": ".tmp/bot_playtest/headless/duel.stdout.txt",
            "stderr_path": ".tmp/bot_playtest/headless/duel.stderr.txt",
        }

        notes = headless.build_notes_from_results(
            plan,
            [result],
            pathlib.Path(".tmp") / "bot_playtest" / "headless" / "bot_playdepth_headless_runs.json",
        )
        duel_note = next(case for case in notes["cases"] if case["id"] == "duel_rotation")

        self.assertEqual("blocked", duel_note["outcome"])
        self.assertEqual(["headless run returned 1"], duel_note["custom_failure_signals"])

    def test_write_outputs_includes_artifact_paths_in_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output_dir = pathlib.Path(temp)
            report = headless.build_report(
                plan=build_plan(),
                results=[],
                missing_cases=["missing_case"],
                output_dir=output_dir,
            )
            notes = {"schema": "test", "cases": []}

            artifacts = headless.write_outputs(report, notes, output_dir)
            written = json.loads(pathlib.Path(artifacts["json"]).read_text())

            self.assertEqual(headless.SCHEMA, written["schema"])
            self.assertIn("artifacts", written)
            self.assertEqual(artifacts["notes"], written["artifacts"]["notes"])


if __name__ == "__main__":
    unittest.main()
