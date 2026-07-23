#!/usr/bin/env python3
"""Structural guardrails for native paletted-sprite cutout parity."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
CONFIG = (ROOT / "assets/renderer_parity/fr02_sprite_cutout.cfg").read_text(
    encoding="utf-8"
)
MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr02_sprite_cutout_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanSpriteCutoutSourceTests(unittest.TestCase):
    def test_image_registry_retains_resolved_paletted_encoding(self) -> None:
        self.assertIn("static imageflags_t VK_UI_EncodedImageFlags", VK_UI)
        self.assertIn('".pcx") || !Q_stricmp(ext, ".wal")', VK_UI)
        self.assertIn("file_data[2] == 1 || file_data[2] == 9", VK_UI)
        self.assertIn('file_data[25] == 3', VK_UI)
        self.assertIn("imageflags_t loaded_flags = IF_NONE", VK_UI)
        self.assertIn("flags | loaded_flags", VK_UI)

    def test_native_sprite_uses_the_same_alpha_test_state_as_opengl(self) -> None:
        gl_sprite = GL_MAIN.split("static void GL_DrawSpriteModel", 1)[1].split(
            "static void GL_DrawNullModel", 1
        )[0]
        self.assertIn("IF_PALETTED", gl_sprite)
        self.assertIn("GLS_ALPHATEST_ENABLE", gl_sprite)
        sprite = VK_ENTITY.split("static bool VK_Entity_AddSprite", 1)[1].split(
            "static vk_vertex_t VK_Entity_BeamVertex", 1
        )[0]
        self.assertIn("VK_UI_GetImageFlags(sf->image)", sprite)
        self.assertIn("IF_PALETTED | IF_TRANSPARENT", sprite)
        self.assertIn("VK_ENTITY_VERTEX_ALPHATEST", sprite)
        self.assertIn("VK_Entity_EmitTriCutout", sprite)
        self.assertNotIn('#include "rend_gl', VK_ENTITY)

    def test_cutout_pipeline_disables_blending_and_depth_writes(self) -> None:
        self.assertIn("VK_ENTITY_BLEND_CUTOUT", VK_ENTITY)
        self.assertIn("bool cutout = blend_mode == VK_ENTITY_BLEND_CUTOUT", VK_ENTITY)
        self.assertIn("!alpha && !cutout && !occlusion", VK_ENTITY)
        self.assertIn("pipeline_cutout", VK_ENTITY)
        self.assertIn("pipeline_depthhack_cutout", VK_ENTITY)

    def test_fixture_is_a_paired_unfogged_cutout_capture(self) -> None:
        self.assertIn("set gl_fog 0", CONFIG)
        self.assertIn("set vk_fog 0", CONFIG)
        self.assertIn("map worr_fr01_sprite_fog", CONFIG)
        self.assertEqual(MANIFEST["task_id"], "FR-02-T05")
        scene = MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "sprite_paletted_cutout_fixed_view")
        self.assertEqual(scene["crop"], [440, 220, 360, 400])
        self.assertEqual(scene["metrics"]["pixel_threshold"], 1)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0, 0, 0.001])
        probe = scene["probes"][0]
        self.assertEqual(scene["probes"][0]["name"], "visible_paletted_bfg_sprite_cutout")
        self.assertEqual(probe["color"], [48, 220, 96])
        self.assertEqual(probe["tolerance"], 0)
        self.assertEqual(probe["min_pixels_per_backend"], 34000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 1.0)


if __name__ == "__main__":
    unittest.main()
