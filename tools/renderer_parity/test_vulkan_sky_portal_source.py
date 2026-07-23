#!/usr/bin/env python3
"""Structural guardrails for native Vulkan BSP sky-portal parity."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_SKY_SHADER = (ROOT / "src/rend_vk/shaders/vk_world_sky.frag").read_text(
    encoding="utf-8"
)
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")


class VulkanSkyPortalSourceTests(unittest.TestCase):
    def test_array_path_uses_original_bsp_portals_not_the_static_cube(self) -> None:
        record = VK_WORLD.split("void VK_World_RecordOpaque", 1)[1].split(
            "void VK_World_RecordBloomEmission", 1
        )[0]
        self.assertIn("const bool use_sky_array", record)
        self.assertIn("!use_sky_array", record)
        self.assertIn("render_sky_portals", record)
        self.assertIn("vk_world.pipeline_sky_portal", record)
        self.assertIn("vk_world.sky_array_descriptor_set", record)
        self.assertIn("VK_WORLD_BATCH_SKY", record)
        self.assertIn("use_packed_sky_portals", record)
        self.assertIn("vk_world.sky_portal_vertex_buffer", record)
        self.assertIn("vk_world.sky_portal_vertex_count", record)
        self.assertLess(
            record.index("if (render_skybox_fallback)"),
            record.index("if (render_sky_portals)"),
        )

    def test_compatible_portals_are_packed_once_from_immutable_world_geometry(self) -> None:
        self.assertIn("VK_World_RebuildSkyPortalVertices", VK_WORLD)
        self.assertIn("VK_World_CopyBufferRegions", VK_WORLD)
        self.assertIn("vk_world.vertex_buffer,", VK_WORLD)
        self.assertIn("vk_world.sky_portal_vertex_buffer", VK_WORLD)
        self.assertIn("VK_World_DestroySkyPortalVertexBuffer", VK_WORLD)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_SRC_BIT", VK_WORLD)

    def test_portal_fragment_sampling_uses_world_direction_and_legacy_faces(self) -> None:
        self.assertIn("vec4 sample_portal_sky(vec3 world_pos)", VK_SKY_SHADER)
        self.assertIn("world_pos - view_origin.xyz", VK_SKY_SHADER)
        for face in ("face = 0;", "face = 1;", "face = 2;", "face = 3;", "face = 4;", "face = 5;"):
            self.assertIn(face, VK_SKY_SHADER)
        self.assertIn("sample_portal_sky(in_world_pos)", VK_SKY_SHADER)

    def test_uncovered_sky_portals_clear_to_native_fog_background(self) -> None:
        self.assertIn("bool VK_Shadow_GetSkyFogClearColor(vec3_t out_color)", VK_SHADOW)
        self.assertIn("VK_FOG_GLOBAL | VK_FOG_SKY", VK_SHADOW)
        self.assertIn("const float clear_scale = 250.0f / 255.0f;", VK_SHADOW)
        self.assertIn("VK_Shadow_GetSkyFogClearColor(scene_clear_color);", VK_MAIN)
        self.assertIn("scene_clear_color[0]", VK_MAIN)

    def test_no_opengl_renderer_route(self) -> None:
        for source in (VK_WORLD, VK_SKY_SHADER, VK_SHADOW, VK_MAIN):
            self.assertNotIn('#include "rend_gl', source)


if __name__ == "__main__":
    unittest.main()
