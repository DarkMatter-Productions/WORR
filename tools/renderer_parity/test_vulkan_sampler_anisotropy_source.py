#!/usr/bin/env python3
"""Guard native Vulkan material anisotropy capability and live sampler updates."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_LOCAL = (ROOT / "src/rend_vk/vk_local.h").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")


class VulkanSamplerAnisotropySourceTests(unittest.TestCase):
    def test_logical_device_enables_only_supported_sampler_anisotropy(self) -> None:
        self.assertIn("bool sampler_anisotropy_supported;", VK_LOCAL)
        self.assertIn("float max_sampler_anisotropy;", VK_LOCAL)
        self.assertIn("vkGetPhysicalDeviceFeatures(picked, &features);", VK_MAIN)
        self.assertIn("features.samplerAnisotropy == VK_TRUE", VK_MAIN)
        self.assertIn("enabled_features.samplerAnisotropy = VK_TRUE;", VK_MAIN)
        self.assertIn(".pEnabledFeatures = &enabled_features,", VK_MAIN)

    def test_native_material_samplers_default_to_device_limit_and_rebind(self) -> None:
        self.assertIn('Cvar_Get("vk_anisotropy", va("%g", default_anisotropy)', VK_UI)
        self.assertIn('Cvar_Get("r_anisotropy", vk_anisotropy->string,', VK_UI)
        self.assertIn("static float VK_UI_Anisotropy(void)", VK_UI)
        self.assertIn("return Cvar_ClampValue(r_anisotropy, 1.0f,", VK_UI)
        self.assertIn("static void VK_UI_RegisterAnisotropyCvars", VK_UI)
        self.assertIn(".anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE,", VK_UI)
        self.assertIn(".maxAnisotropy = anisotropy,", VK_UI)
        self.assertIn("static void VK_UI_AnisotropyChanged", VK_UI)
        self.assertIn("vkDeviceWaitIdle(anisotropy update)", VK_UI)
        self.assertIn("VK_UI_CreateSamplers(vk_ui.ctx, &repeat, &clamp,", VK_UI)
        self.assertIn("VK_UI_RebindImageDescriptors();", VK_UI)

    def test_unsupported_devices_keep_one_x_safe_fallback(self) -> None:
        self.assertIn("!vk_ui.ctx->sampler_anisotropy_supported", VK_UI)
        self.assertIn("return 1.0f;", VK_UI)


if __name__ == "__main__":
    unittest.main()
