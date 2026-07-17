#!/usr/bin/env python3
"""Lock the shared video anisotropy control to both native renderers."""

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
SHARED_CONFIG = (ROOT / "assets/renderer_parity/fr01_model_glowmap_shared_anisotropy.cfg").read_text(
    encoding="utf-8"
)
SHARED_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_model_glowmap_shared_anisotropy_manifest.json").read_text(
        encoding="utf-8"
    )
)


class SharedAnisotropyControlSourceTests(unittest.TestCase):
    def test_all_video_settings_routes_bind_the_shared_preference(self) -> None:
        self.assertIn('pairs "anisotropic filter" r_anisotropy', LEGACY_MENU)
        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        anisotropy = next(
            item
            for item in video_menu["items"]
            if item.get("label") == "anisotropic filter"
        )
        self.assertEqual(anisotropy["cvar"], "r_anisotropy")
        self.assertIn(
            '<select id="video-anisotropy" data-cvar="r_anisotropy">',
            RML_VIDEO,
        )

    def test_opengl_alias_updates_existing_native_textures(self) -> None:
        self.assertIn("static cvar_t *r_anisotropy;", GL_TEXTURE)
        self.assertIn("static void gl_sync_anisotropy_defaults(void)", GL_TEXTURE)
        self.assertIn('Cvar_Get("r_anisotropy", gl_anisotropy->string,', GL_TEXTURE)
        self.assertIn("if (self == r_anisotropy && gl_anisotropy)", GL_TEXTURE)
        self.assertIn("Cvar_SetByVar(gl_anisotropy, self->string, FROM_CODE);", GL_TEXTURE)
        self.assertIn("r_anisotropy ? r_anisotropy : self", GL_TEXTURE)
        self.assertIn("update_image_params(BIT(IT_WALL) | BIT(IT_SKIN));", GL_TEXTURE)

    def test_vulkan_alias_rebuilds_native_descriptors(self) -> None:
        self.assertIn("static cvar_t *r_anisotropy;", VK_UI)
        self.assertIn("static void VK_UI_RegisterAnisotropyCvars", VK_UI)
        self.assertIn("static void VK_UI_UnregisterAnisotropyCvars", VK_UI)
        self.assertIn("if (self == r_anisotropy && vk_anisotropy)", VK_UI)
        self.assertIn("Cvar_SetByVar(vk_anisotropy, self->string, FROM_CODE);", VK_UI)
        self.assertIn("VK_UI_RegisterAnisotropyCvars(ctx);", VK_UI)
        self.assertIn("VK_UI_UnregisterAnisotropyCvars();", VK_UI)

    def test_headless_fixture_exercises_the_shared_one_x_preference(self) -> None:
        self.assertIn("set r_anisotropy 1", SHARED_CONFIG)
        scene = SHARED_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "stock_md2_shared_anisotropy_one_x")
        self.assertEqual(
            scene["config"],
            "renderer_parity/fr01_model_glowmap_shared_anisotropy.cfg",
        )
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [1, 0.3, 0.3])
        probe = scene["probes"][0]
        self.assertEqual(probe["min_pixels_per_backend"], 1700)
        self.assertEqual(probe["min_backend_intersection_over_union"], 0.92)


if __name__ == "__main__":
    unittest.main()
