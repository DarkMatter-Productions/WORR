#!/usr/bin/env python3
"""Source contracts for the native Vulkan alias-model cel pass."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
SHADER = (ROOT / "src/rend_vk/shaders/vk_entity.frag").read_text(
    encoding="utf-8"
)
GENERATOR = (ROOT / "tools/gen_vk_world_spv.py").read_text(encoding="utf-8")
CONFIG = (ROOT / "assets/renderer_parity/fr01_model_celshading.cfg").read_text(
    encoding="utf-8"
)
MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_model_celshading_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanCelShadingSourceTests(unittest.TestCase):
    def test_device_enables_the_required_optional_raster_features(self) -> None:
        self.assertIn("features.fillModeNonSolid == VK_TRUE", MAIN)
        self.assertIn("features.wideLines == VK_TRUE", MAIN)
        self.assertIn("enabled_features.fillModeNonSolid = VK_TRUE", MAIN)
        self.assertIn("enabled_features.wideLines = VK_TRUE", MAIN)

    def test_shader_is_a_black_untextured_replay(self) -> None:
        self.assertIn("#ifdef VK_ENTITY_CELSHADING", SHADER)
        cel_section = SHADER.split("#ifdef VK_ENTITY_CELSHADING", 1)[1].split(
            "#elif", 1
        )[0]
        self.assertIn("vec4(0.0, 0.0, 0.0", cel_section)
        self.assertNotIn("texture(", cel_section)
        self.assertIn("vk_entity_celshading_frag_spv", GENERATOR)

    def test_replay_retains_per_entity_fade_and_native_line_raster(self) -> None:
        self.assertIn('Cvar_Get("vk_celshading", "0", 0)', ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_CELSHADING", ENTITY)
        self.assertIn("VK_POLYGON_MODE_LINE", ENTITY)
        self.assertIn("VK_CULL_MODE_BACK_BIT", ENTITY)
        self.assertIn("VK_DYNAMIC_STATE_LINE_WIDTH", ENTITY)
        self.assertIn("vkCmdSetLineWidth(cmd, batch->cel_line_width)", ENTITY)
        self.assertIn("VK_Entity_RecordCelShadingPass", ENTITY)

    def test_replay_occurs_after_opaque_and_before_alpha_passes(self) -> None:
        loop_start = ENTITY.index("for (int pass = 0; pass < 4; pass++)")
        replay = ENTITY.index("VK_Entity_RecordCelShadingPass(cmd, frame, phase, false")
        depth_hack = ENTITY.index("if (vk_entity.frame_has_depth_hack_batches)")
        self.assertGreater(replay, loop_start)
        self.assertLess(replay, depth_hack)

    def test_stock_md2_fixture_enables_both_native_cel_cvars(self) -> None:
        self.assertIn("set gl_md5_use 0", CONFIG)
        self.assertIn("set vk_md5_use 0", CONFIG)
        self.assertIn("set gl_celshading 1", CONFIG)
        self.assertIn("set vk_celshading 1", CONFIG)
        self.assertIn("screenshottga fr01_model_celshading", CONFIG)
        scene = MANIFEST["scenes"][0]
        self.assertEqual("stock_md2_celshading", scene["id"])
        self.assertEqual("fr01_model_celshading.tga", scene["capture"])
        self.assertEqual("black_cel_contours", scene["probes"][0]["name"])


if __name__ == "__main__":
    unittest.main()
