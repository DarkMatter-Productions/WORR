#!/usr/bin/env python3
"""Headless structural regression checks for FR-01-T10 liquid refraction."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
WORLD_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_world_shadow.frag"
).read_text(encoding="utf-8")
VK_POSTPROCESS = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")
POSTPROCESS_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_postprocess.frag"
).read_text(encoding="utf-8")


class VulkanLiquidRefractionSourceTests(unittest.TestCase):
    def test_scene_copy_is_native_vulkan(self) -> None:
        self.assertIn("VK_CreateLiquidSceneResources", VK_MAIN)
        self.assertIn(
            "VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT", VK_MAIN
        )
        self.assertIn("VK_SceneCopy_Record", VK_MAIN)
        self.assertIn("vkCmdCopyImage", VK_MAIN)
        self.assertNotIn("rend_gl", VK_MAIN)

    def test_liquid_pass_loads_color_and_depth_after_the_copy(self) -> None:
        self.assertIn("VK_ATTACHMENT_LOAD_OP_LOAD", VK_MAIN)
        # The load pass now targets either the LDR swapchain scene or the
        # native float scene, so its name describes that shared ownership.
        self.assertIn("ctx->scene_load_render_pass", VK_MAIN)
        copy_index = VK_MAIN.index("VK_SceneCopy_Record(cmd, image_index)")
        alpha_index = VK_MAIN.index(
            "VK_World_RecordAlpha(cmd, &ctx->scene_extent,", copy_index
        )
        self.assertLess(copy_index, alpha_index)
        self.assertIn("VK_ATTACHMENT_STORE_OP_STORE", VK_MAIN)
        self.assertIn("liquid_render_pass_info.pDependencies = dependencies", VK_MAIN)
        self.assertIn("VK_DepthAttachmentBarrier(cmd, frame)", VK_MAIN)
        self.assertIn("VK_IMAGE_ASPECT_STENCIL_BIT", VK_MAIN)

    def test_world_shader_uses_scene_sampler_with_alpha_compensation(self) -> None:
        self.assertIn("layout(set = 3, binding = 0) uniform sampler2D refract_sampler", WORLD_SHADER)
        self.assertIn("dFdx(in_uv)", WORLD_SHADER)
        self.assertIn("scene_refracted", WORLD_SHADER)
        self.assertIn("scene_base * (1.0 - alpha)", WORLD_SHADER)

    def test_world_submission_is_split_without_opengl_fallback(self) -> None:
        self.assertIn("void VK_World_RecordOpaque", VK_WORLD)
        self.assertIn("void VK_World_RecordAlpha", VK_WORLD)
        self.assertIn("bool VK_World_HasTransparentBatches", VK_WORLD)
        self.assertIn("bool VK_World_UsesRefraction", VK_WORLD)
        self.assertIn('Cvar_Get("vk_warp_refraction", "0.1", 0)', VK_WORLD)
        self.assertNotIn('#include "rend_gl', VK_WORLD)

    def test_underwater_waterwarp_is_a_native_fullscreen_pass(self) -> None:
        self.assertIn('Cvar_Get("vk_waterwarp", "1", 0)', VK_POSTPROCESS)
        self.assertIn("RDF_UNDERWATER", VK_POSTPROCESS)
        self.assertIn("VK_PostProcess_RecordFinal", VK_MAIN)
        self.assertIn("vkCmdDraw(cmd, 3, 1, 0, 0)", VK_POSTPROCESS)
        self.assertIn("gl_FragCoord.xy", POSTPROCESS_SHADER)
        self.assertIn("vec2 warp_tc = vec2(tc.x, 1.0 - tc.y)", POSTPROCESS_SHADER)
        self.assertIn("warp_tc.yx * 31.41592653589793 + push_data.time", POSTPROCESS_SHADER)
        self.assertIn("tc += warp_offset * vec2(1.0, -1.0)", POSTPROCESS_SHADER)
        self.assertNotIn('#include "rend_gl', VK_POSTPROCESS)

    def test_entity_alpha_phases_straddle_transparent_world_liquid(self) -> None:
        self.assertIn("VK_ENTITY_SUBMIT_ALPHA_BACK", VK_ENTITY)
        self.assertIn("VK_ENTITY_SUBMIT_ALPHA_FRONT", VK_ENTITY)
        self.assertIn('Cvar_Get("vk_draworder", "1", 0)', VK_ENTITY)
        self.assertIn("void VK_Entity_RecordBeforeLiquid", VK_ENTITY)
        self.assertIn("void VK_Entity_RecordAfterLiquid", VK_ENTITY)
        before_index = VK_MAIN.index("VK_Entity_RecordBeforeLiquid(cmd, &ctx->scene_extent)")
        copy_index = VK_MAIN.index("VK_SceneCopy_Record(cmd, image_index)")
        alpha_index = VK_MAIN.index(
            "VK_World_RecordAlpha(cmd, &ctx->scene_extent,", copy_index
        )
        after_index = VK_MAIN.index(
            "VK_Entity_RecordAfterLiquid(cmd, &ctx->scene_extent)", alpha_index
        )
        self.assertLess(before_index, copy_index)
        self.assertLess(copy_index, alpha_index)
        self.assertLess(alpha_index, after_index)

    def test_non_refractive_transparency_keeps_the_same_phase_boundary(self) -> None:
        self.assertIn(
            "VK_World_RecordAlpha(cmd, &ctx->scene_extent, VK_NULL_HANDLE);",
            VK_MAIN,
        )
        self.assertIn(
            "if (draw_entities && transparent_world) {\n"
            "            VK_Entity_RecordAfterLiquid(cmd, &ctx->scene_extent);",
            VK_MAIN,
        )
        self.assertIn(
            "const bool transparent_world = draw_world &&\n"
            "        VK_World_HasTransparentBatches();",
            VK_MAIN,
        )
        self.assertIn(
            "Preserve OpenGL's transparent sequence even when liquid refraction\n"
            "        // is disabled",
            VK_MAIN,
        )


if __name__ == "__main__":
    unittest.main()
