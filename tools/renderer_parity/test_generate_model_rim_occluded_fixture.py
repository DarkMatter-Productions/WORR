#!/usr/bin/env python3
"""Regression checks for the deterministic occluded player-rim fixture."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import generate_model_rim_occluded_fixture as fixture


class ModelRimOccludedFixtureTests(unittest.TestCase):
    def test_authored_occluded_rim_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_occluder_sits_between_camera_and_player(self) -> None:
        self.assertEqual((-269.0, 144.0, 0.0), fixture.OCCLUDER_ORIGIN)
        asset_root = Path("assets")
        expected = fixture.generated_outputs(asset_root)[
            asset_root / "maps" / fixture.MAP_NAME
        ]
        self.assertGreater(len(expected), 0)

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )

    def test_manifest_requires_no_visible_green_rim_or_halo(self) -> None:
        root = Path(__file__).resolve().parents[2]
        config = (root / "assets" / "renderer_parity" /
                  "fr01_model_rim_occluded_bloom_emission.cfg").read_text(
                      encoding="utf-8")
        manifest = json.loads((root / "assets" / "renderer_parity" /
                               "fr01_model_rim_occluded_bloom_emission_manifest.json")
                              .read_text(encoding="utf-8"))
        self.assertIn("map worr_fr01_model_rim_occluded", config)
        self.assertIn("set cl_thirdperson 1", config)
        self.assertIn("set cl_player_rimlight_team 1", config)
        scene = manifest["scenes"][0]
        self.assertEqual([0, 0, 0], scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])
        probe = scene["probes"][0]
        self.assertEqual(0, probe["min_pixels_per_backend"])
        self.assertEqual(0, probe["max_pixels_per_backend"])


if __name__ == "__main__":
    unittest.main()
