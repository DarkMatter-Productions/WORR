#!/usr/bin/env python3
"""Run the headless FR-10-T11 live railgun-damage fairness gate."""

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


SCHEMA = "worr.networking.rewind-rail-damage-runtime.v2"
MAP_NAME = "worr_fr10_rewind_mover"
STATUS_CVAR = "sg_worr_rewind_rail_damage_selftest_status"
STATUS_RE = re.compile(
    rf'{re.escape(STATUS_CVAR)}\s+"(?P<value>(?:pass|fail):[0-9:]+)"'
)
STATUS_FIELDS = (
    "status",
    "setup_ready",
    "history_ready",
    "current_world_miss",
    "rejected_current_fallback",
    "rejected_no_damage",
    "legacy_rewind_selected",
    "rail_policy_observed",
    "near_latency_hit",
    "bounded_latency_hit",
    "capped_latency_hit",
    "damage_applied",
    "geometry_unchanged",
    "query_authority_unchanged",
    "candidate_count",
    "damage_amount",
    "current_fraction_q6",
    "near_latency_fraction_q6",
    "bounded_latency_fraction_q6",
    "capped_latency_fraction_q6",
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
    """Build a dedicated-only launch with no client or renderer path."""
    return [
        str(dedicated_exe),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "deathmatch", "1",
        "+set", "maxclients", "2",
        "+set", "g_lag_compensation", "1",
        "+set", "sg_lag_compensation_debug", "2",
        "+map", MAP_NAME,
        "+addbot", "RewindRailShooter",
        "+addbot", "RewindRailTarget",
        # The second bot joins on the following server frame.
        "+wait", "2",
        "+sv", "worr_rewind_rail_damage_arm",
        # This yields ordinary end-frame player and legacy-frame captures.
        # It is intentionally longer than spawn protection so the real Damage
        # call below has no test-only protection bypass.
        "+wait", "48",
        "+sv", "worr_rewind_rail_damage_selftest",
        "+cvarlist", STATUS_CVAR,
    ]


def parse_status(text: str) -> dict[str, int | str]:
    matches = list(STATUS_RE.finditer(text))
    if len(matches) != 1:
        raise RuntimeError(
            "expected exactly one rail damage runtime status row; "
            f"observed={len(matches)}"
        )
    values = matches[0].group("value").split(":")
    if len(values) != len(STATUS_FIELDS):
        raise RuntimeError(
            "rail damage runtime status field count changed: "
            f"observed={len(values)} expected={len(STATUS_FIELDS)}"
        )
    record: dict[str, int | str] = {"status": values[0]}
    for name, value in zip(STATUS_FIELDS[1:], values[1:], strict=True):
        if not value.isdecimal():
            raise RuntimeError(
                f"rail damage runtime status {name} is not decimal: {value!r}"
            )
        record[name] = int(value)
    return record


def validate_status(status: dict[str, int | str]) -> dict[str, int | str]:
    if status["status"] != "pass":
        raise RuntimeError(f"rail damage runtime probe reported {status['status']!r}")
    for name in (
        "setup_ready",
        "history_ready",
        "current_world_miss",
        "rejected_current_fallback",
        "rejected_no_damage",
        "legacy_rewind_selected",
        "rail_policy_observed",
        "near_latency_hit",
        "bounded_latency_hit",
        "capped_latency_hit",
        "damage_applied",
        "geometry_unchanged",
        "query_authority_unchanged",
    ):
        if status[name] != 1:
            raise RuntimeError(f"rail damage runtime probe did not prove {name}")
    if not isinstance(status["candidate_count"], int) or status["candidate_count"] < 1:
        raise RuntimeError("rail damage runtime probe found no historical candidates")
    if status["damage_amount"] != 30:
        raise RuntimeError("rail damage runtime probe did not apply all three rail hits")
    if status["current_fraction_q6"] != 1_000_000:
        raise RuntimeError("rail damage current-world baseline was not a full miss")
    for name in (
        "near_latency_fraction_q6",
        "bounded_latency_fraction_q6",
        "capped_latency_fraction_q6",
    ):
        fraction = status[name]
        if not isinstance(fraction, int) or not 0 < fraction < 1_000_000:
            raise RuntimeError(f"rail damage {name} did not hit")
    if status["failure_code"] != 0:
        raise RuntimeError("passing rail damage runtime probe retained a failure code")
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
            raise RuntimeError("dedicated rail damage runtime probe wrote stderr")
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
    if not (working_dir / "basew" / "maps" / f"{MAP_NAME}.bsp").is_file():
        parser.error("staged rail-damage fixture is missing from the working directory")

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
        if any(status != statuses[0] for status in statuses[1:]):
            raise RuntimeError("rail damage runtime evidence was not deterministic")
        report: dict[str, object] = {
            "schema": SCHEMA,
            "run_id": run_id,
            "started_at_utc": started.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "dedicated_executable": str(dedicated_exe),
            "dedicated_sha256": file_sha256(dedicated_exe),
            "working_directory": str(working_dir),
            "command": command,
            "repeat": args.repeat,
            "status": statuses[0],
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
            "working_directory": str(working_dir),
            "command": command,
            "error_type": type(error).__name__,
            "error": str(error),
        }
        write_json_atomic(run_root / "failure.json", failure)
        write_json_atomic(failure_output, failure)
        print(
            f"rewind rail damage runtime gate failed: {type(error).__name__}: {error}",
            file=sys.stderr,
        )
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
