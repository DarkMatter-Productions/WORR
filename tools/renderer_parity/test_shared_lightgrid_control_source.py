#!/usr/bin/env python3
"""Structural coverage for shared Vulkan/OpenGL lightgrid model lighting."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
GL_WORLD = (ROOT / "src/rend_gl/world.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
VK_ENTITY_SHADER = (ROOT / "src/rend_vk/shaders/vk_entity.frag").read_text(
    encoding="utf-8")
MANIFEST = json.loads((ROOT / "assets/renderer_parity/fr01_lightgrid_manifest.json").read_text(
    encoding="utf-8"))


class SharedLightgridControlSourceTests(unittest.TestCase):
    def test_gl_legacy_alias_syncs_to_archived_shared_control(self) -> None:
        self.assertIn('Cvar_Get("gl_lightgrid", "1", 0)', GL_MAIN)
        self.assertIn('Cvar_Get("r_lightgrid", gl_lightgrid->string, CVAR_ARCHIVE)',
                      GL_MAIN)
        self.assertIn("gl_lightgrid_changed", GL_MAIN)
        self.assertIn("gl_sync_lightgrid_defaults", GL_MAIN)
        self.assertIn("r_lightgrid->changed = gl_lightgrid_changed", GL_MAIN)
        self.assertIn("!r_lightgrid || !r_lightgrid->integer", GL_WORLD)

    def test_vulkan_uses_shared_control_and_gl_equivalent_bmodel_fallback(self) -> None:
        self.assertIn('Cvar_Get("r_lightgrid", "1", CVAR_ARCHIVE)', VK_WORLD)
        self.assertIn("vk_r_lightgrid->integer", VK_WORLD)
        self.assertIn("BSP_TransformedLightPoint", VK_WORLD)
        self.assertIn("vk_world.current_fd->entities", VK_WORLD)
        self.assertIn("transformed.fraction < point.fraction", VK_WORLD)
        self.assertIn("GL_LightPoint uses white whenever no static receiver is found", VK_WORLD)
        self.assertIn("bool *out_has_static_light", VK_WORLD)
        self.assertIn("*out_has_static_light = has_light", VK_WORLD)

    def test_vulkan_preserves_the_unmodulated_gl_no_receiver_contract(self) -> None:
        self.assertIn("VK_ENTITY_VERTEX_NO_ENTITY_MODULATE", VK_ENTITY)
        self.assertIn("bool *out_no_static_lighting", VK_ENTITY)
        self.assertIn("*out_no_static_lighting = true", VK_ENTITY)
        self.assertIn("#define VK_ENTITY_VERTEX_NO_ENTITY_MODULATE 524288u",
                      VK_ENTITY_SHADER)
        self.assertIn("(in_flags & VK_ENTITY_VERTEX_NO_ENTITY_MODULATE) == 0u",
                      VK_ENTITY_SHADER)

    def test_fixture_gates_grid_parity_and_the_per_backend_toggle(self) -> None:
        scenes = {scene["id"]: scene for scene in MANIFEST["scenes"]}
        enabled = scenes["stock_md2_bspx_lightgrid_enabled"]
        disabled = scenes["stock_md2_bspx_lightgrid_disabled"]
        inline_receiver = scenes["stock_md2_inline_bsp_static_receiver"]
        self.assertNotEqual(enabled["capture"], disabled["capture"])
        self.assertTrue(enabled.get("probes"))
        self.assertNotIn("compare_backends", disabled)
        self.assertEqual([2, 2, 2], disabled["metrics"]["max_mean_absolute_rgb"])
        self.assertTrue(inline_receiver.get("probes"))
        self.assertEqual(3, len(MANIFEST["control_pairs"]))
        pair = MANIFEST["control_pairs"][0]
        self.assertEqual(enabled["id"], pair["enabled_scene"])
        self.assertEqual(disabled["id"], pair["disabled_scene"])
        self.assertGreaterEqual(pair["min_pixels_over_threshold_per_backend"], 1)
        receiver_pair = MANIFEST["control_pairs"][1]
        self.assertEqual(disabled["id"], receiver_pair["enabled_scene"])
        self.assertEqual("stock_md2_no_static_receiver",
                         receiver_pair["disabled_scene"])
        inline_pair = MANIFEST["control_pairs"][2]
        self.assertEqual(inline_receiver["id"], inline_pair["enabled_scene"])
        self.assertEqual("stock_md2_no_static_receiver",
                         inline_pair["disabled_scene"])


if __name__ == "__main__":
    unittest.main()
