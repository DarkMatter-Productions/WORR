#!/usr/bin/env python3
"""Check RmlUi feature route metadata stays synced with the smoke manifest."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath
from typing import Any


DEFAULT_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_manifest.json")
DEFAULT_ROUTE_METADATA_ROOT = Path("assets/ui/rml")
RML_ASSET_ROOT = Path("assets/ui/rml")
SUPPORT_METADATA_ROUTE_IDS = frozenset({"core.runtime_smoke"})
ADVANCED_PHASES = (
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)


@dataclass(frozen=True)
class RouteRecord:
    route_id: str
    document: str | None
    migration_phase: str | None
    location: str


@dataclass
class MetadataSyncReport:
    central_route_count: int = 0
    metadata_file_count: int = 0
    metadata_route_count: int = 0
    matched_routes: list[str] = field(default_factory=list)
    central_routes_without_feature_metadata: list[str] = field(default_factory=list)
    advanced_central_routes_without_feature_metadata: list[str] = field(default_factory=list)
    support_metadata_routes: list[str] = field(default_factory=list)
    unknown_metadata_routes: list[str] = field(default_factory=list)
    phase_mismatches: list[dict[str, str]] = field(default_factory=list)
    document_mismatches: list[dict[str, str]] = field(default_factory=list)
    duplicate_metadata_route_ids: list[dict[str, Any]] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


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


def normalize_document_path(
    value: Any,
    *,
    label: str,
    errors: list[str],
    feature_metadata_document: bool,
) -> str | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label} field 'document' must be a non-empty string")
        return None
    if "\\" in value:
        errors.append(f"{label} document path must use '/' separators: {value}")
        return None
    if ":" in value or PureWindowsPath(value).is_absolute() or value.startswith("/"):
        errors.append(f"{label} document path must be repo-relative: {value}")
        return None

    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        errors.append(f"{label} document path must not contain empty, '.', or '..' segments: {value}")
        return None

    document_path = Path(*parts)
    if feature_metadata_document and not document_path.as_posix().startswith(
        RML_ASSET_ROOT.as_posix() + "/"
    ):
        document_path = RML_ASSET_ROOT / document_path
    return document_path.as_posix()


def load_central_routes(
    manifest_data: dict[str, Any],
    report: MetadataSyncReport,
) -> dict[str, RouteRecord]:
    routes_by_id: dict[str, RouteRecord] = {}
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
        if route_id in routes_by_id:
            duplicate_route_ids.add(route_id)
            continue

        document = normalize_document_path(
            route.get("document"),
            label=location,
            errors=report.errors,
            feature_metadata_document=False,
        )
        migration_phase = route.get("migration_phase")
        if migration_phase is not None and not isinstance(migration_phase, str):
            report.errors.append(f"{location} field 'migration_phase' must be a string when present")
            migration_phase = None

        routes_by_id[route_id] = RouteRecord(
            route_id=route_id,
            document=document,
            migration_phase=migration_phase,
            location=location,
        )

    for route_id in sorted(duplicate_route_ids):
        report.errors.append(f"central manifest route id {route_id!r} is duplicated")

    report.central_route_count = len(routes_by_id)
    return routes_by_id


def discover_route_metadata_paths(repo_root: Path) -> list[Path]:
    metadata_root = resolve_input_path(repo_root, DEFAULT_ROUTE_METADATA_ROOT)
    if not metadata_root.is_dir():
        return []
    return sorted(path.resolve() for path in metadata_root.glob("*/routes.json") if path.is_file())


def index_route_metadata(
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
    report: MetadataSyncReport,
) -> dict[str, RouteRecord]:
    metadata_by_id: dict[str, RouteRecord] = {}
    locations_by_id: dict[str, list[str]] = {}
    report.metadata_file_count = len(route_metadata_sets)

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

            report.metadata_route_count += 1
            locations_by_id.setdefault(route_id, []).append(location)
            if route_id in metadata_by_id:
                continue

            document = normalize_document_path(
                route.get("document"),
                label=location,
                errors=report.errors,
                feature_metadata_document=True,
            )
            migration_phase = route.get("migration_phase")
            if migration_phase is not None and not isinstance(migration_phase, str):
                report.errors.append(f"{location} field 'migration_phase' must be a string when present")
                migration_phase = None

            metadata_by_id[route_id] = RouteRecord(
                route_id=route_id,
                document=document,
                migration_phase=migration_phase,
                location=location,
            )

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
    central_routes: dict[str, RouteRecord],
    metadata_by_id: dict[str, RouteRecord],
    report: MetadataSyncReport,
) -> None:
    central_ids = set(central_routes)
    metadata_ids = set(metadata_by_id)

    report.matched_routes = sorted(central_ids & metadata_ids)
    report.central_routes_without_feature_metadata = sorted(central_ids - metadata_ids)
    report.advanced_central_routes_without_feature_metadata = [
        route_id
        for route_id in report.central_routes_without_feature_metadata
        if central_routes[route_id].migration_phase in ADVANCED_PHASES
    ]
    extra_metadata_ids = metadata_ids - central_ids
    report.support_metadata_routes = sorted(
        route_id for route_id in extra_metadata_ids if route_id in SUPPORT_METADATA_ROUTE_IDS
    )
    report.unknown_metadata_routes = sorted(
        route_id for route_id in extra_metadata_ids if route_id not in SUPPORT_METADATA_ROUTE_IDS
    )

    for route_id in report.unknown_metadata_routes:
        metadata_route = metadata_by_id[route_id]
        report.errors.append(
            f"metadata route {route_id!r} has no central smoke manifest route "
            f"({metadata_route.location})"
        )

    for route_id in report.advanced_central_routes_without_feature_metadata:
        central_route = central_routes[route_id]
        report.errors.append(
            f"advanced central route {route_id!r} with migration_phase "
            f"{central_route.migration_phase!r} is missing feature route metadata"
        )

    for route_id in report.matched_routes:
        central_route = central_routes[route_id]
        metadata_route = metadata_by_id[route_id]

        if (
            central_route.document is not None
            and metadata_route.document is not None
            and central_route.document != metadata_route.document
        ):
            mismatch = {
                "id": route_id,
                "central_document": central_route.document,
                "metadata_document": metadata_route.document,
                "metadata_location": metadata_route.location,
            }
            report.document_mismatches.append(mismatch)
            report.errors.append(
                f"route {route_id!r} document mismatch: central "
                f"{central_route.document!r}, metadata {metadata_route.document!r} "
                f"({metadata_route.location})"
            )

        if (
            central_route.migration_phase is not None
            and metadata_route.migration_phase is not None
            and central_route.migration_phase != metadata_route.migration_phase
        ):
            mismatch = {
                "id": route_id,
                "central_migration_phase": central_route.migration_phase,
                "metadata_migration_phase": metadata_route.migration_phase,
                "metadata_location": metadata_route.location,
            }
            report.phase_mismatches.append(mismatch)
            report.errors.append(
                f"route {route_id!r} migration_phase mismatch: central "
                f"{central_route.migration_phase!r}, metadata "
                f"{metadata_route.migration_phase!r} ({metadata_route.location})"
            )


def audit_metadata_sync(
    manifest_data: dict[str, Any],
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
) -> MetadataSyncReport:
    report = MetadataSyncReport()
    central_routes = load_central_routes(manifest_data, report)
    metadata_by_id = index_route_metadata(route_metadata_sets, repo_root, report)
    compare_metadata_to_manifest(central_routes, metadata_by_id, report)
    return report


def list_payload(values: list[str]) -> dict[str, Any]:
    return {
        "count": len(values),
        "routes": values,
    }


def json_report_payload(report: MetadataSyncReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "central_route_count": report.central_route_count,
        "metadata_file_count": report.metadata_file_count,
        "metadata_route_count": report.metadata_route_count,
        "matched_route_count": len(report.matched_routes),
        "matched_routes": report.matched_routes,
        "central_routes_without_feature_metadata": list_payload(
            report.central_routes_without_feature_metadata
        ),
        "advanced_central_routes_without_feature_metadata": list_payload(
            report.advanced_central_routes_without_feature_metadata
        ),
        "support_metadata_routes": list_payload(report.support_metadata_routes),
        "unknown_metadata_routes": list_payload(report.unknown_metadata_routes),
        "phase_mismatch_count": len(report.phase_mismatches),
        "phase_mismatches": report.phase_mismatches,
        "document_mismatch_count": len(report.document_mismatches),
        "document_mismatches": report.document_mismatches,
        "duplicate_count": len(report.duplicate_metadata_route_ids),
        "duplicate_metadata_route_ids": report.duplicate_metadata_route_ids,
        "errors": report.errors,
    }


def print_json_report(report: MetadataSyncReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def print_text_report(report: MetadataSyncReport) -> None:
    print("RmlUi metadata sync:")
    print(f"  Central routes: {report.central_route_count}")
    print(f"  Metadata files: {report.metadata_file_count}")
    print(f"  Metadata routes: {report.metadata_route_count}")
    print(f"  Matched routes: {len(report.matched_routes)}")
    print(
        "  Central routes without feature metadata: "
        f"{len(report.central_routes_without_feature_metadata)}"
    )
    print(
        "  Advanced central routes without feature metadata: "
        f"{len(report.advanced_central_routes_without_feature_metadata)}"
    )
    print(f"  Support metadata routes: {len(report.support_metadata_routes)}")
    print(f"  Unknown metadata routes: {len(report.unknown_metadata_routes)}")
    print(f"  Phase mismatches: {len(report.phase_mismatches)}")
    print(f"  Document mismatches: {len(report.document_mismatches)}")
    print(f"  Duplicate route IDs: {len(report.duplicate_metadata_route_ids)}")
    print(
        "  Central missing metadata route IDs: "
        f"{compact_list(report.central_routes_without_feature_metadata)}"
    )
    print(f"  Support metadata route IDs: {compact_list(report.support_metadata_routes)}")
    print(f"  Unknown metadata route IDs: {compact_list(report.unknown_metadata_routes)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi metadata sync check failed.")
    else:
        print("\nResult: RmlUi metadata sync check passed.")


def failure_report(message: str) -> MetadataSyncReport:
    report = MetadataSyncReport()
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
        report = audit_metadata_sync(manifest_data, route_metadata_sets, repo_root)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        report = failure_report(f"Failed to validate RmlUi metadata sync: {exc}")
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
