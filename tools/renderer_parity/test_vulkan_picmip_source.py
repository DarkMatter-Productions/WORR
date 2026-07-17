#!/usr/bin/env python3
"""Guard native Vulkan r_picmip upload-size parity."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(
    encoding="utf-8"
)
PICMIP_CONFIG = (ROOT / "assets/renderer_parity/fr01_model_glowmap_picmip.cfg").read_text(encoding="utf-8")
PICMIP_MANIFEST = json.loads((ROOT / "assets/renderer_parity/fr01_model_glowmap_picmip_manifest.json").read_text(encoding="utf-8"))


class VulkanPicmipSourceTests(unittest.TestCase):
    def test_video_routes_bind_shared_texture_quality(self) -> None:
        self.assertIn('range "texture quality" r_picmip', LEGACY_MENU)
        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        texture_quality = next(
            item for item in video_menu["items"] if item.get("label") == "texture quality"
        )
        self.assertEqual(texture_quality["cvar"], "r_picmip")
        self.assertIn('data-cvar="r_picmip"', RML_VIDEO)

    def test_vulkan_downsamples_eligible_material_uploads_before_mip_generation(self) -> None:
        self.assertIn('Cvar_Get("r_picmip", "0", CVAR_ARCHIVE | CVAR_FILES)', VK_UI)
        self.assertIn('Cvar_Get("r_picmip_filter", "3",', VK_UI)
        self.assertIn('Cvar_Get("gl_downsample_skins", "1",', VK_UI)
        self.assertIn("static bool VK_UI_ShouldPicmip", VK_UI)
        self.assertIn("static bool VK_UI_ApplyPicmip", VK_UI)
        self.assertIn("static void VK_UI_MipMapRgba", VK_UI)
        self.assertIn("VK_UI_ApplyPicmip(image, &width, &height, rgba, &picmip_rgba)", VK_UI)
        self.assertIn("image->mip_levels = VK_UI_ImageMipLevelCount(image, width, height);", VK_UI)

    def test_headless_fixture_locks_reduced_material_residency(self) -> None:
        self.assertIn("set r_picmip 1", PICMIP_CONFIG)
        self.assertIn("set r_picmip_filter 0", PICMIP_CONFIG)
        self.assertEqual(PICMIP_MANIFEST["scenes"][0]["probes"][0]["min_pixels_per_backend"], 1000)


if __name__ == "__main__":
    unittest.main()
