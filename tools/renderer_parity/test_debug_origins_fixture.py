#!/usr/bin/env python3
"""Regression checks for the paired entity-origin diagnostic fixture."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "assets/renderer_parity/fr01_model_origins.cfg"
MANIFEST = ROOT / "assets/renderer_parity/fr01_model_origins_manifest.json"
COMMON_CONFIG = ROOT / "assets/renderer_parity/fr01_bmodel_first_frame_common.cfg"


class DebugOriginsFixtureTests(unittest.TestCase):
    def test_config_enables_both_native_diagnostic_controls(self) -> None:
        config = CONFIG.read_text(encoding="utf-8")
        common = COMMON_CONFIG.read_text(encoding="utf-8")
        self.assertIn("exec renderer_parity/fr01_bmodel_first_frame_common.cfg", config)
        self.assertIn("set cheats 1", common)
        for line in (
            "set gl_showorigins 1",
            "set vk_showorigins 1",
            "map worr_fr01_model_glowmap",
            "screenshottga fr01_model_origins",
        ):
            self.assertIn(line, config)

    def test_manifest_locks_the_shared_rgb_axis_masks(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T15", manifest["task_id"])
        self.assertEqual(1, len(manifest["scenes"]))
        scene = manifest["scenes"][0]
        self.assertEqual("stock_md2_showorigins", scene["id"])
        self.assertEqual("renderer_parity/fr01_model_origins.cfg", scene["config"])
        self.assertEqual("fr01_model_origins.tga", scene["capture"])
        self.assertEqual([700, 320, 120, 130], scene["crop"])
        self.assertEqual(
            {
                "pixel_threshold": 1,
                "max_mean_absolute_rgb": [0.07, 0.07, 0.001],
                "max_pixels_over_threshold_percent": 0.03,
            },
            scene["metrics"],
        )
        probes = {probe["name"]: probe for probe in scene["probes"]}
        self.assertEqual([255, 0, 0], probes["showorigins_red_axis"]["color"])
        self.assertEqual(50, probes["showorigins_red_axis"]["min_pixels_per_backend"])
        self.assertEqual(0.92, probes["showorigins_red_axis"]["min_backend_intersection_over_union"])
        self.assertEqual([0, 255, 0], probes["showorigins_green_axis"]["color"])
        self.assertEqual(80, probes["showorigins_green_axis"]["min_pixels_per_backend"])
        self.assertEqual(0.95, probes["showorigins_green_axis"]["min_backend_intersection_over_union"])
        self.assertEqual([0, 0, 255], probes["showorigins_blue_axis"]["color"])
        self.assertEqual(90, probes["showorigins_blue_axis"]["min_pixels_per_backend"])
        self.assertEqual(1, probes["showorigins_blue_axis"]["min_backend_intersection_over_union"])


if __name__ == "__main__":
    unittest.main()
