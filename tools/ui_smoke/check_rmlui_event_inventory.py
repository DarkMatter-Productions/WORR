#!/usr/bin/env python3
"""Inventory static interaction/event hooks declared by WORR RmlUi route documents."""

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
EVENT_ATTRS = ("data-event-click", "data-event-change")
COMMAND_ATTR = "data-command"
ROUTE_TARGET_ATTR = "data-route-target"
ACTION_TYPE_ATTR = "data-action-type"
BIND_COMMAND_ATTR = "data-bind-command"
COMMAND_CVAR_ATTR = "data-command-cvar"
TRACKED_ATTRS = (
    *EVENT_ATTRS,
    COMMAND_ATTR,
    ROUTE_TARGET_ATTR,
    ACTION_TYPE_ATTR,
    BIND_COMMAND_ATTR,
    COMMAND_CVAR_ATTR,
)


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path


@dataclass(frozen=True)
class EventActionReference:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    category: str
    tokens: tuple[str, ...]


@dataclass(frozen=True)
class EventAttributeProblem:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    reason: str


@dataclass
class EventInventoryStats:
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_event_action_refs: int = 0


@dataclass
class EventInventoryReport:
    repo_root: Path
    documents: list[RouteDocument] = field(default_factory=list)
    references: list[EventActionReference] = field(default_factory=list)
    problems: list[EventAttributeProblem] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: EventInventoryStats = field(default_factory=EventInventoryStats)
    refs_by_attribute: dict[str, int] = field(
        default_factory=lambda: {attr_name: 0 for attr_name in TRACKED_ATTRS}
    )
    routes_with_event_hooks: set[str] = field(default_factory=set)

    def ok(self) -> bool:
        return not self.errors

    @property
    def unique_event_tokens(self) -> list[str]:
        return sorted(
            {
                reference.value
                for reference in self.references
                if reference.attr_name in EVENT_ATTRS and reference.value
            }
        )

    @property
    def unique_action_tokens(self) -> list[str]:
        return sorted(
            {
                reference.value
                for reference in self.references
                if reference.attr_name == ACTION_TYPE_ATTR and reference.value
            }
        )

    @property
    def unique_route_target_tokens(self) -> list[str]:
        return sorted(
            {
                reference.value
                for reference in self.references
                if reference.attr_name == ROUTE_TARGET_ATTR and reference.value
            }
        )

    @property
    def unique_command_tokens(self) -> list[str]:
        return sorted(
            {
                token
                for reference in self.references
                if reference.attr_name == COMMAND_ATTR
                for token in reference.tokens
            }
        )

    @property
    def unique_bind_command_refs(self) -> list[str]:
        return sorted(
            {
                reference.value
                for reference in self.references
                if reference.attr_name == BIND_COMMAND_ATTR and reference.value
            }
        )

    @property
    def unique_command_cvars(self) -> list[str]:
        return sorted(
            {
                reference.value
                for reference in self.references
                if reference.attr_name == COMMAND_CVAR_ATTR and reference.value
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


def collect_manifest_documents(data: dict[str, Any], repo_root: Path) -> EventInventoryReport:
    report = EventInventoryReport(repo_root=repo_root)
    schema = data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    routes = data.get("routes")
    if not isinstance(routes, list):
        report.errors.append("manifest field 'routes' must be a list")
        return report

    report.stats.route_count = len(routes)
    seen_route_ids: set[str] = set()
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
        if route_id in seen_route_ids:
            report.errors.append(f"{label} duplicates a previous route id")
            continue
        seen_route_ids.add(route_id)
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


def split_command_chain(value: str) -> list[str]:
    segments: list[str] = []
    current: list[str] = []
    quote: str | None = None
    escaped = False

    for char in value:
        if escaped:
            current.append(char)
            escaped = False
            continue

        if quote is not None and char == "\\":
            current.append(char)
            escaped = True
            continue

        if char in ('"', "'"):
            if quote is None:
                quote = char
            elif quote == char:
                quote = None
            current.append(char)
            continue

        if char == ";" and quote is None:
            segments.append("".join(current))
            current = []
            continue

        current.append(char)

    segments.append("".join(current))
    return segments


def command_token(segment: str) -> str | None:
    stripped = segment.strip()
    if not stripped:
        return None
    return stripped.split(None, 1)[0]


def tokens_for_attr(attr_name: str, value: str) -> tuple[str, ...]:
    stripped = value.strip()
    if not stripped:
        return ()
    if attr_name == COMMAND_ATTR:
        return tuple(
            token
            for segment in split_command_chain(stripped)
            if (token := command_token(segment)) is not None
        )
    if attr_name == BIND_COMMAND_ATTR:
        token = command_token(stripped)
        return () if token is None else (token,)
    return (stripped,)


def category_for_attr(attr_name: str) -> str:
    if attr_name in EVENT_ATTRS:
        return "event"
    if attr_name == COMMAND_ATTR:
        return "command"
    if attr_name == ROUTE_TARGET_ATTR:
        return "route_target"
    if attr_name == ACTION_TYPE_ATTR:
        return "action_type"
    if attr_name == BIND_COMMAND_ATTR:
        return "bind_command"
    if attr_name == COMMAND_CVAR_ATTR:
        return "command_cvar"
    raise ValueError(f"unsupported event inventory attribute {attr_name!r}")


def add_problem(
    report: EventInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
    reason: str,
) -> None:
    problem = EventAttributeProblem(
        route_id=document.route_id,
        document=document.path,
        element=element_label(element),
        attr_name=attr_name,
        value=value,
        reason=reason,
    )
    report.problems.append(problem)
    report.errors.append(
        f"{document_label(document, report.repo_root)} {problem.element} "
        f"{attr_name} is malformed: {reason}"
    )


def record_reference(
    report: EventInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
) -> None:
    stripped = value.strip()
    report.stats.total_event_action_refs += 1
    report.refs_by_attribute[attr_name] += 1
    report.routes_with_event_hooks.add(document.route_id)

    if not stripped:
        add_problem(report, document, element, attr_name, value, "empty interaction attribute")

    report.references.append(
        EventActionReference(
            route_id=document.route_id,
            document=document.path,
            element=element_label(element),
            attr_name=attr_name,
            value=stripped,
            category=category_for_attr(attr_name),
            tokens=tokens_for_attr(attr_name, value),
        )
    )


def scan_document(document: RouteDocument, report: EventInventoryReport) -> None:
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
        for attr_name in TRACKED_ATTRS:
            value = element.attrib.get(attr_name)
            if value is not None:
                record_reference(report, document, element, attr_name, value)


def build_event_inventory(
    data: dict[str, Any],
    repo_root: Path,
) -> EventInventoryReport:
    report = collect_manifest_documents(data, repo_root.resolve())
    for document in report.documents:
        scan_document(document, report)
    return report


def problem_payload(problem: EventAttributeProblem, repo_root: Path) -> dict[str, str]:
    return {
        "route": problem.route_id,
        "document": display_path(problem.document, repo_root),
        "element": problem.element,
        "attribute": problem.attr_name,
        "value": problem.value,
        "reason": problem.reason,
    }


def references_by_route(report: EventInventoryReport) -> dict[str, list[dict[str, Any]]]:
    by_route: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for reference in report.references:
        by_route[reference.route_id].append(
            {
                "attribute": reference.attr_name,
                "value": reference.value,
                "category": reference.category,
                "tokens": list(reference.tokens),
            }
        )
    return {route_id: refs for route_id, refs in sorted(by_route.items())}


def json_report_payload(report: EventInventoryReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "route_count": report.stats.route_count,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "total_event_action_refs": report.stats.total_event_action_refs,
        "refs_by_attribute": dict(sorted(report.refs_by_attribute.items())),
        "routes_with_event_hooks": {
            "count": len(report.routes_with_event_hooks),
            "routes": sorted(report.routes_with_event_hooks),
        },
        "unique_event_tokens": {
            "count": len(report.unique_event_tokens),
            "tokens": report.unique_event_tokens,
        },
        "unique_action_tokens": {
            "count": len(report.unique_action_tokens),
            "tokens": report.unique_action_tokens,
        },
        "unique_route_target_tokens": {
            "count": len(report.unique_route_target_tokens),
            "tokens": report.unique_route_target_tokens,
        },
        "unique_command_tokens": {
            "count": len(report.unique_command_tokens),
            "tokens": report.unique_command_tokens,
        },
        "unique_bind_command_refs": {
            "count": len(report.unique_bind_command_refs),
            "refs": report.unique_bind_command_refs,
        },
        "unique_command_cvars": {
            "count": len(report.unique_command_cvars),
            "cvars": report.unique_command_cvars,
        },
        "malformed_event_attributes": [
            problem_payload(problem, report.repo_root) for problem in report.problems
        ],
        "references_by_route": references_by_route(report),
        "errors": report.errors,
    }


def print_json_report(report: EventInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def print_text_report(report: EventInventoryReport) -> None:
    print("RmlUi event/action inventory:")
    print(f"  Routes known: {report.stats.route_count}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Total event/action refs: {report.stats.total_event_action_refs}")
    print("  Refs by attribute:")
    for attr_name in TRACKED_ATTRS:
        print(f"    {attr_name}: {report.refs_by_attribute[attr_name]}")
    print(f"  Routes with event hooks: {len(report.routes_with_event_hooks)}")
    print(f"  Unique event tokens: {len(report.unique_event_tokens)}")
    print(f"  Unique action tokens: {len(report.unique_action_tokens)}")
    print(f"  Unique route-target tokens: {len(report.unique_route_target_tokens)}")
    print(f"  Unique command tokens: {len(report.unique_command_tokens)}")
    print(f"  Unique bind-command refs: {len(report.unique_bind_command_refs)}")
    print(f"  Unique command-cvar refs: {len(report.unique_command_cvars)}")
    print(f"  Malformed/empty event attributes: {len(report.problems)}")
    print(f"  Event tokens: {compact_list(report.unique_event_tokens)}")
    print(f"  Action tokens: {compact_list(report.unique_action_tokens)}")
    print(f"  Route-target tokens: {compact_list(report.unique_route_target_tokens)}")
    print(f"  Command tokens: {compact_list(report.unique_command_tokens)}")
    print(
        "  Route IDs with event hooks: "
        f"{compact_list(sorted(report.routes_with_event_hooks))}"
    )

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi event/action inventory check failed.")
    else:
        print("\nResult: RmlUi event/action inventory check passed.")


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
        report = build_event_inventory(data, args.repo_root.resolve())
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = EventInventoryReport(repo_root=args.repo_root.resolve())
            report.errors.append(f"Failed to validate event/action inventory: {exc}")
            print_json_report(report)
            return 1
        print(f"Failed to validate event/action inventory: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
