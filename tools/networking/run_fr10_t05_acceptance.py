#!/usr/bin/env python3
"""Run and record the task-level FR-10-T05 event-journal acceptance gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path


EVIDENCE_SCHEMA = "worr.networking.fr10-t05-acceptance-evidence.v1"
REQUIRED_GATES = {
    "event-journal",
    "event-stream",
    "event-shadow",
    "cgame-presentation",
    "cgame-runtime",
    "native-admission",
    "native-sender",
    "native-virtual-link",
    "snapshot-event-fence",
    "server-candidates",
    "legacy-temp",
    "legacy-muzzle",
    "legacy-spatial-audio",
    "legacy-game-event",
    "legacy-damage",
    "legacy-help-path",
    "legacy-keyed-poi",
    "local-action-prediction",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gate", nargs=2, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    return parser.parse_args()


def run_executable(path: Path, cwd: Path) -> tuple[bytes, bytes]:
    completed = subprocess.run(
        [str(path)],
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
        check=False,
    )
    if completed.returncode != 0:
        sys.stderr.buffer.write(completed.stdout)
        sys.stderr.buffer.write(completed.stderr)
        raise RuntimeError(f"{path.name} failed with {completed.returncode}")
    return completed.stdout, completed.stderr


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    output_path = args.output.resolve()
    gates = [(name, Path(path).resolve()) for name, path in args.gate]
    gate_names = {name for name, _ in gates}
    if gate_names != REQUIRED_GATES or len(gates) != len(REQUIRED_GATES):
        missing = sorted(REQUIRED_GATES - gate_names)
        extra = sorted(gate_names - REQUIRED_GATES)
        raise RuntimeError(f"gate set mismatch: missing={missing}, extra={extra}")

    first: list[tuple[str, Path, bytes, bytes]] = []
    second: list[tuple[str, Path, bytes, bytes]] = []
    for name, path in gates:
        stdout, stderr = run_executable(path, repo_root)
        first.append((name, path, stdout, stderr))
    for name, path in gates:
        stdout, stderr = run_executable(path, repo_root)
        second.append((name, path, stdout, stderr))

    digest = hashlib.sha256()
    gate_evidence: list[dict[str, object]] = []
    for first_row, second_row in zip(first, second, strict=True):
        name, path, stdout, stderr = first_row
        second_name, second_path, second_stdout, second_stderr = second_row
        if name != second_name or path != second_path:
            raise RuntimeError("gate ordering changed between repetitions")
        if stdout != second_stdout or stderr != second_stderr:
            raise RuntimeError(f"deterministic gate diverged: {name}")
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(stdout)
        digest.update(b"\0")
        digest.update(stderr)
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
        "task": "FR-10-T05",
        "result": "pass",
        "platform": {
            "system": platform.system(),
            "machine": platform.machine(),
        },
        "gate_repetitions": 2,
        "gate_count": len(gates),
        "deterministic_digest": digest.hexdigest(),
        "gates": gate_evidence,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "FR-10-T05 acceptance passed: "
        f"{len(gates)} compiled gates x 2 repetitions, "
        f"digest {evidence['deterministic_digest']}"
    )
    print(f"evidence: {output_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FR-10-T05 acceptance failed: {error}", file=sys.stderr)
        raise SystemExit(1)
