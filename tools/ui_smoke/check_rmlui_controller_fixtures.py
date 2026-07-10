#!/usr/bin/env python3
"""Validate RmlUi controller mock fixtures referenced by route metadata."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any


DEFAULT_ROUTES_ROOT = Path("assets/ui/rml")
DEFAULT_CONTRACTS_DIR = Path("assets/ui/rml/contracts")
REQUIRED_CONTRACT_FIELDS = ("category", "contract", "fixture", "model", "status")
IDENTIFIER_FIELDS = ("contract", "$id", "id", "schema", "$schema")
TOKEN_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")


@dataclass(frozen=True)
class ContractRef:
    metadata_path: Path
    route_id: str
    ref_index: int
    fixture: str
    fixture_path: Path
    category: str | None
    contract: str | None
    model: str | None
    status: str | None
    label: str


@dataclass
class ControllerFixtureStats:
    metadata_files_checked: int = 0
    routes_checked: int = 0
    routes_with_controller_contracts: int = 0
    route_contract_refs: int = 0
    unique_fixtures_referenced: int = 0
    fixtures_present: int = 0
    missing_fixtures: int = 0
    malformed_fixtures: int = 0
    malformed_contract_refs: int = 0


@dataclass
class ControllerFixtureReport:
    repo_root: Path
    contracts_dir: Path
    metadata_files: list[Path] = field(default_factory=list)
    stats: ControllerFixtureStats = field(default_factory=ControllerFixtureStats)
    refs_by_fixture: dict[Path, list[ContractRef]] = field(default_factory=lambda: defaultdict(list))
    metadata_errors: list[str] = field(default_factory=list)
    missing_fixture_errors: list[str] = field(default_factory=list)
    malformed_fixture_errors: list[str] = field(default_factory=list)
    malformed_contract_ref_errors: list[str] = field(default_factory=list)

    def all_errors(self) -> list[str]:
        return (
            self.metadata_errors
            + self.missing_fixture_errors
            + self.malformed_fixture_errors
            + self.malformed_contract_ref_errors
        )

    def ok(self) -> bool:
        return not self.all_errors()


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def resolve_under_repo(repo_root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def is_windows_absolute(value: str) -> bool:
    return PureWindowsPath(value).is_absolute()


def is_relative_slash_path(value: str) -> bool:
    return not (
        "\\" in value
        or ":" in value
        or value.startswith("/")
        or PurePosixPath(value).is_absolute()
        or is_windows_absolute(value)
    )


def has_unsafe_segments(value: str) -> bool:
    return any(part in ("", ".", "..") for part in value.split("/"))


def route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"route {route_id!r}"
    return f"route at index {index}"


def discover_metadata_files(repo_root: Path, routes_root: Path) -> list[Path]:
    routes_root = resolve_under_repo(repo_root, routes_root)
    if not routes_root.is_dir():
        return []
    return sorted(path.resolve() for path in routes_root.glob("*/routes.json") if path.is_file())


def validate_token_field(value: Any, field_name: str, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label} field {field_name!r} must be a non-empty string")
        return None
    if not TOKEN_RE.fullmatch(value):
        errors.append(f"{label} field {field_name!r} must use lowercase token characters: {value!r}")
        return None
    return value


def resolve_fixture_path(
    repo_root: Path,
    contracts_dir: Path,
    value: Any,
    label: str,
    errors: list[str],
) -> tuple[str, Path] | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label} field 'fixture' must be a non-empty string")
        return None
    if not is_relative_slash_path(value):
        errors.append(f"{label} fixture path must be relative and use '/' separators: {value}")
        return None
    if has_unsafe_segments(value):
        errors.append(f"{label} fixture path must not contain empty, '.', or '..' segments: {value}")
        return None

    fixture_path = PurePosixPath(value)
    if not fixture_path.name.endswith(".mock.json"):
        errors.append(f"{label} fixture path must point to a .mock.json file: {value}")
        return None

    contracts_rel = PurePosixPath(DEFAULT_CONTRACTS_DIR.as_posix())
    if fixture_path.parts[: len(contracts_rel.parts)] == contracts_rel.parts:
        resolved = resolve_under_repo(repo_root, Path(*fixture_path.parts))
    else:
        resolved = (contracts_dir / Path(*fixture_path.parts)).resolve()

    try:
        resolved.relative_to(contracts_dir)
        resolved.relative_to(repo_root)
    except ValueError:
        errors.append(f"{label} fixture path must stay under {display_path(contracts_dir, repo_root)}: {value}")
        return None

    return value, resolved


def validate_contract_ref(
    repo_root: Path,
    contracts_dir: Path,
    metadata_path: Path,
    route: dict[str, Any],
    route_index: int,
    ref_index: int,
    value: Any,
) -> tuple[ContractRef | None, list[str]]:
    label = (
        f"{display_path(metadata_path, repo_root)} "
        f"{route_label(route, route_index)} controller_contracts[{ref_index}]"
    )
    errors: list[str] = []
    if not isinstance(value, dict):
        return None, [f"{label} must be an object"]

    category = validate_token_field(value.get("category"), "category", label, errors)
    contract = validate_token_field(value.get("contract"), "contract", label, errors)
    model = validate_token_field(value.get("model"), "model", label, errors)
    status = validate_token_field(value.get("status"), "status", label, errors)
    fixture_result = resolve_fixture_path(
        repo_root,
        contracts_dir,
        value.get("fixture"),
        label,
        errors,
    )

    ref: ContractRef | None = None
    if fixture_result is not None:
        fixture, fixture_path = fixture_result
        route_id = route.get("id") if isinstance(route.get("id"), str) and route.get("id") else f"<index {route_index}>"
        ref = ContractRef(
            metadata_path=metadata_path,
            route_id=route_id,
            ref_index=ref_index,
            fixture=fixture,
            fixture_path=fixture_path,
            category=category,
            contract=contract,
            model=model,
            status=status,
            label=label,
        )

    return ref, errors


def validate_fixture_identifier(
    data: dict[str, Any],
    fixture_display: str,
    errors: list[str],
) -> None:
    identifiers = {field_name: data.get(field_name) for field_name in IDENTIFIER_FIELDS if field_name in data}
    if not identifiers:
        allowed = ", ".join(IDENTIFIER_FIELDS)
        errors.append(f"{fixture_display} must declare at least one identifier field: {allowed}")
        return

    for field_name, value in identifiers.items():
        if not isinstance(value, str) or not value:
            errors.append(f"{fixture_display} identifier field {field_name!r} must be a non-empty string")


def validate_fixture_payload(
    repo_root: Path,
    fixture_path: Path,
    refs: list[ContractRef],
) -> list[str]:
    fixture_display = display_path(fixture_path, repo_root)
    errors: list[str] = []

    try:
        data = read_json_object(fixture_path, fixture_display)
    except json.JSONDecodeError as exc:
        return [f"{fixture_display} is not valid JSON: {exc}"]
    except (OSError, ValueError) as exc:
        return [f"{fixture_display} must be a JSON object: {exc}"]

    validate_fixture_identifier(data, fixture_display, errors)

    fixture_contract = data.get("contract")
    if isinstance(fixture_contract, str) and fixture_contract:
        for ref in refs:
            if ref.contract is not None and ref.contract != fixture_contract:
                errors.append(
                    f"{fixture_display} contract {fixture_contract!r} does not match "
                    f"{ref.label} contract {ref.contract!r}"
                )

    fixture_category = data.get("category")
    if fixture_category is not None:
        if not isinstance(fixture_category, str) or not fixture_category:
            errors.append(f"{fixture_display} field 'category' must be a non-empty string when present")
        elif not TOKEN_RE.fullmatch(fixture_category):
            errors.append(f"{fixture_display} field 'category' must use lowercase token characters: {fixture_category!r}")
        else:
            for ref in refs:
                if ref.category is not None and ref.category != fixture_category:
                    errors.append(
                        f"{fixture_display} category {fixture_category!r} does not match "
                        f"{ref.label} category {ref.category!r}"
                    )

    mock_value = data.get("mock")
    if mock_value is not None and not isinstance(mock_value, bool):
        errors.append(f"{fixture_display} field 'mock' must be a boolean when present")

    return errors


def audit_metadata_file(report: ControllerFixtureReport, metadata_path: Path) -> None:
    try:
        data = read_json_object(metadata_path, display_path(metadata_path, report.repo_root))
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        report.metadata_errors.append(f"{display_path(metadata_path, report.repo_root)} failed to load: {exc}")
        return

    routes = data.get("routes")
    if not isinstance(routes, list):
        report.metadata_errors.append(
            f"{display_path(metadata_path, report.repo_root)} field 'routes' must be a list"
        )
        return

    report.stats.routes_checked += len(routes)
    for route_index, route in enumerate(routes):
        if not isinstance(route, dict):
            report.metadata_errors.append(
                f"{display_path(metadata_path, report.repo_root)} route at index {route_index} must be an object"
            )
            continue

        if "controller_contracts" not in route:
            continue

        controller_contracts = route.get("controller_contracts")
        report.stats.routes_with_controller_contracts += 1
        if not isinstance(controller_contracts, list):
            label = f"{display_path(metadata_path, report.repo_root)} {route_label(route, route_index)}"
            report.stats.malformed_contract_refs += 1
            report.malformed_contract_ref_errors.append(f"{label} field 'controller_contracts' must be a list")
            continue

        report.stats.route_contract_refs += len(controller_contracts)
        for ref_index, contract_ref in enumerate(controller_contracts):
            ref, errors = validate_contract_ref(
                report.repo_root,
                report.contracts_dir,
                metadata_path,
                route,
                route_index,
                ref_index,
                contract_ref,
            )
            if errors:
                report.stats.malformed_contract_refs += 1
                report.malformed_contract_ref_errors.extend(errors)
            if ref is not None:
                report.refs_by_fixture[ref.fixture_path].append(ref)


def audit_controller_fixtures(
    repo_root: Path,
    routes_root: Path = DEFAULT_ROUTES_ROOT,
    contracts_dir: Path = DEFAULT_CONTRACTS_DIR,
) -> ControllerFixtureReport:
    repo_root = repo_root.resolve()
    contracts_dir = resolve_under_repo(repo_root, contracts_dir)
    report = ControllerFixtureReport(repo_root=repo_root, contracts_dir=contracts_dir)
    report.metadata_files = discover_metadata_files(repo_root, routes_root)
    report.stats.metadata_files_checked = len(report.metadata_files)

    if not report.metadata_files:
        report.metadata_errors.append(
            f"no route metadata files found under {display_path(resolve_under_repo(repo_root, routes_root), repo_root)}"
        )
        return report

    for metadata_path in report.metadata_files:
        audit_metadata_file(report, metadata_path)

    report.stats.unique_fixtures_referenced = len(report.refs_by_fixture)
    for fixture_path, refs in sorted(report.refs_by_fixture.items(), key=lambda item: display_path(item[0], repo_root)):
        if not fixture_path.is_file():
            report.stats.missing_fixtures += 1
            first_ref = refs[0]
            report.missing_fixture_errors.append(
                f"{first_ref.label} fixture file does not exist: {first_ref.fixture}"
            )
            continue

        report.stats.fixtures_present += 1
        fixture_errors = validate_fixture_payload(repo_root, fixture_path, refs)
        if fixture_errors:
            report.stats.malformed_fixtures += 1
            report.malformed_fixture_errors.extend(fixture_errors)

    return report


def format_fixture_reference_summary(report: ControllerFixtureReport) -> list[dict[str, Any]]:
    fixtures: list[dict[str, Any]] = []
    for fixture_path, refs in sorted(report.refs_by_fixture.items(), key=lambda item: display_path(item[0], report.repo_root)):
        fixtures.append(
            {
                "path": display_path(fixture_path, report.repo_root),
                "references": len(refs),
                "categories": dict(sorted(Counter(ref.category for ref in refs if ref.category).items())),
                "contracts": sorted({ref.contract for ref in refs if ref.contract}),
                "routes": sorted({ref.route_id for ref in refs}),
            }
        )
    return fixtures


def format_json_report(report: ControllerFixtureReport) -> str:
    payload = {
        "ok": report.ok(),
        "metadata_files": [display_path(path, report.repo_root) for path in report.metadata_files],
        "counts": {
            "metadata_files_checked": report.stats.metadata_files_checked,
            "routes_checked": report.stats.routes_checked,
            "routes_with_controller_contracts": report.stats.routes_with_controller_contracts,
            "route_contract_refs": report.stats.route_contract_refs,
            "unique_fixtures_referenced": report.stats.unique_fixtures_referenced,
            "fixtures_present": report.stats.fixtures_present,
            "missing_fixtures": report.stats.missing_fixtures,
            "malformed_fixtures": report.stats.malformed_fixtures,
            "malformed_contract_refs": report.stats.malformed_contract_refs,
        },
        "fixtures": format_fixture_reference_summary(report),
        "missing_fixtures": report.missing_fixture_errors,
        "malformed_fixtures": report.malformed_fixture_errors,
        "malformed_contract_refs": report.malformed_contract_ref_errors,
        "metadata_errors": report.metadata_errors,
        "errors": report.all_errors(),
    }
    return json.dumps(payload, indent=2)


def format_text_report(report: ControllerFixtureReport) -> str:
    lines = [
        "RmlUi controller fixture audit",
        f"Route metadata files: {report.stats.metadata_files_checked}",
        f"Routes checked: {report.stats.routes_checked}",
        f"Routes with controller contracts: {report.stats.routes_with_controller_contracts}",
        f"Route contract refs: {report.stats.route_contract_refs}",
        f"Fixtures referenced: {report.stats.unique_fixtures_referenced}",
        f"Fixtures present: {report.stats.fixtures_present}",
        f"Missing fixtures: {report.stats.missing_fixtures}",
        f"Malformed fixtures: {report.stats.malformed_fixtures}",
        f"Malformed contract refs: {report.stats.malformed_contract_refs}",
    ]

    def append_error_group(title: str, errors: list[str]) -> None:
        if not errors:
            return
        lines.append("")
        lines.append(f"{title}:")
        for error in errors:
            lines.append(f"  - {error}")

    append_error_group("Metadata errors", report.metadata_errors)
    append_error_group("Missing fixtures", report.missing_fixture_errors)
    append_error_group("Malformed fixtures", report.malformed_fixture_errors)
    append_error_group("Malformed contract refs", report.malformed_contract_ref_errors)

    lines.append("")
    if report.ok():
        lines.append("Result: RmlUi controller fixture check passed.")
    else:
        lines.append("Result: RmlUi controller fixture check failed.")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve route metadata and fixtures.",
    )
    parser.add_argument(
        "--routes-root",
        type=Path,
        default=DEFAULT_ROUTES_ROOT,
        help="Root containing one-level route metadata directories.",
    )
    parser.add_argument(
        "--contracts-dir",
        type=Path,
        default=DEFAULT_CONTRACTS_DIR,
        help="Directory containing controller *.mock.json fixtures.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format.",
    )
    args = parser.parse_args(argv)

    report = audit_controller_fixtures(args.repo_root, args.routes_root, args.contracts_dir)
    if args.format == "json":
        print(format_json_report(report))
    else:
        print(format_text_report(report))
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
