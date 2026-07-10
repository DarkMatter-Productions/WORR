#!/usr/bin/env python3
"""Inventory static cvar references declared by WORR RmlUi route documents."""

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
DIRECT_CVAR_ATTRS = ("data-cvar", "data-bind-cvar")
LABEL_CVAR_ATTR = "data-label-cvar"
COMMAND_CVAR_ATTR = "data-command-cvar"
CONDITION_CVAR_ATTRS = ("data-enable-if", "data-show-if", "data-visible-if")
CVAR_RE = re.compile(r"^_?[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")
CONDITION_TOKEN_RE = re.compile(
    r"^\s*!?\s*\(?\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)?\s*"
    r"(?:(?:==|!=|>=|<=|=|>|<).*)?\s*$"
)


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path


@dataclass(frozen=True)
class CvarReference:
    route_id: str
    document: Path
    element: str
    attr_name: str
    token: str
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
class CvarInventoryStats:
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    direct_cvar_refs: int = 0
    label_cvar_refs: int = 0
    command_cvar_refs: int = 0
    condition_cvar_refs: int = 0
    dynamic_values_skipped: int = 0


@dataclass
class CvarInventoryReport:
    repo_root: Path
    documents: list[RouteDocument] = field(default_factory=list)
    references: list[CvarReference] = field(default_factory=list)
    bad_tokens: list[BadToken] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: CvarInventoryStats = field(default_factory=CvarInventoryStats)

    def ok(self) -> bool:
        return not self.errors

    @property
    def unique_cvars(self) -> list[str]:
        return sorted({reference.token for reference in self.references})

    @property
    def routes_with_cvar_hooks(self) -> list[str]:
        return sorted({reference.route_id for reference in self.references})


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


def is_cvar_token(value: str) -> bool:
    return bool(CVAR_RE.fullmatch(value.strip()))


def is_dynamic_value(value: str) -> bool:
    return "{{" in value or "}}" in value


def collect_manifest_documents(data: dict[str, Any], repo_root: Path) -> CvarInventoryReport:
    report = CvarInventoryReport(repo_root=repo_root)
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


def increment_category(stats: CvarInventoryStats, category: str) -> None:
    if category == "direct":
        stats.direct_cvar_refs += 1
    elif category == "label":
        stats.label_cvar_refs += 1
    elif category == "command":
        stats.command_cvar_refs += 1
    elif category == "condition":
        stats.condition_cvar_refs += 1
    else:
        raise ValueError(f"unknown cvar reference category {category!r}")


def record_bad_token(
    report: CvarInventoryReport,
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
        f"{attr_name} has unsupported cvar token {value!r}: {reason}"
    )


def record_reference(
    report: CvarInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    token: str,
    category: str,
) -> None:
    if not is_cvar_token(token):
        record_bad_token(
            report,
            document,
            element,
            attr_name,
            token,
            "expected lowercase snake_case token with digits allowed",
        )
        return

    increment_category(report.stats, category)
    report.references.append(
        CvarReference(
            route_id=document.route_id,
            document=document.path,
            element=element_label(element),
            attr_name=attr_name,
            token=token.strip(),
            category=category,
        )
    )


def collect_direct_attr(
    report: CvarInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
    category: str,
) -> None:
    value = element.attrib.get(attr_name)
    if value is None:
        return

    stripped = value.strip()
    if not stripped:
        record_bad_token(report, document, element, attr_name, value, "empty cvar token")
        return
    if is_dynamic_value(stripped):
        report.stats.dynamic_values_skipped += 1
        return

    record_reference(report, document, element, attr_name, stripped, category)


def condition_token_candidates(value: str) -> list[str]:
    tokens: list[str] = []
    for clause in re.split(r";|&&|\|\|", value):
        stripped = clause.strip()
        if not stripped or is_dynamic_value(stripped):
            continue
        if any(quote in stripped for quote in ("'", '"')):
            continue

        stripped = stripped.strip("() ")
        match = CONDITION_TOKEN_RE.match(stripped)
        if match:
            tokens.append(match.group(1))
    return tokens


