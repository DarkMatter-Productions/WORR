#!/usr/bin/env python3
"""Regression checks for static-world texture-replace fixture generation."""

from __future__ import annotations

import struct
import unittest
from pathlib import Path

import generate_world_texture_replace_fixture as fixture


class WorldTextureReplaceFixtureTests(unittest.TestCase):
    def test_authored_maps_are_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_world_face_has_no_lightmap_in_both_variants(self) -> None:
        for payload in fixture.generated_outputs(Path("assets")).values():
            face_lump_offset, _ = struct.unpack_from("<II", payload, 8 + 6 * 8)
            light_offset = struct.unpack_from("<i", payload, face_lump_offset + 16)[0]
            self.assertEqual(-1, light_offset)

    def test_fog_variant_retains_global_fog_worldspawn(self) -> None:
        worldspawn = "".join(fixture.FOG_WORLDSPAWN_PROPERTIES)
        self.assertIn('"fog_density" "0.50"', worldspawn)


if __name__ == "__main__":
    unittest.main()
