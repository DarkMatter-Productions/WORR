#!/usr/bin/env python3
"""Headless structural checks for bounded native Vulkan frame contexts."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_LOCAL = (ROOT / "src/rend_vk/vk_local.h").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
VK_DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
VK_POST = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")


class VulkanFramesInFlightSourceTests(unittest.TestCase):
    def test_frame_context_owns_submission_sync_and_commands(self) -> None:
        self.assertIn("#define VK_MAX_FRAMES_IN_FLIGHT 2", VK_LOCAL)
        self.assertIn("typedef struct vk_frame_context_s", VK_LOCAL)
        for member in ("VkCommandBuffer command_buffer;", "VkSemaphore image_available;",
                       "VkFence in_flight_fence;", "bool submitted;"):
            self.assertIn(member, VK_LOCAL)
        self.assertIn("vk_frame_context_t frames[VK_MAX_FRAMES_IN_FLIGHT];", VK_LOCAL)
        self.assertIn("VkImage depth_image;", VK_LOCAL)
        self.assertIn("VkFramebuffer *framebuffers;", VK_LOCAL)
        self.assertIn("VkImage liquid_scene_image;", VK_LOCAL)
        self.assertIn("VkDescriptorSet liquid_scene_descriptor_set;", VK_LOCAL)
        self.assertIn("VkImageView depth_sample_view;", VK_LOCAL)
        self.assertIn("uint32_t *image_frame_slots;", VK_LOCAL)

    def test_scheduler_waits_current_slot_and_acquired_image_owner(self) -> None:
        self.assertIn(
            "VK_WaitForFrame(ctx, ctx->current_frame",
            VK_MAIN,
        )
        self.assertIn("image_frame_slots[image_index]", VK_MAIN)
        self.assertIn("vkWaitForFences(acquired swapchain image)", VK_MAIN)
        self.assertIn("ctx->current_frame = (frame_index + 1) % ctx->frame_count;", VK_MAIN)
        self.assertNotIn("frame_submitted", VK_MAIN)
        self.assertNotIn("ctx->in_flight_fence", VK_MAIN)
        self.assertNotIn("ctx->image_available", VK_MAIN)

    def test_transient_uploads_and_queries_are_frame_local(self) -> None:
        self.assertIn("frame_buffers[VK_MAX_FRAMES_IN_FLIGHT]", VK_UI)
        self.assertIn("frame_buffers[VK_MAX_FRAMES_IN_FLIGHT]", VK_ENTITY)
        self.assertIn("frame_buffers[VK_MAX_FRAMES_IN_FLIGHT]", VK_WORLD)
        self.assertIn("frame_resources[VK_MAX_FRAMES_IN_FLIGHT]", VK_SHADOW)
        self.assertIn("frame_buffers[VK_MAX_FRAMES_IN_FLIGHT]", VK_DEBUG)
        self.assertIn("ctx->frame_count * VK_DEBUG_GPU_TIMESTAMP_COUNT", VK_DEBUG)
        self.assertIn("timestamp_query_base[VK_MAX_FRAMES_IN_FLIGHT]", VK_DEBUG)
        self.assertIn("frame->depth_view", VK_MAIN)
        self.assertIn("frame->framebuffers[image_index]", VK_MAIN)
        self.assertIn("frame->liquid_scene_image", VK_MAIN)
        self.assertIn("frame->liquid_scene_descriptor_set", VK_MAIN)
        self.assertIn("frame->depth_sample_view", VK_MAIN)
        self.assertIn("frame->dof_ping", VK_POST)
        self.assertIn("frame->dof_pong", VK_POST)
        self.assertIn("frame->dof_scene", VK_POST)

    def test_shared_postprocess_rebuilds_retire_submitted_frames(self) -> None:
        self.assertIn("VK_PostProcess_NeedsSafeResourceUpdate", VK_POST)
        self.assertIn("VK_PostProcess_CurrentSceneView", VK_POST)
        self.assertIn(
            "frame_resources[VK_MAX_FRAMES_IN_FLIGHT]",
            VK_POST,
        )
        self.assertIn("VK_PostProcess_DestroyAllExternalDescriptors", VK_POST)
        self.assertIn("frame->bloom_ping", VK_POST)
        self.assertIn("frame->final_scene_descriptor_set", VK_POST)
        self.assertIn("dof_resources_dirty", VK_POST)
        self.assertIn("descriptor_dof_active", VK_POST)
        self.assertIn("descriptor_lut_active", VK_POST)
        self.assertIn("descriptor_bloom_active", VK_POST)
        self.assertIn("VK_WaitForSubmittedFrames(ctx,", VK_MAIN)
        self.assertIn("vkWaitForFences(postprocess resource rebuild)", VK_MAIN)
        self.assertNotIn("VK_FrameUsesSharedPresentationTargets", VK_MAIN)

    def test_no_opengl_renderer_route(self) -> None:
        for source in (VK_LOCAL, VK_MAIN, VK_UI, VK_ENTITY, VK_WORLD, VK_SHADOW,
                       VK_DEBUG, VK_POST):
            self.assertNotIn('#include "rend_gl', source)


if __name__ == "__main__":
    unittest.main()
