#!/usr/bin/env python3
"""Inventory static accessibility and localization hooks in WORR RmlUi route documents."""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from xml.etree import ElementTree


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
LOCALIZATION_ATTRS = (
    "data-l10n",
    "data-l10n-key",
    "data-loc",
    "data-loc-key",
    "data-localization-key",
    "data-localisation-key",
)
A11Y_ATTRS = (
    "aria-label",
    "aria-labelledby",
    "aria-describedby",
    "role",
    "tabindex",
    "accesskey",
)
HOOK_ATTRS = (*LOCALIZATION_ATTRS, *A11Y_ATTRS)


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path


@dataclass(frozen=True)
class A11yReference:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    category: str
    status: str


@dataclass(frozen=True)
class HookProblem:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    reason: str


@dataclass
class A11yInventoryStats:
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_hook_refs: int = 0
    malformed_hooks: int = 0


@dataclass
class A11yInventoryReport:
    repo_root: Path
    documents: list[RouteDocument] = field(default_factory=list)
    references: list[A11yReference] = field(default_factory=list)
    problems: list[HookProblem] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: A11yInventoryStats = field(default_factory=A11yInventoryStats)
    refs_by_attribute: dict[str, int] = field(
        default_factory=lambda: {attr_name: 0 for attr_name in HOOK_ATTRS}
    )
    routes_with_hooks: set[str] = field(default_factory=set)

    def ok(self) -> bool:
        return not self.errors

    @property
    def unique_localization_keys(self) -> list[str]:
        return sorted(
            {
                reference.value
                for reference in self.references
                if reference.attr_name in LOCALIZATION_ATTRS and reference.value
            }
        )

    @property
    def unique_roles(self) -> list[str]:
        roles: set[str] = set()
        for reference in self.references:
            if reference.attr_name == "role" and reference.value:
                roles.update(reference.value.split())
        return sorted(roles)


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


def collect_manifest_documents(data: dict[str, Any], repo_root: Path) -> A11yInventoryReport:
    report = A11yInventoryReport(repo_root=repo_root)
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


def category_for_attr(attr_name: str) -> str:
    if attr_name in LOCALIZATION_ATTRS:
        return "localization"
    if attr_name.startswith("aria-"):
        return "aria"
    if attr_name == "role":
        return "role"
    if attr_name == "tabindex":
        return "tabindex"
    if attr_name == "accesskey":
        return "accesskey"
    raise ValueError(f"unsupported a11y/localization inventory attribute {attr_name!r}")


def hook_problem_reason(attr_name: str, value: str) -> str | None:
    stripped = value.strip()
    if not stripped:
        return "hook value must not be empty"
    if attr_name == "tabindex":
        try:
            int(stripped)
        except ValueError:
            return "tabindex must be an integer string"
    return None


def record_problem(
    report: A11yInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
    reason: str,
) -> None:
    problem = HookProblem(
        route_id=document.route_id,
        document=document.path,
        element=element_label(element),
        attr_name=attr_name,
        value=value,
        reason=reason,
    )
    report.problems.append(problem)
    report.stats.malformed_hooks += 1
    report.errors.append(
        f"{document_label(document, report.repo_root)} {problem.element} "
        f"{attr_name} is malformed: {reason}"
    )


def record_reference(
    report: A11yInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
) -> None:
    stripped = value.strip()
    reason = hook_problem_reason(attr_name, value)
    status = "malformed" if reason is not None else "valid"

    report.stats.total_hook_refs += 1
    report.refs_by_attribute[attr_name] += 1
    report.routes_with_hooks.add(document.route_id)
    if reason is not None:
        record_problem(report, document, element, attr_name, value, reason)

    report.references.append(
        A11yReference(
            route_id=document.route_id,
            document=document.path,
            element=element_label(element),
            attr_name=attr_name,
            value=stripped,
            category=category_for_attr(attr_name),
            status=status,
        )
    )


def scan_document(document: RouteDocument, report: A11yInventoryReport) -> None:
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
        for attr_name in HOOK_ATTRS:
            value = element.attrib.get(attr_name)
            if value is not None:
                record_reference(report, document, element, attr_name, value)


def build_a11y_inventory(
    data: dict[str, Any],
    repo_root: Path,
) -> A11yInventoryReport:
    report = collect_manifest_documents(data, repo_root.resolve())
    for document in report.documents:
        scan_document(document, report)
    return report


def hook_problem_payload(problem: HookProblem, repo_root: Path) -> dict[str, str]:
    return {
        "route": problem.route_id,
        "document": display_path(problem.document, repo_root),
        "element": problem.element,
        "attribute": problem.attr_name,
        "value": problem.value,
        "reason": problem.reason,
    }


def references_by_route(report: A11yInventoryReport) -> dict[str, list[dict[str, str]]]:
    by_route: dict[str, list[dict[str, str]]] = defaultdict(list)
    for reference in report.references:
        by_route[reference.route_id].append(
            {
                "attribute": reference.attr_name,
                "value": reference.value,
                "category": reference.category,
                "status": reference.status,
            }
        )
    return {route_id: refs for route_id, refs in sorted(by_route.items())}


def json_report_payload(report: A11yInventoryReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "route_count": report.stats.route_count,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "total_hook_refs": report.stats.total_hook_refs,
        "refs_by_attribute": dict(sorted(report.refs_by_attribute.items())),
        "routes_with_a11y_localization_hooks": {
            "count": len(report.routes_with_hooks),
            "routes": sorted(report.routes_with_hooks),
        },
        "unique_localization_keys": {
            "count": len(report.unique_localization_keys),
            "keys": report.unique_localization_keys,
        },
        "unique_roles": {
            "count": len(report.unique_roles),
            "roles": report.unique_roles,
        },
        "malformed_hooks": [
            hook_problem_payload(problem, report.repo_root) for problem in report.problems
        ],
        "references_by_route": references_by_route(report),
        "errors": report.errors,
    }


def print_json_report(report: A11yInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def print_text_report(report: A11yInventoryReport) -> None:
    print("RmlUi accessibility/localization inventory:")
    print(f"  Routes known: {report.stats.route_count}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Total a11y/localization refs: {report.stats.total_hook_refs}")
    print("  Refs by attribute:")
    for attr_name in HOOK_ATTRS:
        print(f"    {attr_name}: {report.refs_by_attribute[attr_name]}")
    print(f"  Routes with a11y/localization hooks: {len(report.routes_with_hooks)}")
    print(f"  Unique localization keys: {len(report.unique_localization_keys)}")
    print(f"  Unique roles: {len(report.unique_roles)}")
    print(f"  Malformed/empty hooks: {len(report.problems)}")
    print(f"  Route IDs with hooks: {compact_list(sorted(report.routes_with_hooks))}")
    print(f"  Localization keys: {compact_list(report.unique_localization_keys)}")
    print(f"  Roles: {compact_list(report.unique_roles)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi accessibility/localization inventory check failed.")
    else:
        print("\nResult: RmlUi accessibility/localization inventory check passed.")


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
        report = build_a11y_inventory(data, args.repo_root.resolve())
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = A11yInventoryReport(repo_root=args.repo_root.resolve())
            report.errors.append(
                f"Failed to validate accessibility/localization inventory: {exc}"
            )
            print_json_report(report)
            return 1
        print(f"Failed to validate accessibility/localization inventory: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
