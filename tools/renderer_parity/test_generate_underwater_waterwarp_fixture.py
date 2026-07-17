#!/usr/bin/env python3
"""Regression checks for the deterministic underwater-waterwarp receiver."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import generate_underwater_waterwarp_fixture as fixture


class UnderwaterWaterwarpFixtureTests(unittest.TestCase):
    def test_authored_underwater_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_marks_only_the_playable_leaf_as_water(self) -> None:
        payload = next(iter(fixture.generated_outputs(Path("assets")).values()))
        dry_payload = fixture.build_bsp(warp_flags=0)
        self.assertEqual(fixture.CONTENTS_WATER, 32)
        changes = [(dry, wet) for dry, wet in zip(dry_payload, payload)
                   if dry != wet]
        self.assertEqual([(0, fixture.CONTENTS_WATER)], changes)

    def test_capture_freezes_the_underwater_phase_and_enables_both_backends(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        config = (
            asset_root / "renderer_parity" / "fr01_underwater_waterwarp.cfg"
        ).read_text(encoding="utf-8")
        manifest = json.loads((
            asset_root / "renderer_parity" /
            "fr01_underwater_waterwarp_manifest.json"
        ).read_text(encoding="utf-8"))
        self.assertIn("set gl_waterwarp 1", config)
        self.assertIn("set vk_waterwarp 1", config)
        self.assertIn("set cl_add_blend 0", config)
        self.assertIn("pause\nwait 10\nscreenshottga", config)
        self.assertEqual(2, len(manifest["scenes"]))
        scene = manifest["scenes"][0]
        self.assertEqual([160, 120, 640, 480], scene["crop"])
        self.assertEqual(2, scene["metrics"]["pixel_threshold"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])
        control = manifest["scenes"][1]
        self.assertEqual("underwater_waterwarp_disabled_control", control["id"])

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
