#!/usr/bin/env python3
"""Headless structural regression checks for FR-01-T12 colour correction."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_POSTPROCESS = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
POSTPROCESS_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_postprocess.frag"
).read_text(encoding="utf-8")


class VulkanColorCorrectionSourceTests(unittest.TestCase):
    def test_vulkan_exclusive_controls_match_opengl_ranges(self) -> None:
        self.assertIn('Cvar_Get("vk_color_correction", "1", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_color_brightness", "0.0", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_color_contrast", "1.0", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_color_saturation", "1.0", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_color_tint", "white", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn("SCR_ParseColor", VK_POSTPROCESS)
        self.assertIn("Cvar_ClampValue(vk_postprocess.color_brightness, -1.0f, 1.0f)", VK_POSTPROCESS)
        self.assertIn("Cvar_ClampValue(vk_postprocess.color_contrast, 0.0f, 4.0f)", VK_POSTPROCESS)
        self.assertIn("Cvar_ClampValue(vk_postprocess.color_saturation, 0.0f, 4.0f)", VK_POSTPROCESS)

    def test_identity_configuration_skips_the_scene_copy_and_fullscreen_draw(self) -> None:
        self.assertIn("vk_postprocess.color_active = correction_enabled &&", VK_POSTPROCESS)
        self.assertIn("fabsf(vk_postprocess.push.brightness) > 0.0001f", VK_POSTPROCESS)
        self.assertIn("vk_postprocess.waterwarp_active || vk_postprocess.color_active ||", VK_POSTPROCESS)
        self.assertIn("bool VK_PostProcess_UsesCompositePass(void)", VK_POSTPROCESS)
        self.assertIn("VK_PostProcess_UsesCompositePass()", VK_MAIN)
        self.assertIn("const bool final_postprocess", VK_MAIN)

    def test_static_hdr_controls_are_native_and_use_the_shared_per_frame_ubo(self) -> None:
        self.assertIn('Cvar_Get("vk_hdr", "0",', VK_POSTPROCESS)
        self.assertIn("CVAR_ARCHIVE | CVAR_RENDERER", VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_hdr_exposure", "1.0", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_hdr_white", "1.0", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_hdr_gamma", "2.2", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn("vk_postprocess.hdr_active = vk_postprocess.hdr &&", VK_POSTPROCESS)
        self.assertIn("Cvar_ClampValue(vk_postprocess.hdr_exposure, 0.0f, 10.0f)", VK_POSTPROCESS)
        self.assertIn("Cvar_ClampValue(vk_postprocess.hdr_white, 0.1f, 20.0f)", VK_POSTPROCESS)
        self.assertIn("Cvar_ClampValue(vk_postprocess.hdr_gamma, 1.0f, 3.0f)", VK_POSTPROCESS)
        self.assertIn("float hdr[4];", VK_POSTPROCESS)
        self.assertIn("memcpy(mapped->hdr, vk_postprocess.hdr_controls", VK_POSTPROCESS)
        self.assertIn("vk_postprocess.hdr_active ||", VK_POSTPROCESS)
        self.assertIn("frame->blur_kernel_descriptor_set", VK_POSTPROCESS)
        self.assertIn("layout(std140, set = 1, binding = 0) uniform PostControls", POSTPROCESS_SHADER)
        self.assertIn("vec4 hdr;", POSTPROCESS_SHADER)
        self.assertIn("if (post_controls.hdr.w > 0.5)", POSTPROCESS_SHADER)
        self.assertIn("float exposure = post_controls.hdr.x;", POSTPROCESS_SHADER)
        self.assertIn("color.rgb *= exposure;", POSTPROCESS_SHADER)
        self.assertIn("vec3 mapped = clamp", POSTPROCESS_SHADER)
        self.assertIn("vec3 white = clamp", POSTPROCESS_SHADER)
        self.assertIn("vec3(1.0 / post_controls.hdr.z)", POSTPROCESS_SHADER)

    def test_auto_exposure_reduces_the_float_scene_natively(self) -> None:
        self.assertIn('Cvar_Get("vk_hdr_auto_exposure", "0", CVAR_ARCHIVE | CVAR_RENDERER)', VK_POSTPROCESS)
        self.assertIn("VK_PostProcess_RecordAutoExposure", VK_POSTPROCESS)
        self.assertIn("VK_PostProcess_UsesAutoExposure()", VK_MAIN)
        self.assertIn("linear_scene_mip_levels", VK_MAIN)
        self.assertIn("vkCmdBlitImage", VK_MAIN)
        self.assertIn("linear_scene_copy_base_descriptor_set", VK_MAIN)
        self.assertIn("VK_LinearSceneCopy_Record(cmd, false)", VK_MAIN)
        self.assertIn("VK_PostProcess_UsesAutoExposure())", VK_MAIN)
        self.assertIn("vec4 auto_exposure;", POSTPROCESS_SHADER)
        self.assertIn("textureLod(scene_sampler, vec2(0.5), max_mip)", POSTPROCESS_SHADER)
        self.assertIn("texture(auto_exposure_sampler, vec2(0.5)).r", POSTPROCESS_SHADER)

    def test_final_pass_uses_native_scene_copy_before_ui_overlay(self) -> None:
        final_record = VK_MAIN.index("VK_PostProcess_RecordFinal(")
        final_copy = VK_MAIN.rindex("VK_SceneCopy_Record(cmd, image_index);", 0, final_record)
        self.assertLess(final_copy, final_record)
        self.assertIn("if (final_postprocess) {", VK_MAIN)
        self.assertIn("VK_PostProcess_RecordFinal", VK_MAIN)
        self.assertIn("vkCmdDraw(cmd, 3, 1, 0, 0)", VK_POSTPROCESS)

    def test_mode_change_rebuilds_native_postprocess_swapchain_resources(self) -> None:
        recreate_start = VK_MAIN.index("static bool VK_RecreateSwapchain")
        recreate_end = VK_MAIN.index("static void VK_Screenshot_DestroyBuffer", recreate_start)
        recreate = VK_MAIN[recreate_start:recreate_end]
        self.assertLess(
            recreate.index("VK_PostProcess_DestroySwapchainResources(ctx);"),
            recreate.index("VK_DestroySwapchain(ctx);"),
        )
        self.assertLess(
            recreate.index("VK_UI_CreateSwapchainResources(ctx)"),
            recreate.index("VK_PostProcess_CreateSwapchainResources(ctx)"),
        )

        mode_start = VK_MAIN.index("void R_ModeChanged")
        mode_end = VK_MAIN.index("bool R_VideoSync", mode_start)
        mode = VK_MAIN[mode_start:mode_end]
        self.assertLess(
            mode.index("VK_PostProcess_DestroySwapchainResources(&vk_state.ctx);"),
            mode.index("VK_DestroySwapchain(&vk_state.ctx);"),
        )
        self.assertLess(
            mode.index("VK_UI_CreateSwapchainResources(&vk_state.ctx)"),
            mode.index("VK_PostProcess_CreateSwapchainResources(&vk_state.ctx)"),
        )

    def test_shader_matches_opengl_color_transform_order(self) -> None:
        self.assertIn("if (push_data.color_enabled > 0.5)", POSTPROCESS_SHADER)
        self.assertIn(
            "color.rgb = (color.rgb - vec3(0.5)) * push_data.contrast + vec3(0.5);",
            POSTPROCESS_SHADER,
        )
        self.assertIn("color.rgb += push_data.brightness;", POSTPROCESS_SHADER)
        self.assertIn("dot(color.rgb, vec3(0.2126, 0.7152, 0.0722))", POSTPROCESS_SHADER)
        self.assertIn("mix(vec3(luma), color.rgb, push_data.saturation)", POSTPROCESS_SHADER)
        self.assertIn("color.rgb *= push_data.tint.rgb;", POSTPROCESS_SHADER)
        self.assertIn("if (push_data.waterwarp > 0.5)", POSTPROCESS_SHADER)
        self.assertNotIn("rend_gl", POSTPROCESS_SHADER)

    def test_split_toning_matches_opengl_order_and_identity_skip(self) -> None:
        self.assertIn(
            'Cvar_Get("vk_color_split_shadows", "white", CVAR_ARCHIVE)',
            VK_POSTPROCESS,
        )
        self.assertIn(
            'Cvar_Get("vk_color_split_highlights", "white", CVAR_ARCHIVE)',
            VK_POSTPROCESS,
        )
        self.assertIn(
            'Cvar_Get("vk_color_split_strength", "0.0", CVAR_ARCHIVE)',
            VK_POSTPROCESS,
        )
        self.assertIn(
            'Cvar_Get("vk_color_split_balance", "0.0", CVAR_ARCHIVE)',
            VK_POSTPROCESS,
        )
        self.assertIn("VK_PostProcess_ColorSplitShadowsChanged", VK_POSTPROCESS)
        self.assertIn("VK_PostProcess_ColorSplitHighlightsChanged", VK_POSTPROCESS)
        self.assertIn("vk_postprocess.split_active =", VK_POSTPROCESS)
        self.assertIn("vk_postprocess.push.split_params[0] > 0.0001f", VK_POSTPROCESS)
        self.assertIn("if (push_data.split_params.x > 0.0)", POSTPROCESS_SHADER)
        self.assertIn("float pivot = 0.5 + balance * 0.5;", POSTPROCESS_SHADER)
        self.assertIn("smoothstep(pivot - 0.25, pivot + 0.25, luma)", POSTPROCESS_SHADER)
        self.assertIn("color.rgb * push_data.split_shadow.rgb", POSTPROCESS_SHADER)
        self.assertIn("color.rgb * push_data.split_highlight.rgb", POSTPROCESS_SHADER)
        self.assertIn("color.rgb = mix(color.rgb, toned, push_data.split_params.x);", POSTPROCESS_SHADER)

    def test_lut_grading_uses_native_combined_scene_descriptor(self) -> None:
        self.assertIn('Cvar_Get("vk_color_lut", "", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn('Cvar_Get("vk_color_lut_intensity", "1.0", CVAR_ARCHIVE)', VK_POSTPROCESS)
        self.assertIn("VK_PostProcess_ColorLutChanged", VK_POSTPROCESS)
        self.assertIn("VK_UI_RegisterImage(", VK_POSTPROCESS)
        self.assertIn("IF_PERMANENT | IF_EXACT | IF_NO_COLOR_ADJUST", VK_POSTPROCESS)
        self.assertIn("width == height * height", VK_POSTPROCESS)
        self.assertIn("height == width * width", VK_POSTPROCESS)
        self.assertIn("VK_UI_CreateExternalImageTripleDescriptor", VK_POSTPROCESS)
        self.assertIn("VkDescriptorSet VK_UI_CreateExternalImageTripleDescriptor", VK_UI)
        self.assertIn(".imageView = first_view", VK_UI)
        self.assertIn(".imageView = second_view", VK_UI)
        self.assertIn(".imageView = third_view", VK_UI)
        self.assertIn(".dstBinding = 2", VK_UI)
        self.assertIn("vk_postprocess.lut_active", VK_POSTPROCESS)
        self.assertIn("layout(set = 0, binding = 1) uniform sampler2D lut_sampler", POSTPROCESS_SHADER)
        self.assertIn("if (push_data.lut_params.x > 0.0", POSTPROCESS_SHADER)
        self.assertIn("float slice = lut_color.b * (size - 1.0);", POSTPROCESS_SHADER)
        self.assertIn(
            "float u = lut_color.r * (size - 1.0) + 0.5;",
            POSTPROCESS_SHADER,
        )
        self.assertIn(
            "float v = lut_color.g * (size - 1.0) + 0.5;",
            POSTPROCESS_SHADER,
        )
        self.assertNotIn(
            "float u = (lut_color.r * (size - 1.0) + 0.5) / size;",
            POSTPROCESS_SHADER,
        )
        self.assertIn("texture(lut_sampler, uv0).rgb", POSTPROCESS_SHADER)
        self.assertIn("texture(lut_sampler, uv1).rgb", POSTPROCESS_SHADER)
        self.assertIn("color.rgb = mix(color.rgb, graded, push_data.lut_params.x);", POSTPROCESS_SHADER)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_POSTPROCESS)
        self.assertNotIn('#include "rend_gl', VK_MAIN)


if __name__ == "__main__":
    unittest.main()
