#!/usr/bin/env python3
"""Focused fail-closed tests for the production-corpus qualifier."""

from __future__ import annotations

import copy
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

try:
    from tools.networking import run_native_snapshot_production_corpus as runner
except ModuleNotFoundError:
    import run_native_snapshot_production_corpus as runner


def valid_manifest() -> dict[str, object]:
    return copy.deepcopy(runner.load_json(runner.DEFAULT_MANIFEST))


def valid_result(manifest: dict[str, object]) -> dict[str, object]:
    requested = runner.EXPECTED_SNAPSHOTS
    coverage = {field: 1 for field in runner.COVERAGE_FIELDS}
    coverage.update(
        {
            "serialized_frames": requested + runner.EXPECTED_NEGATIVE_PROBES,
            "acknowledged_frames": requested,
            "released_frames": requested,
            "prediction_authorities": requested,
            "resolved_prediction_commands": requested + 1,
            "prediction_pending_ranges": 33_334,
            "prediction_nonpending_ranges": 66_666,
            "prediction_max_replay": runner.EXPECTED_PREDICTION_MAX_REPLAY,
            "prediction_bootstrap_ranges": runner.EXPECTED_EPOCHS,
            "prediction_nonzero_cursor_ranges": (
                requested - runner.EXPECTED_EPOCHS
            ),
            "prediction_limit_ranges": runner.EXPECTED_PREDICTION_LIMIT_RANGES,
            "prediction_range_exhaustion_rejections": (
                runner.EXPECTED_PREDICTION_EXHAUSTION_REJECTIONS
            ),
            "negative_probe_frames": runner.EXPECTED_NEGATIVE_PROBES,
            "corrupt_ack_probe_acceptances": 1,
            "corrupt_ack_probe_prediction_authorities": 1,
            "epochs": runner.EXPECTED_EPOCHS,
            "connection_activations": runner.EXPECTED_EPOCHS,
            "corrupt_server_to_client": 2,
            "corrupt_client_to_server": 1,
            "corrupt_rejections": runner.EXPECTED_NEGATIVE_PROBES,
            "exact_once_checks": requested,
            "premature_release_checks": requested,
            "retained_until_epoch_reset": runner.EXPECTED_NEGATIVE_PROBES,
        }
    )
    return {
        "schema": runner.EXPECTED_RESULT_SCHEMA,
        "classification": runner.EXPECTED_CLASSIFICATION,
        "status": runner.EXPECTED_STATUS,
        "requested_frames": requested,
        "accepted_frames": requested,
        "seed": manifest["seed"],
        "corpus_digest": manifest["golden_corpus_digest"],
        "coverage": coverage,
    }


class ProductionCorpusQualifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        runner.TMP_ROOT.mkdir(parents=True, exist_ok=True)

    def test_manifest_requires_a_real_lowercase_golden_digest(self) -> None:
        invalid_values = (
            None,
            "",
            "PENDING",
            "TO_BE_FILLED",
            "C6AEE48DF85341AB",
            "0123456789abcde",
        )
        for value in invalid_values:
            with self.subTest(value=value):
                manifest = valid_manifest()
                manifest["golden_corpus_digest"] = value
                with self.assertRaisesRegex(ValueError, "lowercase 16-digit"):
                    runner.validate_manifest(manifest)

    def test_valid_manifest_is_accepted(self) -> None:
        runner.validate_manifest(valid_manifest())

    def test_strict_json_rejects_duplicate_nonfinite_and_trailing_input(self) -> None:
        invalid_documents = (
            '{"member":1,"member":2}',
            '{"member":NaN}',
            '{"member":Infinity}',
            '{"member":1}{"trailing":2}',
        )
        for document in invalid_documents:
            with self.subTest(document=document):
                with self.assertRaises(ValueError):
                    runner.parse_strict_json(document, "test input")

    def test_exact_result_contract_is_accepted(self) -> None:
        manifest = valid_manifest()
        self.assertEqual(
            runner.validate_result(valid_result(manifest), manifest),
            {"negative_probes": 3, "accepted_abandonment": 0},
        )

    def test_mutated_result_counters_fail_closed(self) -> None:
        manifest = valid_manifest()
        mutations = {
            "acknowledged_frames": runner.EXPECTED_SNAPSHOTS - 1,
            "resolved_prediction_commands": runner.EXPECTED_SNAPSHOTS,
            "prediction_pending_ranges": 33_333,
            "prediction_max_replay": 126,
            "prediction_bootstrap_ranges": 3,
            "prediction_limit_ranges": 3,
            "prediction_range_exhaustion_rejections": 0,
            "serialized_frames": runner.EXPECTED_SNAPSHOTS + 2,
            "corrupt_rejections": 2,
            "server_to_client_fragment_release_inversions": 0,
            "retained_until_epoch_reset": 2,
        }
        for field, value in mutations.items():
            with self.subTest(field=field):
                result = valid_result(manifest)
                result["coverage"][field] = value  # type: ignore[index]
                with self.assertRaises(ValueError):
                    runner.validate_result(result, manifest)

    def test_evidence_path_is_confined_below_tmp(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="corpus-runner-path-", dir=runner.TMP_ROOT
        ) as directory:
            expected = Path(directory) / "nested" / "evidence.json"
            self.assertEqual(runner.resolve_evidence_path(expected), expected)
        with self.assertRaisesRegex(ValueError, "must remain under"):
            runner.resolve_evidence_path(runner.REPO_ROOT / "evidence.json")
        with self.assertRaisesRegex(ValueError, "must name a file"):
            runner.resolve_evidence_path(runner.TMP_ROOT)

    def test_evidence_target_rejects_directories_and_links(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="corpus-runner-safety-", dir=runner.TMP_ROOT
        ) as directory:
            base = Path(directory)
            directory_target = base / "evidence-directory"
            directory_target.mkdir()
            with self.assertRaisesRegex(ValueError, "regular file"):
                runner.prepare_evidence_path(directory_target)

            non_directory_parent = base / "not-a-directory"
            non_directory_parent.write_text("occupied\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "component must be a directory"):
                runner.prepare_evidence_path(
                    non_directory_parent / "evidence.json"
                )

            source = base / "source.json"
            source.write_text("{}\n", encoding="utf-8")
            link = base / "evidence-link.json"
            try:
                link.symlink_to(source)
            except (NotImplementedError, OSError):
                return
            with self.assertRaisesRegex(ValueError, "must not traverse a link"):
                runner.prepare_evidence_path(link)

    def test_stale_evidence_is_removed_before_mocked_run_failure(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="corpus-runner-stale-", dir=runner.TMP_ROOT
        ) as directory:
            base = Path(directory)
            executable = base / "native_snapshot_production_corpus_test.exe"
            executable.write_bytes(b"unit-test placeholder")
            evidence = base / "evidence.json"
            evidence.write_text('{"status":"ok"}\n', encoding="utf-8")
            arguments = [
                "run_native_snapshot_production_corpus.py",
                "--corpus-exe",
                str(executable),
                "--manifest",
                str(runner.DEFAULT_MANIFEST),
                "--evidence",
                str(evidence),
            ]
            with mock.patch.object(sys, "argv", arguments), mock.patch.object(
                runner, "run_corpus", side_effect=RuntimeError("mocked failure")
            ) as launch:
                with self.assertRaisesRegex(RuntimeError, "mocked failure"):
                    runner.main()
            launch.assert_called_once()
            self.assertFalse(evidence.exists())

    def test_atomic_write_replaces_in_same_directory_without_residue(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="corpus-runner-atomic-", dir=runner.TMP_ROOT
        ) as directory:
            target = Path(directory) / "evidence.json"
            target.write_text("stale\n", encoding="utf-8")
            real_replace = os.replace
            replacements: list[tuple[Path, Path]] = []

            def checked_replace(
                source: os.PathLike[str], destination: os.PathLike[str]
            ) -> None:
                source_path = Path(source)
                destination_path = Path(destination)
                self.assertEqual(source_path.parent, destination_path.parent)
                self.assertTrue(source_path.is_file())
                self.assertEqual(target.read_text(encoding="utf-8"), "stale\n")
                replacements.append((source_path, destination_path))
                real_replace(source_path, destination_path)

            with mock.patch.object(runner.os, "replace", side_effect=checked_replace):
                runner.write_json(target, {"status": "ok"})

            self.assertEqual(len(replacements), 1)
            self.assertEqual(
                json.loads(target.read_text(encoding="utf-8")),
                {"status": "ok"},
            )
            self.assertEqual(list(target.parent.glob(f".{target.name}.*.tmp")), [])

    def test_atomic_write_failure_preserves_target_and_cleans_temp(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="corpus-runner-atomic-failure-", dir=runner.TMP_ROOT
        ) as directory:
            target = Path(directory) / "evidence.json"
            target.write_text("previous\n", encoding="utf-8")
            with mock.patch.object(
                runner.os, "replace", side_effect=OSError("mocked replace failure")
            ):
                with self.assertRaisesRegex(OSError, "mocked replace failure"):
                    runner.write_json(target, {"status": "ok"})
            self.assertEqual(target.read_text(encoding="utf-8"), "previous\n")
            self.assertEqual(list(target.parent.glob(f".{target.name}.*.tmp")), [])

    def test_mocked_success_records_runner_and_mouse_policy(self) -> None:
        manifest = valid_manifest()
        result = valid_result(manifest)
        with tempfile.TemporaryDirectory(
            prefix="corpus-runner-success-", dir=runner.TMP_ROOT
        ) as directory:
            base = Path(directory)
            executable = base / "native_snapshot_production_corpus_test.exe"
            executable.write_bytes(b"unit-test placeholder")
            evidence = base / "evidence.json"
            arguments = [
                "run_native_snapshot_production_corpus.py",
                "--corpus-exe",
                str(executable),
                "--manifest",
                str(runner.DEFAULT_MANIFEST),
                "--evidence",
                str(evidence),
            ]
            with mock.patch.object(sys, "argv", arguments), mock.patch.object(
                runner,
                "run_corpus",
                side_effect=[copy.deepcopy(result), copy.deepcopy(result)],
            ), mock.patch.object(runner, "write_json") as write, redirect_stdout(
                io.StringIO()
            ):
                self.assertEqual(runner.main(), 0)

            published = write.call_args.args[1]
            self.assertEqual(
                published["runner_sha256"],
                runner.sha256_file(Path(runner.__file__).resolve()),
            )
            self.assertFalse(
                published["headless_process_policy"]["mouse_capture"]
            )
            self.assertTrue(published["golden_digest_verified"])


if __name__ == "__main__":
    unittest.main()
