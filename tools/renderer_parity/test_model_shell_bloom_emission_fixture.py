#!/usr/bin/env python3
"""Regression contract for the translucent colour-shell bloom capture."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_model_shell_bloom_emission.cfg"
MANIFEST_PATH = ROOT / "assets/renderer_parity/fr01_model_shell_bloom_emission_manifest.json"


class ModelShellBloomEmissionFixtureTests(unittest.TestCase):
    def test_config_isolates_colour_shell_bloom_from_scene_thresholding(self) -> None:
        config = CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set r_fullbright 1",
            "set r_glowmaps 0",
            "set gl_bloom 1",
            "set vk_bloom 1",
            "set gl_bloom_threshold 100",
            "set vk_bloom_threshold 100",
            "map worr_fr01_model_shell",
        ):
            self.assertIn(line, config)

    def test_manifest_targets_the_colour_shell_capture(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T12", manifest["task_id"])
        scene = manifest["scenes"][0]
        self.assertEqual("stock_md2_colour_shell_bloom_emission", scene["id"])
        self.assertEqual("renderer_parity/fr01_model_shell_bloom_emission.cfg",
                         scene["config"])
        self.assertEqual("fr01_model_shell_bloom_emission.tga", scene["capture"])
        self.assertEqual([570, 450, 330, 180], scene["crop"])
        self.assertEqual([1.6, 2.7, 0.07],
                         scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0,
                         scene["metrics"]["max_pixels_over_threshold_percent"])
        probes = {probe["name"]: probe for probe in scene["probes"]}
        core = probes["translucent_blue_shell_bloom_core"]
        self.assertEqual([0, 0, 250], core["min_color"])
        self.assertEqual(52000, core["min_pixels_per_backend"])
        self.assertEqual(1, core["min_backend_intersection_over_union"])
        halo = probes["soft_shell_bloom_halo_on_blue_backdrop"]
        self.assertEqual([15, 25, 82], halo["min_color"])
        self.assertEqual([25, 40, 100], halo["max_color"])
        self.assertEqual(1750, halo["min_pixels_per_backend"])
        self.assertEqual(0.98, halo["min_backend_intersection_over_union"])


if __name__ == "__main__":
    unittest.main()
