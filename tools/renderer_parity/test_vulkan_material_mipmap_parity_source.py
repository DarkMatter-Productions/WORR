#!/usr/bin/env python3
"""Structural coverage for native Vulkan material mipmap generation."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")


class VulkanMaterialMipmapParitySourceTests(unittest.TestCase):
    def test_wall_and_skin_images_allocate_full_native_mip_chains(self) -> None:
        self.assertIn("VK_UI_ImageMipLevelCount", VK_UI)
        self.assertIn("image->type != IT_WALL && image->type != IT_SKIN", VK_UI)
        self.assertIn(".mipLevels = image->mip_levels", VK_UI)
        self.assertIn(".levelCount = image->mip_levels", VK_UI)

    def test_upload_generates_and_synchronizes_native_linear_mips(self) -> None:
        self.assertIn("VK_UI_SupportsLinearMipmapBlit", VK_UI)
        self.assertIn("VK_FORMAT_FEATURE_BLIT_SRC_BIT", VK_UI)
        self.assertIn("VK_FORMAT_FEATURE_BLIT_DST_BIT", VK_UI)
        self.assertIn("VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT", VK_UI)
        self.assertIn("vkCmdBlitImage", VK_UI)
        self.assertIn("VK_FILTER_LINEAR", VK_UI)
        self.assertIn("VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL", VK_UI)
        self.assertIn("VK_UI_TransitionImageMipRange", VK_UI)
        self.assertIn("material sub-rect update requires a full image upload", VK_UI)

    def test_dynamic_lightmap_atlas_stays_single_level(self) -> None:
        self.assertIn('"**world_lightmap**"', VK_WORLD)
        self.assertIn("IT_SPRITE, IF_REPEAT", VK_WORLD)


if __name__ == "__main__":
    unittest.main()
