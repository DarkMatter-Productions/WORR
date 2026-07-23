#!/usr/bin/env python3
"""Run the explicitly partial aggregate evidence gate for FR-10-T12.

This parent does not claim exhaustive weapon-interaction acceptance.  It binds
the existing production headless weapon runner to a fixed, reviewable subset
of 39 modes, requires three semantically identical repetitions per mode, and
publishes the remaining T12 coverage as an explicit open list.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import signal
import subprocess
import sys
from collections.abc import Sequence
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


MANIFEST_SCHEMA = "worr.networking.fr10-t12-partial-acceptance-manifest.v1"
EVIDENCE_SCHEMA = "worr.networking.fr10-t12-partial-acceptance-evidence.v1"
FAILURE_SCHEMA = EVIDENCE_SCHEMA + ".failure"
TASK = "FR-10-T12"
ARTIFACT_STATUS = "partial"
REPEAT = 3
MAX_PER_REPEAT_TIMEOUT_SECONDS = 60.0
MAX_CHILD_TIMEOUT_SECONDS = 240.0
RUNTIME_MODULE_SUFFIXES = (".dll", ".so", ".dylib")

# This is intentionally an embedded, literal manifest rather than a discovery
# of every mode exported by the child runner.  Adding a child mode therefore
# cannot silently widen this artifact's stated coverage.
SCENARIO_MANIFEST: tuple[dict[str, Any], ...] = (
    {"mode": "disruptor", "weapon_policy": 6, "expected_damage": 45,
     "require_damage": True, "coverage": "historical-projectile-convergence"},
    {"mode": "plasma-beam", "weapon_policy": 7, "expected_damage": 8,
     "require_damage": True, "coverage": "historical-continuous-beam"},
    {"mode": "plasma-beam-held", "weapon_policy": 7, "expected_damage": 24,
     "require_damage": True, "coverage": "historical-continuous-beam-held"},
    {"mode": "plasma-beam-sustained", "weapon_policy": 7, "expected_damage": 256,
     "require_damage": True, "coverage": "historical-continuous-beam-sustained"},
    {"mode": "plasma-beam-release", "weapon_policy": 7, "expected_damage": 24,
     "require_damage": True, "coverage": "historical-continuous-beam-release"},
    {"mode": "plasma-beam-water-retrace", "weapon_policy": 7, "expected_damage": 4,
     "require_damage": True, "coverage": "historical-continuous-beam-water-retrace"},
    {"mode": "thunderbolt", "weapon_policy": 8, "expected_damage": 8,
     "require_damage": True, "coverage": "historical-thunderbolt-beam"},
    {"mode": "thunderbolt-held", "weapon_policy": 8, "expected_damage": 24,
     "require_damage": True, "coverage": "historical-thunderbolt-held"},
    {"mode": "thunderbolt-sustained", "weapon_policy": 8, "expected_damage": 256,
     "require_damage": True, "coverage": "historical-thunderbolt-sustained"},
    {"mode": "thunderbolt-release", "weapon_policy": 8, "expected_damage": 24,
     "require_damage": True, "coverage": "historical-thunderbolt-release"},
    {"mode": "thunderbolt-water-retrace", "weapon_policy": 8, "expected_damage": 4,
     "require_damage": True, "coverage": "historical-thunderbolt-water-retrace"},
    {"mode": "thunderbolt-discharge", "weapon_policy": 8, "expected_damage": 70,
     "require_damage": True, "coverage": "current-authority-water-discharge"},
    {"mode": "rocket", "weapon_policy": 9, "expected_damage": 100,
     "require_damage": True, "coverage": "current-projectile-direct"},
    {"mode": "rocket-mover-relative", "weapon_policy": 9,
     "expected_damage": 100, "require_damage": True,
     "coverage": "current-projectile-mover-relative-direct"},
    {"mode": "rocket-lifecycle-touch", "weapon_policy": 9,
     "expected_damage": 100, "require_damage": True,
     "rocket_lifecycle_policy": 1,
     "coverage": "current-rocket-owner-single-touch-retirement"},
    {"mode": "rocket-lifetime-expiry", "weapon_policy": 9,
     "expected_damage": 0, "require_damage": False,
     "rocket_lifecycle_policy": 2,
     "coverage": "current-rocket-target-free-lifetime-expiry"},
    {"mode": "rocket-splash", "weapon_policy": 9, "expected_damage": 58,
     "require_damage": True, "splash_occlusion_policy": 1,
     "coverage": "current-projectile-splash-clear-player"},
    {"mode": "rocket-splash-bsp-occlusion", "weapon_policy": 9,
     "expected_damage": 0, "require_damage": False,
     "splash_occlusion_policy": 2,
     "coverage": "current-projectile-splash-bsp-occluded"},
    {"mode": "rocket-splash-water-boundary", "weapon_policy": 9,
     "expected_damage": 58, "require_damage": True,
     "splash_occlusion_policy": 3,
     "coverage": "current-projectile-splash-water-boundary"},
    {"mode": "plasma-gun", "weapon_policy": 10, "expected_damage": 20,
     "require_damage": True, "coverage": "current-plasma-projectile-direct"},
    {"mode": "plasma-gun-splash", "weapon_policy": 10, "expected_damage": 7,
     "require_damage": True, "coverage": "current-plasma-projectile-splash"},
    {"mode": "blaster", "weapon_policy": 11, "expected_damage": 15,
     "require_damage": True, "coverage": "current-bolt-direct"},
    {"mode": "hyperblaster", "weapon_policy": 11, "expected_damage": 15,
     "require_damage": True, "coverage": "current-repeating-bolt"},
    {"mode": "chainfist", "weapon_policy": 12, "expected_damage": 15,
     "require_damage": True, "coverage": "hybrid-historical-melee-selection"},
    {"mode": "etf-rifle", "weapon_policy": 13, "expected_damage": 10,
     "require_damage": True, "coverage": "current-flechette-direct"},
    {"mode": "phalanx", "weapon_policy": 14, "expected_damage": 80,
     "require_damage": True, "coverage": "current-phalanx-direct"},
    {"mode": "phalanx-splash", "weapon_policy": 14, "expected_damage": 93,
     "require_damage": True, "coverage": "current-phalanx-splash"},
    {"mode": "grenade-launcher", "weapon_policy": 15, "expected_damage": 60,
     "minimum_damage": 57, "require_damage": True,
     "coverage": "current-gravity-projectile-splash"},
    {"mode": "hand-grenade", "weapon_policy": 16, "expected_damage": 60,
     "minimum_damage": 57, "require_damage": False,
     "coverage": "current-held-release-grenade-flight"},
    {"mode": "hand-grenade-splash", "weapon_policy": 16, "expected_damage": 60,
     "minimum_damage": 45, "require_damage": True,
     "coverage": "current-held-release-grenade-splash"},
    {"mode": "prox-launcher", "weapon_policy": 17, "expected_damage": 90,
     "require_damage": False, "coverage": "current-proximity-mine-flight"},
    {"mode": "prox-launcher-lifecycle", "weapon_policy": 17, "expected_damage": 61,
     "require_damage": True, "coverage": "current-proximity-mine-lifecycle"},
    {"mode": "bfg", "weapon_policy": 18, "expected_damage": 200,
     "require_damage": False, "coverage": "current-bfg-flight"},
    {"mode": "ion-ripper", "weapon_policy": 19, "expected_damage": 10,
     "require_damage": False, "coverage": "current-multi-bolt-flight"},
    {"mode": "tesla-mine", "weapon_policy": 20, "expected_damage": 3,
     "require_damage": False, "coverage": "current-tesla-release-flight"},
    {"mode": "trap", "weapon_policy": 21, "expected_damage": 20,
     "require_damage": False, "coverage": "current-trap-release-flight"},
    {"mode": "grapple", "weapon_policy": 22, "expected_damage": 1,
     "require_damage": False, "coverage": "current-grapple-flight"},
    {"mode": "proball-throw", "weapon_policy": 23, "expected_damage": 1,
     "require_damage": False, "coverage": "current-proball-release-flight"},
    {"mode": "offhand-hook", "weapon_policy": 24, "expected_damage": 1,
     "require_damage": False, "coverage": "authenticated-offhand-hook-flight"},
)

OPEN_COVERAGE: tuple[str, ...] = (
    "exhaustive-non-weapon-interaction-catalog",
    "remaining-projectile-family-ownership-lifetime-and-collision-matrix",
    "moving-and-multi-target-fairness",
    "trigger-and-deployable-lifecycle-breadth",
    "coop-monster-and-other-melee-interactions",
    "abuse-load-and-release-sequence-breadth",
)

EXCLUDED_PREVIOUS_TASK_MODES = frozenset({
    "machinegun", "chaingun", "shotgun", "super-shotgun", "railgun",
    "railgun-mover-occlusion", "railgun-spectator-exclusion",
    "railgun-spawn-protection",
})
EXCLUDED_LOCAL_ACTION_MODES = frozenset({
    "blaster-local-action-lease", "blaster-local-action-lease-combined",
    "blaster-native-snapshot-presentation",
})


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=root)
    parser.add_argument(
        "--canonical-runner", type=Path,
        default=Path("tools/networking/run_canonical_rail_damage_runtime_gate.py"),
    )
    parser.add_argument("--client-exe", type=Path, required=True)
    parser.add_argument("--dedicated-exe", type=Path, required=True)
    parser.add_argument("--working-dir", type=Path, required=True)
    parser.add_argument(
        "--output", type=Path,
        default=Path(".tmp/networking/fr10_t12_partial_acceptance.json"),
    )
    parser.add_argument("--repeat", type=int, default=REPEAT)
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("--child-timeout", type=float, default=180.0)
    parser.add_argument("--base-port", type=int, default=27960)
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


def _runtime_module(directory: Path, stem: str) -> Path:
    matches = [
        directory / f"{stem}{suffix}" for suffix in RUNTIME_MODULE_SUFFIXES
        if (directory / f"{stem}{suffix}").is_file()
    ]
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one staged runtime module for {stem}, got {matches}"
        )
    return matches[0].resolve()


def runtime_artifact_paths(
    runner: Path, client_exe: Path, dedicated_exe: Path, working_dir: Path,
) -> dict[str, Path]:
    client_name = client_exe.name
    if client_name.lower().endswith(".exe"):
        client_name = client_name[:-4]
    if not client_name.startswith("worr_") or client_name.startswith("worr_ded_"):
        raise ValueError("client launcher name does not expose its architecture")
    architecture = client_name[len("worr_"):]
    expected_dedicated = f"worr_ded_{architecture}"
    dedicated_name = dedicated_exe.name
    if dedicated_name.lower().endswith(".exe"):
        dedicated_name = dedicated_name[:-4]
    if dedicated_name != expected_dedicated:
        raise ValueError("client and dedicated launcher architectures differ")

    base_game = working_dir / "basew"
    paths = {
        "canonical_runner": runner.resolve(),
        "client_launcher": client_exe.resolve(),
        "dedicated_launcher": dedicated_exe.resolve(),
        "client_engine": _runtime_module(
            working_dir, f"worr_engine_{architecture}"
        ),
        "dedicated_engine": _runtime_module(
            working_dir, f"worr_ded_engine_{architecture}"
        ),
        "opengl_renderer": _runtime_module(
            working_dir, f"worr_opengl_{architecture}"
        ),
        "cgame_module": _runtime_module(
            base_game, f"cgame_{architecture}"
        ),
        "sgame_module": _runtime_module(
            base_game, f"sgame_{architecture}"
        ),
        "staged_assets": (base_game / "pak0.pkz").resolve(),
        "fixture_map": (
            base_game / "maps" / "worr_fr10_rewind_mover.bsp"
        ).resolve(),
        "base_config": (base_game / "config.cfg").resolve(),
    }
    missing = [name for name, path in paths.items() if not path.is_file()]
    if missing:
        raise ValueError(f"staged runtime closure is incomplete: {missing}")
    return paths


def artifact_document(
    root: Path, paths: dict[str, Path], hashes: dict[str, str],
) -> dict[str, Any]:
    if paths.keys() != hashes.keys():
        raise ValueError("runtime artifact path/hash keys differ")
    return {
        name: {
            "path": relative_name(root, path),
            "sha256": hashes[name],
        }
        for name, path in paths.items()
    }


def semantic_sha256(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return sha256_bytes(encoded)


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
    networking_root = (root / ".tmp" / "networking").resolve()
    try:
        output.relative_to(networking_root)
    except ValueError as error:
        raise ValueError("output must be under .tmp/networking") from error
    if output.suffix.lower() != ".json":
        raise ValueError("output must be a JSON file")
    return output


def relative_name(root: Path, path: Path) -> str:
    return path.resolve().relative_to(root).as_posix()


def validate_execution_bounds(
    repeat: int, timeout: float, child_timeout: float, base_port: int,
) -> None:
    if repeat != REPEAT:
        raise ValueError("the bounded partial contract requires exactly three repeats")
    if not 0 < timeout <= MAX_PER_REPEAT_TIMEOUT_SECONDS:
        raise ValueError("per-repeat timeout exceeds the bounded contract")
    if not 0 < child_timeout <= MAX_CHILD_TIMEOUT_SECONDS:
        raise ValueError("child timeout exceeds the bounded contract")
    if not 1 <= base_port <= 65535 - len(SCENARIO_MANIFEST) + 1:
        raise ValueError("base port cannot provide one valid port per manifest mode")


def manifest_document(rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    core = {
        "schema": MANIFEST_SCHEMA,
        "task": TASK,
        "status": ARTIFACT_STATUS,
        "repeat": REPEAT,
        "mode_count": len(rows),
        "policy_count": len({row["weapon_policy"] for row in rows}),
        "live_repetitions": len(rows) * REPEAT,
        "scenarios": [dict(row) for row in rows],
    }
    return core | {"sha256": semantic_sha256(core)}


def validate_manifest(
    rows: Sequence[dict[str, Any]], child: Any, *, repeat: int = REPEAT,
) -> tuple[dict[str, Any], ...]:
    if repeat != REPEAT:
        raise ValueError("the T12 partial manifest requires exactly three repeats")
    if len(rows) != 39:
        raise ValueError("the T12 partial manifest must contain exactly 39 modes")
    normalized = tuple(dict(row) for row in rows)
    expected_names = tuple(row["mode"] for row in SCENARIO_MANIFEST)
    names = tuple(row.get("mode") for row in normalized)
    if names != expected_names or len(set(names)) != len(names):
        raise ValueError("the T12 partial manifest mode order/set changed")
    if set(names) & (EXCLUDED_PREVIOUS_TASK_MODES | EXCLUDED_LOCAL_ACTION_MODES):
        raise ValueError("the T12 partial manifest crossed a task ownership boundary")
    if {row.get("weapon_policy") for row in normalized} != set(range(6, 25)):
        raise ValueError("the T12 partial manifest must span policies 6 through 24")
    if not callable(getattr(child, "validate_status", None)) or not callable(
        getattr(child, "determinism_signature", None)
    ):
        raise ValueError("canonical child lacks semantic validation helpers")
    modes = getattr(child, "GATE_MODES", None)
    if not isinstance(modes, dict):
        raise ValueError("canonical child lacks the mode registry")

    for index, (row, expected) in enumerate(zip(normalized, SCENARIO_MANIFEST)):
        label = f"manifest[{index}]"
        if row != expected:
            raise ValueError(f"{label} differs from the explicit T12 partial manifest")
        mode = modes.get(row["mode"])
        if not isinstance(mode, dict):
            raise ValueError(f"{row['mode']}: canonical child mode is missing")
        exact = {
            "weapon_policy": row["weapon_policy"],
            "expected_damage": row["expected_damage"],
            "minimum_damage": row.get("minimum_damage"),
            "require_damage": row["require_damage"],
            "required_client_count": 2,
        }
        observed = {
            "weapon_policy": mode.get("weapon_policy"),
            "expected_damage": mode.get("expected_damage"),
            "minimum_damage": mode.get("minimum_damage"),
            "require_damage": mode.get("require_damage", True),
            "required_client_count": mode.get("required_client_count", 2),
        }
        if "splash_occlusion_policy" in row:
            exact["splash_occlusion_policy"] = row["splash_occlusion_policy"]
            observed["splash_occlusion_policy"] = mode.get(
                "expected_splash_occlusion_policy"
            )
        if "rocket_lifecycle_policy" in row:
            exact["rocket_lifecycle_policy"] = row["rocket_lifecycle_policy"]
            observed["rocket_lifecycle_policy"] = mode.get(
                "expected_rocket_lifecycle_policy"
            )
        if observed != exact:
            raise ValueError(
                f"{row['mode']}: canonical child policy/damage/client contract drifted"
            )
    return normalized


def command_sets(command: Any, label: str) -> dict[str, str]:
    if not isinstance(command, list) or not command or not all(
        isinstance(item, str) for item in command
    ):
        raise ValueError(f"{label} must be a non-empty string command array")
    result: dict[str, str] = {}
    index = 1
    while index < len(command):
        if command[index] != "+set":
            index += 1
            continue
        if index + 2 >= len(command):
            raise ValueError(f"{label} has a truncated +set")
        name, value = command[index + 1], command[index + 2]
        previous = result.get(name)
        if previous is not None and previous != value:
            raise ValueError(f"{label} changes cvar {name} within one launch")
        result[name] = value
        index += 3
    return result


def _require_within(path: Path, parent: Path, label: str) -> None:
    try:
        path.resolve().relative_to(parent.resolve())
    except ValueError as error:
        raise ValueError(f"{label} escaped the isolated parent run") from error


def validate_headless_commands(
    report: dict[str, Any], client_exe: Path, dedicated_exe: Path,
    isolated_root: Path, expected_port: int,
) -> None:
    dedicated = report.get("dedicated_command")
    if not isinstance(dedicated, list) or not dedicated or Path(
        dedicated[0]
    ).resolve() != dedicated_exe:
        raise ValueError("canonical child did not launch the required dedicated binary")
    dedicated_sets = command_sets(dedicated, "dedicated command")
    if dedicated_sets.get("maxclients") != "2" or dedicated_sets.get(
        "g_lag_compensation"
    ) != "1" or dedicated_sets.get("net_port") != str(expected_port):
        raise ValueError("canonical dedicated launch policy changed")
    if "+addbot" in dedicated or "+connect" in dedicated:
        raise ValueError("canonical dedicated launch used a client/bot path")

    commands = [report.get("shooter_command"), report.get("target_command")]
    if report.get("spectator_command") is not None or report.get("client_count") != 2:
        raise ValueError("T12 partial modes require exactly two real clients")
    exact = {
        "loc_language": "english", "win_headless": "1", "cl_headless": "1",
        "in_enable": "0", "in_grab": "0", "s_enable": "0",
    }
    qports: list[int] = []
    homes: list[Path] = []
    for index, command in enumerate(commands):
        label = f"canonical client {index}"
        if not isinstance(command, list) or not command or Path(
            command[0]
        ).resolve() != client_exe:
            raise ValueError(f"{label} executable changed")
        sets = command_sets(command, label)
        for name, expected in exact.items():
            if sets.get(name) != expected:
                raise ValueError(f"{label} violates headless {name}={expected}")
        if "+connect" not in command:
            raise ValueError(f"{label} does not connect as a real client")
        qport_positions = [
            offset for offset in range(1, len(command) - 2)
            if command[offset:offset + 2] == ["+set", "qport"]
        ]
        if len(qport_positions) != 1 or qport_positions[0] > command.index("+connect"):
            raise ValueError(f"{label} qport/connect ordering changed")
        qport_text = command[qport_positions[0] + 2]
        if not qport_text.isascii() or not qport_text.isdecimal():
            raise ValueError(f"{label} qport must be decimal")
        qport = int(qport_text)
        if not 0 < qport <= 0xff:
            raise ValueError(f"{label} qport must be a non-zero byte")
        qports.append(qport)
        home_text = sets.get("fs_homepath")
        if home_text is None:
            raise ValueError(f"{label} lacks an isolated fs_homepath")
        home = Path(home_text).resolve()
        _require_within(home, isolated_root, f"{label} fs_homepath")
        homes.append(home)
    if len(set(qports)) != 2 or len(set(homes)) != 2:
        raise ValueError("canonical clients must use distinct qports and homes")

    server_home_text = dedicated_sets.get("fs_homepath")
    if server_home_text is None:
        raise ValueError("canonical dedicated launch lacks an isolated fs_homepath")
    server_home = Path(server_home_text).resolve()
    _require_within(server_home, isolated_root, "dedicated fs_homepath")
    if server_home in homes:
        raise ValueError("dedicated and client runtime homes must be distinct")


def validate_child_report(
    report: dict[str, Any], row: dict[str, Any], child: Any,
    client_exe: Path, dedicated_exe: Path, isolated_root: Path,
    expected_port: int,
) -> dict[str, Any]:
    mode_name = row["mode"]
    if report.get("schema") != getattr(child, "SCHEMA", None):
        raise ValueError(f"{mode_name}: canonical child schema changed")
    exact = {
        "weapon": mode_name,
        "weapon_policy": row["weapon_policy"],
        "expected_damage": row["expected_damage"],
        "repeat": REPEAT,
        "client_count": 2,
    }
    for field, expected in exact.items():
        if report.get(field) != expected:
            raise ValueError(f"{mode_name}: report.{field} is not exact")
    validate_headless_commands(
        report, client_exe, dedicated_exe, isolated_root, expected_port
    )
    mode = child.GATE_MODES[mode_name]
    runs = require_list(report.get("runs"), f"{mode_name}.runs")
    if len(runs) != REPEAT:
        raise ValueError(f"{mode_name}: canonical run count changed")
    signatures: list[tuple[Any, ...]] = []
    for index, run_value in enumerate(runs):
        run = require_dict(run_value, f"{mode_name}.runs[{index}]")
        status = require_dict(run.get("status"), f"{mode_name}.runs[{index}].status")
        child.validate_status(status, mode)
        signature = tuple(child.determinism_signature(status))
        if not signature:
            raise ValueError(f"{mode_name}: child returned an empty semantic signature")
        signatures.append(signature)
        exact_termination = {
            "shooter_terminated_by_gate": True,
            "target_terminated_by_gate": True,
            "spectator_terminated_by_gate": False,
            "server_terminated_by_gate": True,
        }
        for field, expected in exact_termination.items():
            if run.get(field) is not expected:
                raise ValueError(f"{mode_name}: {field} is not {expected}")
    if any(signature != signatures[0] for signature in signatures[1:]):
        raise ValueError(f"{mode_name}: semantic repeat output diverged")
    top_status = require_dict(report.get("status"), f"{mode_name}.status")
    child.validate_status(top_status, mode)
    if tuple(child.determinism_signature(top_status)) != signatures[0]:
        raise ValueError(f"{mode_name}: top-level status differs from repeats")
    signature_list = list(signatures[0])
    return {
        "mode": mode_name,
        "weapon_policy": row["weapon_policy"],
        "expected_damage": row["expected_damage"],
        "coverage": row["coverage"],
        "repeat": REPEAT,
        "client_count": 2,
        "semantic_signature": signature_list,
        "semantic_sha256": semantic_sha256(signature_list),
        "all_launched_processes_terminated": True,
    }


def load_python_runner(path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(
        f"fr10_t12_child_{sha256_bytes(str(path).encode())[:12]}", path
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load Python runner: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, "main", None)):
        raise RuntimeError(f"Python runner has no callable main: {path}")
    return module


def run_child_checked(command: list[str], cwd: Path, timeout: float) -> None:
    """Run one child orchestrator with an enforceable wall-clock deadline.

    The child owns kill-on-close jobs for its Windows engine processes.  On a
    parent timeout, terminating the child closes those handles; on POSIX the
    child and all descendants share a fresh process group which is killed as a
    unit.  No timed-out child can continue publishing evidence after return.
    """
    if len(command) < 2 or Path(command[0]).resolve() != Path(sys.executable).resolve():
        raise RuntimeError("child runner command must use this Python executable")
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    process = subprocess.Popen(
        command,
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=creationflags,
        start_new_session=os.name != "nt",
    )
    try:
        _stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        if os.name == "nt":
            try:
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=10,
                    check=False,
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
        raise RuntimeError(
            f"{Path(command[1]).name} exceeded its bounded timeout of {timeout}s"
        ) from error

    detail = stderr.decode("utf-8", errors="replace").strip()
    if process.returncode != 0:
        raise RuntimeError(
            f"{Path(command[1]).name} failed with {process.returncode}: {detail}"
        )
    if detail:
        raise RuntimeError(f"{Path(command[1]).name} emitted stderr")


def build_child_command(
    runner: Path, client_exe: Path, dedicated_exe: Path, working_dir: Path,
    output: Path, mode: str, port: int, timeout: float,
) -> list[str]:
    return [
        sys.executable, str(runner),
        "--client-exe", str(client_exe),
        "--dedicated-exe", str(dedicated_exe),
        "--working-dir", str(working_dir),
        "--output", str(output),
        "--port", str(port),
        "--repeat", str(REPEAT),
        "--timeout", str(timeout),
        "--weapon", mode,
    ]


def build_evidence(
    *, run_id: str, started_at: str, completed_at: str,
    manifest: dict[str, Any], summaries: list[dict[str, Any]],
    components: list[dict[str, Any]], artifacts: dict[str, Any],
) -> dict[str, Any]:
    manifest_modes = [row["mode"] for row in manifest["scenarios"]]
    summary_modes = [row.get("mode") for row in summaries]
    component_modes = [row.get("mode") for row in components]
    if summary_modes != manifest_modes or component_modes != manifest_modes:
        raise ValueError("parent evidence does not cover the exact manifest mode order")
    if len(summaries) != 39 or len({row["weapon_policy"] for row in summaries}) != 19:
        raise ValueError("parent evidence lacks the bounded 39-mode/19-policy scope")
    gates = {
        "explicit_partial_status": True,
        "manifest_bound": True,
        "all_manifest_modes_passed": True,
        "semantic_repeat_determinism": True,
        "headless_input_free": True,
        "fresh_isolated_mode_and_role_roots": True,
        "fresh_runtime_root_per_repetition": False,
        "runtime_artifacts_stable": True,
        "all_launched_processes_terminated": True,
        "task_complete": False,
    }
    semantic = {
        "task": TASK,
        "status": ARTIFACT_STATUS,
        "manifest_sha256": manifest["sha256"],
        "runtime_artifact_sha256": {
            name: value["sha256"]
            for name, value in require_dict(
                artifacts.get("runtime"), "artifacts.runtime"
            ).items()
        },
        "proven_modes": summaries,
        "open_coverage": list(OPEN_COVERAGE),
        "gates": gates,
    }
    return {
        "schema": EVIDENCE_SCHEMA,
        "task": TASK,
        "status": ARTIFACT_STATUS,
        "result": "pass",
        "run_id": run_id,
        "started_at_utc": started_at,
        "completed_at_utc": completed_at,
        "manifest": manifest,
        "artifacts": artifacts,
        "coverage": {
            "proven_mode_count": len(summaries),
            "proven_policy_count": len({row["weapon_policy"] for row in summaries}),
            "live_repetitions": len(summaries) * REPEAT,
            "open": list(OPEN_COVERAGE),
        },
        "scenarios": summaries,
        "components": components,
        "gates": gates,
        "semantic_sha256": semantic_sha256(semantic),
    }


def execute(args: argparse.Namespace) -> dict[str, Any]:
    root = args.repo_root.resolve()
    if not root.is_dir():
        raise ValueError("repository root is missing")
    validate_execution_bounds(
        args.repeat, args.timeout, args.child_timeout, args.base_port
    )
    runner = resolve_repo_path(root, args.canonical_runner, "canonical runner")
    client_exe = resolve_repo_path(root, args.client_exe, "client executable")
    dedicated_exe = resolve_repo_path(root, args.dedicated_exe, "dedicated executable")
    working_dir = resolve_repo_path(root, args.working_dir, "working directory")
    output = require_networking_output(root, args.output)
    if not runner.is_file() or not client_exe.is_file() or not dedicated_exe.is_file():
        raise ValueError("canonical runner, client, or dedicated executable is missing")
    if not working_dir.is_dir():
        raise ValueError("working directory is missing")

    child = load_python_runner(runner)
    rows = validate_manifest(SCENARIO_MANIFEST, child, repeat=args.repeat)
    manifest = manifest_document(rows)
    artifact_paths = runtime_artifact_paths(
        runner, client_exe, dedicated_exe, working_dir
    )
    artifact_hashes = {
        name: file_sha256(path) for name, path in artifact_paths.items()
    }
    started = datetime.now(timezone.utc)
    run_id = started.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    parent_run = output.parent / f"{output.stem}.runs" / run_id
    modes_root = parent_run / "modes"
    modes_root.mkdir(parents=True, exist_ok=False)

    summaries: list[dict[str, Any]] = []
    components: list[dict[str, Any]] = []
    for index, row in enumerate(rows):
        mode = row["mode"]
        component_root = modes_root / f"{index + 1:02d}-{mode}"
        component_root.mkdir()
        child_output = component_root / "report.json"
        port = args.base_port + index
        command = build_child_command(
            runner, client_exe, dedicated_exe, working_dir, child_output,
            mode, port, args.timeout,
        )
        run_child_checked(command, root, args.child_timeout)
        report = load_json(child_output, f"{mode} child report")
        summary = validate_child_report(
            report, row, child, client_exe, dedicated_exe, component_root, port
        )
        summaries.append(summary)
        components.append({
            "mode": mode,
            "report": relative_name(root, child_output),
            "semantic_sha256": summary["semantic_sha256"],
        })

    final_artifact_hashes = {
        name: file_sha256(path) for name, path in artifact_paths.items()
    }
    if final_artifact_hashes != artifact_hashes:
        changed = sorted(
            name for name in artifact_hashes
            if final_artifact_hashes[name] != artifact_hashes[name]
        )
        raise RuntimeError(
            f"runtime artifacts changed while the live gate ran: {changed}"
        )

    artifacts = {
        "runtime": artifact_document(root, artifact_paths, artifact_hashes),
        "working_dir": relative_name(root, working_dir),
        "component_directory": relative_name(root, modes_root),
        "drift_check": "pre-and-post-identical",
        "runtime_home_scope": "fresh-per-mode-and-role; reused-across-repeats",
    }
    return build_evidence(
        run_id=run_id,
        started_at=started.isoformat(),
        completed_at=datetime.now(timezone.utc).isoformat(),
        manifest=manifest,
        summaries=summaries,
        components=components,
        artifacts=artifacts,
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    root = args.repo_root.resolve()
    try:
        output = require_networking_output(root, args.output)
    except (OSError, ValueError) as error:
        print(f"FR-10-T12 partial acceptance failed: {error}", file=sys.stderr)
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
            "status": ARTIFACT_STATUS,
            "result": "fail",
            "error_type": type(error).__name__,
            "error": str(error),
        })
        print(f"FR-10-T12 partial acceptance failed: {error}", file=sys.stderr)
        return 1
    print(
        "FR-10-T12 partial evidence passed: 39 manifest modes, 19 policies, "
        "and 117 deterministic live repetitions; task closure remains open"
    )
    print(f"evidence: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
