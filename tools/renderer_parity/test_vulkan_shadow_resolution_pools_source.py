#!/usr/bin/env python3
"""Structural guardrails for native Vulkan mixed-resolution shadow pools."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
WORLD = (ROOT / "src/rend_vk/shaders/vk_world_shadow.frag").read_text(
    encoding="utf-8"
)
ENTITY = (ROOT / "src/rend_vk/shaders/vk_entity.frag").read_text(
    encoding="utf-8"
)


class VulkanShadowResolutionPoolsSourceTests(unittest.TestCase):
    def test_native_pools_cover_every_supported_shadow_resolution(self) -> None:
        self.assertIn("VK_SHADOW_RESOLUTION_POOL_COUNT 5u", VK_SHADOW)
        self.assertIn("static uint32_t VK_Shadow_ResolutionPoolIndex", VK_SHADOW)
        self.assertIn("static int VK_Shadow_ResolutionForPool", VK_SHADOW)
        self.assertIn("vk_shadow_resolution_pools[VK_SHADOW_RESOLUTION_POOL_COUNT]", VK_SHADOW)
        self.assertIn("VK_Shadow_SelectResolutionPool", VK_SHADOW)

    def test_page_ids_keep_a_stable_compact_layer_per_pool(self) -> None:
        self.assertIn("typedef struct {\n    uint32_t generation;", VK_SHADOW)
        self.assertIn("} vk_shadow_page_mapping_t;", VK_SHADOW)
        self.assertIn("VK_Shadow_EnsurePageMapping", VK_SHADOW)
        self.assertIn("bool used[VK_SHADOW_MAX_PAGES] = { false };", VK_SHADOW)
        self.assertIn("mapping->pool_layer", VK_SHADOW)
        self.assertIn("job->pool_layer = pool_layer;", VK_SHADOW)
        self.assertIn("page->location[0] = (float)mapping->pool_layer;", VK_SHADOW)

    def test_receiver_descriptors_and_both_receiver_shaders_route_by_pool(self) -> None:
        self.assertIn(".descriptorCount = VK_SHADOW_RESOLUTION_POOL_COUNT", VK_SHADOW)
        self.assertIn("VkDescriptorImageInfo image_infos[VK_SHADOW_RESOLUTION_POOL_COUNT]", VK_SHADOW)
        for shader in (WORLD, ENTITY):
            self.assertIn("#define VK_SHADOW_RESOLUTION_POOL_COUNT 5", shader)
            self.assertIn("vec4 location;", shader)
            self.assertIn("shadow_sampler[VK_SHADOW_RESOLUTION_POOL_COUNT]", shader)
            self.assertIn("shadow_sampler_cmp[VK_SHADOW_RESOLUTION_POOL_COUNT]", shader)
            self.assertIn("shadow_moments[VK_SHADOW_RESOLUTION_POOL_COUNT]", shader)
            self.assertIn("int shadow_page_pool(int page)", shader)
            self.assertIn("vec2 shadow_moment_lookup", shader)

    def test_recording_switches_resources_and_groups_jobs_by_pool(self) -> None:
        self.assertIn("static void VK_Shadow_RecordResolutionPool", VK_SHADOW)
        self.assertIn("VK_Shadow_PoolHasJobs(pool_index)", VK_SHADOW)
        self.assertIn("VK_Shadow_CollectMomentPages(pool_index, moment_pages)", VK_SHADOW)
        self.assertIn("job->pool_index != pool_index", VK_SHADOW)
        self.assertIn("vk_shadow.framebuffers[job->pool_layer]", VK_SHADOW)
        self.assertIn("VK_Shadow_InitializeMomentLayouts(cmd);", VK_SHADOW)

    def test_empty_pool_uses_device_capability_formats_not_parked_handles(self) -> None:
        self.assertIn("static VkFormat vk_shadow_preferred_depth_format;", VK_SHADOW)
        self.assertIn("static VkFormat vk_shadow_preferred_moment_format;", VK_SHADOW)
        self.assertIn("VkFormat depth_format = vk_shadow_preferred_depth_format;", VK_SHADOW)
        self.assertIn("VkFormat moment_format = vk_shadow_preferred_moment_format;", VK_SHADOW)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
