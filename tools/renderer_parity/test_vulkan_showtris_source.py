#!/usr/bin/env python3
"""Structural contract for Vulkan's native portable show-tris path."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
VK_DEBUG_HEADER = (ROOT / "src/rend_vk/vk_debug.h").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
SHOWTRIS_FIXTURE = (
    ROOT / "assets/renderer_parity/fr01_showtris_debug.cfg"
).read_text(encoding="utf-8")
SHOWTRIS_UI_FIXTURE = (
    ROOT / "assets/renderer_parity/fr01_showtris_ui_debug.cfg"
).read_text(encoding="utf-8")


class VulkanShowTrisSourceTests(unittest.TestCase):
    def test_native_cvar_keeps_all_four_gl_category_bits(self) -> None:
        self.assertIn('Cvar_Get("vk_showtris", "0", CVAR_CHEAT)', VK_DEBUG)
        for category in (
            "VK_DEBUG_SHOWTRIS_WORLD",
            "VK_DEBUG_SHOWTRIS_MESH",
            "VK_DEBUG_SHOWTRIS_PIC",
            "VK_DEBUG_SHOWTRIS_FX",
        ):
            self.assertIn(category, VK_DEBUG_HEADER)
        self.assertNotIn('#include "rend_gl', VK_DEBUG)

    def test_3d_triangles_use_portable_line_lists_not_non_solid_fill(self) -> None:
        self.assertIn("VK_PRIMITIVE_TOPOLOGY_LINE_LIST", VK_DEBUG)
        self.assertIn("VK_Debug_QueueShowTrisTriangle", VK_DEBUG)
        self.assertIn("VK_Debug_QueueShowTrisTriangleNoDepth", VK_DEBUG_HEADER)
        self.assertIn("VK_Debug_QueueShowTrisTriangles", VK_DEBUG)
        self.assertIn("VK_Debug_RecordShowTris", VK_DEBUG)
        self.assertIn("GL_DrawOutlines preserves each source pass's depth-test state", VK_DEBUG)
        self.assertIn("vk_debug.pipeline_no_depth", VK_DEBUG)
        self.assertNotIn("VK_POLYGON_MODE_LINE", VK_DEBUG)

    def test_world_and_entity_producers_submit_native_geometry(self) -> None:
        self.assertIn("showtris_positions", VK_WORLD)
        self.assertIn("VK_Debug_QueueShowTrisTriangles(VK_DEBUG_SHOWTRIS_WORLD", VK_WORLD)
        self.assertIn("VK_Debug_QueueShowTrisTriangle(vk_entity.showtris_category", VK_ENTITY)
        self.assertIn("VK_Entity_QueueIndexedShowTris", VK_ENTITY)
        self.assertIn("VK_DEBUG_SHOWTRIS_FX", VK_ENTITY)
        self.assertIn("VK_DEBUG_SHOWTRIS_WORLD", VK_ENTITY)
        self.assertIn("VK_DEBUG_SHOWTRIS_MESH", VK_ENTITY)
        self.assertIn("!VK_Debug_ShowTris(VK_DEBUG_SHOWTRIS_MESH)", VK_ENTITY)
        self.assertIn("!VK_Debug_ShowTris(VK_DEBUG_SHOWTRIS_WORLD)", VK_ENTITY)
        self.assertIn("if (flare)", VK_ENTITY)
        self.assertIn("VK_Debug_QueueShowTrisTriangleNoDepth", VK_ENTITY)

    def test_ui_replays_triangle_edges_with_white_clipped_lines(self) -> None:
        self.assertIn("showtris_pipeline", VK_UI)
        self.assertIn("VK_UI_BuildShowTris", VK_UI)
        self.assertIn("VK_PRIMITIVE_TOPOLOGY_LINE_LIST", VK_UI)
        self.assertIn("VK_UI_GetDescriptorSetForImage(vk_ui.white_image)", VK_UI)
        self.assertIn("draw->showtris_first_vertex", VK_UI)
        self.assertIn("vkCmdSetScissor(cmd, 0, 1, &draw->scissor)", VK_UI)

    def test_showtris_is_recorded_after_3d_geometry(self) -> None:
        self.assertIn("VK_Debug_RecordShowTris(cmd, &ctx->scene_extent)", VK_MAIN)
        self.assertLess(
            VK_MAIN.index("VK_World_Record(cmd, &ctx->scene_extent)"),
            VK_MAIN.index("VK_Debug_RecordShowTris(cmd, &ctx->scene_extent)"),
        )

    def test_paired_fixtures_clear_cvars_and_wait_for_async_screenshots(self) -> None:
        for fixture, baseline, overlay in (
            (SHOWTRIS_FIXTURE, "fr01_showtris_baseline", "fr01_showtris_overlay"),
            (SHOWTRIS_UI_FIXTURE, "fr01_showtris_ui_baseline",
             "fr01_showtris_ui_overlay"),
        ):
            self.assertIn("set gl_showtris 0", fixture)
            self.assertIn("set vk_showtris 0", fixture)
            self.assertIn("set gl_showtris ", fixture)
            self.assertIn("set vk_showtris ", fixture)
            self.assertIn(f"screenshottga {baseline}\nwait 2\nset gl_showtris",
                          fixture)
            self.assertIn(f"screenshottga {overlay}\nwait 2", fixture)


if __name__ == "__main__":
    unittest.main()
