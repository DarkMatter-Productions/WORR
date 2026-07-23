#!/usr/bin/env python3
"""Regression checks for the opaque-sprite depth-write fixture."""

from __future__ import annotations

import unittest
from pathlib import Path

try:
    from . import generate_sprite_depth_write_fixture as fixture
except ImportError:
    import generate_sprite_depth_write_fixture as fixture


ROOT = Path(__file__).resolve().parents[2]


class SpriteDepthWriteFixtureTests(unittest.TestCase):
    def test_authored_outputs_are_current(self) -> None:
        for path, data in fixture.generated_outputs(ROOT / "assets").items():
            self.assertEqual(path.read_bytes(), data, path)

    def test_opaque_and_alpha_sources_are_distinct(self) -> None:
        self.assertNotEqual(fixture.OPAQUE_RGB, fixture.ALPHA_RGB)
        self.assertNotEqual(fixture.OPAQUE_RGB, fixture.BACKDROP_RGB)
        self.assertNotEqual(fixture.ALPHA_RGB, fixture.BACKDROP_RGB)

    def test_fixture_keeps_the_opaque_sprite_in_front_of_later_alpha(self) -> None:
        self.assertIn('"origin" "256 0 -22"', fixture.SPRITE_ENTITIES[0])
        self.assertIn('"origin" "384 0 -22"', fixture.SPRITE_ENTITIES[1])
        self.assertIn('"alpha" "0.5"', fixture.SPRITE_ENTITIES[1])
        self.assertIn('"renderFX" "32"', fixture.SPRITE_ENTITIES[1])


if __name__ == "__main__":
    unittest.main()
