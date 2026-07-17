#!/usr/bin/env python3
"""Regression checks for the deterministic player-rim bloom fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_model_rim_fixture as fixture


class ModelRimFixtureTests(unittest.TestCase):
    def test_authored_rim_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_has_only_a_non_emissive_player_start_scene(self) -> None:
        asset_root = Path("assets")
        expected = fixture.generated_outputs(asset_root)[asset_root / "maps" / fixture.MAP_NAME]
        self.assertEqual(
            expected,
            fixture.build_bsp(
                background_texture=fixture.BACKGROUND_TEXTURE,
                bmodel_texture=fixture.BACKGROUND_TEXTURE,
            ),
        )

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
