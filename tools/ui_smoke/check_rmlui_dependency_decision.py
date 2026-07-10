#!/usr/bin/env python3
"""Validate the active RmlUi dependency decision record and remaining guardrails."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_RECORD = Path("docs-dev/rmlui-dependency-decision-record-2026-07-02.md")
REQUIRED_TASK_IDS = (
    "DV-06-T01",
    "FR-09-T02",
    "FR-09-T03",
    "DV-03-T07",
    "DV-07-T04",
)
STATUS_LINE_RE = re.compile(r"(?im)^\s*decision\s+status\s*:\s*(.+?)\s*$")
TASK_ID_RE = re.compile(r"\b(?:FR|DV)-\d{2}-T\d{2}\b")


@dataclass(frozen=True)
class PatternRequirement:
    key: str
    label: str
    patterns: tuple[re.Pattern[str], ...]


@dataclass
class DependencyDecisionReport:
    repo_root: Path
    record_path: Path
    exists: bool = False
    task_ids_found: set[str] = field(default_factory=set)
    missing_task_ids: list[str] = field(default_factory=list)
    status_line: str = ""
    has_proposed_status: bool = False
    has_active_status: bool = False
    has_not_implemented_status: bool = False
    status_overclaim_count: int = 0
    boundary_results: dict[str, bool] = field(default_factory=dict)
    remaining_guardrail_results: dict[str, bool] = field(default_factory=dict)
    native_renderer_results: dict[str, bool] = field(default_factory=dict)
    gate_g1_results: dict[str, bool] = field(default_factory=dict)
    validation_results: dict[str, bool] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve(strict=False).relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def compile_requirement(
    key: str,
    label: str,
    patterns: tuple[str, ...],
) -> PatternRequirement:
    return PatternRequirement(
        key=key,
        label=label,
        patterns=tuple(re.compile(pattern, re.IGNORECASE | re.DOTALL) for pattern in patterns),
    )


IMPLEMENTATION_BOUNDARY_REQUIREMENTS = (
    compile_requirement(
        "compiled_core_adapter",
        "compiled RmlUi Core adapter evidence",
        (
            r"\bcompiled\s+RmlUi\s+Core\s+adapter\b",
            r"\bRmlUi\s+Core\s+compile/link\b",
            r"\bcompile/link\s+slice\s+through\s+RmlUi\s+Core\b",
        ),
    ),
    compile_requirement(
        "optional_build_boundary",
        "default-disabled optional build boundary evidence",
        (
            r"\bdefault-disabled\b",
            r"\boptional\s+RmlUi\s+Core\s+compile/link\b",
            r"\benabled\s+scratch\s+build\b",
        ),
    ),
    compile_requirement(
        "system_file_bridge",
        "WORR-backed RmlUi system/file bridge evidence",
        (
            r"\bSystemInterface\b",
            r"\bFileInterface\b",
            r"\bsystem/file\s+bridge\b",
        ),
    ),
    compile_requirement(
        "worr_filesystem_api",
        "WORR filesystem API bridge evidence",
        (
            r"\bFS_OpenFile\b",
            r"\bFS_Read\b",
            r"\bFS_Seek\b",
            r"\bWORR's\s+filesystem/package\s+search\s+path\b",
        ),
    ),
    compile_requirement(
        "runtime_probe_command",
        "runtime file-probe command evidence",
        (
            r"\bui_rml_runtime_probe\b",
            r"\bruntime-facing\s+file\s+probe\b",
        ),
    ),
)

REMAINING_GUARDRAIL_REQUIREMENTS = (
    compile_requirement(
        "native_renderer_not_implemented",
        "native renderer bridge remains unimplemented",
        (
            r"\bNo\s+native\s+renderer\s+bridge\s+is\s+implemented\b",
            r"\bnative\s+renderer/runtime\s+route\s+ownership\s+not\s+implemented\b",
        ),
    ),
    compile_requirement(
        "route_rendering_not_claimed",
        "route rendering remains unclaimed",
        (
            r"\bNo\s+route(?:s)?\s+opens?\s+or\s+draws?\b",
            r"\bNo\s+route-rendering\s+runtime\b",
            r"\bdoes\s+not\s+claim\s+route\s+ownership\b",
        ),
    ),
    compile_requirement(
        "runtime_switch_not_default",
        "runtime switch remains non-default",
        (
            r"\bNo\s+runtime\s+switch\s+is\s+enabled\s+by\s+default\b",
            r"\bdefault-disabled\b",
        ),
    ),
    compile_requirement(
        "vulkan_not_redirected",
        "Vulkan must not be redirected to OpenGL",
        (
            r"\bNo\s+Vulkan\s+renderer\s+path\s+is\s+redirected\s+to\s+OpenGL\b",
            r"\bno\s+vulkan-to-opengl\s+fallback\b",
        ),
    ),
    compile_requirement(
        "legacy_json_not_removed",
        "legacy JSON path remains intact",
        (
            r"\bNo\s+legacy\s+JSON\s+path\s+is\s+removed\b",
            r"\bDo\s+not\s+remove\s+legacy\s+JSON\b",
        ),
    ),
)

NATIVE_RENDERER_REQUIREMENTS = (
    compile_requirement(
        "opengl_native",
        "OpenGL-native renderer proof",
        (
            r"\bopengl\b[^.\n]*(?:native|bridge|draws?)",
            r"\bopengl-native\b",
        ),
    ),
    compile_requirement(
        "vulkan_native",
        "Vulkan-native renderer proof",
        (
            r"\bvulkan\b[^.\n]*(?:native|bridge|draws?)",
            r"\bvulkan-native\b",
        ),
    ),
    compile_requirement(
        "rtx_vkpt_native",
        "RTX/vkpt renderer proof",
        (
            r"\brtx\s*/\s*vkpt\b[^.\n]*(?:renderer|path[- ]tracing|draws?|native|proof)",
            r"\bvkpt\b[^.\n]*(?:renderer|path[- ]tracing|draws?|native|proof)",
            r"\bpath[- ]tracing\b[^.\n]*(?:renderer|draws?|native|proof)",
        ),
    ),
    compile_requirement(
        "no_vulkan_to_opengl_fallback",
        "Vulkan must not fall back to OpenGL",
        (
            r"\bno\s+vulkan-to-opengl\s+fallback\b",
            r"\bvulkan\s+renderer\s+work\s+must\s+(?:stay|remain)\s+native\b",
            r"\bvulkan\s+work\s+must\s+not\s+fall\s+back\s+to\s+opengl\b",
        ),
    ),
)

GATE_G1_REQUIREMENTS = (
    compile_requirement(
        "system_interface",
        "Gate G1 system interface",
        (r"\|\s*system\s*\|", r"\bsystem\b[^.\n]*(?:time|logging|allocation|shutdown)"),
    ),
    compile_requirement(
        "file_interface",
        "Gate G1 file interface",
        (r"\|\s*file\s*\|", r"\bfile\b[^.\n]*(?:document|style|image|font|filesystem)"),
    ),
    compile_requirement(
        "input_interface",
        "Gate G1 input interface",
        (r"\|\s*input\s*\|", r"\binput\b[^.\n]*(?:keyboard|mouse|gamepad|focus)"),
    ),
    compile_requirement(
        "font_text_interface",
        "Gate G1 font/text interface",
        (r"\|\s*font\s*/?\s*text\s*\|", r"\bfont/text\b", r"\bfont\b[^.\n]*\btext\b"),
    ),
    compile_requirement(
        "runtime_route_controller_integration",
        "Gate G1 runtime route/controller integration",
        (
            r"\|\s*runtime\s+route\s*\|",
            r"\bruntime\s+route\b[^.\n]*(?:controller|command|cvar|conditional)",
            r"\bruntime\s+route/controller\s+integration\b",
        ),
    ),
)

VALIDATION_REQUIREMENTS = (
    compile_requirement(
        "static_validation_commands",
        "static validation commands",
        (r"python\s+tools[\\/]+ui_smoke[\\/]+check_rmlui_", r"report_rmlui_progress\.py"),
    ),
    compile_requirement(
        "asset_staging_commands",
        "asset packaging and staging commands",
        (r"package_assets\.py", r"\.tmp[\\/]+rmlui", r"check_rmlui_runtime_assets\.py"),
    ),
    compile_requirement(
        "runtime_evidence",
        "build/runtime evidence requirements",
        (r"\bbuild\s+and\s+runtime\s+checks\b", r"\bcapture\s+evidence\b"),
    ),
)


def has_any_pattern(text: str, requirement: PatternRequirement) -> bool:
    return any(pattern.search(text) is not None for pattern in requirement.patterns)


def validate_requirements(
    text: str,
    requirements: tuple[PatternRequirement, ...],
    report: DependencyDecisionReport,
    result_attr: str,
    error_prefix: str,
) -> None:
    results: dict[str, bool] = {}
    for requirement in requirements:
        passed = has_any_pattern(text, requirement)
        results[requirement.key] = passed
        if not passed:
            report.errors.append(f"{error_prefix}: missing {requirement.label}")
    setattr(report, result_attr, results)


def validate_status(text: str, report: DependencyDecisionReport) -> None:
    match = STATUS_LINE_RE.search(text)
    if not match:
        report.errors.append("status: missing 'Decision status:' line")
        return

    status = match.group(1).strip()
    lowered = status.lower()
    report.status_line = status
    report.has_proposed_status = "proposed" in lowered
    report.has_active_status = "active" in lowered
    report.has_not_implemented_status = any(
        phrase in lowered
        for phrase in (
            "not implemented",
            "implementation pending",
            "planning evidence",
            "renderer/runtime route ownership not implemented",
        )
    )

    if not (report.has_active_status or report.has_proposed_status):
        report.errors.append("status: decision status must explicitly be active or proposed")
    if not report.has_not_implemented_status:
        report.errors.append("status: decision status must preserve renderer/runtime not-implemented scope")

    overclaim_patterns = (
        r"\bcompleted\b",
        r"\baccepted\s+as\s+(?:a\s+)?(?:default\s+)?(?:build\s+)?dependency\b",
        r"\bdependency\s+(?:is\s+)?(?:accepted|complete|implemented)\b",
        r"\broute\s+ownership\s+(?:is\s+)?(?:accepted|complete|implemented)\b",
        r"\bdefault\s+runtime\s+(?:is\s+)?(?:accepted|enabled|implemented)\b",
    )
    for pattern in overclaim_patterns:
        report.status_overclaim_count += len(re.findall(pattern, lowered))

    if report.status_overclaim_count:
        report.errors.append("status: decision status overclaims completion/default enablement")


def validate_task_ids(text: str, report: DependencyDecisionReport) -> None:
    report.task_ids_found = set(TASK_ID_RE.findall(text))
    report.missing_task_ids = [
        task_id for task_id in REQUIRED_TASK_IDS if task_id not in report.task_ids_found
    ]
    if report.missing_task_ids:
        missing = ", ".join(report.missing_task_ids)
        report.errors.append(f"task IDs: missing required dependency decision context: {missing}")


def validate_dependency_decision(record_path: Path, repo_root: Path) -> DependencyDecisionReport:
    repo_root = repo_root.resolve()
    report = DependencyDecisionReport(repo_root=repo_root, record_path=record_path.resolve(strict=False))

    if not report.record_path.is_file():
        report.errors.append(
            f"record: dependency decision record does not exist: "
            f"{display_path(report.record_path, repo_root)}"
        )
        return report

    report.exists = True
    try:
        text = report.record_path.read_text(encoding="utf-8")
    except OSError as exc:
        report.errors.append(f"record: dependency decision record cannot be read: {exc}")
        return report

    validate_task_ids(text, report)
    validate_status(text, report)
    validate_requirements(
        text,
        IMPLEMENTATION_BOUNDARY_REQUIREMENTS,
        report,
        "boundary_results",
        "implementation boundary evidence",
    )
    validate_requirements(
        text,
        REMAINING_GUARDRAIL_REQUIREMENTS,
        report,
        "remaining_guardrail_results",
        "remaining guardrails",
    )
    validate_requirements(
        text,
        NATIVE_RENDERER_REQUIREMENTS,
        report,
        "native_renderer_results",
        "native renderer obligations",
    )
    validate_requirements(
        text,
        GATE_G1_REQUIREMENTS,
        report,
        "gate_g1_results",
        "Gate G1 interfaces",
    )
    validate_requirements(
        text,
        VALIDATION_REQUIREMENTS,
        report,
        "validation_results",
        "validation evidence",
    )
    return report


def json_report_payload(report: DependencyDecisionReport) -> dict[str, Any]:
    return {
        "ok": report.ok(),
        "record": display_path(report.record_path, report.repo_root),
        "exists": report.exists,
        "required_task_ids": list(REQUIRED_TASK_IDS),
        "task_ids_found": sorted(report.task_ids_found),
        "missing_task_ids": report.missing_task_ids,
        "status_line": report.status_line,
        "has_proposed_status": report.has_proposed_status,
        "has_active_status": report.has_active_status,
        "has_not_implemented_status": report.has_not_implemented_status,
        "status_overclaim_count": report.status_overclaim_count,
        "implementation_boundary": report.boundary_results,
        "remaining_guardrails": report.remaining_guardrail_results,
        "native_renderer": report.native_renderer_results,
        "gate_g1_interfaces": report.gate_g1_results,
        "validation_evidence": report.validation_results,
        "implementation_boundary_passed": sum(1 for passed in report.boundary_results.values() if passed),
        "remaining_guardrails_passed": sum(
            1 for passed in report.remaining_guardrail_results.values() if passed
        ),
        "native_renderer_passed": sum(
            1 for passed in report.native_renderer_results.values() if passed
        ),
        "gate_g1_interfaces_passed": sum(
            1 for passed in report.gate_g1_results.values() if passed
        ),
        "validation_evidence_passed": sum(
            1 for passed in report.validation_results.values() if passed
        ),
        "errors": report.errors,
    }


def print_json_report(report: DependencyDecisionReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def print_requirement_group(label: str, results: dict[str, bool]) -> None:
    passed = sum(1 for value in results.values() if value)
    print(f"  {label}: {passed}/{len(results)}")
    for key, value in sorted(results.items()):
        marker = "ok" if value else "missing"
        print(f"    - {key}: {marker}")


def print_report(report: DependencyDecisionReport) -> None:
    print("RmlUi dependency decision record:")
    print(f"  Record: {display_path(report.record_path, report.repo_root)}")
    print(f"  Exists: {'yes' if report.exists else 'no'}")
    print(
        "  Required task IDs: "
        f"{len(REQUIRED_TASK_IDS) - len(report.missing_task_ids)}/{len(REQUIRED_TASK_IDS)}"
    )
    print(f"  Task IDs found: {', '.join(sorted(report.task_ids_found)) or '-'}")
    print(f"  Decision status line: {report.status_line or '-'}")
    print(f"  Active status: {'yes' if report.has_active_status else 'no'}")
    print(f"  Proposed status: {'yes' if report.has_proposed_status else 'no'}")
    print(
        "  Not-implemented status: "
        f"{'yes' if report.has_not_implemented_status else 'no'}"
    )
    print(f"  Status overclaims: {report.status_overclaim_count}")
    print_requirement_group("Implementation boundary", report.boundary_results)
    print_requirement_group("Remaining guardrails", report.remaining_guardrail_results)
    print_requirement_group("Native renderer obligations", report.native_renderer_results)
    print_requirement_group("Gate G1 interfaces", report.gate_g1_results)
    print_requirement_group("Validation evidence", report.validation_results)

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi dependency decision check failed.")
    else:
        print("\nResult: RmlUi dependency decision check passed.")


def resolve_record_path(record: Path, repo_root: Path) -> Path:
    if record.is_absolute():
        return record
    return repo_root / record


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--record",
        type=Path,
        default=DEFAULT_RECORD,
        help="Path to the RmlUi dependency decision record.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve the default record path.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the text report.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    record_path = resolve_record_path(args.record, repo_root)
    report = validate_dependency_decision(record_path, repo_root)

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
