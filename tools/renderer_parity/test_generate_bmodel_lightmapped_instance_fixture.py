#!/usr/bin/env python3
"""Regression checks for the static-lightmapped inline-BSP grid fixture."""

from __future__ import annotations

import struct
import unittest
from pathlib import Path

import generate_bmodel_first_frame_fixture as first_frame
import generate_bmodel_lightmapped_instance_fixture as fixture


class LightmappedBmodelInstanceFixtureTests(unittest.TestCase):
    def test_authored_instance_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_every_inline_box_face_has_its_own_authored_lightmap_range(self) -> None:
        data = next(iter(fixture.generated_outputs(Path("unused")).values()))
        face_lump = 8 + first_frame.LUMP_FACES * 8
        face_offset, face_length = struct.unpack_from("<II", data, face_lump)
        face_size = struct.calcsize("<Hhihh4Bi")
        self.assertEqual(7 * face_size, face_length)
        offsets = [
            struct.unpack_from("<i", data, face_offset + index * face_size + 16)[0]
            for index in range(7)
        ]
        self.assertEqual(0, offsets[0])
        self.assertTrue(all(offset >= 0 for offset in offsets[1:]))
        self.assertEqual(len(set(offsets[1:])), 6)
        self.assertTrue(all(
            data[face_offset + index * face_size + 12] == 0
            for index in range(1, 7)
        ))


if __name__ == "__main__":
    unittest.main()
