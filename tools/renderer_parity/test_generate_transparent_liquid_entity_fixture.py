#!/usr/bin/env python3
"""Regression checks for the liquid/entity transparent-ordering fixture."""

from __future__ import annotations

import sys
import unittest
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "renderer_parity"))

import generate_transparent_liquid_entity_fixture as fixture


class TransparentLiquidEntityFixtureTests(unittest.TestCase):
    def test_authored_map_is_current(self) -> None:
        expected = fixture.generated_outputs(ROOT / "assets")
        for path, data in expected.items():
            self.assertTrue(path.is_file(), path)
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_uses_native_liquid_and_opposite_entity_depths(self) -> None:
        entity_text = "".join(fixture.TRANSPARENT_SPRITE_ENTITIES)
        self.assertEqual(2, entity_text.count('"classname" "misc_model"'))
        self.assertEqual(2, entity_text.count('"model" "sprites/s_bfg1.sp2"'))
        self.assertIn('"origin" "480 -128 -22"', entity_text)
        self.assertIn('"origin" "160 128 -22"', entity_text)
        self.assertIn('"alpha" "0.75"', entity_text)
        self.assertIn('"alpha" "0.25"', entity_text)
        self.assertIn('"renderFX" "32"', entity_text)
        self.assertEqual(
            fixture.SURF_WARP | fixture.SURF_TRANS33 | fixture.SURF_FLOWING,
            fixture.TRANSPARENT_LIQUID_FLAGS,
        )

    def test_refractive_config_activates_after_the_transparent_map_loads(self) -> None:
        config = (ROOT / "assets" / "renderer_parity" /
                  "fr01_liquid_entity_ordering.cfg").read_text(encoding="utf-8")
        map_index = config.index("map worr_fr01_liquid_entity_ordering")
        self.assertIn("set gl_warp_refraction 0\n", config[:map_index])
        self.assertIn("set vk_warp_refraction 0\n", config[:map_index])
        activation = (
            "set gl_warp_refraction 0.1\n"
            "set vk_warp_refraction 0.1\n"
            "wait 10\n"
        )
        self.assertGreater(config.index(activation), map_index)
        self.assertIn("set gl_draworder 0.5", config)
        self.assertIn("set vk_draworder 0.5", config)

    def test_manifest_separates_live_refraction_from_the_tight_ordering_control(self) -> None:
        manifest = json.loads((ROOT / "assets" / "renderer_parity" /
                               "fr01_liquid_entity_ordering_manifest.json").read_text(
                                   encoding="utf-8"))
        self.assertEqual("FR-01-T10", manifest["task_id"])
        live, control = manifest["scenes"]
        self.assertEqual("transparent_entities_straddle_refractive_liquid", live["id"])
        self.assertEqual(12, live["metrics"]["pixel_threshold"])
        self.assertEqual([1.2, 1.0, 1.0], live["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(1.5, live["metrics"]["max_pixels_over_threshold_percent"])
        self.assertEqual("transparent_entities_liquid_unrefracted_control", control["id"])
        self.assertEqual(1, control["metrics"]["pixel_threshold"])
        self.assertEqual([0.5, 0.2, 0.5], control["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0.0, control["metrics"]["max_pixels_over_threshold_percent"])


if __name__ == "__main__":
    unittest.main()
