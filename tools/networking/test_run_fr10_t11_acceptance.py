#!/usr/bin/env python3
"""Parser and orchestration contracts for the FR-10-T11 parent gate."""

from __future__ import annotations

import copy
import importlib.util
import json
import unittest
from argparse import Namespace
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/run_fr10_t11_acceptance.py"
SPEC = importlib.util.spec_from_file_location("fr10_t11_acceptance", SCRIPT)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)

MANIFEST_PATH = (
    ROOT / "tools/networking/scenarios/fr10_t11_acceptance_manifest.json"
)
MATRIX_PATH = ROOT / "tools/networking/scenarios/rewind_player_acceptance_matrix.json"


def manifest() -> dict[str, object]:
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def scenario(mode: str) -> dict[str, object]:
    return next(
        row for row in manifest()["canonical"]["scenarios"]
        if row["mode"] == mode
    )


def client_command(executable: Path, qport: int) -> list[str]:
    return [
        str(executable),
        "+set", "loc_language", "english",
        "+set", "win_headless", "1",
        "+set", "cl_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "s_enable", "0",
        "+set", "qport", str(qport),
        "+connect", "127.0.0.1:27960",
    ]


def canonical_status(row: dict[str, object]) -> dict[str, object]:
    status = {field: 0 for field in GATE.CANONICAL_SEMANTIC_FIELDS}
    status.update(manifest()["canonical"]["common_status"])
    status.update({
        "target_history_captures": 6,
        "applied_age_us": 50_000,
        "observation_weapon_policy": row["weapon_policy"],
        "expected_damage": row["fixture_expected_damage"],
        "observed_damage": row["observed_damage"],
        "damage_applied": (
            0 if row["mode"] == "railgun-mover-occlusion" else 1
        ),
    })
    for field in row.get("required_true", []):
        status[field] = 1
    for field, minimum in row.get("minimums", {}).items():
        status[field] = minimum
    return status


def canonical_report(
    row: dict[str, object], client: Path, dedicated: Path
) -> dict[str, object]:
    status = canonical_status(row)
    spectator = {
        "required": row.get("require_spectator_exclusion") is True,
        "roster_size": row["client_count"],
        "team_verified_before_fire": row.get("require_spectator_exclusion") is True,
        "team_verified_after_fire": row.get("require_spectator_exclusion") is True,
        "spectator_undamaged": row.get("require_spectator_exclusion") is True,
    }
    run = {"status": status, "spectator_exclusion": spectator}
    return {
        "schema": manifest()["canonical"]["schema"],
        "weapon": row["mode"],
        "weapon_policy": row["weapon_policy"],
        "expected_damage": row["fixture_expected_damage"],
        "repeat": 3,
        "client_count": row["client_count"],
        "dedicated_command": [
            str(dedicated), "+set", "maxclients", str(row["client_count"]),
            "+set", "g_lag_compensation", "1",
        ],
        "shooter_command": client_command(client, 101),
        "target_command": client_command(client, 102),
        "spectator_command": (
            client_command(client, 103) if row["client_count"] == 3 else None
        ),
        "status": copy.deepcopy(status),
        "runs": [copy.deepcopy(run) for _ in range(3)],
    }


def rewind_reports() -> tuple[dict[str, object], dict[str, object]]:
    case_ids = [
        f"normal/{weapon}/{latency}ms"
        for weapon in (
            "machinegun", "chaingun", "shotgun", "super-shotgun",
            "railgun", "disruptor-convergence", "plasma-beam", "thunderbolt",
        )
        for latency in (0, 50, 100, 200)
    ] + [f"boundary/{name}" for name in GATE.MATRIX_BOUNDARIES]
    cases = []
    disabled = manifest()["matrix"]["disabled_opt_out_case"]
    for case_id in case_ids:
        report = {"pass": True, "authoritative_unchanged": True}
        if case_id == "boundary/disabled":
            report.update({key: value for key, value in disabled.items() if key != "case_id"})
        cases.append({
            "case_id": case_id,
            "deterministic": True,
            "repeat_digests": [case_id, case_id, case_id],
            "report": report,
        })
    evidence = {
        "schema": manifest()["matrix"]["evidence_schema"],
        "overall_result": "pass",
        "workload": {"case_count": 40, "weapon_policy_count": 8, "repeat": 3},
        "measurements": {
            "invocations": 120,
            "determinism_mismatches": 0,
            "authoritative_mutations": 0,
            "failed_assertions": 0,
        },
        "gates": {
            "matrix_complete": True,
            "production_weapon_route_tags_present": True,
            "repeat_determinism": True,
            "authoritative_state_immutable": True,
            "expected_outcomes": True,
        },
        "scenario_manifest": {"sha256": GATE.sha256_bytes(MATRIX_PATH.read_bytes())},
    }
    raw = {
        "schema": manifest()["matrix"]["raw_schema"],
        "repeat": 3,
        "cases": cases,
    }
    return evidence, raw


