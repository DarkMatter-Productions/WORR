#!/usr/bin/env python3
"""Regression checks for the legacy-PCX sprite replacement fixture."""

from __future__ import annotations

import unittest
from pathlib import Path

try:
    from . import generate_sprite_replacement_fixture as fixture
except ImportError:
    import generate_sprite_replacement_fixture as fixture


ROOT = Path(__file__).resolve().parents[2]


class SpriteReplacementFixtureTests(unittest.TestCase):
    def test_authored_outputs_are_current(self) -> None:
        for path, data in fixture.generated_outputs(ROOT / "assets").items():
            self.assertEqual(path.read_bytes(), data, path)

    def test_legacy_source_and_png_override_are_visibly_distinct(self) -> None:
        self.assertNotEqual(fixture.SOURCE_PCX_RGB, fixture.REPLACEMENT_RGB)
        alphas = fixture.replacement_pixels()[3::4]
        self.assertIn(0, alphas)
        self.assertIn(255, alphas)

    def test_sprite_model_requests_the_legacy_pcx_name(self) -> None:
        self.assertIn(fixture.SPRITE_SOURCE.encode("ascii"), fixture.sprite_model())


if __name__ == "__main__":
    unittest.main()
