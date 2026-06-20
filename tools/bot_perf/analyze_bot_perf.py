#!/usr/bin/env python3
"""Analyze WORR bot frame-command smoke telemetry."""

from __future__ import annotations

import argparse
import csv
import glob
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Any


STATUS_MARKER = "q3a_bot_frame_command_status"
SOURCE_STATUS_MARKER = "q3a_bot_source_counter_status"
SOAK_BEGIN_MARKER = "q3a_bot_frame_command_smoke_soak=begin"
SOAK_PROGRESS_MARKER = "q3a_bot_frame_command_smoke_soak_progress"
SOAK_COMPLETE_MARKER = "q3a_bot_frame_command_smoke_soak=complete"
KEY_VALUE_RE = re.compile(r"([A-Za-z0-9_]+)=(-?\d+(?:\.\d+)?)")
NS_PER_MS = 1_000_000.0
NS_PER_US = 1_000.0

SOURCE_COUNTER_GROUPS = {
    "bot_frame_cpu": (
        "bot_frame_cpu_ns",
        "bot_frame_cpu_samples",
        "bot_frame_cpu_max_ns",
        "bot_frame_cpu_success_ns",
        "bot_frame_cpu_success_samples",
        "bot_cpu_ns",
        "bot_cpu_ms",
        "bot_frame_cpu_ms",
    ),
    "route_query_cpu": (
        "route_query_cpu_ns",
        "route_query_cpu_samples",
        "route_query_cpu_max_ns",
        "route_query_cpu_fail_ns",
        "route_query_cpu_fail_samples",
        "route_reuse_cpu_ns",
        "route_reuse_cpu_samples",
        "bot_route_cpu_ms",
    ),
    "q3a_route_cpu": (
        "q3a_route_cpu_ns",
        "q3a_route_cpu_samples",
        "q3a_route_cpu_max_ns",
        "q3a_route_cpu_fail_ns",
        "q3a_route_cpu_fail_samples",
    ),
    "q3a_memory": (
        "q3a_memory_zone_active",
        "q3a_memory_zone_peak",
        "q3a_memory_hunk_active",
        "q3a_memory_hunk_peak",
        "q3a_memory_total_active",
        "q3a_memory_total_peak",
        "q3a_memory_failures",
        "q3a_memory_available",
    ),
    "visibility": (
        "aas_inpvs_checks",
        "aas_inpvs_visible",
        "aas_inpvs_misses",
        "aas_inphs_checks",
        "aas_inphs_visible",
        "aas_inphs_misses",
        "visibility_cluster_checks",
        "visibility_cluster_same",
        "visibility_cluster_invalid",
        "visibility_decompress_calls",
        "visibility_decompress_bytes",
        "visibility_decompress_runs",
        "visibility_decompress_failures",
        "visibility_traces",
        "visibility_trace_tests",
        "pvs_checks",
        "phs_checks",
    ),
    "static_bsp_trace": (
        "aas_trace_calls",
        "bsp_trace_calls",
        "bsp_trace_point_calls",
        "bsp_trace_box_calls",
        "bsp_trace_zero_length_calls",
        "bsp_trace_hits",
        "bsp_trace_misses",
        "bsp_trace_startsolid",
        "bsp_trace_allsolid",
        "bsp_trace_hull_nodes",
        "bsp_trace_brush_tests",
        "bsp_trace_cpu_ns",
        "bsp_trace_cpu_samples",
        "bsp_trace_cpu_max_ns",
    ),
    "entity_trace": (
        "entity_trace_attempts",
        "entity_trace_hits",
        "entity_trace_misses",
        "entity_trace_failures",
        "entity_trace_clip_calls",
        "entity_trace_clip_hits",
        "entity_trace_clip_misses",
        "entity_trace_clip_startsolid",
        "entity_trace_clip_allsolid",
        "entity_trace_clip_cpu_ns",
        "entity_trace_clip_cpu_max_ns",
    ),
}

SOURCE_COUNTER_KEYS = tuple(
    dict.fromkeys(key for keys in SOURCE_COUNTER_GROUPS.values() for key in keys)
)

SOURCE_COUNTER_METRIC_INPUTS: dict[str, dict[str, Any]] = {
    "bot_frame_cpu_total_ms": {
        "group": "bot_frame_cpu",
        "any_of": ("bot_frame_cpu_ns", "bot_cpu_ns", "bot_frame_cpu_ms", "bot_cpu_ms"),
    },
    "bot_frame_cpu_ms_per_sec": {
        "group": "bot_frame_cpu",
        "any_of": ("bot_frame_cpu_ns", "bot_cpu_ns", "bot_frame_cpu_ms", "bot_cpu_ms"),
    },
    "bot_frame_cpu_ms_per_bot_sec": {
        "group": "bot_frame_cpu",
        "any_of": ("bot_frame_cpu_ns", "bot_cpu_ns", "bot_frame_cpu_ms", "bot_cpu_ms"),
    },
    "bot_frame_cpu_avg_us": {
        "group": "bot_frame_cpu",
        "any_of": ("bot_frame_cpu_ns", "bot_cpu_ns"),
        "all_of": ("bot_frame_cpu_samples",),
    },
    "bot_frame_cpu_max_us": {
        "group": "bot_frame_cpu",
        "all_of": ("bot_frame_cpu_max_ns",),
    },
    "bot_frame_cpu_success_avg_us": {
        "group": "bot_frame_cpu",
        "all_of": ("bot_frame_cpu_success_ns", "bot_frame_cpu_success_samples"),
    },
    "route_query_cpu_total_ms": {
        "group": "route_query_cpu",
        "any_of": ("route_query_cpu_ns", "bot_route_cpu_ms"),
    },
    "route_query_cpu_ms_per_sec": {
        "group": "route_query_cpu",
        "any_of": ("route_query_cpu_ns", "bot_route_cpu_ms"),
    },
    "route_query_cpu_ms_per_bot_sec": {
        "group": "route_query_cpu",
        "any_of": ("route_query_cpu_ns", "bot_route_cpu_ms"),
    },
    "route_query_cpu_avg_us": {
        "group": "route_query_cpu",
        "all_of": ("route_query_cpu_ns", "route_query_cpu_samples"),
    },
    "route_query_cpu_max_us": {
        "group": "route_query_cpu",
        "all_of": ("route_query_cpu_max_ns",),
    },
    "route_query_cpu_fail_avg_us": {
        "group": "route_query_cpu",
        "all_of": ("route_query_cpu_fail_ns", "route_query_cpu_fail_samples"),
    },
    "route_reuse_cpu_total_ms": {
        "group": "route_query_cpu",
        "all_of": ("route_reuse_cpu_ns",),
    },
    "route_reuse_cpu_ms_per_sec": {
        "group": "route_query_cpu",
        "all_of": ("route_reuse_cpu_ns",),
    },
    "route_reuse_cpu_ms_per_bot_sec": {
        "group": "route_query_cpu",
        "all_of": ("route_reuse_cpu_ns",),
    },
    "route_reuse_cpu_avg_us": {
        "group": "route_query_cpu",
        "all_of": ("route_reuse_cpu_ns", "route_reuse_cpu_samples"),
    },
    "q3a_route_cpu_total_ms": {
        "group": "q3a_route_cpu",
        "all_of": ("q3a_route_cpu_ns",),
    },
    "q3a_route_cpu_ms_per_sec": {
        "group": "q3a_route_cpu",
        "all_of": ("q3a_route_cpu_ns",),
    },
    "q3a_route_cpu_ms_per_bot_sec": {
        "group": "q3a_route_cpu",
        "all_of": ("q3a_route_cpu_ns",),
    },
    "q3a_route_cpu_avg_us": {
        "group": "q3a_route_cpu",
        "all_of": ("q3a_route_cpu_ns", "q3a_route_cpu_samples"),
    },
    "q3a_route_cpu_max_us": {
        "group": "q3a_route_cpu",
        "all_of": ("q3a_route_cpu_max_ns",),
    },
    "q3a_route_cpu_fail_avg_us": {
        "group": "q3a_route_cpu",
        "all_of": ("q3a_route_cpu_fail_ns", "q3a_route_cpu_fail_samples"),
    },
    "bsp_trace_cpu_total_ms": {
        "group": "static_bsp_trace",
        "all_of": ("bsp_trace_cpu_ns",),
    },
    "bsp_trace_cpu_ms_per_sec": {
        "group": "static_bsp_trace",
        "all_of": ("bsp_trace_cpu_ns",),
    },
    "bsp_trace_cpu_ms_per_bot_sec": {
        "group": "static_bsp_trace",
        "all_of": ("bsp_trace_cpu_ns",),
    },
    "bsp_trace_cpu_avg_us": {
        "group": "static_bsp_trace",
        "all_of": ("bsp_trace_cpu_ns",),
        "any_of": ("bsp_trace_cpu_samples", "bsp_trace_calls"),
    },
    "bsp_trace_cpu_max_us": {
        "group": "static_bsp_trace",
        "all_of": ("bsp_trace_cpu_max_ns",),
    },
    "entity_trace_clip_cpu_total_ms": {
        "group": "entity_trace",
        "all_of": ("entity_trace_clip_cpu_ns",),
    },
    "entity_trace_clip_cpu_ms_per_sec": {
        "group": "entity_trace",
        "all_of": ("entity_trace_clip_cpu_ns",),
    },
    "entity_trace_clip_cpu_ms_per_bot_sec": {
        "group": "entity_trace",
        "all_of": ("entity_trace_clip_cpu_ns",),
    },
    "entity_trace_clip_cpu_avg_us": {
        "group": "entity_trace",
        "all_of": ("entity_trace_clip_cpu_ns", "entity_trace_clip_calls"),
    },
    "entity_trace_clip_cpu_max_us": {
        "group": "entity_trace",
        "all_of": ("entity_trace_clip_cpu_max_ns",),
    },
    "aas_inpvs_visible_ratio": {
        "group": "visibility",
        "all_of": ("aas_inpvs_visible", "aas_inpvs_checks"),
    },
    "aas_inphs_visible_ratio": {
        "group": "visibility",
        "all_of": ("aas_inphs_visible", "aas_inphs_checks"),
    },
    "bsp_trace_hit_ratio": {
        "group": "static_bsp_trace",
        "all_of": ("bsp_trace_hits", "bsp_trace_calls"),
    },
    "entity_trace_clip_hit_ratio": {
        "group": "entity_trace",
        "all_of": ("entity_trace_clip_hits", "entity_trace_clip_calls"),
    },
}

