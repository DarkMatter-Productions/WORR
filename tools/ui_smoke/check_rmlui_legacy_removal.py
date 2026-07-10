#!/usr/bin/env python3
"""Validate the planning-only RmlUi legacy removal inventory."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import check_rmlui_parity_manifest as parity


EXPECTED_SCHEMA = "worr.rmlui.legacy_removal_manifest.v1"
ALLOWED_STATUSES = ("blocked", "pending", "ready", "complete")
READY_STATUSES = ("ready", "complete")
REQUIRED_TASK_IDS = (
    "FR-09-T10",
    "FR-09-T09",
    "DV-03-T07",
    "DV-07-T04",
)
REQUIRED_ITEM_CATEGORIES = (
    "json_menu_surfaces",
    "legacy_bridge_runtime_fallback",
    "package_staging_cleanup",
    "docs_update",
    "renderer_input_smoke_evidence",
)
TASK_ID_RE = re.compile(r"^(?:FR|DV)-\d{2}-T\d{2}$")
TARGET_LOCATOR_FIELDS = ("path", "function", "command", "route", "evidence", "document")


@dataclass(frozen=True)
class RemovalItem:
    item_id: str
    category: str
    status: str


@dataclass
class ParityGateState:
    ok: bool = False
    parity_ready_routes: int = 0
    pending_evidence: dict[str, int] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def closed_reasons(self) -> list[str]:
        reasons: list[str] = []
        if not self.ok:
            reasons.append("parity manifest validation failed")
        if self.parity_ready_routes == 0:
            reasons.append("parity manifest has zero parity_ready routes")
        incomplete = {
            category: count
            for category, count in self.pending_evidence.items()
            if count > 0
        }
        if incomplete:
            joined = ", ".join(
                f"{category}={count}" for category, count in sorted(incomplete.items())
            )
            reasons.append(f"required parity evidence is incomplete: {joined}")
        return reasons

    def open(self) -> bool:
        return not self.closed_reasons()


@dataclass
class LegacyRemovalReport:
    repo_root: Path
    manifest_path: Path
    parity_manifest_path: Path
    smoke_manifest_path: Path
    items_checked: int = 0
    categories_checked: int = len(REQUIRED_ITEM_CATEGORIES)
    status_counts: Counter[str] = field(default_factory=Counter)
    category_counts: Counter[str] = field(default_factory=Counter)
    task_ids_found: set[str] = field(default_factory=set)
    missing_task_ids: list[str] = field(default_factory=list)
    ready_or_complete_items: list[str] = field(default_factory=list)
    parity_gate: ParityGateState = field(default_factory=ParityGateState)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_path(repo_root: Path, path: Path) -> Path:
    return path if path.is_absolute() else repo_root / path


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve(strict=False).relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def require_string(
    obj: dict[str, Any],
    field_name: str,
    label: str,
    errors: list[str],
) -> str | None:
    value = obj.get(field_name)
    if not isinstance(value, str) or not value:
        errors.append(f"{label} field {field_name!r} must be a non-empty string")
        return None
    return value


def validate_string_list(
    value: Any,
    label: str,
    errors: list[str],
    *,
    allow_empty: bool = False,
) -> list[str]:
    if not isinstance(value, list):
        errors.append(f"{label} must be a list")
        return []
    if not value and not allow_empty:
        errors.append(f"{label} must not be empty")

    strings: list[str] = []
    for index, entry in enumerate(value):
        if not isinstance(entry, str) or not entry:
            errors.append(f"{label}[{index}] must be a non-empty string")
            continue
        strings.append(entry)
    return strings


def validate_task_ids(task_ids: list[str], label: str, errors: list[str]) -> set[str]:
    found: set[str] = set()
    duplicate_ids: set[str] = set()
    for task_id in task_ids:
        if not TASK_ID_RE.match(task_id):
            errors.append(f"{label} task id {task_id!r} must look like FR-09-T10 or DV-03-T07")
            continue
        if task_id in found:
            duplicate_ids.add(task_id)
        found.add(task_id)

    for task_id in sorted(duplicate_ids):
        errors.append(f"{label} has duplicate task id {task_id!r}")
    return found


def validate_cutover_gate(data: dict[str, Any], errors: list[str]) -> tuple[str, ...]:
    gate = data.get("cutover_gate")
    if not isinstance(gate, dict):
        errors.append("field 'cutover_gate' must be an object")
        return ()

    if gate.get("parity_ready_routes_required") is not True:
        errors.append("cutover_gate.parity_ready_routes_required must be true")

    categories = validate_string_list(
        gate.get("required_evidence_categories"),
        "cutover_gate.required_evidence_categories",
        errors,
    )
    duplicate_categories = {
        category for category in categories if categories.count(category) > 1
    }
    for category in sorted(duplicate_categories):
        errors.append(f"cutover_gate.required_evidence_categories has duplicate {category!r}")

    canonical = set(parity.CANONICAL_CATEGORIES)
    for category in sorted(set(categories) - canonical):
        errors.append(f"cutover_gate.required_evidence_categories has unknown category {category!r}")
    for category in parity.CANONICAL_CATEGORIES:
        if category not in categories:
            errors.append(f"cutover_gate.required_evidence_categories is missing {category!r}")
    return tuple(category for category in categories if category in canonical)


def item_label(item: dict[str, Any], index: int) -> str:
    item_id = item.get("id")
    if isinstance(item_id, str) and item_id:
        return f"removal item {item_id!r}"
    return f"removal item at index {index}"


def validate_targets(targets: Any, label: str, errors: list[str]) -> None:
    if not isinstance(targets, list):
        errors.append(f"{label} field 'targets' must be a list")
        return
    if not targets:
        errors.append(f"{label} field 'targets' must not be empty")

    for index, target in enumerate(targets):
        target_label = f"{label} target at index {index}"
        if not isinstance(target, dict):
            errors.append(f"{target_label} must be an object")
            continue
        require_string(target, "kind", target_label, errors)
        has_locator = any(
            isinstance(target.get(field_name), str) and target.get(field_name)
            for field_name in TARGET_LOCATOR_FIELDS
        )
        if not has_locator:
            joined = ", ".join(TARGET_LOCATOR_FIELDS)
            errors.append(f"{target_label} must include at least one locator field: {joined}")


def load_removal_items(
    data: dict[str, Any],
    report: LegacyRemovalReport,
) -> list[RemovalItem]:
    raw_items = data.get("items")
    if not isinstance(raw_items, list):
        report.errors.append("field 'items' must be a list")
        return []
    if not raw_items:
        report.errors.append("field 'items' must not be empty")

    items: list[RemovalItem] = []
    seen_ids: set[str] = set()
    duplicate_ids: set[str] = set()

    for index, raw_item in enumerate(raw_items):
        if not isinstance(raw_item, dict):
            report.errors.append(f"removal item at index {index} must be an object")
            continue

        label = item_label(raw_item, index)
        item_id = require_string(raw_item, "id", label, report.errors)
        category = require_string(raw_item, "category", label, report.errors)
        status = require_string(raw_item, "status", label, report.errors)

        if item_id is not None:
            if item_id in seen_ids:
                duplicate_ids.add(item_id)
            seen_ids.add(item_id)

        if category is not None and category not in REQUIRED_ITEM_CATEGORIES:
            allowed = ", ".join(REQUIRED_ITEM_CATEGORIES)
            report.errors.append(f"{label} category must be one of {allowed}; got {category!r}")

        if status is not None:
            if status not in ALLOWED_STATUSES:
                allowed = ", ".join(ALLOWED_STATUSES)
                report.errors.append(f"{label} status must be one of {allowed}; got {status!r}")
            else:
                report.status_counts[status] += 1
                if status in READY_STATUSES and item_id is not None:
                    report.ready_or_complete_items.append(item_id)

        item_task_ids = validate_string_list(raw_item.get("task_ids"), f"{label}.task_ids", report.errors)
        report.task_ids_found.update(validate_task_ids(item_task_ids, label, report.errors))

        validate_targets(raw_item.get("targets"), label, report.errors)

        blockers = validate_string_list(
            raw_item.get("blockers"),
            f"{label}.blockers",
            report.errors,
            allow_empty=status not in ("blocked", "pending"),
        )
        if status in ("blocked", "pending") and not blockers:
            report.errors.append(f"{label} must list blockers while status is {status!r}")

        if item_id is not None and category is not None and status is not None:
            items.append(RemovalItem(item_id=item_id, category=category, status=status))
            report.items_checked += 1
            report.category_counts[category] += 1

    for item_id in sorted(duplicate_ids):
        report.errors.append(f"duplicate removal item id {item_id!r}")

    for category in REQUIRED_ITEM_CATEGORIES:
        if report.category_counts[category] == 0:
            report.errors.append(f"missing required removal category {category!r}")

    return items


def load_parity_gate_state(
    smoke_data: dict[str, Any],
    parity_data: dict[str, Any],
    required_evidence_categories: tuple[str, ...],
) -> ParityGateState:
    parity_report = parity.validate_parity_manifest(smoke_data, parity_data)
    return ParityGateState(
        ok=parity_report.ok(),
        parity_ready_routes=parity_report.parity_ready_routes,
        pending_evidence={
            category: parity_report.pending_counts[category]
            for category in required_evidence_categories
        },
        errors=list(parity_report.errors),
    )


def validate_legacy_removal_manifest(
    manifest_data: dict[str, Any],
    smoke_data: dict[str, Any],
    parity_data: dict[str, Any],
    *,
    repo_root: Path,
    manifest_path: Path,
    parity_manifest_path: Path,
    smoke_manifest_path: Path,
) -> LegacyRemovalReport:
    report = LegacyRemovalReport(
        repo_root=repo_root,
        manifest_path=manifest_path,
        parity_manifest_path=parity_manifest_path,
        smoke_manifest_path=smoke_manifest_path,
    )

    schema = manifest_data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected legacy removal schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    allowed_statuses = validate_string_list(
        manifest_data.get("allowed_statuses"),
        "allowed_statuses",
        report.errors,
    )
    if tuple(allowed_statuses) != ALLOWED_STATUSES:
        allowed = ", ".join(ALLOWED_STATUSES)
        report.errors.append(f"allowed_statuses must be exactly: {allowed}")

    top_task_ids = validate_string_list(
        manifest_data.get("required_task_ids"),
        "required_task_ids",
        report.errors,
    )
    report.task_ids_found.update(validate_task_ids(top_task_ids, "required_task_ids", report.errors))

    required_evidence_categories = validate_cutover_gate(manifest_data, report.errors)
    items = load_removal_items(manifest_data, report)

    report.missing_task_ids = [
        task_id for task_id in REQUIRED_TASK_IDS if task_id not in report.task_ids_found
    ]
    if report.missing_task_ids:
        missing = ", ".join(report.missing_task_ids)
        report.errors.append(f"missing required legacy-removal task IDs: {missing}")

    report.parity_gate = load_parity_gate_state(
        smoke_data,
        parity_data,
        required_evidence_categories,
    )
    if report.parity_gate.errors:
        for error in report.parity_gate.errors:
            report.errors.append(f"parity manifest: {error}")

    closed_reasons = report.parity_gate.closed_reasons()
    if closed_reasons:
        joined_reasons = "; ".join(closed_reasons)
        for item in items:
            if item.status in READY_STATUSES:
                report.errors.append(
                    f"removal item {item.item_id!r} cannot be {item.status!r} "
                    f"while cutover gate is closed: {joined_reasons}"
                )

    return report


def ordered_counts(counter: Counter[str], keys: tuple[str, ...]) -> dict[str, int]:
    return {key: counter[key] for key in keys}


def json_report_payload(report: LegacyRemovalReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "manifest": display_path(report.manifest_path, report.repo_root),
        "parity_manifest": display_path(report.parity_manifest_path, report.repo_root),
        "smoke_manifest": display_path(report.smoke_manifest_path, report.repo_root),
        "items_checked": report.items_checked,
        "categories_checked": report.categories_checked,
        "status_counts": ordered_counts(report.status_counts, ALLOWED_STATUSES),
        "category_counts": ordered_counts(report.category_counts, REQUIRED_ITEM_CATEGORIES),
        "required_task_ids": list(REQUIRED_TASK_IDS),
        "task_ids_found": sorted(report.task_ids_found),
        "missing_task_ids": report.missing_task_ids,
        "ready_or_complete_items": report.ready_or_complete_items,
        "parity_gate": {
            "open": report.parity_gate.open(),
            "ok": report.parity_gate.ok,
            "parity_ready_routes": report.parity_gate.parity_ready_routes,
            "pending_evidence": report.parity_gate.pending_evidence,
            "closed_reasons": report.parity_gate.closed_reasons(),
            "errors": report.parity_gate.errors,
        },
        "errors": report.errors,
    }


def print_json_report(report: LegacyRemovalReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def print_count_line(label: str, counts: Counter[str], keys: tuple[str, ...]) -> None:
    print(f"  {label}: " + ", ".join(f"{key}={counts[key]}" for key in keys))


def print_report(report: LegacyRemovalReport) -> None:
    print("RmlUi legacy removal inventory:")
    print(f"  Manifest: {display_path(report.manifest_path, report.repo_root)}")
    print(f"  Parity manifest: {display_path(report.parity_manifest_path, report.repo_root)}")
    print(f"  Smoke manifest: {display_path(report.smoke_manifest_path, report.repo_root)}")
    print(f"  Items checked: {report.items_checked}")
    print(f"  Required categories checked: {report.categories_checked}")
    print_count_line("Statuses", report.status_counts, ALLOWED_STATUSES)
    print_count_line("Categories", report.category_counts, REQUIRED_ITEM_CATEGORIES)
    print(
        "  Required task IDs: "
        f"{len(REQUIRED_TASK_IDS) - len(report.missing_task_ids)}/{len(REQUIRED_TASK_IDS)}"
    )
    print(f"  Task IDs found: {', '.join(sorted(report.task_ids_found)) or '-'}")
    print(f"  Ready/complete removal items: {len(report.ready_or_complete_items)}")
    if report.ready_or_complete_items:
        print(f"    - {', '.join(report.ready_or_complete_items)}")

    print("  Parity gate:")
    print(f"    Open: {'yes' if report.parity_gate.open() else 'no'}")
    print(f"    Parity manifest ok: {'yes' if report.parity_gate.ok else 'no'}")
    print(f"    Parity-ready routes: {report.parity_gate.parity_ready_routes}")
    pending = report.parity_gate.pending_evidence
    print(
        "    Pending required evidence: "
        + ", ".join(f"{category}={count}" for category, count in sorted(pending.items()))
    )
    closed_reasons = report.parity_gate.closed_reasons()
    if closed_reasons:
        print("    Closed reasons:")
        for reason in closed_reasons:
            print(f"      - {reason}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi legacy removal inventory check failed.")
    else:
        print("\nResult: RmlUi legacy removal inventory check passed.")


def failure_report(
    message: str,
    *,
    repo_root: Path,
    manifest_path: Path,
    parity_manifest_path: Path,
    smoke_manifest_path: Path,
) -> LegacyRemovalReport:
    report = LegacyRemovalReport(
        repo_root=repo_root,
        manifest_path=manifest_path,
        parity_manifest_path=parity_manifest_path,
        smoke_manifest_path=smoke_manifest_path,
    )
    report.errors.append(f"Failed to validate RmlUi legacy removal inventory: {message}")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_legacy_removal_manifest.json"),
        help="Path to the RmlUi legacy removal inventory manifest JSON.",
    )
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
    manifest_path = resolve_path(repo_root, args.manifest).resolve()
    parity_manifest_path = resolve_path(repo_root, args.parity_manifest).resolve()
    smoke_manifest_path = resolve_path(repo_root, args.smoke_manifest).resolve()

    try:
        manifest_data = read_json_object(manifest_path, "RmlUi legacy removal manifest")
        smoke_data = read_json_object(smoke_manifest_path, "RmlUi smoke manifest")
        parity_data = read_json_object(parity_manifest_path, "RmlUi parity manifest")
        report = validate_legacy_removal_manifest(
            manifest_data,
            smoke_data,
            parity_data,
            repo_root=repo_root,
            manifest_path=manifest_path,
            parity_manifest_path=parity_manifest_path,
            smoke_manifest_path=smoke_manifest_path,
        )
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        report = failure_report(
            str(exc),
            repo_root=repo_root,
            manifest_path=manifest_path,
            parity_manifest_path=parity_manifest_path,
            smoke_manifest_path=smoke_manifest_path,
        )

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
