#!/usr/bin/env python3
"""Validate the static RmlUi runtime route registry against the smoke manifest."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
SOURCE_ROOT = PurePosixPath("assets/ui/rml")
RUNTIME_ROOT = PurePosixPath("ui/rml")
DEFAULT_ALLOWED_EXTRA_ROUTES = ("core.runtime_smoke",)

ROUTE_TABLE_RE = re.compile(
    r"static\s+const\s+ui_rml_route_t\s+ui_rml_routes\[\]\s*=\s*\{(?P<body>.*?)\};",
    re.DOTALL,
)
ROUTE_ENTRY_RE = re.compile(
    r"\{\s*\"(?P<route_id>(?:\\.|[^\"\\])*)\"\s*,\s*"
    r"\"(?P<document>(?:\\.|[^\"\\])*)\"\s*\}\s*,?",
    re.DOTALL,
)
C_COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)


@dataclass(frozen=True)
class ManifestRoute:
    route_id: str
    document: str
    runtime_path: PurePosixPath


@dataclass(frozen=True)
class RegisteredRoute:
    route_id: str
    document: str
    runtime_path: PurePosixPath | None
    line: int


@dataclass
class RegistryStats:
    manifest_routes: int = 0
    registered_routes: int = 0
    missing: int = 0
    unexpected: int = 0
    duplicates: int = 0
    matched_runtime_paths: int = 0


@dataclass
class RegistryReport:
    stats: RegistryStats = field(default_factory=RegistryStats)
    manifest_routes: dict[str, ManifestRoute] = field(default_factory=dict)
    registered_routes: list[RegisteredRoute] = field(default_factory=list)
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


def load_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


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


def path_has_unsafe_segments(value: str) -> bool:
    return any(part in ("", ".", "..") for part in value.split("/"))


def manifest_route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"manifest route {route_id!r}"
    return f"manifest route at index {index}"


def runtime_path_from_manifest_document(
    value: Any,
    label: str,
    errors: list[str],
) -> PurePosixPath | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label} document path must be a non-empty string")
        return None
    if not is_relative_slash_path(value):
        errors.append(f"{label} document path must be repo-relative and use '/' separators: {value}")
        return None
    if path_has_unsafe_segments(value):
        errors.append(f"{label} document path must not contain empty, '.', or '..' segments: {value}")
        return None
    if not value.endswith(".rml"):
        errors.append(f"{label} document path must end with .rml: {value}")
        return None

    document_path = PurePosixPath(value)
    try:
        relative_document = document_path.relative_to(SOURCE_ROOT)
    except ValueError:
        errors.append(f"{label} document path must be under {SOURCE_ROOT.as_posix()}: {value}")
        return None

    return RUNTIME_ROOT / relative_document


def runtime_path_from_registered_document(
    value: str,
    label: str,
    errors: list[str],
) -> PurePosixPath | None:
    if not value:
        errors.append(f"{label} document path must be a non-empty string")
        return None
    if not is_relative_slash_path(value):
        errors.append(f"{label} document path must be relative and use '/' separators: {value}")
        return None
    if path_has_unsafe_segments(value):
        errors.append(f"{label} document path must not contain empty, '.', or '..' segments: {value}")
        return None
    if not value.endswith(".rml"):
        errors.append(f"{label} document path must end with .rml: {value}")
        return None

    runtime_path = RUNTIME_ROOT / PurePosixPath(value)
    try:
        runtime_path.relative_to(RUNTIME_ROOT)
    except ValueError:
        errors.append(
            f"{label} document path escapes {RUNTIME_ROOT.as_posix()} when prefixed: {value}"
        )
        return None
    return runtime_path


def manifest_routes_from_data(data: dict[str, Any], errors: list[str]) -> dict[str, ManifestRoute]:
    schema = data.get("schema")
    if schema != EXPECTED_SCHEMA:
        errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append("manifest field 'routes' must be a list")
        return {}

    manifest_routes: dict[str, ManifestRoute] = {}
    duplicate_route_ids: set[str] = set()
    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            errors.append(f"manifest route at index {index} must be an object")
            continue

        label = manifest_route_label(route, index)
        route_id_value = route.get("id")
        if not isinstance(route_id_value, str) or not route_id_value:
            errors.append(f"{label} field 'id' must be a non-empty string")
            continue

        runtime_path = runtime_path_from_manifest_document(route.get("document"), label, errors)
        if runtime_path is None:
            continue

        if route_id_value in manifest_routes:
            duplicate_route_ids.add(route_id_value)
            continue

        manifest_routes[route_id_value] = ManifestRoute(
            route_id=route_id_value,
            document=str(route.get("document")),
            runtime_path=runtime_path,
        )

    for route_id in sorted(duplicate_route_ids):
        errors.append(f"duplicate manifest route id {route_id!r}")

    return manifest_routes


def strip_c_comments_keep_newlines(value: str) -> str:
    def replace(match: re.Match[str]) -> str:
        return "".join("\n" if char == "\n" else " " for char in match.group(0))

    return C_COMMENT_RE.sub(replace, value)


def compact_unparsed_table_body(value: str) -> str:
    compact = " ".join(value.split())
    if len(compact) <= 120:
        return compact
    return compact[:117] + "..."


def parse_registered_routes(cpp_text: str, errors: list[str]) -> list[RegisteredRoute]:
    table_match = ROUTE_TABLE_RE.search(cpp_text)
    if table_match is None:
        errors.append("could not find static ui_rml_routes table")
        return []

    table_body = table_match.group("body")
    body_offset = table_match.start("body")
    stripped_body = strip_c_comments_keep_newlines(table_body)
    consumed = list(stripped_body)
    registered_routes: list[RegisteredRoute] = []

    for entry_match in ROUTE_ENTRY_RE.finditer(stripped_body):
        route_id = entry_match.group("route_id")
        document = entry_match.group("document")
        line = cpp_text.count("\n", 0, body_offset + entry_match.start()) + 1
        runtime_path = runtime_path_from_registered_document(
            document,
            f"registered route {route_id!r} at line {line}",
            errors,
        )
        registered_routes.append(
            RegisteredRoute(
                route_id=route_id,
                document=document,
                runtime_path=runtime_path,
                line=line,
            )
        )
        for index in range(entry_match.start(), entry_match.end()):
            consumed[index] = " "

    leftover = "".join(consumed).strip()
    if leftover:
        errors.append(
            "ui_rml_routes contains unparsed content: "
            f"{compact_unparsed_table_body(leftover)!r}"
        )

    return registered_routes


def line_list(routes: list[RegisteredRoute]) -> str:
    return ", ".join(str(route.line) for route in routes)


def validate_runtime_registry(
    manifest_data: dict[str, Any],
    cpp_text: str,
    *,
    allowed_extra_routes: tuple[str, ...] = DEFAULT_ALLOWED_EXTRA_ROUTES,
) -> RegistryReport:
    report = RegistryReport()
    manifest_errors: list[str] = []
    manifest_routes = manifest_routes_from_data(manifest_data, manifest_errors)
    registered_routes = parse_registered_routes(cpp_text, report.errors)
    report.errors.extend(manifest_errors)
    report.manifest_routes = manifest_routes
    report.registered_routes = registered_routes

    routes_field = manifest_data.get("routes")
    report.stats.manifest_routes = len(routes_field) if isinstance(routes_field, list) else 0
    report.stats.registered_routes = len(registered_routes)

    registered_by_id: dict[str, list[RegisteredRoute]] = {}
    for route in registered_routes:
        registered_by_id.setdefault(route.route_id, []).append(route)

    duplicates = {
        route_id: routes
        for route_id, routes in registered_by_id.items()
        if len(routes) > 1
    }
    report.stats.duplicates = len(duplicates)
    for route_id in sorted(duplicates):
        report.errors.append(
            f"duplicate registered route {route_id!r} at lines {line_list(duplicates[route_id])}"
        )

    manifest_ids = set(manifest_routes)
    registered_ids = set(registered_by_id)
    allowed_extra_ids = set(allowed_extra_routes)

    missing = sorted(manifest_ids - registered_ids)
    unexpected = sorted(registered_ids - manifest_ids - allowed_extra_ids)
    report.stats.missing = len(missing)
    report.stats.unexpected = len(unexpected)

    for route_id in missing:
        report.errors.append(f"manifest route {route_id!r} is missing from ui_rml_routes")
    for route_id in unexpected:
        report.errors.append(
            f"unexpected registered route {route_id!r} at lines {line_list(registered_by_id[route_id])}"
        )

    for route_id in sorted(manifest_ids & registered_ids):
        registered_matches = registered_by_id[route_id]
        if len(registered_matches) != 1:
            continue
        registered_route = registered_matches[0]
        expected_runtime_path = manifest_routes[route_id].runtime_path
        if registered_route.runtime_path is None:
            continue
        if registered_route.runtime_path != expected_runtime_path:
            report.errors.append(
                f"registered route {route_id!r} runtime path mismatch: "
                f"{registered_route.runtime_path.as_posix()} != {expected_runtime_path.as_posix()}"
            )
            continue
        report.stats.matched_runtime_paths += 1

    return report


def print_report(report: RegistryReport) -> None:
    stats = report.stats
    print("RmlUi runtime route registry:")
    print(f"  Manifest routes: {stats.manifest_routes}")
    print(f"  Registered routes: {stats.registered_routes}")
    print(f"  Missing: {stats.missing}")
    print(f"  Unexpected: {stats.unexpected}")
    print(f"  Duplicates: {stats.duplicates}")
    print(f"  Matched runtime paths: {stats.matched_runtime_paths}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi runtime route registry check passed.")


def main(argv: list[str] | None = None) -> int:
    repo_root = repo_root_from_script()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_manifest.json"),
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--cpp",
        type=Path,
        default=Path("src/client/ui_rml/ui_rml.cpp"),
        help="Path to src/client/ui_rml/ui_rml.cpp.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root,
        help="Repository root used to resolve default paths.",
    )
    parser.add_argument(
        "--allowed-extra-route",
        action="append",
        dest="allowed_extra_routes",
        default=None,
        help=(
            "Registered route ID allowed outside the smoke manifest. "
            "Can be repeated; defaults to core.runtime_smoke."
        ),
    )
    args = parser.parse_args(argv)

    allowed_extra_routes = tuple(args.allowed_extra_routes or DEFAULT_ALLOWED_EXTRA_ROUTES)
    manifest_path = args.manifest
    cpp_path = args.cpp
    if not manifest_path.is_absolute():
        manifest_path = args.repo_root / manifest_path
    if not cpp_path.is_absolute():
        cpp_path = args.repo_root / cpp_path

    try:
        manifest_data = load_manifest(manifest_path.resolve())
        cpp_text = load_text(cpp_path.resolve())
        report = validate_runtime_registry(
            manifest_data,
            cpp_text,
            allowed_extra_routes=allowed_extra_routes,
        )
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Failed to validate RmlUi runtime route registry: {exc}", file=sys.stderr)
        return 1

    print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