def collect_condition_attr(
    report: CvarInventoryReport,
    document: RouteDocument,
    element: ElementTree.Element,
    attr_name: str,
) -> None:
    value = element.attrib.get(attr_name)
    if value is None:
        return

    stripped = value.strip()
    if not stripped:
        record_bad_token(report, document, element, attr_name, value, "empty condition expression")
        return
    if is_dynamic_value(stripped):
        report.stats.dynamic_values_skipped += 1
        return

    tokens = condition_token_candidates(stripped)
    if not tokens:
        record_bad_token(
            report,
            document,
            element,
            attr_name,
            stripped,
            "no conservative cvar token could be extracted",
        )
        return

    for token in tokens:
        record_reference(report, document, element, attr_name, token, "condition")


def scan_document(document: RouteDocument, report: CvarInventoryReport) -> None:
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
        for attr_name in DIRECT_CVAR_ATTRS:
            collect_direct_attr(report, document, element, attr_name, "direct")
        collect_direct_attr(report, document, element, LABEL_CVAR_ATTR, "label")
        collect_direct_attr(report, document, element, COMMAND_CVAR_ATTR, "command")
        for attr_name in CONDITION_CVAR_ATTRS:
            collect_condition_attr(report, document, element, attr_name)


def build_cvar_inventory(data: dict[str, Any], repo_root: Path) -> CvarInventoryReport:
    report = collect_manifest_documents(data, repo_root.resolve())
    seen_documents: set[Path] = set()
    for document in report.documents:
        resolved = document.path.resolve(strict=False)
        if resolved in seen_documents:
            continue
        seen_documents.add(resolved)
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


def cvars_by_route(report: CvarInventoryReport) -> dict[str, list[str]]:
    by_route: dict[str, set[str]] = defaultdict(set)
    for reference in report.references:
        by_route[reference.route_id].add(reference.token)
    return {route_id: sorted(tokens) for route_id, tokens in sorted(by_route.items())}


def json_report_payload(report: CvarInventoryReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "route_count": report.stats.route_count,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "references": {
            "direct": report.stats.direct_cvar_refs,
            "label": report.stats.label_cvar_refs,
            "command": report.stats.command_cvar_refs,
            "condition": report.stats.condition_cvar_refs,
            "total": len(report.references),
        },
        "unique_cvars": {
            "count": len(report.unique_cvars),
            "tokens": report.unique_cvars,
        },
        "routes_with_cvar_hooks": {
            "count": len(report.routes_with_cvar_hooks),
            "routes": report.routes_with_cvar_hooks,
        },
        "dynamic_values_skipped": report.stats.dynamic_values_skipped,
        "unknown_or_bad_tokens": [
            bad_token_payload(token, report.repo_root) for token in report.bad_tokens
        ],
        "cvars_by_route": cvars_by_route(report),
        "errors": report.errors,
    }


def print_json_report(report: CvarInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def print_text_report(report: CvarInventoryReport) -> None:
    print("RmlUi cvar inventory:")
    print(f"  Routes known: {report.stats.route_count}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Direct cvar refs: {report.stats.direct_cvar_refs}")
    print(f"  Label cvar refs: {report.stats.label_cvar_refs}")
    print(f"  Command cvar refs: {report.stats.command_cvar_refs}")
    print(f"  Condition cvar refs: {report.stats.condition_cvar_refs}")
    print(f"  Unique cvars: {len(report.unique_cvars)}")
    print(f"  Routes with cvar hooks: {len(report.routes_with_cvar_hooks)}")
    print(f"  Dynamic values skipped: {report.stats.dynamic_values_skipped}")
    print(f"  Unknown/bad tokens: {len(report.bad_tokens)}")
    print(f"  Route IDs with cvar hooks: {compact_list(report.routes_with_cvar_hooks)}")
    print(f"  Unique cvar tokens: {compact_list(report.unique_cvars)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi cvar inventory check passed.")


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
        report = build_cvar_inventory(data, args.repo_root.resolve())
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = CvarInventoryReport(repo_root=args.repo_root.resolve())
            report.errors.append(f"Failed to validate cvar inventory: {exc}")
            print_json_report(report)
            return 1
        print(f"Failed to validate cvar inventory: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
