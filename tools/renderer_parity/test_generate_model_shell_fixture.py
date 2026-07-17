#!/usr/bin/env python3
"""Regression checks for the deterministic translucent-shell fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import generate_model_shell_fixture as fixture


class ModelShellFixtureTests(unittest.TestCase):
    def test_authored_shell_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_uses_a_translucent_stock_md2_shell(self) -> None:
        entity_text = "".join(fixture.SHELL_ENTITY)
        for field in (
            '"classname" "misc_model"',
            '"model" "models/objects/dmspot/tris.md2"',
            '"origin" "256 -128 -22"',
            '"scale" "3"',
            '"alpha" "0.7"',
            '"renderFX" "4128"',
        ):
            self.assertIn(field, entity_text)

    def test_fixture_uses_a_non_emissive_background_for_all_bsp_faces(self) -> None:
        asset_root = Path("assets")
        expected = fixture.generated_outputs(asset_root)[asset_root / "maps" / fixture.MAP_NAME]
        baseline = fixture.build_bsp(
            extra_entities=fixture.SHELL_ENTITY,
            background_texture=fixture.BACKGROUND_TEXTURE,
            bmodel_texture=fixture.BACKGROUND_TEXTURE,
        )
        self.assertEqual(baseline, expected)

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
