#!/usr/bin/env python3
"""Structural checks for native Vulkan active shadow-array capacity."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FRONTEND_HEADER = (ROOT / "inc/renderer/shadow_frontend.h").read_text(
    encoding="utf-8"
)
FRONTEND = (ROOT / "src/renderer/shadow_frontend.c").read_text(encoding="utf-8")
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")


class VulkanShadowActiveCapacitySourceTests(unittest.TestCase):
    def test_frontend_supplies_full_view_set_page_requirement_before_recording(self) -> None:
        self.assertIn("uint32_t required_page_count", FRONTEND_HEADER)
        self.assertIn("uint32_t required_page_count = 1;", FRONTEND)
        self.assertIn(
            "required_page_count = max(required_page_count, view->page.index + 1);",
            FRONTEND,
        )
        self.assertIn(
            "backend->begin_frame(backend->userdata, policy, required_page_count);",
            FRONTEND,
        )

    def test_vulkan_uses_geometric_page_capacity_buckets(self) -> None:
        self.assertIn("uint32_t page_capacity;", VK_SHADOW)
        self.assertIn("static bool VK_Shadow_GrowPageCapacity", VK_SHADOW)
        self.assertIn("uint32_t capacity = current ? current : 1;", VK_SHADOW)
        self.assertIn("while (capacity < needed)", VK_SHADOW)
        self.assertIn("capacity *= 2u;", VK_SHADOW)
        self.assertIn("needed > VK_SHADOW_MAX_PAGES", VK_SHADOW)

    def test_images_views_and_framebuffers_are_bounded_to_capacity(self) -> None:
        self.assertNotIn(".arrayLayers = VK_SHADOW_MAX_PAGES", VK_SHADOW)
        self.assertIn(".arrayLayers = page_capacity,", VK_SHADOW)
        self.assertIn(".layerCount = page_capacity,", VK_SHADOW)
        self.assertIn("for (uint32_t i = 0; i < page_capacity; i++)", VK_SHADOW)
        self.assertIn(".layerCount = vk_shadow.page_capacity,", VK_SHADOW)

    def test_page_mapping_uses_compact_local_capacity_not_global_page_ids(self) -> None:
        self.assertIn(
            "VK_Shadow_EnsurePageMapping(view, pool_index, &pool_layer,",
            VK_SHADOW,
        )
        self.assertIn(
            "uint32_t requested_pages = pool_layer + 1;",
            VK_SHADOW,
        )
        self.assertIn(
            "vk_shadow_pool_frame_required_pages[pool_index] = max(",
            VK_SHADOW,
        )

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
