#!/usr/bin/env python3
"""Report whether RmlUi routes have advanced beyond the starter phase."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_manifest.json")
MIGRATION_PHASES = (
    "starter",
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)
ADVANCED_PHASES = (
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)
NON_RUNTIME_ADVANCED_PHASES = (
    "controller_stub",
    "parity_pending",
    "parity_ready",
)


@dataclass
class CompletionReport:
    manifest_path: Path
    repo_root: Path
    require_complete_controller_stubs: bool = False
    total_routes: int = 0
    phase_counts: Counter[str] = field(default_factory=Counter)
    routes_by_phase: dict[str, list[str]] = field(
        default_factory=lambda: {phase: [] for phase in MIGRATION_PHASES}
    )
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors

    @property
    def starter_route_ids(self) -> list[str]:
        return sorted(self.routes_by_phase.get("starter", []))

    @property
    def starter_routes(self) -> int:
        return self.phase_counts["starter"]

    @property
    def controller_stub_routes(self) -> int:
        return self.phase_counts["controller_stub"]

    @property
    def runtime_stub_routes(self) -> int:
        return self.phase_counts["runtime_stub"]

    @property
    def parity_pending_routes(self) -> int:
        return self.phase_counts["parity_pending"]

    @property
    def parity_ready_routes(self) -> int:
        return self.phase_counts["parity_ready"]

    @property
    def advanced_routes(self) -> int:
        return sum(self.phase_counts[phase] for phase in ADVANCED_PHASES)

    @property
    def non_runtime_routes(self) -> int:
        return self.total_routes - self.runtime_stub_routes

    @property
    def non_runtime_advanced_routes(self) -> int:
        return sum(self.phase_counts[phase] for phase in NON_RUNTIME_ADVANCED_PHASES)


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_input_path(repo_root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"route {route_id!r}"
    return f"route at index {index}"


def route_progress_id(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return route_id
    return f"<index:{index}>"


def percent(part: int, total: int) -> float:
    if total == 0:
        return 100.0
    return round((part / total) * 100.0, 1)


def ordered_phase_counts(counter: Counter[str]) -> dict[str, int]:
    payload = {phase: counter[phase] for phase in MIGRATION_PHASES}
    for phase in sorted(counter):
        if phase not in payload:
            payload[phase] = counter[phase]
    return payload


def ordered_routes_by_phase(routes_by_phase: dict[str, list[str]]) -> list[dict[str, Any]]:
    phases = list(MIGRATION_PHASES)
    phases.extend(sorted(phase for phase in routes_by_phase if phase not in MIGRATION_PHASES))
    return [
        {
            "phase": phase,
            "route_ids": sorted(routes_by_phase.get(phase, [])),
        }
        for phase in phases
    ]


def audit_controller_stub_completion(
    manifest_data: dict[str, Any],
    repo_root: Path,
    manifest_path: Path,
    *,
    require_complete_controller_stubs: bool = False,
) -> CompletionReport:
    report = CompletionReport(
        manifest_path=manifest_path.resolve(),
        repo_root=repo_root.resolve(),
        require_complete_controller_stubs=require_complete_controller_stubs,
    )

    routes = manifest_data.get("routes")
    if not isinstance(routes, list):
        report.errors.append("manifest field 'routes' must be a list")
        return report

    report.total_routes = len(routes)
    seen_route_ids: set[str] = set()
    duplicate_route_ids: set[str] = set()

    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            report.errors.append(f"manifest route at index {index} must be an object")
            continue

        label = route_label(route, index)
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            report.errors.append(f"{label} field 'id' must be a non-empty string")
        elif route_id in seen_route_ids:
            duplicate_route_ids.add(route_id)
        else:
            seen_route_ids.add(route_id)

        migration_phase = route.get("migration_phase")
        if not isinstance(migration_phase, str) or not migration_phase:
            report.errors.append(f"{label} field 'migration_phase' must be a non-empty string")
            migration_phase = "<missing>"
        elif migration_phase not in MIGRATION_PHASES:
            allowed = ", ".join(MIGRATION_PHASES)
            report.errors.append(
                f"{label} field 'migration_phase' must be one of {allowed}; "
                f"got {migration_phase!r}"
            )

        report.phase_counts[migration_phase] += 1
        report.routes_by_phase.setdefault(migration_phase, []).append(
            route_progress_id(route, index)
        )

    for route_id in sorted(duplicate_route_ids):
        report.errors.append(f"duplicate manifest route id {route_id!r}")

    if require_complete_controller_stubs and report.starter_route_ids:
        report.errors.append(
            "starter routes remain after controller-stub completion is required: "
            + ", ".join(report.starter_route_ids)
        )

    return report


def format_route_ids(route_ids: list[str]) -> str:
    if not route_ids:
        return "none"
    return ", ".join(route_ids)


def format_count_line(report: CompletionReport) -> str:
    counts = ordered_phase_counts(report.phase_counts)
    return ", ".join(f"{phase}={count}" for phase, count in counts.items())


def format_json_report(report: CompletionReport) -> str:
    payload = {
        "ok": report.ok(),
        "strict": report.require_complete_controller_stubs,
        "manifest_path": display_path(report.manifest_path, report.repo_root),
        "total_routes": report.total_routes,
        "phase_counts": ordered_phase_counts(report.phase_counts),
        "starter_routes": {
            "count": report.starter_routes,
            "route_ids": report.starter_route_ids,
        },
        "controller_stub_routes": report.controller_stub_routes,
        "runtime_stub_routes": report.runtime_stub_routes,
        "parity_pending_routes": report.parity_pending_routes,
        "parity_ready_routes": report.parity_ready_routes,
        "advanced_routes": {
            "count": report.advanced_routes,
            "total": report.total_routes,
            "percent": percent(report.advanced_routes, report.total_routes),
        },
        "non_runtime_routes": {
            "advanced": report.non_runtime_advanced_routes,
            "total": report.non_runtime_routes,
            "percent": percent(report.non_runtime_advanced_routes, report.non_runtime_routes),
        },
        "routes_by_phase": ordered_routes_by_phase(report.routes_by_phase),
        "errors": report.errors,
    }
    return json.dumps(payload, indent=2)


def format_error_json(
    message: str,
    repo_root: Path,
    manifest_path: Path,
    *,
    require_complete_controller_stubs: bool,
) -> str:
    payload = {
        "ok": False,
        "strict": require_complete_controller_stubs,
        "manifest_path": display_path(manifest_path, repo_root),
        "total_routes": 0,
        "phase_counts": ordered_phase_counts(Counter()),
        "starter_routes": {
            "count": 0,
            "route_ids": [],
        },
        "controller_stub_routes": 0,
        "runtime_stub_routes": 0,
        "parity_pending_routes": 0,
        "parity_ready_routes": 0,
        "advanced_routes": {
            "count": 0,
            "total": 0,
            "percent": 100.0,
        },
        "non_runtime_routes": {
            "advanced": 0,
            "total": 0,
            "percent": 100.0,
        },
        "routes_by_phase": ordered_routes_by_phase({phase: [] for phase in MIGRATION_PHASES}),
        "errors": [message],
    }
    return json.dumps(payload, indent=2)


def print_text_report(report: CompletionReport) -> None:
    print("RmlUi controller-stub completion:")
    print(f"  Routes checked: {report.total_routes}")
    print(f"  Phase counts: {format_count_line(report)}")
    print(
        "  Advanced routes: "
        f"{report.advanced_routes}/{report.total_routes} "
        f"({percent(report.advanced_routes, report.total_routes)}%)"
    )
    print(
        "  Non-runtime routes advanced: "
        f"{report.non_runtime_advanced_routes}/{report.non_runtime_routes} "
        f"({percent(report.non_runtime_advanced_routes, report.non_runtime_routes)}%)"
    )
    print(f"  Remaining starter routes: {report.starter_routes}")
    print(f"  Starter route ids: {format_route_ids(report.starter_route_ids)}")
    print(
        "  Strict completion required: "
        + ("yes" if report.require_complete_controller_stubs else "no")
    )

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        return

    if report.starter_route_ids:
        print(
            "\nResult: RmlUi controller-stub completion report passed; "
            "remaining starter routes are informational."
        )
    else:
        print("\nResult: RmlUi controller-stub completion check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST_PATH,
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve the default manifest path.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format.",
    )
    parser.add_argument(
        "--require-complete-controller-stubs",
        action="store_true",
        help=(
            "Fail when any route remains in migration_phase 'starter'. "
            "Default mode reports starters without requiring completion."
        ),
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = resolve_input_path(repo_root, args.manifest)

    try:
        manifest_data = read_json_object(manifest_path, "RmlUi smoke manifest")
        report = audit_controller_stub_completion(
            manifest_data,
            repo_root,
            manifest_path,
            require_complete_controller_stubs=args.require_complete_controller_stubs,
        )
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        message = f"Failed to validate RmlUi controller-stub completion: {exc}"
        if args.format == "json":
            print(
                format_error_json(
                    message,
                    repo_root,
                    manifest_path,
                    require_complete_controller_stubs=args.require_complete_controller_stubs,
                )
            )
        else:
            print(message, file=sys.stderr)
        return 1

    if args.format == "json":
        print(format_json_report(report))
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