def fallback_report(dedicated: Path) -> dict[str, object]:
    status = {
        "status": "pass",
        "setup_ready": 1,
        "history_ready": 1,
        "current_world_miss": 1,
        "rejected_current_fallback": 1,
        "rejected_no_damage": 1,
        "legacy_rewind_selected": 1,
        "rail_policy_observed": 1,
        "near_latency_hit": 1,
        "bounded_latency_hit": 1,
        "capped_latency_hit": 1,
        "damage_applied": 1,
        "geometry_unchanged": 1,
        "query_authority_unchanged": 1,
        "candidate_count": 2,
        "damage_amount": 30,
        "current_fraction_q6": 1_000_000,
        "near_latency_fraction_q6": 700_000,
        "bounded_latency_fraction_q6": 600_000,
        "capped_latency_fraction_q6": 500_000,
        "failure_code": 0,
    }
    return {
        "schema": manifest()["fallback"]["schema"],
        "repeat": 3,
        "command": [str(dedicated), "+set", "g_lag_compensation", "1"],
        "status": copy.deepcopy(status),
        "runs": [{"status": copy.deepcopy(status)} for _ in range(3)],
    }


class Fr10T11AcceptanceTests(unittest.TestCase):
    def test_run_checked_retries_only_empty_windows_pre_evidence_exit(self) -> None:
        failed = mock.Mock(returncode=0xffffffff, stdout=b"", stderr=b"")
        passed = mock.Mock(returncode=0, stdout=b"done", stderr=b"")
        with (
            mock.patch.object(GATE.os, "name", "nt"),
            mock.patch.object(GATE.subprocess, "run", side_effect=(failed, passed)) as run,
            mock.patch.object(GATE.time, "sleep") as sleep,
        ):
            retries = GATE.run_checked(
                [GATE.sys.executable, "runner.py"], ROOT, 180
            )
        self.assertEqual(retries, 1)
        self.assertEqual(run.call_count, 2)
        sleep.assert_called_once_with(0.1)

    def test_run_checked_does_not_retry_diagnostic_failure(self) -> None:
        failed = mock.Mock(returncode=0xffffffff, stdout=b"", stderr=b"boom")
        with (
            mock.patch.object(GATE.os, "name", "nt"),
            mock.patch.object(GATE.subprocess, "run", return_value=failed) as run,
            self.assertRaisesRegex(RuntimeError, "boom"),
        ):
            GATE.run_checked([GATE.sys.executable, "runner.py"], ROOT, 180)
        run.assert_called_once()

    def test_in_process_runner_preserves_cli_and_stderr_contract(self) -> None:
        module = mock.Mock()
        module.main.return_value = 0
        command = [GATE.sys.executable, "runner.py", "--mode", "railgun"]
        GATE.run_python_main_checked(module, command, 180)
        module.main.assert_called_once_with(["--mode", "railgun"])

        module.main.reset_mock()
        module.main.side_effect = lambda _argv: print("bad", file=GATE.sys.stderr) or 0
        with self.assertRaisesRegex(RuntimeError, "emitted stderr"):
            GATE.run_python_main_checked(module, command, 180)

    def test_checked_in_manifest_is_the_exact_bounded_contract(self) -> None:
        checked = GATE.validate_manifest(manifest())
        self.assertEqual(checked["repeat"], 3)
        self.assertEqual(checked["bounds"]["matrix_invocations"], 120)
        self.assertEqual(checked["bounds"]["total_live_repetitions"], 36)

    def test_manifest_rejects_repeat_or_policy_drift(self) -> None:
        changed = manifest()
        changed["repeat"] = 4
        with self.assertRaisesRegex(ValueError, "must equal|exactly three"):
            GATE.validate_manifest(changed)
        changed = manifest()
        changed["canonical"]["scenarios"][0]["observed_damage"] = 7
        with self.assertRaisesRegex(ValueError, "eight supported"):
            GATE.validate_manifest(changed)

    def test_manifest_requires_both_live_fairness_modes(self) -> None:
        changed = manifest()
        row = next(
            item for item in changed["canonical"]["scenarios"]
            if item["mode"] == "railgun-spawn-protection"
        )
        row["mode"] = "railgun-other"
        with self.assertRaisesRegex(ValueError, "fairness mode set"):
            GATE.validate_manifest(changed)

    def test_strict_json_rejects_duplicate_keys_and_non_finite_values(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            GATE.strict_json(b'{"a":1,"a":2}', "duplicate")
        with self.assertRaisesRegex(ValueError, "non-finite"):
            GATE.strict_json(b'{"a":NaN}', "nan")

    def test_parent_output_is_confined_to_tmp_networking(self) -> None:
        expected = GATE.require_networking_output(
            ROOT, Path(".tmp/networking/fr10_t11_acceptance.json")
        )
        self.assertEqual(expected, ROOT / ".tmp/networking/fr10_t11_acceptance.json")
        with self.assertRaisesRegex(ValueError, "under .tmp/networking"):
            GATE.require_networking_output(ROOT, Path("outside.json"))

    def test_checked_in_matrix_retains_all_40_case_inputs(self) -> None:
        matrix = GATE.load_json(MATRIX_PATH, "matrix")
        GATE.validate_matrix_source(matrix, manifest())
        changed = copy.deepcopy(matrix)
        changed["normal_latency_ms"] = [0, 50, 100]
        with self.assertRaisesRegex(ValueError, "latency classes"):
            GATE.validate_matrix_source(changed, manifest())

    def test_source_fixture_preflight_fails_closed_on_missing_token(self) -> None:
        changed = manifest()
        changed["required_source_tokens"] = [{
            "mode": "missing",
            "path": "tools/networking/run_fr10_t11_acceptance.py",
            "tokens": ["this-token-cannot-exist-in-the-runner"],
        }]
        with self.assertRaisesRegex(RuntimeError, "fixture is unavailable"):
            GATE.validate_required_source_tokens(ROOT, changed)

    def test_child_commands_pin_repeat_timeout_port_and_mode(self) -> None:
        row = scenario("railgun-spectator-exclusion")
        command = GATE.build_canonical_command(
            SCRIPT, ROOT / "client.exe", ROOT / "dedicated.exe", ROOT,
            ROOT / ".tmp/networking/report.json", row, 3, 45, 27969,
        )
        self.assertEqual(command[command.index("--weapon") + 1], row["mode"])
        self.assertEqual(command[command.index("--repeat") + 1], "3")
        self.assertEqual(command[command.index("--timeout") + 1], "45")
        self.assertEqual(command[command.index("--port") + 1], "27969")

    def test_canonical_policy_report_requires_exact_damage_and_semantics(self) -> None:
        row = scenario("shotgun")
        client, dedicated = ROOT / "client.exe", ROOT / "dedicated.exe"
        report = canonical_report(row, client, dedicated)
        summary = GATE.validate_canonical_report(
            report, row, manifest(), client.resolve(), dedicated.resolve()
        )
        self.assertEqual(summary["observed_damage"], 48)
        changed = copy.deepcopy(report)
        changed["runs"][1]["status"]["observed_damage"] = 44
        with self.assertRaisesRegex(ValueError, "exact damage"):
            GATE.validate_canonical_report(
                changed, row, manifest(), client.resolve(), dedicated.resolve()
            )

    def test_canonical_report_rejects_interactive_client_policy(self) -> None:
        row = scenario("railgun")
        client, dedicated = ROOT / "client.exe", ROOT / "dedicated.exe"
        report = canonical_report(row, client, dedicated)
        command = report["shooter_command"]
        command[command.index("in_enable") + 1] = "1"
        with self.assertRaisesRegex(ValueError, "headless in_enable"):
            GATE.validate_canonical_report(
                report, row, manifest(), client.resolve(), dedicated.resolve()
            )

    def test_canonical_report_rejects_duplicate_client_qports(self) -> None:
        row = scenario("railgun-spectator-exclusion")
        client, dedicated = ROOT / "client.exe", ROOT / "dedicated.exe"
        report = canonical_report(row, client, dedicated)
        target = report["target_command"]
        target[target.index("qport") + 1] = "101"
        with self.assertRaisesRegex(ValueError, "distinct qports"):
            GATE.validate_canonical_report(
                report, row, manifest(), client.resolve(), dedicated.resolve()
            )

    def test_canonical_report_rejects_qport_after_connect(self) -> None:
        row = scenario("railgun")
        client, dedicated = ROOT / "client.exe", ROOT / "dedicated.exe"
        report = canonical_report(row, client, dedicated)
        shooter = report["shooter_command"]
        qport = shooter[shooter.index("qport") - 1:shooter.index("qport") + 2]
        del shooter[shooter.index("qport") - 1:shooter.index("qport") + 2]
        shooter.extend(qport)
        with self.assertRaisesRegex(ValueError, "before \+connect"):
            GATE.validate_canonical_report(
                report, row, manifest(), client.resolve(), dedicated.resolve()
            )

    def test_spectator_report_requires_three_clients_and_both_team_latches(self) -> None:
        row = scenario("railgun-spectator-exclusion")
        client, dedicated = ROOT / "client.exe", ROOT / "dedicated.exe"
        report = canonical_report(row, client, dedicated)
        GATE.validate_canonical_report(
            report, row, manifest(), client.resolve(), dedicated.resolve()
        )
        report["runs"][2]["spectator_exclusion"]["team_verified_after_fire"] = False
        with self.assertRaisesRegex(ValueError, "spectator exclusion proof"):
            GATE.validate_canonical_report(
                report, row, manifest(), client.resolve(), dedicated.resolve()
            )

    def test_spawn_protection_uses_zero_damage_range_pass_bit(self) -> None:
        row = scenario("railgun-spawn-protection")
        status = canonical_status(row)
        self.assertEqual(status["observed_damage"], 0)
        self.assertEqual(status["damage_applied"], 1)
        GATE.validate_canonical_status(status, row, manifest())
        status["damage_applied"] = 0
        with self.assertRaisesRegex(ValueError, "damage-applied"):
            GATE.validate_canonical_status(status, row, manifest())

    def test_mover_report_requires_zero_damage_and_all_occlusion_proofs(self) -> None:
        row = scenario("railgun-mover-occlusion")
        status = canonical_status(row)
        GATE.validate_canonical_status(status, row, manifest())
        status["historical_mover_occlusion_observed"] = 0
        with self.assertRaisesRegex(ValueError, "required proof"):
            GATE.validate_canonical_status(status, row, manifest())

    def test_matrix_evidence_requires_disabled_opt_out_and_no_mutation(self) -> None:
        evidence, raw = rewind_reports()
        summary = GATE.validate_rewind_evidence(
            evidence, raw, MATRIX_PATH, manifest()
        )
        self.assertEqual(summary["case_count"], 40)
        self.assertEqual(summary["disabled_opt_out"]["fallback_reason"], 1)
        changed = copy.deepcopy(raw)
        disabled = next(
            item for item in changed["cases"]
            if item["case_id"] == "boundary/disabled"
        )
        disabled["report"]["fallback_reason"] = 0
        with self.assertRaisesRegex(ValueError, "disabled/opt-out"):
            GATE.validate_rewind_evidence(
                evidence, changed, MATRIX_PATH, manifest()
            )

    def test_matrix_evidence_rejects_repeat_or_authority_drift(self) -> None:
        evidence, raw = rewind_reports()
        raw["cases"][0]["repeat_digests"][2] = "different"
        with self.assertRaisesRegex(ValueError, "repeats diverged"):
            GATE.validate_rewind_evidence(
                evidence, raw, MATRIX_PATH, manifest()
            )
        evidence, raw = rewind_reports()
        raw["cases"][0]["report"]["authoritative_unchanged"] = False
        with self.assertRaisesRegex(ValueError, "authority proof"):
            GATE.validate_rewind_evidence(
                evidence, raw, MATRIX_PATH, manifest()
            )

    def test_fallback_report_requires_invalid_ack_and_three_exact_hits(self) -> None:
        dedicated = (ROOT / "dedicated.exe").resolve()
        summary = GATE.validate_fallback_report(
            fallback_report(dedicated), manifest(), dedicated
        )
        self.assertTrue(summary["invalid_ack_current_fallback"])
        self.assertEqual(summary["invalid_ack_damage"], 0)
        self.assertEqual(summary["damage_amount"], 30)

    def test_fallback_report_rejects_damage_or_semantic_repeat_drift(self) -> None:
        dedicated = (ROOT / "dedicated.exe").resolve()
        report = fallback_report(dedicated)
        report["runs"][1]["status"]["damage_amount"] = 20
        with self.assertRaisesRegex(ValueError, "repeat output diverged"):
            GATE.validate_fallback_report(report, manifest(), dedicated)
        report = fallback_report(dedicated)
        report["status"]["damage_amount"] = 20
        for run in report["runs"]:
            run["status"]["damage_amount"] = 20
        with self.assertRaisesRegex(ValueError, "exact damage"):
            GATE.validate_fallback_report(report, manifest(), dedicated)

    def test_required_source_mode_is_deliberately_manifested(self) -> None:
        rows = manifest()["required_source_tokens"]
        self.assertEqual({row["mode"] for row in rows}, {"railgun-spawn-protection"})
        tokens = {token for row in rows for token in row["tokens"]}
        self.assertIn("worr_rewind_canonical_rail_spawn_protection_arm", tokens)
        self.assertIn("sg_worr_rewind_canonical_rail_spawn_protection_status", tokens)


if __name__ == "__main__":
    unittest.main()
