#!/usr/bin/env python3
"""Summarize WORR RmlUi route migration progress."""

from __future__ import annotations

import argparse
import importlib
import importlib.util
import inspect
import json
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    from tools.ui_smoke import check_rmlui_parity_manifest as parity_manifest
except ModuleNotFoundError:  # pragma: no cover - used when run from this directory.
    import check_rmlui_parity_manifest as parity_manifest

try:
    from tools.ui_smoke import check_rmlui_command_inventory as command_inventory
except ModuleNotFoundError:  # pragma: no cover - used when run from this directory.
    import check_rmlui_command_inventory as command_inventory

try:
    from tools.ui_smoke import check_rmlui_cvar_inventory as cvar_inventory
except ModuleNotFoundError:  # pragma: no cover - used when run from this directory.
    import check_rmlui_cvar_inventory as cvar_inventory


SUMMARY_FIELDS = ("wave", "owner", "status", "migration_phase")
MIGRATION_PHASE_ORDER = (
    "starter",
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)
MISSING_VALUE = "<missing>"
DEFAULT_ROUTE_METADATA_ROOT = Path("assets/ui/rml")
DEFAULT_PARITY_MANIFEST = Path("tools/ui_smoke/rmlui_parity_manifest.json")
DATA_MODEL_INVENTORY_MODULE = "tools.ui_smoke.check_rmlui_data_model_inventory"
DATA_MODEL_INVENTORY_SCRIPT = Path(__file__).with_name(
    "check_rmlui_data_model_inventory.py"
)
CONDITION_INVENTORY_MODULE = "tools.ui_smoke.check_rmlui_condition_inventory"
CONDITION_INVENTORY_SCRIPT = Path(__file__).with_name(
    "check_rmlui_condition_inventory.py"
)
METADATA_SYNC_MODULE = "tools.ui_smoke.check_rmlui_metadata_sync"
METADATA_SYNC_SCRIPT = Path(__file__).with_name("check_rmlui_metadata_sync.py")
EVENT_INVENTORY_MODULE = "tools.ui_smoke.check_rmlui_event_inventory"
EVENT_INVENTORY_SCRIPT = Path(__file__).with_name("check_rmlui_event_inventory.py")
A11Y_INVENTORY_MODULE = "tools.ui_smoke.check_rmlui_a11y_inventory"
A11Y_INVENTORY_SCRIPT = Path(__file__).with_name("check_rmlui_a11y_inventory.py")
LEGACY_REMOVAL_MODULE = "tools.ui_smoke.check_rmlui_legacy_removal"
LEGACY_REMOVAL_SCRIPT = Path(__file__).with_name("check_rmlui_legacy_removal.py")
DEFAULT_LEGACY_REMOVAL_MANIFEST = Path(
    "tools/ui_smoke/rmlui_legacy_removal_manifest.json"
)


class ManifestShapeError(ValueError):
    """Raised when a manifest is readable JSON but not shaped like a route manifest."""


@dataclass
class ControllerContractSummary:
    total_references: int = 0
    routes_with_contracts: int = 0
    category_counts: Counter[str] = field(default_factory=Counter)
    migration_phase_counts: Counter[str] = field(default_factory=Counter)


@dataclass
class ParityChecklistSummary:
    manifest_path: Path
    repo_root: Path
    routes_checked: int = 0
    categories_checked: int = 0
    parity_ready_routes: int = 0
    pending_counts: Counter[str] = field(default_factory=Counter)
    complete_counts: Counter[str] = field(default_factory=Counter)


@dataclass
class CommandInventorySummary:
    ok: bool = True
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    direct_command_refs: int = 0
    cvar_command_refs: int = 0
    unique_command_tokens: int = 0
    unique_cvar_command_refs: int = 0
    malformed_command_attributes: int = 0
    routes_with_command_hooks: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class CvarInventorySummary:
    ok: bool = True
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    direct_cvar_refs: int = 0
    label_cvar_refs: int = 0
    command_cvar_refs: int = 0
    condition_cvar_refs: int = 0
    total_cvar_refs: int = 0
    unique_cvars: int = 0
    routes_with_cvar_hooks: int = 0
    dynamic_values_skipped: int = 0
    unknown_or_bad_tokens: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class DataModelInventorySummary:
    available: bool = True
    ok: bool = True
    status: str = "ok"
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_data_binding_refs: int = 0
    unique_model_tokens: int = 0
    component_refs: int = 0
    controller_refs: int = 0
    action_type_refs: int = 0
    slot_refs: int = 0
    routes_with_data_model_hooks: int = 0
    malformed_tokens: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class ConditionInventorySummary:
    available: bool = True
    ok: bool = True
    status: str = "ok"
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_condition_refs: int = 0
    routes_with_condition_hooks: int = 0
    unique_expressions: int = 0
    unique_tokens: int = 0
    malformed_conditions: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class MetadataSyncSummary:
    available: bool = True
    ok: bool = True
    status: str = "ok"
    metadata_files: int = 0
    metadata_routes: int = 0
    matched_routes: int = 0
    support_metadata_routes: int = 0
    central_routes_without_metadata: int = 0
    advanced_missing_metadata: int = 0
    phase_mismatches: int = 0
    document_mismatches: int = 0
    duplicate_metadata_routes: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class EventInventorySummary:
    available: bool = True
    ok: bool = True
    status: str = "ok"
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_event_refs: int = 0
    routes_with_event_hooks: int = 0
    unique_events: int = 0
    malformed_events: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class A11yInventorySummary:
    available: bool = True
    ok: bool = True
    status: str = "ok"
    route_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0
    total_a11y_refs: int = 0
    routes_with_a11y_hooks: int = 0
    unique_localization_keys: int = 0
    unique_roles: int = 0
    malformed_hooks: int = 0
    errors: list[str] = field(default_factory=list)


@dataclass
class LegacyRemovalSummary:
    available: bool = True
    ok: bool = True
    status: str = "ok"
    items_checked: int = 0
    categories_checked: int = 0
    status_counts: dict[str, int] = field(default_factory=dict)
    category_counts: dict[str, int] = field(default_factory=dict)
    missing_task_ids: list[str] = field(default_factory=list)
    ready_or_complete_items: list[str] = field(default_factory=list)
    parity_gate_open: bool = False
    parity_gate_ok: bool = False
    parity_ready_routes: int = 0
    parity_gate_pending_evidence: dict[str, int] = field(default_factory=dict)
    parity_gate_closed_reasons: list[str] = field(default_factory=list)
    parity_gate_errors: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    @property
    def ready_or_complete_count(self) -> int:
        return len(self.ready_or_complete_items)


