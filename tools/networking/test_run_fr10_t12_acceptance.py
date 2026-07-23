#!/usr/bin/env python3
"""Parser and semantic-contract tests for the FR-10-T12 partial parent gate."""

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = ROOT / "tools" / "networking" / "run_fr10_t12_acceptance.py"
CHILD_PATH = (
    ROOT / "tools" / "networking" /
    "run_canonical_rail_damage_runtime_gate.py"
)


def load_module(path: Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load test module {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RUNNER = load_module(RUNNER_PATH, "fr10_t12_acceptance_under_test")
CHILD = load_module(CHILD_PATH, "fr10_t12_canonical_child_under_test")


class FakeChild:
    SCHEMA = "test.canonical-child.v1"
    GATE_MODES = {
        "disruptor": {"weapon_policy": 6, "expected_damage": 45},
    }

    @staticmethod
    def validate_status(status: dict[str, Any], mode: dict[str, Any]) -> dict[str, Any]:
        if status.get("status") != "pass":
            raise RuntimeError("synthetic status did not pass")
        if status.get("weapon_policy") != mode["weapon_policy"]:
            raise RuntimeError("synthetic weapon policy changed")
        return status

    @staticmethod
    def determinism_signature(status: dict[str, Any]) -> tuple[Any, ...]:
        return (
            status["status"], status["weapon_policy"], status["semantic_value"]
        )


def client_command(
    executable: Path, home: Path, qport: int, name: str, port: int,
) -> list[str]:
    return [
        str(executable),
        "+set", "fs_homepath", str(home),
        "+set", "loc_language", "english",
        "+set", "win_headless", "1",
        "+set", "cl_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "s_enable", "0",
        "+set", "qport", str(qport),
        "+set", "name", name,
        "+connect", f"127.0.0.1:{port}",
    ]


def synthetic_report(
    isolated_root: Path, client_exe: Path, dedicated_exe: Path,
    *, port: int = 27960, semantic_value: int = 71,
) -> dict[str, Any]:
    status = {
        "status": "pass",
        "weapon_policy": 6,
        "semantic_value": semantic_value,
        "volatile_sample_us": 1000,
    }
    server_home = isolated_root / "child" / "runtime" / "server"
    shooter_home = isolated_root / "child" / "runtime" / "shooter"
    target_home = isolated_root / "child" / "runtime" / "target"
    run = {
        "status": copy.deepcopy(status),
        "server_stdout_sha256": "a" * 64,
        "shooter_stdout_sha256": "b" * 64,
        "target_stdout_sha256": "c" * 64,
        "shooter_terminated_by_gate": True,
        "target_terminated_by_gate": True,
        "spectator_terminated_by_gate": False,
        "server_terminated_by_gate": True,
        "logs": {"server.stdout": str(isolated_root / "volatile-server.log")},
    }
    return {
        "schema": FakeChild.SCHEMA,
        "run_id": "volatile-child-run",
        "started_at_utc": "2026-07-20T00:00:00+00:00",
        "completed_at_utc": "2026-07-20T00:00:01+00:00",
        "dedicated_command": [
            str(dedicated_exe),
            "+set", "fs_homepath", str(server_home),
            "+set", "maxclients", "2",
            "+set", "g_lag_compensation", "1",
            "+set", "net_port", str(port),
            "+map", "worr_fr10_rewind_mover",
        ],
        "shooter_command": client_command(
            client_exe, shooter_home, 11, "rewind_shooter", port
        ),
        "target_command": client_command(
            client_exe, target_home, 12, "rewind_target", port
        ),
        "spectator_command": None,
        "client_count": 2,
        "repeat": 3,
        "weapon": "disruptor",
        "weapon_policy": 6,
        "expected_damage": 45,
        "status": copy.deepcopy(status),
        "runs": [copy.deepcopy(run) for _ in range(3)],
    }


def all_summaries() -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for index, row in enumerate(RUNNER.SCENARIO_MANIFEST):
        signature = ["pass", row["weapon_policy"], index]
        result.append({
            "mode": row["mode"],
            "weapon_policy": row["weapon_policy"],
            "expected_damage": row["expected_damage"],
            "coverage": row["coverage"],
            "repeat": 3,
            "client_count": 2,
            "semantic_signature": signature,
            "semantic_sha256": RUNNER.semantic_sha256(signature),
            "all_launched_processes_terminated": True,
        })
    return result


class Fr10T12PartialAcceptanceTests(unittest.TestCase):
    def test_manifest_is_exact_and_matches_the_production_child(self) -> None:
        rows = RUNNER.validate_manifest(RUNNER.SCENARIO_MANIFEST, CHILD)
        self.assertEqual(len(rows), 39)
        self.assertEqual({row["weapon_policy"] for row in rows}, set(range(6, 25)))
        self.assertEqual(len(rows) * RUNNER.REPEAT, 117)
        by_name = {row["mode"]: row for row in rows}
        self.assertEqual(
            by_name["rocket-lifecycle-touch"]["rocket_lifecycle_policy"], 1
        )
        self.assertEqual(
            by_name["rocket-lifetime-expiry"]["rocket_lifecycle_policy"], 2
        )
        self.assertEqual(by_name["rocket-splash"]["splash_occlusion_policy"], 1)
        self.assertEqual(
            by_name["rocket-splash-bsp-occlusion"]["splash_occlusion_policy"],
            2,
        )
        self.assertEqual(
            by_name["rocket-splash-water-boundary"][
                "splash_occlusion_policy"
            ],
            3,
        )
        self.assertNotIn(
            "splash-occlusion-across-player-bsp-and-water-boundaries",
            RUNNER.OPEN_COVERAGE,
        )
        self.assertNotIn(
            "projectile-ownership-lifetime-and-collision-matrix",
            RUNNER.OPEN_COVERAGE,
        )
        self.assertIn(
            "remaining-projectile-family-ownership-lifetime-and-collision-matrix",
            RUNNER.OPEN_COVERAGE,
        )
        self.assertFalse(
            {row["mode"] for row in rows} & RUNNER.EXCLUDED_PREVIOUS_TASK_MODES
        )
        self.assertFalse(
            {row["mode"] for row in rows} & RUNNER.EXCLUDED_LOCAL_ACTION_MODES
        )
        document = RUNNER.manifest_document(rows)
        self.assertEqual(document["status"], "partial")
        self.assertEqual(document["mode_count"], 39)
        self.assertEqual(document["policy_count"], 19)
        self.assertEqual(document["live_repetitions"], 117)
        core = {key: value for key, value in document.items() if key != "sha256"}
        self.assertEqual(document["sha256"], RUNNER.semantic_sha256(core))

    def test_manifest_rejects_mode_policy_damage_and_repeat_drift(self) -> None:
        mutations = (
            ("mode", "unexpected-mode"),
            ("weapon_policy", 99),
            ("expected_damage", 999),
            ("require_damage", False),
        )
        for field, value in mutations:
            with self.subTest(field=field):
                rows = [dict(row) for row in RUNNER.SCENARIO_MANIFEST]
                rows[0][field] = value
                with self.assertRaises(ValueError):
                    RUNNER.validate_manifest(rows, CHILD)
        with self.assertRaises(ValueError):
            RUNNER.validate_manifest(RUNNER.SCENARIO_MANIFEST, CHILD, repeat=2)
        splash_rows = [dict(row) for row in RUNNER.SCENARIO_MANIFEST]
        splash_index = next(
            index for index, row in enumerate(splash_rows)
            if row["mode"] == "rocket-splash-bsp-occlusion"
        )
        splash_rows[splash_index]["splash_occlusion_policy"] = 3
        with self.assertRaises(ValueError):
            RUNNER.validate_manifest(splash_rows, CHILD)
        lifecycle_rows = [dict(row) for row in RUNNER.SCENARIO_MANIFEST]
        lifecycle_index = next(
            index for index, row in enumerate(lifecycle_rows)
            if row["mode"] == "rocket-lifetime-expiry"
        )
        lifecycle_rows[lifecycle_index]["rocket_lifecycle_policy"] = 1
        with self.assertRaises(ValueError):
            RUNNER.validate_manifest(lifecycle_rows, CHILD)

    def test_strict_json_rejects_duplicates_nonfinite_and_nonobject(self) -> None:
        self.assertEqual(RUNNER.strict_json(b'{"ok":1}', "test"), {"ok": 1})
        for data in (b'{"a":1,"a":2}', b'{"a":NaN}', b'[1,2,3]'):
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    RUNNER.strict_json(data, "test")

    def test_output_is_confined_to_tmp_networking_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            expected = (root / ".tmp" / "networking" / "evidence.json").resolve()
            self.assertEqual(
                RUNNER.require_networking_output(
                    root, Path(".tmp/networking/evidence.json")
                ),
                expected,
            )
            for invalid in (Path("evidence.json"), Path(".tmp/networking/evidence.txt")):
                with self.subTest(path=invalid):
                    with self.assertRaises(ValueError):
                        RUNNER.require_networking_output(root, invalid)

    def test_execution_bounds_are_exact_and_bounded(self) -> None:
        RUNNER.validate_execution_bounds(3, 35.0, 180.0, 27960)
        invalid = (
            (2, 35.0, 180.0, 27960),
            (3, 61.0, 180.0, 27960),
            (3, 35.0, 241.0, 27960),
            (3, 35.0, 180.0, 65535),
        )
        for values in invalid:
            with self.subTest(values=values):
                with self.assertRaises(ValueError):
                    RUNNER.validate_execution_bounds(*values)

    def test_child_command_pins_mode_repeat_timeout_port_and_output(self) -> None:
        command = RUNNER.build_child_command(
            Path("runner.py"), Path("client.exe"), Path("dedicated.exe"),
            Path("install"), Path("report.json"), "thunderbolt-discharge",
            27971, 35.0,
        )
        self.assertEqual(command[:2], [sys.executable, "runner.py"])
        self.assertEqual(command[command.index("--repeat") + 1], "3")
        self.assertEqual(command[command.index("--timeout") + 1], "35.0")
        self.assertEqual(command[command.index("--port") + 1], "27971")
        self.assertEqual(
            command[command.index("--weapon") + 1], "thunderbolt-discharge"
        )
        self.assertEqual(command[command.index("--output") + 1], "report.json")

    def test_child_timeout_is_enforced_by_process_termination(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            child = root / "slow_child.py"
            child.write_text(
                "import time\ntime.sleep(2.0)\n", encoding="utf-8"
            )
            started = time.monotonic()
            with self.assertRaisesRegex(RuntimeError, "bounded timeout"):
                RUNNER.run_child_checked(
                    [sys.executable, str(child)], root, 0.05
                )
            self.assertLess(time.monotonic() - started, 1.5)

    def test_runtime_provenance_binds_the_loaded_staged_closure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            install = root / ".install"
            base_game = install / "basew"
            maps = base_game / "maps"
            maps.mkdir(parents=True)
            runner = root / "tools" / "child.py"
            runner.parent.mkdir(parents=True)
            client = install / "worr_x86_64.exe"
            dedicated = install / "worr_ded_x86_64.exe"
            required = (
                runner, client, dedicated,
                install / "worr_engine_x86_64.dll",
                install / "worr_ded_engine_x86_64.dll",
                install / "worr_opengl_x86_64.dll",
                base_game / "cgame_x86_64.dll",
                base_game / "sgame_x86_64.dll",
                base_game / "pak0.pkz",
                maps / "worr_fr10_rewind_mover.bsp",
                base_game / "config.cfg",
            )
            for index, path in enumerate(required):
                path.write_bytes(f"artifact-{index}".encode())

            paths = RUNNER.runtime_artifact_paths(
                runner, client, dedicated, install
            )
            self.assertEqual(
                set(paths),
                {
                    "canonical_runner", "client_launcher",
                    "dedicated_launcher", "client_engine",
                    "dedicated_engine", "opengl_renderer", "cgame_module",
                    "sgame_module", "staged_assets", "fixture_map",
                    "base_config",
                },
            )
            before = {
                name: RUNNER.file_sha256(path) for name, path in paths.items()
            }
            (base_game / "sgame_x86_64.dll").write_bytes(b"drift")
            after = {
                name: RUNNER.file_sha256(path) for name, path in paths.items()
            }
            self.assertNotEqual(before, after)

    def test_child_report_requires_headless_semantic_and_termination_proofs(self) -> None:
        row = dict(RUNNER.SCENARIO_MANIFEST[0])
        with tempfile.TemporaryDirectory() as temporary:
            isolated_root = Path(temporary).resolve()
            client_exe = isolated_root / "client.exe"
            dedicated_exe = isolated_root / "dedicated.exe"
            report = synthetic_report(isolated_root, client_exe, dedicated_exe)
            summary = RUNNER.validate_child_report(
                report, row, FakeChild, client_exe, dedicated_exe,
                isolated_root, 27960,
            )
            self.assertEqual(summary["mode"], "disruptor")
            self.assertEqual(summary["repeat"], 3)
            self.assertTrue(summary["all_launched_processes_terminated"])

            mutations = []
            bad = copy.deepcopy(report)
            bad["repeat"] = 2
            mutations.append(bad)
            bad = copy.deepcopy(report)
            q = bad["shooter_command"]
            q[q.index("win_headless") + 1] = "0"
            mutations.append(bad)
            bad = copy.deepcopy(report)
            bad["runs"][0]["server_terminated_by_gate"] = False
            mutations.append(bad)
            bad = copy.deepcopy(report)
            bad["runs"][2]["status"]["semantic_value"] = 99
            mutations.append(bad)
            bad = copy.deepcopy(report)
            q = bad["target_command"]
            q[q.index("fs_homepath") + 1] = str(
                isolated_root.parent / "sibling-mode" / "target"
            )
            mutations.append(bad)
            for index, bad_report in enumerate(mutations):
                with self.subTest(mutation=index):
                    with self.assertRaises((RuntimeError, ValueError)):
                        RUNNER.validate_child_report(
                            bad_report, row, FakeChild, client_exe,
                            dedicated_exe, isolated_root, 27960,
                        )

    def test_normalized_child_semantics_ignore_volatile_samples_and_logs(self) -> None:
        row = dict(RUNNER.SCENARIO_MANIFEST[0])
        with tempfile.TemporaryDirectory() as temporary:
            isolated_root = Path(temporary).resolve()
            client_exe = isolated_root / "client.exe"
            dedicated_exe = isolated_root / "dedicated.exe"
            first = synthetic_report(isolated_root, client_exe, dedicated_exe)
            second = copy.deepcopy(first)
            second["run_id"] = "different-run"
            second["started_at_utc"] = "2030-01-01T00:00:00+00:00"
            for index, run in enumerate(second["runs"]):
                run["server_stdout_sha256"] = str(index) * 64
                run["logs"] = {"different": f"log-{index}"}
                run["status"]["volatile_sample_us"] += index + 100
            first_summary = RUNNER.validate_child_report(
                first, row, FakeChild, client_exe, dedicated_exe,
                isolated_root, 27960,
            )
            second_summary = RUNNER.validate_child_report(
                second, row, FakeChild, client_exe, dedicated_exe,
                isolated_root, 27960,
            )
            self.assertEqual(first_summary, second_summary)

    def test_parent_artifact_is_stably_hashed_and_cannot_claim_completion(self) -> None:
        manifest = RUNNER.manifest_document(RUNNER.SCENARIO_MANIFEST)
        summaries = all_summaries()
        components_a = [
            {"mode": row["mode"], "report": f"run-a/{row['mode']}.json",
             "semantic_sha256": summaries[index]["semantic_sha256"]}
            for index, row in enumerate(RUNNER.SCENARIO_MANIFEST)
        ]
        components_b = copy.deepcopy(components_a)
        for component in components_b:
            component["report"] = "run-b/" + component["report"].split("/")[-1]
        runtime = {
            "canonical_runner": {
                "path": "tools/networking/child.py", "sha256": "1" * 64,
            },
            "client_launcher": {
                "path": ".install/client.exe", "sha256": "2" * 64,
            },
            "dedicated_launcher": {
                "path": ".install/dedicated.exe", "sha256": "3" * 64,
            },
        }
        artifacts = {
            "runtime": runtime,
            "working_dir": ".install",
            "component_directory": ".tmp/networking/run-a",
        }
        first = RUNNER.build_evidence(
            run_id="run-a", started_at="one", completed_at="two",
            manifest=manifest, summaries=summaries, components=components_a,
            artifacts=artifacts,
        )
        artifacts_b = dict(artifacts)
        artifacts_b["component_directory"] = ".tmp/networking/run-b"
        second = RUNNER.build_evidence(
            run_id="run-b", started_at="three", completed_at="four",
            manifest=manifest, summaries=copy.deepcopy(summaries),
            components=components_b, artifacts=artifacts_b,
        )
        self.assertEqual(first["semantic_sha256"], second["semantic_sha256"])
        self.assertEqual(first["status"], "partial")
        self.assertEqual(first["coverage"]["proven_mode_count"], 39)
        self.assertEqual(first["coverage"]["live_repetitions"], 117)
        self.assertEqual(first["coverage"]["open"], list(RUNNER.OPEN_COVERAGE))
        self.assertFalse(first["gates"]["task_complete"])
        self.assertTrue(first["gates"]["explicit_partial_status"])
        self.assertFalse(first["gates"]["fresh_runtime_root_per_repetition"])
        self.assertTrue(first["gates"]["runtime_artifacts_stable"])

    def test_parent_rejects_missing_or_reordered_component_evidence(self) -> None:
        manifest = RUNNER.manifest_document(RUNNER.SCENARIO_MANIFEST)
        summaries = all_summaries()
        components = [
            {"mode": row["mode"]} for row in RUNNER.SCENARIO_MANIFEST
        ]
        artifacts = {
            "runtime": {
                "canonical_runner": {
                    "path": "tools/networking/child.py",
                    "sha256": "1" * 64,
                },
            },
        }
        with self.assertRaises(ValueError):
            RUNNER.build_evidence(
                run_id="x", started_at="x", completed_at="x",
                manifest=manifest, summaries=summaries[:-1],
                components=components[:-1], artifacts=artifacts,
            )
        components.reverse()
        with self.assertRaises(ValueError):
            RUNNER.build_evidence(
                run_id="x", started_at="x", completed_at="x",
                manifest=manifest, summaries=summaries,
                components=components, artifacts=artifacts,
            )


if __name__ == "__main__":
    unittest.main()
