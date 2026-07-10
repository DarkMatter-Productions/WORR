#!/usr/bin/env python3
"""Audit WORR RmlUi route manifests against the lightweight route contract."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath
from typing import Any


DEFAULT_SCHEMA_PATH = Path("assets/ui/rml/contracts/route-contract.schema.json")
DEFAULT_CONTROLLER_CONTRACT_BASE = Path("assets/ui/rml/contracts")
DEFAULT_MANIFESTS = (
    {
        "name": "core",
        "path": "assets/ui/rml/core/routes.json",
        "document_base": "assets/ui/rml",
        "schema": "worr.rml.routes.v1",
    },
    {
        "name": "shell",
        "path": "assets/ui/rml/shell/routes.json",
        "document_base": "assets/ui/rml",
        "schema": "worr.rml.agent4.routes.v1",
    },
    {
        "name": "smoke",
        "path": "tools/ui_smoke/rmlui_manifest.json",
        "document_base": ".",
        "schema": "worr.rmlui.smoke_manifest.v1",
    },
)

ROUTE_ID_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
TOKEN_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
DOCUMENT_RE = re.compile(r"^[A-Za-z0-9_./-]+\.rml$")
FIXTURE_RE = re.compile(r"^[A-Za-z0-9_./-]+\.json$")
MIGRATION_PHASES = (
    "starter",
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)


@dataclass(frozen=True)
class ManifestSpec:
    name: str
    path: Path
    document_base: Path
    expected_schema: str | None = None


@dataclass
class ManifestReport:
    spec: ManifestSpec
    schema: str | None = None
    route_count: int = 0
    required_now_count: int = 0
    required_now_present: int = 0
    present_documents: int = 0
    pending_documents: int = 0
    controller_contract_count: int = 0
    owners: Counter[str] = field(default_factory=Counter)
    statuses: Counter[str] = field(default_factory=Counter)
    migration_phases: Counter[str] = field(default_factory=Counter)
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


@dataclass
class AuditRun:
    contract_id: str
    required_route_fields: list[str]
    reports: list[ManifestReport] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def all_errors(self) -> list[str]:
        errors = list(self.errors)
        for report in self.reports:
            errors.extend(f"{report.spec.name}: {error}" for error in report.errors)
        return errors

    def all_warnings(self) -> list[str]:
        warnings = list(self.warnings)
        for report in self.reports:
            warnings.extend(f"{report.spec.name}: {warning}" for warning in report.warnings)
        return warnings

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


def as_repo_relative_path(value: str, field_name: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{field_name} must be a non-empty string")
    if "\\" in value:
        raise ValueError(f"{field_name} must use '/' separators: {value}")
    if ":" in value or PureWindowsPath(value).is_absolute() or value.startswith("/"):
        raise ValueError(f"{field_name} must be repo-relative: {value}")
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ValueError(f"{field_name} must not contain empty, '.', or '..' segments: {value}")
    return Path(*parts)


def route_required_fields(contract: dict[str, Any]) -> list[str]:
    route_def = contract.get("$defs", {}).get("route", {})
    required = route_def.get("required", ["id", "document"])
    if not isinstance(required, list) or not all(isinstance(item, str) and item for item in required):
        raise ValueError("contract #/$defs/route/required must be a non-empty string list")
    return list(required)


def manifest_specs_from_contract(contract: dict[str, Any]) -> list[ManifestSpec]:
    audit_contract = contract.get("audit_contract", {})
    manifests = audit_contract.get("manifests", DEFAULT_MANIFESTS) if isinstance(audit_contract, dict) else DEFAULT_MANIFESTS
    if not isinstance(manifests, list) and not isinstance(manifests, tuple):
        raise ValueError("contract audit_contract.manifests must be a list")

    specs: list[ManifestSpec] = []
    for index, item in enumerate(manifests):
        if not isinstance(item, dict):
            raise ValueError(f"contract audit_contract.manifests[{index}] must be an object")
        name = item.get("name")
        path = item.get("path")
        document_base = item.get("document_base")
        expected_schema = item.get("schema")
        if not isinstance(name, str) or not name:
            raise ValueError(f"contract audit_contract.manifests[{index}].name must be a non-empty string")
        if expected_schema is not None and (not isinstance(expected_schema, str) or not expected_schema):
            raise ValueError(f"contract audit_contract.manifests[{index}].schema must be a non-empty string")
        specs.append(
            ManifestSpec(
                name=name,
                path=as_repo_relative_path(path, f"audit manifest {name} path"),
                document_base=Path(".") if document_base == "." else as_repo_relative_path(document_base, f"audit manifest {name} document_base"),
                expected_schema=expected_schema,
            )
        )
    return specs


def extend_specs_with_discovered_feature_manifests(
    repo_root: Path,
    specs: list[ManifestSpec],
) -> list[ManifestSpec]:
    """Add assets/ui/rml/<feature>/routes.json manifests not listed by the schema."""

    feature_root = (repo_root / "assets/ui/rml").resolve()
    if not feature_root.is_dir():
        return specs

    existing_paths = {spec.path.as_posix() for spec in specs}
    discovered = []
    for manifest_path in sorted(feature_root.glob("*/routes.json")):
        try:
            relative_path = manifest_path.resolve().relative_to(repo_root)
        except ValueError:
            continue

        relative_posix = relative_path.as_posix()
        if relative_posix in existing_paths:
            continue

        discovered.append(
            ManifestSpec(
                name=manifest_path.parent.name,
                path=relative_path,
                document_base=Path("assets/ui/rml"),
                expected_schema=None,
            )
        )
        existing_paths.add(relative_posix)

    return [*specs, *discovered]


def validate_token(report: ManifestReport, value: Any, field_name: str, label: str) -> str | None:
    if not isinstance(value, str) or not value:
        report.errors.append(f"{label} field {field_name!r} must be a non-empty string")
        return None
    if not TOKEN_RE.fullmatch(value):
        report.errors.append(f"{label} field {field_name!r} must use lowercase token characters: {value!r}")
        return None
    return value


def validate_document_value(report: ManifestReport, value: Any, label: str) -> str | None:
    if not isinstance(value, str) or not value:
        report.errors.append(f"{label} field 'document' must be a non-empty string")
        return None
    if "\\" in value:
        report.errors.append(f"{label} document path must use '/' separators: {value}")
        return None
    if ":" in value or PureWindowsPath(value).is_absolute() or value.startswith("/"):
        report.errors.append(f"{label} document path must be relative, not absolute: {value}")
        return None
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        report.errors.append(f"{label} document path must not contain empty, '.', or '..' segments: {value}")
        return None
    if not value.endswith(".rml"):
        report.errors.append(f"{label} document path must end with .rml: {value}")
        return None
    if not DOCUMENT_RE.fullmatch(value):
        report.errors.append(f"{label} document path contains unsupported characters: {value}")
        return None
    return value


def resolve_document_path(repo_root: Path, spec: ManifestSpec, document: str) -> Path:
    document_path = Path(*document.split("/"))
    return (repo_root / spec.document_base / document_path).resolve()


def resolve_controller_fixture_path(repo_root: Path, fixture: str) -> Path:
    fixture_path = Path(*fixture.split("/"))
    if fixture_path.parts[: len(DEFAULT_CONTROLLER_CONTRACT_BASE.parts)] == DEFAULT_CONTROLLER_CONTRACT_BASE.parts:
        return (repo_root / fixture_path).resolve()
    return (repo_root / DEFAULT_CONTROLLER_CONTRACT_BASE / fixture_path).resolve()


def validate_controller_fixture(
    report: ManifestReport,
    repo_root: Path,
    value: Any,
    label: str,
) -> Path | None:
    if not isinstance(value, str) or not value:
        report.errors.append(f"{label} field 'fixture' must be a non-empty string")
        return None
    if "\\" in value:
        report.errors.append(f"{label} fixture path must use '/' separators: {value}")
        return None
    if ":" in value or PureWindowsPath(value).is_absolute() or value.startswith("/"):
        report.errors.append(f"{label} fixture path must be repo-relative: {value}")
        return None
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        report.errors.append(f"{label} fixture path must not contain empty, '.', or '..' segments: {value}")
        return None
    if not value.endswith(".json"):
        report.errors.append(f"{label} fixture path must end with .json: {value}")
        return None
    if not FIXTURE_RE.fullmatch(value):
        report.errors.append(f"{label} fixture path contains unsupported characters: {value}")
        return None

    fixture_base = (repo_root / DEFAULT_CONTROLLER_CONTRACT_BASE).resolve()
    resolved_fixture = resolve_controller_fixture_path(repo_root, value)
    try:
        resolved_fixture.relative_to(fixture_base)
        resolved_fixture.relative_to(repo_root)
    except ValueError:
        report.errors.append(
            f"{label} fixture path must stay under {DEFAULT_CONTROLLER_CONTRACT_BASE.as_posix()}: {value}"
        )
        return None

    if not resolved_fixture.is_file():
        report.errors.append(f"{label} fixture file does not exist: {value}")
        return None

    try:
        read_json_object(resolved_fixture, f"{label} fixture")
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        report.errors.append(f"{label} fixture must be a JSON object: {value}: {exc}")
        return None

    return resolved_fixture


def validate_controller_contracts(
    report: ManifestReport,
    route: dict[str, Any],
    repo_root: Path,
    label: str,
) -> None:
    if "controller_contracts" not in route:
        return

    controller_contracts = route.get("controller_contracts")
    if not isinstance(controller_contracts, list):
        report.errors.append(f"{label} field 'controller_contracts' must be a list")
        return

    report.controller_contract_count += len(controller_contracts)
    for index, contract_ref in enumerate(controller_contracts):
        contract_label = f"{label} controller_contracts[{index}]"
        if not isinstance(contract_ref, dict):
            report.errors.append(f"{contract_label} must be an object")
            continue

        for field_name in ("category", "contract", "model", "status"):
            validate_token(report, contract_ref.get(field_name), field_name, contract_label)

        validate_controller_fixture(report, repo_root, contract_ref.get("fixture"), contract_label)

        if "notes" in contract_ref:
            notes = contract_ref.get("notes")
            if not isinstance(notes, str) or not notes:
                report.errors.append(f"{contract_label} field 'notes' must be a non-empty string")


def validate_required_field(report: ManifestReport, route: dict[str, Any], field_name: str, label: str) -> None:
    if field_name not in route:
        report.errors.append(f"{label} is missing required field {field_name!r}")
    elif route[field_name] in ("", None):
        report.errors.append(f"{label} required field {field_name!r} must not be empty")


def audit_manifest_data(
    data: dict[str, Any],
    repo_root: Path,
    spec: ManifestSpec,
    required_route_fields: list[str],
) -> ManifestReport:
    report = ManifestReport(spec=spec)
    repo_root = repo_root.resolve()
    document_base = (repo_root / spec.document_base).resolve()

    try:
        document_base.relative_to(repo_root)
    except ValueError:
        report.errors.append(f"document_base escapes repository root: {spec.document_base}")
        return report

    schema = data.get("schema")
    if isinstance(schema, str):
        report.schema = schema
    else:
        report.errors.append("field 'schema' must be a non-empty string")

    if spec.expected_schema and schema != spec.expected_schema:
        report.errors.append(f"unexpected schema {schema!r}; expected {spec.expected_schema!r}")

    manifest_owner: str | None = None
    if "owner" in data:
        manifest_owner = validate_token(report, data.get("owner"), "owner", "manifest")

    status_values = data.get("status_values")
    if status_values is not None:
        if not isinstance(status_values, dict):
            report.errors.append("field 'status_values' must be an object when present")
        else:
            for status_key, description in status_values.items():
                validate_token(report, status_key, "status_values key", "manifest")
                if not isinstance(description, str) or not description:
                    report.errors.append(f"manifest status_values[{status_key!r}] must be a non-empty string")

    routes = data.get("routes")
    if not isinstance(routes, list):
        report.errors.append("field 'routes' must be a list")
        return report

    report.route_count = len(routes)
    seen_ids: set[str] = set()
    duplicate_ids: set[str] = set()

    for index, route in enumerate(routes):
        label = f"route at index {index}"
        if not isinstance(route, dict):
            report.errors.append(f"{label} must be an object")
            continue

        for required_field in required_route_fields:
            validate_required_field(report, route, required_field, label)

        route_id = route.get("id")
        if isinstance(route_id, str) and route_id:
            label = f"route {route_id!r}"
            if not ROUTE_ID_RE.fullmatch(route_id):
                report.errors.append(f"{label} field 'id' must use lowercase route token characters")
            if route_id in seen_ids:
                duplicate_ids.add(route_id)
            seen_ids.add(route_id)
        else:
            report.errors.append(f"{label} field 'id' must be a non-empty string")

        route_owner = manifest_owner
        if "owner" in route:
            route_owner = validate_token(report, route.get("owner"), "owner", label)
        if route_owner is None:
            report.errors.append(f"{label} is missing owner and the manifest has no owner")
        else:
            report.owners[route_owner] += 1

        if "status" in route:
            route_status = validate_token(report, route.get("status"), "status", label)
            if route_status is not None:
                report.statuses[route_status] += 1

        if "migration_phase" in route:
            migration_phase = route.get("migration_phase")
            if not isinstance(migration_phase, str) or migration_phase not in MIGRATION_PHASES:
                allowed = ", ".join(MIGRATION_PHASES)
                report.errors.append(
                    f"{label} field 'migration_phase' must be one of {allowed}: "
                    f"{migration_phase!r}"
                )
            else:
                report.migration_phases[migration_phase] += 1

        validate_controller_contracts(report, route, repo_root, label)

        required_now = route.get("required_now", False)
        if "required_now" in route and not isinstance(required_now, bool):
            report.errors.append(f"{label} field 'required_now' must be a boolean")
            required_now = False
        if required_now:
            report.required_now_count += 1

        document = validate_document_value(report, route.get("document"), label)
        if document is None:
            continue

        resolved_document = resolve_document_path(repo_root, spec, document)
        try:
            resolved_document.relative_to(document_base)
            resolved_document.relative_to(repo_root)
        except ValueError:
            report.errors.append(f"{label} document path escapes its audit base: {document}")
            continue

        if resolved_document.is_file():
            report.present_documents += 1
            if required_now:
                report.required_now_present += 1
        elif required_now:
            report.errors.append(f"{label} required_now document does not exist: {document}")
        else:
            report.pending_documents += 1

    for route_id in sorted(duplicate_ids):
        report.errors.append(f"duplicate route id {route_id!r}")

    return report


def audit_route_contracts(repo_root: Path, schema_path: Path) -> AuditRun:
    repo_root = repo_root.resolve()
    contract_path = (repo_root / schema_path).resolve() if not schema_path.is_absolute() else schema_path.resolve()
    contract = read_json_object(contract_path, "route contract schema")
    contract_id = str(contract.get("$id", "<missing contract id>"))
    required_route_fields = route_required_fields(contract)
    specs = extend_specs_with_discovered_feature_manifests(
        repo_root,
        manifest_specs_from_contract(contract),
    )
    run = AuditRun(contract_id=contract_id, required_route_fields=required_route_fields)

    for spec in specs:
        manifest_path = (repo_root / spec.path).resolve()
        try:
            manifest_path.relative_to(repo_root)
        except ValueError:
            report = ManifestReport(spec=spec)
            report.errors.append(f"manifest path escapes repository root: {spec.path}")
            run.reports.append(report)
            continue
        try:
            manifest = read_json_object(manifest_path, f"{spec.name} manifest")
        except (OSError, json.JSONDecodeError, ValueError) as exc:
            report = ManifestReport(spec=spec)
            report.errors.append(f"failed to load {manifest_path}: {exc}")
            run.reports.append(report)
            continue
        run.reports.append(audit_manifest_data(manifest, repo_root, spec, required_route_fields))

    return run


def format_counter(counter: Counter[str]) -> str:
    return ", ".join(f"{key}={counter[key]}" for key in sorted(counter))


def print_run(run: AuditRun) -> None:
    print(f"Route contract: {run.contract_id}")
    print("Required route fields: " + ", ".join(run.required_route_fields))

    for report in run.reports:
        print()
        print(f"{report.spec.name}: {report.spec.path.as_posix()}")
        print(f"  Schema: {report.schema or '<missing>'}")
        print(f"  Document base: {report.spec.document_base.as_posix()}")
        print(f"  Routes: {report.route_count} total, {report.required_now_count} required_now")
        print(f"  Controller contracts: {report.controller_contract_count} references")
        print(f"  Required documents present: {report.required_now_present}/{report.required_now_count}")
        print(f"  Documents present: {report.present_documents}; optional/pending missing: {report.pending_documents}")
        if report.owners:
            print(f"  Owners: {format_counter(report.owners)}")
        if report.statuses:
            print(f"  Statuses: {format_counter(report.statuses)}")
        if report.migration_phases:
            print(f"  Migration phases: {format_counter(report.migration_phases)}")

    warnings = run.all_warnings()
    if warnings:
        print("\nWarnings:")
        for warning in warnings:
            print(f"  - {warning}")

    errors = run.all_errors()
    if errors:
        print("\nErrors:")
        for error in errors:
            print(f"  - {error}")
    else:
        print("\nRmlUi route contract audit passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--schema",
        type=Path,
        default=DEFAULT_SCHEMA_PATH,
        help="Path to the route contract schema/audit profile.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve manifests and documents.",
    )
    args = parser.parse_args(argv)

    try:
        run = audit_route_contracts(args.repo_root, args.schema)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Failed to audit route contracts: {exc}", file=sys.stderr)
        return 1

    print_run(run)
    return 0 if run.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
