#!/usr/bin/env python3
"""Tests for the WORR bot release acceptance runner."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import run_bot_acceptance as acceptance


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


def write_json(path: pathlib.Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def scenario_payload(
    *,
    total: int = 114,
    passed: int = 114,
    failed: int = 0,
    required: bool = True,
) -> dict:
    names = list(acceptance.REQUIRED_SCENARIOS) if required else ["spawn_route_to_item"]
    scenarios = [
        {
            "name": name,
            "selection_tags": ["movement"] if name.startswith("movement_") else [],
        }
        for name in names
    ]
    while len(scenarios) < total:
        scenarios.append({"name": f"filler_{len(scenarios)}", "selection_tags": []})
    return {
        "summary": {
            "total": total,
            "passed": passed,
            "failed": failed,
            "timeout": 0,
            "error": 0,
            "pending": 0,
            "overall": "pass" if failed == 0 else "fail",
        },
        "scenarios": scenarios,
    }


def movement_audit_payload(*, accepted: bool = True) -> dict:
    return {
        "schema": "worr-bot-movement-reference-gap-audit-v1",
        "status": "accepted" if accepted else "blocked",
        "summary": {
            "check_count": 2,
            "blocked": 0 if accepted else 1,
            "ready_for_promotion": 0,
            "accepted": 2 if accepted else 1,
        },
        "checks": [
            {
                "id": "natural_crouch",
                "status": "accepted" if accepted else "blocked_no_reference_content",
                "scenario": {"name": "movement_crouch_route"},
            },
            {
                "id": "hazard_context",
                "status": "accepted",
                "scenario": {"name": "movement_hazard_context"},
            },
        ],
    }


def write_required_user_docs(
    root: pathlib.Path,
    *,
    chat_events: bool = True,
    chat_links: bool = True,
) -> None:
    docs = root / "docs-user"
    docs.mkdir(parents=True, exist_ok=True)
    link = " See [Bot Chat](bot-chat.md)." if chat_links else ""
    (docs / "bots.md").write_text(
        "bot_min_players addbot bot_reload_profiles" + link,
        encoding="utf-8",
    )
    (docs / "bot-cvars.md").write_text(
        "bot_allow_chat bot_chat_live_events bot_chat_min_interval_ms "
        "bot_chat_team_only" + link,
        encoding="utf-8",
    )
    (docs / "bot-profiles.md").write_text(
        "chat_personality" + link,
        encoding="utf-8",
    )
    (docs / "bot-map-readiness.md").write_text("AAS map readiness", encoding="utf-8")
    (docs / "bot-playtest.md").write_text("playtest bots", encoding="utf-8")
    chat_tokens = " ".join(acceptance.REQUIRED_CHAT_DOC_CVARS)
    if chat_events:
        chat_tokens += " " + " ".join(acceptance.REQUIRED_CHAT_DOC_EVENTS)
    (docs / "bot-chat.md").write_text(chat_tokens, encoding="utf-8")


class BotAcceptanceUnitTests(unittest.TestCase):
    def test_scenario_report_passes_required_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            report = root / ".tmp" / "bot_scenarios" / "implemented.json"
            write_json(report, scenario_payload())

            result = acceptance.check_scenario_report(
                root,
                report,
                min_implemented_rows=114,
                allow_missing=False,
            )

        self.assertEqual("pass", result.status)
        self.assertEqual(114, result.metrics["total"])

    def test_scenario_report_accepts_supplemental_required_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            main = scenario_payload()
            main["scenarios"] = [
                row for row in main["scenarios"]
                if row["name"] != "movement_crouch_route"
            ]
            main["scenarios"].append({"name": "filler_replacement", "selection_tags": []})
            report = root / ".tmp" / "bot_scenarios" / "implemented.json"
            focused = root / ".tmp" / "bot_scenarios" / "movement_crouch_route.json"
            write_json(report, main)
            write_json(
                focused,
                {
                    "summary": {
                        "total": 1,
                        "passed": 1,
                        "failed": 0,
                        "timeout": 0,
                        "error": 0,
                        "pending": 0,
                        "overall": "pass",
                    },
                    "scenarios": [
                        {"name": "movement_crouch_route", "selection_tags": ["movement"]}
                    ],
                },
            )

            result = acceptance.check_scenario_report(
                root,
                report,
                min_implemented_rows=114,
                allow_missing=False,
            )

        self.assertEqual("pass", result.status)
        self.assertEqual(1, result.metrics["supplemental_reports"])

    def test_scenario_report_rejects_failed_supplemental_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            main = scenario_payload()
            main["scenarios"] = [
                row for row in main["scenarios"]
                if row["name"] != "movement_crouch_route"
            ]
            main["scenarios"].append({"name": "filler_replacement", "selection_tags": []})
            report = root / ".tmp" / "bot_scenarios" / "implemented.json"
            focused = root / ".tmp" / "bot_scenarios" / "movement_crouch_route_failed.json"
            write_json(report, main)
            write_json(
                focused,
                {
                    "catalog": [
                        {
                            "name": "movement_crouch_route",
                            "returncode": 1,
                            "duration_budget_passed": True,
                            "failures": ["route failed"],
                        }
                    ],
                },
            )

            result = acceptance.check_scenario_report(
                root,
                report,
                min_implemented_rows=114,
                allow_missing=False,
            )

        self.assertEqual("fail", result.status)
        self.assertIn("missing required scenario evidence: movement_crouch_route", result.failures)

    def test_scenario_report_fails_missing_required_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            report = root / ".tmp" / "bot_scenarios" / "small.json"
            write_json(report, scenario_payload(total=20, passed=20, required=False))

            result = acceptance.check_scenario_report(
                root,
                report,
                min_implemented_rows=114,
                allow_missing=False,
            )

        self.assertEqual("fail", result.status)
        self.assertTrue(any("expected at least 114" in failure for failure in result.failures))
        self.assertTrue(any("behavior_arbitration" in failure for failure in result.failures))

    def test_missing_scenario_report_can_warn(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)

            result = acceptance.check_scenario_report(
                root,
                None,
                min_implemented_rows=114,
                allow_missing=True,
            )

        self.assertEqual("warn", result.status)
        self.assertTrue(result.warnings)

    def test_movement_reference_audit_accepts_promoted_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            audit = root / ".tmp" / "bot_scenarios" / "movement_reference_gap_audit.json"
            write_json(audit, movement_audit_payload())

            result = acceptance.check_movement_reference_audit(root, audit)

        self.assertEqual("pass", result.status)
        self.assertEqual(2, result.metrics["accepted"])

    def test_movement_reference_audit_fails_blockers(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            audit = root / ".tmp" / "bot_scenarios" / "movement_reference_gap_audit.json"
            write_json(audit, movement_audit_payload(accepted=False))

            result = acceptance.check_movement_reference_audit(root, audit)

        self.assertEqual("fail", result.status)
        self.assertTrue(any("blocked" in failure for failure in result.failures))

    def test_bots_txt_requires_first_party_roster(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            bots_txt = root / "assets" / "botfiles" / "bots.txt"
            bots_txt.parent.mkdir(parents=True)
            bots_txt.write_text(
                """
                { name vanguard aifile bots/vanguard_c.c }
                { name vector aifile bots/vector_c.c }
                """,
                encoding="utf-8",
            )

            result = acceptance.check_bots_txt(root)

        self.assertEqual("fail", result.status)
        self.assertTrue(any("bulwark" in failure for failure in result.failures))

    def test_user_docs_require_chat_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_required_user_docs(root)

            result = acceptance.check_user_docs(root)

        self.assertEqual("pass", result.status)
        self.assertEqual(
            len(acceptance.REQUIRED_CHAT_DOC_EVENTS),
            result.metrics["chat_doc_required_events"],
        )

    def test_user_docs_reject_missing_chat_events_and_links(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_required_user_docs(root, chat_events=False, chat_links=False)

            result = acceptance.check_user_docs(root)

        self.assertEqual("fail", result.status)
        self.assertTrue(any("victory_defeat" in failure for failure in result.failures))
        self.assertTrue(any("docs-user/bots.md should link" in failure for failure in result.failures))

    def test_playtest_plan_covers_required_modes_and_min_players(self) -> None:
        result = acceptance.check_playtest_plan(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertEqual(4, result.metrics["cases"])
        self.assertEqual(4, result.metrics["modes"])

    def test_playtest_triage_classifies_playtest_failures(self) -> None:
        result = acceptance.check_playtest_triage(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertGreaterEqual(result.metrics["categories"], 7)
        self.assertGreaterEqual(result.metrics["case_failure_signals"], 10)

    def test_playdepth_evidence_tooling_builds_required_attachment(self) -> None:
        result = acceptance.check_playdepth_evidence_tooling(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertEqual(2, result.metrics["required_cases"])
        self.assertEqual(2, result.metrics["passed"])

    def test_m3_multiplayer_gate_tooling_builds_completion_gate(self) -> None:
        result = acceptance.check_m3_multiplayer_gate_tooling(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertEqual(6, result.metrics["automated_passed"])
        self.assertEqual(2, result.metrics["playdepth_passed"])

    def test_playdepth_headless_tooling_builds_review_notes(self) -> None:
        result = acceptance.check_playdepth_headless_tooling(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertEqual(2, result.metrics["cases"])
        self.assertEqual(2, result.metrics["passed"])
        self.assertEqual(2, result.metrics["profile_coverage_passed"])
        self.assertEqual(2, result.metrics["pending_review_notes"])

    def test_perf_tooling_validates_budget_artifacts(self) -> None:
        result = acceptance.check_perf_tooling(REPO_ROOT)

        self.assertEqual("pass", result.status)
        self.assertEqual(2, result.metrics["budgets"])
        self.assertEqual(1, result.metrics["variance_budgets"])

    def test_current_repo_acceptance_core_passes_with_existing_artifacts(self) -> None:
        report = acceptance.run_acceptance(
            REPO_ROOT,
            scenario_report=REPO_ROOT / ".tmp" / "bot_scenarios" / "implemented_hazard_context.json",
            min_implemented_rows=114,
        )

        self.assertEqual("passed", report["summary"]["status"])


if __name__ == "__main__":
    unittest.main()
