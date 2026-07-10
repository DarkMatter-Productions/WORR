#!/usr/bin/env python3
"""Validate RmlUi feature route metadata has a stable static shape."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath
from typing import Any


DEFAULT_ROUTE_METADATA_ROOT = Path("assets/ui/rml")
RML_ASSET_ROOT = Path("assets/ui/rml")
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
TASK_ID_RE = re.compile(r"^(?:FR|DV)-\d{2}-T\d{2}$")
ROOT_REQUIRED_FIELDS = (
    "schema",
    "owner",
    "tasks",
    "status_values",
    "migration_phase_values",
    "routes",
)
ROOT_REQUIRED_FIELDS_FOR_SUPPORT_METADATA = (
    "schema",
    "migration_phase_values",
    "routes",
)
ROUTE_REQUIRED_FIELDS = (
    "id",
    "wave",
    "group",
    "document",
    "document_id",
    "status",
    "source_menu",
    "legacy_surface",
    "current_surface",
    "source_owner",
    "migration_phase",
    "task_ids",
    "controller_scope",
    "entry_points",
    "data_models",
    "notes",
)
SUPPORT_ROUTE_REQUIRED_FIELDS = (
    "id",
    "document",
    "document_id",
    "legacy_surface",
    "current_surface",
    "source_owner",
    "migration_phase",
    "task_ids",
    "controller_scope",
    "entry_points",
    "data_models",
)
CONTRACT_REQUIRED_FIELDS = (
    "category",
    "contract",
    "fixture",
    "model",
    "status",
)
SUPPORT_METADATA_ROUTE_IDS = frozenset({"core.runtime_smoke"})
SUPPORT_METADATA_PATHS = frozenset({"assets/ui/rml/core/routes.json"})
INTENTIONAL_EMPTY_DATA_MODEL_ROUTES = frozenset(
    {
        "core.runtime_smoke",
        "game",
        "leave_match_confirm",
        "main",
        "options",
        "quit_confirm",
    }
)
ADVANCED_CONTRACT_EXCEPTIONS = frozenset()


@dataclass
class RouteMetadataShapeReport:
    metadata_files: int = 0
    metadata_routes: int = 0
    routes_by_phase: Counter[str] = field(default_factory=Counter)
    routes_with_controller_contracts: int = 0
    controller_contract_refs: int = 0
    malformed_routes: set[str] = field(default_factory=set)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


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


def discover_route_metadata_paths(repo_root: Path) -> list[Path]:
    metadata_root = resolve_input_path(repo_root, DEFAULT_ROUTE_METADATA_ROOT)
    if not metadata_root.is_dir():
        return []
    return sorted(path.resolve() for path in metadata_root.glob("*/routes.json") if path.is_file())


def add_error(report: RouteMetadataShapeReport, message: str) -> None:
    report.errors.append(message)


def add_route_error(
    report: RouteMetadataShapeReport,
    route_label: str,
    message: str,
) -> None:
    report.malformed_routes.add(route_label)
    report.errors.append(f"{route_label} {message}")


def is_non_empty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value)


def route_label(metadata_label: str, route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if is_non_empty_string(route_id):
        return f"{metadata_label} route {route_id!r}"
    return f"{metadata_label} route at index {index}"


def validate_string_field(
    value: Any,
    field_name: str,
    report: RouteMetadataShapeReport,
    route_label_value: str,
) -> str | None:
    if not is_non_empty_string(value):
        add_route_error(
            report,
            route_label_value,
            f"field '{field_name}' must be a non-empty string",
        )
        return None
    return value


def validate_task_id_list(
    value: Any,
    *,
    label: str,
    report: RouteMetadataShapeReport,
    route_label_value: str | None = None,
) -> None:
    def record(message: str) -> None:
        if route_label_value is None:
            add_error(report, f"{label} {message}")
        else:
            add_route_error(report, route_label_value, f"{label} {message}")

    if not isinstance(value, list) or not value:
        record("must be a non-empty list")
        return

    for index, task_id in enumerate(value):
        if not isinstance(task_id, str) or not TASK_ID_RE.fullmatch(task_id):
            record(
                f"[{index}] must look like FR-09-T08 or DV-03-T07; "
                f"got {task_id!r}"
            )


def validate_string_list(
    value: Any,
    *,
    label: str,
    report: RouteMetadataShapeReport,
    route_label_value: str,
    allow_empty: bool = False,
) -> bool:
    if not isinstance(value, list):
        add_route_error(report, route_label_value, f"{label} must be a list of strings")
        return False
    if not value and not allow_empty:
        add_route_error(report, route_label_value, f"{label} must not be empty")
        return False

    ok = True
    for index, item in enumerate(value):
        if not is_non_empty_string(item):
            add_route_error(
                report,
                route_label_value,
                f"{label}[{index}] must be a non-empty string",
            )
            ok = False
    return ok


def normalize_document_path(
    value: Any,
    *,
    label: str,
    report: RouteMetadataShapeReport,
    route_label_value: str,
) -> str | None:
    if not is_non_empty_string(value):
        add_route_error(report, route_label_value, f"{label} must be a non-empty string")
        return None
    if "\\" in value:
        add_route_error(
            report,
            route_label_value,
            f"{label} must use '/' separators: {value}",
        )
        return None
    if ":" in value or PureWindowsPath(value).is_absolute() or value.startswith("/"):
        add_route_error(
            report,
            route_label_value,
            f"{label} must be repo-relative: {value}",
        )
        return None

    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        add_route_error(
            report,
            route_label_value,
            f"{label} must not contain empty, '.', or '..' segments: {value}",
        )
        return None

    document_path = Path(*parts)
    document_posix = document_path.as_posix()
    if not document_posix.startswith(RML_ASSET_ROOT.as_posix() + "/"):
        document_path = RML_ASSET_ROOT / document_path
        document_posix = document_path.as_posix()

    if document_path.suffix != ".rml":
        add_route_error(
            report,
            route_label_value,
            f"{label} must point at an .rml document: {document_posix}",
        )
        return None

    return document_posix


def validate_migration_phase_values(
    value: Any,
    *,
    label: str,
    report: RouteMetadataShapeReport,
) -> None:
    if not isinstance(value, dict) or not value:
        add_error(report, f"{label} field 'migration_phase_values' must be a non-empty object")
        return

    keys = set(value)
    expected = set(MIGRATION_PHASES)
    missing = sorted(expected - keys)
    unknown = sorted(keys - expected)
    if missing:
        add_error(
            report,
            f"{label} field 'migration_phase_values' is missing phases: "
            + ", ".join(missing),
        )
    if unknown:
        add_error(
            report,
            f"{label} field 'migration_phase_values' has unknown phases: "
            + ", ".join(unknown),
        )

    for phase in MIGRATION_PHASES:
        if phase in value and not is_non_empty_string(value[phase]):
            add_error(
                report,
                f"{label} field 'migration_phase_values.{phase}' must be a non-empty string",
            )


def validate_status_values(
    value: Any,
    *,
    label: str,
    report: RouteMetadataShapeReport,
) -> set[str]:
    if not isinstance(value, dict) or not value:
        add_error(report, f"{label} field 'status_values' must be a non-empty object")
        return set()

    statuses: set[str] = set()
    for key, description in value.items():
        if not is_non_empty_string(key):
            add_error(report, f"{label} field 'status_values' keys must be non-empty strings")
            continue
        statuses.add(key)
        if not is_non_empty_string(description):
            add_error(
                report,
                f"{label} field 'status_values.{key}' must be a non-empty string",
            )
    return statuses


def validate_root(
    metadata_path: Path,
    metadata_label: str,
    data: dict[str, Any],
    repo_root: Path,
    report: RouteMetadataShapeReport,
) -> tuple[list[Any], set[str]]:
    relative_path = display_path(metadata_path, repo_root)
    support_metadata_root = relative_path in SUPPORT_METADATA_PATHS
    required_fields = (
        ROOT_REQUIRED_FIELDS_FOR_SUPPORT_METADATA
        if support_metadata_root
        else ROOT_REQUIRED_FIELDS
    )

    for field_name in required_fields:
        if field_name not in data:
            add_error(report, f"{metadata_label} missing required root field '{field_name}'")

    schema = data.get("schema")
    if "schema" in data and not is_non_empty_string(schema):
        add_error(report, f"{metadata_label} field 'schema' must be a non-empty string")

    owner = data.get("owner")
    if "owner" in data and not is_non_empty_string(owner):
        add_error(report, f"{metadata_label} field 'owner' must be a non-empty string")

    if "tasks" in data:
        validate_task_id_list(data.get("tasks"), label="field 'tasks'", report=report)

    status_values: set[str] = set()
    if "status_values" in data:
        status_values = validate_status_values(
            data.get("status_values"),
            label=metadata_label,
            report=report,
        )

    if "migration_phase_values" in data:
        validate_migration_phase_values(
            data.get("migration_phase_values"),
            label=metadata_label,
            report=report,
        )

    routes = data.get("routes")
    if not isinstance(routes, list):
        add_error(report, f"{metadata_label} field 'routes' must be a list")
        return [], status_values
    return routes, status_values


def validate_controller_contracts(
    route: dict[str, Any],
    *,
    route_id: str | None,
    migration_phase: str | None,
    route_label_value: str,
    report: RouteMetadataShapeReport,
) -> None:
    controller_contracts = route.get("controller_contracts")
    requires_contracts = (
        migration_phase in ADVANCED_PHASES
        and route_id not in ADVANCED_CONTRACT_EXCEPTIONS
    )

    if controller_contracts is None:
        if requires_contracts:
            add_route_error(
                report,
                route_label_value,
                "advanced route must include non-empty controller_contracts",
            )
        return

    if not isinstance(controller_contracts, list):
        add_route_error(
            report,
            route_label_value,
            "field 'controller_contracts' must be a list when present",
        )
        return

    if not controller_contracts:
        if requires_contracts:
            add_route_error(
                report,
                route_label_value,
                "advanced route must include non-empty controller_contracts",
            )
        return

    report.routes_with_controller_contracts += 1
    report.controller_contract_refs += len(controller_contracts)

    for index, contract_ref in enumerate(controller_contracts):
        if not isinstance(contract_ref, dict):
            add_route_error(
                report,
                route_label_value,
                f"controller_contracts[{index}] must be an object",
            )
            continue
        for field_name in CONTRACT_REQUIRED_FIELDS:
            if not is_non_empty_string(contract_ref.get(field_name)):
                add_route_error(
                    report,
                    route_label_value,
                    f"controller_contracts[{index}] field '{field_name}' "
                    "must be a non-empty string",
                )


def validate_route(
    route: Any,
    *,
    index: int,
    metadata_label: str,
    status_values: set[str],
    report: RouteMetadataShapeReport,
) -> None:
    if not isinstance(route, dict):
        route_label_value = f"{metadata_label} route at index {index}"
        report.malformed_routes.add(route_label_value)
        add_error(report, f"{route_label_value} must be an object")
        return

    report.metadata_routes += 1
    route_label_value = route_label(metadata_label, route, index)
    route_id = route.get("id") if is_non_empty_string(route.get("id")) else None
    support_route = route_id in SUPPORT_METADATA_ROUTE_IDS
    required_fields = SUPPORT_ROUTE_REQUIRED_FIELDS if support_route else ROUTE_REQUIRED_FIELDS

    for field_name in required_fields:
        if field_name not in route:
            add_route_error(
                report,
                route_label_value,
                f"missing required field '{field_name}'",
            )

    for field_name in (
        "id",
        "wave",
        "group",
        "document_id",
        "status",
        "source_menu",
        "legacy_surface",
        "current_surface",
        "source_owner",
        "controller_scope",
        "notes",
    ):
        if field_name in route:
            validate_string_field(route.get(field_name), field_name, report, route_label_value)

    document = route.get("document")
    if "document" in route:
        normalize_document_path(
            document,
            label="field 'document'",
            report=report,
            route_label_value=route_label_value,
        )

    migration_phase = route.get("migration_phase")
    if "migration_phase" in route:
        if not is_non_empty_string(migration_phase):
            add_route_error(
                report,
                route_label_value,
                "field 'migration_phase' must be a non-empty string",
            )
            migration_phase = None
        elif migration_phase not in MIGRATION_PHASES:
            allowed = ", ".join(MIGRATION_PHASES)
            add_route_error(
                report,
                route_label_value,
                f"field 'migration_phase' must be one of {allowed}; got {migration_phase!r}",
            )
        else:
            report.routes_by_phase[migration_phase] += 1

    if "status" in route and status_values:
        status = route.get("status")
        if is_non_empty_string(status) and status not in status_values:
            add_route_error(
                report,
                route_label_value,
                f"field 'status' must be declared in root status_values; got {status!r}",
            )

    if "task_ids" in route:
        validate_task_id_list(
            route.get("task_ids"),
            label="field 'task_ids'",
            report=report,
            route_label_value=route_label_value,
        )

    if "entry_points" in route:
        validate_string_list(
            route.get("entry_points"),
            label="field 'entry_points'",
            report=report,
            route_label_value=route_label_value,
        )

    if "data_models" in route:
        allow_empty_data_models = route_id in INTENTIONAL_EMPTY_DATA_MODEL_ROUTES
        validate_string_list(
            route.get("data_models"),
            label="field 'data_models'",
            report=report,
            route_label_value=route_label_value,
            allow_empty=allow_empty_data_models,
        )

    validate_controller_contracts(
        route,
        route_id=route_id,
        migration_phase=migration_phase if isinstance(migration_phase, str) else None,
        route_label_value=route_label_value,
        report=report,
    )


def audit_route_metadata_shape(
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
) -> RouteMetadataShapeReport:
    report = RouteMetadataShapeReport(metadata_files=len(route_metadata_sets))
    seen_route_ids: dict[str, str] = {}
    duplicate_route_ids: set[str] = set()

    for metadata_path, data in route_metadata_sets:
        metadata_label = display_path(metadata_path, repo_root)
        routes, status_values = validate_root(
            metadata_path,
            metadata_label,
            data,
            repo_root,
            report,
        )

        for index, route in enumerate(routes):
            validate_route(
                route,
                index=index,
                metadata_label=metadata_label,
                status_values=status_values,
                report=report,
            )
            if not isinstance(route, dict):
                continue
            route_id = route.get("id")
            if not is_non_empty_string(route_id):
                continue
            location = route_label(metadata_label, route, index)
            if route_id in seen_route_ids:
                duplicate_route_ids.add(route_id)
                report.malformed_routes.add(location)
                report.malformed_routes.add(seen_route_ids[route_id])
            else:
                seen_route_ids[route_id] = location

    for route_id in sorted(duplicate_route_ids):
        add_error(report, f"route metadata id {route_id!r} is duplicated")

    return report


def ordered_phase_counts(counter: Counter[str]) -> dict[str, int]:
    payload = {phase: counter[phase] for phase in MIGRATION_PHASES}
    for phase in sorted(counter):
        if phase not in payload:
            payload[phase] = counter[phase]
    return payload


def json_report_payload(report: RouteMetadataShapeReport) -> dict[str, Any]:
    malformed_routes = sorted(report.malformed_routes)
    return {
        "ok": report.ok(),
        "metadata_files": report.metadata_files,
        "metadata_routes": report.metadata_routes,
        "routes_by_phase": ordered_phase_counts(report.routes_by_phase),
        "routes_with_controller_contracts": report.routes_with_controller_contracts,
        "controller_contract_refs": report.controller_contract_refs,
        "malformed_route_count": len(malformed_routes),
        "malformed_routes": malformed_routes,
        "errors": report.errors,
    }


def print_json_report(report: RouteMetadataShapeReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def print_text_report(report: RouteMetadataShapeReport) -> None:
    phase_counts = ordered_phase_counts(report.routes_by_phase)
    print("RmlUi route metadata shape:")
    print(f"  Metadata files: {report.metadata_files}")
    print(f"  Metadata routes: {report.metadata_routes}")
    print(
        "  Routes by phase: "
        + ", ".join(f"{phase}={phase_counts[phase]}" for phase in MIGRATION_PHASES)
    )
    print(f"  Routes with controller contracts: {report.routes_with_controller_contracts}")
    print(f"  Controller contract refs: {report.controller_contract_refs}")
    print(f"  Malformed routes: {len(report.malformed_routes)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi route metadata shape check failed.")
    else:
        print("\nResult: RmlUi route metadata shape check passed.")


def failure_report(message: str) -> RouteMetadataShapeReport:
    report = RouteMetadataShapeReport()
    report.errors.append(message)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
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
    route_metadata_paths = (
        [resolve_input_path(repo_root, path) for path in args.route_metadata]
        if args.route_metadata
        else discover_route_metadata_paths(repo_root)
    )

    try:
        route_metadata_sets = [
            (path, read_json_object(path, f"{display_path(path, repo_root)} route metadata"))
            for path in route_metadata_paths
        ]
        report = audit_route_metadata_shape(route_metadata_sets, repo_root)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        report = failure_report(f"Failed to validate RmlUi route metadata shape: {exc}")
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