COMPARISON_METRICS = (
    {
        "key": "commands_per_bot_sec",
        "label": "commands/bot/sec",
        "goal": "higher",
    },
    {
        "key": "route_queries_per_bot_sec",
        "label": "route queries/bot/sec",
        "goal": "lower",
    },
    {
        "key": "route_refresh_ratio",
        "label": "route refresh ratio",
        "goal": "lower",
    },
    {
        "key": "route_reuse_ratio",
        "label": "route reuse ratio",
        "goal": "higher",
    },
    {
        "key": "route_failures",
        "label": "route failures",
        "goal": "lower",
    },
    {
        "key": "debug_work_units_per_bot_sec",
        "label": "debug work units/bot/sec",
        "goal": "lower",
    },
    {
        "key": "recovery_command_uses_per_bot_sec",
        "label": "recovery commands/bot/sec",
        "goal": "lower",
    },
    {
        "key": "stuck_detections_per_sec",
        "label": "stuck detections/sec",
        "goal": "lower",
    },
    {
        "key": "bot_frame_cpu_ms_per_bot_sec",
        "label": "bot frame CPU ms/bot/sec",
        "goal": "lower",
    },
    {
        "key": "route_query_cpu_ms_per_bot_sec",
        "label": "route query CPU ms/bot/sec",
        "goal": "lower",
    },
    {
        "key": "q3a_route_cpu_ms_per_bot_sec",
        "label": "Q3A route CPU ms/bot/sec",
        "goal": "lower",
    },
    {
        "key": "aas_inpvs_checks_per_bot_sec",
        "label": "AAS inPVS checks/bot/sec",
        "goal": "lower",
    },
    {
        "key": "aas_inphs_checks_per_bot_sec",
        "label": "AAS inPHS checks/bot/sec",
        "goal": "lower",
    },
    {
        "key": "bsp_trace_calls_per_bot_sec",
        "label": "BSP traces/bot/sec",
        "goal": "lower",
    },
    {
        "key": "entity_trace_clip_calls_per_bot_sec",
        "label": "entity clip traces/bot/sec",
        "goal": "lower",
    },
)


@dataclass(frozen=True)
class ParsedLog:
    path: pathlib.Path
    status: dict[str, int | float]
    soak_begin: dict[str, int | float] | None
    soak_complete: dict[str, int | float] | None
    progress: list[dict[str, int | float]]
    status_lines: int


@dataclass(frozen=True)
class Budget:
    path: pathlib.Path
    metrics: dict[str, dict[str, Any]]
    status: dict[str, dict[str, Any]]


def parse_number(value: str) -> int | float:
    if "." in value:
        return float(value)
    return int(value)


def parse_key_values(text: str) -> dict[str, int | float]:
    return {match.group(1): parse_number(match.group(2)) for match in KEY_VALUE_RE.finditer(text)}


def marker_payload(line: str, marker: str) -> str | None:
    index = line.find(marker)
    if index < 0:
        return None
    return line[index + len(marker):]


def parse_log(path: pathlib.Path) -> ParsedLog:
    status: dict[str, int | float] | None = None
    source_status: dict[str, int | float] | None = None
    soak_begin: dict[str, int | float] | None = None
    soak_complete: dict[str, int | float] | None = None
    progress: list[dict[str, int | float]] = []
    status_lines = 0

    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise SystemExit(f"Unable to read {path}: {exc}") from exc

    for line in lines:
        payload = marker_payload(line, SOAK_BEGIN_MARKER)
        if payload is not None:
            soak_begin = parse_key_values(payload)
            continue

        payload = marker_payload(line, SOAK_PROGRESS_MARKER)
        if payload is not None:
            progress.append(parse_key_values(payload))
            continue

        payload = marker_payload(line, SOAK_COMPLETE_MARKER)
        if payload is not None:
            soak_complete = parse_key_values(payload)
            continue

        payload = marker_payload(line, STATUS_MARKER)
        if payload is not None:
            status = parse_key_values(payload)
            status_lines += 1
            continue

        payload = marker_payload(line, SOURCE_STATUS_MARKER)
        if payload is not None:
            source_status = parse_key_values(payload)

    if status is None:
        raise SystemExit(f"No {STATUS_MARKER} line found in {path}")

    if source_status is not None:
        status = {**status, **source_status}

    return ParsedLog(
        path=path,
        status=status,
        soak_begin=soak_begin,
        soak_complete=soak_complete,
        progress=progress,
        status_lines=status_lines,
    )


def as_float(payload: dict[str, int | float], key: str) -> float | None:
    value = payload.get(key)
    if isinstance(value, (int, float)):
        return float(value)
    return None


def as_int(payload: dict[str, int | float], key: str) -> int | None:
    value = payload.get(key)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    return None


def first_float(payload: dict[str, int | float], *keys: str) -> float | None:
    for key in keys:
        value = as_float(payload, key)
        if value is not None:
            return value
    return None


def safe_rate(total: float | None, seconds: float | None) -> float | None:
    if total is None or seconds is None or seconds <= 0:
        return None
    return total / seconds


def safe_ratio(part: float | None, whole: float | None) -> float | None:
    if part is None or whole is None or whole == 0:
        return None
    return part / whole


def round_metric(value: float | None, digits: int = 3) -> float | None:
    if value is None:
        return None
    return round(value, digits)


def ns_to_ms(value: float | None) -> float | None:
    if value is None:
        return None
    return value / NS_PER_MS


def ns_to_us(value: float | None) -> float | None:
    if value is None:
        return None
    return value / NS_PER_US


def average_ns_to_us(total_ns: float | None, samples: float | None) -> float | None:
    if total_ns is None or samples is None or samples <= 0:
        return None
    return total_ns / samples / NS_PER_US


def add_rate_metrics(
    metrics: dict[str, Any],
    status: dict[str, int | float],
    key: str,
    duration_sec: float | None,
    per_bot_divisor: float | None,
) -> None:
    value = as_float(status, key)
    metrics[f"{key}_per_sec"] = round_metric(safe_rate(value, duration_sec))
    metrics[f"{key}_per_bot_sec"] = round_metric(safe_rate(value, per_bot_divisor))


def add_ratio_metric(
    metrics: dict[str, Any],
    status: dict[str, int | float],
    key: str,
    part_key: str,
    whole_key: str,
) -> None:
    metrics[key] = round_metric(
        safe_ratio(as_float(status, part_key), as_float(status, whole_key)),
        4,
    )


