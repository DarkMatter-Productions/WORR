#!/usr/bin/env python3
"""Validate static semantics in authored WORR RmlUi documents."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable
from xml.etree import ElementTree


ROUTE_TARGET_ATTR = "data-route-target"
COMMAND_ATTR = "data-command"
DIRECT_CVAR_ATTRS = (
    "data-cvar",
    "data-bind-cvar",
    "data-label-cvar",
    "data-command-cvar",
)
EXPRESSION_CVAR_ATTRS = (
    "data-enable-if",
    "data-show-if",
    "data-visible-if",
)

ROUTE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
CVAR_RE = re.compile(r"^_?[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")
COMPARISON_RE = re.compile(r"^\s*!?\s*([A-Za-z][A-Za-z0-9_]*)\s*(?:==|!=|>=|<=|=|>|<)")
BARE_IDENTIFIER_RE = re.compile(r"^\s*!?\s*([A-Za-z][A-Za-z0-9_]*)\s*$")

# Documented skips for route targets that are not static manifest route IDs.
DYNAMIC_ROUTE_TARGET_RE = re.compile(r"^\{\{[^{}]+\}\}$")
EXTERNAL_ROUTE_TARGET_RE = re.compile(r"^(?:[a-z][a-z0-9+.-]*:|//|#)")


@dataclass
class RouteDocument:
    route_id: str
    path: Path


@dataclass
class SemanticsStats:
    documents_checked: int = 0
    route_targets_checked: int = 0
    route_targets_skipped: int = 0
    command_elements_checked: int = 0
    cvar_references_checked: int = 0


@dataclass
class SemanticsReport:
    route_ids: set[str] = field(default_factory=set)
    documents: list[RouteDocument] = field(default_factory=list)
    stats: SemanticsStats = field(default_factory=SemanticsStats)
    errors: list[str] = field(default_factory=list)

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


def is_dynamic_value(value: str) -> bool:
    stripped = value.strip()
    return bool(DYNAMIC_ROUTE_TARGET_RE.fullmatch(stripped)) or "{{" in stripped or "}}" in stripped


def is_external_route_target(value: str) -> bool:
    return bool(EXTERNAL_ROUTE_TARGET_RE.match(value.strip()))


def is_static_route_target(value: str) -> bool:
    return bool(ROUTE_ID_RE.fullmatch(value.strip()))


def is_cvar_token(value: str) -> bool:
    return bool(CVAR_RE.fullmatch(value.strip()))


def expression_cvar_tokens(value: str) -> Iterable[str]:
    if "{{" in value or "}}" in value:
        return

    for clause in re.split(r";|&&|\|\|", value):
        stripped = clause.strip()
        if not stripped or any(quote in stripped for quote in ("'", '"')):
            continue

        stripped = stripped.strip("() ")
        comparison = COMPARISON_RE.match(stripped)
        if comparison:
            yield comparison.group(1)
            continue

        bare = BARE_IDENTIFIER_RE.fullmatch(stripped)
        if bare:
            yield bare.group(1)


def collect_manifest_documents(data: dict[str, Any], repo_root: Path, report: SemanticsReport) -> None:
    routes = data.get("routes")
    if not isinstance(routes, list):
        report.errors.append("manifest field 'routes' must be a list")
        return

    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            report.errors.append(f"route at index {index} must be an object")
            continue

        route_id = route.get("id")
        document = route.get("document")
        if not isinstance(route_id, str) or not route_id:
            report.errors.append(f"route at index {index} is missing a non-empty id")
            continue
        report.route_ids.add(route_id)

        if not isinstance(document, str) or not document:
            report.errors.append(f"route {route_id!r} is missing a non-empty document path")
            continue
        if Path(document).is_absolute():
            report.errors.append(f"route {route_id!r} document path must be repo-relative: {document}")
            continue

        document_path = (repo_root / document).resolve(strict=False)
        if not is_within_repo(document_path, repo_root):
            report.errors.append(f"route {route_id!r} document path escapes repo: {document}")
            continue
        if document_path.is_file():
            report.documents.append(
                RouteDocument(route_id=route_id, path=document_path)
            )


def validate_route_target(
    value: str,
    element: ElementTree.Element,
    document: RouteDocument,
    repo_root: Path,
    route_ids: set[str],
    report: SemanticsReport,
) -> None:
    stripped = value.strip()
    if not stripped:
        report.errors.append(
            f"{document_label(document, repo_root)} {element_label(element)} "
            f"{ROUTE_TARGET_ATTR} must not be empty"
        )
        return

    if is_static_route_target(stripped):
        report.stats.route_targets_checked += 1
        if stripped not in route_ids:
            report.errors.append(
                f"{document_label(document, repo_root)} {element_label(element)} "
                f"{ROUTE_TARGET_ATTR} references unknown route {stripped!r}"
            )
        return

    if is_dynamic_value(stripped) or is_external_route_target(stripped):
        report.stats.route_targets_skipped += 1
        return

    report.errors.append(
        f"{document_label(document, repo_root)} {element_label(element)} "
        f"{ROUTE_TARGET_ATTR} is neither a static route token nor an allowed "
        f"dynamic/external target: {stripped!r}"
    )


def validate_command_element(
    value: str,
    element: ElementTree.Element,
    document: RouteDocument,
    repo_root: Path,
    report: SemanticsReport,
) -> None:
    report.stats.command_elements_checked += 1
    if not value.strip():
        report.errors.append(
            f"{document_label(document, repo_root)} {element_label(element)} "
            f"{COMMAND_ATTR} must not be empty"
        )
    if not element.attrib.get("id", "").strip():
        report.errors.append(
            f"{document_label(document, repo_root)} {element_label(element)} "
            f"uses {COMMAND_ATTR} but is missing a non-empty id"
        )


def validate_cvar_token(
    attr_name: str,
    token: str,
    element: ElementTree.Element,
    document: RouteDocument,
    repo_root: Path,
    report: SemanticsReport,
) -> None:
    report.stats.cvar_references_checked += 1
    if not is_cvar_token(token):
        report.errors.append(
            f"{document_label(document, repo_root)} {element_label(element)} "
            f"{attr_name} must use a lowercase snake_case-ish cvar token: {token!r}"
        )


def validate_direct_cvar_attr(
    attr_name: str,
    value: str,
    element: ElementTree.Element,
    document: RouteDocument,
    repo_root: Path,
    report: SemanticsReport,
) -> None:
    stripped = value.strip()
    if not stripped:
        report.errors.append(
            f"{document_label(document, repo_root)} {element_label(element)} "
            f"{attr_name} must not be empty"
        )
        return
    if "{{" in stripped or "}}" in stripped:
        return
    validate_cvar_token(attr_name, stripped, element, document, repo_root, report)


def validate_expression_cvar_attr(
    attr_name: str,
    value: str,
    element: ElementTree.Element,
    document: RouteDocument,
    repo_root: Path,
    report: SemanticsReport,
) -> None:
    for token in expression_cvar_tokens(value):
        validate_cvar_token(attr_name, token, element, document, repo_root, report)


def validate_document(
    document: RouteDocument,
    repo_root: Path,
    route_ids: set[str],
    report: SemanticsReport,
) -> None:
    try:
        root = ElementTree.parse(document.path).getroot()
    except ElementTree.ParseError as exc:
        report.errors.append(f"{document_label(document, repo_root)} is malformed RML: {exc}")
        return
    except OSError as exc:
        report.errors.append(f"{document_label(document, repo_root)} cannot be read: {exc}")
        return

    report.stats.documents_checked += 1
    for element in root.iter():
        route_target = element.attrib.get(ROUTE_TARGET_ATTR)
        if route_target is not None:
            validate_route_target(route_target, element, document, repo_root, route_ids, report)

        command = element.attrib.get(COMMAND_ATTR)
        if command is not None:
            validate_command_element(command, element, document, repo_root, report)

        for attr_name in DIRECT_CVAR_ATTRS:
            value = element.attrib.get(attr_name)
            if value is not None:
                validate_direct_cvar_attr(attr_name, value, element, document, repo_root, report)

        for attr_name in EXPRESSION_CVAR_ATTRS:
            value = element.attrib.get(attr_name)
            if value is not None:
                validate_expression_cvar_attr(attr_name, value, element, document, repo_root, report)


def validate_manifest_semantics(data: dict[str, Any], repo_root: Path) -> SemanticsReport:
    report = SemanticsReport()
    repo_root = repo_root.resolve()
    collect_manifest_documents(data, repo_root, report)

    seen_documents: set[Path] = set()
    for document in report.documents:
        resolved = document.path.resolve(strict=False)
        if resolved in seen_documents:
            continue
        seen_documents.add(resolved)
        validate_document(document, repo_root, report.route_ids, report)

    return report


def print_report(report: SemanticsReport) -> None:
    print("RML semantics:")
    print(f"  Routes known: {len(report.route_ids)}")
    print(f"  Documents checked: {report.stats.documents_checked}")
    print(
        f"  Route targets checked: {report.stats.route_targets_checked}"
        f" (skipped dynamic/external: {report.stats.route_targets_skipped})"
    )
    print(f"  Command elements checked: {report.stats.command_elements_checked}")
    print(f"  Cvar references checked: {report.stats.cvar_references_checked}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi semantics check passed.")


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
    args = parser.parse_args(argv)

    try:
        data = load_manifest(args.manifest.resolve())
        report = validate_manifest_semantics(data, args.repo_root.resolve())
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Failed to load manifest {args.manifest}: {exc}", file=sys.stderr)
        return 1

    print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
