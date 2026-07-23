#!/usr/bin/env python3
"""Regression checks for the paired full-screen blend fixture."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


class ScreenBlendFixtureTests(unittest.TestCase):
    def test_config_locks_both_blends_and_capture_time(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        config = (asset_root / "renderer_parity" / "fr02_screen_blend.cfg").read_text(
            encoding="utf-8"
        )
        self.assertIn("set cl_testblend 3\n", config)
        self.assertIn("set gl_damageblend_frac 0.2\n", config)
        self.assertIn("set fixedtime 16\nmap worr_fr01_bmodel_first_frame", config)
        self.assertIn("set fixedtime 0\nset cl_testblend 0\nquit", config)

    def test_manifest_locks_the_blended_frame_and_inner_receiver(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        manifest = json.loads(
            (asset_root / "renderer_parity" / "fr02_screen_blend_manifest.json").read_text(
                encoding="utf-8"
            )
        )
        scene = manifest["scenes"][0]
        self.assertEqual([0, 0, 960, 720], scene["crop"])
        self.assertEqual(2, scene["metrics"]["pixel_threshold"])
        self.assertEqual([1.0, 1.0, 0.4], scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])
        probe = scene["probes"][0]
        self.assertEqual("screen_blend_inner_receiver", probe["name"])
        self.assertEqual(245000, probe["min_pixels_per_backend"])
        self.assertEqual(1, probe["min_backend_intersection_over_union"])

    def test_lava_config_and_manifest_use_the_real_gameplay_receiver(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        config = (asset_root / "renderer_parity" /
                  "fr02_screen_blend_lava.cfg").read_text(encoding="utf-8")
        manifest = json.loads(
            (asset_root / "renderer_parity" /
             "fr02_screen_blend_lava_manifest.json").read_text(encoding="utf-8")
        )
        self.assertIn("set cl_testblend 0\n", config)
        self.assertIn("set fixedtime 16\nmap q2dm1", config)
        self.assertIn("teleport 600 950 320 0 0 0", config)
        self.assertIn("set fixedtime 0\nquit", config)
        scene = manifest["scenes"][0]
        self.assertEqual([1.3, 1.0, 0.75], scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(24, scene["metrics"]["pixel_threshold"])
        self.assertEqual(0.02, scene["metrics"]["max_pixels_over_threshold_percent"])
        probe = scene["probes"][0]
        self.assertEqual("real_lava_screen_blend_receiver", probe["name"])
        self.assertEqual([214, 64, 0], probe["min_color"])
        self.assertEqual(380000, probe["min_pixels_per_backend"])
        self.assertEqual(0.999, probe["min_backend_intersection_over_union"])

    def test_native_vulkan_keeps_blends_between_scene_and_hud(self) -> None:
        root = Path(__file__).resolve().parents[2]
        vk_main = (root / "src" / "rend_vk" / "vk_main.c").read_text(encoding="utf-8")
        vk_ui = (root / "src" / "rend_vk" / "vk_ui.c").read_text(encoding="utf-8")
        gl_draw = (root / "src" / "rend_gl" / "draw.c").read_text(encoding="utf-8")
        self.assertIn("VK_UI_DrawScreenBlend(fd, vk_damageblend_frac", vk_main)
        self.assertLess(
            vk_main.index("VK_Entity_RenderFrame(fd);"),
            vk_main.index("VK_UI_DrawScreenBlend(fd, vk_damageblend_frac"),
        )
        self.assertIn("void VK_UI_DrawScreenBlend", vk_ui)
        self.assertIn("VK_UI_EnqueueVignette", vk_ui)
        self.assertIn("fd->screen_blend[3]", vk_ui)
        self.assertIn("fd->damage_blend[3]", vk_ui)
        self.assertIn("void GL_Blend(void)", gl_draw)
        self.assertIn("GL_DrawVignette", gl_draw)


if __name__ == "__main__":
    unittest.main()