@dataclass
class ProgressReport:
    manifest_path: Path
    repo_root: Path
    total_routes: int = 0
    required_routes: int = 0
    present_documents: int = 0
    required_documents_present: int = 0
    counters: dict[str, Counter[str]] = field(
        default_factory=lambda: {field_name: Counter() for field_name in SUMMARY_FIELDS}
    )
    controller_contracts: ControllerContractSummary = field(
        default_factory=ControllerContractSummary
    )
    routes_by_phase: dict[str, list[str]] = field(
        default_factory=lambda: {phase: [] for phase in MIGRATION_PHASE_ORDER}
    )
    parity_checklist: ParityChecklistSummary | None = None
    command_inventory: CommandInventorySummary | None = None
    cvar_inventory: CvarInventorySummary | None = None
    data_model_inventory: DataModelInventorySummary | None = None
    condition_inventory: ConditionInventorySummary | None = None
    metadata_sync: MetadataSyncSummary | None = None
    event_inventory: EventInventorySummary | None = None
    a11y_inventory: A11yInventorySummary | None = None
    legacy_removal: LegacyRemovalSummary | None = None

    @property
    def missing_documents(self) -> int:
        return self.total_routes - self.present_documents

    @property
    def required_documents_missing(self) -> int:
        return self.required_routes - self.required_documents_present


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def read_json_object(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ManifestShapeError("manifest root must be a JSON object")
    return data


def resolve_repo_relative_path(repo_root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path.resolve(strict=False)
    return (repo_root / path).resolve(strict=False)


def route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"route {route_id!r}"
    return f"route at index {index}"


def route_progress_id(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return route_id
    return f"<index:{index}>"


def counter_value(route: dict[str, Any], field_name: str, label: str, errors: list[str]) -> str:
    value = route.get(field_name)
    if value is None:
        return MISSING_VALUE
    if not isinstance(value, str) or not value:
        errors.append(f"{label} field {field_name!r} must be a non-empty string when present")
        return MISSING_VALUE
    return value


def resolve_document_path(repo_root: Path, document: str, label: str, errors: list[str]) -> Path | None:
    document_path = Path(document)
    if document_path.is_absolute():
        errors.append(f"{label} document path must be repo-relative: {document}")
        return None

    resolved = (repo_root / document_path).resolve(strict=False)
    try:
        resolved.relative_to(repo_root)
    except ValueError:
        errors.append(f"{label} document path escapes repo root: {document}")
        return None
    return resolved


def build_progress_report(data: dict[str, Any], repo_root: Path, manifest_path: Path) -> ProgressReport:
    routes = data.get("routes")
    if not isinstance(routes, list):
        raise ManifestShapeError("manifest field 'routes' must be a list")

    repo_root = repo_root.resolve()
    report = ProgressReport(manifest_path=manifest_path.resolve(), repo_root=repo_root)
    report.total_routes = len(routes)
    errors: list[str] = []

    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            errors.append(f"route at index {index} must be an object")
            continue

        label = route_label(route, index)
        migration_phase = MISSING_VALUE
        for field_name in SUMMARY_FIELDS:
            value = counter_value(route, field_name, label, errors)
            report.counters[field_name][value] += 1
            if field_name == "migration_phase":
                migration_phase = value
        report.routes_by_phase.setdefault(migration_phase, []).append(
            route_progress_id(route, index)
        )

        required_now = route.get("required_now", False)
        if not isinstance(required_now, bool):
            errors.append(f"{label} field 'required_now' must be a boolean when present")
            required_now = False
        if required_now:
            report.required_routes += 1

        document = route.get("document")
        if not isinstance(document, str) or not document:
            errors.append(f"{label} field 'document' must be a non-empty string")
            continue

        resolved_document = resolve_document_path(repo_root, document, label, errors)
        if resolved_document is None:
            continue
        if resolved_document.is_file():
            report.present_documents += 1
            if required_now:
                report.required_documents_present += 1

    if errors:
        raise ManifestShapeError("\n".join(errors))
    return report


def contract_category(
    contract_ref: dict[str, Any],
    label: str,
    errors: list[str],
) -> str:
    category = contract_ref.get("category")
    if not isinstance(category, str) or not category:
        errors.append(f"{label} field 'category' must be a non-empty string")
        return MISSING_VALUE
    return category


def build_controller_contract_summary(
    data: dict[str, Any],
    source_label: str = "route metadata",
) -> ControllerContractSummary:
    routes = data.get("routes")
    if not isinstance(routes, list):
        raise ManifestShapeError(f"{source_label} field 'routes' must be a list")

    summary = ControllerContractSummary()
    errors: list[str] = []

    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            errors.append(f"{source_label} route at index {index} must be an object")
            continue

        label = route_label(route, index)
        controller_contracts = route.get("controller_contracts")
        if controller_contracts is None:
            continue
        if not isinstance(controller_contracts, list):
            errors.append(f"{label} field 'controller_contracts' must be a list")
            continue
        if not controller_contracts:
            continue

        summary.routes_with_contracts += 1
        migration_phase = counter_value(route, "migration_phase", label, errors)
        summary.migration_phase_counts[migration_phase] += 1
        summary.total_references += len(controller_contracts)

        for contract_index, contract_ref in enumerate(controller_contracts):
            contract_label = f"{label} controller_contracts[{contract_index}]"
            if not isinstance(contract_ref, dict):
                errors.append(f"{contract_label} must be an object")
                continue
            category = contract_category(contract_ref, contract_label, errors)
            summary.category_counts[category] += 1

    if errors:
        raise ManifestShapeError("\n".join(errors))
    return summary


def merge_controller_contract_summary(
    target: ControllerContractSummary,
    source: ControllerContractSummary,
) -> None:
    target.total_references += source.total_references
    target.routes_with_contracts += source.routes_with_contracts
    target.category_counts.update(source.category_counts)
    target.migration_phase_counts.update(source.migration_phase_counts)


def discover_route_metadata_paths(repo_root: Path, routes_root: Path) -> list[Path]:
    root = resolve_repo_relative_path(repo_root, routes_root)
    if not root.is_dir():
        return []
    return sorted(path.resolve() for path in root.glob("*/routes.json") if path.is_file())


def load_controller_contract_summary(
    repo_root: Path,
    shell_routes: Path | None,
    routes_root: Path,
) -> ControllerContractSummary:
    repo_root = repo_root.resolve()

    if shell_routes is not None:
        metadata_paths = [resolve_repo_relative_path(repo_root, shell_routes)]
    else:
        metadata_paths = discover_route_metadata_paths(repo_root, routes_root)

    summary = ControllerContractSummary()
    if not metadata_paths:
        return ControllerContractSummary()

    for metadata_path in metadata_paths:
        if not metadata_path.is_file():
            continue
        source_label = display_path(metadata_path, repo_root)
        merge_controller_contract_summary(
            summary,
            build_controller_contract_summary(
                read_json_object(metadata_path),
                source_label,
            ),
        )
    return summary


def load_parity_checklist_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
    parity_manifest_path: Path,
) -> ParityChecklistSummary | None:
    resolved_path = resolve_repo_relative_path(repo_root, parity_manifest_path)
    if not resolved_path.is_file():
        return None

    parity_data = read_json_object(resolved_path)
    parity_report = parity_manifest.validate_parity_manifest(smoke_data, parity_data)
    if not parity_report.ok():
        raise ManifestShapeError(
            "parity checklist manifest failed validation:\n"
            + "\n".join(parity_report.errors)
        )

    return ParityChecklistSummary(
        manifest_path=resolved_path,
        repo_root=repo_root.resolve(),
        routes_checked=parity_report.routes_checked,
        categories_checked=parity_report.categories_checked,
        parity_ready_routes=parity_report.parity_ready_routes,
        pending_counts=parity_report.pending_counts,
        complete_counts=parity_report.complete_counts,
    )


def load_command_inventory_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
) -> CommandInventorySummary:
    inventory_report = command_inventory.validate_command_inventory(
        smoke_data,
        repo_root.resolve(),
    )
    return CommandInventorySummary(
        ok=inventory_report.ok(),
        route_count=inventory_report.stats.route_count,
        documents_checked=inventory_report.stats.documents_checked,
        documents_missing=inventory_report.stats.documents_missing,
        direct_command_refs=inventory_report.stats.direct_command_refs,
        cvar_command_refs=inventory_report.stats.cvar_command_refs,
        unique_command_tokens=len(inventory_report.command_tokens),
        unique_cvar_command_refs=len(inventory_report.command_cvars),
        malformed_command_attributes=len(inventory_report.problems),
        routes_with_command_hooks=len(inventory_report.routes_with_command_hooks),
        errors=list(inventory_report.errors),
    )


def load_cvar_inventory_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
) -> CvarInventorySummary:
    inventory_report = cvar_inventory.build_cvar_inventory(
        smoke_data,
        repo_root.resolve(),
    )
    return CvarInventorySummary(
        ok=inventory_report.ok(),
        route_count=inventory_report.stats.route_count,
        documents_checked=inventory_report.stats.documents_checked,
        documents_missing=inventory_report.stats.documents_missing,
        direct_cvar_refs=inventory_report.stats.direct_cvar_refs,
        label_cvar_refs=inventory_report.stats.label_cvar_refs,
        command_cvar_refs=inventory_report.stats.command_cvar_refs,
        condition_cvar_refs=inventory_report.stats.condition_cvar_refs,
        total_cvar_refs=len(inventory_report.references),
        unique_cvars=len(inventory_report.unique_cvars),
        routes_with_cvar_hooks=len(inventory_report.routes_with_cvar_hooks),
        dynamic_values_skipped=inventory_report.stats.dynamic_values_skipped,
        unknown_or_bad_tokens=len(inventory_report.bad_tokens),
        errors=list(inventory_report.errors),
    )


def unavailable_data_model_inventory(reason: str) -> DataModelInventorySummary:
    return DataModelInventorySummary(
        available=False,
        ok=False,
        status="unavailable",
        errors=[reason],
    )


def error_data_model_inventory(reason: str) -> DataModelInventorySummary:
    return DataModelInventorySummary(
        available=True,
        ok=False,
        status="error",
        errors=[reason],
    )


def unavailable_condition_inventory(reason: str) -> ConditionInventorySummary:
    return ConditionInventorySummary(
        available=False,
        ok=False,
        status="unavailable",
        errors=[reason],
    )


def error_condition_inventory(reason: str) -> ConditionInventorySummary:
    return ConditionInventorySummary(
        available=True,
        ok=False,
        status="error",
        errors=[reason],
    )


def unavailable_metadata_sync(reason: str) -> MetadataSyncSummary:
    return MetadataSyncSummary(
        available=False,
        ok=False,
        status="unavailable",
        errors=[reason],
    )


def error_metadata_sync(reason: str) -> MetadataSyncSummary:
    return MetadataSyncSummary(
        available=True,
        ok=False,
        status="error",
        errors=[reason],
    )


def unavailable_event_inventory(reason: str) -> EventInventorySummary:
    return EventInventorySummary(
        available=False,
        ok=False,
        status="unavailable",
        errors=[reason],
    )


def error_event_inventory(reason: str) -> EventInventorySummary:
    return EventInventorySummary(
        available=True,
        ok=False,
        status="error",
        errors=[reason],
    )


def unavailable_a11y_inventory(reason: str) -> A11yInventorySummary:
    return A11yInventorySummary(
        available=False,
        ok=False,
        status="unavailable",
        errors=[reason],
    )


def error_a11y_inventory(reason: str) -> A11yInventorySummary:
    return A11yInventorySummary(
        available=True,
        ok=False,
        status="error",
        errors=[reason],
    )


def unavailable_legacy_removal(reason: str) -> LegacyRemovalSummary:
    return LegacyRemovalSummary(
        available=False,
        ok=False,
        status="unavailable",
        errors=[reason],
    )


def error_legacy_removal(reason: str) -> LegacyRemovalSummary:
    return LegacyRemovalSummary(
        available=True,
        ok=False,
        status="error",
        errors=[reason],
    )


def value_from_summary(source: Any, *names: str) -> Any:
    if isinstance(source, dict):
        for name in names:
            if name in source:
                return source[name]
        stats = source.get("stats")
        if isinstance(stats, dict):
            value = value_from_summary(stats, *names)
            if value is not None:
                return value
        return None

    for name in names:
        if hasattr(source, name):
            return getattr(source, name)
    stats = getattr(source, "stats", None)
    if stats is not None:
        value = value_from_summary(stats, *names)
        if value is not None:
            return value
    return None


