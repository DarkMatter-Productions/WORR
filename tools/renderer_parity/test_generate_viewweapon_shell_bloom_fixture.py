#!/usr/bin/env python3
"""Regression checks for the deterministic view-weapon shell bloom fixture."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import generate_viewweapon_shell_bloom_fixture as fixture


class ViewWeaponShellBloomFixtureTests(unittest.TestCase):
    def test_authored_viewweapon_shell_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_enables_an_instant_quad_at_player_spawn(self) -> None:
        self.assertEqual(
            ('"instantItems" "1"\n', '"start_items" "item_quad 30"\n'),
            fixture.WORLDSPAWN_PROPERTIES,
        )

    def test_capture_locks_viewweapon_pose_and_retains_strict_gate(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        config = (asset_root / "renderer_parity" /
                  "fr01_viewweapon_shell_bloom_emission.cfg").read_text(
                      encoding="utf-8")
        manifest = json.loads((asset_root / "renderer_parity" /
                               "fr01_viewweapon_shell_bloom_emission_manifest.json")
                              .read_text(encoding="utf-8"))
        self.assertIn("set cl_gunfov 90\n", config)
        self.assertIn("set hand 0\n", config)
        self.assertIn("set fixedtime 16\nmap ", config)
        self.assertIn(
            "screenshottga fr01_viewweapon_shell_bloom_emission\nwait 10\n"
            "set fixedtime 0\nquit",
            config,
        )
        scene = manifest["scenes"][0]
        self.assertEqual([400, 340, 560, 380], scene["crop"])
        self.assertEqual([0.1, 0.1, 0.1], scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0.1, scene["metrics"]["max_pixels_over_threshold_percent"])

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
