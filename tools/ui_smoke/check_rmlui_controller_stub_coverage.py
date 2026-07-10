#!/usr/bin/env python3
"""Validate controller_stub route claims against static RML controller hooks."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath
from typing import Any
from xml.etree import ElementTree


CONTROLLER_STUB_PHASE = "controller_stub"
DEFAULT_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_manifest.json")
DEFAULT_SHELL_ROUTES_PATH = Path("assets/ui/rml/shell/routes.json")
DEFAULT_ROUTE_METADATA_ROOT = Path("assets/ui/rml")

CATEGORY_ORDER = (
    "navigation",
    "command_action",
    "cvar_binding",
    "condition_state",
    "list_provider",
    "keybind",
    "preview",
)

ATTR_CATEGORY_MAP = {
    "data-route-target": "navigation",
    "data-command": "command_action",
    "data-cvar": "cvar_binding",
    "data-bind-cvar": "cvar_binding",
    "data-label-cvar": "cvar_binding",
    "data-command-cvar": "cvar_binding",
    "data-enable-if": "condition_state",
    "data-show-if": "condition_state",
    "data-visible-if": "condition_state",
    "data-list-provider": "list_provider",
    "data-bind-command": "keybind",
    "data-bind-group": "keybind",
    "data-preview": "preview",
    "data-preview-kind": "preview",
}

ATTR_VALUE_CATEGORY_MAP = {
    "data-event-click": {
        "keybind.capture": "keybind",
    },
}


@dataclass
class CoverageReport:
    route_metadata_files_checked: int = 0
    controller_stub_routes_checked: int = 0
    inferred_categories: Counter[str] = field(default_factory=Counter)
    covered_categories: Counter[str] = field(default_factory=Counter)
    missing_categories: Counter[str] = field(default_factory=Counter)
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


def route_list(data: dict[str, Any], label: str, report: CoverageReport) -> list[Any]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        report.errors.append(f"{label} field 'routes' must be a list")
        return []
    return routes


def route_metadata_label(path: Path, repo_root: Path) -> str:
    try:
        relative_path = path.resolve().relative_to(repo_root.resolve())
        if relative_path.name == "routes.json" and relative_path.parent.name:
            return relative_path.parent.name
        return relative_path.as_posix()
    except ValueError:
        return str(path)


def discover_route_metadata_paths(repo_root: Path) -> list[Path]:
    metadata_root = (repo_root / DEFAULT_ROUTE_METADATA_ROOT).resolve()
    if not metadata_root.is_dir():
        return []
    return sorted(metadata_root.glob("*/routes.json"))


def index_route_metadata_sets(
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
    report: CoverageReport,
) -> dict[str, dict[str, Any]]:
    route_index: dict[str, dict[str, Any]] = {}
    duplicate_ids: set[str] = set()

    report.route_metadata_files_checked = len(route_metadata_sets)

    for metadata_path, data in route_metadata_sets:
        metadata_name = route_metadata_label(metadata_path, repo_root)
        for index, route in enumerate(route_list(data, f"{metadata_name} routes", report)):
            if not isinstance(route, dict):
                report.errors.append(f"{metadata_name} route at index {index} must be an object")
                continue

            route_id = route.get("id")
            if not isinstance(route_id, str) or not route_id:
                report.errors.append(f"{metadata_name} route at index {index} is missing a non-empty id")
                continue

            if route_id in route_index:
                duplicate_ids.add(route_id)
                continue
            route_index[route_id] = route

    for route_id in sorted(duplicate_ids):
        report.errors.append(f"route metadata id {route_id!r} is duplicated")

    return route_index


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def resolve_repo_document(repo_root: Path, value: Any, route_id: str, report: CoverageReport) -> Path | None:
    if not isinstance(value, str) or not value:
        report.errors.append(f"route {route_id!r} manifest document must be a non-empty string")
        return None
    if "\\" in value:
        report.errors.append(f"route {route_id!r} manifest document must use '/' separators: {value}")
        return None
    if ":" in value or PureWindowsPath(value).is_absolute() or value.startswith("/"):
        report.errors.append(f"route {route_id!r} manifest document must be repo-relative: {value}")
        return None

    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        report.errors.append(
            f"route {route_id!r} manifest document must not contain empty, '.', or '..' segments: {value}"
        )
        return None

    document_path = (repo_root / Path(*parts)).resolve(strict=False)
    try:
        document_path.relative_to(repo_root)
    except ValueError:
        report.errors.append(f"route {route_id!r} manifest document escapes repo: {value}")
        return None

    return document_path


def infer_static_contract_categories(
    route_id: str,
    document_path: Path,
    repo_root: Path,
    report: CoverageReport,
) -> set[str]:
    try:
        root = ElementTree.parse(document_path).getroot()
    except ElementTree.ParseError as exc:
        report.errors.append(
            f"route {route_id!r} {display_path(document_path, repo_root)} is malformed RML: {exc}"
        )
        return set()
    except OSError as exc:
        report.errors.append(
            f"route {route_id!r} {display_path(document_path, repo_root)} cannot be read: {exc}"
        )
        return set()

    categories: set[str] = set()
    for element in root.iter():
        for attr_name, category in ATTR_CATEGORY_MAP.items():
            if attr_name in element.attrib:
                categories.add(category)
        for attr_name, value_map in ATTR_VALUE_CATEGORY_MAP.items():
            attr_value = element.attrib.get(attr_name)
            if attr_value in value_map:
                categories.add(value_map[attr_value])
    return categories


def controller_contract_categories(
    route_id: str,
    route_metadata: dict[str, Any],
    report: CoverageReport,
) -> set[str]:
    controller_contracts = route_metadata.get("controller_contracts")
    if not isinstance(controller_contracts, list):
        report.errors.append(f"route {route_id!r} route metadata is missing a non-empty controller_contracts list")
        return set()
    if not controller_contracts:
        report.errors.append(f"route {route_id!r} route metadata controller_contracts list must not be empty")
        return set()

    categories: set[str] = set()
    for index, contract_ref in enumerate(controller_contracts):
        if not isinstance(contract_ref, dict):
            report.errors.append(f"route {route_id!r} controller_contracts[{index}] must be an object")
            continue

        category = contract_ref.get("category")
        if not isinstance(category, str) or not category:
            report.errors.append(
                f"route {route_id!r} controller_contracts[{index}] field 'category' "
                "must be a non-empty string"
            )
            continue
        categories.add(category)

    return categories


def audit_controller_stub_coverage(
    manifest: dict[str, Any],
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
) -> CoverageReport:
    report = CoverageReport()
    repo_root = repo_root.resolve()
    route_metadata_index = index_route_metadata_sets(route_metadata_sets, repo_root, report)

    for index, manifest_route in enumerate(route_list(manifest, "manifest", report)):
        if not isinstance(manifest_route, dict):
            report.errors.append(f"manifest route at index {index} must be an object")
            continue
        if manifest_route.get("migration_phase") != CONTROLLER_STUB_PHASE:
            continue

        route_id = manifest_route.get("id")
        if not isinstance(route_id, str) or not route_id:
            report.errors.append(f"controller_stub manifest route at index {index} is missing a non-empty id")
            continue

        report.controller_stub_routes_checked += 1

        route_metadata = route_metadata_index.get(route_id)
        if route_metadata is None:
            report.errors.append(f"route {route_id!r} is missing matching route metadata")
            declared_categories: set[str] = set()
        else:
            metadata_phase = route_metadata.get("migration_phase")
            if metadata_phase != CONTROLLER_STUB_PHASE:
                report.errors.append(
                    f"route {route_id!r} route metadata migration_phase must be "
                    f"{CONTROLLER_STUB_PHASE!r}; got {metadata_phase!r}"
                )
            declared_categories = controller_contract_categories(route_id, route_metadata, report)

        document_path = resolve_repo_document(
            repo_root,
            manifest_route.get("document"),
            route_id,
            report,
        )
        if document_path is None:
            inferred_categories: set[str] = set()
        else:
            inferred_categories = infer_static_contract_categories(
                route_id,
                document_path,
                repo_root,
                report,
            )

        for category in sorted(inferred_categories):
            report.inferred_categories[category] += 1
            if category in declared_categories:
                report.covered_categories[category] += 1
            else:
                report.missing_categories[category] += 1
                report.errors.append(
                    f"route {route_id!r} infers controller category {category!r} "
                    "from static RML attributes but route metadata controller_contracts "
                    "does not cover it"
                )

    return report


def format_category_counter(counter: Counter[str]) -> str:
    if not counter:
        return "none"

    keys = [category for category in CATEGORY_ORDER if counter.get(category)]
    keys.extend(sorted(category for category in counter if category not in CATEGORY_ORDER and counter[category]))
    return ", ".join(f"{category}={counter[category]}" for category in keys)


def print_report(report: CoverageReport) -> None:
    print("RmlUi controller_stub coverage:")
    print(f"  route metadata files checked: {report.route_metadata_files_checked}")
    print(f"  controller_stub routes checked: {report.controller_stub_routes_checked}")
    print(f"  inferred categories: {format_category_counter(report.inferred_categories)}")
    print(f"  covered categories: {format_category_counter(report.covered_categories)}")
    print(f"  missing categories: {format_category_counter(report.missing_categories)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi controller_stub coverage check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST_PATH,
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--shell-routes",
        type=Path,
        default=DEFAULT_SHELL_ROUTES_PATH,
        help=(
            "Deprecated compatibility path to route metadata JSON. "
            "When omitted, all assets/ui/rml/*/routes.json files are discovered."
        ),
    )
    parser.add_argument(
        "--route-metadata",
        type=Path,
        action="append",
        default=None,
        help=(
            "Path to a route metadata JSON file. May be repeated. "
            "Defaults to all assets/ui/rml/*/routes.json files."
        ),
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve route documents.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = resolve_input_path(repo_root, args.manifest)
    explicit_route_metadata = list(args.route_metadata or [])
    default_shell_routes_supplied = args.shell_routes != DEFAULT_SHELL_ROUTES_PATH
    if default_shell_routes_supplied:
        explicit_route_metadata.append(args.shell_routes)
    route_metadata_paths = (
        [resolve_input_path(repo_root, path) for path in explicit_route_metadata]
        if explicit_route_metadata
        else discover_route_metadata_paths(repo_root)
    )

    try:
        manifest = read_json_object(manifest_path, "RmlUi smoke manifest")
        route_metadata_sets = [
            (path, read_json_object(path, f"{display_path(path, repo_root)} route metadata"))
            for path in route_metadata_paths
        ]
        report = audit_controller_stub_coverage(manifest, route_metadata_sets, repo_root)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Failed to audit controller_stub coverage: {exc}", file=sys.stderr)
        return 1

    print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
