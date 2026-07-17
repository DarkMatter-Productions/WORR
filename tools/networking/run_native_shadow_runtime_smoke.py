#!/usr/bin/env python3
"""Run the staged two-process native command-shadow acceptance gate.

This gate intentionally runs two sequential client/server trials over IPv4 UDP
localhost.  Unlike an in-process ``map`` client, that route reaches the
dedicated server's real per-client rate limiter.  The first trial proves the
fragment-owner path; a fresh second connection proves the post-burst async ACK
wake.  Both use bounded reliable SVC_PRINT traffic, one short ``stuffall``
control, stable V1 status rows, and exact client retention release.

The pilot remains diagnostic-only: legacy MOVE/BATCH_MOVE is authoritative.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import time
from contextlib import ExitStack
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Mapping, Sequence

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


SCHEMA = "worr.networking.native-shadow-runtime.v1"
FAILURE_SCHEMA = "worr.networking.native-shadow-runtime-failure.v1"
PROTOCOL = 1038
# Use a stock single-player map so this transport-only gate does not enter the
# multiplayer welcome/menu service.  That service deliberately emits large
# UI stufftext batches and is independent of the native carrier proof.
MAP_NAME = "base1"
ADDRESS = "127.0.0.1"
NET_MAXMSGLEN = 512
FRAGMENT_RATE_BYTES_PER_SECOND = 1500
# Every staged datagram is below 1,000 bytes, so this rate makes the integer
# async send-delay calculation zero.  The second trial therefore proves the
# async owner by construction instead of racing the synchronous snapshot tick.
ASYNC_RATE_BYTES_PER_SECOND = 1_000_000
SERVER_FPS = 40
PUBLIC_MASK = 0x03
PRIVATE_MASK = 0x53
IMPAIR_LATENCY_MS = 25
CLIENT_IMPAIR_SEED = 424242
SERVER_IMPAIR_SEED = 817263
IMPAIR_QUEUE_LIMIT = 1024
RELIABLE_RECORD_BYTES = 800
FRAGMENT_RELIABLE_RECORD_COUNT = 16
ASYNC_RELIABLE_RECORD_COUNT = 8
SERVER_QUEUE_WAIT_FRAMES = 1000      # 25 s; drain join/welcome reliables first.
SERVER_BURST_DELAY_FRAMES = 80       # 2 s after stuffed client control.
FRAGMENT_CLIENT_RELEASE_WAIT_FRAMES = 124  # 2 s after client control.
ASYNC_CLIENT_RELEASE_WAIT_FRAMES = 186     # 3 s, after the smaller burst.
SERVER_FINAL_WAIT_FRAMES = 600       # 15 s after queueing the burst.
CLIENT_FINAL_WAIT_FRAMES = 620       # 10 s after releasing DATA.
CLIENT_REHOLD_WAIT_FRAMES = 2        # Freeze sampling after first enqueue.
WAIT_LIMIT = 1000
WINDOWS_COMMAND_LINE_LIMIT = 30_000
TRIAL_TIMEOUT_SECONDS = 55.0

FRAGMENT_TRIAL = "fragment_pressure"
ASYNC_TRIAL = "post_burst_async_ack"


@dataclass(frozen=True)
class TrialSpec:
    name: str
    reliable_record_count: int
    client_release_wait_frames: int
    rate_bytes_per_second: int
    required_server_evidence: tuple[str, ...]


TRIAL_SPECS = (
    TrialSpec(
        name=FRAGMENT_TRIAL,
        reliable_record_count=FRAGMENT_RELIABLE_RECORD_COUNT,
        client_release_wait_frames=FRAGMENT_CLIENT_RELEASE_WAIT_FRAMES,
        rate_bytes_per_second=FRAGMENT_RATE_BYTES_PER_SECOND,
        required_server_evidence=(
            "async_rate_deferrals",
            "async_fragment_deferrals",
        ),
    ),
    TrialSpec(
        name=ASYNC_TRIAL,
        reliable_record_count=ASYNC_RELIABLE_RECORD_COUNT,
        client_release_wait_frames=ASYNC_CLIENT_RELEASE_WAIT_FRAMES,
        rate_bytes_per_second=ASYNC_RATE_BYTES_PER_SECOND,
        required_server_evidence=(
            "async_wake_attempts",
            "async_ack_handoffs",
        ),
    ),
)
TRIAL_NAMES = tuple(spec.name for spec in TRIAL_SPECS)

CLIENT_STATUS_PREFIX = "WORR_NATIVE_CLIENT_STATUS_V1"
SERVER_STATUS_PREFIX = "WORR_NATIVE_SERVER_STATUS_V1"

CLIENT_STATUS_FIELDS = (
    "schema",
    "enabled",
    "mode",
    "hooks",
    "capability_confirmed",
    "readiness_phase",
    "official_epoch",
    "transport_epoch",
    "protocol",
    "public_mask",
    "private_mask",
    "probe_hold",
    "cancelled_through_epoch",
    "cancellation_barriers",
    "cancelled_transports",
    "cancelled_command_tx",
    "cancelled_event_rx",
    "cancelled_event_receipts",
    "stale_cancelled_carriers",
    "stale_cancelled_readiness_records",
    "challenges",
    "client_ready_queued",
    "server_active",
    "proof_enqueued",
    "retained",
    "retained_highwater",
    "retained_releases",
    "tx_first_sends",
    "tx_retries",
    "tx_handoffs",
    "ack_carriers",
    "acknowledged_reliable",
    "drains",
    "failures",
    "last_failure",
)

SERVER_STATUS_FIELDS = (
    "schema",
    "slot",
    "protocol",
    "enabled",
    "lifecycle",
    "hooks",
    "readiness_phase",
    "official_epoch",
    "transport_epoch",
    "cancelled_through_epoch",
    "public_mask",
    "private_mask",
    "wire_committed",
    "wire_committed_transport_epoch",
    "challenges_queued",
    "client_ready",
    "server_active",
    "rx_carriers",
    "rx_commits",
    "rx_repeat_refreshes",
    "legacy_joins",
    "command_matches",
    "command_mismatches",
    "sample_mismatches",
    "ack_eligible",
    "ack_prepares",
    "ack_handoffs",
    "async_rate_deferrals",
    "async_fragment_deferrals",
    "async_wake_attempts",
    "async_ack_handoffs",
    "async_wake_no_handoff",
    "rx_rejections",
    "tx_ack_rejections",
    "rx_drained",
    "drains",
    "failures",
    "cancellation_barriers",
    "cancelled_transports",
    "cancelled_rx_messages",
    "cancelled_receipts",
    "cancelled_event_records",
    "stale_cancelled_carriers",
    "stale_cancelled_readiness_records",
    "last_failure",
)

STATUS_NUMBER_RE = re.compile(r"(?:0[xX][0-9a-fA-F]+|[0-9]+)\Z")
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")

IMPAIR_CONFIG_RE = re.compile(
    r"net_impair: enabled=(?P<enabled>\d+) seed=(?P<seed>-?\d+) "
    r"latency=(?P<latency>\d+) jitter=(?P<jitter>\d+) "
    r"loss=(?P<loss>\d+(?:\.\d+)?) "
    r"burst=(?P<burst>\d+(?:\.\d+)?)/(?P<burst_length>\d+) "
    r"reorder=(?P<reorder>\d+(?:\.\d+)?) "
    r"duplicate=(?P<duplicate>\d+(?:\.\d+)?) "
    r"corrupt=(?P<corrupt>\d+(?:\.\d+)?) "
    r"upstream_stall=(?P<upstream_stall>\d+) "
    r"rate_kbps=(?P<rate_kbps>\d+) "
    r"queue=(?P<queue_current>\d+)/(?P<queue_limit>\d+) "
    r"high_water=(?P<high_water>\d+)"
)

IMPAIR_COUNTERS_RE = re.compile(
    r"net_impair counters: seen=(?P<seen>\d+) "
    r"dropped=(?P<dropped>\d+) "
    r"burst_dropped=(?P<burst_dropped>\d+) "
    r"reordered=(?P<reordered>\d+) "
    r"duplicated=(?P<duplicated>\d+) "
    r"corrupted=(?P<corrupted>\d+) "
    r"upstream_stalled=(?P<upstream_stalled>\d+) "
    r"throttled=(?P<throttled>\d+) "
    r"overflow=(?P<overflow>\d+) resets=(?P<resets>\d+)"
)

FAILURE_MARKERS = (
    "invalid canonical command stream",
    "was dropped:",
    "Server disconnected",
    "Fatal error",
    "ERROR:",
    "Cbuf_AddText: overflow",
    "reliable queue overflowed",
)

RUNTIME_COMPONENTS = (
    ("client_executable", Path("worr_x86_64.exe")),
    ("dedicated_executable", Path("worr_ded_x86_64.exe")),
    ("client_engine", Path("worr_engine_x86_64.dll")),
    ("dedicated_engine", Path("worr_ded_engine_x86_64.dll")),
    ("cgame", Path("basew/cgame_x86_64.dll")),
    ("sgame", Path("basew/sgame_x86_64.dll")),
    ("renderer", Path("worr_opengl_x86_64.dll")),
)
RUNTIME_ROLES = tuple(role for role, _ in RUNTIME_COMPONENTS)
ENDPOINT_LOG_ROLES = (
    "client_stdout", "client_stderr", "server_stdout", "server_stderr"
)
LOG_ROLES = tuple(
    f"{trial}_{role}"
    for trial in TRIAL_NAMES
    for role in ENDPOINT_LOG_ROLES
)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def argv_sha256(argv: Sequence[str]) -> str:
    digest = hashlib.sha256()
    for argument in argv:
        digest.update(argument.encode("utf-8"))
        digest.update(b"\0")
    return digest.hexdigest()


def make_file_record(role: str, path: Path) -> dict[str, object]:
    resolved = path.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"required native-shadow artifact missing: {resolved}")
    before = resolved.stat()
    sha256 = file_sha256(resolved)
    after = resolved.stat()
    if (
        before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
    ):
        raise RuntimeError(
            f"native-shadow artifact changed while hashing: {resolved}"
        )
    return {
        "role": role,
        "path": str(resolved),
        "bytes": after.st_size,
        "mtime_ns": after.st_mtime_ns,
        "sha256": sha256,
    }


def validate_file_records(
    records: object,
    expected_roles: Iterable[str],
) -> None:
    if not isinstance(records, list):
        raise RuntimeError("artifact manifest must be a list")
    expected = set(expected_roles)
    seen: set[str] = set()
    for raw in records:
        if not isinstance(raw, dict):
            raise RuntimeError("artifact manifest contains a non-object record")
        role = raw.get("role")
        if not isinstance(role, str) or role not in expected:
            raise RuntimeError(f"artifact manifest has an unexpected role: {role!r}")
        if role in seen:
            raise RuntimeError(f"artifact manifest duplicates role {role!r}")
        seen.add(role)
        path_value = raw.get("path")
        sha256 = raw.get("sha256")
        byte_count = raw.get("bytes")
        mtime_ns = raw.get("mtime_ns")
        if not isinstance(path_value, str) or not path_value:
            raise RuntimeError(f"artifact {role!r} has no path")
        if not isinstance(sha256, str) or not SHA256_RE.fullmatch(sha256):
            raise RuntimeError(f"artifact {role!r} has an invalid SHA-256")
        if not isinstance(byte_count, int) or byte_count < 0:
            raise RuntimeError(f"artifact {role!r} has an invalid byte count")
        if not isinstance(mtime_ns, int) or mtime_ns < 0:
            raise RuntimeError(f"artifact {role!r} has an invalid mtime")
        path = Path(path_value)
        if not path.is_file():
            raise RuntimeError(f"artifact {role!r} no longer exists: {path}")
        stat = path.stat()
        if stat.st_size != byte_count:
            raise RuntimeError(f"artifact {role!r} byte count changed")
        if stat.st_mtime_ns != mtime_ns:
            raise RuntimeError(f"artifact {role!r} mtime changed")
        if file_sha256(path) != sha256:
            raise RuntimeError(f"artifact {role!r} SHA-256 mismatch")
    if seen != expected:
        missing = sorted(expected - seen)
        raise RuntimeError(f"artifact manifest is missing roles: {missing}")


def runtime_manifest(working_dir: Path) -> list[dict[str, object]]:
    return [
        make_file_record(role, working_dir / relative)
        for role, relative in RUNTIME_COMPONENTS
    ]


def parse_status_row(
    text: str,
    prefix: str,
    required_fields: Sequence[str],
) -> dict[str, int]:
    lines = [line for line in text.splitlines() if prefix in line]
    if not lines:
        raise RuntimeError(f"missing {prefix} row")
    if len(lines) != 1:
        raise RuntimeError(f"duplicate {prefix} rows: {len(lines)}")
    line = lines[0]
    if line.count(prefix) != 1:
        raise RuntimeError(f"duplicate {prefix} prefix in one row")
    tail = line.split(prefix, 1)[1].strip()
    values: dict[str, int] = {}
    for token in tail.split():
        if token.count("=") != 1:
            raise RuntimeError(f"malformed {prefix} token: {token!r}")
        name, raw = token.split("=", 1)
        if not name or name in values:
            raise RuntimeError(f"duplicate or empty {prefix} field: {name!r}")
        if not STATUS_NUMBER_RE.fullmatch(raw):
            raise RuntimeError(
                f"non-numeric {prefix} field {name!r}: {raw!r}"
            )
        values[name] = int(raw, 16 if raw.lower().startswith("0x") else 10)
    missing = [name for name in required_fields if name not in values]
    if missing:
        raise RuntimeError(f"{prefix} row is missing fields: {missing}")
    return values


def parse_client_status(text: str) -> dict[str, int]:
    return parse_status_row(text, CLIENT_STATUS_PREFIX, CLIENT_STATUS_FIELDS)


def parse_server_status(text: str) -> dict[str, int]:
    return parse_status_row(text, SERVER_STATUS_PREFIX, SERVER_STATUS_FIELDS)


def _parse_single_regex(
    pattern: re.Pattern[str],
    text: str,
    description: str,
) -> dict[str, str]:
    matches = list(pattern.finditer(text))
    if not matches:
        raise RuntimeError(f"missing {description}")
    if len(matches) != 1:
        raise RuntimeError(f"duplicate {description}: {len(matches)}")
    return matches[0].groupdict()


def parse_impairment_status(text: str) -> tuple[dict[str, object], dict[str, int]]:
    raw_config = _parse_single_regex(
        IMPAIR_CONFIG_RE, text, "net_impair profile row"
    )
    raw_counters = _parse_single_regex(
        IMPAIR_COUNTERS_RE, text, "net_impair counters row"
    )
    float_fields = {"loss", "burst", "reorder", "duplicate", "corrupt"}
    config: dict[str, object] = {
        name: float(value) if name in float_fields else int(value)
        for name, value in raw_config.items()
    }
    counters = {name: int(value) for name, value in raw_counters.items()}
    return config, counters


def validate_impairment_status(
    config: Mapping[str, object],
    counters: Mapping[str, int],
    *,
    expected_seed: int,
) -> None:
    expected_config: dict[str, object] = {
        "enabled": 1,
        "seed": expected_seed,
        "latency": IMPAIR_LATENCY_MS,
        "jitter": 0,
        "loss": 0.0,
        "burst": 0.0,
        "burst_length": 3,
        "reorder": 0.0,
        "duplicate": 0.0,
        "corrupt": 0.0,
        "upstream_stall": 0,
        "rate_kbps": 0,
        "queue_limit": IMPAIR_QUEUE_LIMIT,
    }
    for name, expected in expected_config.items():
        if config.get(name) != expected:
            raise RuntimeError(
                f"net_impair profile mismatch: {name}={config.get(name)!r} "
                f"expected={expected!r}"
            )
    queue_current = config.get("queue_current")
    high_water = config.get("high_water")
    if not isinstance(queue_current, int) or queue_current < 0:
        raise RuntimeError("net_impair queue occupancy is invalid")
    if not isinstance(high_water, int) or high_water <= 0:
        raise RuntimeError("net_impair latency queue was never exercised")
    if counters.get("seen", 0) <= 0:
        raise RuntimeError("net_impair observed no packets")
    for name in (
        "dropped",
        "burst_dropped",
        "reordered",
        "duplicated",
        "corrupted",
        "upstream_stalled",
        "throttled",
        "overflow",
        "resets",
    ):
        if counters.get(name) != 0:
            raise RuntimeError(
                f"deterministic latency-only profile has nonzero {name}"
            )


def validate_client_status(status: Mapping[str, int]) -> None:
    exact = {
        "schema": 1,
        "enabled": 1,
        "mode": 2,
        "hooks": 1,
        "capability_confirmed": 1,
        "readiness_phase": 5,
        "protocol": PROTOCOL,
        "public_mask": PUBLIC_MASK,
        "private_mask": PRIVATE_MASK,
        "probe_hold": 1,
        "cancelled_through_epoch": 0,
        "cancellation_barriers": 1,
        "cancelled_transports": 0,
        "cancelled_command_tx": 0,
        "cancelled_event_rx": 0,
        "cancelled_event_receipts": 0,
        "stale_cancelled_carriers": 0,
        "stale_cancelled_readiness_records": 0,
        "client_ready_queued": 1,
        "proof_enqueued": 1,
        "retained": 0,
        "retained_highwater": 1,
        "retained_releases": 1,
        "tx_first_sends": 1,
        "acknowledged_reliable": 1,
        "drains": 0,
        "failures": 0,
        "last_failure": 0,
    }
    for name, expected in exact.items():
        if status.get(name) != expected:
            raise RuntimeError(
                f"client status mismatch: {name}={status.get(name)!r} "
                f"expected={expected}"
            )
    for name in ("official_epoch", "transport_epoch", "challenges",
                 "server_active", "tx_handoffs", "ack_carriers"):
        if status.get(name, 0) <= 0:
            raise RuntimeError(f"client status has no {name} evidence")
    if status.get("tx_handoffs", 0) < status.get("tx_first_sends", 0):
        raise RuntimeError("client DATA handoff accounting regressed")
    if status.get("tx_retries", 0) > status.get("tx_handoffs", 0):
        raise RuntimeError("client retry count exceeds DATA handoffs")


def validate_server_status(
    status: Mapping[str, int],
    *,
    required_evidence: Sequence[str] = (),
) -> None:
    exact = {
        "schema": 1,
        "protocol": PROTOCOL,
        "enabled": 1,
        "lifecycle": 2,
        "hooks": 1,
        "readiness_phase": 4,
        "cancelled_through_epoch": 0,
        "public_mask": PUBLIC_MASK,
        "private_mask": PRIVATE_MASK,
        "wire_committed": 1,
        "challenges_queued": 1,
        "client_ready": 1,
        "server_active": 1,
        "rx_commits": 1,
        "legacy_joins": 1,
        "command_matches": 1,
        "command_mismatches": 0,
        "sample_mismatches": 0,
        "ack_eligible": 0,
        "rx_rejections": 0,
        "tx_ack_rejections": 0,
        "rx_drained": 0,
        "async_wake_no_handoff": 0,
        "drains": 0,
        "failures": 0,
        "cancellation_barriers": 1,
        "cancelled_transports": 0,
        "cancelled_rx_messages": 0,
        "cancelled_receipts": 0,
        "cancelled_event_records": 0,
        "stale_cancelled_carriers": 0,
        "stale_cancelled_readiness_records": 0,
        "last_failure": 0,
    }
    for name, expected in exact.items():
        if status.get(name) != expected:
            raise RuntimeError(
                f"server status mismatch: {name}={status.get(name)!r} "
                f"expected={expected}"
            )
    if status.get("slot", -1) < 0:
        raise RuntimeError("server status has an invalid client slot")
    if status.get("wire_committed_transport_epoch") != status.get(
        "transport_epoch"
    ):
        raise RuntimeError(
            "server status wire-committed epoch does not match the active transport"
        )
    for name in (
        "official_epoch",
        "transport_epoch",
        "rx_carriers",
        "ack_prepares",
        "ack_handoffs",
    ):
        if status.get(name, 0) <= 0:
            raise RuntimeError(f"server status has no {name} evidence")
    allowed_evidence = {
        "async_rate_deferrals",
        "async_fragment_deferrals",
        "async_wake_attempts",
        "async_ack_handoffs",
    }
    if any(name not in allowed_evidence for name in required_evidence):
        raise ValueError("unknown required server evidence field")
    for name in required_evidence:
        if status.get(name, 0) <= 0:
            raise RuntimeError(f"server status has no {name} evidence")
    if status.get("ack_prepares", 0) < status.get("ack_handoffs", 0):
        raise RuntimeError("server ACK handoffs exceed prepares")
    if status.get("ack_handoffs", 0) < status.get("async_ack_handoffs", 0):
        raise RuntimeError("server asynchronous ACK handoffs exceed all handoffs")
    if status.get("async_wake_attempts", 0) < status.get(
        "async_ack_handoffs", 0
    ):
        raise RuntimeError("server asynchronous ACK handoffs exceed wake attempts")
    if status.get("async_wake_attempts", 0) != (
        status.get("async_ack_handoffs", 0)
        + status.get("async_wake_no_handoff", 0)
    ):
        raise RuntimeError("server asynchronous wake accounting is inconsistent")


def validate_status_pair(
    client: Mapping[str, int],
    server: Mapping[str, int],
    *,
    required_server_evidence: Sequence[str] = (),
) -> None:
    validate_client_status(client)
    validate_server_status(
        server, required_evidence=required_server_evidence
    )
    for name in ("official_epoch", "transport_epoch", "protocol",
                 "public_mask", "private_mask"):
        if client.get(name) != server.get(name):
            raise RuntimeError(f"client/server status disagrees on {name}")
    if client["acknowledged_reliable"] != server["command_matches"]:
        raise RuntimeError("server command match did not release one client record")


def validate_terminal_order(
    text: str,
    *,
    status_prefix: str,
    completion_marker: str,
    failure_markers: Sequence[str] = FAILURE_MARKERS,
) -> None:
    marker_pattern = re.compile(
        rf"(?m)^[^\S\r\n]*{re.escape(completion_marker)}[^\S\r\n]*\r?$"
    )
    marker_matches = list(marker_pattern.finditer(text))
    marker_count = len(marker_matches)
    if marker_count != 1:
        raise RuntimeError(
            f"completion marker count is {marker_count}, expected exactly one"
        )
    status_index = text.find(status_prefix)
    marker_index = marker_matches[0].start()
    if status_index < 0 or status_index > marker_index:
        raise RuntimeError("status row did not precede the completion marker")
    for failure in failure_markers:
        index = text.find(failure)
        if index >= 0:
            relation = "after" if index > marker_index else "before"
            raise RuntimeError(
                f"failure marker {failure!r} appeared {relation} completion"
            )


def validate_runtime_text(
    client_text: str,
    server_text: str,
    *,
    client_marker: str,
    server_marker: str,
    port: int,
    reliable_record_count: int = FRAGMENT_RELIABLE_RECORD_COUNT,
    required_server_evidence: Sequence[str] = (),
) -> dict[str, object]:
    validate_terminal_order(
        client_text,
        status_prefix=CLIENT_STATUS_PREFIX,
        completion_marker=client_marker,
    )
    validate_terminal_order(
        server_text,
        status_prefix=SERVER_STATUS_PREFIX,
        completion_marker=server_marker,
    )
    if "Serverdata packet received" not in client_text:
        raise RuntimeError("client did not accept serverdata")
    if f"Connected to {ADDRESS}:{port}" not in client_text:
        raise RuntimeError("client did not report the reserved UDP endpoint")
    if f"protocol {PROTOCOL}" not in client_text:
        raise RuntimeError("client connection log did not report protocol 1038")
    if f"SpawnServer: {MAP_NAME}" not in server_text:
        raise RuntimeError("dedicated server did not spawn the acceptance map")
    if "Going from cs_primed to cs_spawned" not in server_text:
        raise RuntimeError("dedicated server did not admit the client to cs_spawned")

    reliable_delivery = validate_reliable_delivery(
        client_text, reliable_record_count=reliable_record_count
    )

    client_status = parse_client_status(client_text)
    server_status = parse_server_status(server_text)
    validate_status_pair(
        client_status,
        server_status,
        required_server_evidence=required_server_evidence,
    )
    client_impair, client_counters = parse_impairment_status(client_text)
    server_impair, server_counters = parse_impairment_status(server_text)
    validate_impairment_status(
        client_impair, client_counters, expected_seed=CLIENT_IMPAIR_SEED
    )
    validate_impairment_status(
        server_impair, server_counters, expected_seed=SERVER_IMPAIR_SEED
    )
    return {
        "client_status": client_status,
        "server_status": server_status,
        "client_impairment": {
            "config": client_impair,
            "counters": client_counters,
        },
        "server_impairment": {
            "config": server_impair,
            "counters": server_counters,
        },
        "reliable_delivery": reliable_delivery,
    }


def wait_commands(frames: int) -> list[str]:
    if frames <= 0:
        raise ValueError("wait frame count must be positive")
    result: list[str] = []
    remaining = frames
    while remaining:
        chunk = min(remaining, WAIT_LIMIT)
        result.extend(("+wait", str(chunk)))
        remaining -= chunk
    return result


def _common_impairment_settings(seed: int) -> list[str]:
    return [
        "+set", "net_dropsim", "0",
        "+set", "net_impair_enable", "1",
        "+set", "net_impair_seed", str(seed),
        "+set", "net_impair_latency_ms", str(IMPAIR_LATENCY_MS),
        "+set", "net_impair_jitter_ms", "0",
        "+set", "net_impair_loss_pct", "0",
        "+set", "net_impair_burst_loss_pct", "0",
        "+set", "net_impair_burst_length", "3",
        "+set", "net_impair_reorder_pct", "0",
        "+set", "net_impair_duplicate_pct", "0",
        "+set", "net_impair_corrupt_pct", "0",
        "+set", "net_impair_upstream_stall_ms", "0",
        "+set", "net_impair_rate_kbps", "0",
        "+set", "net_impair_queue_limit", str(IMPAIR_QUEUE_LIMIT),
    ]


def reliable_payloads(
    reliable_record_count: int = FRAGMENT_RELIABLE_RECORD_COUNT,
) -> list[str]:
    if reliable_record_count <= 0:
        raise ValueError("reliable record count must be positive")
    payloads: list[str] = []
    for index in range(reliable_record_count):
        prefix = f"WORR_NATIVE_RELIABLE_PRINT_{index:02d}_"
        suffix = ""
        filler_bytes = RELIABLE_RECORD_BYTES - len(prefix) - len(suffix)
        if filler_bytes <= 0:
            raise AssertionError("reliable record metadata exceeds its budget")
        payload = prefix + ("x" * filler_bytes) + suffix
        if len(payload.encode("ascii")) != RELIABLE_RECORD_BYTES:
            raise AssertionError("reliable record byte budget drifted")
        payloads.append(payload)
    return payloads


def validate_reliable_delivery(
    text: str,
    *,
    reliable_record_count: int = FRAGMENT_RELIABLE_RECORD_COUNT,
) -> dict[str, object]:
    """Require every numbered reliable print, complete and exactly once."""
    delivered: list[str] = []
    for index, payload in enumerate(
        reliable_payloads(reliable_record_count)
    ):
        prefix = f"WORR_NATIVE_RELIABLE_PRINT_{index:02d}_"
        prefix_count = text.count(prefix)
        payload_count = text.count(payload)
        if prefix_count != 1 or payload_count != 1:
            raise RuntimeError(
                "reliable print delivery mismatch: "
                f"{prefix} prefix_count={prefix_count} "
                f"complete_payload_count={payload_count} expected=1"
            )
        delivered.append(prefix)
    return {
        "record_count": len(delivered),
        "record_bytes": RELIABLE_RECORD_BYTES,
        "payload_bytes": reliable_record_count * RELIABLE_RECORD_BYTES,
        "complete_exact_once": True,
        "prefixes": delivered,
    }


def validate_reliable_delivery_evidence(
    evidence: object,
    *,
    reliable_record_count: int,
) -> None:
    expected = {
        "record_count": reliable_record_count,
        "record_bytes": RELIABLE_RECORD_BYTES,
        "payload_bytes": reliable_record_count * RELIABLE_RECORD_BYTES,
        "complete_exact_once": True,
        "prefixes": [
            f"WORR_NATIVE_RELIABLE_PRINT_{index:02d}_"
            for index in range(reliable_record_count)
        ],
    }
    if evidence != expected:
        raise RuntimeError("native-shadow reliable delivery evidence mismatch")


def client_control_payload(
    completion_marker: str,
    *,
    client_release_wait_frames: int = FRAGMENT_CLIENT_RELEASE_WAIT_FRAMES,
) -> str:
    if not completion_marker or any(
        character.isspace() or character in ';"'
        for character in completion_marker
    ):
        raise ValueError("completion marker must be one safe token")
    return (
        f"wait {client_release_wait_frames}; "
        "cl_worr_native_shadow_probe_hold 0; "
        f"wait {CLIENT_REHOLD_WAIT_FRAMES}; "
        "cl_worr_native_shadow_probe_hold 1; "
        f"wait {CLIENT_FINAL_WAIT_FRAMES}; "
        "cl_worr_native_shadow_status; "
        "net_impair_status; "
        f"echo {completion_marker};"
    )


def server_command(
    executable: Path,
    *,
    port: int,
    client_completion_marker: str,
    server_completion_marker: str,
    reliable_record_count: int = FRAGMENT_RELIABLE_RECORD_COUNT,
    client_release_wait_frames: int = FRAGMENT_CLIENT_RELEASE_WAIT_FRAMES,
    rate_bytes_per_second: int = FRAGMENT_RATE_BYTES_PER_SECOND,
) -> list[str]:
    command = [
        str(executable),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "net_enable_ipv6", "0",
        "+set", "net_ip", ADDRESS,
        "+set", "net_port", str(port),
        "+set", "net_maxmsglen", str(NET_MAXMSGLEN),
        # Keep this transport gate outside the multiplayer welcome/menu
        # service, whose large UI stufftext batches are tested separately.
        "+set", "deathmatch", "0",
        "+set", "coop", "0",
        "+set", "maxclients", "1",
        "+set", "g_owner_auto_join", "1",
        "+set", "match_auto_join", "1",
        "+set", "match_force_join", "1",
        "+set", "sv_lan_force_rate", "0",
        "+set", "sv_fps", str(SERVER_FPS),
        "+set", "sv_min_rate", str(rate_bytes_per_second),
        "+set", "sv_max_rate", str(rate_bytes_per_second),
        "+set", "sv_worr_native_shadow", "1",
    ]
    command.extend(_common_impairment_settings(SERVER_IMPAIR_SEED))
    command.extend(("+map", MAP_NAME))
    command.extend(wait_commands(SERVER_QUEUE_WAIT_FRAMES))
    command.extend(
        (
            "+stuffall",
            f'"{client_control_payload(client_completion_marker, client_release_wait_frames=client_release_wait_frames)}"',
        )
    )
    command.extend(wait_commands(SERVER_BURST_DELAY_FRAMES))
    for payload in reliable_payloads(reliable_record_count):
        # SVC_PRINT is reliable but never enters the client's 4 KiB stuffed
        # command buffer.  Literal quotes preserve each payload as one server
        # command when Com_AddLateCommands rebuilds the command stream.
        command.extend(("+printall", f'"{payload}"'))
    command.extend(wait_commands(SERVER_FINAL_WAIT_FRAMES))
    command.extend(
        (
            "+sv_worr_native_shadow_status",
            "+net_impair_status",
            "+echo", server_completion_marker,
            "+wait", "1000",
        )
    )
    _validate_command_line(command, "server")
    return command


def client_command(
    executable: Path,
    *,
    port: int,
    rate_bytes_per_second: int = FRAGMENT_RATE_BYTES_PER_SECOND,
) -> list[str]:
    command = [
        str(executable),
        "+set", "game", "basew",
        "+set", "developer", "1",
        # A client is needed for this real UDP exchange, but automation must
        # remain invisible and must never initialize or capture mouse input.
        "+set", "win_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "name", "native_shadow_probe",
        "+set", "r_renderer", "opengl",
        "+set", "r_fullscreen", "0",
        "+set", "r_geometry", "640x480+0+0",
        "+set", "gl_swapinterval", "0",
        "+set", "cl_async", "0",
        "+set", "cl_maxfps", "62",
        "+set", "r_maxfps", "62",
        "+set", "s_enable", "0",
        "+set", "net_enable_ipv6", "0",
        "+set", "net_clientport", "-1",
        "+set", "net_maxmsglen", str(NET_MAXMSGLEN),
        "+set", "cl_protocol", str(PROTOCOL),
        "+set", "rate", str(rate_bytes_per_second),
        "+set", "cl_worr_native_shadow", "1",
        "+set", "cl_worr_native_shadow_probe_hold", "1",
        "+set", "cl_ignore_stufftext", "0",
        "+set", "allow_download", "0",
    ]
    command.extend(_common_impairment_settings(CLIENT_IMPAIR_SEED))
    command.extend(("+connect", f"{ADDRESS}:{port}"))
    _validate_command_line(command, "client")
    return command


def _validate_command_line(command: Sequence[str], endpoint: str) -> None:
    length = len(subprocess.list2cmdline(list(command)))
    if length > WINDOWS_COMMAND_LINE_LIMIT:
        raise RuntimeError(
            f"{endpoint} command line is {length} characters; "
            f"limit is {WINDOWS_COMMAND_LINE_LIMIT}"
        )


def reserve_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind((ADDRESS, 0))
        port = int(reservation.getsockname()[1])
    if port <= 0 or port > 65535:
        raise RuntimeError(f"operating system returned invalid UDP port {port}")
    return port


def terminate_process(process: subprocess.Popen[object] | None) -> bool:
    return terminate_process_tree(process)


def _read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def _wait_for_server_map(
    process: subprocess.Popen[object],
    stdout_path: Path,
    timeout: float,
) -> None:
    deadline = time.monotonic() + min(timeout, 20.0)
    marker = f"SpawnServer: {MAP_NAME}"
    while time.monotonic() < deadline:
        if marker in _read_text(stdout_path):
            return
        if process.poll() is not None:
            raise RuntimeError(
                "dedicated server exited before spawning the acceptance map "
                f"(returncode={process.returncode})"
            )
        time.sleep(0.1)
    raise RuntimeError("timed out waiting for the dedicated acceptance map")


def run_processes(
    *,
    server_argv: Sequence[str],
    client_argv: Sequence[str],
    working_dir: Path,
    run_root: Path,
    client_marker: str,
    server_marker: str,
    timeout: float,
) -> dict[str, object]:
    paths = {
        "client_stdout": run_root / "client.stdout.log",
        "client_stderr": run_root / "client.stderr.log",
        "server_stdout": run_root / "server.stdout.log",
        "server_stderr": run_root / "server.stderr.log",
    }
    creation_flags = _headless_creation_flags()
    server: subprocess.Popen[object] | None = None
    client: subprocess.Popen[object] | None = None
    server_terminated = False
    client_terminated = False
    started = time.monotonic()
    with ExitStack() as stack:
        files = {
            role: stack.enter_context(path.open("w", encoding="utf-8"))
            for role, path in paths.items()
        }
        try:
            server = start_headless_process(
                list(server_argv),
                cwd=working_dir,
                stdin=subprocess.DEVNULL,
                stdout=files["server_stdout"],
                stderr=files["server_stderr"],
                creationflags=creation_flags,
            )
            _wait_for_server_map(server, paths["server_stdout"], timeout)
            client = start_headless_process(
                list(client_argv),
                cwd=working_dir,
                stdin=subprocess.DEVNULL,
                stdout=files["client_stdout"],
                stderr=files["client_stderr"],
                creationflags=creation_flags,
            )
            deadline = started + timeout
            while time.monotonic() < deadline:
                client_text = _read_text(paths["client_stdout"])
                server_text = _read_text(paths["server_stdout"])
                if client_marker in client_text and server_marker in server_text:
                    # Both commands place status immediately before their
                    # marker.  A short live grace period catches a terminal
                    # failure flushed in the following engine iteration.
                    time.sleep(0.25)
                    if client.poll() is not None or server.poll() is not None:
                        raise RuntimeError(
                            "an endpoint exited after evidence before harness "
                            "termination"
                        )
                    break
                if client.poll() is not None:
                    raise RuntimeError(
                        "client exited before terminal evidence "
                        f"(returncode={client.returncode})"
                    )
                if server.poll() is not None:
                    raise RuntimeError(
                        "server exited before terminal evidence "
                        f"(returncode={server.returncode})"
                    )
                time.sleep(0.1)
            else:
                raise RuntimeError("two-process native-shadow gate timed out")
        finally:
            # Kill the client first and the server immediately after it.  With
            # the configured 25 ms impairment, no harness-induced disconnect
            # packet can become terminal evidence between these operations.
            client_terminated = terminate_process(client)
            server_terminated = terminate_process(server)

    texts = {role: _read_text(path) for role, path in paths.items()}
    if texts["client_stderr"].strip():
        raise RuntimeError("staged client wrote unexpected stderr output")
    if texts["server_stderr"].strip():
        raise RuntimeError("staged dedicated server wrote unexpected stderr output")
    if not client_terminated or not server_terminated:
        raise RuntimeError("both endpoints must remain alive for harness termination")
    return {
        "paths": paths,
        "texts": texts,
        "client_terminated_by_harness": client_terminated,
        "server_terminated_by_harness": server_terminated,
        "elapsed_seconds": round(time.monotonic() - started, 3),
    }


def validate_profile(profile: object) -> None:
    if not isinstance(profile, dict):
        raise RuntimeError("report profile is missing")
    exact = {
        "transport": "udp_ipv4",
        "address": ADDRESS,
        "protocol": PROTOCOL,
        "map": MAP_NAME,
        "net_maxmsglen": NET_MAXMSGLEN,
        "rate_bytes_per_second": {
            spec.name: spec.rate_bytes_per_second
            for spec in TRIAL_SPECS
        },
        "server_fps": SERVER_FPS,
        "client_native_shadow": 1,
        "server_native_shadow": 1,
        "client_probe_hold_initial": 1,
        "client_probe_hold_final": 1,
        "impairment_latency_ms": IMPAIR_LATENCY_MS,
        "client_impairment_seed": CLIENT_IMPAIR_SEED,
        "server_impairment_seed": SERVER_IMPAIR_SEED,
        "trial_order": list(TRIAL_NAMES),
    }
    for name, expected in exact.items():
        if profile.get(name) != expected:
            raise RuntimeError(
                f"report profile mismatch: {name}={profile.get(name)!r} "
                f"expected={expected!r}"
            )
    trials = profile.get("trials")
    if not isinstance(trials, dict) or set(trials) != set(TRIAL_NAMES):
        raise RuntimeError("report profile trial set mismatch")
    ports: list[int] = []
    for spec in TRIAL_SPECS:
        trial = trials.get(spec.name)
        if not isinstance(trial, dict):
            raise RuntimeError(f"report profile has no {spec.name} trial")
        expected = {
            "client_release_wait_frames": spec.client_release_wait_frames,
            "client_rehold_wait_frames": CLIENT_REHOLD_WAIT_FRAMES,
            "server_queue_wait_frames": SERVER_QUEUE_WAIT_FRAMES,
            "server_burst_delay_frames": SERVER_BURST_DELAY_FRAMES,
            "client_final_wait_frames": CLIENT_FINAL_WAIT_FRAMES,
            "server_final_wait_frames": SERVER_FINAL_WAIT_FRAMES,
            "reliable_record_count": spec.reliable_record_count,
            "reliable_record_bytes": RELIABLE_RECORD_BYTES,
            "rate_bytes_per_second": spec.rate_bytes_per_second,
            "reliable_payload_bytes": (
                spec.reliable_record_count * RELIABLE_RECORD_BYTES
            ),
            "required_server_evidence": list(
                spec.required_server_evidence
            ),
        }
        for name, expected_value in expected.items():
            if trial.get(name) != expected_value:
                raise RuntimeError(
                    f"report profile {spec.name} mismatch: "
                    f"{name}={trial.get(name)!r} expected={expected_value!r}"
                )
        port = trial.get("port")
        if not isinstance(port, int) or port <= 0 or port > 65535:
            raise RuntimeError(
                f"report profile {spec.name} has an invalid UDP port"
            )
        ports.append(port)
    if len(set(ports)) != len(ports):
        raise RuntimeError("report profile trials did not use distinct ports")


def validate_trial_report(trial: object, spec: TrialSpec) -> None:
    if not isinstance(trial, dict):
        raise RuntimeError(f"native-shadow report has no {spec.name} trial")
    statuses = trial.get("statuses")
    if not isinstance(statuses, dict):
        raise RuntimeError(f"native-shadow {spec.name} has no statuses")
    client_status = statuses.get("client")
    server_status = statuses.get("server")
    if not isinstance(client_status, dict) or not isinstance(server_status, dict):
        raise RuntimeError(
            f"native-shadow {spec.name} endpoint status is missing"
        )
    validate_status_pair(
        client_status,
        server_status,
        required_server_evidence=spec.required_server_evidence,
    )
    validate_reliable_delivery_evidence(
        trial.get("reliable_delivery"),
        reliable_record_count=spec.reliable_record_count,
    )
    impairment = trial.get("impairment")
    if not isinstance(impairment, dict):
        raise RuntimeError(
            f"native-shadow {spec.name} impairment evidence is missing"
        )
    for endpoint, seed in (
        ("client", CLIENT_IMPAIR_SEED),
        ("server", SERVER_IMPAIR_SEED),
    ):
        endpoint_evidence = impairment.get(endpoint)
        if not isinstance(endpoint_evidence, dict):
            raise RuntimeError(
                f"native-shadow {spec.name} has no {endpoint} impairment"
            )
        config = endpoint_evidence.get("config")
        counters = endpoint_evidence.get("counters")
        if not isinstance(config, dict) or not isinstance(counters, dict):
            raise RuntimeError(
                f"native-shadow {spec.name} {endpoint} impairment malformed"
            )
        validate_impairment_status(config, counters, expected_seed=seed)
    commands = trial.get("commands")
    if not isinstance(commands, dict):
        raise RuntimeError(
            f"native-shadow {spec.name} command hashes are missing"
        )
    for endpoint in ("client", "server"):
        argc = commands.get(f"{endpoint}_argc")
        digest = commands.get(f"{endpoint}_argv_sha256")
        if not isinstance(argc, int) or argc <= 0:
            raise RuntimeError(
                f"native-shadow {spec.name} {endpoint} argc is invalid"
            )
        if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
            raise RuntimeError(
                f"native-shadow {spec.name} {endpoint} command SHA-256 "
                "is invalid"
            )
    processes = trial.get("processes")
    if (
        not isinstance(processes, dict)
        or processes.get("client_terminated_by_harness") is not True
        or processes.get("server_terminated_by_harness") is not True
    ):
        raise RuntimeError(
            f"native-shadow {spec.name} endpoints were not harness-terminated"
        )
    elapsed = processes.get("elapsed_seconds")
    if (
        not isinstance(elapsed, (int, float))
        or isinstance(elapsed, bool)
        or elapsed <= 0
        or elapsed > TRIAL_TIMEOUT_SECONDS + 1.0
    ):
        raise RuntimeError(
            f"native-shadow {spec.name} elapsed time is invalid"
        )


def validate_trial_artifact_binding(
    trial: Mapping[str, object],
    spec: TrialSpec,
    *,
    port: int,
    run_id: str,
    runtime_paths: Mapping[str, Path],
    log_paths: Mapping[str, Path],
) -> None:
    client_marker = (
        f"worr_native_shadow_{spec.name}_client_complete_{run_id}"
    )
    server_marker = (
        f"worr_native_shadow_{spec.name}_server_complete_{run_id}"
    )
    expected_commands = {
        "client": client_command(
            runtime_paths["client_executable"],
            port=port,
            rate_bytes_per_second=spec.rate_bytes_per_second,
        ),
        "server": server_command(
            runtime_paths["dedicated_executable"],
            port=port,
            client_completion_marker=client_marker,
            server_completion_marker=server_marker,
            reliable_record_count=spec.reliable_record_count,
            client_release_wait_frames=spec.client_release_wait_frames,
            rate_bytes_per_second=spec.rate_bytes_per_second,
        ),
    }
    commands = trial["commands"]
    assert isinstance(commands, dict)
    for endpoint, expected_argv in expected_commands.items():
        if (
            commands.get(f"{endpoint}_argc") != len(expected_argv)
            or commands.get(f"{endpoint}_argv_sha256") !=
                argv_sha256(expected_argv)
        ):
            raise RuntimeError(
                f"native-shadow {spec.name} {endpoint} argv binding "
                "mismatch"
            )

    client_stderr = _read_text(
        log_paths[f"{spec.name}_client_stderr"]
    )
    server_stderr = _read_text(
        log_paths[f"{spec.name}_server_stderr"]
    )
    if client_stderr.strip() or server_stderr.strip():
        raise RuntimeError(
            f"native-shadow {spec.name} contains unexpected stderr"
        )
    evidence = validate_runtime_text(
        _read_text(log_paths[f"{spec.name}_client_stdout"]),
        _read_text(log_paths[f"{spec.name}_server_stdout"]),
        client_marker=client_marker,
        server_marker=server_marker,
        port=port,
        reliable_record_count=spec.reliable_record_count,
        required_server_evidence=spec.required_server_evidence,
    )
    statuses = trial["statuses"]
    impairment = trial["impairment"]
    assert isinstance(statuses, dict) and isinstance(impairment, dict)
    if (
        evidence["client_status"] != statuses.get("client")
        or evidence["server_status"] != statuses.get("server")
        or evidence["reliable_delivery"] !=
            trial.get("reliable_delivery")
        or evidence["client_impairment"] != impairment.get("client")
        or evidence["server_impairment"] != impairment.get("server")
    ):
        raise RuntimeError(
            f"native-shadow {spec.name} report evidence disagrees with "
            "its hashed logs"
        )


def validate_report(report: object) -> None:
    if not isinstance(report, dict):
        raise RuntimeError("native-shadow report must be an object")
    if report.get("schema") != SCHEMA:
        raise RuntimeError("native-shadow report schema mismatch")
    if report.get("passed") is not True:
        raise RuntimeError("native-shadow report is not a passing report")
    run_id = report.get("run_id")
    if (
        not isinstance(run_id, str)
        or not run_id
        or any(character.isspace() or character in ';"'
               for character in run_id)
    ):
        raise RuntimeError("native-shadow report run ID is invalid")
    validate_profile(report.get("profile"))
    contract = report.get("status_contract")
    expected_contract = {
        "client_prefix": CLIENT_STATUS_PREFIX,
        "client_required_fields": list(CLIENT_STATUS_FIELDS),
        "server_prefix": SERVER_STATUS_PREFIX,
        "server_required_fields": list(SERVER_STATUS_FIELDS),
    }
    if contract != expected_contract:
        raise RuntimeError("native-shadow report status contract mismatch")
    trials = report.get("trials")
    if not isinstance(trials, dict) or set(trials) != set(TRIAL_NAMES):
        raise RuntimeError("native-shadow report trial set mismatch")
    for spec in TRIAL_SPECS:
        validate_trial_report(trials.get(spec.name), spec)
    overall_elapsed = report.get("overall_elapsed_seconds")
    if (
        not isinstance(overall_elapsed, (int, float))
        or isinstance(overall_elapsed, bool)
        or overall_elapsed <= 0
        or overall_elapsed > 120.0
    ):
        raise RuntimeError("native-shadow overall elapsed time is invalid")
    runtime_records = report.get("runtime_components")
    log_records = report.get("logs")
    validate_file_records(runtime_records, RUNTIME_ROLES)
    validate_file_records(log_records, LOG_ROLES)
    assert isinstance(runtime_records, list) and isinstance(log_records, list)
    runtime_paths = {
        record["role"]: Path(record["path"])
        for record in runtime_records
        if isinstance(record, dict)
    }
    log_paths = {
        record["role"]: Path(record["path"])
        for record in log_records
        if isinstance(record, dict)
    }
    profile = report["profile"]
    assert isinstance(profile, dict)
    profile_trials = profile["trials"]
    assert isinstance(profile_trials, dict)
    for spec in TRIAL_SPECS:
        trial = trials[spec.name]
        trial_profile = profile_trials[spec.name]
        assert isinstance(trial, dict) and isinstance(trial_profile, dict)
        validate_trial_artifact_binding(
            trial,
            spec,
            port=int(trial_profile["port"]),
            run_id=run_id,
            runtime_paths=runtime_paths,
            log_paths=log_paths,
        )
    limitations = report.get("limitations")
    if (
        not isinstance(limitations, list)
        or len(limitations) < 4
        or any(not isinstance(item, str) or not item for item in limitations)
    ):
        raise RuntimeError("native-shadow report limitations are incomplete")


def write_json_atomic(path: Path, payload: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def invalidate_previous_outputs(output: Path) -> Path:
    failure = output.with_suffix(".failure.json")
    output.unlink(missing_ok=True)
    failure.unlink(missing_ok=True)
    return failure


def validate_staged_paths(
    client_exe: Path,
    dedicated_exe: Path,
    working_dir: Path,
) -> None:
    working = working_dir.resolve()
    if working.name != ".install":
        raise RuntimeError("native-shadow runtime gate requires the .install root")
    expected = {
        client_exe.resolve(): working / "worr_x86_64.exe",
        dedicated_exe.resolve(): working / "worr_ded_x86_64.exe",
    }
    for actual, required in expected.items():
        if actual != required.resolve():
            raise RuntimeError(
                f"runtime executable is not the staged artifact: {actual}"
            )


def default_paths() -> tuple[Path, Path, Path, Path]:
    repository = Path(__file__).resolve().parents[2]
    working = repository / ".install"
    return (
        working / "worr_x86_64.exe",
        working / "worr_ded_x86_64.exe",
        working,
        repository / ".tmp/networking/native-shadow-runtime.json",
    )


def build_profile(trial_ports: Mapping[str, int]) -> dict[str, object]:
    if set(trial_ports) != set(TRIAL_NAMES):
        raise ValueError("every native-shadow trial requires one UDP port")
    return {
        "transport": "udp_ipv4",
        "address": ADDRESS,
        "protocol": PROTOCOL,
        "map": MAP_NAME,
        "net_maxmsglen": NET_MAXMSGLEN,
        "rate_bytes_per_second": {
            spec.name: spec.rate_bytes_per_second
            for spec in TRIAL_SPECS
        },
        "server_fps": SERVER_FPS,
        "client_native_shadow": 1,
        "server_native_shadow": 1,
        "client_probe_hold_initial": 1,
        "client_probe_hold_final": 1,
        "impairment_latency_ms": IMPAIR_LATENCY_MS,
        "client_impairment_seed": CLIENT_IMPAIR_SEED,
        "server_impairment_seed": SERVER_IMPAIR_SEED,
        "trial_order": list(TRIAL_NAMES),
        "trials": {
            spec.name: {
                "port": trial_ports[spec.name],
                "client_release_wait_frames": (
                    spec.client_release_wait_frames
                ),
                "client_rehold_wait_frames": CLIENT_REHOLD_WAIT_FRAMES,
                "server_queue_wait_frames": SERVER_QUEUE_WAIT_FRAMES,
                "server_burst_delay_frames": SERVER_BURST_DELAY_FRAMES,
                "client_final_wait_frames": CLIENT_FINAL_WAIT_FRAMES,
                "server_final_wait_frames": SERVER_FINAL_WAIT_FRAMES,
                "reliable_record_count": spec.reliable_record_count,
                "reliable_record_bytes": RELIABLE_RECORD_BYTES,
                "rate_bytes_per_second": spec.rate_bytes_per_second,
                "reliable_payload_bytes": (
                    spec.reliable_record_count * RELIABLE_RECORD_BYTES
                ),
                "required_server_evidence": list(
                    spec.required_server_evidence
                ),
            }
            for spec in TRIAL_SPECS
        },
    }


def main() -> int:
    default_client, default_dedicated, default_working, default_output = (
        default_paths()
    )
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-exe", type=Path, default=default_client)
    parser.add_argument("--dedicated-exe", type=Path, default=default_dedicated)
    parser.add_argument("--working-dir", type=Path, default=default_working)
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()
    if args.timeout < 75.0:
        parser.error("--timeout must be at least 75 seconds")
    if args.timeout > 120.0:
        parser.error("--timeout must not exceed 120 seconds")

    args.client_exe = args.client_exe.resolve()
    args.dedicated_exe = args.dedicated_exe.resolve()
    args.working_dir = args.working_dir.resolve()
    args.output = args.output.resolve()
    validate_staged_paths(
        args.client_exe, args.dedicated_exe, args.working_dir
    )
    failure_output = invalidate_previous_outputs(args.output)
    started_at = datetime.now(timezone.utc)
    run_id = started_at.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_root = (
        args.output.parent / f"{args.output.stem}.runs" / run_id
    )
    run_root.mkdir(parents=True, exist_ok=False)
    before_manifest: list[dict[str, object]] = []
    trial_ports: dict[str, int] = {}
    trial_commands: dict[str, dict[str, object]] = {}
    trial_runs: dict[str, dict[str, object]] = {}
    current_trial: str | None = None
    overall_started = time.monotonic()
    try:
        before_manifest = runtime_manifest(args.working_dir)
        for spec in TRIAL_SPECS:
            current_trial = spec.name
            remaining = args.timeout - (time.monotonic() - overall_started)
            if remaining < 30.0:
                raise RuntimeError(
                    f"insufficient overall timeout for {spec.name} trial"
                )
            port = reserve_udp_port()
            while port in trial_ports.values():
                port = reserve_udp_port()
            trial_ports[spec.name] = port
            trial_root = run_root / spec.name
            trial_root.mkdir(parents=True, exist_ok=False)
            client_marker = (
                f"worr_native_shadow_{spec.name}_client_complete_{run_id}"
            )
            server_marker = (
                f"worr_native_shadow_{spec.name}_server_complete_{run_id}"
            )
            client_argv = client_command(
                args.client_exe,
                port=port,
                rate_bytes_per_second=spec.rate_bytes_per_second,
            )
            server_argv = server_command(
                args.dedicated_exe,
                port=port,
                client_completion_marker=client_marker,
                server_completion_marker=server_marker,
                reliable_record_count=spec.reliable_record_count,
                client_release_wait_frames=(
                    spec.client_release_wait_frames
                ),
                rate_bytes_per_second=spec.rate_bytes_per_second,
            )
            trial_commands[spec.name] = {
                "client_argc": len(client_argv),
                "client_argv_sha256": argv_sha256(client_argv),
                "server_argc": len(server_argv),
                "server_argv_sha256": argv_sha256(server_argv),
            }
            process_result = run_processes(
                server_argv=server_argv,
                client_argv=client_argv,
                working_dir=args.working_dir,
                run_root=trial_root,
                client_marker=client_marker,
                server_marker=server_marker,
                timeout=min(TRIAL_TIMEOUT_SECONDS, remaining),
            )
            texts = process_result["texts"]
            paths = process_result["paths"]
            assert isinstance(texts, dict) and isinstance(paths, dict)
            evidence = validate_runtime_text(
                str(texts["client_stdout"]),
                str(texts["server_stdout"]),
                client_marker=client_marker,
                server_marker=server_marker,
                port=port,
                reliable_record_count=spec.reliable_record_count,
                required_server_evidence=(
                    spec.required_server_evidence
                ),
            )
            trial_runs[spec.name] = {
                "process": process_result,
                "evidence": evidence,
            }
        after_manifest = runtime_manifest(args.working_dir)
        if after_manifest != before_manifest:
            raise RuntimeError("staged runtime components changed during the gate")
        logs: list[dict[str, object]] = []
        for spec in TRIAL_SPECS:
            process = trial_runs[spec.name]["process"]
            assert isinstance(process, dict)
            paths = process["paths"]
            assert isinstance(paths, dict)
            for role in ENDPOINT_LOG_ROLES:
                logs.append(
                    make_file_record(
                        f"{spec.name}_{role}", paths[role]
                    )
                )
        trials: dict[str, object] = {}
        for spec in TRIAL_SPECS:
            run = trial_runs[spec.name]
            process = run["process"]
            evidence = run["evidence"]
            assert isinstance(process, dict) and isinstance(evidence, dict)
            trials[spec.name] = {
                "statuses": {
                    "client": evidence["client_status"],
                    "server": evidence["server_status"],
                },
                "reliable_delivery": evidence["reliable_delivery"],
                "impairment": {
                    "client": evidence["client_impairment"],
                    "server": evidence["server_impairment"],
                },
                "commands": trial_commands[spec.name],
                "processes": {
                    "client_terminated_by_harness": process[
                        "client_terminated_by_harness"
                    ],
                    "server_terminated_by_harness": process[
                        "server_terminated_by_harness"
                    ],
                    "elapsed_seconds": process["elapsed_seconds"],
                },
            }
        report: dict[str, object] = {
            "schema": SCHEMA,
            "passed": True,
            "run_id": run_id,
            "started_at_utc": started_at.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "profile": build_profile(trial_ports),
            "status_contract": {
                "client_prefix": CLIENT_STATUS_PREFIX,
                "client_required_fields": list(CLIENT_STATUS_FIELDS),
                "server_prefix": SERVER_STATUS_PREFIX,
                "server_required_fields": list(SERVER_STATUS_FIELDS),
            },
            "trials": trials,
            "overall_elapsed_seconds": round(
                time.monotonic() - overall_started, 3
            ),
            "runtime_components": before_manifest,
            "logs": logs,
            "limitations": [
                "Each trial observes exactly one native command while legacy MOVE/BATCH_MOVE remains authoritative.",
                "The gate does not prove mixed DATA+ACK packets or repeated native command streams.",
                "Fragment-owner evidence uses 1,500 B/s while the async-wake trial uses a distinct 1,000,000 B/s zero-delay admission profile; the mutually exclusive scheduler properties use separate fresh connections.",
                "Latency is deterministic, but this profile does not target directional readiness-packet loss, reordering, or duplication.",
                "UDP localhost does not model WAN congestion, NAT traversal, multi-client fairness, or promotion to native authority.",
            ],
        }
        validate_report(report)
        write_json_atomic(run_root / "report.json", report)
        write_json_atomic(args.output, report)
    except Exception as error:
        artifacts: list[dict[str, object]] = []
        for path in sorted(run_root.rglob("*.log")):
            if not path.is_file():
                continue
            relative = path.relative_to(run_root)
            role = "_".join(relative.parts).replace(".", "_")
            artifacts.append(make_file_record(role, path))
        trial_state: dict[str, object] = {}
        for name in TRIAL_NAMES:
            if name not in trial_ports and name not in trial_commands:
                continue
            state: dict[str, object] = {
                "port": trial_ports.get(name),
                "commands": trial_commands.get(name),
                "completed": name in trial_runs,
            }
            if name in trial_runs:
                completed_run = trial_runs[name]
                completed_evidence = completed_run["evidence"]
                completed_process = completed_run["process"]
                assert isinstance(completed_evidence, dict)
                assert isinstance(completed_process, dict)
                state["evidence"] = completed_evidence
                state["processes"] = {
                    "client_terminated_by_harness": completed_process[
                        "client_terminated_by_harness"
                    ],
                    "server_terminated_by_harness": completed_process[
                        "server_terminated_by_harness"
                    ],
                    "elapsed_seconds": completed_process[
                        "elapsed_seconds"
                    ],
                }
            trial_state[name] = state
        failure: dict[str, object] = {
            "schema": FAILURE_SCHEMA,
            "run_id": run_id,
            "started_at_utc": started_at.isoformat(),
            "failed_at_utc": datetime.now(timezone.utc).isoformat(),
            "requested_output": str(args.output),
            "requested_output_invalidated": not args.output.exists(),
            "profile": {
                "address": ADDRESS,
                "protocol": PROTOCOL,
                "net_maxmsglen": NET_MAXMSGLEN,
                "rate_bytes_per_second": {
                    spec.name: spec.rate_bytes_per_second
                    for spec in TRIAL_SPECS
                },
                "server_fps": SERVER_FPS,
                "trial_order": list(TRIAL_NAMES),
            },
            "current_trial": current_trial,
            "trials": trial_state,
            "runtime_components": before_manifest,
            "artifacts": artifacts,
            "error_type": type(error).__name__,
            "error": str(error),
            "argv": sys.argv,
        }
        write_json_atomic(run_root / "failure.json", failure)
        write_json_atomic(failure_output, failure)
        raise

    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
