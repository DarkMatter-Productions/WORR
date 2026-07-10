#!/usr/bin/env python3
"""Inventory static RmlUi route entry point metadata."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
DEFAULT_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_manifest.json")
DEFAULT_ROUTE_METADATA_ROOT = Path("assets/ui/rml")
SUPPORT_METADATA_ROUTE_IDS = frozenset({"core.runtime_smoke"})


@dataclass(frozen=True)
class RouteEntrypoints:
    route_id: str
    location: str
    entry_points: tuple[str, ...]


@dataclass(frozen=True)
class EntrypointProblem:
    route_id: str
    location: str
    reason: str
    index: int | None = None
    value: str | None = None


@dataclass
class EntrypointInventoryReport:
    central_route_ids: list[str] = field(default_factory=list)
    metadata_files: int = 0
    metadata_routes: int = 0
    entrypoints_by_route: dict[str, list[str]] = field(default_factory=dict)
    support_metadata_route_ids: list[str] = field(default_factory=list)
    unknown_metadata_route_ids: list[str] = field(default_factory=list)
    central_route_ids_without_metadata: list[str] = field(default_factory=list)
    malformed_entrypoint_details: list[EntrypointProblem] = field(default_factory=list)
    duplicate_entrypoint_details: list[dict[str, Any]] = field(default_factory=list)
    duplicate_metadata_route_ids: list[dict[str, Any]] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors

    @property
    def central_routes(self) -> int:
        return len(self.central_route_ids)

    @property
    def routes_with_entrypoints(self) -> int:
        return len(
            [
                route_id
                for route_id in self.inventory_route_ids
                if self.entrypoints_by_route.get(route_id)
            ]
        )

    @property
    def routes_without_entrypoints(self) -> int:
        return len(
            [
                route_id
                for route_id in self.inventory_route_ids
                if not self.entrypoints_by_route.get(route_id)
            ]
        )

    @property
    def total_entrypoint_refs(self) -> int:
        return sum(len(entry_points) for entry_points in self.entrypoints_by_route.values())

    @property
    def unique_entrypoints(self) -> int:
        return len(
            {
                entry_point
                for entry_points in self.entrypoints_by_route.values()
                for entry_point in entry_points
            }
        )

    @property
    def duplicate_entrypoints(self) -> int:
        return len(self.duplicate_entrypoint_details)

    @property
    def malformed_entrypoints(self) -> int:
        return len(self.malformed_entrypoint_details)

    @property
    def central_routes_without_metadata(self) -> int:
        return len(self.central_route_ids_without_metadata)

    @property
    def support_metadata_routes(self) -> int:
        return len(self.support_metadata_route_ids)

    @property
    def inventory_route_ids(self) -> list[str]:
        return sorted(set(self.entrypoints_by_route) - set(self.unknown_metadata_route_ids))

    @property
    def unique_entrypoint_values(self) -> list[str]:
        return sorted(
            {
                entry_point
                for entry_points in self.entrypoints_by_route.values()
                for entry_point in entry_points
            }
        )


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_input_path(repo_root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def route_list(data: dict[str, Any], label: str, errors: list[str]) -> list[Any]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append(f"{label} field 'routes' must be a list")
        return []
    return routes


def route_location(prefix: str, route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"{prefix} route {route_id!r}"
    return f"{prefix} route at index {index}"


def discover_route_metadata_paths(repo_root: Path) -> list[Path]:
    metadata_root = resolve_input_path(repo_root, DEFAULT_ROUTE_METADATA_ROOT)
    if not metadata_root.is_dir():
        return []
    return sorted(path.resolve() for path in metadata_root.glob("*/routes.json") if path.is_file())


def load_central_route_ids(
    manifest_data: dict[str, Any],
    report: EntrypointInventoryReport,
) -> set[str]:
    schema = manifest_data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    central_route_ids: list[str] = []
    duplicate_route_ids: set[str] = set()
    for index, route in enumerate(route_list(manifest_data, "central manifest", report.errors)):
        if not isinstance(route, dict):
            report.errors.append(f"central manifest route at index {index} must be an object")
            continue

        location = route_location("central manifest", route, index)
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            report.errors.append(f"{location} field 'id' must be a non-empty string")
            continue
        if route_id in central_route_ids:
            duplicate_route_ids.add(route_id)
            continue
        central_route_ids.append(route_id)

    for route_id in sorted(duplicate_route_ids):
        report.errors.append(f"central manifest route id {route_id!r} is duplicated")

    report.central_route_ids = sorted(central_route_ids)
    return set(central_route_ids)


def add_entrypoint_problem(
    report: EntrypointInventoryReport,
    route_id: str,
    location: str,
    reason: str,
    *,
    index: int | None = None,
    value: Any = None,
) -> None:
    problem = EntrypointProblem(
        route_id=route_id,
        location=location,
        reason=reason,
        index=index,
        value=None if value is None else repr(value),
    )
    report.malformed_entrypoint_details.append(problem)

    index_label = "" if index is None else f"[{index}]"
    report.errors.append(f"{location} entry_points{index_label} is malformed: {reason}")


def validate_entrypoints(
    route: dict[str, Any],
    route_id: str,
    location: str,
    report: EntrypointInventoryReport,
) -> tuple[str, ...]:
    if "entry_points" not in route:
        add_entrypoint_problem(report, route_id, location, "missing entry_points list")
        return ()

    entry_points = route.get("entry_points")
    if not isinstance(entry_points, list):
        add_entrypoint_problem(report, route_id, location, "entry_points must be a list")
        return ()

    valid_entrypoints: list[str] = []
    seen: set[str] = set()
    duplicate_values: set[str] = set()
    for index, value in enumerate(entry_points):
        if not isinstance(value, str):
            add_entrypoint_problem(
                report,
                route_id,
                location,
                "entry point must be a string",
                index=index,
                value=value,
            )
            continue

        stripped = value.strip()
        if not stripped:
            add_entrypoint_problem(
                report,
                route_id,
                location,
                "entry point must not be empty",
                index=index,
                value=value,
            )
            continue

        valid_entrypoints.append(stripped)
        if stripped in seen and stripped not in duplicate_values:
            duplicate_values.add(stripped)
            duplicate = {
                "route": route_id,
                "location": location,
                "entry_point": stripped,
            }
            report.duplicate_entrypoint_details.append(duplicate)
            report.errors.append(
                f"{location} entry_points duplicates entry point {stripped!r}"
            )
        seen.add(stripped)

    return tuple(valid_entrypoints)


def index_route_metadata(
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    central_route_ids: set[str],
    repo_root: Path,
    report: EntrypointInventoryReport,
) -> dict[str, RouteEntrypoints]:
    metadata_by_id: dict[str, RouteEntrypoints] = {}
    locations_by_id: dict[str, list[str]] = {}
    report.metadata_files = len(route_metadata_sets)

    for metadata_path, data in route_metadata_sets:
        metadata_label = display_path(metadata_path, repo_root)
        for index, route in enumerate(route_list(data, metadata_label, report.errors)):
            if not isinstance(route, dict):
                report.errors.append(f"{metadata_label} route at index {index} must be an object")
                continue

            location = route_location(metadata_label, route, index)
            route_id = route.get("id")
            if not isinstance(route_id, str) or not route_id:
                report.errors.append(f"{location} field 'id' must be a non-empty string")
                continue

            report.metadata_routes += 1
            locations_by_id.setdefault(route_id, []).append(location)

            entry_points = validate_entrypoints(route, route_id, location, report)
            if route_id not in metadata_by_id:
                metadata_by_id[route_id] = RouteEntrypoints(
                    route_id=route_id,
                    location=location,
                    entry_points=entry_points,
                )

            if route_id in central_route_ids or route_id in SUPPORT_METADATA_ROUTE_IDS:
                report.entrypoints_by_route.setdefault(route_id, list(entry_points))

    for route_id, locations in sorted(locations_by_id.items()):
        if len(locations) <= 1:
            continue
        report.duplicate_metadata_route_ids.append(
            {
                "id": route_id,
                "locations": locations,
            }
        )
        report.errors.append(
            f"route metadata id {route_id!r} is duplicated across feature metadata: "
            + "; ".join(locations)
        )

    return metadata_by_id


def compare_metadata_to_manifest(
    central_route_ids: set[str],
    metadata_by_id: dict[str, RouteEntrypoints],
    report: EntrypointInventoryReport,
) -> None:
    metadata_ids = set(metadata_by_id)
    extra_metadata_ids = metadata_ids - central_route_ids

    report.central_route_ids_without_metadata = sorted(central_route_ids - metadata_ids)
    report.support_metadata_route_ids = sorted(
        route_id for route_id in extra_metadata_ids if route_id in SUPPORT_METADATA_ROUTE_IDS
    )
    report.unknown_metadata_route_ids = sorted(
        route_id for route_id in extra_metadata_ids if route_id not in SUPPORT_METADATA_ROUTE_IDS
    )

    for route_id in report.central_route_ids_without_metadata:
        report.errors.append(f"central route {route_id!r} is missing route metadata")

    for route_id in report.unknown_metadata_route_ids:
        metadata_route = metadata_by_id[route_id]
        report.errors.append(
            f"metadata route {route_id!r} has no central smoke manifest route "
            f"and is not a supported metadata-only route ({metadata_route.location})"
        )


def audit_entrypoint_inventory(
    manifest_data: dict[str, Any],
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
) -> EntrypointInventoryReport:
    report = EntrypointInventoryReport()
    central_route_ids = load_central_route_ids(manifest_data, report)
    metadata_by_id = index_route_metadata(route_metadata_sets, central_route_ids, repo_root, report)
    compare_metadata_to_manifest(central_route_ids, metadata_by_id, report)
    return report


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def problem_payload(problem: EntrypointProblem) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "route": problem.route_id,
        "location": problem.location,
        "reason": problem.reason,
    }
    if problem.index is not None:
        payload["index"] = problem.index
    if problem.value is not None:
        payload["value"] = problem.value
    return payload


def json_report_payload(report: EntrypointInventoryReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "central_routes": report.central_routes,
        "metadata_files": report.metadata_files,
        "metadata_routes": report.metadata_routes,
        "routes_with_entrypoints": report.routes_with_entrypoints,
        "routes_without_entrypoints": report.routes_without_entrypoints,
        "total_entrypoint_refs": report.total_entrypoint_refs,
        "unique_entrypoints": report.unique_entrypoints,
        "duplicate_entrypoints": report.duplicate_entrypoints,
        "support_metadata_routes": report.support_metadata_routes,
        "central_routes_without_metadata": report.central_routes_without_metadata,
        "malformed_entrypoints": report.malformed_entrypoints,
        "entrypoints_by_route": {
            route_id: report.entrypoints_by_route[route_id]
            for route_id in sorted(report.entrypoints_by_route)
        },
        "unique_entrypoint_values": report.unique_entrypoint_values,
        "routes_without_entrypoint_ids": [
            route_id
            for route_id in report.inventory_route_ids
            if not report.entrypoints_by_route.get(route_id)
        ],
        "support_metadata_route_ids": report.support_metadata_route_ids,
        "central_route_ids_without_metadata": report.central_route_ids_without_metadata,
        "unknown_metadata_route_ids": report.unknown_metadata_route_ids,
        "duplicate_entrypoint_details": report.duplicate_entrypoint_details,
        "duplicate_metadata_route_ids": report.duplicate_metadata_route_ids,
        "malformed_entrypoint_details": [
            problem_payload(problem) for problem in report.malformed_entrypoint_details
        ],
        "errors": report.errors,
    }


def print_json_report(report: EntrypointInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def print_text_report(report: EntrypointInventoryReport) -> None:
    print("RmlUi route entrypoint inventory:")
    print(f"  Central routes: {report.central_routes}")
    print(f"  Metadata files: {report.metadata_files}")
    print(f"  Metadata routes: {report.metadata_routes}")
    print(f"  Routes with entrypoints: {report.routes_with_entrypoints}")
    print(f"  Routes without entrypoints: {report.routes_without_entrypoints}")
    print(f"  Total entrypoint refs: {report.total_entrypoint_refs}")
    print(f"  Unique entrypoints: {report.unique_entrypoints}")
    print(f"  Duplicate entrypoints: {report.duplicate_entrypoints}")
    print(f"  Support metadata routes: {report.support_metadata_routes}")
    print(f"  Central routes without metadata: {report.central_routes_without_metadata}")
    print(f"  Malformed entrypoints: {report.malformed_entrypoints}")
    print(f"  Entry point values: {compact_list(report.unique_entrypoint_values)}")
    print(f"  Support metadata route IDs: {compact_list(report.support_metadata_route_ids)}")
    print(
        "  Central route IDs without metadata: "
        f"{compact_list(report.central_route_ids_without_metadata)}"
    )
    print(f"  Unknown metadata route IDs: {compact_list(report.unknown_metadata_route_ids)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi route entrypoint inventory check failed.")
    else:
        print("\nResult: RmlUi route entrypoint inventory check passed.")


def failure_report(message: str) -> EntrypointInventoryReport:
    report = EntrypointInventoryReport()
    report.errors.append(message)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST_PATH,
        help="Path to the central RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--route-metadata",
        type=Path,
        action="append",
        default=None,
        help=(
            "Path to a feature route metadata JSON file. May be repeated. "
            "Defaults to every assets/ui/rml/*/routes.json file."
        ),
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve default paths.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = resolve_input_path(repo_root, args.manifest)
    route_metadata_paths = (
        [resolve_input_path(repo_root, path) for path in args.route_metadata]
        if args.route_metadata
        else discover_route_metadata_paths(repo_root)
    )

    try:
        manifest_data = read_json_object(manifest_path, "RmlUi smoke manifest")
        route_metadata_sets = [
            (path, read_json_object(path, f"{display_path(path, repo_root)} route metadata"))
            for path in route_metadata_paths
        ]
        report = audit_entrypoint_inventory(manifest_data, route_metadata_sets, repo_root)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        report = failure_report(f"Failed to validate RmlUi route entrypoint inventory: {exc}")
        if args.format == "json":
            print_json_report(report)
        else:
            print(report.errors[0], file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
