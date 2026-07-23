#!/usr/bin/env python3
"""Run the bounded FR-10-T04 exact-bundle acceptance parent.

The parent intentionally reports ``partial`` even when every child passes.
It proves the currently implemented exact capability bundles and production
shadow adapters without treating that bounded evidence as full T04 closure.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from collections.abc import Mapping, Sequence
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


MANIFEST_SCHEMA = "worr.networking.fr10-t04-acceptance-manifest.v1"
EVIDENCE_SCHEMA = "worr.networking.fr10-t04-partial-acceptance-evidence.v1"
FAILURE_SCHEMA = EVIDENCE_SCHEMA + ".failure"
TASK = "FR-10-T04"
STATUS = "partial"
PROTOCOL = 1038
RUNTIME_TRANSIENT_PATTERNS = (".install", ".tmp", "crashdump_*.dmp")
CANONICAL_CHILD_SCHEMA = (
    "worr.networking.canonical-weapon-damage-runtime.v42"
)

EXACT_MASKS: tuple[dict[str, Any], ...] = (
    {"lane": "legacy", "native": False, "event": False,
     "snapshot": False, "mask": 0x03},
    {"lane": "command", "native": True, "event": False,
     "snapshot": False, "mask": 0x53},
    {"lane": "event", "native": True, "event": True,
     "snapshot": False, "mask": 0x73},
    {"lane": "snapshot", "native": True, "event": False,
     "snapshot": True, "mask": 0x57},
    {"lane": "combined", "native": True, "event": True,
     "snapshot": True, "mask": 0x77},
)

# Literal and reviewable: newly added tests cannot silently widen acceptance.
FOCUSED_MANIFEST: tuple[dict[str, Any], ...] = (
    {
        "name": "capability-policy",
        "executable": "net_capability_test",
        "stdout_marker": "capability_test: ok",
        "stderr_marker": None,
        "coverage": (
            "exact-public-bundles", "downgrade", "unknown-version",
            "malformed-capability-text", "connection-epoch-fail-closed",
        ),
    },
    {
        "name": "envelope-mtu-validation",
        "executable": "native_envelope_test",
        "stdout_marker": "native envelope tests passed",
        "stderr_marker": None,
        "coverage": (
            "fragmentation", "mtu-boundary", "malformed-envelope",
            "unknown-envelope-version", "priority",
        ),
    },
    {
        "name": "carrier-admission",
        "executable": "native_carrier_test",
        "stdout_marker": "native carrier tests passed",
        "stderr_marker": None,
        "coverage": (
            "mixed-legacy-native-carrier", "capacity-boundary",
            "malformed-carrier", "wrong-direction",
        ),
    },
    {
        "name": "session-retention",
        "executable": "native_session_test",
        "stdout_marker": "native transport session tests passed",
        "stderr_marker": None,
        "coverage": (
            "session-epoch", "retention", "acknowledgement",
            "sequence-exhaustion", "transactionality",
        ),
    },
    {
        "name": "readiness-reconnect",
        "executable": "native_readiness_test",
        "stdout_marker": "native_readiness_test: ok",
        "stderr_marker": None,
        "coverage": (
            "readiness-order", "fresh-reconnect", "captured-old-challenge",
            "deadline", "sticky-failure", "snapshot-epoch-binding",
        ),
    },
    {
        "name": "server-production-pilot",
        "executable": "native_server_shadow_pilot_test",
        "stdout_marker": "native_server_shadow_pilot_test: ok",
        "stderr_marker": None,
        "coverage": (
            "server-netchan-hooks", "malformed-admission",
            "epoch-cancellation", "combined-fairness", "mtu-append-capacity",
        ),
    },
    {
        "name": "client-production-pilot",
        "executable": "native_client_readiness_pilot_test",
        "stdout_marker": "native_client_readiness_pilot_test: ok",
        "stderr_marker": None,
        "coverage": (
            "client-netchan-hooks", "reconnect-cancellation",
            "malformed-wrong-epoch", "combined-disjoint-sequences",
            "snapshot-admission",
        ),
    },
    {
        "name": "command-adapter",
        "executable": "native_command_shadow_test",
        "stdout_marker": "native command shadow tests passed",
        "stderr_marker": None,
        "coverage": (
            "canonical-command-codec", "retention", "legacy-command-join",
            "bounded-replay",
        ),
    },
    {
        "name": "event-production-virtual-link",
        "executable": "native_event_virtual_link_test",
        "stdout_marker": "native_event_virtual_link_test: ok",
        "stderr_marker": None,
        "coverage": (
            "exact-event-bundle", "canonical-event-admission", "loss",
            "reorder", "duplicate", "corruption", "epoch-cancellation",
        ),
    },
    {
        "name": "cgame-local-action-reconciliation",
        "executable": "cgame_local_interaction_test",
        "stdout_marker": "cgame_local_interaction_test: ok",
        "stderr_marker": None,
        "coverage": (
            "ordered-private-reconciliation", "delayed-receipt-bound",
            "terminal-retirement", "no-receipt-rolling",
            "lost-coverage-fail-closed",
        ),
    },
    {
        "name": "snapshot-production-virtual-link",
        "executable": "native_snapshot_production_virtual_link_test",
        "stdout_marker": "native_snapshot_production_virtual_link_test: ok",
        "stderr_marker": "cg_prediction_snapshot_authority:",
        "coverage": (
            "exact-snapshot-bundle", "canonical-snapshot-admission",
            "fragment-loss-reorder", "semantic-ack", "hash-quarantine",
            "cgame-publication",
        ),
    },
)

LIVE_MANIFEST: tuple[dict[str, Any], ...] = (
    {
        "lane": "legacy", "mask": 0x03,
        "runner": "tools/networking/run_canonical_rail_damage_runtime_gate.py",
        "runner_kind": "canonical-weapon",
        "weapon": "blaster-legacy-capability-status",
        "port_offset": 3,
        "coverage": (
            "real-udp", "exact-mask-status", "no-native-endpoint",
            "legacy-command-sideband", "consumed-command-cursor",
        ),
    },
    {
        "lane": "command", "mask": 0x53,
        "runner": "tools/networking/run_native_shadow_runtime_smoke.py",
        "runner_kind": "native-command", "weapon": None,
        "port_offset": None,
        "coverage": (
            "real-udp", "exact-mask-status", "fragment-pressure",
            "async-ack", "fresh-connections",
        ),
    },
    {
        "lane": "event", "mask": 0x73,
        "runner": "tools/networking/run_canonical_rail_damage_runtime_gate.py",
        "runner_kind": "canonical-weapon", "weapon": "blaster-local-action-lease",
        "port_offset": 0,
        "coverage": (
            "real-udp", "exact-mask-status", "in-session-reconnect",
            "native-event-receipt", "canonical-command-correlation",
        ),
    },
    {
        "lane": "snapshot", "mask": 0x57,
        "runner": "tools/networking/run_canonical_rail_damage_runtime_gate.py",
        "runner_kind": "canonical-weapon",
        "weapon": "blaster-native-snapshot-presentation",
        "port_offset": 1,
        "coverage": (
            "real-udp", "exact-mask-status", "semantic-snapshot-ack",
            "native-cgame-presentation",
        ),
    },
    {
        "lane": "combined", "mask": 0x77,
        "runner": "tools/networking/run_canonical_rail_damage_runtime_gate.py",
        "runner_kind": "canonical-weapon",
        "weapon": "blaster-local-action-lease-combined",
        "port_offset": 2,
        "coverage": (
            "real-udp", "exact-mask-status", "in-session-reconnect",
            "native-event-receipt", "semantic-snapshot-ack",
        ),
    },
)

SOURCE_INPUTS: tuple[str, ...] = (
    "meson.build",
    "inc/common/net/capability.h",
    "inc/common/net/native_codec.h",
    "inc/common/net/native_demo.h",
    "inc/common/net/native_demo_recorder.h",
    "inc/common/net/native_input_batch.h",
    "inc/common/net/native_input_batch_sideband.h",
    "inc/common/net/native_input_delivery.h",
    "inc/common/net/native_event_sender.h",
    "inc/common/net/predicted_presentation.h",
    "inc/common/net/snapshot_q2proto.h",
    "inc/common/net/event_journal.h",
    "inc/common/net/legacy_poi_event_candidate.h",
    "inc/common/protocol.h",
    "inc/client/cgame_event_shadow_runtime.h",
    "inc/client/native_demo_recorder.h",
    "inc/client/native_readiness_pilot.h",
    "inc/server/local_action_shadow_authority.h",
    "inc/server/native_shadow.h",
    "inc/server/snapshot_event_candidates.h",
    "inc/shared/cgame_event_shadow.h",
    "inc/shared/cgame_native_event_probe.h",
    "inc/shared/cgame_prediction.h",
    "inc/shared/cgame_event_runtime.h",
    "inc/shared/event_abi.h",
    "inc/shared/game.h",
    "inc/shared/local_action_shadow.h",
    "src/common/net/capability.c",
    "src/common/net/cgame_event_shadow.c",
    "src/common/net/native_codec.c",
    "src/common/net/native_demo.c",
    "src/common/net/native_demo_recorder.c",
    "src/common/net/native_input_batch.c",
    "src/common/net/native_input_batch_sideband.c",
    "src/common/net/native_input_delivery.c",
    "src/common/net/native_event_sender.c",
    "src/common/net/event_abi.c",
    "src/common/net/legacy_poi_event_candidate.c",
    "src/common/net/predicted_presentation.c",
    "src/common/net/snapshot_q2proto.cpp",
    "src/common/net/event_journal.c",
    "src/common/net/local_action_shadow.c",
    "src/client/client.h",
    "src/client/cgame.cpp",
    "src/client/demo.cpp",
    "src/client/event_shadow.cpp",
    "src/client/cgame_prediction_input.cpp",
    "src/client/input.cpp",
    "src/client/native_demo_recorder.cpp",
    "src/client/net_capability.cpp",
    "src/client/native_readiness_pilot.cpp",
    "src/client/parse.cpp",
    "src/client/screen.cpp",
    "src/game/bgame/game.hpp",
    "src/game/cgame/cg_draw.cpp",
    "src/game/cgame/cg_entity_api.cpp",
    "src/game/cgame/cg_main.cpp",
    "src/game/cgame/cg_entities.cpp",
    "src/game/cgame/cg_event_shadow.hpp",
    "src/game/cgame/cg_event_shadow.cpp",
    "src/game/cgame/cg_event_runtime.hpp",
    "src/game/cgame/cg_event_runtime.cpp",
    "src/game/cgame/cg_local_interaction.hpp",
    "src/game/cgame/cg_local_interaction.cpp",
    "src/game/cgame/cg_canonical_render_entities.hpp",
    "src/game/cgame/cg_canonical_render_entities.cpp",
    "src/game/cgame/cg_native_event_presenter.hpp",
    "src/game/cgame/cg_native_event_presenter.cpp",
    "src/game/sgame/commands/command_client.cpp",
    "src/game/sgame/gameplay/g_items.cpp",
    "src/game/sgame/network/local_action_observation.cpp",
    "src/game/sgame/player/p_client.cpp",
    "src/server/commands.c",
    "src/server/entities.c",
    "src/server/local_action_shadow_authority.c",
    "src/server/main.c",
    "src/server/native_shadow.c",
    "src/server/send.c",
    "src/server/snapshot_event_candidates.c",
    "src/server/user.c",
    "tools/networking/capability_test.c",
    "tools/networking/native_envelope_test.c",
    "tools/networking/native_carrier_test.c",
    "tools/networking/native_session_test.c",
    "tools/networking/native_readiness_test.c",
    "tools/networking/native_server_shadow_pilot_test.c",
    "tools/networking/native_client_readiness_pilot_test.cpp",
    "tools/networking/native_command_shadow_test.c",
    "tools/networking/native_demo_recorder_test.c",
    "tools/networking/native_input_batch_test.c",
    "tools/networking/native_input_delivery_test.c",
    "tools/networking/native_event_sender_test.c",
    "tools/networking/native_event_virtual_link_test.cpp",
    "tools/networking/cgame_event_presentation_test.cpp",
    "tools/networking/cgame_event_shadow_test.c",
    "tools/networking/cgame_event_runtime_test.cpp",
    "tools/networking/cgame_local_interaction_test.cpp",
    "tools/networking/cgame_native_event_probe_layout_c.c",
    "tools/networking/cgame_native_event_probe_layout_cpp.cpp",
    "tools/networking/local_action_shadow_authority_test.c",
    "tools/networking/event_journal_test.c",
    "tools/networking/event_schema_layout_c.c",
    "tools/networking/event_schema_layout_cpp.cpp",
    "tools/networking/legacy_poi_event_candidate_test.c",
    "tools/networking/native_codec_test.c",
    "tools/networking/native_codec_layout_c.c",
    "tools/networking/native_codec_layout_cpp.cpp",
    "tools/networking/native_event_presenter_ownership_inventory.json",
    "tools/networking/native_snapshot_production_virtual_link_test.cpp",
    "tools/networking/cgame_canonical_render_entities_test.cpp",
    "tools/networking/cgame_native_event_presenter_test.cpp",
    "tools/networking/predicted_presentation_test.c",
    "tools/networking/server_snapshot_event_candidates_test.cpp",
    "tools/networking/snapshot_q2proto_test.cpp",
    "tools/networking/test_canonical_snapshot_render_policy_contract.py",
    "tools/networking/test_native_event_presenter_source_contract.py",
    "tools/networking/test_poi_source_contract.py",
    "tools/networking/run_native_shadow_runtime_smoke.py",
    "tools/networking/test_run_native_shadow_runtime_smoke.py",
    "tools/networking/run_canonical_rail_damage_runtime_gate.py",
    "tools/networking/test_run_canonical_rail_damage_runtime_gate.py",
    "tools/networking/run_fr10_t04_acceptance.py",
    "tools/networking/test_run_fr10_t04_acceptance.py",
)

OPEN_COVERAGE: tuple[str, ...] = (
    "supported legacy demo, MVD, and relay compatibility matrix",
    "complete game-service event-family adapter coverage",
    "native authority promotion beyond default-off shadow adapters",
    "multi-client fairness and full deterministic impairment-matrix breadth",
    "ACK-exhaustion and map-rotation breadth across every live bundle",
    "sustained load, soak, cross-platform, rollout, and release evidence",
)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=root)
    parser.add_argument("--build-dir", type=Path, default=Path("builddir-win"))
    parser.add_argument("--working-dir", type=Path, default=Path(".install"))
    parser.add_argument(
        "--client-exe", type=Path,
        default=Path(".install/worr_x86_64.exe"),
    )
    parser.add_argument(
        "--dedicated-exe", type=Path,
        default=Path(".install/worr_ded_x86_64.exe"),
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path(".tmp/networking/fr10_t04_partial_acceptance.json"),
    )
    parser.add_argument("--scope", choices=("focused", "full"), default="full")
    parser.add_argument("--base-port", type=int, default=29440)
    parser.add_argument("--focused-timeout", type=float, default=30.0)
    parser.add_argument("--native-command-timeout", type=float, default=120.0)
    parser.add_argument("--canonical-timeout", type=float, default=45.0)
    parser.add_argument("--live-child-timeout", type=float, default=180.0)
    return parser.parse_args(argv)


def _object_no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def strict_json(data: bytes, label: str) -> dict[str, Any]:
    def reject_constant(value: str) -> None:
        raise ValueError(f"{label}: non-finite JSON constant {value}")

    value = json.loads(
        data.decode("utf-8"), object_pairs_hook=_object_no_duplicates,
        parse_constant=reject_constant,
    )
    if not isinstance(value, dict):
        raise ValueError(f"{label}: top-level JSON value must be an object")
    return value


def load_json(path: Path, label: str) -> dict[str, Any]:
    return strict_json(path.read_bytes(), label)


def require_dict(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def require_list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def require_int(value: Any, label: str, *, minimum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{label} must be an integer")
    if minimum is not None and value < minimum:
        raise ValueError(f"{label} must be at least {minimum}")
    return value


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def semantic_sha256(value: Any) -> str:
    return sha256_bytes(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    )


def argv_sha256(command: Sequence[str]) -> str:
    return semantic_sha256(list(command))


def write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def resolve_repo_path(root: Path, path: Path, label: str) -> Path:
    candidate = path if path.is_absolute() else root / path
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValueError(f"{label} must remain inside the repository") from error
    return resolved


def require_networking_output(root: Path, path: Path) -> Path:
    output = resolve_repo_path(root, path, "output")
    expected_root = (root / ".tmp" / "networking").resolve()
    try:
        output.relative_to(expected_root)
    except ValueError as error:
        raise ValueError("output must be under .tmp/networking") from error
    if output.suffix.lower() != ".json":
        raise ValueError("output must be a JSON file")
    return output


def relative_name(root: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return str(path.resolve())


def file_record(root: Path, path: Path) -> dict[str, Any]:
    stat = path.stat()
    return {
        "path": relative_name(root, path),
        "bytes": stat.st_size,
        "sha256": file_sha256(path),
    }


def directory_digest(path: Path) -> dict[str, Any]:
    files = sorted(
        (entry for entry in path.rglob("*") if entry.is_file()),
        key=lambda entry: entry.relative_to(path).as_posix(),
    )
    digest = hashlib.sha256()
    total = 0
    for entry in files:
        relative = entry.relative_to(path).as_posix().encode("utf-8")
        data_hash = file_sha256(entry).encode("ascii")
        size = entry.stat().st_size
        total += size
        digest.update(relative + b"\0" + str(size).encode() + b"\0" + data_hash)
    return {"file_count": len(files), "bytes": total, "sha256": digest.hexdigest()}


def validate_exact_masks(rows: Sequence[Mapping[str, Any]]) -> tuple[dict[str, Any], ...]:
    expected = (
        ("legacy", False, False, False, 0x03),
        ("command", True, False, False, 0x53),
        ("event", True, True, False, 0x73),
        ("snapshot", True, False, True, 0x57),
        ("combined", True, True, True, 0x77),
    )
    actual = tuple(
        (row.get("lane"), row.get("native"), row.get("event"),
         row.get("snapshot"), row.get("mask"))
        for row in rows
    )
    if actual != expected:
        raise ValueError("exact capability-bundle manifest drifted")
    return tuple(dict(row) for row in rows)


def validate_manifests() -> None:
    validate_exact_masks(EXACT_MASKS)
    focused_names = [row["name"] for row in FOCUSED_MANIFEST]
    focused_exes = [row["executable"] for row in FOCUSED_MANIFEST]
    if len(set(focused_names)) != len(focused_names):
        raise ValueError("focused manifest contains duplicate names")
    if len(set(focused_exes)) != len(focused_exes):
        raise ValueError("focused manifest contains duplicate executables")
    required_risks = {
        "downgrade", "unknown-version", "malformed-envelope",
        "mtu-boundary", "fresh-reconnect", "malformed-admission",
        "exact-event-bundle", "exact-snapshot-bundle",
        "delayed-receipt-bound",
    }
    covered = {
        item for row in FOCUSED_MANIFEST for item in row["coverage"]
    }
    if not required_risks <= covered:
        raise ValueError("focused manifest lost a required T04 risk")
    live_lanes = [(row["lane"], row["mask"]) for row in LIVE_MANIFEST]
    if live_lanes != [
        ("legacy", 0x03), ("command", 0x53), ("event", 0x73),
        ("snapshot", 0x57), ("combined", 0x77),
    ]:
        raise ValueError("live exact-bundle manifest drifted")


def manifest_document() -> dict[str, Any]:
    validate_manifests()
    core = {
        "schema": MANIFEST_SCHEMA,
        "task": TASK,
        "status": STATUS,
        "exact_masks": [dict(row) for row in EXACT_MASKS],
        "focused": [
            {
                **{key: value for key, value in row.items() if key != "coverage"},
                "coverage": list(row["coverage"]),
            }
            for row in FOCUSED_MANIFEST
        ],
        "live": [
            {
                **{key: value for key, value in row.items() if key != "coverage"},
                "coverage": list(row["coverage"]),
            }
            for row in LIVE_MANIFEST
        ],
    }
    return core | {"sha256": semantic_sha256(core)}


def validate_bounds(args: argparse.Namespace) -> None:
    if not 0 < args.focused_timeout <= 60:
        raise ValueError("focused timeout must be in (0, 60]")
    if not 75 <= args.native_command_timeout <= 120:
        raise ValueError("native-command timeout must be in [75, 120]")
    if not 0 < args.canonical_timeout <= 60:
        raise ValueError("canonical timeout must be in (0, 60]")
    if not 0 < args.live_child_timeout <= 240:
        raise ValueError("live child timeout must be in (0, 240]")
    if not 1024 <= args.base_port <= 65535 - 2:
        raise ValueError("base port cannot provide the three canonical ports")


def platform_executable(build_dir: Path, stem: str) -> Path:
    candidates = (build_dir / f"{stem}.exe", build_dir / stem)
    matches = [path.resolve() for path in candidates if path.is_file()]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one focused executable {stem}: {matches}")
    return matches[0]


def _kill_process_tree(process: subprocess.Popen[Any]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        try:
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, timeout=10, check=False,
                creationflags=creationflags,
            )
        except subprocess.TimeoutExpired:
            process.kill()
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def run_process_checked(
    command: Sequence[str], cwd: Path, timeout: float,
    stdout_path: Path, stderr_path: Path,
) -> dict[str, Any]:
    if not command:
        raise ValueError("child command is empty")
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        process = subprocess.Popen(
            list(command), cwd=cwd, stdin=subprocess.DEVNULL,
            stdout=stdout, stderr=stderr, creationflags=creationflags,
            start_new_session=os.name != "nt",
        )
        try:
            returncode = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            _kill_process_tree(process)
            raise RuntimeError(
                f"{Path(command[0]).name} exceeded bounded timeout {timeout}s"
            ) from error
    elapsed = round(time.monotonic() - started, 3)
    if returncode != 0:
        detail = stderr_path.read_text(encoding="utf-8", errors="replace").strip()
        raise RuntimeError(
            f"{Path(command[0]).name} failed with {returncode}: {detail}"
        )
    return {
        "argv_sha256": argv_sha256(command),
        "argc": len(command),
        "elapsed_seconds": elapsed,
        "returncode": returncode,
    }


def validate_focused_output(
    row: Mapping[str, Any], stdout: str, stderr: str,
) -> None:
    marker = row["stdout_marker"]
    if marker not in stdout:
        raise ValueError(f"{row['name']}: stdout marker is missing")
    stderr_marker = row["stderr_marker"]
    if stderr_marker is None:
        if stderr.strip():
            raise ValueError(f"{row['name']}: unexpected stderr")
    elif stderr_marker not in stderr:
        raise ValueError(f"{row['name']}: required diagnostic stderr is missing")


def run_focused(
    root: Path, build_dir: Path, run_root: Path, timeout: float,
) -> tuple[list[dict[str, Any]], dict[str, Path]]:
    results: list[dict[str, Any]] = []
    binaries: dict[str, Path] = {}
    focused_root = run_root / "focused"
    focused_root.mkdir()
    for index, row in enumerate(FOCUSED_MANIFEST):
        executable = platform_executable(build_dir, row["executable"])
        binaries[row["name"]] = executable
        child_root = focused_root / f"{index + 1:02d}-{row['name']}"
        child_root.mkdir()
        stdout_path = child_root / "stdout.log"
        stderr_path = child_root / "stderr.log"
        process = run_process_checked(
            [str(executable)], root, timeout, stdout_path, stderr_path
        )
        stdout = stdout_path.read_text(encoding="utf-8", errors="strict")
        stderr = stderr_path.read_text(encoding="utf-8", errors="strict")
        validate_focused_output(row, stdout, stderr)
        results.append({
            "name": row["name"],
            "result": "pass",
            "coverage": list(row["coverage"]),
            "binary": file_record(root, executable),
            "process": process,
            "stdout": file_record(root, stdout_path),
            "stderr": file_record(root, stderr_path),
        })
    return results, binaries


def _runtime_module(directory: Path, stem: str) -> Path:
    candidates = [
        directory / f"{stem}{suffix}"
        for suffix in (".dll", ".so", ".dylib")
        if (directory / f"{stem}{suffix}").is_file()
    ]
    if len(candidates) != 1:
        raise ValueError(f"expected one staged module {stem}: {candidates}")
    return candidates[0].resolve()


def runtime_artifact_paths(
    client_exe: Path, dedicated_exe: Path, working_dir: Path,
) -> dict[str, Path]:
    client_name = client_exe.stem
    if not client_name.startswith("worr_") or client_name.startswith("worr_ded_"):
        raise ValueError("client launcher name does not expose its architecture")
    architecture = client_name[len("worr_"):]
    if dedicated_exe.stem != f"worr_ded_{architecture}":
        raise ValueError("client and dedicated launcher architectures differ")
    base = working_dir / "basew"
    paths = {
        "client_launcher": client_exe.resolve(),
        "dedicated_launcher": dedicated_exe.resolve(),
        "client_engine": _runtime_module(working_dir, f"worr_engine_{architecture}"),
        "dedicated_engine": _runtime_module(
            working_dir, f"worr_ded_engine_{architecture}"
        ),
        "renderer": _runtime_module(working_dir, f"worr_opengl_{architecture}"),
        "rmlui_core": (working_dir / "rmlui_core.dll").resolve(),
        "cgame": _runtime_module(base, f"cgame_{architecture}"),
        "sgame": _runtime_module(base, f"sgame_{architecture}"),
        "pak": (base / "pak0.pkz").resolve(),
        "config": (base / "config.cfg").resolve(),
        "canonical_fixture": (
            base / "maps" / "worr_fr10_rewind_mover.bsp"
        ).resolve(),
    }
    missing = [name for name, path in paths.items() if not path.is_file()]
    if missing:
        raise ValueError(f"staged runtime closure is incomplete: {missing}")
    return paths


def clone_runtime(
    source: Path, destination: Path,
    client_exe: Path, dedicated_exe: Path,
) -> tuple[dict[str, Path], dict[str, str]]:
    before_paths = runtime_artifact_paths(client_exe, dedicated_exe, source)
    before_hashes = {name: file_sha256(path) for name, path in before_paths.items()}
    shutil.copytree(
        source,
        destination,
        ignore=shutil.ignore_patterns(*RUNTIME_TRANSIENT_PATTERNS),
    )
    relative_client = client_exe.relative_to(source)
    relative_dedicated = dedicated_exe.relative_to(source)
    after_hashes = {name: file_sha256(path) for name, path in before_paths.items()}
    if after_hashes != before_hashes:
        changed = sorted(name for name in before_hashes if before_hashes[name] != after_hashes[name])
        raise RuntimeError(f"staged runtime changed during isolation: {changed}")
    clone_paths = runtime_artifact_paths(
        destination / relative_client, destination / relative_dedicated,
        destination,
    )
    clone_hashes = {name: file_sha256(path) for name, path in clone_paths.items()}
    if clone_hashes != before_hashes:
        raise RuntimeError("isolated runtime closure does not match staged input")
    return clone_paths, before_hashes


def command_sets(command: Any, label: str) -> dict[str, str]:
    if not isinstance(command, list) or not command or not all(
        isinstance(value, str) for value in command
    ):
        raise ValueError(f"{label} must be a non-empty argv array")
    result: dict[str, str] = {}
    index = 1
    while index < len(command):
        if command[index] != "+set":
            index += 1
            continue
        if index + 2 >= len(command):
            raise ValueError(f"{label} has a truncated +set")
        name, value = command[index + 1], command[index + 2]
        # Preserve command-line order: Quake accepts repeated +set entries and
        # the canonical snapshot-presentation child intentionally starts with
        # cl_headless=1 before restoring only its hidden presentation cadence
        # with cl_headless=0. Safety-critical final values are checked below.
        result[name] = value
        index += 3
    return result


def validate_command_report(report: Mapping[str, Any], expected_mask: int) -> dict[str, Any]:
    if report.get("schema") != "worr.networking.native-shadow-runtime.v1":
        raise ValueError("native-command child schema changed")
    if report.get("passed") is not True:
        raise ValueError("native-command child did not pass")
    trials = require_dict(report.get("trials"), "native-command.trials")
    if set(trials) != {"fragment_pressure", "post_burst_async_ack"}:
        raise ValueError("native-command child trial set changed")
    for name, trial_value in trials.items():
        trial = require_dict(trial_value, f"native-command.{name}")
        statuses = require_dict(trial.get("statuses"), f"{name}.statuses")
        for endpoint in ("client", "server"):
            row = require_dict(statuses.get(endpoint), f"{name}.{endpoint}")
            exact = {
                "schema": 1, "enabled": 1, "protocol": PROTOCOL,
                "public_mask": expected_mask, "private_mask": expected_mask,
                "failures": 0, "last_failure": 0,
            }
            for field, expected in exact.items():
                if require_int(row.get(field), f"{name}.{endpoint}.{field}") != expected:
                    raise ValueError(
                        f"{name}.{endpoint} did not report {field}={expected:#x}"
                    )
        processes = require_dict(trial.get("processes"), f"{name}.processes")
        if (processes.get("client_terminated_by_harness") is not True or
                processes.get("server_terminated_by_harness") is not True):
            raise ValueError(f"{name}: child processes were not terminated")
        delivery = require_dict(
            trial.get("reliable_delivery"), f"{name}.reliable_delivery"
        )
        if delivery.get("complete_exact_once") is not True:
            raise ValueError(f"{name}: reliable payload was not exact-once")
    return {
        "lane": "command", "mask": expected_mask,
        "mask_observation": "direct-client-and-server-status",
        "trials": sorted(trials),
    }


def _validate_headless_canonical_commands(
    report: Mapping[str, Any], expected_flags: Mapping[str, bool],
) -> None:
    dedicated = report.get("dedicated_command")
    dedicated_sets = command_sets(dedicated, "canonical dedicated command")
    if "+connect" in dedicated:
        raise ValueError("canonical dedicated command launched a client")
    server_expected = {
        "sv_worr_native_shadow": expected_flags["native"],
        "sv_worr_native_event_shadow": expected_flags["event"],
        "sv_worr_native_snapshot_shadow": expected_flags["snapshot"],
    }
    for name, enabled in server_expected.items():
        if (dedicated_sets.get(name) == "1") != enabled:
            raise ValueError(f"canonical dedicated flag {name} drifted")
    for role in ("shooter", "target"):
        command = report.get(f"{role}_command")
        sets = command_sets(command, f"canonical {role} command")
        exact_headless = {
            "win_headless": "1", "in_enable": "0", "in_grab": "0",
            "s_enable": "0",
        }
        for name, expected in exact_headless.items():
            if sets.get(name) != expected:
                raise ValueError(f"canonical {role} violates {name}={expected}")
        if "+connect" not in command:
            raise ValueError(f"canonical {role} is not a real connected client")
        # The snapshot-presentation fixture deliberately keeps the shooter on
        # legacy networking and arms only the independent target. Event-only
        # and combined fixtures arm both real clients.
        snapshot_enabled = expected_flags["snapshot"] and not (
            role == "shooter" and not expected_flags["event"]
        )
        native_enabled = expected_flags["native"] and (
            expected_flags["event"] or snapshot_enabled
        )
        client_expected = {
            "cl_worr_native_shadow": native_enabled,
            "cl_worr_native_event_shadow": expected_flags["event"],
            "cl_worr_native_snapshot_shadow": snapshot_enabled,
        }
        for name, enabled in client_expected.items():
            if (sets.get(name) == "1") != enabled:
                raise ValueError(f"canonical {role} flag {name} drifted")


def _validate_native_base_status_container(
    container: Mapping[str, Any], expected_mask: int,
) -> int:
    clients = require_dict(container.get("clients"), "native clients")
    server_peers = require_list(container.get("server_peers"), "native server peers")
    if not clients or not server_peers:
        raise ValueError("native status container has no live peer evidence")
    for role, row_value in clients.items():
        row = require_dict(row_value, f"native client {role}")
        exact = {
            "schema": 1, "enabled": 1, "mode": 2,
            "capability_confirmed": 1,
            "protocol": PROTOCOL, "public_mask": expected_mask,
            "private_mask": expected_mask, "failures": 0, "last_failure": 0,
        }
        for field, expected in exact.items():
            if require_int(row.get(field), f"client {role}.{field}") != expected:
                raise ValueError(f"native client {role} did not prove {field}={expected:#x}")
        require_int(row.get("server_active"), f"client {role}.server_active", minimum=1)
    for index, row_value in enumerate(server_peers):
        row = require_dict(row_value, f"native server peer {index}")
        exact = {
            "schema": 1, "enabled": 1, "protocol": PROTOCOL,
            "public_mask": expected_mask, "private_mask": expected_mask,
            "wire_committed": 1, "failures": 0, "last_failure": 0,
            "rx_rejections": 0, "tx_ack_rejections": 0,
        }
        for field, expected in exact.items():
            if require_int(row.get(field), f"server {index}.{field}") != expected:
                raise ValueError(f"native server {index} did not prove {field}={expected:#x}")
        require_int(
            row.get("server_active"), f"server {index}.server_active", minimum=1,
        )
    return len(server_peers)


def _validate_native_status_container(
    container: Mapping[str, Any], expected_mask: int,
) -> int:
    native_peers = _validate_native_base_status_container(container, expected_mask)
    snapshot_peers = require_list(
        container.get("snapshot_peers"), "native snapshot peers"
    )
    if not snapshot_peers:
        raise ValueError("native status container has no snapshot peer evidence")
    for index, row_value in enumerate(snapshot_peers):
        row = require_dict(row_value, f"native snapshot peer {index}")
        for field, expected in {
            "schema": 1, "sender": 1, "queue_failures": 0,
            "rejected": 0, "retired_sender": 0, "retired_retained": 0,
        }.items():
            if require_int(row.get(field), f"snapshot {index}.{field}") != expected:
                raise ValueError(
                    f"native snapshot peer {index} did not prove {field}={expected}"
                )
        for field in ("acks", "released"):
            require_int(row.get(field), f"snapshot {index}.{field}", minimum=1)
    return len(snapshot_peers)


def _validate_legacy_capability_status_container(
    container: Mapping[str, Any], expected_mask: int,
) -> int:
    clients = require_dict(container.get("clients"), "legacy clients")
    server_peers = require_list(
        container.get("server_peers"), "legacy server peers",
    )
    if set(clients) != {"shooter", "target"} or len(server_peers) != 2:
        raise ValueError("legacy status did not report both exact live peers")
    client_epochs: set[int] = set()
    for role, row_value in clients.items():
        row = require_dict(row_value, f"legacy client {role}")
        for field, expected in {
            "schema": 1, "valid": 1, "phase": 2, "protocol": PROTOCOL,
            "offered": expected_mask, "supported": expected_mask,
            "peer_supported": expected_mask, "negotiated": expected_mask,
        }.items():
            if require_int(row.get(field), f"legacy client {role}.{field}") != expected:
                raise ValueError(
                    f"legacy client {role} did not prove {field}={expected:#x}"
                )
        client_epochs.add(require_int(
            row.get("epoch"), f"legacy client {role}.epoch", minimum=1,
        ))
    server_epochs: set[int] = set()
    for index, row_value in enumerate(server_peers):
        row = require_dict(row_value, f"legacy server peer {index}")
        for field, expected in {
            "schema": 1, "protocol": PROTOCOL,
            "offered": expected_mask, "supported": expected_mask,
            "negotiated": expected_mask, "confirm_sent": 1, "failed": 0,
            "native_shadow": 0, "input_batch_requested": 0,
            "command_parser": 1,
        }.items():
            if require_int(row.get(field), f"legacy server {index}.{field}") != expected:
                raise ValueError(
                    f"legacy server {index} did not prove {field}={expected:#x}"
                )
        server_epochs.add(require_int(
            row.get("epoch"), f"legacy server {index}.epoch", minimum=1,
        ))
    if len(client_epochs) != 2 or client_epochs != server_epochs:
        raise ValueError("legacy client/server capability epochs did not align")
    return len(server_peers)


def _validate_reconnect(run: Mapping[str, Any], lane: str) -> None:
    reconnect = require_dict(run.get("reconnect"), f"{lane} reconnect")
    if (reconnect.get("required") is not True or
            require_int(
                reconnect.get("server_admissions"),
                f"{lane} reconnect admissions",
            ) < 3 or
            require_int(
                reconnect.get("shooter_serverdata_packets"),
                f"{lane} reconnect serverdata",
            ) < 2):
        raise ValueError(f"{lane} child did not prove the live reconnect")


def _validate_parity(parity_value: Any) -> None:
    parity = require_dict(parity_value, "local-action authority parity")
    for field in ("matches", "receipts", "passes"):
        require_int(parity.get(field), f"parity.{field}", minimum=1)
    for field in (
        "conflicts", "mismatches", "resync", "unmatched", "outstanding",
    ):
        if require_int(parity.get(field), f"parity.{field}") != 0:
            raise ValueError(f"local-action parity reported {field}")


def validate_canonical_report(
    report: Mapping[str, Any], row: Mapping[str, Any],
) -> dict[str, Any]:
    if report.get("schema") != CANONICAL_CHILD_SCHEMA:
        raise ValueError(f"{row['lane']}: canonical child schema changed")
    if report.get("weapon") != row["weapon"] or report.get("repeat") != 1:
        raise ValueError(f"{row['lane']}: canonical child mode/repeat changed")
    if report.get("client_count") != 2:
        raise ValueError(f"{row['lane']}: canonical child count changed")
    expected_flags = {
        "native": row["lane"] != "legacy",
        "event": row["lane"] in ("event", "combined"),
        "snapshot": row["lane"] in ("snapshot", "combined"),
    }
    _validate_headless_canonical_commands(report, expected_flags)
    status = require_dict(report.get("status"), f"{row['lane']}.status")
    if status.get("status") != "pass" or status.get("failure_code") != 0:
        raise ValueError(f"{row['lane']}: canonical weapon fixture did not pass")
    runs = require_list(report.get("runs"), f"{row['lane']}.runs")
    if len(runs) != 1:
        raise ValueError(f"{row['lane']}: canonical child did not run once")
    run = require_dict(runs[0], f"{row['lane']}.run")
    for field, expected in {
        "shooter_terminated_by_gate": True,
        "target_terminated_by_gate": True,
        "spectator_terminated_by_gate": False,
        "server_terminated_by_gate": True,
    }.items():
        if run.get(field) is not expected:
            raise ValueError(f"{row['lane']}: {field} is not {expected}")

    lane = row["lane"]
    result: dict[str, Any] = {"lane": lane, "mask": row["mask"]}
    if lane == "legacy":
        container = require_dict(
            run.get("legacy_capability_status"), "legacy capability status",
        )
        result["legacy_peers"] = _validate_legacy_capability_status_container(
            container, row["mask"],
        )
        result["mask_observation"] = "direct-client-and-server-status"
        result["numeric_status_in_child_report"] = True
    elif lane == "event":
        _validate_reconnect(run, lane)
        _validate_parity(run.get("local_action_authority_parity"))
        container = require_dict(run.get("native_event_shadow"), "event shadow")
        event_clients = require_dict(container.get("clients"), "event clients")
        event_server_peers = require_list(
            container.get("server_peers"), "event server peers",
        )
        if set(event_clients) != {"shooter", "target"} or len(event_server_peers) != 2:
            raise ValueError("event shadow did not report both exact live peers")
        native_peers = _validate_native_base_status_container(
            container, row["mask"],
        )
        result["mask_observation"] = "direct-client-and-server-status"
        result["reconnect"] = True
        result["numeric_status_in_child_report"] = True
        result["native_peers"] = native_peers
    elif lane == "snapshot":
        container = require_dict(run.get("native_snapshot_shadow"), "snapshot shadow")
        snapshot_peers = _validate_native_status_container(container, row["mask"])
        presentation = require_dict(
            run.get("native_snapshot_presentation"), "snapshot presentation"
        )
        for field in ("native_authority_samples", "promoted_transforms"):
            require_int(presentation.get(field), field, minimum=1)
        for field in (
            "clock_failures", "pair_failures", "alignment_failures",
            "sample_failures", "event_audit_failures", "parity_mismatches",
        ):
            if require_int(presentation.get(field), field) != 0:
                raise ValueError(f"snapshot presentation reported {field}")
        result["mask_observation"] = "direct-client-and-server-status"
        result["presentation"] = True
        result["snapshot_peers"] = snapshot_peers
    elif lane == "combined":
        preflight = require_dict(
            run.get("combined_native_preflight"), "combined preflight",
        )
        _validate_native_status_container(preflight, row["mask"])
        container = require_dict(run.get("combined_native_shadow"), "combined shadow")
        snapshot_peers = _validate_native_status_container(container, row["mask"])
        _validate_parity(run.get("local_action_authority_parity"))
        _validate_reconnect(run, lane)
        result["mask_observation"] = "direct-client-and-server-status"
        result["preflight"] = True
        result["reconnect"] = True
        result["snapshot_peers"] = snapshot_peers
    else:
        raise ValueError(f"unexpected canonical live lane {lane}")
    return result


def build_live_command(
    row: Mapping[str, Any], runner: Path, client: Path, dedicated: Path,
    working_dir: Path, output: Path, args: argparse.Namespace,
) -> list[str]:
    command = [
        sys.executable, str(runner),
        "--client-exe", str(client),
        "--dedicated-exe", str(dedicated),
        "--working-dir", str(working_dir),
        "--output", str(output),
    ]
    if row["runner_kind"] == "native-command":
        command.extend(("--timeout", str(args.native_command_timeout)))
    elif row["runner_kind"] == "canonical-weapon":
        command.extend((
            "--port", str(args.base_port + row["port_offset"]),
            "--repeat", "1",
            "--timeout", str(args.canonical_timeout),
            "--weapon", str(row["weapon"]),
        ))
    else:
        raise ValueError(f"unknown live runner kind {row['runner_kind']}")
    return command


def run_live(
    root: Path, run_root: Path, runtime_paths: Mapping[str, Path],
    runtime_root: Path, args: argparse.Namespace,
) -> list[dict[str, Any]]:
    live_root = run_root / "live"
    live_root.mkdir()
    results: list[dict[str, Any]] = []
    for index, row in enumerate(LIVE_MANIFEST):
        child_root = live_root / f"{index + 1:02d}-{row['lane']}"
        child_root.mkdir()
        runner = (root / row["runner"]).resolve()
        if not runner.is_file():
            raise ValueError(f"live runner is missing: {runner}")
        child_output = child_root / "report.json"
        stdout_path = child_root / "parent-child.stdout.log"
        stderr_path = child_root / "parent-child.stderr.log"
        command = build_live_command(
            row, runner, runtime_paths["client_launcher"],
            runtime_paths["dedicated_launcher"], runtime_root,
            child_output, args,
        )
        process = run_process_checked(
            command, root, args.live_child_timeout, stdout_path, stderr_path
        )
        if stderr_path.read_text(encoding="utf-8", errors="replace").strip():
            raise RuntimeError(f"{row['lane']} live child emitted stderr")
        report = load_json(child_output, f"{row['lane']} live report")
        if row["runner_kind"] == "native-command":
            summary = validate_command_report(report, row["mask"])
        else:
            summary = validate_canonical_report(report, row)
        results.append({
            **summary,
            "result": "pass",
            "coverage": list(row["coverage"]),
            "runner": file_record(root, runner),
            "process": process,
            "report": file_record(root, child_output),
            "stdout": file_record(root, stdout_path),
            "stderr": file_record(root, stderr_path),
        })
    return results


def q2proto_state(root: Path) -> dict[str, Any]:
    q2proto = root / "q2proto"
    if not q2proto.is_dir():
        raise ValueError("q2proto directory is missing")
    process = subprocess.run(
        ["git", "status", "--porcelain=v1", "--untracked-files=all", "--", "q2proto"],
        cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
        creationflags=(getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0),
    )
    if process.returncode != 0:
        raise RuntimeError(
            "could not inspect q2proto git state: " +
            process.stderr.decode("utf-8", errors="replace").strip()
        )
    status = process.stdout.decode("utf-8", errors="strict").strip()
    if status:
        raise RuntimeError("q2proto is not unchanged: " + status)
    return {"tracked_and_untracked_diff_empty": True, **directory_digest(q2proto)}


def source_artifacts(root: Path) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for relative in SOURCE_INPUTS:
        path = (root / relative).resolve()
        if not path.is_file():
            raise ValueError(f"source input is missing: {relative}")
        result[relative] = file_record(root, path)
    return result


def build_evidence(
    *, run_id: str, started: datetime, scope: str,
    manifest: dict[str, Any], sources: dict[str, Any],
    focused: list[dict[str, Any]], live: list[dict[str, Any]],
    q2proto: dict[str, Any], runtime: dict[str, Any] | None,
) -> dict[str, Any]:
    if [row["name"] for row in focused] != [
        row["name"] for row in FOCUSED_MANIFEST
    ]:
        raise ValueError("focused results do not match the fixed manifest")
    if scope == "full" and [row["lane"] for row in live] != [
        row["lane"] for row in LIVE_MANIFEST
    ]:
        raise ValueError("live results do not match the fixed manifest")
    if scope == "focused" and live:
        raise ValueError("focused scope unexpectedly contains live results")
    direct_numeric = {
        row["lane"]: row.get("mask_observation") ==
            "direct-client-and-server-status"
        for row in live
    }
    if scope == "full":
        for lane in ("legacy", "event"):
            if direct_numeric.get(lane) is not True:
                raise ValueError(
                    "full T04 evidence requires direct client/server numeric "
                    f"{lane} status"
                )
    gates = {
        "exact_mask_contract": True,
        "all_focused_children_passed": True,
        "source_inputs_stable": True,
        "q2proto_unchanged": True,
        "isolated_runtime": scope == "full",
        "all_live_children_passed": scope == "full",
        "live_legacy_0x03": scope == "full",
        "live_command_0x53": scope == "full",
        "live_event_0x73": scope == "full",
        "live_snapshot_0x57": scope == "full",
        "live_combined_0x77": scope == "full",
        "direct_numeric_legacy_status": direct_numeric.get("legacy", False),
        "direct_numeric_event_status": direct_numeric.get("event", False),
        "task_complete": False,
    }
    dod = [
        {
            "row": "legacy stream cannot be reinterpreted as WORR traffic",
            "status": "direct-focused",
            "evidence": ["capability-policy", "readiness-reconnect"],
        },
        {
            "row": "envelope serializes/fragments/prioritizes canonical records",
            "status": "bounded-direct" if scope == "full" else "focused-only",
            "evidence": [
                "envelope-mtu-validation", "command-adapter",
                "event-production-virtual-link", "snapshot-production-virtual-link",
            ],
        },
        {
            "row": "legacy and WORR adapters feed the same validators and consumers",
            "status": "partial",
            "evidence": [
                "server-production-pilot", "client-production-pilot",
                "command/event/snapshot/combined live lanes" if scope == "full"
                else "live lanes not run",
            ],
        },
        {
            "row": "downgrade/version/malformed/reconnect/MTU deterministic matrix",
            "status": "bounded-direct" if scope == "full" else "focused-only",
            "evidence": [
                "capability-policy", "envelope-mtu-validation",
                "readiness-reconnect", "server-production-pilot",
                "client-production-pilot",
            ],
        },
        {
            "row": "q2proto unchanged and legacy server/demo paths operational",
            "status": "partial",
            "evidence": [
                "q2proto clean tree",
                "direct live legacy 0x03 client/server status" if scope == "full"
                else "legacy live row not run",
                "supported legacy demo/MVD/relay matrix remains open",
            ],
        },
    ]
    semantic = {
        "task": TASK, "status": STATUS, "scope": scope,
        "manifest_sha256": manifest["sha256"],
        "source_sha256": {key: value["sha256"] for key, value in sources.items()},
        "focused": [{
            "name": row["name"],
            "binary": row["binary"]["sha256"],
            "argv": row["process"]["argv_sha256"],
            "stdout": row["stdout"]["sha256"],
            "stderr": row["stderr"]["sha256"],
        } for row in focused],
        "live": [{"lane": row["lane"], "mask": row["mask"],
                  "runner": row["runner"]["sha256"],
                  "argv": row["process"]["argv_sha256"],
                  "report": row["report"]["sha256"],
                  "stdout": row["stdout"]["sha256"],
                  "stderr": row["stderr"]["sha256"]} for row in live],
        "runtime": None if runtime is None else {
            key: value["sha256"]
            for key, value in require_dict(
                runtime.get("components"), "runtime components"
            ).items()
        },
        "q2proto_sha256": q2proto["sha256"],
        "open": list(OPEN_COVERAGE), "gates": gates,
    }
    return {
        "schema": EVIDENCE_SCHEMA,
        "task": TASK,
        "status": STATUS,
        "result": "pass",
        "scope": scope,
        "run_id": run_id,
        "started_at_utc": started.isoformat(),
        "completed_at_utc": datetime.now(timezone.utc).isoformat(),
        "manifest": manifest,
        "artifacts": {
            "sources": sources,
            "q2proto": q2proto,
            "runtime": runtime,
        },
        "focused": focused,
        "live": live,
        "definition_of_done": dod,
        "gates": gates,
        "limitations": list(OPEN_COVERAGE),
        "semantic_sha256": semantic_sha256(semantic),
    }


def execute(args: argparse.Namespace) -> dict[str, Any]:
    root = args.repo_root.resolve()
    if not root.is_dir():
        raise ValueError("repository root is missing")
    validate_bounds(args)
    validate_manifests()
    output = require_networking_output(root, args.output)
    build_dir = resolve_repo_path(root, args.build_dir, "build directory")
    if not build_dir.is_dir():
        raise ValueError("build directory is missing")
    working_dir = resolve_repo_path(root, args.working_dir, "working directory")
    client_exe = resolve_repo_path(root, args.client_exe, "client executable")
    dedicated_exe = resolve_repo_path(root, args.dedicated_exe, "dedicated executable")

    started = datetime.now(timezone.utc)
    run_id = started.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_root = output.parent / f"{output.stem}.runs" / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    manifest = manifest_document()
    before_sources = source_artifacts(root)
    q2proto_before = q2proto_state(root)
    focused, focused_binaries = run_focused(
        root, build_dir, run_root, args.focused_timeout
    )
    binary_hashes = {
        name: file_sha256(path) for name, path in focused_binaries.items()
    }

    live: list[dict[str, Any]] = []
    runtime_document: dict[str, Any] | None = None
    if args.scope == "full":
        if not working_dir.is_dir() or not client_exe.is_file() or not dedicated_exe.is_file():
            raise ValueError("staged working directory or launcher is missing")
        try:
            client_exe.relative_to(working_dir)
            dedicated_exe.relative_to(working_dir)
        except ValueError as error:
            raise ValueError("staged launchers must be inside working-dir") from error
        isolated_runtime = run_root / "runtime" / ".install"
        isolated_runtime.parent.mkdir()
        try:
            isolated_runtime.relative_to(working_dir)
        except ValueError:
            pass
        else:
            raise ValueError("isolated runtime destination cannot be inside working-dir")
        runtime_paths, staged_hashes = clone_runtime(
            working_dir, isolated_runtime, client_exe, dedicated_exe
        )
        live = run_live(root, run_root, runtime_paths, isolated_runtime, args)
        final_clone_hashes = {
            name: file_sha256(path) for name, path in runtime_paths.items()
        }
        if final_clone_hashes != staged_hashes:
            changed = sorted(
                name for name in staged_hashes
                if staged_hashes[name] != final_clone_hashes[name]
            )
            raise RuntimeError(f"isolated runtime changed during live gate: {changed}")
        runtime_document = {
            "source": relative_name(root, working_dir),
            "isolated_copy": relative_name(root, isolated_runtime),
            "drift_check": "source-before/after-copy and clone-after-live identical",
            "components": {
                name: file_record(root, path) for name, path in runtime_paths.items()
            },
        }

    after_sources = source_artifacts(root)
    if after_sources != before_sources:
        changed = sorted(
            name for name in before_sources
            if before_sources[name] != after_sources[name]
        )
        raise RuntimeError(f"source inputs changed during acceptance: {changed}")
    final_binary_hashes = {
        name: file_sha256(path) for name, path in focused_binaries.items()
    }
    if final_binary_hashes != binary_hashes:
        raise RuntimeError("focused binaries changed during acceptance")
    q2proto_after = q2proto_state(root)
    if q2proto_after != q2proto_before:
        raise RuntimeError("q2proto tree changed during acceptance")
    return build_evidence(
        run_id=run_id, started=started, scope=args.scope,
        manifest=manifest, sources=before_sources, focused=focused,
        live=live, q2proto=q2proto_before, runtime=runtime_document,
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    root = args.repo_root.resolve()
    try:
        output = require_networking_output(root, args.output)
    except (OSError, ValueError) as error:
        print(f"FR-10-T04 acceptance failed: {error}", file=sys.stderr)
        return 1
    output.unlink(missing_ok=True)
    failure_output = output.with_suffix(".failure.json")
    failure_output.unlink(missing_ok=True)
    try:
        evidence = execute(args)
        write_json_atomic(output, evidence)
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        write_json_atomic(failure_output, {
            "schema": FAILURE_SCHEMA,
            "task": TASK,
            "status": "failed",
            "scope": args.scope,
            "failed_at_utc": datetime.now(timezone.utc).isoformat(),
            "error_type": type(error).__name__,
            "error": str(error),
            "requested_output": relative_name(root, output),
        })
        print(f"FR-10-T04 acceptance failed: {error}", file=sys.stderr)
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
