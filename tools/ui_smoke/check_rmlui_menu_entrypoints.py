#!/usr/bin/env python3
"""Validate RmlUi menu-open entrypoints against the smoke manifest and registry."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import check_rmlui_runtime_registry as runtime_registry


DEFAULT_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_manifest.json")
DEFAULT_CPP_PATH = Path("src/client/ui_rml/ui_rml.cpp")

ROUTE_FOR_MENU_RE = re.compile(
    r"\bconst\s+char\s*\*\s*UI_Rml_RouteForMenu\s*\([^)]*\)\s*",
    re.DOTALL,
)
MENU_TOKEN_RE = re.compile(
    r"case\s+(?P<case>UIMENU_[A-Za-z0-9_]+)\s*:"
    r"|default\s*:"
    r"|return\s+(?P<return>[^;]+)\s*;",
    re.DOTALL,
)
STRING_LITERAL_RE = re.compile(r'^"(?P<value>(?:\\.|[^"\\])*)"$', re.DOTALL)
NULL_RETURN_VALUES = {"NULL", "nullptr", "0"}


@dataclass(frozen=True)
class MenuMapping:
    menu_case: str
    route_id: str | None
    line: int


@dataclass
class MenuEntrypointStats:
    menu_cases_checked: int = 0
    mapped_routes: int = 0
    unique_mapped_routes: int = 0
    manifest_matches: int = 0
    registry_matches: int = 0


@dataclass
class MenuEntrypointReport:
    stats: MenuEntrypointStats = field(default_factory=MenuEntrypointStats)
    mappings: dict[str, MenuMapping] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_input_path(repo_root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def read_json_object(path: Path, label: str) -> dict[str, object]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def find_matching_brace(text: str, open_index: int) -> int | None:
    depth = 0
    in_string = False
    in_char = False
    escaped = False

    for index in range(open_index, len(text)):
        char = text[index]

        if escaped:
            escaped = False
            continue
        if char == "\\" and (in_string or in_char):
            escaped = True
            continue
        if char == '"' and not in_char:
            in_string = not in_string
            continue
        if char == "'" and not in_string:
            in_char = not in_char
            continue
        if in_string or in_char:
            continue

        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index

    return None


def extract_route_for_menu_body(cpp_text: str, errors: list[str]) -> tuple[str, int] | None:
    stripped_text = runtime_registry.strip_c_comments_keep_newlines(cpp_text)

    for match in ROUTE_FOR_MENU_RE.finditer(stripped_text):
        open_index = match.end()
        while open_index < len(stripped_text) and stripped_text[open_index].isspace():
            open_index += 1
        if open_index >= len(stripped_text) or stripped_text[open_index] != "{":
            continue

        close_index = find_matching_brace(stripped_text, open_index)
        if close_index is None:
            errors.append("could not find closing brace for UI_Rml_RouteForMenu")
            return None

        return stripped_text[open_index + 1 : close_index], open_index + 1

    errors.append("could not find UI_Rml_RouteForMenu function body")
    return None


def decode_string_literal(value: str) -> str:
    return re.sub(r"\\([\\\"])", r"\1", value)


def parse_return_route(expr: str) -> str | None | object:
    stripped_expr = expr.strip()
    if stripped_expr in NULL_RETURN_VALUES:
        return None

    string_match = STRING_LITERAL_RE.fullmatch(stripped_expr)
    if string_match is None:
        return _UNSUPPORTED_RETURN

    return decode_string_literal(string_match.group("value"))


_UNSUPPORTED_RETURN = object()


def add_menu_mapping(
    mappings: dict[str, MenuMapping],
    menu_case: str,
    route_id: str | None,
    line: int,
    errors: list[str],
) -> None:
    existing = mappings.get(menu_case)
    if existing is not None and existing.route_id != route_id:
        errors.append(
            f"duplicate contradictory mapping for {menu_case}: "
            f"{existing.route_id!r} at line {existing.line} conflicts with "
            f"{route_id!r} at line {line}"
        )
        return

    if existing is None:
        mappings[menu_case] = MenuMapping(menu_case=menu_case, route_id=route_id, line=line)


def parse_menu_mappings(cpp_text: str, errors: list[str]) -> dict[str, MenuMapping]:
    function_body = extract_route_for_menu_body(cpp_text, errors)
    if function_body is None:
        return {}

    body, body_offset = function_body
    pending_cases: list[str] = []
    mappings: dict[str, MenuMapping] = {}

    for token_match in MENU_TOKEN_RE.finditer(body):
        menu_case = token_match.group("case")
        if menu_case is not None:
            pending_cases.append(menu_case)
            continue

        return_expr = token_match.group("return")
        if return_expr is None:
            continue

        if not pending_cases:
            continue

        line = cpp_text.count("\n", 0, body_offset + token_match.start()) + 1
        route_id = parse_return_route(return_expr)
        if route_id is _UNSUPPORTED_RETURN:
            errors.append(
                "unsupported UI_Rml_RouteForMenu return expression for "
                f"{', '.join(pending_cases)} at line {line}: {return_expr.strip()}"
            )
            pending_cases = []
            continue

        for pending_case in pending_cases:
            add_menu_mapping(mappings, pending_case, route_id, line, errors)
        pending_cases = []

    if pending_cases:
        errors.append(
            "UI_Rml_RouteForMenu case label(s) have no literal return: "
            f"{', '.join(pending_cases)}"
        )

    return mappings


def registry_line_list(routes: list[runtime_registry.RegisteredRoute]) -> str:
    return ", ".join(str(route.line) for route in routes)


def validate_menu_entrypoints(
    manifest_data: dict[str, object],
    cpp_text: str,
) -> MenuEntrypointReport:
    report = MenuEntrypointReport()

    manifest_errors: list[str] = []
    manifest_routes = runtime_registry.manifest_routes_from_data(manifest_data, manifest_errors)
    registered_routes = runtime_registry.parse_registered_routes(cpp_text, report.errors)
    mappings = parse_menu_mappings(cpp_text, report.errors)
    report.errors.extend(manifest_errors)
    report.mappings = mappings

    mapped_route_ids = sorted(
        {mapping.route_id for mapping in mappings.values() if mapping.route_id is not None}
    )
    registered_by_id: dict[str, list[runtime_registry.RegisteredRoute]] = {}
    for route in registered_routes:
        registered_by_id.setdefault(route.route_id, []).append(route)

    report.stats.menu_cases_checked = len(mappings)
    report.stats.mapped_routes = sum(1 for mapping in mappings.values() if mapping.route_id is not None)
    report.stats.unique_mapped_routes = len(mapped_route_ids)

    for route_id in mapped_route_ids:
        manifest_route = manifest_routes.get(route_id)
        registered_matches = registered_by_id.get(route_id, [])

        if manifest_route is None:
            report.errors.append(f"mapped route {route_id!r} is missing from RmlUi smoke manifest")
        else:
            report.stats.manifest_matches += 1

        if not registered_matches:
            report.errors.append(f"mapped route {route_id!r} is missing from ui_rml_routes")
            continue

        if len(registered_matches) > 1:
            report.errors.append(
                f"mapped route {route_id!r} has multiple ui_rml_routes entries "
                f"at lines {registry_line_list(registered_matches)}"
            )
            continue

        registered_route = registered_matches[0]
        report.stats.registry_matches += 1

        if manifest_route is None or registered_route.runtime_path is None:
            continue

        if registered_route.runtime_path != manifest_route.runtime_path:
            report.errors.append(
                f"mapped route {route_id!r} runtime path mismatch: "
                f"{registered_route.runtime_path.as_posix()} != "
                f"{manifest_route.runtime_path.as_posix()}"
            )

    return report


def print_report(report: MenuEntrypointReport) -> None:
    stats = report.stats
    print("RmlUi menu entrypoints:")
    print(f"  Menu cases checked: {stats.menu_cases_checked}")
    print(f"  Mapped routes: {stats.mapped_routes}")
    print(f"  Unique mapped routes: {stats.unique_mapped_routes}")
    print(f"  Manifest matches: {stats.manifest_matches}")
    print(f"  Registry matches: {stats.registry_matches}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi menu entrypoint check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST_PATH,
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--cpp",
        type=Path,
        default=DEFAULT_CPP_PATH,
        help="Path to src/client/ui_rml/ui_rml.cpp.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve default paths.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = resolve_input_path(repo_root, args.manifest)
    cpp_path = resolve_input_path(repo_root, args.cpp)

    try:
        manifest_data = read_json_object(manifest_path, "RmlUi smoke manifest")
        cpp_text = read_text(cpp_path)
        report = validate_menu_entrypoints(manifest_data, cpp_text)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Failed to validate RmlUi menu entrypoints: {exc}", file=sys.stderr)
        return 1

    print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
