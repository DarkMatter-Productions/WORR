#!/usr/bin/env python3
"""Regression checks for the isolated split-toning parity scene."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "assets/renderer_parity/fr01_split_toning.cfg"
MANIFEST = ROOT / "assets/renderer_parity/fr01_split_toning_manifest.json"


class SplitToningFixtureTests(unittest.TestCase):
    def test_config_isolates_non_identity_split_toning(self) -> None:
        config = CONFIG.read_text(encoding="utf-8")
        for line in (
            "set gl_color_correction 0",
            "set vk_color_correction 0",
            'set gl_color_split_shadows "#402060"',
            'set vk_color_split_shadows "#402060"',
            'set gl_color_split_highlights "#ffd080"',
            'set vk_color_split_highlights "#ffd080"',
            "set gl_color_split_strength 0.85",
            "set vk_color_split_strength 0.85",
            "set gl_color_split_balance -0.10",
            "set vk_color_split_balance -0.10",
        ):
            self.assertIn(line, config)

    def test_manifest_locks_the_non_identity_output(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        scene = manifest["scenes"][0]
        self.assertEqual("split_toning_fixed_view", scene["id"])
        self.assertEqual("renderer_parity/fr01_split_toning.cfg", scene["config"])
        self.assertEqual("fr01_split_toning.tga", scene["capture"])
        self.assertEqual([9, 10, 34], scene["probes"][0]["color"])
        self.assertEqual(50000, scene["probes"][0]["min_pixels_per_backend"])


if __name__ == "__main__":
    unittest.main()
