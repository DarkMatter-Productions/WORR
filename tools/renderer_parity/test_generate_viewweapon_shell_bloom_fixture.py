#!/usr/bin/env python3
"""Regression checks for the deterministic view-weapon shell bloom fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_viewweapon_shell_bloom_fixture as fixture


class ViewWeaponShellBloomFixtureTests(unittest.TestCase):
    def test_authored_viewweapon_shell_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_enables_an_instant_quad_at_player_spawn(self) -> None:
        self.assertEqual(
            ('"instantItems" "1"\n', '"start_items" "item_quad 30"\n'),
            fixture.WORLDSPAWN_PROPERTIES,
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
