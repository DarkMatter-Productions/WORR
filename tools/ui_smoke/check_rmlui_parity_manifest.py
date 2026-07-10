#!/usr/bin/env python3
"""Validate the WORR RmlUi parity checklist manifest."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


EXPECTED_SCHEMA = "worr.rmlui.parity_manifest.v1"
CANONICAL_CATEGORIES = (
    "document_load",
    "navigation",
    "controller_bindings",
    "renderer_open_gl",
    "renderer_vulkan",
    "renderer_rtx_vkpt",
    "screenshot_layout",
    "input_escape_back",
    "legacy_fallback",
)
MIGRATION_PHASES = (
    "starter",
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)
STATUS_VALUES = ("pending", "complete")


@dataclass(frozen=True)
class SmokeRoute:
    route_id: str
    migration_phase: str


@dataclass(frozen=True)
class ParityRoute:
    route_id: str
    evidence: dict[str, str]


@dataclass
class ParityReport:
    routes_checked: int = 0
    categories_checked: int = len(CANONICAL_CATEGORIES)
    phase_counts: Counter[str] = field(default_factory=Counter)
    pending_counts: Counter[str] = field(default_factory=Counter)
    complete_counts: Counter[str] = field(default_factory=Counter)
    parity_ready_routes: int = 0
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_path(repo_root: Path, path: Path) -> Path:
    return path if path.is_absolute() else repo_root / path


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def route_label(route: dict[str, Any], index: int, prefix: str) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"{prefix} route {route_id!r}"
    return f"{prefix} route at index {index}"


def status_from_entry(entry: Any, label: str, errors: list[str]) -> str | None:
    if isinstance(entry, str):
        status = entry
    elif isinstance(entry, dict):
        status = entry.get("status")
    else:
        errors.append(f"{label} must be a status string or object with a status field")
        return None

    if status not in STATUS_VALUES:
        allowed = ", ".join(STATUS_VALUES)
        errors.append(f"{label} status must be one of {allowed}; got {status!r}")
        return None
    return str(status)


def load_smoke_routes(data: dict[str, Any], errors: list[str]) -> dict[str, SmokeRoute]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append("smoke manifest field 'routes' must be a list")
        return {}

    smoke_routes: dict[str, SmokeRoute] = {}
    duplicate_route_ids: set[str] = set()
    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            errors.append(f"smoke route at index {index} must be an object")
            continue

        label = route_label(route, index, "smoke")
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            errors.append(f"{label} field 'id' must be a non-empty string")
            continue
        if route_id in smoke_routes:
            duplicate_route_ids.add(route_id)
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

        smoke_routes[route_id] = SmokeRoute(
            route_id=route_id,
            migration_phase=migration_phase,
        )

    for route_id in sorted(duplicate_route_ids):
        errors.append(f"duplicate smoke route id {route_id!r}")

    return smoke_routes


def validate_categories(data: dict[str, Any], errors: list[str]) -> None:
    categories = data.get("evidence_categories")
    if not isinstance(categories, list):
        errors.append("parity manifest field 'evidence_categories' must be a list")
        return

    seen_ids: set[str] = set()
    duplicate_ids: set[str] = set()
    for index, category in enumerate(categories):
        if not isinstance(category, dict):
            errors.append(f"evidence category at index {index} must be an object")
            continue
        category_id = category.get("id")
        if not isinstance(category_id, str) or not category_id:
            errors.append(f"evidence category at index {index} field 'id' must be a non-empty string")
            continue
        if category_id in seen_ids:
            duplicate_ids.add(category_id)
        seen_ids.add(category_id)

    for category_id in sorted(duplicate_ids):
        errors.append(f"duplicate evidence category id {category_id!r}")

    expected = set(CANONICAL_CATEGORIES)
    unknown = sorted(seen_ids - expected)
    missing = sorted(expected - seen_ids)
    for category_id in missing:
        errors.append(f"missing canonical evidence category {category_id!r}")
    for category_id in unknown:
        errors.append(f"unknown evidence category {category_id!r}")


def load_phase_defaults(data: dict[str, Any], errors: list[str]) -> dict[str, dict[str, str]]:
    raw_defaults = data.get("phase_defaults")
    if not isinstance(raw_defaults, dict):
        errors.append("parity manifest field 'phase_defaults' must be an object")
        return {}

    phase_defaults: dict[str, dict[str, str]] = {}
    for phase in MIGRATION_PHASES:
        raw_phase = raw_defaults.get(phase)
        if not isinstance(raw_phase, dict):
            errors.append(f"phase_defaults.{phase} must be an object")
            continue

        phase_statuses: dict[str, str] = {}
        for category in CANONICAL_CATEGORIES:
            if category not in raw_phase:
                errors.append(f"phase_defaults.{phase} is missing category {category!r}")
                continue
            status = status_from_entry(
                raw_phase[category],
                f"phase_defaults.{phase}.{category}",
                errors,
            )
            if status is not None:
                phase_statuses[category] = status

        for category in sorted(set(raw_phase) - set(CANONICAL_CATEGORIES)):
            errors.append(f"phase_defaults.{phase} has unknown category {category!r}")
        phase_defaults[phase] = phase_statuses

    for phase in sorted(set(raw_defaults) - set(MIGRATION_PHASES)):
        errors.append(f"phase_defaults has unknown migration phase {phase!r}")

    return phase_defaults


def load_parity_routes(data: dict[str, Any], errors: list[str]) -> dict[str, ParityRoute]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append("parity manifest field 'routes' must be a list")
        return {}

    parity_routes: dict[str, ParityRoute] = {}
    duplicate_route_ids: set[str] = set()
    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            errors.append(f"parity route at index {index} must be an object")
            continue

        label = route_label(route, index, "parity")
        route_id = route.get("id")
        if not isinstance(route_id, str) or not route_id:
            errors.append(f"{label} field 'id' must be a non-empty string")
            continue
        if route_id in parity_routes:
            duplicate_route_ids.add(route_id)
            continue

        evidence: dict[str, str] = {}
        raw_evidence = route.get("evidence", {})
        if not isinstance(raw_evidence, dict):
            errors.append(f"{label} field 'evidence' must be an object when present")
        else:
            for category, entry in raw_evidence.items():
                if category not in CANONICAL_CATEGORIES:
                    errors.append(f"{label} evidence has unknown category {category!r}")
                    continue
                status = status_from_entry(
                    entry,
                    f"{label} evidence.{category}",
                    errors,
                )
                if status is not None:
                    evidence[category] = status

        parity_routes[route_id] = ParityRoute(route_id=route_id, evidence=evidence)

    for route_id in sorted(duplicate_route_ids):
        errors.append(f"duplicate parity route id {route_id!r}")

    return parity_routes


def validate_route_coverage(
    smoke_routes: dict[str, SmokeRoute],
    parity_routes: dict[str, ParityRoute],
    errors: list[str],
) -> None:
    smoke_ids = set(smoke_routes)
    parity_ids = set(parity_routes)

    for route_id in sorted(smoke_ids - parity_ids):
        errors.append(f"missing parity checklist coverage for route {route_id!r}")
    for route_id in sorted(parity_ids - smoke_ids):
        errors.append(f"parity checklist contains unknown route {route_id!r}")


def effective_statuses(
    smoke_route: SmokeRoute,
    parity_route: ParityRoute,
    phase_defaults: dict[str, dict[str, str]],
    errors: list[str],
) -> dict[str, str]:
    defaults = phase_defaults.get(smoke_route.migration_phase)
    if defaults is None:
        errors.append(
            f"route {smoke_route.route_id!r} uses migration_phase "
            f"{smoke_route.migration_phase!r} without phase defaults"
        )
        return {}

    statuses: dict[str, str] = {}
    for category in CANONICAL_CATEGORIES:
        if category in parity_route.evidence:
            statuses[category] = parity_route.evidence[category]
        elif category in defaults:
            statuses[category] = defaults[category]
        else:
            errors.append(
                f"route {smoke_route.route_id!r} category {category!r} has no default or override"
            )
    return statuses


def validate_parity_manifest(
    smoke_data: dict[str, Any],
    parity_data: dict[str, Any],
) -> ParityReport:
    report = ParityReport()

    schema = parity_data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected parity schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    validate_categories(parity_data, report.errors)
    phase_defaults = load_phase_defaults(parity_data, report.errors)
    smoke_routes = load_smoke_routes(smoke_data, report.errors)
    parity_routes = load_parity_routes(parity_data, report.errors)
    validate_route_coverage(smoke_routes, parity_routes, report.errors)

    for route_id in sorted(set(smoke_routes) & set(parity_routes)):
        smoke_route = smoke_routes[route_id]
        parity_route = parity_routes[route_id]
        report.routes_checked += 1
        report.phase_counts[smoke_route.migration_phase] += 1

        statuses = effective_statuses(smoke_route, parity_route, phase_defaults, report.errors)
        for category, status in statuses.items():
            if status == "pending":
                report.pending_counts[category] += 1
            elif status == "complete":
                report.complete_counts[category] += 1

            if smoke_route.migration_phase == "parity_ready" and status != "complete":
                report.errors.append(
                    f"parity_ready route {route_id!r} category {category!r} is "
                    f"{status}; completed evidence is required"
                )

        if smoke_route.migration_phase == "parity_ready":
            report.parity_ready_routes += 1

    return report


def ordered_counts(counter: Counter[str], keys: tuple[str, ...]) -> dict[str, int]:
    return {key: counter[key] for key in keys}


def format_json_report(report: ParityReport) -> str:
    payload = {
        "ok": report.ok(),
        "routes_checked": report.routes_checked,
        "categories_checked": report.categories_checked,
        "phase_counts": ordered_counts(report.phase_counts, MIGRATION_PHASES),
        "pending_counts": ordered_counts(report.pending_counts, CANONICAL_CATEGORIES),
        "complete_counts": ordered_counts(report.complete_counts, CANONICAL_CATEGORIES),
        "parity_ready_routes": report.parity_ready_routes,
        "errors": report.errors,
    }
    return json.dumps(payload, indent=2)


def format_count_line(label: str, counter: Counter[str], keys: tuple[str, ...]) -> str:
    return f"{label}: " + ", ".join(f"{key}={counter[key]}" for key in keys)


def print_text_report(report: ParityReport) -> None:
    print("RmlUi parity checklist manifest:")
    print(f"  Routes checked: {report.routes_checked}")
    print(f"  Categories checked: {report.categories_checked}")
    print(f"  Parity-ready routes: {report.parity_ready_routes}")
    print("  " + format_count_line("Phases", report.phase_counts, MIGRATION_PHASES))
    print("  " + format_count_line("Pending", report.pending_counts, CANONICAL_CATEGORIES))
    print("  " + format_count_line("Complete", report.complete_counts, CANONICAL_CATEGORIES))

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi parity checklist check passed.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--parity-manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_parity_manifest.json"),
        help="Path to the RmlUi parity checklist manifest JSON.",
    )
    parser.add_argument(
        "--smoke-manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_manifest.json"),
        help="Path to the RmlUi smoke route manifest JSON.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve relative manifest paths.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    parity_manifest_path = resolve_path(repo_root, args.parity_manifest).resolve()
    smoke_manifest_path = resolve_path(repo_root, args.smoke_manifest).resolve()

    try:
        smoke_data = read_json_object(smoke_manifest_path, "RmlUi smoke manifest")
        parity_data = read_json_object(parity_manifest_path, "RmlUi parity manifest")
        report = validate_parity_manifest(smoke_data, parity_data)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            print(
                json.dumps(
                    {
                        "ok": False,
                        "routes_checked": 0,
                        "categories_checked": len(CANONICAL_CATEGORIES),
                        "phase_counts": ordered_counts(Counter(), MIGRATION_PHASES),
                        "pending_counts": ordered_counts(Counter(), CANONICAL_CATEGORIES),
                        "complete_counts": ordered_counts(Counter(), CANONICAL_CATEGORIES),
                        "parity_ready_routes": 0,
                        "errors": [f"Failed to validate RmlUi parity manifest: {exc}"],
                    },
                    indent=2,
                )
            )
        else:
            print(f"Failed to validate RmlUi parity manifest: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print(format_json_report(report))
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
