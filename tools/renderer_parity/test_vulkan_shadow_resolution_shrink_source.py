#!/usr/bin/env python3
"""Structural guardrails for native Vulkan shadow-resolution pools."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")


class VulkanShadowResolutionPoolsSourceTests(unittest.TestCase):
    def test_each_pool_is_exact_resolution_with_completed_local_demand(self) -> None:
        self.assertIn("static int VK_Shadow_ResolutionForPool", VK_SHADOW)
        self.assertIn("vk_shadow.resolution == resolution", VK_SHADOW)
        self.assertIn("vk_shadow_pool_frame_required_pages", VK_SHADOW)
        self.assertIn("vk_shadow_pool_completed_required_pages", VK_SHADOW)
        self.assertIn("memcpy(vk_shadow_pool_completed_required_pages,", VK_SHADOW)

    def test_local_low_water_capacity_is_hysteretic_but_growth_stays_immediate(self) -> None:
        self.assertIn("static bool VK_Shadow_RequestCapacityShrink(uint32_t pool_index,", VK_SHADOW)
        self.assertIn("vk_shadow_pool_shrink_target_capacity[pool_index]", VK_SHADOW)
        self.assertIn("vk_shadow_pool_shrink_stable_frames[pool_index]", VK_SHADOW)
        self.assertIn("VK_Shadow_ShrinkDelay();", VK_SHADOW)
        ensure = VK_SHADOW.split("static bool VK_Shadow_EnsureResources", 1)[1].split(
            "static bool VK_Shadow_EnsureCpuCapacity", 1
        )[0]
        self.assertIn("bool permit_capacity_shrink,", ensure)
        self.assertIn("const uint32_t current_capacity = permit_capacity_shrink", ensure)
        self.assertIn("vk_shadow.resolution == resolution", ensure)

    def test_begin_frame_reclaims_only_the_affected_pool(self) -> None:
        begin = VK_SHADOW.split("void VK_Shadow_BeginFrame", 1)[1].split(
            "bool VK_Shadow_EnsurePage", 1
        )[0]
        self.assertIn("for (uint32_t pool_index = 0;", begin)
        self.assertIn("VK_Shadow_RequestCapacityShrink(", begin)
        self.assertIn("vk_shadow_pool_completed_required_pages[pool_index]", begin)
        self.assertIn("VK_Shadow_ResolutionForPool(pool_index)", begin)
        self.assertIn("vk_shadow_pool_shrink_target_capacity[pool_index]", begin)

    def test_idle_base_pool_remains_a_one_page_descriptor_fallback(self) -> None:
        begin = VK_SHADOW.split("void VK_Shadow_BeginFrame", 1)[1].split(
            "bool VK_Shadow_EnsurePage", 1
        )[0]
        self.assertIn("if (pool_index == 0 && vk_shadow.resources_ok)", begin)
        self.assertIn("vk_shadow.storage_family, 1, true", begin)
        self.assertIn("vk_shadow.last_shrink_to_capacity = 1;", begin)

    def test_dump_keeps_runtime_reclamation_evidence(self) -> None:
        self.assertIn("pool-shrinks=%u last-pool-shrink=%u>%u", VK_SHADOW)

    def test_headless_hook_uses_the_public_cvar_after_live_frames(self) -> None:
        self.assertIn("vk_shadow_test_sun_resolution_drop_after_frames", VK_SHADOW)
        self.assertIn("static void VK_Shadow_RunTestHooks", VK_SHADOW)
        self.assertIn("vk_shadow.test_sun_resolution_drop_frames++", VK_SHADOW)
        self.assertIn('Cvar_Set("r_shadow_sun_resolution", "64");', VK_SHADOW)
        self.assertIn("VK_Shadow_RunTestHooks();", VK_SHADOW)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
