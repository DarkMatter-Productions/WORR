#!/usr/bin/env python3
"""Run staged default-off and impaired loopback networking profiles."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
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


COUNTERS_RE = re.compile(
    r"net_impair counters: seen=(?P<seen>\d+) dropped=(?P<dropped>\d+) "
    r"burst_dropped=(?P<burst_dropped>\d+) reordered=(?P<reordered>\d+) "
    r"duplicated=(?P<duplicated>\d+) corrupted=(?P<corrupted>\d+) "
    r"upstream_stalled=(?P<upstream_stalled>\d+) "
    r"throttled=(?P<throttled>\d+) overflow=(?P<overflow>\d+) "
    r"resets=(?P<resets>\d+)"
)

ADAPTIVE_INPUT_RE = re.compile(
    r"adaptive input: enabled=(?P<enabled>[01]) active=(?P<active>[01]) "
    r"decision_valid=(?P<decision_valid>[01]) fallbacks=(?P<fallbacks>\d+)"
)

ADAPTIVE_INPUT_TELEMETRY_RE = re.compile(
    r"evaluations=(?P<evaluations>\d+) windows=(?P<windows>\d+) "
    r"held=(?P<held>\d+) recovery_holds=(?P<recovery_holds>\d+) "
    r"counter_resets=(?P<counter_resets>\d+) "
    r"clock_resets=(?P<clock_resets>\d+) "
    r"received=(?P<received>\d+) dropped=(?P<dropped>\d+)"
)

SNAPSHOT_RECOVERY_RE = re.compile(
    r"snapshot recovery: enabled=(?P<enabled>[01]) "
    r"active=(?P<active>[01]) exhausted=(?P<exhausted>[01]) "
    r"generation=(?P<generation>\d+) reasons=0x(?P<reasons>[0-9a-fA-F]+) "
    r"legacy_streak=(?P<legacy_streak>\d+) "
    r"canonical_streak=(?P<canonical_streak>\d+) "
    r"attempts=(?P<attempts>\d+) cooldown=(?P<cooldown>\d+) "
    r"arms=(?P<arms>\d+) decisions=(?P<decisions>\d+) "
    r"recoveries=(?P<recoveries>\d+) overrides=(?P<overrides>\d+) "
    r"inherited=(?P<inherited>\d+) disabled=(?P<disabled>\d+) "
    r"ignored_nontransport=(?P<ignored_nontransport>\d+) "
    r"last_result=(?P<last_result>\d+)"
)

SNAPSHOT_SHADOW_RE = re.compile(
    r"snapshot shadow: active=(?P<active>[01]) "
    r"lifecycle=(?P<lifecycle>\d+) epoch=(?P<epoch>\d+) "
    r"pending=(?P<pending>[01]) last_result=(?P<last_result>\d+) "
    r"capture_failure=(?P<capture_failure>\d+) "
    r"parity_last=0x(?P<parity_last>[0-9a-fA-F]+) "
    r"accept_flags=0x(?P<accept_flags>[0-9a-fA-F]+) "
    r"consumer=(?P<consumer>[01]) "
    r"consumer_last_rejection=(?P<consumer_last_rejection>\d+)"
)

SNAPSHOT_SHADOW_TELEMETRY_RE = re.compile(
    r"snapshot shadow telemetry: attempts=(?P<attempts>\d+) "
    r"projected=(?P<projected>\d+) published=(?P<published>\d+) "
    r"lineage_only=(?P<lineage_only>\d+) "
    r"promotion_eligible=(?P<promotion_eligible>\d+) "
    r"comparisons=(?P<comparisons>\d+) mismatches=(?P<mismatches>\d+) "
    r"entity_mismatches=(?P<entity_mismatches>\d+) "
    r"frame_failures=(?P<frame_failures>\d+) "
    r"capture_overflows=(?P<capture_overflows>\d+) "
    r"promotion_blocks=(?P<promotion_blocks>\d+) "
    r"consumer_attempts=(?P<consumer_attempts>\d+) "
    r"consumer_accepts=(?P<consumer_accepts>\d+) "
    r"consumer_rejections=(?P<consumer_rejections>\d+)"
)


def parse_counter_sets(text: str) -> list[dict[str, int]]:
    return [
        {name: int(value) for name, value in match.groupdict().items()}
        for match in COUNTERS_RE.finditer(text)
    ]


def parse_last_status(pattern: re.Pattern[str], text: str) -> dict[str, int]:
    matches = list(pattern.finditer(text))
    if not matches:
        raise RuntimeError(f"staged client omitted status matching {pattern.pattern!r}")
    values: dict[str, int] = {}
    for name, value in matches[-1].groupdict().items():
        values[name] = int(
            value,
            16 if name in ("reasons", "parity_last", "accept_flags") else 10,
        )
    return values


def terminate(process: subprocess.Popen[object]) -> bool:
    """Terminate a still-running test process; return whether we did it."""
    return terminate_process_tree(process)


def client_command(executable: Path, impaired: bool) -> list[str]:
    """Build a hidden, input-free client command for automated evidence."""
    command = [
        str(executable),
        "+set", "game", "basew",
        "+set", "developer", "1",
        # The Windows client keeps a valid hidden presentation surface for
        # automated engine work. Disable input before startup so it never
        # initializes or captures the user's mouse.
        "+set", "win_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "vid_renderer", "opengl",
        "+set", "vid_fullscreen", "0",
        "+set", "vid_geometry", "640x480+0+0",
        "+set", "net_dropsim", "0",
        "+set", "net_impair_enable", "1" if impaired else "0",
        "+set", "cl_adaptive_input", "1" if impaired else "0",
        "+set", "cl_snapshot_recovery", "1" if impaired else "0",
    ]
    if impaired:
        command.extend(
            [
                "+set", "net_impair_seed", "424242",
                "+set", "net_impair_latency_ms", "25",
                "+set", "net_impair_jitter_ms", "5",
                "+set", "net_impair_loss_pct", "1",
                "+set", "net_impair_burst_loss_pct", "0.1",
                "+set", "net_impair_burst_length", "3",
                "+set", "net_impair_reorder_pct", "0.5",
                "+set", "net_impair_duplicate_pct", "0.5",
                "+set", "net_impair_upstream_stall_ms", "20",
                "+set", "net_impair_rate_kbps", "1024",
            ]
        )
    command.extend(
        [
            "+map", "mm-rage",
            "+wait", "600",
            "+cl_adaptive_input_status",
            "+cl_snapshot_shadow_status",
            "+cl_snapshot_recovery_status",
            "+net_impair_status",
            "+wait", "1000",
        ]
    )
    return command


def run_client(
    command: list[str],
    working_dir: Path,
    stdout_path: Path,
    stderr_path: Path,
    timeout: float,
    creation_flags: int,
    completion_marker: str | None = None,
    failure_markers: tuple[str, ...] = (),
) -> tuple[str, str, dict[str, int], bool]:
    with stdout_path.open("w", encoding="utf-8") as stdout_file, \
         stderr_path.open("w", encoding="utf-8") as stderr_file:
        process = start_headless_process(
            command,
            cwd=working_dir,
            stdin=subprocess.DEVNULL,
            stdout=stdout_file,
            stderr=stderr_file,
            creationflags=creation_flags,
        )

        deadline = time.monotonic() + timeout
        evidence_seen_while_alive = False
        spawned_seen = False
        counters_seen = False
        marker_seen = completion_marker is None
        failure_seen: str | None = None
        read_offset = 0
        scan_tail = ""
        exited_before_evidence = False
        while time.monotonic() < deadline:
            if process.poll() is not None:
                exited_before_evidence = True
                break
            stdout_file.flush()
            with stdout_path.open(
                "r", encoding="utf-8", errors="replace"
            ) as reader:
                reader.seek(read_offset)
                appended = reader.read()
                read_offset = reader.tell()
            scan = scan_tail + appended
            if "Going from cs_primed to cs_spawned" in scan:
                spawned_seen = True
            if parse_counter_sets(scan):
                counters_seen = True
            # Failure evidence always wins within an appended chunk.  A drop
            # line and completion echo can be flushed together, so suppressing
            # this scan after observing the marker would create a false pass.
            for candidate in failure_markers:
                if candidate and candidate in scan:
                    failure_seen = candidate
                    break
            if failure_seen:
                break
            if completion_marker and completion_marker in scan:
                marker_seen = True
            if spawned_seen and counters_seen and marker_seen:
                evidence_seen_while_alive = True
                break
            scan_tail = scan[-16384:]
            time.sleep(0.1)

        terminated_by_harness = terminate(process)

    text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    counters = parse_counter_sets(text)
    if failure_seen:
        raise RuntimeError(
            "staged client emitted a failure marker before completion: "
            f"{failure_seen!r} (returncode={process.returncode})"
        )
    if not evidence_seen_while_alive:
        disposition = "exited" if exited_before_evidence else "timed out"
        raise RuntimeError(
            f"staged client {disposition} before live evidence "
            f"(returncode={process.returncode})"
        )
    if not terminated_by_harness:
        raise RuntimeError(
            "staged client exited after evidence before harness termination "
            f"(returncode={process.returncode})"
        )
    if not counters:
        raise RuntimeError("staged client did not emit impairment counters")
    return text, stderr_text, counters[-1], terminated_by_harness


def validate_common(text: str, stderr_text: str) -> None:
    if "Going from cs_primed to cs_spawned" not in text:
        raise RuntimeError("staged client did not reach cs_spawned")
    if "Connected to loopback (protocol 1038)" not in text:
        raise RuntimeError("staged client did not use protocol 1038")
    if "Game API version: 2025" not in text:
        raise RuntimeError("staged client did not load game API 2025")
    if stderr_text:
        raise RuntimeError("staged client wrote unexpected stderr output")


def validate_adapter_status(
    text: str, enabled: bool
) -> tuple[
    dict[str, int],
    dict[str, int],
    dict[str, int] | None,
    dict[str, int],
    dict[str, int],
]:
    adaptive = parse_last_status(ADAPTIVE_INPUT_RE, text)
    recovery = parse_last_status(SNAPSHOT_RECOVERY_RE, text)
    shadow = parse_last_status(SNAPSHOT_SHADOW_RE, text)
    shadow_telemetry = parse_last_status(SNAPSHOT_SHADOW_TELEMETRY_RE, text)
    telemetry = (
        parse_last_status(ADAPTIVE_INPUT_TELEMETRY_RE, text)
        if adaptive["active"] and adaptive["decision_valid"]
        else None
    )

    expected = 1 if enabled else 0
    if adaptive["enabled"] != expected or recovery["enabled"] != expected:
        raise RuntimeError("staged adapter cvar state did not match its profile")
    if adaptive["fallbacks"] != 0:
        raise RuntimeError("adaptive input used an integration fallback")
    if recovery["exhausted"] != 0 or recovery["last_result"] != 0:
        raise RuntimeError("snapshot recovery entered an invalid/exhausted state")
    if (
        shadow["active"] != 1
        or shadow["lifecycle"] != 2
        or shadow["last_result"] != 0
        or shadow["consumer"] != 1
        or shadow["accept_flags"] != 0x3
    ):
        raise RuntimeError(
            "canonical snapshot shadow/consumer was not healthy and active"
        )
    if shadow["pending"] != 0 or shadow["capture_failure"] != 0:
        raise RuntimeError("canonical snapshot shadow retained an incomplete frame")
    if (
        shadow_telemetry["attempts"] == 0
        or shadow_telemetry["projected"] == 0
        or shadow_telemetry["published"] == 0
        or shadow_telemetry["comparisons"] == 0
    ):
        raise RuntimeError("canonical snapshot shadow emitted no live parity sample")
    if (
        shadow["parity_last"] != 0
        or shadow_telemetry["mismatches"] != 0
        or shadow_telemetry["entity_mismatches"] != 0
        or shadow_telemetry["frame_failures"] != 0
        or shadow_telemetry["capture_overflows"] != 0
    ):
        raise RuntimeError("canonical snapshot shadow failed live legacy parity")
    if (
        shadow_telemetry["consumer_attempts"] == 0
        or shadow_telemetry["consumer_accepts"] == 0
        or shadow_telemetry["consumer_rejections"] != 0
        or shadow["consumer_last_rejection"] != 0
    ):
        raise RuntimeError(
            "canonical snapshot consumer did not accept live frames "
            f"(attempts={shadow_telemetry['consumer_attempts']} "
            f"accepts={shadow_telemetry['consumer_accepts']} "
            f"rejections={shadow_telemetry['consumer_rejections']} "
            f"last_rejection={shadow['consumer_last_rejection']})"
        )

    if enabled:
        if adaptive["active"] != 1 or adaptive["decision_valid"] != 1:
            raise RuntimeError("adaptive input did not produce a live decision")
        if telemetry is None or telemetry["evaluations"] == 0 or telemetry["windows"] == 0:
            raise RuntimeError("adaptive input emitted no live evaluation window")
    elif adaptive["active"] != 0 or adaptive["decision_valid"] != 0:
        raise RuntimeError("default-off adaptive input became active")

    if not enabled and any(
        recovery[name] != 0
        for name in (
            "active", "exhausted", "generation", "reasons", "legacy_streak",
            "canonical_streak", "attempts", "cooldown", "arms", "decisions",
            "recoveries", "overrides", "inherited", "disabled",
            "ignored_nontransport", "last_result",
        )
    ):
        raise RuntimeError("clean control profile exercised snapshot recovery")

    return adaptive, recovery, telemetry, shadow, shadow_telemetry


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-exe", required=True, type=Path)
    parser.add_argument("--dedicated-exe", required=True, type=Path)
    parser.add_argument("--working-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    impaired_stdout = args.output.with_suffix(".stdout.log")
    impaired_stderr = args.output.with_suffix(".stderr.log")
    control_stdout = args.output.with_suffix(".control.stdout.log")
    control_stderr = args.output.with_suffix(".control.stderr.log")
    queue_stdout = args.output.with_suffix(".queue.stdout.log")
    queue_stderr = args.output.with_suffix(".queue.stderr.log")

    creation_flags = _headless_creation_flags()
    queue_run = subprocess.run(
        [
            str(args.dedicated_exe),
            "+set", "game", "basew",
            "+set", "net_impair_queue_limit", "7",
            "+net_impair_queue_selftest",
            "+quit",
        ],
        cwd=args.working_dir,
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        timeout=20,
        creationflags=creation_flags,
    )
    queue_stdout.write_text(queue_run.stdout, encoding="utf-8")
    queue_stderr.write_text(queue_run.stderr, encoding="utf-8")
    queue_marker = (
        "net_impair_queue_selftest: pass capacity=7 high_water=7 overflow=1"
    )
    if queue_run.returncode != 0 or queue_marker not in queue_run.stdout:
        raise RuntimeError("dedicated impairment queue self-test failed")
    if queue_run.stderr:
        raise RuntimeError("dedicated impairment queue self-test wrote stderr")

    control = run_client(
        client_command(args.client_exe, impaired=False),
        args.working_dir,
        control_stdout,
        control_stderr,
        args.timeout,
        creation_flags,
    )
    validate_common(control[0], control[1])
    (
        control_adaptive,
        control_recovery,
        control_adaptive_telemetry,
        control_shadow,
        control_shadow_telemetry,
    ) = (
        validate_adapter_status(control[0], enabled=False)
    )
    control_counters = control[2]
    if any(
        control_counters[name] != 0
        for name in (
            "seen", "dropped", "reordered", "duplicated",
            "upstream_stalled", "overflow",
        )
    ):
        raise RuntimeError("default-off control did not preserve raw routing")

    impaired = run_client(
        client_command(args.client_exe, impaired=True),
        args.working_dir,
        impaired_stdout,
        impaired_stderr,
        args.timeout,
        creation_flags,
    )
    validate_common(impaired[0], impaired[1])
    (
        impaired_adaptive,
        impaired_recovery,
        impaired_adaptive_telemetry,
        impaired_shadow,
        impaired_shadow_telemetry,
    ) = (
        validate_adapter_status(impaired[0], enabled=True)
    )
    impaired_counters = impaired[2]
    for counter in (
        "seen", "dropped", "reordered", "duplicated", "upstream_stalled",
    ):
        if impaired_counters[counter] <= 0:
            raise RuntimeError(f"impaired profile produced no {counter} events")
    if impaired_counters["overflow"] != 0:
        raise RuntimeError("impaired profile overflowed the bounded packet queue")

    report = {
        "schema": "worr.networking.impairment-runtime.v3",
        "connected_protocol": 1038,
        "game_api": 2025,
        "cgame_api": 2028,
        "spawned": True,
        "default_off_profile": {
            "counters": control_counters,
            "adaptive_input": control_adaptive,
            "adaptive_input_telemetry": control_adaptive_telemetry,
            "snapshot_recovery": control_recovery,
            "snapshot_shadow": control_shadow,
            "snapshot_shadow_telemetry": control_shadow_telemetry,
            "terminated_by_harness": control[3],
            "stdout": str(control_stdout),
            "stderr": str(control_stderr),
        },
        "impaired_profile": {
            "counters": impaired_counters,
            "adaptive_input": impaired_adaptive,
            "adaptive_input_telemetry": impaired_adaptive_telemetry,
            "snapshot_recovery": impaired_recovery,
            "snapshot_shadow": impaired_shadow,
            "snapshot_shadow_telemetry": impaired_shadow_telemetry,
            "terminated_by_harness": impaired[3],
            "stdout": str(impaired_stdout),
            "stderr": str(impaired_stderr),
        },
        "queue_selftest": {
            "capacity": 7,
            "high_water": 7,
            "overflow": 1,
            "ordered_release": True,
            "slot_reuse": True,
            "stdout": str(queue_stdout),
            "stderr": str(queue_stderr),
        },
    }
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
