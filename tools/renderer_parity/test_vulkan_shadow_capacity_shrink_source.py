#!/usr/bin/env python3
"""Structural checks for delayed native Vulkan shadow-array shrinking."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")


class VulkanShadowCapacityShrinkSourceTests(unittest.TestCase):
    def test_shrink_uses_a_delayed_smaller_geometric_bucket(self) -> None:
        self.assertIn("VK_SHADOW_DEFAULT_SHRINK_FRAMES 180u", VK_SHADOW)
        self.assertIn("vk_shadow_pool_shrink_target_capacity", VK_SHADOW)
        self.assertIn("vk_shadow_pool_shrink_stable_frames", VK_SHADOW)
        self.assertIn("uint32_t shrink_count;", VK_SHADOW)
        self.assertIn('Cvar_Get("vk_shadow_shrink_frames", "180",', VK_SHADOW)
        self.assertIn(
            "static bool VK_Shadow_RequestCapacityShrink(uint32_t pool_index,",
            VK_SHADOW,
        )
        self.assertIn("VK_Shadow_GrowPageCapacity(0, required_pages, &target_capacity)", VK_SHADOW)
        self.assertIn(
            "vk_shadow_pool_shrink_stable_frames[pool_index] >=",
            VK_SHADOW,
        )

    def test_recreation_is_deferred_to_begin_frame_and_reuses_existing_transaction(self) -> None:
        begin = VK_SHADOW.split("void VK_Shadow_BeginFrame", 1)[1].split(
            "bool VK_Shadow_EnsurePage", 1
        )[0]
        self.assertIn("VK_Shadow_RequestCapacityShrink(", begin)
        self.assertIn("vk_shadow_pool_completed_required_pages[pool_index]", begin)
        self.assertIn("const uint32_t target_capacity =", begin)
        self.assertIn("VK_Shadow_ResolutionForPool(pool_index)", begin)
        self.assertIn("bool permit_capacity_shrink,", VK_SHADOW)
        self.assertIn("const uint32_t current_capacity = permit_capacity_shrink", VK_SHADOW)
        self.assertIn("vk_shadow.page_capacity == page_capacity", VK_SHADOW)
        self.assertIn("vk_shadow.last_shrink_from_capacity = previous_capacity;", VK_SHADOW)
        self.assertIn("vk_shadow.last_shrink_to_capacity = target_capacity;", VK_SHADOW)

    def test_runtime_dump_retains_capacity_transition_evidence(self) -> None:
        self.assertIn("pool-shrinks=%u last-pool-shrink=%u>%u", VK_SHADOW)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
