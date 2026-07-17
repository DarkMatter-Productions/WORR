#!/usr/bin/env python3
"""Headless contract coverage for the forced native GPU MD2 parity lane."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG = (
    ROOT / "assets/renderer_parity/fr01_model_glowmap_md2_gpu.cfg"
).read_text(encoding="utf-8")
MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_model_glowmap_md2_gpu_manifest.json")
    .read_text(encoding="utf-8")
)


class ModelGpuMd2FixtureTests(unittest.TestCase):
    def test_config_forces_the_stock_md2_path_before_map_registration(self) -> None:
        self.assertEqual("set r_dof 0", CONFIG.splitlines()[0])
        for line in (
            "set gl_md5_load 0",
            "set vk_md5_load 0",
            "set gl_md5_use 0",
            "set vk_md5_use 0",
            "map worr_fr01_model_glowmap",
            "screenshottga fr01_model_glowmap_md2_gpu",
        ):
            self.assertIn(line, CONFIG)
        self.assertLess(
            CONFIG.index("set vk_md5_load 0"),
            CONFIG.index("map worr_fr01_model_glowmap"),
        )

    def test_manifest_locks_the_model_crop_and_glow_emission_probe(self) -> None:
        self.assertEqual(1, MANIFEST["schema_version"])
        self.assertEqual("FR-01-T15", MANIFEST["task_id"])
        scene = MANIFEST["scenes"][0]
        self.assertEqual(
            "renderer_parity/fr01_model_glowmap_md2_gpu.cfg", scene["config"]
        )
        self.assertEqual("fr01_model_glowmap_md2_gpu.tga", scene["capture"])
        self.assertEqual([570, 470, 310, 110], scene["crop"])
        self.assertEqual(
            "stock_md2_gpu_interpolation_glow_emission",
            scene["probes"][0]["name"],
        )


if __name__ == "__main__":
    unittest.main()
