#!/usr/bin/env python3
"""Compare like-for-like renderer telemetry without launching a client."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
import sys
from pathlib import Path


STAT_RE = re.compile(r"^(VK_STATS|GL_STATS)\s+(.*)$", re.MULTILINE)
FIELD_RE = re.compile(r"([a-z_]+)=([^\s]+)")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_sha256(value: object, name: str) -> str:
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise ValueError(f"capture manifest {name} must be a SHA-256 string")
    return value.lower()


def validate_capture_manifest(manifest_path: Path, vulkan_log: Path,
                              opengl_log: Path) -> dict[str, str]:
    """Verify that telemetry inputs belong to one reproducible paired capture."""
    raw = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("schema_version") != 1:
        raise ValueError("capture manifest schema_version must be 1")

    scenario = raw.get("scenario")
    environment = raw.get("environment")
    if not isinstance(scenario, dict) or not isinstance(environment, dict):
        raise ValueError("capture manifest requires scenario and environment objects")
    scenario_id = scenario.get("id")
    if not isinstance(scenario_id, str) or not scenario_id.strip():
        raise ValueError("capture manifest scenario.id must be non-empty")
    fixture_sha256 = require_sha256(scenario.get("fixture_sha256"),
                                    "scenario.fixture_sha256")
    config_sha256 = require_sha256(scenario.get("config_sha256"),
                                   "scenario.config_sha256")
    hardware_id = environment.get("hardware_id")
    driver = environment.get("driver")
    if not isinstance(hardware_id, str) or not hardware_id.strip():
        raise ValueError("capture manifest environment.hardware_id must be non-empty")
    if not isinstance(driver, str) or not driver.strip():
        raise ValueError("capture manifest environment.driver must be non-empty")

    capture_specs = (("vulkan", vulkan_log), ("opengl", opengl_log))
    result = {
        "scenario_id": scenario_id,
        "fixture_sha256": fixture_sha256,
        "config_sha256": config_sha256,
        "hardware_id": hardware_id,
        "driver": driver,
        "manifest_sha256": file_sha256(manifest_path),
    }
    for renderer, log_path in capture_specs:
        capture = raw.get(renderer)
        if not isinstance(capture, dict):
            raise ValueError(f"capture manifest requires {renderer} object")
        if capture.get("renderer") != renderer:
            raise ValueError(
                f"capture manifest {renderer}.renderer must be {renderer!r}"
            )
        expected_hash = require_sha256(capture.get("log_sha256"),
                                       f"{renderer}.log_sha256")
        actual_hash = file_sha256(log_path)
        if actual_hash != expected_hash:
            raise ValueError(
                f"capture manifest {renderer} log hash does not match {log_path}"
            )
        result[f"{renderer}_log_sha256"] = actual_hash
    return result


def parse_stats(path: Path, expected_prefix: str) -> list[dict[str, float]]:
    records: list[dict[str, float]] = []
    for prefix, fields in STAT_RE.findall(path.read_text(encoding="utf-8", errors="replace")):
        if prefix != expected_prefix:
            continue
        record: dict[str, float] = {}
        for key, value in FIELD_RE.findall(fields):
            if key == "missing_mask":
                continue
            try:
                record[key] = float(value)
            except ValueError:
                continue
        records.append(record)
    return records


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        raise ValueError("cannot calculate percentile of no values")
    ordered = sorted(values)
    index = max(0, math.ceil(fraction * len(ordered)) - 1)
    return ordered[index]


def summarize(records: list[dict[str, float]], warmup: int) -> dict[str, float | int]:
    samples = records[warmup:]
    if not samples:
        raise ValueError("no telemetry samples remain after warmup")
    result: dict[str, float | int] = {"samples": len(samples)}
    for metric in (
        "cpu_ms",
        "gpu_ms",
        "gpu_frame_ms",
        "gpu_upload_ms",
        "gpu_shadow_ms",
        "gpu_opaque_world_ms",
        "gpu_opaque_entity_ms",
        "gpu_scene_ms",
        "gpu_post_ms",
        "world_fast_lit_draws",
        "world_fast_lit_no_fog_draws",
        "world_texture_replace_draws",
        "world_texture_replace_no_fog_draws",
        "entity_fast_lit_draws",
        "entity_fast_lit_no_fog_draws",
        "entity_texture_replace_draws",
        "entity_texture_replace_no_fog_draws",
        "world_fast_lit_candidates",
        "world_fast_lit_disabled",
        "world_fast_lit_fullbright",
        "world_fast_lit_receiver_lighting",
        "world_fast_lit_pipeline_unavailable",
        "world_fast_lit_material_ineligible",
        "draws",
        "uploads",
        "world_draws",
        "entity_draws",
        "ui_draws",
        "post_draws",
        "shadow_draws",
        "debug_draws",
    ):
        values = [sample[metric] for sample in samples if metric in sample]
        if not values:
            continue
        result[f"{metric}_mean"] = statistics.fmean(values)
        result[f"{metric}_p50"] = statistics.median(values)
        result[f"{metric}_p95"] = percentile(values, 0.95)
    result["gpu_valid_samples"] = sum(sample.get("gpu_valid", 0.0) > 0.0 for sample in samples)
    result["gpu_frame_valid_samples"] = sum(
        sample.get("gpu_frame_valid", 0.0) > 0.0 for sample in samples
    )
    return result


def ratios(vulkan: dict[str, float | int], opengl: dict[str, float | int]) -> dict[str, float]:
    result: dict[str, float] = {}
    for metric in (
        "cpu_ms_mean", "cpu_ms_p95", "gpu_ms_mean", "gpu_ms_p95",
        "gpu_frame_ms_mean", "gpu_frame_ms_p95",
        "gpu_post_ms_mean", "gpu_post_ms_p95",
    ):
        lhs = vulkan.get(metric)
        rhs = opengl.get(metric)
        if isinstance(lhs, (float, int)) and isinstance(rhs, (float, int)) and rhs > 0:
            result[metric] = float(lhs) / float(rhs)
    return result


def evaluate_budget(result: dict[str, object], budget_path: Path) -> list[str]:
    raw = json.loads(budget_path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("schema_version") != 1:
        raise ValueError("budget schema_version must be 1")
    failures: list[str] = []
    minimum = raw.get("min_samples")
    if isinstance(minimum, int):
        for renderer in ("vulkan", "opengl"):
            summary = result[renderer]
            assert isinstance(summary, dict)
            if int(summary.get("samples", 0)) < minimum:
                failures.append(f"{renderer} has fewer than budget min_samples={minimum}")
    if raw.get("require_gpu_valid") is True:
        for renderer in ("vulkan", "opengl"):
            summary = result[renderer]
            assert isinstance(summary, dict)
            if int(summary.get("gpu_valid_samples", 0)) < int(summary.get("samples", 0)):
                failures.append(f"{renderer} is missing valid GPU timing samples")
    if raw.get("require_gpu_frame_valid") is True:
        for renderer in ("vulkan", "opengl"):
            summary = result[renderer]
            assert isinstance(summary, dict)
            if int(summary.get("gpu_frame_valid_samples", 0)) < int(summary.get("samples", 0)):
                failures.append(f"{renderer} is missing valid full-frame GPU timing samples")

    capture_contract = raw.get("capture_contract")
    if capture_contract is not None:
        if not isinstance(capture_contract, dict):
            raise ValueError("budget capture_contract must be an object")
        observed_contract = result.get("capture_manifest")
        if not isinstance(observed_contract, dict):
            failures.append("budget capture_contract requires --capture-manifest")
        else:
            for field in (
                "scenario_id",
                "fixture_sha256",
                "config_sha256",
                "hardware_id",
                "driver",
            ):
                expected = capture_contract.get(field)
                if not isinstance(expected, str) or not expected.strip():
                    raise ValueError(
                        f"budget capture_contract.{field} must be a non-empty string"
                    )
                observed = observed_contract.get(field)
                if observed != expected:
                    failures.append(
                        f"capture {field}={observed!r} does not match "
                        f"budget contract {expected!r}"
                    )

    vulkan_limits = raw.get("vulkan_max", {})
    if not isinstance(vulkan_limits, dict):
        raise ValueError("budget vulkan_max must be an object")
    vulkan = result["vulkan"]
    assert isinstance(vulkan, dict)
    for metric, maximum in vulkan_limits.items():
        if not isinstance(maximum, (int, float)) or maximum <= 0:
            raise ValueError(f"budget Vulkan {metric} maximum must be positive")
        value = vulkan.get(metric)
        if not isinstance(value, (int, float)):
            failures.append(f"missing Vulkan metric: {metric}")
        elif value > maximum:
            failures.append(
                f"Vulkan {metric}={value:.4f} exceeds max={maximum:.4f}"
            )

    limits = raw.get("vulkan_over_opengl_max", {})
    if not isinstance(limits, dict):
        raise ValueError("budget vulkan_over_opengl_max must be an object")
    observed = result["ratios_vulkan_over_opengl"]
    assert isinstance(observed, dict)
    for metric, maximum in limits.items():
        if not isinstance(maximum, (int, float)) or maximum <= 0:
            raise ValueError(f"budget {metric} maximum must be positive")
        value = observed.get(metric)
        if not isinstance(value, (int, float)):
            failures.append(f"missing comparable ratio: {metric}")
        elif value > maximum:
            failures.append(f"{metric} ratio={value:.4f} exceeds max={maximum:.4f}")
    return failures


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vulkan", type=Path, required=True)
    parser.add_argument("--opengl", type=Path, required=True)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--min-samples", type=int, default=30)
    parser.add_argument("--budget", type=Path)
    parser.add_argument("--capture-manifest", type=Path,
                        help="paired scenario/environment and telemetry hashes")
    parser.add_argument("--json-output", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.warmup < 0 or args.min_samples < 1:
        raise SystemExit("warmup must be >= 0 and min-samples must be >= 1")
    try:
        if args.budget and not args.capture_manifest:
            raise ValueError("--budget requires --capture-manifest")
        capture_manifest = None
        if args.capture_manifest:
            capture_manifest = validate_capture_manifest(
                args.capture_manifest, args.vulkan, args.opengl
            )
        vk = summarize(parse_stats(args.vulkan, "VK_STATS"), args.warmup)
        gl = summarize(parse_stats(args.opengl, "GL_STATS"), args.warmup)
        failures: list[str] = []
        for name, summary in (("vulkan", vk), ("opengl", gl)):
            if int(summary["samples"]) < args.min_samples:
                failures.append(f"{name} has fewer than {args.min_samples} samples")
        result = {
            "schema_version": 1,
            "passed": not failures,
            "warmup": args.warmup,
            "vulkan": vk,
            "opengl": gl,
            "ratios_vulkan_over_opengl": ratios(vk, gl),
            "failures": failures,
        }
        if capture_manifest:
            result["capture_manifest"] = capture_manifest
        if args.budget:
            result["budget"] = str(args.budget)
            failures.extend(evaluate_budget(result, args.budget))
            result["passed"] = not failures
        if args.json_output:
            args.json_output.parent.mkdir(parents=True, exist_ok=True)
            args.json_output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if result["passed"] else 1
    except (OSError, ValueError) as exc:
        print(f"renderer perf analysis: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
