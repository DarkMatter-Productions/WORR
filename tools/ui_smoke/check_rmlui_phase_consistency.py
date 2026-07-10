#!/usr/bin/env python3
"""Cross-check WORR RmlUi migration phases against static evidence."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


MIGRATION_PHASES = (
    "starter",
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)
METADATA_BACKED_PHASES = ("controller_stub", "runtime_stub")
CONTROLLER_BINDING_PHASES = ("controller_stub", "runtime_stub")
DEFAULT_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_manifest.json")
DEFAULT_ROUTE_METADATA_ROOT = Path("assets/ui/rml")
DEFAULT_PARITY_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_parity_manifest.json")
DEFAULT_CPP_PATH = Path("src/client/ui_rml/ui_rml.cpp")
DEFAULT_DOCUMENTED_RUNTIME_ROUTES = ("main", "game", "download_status")

C_COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
MENU_TOKEN_RE = re.compile(
    r"\bcase\s+(?P<case>UIMENU_[A-Za-z0-9_]+)\s*:"
    r"|\bdefault\s*:"
    r"|\breturn\s+\"(?P<route>(?:\\.|[^\"\\])*)\"\s*;"
    r"|\breturn\s+NULL\s*;",
    re.DOTALL,
)


@dataclass(frozen=True)
class ManifestRoute:
    route_id: str
    migration_phase: str


@dataclass
class ParityModel:
    categories: tuple[str, ...] = ()
    phase_defaults: dict[str, dict[str, str]] = field(default_factory=dict)
    route_evidence: dict[str, dict[str, str]] = field(default_factory=dict)
    route_ids: set[str] = field(default_factory=set)

    def models_category(self, category: str) -> bool:
        return category in self.categories


@dataclass
class PhaseConsistencyReport:
    routes_checked: int = 0
    route_metadata_files_checked: int = 0
    phase_counts: Counter[str] = field(default_factory=Counter)
    metadata_backed_advanced_routes: int = 0
    runtime_stub_routes: int = 0
    runtime_menu_mapped_routes: int = 0
    runtime_documented_guard_routes: int = 0
    parity_ready_routes: int = 0
    missing_evidence_counts: Counter[str] = field(default_factory=Counter)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_input_path(repo_root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def route_label(route: dict[str, Any], index: int, prefix: str) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"{prefix} route {route_id!r}"
    return f"{prefix} route at index {index}"


def route_list(data: dict[str, Any], label: str, errors: list[str]) -> list[Any]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append(f"{label} field 'routes' must be a list")
        return []
    return routes


def load_manifest_routes(data: dict[str, Any], errors: list[str]) -> dict[str, ManifestRoute]:
    routes_by_id: dict[str, ManifestRoute] = {}
    duplicate_ids: set[str] = set()

    for index, route in enumerate(route_list(data, "manifest", errors)):
        if not isinstance(route, dict):
            errors.append(f"manifest route at index {index} must be an object")
            continue

        label = route_label(route, index, "manifest")
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            errors.append(f"{label} field 'id' must be a non-empty string")
            continue
        if route_id in routes_by_id:
            duplicate_ids.add(route_id)
            continue

        migration_phase = route.get("migration_phase")
        if not isinstance(migration_phase, str) or not migration_phase:
            errors.append(f"{label} field 'migration_phase' must be a non-empty string")
            migration_phase = "<missing>"
        elif migration_phase not in MIGRATION_PHASES:
            allowed = ", ".join(MIGRATION_PHASES)
            errors.append(
                f"{label} field 'migration_phase' must be one of {allowed}; "
                f"got {migration_phase!r}"
            )

        routes_by_id[route_id] = ManifestRoute(route_id, migration_phase)

    for route_id in sorted(duplicate_ids):
        errors.append(f"duplicate manifest route id {route_id!r}")

    return routes_by_id


def discover_route_metadata_paths(repo_root: Path) -> list[Path]:
    metadata_root = resolve_input_path(repo_root, DEFAULT_ROUTE_METADATA_ROOT)
    if not metadata_root.is_dir():
        return []
    return sorted(path.resolve() for path in metadata_root.glob("*/routes.json") if path.is_file())


def index_route_metadata(
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
    report: PhaseConsistencyReport,
) -> dict[str, dict[str, Any]]:
    metadata_by_id: dict[str, dict[str, Any]] = {}
    duplicate_ids: set[str] = set()
    report.route_metadata_files_checked = len(route_metadata_sets)

    for metadata_path, data in route_metadata_sets:
        metadata_label = display_path(metadata_path, repo_root)
        for index, route in enumerate(route_list(data, metadata_label, report.errors)):
            if not isinstance(route, dict):
                report.errors.append(f"{metadata_label} route at index {index} must be an object")
                continue

            route_id = route.get("id")
            if not isinstance(route_id, str) or not route_id:
                report.errors.append(
                    f"{metadata_label} route at index {index} field 'id' must be a non-empty string"
                )
                continue
            if route_id in metadata_by_id:
                duplicate_ids.add(route_id)
                continue
            metadata_by_id[route_id] = route

    for route_id in sorted(duplicate_ids):
        report.errors.append(f"route metadata id {route_id!r} is duplicated")

    return metadata_by_id


def has_controller_contracts(route: dict[str, Any], route_id: str, errors: list[str]) -> bool:
    controller_contracts = route.get("controller_contracts")
    if not isinstance(controller_contracts, list) or not controller_contracts:
        errors.append(f"route {route_id!r} metadata must include non-empty controller_contracts")
        return False

    ok = True
    for index, contract_ref in enumerate(controller_contracts):
        if not isinstance(contract_ref, dict):
            errors.append(f"route {route_id!r} controller_contracts[{index}] must be an object")
            ok = False
            continue
        category = contract_ref.get("category")
        if not isinstance(category, str) or not category:
            errors.append(
                f"route {route_id!r} controller_contracts[{index}] field 'category' "
                "must be a non-empty string"
            )
            ok = False
    return ok


def strip_c_comments_keep_newlines(value: str) -> str:
    def replace(match: re.Match[str]) -> str:
        return "".join("\n" if char == "\n" else " " for char in match.group(0))

    return C_COMMENT_RE.sub(replace, value)


def extract_function_body(cpp_text: str, function_name: str, errors: list[str]) -> str | None:
    stripped_text = strip_c_comments_keep_newlines(cpp_text)
    signature_re = re.compile(
        r"\b" + re.escape(function_name) + r"\s*\([^;{}]*\)\s*\{",
        re.DOTALL,
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


def status_from_entry(entry: Any, label: str, errors: list[str]) -> str | None:
    if isinstance(entry, str):
        status = entry
    elif isinstance(entry, dict):
        status = entry.get("status")
    else:
        errors.append(f"{label} must be a status string or object with a status field")
        return None

    if status not in ("pending", "complete"):
        errors.append(f"{label} status must be 'pending' or 'complete'; got {status!r}")
        return None
    return str(status)


def load_parity_model(data: dict[str, Any], errors: list[str]) -> ParityModel:
    categories: list[str] = []
    raw_categories = data.get("evidence_categories")
    if isinstance(raw_categories, list):
        duplicate_categories: set[str] = set()
        seen_categories: set[str] = set()
        for index, category in enumerate(raw_categories):
            if not isinstance(category, dict):
                errors.append(f"parity evidence category at index {index} must be an object")
                continue
            category_id = category.get("id")
            if not isinstance(category_id, str) or not category_id:
                errors.append(
                    f"parity evidence category at index {index} field 'id' must be a non-empty string"
                )
                continue
            if category_id in seen_categories:
                duplicate_categories.add(category_id)
                continue
            seen_categories.add(category_id)
            categories.append(category_id)
        for category_id in sorted(duplicate_categories):
            errors.append(f"duplicate parity evidence category id {category_id!r}")
    elif raw_categories is not None:
        errors.append("parity manifest field 'evidence_categories' must be a list")

    phase_defaults: dict[str, dict[str, str]] = {}
    raw_defaults = data.get("phase_defaults", {})
    if not isinstance(raw_defaults, dict):
        errors.append("parity manifest field 'phase_defaults' must be an object when present")
    else:
        for phase, raw_phase in raw_defaults.items():
            if not isinstance(phase, str) or not phase:
                errors.append("parity phase_defaults keys must be non-empty strings")
                continue
            if not isinstance(raw_phase, dict):
                errors.append(f"parity phase_defaults.{phase} must be an object")
                continue
            phase_defaults[phase] = {}
            for category, entry in raw_phase.items():
                if not isinstance(category, str) or not category:
                    errors.append(f"parity phase_defaults.{phase} category keys must be non-empty strings")
                    continue
                status = status_from_entry(entry, f"phase_defaults.{phase}.{category}", errors)
                if status is not None:
                    phase_defaults[phase][category] = status

    route_evidence: dict[str, dict[str, str]] = {}
    route_ids: set[str] = set()
    duplicate_route_ids: set[str] = set()
    for index, route in enumerate(route_list(data, "parity manifest", errors)):
        if not isinstance(route, dict):
            errors.append(f"parity route at index {index} must be an object")
            continue

        label = route_label(route, index, "parity")
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            errors.append(f"{label} field 'id' must be a non-empty string")
            continue
        if route_id in route_ids:
            duplicate_route_ids.add(route_id)
            continue
        route_ids.add(route_id)

        evidence: dict[str, str] = {}
        raw_evidence = route.get("evidence", {})
        if raw_evidence is None:
            raw_evidence = {}
        if not isinstance(raw_evidence, dict):
            errors.append(f"{label} field 'evidence' must be an object when present")
            raw_evidence = {}
        for category, entry in raw_evidence.items():
            if not isinstance(category, str) or not category:
                errors.append(f"{label} evidence category keys must be non-empty strings")
                continue
            status = status_from_entry(entry, f"{label} evidence.{category}", errors)
            if status is not None:
                evidence[category] = status
                if category not in categories:
                    categories.append(category)
        route_evidence[route_id] = evidence

    for route_id in sorted(duplicate_route_ids):
        errors.append(f"duplicate parity route id {route_id!r}")

    return ParityModel(tuple(categories), phase_defaults, route_evidence, route_ids)


def effective_parity_statuses(route: ManifestRoute, parity_model: ParityModel) -> dict[str, str]:
    defaults = parity_model.phase_defaults.get(route.migration_phase, {})
    overrides = parity_model.route_evidence.get(route.route_id, {})
    statuses: dict[str, str] = {}
    for category in parity_model.categories:
        if category in overrides:
            statuses[category] = overrides[category]
        elif category in defaults:
            statuses[category] = defaults[category]
    return statuses


def parity_model_from_optional_path(
    parity_manifest_path: Path | None,
    report: PhaseConsistencyReport,
) -> ParityModel | None:
    if parity_manifest_path is None:
        return None
    if not parity_manifest_path.is_file():
        return None
    data = read_json_object(parity_manifest_path, "RmlUi parity manifest")
    return load_parity_model(data, report.errors)


def audit_phase_consistency(
    manifest_data: dict[str, Any],
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
    *,
    cpp_text: str | None = None,
    parity_model: ParityModel | None = None,
    documented_runtime_routes: tuple[str, ...] = DEFAULT_DOCUMENTED_RUNTIME_ROUTES,
) -> PhaseConsistencyReport:
    report = PhaseConsistencyReport()
    manifest_routes = load_manifest_routes(manifest_data, report.errors)
    metadata_by_id = index_route_metadata(route_metadata_sets, repo_root, report)
    report.routes_checked = len(manifest_routes)

    menu_mappings: dict[str, set[str]] = {}
    if cpp_text is not None:
        menu_mappings = parse_route_for_menu(cpp_text, report.errors)

    documented_runtime_route_set = set(documented_runtime_routes)

    for route_id in sorted(manifest_routes):
        manifest_route = manifest_routes[route_id]
        report.phase_counts[manifest_route.migration_phase] += 1

        if manifest_route.migration_phase in METADATA_BACKED_PHASES:
            route_metadata = metadata_by_id.get(route_id)
            if route_metadata is None:
                report.missing_evidence_counts["route_metadata"] += 1
                report.errors.append(
                    f"{manifest_route.migration_phase} route {route_id!r} is missing route metadata"
                )
            else:
                metadata_phase = route_metadata.get("migration_phase")
                metadata_phase_matches = metadata_phase == manifest_route.migration_phase
                if metadata_phase != manifest_route.migration_phase:
                    report.missing_evidence_counts["phase_metadata_match"] += 1
                    report.errors.append(
                        f"{manifest_route.migration_phase} route {route_id!r} route metadata "
                        f"migration_phase is {metadata_phase!r}"
                    )

                before_error_count = len(report.errors)
                if (
                    has_controller_contracts(route_metadata, route_id, report.errors)
                    and metadata_phase_matches
                ):
                    report.metadata_backed_advanced_routes += 1
                elif len(report.errors) > before_error_count:
                    report.missing_evidence_counts["controller_contracts"] += 1

        if manifest_route.migration_phase == "runtime_stub":
            report.runtime_stub_routes += 1
            if menu_mappings.get(route_id):
                report.runtime_menu_mapped_routes += 1
            elif route_id in documented_runtime_route_set:
                report.runtime_documented_guard_routes += 1
            else:
                report.missing_evidence_counts["runtime_entrypoint"] += 1
                report.errors.append(
                    f"runtime_stub route {route_id!r} is not returned by UI_Rml_RouteForMenu "
                    "and is not listed as a documented guarded runtime route"
                )

        if manifest_route.migration_phase == "parity_ready":
            report.parity_ready_routes += 1

    if parity_model is None:
        if report.parity_ready_routes:
            report.missing_evidence_counts["parity_manifest"] += report.parity_ready_routes
            report.errors.append("parity_ready routes require a parity manifest with complete evidence")
        return report

    manifest_ids = set(manifest_routes)
    for route_id in sorted(manifest_ids - parity_model.route_ids):
        report.missing_evidence_counts["parity_route"] += 1
        report.errors.append(f"missing parity manifest route coverage for {route_id!r}")
    for route_id in sorted(parity_model.route_ids - manifest_ids):
        report.errors.append(f"parity manifest contains unknown route {route_id!r}")

    parity_ready_incomplete: Counter[str] = Counter()
    controller_bindings_complete: set[str] = set()
    for route_id in sorted(manifest_ids & parity_model.route_ids):
        manifest_route = manifest_routes[route_id]
        statuses = effective_parity_statuses(manifest_route, parity_model)

        if (
            parity_model.models_category("controller_bindings")
            and statuses.get("controller_bindings") == "complete"
        ):
            controller_bindings_complete.add(route_id)

        if manifest_route.migration_phase != "parity_ready":
            continue

        for category in parity_model.categories:
            status = statuses.get(category)
            if status != "complete":
                parity_ready_incomplete[category] += 1
                report.errors.append(
                    f"parity_ready route {route_id!r} category {category!r} is "
                    f"{status or '<missing>'}; completed evidence is required"
                )

    if parity_ready_incomplete:
        report.missing_evidence_counts["parity_complete"] += sum(parity_ready_incomplete.values())

    if parity_model.models_category("controller_bindings"):
        expected_controller_bindings = {
            route_id
            for route_id, route in manifest_routes.items()
            if route.migration_phase in CONTROLLER_BINDING_PHASES
        }
        missing = sorted(expected_controller_bindings - controller_bindings_complete)
        unexpected = sorted(controller_bindings_complete - expected_controller_bindings)
        if missing or unexpected:
            report.missing_evidence_counts["controller_bindings"] += len(missing) + len(unexpected)
            parts = []
            if missing:
                parts.append(f"missing advanced routes {missing!r}")
            if unexpected:
                parts.append(f"unexpected complete routes {unexpected!r}")
            report.errors.append(
                "parity controller_bindings complete routes do not match "
                "controller_stub/runtime_stub routes: "
                + "; ".join(parts)
            )

    return report


def ordered_counts(counter: Counter[str], keys: tuple[str, ...]) -> dict[str, int]:
    payload = {key: counter[key] for key in keys}
    for key in sorted(counter):
        if key not in payload:
            payload[key] = counter[key]
    return payload


def format_json_report(report: PhaseConsistencyReport) -> str:
    payload = {
        "ok": report.ok(),
        "routes_checked": report.routes_checked,
        "route_metadata_files_checked": report.route_metadata_files_checked,
        "phase_counts": ordered_counts(report.phase_counts, MIGRATION_PHASES),
        "metadata_backed_advanced_routes": report.metadata_backed_advanced_routes,
        "runtime_stub_routes": report.runtime_stub_routes,
        "runtime_menu_mapped_routes": report.runtime_menu_mapped_routes,
        "runtime_documented_guard_routes": report.runtime_documented_guard_routes,
        "parity_ready_routes": report.parity_ready_routes,
        "missing_evidence_counts": dict(sorted(report.missing_evidence_counts.items())),
        "errors": report.errors,
    }
    return json.dumps(payload, indent=2)


def format_count_line(label: str, counter: Counter[str], keys: tuple[str, ...]) -> str:
    return f"{label}: " + ", ".join(f"{key}={counter[key]}" for key in keys)


def print_text_report(report: PhaseConsistencyReport) -> None:
    print("RmlUi phase consistency:")
    print(f"  Routes checked: {report.routes_checked}")
    print(f"  Route metadata files checked: {report.route_metadata_files_checked}")
    print("  " + format_count_line("Phases", report.phase_counts, MIGRATION_PHASES))
    print(f"  Metadata-backed advanced routes: {report.metadata_backed_advanced_routes}")
    print(f"  Runtime_stub routes: {report.runtime_stub_routes}")
    print(f"  Runtime menu-mapped routes: {report.runtime_menu_mapped_routes}")
    print(f"  Runtime documented guard routes: {report.runtime_documented_guard_routes}")
    print(f"  Parity-ready routes: {report.parity_ready_routes}")
    if report.missing_evidence_counts:
        print(
            "  Missing evidence: "
            + ", ".join(
                f"{key}={report.missing_evidence_counts[key]}"
                for key in sorted(report.missing_evidence_counts)
            )
        )
    else:
        print("  Missing evidence: none")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi phase consistency check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST_PATH,
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--route-metadata",
        type=Path,
        action="append",
        default=None,
        help=(
            "Path to a route metadata JSON file. May be repeated. "
            "Defaults to every assets/ui/rml/*/routes.json file."
        ),
    )
    parser.add_argument(
        "--parity-manifest",
        type=Path,
        default=DEFAULT_PARITY_MANIFEST_PATH,
        help="Path to the optional RmlUi parity checklist manifest JSON.",
    )
    parser.add_argument(
        "--cpp",
        type=Path,
        default=DEFAULT_CPP_PATH,
        help="Path to src/client/ui_rml/ui_rml.cpp for menu-entrypoint evidence.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve default paths.",
    )
    parser.add_argument(
        "--documented-runtime-route",
        action="append",
        default=None,
        help=(
            "Route ID accepted as a documented guarded runtime route when not "
            "returned by UI_Rml_RouteForMenu. Can be repeated; defaults to "
            "main, game, and download_status."
        ),
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = resolve_input_path(repo_root, args.manifest)
    route_metadata_paths = (
        [resolve_input_path(repo_root, path) for path in args.route_metadata]
        if args.route_metadata
        else discover_route_metadata_paths(repo_root)
    )
    parity_manifest_path = resolve_input_path(repo_root, args.parity_manifest)
    cpp_path = resolve_input_path(repo_root, args.cpp)
    documented_runtime_routes = tuple(
        args.documented_runtime_route or DEFAULT_DOCUMENTED_RUNTIME_ROUTES
    )

    try:
        manifest_data = read_json_object(manifest_path, "RmlUi smoke manifest")
        route_metadata_sets = [
            (path, read_json_object(path, f"{display_path(path, repo_root)} route metadata"))
            for path in route_metadata_paths
        ]
        cpp_text = cpp_path.read_text(encoding="utf-8") if cpp_path.is_file() else None
        parity_load_report = PhaseConsistencyReport()
        parity_model = parity_model_from_optional_path(parity_manifest_path, parity_load_report)
        report = audit_phase_consistency(
            manifest_data,
            route_metadata_sets,
            repo_root,
            cpp_text=cpp_text,
            parity_model=parity_model,
            documented_runtime_routes=documented_runtime_routes,
        )
        report.errors[0:0] = parity_load_report.errors
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        message = f"Failed to validate RmlUi phase consistency: {exc}"
        if args.format == "json":
            print(
                json.dumps(
                    {
                        "ok": False,
                        "routes_checked": 0,
                        "route_metadata_files_checked": 0,
                        "phase_counts": ordered_counts(Counter(), MIGRATION_PHASES),
                        "metadata_backed_advanced_routes": 0,
                        "runtime_stub_routes": 0,
                        "runtime_menu_mapped_routes": 0,
                        "runtime_documented_guard_routes": 0,
                        "parity_ready_routes": 0,
                        "missing_evidence_counts": {},
                        "errors": [message],
                    },
                    indent=2,
                )
            )
        else:
            print(message, file=sys.stderr)
        return 1

    if args.format == "json":
        print(format_json_report(report))
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
