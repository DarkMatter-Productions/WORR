#!/usr/bin/env python3
"""Lock the shared material-filter setting to both native renderers."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_TEXTURE = (ROOT / "src/rend_gl/texture.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(
    encoding="utf-8"
)
FILTER_CONFIG = (ROOT / "assets/renderer_parity/fr01_model_glowmap_shared_texture_filter.cfg").read_text(
    encoding="utf-8"
)
FILTER_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_model_glowmap_shared_texture_filter_manifest.json").read_text(
        encoding="utf-8"
    )
)


class SharedTextureFilterControlSourceTests(unittest.TestCase):
    def test_video_routes_bind_the_shared_material_filter(self) -> None:
        self.assertIn('pairs "texture filter" r_texture_filter', LEGACY_MENU)
        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        texture_filter = next(
            item for item in video_menu["items"] if item.get("label") == "texture filter"
        )
        self.assertEqual(texture_filter["cvar"], "r_texture_filter")
        self.assertIn(
            '<select id="video-texture-filter" data-cvar="r_texture_filter">',
            RML_VIDEO,
        )

    def test_opengl_keeps_legacy_alias_and_reapplies_wall_skin_parameters(self) -> None:
        self.assertIn("static cvar_t *r_texture_filter;", GL_TEXTURE)
        self.assertIn("static void gl_sync_texturemode_defaults(void)", GL_TEXTURE)
        self.assertIn('Cvar_Get("r_texture_filter", gl_texturemode->string,', GL_TEXTURE)
        self.assertIn("if (self == r_texture_filter && gl_texturemode)", GL_TEXTURE)
        self.assertIn("r_texture_filter ? r_texture_filter : self", GL_TEXTURE)
        self.assertIn("update_image_params(BIT(IT_WALL) | BIT(IT_SKIN));", GL_TEXTURE)

    def test_vulkan_uses_an_independent_native_material_sampler(self) -> None:
        self.assertIn("VkSampler sampler_material_repeat;", VK_UI)
        self.assertIn("static void VK_UI_ConfigureMaterialSampler", VK_UI)
        self.assertIn("static bool VK_UI_IsMaterialFilter", VK_UI)
        self.assertIn("VK_UI_RegisterTextureFilterCvars();", VK_UI)
        self.assertIn("VK_UI_UnregisterTextureFilterCvars();", VK_UI)
        self.assertIn("vkCreateSampler(material repeat)", VK_UI)
        self.assertIn("sampler = vk_ui.sampler_material_repeat;", VK_UI)
        self.assertIn("vkDeviceWaitIdle(texture filter update)", VK_UI)

    def test_headless_fixture_exercises_nearest_material_sampling(self) -> None:
        self.assertIn("set r_texture_filter GL_NEAREST", FILTER_CONFIG)
        scene = FILTER_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "stock_md2_shared_texture_filter_nearest")
        self.assertEqual(
            scene["config"],
            "renderer_parity/fr01_model_glowmap_shared_texture_filter.cfg",
        )
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [1, 0.3, 0.3])
        self.assertEqual(scene["probes"][0]["min_pixels_per_backend"], 1700)


if __name__ == "__main__":
    unittest.main()
