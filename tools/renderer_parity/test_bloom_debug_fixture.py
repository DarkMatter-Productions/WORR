#!/usr/bin/env python3
"""Regression contract for the native Vulkan bloom-preview fixture."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_bloom_debug.cfg"
MANIFEST_PATH = ROOT / "assets/renderer_parity/fr01_bloom_debug_manifest.json"


class BloomDebugFixtureTests(unittest.TestCase):
    def test_config_enables_paired_native_bloom_previews(self) -> None:
        config = CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set cheats 1",
            "set gl_bloom 1",
            "set vk_bloom 1",
            "set gl_showbloom 1",
            "set vk_showbloom 1",
            "set r_dof 0",
            "set r_crtmode 0",
        ):
            self.assertIn(line, config)

    def test_manifest_gates_the_full_debug_preview(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T15", manifest["task_id"])
        scene = manifest["scenes"][0]
        self.assertEqual("native_bloom_debug_preview", scene["id"])
        self.assertEqual("renderer_parity/fr01_bloom_debug.cfg", scene["config"])
        self.assertEqual("fr01_bloom_debug.tga", scene["capture"])
        self.assertEqual(
            {
                "pixel_threshold": 0,
                "max_mean_absolute_rgb": [0.001, 0.001, 0.002],
                "max_pixels_over_threshold_percent": 0.5,
            },
            scene["metrics"],
        )


if __name__ == "__main__":
    unittest.main()
