#!/usr/bin/env python3
"""Compare deterministic OpenGL and Vulkan TGA captures against a manifest."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


TGA_HEADER_SIZE = 18


@dataclass(frozen=True)
class TgaImage:
    width: int
    height: int
    rgb: bytes


class CaptureError(ValueError):
    pass


def load_tga(path: Path) -> TgaImage:
    data = path.read_bytes()
    if len(data) < TGA_HEADER_SIZE:
        raise CaptureError(f"{path}: truncated TGA header")

    (
        id_length,
        color_map_type,
        image_type,
        _color_map_first,
        _color_map_length,
        _color_map_depth,
        _x_origin,
        _y_origin,
        width,
        height,
        pixel_depth,
        descriptor,
    ) = struct.unpack_from("<BBBHHBHHHHBB", data, 0)

    if color_map_type != 0 or image_type != 2:
        raise CaptureError(f"{path}: only uncompressed true-color TGA is supported")
    if width <= 0 or height <= 0 or pixel_depth not in (24, 32):
        raise CaptureError(f"{path}: invalid TGA dimensions or pixel depth")

    bytes_per_pixel = pixel_depth // 8
    pixel_offset = TGA_HEADER_SIZE + id_length
    expected = pixel_offset + width * height * bytes_per_pixel
    if expected > len(data):
        raise CaptureError(f"{path}: truncated TGA pixel payload")

    top_origin = (descriptor & 0x20) != 0
    right_origin = (descriptor & 0x10) != 0
    rgb = bytearray(width * height * 3)
    for file_y in range(height):
        output_y = file_y if top_origin else height - 1 - file_y
        for file_x in range(width):
            output_x = width - 1 - file_x if right_origin else file_x
            src = pixel_offset + (file_y * width + file_x) * bytes_per_pixel
            dst = (output_y * width + output_x) * 3
            rgb[dst + 0] = data[src + 2]
            rgb[dst + 1] = data[src + 1]
            rgb[dst + 2] = data[src + 0]
    return TgaImage(width=width, height=height, rgb=bytes(rgb))


def _number(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CaptureError(f"{label} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise CaptureError(f"{label} must be finite")
    return result


def _triplet(value: Any, label: str) -> tuple[float, float, float]:
    if not isinstance(value, list) or len(value) != 3:
        raise CaptureError(f"{label} must contain three values")
    return tuple(_number(item, f"{label}[{index}]") for index, item in enumerate(value))  # type: ignore[return-value]


def _crop(scene: dict[str, Any], image: TgaImage) -> tuple[int, int, int, int]:
    raw = scene.get("crop", [0, 0, image.width, image.height])
    if not isinstance(raw, list) or len(raw) != 4 or any(
        isinstance(item, bool) or not isinstance(item, int) for item in raw
    ):
        raise CaptureError("scene crop must contain four integers")
    x, y, width, height = raw
    if x < 0 or y < 0 or width <= 0 or height <= 0:
        raise CaptureError("scene crop must have a non-negative origin and positive size")
    if x + width > image.width or y + height > image.height:
        raise CaptureError("scene crop exceeds capture bounds")
    return x, y, width, height


def _pixel_offsets(image: TgaImage, crop: tuple[int, int, int, int]) -> Iterable[int]:
    x, y, width, height = crop
    for py in range(y, y + height):
        row = (py * image.width + x) * 3
        for px in range(width):
            yield row + px * 3


def compare_images(
    reference: TgaImage,
    candidate: TgaImage,
    crop: tuple[int, int, int, int],
    pixel_threshold: float,
) -> dict[str, Any]:
    if (reference.width, reference.height) != (candidate.width, candidate.height):
        raise CaptureError(
            "capture dimensions differ: "
            f"{reference.width}x{reference.height} vs "
            f"{candidate.width}x{candidate.height}"
        )

    sums = [0, 0, 0]
    squared = [0, 0, 0]
    maximum = [0, 0, 0]
    over_threshold = 0
    count = 0
    for offset in _pixel_offsets(reference, crop):
        channel_error = [
            abs(reference.rgb[offset + channel] - candidate.rgb[offset + channel])
            for channel in range(3)
        ]
        for channel, error in enumerate(channel_error):
            sums[channel] += error
            squared[channel] += error * error
            maximum[channel] = max(maximum[channel], error)
        if max(channel_error) > pixel_threshold:
            over_threshold += 1
        count += 1

    return {
        "pixels": count,
        "mean_absolute_rgb": [value / count for value in sums],
        "root_mean_square_rgb": [math.sqrt(value / count) for value in squared],
        "maximum_rgb": maximum,
        "pixel_threshold": pixel_threshold,
        "pixels_over_threshold": over_threshold,
        "pixels_over_threshold_percent": over_threshold * 100.0 / count,
    }


def compare_probe_masks(
    reference: TgaImage,
    candidate: TgaImage,
    crop: tuple[int, int, int, int],
    min_color: tuple[float, float, float],
    max_color: tuple[float, float, float],
) -> dict[str, int | float]:
    reference_count = 0
    candidate_count = 0
    intersection = 0
    union = 0
    for offset in _pixel_offsets(reference, crop):
        reference_matches = all(
            min_color[channel] <= reference.rgb[offset + channel] <= max_color[channel]
            for channel in range(3)
        )
        candidate_matches = all(
            min_color[channel] <= candidate.rgb[offset + channel] <= max_color[channel]
            for channel in range(3)
        )
        reference_count += int(reference_matches)
        candidate_count += int(candidate_matches)
        intersection += int(reference_matches and candidate_matches)
        union += int(reference_matches or candidate_matches)
    return {
        "reference_pixels": reference_count,
        "candidate_pixels": candidate_count,
        "intersection_pixels": intersection,
        "union_pixels": union,
        "intersection_over_union": intersection / union if union else 1.0,
    }


def evaluate_scene(
    scene: dict[str, Any], capture_root: Path
) -> tuple[dict[str, Any], list[str]]:
    scene_id = scene.get("id")
    capture = scene.get("capture")
    if not isinstance(scene_id, str) or not scene_id:
        raise CaptureError("scene id must be a non-empty string")
    if not isinstance(capture, str) or not capture or Path(capture).name != capture:
        raise CaptureError(f"{scene_id}: capture must be a plain filename")

    opengl_path = capture_root / "opengl" / capture
    vulkan_path = capture_root / "vulkan" / capture
    if not opengl_path.is_file():
        raise CaptureError(f"{scene_id}: missing OpenGL capture {opengl_path}")
    if not vulkan_path.is_file():
        raise CaptureError(f"{scene_id}: missing Vulkan capture {vulkan_path}")

    opengl = load_tga(opengl_path)
    vulkan = load_tga(vulkan_path)
    crop = _crop(scene, opengl)
    metrics_config = scene.get("metrics", {})
    if not isinstance(metrics_config, dict):
        raise CaptureError(f"{scene_id}: metrics must be an object")
    pixel_threshold = _number(metrics_config.get("pixel_threshold", 8), "pixel_threshold")
    metrics = compare_images(opengl, vulkan, crop, pixel_threshold)

    failures: list[str] = []
    max_mean = _triplet(
        metrics_config.get("max_mean_absolute_rgb", [255, 255, 255]),
        "max_mean_absolute_rgb",
    )
    for channel, label in enumerate("RGB"):
        if metrics["mean_absolute_rgb"][channel] > max_mean[channel]:
            failures.append(
                f"{scene_id}: mean absolute {label} error "
                f"{metrics['mean_absolute_rgb'][channel]:.5f} exceeds "
                f"{max_mean[channel]:.5f}"
            )
    max_over = _number(
        metrics_config.get("max_pixels_over_threshold_percent", 100),
        "max_pixels_over_threshold_percent",
    )
    if metrics["pixels_over_threshold_percent"] > max_over:
        failures.append(
            f"{scene_id}: {metrics['pixels_over_threshold_percent']:.5f}% of pixels "
            f"exceed error {pixel_threshold:g}; limit is {max_over:.5f}%"
        )

    probe_reports: list[dict[str, Any]] = []
    probes = scene.get("probes", [])
    if not isinstance(probes, list):
        raise CaptureError(f"{scene_id}: probes must be an array")
    for probe in probes:
        if not isinstance(probe, dict):
            raise CaptureError(f"{scene_id}: probe must be an object")
        name = probe.get("name")
        if not isinstance(name, str) or not name:
            raise CaptureError(f"{scene_id}: probe name must be non-empty")
        uses_color = "color" in probe
        uses_range = "min_color" in probe or "max_color" in probe
        if uses_color == uses_range:
            raise CaptureError(
                f"{scene_id}.{name}: specify either color/tolerance or "
                "min_color/max_color"
            )
        color: tuple[float, float, float] | None = None
        tolerance: float | None = None
        if uses_color:
            color = _triplet(probe.get("color"), f"{scene_id}.{name}.color")
            tolerance = _number(
                probe.get("tolerance", 0), f"{scene_id}.{name}.tolerance"
            )
            if tolerance < 0:
                raise CaptureError(f"{scene_id}.{name}.tolerance must be non-negative")
            min_color = tuple(channel - tolerance for channel in color)
            max_color = tuple(channel + tolerance for channel in color)
        else:
            min_color = _triplet(
                probe.get("min_color"), f"{scene_id}.{name}.min_color"
            )
            max_color = _triplet(
                probe.get("max_color"), f"{scene_id}.{name}.max_color"
            )
            if any(low > high for low, high in zip(min_color, max_color)):
                raise CaptureError(
                    f"{scene_id}.{name}: min_color must not exceed max_color"
                )
        minimum = int(_number(probe.get("min_pixels_per_backend", 0), f"{scene_id}.{name}.min_pixels_per_backend"))
        maximum: int | None = None
        if "max_pixels_per_backend" in probe:
            maximum = int(_number(
                probe["max_pixels_per_backend"],
                f"{scene_id}.{name}.max_pixels_per_backend",
            ))
            if maximum < 0:
                raise CaptureError(
                    f"{scene_id}.{name}.max_pixels_per_backend must be non-negative"
                )
            if minimum > maximum:
                raise CaptureError(
                    f"{scene_id}.{name}: min_pixels_per_backend must not exceed "
                    "max_pixels_per_backend"
                )
        max_delta = _number(
            probe.get("max_backend_count_delta_percent", 100),
            f"{scene_id}.{name}.max_backend_count_delta_percent",
        )
        min_iou = _number(
            probe.get("min_backend_intersection_over_union", 0),
            f"{scene_id}.{name}.min_backend_intersection_over_union",
        )
        if min_iou < 0 or min_iou > 1:
            raise CaptureError(
                f"{scene_id}.{name}.min_backend_intersection_over_union "
                "must be between 0 and 1"
            )
        mask = compare_probe_masks(opengl, vulkan, crop, min_color, max_color)
        gl_count = int(mask["reference_pixels"])
        vk_count = int(mask["candidate_pixels"])
        denominator = max(gl_count, vk_count, 1)
        delta_percent = abs(gl_count - vk_count) * 100.0 / denominator
        probe_report = {
            "name": name,
            "opengl_pixels": gl_count,
            "vulkan_pixels": vk_count,
            "backend_count_delta_percent": delta_percent,
            "intersection_pixels": mask["intersection_pixels"],
            "union_pixels": mask["union_pixels"],
            "intersection_over_union": mask["intersection_over_union"],
        }
        if color is not None:
            probe_report.update({"color": list(color), "tolerance": tolerance})
        else:
            probe_report.update(
                {"min_color": list(min_color), "max_color": list(max_color)}
            )
        probe_reports.append(probe_report)
        if gl_count < minimum or vk_count < minimum:
            failures.append(
                f"{scene_id}.{name}: required at least {minimum} matching pixels "
                f"per backend; OpenGL={gl_count}, Vulkan={vk_count}"
            )
        if maximum is not None and (gl_count > maximum or vk_count > maximum):
            failures.append(
                f"{scene_id}.{name}: required at most {maximum} matching pixels "
                f"per backend; OpenGL={gl_count}, Vulkan={vk_count}"
            )
        if delta_percent > max_delta:
            failures.append(
                f"{scene_id}.{name}: backend pixel-count delta "
                f"{delta_percent:.5f}% exceeds {max_delta:.5f}%"
            )
        if mask["intersection_over_union"] < min_iou:
            failures.append(
                f"{scene_id}.{name}: backend mask intersection-over-union "
                f"{mask['intersection_over_union']:.5f} is below {min_iou:.5f}"
            )

    report = {
        "id": scene_id,
        "capture": capture,
        "dimensions": [opengl.width, opengl.height],
        "crop": list(crop),
        "metrics": metrics,
        "probes": probe_reports,
        "passed": not failures,
    }
    return report, failures


def evaluate_manifest(
    manifest_path: Path,
    capture_root: Path,
    scene_ids: set[str] | None = None,
) -> dict[str, Any]:
    raw = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("schema_version") != 1:
        raise CaptureError("manifest schema_version must be 1")
    scenes = raw.get("scenes")
    if not isinstance(scenes, list) or not scenes:
        raise CaptureError("manifest scenes must be a non-empty array")

    reports: list[dict[str, Any]] = []
    failures: list[str] = []
    requested_scene_ids = set(scene_ids or ())
    found_scene_ids: set[str] = set()
    for scene in scenes:
        if not isinstance(scene, dict):
            raise CaptureError("manifest scene must be an object")
        scene_id = scene.get("id")
        if not isinstance(scene_id, str) or not scene_id:
            raise CaptureError("manifest scene id must be non-empty")
        if requested_scene_ids and scene_id not in requested_scene_ids:
            continue
        found_scene_ids.add(scene_id)
        report, scene_failures = evaluate_scene(scene, capture_root)
        reports.append(report)
        failures.extend(scene_failures)
    unknown_scene_ids = requested_scene_ids - found_scene_ids
    if unknown_scene_ids:
        raise CaptureError(
            "unknown scene id(s): " + ", ".join(sorted(unknown_scene_ids))
        )
    return {
        "schema_version": 1,
        "task_id": raw.get("task_id"),
        "manifest": str(manifest_path),
        "capture_root": str(capture_root),
        "scenes": reports,
        "failures": failures,
        "passed": not failures,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--capture-root", type=Path, required=True)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument(
        "--scene",
        action="append",
        help="evaluate only this manifest scene id (repeatable; defaults to all)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = evaluate_manifest(
            args.manifest.resolve(),
            args.capture_root.resolve(),
            set(args.scene or ()),
        )
    except (CaptureError, OSError, json.JSONDecodeError) as exc:
        print(f"renderer parity comparison error: {exc}", file=sys.stderr)
        return 2

    serialized = json.dumps(report, indent=2, sort_keys=True)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(serialized + "\n", encoding="utf-8")
    print(serialized)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
