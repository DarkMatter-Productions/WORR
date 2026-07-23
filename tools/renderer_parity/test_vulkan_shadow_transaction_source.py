#!/usr/bin/env python3
"""Structural guardrails for transactional Vulkan shadow resource replacement."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")


class VulkanShadowTransactionSourceTests(unittest.TestCase):
    def test_resource_bundle_contains_all_replacement_owned_handles(self) -> None:
        self.assertIn("typedef struct {\n    bool resources_ok;", VK_SHADOW)
        self.assertIn("} vk_shadow_resources_t;", VK_SHADOW)
        for member in (
            "VkImage image;",
            "VkImage moment_image;",
            "VkSampler compare_sampler;",
            "VkPipeline alpha_pipeline;",
            "VkImageView layer_views[VK_SHADOW_MAX_PAGES];",
            "VkFramebuffer framebuffers[VK_SHADOW_MAX_PAGES];",
        ):
            self.assertIn(member, VK_SHADOW)
        self.assertIn("static void VK_Shadow_SwapResources", VK_SHADOW)

    def test_failed_build_restores_the_previous_resource_bundle(self) -> None:
        self.assertIn("static vk_shadow_resources_t *vk_shadow_rollback_resources;", VK_SHADOW)
        ensure = VK_SHADOW.split("static bool VK_Shadow_EnsureResources", 1)[1].split(
            "static bool VK_Shadow_EnsureCpuCapacity", 1
        )[0]
        self.assertIn("vk_shadow_resources_t previous = {0};", ensure)
        self.assertIn("const bool had_previous_resources = vk_shadow.resources_ok;", ensure)
        self.assertIn("VK_Shadow_SwapResources(&previous);", ensure)
        self.assertIn("vk_shadow_rollback_resources = &previous;", ensure)
        self.assertIn("vk_shadow_rollback_resources = NULL;", ensure)
        self.assertGreaterEqual(ensure.count("VK_Shadow_SwapResources(&previous);"), 3)

        destroy = VK_SHADOW.split("static void VK_Shadow_DestroyResources", 1)[1].split(
            "static bool VK_Shadow_CreateRenderPass", 1
        )[0]
        self.assertIn("if (vk_shadow_rollback_resources)", destroy)
        self.assertIn("VK_Shadow_UpdateDescriptorSet();", destroy)
        self.assertIn("vk_shadow_rollback_resources = NULL;", destroy)

    def test_test_only_fault_hook_runs_only_after_a_live_resource_set_is_saved(self) -> None:
        self.assertIn('Cvar_Get("vk_shadow_test_fail_recreate",', VK_SHADOW)
        ensure = VK_SHADOW.split("static bool VK_Shadow_EnsureResources", 1)[1].split(
            "static bool VK_Shadow_EnsureCpuCapacity", 1
        )[0]
        self.assertIn("if (had_previous_resources) {", ensure)
        self.assertIn("vk_shadow_test_fail_recreate->integer", ensure)
        self.assertIn("test-injected replacement failure; restoring prior resources", ensure)
        self.assertIn('Cvar_SetByVar(vk_shadow_test_fail_recreate, "0", FROM_CODE);', ensure)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_SHADOW)


if __name__ == "__main__":
    unittest.main()
