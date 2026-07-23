#!/usr/bin/env python3
"""Regression checks for the shared minimum resolution-scale contract."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")


class VulkanResolutionScaleLimitSourceTests(unittest.TestCase):
    def test_vulkan_keeps_the_shared_ten_percent_lower_bound(self) -> None:
        self.assertIn("#define RESOLUTION_SCALE_MIN 0.1f", GL_MAIN)
        self.assertIn("#define VK_RESOLUTION_SCALE_MIN 0.1f", VK_MAIN)
        self.assertIn(
            "Cvar_ClampValue(vk_resolutionscale_fixedscale_w,\n"
            "                        VK_RESOLUTION_SCALE_MIN, VK_RESOLUTION_SCALE_MAX)",
            VK_MAIN,
        )
        self.assertIn(
            "Cvar_ClampValue(vk_resolutionscale_fixedscale_h,\n"
            "                        VK_RESOLUTION_SCALE_MIN, VK_RESOLUTION_SCALE_MAX)",
            VK_MAIN,
        )

    def test_low_scale_extent_remains_a_real_native_offscreen_target(self) -> None:
        self.assertIn("static VkExtent2D VK_ResolutionScaleExtent", VK_MAIN)
        self.assertIn("max(\n          1u", VK_MAIN)
        self.assertIn("output_extent.width * vk_resolutionscale_current_w", VK_MAIN)
        self.assertIn("output_extent.height * vk_resolutionscale_current_h", VK_MAIN)


if __name__ == "__main__":
    unittest.main()
