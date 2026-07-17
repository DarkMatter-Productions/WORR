#!/usr/bin/env python3
"""Regression checks for the alpha-cutout world and inline-BSP fixture."""

from __future__ import annotations

import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import generate_alpha_test_fixture as fixture


class AlphaTestFixtureTests(unittest.TestCase):
    def test_authored_assets_are_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_bsp_marks_front_world_and_inline_bsp_texinfos_alpha_tested(self) -> None:
        payload = fixture.generated_outputs(Path("assets"))[
            Path("assets") / "maps" / fixture.MAP_NAME
        ]
        texinfo_offset, texinfo_length = struct.unpack_from("<II", payload, 8 + 5 * 8)
        self.assertEqual(228, texinfo_length)
        for texinfo in (0, 2):
            flags = struct.unpack_from("<i", payload, texinfo_offset + texinfo * 76 + 32)[0]
            self.assertEqual(fixture.SURF_ALPHATEST, flags)
        self.assertEqual(
            0, struct.unpack_from("<i", payload, texinfo_offset + 76 + 32)[0]
        )
        node_offset, _ = struct.unpack_from("<II", payload, 8 + 4 * 8)
        self.assertEqual(2, struct.unpack_from("<H", payload, node_offset + 26)[0])

    def test_textures_stably_exercise_discarded_and_visible_cutouts(self) -> None:
        outputs = fixture.generated_outputs(Path("assets"))
        for texture, expected_alpha in (
            (fixture.BACKGROUND_TEXTURE, fixture.PARTIAL_ALPHA),
            (fixture.BACKDROP_TEXTURE, 255),
            (fixture.BMODEL_TEXTURE, 255),
        ):
            payload = outputs[Path("assets") / "textures" / f"{texture}.png"]
            self.assertEqual(b"\x89PNG\r\n\x1a\n", payload[:8])
            ihdr_length = struct.unpack_from(">I", payload, 8)[0]
            self.assertEqual(13, ihdr_length)
            self.assertEqual(b"IHDR", payload[12:16])
            self.assertEqual((16, 16, 8, 6), struct.unpack_from(">IIBB", payload, 16))
            idat_offset = 8 + 12 + ihdr_length
            idat_length = struct.unpack_from(">I", payload, idat_offset)[0]
            self.assertEqual(b"IDAT", payload[idat_offset + 4:idat_offset + 8])
            rows = zlib.decompress(
                payload[idat_offset + 8:idat_offset + 8 + idat_length]
            )
            self.assertEqual({0}, {rows[row * 65] for row in range(16)})
            self.assertEqual(
                {expected_alpha},
                {rows[row * 65 + column * 4 + 4] for row in range(16)
                 for column in range(16)},
            )


if __name__ == "__main__":
    unittest.main()
