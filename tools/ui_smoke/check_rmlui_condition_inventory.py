#!/usr/bin/env python3
"""Inventory static condition hooks declared by WORR RmlUi route documents."""

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
CONDITION_ATTRS = (
    "data-show-if",
    "data-enable-if",
    "data-visible-if",
    "data-enabled-if",
    "data-condition",
)
TOKEN_RE = re.compile(r"^_?[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")
VALUE_RE = re.compile(r"^[A-Za-z0-9_.:+/-]+$")
SIMPLE_CONDITION_RE = re.compile(
    r"^\s*(?P<negate>!)?\s*(?P<token>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"(?:(?P<operator>==|!=|>=|<=|=|>|<)\s*"
    r"(?P<value>[A-Za-z0-9_.:+/-]+))?\s*$"
)


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path


@dataclass(frozen=True)
class ConditionReference:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    tokens: tuple[str, ...]
    status: str


@dataclass(frozen=True)
class ConditionProblem:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    reason: str


@dataclass
class ConditionInventoryStats:
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_condition_refs: int = 0
    malformed_condition_attrs: int = 0
    unsupported_condition_attrs: int = 0


@dataclass
class ConditionInventoryReport:
    repo_root: Path
    documents: list[RouteDocument] = field(default_factory=list)
    references: list[ConditionReference] = field(default_factory=list)
    problems: list[ConditionProblem] = field(default_factory=list)
    unsupported: list[ConditionProblem] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: ConditionInventoryStats = field(default_factory=ConditionInventoryStats)
    refs_by_attribute: dict[str, int] = field(
        default_factory=lambda: {attr_name: 0 for attr_name in CONDITION_ATTRS}
    )
    routes_with_condition_hooks: set[str] = field(default_factory=set)

    def ok(self) -> bool:
        return not self.errors

    @property
    def unique_condition_expressions(self) -> list[str]:
        return sorted({reference.value for reference in self.references if reference.value})

    @property
    def unique_condition_tokens(self) -> list[str]:
        return sorted(
            {
                token
                for reference in self.references
                for token in reference.tokens
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


def collect_manifest_documents(data: dict[str, Any], repo_root: Path) -> ConditionInventoryReport:
    report = ConditionInventoryReport(repo_root=repo_root)
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


def split_condition_expression(value: str) -> list[str]:
    return value.split(";")


def contains_dynamic_placeholder(value: str) -> bool:
    return "{{" in value or "}}" in value


def classify_condition_expression(value: str) -> tuple[str, tuple[str, ...], str | None]:
    stripped = value.strip()
    if not stripped:
        return "malformed", (), "empty condition expression"
    if contains_dynamic_placeholder(stripped):
        return "unsupported", (), "dynamic template condition expression"

    tokens: list[str] = []
    clauses = split_condition_expression(stripped)
    empty_clauses = [index + 1 for index, clause in enumerate(clauses) if not clause.strip()]
    if empty_clauses:
        indexes = ", ".join(str(index) for index in empty_clauses)
        return "malformed", (), f"empty condition clause(s): {indexes}"

    for clause in clauses:
        normalized = clause.strip()
        if any(marker in normalized for marker in ("&&", "||", "?", ":", "$")):
            return "unsupported", (), "unsupported compound or dynamic condition syntax"
        if any(quote in normalized for quote in ("'", '"')):
            return "unsupported", (), "unsupported quoted condition value"
        if "(" in normalized or ")" in normalized:
            return "unsupported", (), "unsupported parenthesized condition expression"

        match = SIMPLE_CONDITION_RE.fullmatch(normalized)
        if match is None:
            return "malformed", (), "expected token or token comparison expression"

        token = match.group("token")
        if not TOKEN_RE.fullmatch(token):
            return "malformed", (), "condition token must use lowercase snake_case style"

        compared_value = match.group("value")
        if compared_value is not None and not VALUE_RE.fullmatch(compared_value):
            return "malformed", (), "condition comparison value has unsupported characters"
        tokens.append(token)

    return "supported", tuple(tokens), None


def add_problem(
    report: ConditionInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
    reason: str,
) -> ConditionProblem:
    problem = ConditionProblem(
        route_id=document.route_id,
        document=document.path,
        element=element_label(element),
        attr_name=attr_name,
        value=value,
        reason=reason,
    )
    report.problems.append(problem)
    report.stats.malformed_condition_attrs += 1
    report.errors.append(
        f"{document_label(document, report.repo_root)} {problem.element} "
        f"{attr_name} is malformed: {reason}"
    )
    return problem


def add_unsupported(
    report: ConditionInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
    reason: str,
) -> ConditionProblem:
    unsupported = ConditionProblem(
        route_id=document.route_id,
        document=document.path,
        element=element_label(element),
        attr_name=attr_name,
        value=value,
        reason=reason,
    )
    report.unsupported.append(unsupported)
    report.stats.unsupported_condition_attrs += 1
    return unsupported


def record_condition_attr(
    report: ConditionInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
) -> None:
    stripped = value.strip()
    status, tokens, reason = classify_condition_expression(value)

    report.stats.total_condition_refs += 1
    report.refs_by_attribute[attr_name] += 1
    report.routes_with_condition_hooks.add(document.route_id)

    if status == "malformed":
        assert reason is not None
        add_problem(report, document, element, attr_name, value, reason)
    elif status == "unsupported":
        assert reason is not None
        add_unsupported(report, document, element, attr_name, value, reason)

    report.references.append(
        ConditionReference(
            route_id=document.route_id,
            document=document.path,
            element=element_label(element),
            attr_name=attr_name,
            value=stripped,
            tokens=tokens,
            status=status,
        )
    )


def scan_document(document: RouteDocument, report: ConditionInventoryReport) -> None:
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
        for attr_name in CONDITION_ATTRS:
            value = element.attrib.get(attr_name)
            if value is not None:
                record_condition_attr(report, document, element, attr_name, value)


def build_condition_inventory(
    data: dict[str, Any],
    repo_root: Path,
) -> ConditionInventoryReport:
    report = collect_manifest_documents(data, repo_root.resolve())
    for document in report.documents:
        scan_document(document, report)
    return report


def condition_problem_payload(problem: ConditionProblem, repo_root: Path) -> dict[str, str]:
    return {
        "route": problem.route_id,
        "document": display_path(problem.document, repo_root),
        "element": problem.element,
        "attribute": problem.attr_name,
        "value": problem.value,
        "reason": problem.reason,
    }


def references_by_route(report: ConditionInventoryReport) -> dict[str, list[dict[str, Any]]]:
    by_route: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for reference in report.references:
        by_route[reference.route_id].append(
            {
                "attribute": reference.attr_name,
                "value": reference.value,
                "status": reference.status,
                "tokens": list(reference.tokens),
            }
        )
    return {route_id: refs for route_id, refs in sorted(by_route.items())}


def json_report_payload(report: ConditionInventoryReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "route_count": report.stats.route_count,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "total_condition_refs": report.stats.total_condition_refs,
        "refs_by_attribute": dict(sorted(report.refs_by_attribute.items())),
        "routes_with_condition_hooks": {
            "count": len(report.routes_with_condition_hooks),
            "routes": sorted(report.routes_with_condition_hooks),
        },
        "unique_condition_expressions": {
            "count": len(report.unique_condition_expressions),
            "expressions": report.unique_condition_expressions,
        },
        "unique_condition_tokens": {
            "count": len(report.unique_condition_tokens),
            "tokens": report.unique_condition_tokens,
        },
        "malformed_condition_attributes": [
            condition_problem_payload(problem, report.repo_root) for problem in report.problems
        ],
        "unsupported_condition_attributes": [
            condition_problem_payload(problem, report.repo_root) for problem in report.unsupported
        ],
        "references_by_route": references_by_route(report),
        "errors": report.errors,
    }


def print_json_report(report: ConditionInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def print_text_report(report: ConditionInventoryReport) -> None:
    print("RmlUi condition inventory:")
    print(f"  Routes known: {report.stats.route_count}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Total condition refs: {report.stats.total_condition_refs}")
    print("  Refs by attribute:")
    for attr_name in CONDITION_ATTRS:
        print(f"    {attr_name}: {report.refs_by_attribute[attr_name]}")
    print(f"  Routes with condition hooks: {len(report.routes_with_condition_hooks)}")
    print(f"  Unique condition expressions: {len(report.unique_condition_expressions)}")
    print(f"  Unique condition tokens/cvars: {len(report.unique_condition_tokens)}")
    print(f"  Unsupported non-static conditions: {len(report.unsupported)}")
    print(f"  Malformed/empty condition attributes: {len(report.problems)}")
    print(
        "  Route IDs with condition hooks: "
        f"{compact_list(sorted(report.routes_with_condition_hooks))}"
    )
    print(f"  Condition tokens/cvars: {compact_list(report.unique_condition_tokens)}")

    if report.unsupported:
        print("\nUnsupported non-static conditions:")
        for problem in report.unsupported:
            print(
                "  - "
                f"{display_path(problem.document, report.repo_root)} "
                f"{problem.element} {problem.attr_name}={problem.value!r}: {problem.reason}"
            )

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi condition inventory check failed.")
    else:
        print("\nResult: RmlUi condition inventory check passed.")


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
        report = build_condition_inventory(data, args.repo_root.resolve())
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = ConditionInventoryReport(repo_root=args.repo_root.resolve())
            report.errors.append(f"Failed to validate condition inventory: {exc}")
            print_json_report(report)
            return 1
        print(f"Failed to validate condition inventory: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
