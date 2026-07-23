#!/usr/bin/env python3
"""Structural guardrails for legal native Vulkan shadow samplers."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")


class VulkanShadowSamplerCapabilitySourceTests(unittest.TestCase):
    def test_format_selection_prefers_linear_sampling_but_keeps_a_legal_fallback(self) -> None:
        self.assertIn("static bool VK_Shadow_FormatSupportsLinearFiltering", VK_SHADOW)
        self.assertIn("bool depth_linear_filtering;", VK_SHADOW)
        self.assertIn("bool moment_linear_filtering;", VK_SHADOW)
        self.assertIn("VK_Shadow_ChooseDepthFormat(bool *linear_filtering)", VK_SHADOW)
        self.assertIn("VK_Shadow_ChooseMomentFormat(bool *linear_filtering)", VK_SHADOW)
        self.assertGreaterEqual(
            VK_SHADOW.count("VK_Shadow_FormatSupportsLinearFiltering(candidates[i])"),
            2,
        )
        self.assertIn("rather than creating an unsupported linear sampler", VK_SHADOW)

    def test_sampler_filter_and_mip_mode_follow_selected_capabilities(self) -> None:
        self.assertIn("const VkFilter depth_filter = vk_shadow.depth_linear_filtering", VK_SHADOW)
        self.assertIn(".magFilter = depth_filter,", VK_SHADOW)
        self.assertIn(".minFilter = depth_filter,", VK_SHADOW)
        self.assertIn("const VkFilter moment_filter = vk_shadow.moment_linear_filtering", VK_SHADOW)
        self.assertIn("moment_sampler_info.magFilter = moment_filter;", VK_SHADOW)
        self.assertIn("moment_sampler_info.minFilter = moment_filter;", VK_SHADOW)
        self.assertIn("moment_sampler_info.mipmapMode = vk_shadow.moment_mips_supported", VK_SHADOW)

    def test_runtime_dump_exposes_realized_filter_path_and_no_opengl_route(self) -> None:
        self.assertIn("depth-filter=%s moment-filter=%s", VK_SHADOW)
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
