#!/usr/bin/env python3
"""Regression checks for the fogged inline-BSP fast-light fixture."""

from __future__ import annotations

import unittest
from pathlib import Path

import generate_bmodel_lightmapped_instance_fog_fixture as fixture


class FoggedLightmappedBmodelInstanceFixtureTests(unittest.TestCase):
    def test_authored_fogged_instance_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_combines_global_fog_and_dense_inline_bsp_instances(self) -> None:
        worldspawn = "".join(fixture.FOG_WORLDSPAWN_PROPERTIES)
        self.assertIn('"fog_density" "0.50"', worldspawn)
        self.assertIn('"fog_sky_factor" "0.60"', worldspawn)
        self.assertEqual(36, len(fixture.INSTANCE_ENTITIES))


if __name__ == "__main__":
    unittest.main()
