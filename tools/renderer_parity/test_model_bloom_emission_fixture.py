#!/usr/bin/env python3
"""Regression contract for the opaque MD2 bloom-emission capture."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_model_bloom_emission.cfg"
MANIFEST_PATH = ROOT / "assets/renderer_parity/fr01_model_bloom_emission_manifest.json"


class ModelBloomEmissionFixtureTests(unittest.TestCase):
    def test_config_isolates_authored_skin_bloom_from_scene_thresholding(self) -> None:
        config = CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set r_fullbright 1",
            "set r_glowmaps 1",
            "set gl_bloom 1",
            "set vk_bloom 1",
            "set gl_bloom_threshold 100",
            "set vk_bloom_threshold 100",
            "set gl_bloom_intensity 1",
            "set vk_bloom_intensity 1",
            "map worr_fr01_model_glowmap",
        ):
            self.assertIn(line, config)

    def test_manifest_locks_skin_and_halo_masks(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T12", manifest["task_id"])
        scene = manifest["scenes"][0]
        self.assertEqual("stock_md2_skin_bloom_emission", scene["id"])
        self.assertEqual("renderer_parity/fr01_model_bloom_emission.cfg",
                         scene["config"])
        self.assertEqual("fr01_model_bloom_emission.tga", scene["capture"])
        self.assertEqual([570, 470, 310, 110], scene["crop"])
        self.assertEqual(
            {
                "pixel_threshold": 16,
                "max_mean_absolute_rgb": [1, 0.35, 0.3],
                "max_pixels_over_threshold_percent": 0.8,
            },
            scene["metrics"],
        )
        self.assertEqual("stock_md2_skin_glow_bloom", scene["probes"][0]["name"])
        self.assertEqual("skin_bloom_halo_on_blue_backdrop",
                         scene["probes"][1]["name"])
        self.assertEqual([29, 34, 80], scene["probes"][1]["color"])
        self.assertEqual(8, scene["probes"][1]["tolerance"])


if __name__ == "__main__":
    unittest.main()
