#!/usr/bin/env python3
"""Inventory static command hooks declared by WORR RmlUi route documents."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from xml.etree import ElementTree


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
COMMAND_ATTR = "data-command"
COMMAND_CVAR_ATTR = "data-command-cvar"
CVAR_RE = re.compile(r"^_?[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path


@dataclass(frozen=True)
class CommandAttributeProblem:
    route_id: str
    document: Path
    element: str
    attr_name: str
    value: str
    reasons: tuple[str, ...]


@dataclass
class CommandInventoryStats:
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    direct_command_refs: int = 0
    cvar_command_refs: int = 0


@dataclass
class CommandInventoryReport:
    repo_root: Path
    documents: list[RouteDocument] = field(default_factory=list)
    command_tokens: set[str] = field(default_factory=set)
    command_cvars: set[str] = field(default_factory=set)
    routes_with_command_hooks: set[str] = field(default_factory=set)
    problems: list[CommandAttributeProblem] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: CommandInventoryStats = field(default_factory=CommandInventoryStats)

    def ok(self) -> bool:
        return not self.errors


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


def document_label(document: RouteDocument, repo_root: Path) -> str:
    return f"route {document.route_id!r} {display_path(document.path, repo_root)}"


def route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"route {route_id!r}"
    return f"route at index {index}"


def collect_manifest_documents(data: dict[str, Any], repo_root: Path) -> CommandInventoryReport:
    report = CommandInventoryReport(repo_root=repo_root)

    schema = data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    routes = data.get("routes")
    if not isinstance(routes, list):
        report.errors.append("manifest field 'routes' must be a list")
        return report

    seen_route_ids: set[str] = set()
    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            report.errors.append(f"route at index {index} must be an object")
            continue

        label = route_label(route, index)
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            report.errors.append(f"{label} field 'id' must be a non-empty string")
            continue
        if route_id in seen_route_ids:
            report.errors.append(f"{label} duplicates a previous route id")
            continue
        seen_route_ids.add(route_id)

        document = route.get("document")
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
        if not resolved_document.is_file():
            report.stats.documents_missing += 1
            report.errors.append(f"{label} document file does not exist: {document}")
            continue

        report.documents.append(RouteDocument(route_id=route_id, path=resolved_document))

    report.stats.route_count = len(seen_route_ids)
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


def add_problem(
    report: CommandInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    value: str,
    reasons: list[str],
) -> None:
    problem = CommandAttributeProblem(
        route_id=document.route_id,
        document=document.path,
        element=element_label(element),
        attr_name=attr_name,
        value=value,
        reasons=tuple(reasons),
    )
    report.problems.append(problem)
    reason_text = "; ".join(reasons)
    report.errors.append(
        f"{document_label(document, report.repo_root)} {problem.element} "
        f"{attr_name} is malformed: {reason_text}"
    )


def record_direct_command(
    value: str,
    element: ElementTree.Element,
    document: RouteDocument,
    report: CommandInventoryReport,
) -> None:
    report.stats.direct_command_refs += 1
    report.routes_with_command_hooks.add(document.route_id)

    reasons: list[str] = []
    if not value.strip():
        reasons.append("empty command attribute")
    else:
        segments = split_command_chain(value)
        empty_segments = [index + 1 for index, segment in enumerate(segments) if not segment.strip()]
        if empty_segments:
            indexes = ", ".join(str(index) for index in empty_segments)
            reasons.append(f"empty command chain segment(s): {indexes}")

        for segment in segments:
            token = command_token(segment)
            if token is not None:
                report.command_tokens.add(token)

    if reasons:
        add_problem(report, document, element, COMMAND_ATTR, value, reasons)


def record_command_cvar(
    value: str,
    element: ElementTree.Element,
    document: RouteDocument,
    report: CommandInventoryReport,
) -> None:
    report.stats.cvar_command_refs += 1
    report.routes_with_command_hooks.add(document.route_id)

    stripped = value.strip()
    reasons: list[str] = []
    if not stripped:
        reasons.append("empty command-cvar attribute")
    elif not CVAR_RE.fullmatch(stripped):
        reasons.append("command-cvar must use lowercase snake_case cvar token style")
    else:
        report.command_cvars.add(stripped)

    if reasons:
        add_problem(report, document, element, COMMAND_CVAR_ATTR, value, reasons)


def scan_document(document: RouteDocument, report: CommandInventoryReport) -> None:
    try:
        root = ElementTree.parse(document.path).getroot()
    except ElementTree.ParseError as exc:
        report.errors.append(
            f"{document_label(document, report.repo_root)} is malformed RML: {exc}"
        )
        return
    except OSError as exc:
        report.errors.append(f"{document_label(document, report.repo_root)} cannot be read: {exc}")
        return

    report.stats.documents_checked += 1
    for element in root.iter():
        direct_command = element.attrib.get(COMMAND_ATTR)
        if direct_command is not None:
            record_direct_command(direct_command, element, document, report)

        command_cvar = element.attrib.get(COMMAND_CVAR_ATTR)
        if command_cvar is not None:
            record_command_cvar(command_cvar, element, document, report)


def validate_command_inventory(data: dict[str, Any], repo_root: Path) -> CommandInventoryReport:
    repo_root = repo_root.resolve()
    report = collect_manifest_documents(data, repo_root)
    for document in report.documents:
        scan_document(document, report)
    return report


def problem_payload(problem: CommandAttributeProblem, repo_root: Path) -> dict[str, Any]:
    return {
        "route": problem.route_id,
        "document": display_path(problem.document, repo_root),
        "element": problem.element,
        "attr": problem.attr_name,
        "value": problem.value,
        "reasons": list(problem.reasons),
    }


def json_report_payload(report: CommandInventoryReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "route_count": report.stats.route_count,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "direct_command_refs": report.stats.direct_command_refs,
        "cvar_command_refs": report.stats.cvar_command_refs,
        "unique_command_tokens": len(report.command_tokens),
        "unique_cvar_command_refs": len(report.command_cvars),
        "malformed_command_attributes": len(report.problems),
        "routes_with_command_hooks": sorted(report.routes_with_command_hooks),
        "command_tokens": sorted(report.command_tokens),
        "command_cvars": sorted(report.command_cvars),
        "problems": [
            problem_payload(problem, report.repo_root) for problem in report.problems
        ],
        "errors": report.errors,
    }


def print_json_report(report: CommandInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 18) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    shown = ", ".join(values[:limit])
    return f"{shown}, ... (+{len(values) - limit} more)"


def print_report(report: CommandInventoryReport) -> None:
    print("RmlUi command inventory:")
    print(f"  Routes known: {report.stats.route_count}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Direct command refs: {report.stats.direct_command_refs}")
    print(f"  Cvar-command refs: {report.stats.cvar_command_refs}")
    print(f"  Unique command tokens: {len(report.command_tokens)}")
    print(f"  Unique cvar-command refs: {len(report.command_cvars)}")
    print(f"  Malformed/empty command attributes: {len(report.problems)}")
    print(f"  Routes with command hooks: {len(report.routes_with_command_hooks)}")
    print(f"  Command tokens: {compact_list(sorted(report.command_tokens))}")
    print(f"  Command cvars: {compact_list(sorted(report.command_cvars))}")
    print(f"  Hooked route IDs: {compact_list(sorted(report.routes_with_command_hooks))}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi command inventory check failed.")
    else:
        print("\nResult: RmlUi command inventory check passed.")


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
        report = validate_command_inventory(data, args.repo_root.resolve())
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = CommandInventoryReport(repo_root=args.repo_root.resolve())
            report.errors.append(f"Failed to validate command inventory: {exc}")
            print_json_report(report)
            return 1
        print(f"Failed to validate command inventory: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
