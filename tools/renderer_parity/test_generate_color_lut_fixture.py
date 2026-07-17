#!/usr/bin/env python3
"""Regression checks for the deterministic native LUT test asset."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSET = ROOT / "assets" / "renderer_parity" / "fr01_color_lut_4x16.tga"
VERTICAL_ASSET = (
    ROOT / "assets" / "renderer_parity" / "fr01_color_lut_4x16_vertical.tga"
)
GENERATOR = ROOT / "tools" / "renderer_parity" / "generate_color_lut_fixture.py"
SPEC = importlib.util.spec_from_file_location("color_lut_fixture_generator", GENERATOR)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
build_tga = MODULE.build_tga


class ColorLutFixtureGeneratorTests(unittest.TestCase):
    def test_generated_tga_has_the_expected_horizontal_lut_layout(self) -> None:
        data = build_tga()
        self.assertEqual(18 + 16 * 4 * 3, len(data))
        self.assertEqual(2, data[2])
        self.assertEqual(16, int.from_bytes(data[12:14], "little"))
        self.assertEqual(4, int.from_bytes(data[14:16], "little"))
        self.assertEqual(24, data[16])
        self.assertEqual(0x20, data[17])

        # Pixel (red=3, green=2, blue=1) is BGR in the 4x16 strip.
        offset = 18 + ((2 * 16 + 1 * 4 + 3) * 3)
        self.assertEqual(bytes((170, 170, 0)), data[offset:offset + 3])

    def test_generated_tga_has_the_expected_vertical_lut_layout(self) -> None:
        data = build_tga(vertical=True)
        self.assertEqual(18 + 4 * 16 * 3, len(data))
        self.assertEqual(4, int.from_bytes(data[12:14], "little"))
        self.assertEqual(16, int.from_bytes(data[14:16], "little"))

        # Pixel (red=3, green=2, blue=1) is BGR in the 4x16 strip.
        offset = 18 + (((1 * 4 + 2) * 4 + 3) * 3)
        self.assertEqual(bytes((170, 170, 0)), data[offset:offset + 3])

    def test_checked_in_asset_matches_the_generator(self) -> None:
        self.assertEqual(build_tga(), ASSET.read_bytes())
        self.assertEqual(build_tga(vertical=True), VERTICAL_ASSET.read_bytes())

    def test_generator_can_write_an_independent_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "fixture.tga"
            output.write_bytes(build_tga())
            self.assertEqual(build_tga(), output.read_bytes())


if __name__ == "__main__":
    unittest.main()
