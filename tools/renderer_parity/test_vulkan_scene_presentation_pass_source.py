#!/usr/bin/env python3
"""Structural guard for the native scene/presentation pass split."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOCAL = (ROOT / "src/rend_vk/vk_local.h").read_text(encoding="utf-8")
MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
POST = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")


class VulkanScenePresentationPassSourceTests(unittest.TestCase):
    def test_pipeline_families_are_explicit(self) -> None:
        for name in (
            "scene_render_pass",
            "scene_load_render_pass",
            "presentation_render_pass",
            "presentation_overlay_render_pass",
            "presentation_load_render_pass",
        ):
            self.assertIn(name, LOCAL)
        self.assertIn("ctx->scene_render_pass", WORLD)
        self.assertIn("ctx->scene_render_pass", ENTITY)
        self.assertIn("ctx->presentation_render_pass", UI)
        self.assertIn("ctx->presentation_load_render_pass", POST)

    def test_recording_routes_scene_and_output_to_their_own_passes(self) -> None:
        self.assertIn("liquid_info.renderPass = ctx->scene_single_sample_active", MAIN)
        self.assertIn("? ctx->scene_single_sample_load_render_pass", MAIN)
        self.assertIn(": ctx->scene_load_render_pass", MAIN)
        self.assertIn("final_info.renderPass = ctx->presentation_load_render_pass", MAIN)
        self.assertIn("crt_info.renderPass = ctx->presentation_load_render_pass", MAIN)
        self.assertIn(
            "ui_overlay_info.renderPass = ctx->presentation_overlay_render_pass",
            MAIN,
        )

    def test_float_scene_capability_is_native_and_explicit(self) -> None:
        self.assertIn("linear_scene_format", LOCAL)
        self.assertIn("linear_scene_supported", LOCAL)
        self.assertIn("VK_FORMAT_R16G16B16A16_SFLOAT", MAIN)
        self.assertIn("VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT", MAIN)
        self.assertIn("VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT", MAIN)
        self.assertIn("VK_FORMAT_FEATURE_TRANSFER_SRC_BIT", MAIN)
        self.assertIn("VK_FORMAT_FEATURE_TRANSFER_DST_BIT", MAIN)
        self.assertIn("VK_CreateLinearSceneResources", MAIN)
        self.assertIn("VK_DestroyLinearSceneResources", MAIN)
        self.assertIn("linear_scene_copy_descriptor_set", MAIN)

    def test_float_graph_preserves_range_until_presentation(self) -> None:
        self.assertIn("VK_LinearSceneCopy_Record", MAIN)
        self.assertIn("VK_PresentationImageToColorAttachment", MAIN)
        self.assertIn("image_presented", LOCAL)
        self.assertIn("CVAR_ARCHIVE | CVAR_RENDERER", MAIN)
        self.assertIn("VK_PostProcess_WorkingFormat", POST)
        self.assertIn(".format = VK_PostProcess_WorkingFormat()", POST)
        self.assertIn("const VkFormat bloom_format", MAIN)

    def test_plain_float_hdr_presents_the_scene_without_a_copy(self) -> None:
        self.assertIn("VK_LinearSceneDirectToShaderRead", MAIN)
        self.assertIn("VK_LinearSceneRestoreColorAttachment", MAIN)
        self.assertIn("direct_linear_presentation", MAIN)
        self.assertIn("!VK_PostProcess_RequiresSceneCopy()", MAIN)
        self.assertLess(
            MAIN.index("if (direct_linear_presentation)"),
            MAIN.index("} else if (linear_scene)"),
        )
        self.assertIn("direct_scene_kernel_descriptor_set", POST)
        self.assertIn("VK_PostProcess_UpdateDirectSceneKernelDescriptors", POST)
        self.assertIn("bool direct_linear_scene", POST)

    def test_resolution_scaling_uses_native_scene_targets(self) -> None:
        self.assertIn("VK_UpdateResolutionScale", MAIN)
        self.assertIn("r_resolutionscale_fixedscale_w", MAIN)
        self.assertIn("r_resolutionscale_targetdrawtime", MAIN)
        self.assertIn("scene_extent", LOCAL)
        self.assertIn("scene_offscreen_supported", LOCAL)
        self.assertIn("VK_ResolutionScaleExtent", MAIN)
        self.assertIn("ctx->scene_extent", MAIN)
        self.assertIn("VK_LinearSceneDirectToShaderRead", MAIN)
        self.assertIn("ctx->scene_extent", POST)

    def test_simple_scaled_ldr_scene_uses_guarded_native_blit(self) -> None:
        self.assertIn("scaled_scene_blit_supported", LOCAL)
        self.assertIn("VK_IMAGE_USAGE_TRANSFER_DST_BIT", MAIN)
        self.assertIn("VK_FORMAT_FEATURE_BLIT_SRC_BIT", MAIN)
        self.assertIn("VK_FORMAT_FEATURE_BLIT_DST_BIT", MAIN)
        self.assertIn("VK_BlitScaledSceneToPresentation", MAIN)
        self.assertIn("vkCmdBlitImage", MAIN)
        self.assertIn("VK_PostProcess_AllowsScaledSceneBlit", MAIN)
        self.assertIn("!liquid_refraction", MAIN)
        self.assertIn("!vk_postprocess.hdr_active", POST)
        self.assertIn("!vk_postprocess.crt_active", POST)

    def test_adaptive_scale_prefers_completed_native_gpu_timestamps(self) -> None:
        self.assertIn("VK_Debug_GetLastGpuFrameTime", MAIN)
        self.assertIn("VK_Debug_GpuTimingSupported", MAIN)
        self.assertIn("vk_resolutionscale_last_gpu_sample_id", MAIN)
        self.assertIn("mixing CPU submission time into the adaptive GPU controller", MAIN)

    def test_scaled_offscreen_extent_refresh_keeps_presentation_resources(self) -> None:
        self.assertIn("VK_RecreateSceneTargets", MAIN)
        self.assertIn("VK_CreateLinearSceneFramebuffers", MAIN)
        self.assertIn("VK_DestroyLinearSceneResources(ctx)", MAIN)
        self.assertIn("VK_DestroyBloomEmissionImages(ctx)", MAIN)
        self.assertIn("VK_WaitForSubmittedFrames(", MAIN)
        self.assertIn("VK_PostProcess_RefreshSceneResources(ctx)", MAIN)
        self.assertIn("VK_PostProcess_RefreshSceneResources", POST)
        refresh_start = MAIN.index("static bool VK_RecreateSceneTargets")
        refresh_end = MAIN.index("static bool VK_DrawFrame", refresh_start)
        refresh = MAIN[refresh_start:refresh_end]
        self.assertNotIn("VK_DestroySwapchain(ctx)", refresh)
        self.assertNotIn("VK_CreateSwapchain(", refresh)
        self.assertNotIn("VK_DestroyBloomEmissionResources(ctx)", refresh)


if __name__ == "__main__":
    unittest.main()
