#!/usr/bin/env python3
"""Headless structural checks for static Vulkan sky submission."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
SHADER = (ROOT / "src/rend_vk/shaders/vk_world.vert").read_text(encoding="utf-8")
SKY_SHADER = (ROOT / "src/rend_vk/shaders/vk_world_sky.frag").read_text(encoding="utf-8")


class VulkanStaticSkyStreamSourceTests(unittest.TestCase):
    def test_sky_vertices_are_immutable_device_local_geometry(self) -> None:
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", WORLD)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_DST_BIT", WORLD)
        self.assertIn("world static sky upload", WORLD)
        self.assertIn("VK_World_CopyStagingToVertexBuffer(staging_buffer, vk_world.sky_vertex_buffer", WORLD)
        self.assertIn("vk_world.sky_geometry_dirty", WORLD)
        self.assertNotIn("sky_vertex_mapped", WORLD)
        self.assertNotIn('#include "rend_gl', WORLD)

    def test_only_streamed_frame_data_carries_sky_rotation(self) -> None:
        self.assertIn("float sky_axis[3][4];", WORLD)
        self.assertIn("sizeof(vk_world_frame_vertex_t) == 64", WORLD)
        self.assertIn("SetupRotationMatrix(sky_matrix, vk_world.sky_axis, degrees)", WORLD)
        self.assertIn("offsetof(vk_world_frame_vertex_t, sky_axis[0])", WORLD)
        self.assertIn("offsetof(vk_world_frame_vertex_t, sky_axis[2])", WORLD)

    def test_shader_keeps_sky_camera_relative_without_cpu_rebuilds(self) -> None:
        self.assertIn("const uint VK_WORLD_VERTEX_SKY = 128u", SHADER)
        self.assertIn("dot(in_sky_axis0.xyz, in_pos)", SHADER)
        self.assertIn("push_data.view * vec4(sky_pos, 0.0)", SHADER)
        self.assertIn("clip = push_data.proj * vec4(view_direction, 1.0)", SHADER)

    def test_compatible_sky_faces_use_one_native_array_draw(self) -> None:
        self.assertIn("VK_World_CreateSkyTextureArray", WORLD)
        self.assertIn("VK_IMAGE_VIEW_TYPE_2D_ARRAY", WORLD)
        self.assertIn("VK_IMAGE_USAGE_TRANSFER_SRC_BIT", (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8"))
        self.assertIn("vk_world.pipeline_sky_array", WORLD)
        self.assertIn("vk_world.sky_portal_vertex_buffer", WORLD)
        self.assertIn(
            "vkCmdDraw(cmd, vk_world.sky_portal_vertex_count, 1, 0, 0)",
            WORLD,
        )
        self.assertIn("pipeline_sky_portal", WORLD)
        self.assertIn("using six-face draws", WORLD)
        self.assertIn("sampler2DArray", SKY_SHADER)
        self.assertIn("shadow_fog_params.z", SKY_SHADER)


if __name__ == "__main__":
    unittest.main()
