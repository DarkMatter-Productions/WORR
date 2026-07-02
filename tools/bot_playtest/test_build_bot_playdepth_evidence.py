#!/usr/bin/env python3
"""Tests for Duel/CTF bot play-depth evidence attachments."""

from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

import build_bot_playdepth_evidence as evidence
import generate_bot_playtest
import triage_bot_playtest


def build_plan() -> dict:
    return generate_bot_playtest.build_payload(
        generate_bot_playtest.default_playtest_cases(),
        repo_root=pathlib.Path.cwd(),
        output_dir=pathlib.Path(".tmp") / "bot_playtest",
        base_game="basew",
        generated_at="2026-06-30T00:00:00Z",
    )


def notes_with_required_outcomes(*, duel: str = "pass", ctf: str = "pass") -> dict:
    plan = build_plan()
    notes = triage_bot_playtest.default_notes_from_plan(plan)
    for case in notes["cases"]:
        if case["id"] == "duel_rotation":
            case["outcome"] = duel
            case["botlist"] = "B|Vanguard\nB|Vector"
            case["profiles_observed"] = ["vanguard", "vector"]
            case["notes"] = "Duel active count held at two."
        if case["id"] == "ctf_objectives":
            case["outcome"] = ctf
            case["botlist"] = "B|Vanguard\nB|Vector\nB|Bulwark\nB|Relay\nB|Smoke\nB|Vanguard"
            case["profiles_observed"] = ["vanguard", "vector", "bulwark", "relay", "smoke"]
            case["notes"] = "Carrier support and dropped-flag response were observed."
    return notes


class BotPlayDepthEvidenceTests(unittest.TestCase):
    def test_required_duel_ctf_notes_pass_release_attachment(self) -> None:
        plan = build_plan()
        report = evidence.build_evidence(plan, notes_with_required_outcomes())

        self.assertEqual(evidence.SCHEMA, report["schema"])
        self.assertEqual("passed", report["summary"]["status"])
        self.assertEqual(2, report["summary"]["required_cases"])
        self.assertEqual(2, report["summary"]["passed"])
        self.assertEqual(2, report["summary"]["botlists_present"])

    def test_pending_required_case_keeps_attachment_pending(self) -> None:
        plan = build_plan()
        report = evidence.build_evidence(plan, notes_with_required_outcomes(ctf="pending"))

        self.assertEqual("pending", report["summary"]["status"])
        self.assertEqual(1, report["summary"]["pending"])

    def test_string_note_fields_are_preserved_as_single_entries(self) -> None:
        plan = build_plan()
        notes = notes_with_required_outcomes()
        for case in notes["cases"]:
            if case["id"] == "duel_rotation":
                case["profiles_observed"] = "vanguard"
                case["repro_steps"] = "Run the generated Duel config."

        report = evidence.build_evidence(plan, notes)
        duel = next(case for case in report["cases"] if case["id"] == "duel_rotation")

        self.assertEqual(["vanguard"], duel["profiles_observed"])
        self.assertEqual(["Run the generated Duel config."], duel["repro_steps"])

    def test_failed_required_case_promotes_triage_candidate(self) -> None:
        plan = build_plan()
        notes = notes_with_required_outcomes(duel="fail")
        for case in notes["cases"]:
            if case["id"] == "duel_rotation":
                case["failure_signals"] = [
                    "A bot ignores available weapons or armor while chasing a stronger opponent with the blaster."
                ]

        report = evidence.build_evidence(plan, notes)

        self.assertEqual("failed", report["summary"]["status"])
        self.assertEqual(1, report["summary"]["failed"])
        self.assertTrue(report["scenario_candidates"])

    def test_passed_ctf_case_without_expected_profiles_fails_attachment(self) -> None:
        plan = build_plan()
        notes = notes_with_required_outcomes()
        for case in notes["cases"]:
            if case["id"] == "ctf_objectives":
                case["profiles_observed"] = ["vanguard", "vector", "bulwark", "relay"]

        report = evidence.build_evidence(plan, notes)
        ctf = next(case for case in report["cases"] if case["id"] == "ctf_objectives")

        self.assertEqual("failed", report["summary"]["status"])
        self.assertEqual(["smoke"], ctf["missing_expected_profiles"])
        self.assertFalse(ctf["profile_coverage_passed"])

    def test_write_evidence_outputs_json_and_markdown(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output_dir = pathlib.Path(temp)
            report = evidence.build_evidence(build_plan(), notes_with_required_outcomes())
            artifacts = evidence.write_evidence(report, output_dir=output_dir)

            json_path = pathlib.Path(artifacts["json"])
            markdown_path = pathlib.Path(artifacts["markdown"])

            self.assertTrue(json_path.is_file())
            self.assertTrue(markdown_path.is_file())
            self.assertEqual(evidence.SCHEMA, json.loads(json_path.read_text())["schema"])
            self.assertIn("Duel/CTF Play-Depth Evidence", markdown_path.read_text())


if __name__ == "__main__":
    unittest.main()
