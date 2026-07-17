#!/usr/bin/env python3
"""Regression checks for the native Vulkan authored-bloom fixture."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_bloom_emission.cfg"
MANIFEST_PATH = ROOT / "assets/renderer_parity/fr01_bloom_emission_manifest.json"


class BloomEmissionFixtureTests(unittest.TestCase):
    def test_config_enables_matched_glowmap_bloom_controls(self) -> None:
        config = CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set r_glowmaps 1",
            "set gl_bloom 1",
            "set vk_bloom 1",
            "set gl_bloom_threshold 0",
            "set vk_bloom_threshold 0",
            "set gl_bloom_knee 0",
            "set vk_bloom_knee 0",
            "set gl_bloom_intensity 1",
            "set vk_bloom_intensity 1",
        ):
            self.assertIn(line, config)

    def test_manifest_targets_the_authored_emission_crop(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T12", manifest["task_id"])
        scene = manifest["scenes"][0]
        self.assertEqual("wall_glowmap_authored_bloom_emission", scene["id"])
        self.assertEqual("renderer_parity/fr01_bloom_emission.cfg", scene["config"])
        self.assertEqual("fr01_bloom_emission.tga", scene["capture"])
        self.assertEqual([100, 100, 250, 200], scene["crop"])
        self.assertEqual(
            {
                "pixel_threshold": 0,
                "max_mean_absolute_rgb": [0, 0, 0],
                "max_pixels_over_threshold_percent": 0,
            },
            scene["metrics"],
        )
        self.assertEqual(
            {
                "name": "authored_wall_glow_bloom_emission",
                "color": [120, 200, 255],
                "tolerance": 0,
                "min_pixels_per_backend": 50000,
                "max_backend_count_delta_percent": 0,
                "min_backend_intersection_over_union": 1,
            },
            scene["probes"][0],
        )


if __name__ == "__main__":
    unittest.main()