def add_cpu_metrics(
    metrics: dict[str, Any],
    status: dict[str, int | float],
    prefix: str,
    duration_sec: float | None,
    per_bot_divisor: float | None,
    *,
    total_ns_keys: tuple[str, ...],
    total_ms_keys: tuple[str, ...] = (),
    samples_key: str | None = None,
    samples_keys: tuple[str, ...] = (),
    max_ns_key: str | None = None,
    success_ns_key: str | None = None,
    success_samples_key: str | None = None,
    fail_ns_key: str | None = None,
    fail_samples_key: str | None = None,
) -> None:
    total_ns = first_float(status, *total_ns_keys)
    total_ms = ns_to_ms(total_ns)
    if total_ms is None and total_ms_keys:
        total_ms = first_float(status, *total_ms_keys)

    sample_key_candidates = ((samples_key,) if samples_key else ()) + samples_keys
    samples = first_float(status, *sample_key_candidates) if sample_key_candidates else None
    max_ns = as_float(status, max_ns_key) if max_ns_key else None
    success_ns = as_float(status, success_ns_key) if success_ns_key else None
    success_samples = as_float(status, success_samples_key) if success_samples_key else None
    fail_ns = as_float(status, fail_ns_key) if fail_ns_key else None
    fail_samples = as_float(status, fail_samples_key) if fail_samples_key else None

    metrics[f"{prefix}_total_ms"] = round_metric(total_ms)
    metrics[f"{prefix}_ms_per_sec"] = round_metric(safe_rate(total_ms, duration_sec))
    metrics[f"{prefix}_ms_per_bot_sec"] = round_metric(safe_rate(total_ms, per_bot_divisor))
    metrics[f"{prefix}_avg_us"] = round_metric(average_ns_to_us(total_ns, samples))
    metrics[f"{prefix}_max_us"] = round_metric(ns_to_us(max_ns))

    if success_ns_key or success_samples_key:
        metrics[f"{prefix}_success_avg_us"] = round_metric(
            average_ns_to_us(success_ns, success_samples)
        )
    if fail_ns_key or fail_samples_key:
        metrics[f"{prefix}_fail_avg_us"] = round_metric(
            average_ns_to_us(fail_ns, fail_samples)
        )


def present_source_counter_groups(status: dict[str, int | float]) -> list[str]:
    return [
        group
        for group, keys in SOURCE_COUNTER_GROUPS.items()
        if any(key in status for key in keys)
    ]


def source_counter_subset(status: dict[str, int | float]) -> dict[str, int | float]:
    return {
        key: status[key]
        for key in SOURCE_COUNTER_KEYS
        if key in status
    }


def source_counter_group_for_key(key: str) -> str | None:
    for group, keys in SOURCE_COUNTER_GROUPS.items():
        if key in keys:
            return group
    return None


def source_counter_group_status(status: dict[str, int | float]) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []
    for group, keys in SOURCE_COUNTER_GROUPS.items():
        present = [key for key in keys if key in status]
        missing = [key for key in keys if key not in status]
        groups.append({
            "group": group,
            "present": bool(present),
            "primary_counter": keys[0],
            "present_counters": present,
            "missing_counters": missing,
        })
    return groups


def missing_current_counter_groups(status: dict[str, int | float]) -> list[dict[str, Any]]:
    return [
        {
            "group": item["group"],
            "primary_counter": item["primary_counter"],
            "present_counters": item["present_counters"],
            "missing_counters": item["missing_counters"],
        }
        for item in source_counter_group_status(status)
        if not item["present"]
    ]


def source_counter_input_spec(namespace: str, metric: str) -> dict[str, Any] | None:
    if namespace == "status":
        group = source_counter_group_for_key(metric)
        if group is None:
            return None
        return {"group": group, "all_of": (metric,)}

    if namespace != "metrics":
        return None

    explicit = SOURCE_COUNTER_METRIC_INPUTS.get(metric)
    if explicit is not None:
        return explicit

    for suffix in ("_per_bot_sec", "_per_sec"):
        if not metric.endswith(suffix):
            continue
        raw_key = metric[: -len(suffix)]
        group = source_counter_group_for_key(raw_key)
        if group is not None:
            return {"group": group, "all_of": (raw_key,)}

    return None


def unique_strings(values: tuple[str, ...] | list[str]) -> list[str]:
    return list(dict.fromkeys(values))


def source_counter_budget_diagnostic(
    status: dict[str, int | float],
    check: dict[str, Any],
) -> dict[str, Any] | None:
    if check.get("value") is not None:
        return None

    namespace = str(check.get("namespace", ""))
    metric = str(check.get("metric", ""))
    spec = source_counter_input_spec(namespace, metric)
    if spec is None:
        return None

    any_of = tuple(str(key) for key in spec.get("any_of", ()))
    all_of = tuple(str(key) for key in spec.get("all_of", ()))
    any_present = [key for key in any_of if key in status]
    all_present = [key for key in all_of if key in status]
    missing_any = [] if any_present or not any_of else list(any_of)
    missing_all = [key for key in all_of if key not in status]
    missing_counters = unique_strings(missing_any + missing_all)
    if not missing_counters:
        return None

    expected_counters = unique_strings(list(any_of) + list(all_of))
    present_counters = unique_strings(any_present + all_present)
    group = str(spec["group"])
    group_present = group in present_source_counter_groups(status)

    return {
        "namespace": namespace,
        "metric": metric,
        "required": bool(check.get("required", True)),
        "result": check.get("result"),
        "group": group,
        "source_counter_group_present": group_present,
        "expected_any_current_counters": list(any_of),
        "expected_all_current_counters": list(all_of),
        "expected_current_counters": expected_counters,
        "present_current_counters": present_counters,
        "missing_current_counters": missing_counters,
        "message": (
            f"{namespace}.{metric} is missing current source counter input: "
            + ", ".join(missing_counters)
        ),
    }


