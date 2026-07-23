#!/usr/bin/env python3
"""Regression checks for native opaque-sprite no-depth-write parity."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src" / "rend_gl" / "main.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src" / "rend_vk" / "vk_entity.c").read_text(encoding="utf-8")
CONFIG = (ROOT / "assets" / "renderer_parity" / "fr02_sprite_depth_write.cfg").read_text(
    encoding="utf-8"
)
MANIFEST = json.loads(
    (ROOT / "assets" / "renderer_parity" / "fr02_sprite_depth_write_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanSpriteDepthWriteParityTests(unittest.TestCase):
    def test_native_opaque_sprite_uses_a_precreated_unblended_no_depth_pipeline(self) -> None:
        gl_sprite = GL_MAIN[GL_MAIN.index("static void GL_DrawSpriteModel("):]
        self.assertIn("GLS_DEPTHMASK_FALSE", gl_sprite)
        add_sprite = VK_ENTITY[VK_ENTITY.index("static bool VK_Entity_AddSprite("):]
        self.assertIn("if (!alpha)", add_sprite)
        self.assertIn("VK_Entity_EmitTriOpaqueNoDepth", add_sprite)
        self.assertIn("VK_ENTITY_BLEND_OPAQUE_NO_DEPTH_WRITE", VK_ENTITY)
        self.assertIn("pipeline_opaque_no_depth_write", VK_ENTITY)
        self.assertIn("!opaque_no_depth_write", VK_ENTITY)

    def test_headless_fixture_places_later_translucency_behind_the_opaque_sprite(self) -> None:
        self.assertIn("map worr_fr02_sprite_depth_write", CONFIG)
        self.assertIn("teleport 0 0 -22 0 0 0", CONFIG)
        fixture = (ROOT / "tools" / "renderer_parity" /
                   "generate_sprite_depth_write_fixture.py").read_text(encoding="utf-8")
        self.assertIn('"origin" "256 0 -22"', fixture)
        self.assertIn('"origin" "384 0 -22"', fixture)
        self.assertIn('"alpha" "0.5"', fixture)

    def test_strict_manifest_locks_the_blend_visible_through_the_opaque_sprite(self) -> None:
        self.assertEqual("FR-02-T05", MANIFEST["task_id"])
        scene = MANIFEST["scenes"][0]
        self.assertEqual([300, 180, 360, 440], scene["crop"])
        self.assertEqual(
            {
                "pixel_threshold": 0,
                "max_mean_absolute_rgb": [0, 0, 0],
                "max_pixels_over_threshold_percent": 0,
            },
            scene["metrics"],
        )
        self.assertEqual(
            ["opaque_sprite_outer_region", "far_translucent_sprite_blended_over_opaque"],
            [probe["name"] for probe in scene["probes"]],
        )
        self.assertTrue(all(probe["min_backend_intersection_over_union"] == 1.0
                            for probe in scene["probes"]))


if __name__ == "__main__":
    unittest.main()
