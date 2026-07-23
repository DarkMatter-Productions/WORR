#!/usr/bin/env python3
"""Run the headless FR-10-T10 maximum-capacity rewind budget gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

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


SCHEMA = "worr.networking.rewind-budget-runtime.v1"
MAP_NAME = "worr_fr10_rewind_mover"
STATUS_CVAR = "sg_worr_rewind_budget_selftest_status"
SGAME_MODULE = Path("basew/sgame_x86_64.dll")
P95_BUDGET_NS = 1_666_600
OWNER_CAP_BYTES = 8 * 1024 * 1024
STATUS_RE = re.compile(
    rf'{re.escape(STATUS_CVAR)}\s+"(?P<value>(?:pass|fail)(?::[0-9]+)+)"'
)
STATUS_FIELDS = (
    "status",
    "player_count",
    "mover_count",
    "player_history_capacity",
    "mover_history_capacity",
    "scene_capacity",
    "warmup_iterations",
    "sample_count",
    "sample_batch_iterations",
    "queries_per_iteration",
    "query_age_us",
    "query_count",
    "expected_query_count",
    "overwrite_count",
    "expected_overwrite_count",
    "capacity_overflows",
    "histories_full",
    "scene_sealed",
    "authority_unchanged",
    "allocation_free",
    "production_fixed_storage_bytes",
    "fixture_fixed_storage_bytes",
    "combined_fixed_storage_bytes",
    "owner_cap_bytes",
    "p50_ns",
    "p95_ns",
    "p99_ns",
    "p95_budget_ns",
    "workload_hash",
    "failure_code",
)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json_atomic(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def creation_flags() -> int:
    return _headless_creation_flags()


def build_command(dedicated_exe: Path) -> list[str]:
    """Use the existing dedicated-only T10 command with no client input."""
    return [
        str(dedicated_exe),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "deathmatch", "1",
        "+set", "maxclients", "2",
        "+map", MAP_NAME,
        "+addbot", "RewindBudget",
        "+sv", "worr_rewind_mover_arm_rider",
        "+wait", "12",
        "+sv", "worr_rewind_mover_selftest",
        "+cvarlist", STATUS_CVAR,
    ]


def parse_status(text: str) -> dict[str, int | str]:
    matches = list(STATUS_RE.finditer(text))
    if len(matches) != 1:
        raise RuntimeError(
            "expected exactly one rewind budget status row; "
            f"observed={len(matches)}"
        )
    values = matches[0].group("value").split(":")
    if len(values) != len(STATUS_FIELDS):
        raise RuntimeError(
            "rewind budget status field count changed: "
            f"observed={len(values)} expected={len(STATUS_FIELDS)}"
        )
    record: dict[str, int | str] = {"status": values[0]}
    for name, value in zip(STATUS_FIELDS[1:], values[1:], strict=True):
        if not value.isdecimal():
            raise RuntimeError(f"rewind budget status {name} is not decimal: {value!r}")
        record[name] = int(value)
    return record


def validate_status(status: dict[str, int | str]) -> dict[str, int | str]:
    if status["status"] != "pass":
        raise RuntimeError(f"rewind budget probe reported {status['status']!r}")
    expected_constants = {
        "player_count": 32,
        "mover_count": 64,
        "player_history_capacity": 512,
        "mover_history_capacity": 64,
        "scene_capacity": 96,
        "warmup_iterations": 32,
        "sample_count": 256,
        "sample_batch_iterations": 1,
        "queries_per_iteration": 96,
        "query_age_us": 200_000,
        "owner_cap_bytes": OWNER_CAP_BYTES,
        "p95_budget_ns": P95_BUDGET_NS,
        "capacity_overflows": 0,
        "histories_full": 1,
        "scene_sealed": 1,
        "authority_unchanged": 1,
        "allocation_free": 1,
        "failure_code": 0,
    }
    for name, expected in expected_constants.items():
        if status[name] != expected:
            raise RuntimeError(
                f"rewind budget proof {name}={status[name]!r}, expected={expected}"
            )

    expected_operations = (
        int(status["warmup_iterations"])
        + int(status["sample_count"]) * int(status["sample_batch_iterations"])
    ) * int(status["queries_per_iteration"])
    for name in ("query_count", "expected_query_count"):
        if status[name] != expected_operations:
            raise RuntimeError(f"rewind budget query bound changed at {name}")
    for name in ("overwrite_count", "expected_overwrite_count"):
        if status[name] != expected_operations:
            raise RuntimeError(f"rewind budget overwrite bound changed at {name}")

    production = int(status["production_fixed_storage_bytes"])
    fixture = int(status["fixture_fixed_storage_bytes"])
    combined = int(status["combined_fixed_storage_bytes"])
    if production <= 0 or fixture <= 0 or combined != production + fixture:
        raise RuntimeError("rewind budget fixed-storage accounting is inconsistent")
    if combined > OWNER_CAP_BYTES:
        raise RuntimeError("rewind budget fixed storage exceeds 8 MiB")

    p50 = int(status["p50_ns"])
    p95 = int(status["p95_ns"])
    p99 = int(status["p99_ns"])
    if not 0 < p50 <= p95 <= p99:
        raise RuntimeError("rewind budget percentiles are not ordered and positive")
    if p95 > P95_BUDGET_NS:
        raise RuntimeError("rewind budget p95 exceeds ten percent of a 60 Hz frame")
    if int(status["workload_hash"]) == 0:
        raise RuntimeError("rewind budget workload hash is absent")
    return status


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def wait_for_marker(
    process: subprocess.Popen[str], stdout_path: Path, marker: str, timeout: float
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if marker in read_text(stdout_path):
            return
        if process.poll() is not None:
            raise RuntimeError(
                "dedicated server exited before readiness marker "
                f"{marker!r} (returncode={process.returncode})"
            )
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for dedicated readiness marker {marker!r}")


def terminate(process: subprocess.Popen[str] | None) -> bool:
    return terminate_process_tree(process)


def run_once(
    *, command: list[str], working_dir: Path, run_root: Path, timeout: float
) -> dict[str, object]:
    stdout_path = run_root / "dedicated.stdout.log"
    stderr_path = run_root / "dedicated.stderr.log"
    process: subprocess.Popen[str] | None = None
    terminated = False
    try:
        with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open(
            "w", encoding="utf-8"
        ) as stderr:
            process = start_headless_process(
                command,
                cwd=working_dir,
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                text=True,
                creationflags=creation_flags(),
            )
            wait_for_marker(process, stdout_path, f"SpawnServer: {MAP_NAME}", timeout)
            wait_for_marker(process, stdout_path, STATUS_CVAR, timeout)
            status = validate_status(parse_status(read_text(stdout_path)))
        terminated = terminate(process)
        if read_text(stderr_path):
            raise RuntimeError("dedicated rewind budget probe wrote stderr")
        return {
            "status": status,
            "stdout": str(stdout_path),
            "stderr": str(stderr_path),
            "stdout_sha256": file_sha256(stdout_path),
            "stderr_sha256": file_sha256(stderr_path),
            "process_terminated_by_gate": terminated,
        }
    finally:
        terminate(process)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dedicated-exe", required=True, type=Path)
    parser.add_argument("--working-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be at least one")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    dedicated_exe = args.dedicated_exe.resolve()
    working_dir = args.working_dir.resolve()
    output = args.output.resolve()
    if not dedicated_exe.is_file():
        parser.error(f"dedicated executable is missing: {dedicated_exe}")
    if not working_dir.is_dir():
        parser.error(f"working directory is missing: {working_dir}")
    sgame_module = working_dir / SGAME_MODULE
    if not sgame_module.is_file():
        parser.error(f"staged sgame module is missing: {sgame_module}")
    if not (working_dir / "basew/maps" / f"{MAP_NAME}.bsp").is_file():
        parser.error("staged rewind fixture map is missing")

    output.unlink(missing_ok=True)
    failure_output = output.with_suffix(".failure.json")
    failure_output.unlink(missing_ok=True)
    started = datetime.now(timezone.utc)
    run_id = started.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_root = output.parent / f"{output.stem}.runs" / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    command = build_command(dedicated_exe)
    try:
        runs: list[dict[str, object]] = []
        for index in range(args.repeat):
            repeat_root = run_root / f"repeat-{index + 1:02d}"
            repeat_root.mkdir()
            runs.append(
                run_once(
                    command=command,
                    working_dir=working_dir,
                    run_root=repeat_root,
                    timeout=args.timeout,
                )
            )
        statuses = [run["status"] for run in runs]
        deterministic_fields = (
            "player_count", "mover_count", "player_history_capacity",
            "mover_history_capacity", "scene_capacity", "warmup_iterations",
            "sample_count", "sample_batch_iterations",
            "queries_per_iteration", "query_age_us", "query_count",
            "overwrite_count", "production_fixed_storage_bytes",
            "fixture_fixed_storage_bytes", "combined_fixed_storage_bytes",
            "owner_cap_bytes", "p95_budget_ns", "workload_hash",
        )
        first = statuses[0]
        for status in statuses[1:]:
            if any(status[name] != first[name] for name in deterministic_fields):
                raise RuntimeError("rewind budget workload evidence was not deterministic")
        report: dict[str, object] = {
            "schema": SCHEMA,
            "run_id": run_id,
            "started_at_utc": started.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "dedicated_executable": str(dedicated_exe),
            "dedicated_sha256": file_sha256(dedicated_exe),
            "sgame_module": str(sgame_module),
            "sgame_sha256": file_sha256(sgame_module),
            "working_directory": str(working_dir),
            "command": command,
            "repeat": args.repeat,
            "worst_p95_ns": max(int(status["p95_ns"]) for status in statuses),
            "status": first,
            "runs": runs,
        }
        write_json_atomic(run_root / "report.json", report)
        write_json_atomic(output, report)
    except Exception as error:
        failure = {
            "schema": SCHEMA + ".failure",
            "run_id": run_id,
            "failed_at_utc": datetime.now(timezone.utc).isoformat(),
            "dedicated_executable": str(dedicated_exe),
            "sgame_module": str(sgame_module),
            "working_directory": str(working_dir),
            "command": command,
            "error_type": type(error).__name__,
            "error": str(error),
        }
        write_json_atomic(run_root / "failure.json", failure)
        write_json_atomic(failure_output, failure)
        print(
            f"rewind budget runtime gate failed: {type(error).__name__}: {error}",
            file=sys.stderr,
        )
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
