#!/usr/bin/env python3
"""Run the deterministic parent acceptance gate for FR-10-T11.

The parent owns no gameplay fixture.  It composes the accepted common-core
matrix, the production headless weapon runner, and the legacy-ack fallback
gate, then independently validates their stable semantics.  Child timestamps,
run identifiers, durations, and log paths are deliberately excluded from the
parent semantic digest so identical gameplay evidence produces identical JSON.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


MANIFEST_SCHEMA = "worr.networking.fr10-t11-acceptance-manifest.v1"
EVIDENCE_SCHEMA = "worr.networking.fr10-t11-acceptance-evidence.v1"
FAILURE_SCHEMA = EVIDENCE_SCHEMA + ".failure"
MAX_TRANSIENT_PROCESS_RETRIES = 2

PRODUCTION_POLICIES = (
    ("machinegun", "machinegun", 1, 8),
    ("chaingun", "chaingun", 2, 18),
    ("shotgun", "shotgun", 3, 48),
    ("super-shotgun", "super-shotgun", 4, 120),
    ("railgun", "railgun", 5, 80),
    ("disruptor", "disruptor", 6, 45),
    ("plasma-beam", "plasma-beam", 7, 8),
    ("thunderbolt", "thunderbolt", 8, 8),
)
FAIRNESS_MODES = {
    "railgun-mover-occlusion",
    "railgun-spectator-exclusion",
    "railgun-spawn-protection",
}
MATRIX_BOUNDARIES = (
    "stale",
    "future",
    "cap",
    "history_miss",
    "teleport",
    "death_respawn",
    "slot_reuse",
    "disabled",
)
CANONICAL_SEMANTIC_FIELDS = (
    "status",
    "armed",
    "players_ready",
    "history_ready",
    "canonical_scope",
    "attack_received",
    "weapon_callback",
    "canonical_historical_hit",
    "damage_applied",
    "current_geometry_unchanged",
    "target_history_captures",
    "failure_code",
    "eligible_candidates",
    "playing_candidates",
    "observation_path",
    "observation_outcome",
    "observation_fallback",
    "observation_flags",
    "observation_query",
    "capture_append_rejections",
    "observation_weapon_policy",
    "expected_damage",
    "observed_damage",
    "historical_mover_occlusion_required",
    "historical_mover_relocated",
    "historical_mover_baseline_clear",
    "historical_mover_occlusion_observed",
    "historical_mover_target_undamaged",
    "historical_mover_history_count",
)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=root)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("tools/networking/scenarios/fr10_t11_acceptance_manifest.json"),
    )
    parser.add_argument(
        "--rewind-runner",
        type=Path,
        default=Path("tools/networking/run_rewind_acceptance.py"),
    )
    parser.add_argument("--rewind-probe", type=Path, required=True)
    parser.add_argument(
        "--canonical-runner",
        type=Path,
        default=Path("tools/networking/run_canonical_rail_damage_runtime_gate.py"),
    )
    parser.add_argument(
        "--fallback-runner",
        type=Path,
        default=Path("tools/networking/run_rewind_rail_damage_runtime_gate.py"),
    )
    parser.add_argument("--client-exe", type=Path, required=True)
    parser.add_argument("--dedicated-exe", type=Path, required=True)
    parser.add_argument("--working-dir", type=Path, required=True)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(".tmp/networking/fr10_t11_acceptance.json"),
    )
    parser.add_argument("--platform-id", default=sys.platform)
    parser.add_argument("--build-type", default="unknown")
    parser.add_argument("--compiler-id", default="unknown")
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
        data.decode("utf-8"),
        object_pairs_hook=_object_no_duplicates,
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


def validate_manifest(manifest: dict[str, Any]) -> dict[str, Any]:
    if manifest.get("schema") != MANIFEST_SCHEMA or manifest.get("task") != "FR-10-T11":
        raise ValueError("FR-10-T11 manifest schema/task changed")
    repeat = require_int(manifest.get("repeat"), "manifest.repeat", minimum=1)
    bounds = require_dict(manifest.get("bounds"), "manifest.bounds")
    exact_bounds = {
        "matrix_cases": 40,
        "matrix_invocations": 40 * repeat,
        "canonical_modes": 11,
        "canonical_live_repetitions": 11 * repeat,
        "fallback_live_repetitions": repeat,
        "total_live_repetitions": 12 * repeat,
        "minimum_repeat": 3,
        "maximum_repeat": 3,
    }
    for field, expected in exact_bounds.items():
        if require_int(bounds.get(field), f"bounds.{field}") != expected:
            raise ValueError(f"bounds.{field} must equal {expected}")
    if repeat != 3:
        raise ValueError("the bounded acceptance contract requires exactly three repeats")
    per_repeat = require_int(
        bounds.get("per_repeat_timeout_seconds"),
        "bounds.per_repeat_timeout_seconds",
        minimum=1,
    )
    child_timeout = require_int(
        bounds.get("child_timeout_seconds"),
        "bounds.child_timeout_seconds",
        minimum=1,
    )
    if per_repeat > 60 or child_timeout > 240:
        raise ValueError("acceptance timeouts exceed their bounded maxima")

    matrix = require_dict(manifest.get("matrix"), "manifest.matrix")
    if matrix.get("path") != "tools/networking/scenarios/rewind_player_acceptance_matrix.json":
        raise ValueError("parent must consume the canonical rewind matrix")
    disabled = require_dict(
        matrix.get("disabled_opt_out_case"), "matrix.disabled_opt_out_case"
    )
    disabled_exact = {
        "case_id": "boundary/disabled",
        "policy_evaluated": False,
        "policy_accepted": False,
        "query_evaluated": False,
        "path": 3,
        "outcome": 4,
        "fallback_reason": 1,
        "authoritative_unchanged": True,
    }
    if disabled != disabled_exact:
        raise ValueError("disabled/opt-out evidence contract changed")

    canonical = require_dict(manifest.get("canonical"), "manifest.canonical")
    scenarios = require_list(canonical.get("scenarios"), "canonical.scenarios")
    if len(scenarios) != 11:
        raise ValueError("canonical manifest must contain exactly eleven live modes")
    policy_rows = [row for row in scenarios if isinstance(row, dict) and row.get("kind") == "production-policy"]
    observed_policies = tuple(
        (
            row.get("name"), row.get("mode"), row.get("weapon_policy"),
            row.get("observed_damage"),
        )
        for row in policy_rows
    )
    if observed_policies != PRODUCTION_POLICIES:
        raise ValueError("the eight supported production policy rows changed")
    fairness_rows = [row for row in scenarios if isinstance(row, dict) and row.get("kind") == "fairness"]
    if {row.get("mode") for row in fairness_rows} != FAIRNESS_MODES or len(fairness_rows) != 3:
        raise ValueError("the mover/spectator/spawn fairness mode set changed")
    modes = [row.get("mode") for row in scenarios if isinstance(row, dict)]
    if len(modes) != len(set(modes)):
        raise ValueError("canonical mode names must be unique")
    spectator = next(row for row in fairness_rows if row.get("mode") == "railgun-spectator-exclusion")
    spawn = next(row for row in fairness_rows if row.get("mode") == "railgun-spawn-protection")
    if spectator.get("client_count") != 3 or spectator.get("require_spectator_exclusion") is not True:
        raise ValueError("spectator exclusion must use a three-client live roster")
    if spawn.get("observed_damage") != 0 or spawn.get("require_spawn_protection") is not True:
        raise ValueError("spawn protection must require exact zero damage")

    abuse = require_dict(manifest.get("abuse"), "manifest.abuse")
    if abuse.get("matrix_cases") != [
        "boundary/stale", "boundary/future", "boundary/history_miss",
        "boundary/death_respawn", "boundary/slot_reuse", "boundary/disabled",
    ]:
        raise ValueError("bounded matrix abuse cases changed")
    if abuse.get("live_cases") != [
        "invalid-current-ack-no-damage", "spectator-exclusion", "spawn-protection",
    ]:
        raise ValueError("bounded live abuse cases changed")
    if manifest.get("output_root") != ".tmp/networking":
        raise ValueError("manifest output root must be .tmp/networking")
    return manifest


def validate_matrix_source(matrix: dict[str, Any], manifest: dict[str, Any]) -> None:
    matrix_contract = require_dict(manifest["matrix"], "manifest.matrix")
    if matrix.get("schema") != matrix_contract.get("schema"):
        raise ValueError("rewind matrix schema changed")
    weapons = require_list(matrix.get("weapon_policies"), "matrix.weapon_policies")
    expected_names = [
        "machinegun", "chaingun", "shotgun", "super-shotgun", "railgun",
        "disruptor-convergence", "plasma-beam", "thunderbolt",
    ]
    if [row.get("id") for row in weapons if isinstance(row, dict)] != list(range(1, 9)) or \
            [row.get("name") for row in weapons if isinstance(row, dict)] != expected_names:
        raise ValueError("rewind matrix production policy registry changed")
    if matrix.get("normal_latency_ms") != [0, 50, 100, 200]:
        raise ValueError("rewind matrix latency classes changed")
    boundaries = require_list(matrix.get("boundary_scenarios"), "matrix.boundary_scenarios")
    if tuple(row.get("name") for row in boundaries if isinstance(row, dict)) != MATRIX_BOUNDARIES:
        raise ValueError("rewind matrix boundaries changed")
    disabled = next(row for row in boundaries if row.get("name") == "disabled")
    expected = manifest["matrix"]["disabled_opt_out_case"]
    if disabled.get("expect") != {
        key: expected[key]
        for key in ("policy_evaluated", "policy_accepted", "query_evaluated", "path", "outcome", "fallback_reason")
    }:
        raise ValueError("matrix disabled/opt-out semantics changed")


def validate_required_source_tokens(root: Path, manifest: dict[str, Any]) -> None:
    rows = require_list(
        manifest.get("required_source_tokens"), "manifest.required_source_tokens"
    )
    for index, row_value in enumerate(rows):
        row = require_dict(row_value, f"required_source_tokens[{index}]")
        path = resolve_repo_path(root, Path(str(row.get("path"))), "source token path")
        text = path.read_text(encoding="utf-8")
        tokens = require_list(row.get("tokens"), f"required_source_tokens[{index}].tokens")
        missing = [token for token in tokens if not isinstance(token, str) or token not in text]
        if missing:
            raise RuntimeError(
                f"required live fixture is unavailable for {row.get('mode')}: missing={missing}"
            )


def command_sets(command: Any, label: str) -> dict[str, str]:
    if not isinstance(command, list) or not command or not all(isinstance(item, str) for item in command):
        raise ValueError(f"{label} must be a non-empty string command array")
    result: dict[str, str] = {}
    index = 1
    while index < len(command):
        if command[index] == "+set":
            if index + 2 >= len(command):
                raise ValueError(f"{label} has a truncated +set")
            name, value = command[index + 1], command[index + 2]
            previous = result.get(name)
            if previous is not None and previous != value:
                raise ValueError(f"{label} changes cvar {name} within one launch")
            result[name] = value
            index += 3
        else:
            index += 1
    return result


def validate_headless_commands(
    report: dict[str, Any], scenario: dict[str, Any],
    client_exe: Path, dedicated_exe: Path,
) -> None:
    dedicated = report.get("dedicated_command")
    if not isinstance(dedicated, list) or not dedicated or Path(dedicated[0]).resolve() != dedicated_exe:
        raise ValueError("canonical report did not launch the required dedicated binary")
    dedicated_sets = command_sets(dedicated, "dedicated command")
    if dedicated_sets.get("maxclients") != str(scenario["client_count"]) or \
            dedicated_sets.get("g_lag_compensation") != "1":
        raise ValueError("canonical dedicated launch policy changed")
    if "+addbot" in dedicated:
        raise ValueError("canonical live gate substituted bots for real clients")

    commands = [report.get("shooter_command"), report.get("target_command")]
    spectator_command = report.get("spectator_command")
    if scenario["client_count"] == 3:
        commands.append(spectator_command)
    elif spectator_command is not None:
        raise ValueError("two-client scenario unexpectedly launched a spectator")
    if report.get("client_count") != scenario["client_count"]:
        raise ValueError("canonical report client count changed")
    exact = {
        "loc_language": "english",
        "win_headless": "1",
        "cl_headless": "1",
        "in_enable": "0",
        "in_grab": "0",
        "s_enable": "0",
    }
    qports: list[int] = []
    for index, command in enumerate(commands):
        if not isinstance(command, list) or not command or Path(command[0]).resolve() != client_exe:
            raise ValueError(f"canonical client {index} executable changed")
        sets = command_sets(command, f"canonical client {index}")
        for name, expected in exact.items():
            if sets.get(name) != expected:
                raise ValueError(f"canonical client {index} violates headless {name}={expected}")
        qport_positions = [
            offset for offset in range(1, len(command) - 2)
            if command[offset:offset + 2] == ["+set", "qport"]
        ]
        if len(qport_positions) != 1:
            raise ValueError(f"canonical client {index} must set qport exactly once")
        if "+connect" not in command or qport_positions[0] > command.index("+connect"):
            raise ValueError(f"canonical client {index} must set qport before +connect")
        qport_text = command[qport_positions[0] + 2]
        if not qport_text.isascii() or not qport_text.isdecimal():
            raise ValueError(f"canonical client {index} qport must be decimal")
        qport = int(qport_text)
        if not 0 < qport <= 0xff:
            raise ValueError(f"canonical client {index} qport must be a non-zero byte")
        qports.append(qport)
    if len(set(qports)) != len(qports):
        raise ValueError("canonical clients must use distinct qports")


def canonical_projection(status: dict[str, Any]) -> dict[str, Any]:
    missing = [name for name in CANONICAL_SEMANTIC_FIELDS if name not in status]
    if missing:
        raise ValueError(f"canonical status lacks semantic fields: {missing}")
    return {name: status[name] for name in CANONICAL_SEMANTIC_FIELDS}


def validate_canonical_status(
    status: dict[str, Any], scenario: dict[str, Any], manifest: dict[str, Any]
) -> None:
    canonical = require_dict(manifest["canonical"], "manifest.canonical")
    exact = require_dict(canonical.get("common_status"), "canonical.common_status")
    for field, expected in exact.items():
        if status.get(field) != expected:
            raise ValueError(
                f"{scenario['mode']}: status.{field} expected {expected!r}, got {status.get(field)!r}"
            )
    for field, minimum in require_dict(
        canonical.get("minimum_status"), "canonical.minimum_status"
    ).items():
        if require_int(status.get(field), f"{scenario['mode']}.{field}") < require_int(minimum, f"minimum_status.{field}"):
            raise ValueError(f"{scenario['mode']}: status.{field} is below its minimum")
    if status.get("observation_weapon_policy") != scenario["weapon_policy"]:
        raise ValueError(f"{scenario['mode']}: weapon policy is not exact")
    if status.get("expected_damage") != scenario["fixture_expected_damage"] or \
            status.get("observed_damage") != scenario["observed_damage"]:
        raise ValueError(f"{scenario['mode']}: exact damage contract failed")
    expected_damage_applied = (
        0 if scenario["mode"] == "railgun-mover-occlusion" else 1
    )
    if status.get("damage_applied") != expected_damage_applied:
        raise ValueError(f"{scenario['mode']}: damage-applied flag is not exact")
    for field in scenario.get("required_true", []):
        if status.get(field) != 1:
            raise ValueError(f"{scenario['mode']}: required proof {field} is absent")
    for field, minimum in scenario.get("minimums", {}).items():
        if require_int(status.get(field), f"{scenario['mode']}.{field}") < require_int(minimum, f"{scenario['mode']}.minimum.{field}"):
            raise ValueError(f"{scenario['mode']}: {field} is below its minimum")


def validate_canonical_report(
    report: dict[str, Any], scenario: dict[str, Any], manifest: dict[str, Any],
    client_exe: Path, dedicated_exe: Path,
) -> dict[str, Any]:
    canonical = require_dict(manifest["canonical"], "manifest.canonical")
    repeat = require_int(manifest["repeat"], "manifest.repeat")
    if report.get("schema") != canonical.get("schema") or report.get("weapon") != scenario["mode"]:
        raise ValueError(f"{scenario['mode']}: canonical report schema/mode changed")
    if report.get("weapon_policy") != scenario["weapon_policy"] or \
            report.get("expected_damage") != scenario["fixture_expected_damage"]:
        raise ValueError(f"{scenario['mode']}: canonical report policy/damage changed")
    if report.get("repeat") != repeat:
        raise ValueError(f"{scenario['mode']}: canonical repeat count changed")
    validate_headless_commands(report, scenario, client_exe, dedicated_exe)
    runs = require_list(report.get("runs"), f"{scenario['mode']}.runs")
    if len(runs) != repeat:
        raise ValueError(f"{scenario['mode']}: canonical run count changed")
    projections: list[dict[str, Any]] = []
    for index, run_value in enumerate(runs):
        run = require_dict(run_value, f"{scenario['mode']}.runs[{index}]")
        status = require_dict(run.get("status"), f"{scenario['mode']}.runs[{index}].status")
        validate_canonical_status(status, scenario, manifest)
        projections.append(canonical_projection(status))
        spectator = require_dict(
            run.get("spectator_exclusion"),
            f"{scenario['mode']}.runs[{index}].spectator_exclusion",
        )
        if scenario.get("require_spectator_exclusion") is True:
            required_spectator = {
                "required": True,
                "roster_size": 3,
                "team_verified_before_fire": True,
                "team_verified_after_fire": True,
                "spectator_undamaged": True,
            }
            for field, expected in required_spectator.items():
                if spectator.get(field) != expected:
                    raise ValueError(f"spectator exclusion proof failed: {field}")
        elif spectator.get("required") is not False:
            raise ValueError(f"{scenario['mode']}: unexpected spectator proof")
    if any(item != projections[0] for item in projections[1:]):
        raise ValueError(f"{scenario['mode']}: semantic repeat output diverged")
    top_status = require_dict(report.get("status"), f"{scenario['mode']}.status")
    if canonical_projection(top_status) != projections[0]:
        raise ValueError(f"{scenario['mode']}: top-level status differs from repeats")
    return {
        "name": scenario["name"],
        "mode": scenario["mode"],
        "kind": scenario["kind"],
        "weapon_policy": scenario["weapon_policy"],
        "expected_damage": scenario["fixture_expected_damage"],
        "observed_damage": scenario["observed_damage"],
        "client_count": scenario["client_count"],
        "repeat": repeat,
        "semantic_sha256": semantic_sha256(projections[0]),
    }


def validate_rewind_evidence(
    evidence: dict[str, Any], raw: dict[str, Any], matrix_path: Path,
    manifest: dict[str, Any],
) -> dict[str, Any]:
    contract = require_dict(manifest["matrix"], "manifest.matrix")
    repeat = require_int(manifest["repeat"], "manifest.repeat")
    if evidence.get("schema") != contract.get("evidence_schema") or \
            evidence.get("overall_result") != "pass":
        raise ValueError("rewind matrix evidence did not pass")
    workload = require_dict(evidence.get("workload"), "rewind.workload")
    if workload.get("case_count") != 40 or workload.get("weapon_policy_count") != 8 or workload.get("repeat") != repeat:
        raise ValueError("rewind matrix workload changed")
    measurements = require_dict(evidence.get("measurements"), "rewind.measurements")
    if measurements.get("invocations") != 40 * repeat or \
            measurements.get("determinism_mismatches") != 0 or \
            measurements.get("authoritative_mutations") != 0 or \
            measurements.get("failed_assertions") != 0:
        raise ValueError("rewind matrix determinism/authority gate failed")
    gates = require_dict(evidence.get("gates"), "rewind.gates")
    for name in (
        "matrix_complete", "production_weapon_route_tags_present",
        "repeat_determinism", "authoritative_state_immutable", "expected_outcomes",
    ):
        if gates.get(name) is not True:
            raise ValueError(f"rewind matrix gate failed: {name}")
    scenario_manifest = require_dict(
        evidence.get("scenario_manifest"), "rewind.scenario_manifest"
    )
    if scenario_manifest.get("sha256") != sha256_bytes(matrix_path.read_bytes()):
        raise ValueError("rewind evidence does not bind the checked-in matrix")

    if raw.get("schema") != contract.get("raw_schema") or raw.get("repeat") != repeat:
        raise ValueError("rewind raw evidence schema/repeat changed")
    cases = require_list(raw.get("cases"), "rewind.raw.cases")
    if len(cases) != 40:
        raise ValueError("rewind raw evidence must contain exactly 40 cases")
    case_ids: set[str] = set()
    projections: list[dict[str, Any]] = []
    disabled_report: dict[str, Any] | None = None
    for index, case_value in enumerate(cases):
        case = require_dict(case_value, f"rewind.raw.cases[{index}]")
        case_id = case.get("case_id")
        if not isinstance(case_id, str) or case_id in case_ids:
            raise ValueError("rewind raw case identifiers must be unique strings")
        case_ids.add(case_id)
        digests = require_list(case.get("repeat_digests"), f"{case_id}.repeat_digests")
        if case.get("deterministic") is not True or len(digests) != repeat or \
                len(set(digests)) != 1:
            raise ValueError(f"{case_id}: rewind semantic repeats diverged")
        report = require_dict(case.get("report"), f"{case_id}.report")
        if report.get("pass") is not True or report.get("authoritative_unchanged") is not True:
            raise ValueError(f"{case_id}: rewind authority proof failed")
        projections.append({"case_id": case_id, "report": report})
        if case_id == contract["disabled_opt_out_case"]["case_id"]:
            disabled_report = report
    required_abuse = set(require_dict(manifest["abuse"], "manifest.abuse")["matrix_cases"])
    if not required_abuse.issubset(case_ids):
        raise ValueError("rewind matrix abuse coverage is incomplete")
    if disabled_report is None:
        raise ValueError("rewind disabled/opt-out evidence is missing")
    for field, expected in contract["disabled_opt_out_case"].items():
        if field != "case_id" and disabled_report.get(field) != expected:
            raise ValueError(f"disabled/opt-out field is not exact: {field}")
    return {
        "case_count": 40,
        "repeat": repeat,
        "invocations": 40 * repeat,
        "weapon_policy_count": 8,
        "authoritative_mutations": 0,
        "determinism_mismatches": 0,
        "disabled_opt_out": {
            field: disabled_report[field]
            for field in contract["disabled_opt_out_case"]
            if field != "case_id"
        } | {"case_id": contract["disabled_opt_out_case"]["case_id"]},
        "semantic_sha256": semantic_sha256(projections),
    }


def validate_fallback_report(
    report: dict[str, Any], manifest: dict[str, Any], dedicated_exe: Path
) -> dict[str, Any]:
    contract = require_dict(manifest["fallback"], "manifest.fallback")
    repeat = require_int(manifest["repeat"], "manifest.repeat")
    if report.get("schema") != contract.get("schema") or report.get("repeat") != repeat:
        raise ValueError("fallback report schema/repeat changed")
    command = report.get("command")
    if not isinstance(command, list) or not command or Path(command[0]).resolve() != dedicated_exe:
        raise ValueError("fallback gate did not use the dedicated binary")
    if "+connect" in command or "win_headless" in command:
        raise ValueError("fallback gate launched an interactive/client path")
    runs = require_list(report.get("runs"), "fallback.runs")
    if len(runs) != repeat:
        raise ValueError("fallback run count changed")
    statuses: list[dict[str, Any]] = []
    for index, run_value in enumerate(runs):
        run = require_dict(run_value, f"fallback.runs[{index}]")
        status = require_dict(run.get("status"), f"fallback.runs[{index}].status")
        statuses.append(status)
    if any(status != statuses[0] for status in statuses[1:]):
        raise ValueError("fallback semantic repeat output diverged")
    status = statuses[0]
    if report.get("status") != status or status.get("status") != "pass":
        raise ValueError("fallback top-level status changed")
    for field in require_list(contract.get("required_true"), "fallback.required_true"):
        if status.get(field) != 1:
            raise ValueError(f"fallback proof failed: {field}")
    if status.get("damage_amount") != contract.get("damage_amount") or \
            require_int(status.get("candidate_count"), "fallback.candidate_count", minimum=1) < 1 or \
            status.get("failure_code") != 0 or status.get("current_fraction_q6") != 1_000_000:
        raise ValueError("fallback exact damage/candidate/current-world contract failed")
    for field in (
        "near_latency_fraction_q6", "bounded_latency_fraction_q6",
        "capped_latency_fraction_q6",
    ):
        fraction = require_int(status.get(field), f"fallback.{field}")
        if not 0 < fraction < 1_000_000:
            raise ValueError(f"fallback historical hit fraction failed: {field}")
    return {
        "repeat": repeat,
        "invalid_ack_current_fallback": True,
        "invalid_ack_damage": 0,
        "near_hit": True,
        "in_budget_hit": True,
        "capped_hit": True,
        "damage_amount": status["damage_amount"],
        "geometry_unchanged": True,
        "query_authority_unchanged": True,
        "semantic_sha256": semantic_sha256(status),
    }


def run_checked(command: list[str], cwd: Path, timeout: int) -> int:
    """Run one bounded child and return discarded pre-evidence launch retries.

    Windows can report 0xffffffff when a just-cleaned process tree prevents the
    next Python child from entering its main function.  Such a child emits no
    stdout/stderr and cannot have published an accepted report.  Retry only
    that exact pre-evidence termination, keep the retry count bounded, and
    expose it in the parent evidence.  Every gameplay report still contains
    exactly the manifest's three fresh repetitions.
    """
    retries = 0
    while True:
        completed = subprocess.run(
            command,
            cwd=cwd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if completed.returncode == 0:
            if completed.stderr:
                raise RuntimeError(
                    f"{Path(command[1] if command[0] == sys.executable else command[0]).name} emitted stderr"
                )
            return retries
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        transient_windows_termination = (
            os.name == "nt"
            and completed.returncode in (-1, 0xffffffff)
            and not completed.stdout
            and not detail
        )
        if (transient_windows_termination and
                retries < MAX_TRANSIENT_PROCESS_RETRIES):
            retries += 1
            time.sleep(0.1)
            continue
        raise RuntimeError(
            f"{Path(command[1] if command[0] == sys.executable else command[0]).name} "
            f"failed with {completed.returncode}: {detail}"
        )


def load_python_runner(path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(
        f"fr10_t11_runner_{sha256_bytes(str(path).encode())[:12]}", path
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load Python runner: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, "main", None)):
        raise RuntimeError(f"Python runner has no callable main: {path}")
    return module


def run_python_main_checked(
    module: Any, command: list[str], timeout: int
) -> None:
    """Invoke a runner in-process while preserving its bounded CLI contract.

    Repeated nested Python processes can inherit the desktop host's Windows job
    hierarchy; closing a game child's kill-on-close job has then terminated the
    Python orchestrator itself.  Keeping one Python orchestrator removes that
    ambiguous job layer.  The canonical runner still launches every client and
    dedicated server through the same headless kill-on-close helper.
    """
    if len(command) < 2 or Path(command[0]).resolve() != Path(sys.executable).resolve():
        raise RuntimeError("in-process runner command must use this Python executable")
    stdout = io.StringIO()
    stderr = io.StringIO()
    started = time.monotonic()
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        result = module.main(command[2:])
    elapsed = time.monotonic() - started
    if elapsed > timeout:
        raise RuntimeError(
            f"{Path(command[1]).name} exceeded its bounded timeout: "
            f"{elapsed:.3f}s > {timeout}s"
        )
    detail = stderr.getvalue().strip()
    if result != 0:
        raise RuntimeError(f"{Path(command[1]).name} failed with {result}: {detail}")
    if detail:
        raise RuntimeError(f"{Path(command[1]).name} emitted stderr")


def build_rewind_command(
    runner: Path, probe: Path, matrix: Path, output: Path,
    repeat: int, args: argparse.Namespace,
) -> list[str]:
    return [
        sys.executable, str(runner),
        "--probe-exe", str(probe),
        "--matrix", str(matrix),
        "--output", str(output),
        "--repeat", str(repeat),
        "--platform-id", args.platform_id,
        "--build-type", args.build_type,
        "--compiler-id", args.compiler_id,
    ]


def build_canonical_command(
    runner: Path, client: Path, dedicated: Path, working_dir: Path,
    output: Path, scenario: dict[str, Any], repeat: int,
    timeout: int, port: int,
) -> list[str]:
    return [
        sys.executable, str(runner),
        "--client-exe", str(client),
        "--dedicated-exe", str(dedicated),
        "--working-dir", str(working_dir),
        "--output", str(output),
        "--port", str(port),
        "--repeat", str(repeat),
        "--timeout", str(timeout),
        "--weapon", str(scenario["mode"]),
    ]


def build_fallback_command(
    runner: Path, dedicated: Path, working_dir: Path,
    output: Path, repeat: int, timeout: int,
) -> list[str]:
    return [
        sys.executable, str(runner),
        "--dedicated-exe", str(dedicated),
        "--working-dir", str(working_dir),
        "--output", str(output),
        "--repeat", str(repeat),
        "--timeout", str(timeout),
    ]


def execute(args: argparse.Namespace) -> dict[str, Any]:
    root = args.repo_root.resolve()
    manifest_path = resolve_repo_path(root, args.manifest, "manifest")
    manifest = validate_manifest(load_json(manifest_path, "FR-10-T11 manifest"))
    matrix_path = resolve_repo_path(root, Path(manifest["matrix"]["path"]), "matrix")
    matrix = load_json(matrix_path, "rewind matrix")
    validate_matrix_source(matrix, manifest)
    validate_required_source_tokens(root, manifest)

    rewind_runner = resolve_repo_path(root, args.rewind_runner, "rewind runner")
    canonical_runner = resolve_repo_path(root, args.canonical_runner, "canonical runner")
    fallback_runner = resolve_repo_path(root, args.fallback_runner, "fallback runner")
    rewind_probe = resolve_repo_path(root, args.rewind_probe, "rewind probe")
    client_exe = resolve_repo_path(root, args.client_exe, "client executable")
    dedicated_exe = resolve_repo_path(root, args.dedicated_exe, "dedicated executable")
    working_dir = resolve_repo_path(root, args.working_dir, "working directory")
    for path, label in (
        (rewind_runner, "rewind runner"), (canonical_runner, "canonical runner"),
        (fallback_runner, "fallback runner"), (rewind_probe, "rewind probe"),
        (client_exe, "client executable"), (dedicated_exe, "dedicated executable"),
    ):
        if not path.is_file():
            raise ValueError(f"{label} is missing: {path}")
    if not working_dir.is_dir():
        raise ValueError(f"working directory is missing: {working_dir}")

    sgame_module = working_dir / "basew" / "sgame_x86_64.dll"
    staged_assets = working_dir / "basew" / "pak0.pkz"
    for path, label in (
        (sgame_module, "staged sgame module"),
        (staged_assets, "staged asset package"),
    ):
        if not path.is_file():
            raise ValueError(f"{label} is missing: {path}")
    artifact_paths = {
        "manifest": manifest_path,
        "rewind_matrix": matrix_path,
        "rewind_runner": rewind_runner,
        "rewind_probe": rewind_probe,
        "canonical_runner": canonical_runner,
        "fallback_runner": fallback_runner,
        "client_executable": client_exe,
        "dedicated_executable": dedicated_exe,
        "sgame_module": sgame_module,
        "staged_assets": staged_assets,
    }
    artifact_hashes = {
        name: file_sha256(path) for name, path in artifact_paths.items()
    }

    output = require_networking_output(root, args.output)
    components = output.parent / "fr10_t11_components"
    components.mkdir(parents=True, exist_ok=True)
    repeat = require_int(manifest["repeat"], "manifest.repeat")
    bounds = manifest["bounds"]
    per_repeat_timeout = bounds["per_repeat_timeout_seconds"]
    child_timeout = bounds["child_timeout_seconds"]
    scenarios = manifest["canonical"]["scenarios"]
    if not 1 <= args.base_port <= 65535 - len(scenarios):
        raise ValueError("base port cannot provide the bounded canonical mode range")

    rewind_output = components / "rewind-matrix.json"
    orchestration_retries: dict[str, int] = {}
    orchestration_retries["rewind_matrix"] = run_checked(
        build_rewind_command(
            rewind_runner, rewind_probe, matrix_path, rewind_output, repeat, args
        ),
        root,
        child_timeout,
    )
    rewind_raw = rewind_output.with_name("rewind-acceptance-raw.json")
    rewind_summary = validate_rewind_evidence(
        load_json(rewind_output, "rewind evidence"),
        load_json(rewind_raw, "rewind raw evidence"),
        matrix_path,
        manifest,
    )

    canonical_summaries: list[dict[str, Any]] = []
    canonical_module = load_python_runner(canonical_runner)
    for index, scenario in enumerate(scenarios):
        canonical_output = components / f"canonical-{scenario['mode']}.json"
        run_python_main_checked(
            canonical_module,
            build_canonical_command(
                canonical_runner, client_exe, dedicated_exe, working_dir,
                canonical_output, scenario, repeat, per_repeat_timeout,
                args.base_port + index,
            ),
            child_timeout,
        )
        orchestration_retries[f"canonical:{scenario['mode']}"] = 0
        canonical_summaries.append(
            validate_canonical_report(
                load_json(canonical_output, f"canonical {scenario['mode']}"),
                scenario,
                manifest,
                client_exe,
                dedicated_exe,
            )
        )

    fallback_output = components / "rail-fallback.json"
    orchestration_retries["fallback"] = run_checked(
        build_fallback_command(
            fallback_runner, dedicated_exe, working_dir, fallback_output,
            repeat, per_repeat_timeout,
        ),
        root,
        child_timeout,
    )
    fallback_summary = validate_fallback_report(
        load_json(fallback_output, "rail fallback evidence"),
        manifest,
        dedicated_exe,
    )

    final_artifact_hashes = {
        name: file_sha256(path) for name, path in artifact_paths.items()
    }
    if final_artifact_hashes != artifact_hashes:
        changed = sorted(
            name for name in artifact_hashes
            if final_artifact_hashes[name] != artifact_hashes[name]
        )
        raise RuntimeError(
            f"acceptance artifacts changed while the live gate ran: {changed}"
        )

    semantic = {
        "manifest_seed": manifest["seed"],
        "rewind_matrix": rewind_summary,
        "canonical": canonical_summaries,
        "fallback": fallback_summary,
    }
    evidence = {
        "schema": EVIDENCE_SCHEMA,
        "task": "FR-10-T11",
        "result": "pass",
        "platform": {
            "system": platform.system(),
            "machine": platform.machine(),
            "platform_id": args.platform_id,
            "build_type": args.build_type,
            "compiler_id": args.compiler_id,
        },
        "manifest": {
            "path": relative_name(root, manifest_path),
            "schema": manifest["schema"],
            "seed": manifest["seed"],
            "sha256": sha256_bytes(manifest_path.read_bytes()),
        },
        "artifacts": {
            name: {
                "path": relative_name(root, artifact_paths[name]),
                "sha256": artifact_hashes[name],
            }
            for name in sorted(artifact_paths)
        },
        "bounds": {
            "repeat": repeat,
            "matrix_cases": 40,
            "matrix_invocations": 40 * repeat,
            "canonical_modes": len(canonical_summaries),
            "canonical_live_repetitions": len(canonical_summaries) * repeat,
            "fallback_live_repetitions": repeat,
            "total_live_repetitions": (len(canonical_summaries) + 1) * repeat,
            "per_repeat_timeout_seconds": per_repeat_timeout,
            "bounded": True,
        },
        "orchestration": {
            "maximum_transient_process_retries_per_component":
                MAX_TRANSIENT_PROCESS_RETRIES,
            "transient_process_retries": orchestration_retries,
            "total_transient_process_retries":
                sum(orchestration_retries.values()),
        },
        "coverage": {
            "production_policies": [row[0] for row in PRODUCTION_POLICIES],
            "fairness_modes": sorted(FAIRNESS_MODES),
            "abuse_matrix_cases": manifest["abuse"]["matrix_cases"],
            "abuse_live_cases": manifest["abuse"]["live_cases"],
            "disabled_opt_out": rewind_summary["disabled_opt_out"],
        },
        "components": {
            "rewind_matrix": rewind_summary,
            "canonical": canonical_summaries,
            "fallback": fallback_summary,
            "directory": relative_name(root, components),
        },
        "gates": {
            "matrix_40_cases": True,
            "all_eight_production_policies": True,
            "historical_mover_occlusion": True,
            "invalid_ack_and_latency_fallback": True,
            "disabled_opt_out": True,
            "spectator_exclusion": True,
            "spawn_protection": True,
            "semantic_repeat_determinism": True,
            "exact_damage_and_fallback": True,
            "authoritative_state_immutable": True,
            "headless_input_free": True,
            "bounded_abuse_and_repeats": True,
        },
        "semantic_sha256": semantic_sha256(semantic),
    }
    return evidence


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = args.repo_root.resolve()
    try:
        output = require_networking_output(root, args.output)
    except (OSError, ValueError) as error:
        print(f"FR-10-T11 acceptance failed: {error}", file=sys.stderr)
        return 1
    output.unlink(missing_ok=True)
    failure_output = output.with_suffix(".failure.json")
    failure_output.unlink(missing_ok=True)
    try:
        evidence = execute(args)
        write_json_atomic(output, evidence)
    except (
        OSError,
        RuntimeError,
        ValueError,
        json.JSONDecodeError,
        subprocess.TimeoutExpired,
    ) as error:
        failure = {
            "schema": FAILURE_SCHEMA,
            "task": "FR-10-T11",
            "result": "fail",
            "error_type": type(error).__name__,
            "error": str(error),
        }
        write_json_atomic(failure_output, failure)
        print(f"FR-10-T11 acceptance failed: {error}", file=sys.stderr)
        return 1
    print(
        "FR-10-T11 acceptance passed: 40 matrix cases, 8 production policies, "
        "3 fairness modes, and 3 exact fallback hits"
    )
    print(f"evidence: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
