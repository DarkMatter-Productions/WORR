#!/usr/bin/env python3

from __future__ import annotations

import json
import struct
import tempfile
import unittest
from pathlib import Path

import compare_captures


def write_tga(path: Path, width: int, height: int, pixels: list[tuple[int, int, int]]) -> None:
    if len(pixels) != width * height:
        raise ValueError("pixel count mismatch")
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,
        0,
        2,
        0,
        0,
        0,
        0,
        0,
        width,
        height,
        24,
        0x20,
    )
    payload = bytearray()
    for red, green, blue in pixels:
        payload.extend((blue, green, red))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + payload)


class CompareCapturesTests(unittest.TestCase):
    def make_manifest(self, root: Path, max_mean: float = 3.0) -> Path:
        manifest = {
            "schema_version": 1,
            "task_id": "FR-01-T04",
            "scenes": [
                {
                    "id": "fixture",
                    "capture": "fixture.tga",
                    "crop": [0, 0, 2, 2],
                    "metrics": {
                        "pixel_threshold": 8,
                        "max_mean_absolute_rgb": [max_mean, max_mean, max_mean],
                        "max_pixels_over_threshold_percent": 30,
                    },
                    "probes": [
                        {
                            "name": "green_outline",
                            "color": [0, 255, 0],
                            "tolerance": 0,
                            "min_pixels_per_backend": 1,
                            "max_backend_count_delta_percent": 0,
                            "min_backend_intersection_over_union": 1,
                        },
                        {
                            "name": "dark_pixels",
                            "min_color": [15, 25, 35],
                            "max_color": [55, 65, 75],
                            "min_pixels_per_backend": 2,
                            "max_backend_count_delta_percent": 0,
                            "min_backend_intersection_over_union": 1,
                        }
                    ],
                }
            ],
        }
        path = root / "manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        return path

    def test_manifest_passes_with_bounded_difference_and_probe(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_root = root / "captures"
            gl_pixels = [(0, 255, 0), (20, 30, 40), (50, 60, 70), (80, 90, 100)]
            vk_pixels = [(0, 255, 0), (22, 31, 39), (50, 60, 70), (80, 90, 100)]
            write_tga(capture_root / "opengl" / "fixture.tga", 2, 2, gl_pixels)
            write_tga(capture_root / "vulkan" / "fixture.tga", 2, 2, vk_pixels)

            report = compare_captures.evaluate_manifest(
                self.make_manifest(root), capture_root
            )

            self.assertTrue(report["passed"])
            self.assertEqual(report["failures"], [])
            self.assertEqual(report["scenes"][0]["probes"][0]["vulkan_pixels"], 1)
            self.assertEqual(
                report["scenes"][0]["probes"][0]["intersection_over_union"],
                1.0,
            )
            self.assertEqual(report["scenes"][0]["probes"][1]["vulkan_pixels"], 2)
            self.assertEqual(
                report["scenes"][0]["probes"][1]["min_color"], [15.0, 25.0, 35.0]
            )

    def test_manifest_reports_metric_and_probe_failures(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_root = root / "captures"
            gl_pixels = [(0, 255, 0)] * 4
            vk_pixels = [(255, 0, 0)] * 4
            write_tga(capture_root / "opengl" / "fixture.tga", 2, 2, gl_pixels)
            write_tga(capture_root / "vulkan" / "fixture.tga", 2, 2, vk_pixels)

            report = compare_captures.evaluate_manifest(
                self.make_manifest(root, max_mean=1.0), capture_root
            )

            self.assertFalse(report["passed"])
            self.assertTrue(any("mean absolute R" in item for item in report["failures"]))
            self.assertTrue(any("green_outline" in item for item in report["failures"]))
            self.assertTrue(any("intersection-over-union" in item for item in report["failures"]))

    def test_manifest_can_require_probe_absence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_root = root / "captures"
            manifest_path = self.make_manifest(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            absence_probe = manifest["scenes"][0]["probes"][0]
            absence_probe["min_pixels_per_backend"] = 0
            absence_probe["max_pixels_per_backend"] = 0
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            pixels = [(0, 255, 0), (20, 30, 40), (50, 60, 70), (80, 90, 100)]
            write_tga(capture_root / "opengl" / "fixture.tga", 2, 2, pixels)
            write_tga(capture_root / "vulkan" / "fixture.tga", 2, 2, pixels)

            report = compare_captures.evaluate_manifest(manifest_path, capture_root)

            self.assertFalse(report["passed"])
            self.assertTrue(any("required at most 0" in item for item in report["failures"]))

    def test_loader_normalizes_bottom_origin_tga(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bottom.tga"
            header = struct.pack(
                "<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 1, 2, 24, 0
            )
            # File order is bottom row then top row, encoded BGR.
            path.write_bytes(header + bytes((255, 0, 0, 0, 0, 255)))
            image = compare_captures.load_tga(path)
            self.assertEqual(image.rgb, bytes((255, 0, 0, 0, 0, 255)))

    def test_manifest_scene_filter_does_not_require_unselected_capture(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            capture_root = root / "captures"
            manifest_path = self.make_manifest(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            unselected = dict(manifest["scenes"][0])
            unselected["id"] = "unselected"
            unselected["capture"] = "missing.tga"
            manifest["scenes"].append(unselected)
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            pixels = [(0, 255, 0), (20, 30, 40), (50, 60, 70), (80, 90, 100)]
            write_tga(capture_root / "opengl" / "fixture.tga", 2, 2, pixels)
            write_tga(capture_root / "vulkan" / "fixture.tga", 2, 2, pixels)

            report = compare_captures.evaluate_manifest(
                manifest_path, capture_root, {"fixture"}
            )

            self.assertTrue(report["passed"])
            self.assertEqual([scene["id"] for scene in report["scenes"]], ["fixture"])


if __name__ == "__main__":
    unittest.main()
