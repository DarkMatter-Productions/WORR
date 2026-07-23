#!/usr/bin/env python3
"""Structural regression checks for Vulkan active-page moment mip work."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")


def function_body(name: str, next_name: str) -> str:
    start = VK_SHADOW.index(name)
    end = VK_SHADOW.index(next_name, start + len(name))
    return VK_SHADOW[start:end]


class VulkanShadowActiveMipsSourceTests(unittest.TestCase):
    def test_moment_descriptor_layout_is_initialized_once(self) -> None:
        self.assertIn(
            "bool moment_layout_initialized;",
            VK_SHADOW,
        )
        self.assertIn(
            "static void VK_Shadow_InitializeMomentLayouts",
            VK_SHADOW,
        )
        self.assertIn(
            "if (!vk_shadow.moment_image || vk_shadow.moment_layout_initialized)",
            VK_SHADOW,
        )
        self.assertIn(
            "vk_shadow.moment_layout_initialized = true;",
            VK_SHADOW,
        )

    def test_moment_jobs_are_deduplicated_before_recording(self) -> None:
        collect = function_body(
            "static uint32_t VK_Shadow_CollectMomentPages",
            "static void VK_Shadow_MomentBarrierPages",
        )
        self.assertIn("bool seen[VK_SHADOW_MAX_PAGES] = { false };", collect)
        self.assertIn("job->pool_index != pool_index", collect)
        self.assertIn("seen[job->pool_layer]", collect)
        self.assertIn("pages[page_count++] = job->pool_layer;", collect)

    def test_barriers_and_blits_cover_only_active_page_layers(self) -> None:
        mips = function_body(
            "static void VK_Shadow_GenerateMomentMips",
            "void VK_Shadow_Record",
        )
        self.assertIn("const uint32_t *pages,", mips)
        self.assertIn("uint32_t page_count", mips)
        self.assertIn(".baseArrayLayer = pages[i]", mips)
        self.assertIn(".layerCount = 1,", mips)
        self.assertIn("page_count, blits, VK_FILTER_LINEAR", mips)
        self.assertNotIn(".layerCount = VK_SHADOW_MAX_PAGES", mips)

    def test_record_uses_the_active_page_set_for_mip_generation(self) -> None:
        record = function_body(
            "static void VK_Shadow_RecordResolutionPool",
            "void VK_Shadow_Record(VkCommandBuffer cmd)",
        )
        self.assertIn(
            "VK_Shadow_CollectMomentPages(pool_index, moment_pages)", record
        )
        self.assertIn(
            "VK_Shadow_GenerateMomentMips(cmd, moment_pages, moment_page_count);",
            record,
        )
        self.assertIn("VK_Shadow_InitializeMomentLayouts(cmd);", record)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
