#!/usr/bin/env python3
"""Tests for the M3 multiplayer milestone gate."""

from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

import build_bot_playdepth_evidence
import check_m3_multiplayer_gate as gate
import generate_bot_playtest
import triage_bot_playtest


def scenario_report(*, fail_name: str | None = None) -> dict:
    rows = []
    for name in gate.REQUIRED_AUTOMATED_SCENARIOS:
        failed = name == fail_name
        rows.append(
            {
                "name": name,
                "status": "failed" if failed else "passed",
                "returncode": 1 if failed else 0,
                "duration_budget_passed": not failed,
                "failures": ["forced failure"] if failed else [],
            }
        )
    return {"scenarios": rows}


def playtest_plan() -> dict:
    return generate_bot_playtest.build_payload(
        generate_bot_playtest.default_playtest_cases(),
        repo_root=pathlib.Path.cwd(),
        output_dir=pathlib.Path(".tmp") / "bot_playtest",
        base_game="basew",
        generated_at="2026-06-30T00:00:00Z",
    )


def playdepth_evidence(*, duel: str = "pass", ctf: str = "pass") -> dict:
    plan = playtest_plan()
    notes = triage_bot_playtest.default_notes_from_plan(plan)
    for case in notes["cases"]:
        if case["id"] == "duel_rotation":
            case["outcome"] = duel
            case["botlist"] = "B|Vanguard\nB|Vector"
        elif case["id"] == "ctf_objectives":
            case["outcome"] = ctf
            case["botlist"] = "B|Vanguard\nB|Vector\nB|Bulwark\nB|Relay\nB|Smoke\nB|Vanguard"
    return build_bot_playdepth_evidence.build_evidence(plan, notes)


class M3MultiplayerGateTests(unittest.TestCase):
    def test_gate_passes_when_automated_and_playdepth_are_green(self) -> None:
        report = gate.evaluate_gate(scenario_report(), playdepth_evidence())

        self.assertEqual("passed", report["summary"]["status"])
        self.assertEqual(6, report["summary"]["automated_passed"])
        self.assertEqual(2, report["summary"]["playdepth_passed"])

    def test_gate_stays_pending_for_pending_playdepth_notes(self) -> None:
        report = gate.evaluate_gate(scenario_report(), playdepth_evidence(ctf="pending"))

        self.assertEqual("pending", report["summary"]["status"])
        self.assertEqual(1, report["summary"]["pending"])

    def test_gate_fails_when_required_automated_scenario_fails(self) -> None:
        report = gate.evaluate_gate(
            scenario_report(fail_name="duel_live_pacing"),
            playdepth_evidence(),
        )

        self.assertEqual("failed", report["summary"]["status"])
        self.assertTrue(any("duel_live_pacing" in failure for failure in report["failures"]))

    def test_write_report_outputs_json_and_markdown(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output_dir = pathlib.Path(temp)
            report = gate.evaluate_gate(scenario_report(), playdepth_evidence())
            artifacts = gate.write_report(report, output_dir=output_dir)

            json_path = pathlib.Path(artifacts["json"])
            markdown_path = pathlib.Path(artifacts["markdown"])

            self.assertTrue(json_path.is_file())
            self.assertTrue(markdown_path.is_file())
            self.assertEqual(gate.SCHEMA, json.loads(json_path.read_text())["schema"])
            self.assertIn("M3 Multiplayer Gate", markdown_path.read_text())


if __name__ == "__main__":
    unittest.main()
