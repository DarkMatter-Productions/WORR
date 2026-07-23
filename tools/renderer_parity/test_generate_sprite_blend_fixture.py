#!/usr/bin/env python3
"""Regression checks for truecolour-alpha and entity-alpha sprite fixtures."""

from __future__ import annotations

import unittest
from pathlib import Path

try:
    from . import generate_sprite_blend_fixture as fixture
except ImportError:
    import generate_sprite_blend_fixture as fixture


ROOT = Path(__file__).resolve().parents[2]


class SpriteBlendFixtureTests(unittest.TestCase):
    def test_authored_outputs_are_current(self) -> None:
        for path, data in fixture.generated_outputs(ROOT / "assets").items():
            self.assertEqual(path.read_bytes(), data, path)

    def test_truecolour_sprite_has_transparent_half_alpha_and_opaque_texels(self) -> None:
        alphas = fixture.truecolour_sprite_pixels()[3::4]
        self.assertIn(0, alphas)
        self.assertIn(128, alphas)
        self.assertIn(255, alphas)

    def test_entity_alpha_uses_the_standard_renderfx_bit(self) -> None:
        entities = "".join(fixture.SPRITE_ENTITIES)
        self.assertIn('"alpha" "0.5"', entities)
        self.assertIn('"renderFX" "32"', entities)


if __name__ == "__main__":
    unittest.main()
