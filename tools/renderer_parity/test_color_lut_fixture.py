#!/usr/bin/env python3
"""Regression checks for the deterministic horizontal LUT parity scene."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "assets" / "renderer_parity" / "fr01_color_lut.cfg"
MANIFEST = ROOT / "assets" / "renderer_parity" / "fr01_color_lut_manifest.json"


class ColorLutFixtureTests(unittest.TestCase):
    def test_config_enables_only_the_matching_horizontal_lut_path(self) -> None:
        config = CONFIG.read_text(encoding="utf-8")
        for line in (
            "set gl_bloom 0",
            "set vk_bloom 0",
            "set r_dof 0",
            "set r_crtmode 0",
            "set gl_color_correction 0",
            "set vk_color_correction 0",
            "set gl_color_split_strength 0",
            "set vk_color_split_strength 0",
            "set gl_color_lut renderer_parity/fr01_color_lut_4x16.tga",
            "set vk_color_lut renderer_parity/fr01_color_lut_4x16.tga",
            "set gl_color_lut_intensity 1",
            "set vk_color_lut_intensity 1",
        ):
            self.assertIn(line, config)

    def test_manifest_targets_the_lut_receiver(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual("FR-01-T12", manifest["task_id"])
        scene = manifest["scenes"][0]
        self.assertEqual("horizontal_lut_texel_coordinate_fixed_view", scene["id"])
        self.assertEqual("renderer_parity/fr01_color_lut.cfg", scene["config"])
        self.assertEqual("fr01_color_lut.tga", scene["capture"])
        self.assertEqual([100, 100, 250, 200], scene["crop"])
        self.assertEqual(
            {
                "name": "horizontal_lut_non_identity_output",
                "color": [24, 40, 72],
                "tolerance": 0,
                "min_pixels_per_backend": 50000,
                "max_backend_count_delta_percent": 0,
                "min_backend_intersection_over_union": 1,
            },
            scene["probes"][0],
        )

        vertical = manifest["scenes"][1]
        self.assertEqual("vertical_lut_texel_coordinate_fixed_view", vertical["id"])
        self.assertEqual("renderer_parity/fr01_color_lut_vertical.cfg", vertical["config"])
        self.assertEqual("fr01_color_lut_vertical.tga", vertical["capture"])
        self.assertEqual("vertical_lut_non_identity_output", vertical["probes"][0]["name"])


if __name__ == "__main__":
    unittest.main()
