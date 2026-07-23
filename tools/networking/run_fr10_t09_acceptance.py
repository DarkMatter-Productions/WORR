#!/usr/bin/env python3
"""Run and record the task-level FR-10-T09 acceptance gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path


PROBE_SCHEMA = "worr.networking.fr10-t09-command-probe.v1"
EVIDENCE_SCHEMA = "worr.networking.fr10-t09-acceptance-evidence.v1"
REQUIRED_GATES = {
    "command-stream",
    "legacy-adapter",
    "consumed-cursor",
    "prediction-input",
    "rewind-context",
    "local-action-correlation",
    "native-command-adapter",
    "native-event-production-link",
    "native-snapshot-production-link",
}
PROBE_TIMEOUT_SECONDS = 420
GATE_TIMEOUT_SECONDS = 240


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--gate", nargs=2, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    return parser.parse_args()


def run_executable(
    path: Path, cwd: Path, *, timeout_seconds: int
) -> tuple[bytes, bytes]:
    completed = subprocess.run(
        [str(path)],
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout_seconds,
        check=False,
    )
    if completed.returncode != 0:
        sys.stderr.buffer.write(completed.stdout)
        sys.stderr.buffer.write(completed.stderr)
        raise RuntimeError(f"{path.name} failed with {completed.returncode}")
    return completed.stdout, completed.stderr


def parse_probe(stdout: bytes) -> dict[str, object]:
    lines = [line for line in stdout.decode("utf-8").splitlines() if line]
    if len(lines) != 1:
        raise RuntimeError("probe must emit exactly one JSON line")
    payload = json.loads(lines[0])
    if payload.get("schema") != PROBE_SCHEMA:
        raise RuntimeError("unexpected probe schema")
    floors = {
        "canonical_commands": 1_000_000,
        "legacy_commands": 100_000,
        "native_round_trips": 100_000,
        "duplicate_acknowledgements": 1_000_000,
        "adversarial_rejections": 100_000,
        "sequence_wraps": 2,
    }
    for field, floor in floors.items():
        value = payload.get(field)
        if not isinstance(value, int) or value < floor:
            raise RuntimeError(f"probe floor failed: {field}={value!r}")
    digest = payload.get("digest")
    if not isinstance(digest, str) or len(digest) != 16:
        raise RuntimeError("probe digest is not a 64-bit hexadecimal string")
    int(digest, 16)
    return payload


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    probe_path = args.probe.resolve()
    output_path = args.output.resolve()
    gates = [(name, Path(path).resolve()) for name, path in args.gate]
    gate_names = {name for name, _ in gates}
    if gate_names != REQUIRED_GATES or len(gates) != len(REQUIRED_GATES):
        missing = sorted(REQUIRED_GATES - gate_names)
        extra = sorted(gate_names - REQUIRED_GATES)
        raise RuntimeError(f"gate set mismatch: missing={missing}, extra={extra}")

    first_stdout, first_stderr = run_executable(
        probe_path, repo_root, timeout_seconds=PROBE_TIMEOUT_SECONDS
    )
    second_stdout, second_stderr = run_executable(
        probe_path, repo_root, timeout_seconds=PROBE_TIMEOUT_SECONDS
    )
    first_probe = parse_probe(first_stdout)
    second_probe = parse_probe(second_stdout)
    if first_probe != second_probe or first_stderr != second_stderr:
        raise RuntimeError("the two deterministic probe repetitions diverged")

    gate_evidence: list[dict[str, object]] = []
    for name, path in gates:
        stdout, stderr = run_executable(
            path, repo_root, timeout_seconds=GATE_TIMEOUT_SECONDS
        )
        gate_evidence.append(
            {
                "name": name,
                "executable": path.name,
                "stdout_sha256": sha256(stdout),
                "stderr_sha256": sha256(stderr),
                "result": "pass",
            }
        )

    evidence = {
        "schema": EVIDENCE_SCHEMA,
        "task": "FR-10-T09",
        "result": "pass",
        "platform": {
            "system": platform.system(),
            "machine": platform.machine(),
        },
        "probe_repetitions": 2,
        "probe": first_probe,
        "probe_stdout_sha256": sha256(first_stdout),
        "gates": gate_evidence,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "FR-10-T09 acceptance passed: "
        f"{first_probe['canonical_commands']} canonical commands, "
        f"{first_probe['legacy_commands']} legacy mappings, "
        f"{first_probe['native_round_trips']} native round trips, "
        f"digest {first_probe['digest']}"
    )
    print(f"evidence: {output_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        OSError,
        RuntimeError,
        ValueError,
        json.JSONDecodeError,
        subprocess.TimeoutExpired,
    ) as error:
        print(f"FR-10-T09 acceptance failed: {error}", file=sys.stderr)
        raise SystemExit(1)