def budget_missing_current_counter_diagnostics(
    status: dict[str, int | float],
    checks: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    for check in checks:
        diagnostic = source_counter_budget_diagnostic(status, check)
        if diagnostic is not None:
            diagnostics.append(diagnostic)
    return diagnostics


def has_any_metric(report: dict[str, Any], keys: tuple[str, ...]) -> bool:
    return any(report.get(key) is not None for key in keys)


def choose_duration_ms(parsed: ParsedLog) -> float | None:
    if parsed.soak_complete is not None:
        elapsed_ms = as_float(parsed.soak_complete, "elapsed_ms")
        if elapsed_ms is not None and elapsed_ms > 0:
            return elapsed_ms

    if parsed.progress:
        elapsed_ms = as_float(parsed.progress[-1], "elapsed_ms")
        if elapsed_ms is not None and elapsed_ms > 0:
            return elapsed_ms

    return None


def choose_bot_count(parsed: ParsedLog) -> int | None:
    for payload in (parsed.soak_complete, parsed.soak_begin):
        if payload is None:
            continue
        for key in ("count", "target"):
            count = as_int(payload, key)
            if count is not None and count > 0:
                return count

    for key in ("expected_min_commands", "expected_min_frames"):
        count = as_int(parsed.status, key)
        if count is not None and count > 0:
            return count

    return None


def progress_summary(progress: list[dict[str, int | float]]) -> dict[str, Any]:
    elapsed = [
        as_float(item, "elapsed_ms")
        for item in progress
        if as_float(item, "elapsed_ms") is not None
    ]
    elapsed = [value for value in elapsed if value is not None]
    if not elapsed:
        return {
            "progress_reports": len(progress),
            "progress_interval_avg_sec": None,
            "progress_interval_min_sec": None,
            "progress_interval_max_sec": None,
        }

    previous = 0.0
    intervals: list[float] = []
    for value in elapsed:
        intervals.append((value - previous) / 1000.0)
        previous = value

    return {
        "progress_reports": len(progress),
        "progress_interval_avg_sec": round(sum(intervals) / len(intervals), 3),
        "progress_interval_min_sec": round(min(intervals), 3),
        "progress_interval_max_sec": round(max(intervals), 3),
    }


def path_key(path: pathlib.Path) -> str:
    try:
        return str(path.resolve()).lower()
    except OSError:
        return str(path).lower()


def analyze(
    parsed: ParsedLog,
    scenario_metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    status = parsed.status
    duration_ms = choose_duration_ms(parsed)
    duration_sec = duration_ms / 1000.0 if duration_ms is not None else None
    duration_source = "soak_elapsed_ms" if duration_sec is not None else None
    if scenario_metadata is not None and isinstance(scenario_metadata.get("duration_seconds"), (int, float)):
        duration_sec = float(scenario_metadata["duration_seconds"])
        duration_source = "scenario_report"
    bot_count = choose_bot_count(parsed)

    route_requests = as_float(status, "route_requests")
    route_queries = as_float(status, "route_queries")
    route_refreshes = as_float(status, "route_refreshes")
    route_reuses = as_float(status, "route_reuses")
    route_commands = as_float(status, "route_commands")

    debug_direct_primitives = sum(
        as_float(status, key) or 0.0
        for key in ("route_debug_lines", "route_debug_crosses", "route_debug_arrows", "route_debug_labels")
    )
    debug_polyline_segments = as_float(status, "route_debug_polyline_segments") or 0.0
    debug_work_units = debug_direct_primitives + debug_polyline_segments

    per_bot_divisor = duration_sec * bot_count if duration_sec is not None and bot_count else None
    present_source_groups = present_source_counter_groups(status)
    missing_source_groups = [
        group for group in SOURCE_COUNTER_GROUPS if group not in present_source_groups
    ]
    group_status = source_counter_group_status(status)

    metrics: dict[str, Any] = {
        "file": str(parsed.path),
        "scenario_name": scenario_metadata.get("name") if scenario_metadata else None,
        "scenario_title": scenario_metadata.get("title") if scenario_metadata else None,
        "scenario_status": scenario_metadata.get("status") if scenario_metadata else None,
        "scenario_returncode": scenario_metadata.get("returncode") if scenario_metadata else None,
        "scenario_duration_budget_passed": scenario_metadata.get("duration_budget_passed") if scenario_metadata else None,
        "duration_source": duration_source,
        "status_lines": parsed.status_lines,
        "pass": as_int(status, "pass"),
        "duration_sec": round_metric(duration_sec),
        "bot_count": bot_count,
        "frames": as_int(status, "frames"),
        "commands": as_int(status, "commands"),
        "frames_per_sec": round_metric(safe_rate(as_float(status, "frames"), duration_sec)),
        "commands_per_sec": round_metric(safe_rate(as_float(status, "commands"), duration_sec)),
        "commands_per_bot_sec": round_metric(safe_rate(as_float(status, "commands"), per_bot_divisor)),
        "route_requests_per_sec": round_metric(safe_rate(route_requests, duration_sec)),
        "route_requests_per_bot_sec": round_metric(safe_rate(route_requests, per_bot_divisor)),
        "route_queries_per_sec": round_metric(safe_rate(route_queries, duration_sec)),
        "route_queries_per_bot_sec": round_metric(safe_rate(route_queries, per_bot_divisor)),
        "route_refreshes_per_sec": round_metric(safe_rate(route_refreshes, duration_sec)),
        "route_refreshes_per_bot_sec": round_metric(safe_rate(route_refreshes, per_bot_divisor)),
        "route_reuses_per_sec": round_metric(safe_rate(route_reuses, duration_sec)),
        "route_commands_per_sec": round_metric(safe_rate(route_commands, duration_sec)),
        "route_commands_per_bot_sec": round_metric(safe_rate(route_commands, per_bot_divisor)),
        "route_reuses_per_bot_sec": round_metric(safe_rate(route_reuses, per_bot_divisor)),
        "route_query_ratio": round_metric(safe_ratio(route_queries, route_requests), 4),
        "route_refresh_ratio": round_metric(safe_ratio(route_refreshes, route_requests), 4),
        "route_reuse_ratio": round_metric(safe_ratio(route_reuses, route_requests), 4),
        "route_failures": as_int(status, "route_failures"),
        "route_failures_per_sec": round_metric(safe_rate(as_float(status, "route_failures"), duration_sec)),
        "route_debug_routes_per_sec": round_metric(safe_rate(as_float(status, "route_debug_routes"), duration_sec)),
        "route_debug_goals_per_sec": round_metric(safe_rate(as_float(status, "route_debug_goals"), duration_sec)),
        "debug_direct_primitives": int(debug_direct_primitives),
        "debug_direct_primitives_per_sec": round_metric(safe_rate(debug_direct_primitives, duration_sec)),
        "debug_polyline_segments_per_sec": round_metric(safe_rate(debug_polyline_segments, duration_sec)),
        "debug_work_units_per_sec": round_metric(safe_rate(debug_work_units, duration_sec)),
        "debug_work_units_per_bot_sec": round_metric(safe_rate(debug_work_units, per_bot_divisor)),
        "stuck_detections_per_sec": round_metric(safe_rate(as_float(status, "stuck_detections"), duration_sec)),
        "stuck_recovery_activations_per_sec": round_metric(
            safe_rate(as_float(status, "stuck_recovery_activations"), duration_sec)
        ),
        "recovery_command_uses_per_sec": round_metric(
            safe_rate(as_float(status, "recovery_command_uses"), duration_sec)
        ),
        "recovery_command_uses_per_bot_sec": round_metric(
            safe_rate(as_float(status, "recovery_command_uses"), per_bot_divisor)
        ),
        "route_goal_assignments_per_sec": round_metric(
            safe_rate(as_float(status, "route_goal_assignments"), duration_sec)
        ),
        "item_goal_assignments_per_sec": round_metric(
            safe_rate(as_float(status, "item_goal_assignments"), duration_sec)
        ),
        "item_goal_reservation_skips_per_sec": round_metric(
            safe_rate(as_float(status, "item_goal_reservation_skips"), duration_sec)
        ),
        "item_goal_peak_active_reservations": as_int(status, "item_goal_peak_active_reservations"),
        "soak_reports": as_int(parsed.soak_complete or {}, "reports"),
        "source_counter_status": "pass" if not missing_source_groups else "fail",
        "source_counter_pass": not missing_source_groups,
        "source_counter_pass_int": 1 if not missing_source_groups else 0,
        "source_counter_groups_expected": list(SOURCE_COUNTER_GROUPS),
        "source_counter_groups_present": present_source_groups,
        "source_counter_groups_missing": missing_source_groups,
        "source_counter_groups_present_count": len(present_source_groups),
        "source_counter_groups_missing_count": len(missing_source_groups),
        "source_counter_group_status": group_status,
        "source_counters": source_counter_subset(status),
        "missing_current_counters": missing_current_counter_groups(status),
        "missing_instrumentation": [
            key
            for key, group in (
                (keys[0], group) for group, keys in SOURCE_COUNTER_GROUPS.items()
            )
            if group not in present_source_groups
        ],
        "q3a_memory_zone_active_bytes": as_int(status, "q3a_memory_zone_active"),
        "q3a_memory_zone_peak_bytes": as_int(status, "q3a_memory_zone_peak"),
        "q3a_memory_hunk_active_bytes": as_int(status, "q3a_memory_hunk_active"),
        "q3a_memory_hunk_peak_bytes": as_int(status, "q3a_memory_hunk_peak"),
        "q3a_memory_total_active_bytes": as_int(status, "q3a_memory_total_active"),
        "q3a_memory_total_peak_bytes": as_int(status, "q3a_memory_total_peak"),
        "q3a_memory_failures": as_int(status, "q3a_memory_failures"),
        "q3a_memory_available_bytes": as_int(status, "q3a_memory_available"),
    }

    add_cpu_metrics(
        metrics,
        status,
        "bot_frame_cpu",
        duration_sec,
        per_bot_divisor,
        total_ns_keys=("bot_frame_cpu_ns", "bot_cpu_ns"),
        total_ms_keys=("bot_frame_cpu_ms", "bot_cpu_ms"),
        samples_key="bot_frame_cpu_samples",
        max_ns_key="bot_frame_cpu_max_ns",
        success_ns_key="bot_frame_cpu_success_ns",
        success_samples_key="bot_frame_cpu_success_samples",
    )
    add_cpu_metrics(
        metrics,
        status,
        "route_query_cpu",
        duration_sec,
        per_bot_divisor,
        total_ns_keys=("route_query_cpu_ns",),
        total_ms_keys=("bot_route_cpu_ms",),
        samples_key="route_query_cpu_samples",
        max_ns_key="route_query_cpu_max_ns",
        fail_ns_key="route_query_cpu_fail_ns",
        fail_samples_key="route_query_cpu_fail_samples",
    )
    add_cpu_metrics(
        metrics,
        status,
        "route_reuse_cpu",
        duration_sec,
        per_bot_divisor,
        total_ns_keys=("route_reuse_cpu_ns",),
        samples_key="route_reuse_cpu_samples",
    )
    add_cpu_metrics(
        metrics,
        status,
        "q3a_route_cpu",
        duration_sec,
        per_bot_divisor,
        total_ns_keys=("q3a_route_cpu_ns",),
        samples_key="q3a_route_cpu_samples",
        max_ns_key="q3a_route_cpu_max_ns",
        fail_ns_key="q3a_route_cpu_fail_ns",
        fail_samples_key="q3a_route_cpu_fail_samples",
    )
    add_cpu_metrics(
        metrics,
        status,
        "bsp_trace_cpu",
        duration_sec,
        per_bot_divisor,
        total_ns_keys=("bsp_trace_cpu_ns",),
        samples_key="bsp_trace_cpu_samples",
        samples_keys=("bsp_trace_calls",),
        max_ns_key="bsp_trace_cpu_max_ns",
    )
    add_cpu_metrics(
        metrics,
        status,
        "entity_trace_clip_cpu",
        duration_sec,
        per_bot_divisor,
        total_ns_keys=("entity_trace_clip_cpu_ns",),
        samples_key="entity_trace_clip_calls",
        max_ns_key="entity_trace_clip_cpu_max_ns",
    )

    for key in (
        "aas_inpvs_checks",
        "aas_inphs_checks",
        "visibility_cluster_checks",
        "visibility_decompress_calls",
        "visibility_decompress_bytes",
        "aas_trace_calls",
        "bsp_trace_calls",
        "bsp_trace_hull_nodes",
        "bsp_trace_brush_tests",
        "entity_trace_attempts",
        "entity_trace_clip_calls",
        "pvs_checks",
        "phs_checks",
        "visibility_traces",
        "visibility_trace_tests",
    ):
        add_rate_metrics(metrics, status, key, duration_sec, per_bot_divisor)

    for key in (
        "visibility_decompress_failures",
        "entity_trace_failures",
        "bsp_trace_startsolid",
        "bsp_trace_allsolid",
        "entity_trace_clip_startsolid",
        "entity_trace_clip_allsolid",
    ):
        metrics[key] = as_int(status, key)

    add_ratio_metric(metrics, status, "aas_inpvs_visible_ratio", "aas_inpvs_visible", "aas_inpvs_checks")
    add_ratio_metric(metrics, status, "aas_inphs_visible_ratio", "aas_inphs_visible", "aas_inphs_checks")
    add_ratio_metric(metrics, status, "bsp_trace_hit_ratio", "bsp_trace_hits", "bsp_trace_calls")
    add_ratio_metric(
        metrics,
        status,
        "entity_trace_clip_hit_ratio",
        "entity_trace_clip_hits",
        "entity_trace_clip_calls",
    )

    metrics.update(progress_summary(parsed.progress))
    return metrics


def load_budget(path: pathlib.Path) -> Budget:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise SystemExit(f"Unable to read budget {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Invalid budget JSON {path}: {exc}") from exc

    if not isinstance(payload, dict):
        raise SystemExit(f"Invalid budget {path}: expected JSON object")

    checks = payload.get("checks")
    if not isinstance(checks, dict):
        raise SystemExit(f"Invalid budget {path}: expected checks object")

    metrics = load_budget_namespace(path, checks, "metrics")
    status = load_budget_namespace(path, checks, "status")
    if not metrics and not status:
        raise SystemExit(f"Invalid budget {path}: expected checks.metrics or checks.status thresholds")

    return Budget(path=path, metrics=metrics, status=status)


def load_scenario_report(path: pathlib.Path) -> dict[str, dict[str, Any]]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise SystemExit(f"Unable to read scenario report {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Invalid scenario report JSON {path}: {exc}") from exc

    if not isinstance(payload, dict):
        raise SystemExit(f"Invalid scenario report {path}: expected JSON object")

    scenarios = payload.get("scenarios")
    if not isinstance(scenarios, list):
        raise SystemExit(f"Invalid scenario report {path}: expected scenarios array")

    result: dict[str, dict[str, Any]] = {}
    for index, scenario in enumerate(scenarios):
        if not isinstance(scenario, dict):
            continue
        stdout_path = scenario.get("stdout_path")
        if not isinstance(stdout_path, str) or not stdout_path:
            continue
        metadata: dict[str, Any] = {
            "name": scenario.get("name"),
            "title": scenario.get("title"),
            "status": scenario.get("status"),
            "returncode": scenario.get("returncode"),
            "duration_seconds": scenario.get("duration_seconds"),
            "duration_budget_passed": scenario.get("duration_budget_passed"),
            "report_index": index,
            "report_path": str(path),
        }
        result[path_key(pathlib.Path(stdout_path))] = metadata

    if not result:
        raise SystemExit(f"Scenario report {path} does not contain stdout_path entries")
    return result


def load_budget_namespace(
    path: pathlib.Path,
    checks: dict[str, Any],
    namespace: str,
) -> dict[str, dict[str, Any]]:
    payload = checks.get(namespace, {})
    if payload is None:
        return {}
    if not isinstance(payload, dict):
        raise SystemExit(f"Invalid budget {path}: checks.{namespace} must be an object")

    result: dict[str, dict[str, Any]] = {}
    for metric, threshold in payload.items():
        if not isinstance(metric, str) or not metric:
            raise SystemExit(f"Invalid budget {path}: checks.{namespace} has a non-string metric name")
        if not isinstance(threshold, dict):
            raise SystemExit(
                f"Invalid budget {path}: checks.{namespace}.{metric} must be an object"
            )

        allowed = {"min", "max", "required", "description"}
        unknown = sorted(set(threshold) - allowed)
        if unknown:
            raise SystemExit(
                f"Invalid budget {path}: checks.{namespace}.{metric} has unknown keys: "
                + ", ".join(unknown)
            )

        if "min" not in threshold and "max" not in threshold:
            raise SystemExit(
                f"Invalid budget {path}: checks.{namespace}.{metric} must define min or max"
            )

        for key in ("min", "max"):
            if key in threshold and not isinstance(threshold[key], (int, float)):
                raise SystemExit(
                    f"Invalid budget {path}: checks.{namespace}.{metric}.{key} must be numeric"
                )

        required = threshold.get("required", True)
        if not isinstance(required, bool):
            raise SystemExit(
                f"Invalid budget {path}: checks.{namespace}.{metric}.required must be boolean"
            )

        if "description" in threshold and not isinstance(threshold["description"], str):
            raise SystemExit(
                f"Invalid budget {path}: checks.{namespace}.{metric}.description must be a string"
            )

        result[metric] = dict(threshold)

    return result


def evaluate_budget(
    report: dict[str, Any],
    status: dict[str, int | float],
    budget: Budget,
) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    failures: list[str] = []
    warnings: list[str] = []

    for namespace, thresholds, values in (
        ("metrics", budget.metrics, report),
        ("status", budget.status, status),
    ):
        for metric, threshold in thresholds.items():
            result = evaluate_budget_check(namespace, metric, threshold, values)
            checks.append(result)
            if result["result"] == "fail":
                failures.append(result["message"])
            elif result["result"] == "missing_optional":
                warnings.append(result["message"])

    required_checks = [check for check in checks if check.get("required", True)]
    optional_checks = [check for check in checks if not check.get("required", True)]
    failed_checks = [check for check in checks if check.get("result") == "fail"]
    passed_checks = [check for check in checks if check.get("result") == "pass"]
    missing_optional_checks = [
        check for check in checks if check.get("result") == "missing_optional"
    ]
    required_failed = [
        check for check in required_checks if check.get("result") == "fail"
    ]
    missing_current_counters = budget_missing_current_counter_diagnostics(status, checks)
    budget_pass = not failures

    return {
        "path": str(budget.path),
        "pass": budget_pass,
        "pass_int": 1 if budget_pass else 0,
        "status": "pass" if budget_pass else "fail",
        "failures": failures,
        "warnings": warnings,
        "checks": checks,
        "check_count": len(checks),
        "passed": len(passed_checks),
        "failed": len(failed_checks),
        "warning_count": len(warnings),
        "required": len(required_checks),
        "optional": len(optional_checks),
        "required_passed": len(required_checks) - len(required_failed),
        "required_failed": len(required_failed),
        "optional_missing": len(missing_optional_checks),
        "missing_current_counters": missing_current_counters,
        "missing_current_counter_count": len(missing_current_counters),
    }


def evaluate_budget_check(
    namespace: str,
    metric: str,
    threshold: dict[str, Any],
    values: dict[str, Any],
) -> dict[str, Any]:
    required = threshold.get("required", True)
    minimum = threshold.get("min")
    maximum = threshold.get("max")
    value = values.get(metric)
    label = f"{namespace}.{metric}"

    result: dict[str, Any] = {
        "namespace": namespace,
        "metric": metric,
        "value": value,
        "min": minimum,
        "max": maximum,
        "required": required,
        "result": "pass",
        "message": "",
    }

    if value is None:
        state = "fail" if required else "missing_optional"
        message = f"{label} is missing"
        if not required:
            message += " (optional)"
        result.update({"result": state, "message": message})
        return result

    if not isinstance(value, (int, float)):
        result.update({
            "result": "fail",
            "message": f"{label} is non-numeric: {value!r}",
        })
        return result

    failures: list[str] = []
    if isinstance(minimum, (int, float)) and value < minimum:
        failures.append(f"{label}={value} is below min {minimum}")
    if isinstance(maximum, (int, float)) and value > maximum:
        failures.append(f"{label}={value} is above max {maximum}")

    if failures:
        result.update({"result": "fail", "message": "; ".join(failures)})
    else:
        result["message"] = f"{label}={value} within budget"
    return result


def attach_budget_result(report: dict[str, Any], budget_result: dict[str, Any]) -> None:
    report["budget"] = budget_result
    report["budget_status"] = budget_result.get("status")
    report["budget_pass"] = bool(budget_result.get("pass"))
    report["budget_pass_int"] = budget_result.get("pass_int")
    report["budget_checks"] = budget_result.get("check_count")
    report["budget_failures"] = len(budget_result.get("failures", []))
    report["budget_warnings"] = len(budget_result.get("warnings", []))
    report["budget_required_failed"] = budget_result.get("required_failed")
    report["budget_required_passed"] = budget_result.get("required_passed")
    report["budget_optional_missing"] = budget_result.get("optional_missing")
    report["budget_missing_current_counters"] = budget_result.get(
        "missing_current_counter_count"
    )


def is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def format_value(value: Any) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, bool):
        return "pass" if value else "fail"
    if isinstance(value, float):
        text = f"{value:.3f}"
        return text.rstrip("0").rstrip(".")
    return str(value)


def markdown_escape(value: Any) -> str:
    return format_value(value).replace("|", "\\|")


def report_label(report: dict[str, Any], index: int) -> str:
    return f"{index + 1}: {report.get('file', 'unknown')}"


def choose_metric_extreme(
    rows: list[tuple[int, dict[str, Any], float]],
    goal: str,
    best: bool,
) -> tuple[int, dict[str, Any], float]:
    reverse = goal == "higher"
    if not best:
        reverse = not reverse
    return sorted(rows, key=lambda row: row[2], reverse=reverse)[0]


def unique_report_values(reports: list[dict[str, Any]], key: str) -> list[Any]:
    values: list[Any] = []
    for report in reports:
        value = report.get(key)
        if value is None:
            continue
        if value not in values:
            values.append(value)
    return values


def add_comparison_guard(
    guards: list[dict[str, Any]],
    code: str,
    message: str,
    values: list[Any] | None = None,
) -> None:
    guards.append({
        "code": code,
        "severity": "warning",
        "message": message,
        "values": [format_value(value) for value in (values or [])],
    })


def build_comparison_guards(reports: list[dict[str, Any]]) -> list[dict[str, Any]]:
    guards: list[dict[str, Any]] = []
    if len(reports) < 2:
        return guards

    scenario_names = unique_report_values(reports, "scenario_name")
    if len(scenario_names) > 1:
        add_comparison_guard(
            guards,
            "mixed_scenarios",
            "comparison includes multiple scenario names; use best/worst values as a cross-scenario overview, not a same-scenario regression ranking",
            scenario_names,
        )

    bot_counts = unique_report_values(reports, "bot_count")
    if len(bot_counts) > 1:
        add_comparison_guard(
            guards,
            "mixed_bot_counts",
            "comparison includes different bot counts; per-bot rates are normalized, but aggregate pressure is not like-for-like",
            bot_counts,
        )

    duration_sources = unique_report_values(reports, "duration_source")
    if len(duration_sources) > 1:
        add_comparison_guard(
            guards,
            "mixed_duration_sources",
            "comparison mixes duration sources; rate metrics may reflect different measurement windows",
            duration_sources,
        )

    missing_duration_count = sum(1 for report in reports if report.get("duration_sec") is None)
    if missing_duration_count:
        add_comparison_guard(
            guards,
            "missing_duration",
            "one or more runs lack duration data; per-second comparison metrics omit those runs",
            [missing_duration_count],
        )

    return guards


def build_comparison(reports: list[dict[str, Any]]) -> dict[str, Any]:
    metrics: list[dict[str, Any]] = []
    latest = reports[-1] if reports else None

    for metric in COMPARISON_METRICS:
        key = str(metric["key"])
        numeric_rows = [
            (index, report, float(report[key]))
            for index, report in enumerate(reports)
            if is_number(report.get(key))
        ]
        if not numeric_rows:
            metrics.append({
                "key": key,
                "label": metric["label"],
                "goal": metric["goal"],
                "latest": None,
                "latest_file": latest.get("file") if latest else None,
                "best": None,
                "best_file": None,
                "worst": None,
                "worst_file": None,
            })
            continue

        best_index, best_report, best_value = choose_metric_extreme(
            numeric_rows,
            str(metric["goal"]),
            best=True,
        )
        worst_index, worst_report, worst_value = choose_metric_extreme(
            numeric_rows,
            str(metric["goal"]),
            best=False,
        )
        metrics.append({
            "key": key,
            "label": metric["label"],
            "goal": metric["goal"],
            "latest": latest.get(key) if latest else None,
            "latest_file": latest.get("file") if latest else None,
            "best": round_metric(best_value),
            "best_run": best_index + 1,
            "best_file": best_report.get("file"),
            "worst": round_metric(worst_value),
            "worst_run": worst_index + 1,
            "worst_file": worst_report.get("file"),
        })

    budget_runs: list[dict[str, Any]] = []
    for index, report in enumerate(reports):
        budget = report.get("budget")
        if not isinstance(budget, dict):
            continue
        budget_runs.append({
            "run": index + 1,
            "file": report.get("file"),
            "pass": bool(budget.get("pass")),
            "failures": len(budget.get("failures", [])),
            "warnings": len(budget.get("warnings", [])),
            "missing_current_counters": budget.get("missing_current_counter_count", 0),
        })

    budget_summary = None
    if budget_runs:
        failed = [item for item in budget_runs if not item["pass"]]
        budget_summary = {
            "runs": budget_runs,
            "passed": len(budget_runs) - len(failed),
            "failed": len(failed),
            "latest_pass": budget_runs[-1]["pass"],
        }

    return {
        "run_count": len(reports),
        "latest_file": latest.get("file") if latest else None,
        "guards": build_comparison_guards(reports),
        "metrics": metrics,
        "budget": budget_summary,
    }


def print_comparison_text(comparison: dict[str, Any]) -> None:
    print()
    print(f"comparison: runs={comparison.get('run_count')} latest={comparison.get('latest_file')}")

    for guard in comparison.get("guards", []):
        print(f"  guard: {guard.get('code')}: {guard.get('message')}")

    budget = comparison.get("budget")
    if isinstance(budget, dict):
        latest_state = "pass" if budget.get("latest_pass") else "fail"
        print(
            "  budget: "
            f"passed={budget.get('passed')} "
            f"failed={budget.get('failed')} "
            f"latest={latest_state}"
        )
        for run in budget.get("runs", []):
            state = "pass" if run.get("pass") else "fail"
            print(
                "    "
                f"run={run.get('run')} state={state} "
                f"failures={run.get('failures')} "
                f"warnings={run.get('warnings')} "
                f"missing_current_counters={run.get('missing_current_counters')} "
                f"file={run.get('file')}"
            )

    for metric in comparison.get("metrics", []):
        best_run = metric.get("best_run")
        worst_run = metric.get("worst_run")
        print(
            "  "
            f"{metric.get('label')}: "
            f"latest={format_value(metric.get('latest'))} "
            f"best={format_value(metric.get('best'))}"
            f"{f' (run {best_run})' if best_run else ''} "
            f"worst={format_value(metric.get('worst'))}"
            f"{f' (run {worst_run})' if worst_run else ''}"
        )


def markdown_table(headers: list[str], rows: list[list[Any]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(markdown_escape(value) for value in row) + " |")
    return lines


def markdown_report(reports: list[dict[str, Any]], comparison: dict[str, Any]) -> str:
    lines: list[str] = [
        "# WORR Bot Perf Comparison",
        "",
        f"Runs: {len(reports)}",
        f"Latest: `{comparison.get('latest_file')}`",
        "",
    ]

    guards = comparison.get("guards", [])
    if guards:
        lines.extend(["## Guards", ""])
        lines.extend(markdown_table(
            ["Code", "Severity", "Values", "Message"],
            [
                [
                    guard.get("code"),
                    guard.get("severity"),
                    ", ".join(guard.get("values", [])),
                    guard.get("message"),
                ]
                for guard in guards
            ],
        ))
        lines.append("")

    lines.extend(["## Runs", ""])

    run_rows: list[list[Any]] = []
    for index, report in enumerate(reports):
        budget = report.get("budget")
        budget_state = "n/a"
        budget_failures = "n/a"
        budget_warnings = "n/a"
        budget_missing_current_counters = "n/a"
        if isinstance(budget, dict):
            budget_state = budget.get("status")
            budget_failures = len(budget.get("failures", []))
            budget_warnings = len(budget.get("warnings", []))
            budget_missing_current_counters = budget.get("missing_current_counter_count")
        run_rows.append([
            index + 1,
            report.get("scenario_name"),
            report.get("file"),
            report.get("pass"),
            budget_state,
            budget_failures,
            budget_warnings,
            budget_missing_current_counters,
            report.get("source_counter_status"),
            report.get("source_counter_groups_missing_count"),
            report.get("duration_sec"),
            report.get("bot_count"),
            report.get("commands_per_bot_sec"),
            report.get("route_queries_per_bot_sec"),
            report.get("route_refresh_ratio"),
            report.get("route_reuse_ratio"),
            report.get("debug_work_units_per_bot_sec"),
            report.get("recovery_command_uses_per_bot_sec"),
            report.get("bot_frame_cpu_ms_per_bot_sec"),
            report.get("route_query_cpu_ms_per_bot_sec"),
            report.get("q3a_route_cpu_ms_per_bot_sec"),
            report.get("aas_inpvs_checks_per_bot_sec"),
            report.get("bsp_trace_calls_per_bot_sec"),
            report.get("entity_trace_clip_calls_per_bot_sec"),
        ])

    lines.extend(markdown_table(
        [
            "Run",
            "Scenario",
            "File",
            "Smoke Pass",
            "Budget",
            "Budget Failures",
            "Budget Warnings",
            "Budget Missing Counters",
            "Source Counters",
            "Missing Source Groups",
            "Duration Sec",
            "Bots",
            "Cmd/Bot/Sec",
            "Route Query/Bot/Sec",
            "Refresh Ratio",
            "Reuse Ratio",
            "Debug Work/Bot/Sec",
            "Recovery Cmd/Bot/Sec",
            "Bot CPU Ms/Bot/Sec",
            "Route CPU Ms/Bot/Sec",
            "Q3A Route CPU Ms/Bot/Sec",
            "inPVS/Bot/Sec",
            "BSP Trace/Bot/Sec",
            "Entity Clip/Bot/Sec",
        ],
        run_rows,
    ))

    lines.extend(["", "## Comparison", ""])
    comparison_rows = [
        [
            metric.get("label"),
            metric.get("goal"),
            metric.get("latest"),
            f"{format_value(metric.get('best'))} (run {metric.get('best_run')})"
            if metric.get("best_run") else "n/a",
            f"{format_value(metric.get('worst'))} (run {metric.get('worst_run')})"
            if metric.get("worst_run") else "n/a",
        ]
        for metric in comparison.get("metrics", [])
    ]
    lines.extend(markdown_table(
        ["Metric", "Goal", "Latest", "Best", "Worst"],
        comparison_rows,
    ))

    budget = comparison.get("budget")
    if isinstance(budget, dict):
        lines.extend(["", "## Budget", ""])
        lines.append(
            f"Passed: `{budget.get('passed')}`; failed: `{budget.get('failed')}`; "
            f"latest: `{'pass' if budget.get('latest_pass') else 'fail'}`."
        )
        lines.append("")
        lines.extend(markdown_table(
            ["Run", "Budget", "Failures", "Warnings", "Missing Current Counters", "File"],
            [
                [
                    run.get("run"),
                    bool(run.get("pass")),
                    run.get("failures"),
                    run.get("warnings"),
                    run.get("missing_current_counters"),
                    run.get("file"),
                ]
                for run in budget.get("runs", [])
            ],
        ))

    lines.append("")
    return "\n".join(lines)


def write_markdown_report(
    path: pathlib.Path,
    reports: list[dict[str, Any]],
    comparison: dict[str, Any],
) -> None:
    parent = path.parent
    if str(parent) and parent != pathlib.Path("."):
        parent.mkdir(parents=True, exist_ok=True)
    path.write_text(markdown_report(reports, comparison), encoding="utf-8")


def expand_inputs(values: list[str]) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    for value in values:
        path = pathlib.Path(value)
        if path.exists():
            paths.append(path)
            continue

        matches = sorted(pathlib.Path(match) for match in glob.glob(value))
        if matches:
            paths.extend(matches)
            continue

        raise SystemExit(f"Input path does not exist: {value}")

    files = [path for path in paths if path.is_file()]
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise SystemExit("Input is not a file: " + ", ".join(str(path) for path in missing))
    return files


def compact_counter_detail(detail: dict[str, Any]) -> str:
    group = detail.get("group")
    metric = detail.get("metric")
    primary = detail.get("primary_counter")
    missing = detail.get("missing_current_counters") or detail.get("missing_counters") or []
    if not isinstance(missing, list):
        missing = [missing]
    missing_text = ",".join(str(item) for item in missing) if missing else "none"

    prefix = f"group={group}"
    if metric:
        prefix += f" metric={metric}"
    if primary:
        prefix += f" primary={primary}"
    return f"{prefix} missing={missing_text}"


def print_text(reports: list[dict[str, Any]]) -> None:
    for index, report in enumerate(reports):
        if index:
            print()
        print(f"file: {report['file']}")
        if report.get("scenario_name"):
            print(
                "  scenario: "
                f"name={report.get('scenario_name')} "
                f"status={report.get('scenario_status')} "
                f"returncode={report.get('scenario_returncode')} "
                f"duration_source={report.get('duration_source')}"
            )
        print(f"  pass={report['pass']} status_lines={report['status_lines']} bots={report['bot_count']} duration_sec={report['duration_sec']}")
        print(
            "  throughput: "
            f"commands/sec={report['commands_per_sec']} "
            f"commands/bot/sec={report['commands_per_bot_sec']} "
            f"frames/sec={report['frames_per_sec']}"
        )
        print(
            "  route: "
            f"requests/sec={report['route_requests_per_sec']} "
            f"queries/sec={report['route_queries_per_sec']} "
            f"queries/bot/sec={report['route_queries_per_bot_sec']} "
            f"refreshes/sec={report['route_refreshes_per_sec']} "
            f"reuses/sec={report['route_reuses_per_sec']} "
            f"query_ratio={report['route_query_ratio']} "
            f"refresh_ratio={report['route_refresh_ratio']} "
            f"reuse_ratio={report['route_reuse_ratio']} "
            f"failures={report['route_failures']}"
        )
        print(
            "  debug: "
            f"routes/sec={report['route_debug_routes_per_sec']} "
            f"goals/sec={report['route_debug_goals_per_sec']} "
            f"direct_primitives/sec={report['debug_direct_primitives_per_sec']} "
            f"polyline_segments/sec={report['debug_polyline_segments_per_sec']} "
            f"work_units/sec={report['debug_work_units_per_sec']} "
            f"work_units/bot/sec={report['debug_work_units_per_bot_sec']}"
        )
        print(
            "  recovery: "
            f"stuck_detections/sec={report['stuck_detections_per_sec']} "
            f"activations/sec={report['stuck_recovery_activations_per_sec']} "
            f"recovery_commands/sec={report['recovery_command_uses_per_sec']} "
            f"recovery_commands/bot/sec={report['recovery_command_uses_per_bot_sec']}"
        )
        print(
            "  goals: "
            f"route_assignments/sec={report['route_goal_assignments_per_sec']} "
            f"item_assignments/sec={report['item_goal_assignments_per_sec']} "
            f"reservation_skips/sec={report['item_goal_reservation_skips_per_sec']} "
            f"peak_active_reservations={report['item_goal_peak_active_reservations']}"
        )
        print(
            "  progress: "
            f"reports={report['progress_reports']} "
            f"avg_interval_sec={report['progress_interval_avg_sec']} "
            f"min_interval_sec={report['progress_interval_min_sec']} "
            f"max_interval_sec={report['progress_interval_max_sec']}"
        )
        present_groups = ", ".join(report["source_counter_groups_present"])
        missing_groups = ", ".join(report["source_counter_groups_missing"])
        print(
            "  source_counters: "
            f"status={report['source_counter_status']} "
            f"pass={report['source_counter_pass_int']} "
            f"present={report['source_counter_groups_present_count']}/"
            f"{len(report['source_counter_groups_expected'])} "
            f"groups={present_groups if present_groups else 'none'} "
            f"missing={missing_groups if missing_groups else 'none'}"
        )
        if has_any_metric(report, (
            "bot_frame_cpu_ms_per_bot_sec",
            "route_query_cpu_ms_per_bot_sec",
            "q3a_route_cpu_ms_per_bot_sec",
        )):
            print(
                "  source_cpu: "
                f"bot_frame_ms/bot/sec={report['bot_frame_cpu_ms_per_bot_sec']} "
                f"bot_frame_avg_us={report['bot_frame_cpu_avg_us']} "
                f"bot_frame_max_us={report['bot_frame_cpu_max_us']} "
                f"route_query_ms/bot/sec={report['route_query_cpu_ms_per_bot_sec']} "
                f"route_query_avg_us={report['route_query_cpu_avg_us']} "
                f"q3a_route_ms/bot/sec={report['q3a_route_cpu_ms_per_bot_sec']} "
                f"q3a_route_avg_us={report['q3a_route_cpu_avg_us']}"
            )
        if has_any_metric(report, (
            "aas_inpvs_checks_per_bot_sec",
            "aas_inphs_checks_per_bot_sec",
            "visibility_decompress_calls_per_sec",
        )):
            print(
                "  source_visibility: "
                f"inpvs/bot/sec={report['aas_inpvs_checks_per_bot_sec']} "
                f"inpvs_visible_ratio={report['aas_inpvs_visible_ratio']} "
                f"inphs/bot/sec={report['aas_inphs_checks_per_bot_sec']} "
                f"inphs_visible_ratio={report['aas_inphs_visible_ratio']} "
                f"decompress_calls/sec={report['visibility_decompress_calls_per_sec']} "
                f"decompress_failures={report['visibility_decompress_failures']}"
            )
        if has_any_metric(report, (
            "bsp_trace_calls_per_bot_sec",
            "entity_trace_clip_calls_per_bot_sec",
        )):
            print(
                "  source_trace: "
                f"bsp_calls/bot/sec={report['bsp_trace_calls_per_bot_sec']} "
                f"bsp_avg_us={report['bsp_trace_cpu_avg_us']} "
                f"bsp_max_us={report['bsp_trace_cpu_max_us']} "
                f"entity_attempts/sec={report['entity_trace_attempts_per_sec']} "
                f"entity_clip/bot/sec={report['entity_trace_clip_calls_per_bot_sec']} "
                f"entity_clip_avg_us={report['entity_trace_clip_cpu_avg_us']} "
                f"entity_failures={report['entity_trace_failures']}"
            )
        missing = ", ".join(report["missing_instrumentation"])
        print(f"  missing_instrumentation: {missing if missing else 'none'}")
        for detail in report.get("missing_current_counters", []):
            print(f"    missing_current_counter: {compact_counter_detail(detail)}")
        budget = report.get("budget")
        if isinstance(budget, dict):
            state = "pass" if budget.get("pass") else "fail"
            check_count = len(budget.get("checks", []))
            print(
                f"  budget: {state} "
                f"pass={budget.get('pass_int')} "
                f"checks={check_count} "
                f"failures={len(budget.get('failures', []))} "
                f"warnings={len(budget.get('warnings', []))} "
                f"optional_missing={budget.get('optional_missing')} "
                f"missing_current_counters={budget.get('missing_current_counter_count')} "
                f"path={budget.get('path')}"
            )
            for failure in budget.get("failures", []):
                print(f"    failure: {failure}")
            for warning in budget.get("warnings", []):
                print(f"    warning: {warning}")
            for detail in budget.get("missing_current_counters", []):
                print(f"    missing_current_counter: {compact_counter_detail(detail)}")


def print_csv(reports: list[dict[str, Any]]) -> None:
    fieldnames = [
        "file",
        "scenario_name",
        "scenario_status",
        "scenario_returncode",
        "scenario_duration_budget_passed",
        "duration_source",
        "pass",
        "source_counter_status",
        "source_counter_pass",
        "source_counter_pass_int",
        "source_counter_groups_present_count",
        "source_counter_groups_missing_count",
        "source_counter_groups_present",
        "source_counter_groups_missing",
        "missing_current_counters",
        "duration_sec",
        "bot_count",
        "commands_per_sec",
        "commands_per_bot_sec",
        "route_requests_per_sec",
        "route_queries_per_sec",
        "route_refreshes_per_sec",
        "route_reuses_per_sec",
        "route_query_ratio",
        "route_refresh_ratio",
        "route_reuse_ratio",
        "route_failures",
        "debug_direct_primitives_per_sec",
        "debug_polyline_segments_per_sec",
        "debug_work_units_per_sec",
        "stuck_detections_per_sec",
        "recovery_command_uses_per_sec",
        "bot_frame_cpu_ms_per_bot_sec",
        "bot_frame_cpu_avg_us",
        "bot_frame_cpu_max_us",
        "route_query_cpu_ms_per_bot_sec",
        "route_query_cpu_avg_us",
        "route_query_cpu_max_us",
        "q3a_route_cpu_ms_per_bot_sec",
        "q3a_route_cpu_avg_us",
        "aas_inpvs_checks_per_bot_sec",
        "aas_inpvs_visible_ratio",
        "aas_inphs_checks_per_bot_sec",
        "aas_inphs_visible_ratio",
        "visibility_decompress_calls_per_sec",
        "visibility_decompress_failures",
        "bsp_trace_calls_per_bot_sec",
        "bsp_trace_cpu_avg_us",
        "bsp_trace_hit_ratio",
        "entity_trace_attempts_per_sec",
        "entity_trace_clip_calls_per_bot_sec",
        "entity_trace_clip_cpu_avg_us",
        "entity_trace_failures",
        "progress_reports",
        "budget_status",
        "budget_pass",
        "budget_pass_int",
        "budget_checks",
        "budget_failures",
        "budget_warnings",
        "budget_required_failed",
        "budget_required_passed",
        "budget_optional_missing",
        "budget_missing_current_counters",
    ]
    writer = csv.DictWriter(sys.stdout, fieldnames=fieldnames, lineterminator="\n")
    writer.writeheader()
    for report in reports:
        row = {key: report.get(key) for key in fieldnames}
        row["source_counter_groups_present"] = ";".join(
            report.get("source_counter_groups_present", [])
        )
        row["source_counter_groups_missing"] = ";".join(
            report.get("source_counter_groups_missing", [])
        )
        row["missing_current_counters"] = ";".join(
            compact_counter_detail(detail)
            for detail in report.get("missing_current_counters", [])
        )
        budget = report.get("budget")
        if isinstance(budget, dict):
            row["budget_status"] = budget.get("status")
            row["budget_pass"] = budget.get("pass")
            row["budget_pass_int"] = budget.get("pass_int")
            row["budget_checks"] = budget.get("check_count")
            row["budget_failures"] = len(budget.get("failures", []))
            row["budget_warnings"] = len(budget.get("warnings", []))
            row["budget_required_failed"] = budget.get("required_failed")
            row["budget_required_passed"] = budget.get("required_passed")
            row["budget_optional_missing"] = budget.get("optional_missing")
            row["budget_missing_current_counters"] = budget.get("missing_current_counter_count")
        writer.writerow(row)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Analyze q3a_bot_frame_command_status and soak progress telemetry."
    )
    parser.add_argument("logs", nargs="+", help="Smoke stdout/log file(s) to analyze.")
    parser.add_argument(
        "--format",
        choices=("text", "json", "csv"),
        default="text",
        help="Output format. Defaults to text.",
    )
    parser.add_argument(
        "--budget",
        help="Optional JSON budget file with checks.metrics and checks.status thresholds.",
    )
    parser.add_argument(
        "--markdown-out",
        help="Optional path for a Markdown comparison report.",
    )
    parser.add_argument(
        "--scenario-report",
        help="Optional bot_scenarios JSON report that supplies scenario names and durations for stdout logs.",
    )
    args = parser.parse_args(argv)

    paths = expand_inputs(args.logs)
    scenario_metadata = load_scenario_report(pathlib.Path(args.scenario_report)) if args.scenario_report else {}
    parsed_logs = [parse_log(path) for path in paths]
    reports = [
        analyze(parsed, scenario_metadata.get(path_key(parsed.path)))
        for parsed in parsed_logs
    ]

    if args.budget:
        budget = load_budget(pathlib.Path(args.budget))
        for report, parsed in zip(reports, parsed_logs):
            attach_budget_result(report, evaluate_budget(report, parsed.status, budget))

    comparison = build_comparison(reports)
    if args.markdown_out:
        write_markdown_report(pathlib.Path(args.markdown_out), reports, comparison)

    if args.format == "json":
        output: Any = reports
        if len(reports) > 1:
            output = {
                "runs": reports,
                "comparison": comparison,
            }
        print(json.dumps(output, indent=2, sort_keys=True))
    elif args.format == "csv":
        print_csv(reports)
    else:
        print_text(reports)
        if len(reports) > 1:
            print_comparison_text(comparison)

    if any(isinstance(report.get("budget"), dict) and not report["budget"]["pass"] for report in reports):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
