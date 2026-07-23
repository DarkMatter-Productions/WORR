#!/usr/bin/env python3
"""Regression coverage for the authored BSPX lightgrid fixture."""

from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import generate_lightgrid_fixture as fixture


class LightgridFixtureTests(unittest.TestCase):
    def test_authored_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_has_a_single_leaf_bspx_lightgrid(self) -> None:
        data = fixture.build_fixture()
        header = data.rfind(b"BSPX")
        self.assertGreater(header, 0)
        self.assertEqual(1, struct.unpack_from("<I", data, header + 4)[0])
        name, offset, length = struct.unpack_from("<24sII", data, header + 8)
        self.assertEqual(fixture.LIGHTGRID_LUMP_NAME, name.rstrip(b"\0"))
        lump = data[offset:offset + length]
        self.assertEqual(1, lump[36])
        self.assertEqual(0x80000000, struct.unpack_from("<I", lump, 37)[0])
        self.assertEqual((0.0, 0.0, -22.0),
                         struct.unpack_from("<3f", lump, 24))
        self.assertEqual(bytes((1, 0, *fixture.LIGHTGRID_SAMPLE_RGB)), lump[-5:])

    def test_generator_writes_the_lightgrid_map_family(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {
                    asset_root / "maps" / fixture.MAP_NAME,
                    asset_root / "maps" / fixture.NO_RECEIVER_MAP_NAME,
                    asset_root / "maps" / fixture.INLINE_RECEIVER_MAP_NAME,
                },
                set(fixture.generated_outputs(asset_root)),
            )

    def test_receiver_and_no_receiver_maps_are_distinct(self) -> None:
        self.assertNotEqual(fixture.build_fixture(),
                            fixture.build_fixture(include_static_receiver=False))
        self.assertNotEqual(fixture.build_fixture(),
                            fixture.build_inline_receiver_fixture())

    def test_inline_receiver_is_hidden_without_leaving_the_refdef(self) -> None:
        inline = fixture.build_inline_receiver_fixture()
        self.assertIn(b'"renderFX" "2"', inline)  # RF_VIEWERMODEL
        self.assertNotIn(b'"alpha" "0"', inline)


if __name__ == "__main__":
    unittest.main()