def count_from_summary(source: Any, *names: str) -> int:
    value = value_from_summary(source, *names)
    if value is None:
        return 0
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, dict) and "count" in value:
        try:
            return int(value["count"])
        except (TypeError, ValueError):
            return 0
    if isinstance(value, (list, tuple, set, frozenset, dict)):
        return len(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def bool_from_summary(source: Any, default: bool = True) -> bool:
    ok_method = getattr(source, "ok", None)
    if callable(ok_method):
        return bool(ok_method())
    value = value_from_summary(source, "ok", "valid")
    if value is None:
        errors = value_from_summary(source, "errors")
        if isinstance(errors, (list, tuple, set, frozenset)):
            return len(errors) == 0
        return default
    return bool(value)


def list_from_summary(source: Any, *names: str) -> list[str]:
    value = value_from_summary(source, *names)
    if value is None:
        return []
    if isinstance(value, str):
        return [value] if value else []
    if isinstance(value, (list, tuple, set, frozenset)):
        return [str(item) for item in value]
    return [str(value)]


def dict_from_summary(source: Any, *names: str) -> dict[str, int]:
    value = value_from_summary(source, *names)
    if not isinstance(value, dict):
        return {}

    counts: dict[str, int] = {}
    for key, raw_count in value.items():
        if isinstance(raw_count, bool):
            counts[str(key)] = int(raw_count)
            continue
        if isinstance(raw_count, int):
            counts[str(key)] = raw_count
            continue
        if isinstance(raw_count, dict) and "count" in raw_count:
            try:
                counts[str(key)] = int(raw_count["count"])
            except (TypeError, ValueError):
                counts[str(key)] = 0
            continue
        try:
            counts[str(key)] = int(raw_count)
        except (TypeError, ValueError):
            counts[str(key)] = 0
    return counts


def call_optional_checker_api(
    function: Any,
    smoke_data: dict[str, Any],
    repo_root: Path,
    manifest_path: Path,
) -> Any:
    signature = inspect.signature(function)
    parameters = list(signature.parameters.values())
    has_varargs = any(
        parameter.kind is inspect.Parameter.VAR_POSITIONAL
        for parameter in parameters
    )
    positional_capacity = sum(
        parameter.kind
        in (
            inspect.Parameter.POSITIONAL_ONLY,
            inspect.Parameter.POSITIONAL_OR_KEYWORD,
        )
        for parameter in parameters
    )
    keyword_names = {
        parameter.name
        for parameter in parameters
        if parameter.kind
        in (
            inspect.Parameter.POSITIONAL_OR_KEYWORD,
            inspect.Parameter.KEYWORD_ONLY,
        )
    }

    kwargs: dict[str, Any] = {}
    if "repo_root" in keyword_names:
        kwargs["repo_root"] = repo_root.resolve()
    if "manifest_path" in keyword_names:
        kwargs["manifest_path"] = manifest_path.resolve()

    if kwargs:
        return function(smoke_data, **kwargs)
    if has_varargs or positional_capacity >= 3:
        return function(smoke_data, repo_root.resolve(), manifest_path.resolve())
    if positional_capacity >= 2:
        return function(smoke_data, repo_root.resolve())
    if positional_capacity >= 1:
        return function(smoke_data)
    return function()


def coerce_data_model_inventory_summary(source: Any) -> DataModelInventorySummary:
    if isinstance(source, dict) and isinstance(source.get("data_model_inventory"), dict):
        source = source["data_model_inventory"]

    errors = list_from_summary(source, "errors")
    errors.extend(list_from_summary(source, "error"))
    ok = bool_from_summary(source, default=not errors)

    total_refs = count_from_summary(
        source,
        "total_data_binding_refs",
        "total_model_binding_refs",
        "total_binding_refs",
        "total_model_refs",
        "total_references",
        "total_refs",
        "data_binding_refs",
        "model_refs",
    )
    if total_refs == 0:
        total_refs = count_from_summary(source, "references")

    malformed_tokens = count_from_summary(
        source,
        "malformed_tokens",
        "malformed_data_model_tokens",
        "malformed_attributes",
        "problems",
        "bad_tokens",
    )

    return DataModelInventorySummary(
        available=True,
        ok=ok,
        status="ok" if ok else "error",
        route_count=count_from_summary(source, "route_count", "routes", "routes_checked"),
        documents_checked=count_from_summary(source, "documents_checked", "docs_checked"),
        documents_missing=count_from_summary(source, "documents_missing", "docs_missing"),
        total_data_binding_refs=total_refs,
        unique_model_tokens=count_from_summary(
            source,
            "unique_model_tokens",
            "model_tokens",
            "models",
            "unique_models",
        ),
        component_refs=count_from_summary(
            source,
            "component_refs",
            "component_references",
            "components",
        ),
        controller_refs=count_from_summary(
            source,
            "controller_refs",
            "controller_references",
            "controllers",
        ),
        action_type_refs=count_from_summary(
            source,
            "action_type_refs",
            "action_type_references",
            "action_types",
            "actions",
        ),
        slot_refs=count_from_summary(
            source,
            "slot_refs",
            "slot_references",
            "slots",
        ),
        routes_with_data_model_hooks=count_from_summary(
            source,
            "routes_with_data_model_hooks",
            "routes_with_model_hooks",
            "routes_with_hooks",
        ),
        malformed_tokens=malformed_tokens,
        errors=errors,
    )


def run_data_model_inventory_subprocess(
    checker_path: Path,
    repo_root: Path,
    manifest_path: Path,
) -> DataModelInventorySummary:
    result = subprocess.run(
        [
            sys.executable,
            str(checker_path),
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            "json",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            if not detail:
                detail = result.stdout.strip() or f"checker exited with code {result.returncode}"
            return error_data_model_inventory(detail)
        summary = coerce_data_model_inventory_summary(payload)
        summary.ok = False
        summary.status = "error"
        if not detail and not summary.errors:
            detail = f"checker exited with code {result.returncode}"
        if detail and detail not in summary.errors:
            summary.errors.append(detail)
        return summary

    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        return error_data_model_inventory(f"checker emitted invalid JSON: {exc}")
    return coerce_data_model_inventory_summary(payload)


def load_data_model_inventory_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
    manifest_path: Path,
) -> DataModelInventorySummary:
    checker_path = DATA_MODEL_INVENTORY_SCRIPT
    if not checker_path.is_file():
        return unavailable_data_model_inventory(
            f"data-model inventory checker not found: {checker_path.name}"
        )

    try:
        module = importlib.import_module(DATA_MODEL_INVENTORY_MODULE)
    except ImportError:
        spec = importlib.util.spec_from_file_location(
            "check_rmlui_data_model_inventory",
            checker_path,
        )
        if spec is None or spec.loader is None:
            return error_data_model_inventory(
                f"failed to import data-model checker: {checker_path}"
            )
        try:
            module = importlib.util.module_from_spec(spec)
            sys.modules.setdefault(spec.name, module)
            spec.loader.exec_module(module)
        except Exception as exc:
            return error_data_model_inventory(
                f"failed to import data-model checker: {exc}"
            )

    for function_name in (
        "build_data_model_inventory",
        "validate_data_model_inventory",
        "collect_data_model_inventory",
        "build_inventory",
        "validate_inventory",
    ):
        function = getattr(module, function_name, None)
        if callable(function):
            try:
                report = call_optional_checker_api(
                    function,
                    smoke_data,
                    repo_root,
                    manifest_path,
                )
            except Exception as exc:  # pragma: no cover - defensive adapter.
                return error_data_model_inventory(
                    f"data-model checker API failed: {exc}"
                )
            return coerce_data_model_inventory_summary(report)

    return run_data_model_inventory_subprocess(checker_path, repo_root, manifest_path)


def coerce_condition_inventory_summary(source: Any) -> ConditionInventorySummary:
    if isinstance(source, dict) and isinstance(source.get("condition_inventory"), dict):
        source = source["condition_inventory"]

    errors = list_from_summary(source, "errors")
    errors.extend(list_from_summary(source, "error"))
    ok = bool_from_summary(source, default=not errors)

    total_refs = count_from_summary(
        source,
        "total_condition_refs",
        "condition_refs",
        "condition_references",
        "total_conditions",
        "total_references",
        "total_refs",
    )
    if total_refs == 0:
        total_refs = count_from_summary(source, "references")

    return ConditionInventorySummary(
        available=True,
        ok=ok,
        status="ok" if ok else "error",
        route_count=count_from_summary(source, "route_count", "routes", "routes_checked"),
        documents_checked=count_from_summary(source, "documents_checked", "docs_checked"),
        documents_missing=count_from_summary(source, "documents_missing", "docs_missing"),
        total_condition_refs=total_refs,
        routes_with_condition_hooks=count_from_summary(
            source,
            "routes_with_condition_hooks",
            "routes_with_condition_refs",
            "routes_with_hooks",
        ),
        unique_expressions=count_from_summary(
            source,
            "unique_expressions",
            "unique_condition_expressions",
            "condition_expressions",
            "expressions",
        ),
        unique_tokens=count_from_summary(
            source,
            "unique_tokens",
            "unique_condition_tokens",
            "condition_tokens",
            "tokens",
        ),
        malformed_conditions=count_from_summary(
            source,
            "malformed_conditions",
            "malformed_tokens",
            "malformed_attributes",
            "problems",
            "bad_tokens",
        ),
        errors=errors,
    )


def run_condition_inventory_subprocess(
    checker_path: Path,
    repo_root: Path,
    manifest_path: Path,
) -> ConditionInventorySummary:
    result = subprocess.run(
        [
            sys.executable,
            str(checker_path),
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            "json",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            if not detail:
                detail = result.stdout.strip() or f"checker exited with code {result.returncode}"
            return error_condition_inventory(detail)
        summary = coerce_condition_inventory_summary(payload)
        summary.ok = False
        summary.status = "error"
        if not detail and not summary.errors:
            detail = f"checker exited with code {result.returncode}"
        if detail and detail not in summary.errors:
            summary.errors.append(detail)
        return summary

    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        return error_condition_inventory(f"checker emitted invalid JSON: {exc}")
    return coerce_condition_inventory_summary(payload)


def load_condition_inventory_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
    manifest_path: Path,
) -> ConditionInventorySummary:
    checker_path = CONDITION_INVENTORY_SCRIPT
    if not checker_path.is_file():
        return unavailable_condition_inventory(
            f"condition inventory checker not found: {checker_path.name}"
        )

    try:
        module = importlib.import_module(CONDITION_INVENTORY_MODULE)
    except ImportError:
        spec = importlib.util.spec_from_file_location(
            "check_rmlui_condition_inventory",
            checker_path,
        )
        if spec is None or spec.loader is None:
            return error_condition_inventory(
                f"failed to import condition inventory checker: {checker_path}"
            )
        try:
            module = importlib.util.module_from_spec(spec)
            sys.modules.setdefault(spec.name, module)
            spec.loader.exec_module(module)
        except Exception as exc:
            return error_condition_inventory(
                f"failed to import condition inventory checker: {exc}"
            )

    for function_name in (
        "build_condition_inventory",
        "validate_condition_inventory",
        "collect_condition_inventory",
        "build_inventory",
        "validate_inventory",
    ):
        function = getattr(module, function_name, None)
        if callable(function):
            try:
                report = call_optional_checker_api(
                    function,
                    smoke_data,
                    repo_root,
                    manifest_path,
                )
            except Exception as exc:  # pragma: no cover - defensive adapter.
                return error_condition_inventory(
                    f"condition inventory checker API failed: {exc}"
                )
            return coerce_condition_inventory_summary(report)

    return run_condition_inventory_subprocess(checker_path, repo_root, manifest_path)


def coerce_metadata_sync_summary(source: Any) -> MetadataSyncSummary:
    if isinstance(source, dict) and isinstance(source.get("metadata_sync"), dict):
        source = source["metadata_sync"]

    errors = list_from_summary(source, "errors")
    errors.extend(list_from_summary(source, "error"))
    ok = bool_from_summary(source, default=not errors)

    return MetadataSyncSummary(
        available=True,
        ok=ok,
        status="ok" if ok else "error",
        metadata_files=count_from_summary(
            source,
            "metadata_files",
            "metadata_file_count",
            "route_metadata_files",
        ),
        metadata_routes=count_from_summary(
            source,
            "metadata_routes",
            "metadata_route_count",
            "route_metadata_routes",
        ),
        matched_routes=count_from_summary(
            source,
            "matched_route_count",
            "matched_routes",
            "matches",
        ),
        support_metadata_routes=count_from_summary(
            source,
            "support_metadata_routes",
            "support_metadata_route_count",
            "support_routes",
        ),
        central_routes_without_metadata=count_from_summary(
            source,
            "central_routes_without_metadata",
            "central_routes_without_feature_metadata",
            "routes_without_metadata",
            "missing_metadata_routes",
        ),
        advanced_missing_metadata=count_from_summary(
            source,
            "advanced_missing_metadata",
            "advanced_central_routes_without_feature_metadata",
            "advanced_routes_missing_metadata",
        ),
        phase_mismatches=count_from_summary(
            source,
            "phase_mismatch_count",
            "phase_mismatches",
            "migration_phase_mismatches",
        ),
        document_mismatches=count_from_summary(
            source,
            "document_mismatch_count",
            "document_mismatches",
            "doc_mismatches",
        ),
        duplicate_metadata_routes=count_from_summary(
            source,
            "duplicate_count",
            "duplicate_metadata_routes",
            "duplicate_metadata_route_ids",
            "duplicate_routes",
            "duplicates",
        ),
        errors=errors,
    )


def run_metadata_sync_subprocess(
    checker_path: Path,
    repo_root: Path,
    manifest_path: Path,
) -> MetadataSyncSummary:
    result = subprocess.run(
        [
            sys.executable,
            str(checker_path),
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            "json",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            if not detail:
                detail = result.stdout.strip() or f"checker exited with code {result.returncode}"
            return error_metadata_sync(detail)
        summary = coerce_metadata_sync_summary(payload)
        summary.ok = False
        summary.status = "error"
        if not detail and not summary.errors:
            detail = f"checker exited with code {result.returncode}"
        if detail and detail not in summary.errors:
            summary.errors.append(detail)
        return summary

    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        return error_metadata_sync(f"checker emitted invalid JSON: {exc}")
    return coerce_metadata_sync_summary(payload)


def load_metadata_sync_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
    manifest_path: Path,
) -> MetadataSyncSummary:
    checker_path = METADATA_SYNC_SCRIPT
    if not checker_path.is_file():
        return unavailable_metadata_sync(
            f"metadata sync checker not found: {checker_path.name}"
        )

    try:
        module = importlib.import_module(METADATA_SYNC_MODULE)
    except ImportError:
        spec = importlib.util.spec_from_file_location(
            "check_rmlui_metadata_sync",
            checker_path,
        )
        if spec is None or spec.loader is None:
            return error_metadata_sync(
                f"failed to import metadata sync checker: {checker_path}"
            )
        try:
            module = importlib.util.module_from_spec(spec)
            sys.modules.setdefault(spec.name, module)
            spec.loader.exec_module(module)
        except Exception as exc:
            return error_metadata_sync(
                f"failed to import metadata sync checker: {exc}"
            )

    for function_name in (
        "build_metadata_sync",
        "validate_metadata_sync",
        "collect_metadata_sync",
        "check_metadata_sync",
        "build_sync_report",
        "validate_sync_report",
    ):
        function = getattr(module, function_name, None)
        if callable(function):
            try:
                report = call_optional_checker_api(
                    function,
                    smoke_data,
                    repo_root,
                    manifest_path,
                )
            except Exception as exc:  # pragma: no cover - defensive adapter.
                return error_metadata_sync(f"metadata sync checker API failed: {exc}")
            return coerce_metadata_sync_summary(report)

    return run_metadata_sync_subprocess(checker_path, repo_root, manifest_path)


def coerce_event_inventory_summary(source: Any) -> EventInventorySummary:
    if isinstance(source, dict) and isinstance(source.get("event_inventory"), dict):
        source = source["event_inventory"]

    errors = list_from_summary(source, "errors")
    errors.extend(list_from_summary(source, "error"))
    ok = bool_from_summary(source, default=not errors)

    total_refs = count_from_summary(
        source,
        "total_event_refs",
        "total_event_action_refs",
        "event_refs",
        "event_references",
        "total_events",
        "total_references",
        "total_refs",
    )
    if total_refs == 0:
        total_refs = count_from_summary(source, "references")

    return EventInventorySummary(
        available=True,
        ok=ok,
        status="ok" if ok else "error",
        route_count=count_from_summary(source, "route_count", "routes", "routes_checked"),
        documents_checked=count_from_summary(source, "documents_checked", "docs_checked"),
        documents_missing=count_from_summary(source, "documents_missing", "docs_missing"),
        total_event_refs=total_refs,
        routes_with_event_hooks=count_from_summary(
            source,
            "routes_with_event_hooks",
            "routes_with_event_refs",
            "routes_with_events",
            "routes_with_hooks",
        ),
        unique_events=count_from_summary(
            source,
            "unique_events",
            "unique_event_names",
            "event_names",
            "unique_event_tokens",
            "event_tokens",
            "events",
        ),
        malformed_events=count_from_summary(
            source,
            "malformed_events",
            "malformed_event_attributes",
            "malformed_attributes",
            "problems",
            "bad_tokens",
            "bad_events",
        ),
        errors=errors,
    )


def run_event_inventory_subprocess(
    checker_path: Path,
    repo_root: Path,
    manifest_path: Path,
) -> EventInventorySummary:
    result = subprocess.run(
        [
            sys.executable,
            str(checker_path),
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            "json",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            if not detail:
                detail = result.stdout.strip() or f"checker exited with code {result.returncode}"
            return error_event_inventory(detail)
        summary = coerce_event_inventory_summary(payload)
        summary.ok = False
        summary.status = "error"
        if not detail and not summary.errors:
            detail = f"checker exited with code {result.returncode}"
        if detail and detail not in summary.errors:
            summary.errors.append(detail)
        return summary

    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        return error_event_inventory(f"checker emitted invalid JSON: {exc}")
    return coerce_event_inventory_summary(payload)


def load_event_inventory_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
    manifest_path: Path,
) -> EventInventorySummary:
    checker_path = EVENT_INVENTORY_SCRIPT
    if not checker_path.is_file():
        return unavailable_event_inventory(
            f"event inventory checker not found: {checker_path.name}"
        )

    try:
        module = importlib.import_module(EVENT_INVENTORY_MODULE)
    except ImportError:
        spec = importlib.util.spec_from_file_location(
            "check_rmlui_event_inventory",
            checker_path,
        )
        if spec is None or spec.loader is None:
            return error_event_inventory(
                f"failed to import event inventory checker: {checker_path}"
            )
        try:
            module = importlib.util.module_from_spec(spec)
            sys.modules.setdefault(spec.name, module)
            spec.loader.exec_module(module)
        except Exception as exc:
            return error_event_inventory(
                f"failed to import event inventory checker: {exc}"
            )

    for function_name in (
        "build_event_inventory",
        "validate_event_inventory",
        "collect_event_inventory",
        "build_inventory",
        "validate_inventory",
    ):
        function = getattr(module, function_name, None)
        if callable(function):
            try:
                report = call_optional_checker_api(
                    function,
                    smoke_data,
                    repo_root,
                    manifest_path,
                )
            except Exception as exc:  # pragma: no cover - defensive adapter.
                return error_event_inventory(
                    f"event inventory checker API failed: {exc}"
                )
            return coerce_event_inventory_summary(report)

    return run_event_inventory_subprocess(checker_path, repo_root, manifest_path)


def coerce_a11y_inventory_summary(source: Any) -> A11yInventorySummary:
    if isinstance(source, dict):
        if isinstance(source.get("a11y_inventory"), dict):
            source = source["a11y_inventory"]
        elif isinstance(source.get("accessibility_inventory"), dict):
            source = source["accessibility_inventory"]

    errors = list_from_summary(source, "errors")
    errors.extend(list_from_summary(source, "error"))
    ok = bool_from_summary(source, default=not errors)

    total_refs = count_from_summary(
        source,
        "total_a11y_refs",
        "total_hook_refs",
        "a11y_refs",
        "accessibility_refs",
        "localization_refs",
        "total_localization_refs",
        "total_accessibility_refs",
        "total_references",
        "total_refs",
    )
    if total_refs == 0:
        total_refs = count_from_summary(source, "references")

    return A11yInventorySummary(
        available=True,
        ok=ok,
        status="ok" if ok else "error",
        route_count=count_from_summary(source, "route_count", "routes", "routes_checked"),
        documents_checked=count_from_summary(source, "documents_checked", "docs_checked"),
        documents_missing=count_from_summary(source, "documents_missing", "docs_missing"),
        total_a11y_refs=total_refs,
        routes_with_a11y_hooks=count_from_summary(
            source,
            "routes_with_a11y_hooks",
            "routes_with_a11y_localization_hooks",
            "routes_with_accessibility_hooks",
            "routes_with_localization_hooks",
            "routes_with_a11y_refs",
            "routes_with_hooks",
        ),
        unique_localization_keys=count_from_summary(
            source,
            "unique_localization_keys",
            "localization_keys",
            "unique_l10n_keys",
            "l10n_keys",
            "loc_keys",
        ),
        unique_roles=count_from_summary(
            source,
            "unique_roles",
            "roles",
            "role_tokens",
        ),
        malformed_hooks=count_from_summary(
            source,
            "malformed_hooks",
            "malformed_a11y_attributes",
            "malformed_accessibility_attributes",
            "malformed_attributes",
            "malformed_a11y",
            "problems",
            "bad_tokens",
        ),
        errors=errors,
    )


def run_a11y_inventory_subprocess(
    checker_path: Path,
    repo_root: Path,
    manifest_path: Path,
) -> A11yInventorySummary:
    result = subprocess.run(
        [
            sys.executable,
            str(checker_path),
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            "json",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            if not detail:
                detail = result.stdout.strip() or f"checker exited with code {result.returncode}"
            return error_a11y_inventory(detail)
        summary = coerce_a11y_inventory_summary(payload)
        summary.ok = False
        summary.status = "error"
        if not detail and not summary.errors:
            detail = f"checker exited with code {result.returncode}"
        if detail and detail not in summary.errors:
            summary.errors.append(detail)
        return summary

    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        return error_a11y_inventory(f"checker emitted invalid JSON: {exc}")
    return coerce_a11y_inventory_summary(payload)


def load_a11y_inventory_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
    manifest_path: Path,
) -> A11yInventorySummary:
    checker_path = A11Y_INVENTORY_SCRIPT
    if not checker_path.is_file():
        return unavailable_a11y_inventory(
            f"a11y inventory checker not found: {checker_path.name}"
        )

    try:
        module = importlib.import_module(A11Y_INVENTORY_MODULE)
    except ImportError:
        spec = importlib.util.spec_from_file_location(
            "check_rmlui_a11y_inventory",
            checker_path,
        )
        if spec is None or spec.loader is None:
            return error_a11y_inventory(
                f"failed to import a11y inventory checker: {checker_path}"
            )
        try:
            module = importlib.util.module_from_spec(spec)
            sys.modules.setdefault(spec.name, module)
            spec.loader.exec_module(module)
        except Exception as exc:
            return error_a11y_inventory(
                f"failed to import a11y inventory checker: {exc}"
            )

    for function_name in (
        "build_a11y_inventory",
        "validate_a11y_inventory",
        "collect_a11y_inventory",
        "build_accessibility_inventory",
        "validate_accessibility_inventory",
        "build_inventory",
        "validate_inventory",
    ):
        function = getattr(module, function_name, None)
        if callable(function):
            try:
                report = call_optional_checker_api(
                    function,
                    smoke_data,
                    repo_root,
                    manifest_path,
                )
            except Exception as exc:  # pragma: no cover - defensive adapter.
                return error_a11y_inventory(
                    f"a11y inventory checker API failed: {exc}"
                )
            return coerce_a11y_inventory_summary(report)

    return run_a11y_inventory_subprocess(checker_path, repo_root, manifest_path)


def coerce_legacy_removal_summary(source: Any) -> LegacyRemovalSummary:
    if isinstance(source, dict):
        if isinstance(source.get("legacy_removal"), dict):
            source = source["legacy_removal"]
        elif isinstance(source.get("legacy_removal_inventory"), dict):
            source = source["legacy_removal_inventory"]

    errors = list_from_summary(source, "errors")
    errors.extend(list_from_summary(source, "error"))
    ok = bool_from_summary(source, default=not errors)
    parity_gate = value_from_summary(source, "parity_gate")

    parity_gate_errors = list_from_summary(parity_gate, "errors") if parity_gate is not None else []
    ready_or_complete_items = list_from_summary(
        source,
        "ready_or_complete_items",
        "ready_items",
        "removable_items",
    )
    if not ready_or_complete_items:
        ready_count = count_from_summary(
            source,
            "ready_or_complete_items",
            "ready_or_complete_count",
            "ready_items",
            "removable_items",
        )
        ready_or_complete_items = [f"<count:{ready_count}>"] if ready_count else []

    return LegacyRemovalSummary(
        available=True,
        ok=ok,
        status="ok" if ok else "error",
        items_checked=count_from_summary(source, "items_checked", "items", "item_count"),
        categories_checked=count_from_summary(
            source,
            "categories_checked",
            "categories",
            "category_count",
        ),
        status_counts=dict_from_summary(source, "status_counts", "statuses"),
        category_counts=dict_from_summary(source, "category_counts", "categories_by_id"),
        missing_task_ids=list_from_summary(source, "missing_task_ids", "missing_tasks"),
        ready_or_complete_items=ready_or_complete_items,
        parity_gate_open=bool(value_from_summary(parity_gate, "open", "is_open"))
        if parity_gate is not None
        else False,
        parity_gate_ok=bool_from_summary(parity_gate, default=False)
        if parity_gate is not None
        else False,
        parity_ready_routes=count_from_summary(
            parity_gate,
            "parity_ready_routes",
            "ready_routes",
        )
        if parity_gate is not None
        else 0,
        parity_gate_pending_evidence=dict_from_summary(
            parity_gate,
            "pending_evidence",
            "pending_counts",
        )
        if parity_gate is not None
        else {},
        parity_gate_closed_reasons=list_from_summary(
            parity_gate,
            "closed_reasons",
            "closed_reason",
        )
        if parity_gate is not None
        else [],
        parity_gate_errors=parity_gate_errors,
        errors=errors,
    )


def run_legacy_removal_subprocess(
    checker_path: Path,
    repo_root: Path,
    smoke_manifest_path: Path,
    parity_manifest_path: Path,
    legacy_manifest_path: Path,
) -> LegacyRemovalSummary:
    result = subprocess.run(
        [
            sys.executable,
            str(checker_path),
            "--manifest",
            str(legacy_manifest_path),
            "--parity-manifest",
            str(parity_manifest_path),
            "--smoke-manifest",
            str(smoke_manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            "json",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            if not detail:
                detail = result.stdout.strip() or f"checker exited with code {result.returncode}"
            return error_legacy_removal(detail)
        summary = coerce_legacy_removal_summary(payload)
        summary.ok = False
        summary.status = "error"
        if not detail and not summary.errors:
            detail = f"checker exited with code {result.returncode}"
        if detail and detail not in summary.errors:
            summary.errors.append(detail)
        return summary

    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        return error_legacy_removal(f"checker emitted invalid JSON: {exc}")
    return coerce_legacy_removal_summary(payload)


def load_legacy_removal_summary(
    repo_root: Path,
    smoke_data: dict[str, Any],
    smoke_manifest_path: Path,
    parity_manifest_path: Path,
    legacy_manifest_path: Path,
) -> LegacyRemovalSummary:
    checker_path = LEGACY_REMOVAL_SCRIPT
    if not checker_path.is_file():
        return unavailable_legacy_removal(
            f"legacy-removal checker not found: {checker_path.name}"
        )

    try:
        module = importlib.import_module(LEGACY_REMOVAL_MODULE)
    except ImportError:
        module = None
        spec = importlib.util.spec_from_file_location(
            "check_rmlui_legacy_removal",
            checker_path,
        )
        if spec is not None and spec.loader is not None:
            try:
                module = importlib.util.module_from_spec(spec)
                sys.modules.setdefault(spec.name, module)
                spec.loader.exec_module(module)
            except Exception:
                module = None

    if module is not None:
        for function_name in (
            "build_legacy_removal_inventory",
            "validate_legacy_removal_inventory",
            "collect_legacy_removal_inventory",
            "build_legacy_removal_report",
            "build_inventory",
            "validate_inventory",
        ):
            function = getattr(module, function_name, None)
            if callable(function):
                try:
                    report = call_optional_checker_api(
                        function,
                        smoke_data,
                        repo_root,
                        smoke_manifest_path,
                    )
                except Exception as exc:  # pragma: no cover - defensive adapter.
                    return error_legacy_removal(
                        f"legacy-removal checker API failed: {exc}"
                    )
                return coerce_legacy_removal_summary(report)

    return run_legacy_removal_subprocess(
        checker_path,
        repo_root,
        smoke_manifest_path,
        parity_manifest_path,
        legacy_manifest_path,
    )


def ordered_counter_items(field_name: str, counter: Counter[str]) -> list[tuple[str, int]]:
    if field_name == "migration_phase":
        ordered_keys = [key for key in MIGRATION_PHASE_ORDER if counter[key]]
        extra_keys = sorted(key for key in counter if key not in MIGRATION_PHASE_ORDER)
        keys = ordered_keys + extra_keys
    else:
        keys = sorted(counter)
    return [(key, counter[key]) for key in keys]


def ordered_phase_keys(routes_by_phase: dict[str, list[str]]) -> list[str]:
    extra_keys = sorted(key for key in routes_by_phase if key not in MIGRATION_PHASE_ORDER)
    return [*MIGRATION_PHASE_ORDER, *extra_keys]


def phase_progression(report: ProgressReport) -> dict[str, int | float]:
    starter_routes = report.counters["migration_phase"]["starter"]
    advanced_routes = report.total_routes - starter_routes
    advanced_percent = (
        round((advanced_routes / report.total_routes) * 100, 1)
        if report.total_routes
        else 0.0
    )
    return {
        "starter": report.counters["migration_phase"]["starter"],
        "controller_stub": report.counters["migration_phase"]["controller_stub"],
        "runtime_stub": report.counters["migration_phase"]["runtime_stub"],
        "parity_pending": report.counters["migration_phase"]["parity_pending"],
        "parity_ready": report.counters["migration_phase"]["parity_ready"],
        "advanced_routes": advanced_routes,
        "advanced_percent": advanced_percent,
    }


def format_phase_progression(report: ProgressReport) -> str:
    progression = phase_progression(report)
    return ", ".join(
        [
            f"starter={progression['starter']}",
            f"controller_stub={progression['controller_stub']}",
            f"runtime_stub={progression['runtime_stub']}",
            f"parity_pending={progression['parity_pending']}",
            f"parity_ready={progression['parity_ready']}",
            f"advanced_routes={progression['advanced_routes']}",
            f"advanced_percent={progression['advanced_percent']:.1f}%",
        ]
    )


def json_routes_by_phase(report: ProgressReport) -> list[dict[str, Any]]:
    return [
        {
            "phase": phase,
            "route_ids": sorted(report.routes_by_phase.get(phase, [])),
        }
        for phase in ordered_phase_keys(report.routes_by_phase)
    ]


def format_counter(field_name: str, counter: Counter[str]) -> str:
    items = ordered_counter_items(field_name, counter)
    if not items:
        return "<none>"
    return ", ".join(f"{key}={count}" for key, count in items)


def format_controller_contract_summary(summary: ControllerContractSummary) -> str:
    return (
        f"{summary.total_references} references across "
        f"{summary.routes_with_contracts} routes; "
        f"by category: {format_counter('category', summary.category_counts)}; "
        "by migration_phase: "
        f"{format_counter('migration_phase', summary.migration_phase_counts)}"
    )


def format_ordered_counts(counter: Counter[str], keys: tuple[str, ...]) -> str:
    return ", ".join(f"{key}={counter[key]}" for key in keys)


def format_parity_checklist_summary(summary: ParityChecklistSummary) -> str:
    categories = parity_manifest.CANONICAL_CATEGORIES
    return (
        f"{summary.routes_checked} routes, "
        f"categories={summary.categories_checked}, "
        f"parity_ready_routes={summary.parity_ready_routes}; "
        f"pending: {format_ordered_counts(summary.pending_counts, categories)}; "
        f"complete: {format_ordered_counts(summary.complete_counts, categories)}"
    )


def format_command_inventory_summary(summary: CommandInventorySummary) -> str:
    return (
        f"ok={str(summary.ok).lower()}, "
        f"routes={summary.route_count}, "
        f"documents_checked={summary.documents_checked}, "
        f"documents_missing={summary.documents_missing}, "
        f"direct_commands={summary.direct_command_refs}, "
        f"command_cvars={summary.cvar_command_refs}, "
        f"unique_command_tokens={summary.unique_command_tokens}, "
        f"unique_command_cvars={summary.unique_cvar_command_refs}, "
        f"routes_with_hooks={summary.routes_with_command_hooks}, "
        f"malformed={summary.malformed_command_attributes}"
    )


def format_cvar_inventory_summary(summary: CvarInventorySummary) -> str:
    return (
        f"ok={str(summary.ok).lower()}, "
        f"routes={summary.route_count}, "
        f"documents_checked={summary.documents_checked}, "
        f"documents_missing={summary.documents_missing}, "
        f"direct={summary.direct_cvar_refs}, "
        f"label={summary.label_cvar_refs}, "
        f"command={summary.command_cvar_refs}, "
        f"condition={summary.condition_cvar_refs}, "
        f"total={summary.total_cvar_refs}, "
        f"unique_cvars={summary.unique_cvars}, "
        f"routes_with_hooks={summary.routes_with_cvar_hooks}, "
        f"bad_tokens={summary.unknown_or_bad_tokens}"
    )


def format_data_model_inventory_summary(summary: DataModelInventorySummary) -> str:
    if summary.status != "ok":
        detail = summary.errors[0] if summary.errors else "<none>"
        return (
            f"status={summary.status}, "
            f"ok={str(summary.ok).lower()}, "
            f"error={detail}"
        )

    return (
        f"status={summary.status}, "
        f"ok={str(summary.ok).lower()}, "
        f"routes={summary.route_count}, "
        f"documents_checked={summary.documents_checked}, "
        f"documents_missing={summary.documents_missing}, "
        f"total_refs={summary.total_data_binding_refs}, "
        f"unique_model_tokens={summary.unique_model_tokens}, "
        f"routes_with_hooks={summary.routes_with_data_model_hooks}, "
        f"components={summary.component_refs}, "
        f"controllers={summary.controller_refs}, "
        f"action_types={summary.action_type_refs}, "
        f"slots={summary.slot_refs}, "
        f"malformed={summary.malformed_tokens}"
    )


def format_condition_inventory_summary(summary: ConditionInventorySummary) -> str:
    if summary.status != "ok":
        detail = summary.errors[0] if summary.errors else "<none>"
        return (
            f"status={summary.status}, "
            f"ok={str(summary.ok).lower()}, "
            f"error={detail}"
        )

    return (
        f"status={summary.status}, "
        f"ok={str(summary.ok).lower()}, "
        f"routes={summary.route_count}, "
        f"documents_checked={summary.documents_checked}, "
        f"documents_missing={summary.documents_missing}, "
        f"total_refs={summary.total_condition_refs}, "
        f"routes_with_hooks={summary.routes_with_condition_hooks}, "
        f"unique_expressions={summary.unique_expressions}, "
        f"unique_tokens={summary.unique_tokens}, "
        f"malformed={summary.malformed_conditions}"
    )


def format_metadata_sync_summary(summary: MetadataSyncSummary) -> str:
    if summary.status != "ok":
        detail = summary.errors[0] if summary.errors else "<none>"
        return (
            f"status={summary.status}, "
            f"ok={str(summary.ok).lower()}, "
            f"error={detail}"
        )

    return (
        f"status={summary.status}, "
        f"ok={str(summary.ok).lower()}, "
        f"metadata_files={summary.metadata_files}, "
        f"metadata_routes={summary.metadata_routes}, "
        f"matched_routes={summary.matched_routes}, "
        f"support_metadata_routes={summary.support_metadata_routes}, "
        f"central_without_metadata={summary.central_routes_without_metadata}, "
        f"advanced_missing_metadata={summary.advanced_missing_metadata}, "
        f"phase_mismatches={summary.phase_mismatches}, "
        f"document_mismatches={summary.document_mismatches}, "
        f"duplicate_metadata_routes={summary.duplicate_metadata_routes}"
    )


def format_event_inventory_summary(summary: EventInventorySummary) -> str:
    if summary.status != "ok":
        detail = summary.errors[0] if summary.errors else "<none>"
        return (
            f"status={summary.status}, "
            f"ok={str(summary.ok).lower()}, "
            f"error={detail}"
        )

    return (
        f"status={summary.status}, "
        f"ok={str(summary.ok).lower()}, "
        f"routes={summary.route_count}, "
        f"documents_checked={summary.documents_checked}, "
        f"documents_missing={summary.documents_missing}, "
        f"total_refs={summary.total_event_refs}, "
        f"routes_with_hooks={summary.routes_with_event_hooks}, "
        f"unique_events={summary.unique_events}, "
        f"malformed={summary.malformed_events}"
    )


def format_a11y_inventory_summary(summary: A11yInventorySummary) -> str:
    if summary.status != "ok":
        detail = summary.errors[0] if summary.errors else "<none>"
        return (
            f"status={summary.status}, "
            f"ok={str(summary.ok).lower()}, "
            f"error={detail}"
        )

    return (
        f"status={summary.status}, "
        f"ok={str(summary.ok).lower()}, "
        f"routes={summary.route_count}, "
        f"documents_checked={summary.documents_checked}, "
        f"documents_missing={summary.documents_missing}, "
        f"total_refs={summary.total_a11y_refs}, "
        f"routes_with_hooks={summary.routes_with_a11y_hooks}, "
        f"unique_localization_keys={summary.unique_localization_keys}, "
        f"unique_roles={summary.unique_roles}, "
        f"malformed={summary.malformed_hooks}"
    )


def format_legacy_removal_summary(summary: LegacyRemovalSummary) -> str:
    if summary.status != "ok":
        detail = summary.errors[0] if summary.errors else "<none>"
        return (
            f"status={summary.status}, "
            f"ok={str(summary.ok).lower()}, "
            f"error={detail}"
        )

    status_counts = ", ".join(
        f"{key}={summary.status_counts[key]}"
        for key in sorted(summary.status_counts)
    )
    category_counts = ", ".join(
        f"{key}={summary.category_counts[key]}"
        for key in sorted(summary.category_counts)
    )
    missing_task_ids = (
        ",".join(summary.missing_task_ids) if summary.missing_task_ids else "0"
    )
    ready_or_complete = (
        ",".join(summary.ready_or_complete_items)
        if summary.ready_or_complete_items
        else ""
    )
    parity_gate_state = "open" if summary.parity_gate_open else "closed"

    return (
        f"status={summary.status}, "
        f"ok={str(summary.ok).lower()}, "
        f"items_checked={summary.items_checked}, "
        f"categories_checked={summary.categories_checked}, "
        f"status_counts=({status_counts}), "
        f"category_counts=({category_counts}), "
        f"missing_task_ids={missing_task_ids}, "
        f"ready_or_complete={summary.ready_or_complete_count} [{ready_or_complete}], "
        f"parity_gate={parity_gate_state}, "
        f"parity_ready_routes={summary.parity_ready_routes}"
    )


def format_text_report(report: ProgressReport) -> str:
    lines = [
        "RmlUi route migration progress",
        f"Manifest: {display_path(report.manifest_path, report.repo_root)}",
        f"Total routes: {report.total_routes}",
        (
            "Documents: "
            f"{report.present_documents}/{report.total_routes} present, "
            f"{report.missing_documents} missing"
        ),
        (
            "Required documents: "
            f"{report.required_documents_present}/{report.required_routes} present, "
            f"{report.required_documents_missing} missing"
        ),
        f"By wave: {format_counter('wave', report.counters['wave'])}",
        f"By owner: {format_counter('owner', report.counters['owner'])}",
        f"By status: {format_counter('status', report.counters['status'])}",
        (
            "By migration_phase: "
            f"{format_counter('migration_phase', report.counters['migration_phase'])}"
        ),
        f"Phase progression: {format_phase_progression(report)}",
        f"Controller contracts: {format_controller_contract_summary(report.controller_contracts)}",
    ]
    if report.parity_checklist is not None:
        lines.append(
            f"Parity checklist: {format_parity_checklist_summary(report.parity_checklist)}"
        )
    if report.command_inventory is not None:
        lines.append(
            f"Command inventory: {format_command_inventory_summary(report.command_inventory)}"
        )
    if report.cvar_inventory is not None:
        lines.append(
            f"Cvar inventory: {format_cvar_inventory_summary(report.cvar_inventory)}"
        )
    if report.data_model_inventory is not None:
        lines.append(
            "Data-model inventory: "
            f"{format_data_model_inventory_summary(report.data_model_inventory)}"
        )
    if report.condition_inventory is not None:
        lines.append(
            "Condition inventory: "
            f"{format_condition_inventory_summary(report.condition_inventory)}"
        )
    if report.metadata_sync is not None:
        lines.append(
            "Metadata sync: "
            f"{format_metadata_sync_summary(report.metadata_sync)}"
        )
    if report.event_inventory is not None:
        lines.append(
            "Event inventory: "
            f"{format_event_inventory_summary(report.event_inventory)}"
        )
    if report.a11y_inventory is not None:
        lines.append(
            "A11y inventory: "
            f"{format_a11y_inventory_summary(report.a11y_inventory)}"
        )
    if report.legacy_removal is not None:
        lines.append(
            "Legacy removal: "
            f"{format_legacy_removal_summary(report.legacy_removal)}"
        )
    return "\n".join(lines)


def markdown_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("|", "\\|").replace("\n", " ")


def format_markdown_report(report: ProgressReport) -> str:
    rows = [
        ("Total routes", str(report.total_routes)),
        (
            "Documents",
            f"present={report.present_documents}, missing={report.missing_documents}",
        ),
        (
            "Required documents",
            (
                f"present={report.required_documents_present}, "
                f"missing={report.required_documents_missing}, total={report.required_routes}"
            ),
        ),
        ("Wave", format_counter("wave", report.counters["wave"])),
        ("Owner", format_counter("owner", report.counters["owner"])),
        ("Status", format_counter("status", report.counters["status"])),
        (
            "Migration phase",
            format_counter("migration_phase", report.counters["migration_phase"]),
        ),
        ("Phase progression", format_phase_progression(report)),
        (
            "Controller contracts",
            format_controller_contract_summary(report.controller_contracts),
        ),
    ]
    if report.parity_checklist is not None:
        rows.append(
            (
                "Parity checklist",
                format_parity_checklist_summary(report.parity_checklist),
            )
        )
    if report.command_inventory is not None:
        rows.append(
            (
                "Command inventory",
                format_command_inventory_summary(report.command_inventory),
            )
        )
    if report.cvar_inventory is not None:
        rows.append(
            (
                "Cvar inventory",
                format_cvar_inventory_summary(report.cvar_inventory),
            )
        )
    if report.data_model_inventory is not None:
        rows.append(
            (
                "Data-model inventory",
                format_data_model_inventory_summary(report.data_model_inventory),
            )
        )
    if report.condition_inventory is not None:
        rows.append(
            (
                "Condition inventory",
                format_condition_inventory_summary(report.condition_inventory),
            )
        )
    if report.metadata_sync is not None:
        rows.append(
            (
                "Metadata sync",
                format_metadata_sync_summary(report.metadata_sync),
            )
        )
    if report.event_inventory is not None:
        rows.append(
            (
                "Event inventory",
                format_event_inventory_summary(report.event_inventory),
            )
        )
    if report.a11y_inventory is not None:
        rows.append(
            (
                "A11y inventory",
                format_a11y_inventory_summary(report.a11y_inventory),
            )
        )
    if report.legacy_removal is not None:
        rows.append(
            (
                "Legacy removal",
                format_legacy_removal_summary(report.legacy_removal),
            )
        )
    lines = ["| Group | Counts |", "| --- | --- |"]
    for group, counts in rows:
        lines.append(f"| {markdown_escape(group)} | {markdown_escape(counts)} |")
    return "\n".join(lines)


def json_parity_checklist_summary(summary: ParityChecklistSummary) -> dict[str, Any]:
    categories = parity_manifest.CANONICAL_CATEGORIES
    return {
        "manifest_path": display_path(summary.manifest_path, summary.repo_root),
        "routes_checked": summary.routes_checked,
        "categories_checked": summary.categories_checked,
        "parity_ready_routes": summary.parity_ready_routes,
        "pending_counts": {
            category: summary.pending_counts[category]
            for category in categories
        },
        "complete_counts": {
            category: summary.complete_counts[category]
            for category in categories
        },
    }


def json_command_inventory_summary(summary: CommandInventorySummary) -> dict[str, Any]:
    return {
        "ok": summary.ok,
        "route_count": summary.route_count,
        "documents_checked": summary.documents_checked,
        "documents_missing": summary.documents_missing,
        "direct_command_refs": summary.direct_command_refs,
        "cvar_command_refs": summary.cvar_command_refs,
        "unique_command_tokens": summary.unique_command_tokens,
        "unique_cvar_command_refs": summary.unique_cvar_command_refs,
        "malformed_command_attributes": summary.malformed_command_attributes,
        "routes_with_command_hooks": summary.routes_with_command_hooks,
        "errors": summary.errors,
    }


def json_cvar_inventory_summary(summary: CvarInventorySummary) -> dict[str, Any]:
    return {
        "ok": summary.ok,
        "route_count": summary.route_count,
        "documents_checked": summary.documents_checked,
        "documents_missing": summary.documents_missing,
        "references": {
            "direct": summary.direct_cvar_refs,
            "label": summary.label_cvar_refs,
            "command": summary.command_cvar_refs,
            "condition": summary.condition_cvar_refs,
            "total": summary.total_cvar_refs,
        },
        "unique_cvars": summary.unique_cvars,
        "routes_with_cvar_hooks": summary.routes_with_cvar_hooks,
        "dynamic_values_skipped": summary.dynamic_values_skipped,
        "unknown_or_bad_tokens": summary.unknown_or_bad_tokens,
        "errors": summary.errors,
    }


def json_data_model_inventory_summary(
    summary: DataModelInventorySummary,
) -> dict[str, Any]:
    return {
        "available": summary.available,
        "ok": summary.ok,
        "status": summary.status,
        "route_count": summary.route_count,
        "documents_checked": summary.documents_checked,
        "documents_missing": summary.documents_missing,
        "total_data_binding_refs": summary.total_data_binding_refs,
        "unique_model_tokens": summary.unique_model_tokens,
        "routes_with_data_model_hooks": summary.routes_with_data_model_hooks,
        "references": {
            "component": summary.component_refs,
            "controller": summary.controller_refs,
            "action_type": summary.action_type_refs,
            "slot": summary.slot_refs,
        },
        "malformed_tokens": summary.malformed_tokens,
        "errors": summary.errors,
    }


def json_condition_inventory_summary(
    summary: ConditionInventorySummary,
) -> dict[str, Any]:
    return {
        "available": summary.available,
        "ok": summary.ok,
        "status": summary.status,
        "route_count": summary.route_count,
        "documents_checked": summary.documents_checked,
        "documents_missing": summary.documents_missing,
        "total_condition_refs": summary.total_condition_refs,
        "routes_with_condition_hooks": summary.routes_with_condition_hooks,
        "unique_expressions": summary.unique_expressions,
        "unique_tokens": summary.unique_tokens,
        "malformed_conditions": summary.malformed_conditions,
        "errors": summary.errors,
    }


def json_metadata_sync_summary(summary: MetadataSyncSummary) -> dict[str, Any]:
    return {
        "available": summary.available,
        "ok": summary.ok,
        "status": summary.status,
        "metadata_files": summary.metadata_files,
        "metadata_routes": summary.metadata_routes,
        "matched_routes": summary.matched_routes,
        "support_metadata_routes": summary.support_metadata_routes,
        "central_routes_without_metadata": summary.central_routes_without_metadata,
        "advanced_missing_metadata": summary.advanced_missing_metadata,
        "phase_mismatches": summary.phase_mismatches,
        "document_mismatches": summary.document_mismatches,
        "duplicate_metadata_routes": summary.duplicate_metadata_routes,
        "errors": summary.errors,
    }


def json_event_inventory_summary(summary: EventInventorySummary) -> dict[str, Any]:
    return {
        "available": summary.available,
        "ok": summary.ok,
        "status": summary.status,
        "route_count": summary.route_count,
        "documents_checked": summary.documents_checked,
        "documents_missing": summary.documents_missing,
        "total_event_refs": summary.total_event_refs,
        "routes_with_event_hooks": summary.routes_with_event_hooks,
        "unique_events": summary.unique_events,
        "malformed_events": summary.malformed_events,
        "errors": summary.errors,
    }


def json_a11y_inventory_summary(summary: A11yInventorySummary) -> dict[str, Any]:
    return {
        "available": summary.available,
        "ok": summary.ok,
        "status": summary.status,
        "route_count": summary.route_count,
        "documents_checked": summary.documents_checked,
        "documents_missing": summary.documents_missing,
        "total_a11y_refs": summary.total_a11y_refs,
        "routes_with_a11y_hooks": summary.routes_with_a11y_hooks,
        "unique_localization_keys": summary.unique_localization_keys,
        "unique_roles": summary.unique_roles,
        "malformed_hooks": summary.malformed_hooks,
        "errors": summary.errors,
    }


def json_legacy_removal_summary(summary: LegacyRemovalSummary) -> dict[str, Any]:
    return {
        "available": summary.available,
        "ok": summary.ok,
        "status": summary.status,
        "items_checked": summary.items_checked,
        "categories_checked": summary.categories_checked,
        "status_counts": summary.status_counts,
        "category_counts": summary.category_counts,
        "missing_task_ids": summary.missing_task_ids,
        "ready_or_complete_items": {
            "count": summary.ready_or_complete_count,
            "items": summary.ready_or_complete_items,
        },
        "parity_gate": {
            "open": summary.parity_gate_open,
            "state": "open" if summary.parity_gate_open else "closed",
            "ok": summary.parity_gate_ok,
            "parity_ready_routes": summary.parity_ready_routes,
            "pending_evidence": summary.parity_gate_pending_evidence,
            "closed_reasons": summary.parity_gate_closed_reasons,
            "errors": summary.parity_gate_errors,
        },
        "errors": summary.errors,
    }


def format_json_report(report: ProgressReport) -> str:
    payload = {
        "manifest_path": display_path(report.manifest_path, report.repo_root),
        "total_routes": report.total_routes,
        "documents": {
            "present": report.present_documents,
            "missing": report.missing_documents,
        },
        "required_documents": {
            "present": report.required_documents_present,
            "missing": report.required_documents_missing,
            "total": report.required_routes,
        },
        "grouped_counts": {
            field_name: dict(ordered_counter_items(field_name, report.counters[field_name]))
            for field_name in SUMMARY_FIELDS
        },
        "phase_progression": phase_progression(report),
        "routes_by_phase": json_routes_by_phase(report),
        "controller_contracts": {
            "total_references": report.controller_contracts.total_references,
            "routes_with_contracts": report.controller_contracts.routes_with_contracts,
            "by_category": dict(
                ordered_counter_items("category", report.controller_contracts.category_counts)
            ),
            "by_migration_phase": dict(
                ordered_counter_items(
                    "migration_phase",
                    report.controller_contracts.migration_phase_counts,
                )
            ),
        },
    }
    if report.parity_checklist is not None:
        payload["parity_checklist"] = json_parity_checklist_summary(report.parity_checklist)
    if report.command_inventory is not None:
        payload["command_inventory"] = json_command_inventory_summary(report.command_inventory)
    if report.cvar_inventory is not None:
        payload["cvar_inventory"] = json_cvar_inventory_summary(report.cvar_inventory)
    if report.data_model_inventory is not None:
        payload["data_model_inventory"] = json_data_model_inventory_summary(
            report.data_model_inventory
        )
    if report.condition_inventory is not None:
        payload["condition_inventory"] = json_condition_inventory_summary(
            report.condition_inventory
        )
    if report.metadata_sync is not None:
        payload["metadata_sync"] = json_metadata_sync_summary(report.metadata_sync)
    if report.event_inventory is not None:
        payload["event_inventory"] = json_event_inventory_summary(report.event_inventory)
    if report.a11y_inventory is not None:
        payload["a11y_inventory"] = json_a11y_inventory_summary(report.a11y_inventory)
    if report.legacy_removal is not None:
        payload["legacy_removal"] = json_legacy_removal_summary(report.legacy_removal)
    return json.dumps(payload, indent=2)


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
        choices=("text", "markdown", "json"),
        default="text",
        help="Output format.",
    )
    parser.add_argument(
        "--shell-routes",
        type=Path,
        default=None,
        help=(
            "Optional legacy override for a single route metadata file to use "
            "for controller contract summary."
        ),
    )
    parser.add_argument(
        "--routes-root",
        type=Path,
        default=DEFAULT_ROUTE_METADATA_ROOT,
        help=(
            "Route metadata root used to discover assets/ui/rml/*/routes.json "
            "for controller contract summary."
        ),
    )
    parser.add_argument(
        "--parity-manifest",
        type=Path,
        default=DEFAULT_PARITY_MANIFEST,
        help=(
            "Optional RmlUi parity checklist manifest. The summary is included "
            "when this file exists."
        ),
    )
    parser.add_argument(
        "--legacy-removal-manifest",
        type=Path,
        default=DEFAULT_LEGACY_REMOVAL_MANIFEST,
        help=(
            "Optional RmlUi legacy-removal inventory manifest. The summary is "
            "included with other inventory summaries when the checker exists."
        ),
    )
    parser.add_argument(
        "--no-parity-summary",
        action="store_true",
        help="Skip optional parity checklist summary output.",
    )
    parser.add_argument(
        "--no-inventory-summary",
        action="store_true",
        help="Skip command/cvar inventory summary output.",
    )
    args = parser.parse_args(argv)

    try:
        data = read_json_object(args.manifest)
        report = build_progress_report(data, args.repo_root, args.manifest)
        report.controller_contracts = load_controller_contract_summary(
            args.repo_root,
            args.shell_routes,
            args.routes_root,
        )
        if not args.no_parity_summary:
            report.parity_checklist = load_parity_checklist_summary(
                args.repo_root,
                data,
                args.parity_manifest,
            )
        if not args.no_inventory_summary:
            report.command_inventory = load_command_inventory_summary(
                args.repo_root,
                data,
            )
            report.cvar_inventory = load_cvar_inventory_summary(
                args.repo_root,
                data,
            )
            report.data_model_inventory = load_data_model_inventory_summary(
                args.repo_root,
                data,
                args.manifest,
            )
            report.condition_inventory = load_condition_inventory_summary(
                args.repo_root,
                data,
                args.manifest,
            )
            report.metadata_sync = load_metadata_sync_summary(
                args.repo_root,
                data,
                args.manifest,
            )
            report.event_inventory = load_event_inventory_summary(
                args.repo_root,
                data,
                args.manifest,
            )
            report.a11y_inventory = load_a11y_inventory_summary(
                args.repo_root,
                data,
                args.manifest,
            )
            report.legacy_removal = load_legacy_removal_summary(
                args.repo_root,
                data,
                args.manifest,
                args.parity_manifest,
                args.legacy_removal_manifest,
            )
    except (OSError, json.JSONDecodeError, ManifestShapeError) as exc:
        print(f"Failed to build RmlUi progress report: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print(format_json_report(report))
    elif args.format == "markdown":
        print(format_markdown_report(report))
    else:
        print(format_text_report(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
