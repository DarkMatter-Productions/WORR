#!/usr/bin/env python3
"""Run and record the parent-level FR-10-T06 snapshot acceptance gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any


EVIDENCE_SCHEMA = "worr.networking.fr10-t06-acceptance-evidence.v1"
BUDGET_SCHEMA = "worr.networking.fr10-t06-snapshot-budget.v1"
OFFLINE_CLASSIFICATION = "offline_deterministic_parity_corpus"
PRODUCTION_SCHEMA = "worr.native_snapshot_production_corpus.evidence.v1"
REQUIRED_GATES = {
    "snapshot-store",
    "snapshot-recovery",
    "q2proto-projection",
    "q2proto-wire",
    "server-shadow",
    "native-codec",
    "native-admission",
    "native-sender",
    "native-receiver",
    "native-virtual-link",
    "native-production-link",
    "cgame-authority",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--budget-probe", type=Path, required=True)
    parser.add_argument("--offline-runner", type=Path, required=True)
    parser.add_argument("--offline-probe", type=Path, required=True)
    parser.add_argument("--offline-manifest", type=Path, required=True)
    parser.add_argument("--production-runner", type=Path, required=True)
    parser.add_argument("--production-probe", type=Path, required=True)
    parser.add_argument("--production-manifest", type=Path, required=True)
    parser.add_argument("--platform-id", required=True)
    parser.add_argument("--build-type", required=True)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--gate", nargs=2, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    return parser.parse_args()


def run(
    command: list[str], cwd: Path, timeout: int, *, allow_stderr: bool = False
) -> tuple[bytes, bytes]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    if completed.returncode != 0:
        sys.stderr.buffer.write(completed.stdout)
        sys.stderr.buffer.write(completed.stderr)
        raise RuntimeError(
            f"{Path(command[0]).name} failed with {completed.returncode}"
        )
    if completed.stderr and not allow_stderr:
        sys.stderr.buffer.write(completed.stderr)
        raise RuntimeError(f"{Path(command[0]).name} emitted stderr")
    return completed.stdout, completed.stderr


def strict_json(data: bytes, label: str) -> dict[str, Any]:
    def reject_constant(value: str) -> None:
        raise ValueError(f"{label}: non-finite constant {value}")

    value = json.loads(data.decode("utf-8"), parse_constant=reject_constant)
    if not isinstance(value, dict):
        raise ValueError(f"{label}: top-level value must be an object")
    return value


def load_json(path: Path, label: str) -> dict[str, Any]:
    return strict_json(path.read_bytes(), label)


def require_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    return value


def validate_budget(value: dict[str, Any]) -> None:
    if value.get("schema") != BUDGET_SCHEMA or value.get("status") != "ok":
        raise ValueError("snapshot budget probe did not report the accepted schema/status")
    exact = {
        "entities": 512,
        "area_bytes": 1024,
        "event_refs": 512,
        "history_slots": 64,
        "encoded_bytes": 80869,
        "encoded_capacity": 131072,
    }
    for field, expected in exact.items():
        if require_int(value.get(field), f"budget.{field}") != expected:
            raise ValueError(f"budget.{field} differs from {expected}")
    for value_field, budget_field in (
        ("build_encode_p95_ns", "work_budget_ns"),
        ("decode_p95_ns", "work_budget_ns"),
        ("canonical_history_bytes", "canonical_history_budget_bytes"),
        ("native_owner_bytes", "native_owner_budget_bytes"),
    ):
        actual = require_int(value.get(value_field), f"budget.{value_field}")
        limit = require_int(value.get(budget_field), f"budget.{budget_field}")
        if actual < 0 or limit <= 0 or actual > limit:
            raise ValueError(f"budget exceeded: {value_field}={actual}, limit={limit}")


def validate_offline(value: dict[str, Any]) -> None:
    if value.get("schema_version") != 1:
        raise ValueError("offline corpus schema_version must be 1")
    if value.get("classification") != OFFLINE_CLASSIFICATION:
        raise ValueError("offline corpus classification changed")
    if value.get("project_task") != "FR-10-T06":
        raise ValueError("offline corpus project task changed")
    if value.get("repeatable") is not True or value.get("repeat_count") != 2:
        raise ValueError("offline corpus is not two-run repeatable evidence")
    result = value.get("result")
    if not isinstance(result, dict) or result.get("snapshot_count") != 100_000:
        raise ValueError("offline corpus must contain 100,000 snapshots")
    coverage = result.get("coverage")
    if not isinstance(coverage, dict):
        raise ValueError("offline corpus coverage is missing")
    for field in (
        "endpoint_hash_matches",
        "legacy_hash_matches",
        "component_hash_matches",
        "exact_chronology_matches",
    ):
        if coverage.get(field) != 100_000:
            raise ValueError(f"offline corpus {field} must equal 100,000")


def validate_production(value: dict[str, Any]) -> None:
    if value.get("schema") != PRODUCTION_SCHEMA or value.get("status") != "ok":
        raise ValueError("production corpus schema/status changed")
    if value.get("requested_frames") != 100_000:
        raise ValueError("production corpus must request 100,000 frames")
    if value.get("repeatable") is not True or value.get("repeat_count") != 2:
        raise ValueError("production corpus is not two-run repeatable evidence")
    if value.get("golden_digest_verified") is not True:
        raise ValueError("production corpus golden digest was not verified")
    if value.get("accepted_abandonment") != 0:
        raise ValueError("production corpus abandoned accepted snapshots")
    result = value.get("result")
    coverage = result.get("coverage") if isinstance(result, dict) else None
    if not isinstance(coverage, dict):
        raise ValueError("production corpus coverage is missing")
    for field in (
        "acknowledged_frames",
        "released_frames",
        "prediction_authorities",
    ):
        if coverage.get(field) != 100_000:
            raise ValueError(f"production corpus {field} must equal 100,000")
    if coverage.get("corrupt_rejections") != 3:
        raise ValueError("production corpus must reject all three corrupt probes")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    output = args.output.resolve()
    work_dir = output.parent / "fr10_t06_components"
    work_dir.mkdir(parents=True, exist_ok=True)
    offline_output = work_dir / "offline_parity.json"
    production_output = work_dir / "serialized_production.json"

    gates = [(name, Path(path).resolve()) for name, path in args.gate]
    names = {name for name, _ in gates}
    if names != REQUIRED_GATES or len(gates) != len(REQUIRED_GATES):
        raise RuntimeError(
            f"gate set mismatch: missing={sorted(REQUIRED_GATES - names)}, "
            f"extra={sorted(names - REQUIRED_GATES)}"
        )

    budget_stdout, _ = run([str(args.budget_probe.resolve())], repo_root, 120)
    budget = strict_json(budget_stdout, "budget probe")
    validate_budget(budget)

    gate_evidence: list[dict[str, Any]] = []
    for name, path in gates:
        first_stdout, first_stderr = run(
            [str(path)], repo_root, 240, allow_stderr=True
        )
        second_stdout, second_stderr = run(
            [str(path)], repo_root, 240, allow_stderr=True
        )
        if first_stdout != second_stdout or first_stderr != second_stderr:
            raise RuntimeError(f"deterministic gate diverged: {name}")
        gate_evidence.append(
            {
                "name": name,
                "executable": path.name,
                "stdout_sha256": sha256(first_stdout),
                "stderr_sha256": sha256(first_stderr),
                "repetitions": 2,
                "result": "pass",
            }
        )

    run(
        [
            sys.executable,
            str(args.offline_runner.resolve()),
            "--probe-exe",
            str(args.offline_probe.resolve()),
            "--manifest",
            str(args.offline_manifest.resolve()),
            "--output",
            str(offline_output),
            "--platform-id",
            args.platform_id,
            "--build-type",
            args.build_type,
            "--compiler-id",
            args.compiler_id,
        ],
        repo_root,
        600,
    )
    offline = load_json(offline_output, "offline corpus evidence")
    validate_offline(offline)

    run(
        [
            sys.executable,
            str(args.production_runner.resolve()),
            "--corpus-exe",
            str(args.production_probe.resolve()),
            "--manifest",
            str(args.production_manifest.resolve()),
            "--evidence",
            str(production_output),
        ],
        repo_root,
        1_500,
    )
    production = load_json(production_output, "production corpus evidence")
    validate_production(production)

    evidence = {
        "schema": EVIDENCE_SCHEMA,
        "task": "FR-10-T06",
        "result": "pass",
        "platform": {
            "system": platform.system(),
            "machine": platform.machine(),
            "platform_id": args.platform_id,
            "build_type": args.build_type,
            "compiler_id": args.compiler_id,
        },
        "budget": budget,
        "gate_count": len(gates),
        "gate_repetitions": 2,
        "gates": gate_evidence,
        "offline_corpus": {
            "snapshots": 100_000,
            "repeat_count": 2,
            "digest": offline["result"]["corpus_digest"],
            "evidence_sha256": sha256(offline_output.read_bytes()),
        },
        "production_corpus": {
            "snapshots": 100_000,
            "repeat_count": 2,
            "digest": production["corpus_digest"],
            "negative_probes": production["negative_probes"],
            "evidence_sha256": sha256(production_output.read_bytes()),
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        "FR-10-T06 acceptance passed: "
        f"{len(gates)} gates x 2, two 100,000-snapshot corpora, "
        f"build/encode p95 {budget['build_encode_p95_ns']} ns, "
        f"decode p95 {budget['decode_p95_ns']} ns"
    )
    print(f"evidence: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (json.JSONDecodeError, OSError, RuntimeError, ValueError) as error:
        print(f"FR-10-T06 acceptance failed: {error}", file=sys.stderr)
        raise SystemExit(1)
