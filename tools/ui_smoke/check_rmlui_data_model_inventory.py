#!/usr/bin/env python3
"""Inventory static data-model hooks declared by WORR RmlUi route documents."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from xml.etree import ElementTree


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
MODEL_ATTRS = ("data-model", "data-bind", "data-options", "data-bind-command", "data-bind-group")
TOKEN_ATTRS = (
    "data-model",
    "data-bind",
    "data-options",
    "data-component",
    "data-controller",
    "data-action-type",
    "data-slot",
    "data-bind-group",
)
COMPONENT_ATTR = "data-component"
CONTROLLER_ATTR = "data-controller"
ACTION_TYPE_ATTR = "data-action-type"
SLOT_ATTR = "data-slot"
BIND_COMMAND_ATTR = "data-bind-command"
TOKEN_RE = re.compile(r"^[a-z][a-z0-9_]*(?:[._-][a-z0-9_]+)*$")
BIND_COMMAND_RE = re.compile(r"^[A-Za-z0-9_+./$ -]+$")


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path


@dataclass(frozen=True)
class DataModelReference:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    category: str


@dataclass(frozen=True)
class BadToken:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    reason: str


@dataclass
class DataModelInventoryStats:
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_model_binding_refs: int = 0
    component_refs: int = 0
    controller_refs: int = 0
    action_type_refs: int = 0
    slot_refs: int = 0


@dataclass
class DataModelInventoryReport:
    repo_root: Path
    documents: list[RouteDocument] = field(default_factory=list)
    references: list[DataModelReference] = field(default_factory=list)
    bad_tokens: list[BadToken] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: DataModelInventoryStats = field(default_factory=DataModelInventoryStats)

    def ok(self) -> bool:
        return not self.errors

    @property
    def unique_model_tokens(self) -> list[str]:
        return sorted(
            {
                reference.value
                for reference in self.references
                if reference.attr_name in MODEL_ATTRS
            }
        )

    @property
    def routes_with_data_model_hooks(self) -> list[str]:
        return sorted(
            {
                reference.route_id
                for reference in self.references
                if reference.attr_name in MODEL_ATTRS
            }
        )


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("manifest root must be a JSON object")
    return data


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def is_within_repo(path: Path, repo_root: Path) -> bool:
    try:
        path.relative_to(repo_root)
    except ValueError:
        return False
    return True


def tag_name(tag: str) -> str:
    if "}" in tag:
        return tag.rsplit("}", 1)[1]
    return tag


def element_label(element: ElementTree.Element) -> str:
    tag = tag_name(element.tag)
    element_id = element.attrib.get("id")
    if element_id:
        return f"<{tag} id={element_id!r}>"
    return f"<{tag}>"


def route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"route {route_id!r}"
    return f"route at index {index}"


def document_label(document: RouteDocument, repo_root: Path) -> str:
    return f"route {document.route_id!r} {display_path(document.path, repo_root)}"


def collect_manifest_documents(data: dict[str, Any], repo_root: Path) -> DataModelInventoryReport:
    report = DataModelInventoryReport(repo_root=repo_root)
    schema = data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    routes = data.get("routes")
    if not isinstance(routes, list):
        report.errors.append("manifest field 'routes' must be a list")
        return report

    report.stats.route_count = len(routes)
    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            report.errors.append(f"route at index {index} must be an object")
            continue

        label = route_label(route, index)
        route_id = route.get("id")
        document = route.get("document")
        if not isinstance(route_id, str) or not route_id:
            report.errors.append(f"{label} field 'id' must be a non-empty string")
            continue
        if not isinstance(document, str) or not document:
            report.errors.append(f"{label} field 'document' must be a non-empty string")
            continue

        document_path = Path(document)
        if document_path.is_absolute():
            report.errors.append(f"{label} document path must be repo-relative: {document}")
            continue

        resolved_document = (repo_root / document_path).resolve(strict=False)
        if not is_within_repo(resolved_document, repo_root):
            report.errors.append(f"{label} document path escapes repo root: {document}")
            continue
        if resolved_document.suffix.lower() != ".rml":
            report.errors.append(f"{label} document path must point to an .rml file: {document}")
            continue
        if resolved_document.is_file():
            report.documents.append(RouteDocument(route_id=route_id, path=resolved_document))
        else:
            report.stats.documents_missing += 1
            report.errors.append(f"{label} missing route document {document}")

    return report


def token_reason(attr_name: str, value: str) -> str | None:
    stripped = value.strip()
    if not stripped:
        return "empty data-model token"
    if "{{" in stripped or "}}" in stripped:
        return "template placeholders are not valid route-level model tokens"
    if attr_name == BIND_COMMAND_ATTR:
        if not BIND_COMMAND_RE.fullmatch(stripped):
            return "bind-command must be a non-empty command string without control punctuation"
        return None
    if not TOKEN_RE.fullmatch(stripped):
        return "expected lowercase dotted/snake_case token"
    return None


def category_for_attr(attr_name: str) -> str:
    if attr_name in MODEL_ATTRS:
        return "model_binding"
    if attr_name == COMPONENT_ATTR:
        return "component"
    if attr_name == CONTROLLER_ATTR:
        return "controller"
    if attr_name == ACTION_TYPE_ATTR:
        return "action_type"
    if attr_name == SLOT_ATTR:
        return "slot"
    raise ValueError(f"unsupported data-model inventory attribute {attr_name!r}")


def increment_category(stats: DataModelInventoryStats, attr_name: str) -> None:
    if attr_name in MODEL_ATTRS:
        stats.total_model_binding_refs += 1
    elif attr_name == COMPONENT_ATTR:
        stats.component_refs += 1
    elif attr_name == CONTROLLER_ATTR:
        stats.controller_refs += 1
    elif attr_name == ACTION_TYPE_ATTR:
        stats.action_type_refs += 1
    elif attr_name == SLOT_ATTR:
        stats.slot_refs += 1
    else:
        raise ValueError(f"unsupported data-model inventory attribute {attr_name!r}")


def record_bad_token(
    report: DataModelInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
    reason: str,
) -> None:
    bad_token = BadToken(
        route_id=document.route_id,
        document=document.path,
        element=element_label(element),
        attr_name=attr_name,
        value=value,
        reason=reason,
    )
    report.bad_tokens.append(bad_token)
    report.errors.append(
        f"{document_label(document, report.repo_root)} {bad_token.element} "
        f"{attr_name} has malformed data-model token {value!r}: {reason}"
    )


def record_reference(
    report: DataModelInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
) -> None:
    reason = token_reason(attr_name, value)
    if reason is not None:
        record_bad_token(report, document, element, attr_name, value, reason)
        return

    stripped = value.strip()
    increment_category(report.stats, attr_name)
    report.references.append(
        DataModelReference(
            route_id=document.route_id,
            document=document.path,
            element=element_label(element),
            attr_name=attr_name,
            value=stripped,
            category=category_for_attr(attr_name),
        )
    )


def scan_document(document: RouteDocument, report: DataModelInventoryReport) -> None:
    try:
        root = ElementTree.parse(document.path).getroot()
    except ElementTree.ParseError as exc:
        report.errors.append(f"{document_label(document, report.repo_root)} is malformed RML: {exc}")
        return
    except OSError as exc:
        report.errors.append(f"{document_label(document, report.repo_root)} cannot be read: {exc}")
        return

    report.stats.documents_checked += 1
    for element in root.iter():
        for attr_name in (*MODEL_ATTRS, COMPONENT_ATTR, CONTROLLER_ATTR, ACTION_TYPE_ATTR, SLOT_ATTR):
            value = element.attrib.get(attr_name)
            if value is not None:
                record_reference(report, document, element, attr_name, value)


def build_data_model_inventory(
    data: dict[str, Any],
    repo_root: Path,
) -> DataModelInventoryReport:
    report = collect_manifest_documents(data, repo_root.resolve())
    for document in report.documents:
        scan_document(document, report)
    return report


def bad_token_payload(token: BadToken, repo_root: Path) -> dict[str, str]:
    return {
        "route": token.route_id,
        "document": display_path(token.document, repo_root),
        "element": token.element,
        "attribute": token.attr_name,
        "value": token.value,
        "reason": token.reason,
    }


def references_by_route(report: DataModelInventoryReport) -> dict[str, list[dict[str, str]]]:
    by_route: dict[str, list[dict[str, str]]] = defaultdict(list)
    for reference in report.references:
        by_route[reference.route_id].append(
            {
                "attribute": reference.attr_name,
                "value": reference.value,
                "category": reference.category,
            }
        )
    return {route_id: refs for route_id, refs in sorted(by_route.items())}


def json_report_payload(report: DataModelInventoryReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "route_count": report.stats.route_count,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "total_model_binding_refs": report.stats.total_model_binding_refs,
        "unique_model_tokens": {
            "count": len(report.unique_model_tokens),
            "tokens": report.unique_model_tokens,
        },
        "component_refs": report.stats.component_refs,
        "controller_refs": report.stats.controller_refs,
        "action_type_refs": report.stats.action_type_refs,
        "slot_refs": report.stats.slot_refs,
        "routes_with_data_model_hooks": {
            "count": len(report.routes_with_data_model_hooks),
            "routes": report.routes_with_data_model_hooks,
        },
        "malformed_tokens": [
            bad_token_payload(token, report.repo_root) for token in report.bad_tokens
        ],
        "references_by_route": references_by_route(report),
        "errors": report.errors,
    }


def print_json_report(report: DataModelInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def print_text_report(report: DataModelInventoryReport) -> None:
    print("RmlUi data-model inventory:")
    print(f"  Routes known: {report.stats.route_count}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Total model/data-binding refs: {report.stats.total_model_binding_refs}")
    print(f"  Unique model tokens: {len(report.unique_model_tokens)}")
    print(f"  Component refs: {report.stats.component_refs}")
    print(f"  Controller refs: {report.stats.controller_refs}")
    print(f"  Action-type refs: {report.stats.action_type_refs}")
    print(f"  Slot refs: {report.stats.slot_refs}")
    print(f"  Routes with data-model hooks: {len(report.routes_with_data_model_hooks)}")
    print(f"  Malformed tokens: {len(report.bad_tokens)}")
    print(f"  Route IDs with data-model hooks: {compact_list(report.routes_with_data_model_hooks)}")
    print(f"  Unique model tokens: {compact_list(report.unique_model_tokens)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi data-model inventory check failed.")
    else:
        print("\nResult: RmlUi data-model inventory check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_manifest.json"),
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve manifest document paths.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the text report.",
    )
    args = parser.parse_args(argv)

    try:
        data = load_manifest(args.manifest.resolve())
        report = build_data_model_inventory(data, args.repo_root.resolve())
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = DataModelInventoryReport(repo_root=args.repo_root.resolve())
            report.errors.append(f"Failed to validate data-model inventory: {exc}")
            print_json_report(report)
            return 1
        print(f"Failed to validate data-model inventory: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
