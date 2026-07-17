#!/usr/bin/env python3
"""Qualify the deterministic headless native-snapshot production corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

try:
    from tools.networking.headless_process import (
        creation_flags as _headless_creation_flags,
        start_headless_process,
        terminate_process_tree,
    )
except ModuleNotFoundError:
    from headless_process import (
        creation_flags as _headless_creation_flags,
        start_headless_process,
        terminate_process_tree,
    )


REPO_ROOT = Path(__file__).resolve().parents[2]
TMP_ROOT = REPO_ROOT / ".tmp"
DEFAULT_MANIFEST = (
    Path(__file__).resolve().parent
    / "scenarios"
    / "native_snapshot_production_corpus.json"
)
DEFAULT_EVIDENCE = (
    TMP_ROOT
    / "networking"
    / "native_snapshot_production_corpus"
    / "evidence.json"
)

MANIFEST_SCHEMA = "worr.native_snapshot_production_corpus.scenario.v1"
EVIDENCE_SCHEMA = "worr.native_snapshot_production_corpus.evidence.v1"
EXPECTED_RESULT_SCHEMA = "worr.native_snapshot_production_corpus.v1"
EXPECTED_CLASSIFICATION = "headless_deterministic_serialized_production_path"
EXPECTED_STATUS = "ok"
EXPECTED_SNAPSHOTS = 100_000
EXPECTED_REPEATS = 2
EXPECTED_EPOCHS = 4
EXPECTED_NEGATIVE_PROBES = 3
EXPECTED_PREDICTION_MAX_REPLAY = 127
EXPECTED_PREDICTION_LIMIT_RANGES = 4
EXPECTED_PREDICTION_EXHAUSTION_REJECTIONS = 1
UINT64_MAX = (1 << 64) - 1
DIGEST_PATTERN = re.compile(r"[0-9a-f]{16}")

MANIFEST_FIELDS = frozenset(
    (
        "schema",
        "name",
        "result_schema",
        "classification",
        "expected_status",
        "snapshot_count",
        "seed",
        "repeat",
        "timeout_seconds",
        "golden_corpus_digest",
        "negative_probe_fields",
        "required_nonzero_coverage",
    )
)

RESULT_FIELDS = frozenset(
    (
        "schema",
        "classification",
        "status",
        "requested_frames",
        "accepted_frames",
        "seed",
        "corpus_digest",
        "coverage",
    )
)

COVERAGE_FIELDS = frozenset(
    (
        "serialized_frames",
        "acknowledged_frames",
        "released_frames",
        "prediction_authorities",
        "resolved_prediction_commands",
        "prediction_pending_ranges",
        "prediction_nonpending_ranges",
        "prediction_max_replay",
        "prediction_bootstrap_ranges",
        "prediction_nonzero_cursor_ranges",
        "prediction_limit_ranges",
        "prediction_range_exhaustion_rejections",
        "negative_probe_frames",
        "corrupt_ack_probe_acceptances",
        "corrupt_ack_probe_prediction_authorities",
        "authority_history_resets",
        "epochs",
        "connection_activations",
        "sequence_gaps",
        "fragment_stalls",
        "rate_suppressions",
        "late_expectations",
        "server_transmissions",
        "server_packets",
        "server_packet_bytes",
        "server_to_client_deliveries",
        "server_to_client_delivery_bytes",
        "client_to_server_deliveries",
        "client_to_server_delivery_bytes",
        "server_to_client_losses",
        "server_to_client_impairment_decisions",
        "client_to_server_impairment_decisions",
        "burst_packet_losses",
        "server_to_client_fragment_release_inversions",
        "duplicate_deliveries",
        "acknowledgement_losses",
        "upstream_ack_stalls",
        "repeat_revalidations",
        "corrupt_server_to_client",
        "corrupt_client_to_server",
        "corrupt_rejections",
        "exact_once_checks",
        "premature_ack_checks",
        "premature_release_checks",
        "retained_until_epoch_reset",
    )
)

NEGATIVE_PROBE_FIELDS = frozenset(
    ("corrupt_server_to_client", "corrupt_client_to_server")
)

MANDATORY_NONZERO_COVERAGE = frozenset(
    (
        "resolved_prediction_commands",
        "prediction_pending_ranges",
        "prediction_nonpending_ranges",
        "authority_history_resets",
        "connection_activations",
        "server_to_client_deliveries",
        "server_to_client_delivery_bytes",
        "client_to_server_deliveries",
        "client_to_server_delivery_bytes",
        "server_to_client_losses",
        "server_to_client_impairment_decisions",
        "client_to_server_impairment_decisions",
        "burst_packet_losses",
        "server_to_client_fragment_release_inversions",
        "duplicate_deliveries",
        "acknowledgement_losses",
        "upstream_ack_stalls",
        "repeat_revalidations",
        "corrupt_server_to_client",
        "corrupt_client_to_server",
        "corrupt_rejections",
    )
)


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON constant is forbidden: {value}")


def _strict_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, member in pairs:
        if key in value:
            raise ValueError(f"duplicate JSON object key: {key}")
        value[key] = member
    return value


def parse_strict_json(text: str, label: str) -> dict[str, Any]:
    if not text.strip():
        raise ValueError(f"{label} is empty")
    try:
        value = json.loads(
            text,
            parse_constant=_reject_json_constant,
            object_pairs_hook=_strict_json_object,
        )
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(
            f"{label} must contain exactly one strict JSON document: {error}"
        ) from error
    if not isinstance(value, dict):
        raise ValueError(f"{label} top-level value must be an object")
    return value


def load_json(path: Path) -> dict[str, Any]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise OSError(f"could not read {path}: {error}") from error
    return parse_strict_json(text, str(path))


def require_uint64(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    if value < 0 or value > UINT64_MAX:
        raise ValueError(f"{label} must be in the uint64 domain")
    return value


def require_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a non-empty string")
    return value


def require_exact_fields(
    value: dict[str, Any], expected: frozenset[str], label: str
) -> None:
    actual = frozenset(value)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing or unexpected:
        details: list[str] = []
        if missing:
            details.append(f"missing {', '.join(missing)}")
        if unexpected:
            details.append(f"unexpected {', '.join(unexpected)}")
        raise ValueError(f"{label} fields are invalid: {'; '.join(details)}")


def validate_manifest(manifest: dict[str, Any]) -> None:
    require_exact_fields(manifest, MANIFEST_FIELDS, "manifest")
    if manifest.get("schema") != MANIFEST_SCHEMA:
        raise ValueError(f"manifest schema must be {MANIFEST_SCHEMA}")
    require_string(manifest.get("name"), "manifest.name")
    if manifest.get("result_schema") != EXPECTED_RESULT_SCHEMA:
        raise ValueError(
            f"manifest result_schema must be {EXPECTED_RESULT_SCHEMA}"
        )
    if manifest.get("classification") != EXPECTED_CLASSIFICATION:
        raise ValueError(
            "manifest classification must retain the serialized production-path "
            "classification"
        )
    if manifest.get("expected_status") != EXPECTED_STATUS:
        raise ValueError(f"manifest expected_status must be {EXPECTED_STATUS}")
    if (
        require_uint64(manifest.get("snapshot_count"), "manifest.snapshot_count")
        != EXPECTED_SNAPSHOTS
    ):
        raise ValueError("the production corpus must request exactly 100,000 snapshots")
    require_uint64(manifest.get("seed"), "manifest.seed")
    if require_uint64(manifest.get("repeat"), "manifest.repeat") != EXPECTED_REPEATS:
        raise ValueError("the repeatability contract requires exactly two runs")
    timeout_seconds = require_uint64(
        manifest.get("timeout_seconds"), "manifest.timeout_seconds"
    )
    if timeout_seconds == 0 or timeout_seconds > 3600:
        raise ValueError("manifest.timeout_seconds must be in [1, 3600]")

    nonzero = manifest.get("required_nonzero_coverage")
    if (
        not isinstance(nonzero, list)
        or not nonzero
        or any(not isinstance(field, str) or not field for field in nonzero)
        or len(set(nonzero)) != len(nonzero)
    ):
        raise ValueError(
            "manifest.required_nonzero_coverage must be a non-empty unique "
            "string array"
        )
    unknown_nonzero = sorted(set(nonzero) - COVERAGE_FIELDS)
    if unknown_nonzero:
        raise ValueError(
            "manifest.required_nonzero_coverage contains unknown fields: "
            + ", ".join(unknown_nonzero)
        )
    missing_mandatory = sorted(MANDATORY_NONZERO_COVERAGE - set(nonzero))
    if missing_mandatory:
        raise ValueError(
            "manifest.required_nonzero_coverage weakens the production "
            "contract; missing " + ", ".join(missing_mandatory)
        )

    negative = manifest.get("negative_probe_fields")
    if (
        not isinstance(negative, list)
        or not negative
        or any(not isinstance(field, str) or not field for field in negative)
        or len(set(negative)) != len(negative)
    ):
        raise ValueError(
            "manifest.negative_probe_fields must be a non-empty unique string array"
        )
    unknown_negative = sorted(set(negative) - COVERAGE_FIELDS)
    if unknown_negative:
        raise ValueError(
            "manifest.negative_probe_fields contains unknown fields: "
            + ", ".join(unknown_negative)
        )
    if set(negative) != NEGATIVE_PROBE_FIELDS:
        raise ValueError(
            "manifest.negative_probe_fields must classify the corrupt "
            "server-to-client and client-to-server probes exactly"
        )

    golden = manifest.get("golden_corpus_digest")
    if not isinstance(golden, str) or not DIGEST_PATTERN.fullmatch(golden):
        raise ValueError(
            "manifest.golden_corpus_digest must be a lowercase 16-digit "
            "hex digest"
        )


def validate_result(
    result: dict[str, Any], manifest: dict[str, Any]
) -> dict[str, int]:
    require_exact_fields(result, RESULT_FIELDS, "corpus result")
    if result["schema"] != manifest["result_schema"]:
        raise ValueError("corpus result schema differs from the manifest")
    if result["classification"] != manifest["classification"]:
        raise ValueError("corpus result classification differs from the manifest")
    if result["status"] != manifest["expected_status"]:
        raise ValueError("corpus result status is not ok")

    requested = require_uint64(result["requested_frames"], "requested_frames")
    accepted = require_uint64(result["accepted_frames"], "accepted_frames")
    seed = require_uint64(result["seed"], "seed")
    if requested != manifest["snapshot_count"]:
        raise ValueError("requested_frames differs from the fixed manifest count")
    if accepted != requested:
        raise ValueError("accepted_frames must exactly equal requested_frames")
    if seed != manifest["seed"]:
        raise ValueError("corpus seed differs from the fixed manifest seed")

    corpus_digest = result["corpus_digest"]
    if not isinstance(corpus_digest, str) or not DIGEST_PATTERN.fullmatch(
        corpus_digest
    ):
        raise ValueError(
            "corpus_digest must be a lowercase 16-digit hexadecimal string"
        )

    coverage = result["coverage"]
    if not isinstance(coverage, dict):
        raise ValueError("coverage must be an object")
    require_exact_fields(coverage, COVERAGE_FIELDS, "coverage")
    counters = {
        field: require_uint64(coverage[field], f"coverage.{field}")
        for field in COVERAGE_FIELDS
    }

    acknowledged = counters["acknowledged_frames"]
    released = counters["released_frames"]
    authorities = counters["prediction_authorities"]
    if acknowledged != accepted or released != accepted:
        raise ValueError(
            "acknowledged_frames and released_frames must exactly equal "
            "accepted_frames"
        )
    if authorities != requested:
        raise ValueError(
            "prediction_authorities must exactly equal requested_frames"
        )
    if counters["resolved_prediction_commands"] <= requested:
        raise ValueError(
            "resolved_prediction_commands must exceed requested_frames"
        )
    if (
        counters["prediction_pending_ranges"]
        + counters["prediction_nonpending_ranges"]
        != requested
    ):
        raise ValueError(
            "pending and nonpending prediction ranges must classify every "
            "requested frame exactly once"
        )
    if counters["prediction_max_replay"] != EXPECTED_PREDICTION_MAX_REPLAY:
        raise ValueError(
            "prediction_max_replay must reach the 127-command production limit "
            "for the fixed 100,000-frame corpus"
        )
    epochs = counters["epochs"]
    if epochs != EXPECTED_EPOCHS:
        raise ValueError("epochs must equal four for the fixed corpus")
    if counters["connection_activations"] != epochs:
        raise ValueError("connection_activations must exactly equal epochs")
    if counters["prediction_bootstrap_ranges"] != epochs:
        raise ValueError("prediction_bootstrap_ranges must exactly equal epochs")
    if counters["prediction_nonzero_cursor_ranges"] != requested - epochs:
        raise ValueError(
            "prediction_nonzero_cursor_ranges must equal requested_frames "
            "minus epochs"
        )
    if counters["prediction_limit_ranges"] != EXPECTED_PREDICTION_LIMIT_RANGES:
        raise ValueError(
            "prediction_limit_ranges must equal four for the fixed corpus"
        )
    if (
        counters["prediction_range_exhaustion_rejections"]
        != EXPECTED_PREDICTION_EXHAUSTION_REJECTIONS
    ):
        raise ValueError(
            "prediction_range_exhaustion_rejections must equal one"
        )

    corrupt_probes = sum(
        counters[field] for field in manifest["negative_probe_fields"]
    )
    negative_probes = counters["negative_probe_frames"]
    if negative_probes != EXPECTED_NEGATIVE_PROBES:
        raise ValueError("negative_probe_frames must equal three")
    if negative_probes != corrupt_probes:
        raise ValueError(
            "negative_probe_frames must equal the corrupt server-to-client "
            "and client-to-server probe total"
        )
    if counters["serialized_frames"] != requested + EXPECTED_NEGATIVE_PROBES:
        raise ValueError(
            "serialized_frames must equal requested_frames plus three"
        )
    if (
        counters["corrupt_ack_probe_acceptances"]
        != counters["corrupt_client_to_server"]
    ):
        raise ValueError(
            "corrupt_ack_probe_acceptances must equal corrupt_client_to_server"
        )
    if (
        counters["corrupt_ack_probe_prediction_authorities"]
        != counters["corrupt_ack_probe_acceptances"]
    ):
        raise ValueError(
            "corrupt_ack_probe_prediction_authorities must equal "
            "corrupt_ack_probe_acceptances"
        )
    if counters["corrupt_rejections"] != negative_probes:
        raise ValueError("every negative corrupt probe must be rejected exactly once")
    if counters["retained_until_epoch_reset"] != EXPECTED_NEGATIVE_PROBES:
        raise ValueError(
            "retained_until_epoch_reset must equal the three negative probes"
        )
    if accepted - released != 0:
        raise ValueError("accepted snapshot abandonment must remain zero")

    for field in manifest["required_nonzero_coverage"]:
        if counters[field] == 0:
            raise ValueError(f"coverage.{field} contains no required evidence")

    if counters["exact_once_checks"] < requested:
        raise ValueError("exact_once_checks does not cover every requested snapshot")
    if counters["premature_release_checks"] < requested:
        raise ValueError(
            "premature_release_checks does not cover every requested snapshot"
        )

    return {
        "negative_probes": negative_probes,
        "accepted_abandonment": accepted - released,
    }


def normalized_json(value: dict[str, Any]) -> str:
    return json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    )


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _bounded_text(value: str, limit: int = 16_384) -> str:
    if len(value) <= limit:
        return value
    return value[-limit:]


def run_corpus(
    executable: Path, manifest: dict[str, Any], run_number: int
) -> dict[str, Any]:
    command = [
        str(executable),
        "--snapshots",
        str(manifest["snapshot_count"]),
        "--seed",
        str(manifest["seed"]),
    ]
    process: subprocess.Popen[str] | None = None
    try:
        process = start_headless_process(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="strict",
            cwd=REPO_ROOT,
            creationflags=_headless_creation_flags(),
        )
        try:
            stdout, stderr = process.communicate(
                timeout=manifest["timeout_seconds"]
            )
        except subprocess.TimeoutExpired as error:
            terminate_process_tree(process)
            stdout, stderr = process.communicate()
            raise RuntimeError(
                f"corpus run {run_number} timed out after "
                f"{manifest['timeout_seconds']} seconds\n"
                f"stdout:\n{_bounded_text(stdout)}\n"
                f"stderr:\n{_bounded_text(stderr)}"
            ) from error
    finally:
        terminate_process_tree(process)

    if process is None:
        raise RuntimeError(f"corpus run {run_number} did not launch")
    if process.returncode != 0:
        raise RuntimeError(
            f"corpus run {run_number} failed with exit code {process.returncode}\n"
            f"stdout:\n{_bounded_text(stdout)}\n"
            f"stderr:\n{_bounded_text(stderr)}"
        )
    if stderr.strip():
        raise RuntimeError(
            f"corpus run {run_number} emitted unexpected stderr:\n"
            f"{_bounded_text(stderr)}"
        )
    return parse_strict_json(stdout, f"corpus run {run_number} stdout")


def _is_linklike(path: Path) -> bool:
    if path.is_symlink():
        return True
    is_junction = getattr(path, "is_junction", None)
    return bool(is_junction is not None and is_junction())


def resolve_evidence_path(path: Path) -> Path:
    lexical_tmp_root = Path(os.path.abspath(TMP_ROOT))
    if _is_linklike(lexical_tmp_root):
        raise ValueError(f"temporary root must not be a link: {lexical_tmp_root}")
    if lexical_tmp_root.exists() and not lexical_tmp_root.is_dir():
        raise ValueError(
            f"temporary root must be a directory: {lexical_tmp_root}"
        )

    supplied = path if path.is_absolute() else REPO_ROOT / path
    lexical_candidate = Path(os.path.abspath(supplied))
    try:
        lexical_relative = lexical_candidate.relative_to(lexical_tmp_root)
    except ValueError as error:
        raise ValueError(
            f"evidence path must remain under {lexical_tmp_root}: "
            f"{lexical_candidate}"
        ) from error
    if not lexical_relative.parts:
        raise ValueError("evidence path must name a file below the .tmp root")

    current = lexical_tmp_root
    final_index = len(lexical_relative.parts) - 1
    for index, member in enumerate(lexical_relative.parts):
        current /= member
        if _is_linklike(current):
            raise ValueError(f"evidence path must not traverse a link: {current}")
        if index != final_index and current.exists() and not current.is_dir():
            raise ValueError(
                f"evidence parent component must be a directory: {current}"
            )

    resolved_tmp_root = lexical_tmp_root.resolve()
    candidate = lexical_candidate.resolve(strict=False)
    try:
        resolved_relative = candidate.relative_to(resolved_tmp_root)
    except ValueError as error:
        raise ValueError(
            f"evidence path must resolve under {resolved_tmp_root}: {candidate}"
        ) from error
    if not resolved_relative.parts:
        raise ValueError("evidence path must name a file below the .tmp root")
    return candidate


def prepare_evidence_path(path: Path) -> Path:
    candidate = resolve_evidence_path(path)
    if _is_linklike(candidate):
        raise ValueError(f"evidence path must not be a link: {candidate}")
    if candidate.exists():
        if not candidate.is_file():
            raise ValueError(
                f"existing evidence path must be a regular file: {candidate}"
            )
        candidate.unlink()
    return candidate


def write_json(path: Path, value: object) -> None:
    payload = json.dumps(
        value, allow_nan=False, indent=2, sort_keys=True
    ) + "\n"
    path = resolve_evidence_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path = resolve_evidence_path(path)
    if _is_linklike(path):
        raise ValueError(f"evidence path must not be a link: {path}")
    if path.exists() and not path.is_file():
        raise ValueError(
            f"existing evidence path must be a regular file: {path}"
        )

    descriptor = -1
    temporary_path: Path | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
        )
        temporary_path = Path(temporary_name)
        with os.fdopen(
            descriptor, "w", encoding="utf-8", newline="\n"
        ) as stream:
            descriptor = -1
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        if _is_linklike(path) or (path.exists() and not path.is_file()):
            raise ValueError(f"evidence target became unsafe: {path}")
        os.replace(temporary_path, path)
        temporary_path = None
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run the serialized native-snapshot production corpus twice under "
            "the shared hidden/input-free process policy."
        )
    )
    parser.add_argument("--corpus-exe", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument(
        "--evidence",
        "--evidence-path",
        dest="evidence",
        type=Path,
        default=DEFAULT_EVIDENCE,
    )
    args = parser.parse_args()

    evidence_path = prepare_evidence_path(args.evidence)
    executable = args.corpus_exe.resolve()
    if not executable.is_file():
        raise FileNotFoundError(f"corpus executable not found: {executable}")
    if executable.stem != "native_snapshot_production_corpus_test":
        raise ValueError(
            "--corpus-exe must name native_snapshot_production_corpus_test; "
            "interactive client executables are forbidden"
        )
    manifest_path = args.manifest.resolve()
    manifest = load_json(manifest_path)
    validate_manifest(manifest)

    results: list[dict[str, Any]] = []
    validations: list[dict[str, int]] = []
    normalized_runs: list[str] = []
    normalized_hashes: list[str] = []
    for run_number in range(1, EXPECTED_REPEATS + 1):
        result = run_corpus(executable, manifest, run_number)
        validation = validate_result(result, manifest)
        normalized = normalized_json(result)
        results.append(result)
        validations.append(validation)
        normalized_runs.append(normalized)
        normalized_hashes.append(sha256_bytes(normalized.encode("utf-8")))

    if normalized_runs[0] != normalized_runs[1]:
        raise RuntimeError(
            "the two corpus runs produced different normalized JSON evidence"
        )
    if results[0]["corpus_digest"] != results[1]["corpus_digest"]:
        raise RuntimeError("the two corpus runs produced different corpus digests")
    if validations[0] != validations[1]:
        raise RuntimeError("the two corpus runs produced different classifications")

    golden = manifest["golden_corpus_digest"]
    if results[0]["corpus_digest"] != golden:
        raise RuntimeError(
            "corpus digest differs from manifest golden: "
            f"expected {golden}, got {results[0]['corpus_digest']}"
        )

    evidence = {
        "schema": EVIDENCE_SCHEMA,
        "classification": manifest["classification"],
        "status": EXPECTED_STATUS,
        "manifest_name": manifest["name"],
        "manifest_sha256": sha256_file(manifest_path),
        "executable_sha256": sha256_file(executable),
        "runner_sha256": sha256_file(Path(__file__).resolve()),
        "headless_process_policy": {
            "shared_helper": "tools/networking/headless_process.py",
            "visible_window": False,
            "stdin": "devnull",
            "client_input_initialized": False,
            "mouse_capture": False,
        },
        "repeat_count": EXPECTED_REPEATS,
        "repeatable": True,
        "golden_corpus_digest": golden,
        "golden_digest_verified": True,
        "corpus_digest": results[0]["corpus_digest"],
        "normalized_json_sha256": normalized_hashes[0],
        "requested_frames": manifest["snapshot_count"],
        "negative_probes": validations[0]["negative_probes"],
        "accepted_abandonment": validations[0]["accepted_abandonment"],
        "runs": [
            {
                "run": index + 1,
                "corpus_digest": result["corpus_digest"],
                "normalized_json_sha256": normalized_hashes[index],
            }
            for index, result in enumerate(results)
        ],
        "result": results[0],
    }
    write_json(evidence_path, evidence)
    print(evidence_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
