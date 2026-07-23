#!/usr/bin/env python3
"""Regression checks for the forced native GPU-MD5 parity fixture."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG = (ROOT / "assets/renderer_parity/fr01_model_gpu_md5.cfg").read_text(
    encoding="utf-8"
)
MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_model_gpu_md5_manifest.json").read_text(
        encoding="utf-8"
    )
)
VK_DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")


class ModelGpuMd5FixtureTests(unittest.TestCase):
    def test_config_keeps_the_normal_eligible_md5_path(self) -> None:
        for command in (
            "set gl_md5_load 1",
            "set vk_md5_load 1",
            "set gl_md5_use 1",
            "set vk_md5_use 1",
            "set gl_bloom 0",
            "set vk_bloom 0",
            "set vk_celshading 0",
            "vk_stats",
        ):
            self.assertIn(command, CONFIG)
        self.assertNotIn("cl_player_outline", CONFIG)
        self.assertNotIn("RF_OUTLINE", CONFIG)

    def test_manifest_latches_gpu_skinning_before_model_registration(self) -> None:
        scene = MANIFEST["scenes"][0]
        self.assertEqual("gpu_md5_skinning", scene["id"])
        self.assertEqual("renderer_parity/fr01_model_gpu_md5.cfg", scene["config"])
        self.assertEqual("1", scene["launch_cvars"]["vk_md5_gpu_skinning"])
        self.assertEqual([570, 470, 310, 110], scene["crop"])

    def test_vulkan_stats_report_native_gpu_md5_draws(self) -> None:
        self.assertIn("entity_gpu_md5_draws=%u", VK_DEBUG)
        self.assertIn("entity_gpu_md5_instances=%u", VK_DEBUG)
        self.assertIn("void VK_Debug_RecordEntityGpuMd5Draw", VK_DEBUG)
        self.assertIn("VK_Debug_RecordEntityGpuMd5Draw(batch->instance_count)", VK_ENTITY)


if __name__ == "__main__":
    unittest.main()
