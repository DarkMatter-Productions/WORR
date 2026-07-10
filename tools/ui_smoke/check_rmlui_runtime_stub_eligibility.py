#!/usr/bin/env python3
"""Validate Round 8 RmlUi runtime_stub route eligibility."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any


SOURCE_ROOT = PurePosixPath("assets/ui/rml")
RUNTIME_ROOT = PurePosixPath("ui/rml")
DEFAULT_ALLOWED_RUNTIME_STUB_ROUTES = ("main", "game", "download_status")

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
MENU_TOKEN_RE = re.compile(
    r"\bcase\s+(?P<case>UIMENU_[A-Za-z0-9_]+)\s*:"
    r"|\bdefault\s*:"
    r"|\breturn\s+\"(?P<route>(?:\\.|[^\"\\])*)\"\s*;"
    r"|\breturn\s+NULL\s*;",
    re.DOTALL,
)


@dataclass(frozen=True)
class RuntimeStubRoute:
    route_id: str
    manifest_document: str
    registered_document: PurePosixPath
    runtime_path: PurePosixPath


@dataclass(frozen=True)
class RegisteredRoute:
    route_id: str
    document: str
    runtime_path: PurePosixPath | None
    line: int


@dataclass
class EligibilityStats:
    runtime_stub_routes_checked: int = 0
    menu_mapped_routes: int = 0
    registry_matches: int = 0
    controller_contract_matches: int = 0


@dataclass
class EligibilityReport:
    stats: EligibilityStats = field(default_factory=EligibilityStats)
    runtime_stub_routes: dict[str, RuntimeStubRoute] = field(default_factory=dict)
    menu_mappings: dict[str, set[str]] = field(default_factory=dict)
    registered_routes: list[RegisteredRoute] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def read_text(path: Path) -> str:
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


def route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"manifest route {route_id!r}"
    return f"manifest route at index {index}"


def registered_document_from_manifest_document(
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
        return document_path.relative_to(SOURCE_ROOT)
    except ValueError:
        errors.append(f"{label} document path must be under {SOURCE_ROOT.as_posix()}: {value}")
        return None


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

    return RUNTIME_ROOT / PurePosixPath(value)


def runtime_stub_routes_from_manifest(
    data: dict[str, Any],
    errors: list[str],
    *,
    allowed_runtime_stub_routes: tuple[str, ...],
) -> tuple[int, dict[str, RuntimeStubRoute]]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append("manifest field 'routes' must be a list")
        return 0, {}

    runtime_stub_count = 0
    runtime_stub_routes: dict[str, RuntimeStubRoute] = {}
    duplicate_route_ids: set[str] = set()
    allowed_route_ids = set(allowed_runtime_stub_routes)

    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            continue
        if route.get("migration_phase") != "runtime_stub":
            continue

        runtime_stub_count += 1
        label = route_label(route, index)
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            errors.append(f"{label} field 'id' must be a non-empty string")
            continue

        if route_id not in allowed_route_ids:
            allowed = ", ".join(sorted(allowed_route_ids))
            errors.append(
                f"runtime_stub route {route_id!r} is not Round 8 eligible; "
                f"allowed route IDs: {allowed}"
            )

        registered_document = registered_document_from_manifest_document(
            route.get("document"),
            label,
            errors,
        )
        if registered_document is None:
            continue

        if route_id in runtime_stub_routes:
            duplicate_route_ids.add(route_id)
            continue

        runtime_stub_routes[route_id] = RuntimeStubRoute(
            route_id=route_id,
            manifest_document=str(route.get("document")),
            registered_document=registered_document,
            runtime_path=RUNTIME_ROOT / registered_document,
        )

    for route_id in sorted(duplicate_route_ids):
        errors.append(f"duplicate runtime_stub manifest route id {route_id!r}")

    return runtime_stub_count, runtime_stub_routes


def shell_routes_by_id(data: dict[str, Any], errors: list[str]) -> dict[str, dict[str, Any]]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append("shell routes field 'routes' must be a list")
        return {}

    shell_routes: dict[str, dict[str, Any]] = {}
    duplicate_route_ids: set[str] = set()
    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            errors.append(f"shell route at index {index} must be an object")
            continue
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            errors.append(f"shell route at index {index} field 'id' must be a non-empty string")
            continue
        if route_id in shell_routes:
            duplicate_route_ids.add(route_id)
            continue
        shell_routes[route_id] = route

    for route_id in sorted(duplicate_route_ids):
        errors.append(f"duplicate shell route id {route_id!r}")

    return shell_routes


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


def extract_function_body(cpp_text: str, function_name: str, errors: list[str]) -> str | None:
    stripped_text = strip_c_comments_keep_newlines(cpp_text)
    signature_re = re.compile(
        r"^[ \t]*(?:static\s+)?[A-Za-z_][A-Za-z0-9_:\s<>,*&]*\b"
        + re.escape(function_name)
        + r"\s*\([^;{}]*\)\s*\{",
        re.DOTALL | re.MULTILINE,
    )
    signature_match = signature_re.search(stripped_text)
    if signature_match is None:
        errors.append(f"could not find {function_name} function body")
        return None

    open_brace = signature_match.end() - 1
    depth = 1
    in_string: str | None = None
    escaped = False
    for index in range(open_brace + 1, len(stripped_text)):
        char = stripped_text[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == in_string:
                in_string = None
            continue
        if char in ("\"", "'"):
            in_string = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return stripped_text[open_brace + 1 : index]

    errors.append(f"could not find closing brace for {function_name}")
    return None


def parse_route_for_menu(cpp_text: str, errors: list[str]) -> dict[str, set[str]]:
    body = extract_function_body(cpp_text, "UI_Rml_RouteForMenu", errors)
    if body is None:
        return {}

    menu_mappings: dict[str, set[str]] = {}
    pending_cases: list[str] = []
    for token_match in MENU_TOKEN_RE.finditer(body):
        case_name = token_match.group("case")
        route_id = token_match.group("route")
        if case_name is not None:
            pending_cases.append(case_name)
        elif route_id is not None:
            for pending_case in pending_cases:
                menu_mappings.setdefault(route_id, set()).add(pending_case)
            pending_cases.clear()
        else:
            pending_cases.clear()

    return menu_mappings


def validate_open_menu_probe_fallback(cpp_text: str, errors: list[str]) -> bool:
    body = extract_function_body(cpp_text, "UI_Rml_OpenMenu", errors)
    if body is None:
        return False

    probe_match = re.search(r"\bUI_Rml_ProbeRoute\s*\(\s*route\s*\)\s*;", body)
    if probe_match is not None:
        after_probe = body[probe_match.end() :]
        if re.search(r"\breturn\s+false\s*;", after_probe) is None:
            errors.append("UI_Rml_OpenMenu does not return false after probing the route")
            return False

        return True

    if re.search(r"\bUI_Rml_OpenRouteInternal\s*\(\s*route\s*\)", body) is None:
        errors.append(
            "UI_Rml_OpenMenu neither probes the route nor delegates to UI_Rml_OpenRouteInternal(route)"
        )
        return False

    open_body = extract_function_body(cpp_text, "UI_Rml_OpenRouteInternal", errors)
    if open_body is None:
        return False

    probe_call = re.search(r"\bUI_Rml_ProbeRoute\s*\(\s*route->id\s*\)", open_body)
    runtime_open_call = re.search(r"\bui_rml_runtime\.OpenRoute\s*\(", open_body)
    if probe_call is None:
        errors.append("UI_Rml_OpenRouteInternal does not call UI_Rml_ProbeRoute(route->id)")
        return False
    if runtime_open_call is None:
        errors.append("UI_Rml_OpenRouteInternal does not call ui_rml_runtime.OpenRoute")
        return False
    if probe_call.start() > runtime_open_call.start():
        errors.append("UI_Rml_OpenRouteInternal must probe before runtime OpenRoute")
        return False

    return True


def registered_routes_by_id(
    registered_routes: list[RegisteredRoute],
) -> dict[str, list[RegisteredRoute]]:
    by_id: dict[str, list[RegisteredRoute]] = {}
    for route in registered_routes:
        by_id.setdefault(route.route_id, []).append(route)
    return by_id


def validate_runtime_stub_eligibility(
    manifest_data: dict[str, Any],
    shell_data: dict[str, Any],
    cpp_text: str,
    *,
    allowed_runtime_stub_routes: tuple[str, ...] = DEFAULT_ALLOWED_RUNTIME_STUB_ROUTES,
) -> EligibilityReport:
    report = EligibilityReport()
    runtime_stub_count, runtime_stub_routes = runtime_stub_routes_from_manifest(
        manifest_data,
        report.errors,
        allowed_runtime_stub_routes=allowed_runtime_stub_routes,
    )
    report.stats.runtime_stub_routes_checked = runtime_stub_count
    report.runtime_stub_routes = runtime_stub_routes

    if runtime_stub_count == 0:
        return report

    shell_routes = shell_routes_by_id(shell_data, report.errors)
    registered_routes = parse_registered_routes(cpp_text, report.errors)
    report.registered_routes = registered_routes
    report.menu_mappings = parse_route_for_menu(cpp_text, report.errors)
    validate_open_menu_probe_fallback(cpp_text, report.errors)

    registered_by_id = registered_routes_by_id(registered_routes)

    for route_id in sorted(runtime_stub_routes):
        route = runtime_stub_routes[route_id]

        shell_route = shell_routes.get(route_id)
        if shell_route is None:
            report.errors.append(f"runtime_stub route {route_id!r} is missing shell route metadata")
        else:
            shell_phase = shell_route.get("migration_phase")
            if shell_phase != "runtime_stub":
                report.errors.append(
                    f"runtime_stub route {route_id!r} shell migration_phase is "
                    f"{shell_phase!r}; expected 'runtime_stub'"
                )

            controller_contracts = shell_route.get("controller_contracts")
            if isinstance(controller_contracts, list) and controller_contracts:
                report.stats.controller_contract_matches += 1
            else:
                report.errors.append(
                    f"runtime_stub route {route_id!r} shell metadata must keep "
                    "non-empty controller_contracts"
                )

        if report.menu_mappings.get(route_id):
            report.stats.menu_mapped_routes += 1
        else:
            report.errors.append(
                f"runtime_stub route {route_id!r} is not returned by "
                "UI_Rml_RouteForMenu for any UIMENU_* case"
            )

        registered_matches = registered_by_id.get(route_id, [])
        if not registered_matches:
            report.errors.append(f"runtime_stub route {route_id!r} is missing from ui_rml_routes")
            continue

        matching_runtime_paths = [
            registered_route
            for registered_route in registered_matches
            if registered_route.runtime_path == route.runtime_path
        ]
        if matching_runtime_paths:
            report.stats.registry_matches += 1
            continue

        first_registered_route = registered_matches[0]
        if first_registered_route.runtime_path is not None:
            report.errors.append(
                f"registered route {route_id!r} runtime path mismatch: "
                f"{first_registered_route.runtime_path.as_posix()} != "
                f"{route.runtime_path.as_posix()}"
            )

    return report


def print_report(report: EligibilityReport) -> None:
    stats = report.stats
    print("RmlUi runtime_stub eligibility:")
    print(f"  Runtime_stub routes checked: {stats.runtime_stub_routes_checked}")
    print(f"  Menu-mapped routes: {stats.menu_mapped_routes}")
    print(f"  Registry matches: {stats.registry_matches}")
    print(f"  Controller contract matches: {stats.controller_contract_matches}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi runtime_stub eligibility check passed.")


def resolve_path(repo_root: Path, path: Path) -> Path:
    return path if path.is_absolute() else repo_root / path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_manifest.json"),
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--shell-routes",
        type=Path,
        default=Path("assets/ui/rml/shell/routes.json"),
        help="Path to the shell route metadata JSON.",
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
        default=repo_root_from_script(),
        help="Repository root used to resolve default paths.",
    )
    parser.add_argument(
        "--allowed-runtime-route",
        action="append",
        dest="allowed_runtime_routes",
        default=None,
        help=(
            "Route ID allowed to claim runtime_stub in this round. Can be "
            "repeated; defaults to main, game, and download_status."
        ),
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = resolve_path(repo_root, args.manifest).resolve()
    shell_routes_path = resolve_path(repo_root, args.shell_routes).resolve()
    cpp_path = resolve_path(repo_root, args.cpp).resolve()
    allowed_runtime_stub_routes = tuple(
        args.allowed_runtime_routes or DEFAULT_ALLOWED_RUNTIME_STUB_ROUTES
    )

    try:
        manifest_data = read_json_object(manifest_path, "RmlUi smoke manifest")
        shell_data = read_json_object(shell_routes_path, "RmlUi shell routes")
        cpp_text = read_text(cpp_path)
        report = validate_runtime_stub_eligibility(
            manifest_data,
            shell_data,
            cpp_text,
            allowed_runtime_stub_routes=allowed_runtime_stub_routes,
        )
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"Failed to validate RmlUi runtime_stub eligibility: {exc}", file=sys.stderr)
        return 1

    print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
