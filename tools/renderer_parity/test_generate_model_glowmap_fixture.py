#!/usr/bin/env python3
"""Regression checks for the deterministic model-skin glowmap fixture."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import generate_model_glowmap_fixture as fixture


class ModelGlowmapFixtureTests(unittest.TestCase):
    def test_authored_model_glowmap_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_uses_a_stock_glowmap_model_via_the_game_entity_path(self) -> None:
        entity_text = "".join(fixture.MODEL_ENTITY)
        self.assertIn('"classname" "misc_model"', entity_text)
        self.assertIn('"model" "models/objects/dmspot/tris.md2"', entity_text)
        self.assertIn('"origin" "256 -128 -22"', entity_text)
        self.assertIn('"scale" "3"', entity_text)

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )

    def test_manifest_locks_the_luminous_skin_region(self) -> None:
        manifest_path = (
            Path(__file__).resolve().parents[2]
            / "assets/renderer_parity/fr01_model_glowmap_manifest.json"
        )
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        scene = manifest["scenes"][0]
        self.assertEqual("stock_md2_skin_glowmap", scene["id"])
        self.assertEqual([570, 470, 310, 110], scene["crop"])
        self.assertEqual([1, 0.3, 0.3], scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0.8, scene["metrics"]["max_pixels_over_threshold_percent"])
        self.assertEqual(
            {
                "name": "stock_md2_skin_glow_emission",
                "min_color": [160, 0, 0],
                "max_color": [255, 60, 30],
                "min_pixels_per_backend": 1800,
                "max_backend_count_delta_percent": 4,
                "min_backend_intersection_over_union": 0.92,
            },
            scene["probes"][0],
        )

    def test_capture_config_keeps_glowmaps_enabled_at_fixed_intensity(self) -> None:
        config_path = (
            Path(__file__).resolve().parents[2]
            / "assets/renderer_parity/fr01_model_glowmap.cfg"
        )
        config = config_path.read_text(encoding="utf-8")
        self.assertIn("set r_fullbright 1", config)
        self.assertIn("set r_glowmaps 1", config)
        self.assertIn("set r_glowmap_intensity 1", config)
        self.assertIn("set gl_glowmap_intensity 1", config)
        self.assertIn("set gl_bloom 0", config)
        self.assertIn("set vk_bloom 0", config)


if __name__ == "__main__":
    unittest.main()
