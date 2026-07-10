#!/usr/bin/env python3
"""Validate the static navigation graph declared by WORR RmlUi route documents."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from xml.etree import ElementTree


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
ROUTE_TARGET_ATTR = "data-route-target"
DEFAULT_GUARDED_ROOTS = ("main", "game", "download_status")
ROUTE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path


@dataclass(frozen=True)
class NavigationEdge:
    source: str
    target: str
    document: Path
    element: str


@dataclass(frozen=True)
class UnknownTarget:
    source: str
    target: str
    document: Path
    element: str
    reason: str


@dataclass
class NavigationGraphStats:
    routes_known: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    route_target_references: int = 0


@dataclass
class NavigationGraphReport:
    repo_root: Path
    roots: tuple[str, ...]
    route_ids: set[str] = field(default_factory=set)
    present_route_ids: set[str] = field(default_factory=set)
    documents: list[RouteDocument] = field(default_factory=list)
    edges: list[NavigationEdge] = field(default_factory=list)
    unknown_targets: list[UnknownTarget] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: NavigationGraphStats = field(default_factory=NavigationGraphStats)

    def ok(self) -> bool:
        return not self.errors

    @property
    def unique_edges(self) -> set[tuple[str, str]]:
        return {(edge.source, edge.target) for edge in self.edges}

    @property
    def dead_end_routes(self) -> list[str]:
        sources_with_edges = {source for source, _target in self.unique_edges}
        return sorted(self.present_route_ids - sources_with_edges)

    @property
    def unreachable_routes(self) -> list[str]:
        reachable = reachable_routes(self.route_ids, self.unique_edges, self.roots)
        return sorted(self.route_ids - reachable)


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


def collect_manifest_routes(
    data: dict[str, Any],
    repo_root: Path,
    roots: tuple[str, ...],
) -> NavigationGraphReport:
    report = NavigationGraphReport(repo_root=repo_root, roots=roots)

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
        report.route_ids.add(route_id)

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
        if resolved_document.is_file():
            report.present_route_ids.add(route_id)
            report.documents.append(RouteDocument(route_id=route_id, path=resolved_document))
        else:
            report.stats.documents_missing += 1

    report.stats.routes_known = len(report.route_ids)
    for root in roots:
        if root not in report.route_ids:
            report.errors.append(f"guarded root route {root!r} is missing from the manifest")
    return report


def record_route_target(
    value: str,
    element: ElementTree.Element,
    document: RouteDocument,
    report: NavigationGraphReport,
) -> None:
    report.stats.route_target_references += 1
    target = value.strip()
    if not target:
        unknown = UnknownTarget(
            source=document.route_id,
            target=target,
            document=document.path,
            element=element_label(element),
            reason="empty route target",
        )
        report.unknown_targets.append(unknown)
        report.errors.append(
            f"{document_label(document, report.repo_root)} {unknown.element} "
            f"{ROUTE_TARGET_ATTR} must not be empty"
        )
        return

    if not ROUTE_ID_RE.fullmatch(target):
        unknown = UnknownTarget(
            source=document.route_id,
            target=target,
            document=document.path,
            element=element_label(element),
            reason="not a static route token",
        )
        report.unknown_targets.append(unknown)
        report.errors.append(
            f"{document_label(document, report.repo_root)} {unknown.element} "
            f"{ROUTE_TARGET_ATTR} must be a static manifest route id: {target!r}"
        )
        return

    if target not in report.route_ids:
        unknown = UnknownTarget(
            source=document.route_id,
            target=target,
            document=document.path,
            element=element_label(element),
            reason="target not in manifest",
        )
        report.unknown_targets.append(unknown)
        report.errors.append(
            f"{document_label(document, report.repo_root)} {unknown.element} "
            f"{ROUTE_TARGET_ATTR} references unknown route {target!r}"
        )
        return

    report.edges.append(
        NavigationEdge(
            source=document.route_id,
            target=target,
            document=document.path,
            element=element_label(element),
        )
    )


def scan_document(document: RouteDocument, report: NavigationGraphReport) -> None:
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
        route_target = element.attrib.get(ROUTE_TARGET_ATTR)
        if route_target is not None:
            record_route_target(route_target, element, document, report)


def reachable_routes(
    route_ids: set[str],
    edges: set[tuple[str, str]],
    roots: tuple[str, ...],
) -> set[str]:
    adjacency: dict[str, set[str]] = defaultdict(set)
    for source, target in edges:
        adjacency[source].add(target)

    seen: set[str] = set()
    pending: deque[str] = deque(root for root in roots if root in route_ids)
    while pending:
        route_id = pending.popleft()
        if route_id in seen:
            continue
        seen.add(route_id)
        for target in sorted(adjacency.get(route_id, ())):
            if target not in seen:
                pending.append(target)
    return seen


def validate_navigation_graph(
    data: dict[str, Any],
    repo_root: Path,
    roots: tuple[str, ...] = DEFAULT_GUARDED_ROOTS,
) -> NavigationGraphReport:
    report = collect_manifest_routes(data, repo_root.resolve(), roots)
    seen_documents: set[Path] = set()
    for document in report.documents:
        resolved = document.path.resolve(strict=False)
        if resolved in seen_documents:
            continue
        seen_documents.add(resolved)
        scan_document(document, report)
    return report


def compact_route_list(route_ids: list[str], *, limit: int = 16) -> str:
    if not route_ids:
        return "-"
    if len(route_ids) <= limit:
        return ", ".join(route_ids)
    shown = ", ".join(route_ids[:limit])
    return f"{shown}, ... (+{len(route_ids) - limit} more)"


def unknown_target_payload(
    unknown: UnknownTarget,
    repo_root: Path,
) -> dict[str, str]:
    return {
        "source": unknown.source,
        "target": unknown.target,
        "document": display_path(unknown.document, repo_root),
        "element": unknown.element,
        "reason": unknown.reason,
    }


def json_report_payload(report: NavigationGraphReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "roots": list(report.roots),
        "route_count": report.stats.routes_known,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "route_target_references": report.stats.route_target_references,
        "edge_count": len(report.unique_edges),
        "unknown_targets": [
            unknown_target_payload(unknown, report.repo_root)
            for unknown in report.unknown_targets
        ],
        "dead_end_routes": report.dead_end_routes,
        "unreachable_routes": report.unreachable_routes,
        "errors": report.errors,
    }


def print_json_report(report: NavigationGraphReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def print_report(report: NavigationGraphReport) -> None:
    dead_ends = report.dead_end_routes
    unreachable = report.unreachable_routes

    print("RmlUi navigation graph:")
    print(f"  Routes known: {report.stats.routes_known}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Route-target references: {report.stats.route_target_references}")
    print(f"  Edges: {len(report.unique_edges)}")
    print(f"  Unknown targets: {len(report.unknown_targets)}")
    print(f"  Dead-end routes: {len(dead_ends)}")
    print(f"  Unreachable from guarded roots {list(report.roots)}: {len(unreachable)}")
    print(f"  Dead-end route IDs: {compact_route_list(dead_ends)}")
    print(f"  Unreachable route IDs: {compact_route_list(unreachable)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi navigation graph check passed.")


def parse_roots(value: str) -> tuple[str, ...]:
    roots = tuple(root.strip() for root in value.split(",") if root.strip())
    if not roots:
        raise argparse.ArgumentTypeError("at least one guarded root must be provided")
    return roots


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
        "--roots",
        type=parse_roots,
        default=DEFAULT_GUARDED_ROOTS,
        help=(
            "Comma-separated guarded root route IDs used for reachability. "
            "Defaults to main,game,download_status."
        ),
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
        report = validate_navigation_graph(data, args.repo_root.resolve(), args.roots)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = NavigationGraphReport(
                repo_root=args.repo_root.resolve(),
                roots=args.roots,
            )
            report.errors.append(f"Failed to validate navigation graph: {exc}")
            print_json_report(report)
            return 1
        print(f"Failed to validate navigation graph: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
