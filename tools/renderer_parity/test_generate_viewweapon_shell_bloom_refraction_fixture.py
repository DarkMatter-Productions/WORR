#!/usr/bin/env python3
"""Regression checks for the refraction-aware view-weapon shell fixture."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import generate_viewweapon_shell_bloom_refraction_fixture as fixture


class ViewWeaponShellBloomRefractionFixtureTests(unittest.TestCase):
    def test_authored_viewweapon_refraction_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_enables_an_instant_quad_at_player_spawn(self) -> None:
        self.assertEqual(
            ('"instantItems" "1"\n', '"start_items" "item_quad 30"\n'),
            fixture.WORLDSPAWN_PROPERTIES,
        )

    def test_fixture_uses_a_transparent_warp_receiver(self) -> None:
        self.assertEqual(
            fixture.SURF_WARP | fixture.SURF_TRANS33 | fixture.SURF_FLOWING,
            fixture.TRANSPARENT_WARP_FLAGS,
        )

    def test_capture_is_paused_and_water_region_is_a_strict_gate(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        config = (
            asset_root / "renderer_parity" /
            "fr01_viewweapon_shell_bloom_refraction.cfg"
        ).read_text(encoding="utf-8")
        manifest = json.loads((
            asset_root / "renderer_parity" /
            "fr01_viewweapon_shell_bloom_refraction_manifest.json"
        ).read_text(encoding="utf-8"))
        self.assertIn("pause\nwait 10\nscreenshottga", config)
        scene = manifest["scenes"][0]
        self.assertEqual([160, 120, 320, 400], scene["crop"])
        self.assertEqual(1, scene["metrics"]["pixel_threshold"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])

    def test_refraction_is_enabled_only_after_the_map_has_initialized(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for filename in (
            "fr01_viewweapon_shell_bloom_refraction.cfg",
            "fr01_hdr_viewweapon_shell_bloom_refraction.cfg",
        ):
            config = (asset_root / "renderer_parity" / filename).read_text(
                encoding="utf-8"
            )
            map_marker = "map worr_fr01_viewweapon_shell_bloom_refraction\nwait 60\n"
            enable_marker = (
                "set gl_warp_refraction 0.1\n"
                "set vk_warp_refraction 0.1\n"
                "wait 10\n"
            )
            self.assertIn("set gl_warp_refraction 0\n", config)
            self.assertIn("set vk_warp_refraction 0\n", config)
            self.assertLess(config.index(map_marker), config.index(enable_marker))

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
