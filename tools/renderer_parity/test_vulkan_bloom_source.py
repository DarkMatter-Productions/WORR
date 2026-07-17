#!/usr/bin/env python3
"""Headless structural checks for the native Vulkan bloom baseline."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_POST = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
WORLD_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_world_shadow.frag"
).read_text(encoding="utf-8")
ENTITY_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_entity.frag"
).read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
POST_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_postprocess.frag"
).read_text(encoding="utf-8")
BLOOM_SHADER = (ROOT / "src/rend_vk/shaders/vk_bloom.frag").read_text(
    encoding="utf-8"
)


class VulkanBloomSourceTests(unittest.TestCase):
    def test_native_controls_cover_opengl_baseline_parameters(self) -> None:
        for name in (
            "vk_bloom",
            "vk_bloom_iterations",
            "vk_bloom_levels",
            "vk_bloom_downscale",
            "vk_bloom_firefly",
            "vk_bloom_sigma",
            "vk_bloom_threshold",
            "vk_bloom_knee",
            "vk_bloom_intensity",
            "vk_bloom_saturation",
            "vk_bloom_scene_saturation",
        ):
            self.assertIn(f'Cvar_Get("{name}"', VK_POST)

    def test_bloom_uses_native_downsampled_ping_pong_images(self) -> None:
        self.assertIn("VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |", VK_POST)
        self.assertIn("VK_IMAGE_USAGE_SAMPLED_BIT", VK_POST)
        self.assertIn("VK_IMAGE_USAGE_TRANSFER_SRC_BIT", VK_POST)
        self.assertIn("VK_IMAGE_USAGE_TRANSFER_DST_BIT", VK_POST)
        self.assertIn("VK_PostProcess_CreateBloomImage", VK_POST)
        self.assertIn("VK_PostProcess_RecordBloomPass", VK_POST)
        self.assertIn("VK_BLOOM_MODE_PREFILTER", VK_POST)
        self.assertIn("VK_BLOOM_MODE_BLUR_X", VK_POST)
        self.assertIn("VK_BLOOM_MODE_BLUR_Y", VK_POST)
        self.assertNotIn('#include "rend_gl', VK_POST)

    def test_native_show_bloom_previews_level_zero_without_display_transforms(self) -> None:
        self.assertIn('Cvar_Get("vk_showbloom", "0", CVAR_CHEAT)', VK_POST)
        self.assertIn("show_bloom_active", VK_POST)
        self.assertIn("descriptor_show_bloom_active", VK_POST)
        self.assertIn("frame->bloom_ping.render_view", VK_POST)
        self.assertIn("? -2.0f : (float)vk_postprocess.bloom_active_mip_levels",
                      VK_POST)
        self.assertIn("push_data.bloom_final.w < -1.5", POST_SHADER)
        self.assertIn("out_color = texture(scene_sampler, tc)", POST_SHADER)
        self.assertIn("bypass all display", POST_SHADER)
        self.assertNotIn('#include "rend_gl', VK_POST)

    def test_bloom_mip_chain_matches_the_opengl_level_blend_contract(self) -> None:
        self.assertIn("VK_POSTPROCESS_BLOOM_MAX_LEVELS = 6", VK_POST)
        self.assertIn("VK_PostProcess_BloomMipCount", VK_POST)
        self.assertIn("VK_PostProcess_ActiveBloomMipLevels", VK_POST)
        self.assertIn("VK_PostProcess_GenerateBloomMips", VK_POST)
        self.assertIn("vkCmdBlitImage", VK_POST)
        self.assertIn("VK_FORMAT_FEATURE_BLIT_SRC_BIT", VK_POST)
        self.assertIn("VK_FORMAT_FEATURE_BLIT_DST_BIT", VK_POST)
        self.assertIn("VK_LOD_CLAMP_NONE", VK_UI)
        self.assertIn("textureLod(bloom_sampler, tc, float(i))", POST_SHADER)
        self.assertIn("weight *= 0.5", POST_SHADER)

    def test_shaders_keep_prefilter_blur_and_composite_separate(self) -> None:
        self.assertIn("float soft = clamp", BLOOM_SHADER)
        self.assertIn("Gaussian kernel", BLOOM_SHADER)
        self.assertIn("Pair two adjacent taps into one bilinear sample", BLOOM_SHADER)
        self.assertIn("layout(std140, set = 1, binding = 0)", BLOOM_SHADER)
        self.assertIn("vec4 offset_weight[51]", BLOOM_SHADER)
        self.assertIn("for (int i = -radius; i <= radius; i += 2, ++pair)",
                      BLOOM_SHADER)
        self.assertIn("bloom_kernel.offset_weight[pair].xy", BLOOM_SHADER)
        self.assertNotIn("float pair_offset", BLOOM_SHADER)
        self.assertIn("VK_PostProcess_UpdateBlurKernel", VK_POST)
        self.assertIn("VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER", VK_POST)
        self.assertIn("binding = 2", POST_SHADER)
        self.assertIn("bloom_final", POST_SHADER)
        self.assertLess(
            POST_SHADER.index("vec4 color = texture(scene_sampler, tc)"),
            POST_SHADER.index("if (push_data.color_enabled > 0.5)"),
        )

    def test_native_emission_extract_preserves_the_glowmap_mrt_contract(self) -> None:
        self.assertIn("VK_CreateBloomEmissionResources", VK_MAIN)
        self.assertIn("VK_BloomEmission_Record", VK_MAIN)
        self.assertIn("VK_World_RecordBloomEmission", VK_MAIN)
        self.assertIn("VK_PostProcess_UsesBloom", VK_MAIN)
        self.assertIn("pipeline_bloom_extract", VK_WORLD)
        self.assertIn("VK_WORLD_BATCH_BLOOM_EMISSION", VK_WORLD)
        self.assertIn("VK_WORLD_BLOOM_EXTRACT", WORLD_SHADER)
        self.assertIn("emission_sampler", BLOOM_SHADER)
        self.assertIn("authored material emission", BLOOM_SHADER)
        self.assertIn("VK_UI_CreateExternalImagePairDescriptor", VK_POST)
        self.assertIn("bloom_push.aux[1]", VK_POST)
        opaque_record = VK_WORLD.split("void VK_World_RecordOpaque", 1)[1].split(
            "void VK_World_RecordBloomEmission", 1
        )[0]
        emission_record = VK_WORLD.split("void VK_World_RecordBloomEmission", 1)[1].split(
            "void VK_World_RecordAlpha", 1
        )[0]
        self.assertNotIn("VK_WORLD_BATCH_BLOOM_EMISSION", opaque_record)
        self.assertIn("!(batch->flags & VK_WORLD_BATCH_BLOOM_EMISSION)", emission_record)

    def test_frames_without_authored_sources_skip_the_native_extract_replay(self) -> None:
        self.assertIn("VK_World_HasBloomEmission", VK_WORLD)
        self.assertIn("VK_Entity_HasBloomEmission", VK_ENTITY)
        self.assertIn("VK_PostProcess_SetBloomAuthoredEmission", VK_POST)
        self.assertIn("VK_PostProcess_UsesBloomEmission", VK_POST)
        self.assertIn("bloom_authored_emission", VK_POST)
        self.assertIn("VK_PostProcess_SetBloomAuthoredEmission", VK_MAIN)
        self.assertIn("VK_PostProcess_UsesBloomEmission", VK_MAIN)

    def test_native_entity_extract_replays_glowmaps_shells_and_additive_rims(self) -> None:
        self.assertIn("VK_Entity_RecordBloomEmission", VK_MAIN)
        self.assertIn("VK_Entity_RecordBloomEmission", VK_ENTITY)
        self.assertIn("pipeline_gpu_md2_bloom_extract", VK_ENTITY)
        self.assertIn("pipeline_gpu_md2_bloom_extract_alpha", VK_ENTITY)
        self.assertIn("pipeline_gpu_md2_bloom_extract_additive", VK_ENTITY)
        self.assertIn("pipeline_bloom_extract_alpha", VK_ENTITY)
        self.assertIn("pipeline_bloom_extract_additive", VK_ENTITY)
        self.assertIn("pipeline_gpu_bmodel_bloom_extract", VK_ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_GLOWMAP", VK_ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_BLOOM_SHELL", VK_ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_BLOOM_RIM", VK_ENTITY)
        self.assertIn("(batch->alpha && !bloom_shell && !bloom_rim)", VK_ENTITY)
        self.assertIn("(batch->additive && !bloom_rim)", VK_ENTITY)
        self.assertIn("batch->depth_hack ||", VK_ENTITY)
        self.assertIn("VK_ENTITY_BLOOM_EXTRACT", ENTITY_SHADER)
        self.assertIn("VK_ENTITY_VERTEX_BLOOM_SHELL |", ENTITY_SHADER)
        self.assertIn("VK_ENTITY_VERTEX_BLOOM_RIM)) != 0u && !gl_FrontFacing",
                      ENTITY_SHADER)
        self.assertIn("emission = texture(glow_sampler, in_uv).rgb", ENTITY_SHADER)
        self.assertIn("emission = base.rgb * glow_alpha", ENTITY_SHADER)
        self.assertIn("emission = base.rgb * in_color.rgb", ENTITY_SHADER)
        self.assertIn("emission = in_color.rgb * rim * 0.70", ENTITY_SHADER)
        self.assertIn("emission_alpha = in_color.a * rim", ENTITY_SHADER)
        self.assertIn("out_color = vec4(emission, emission_alpha)",
                      ENTITY_SHADER)

    def test_rim_emission_keeps_a_native_depth_disabled_fallback(self) -> None:
        self.assertIn("VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH", VK_ENTITY)
        self.assertIn(
            "stencil_mode == VK_ENTITY_STENCIL_RIM_BLOOM_NO_DEPTH",
            VK_ENTITY,
        )
        self.assertIn(
            "pipeline_gpu_md2_bloom_extract_additive",
            VK_ENTITY,
        )

    def test_rim_emission_uses_sampled_depth_when_available(self) -> None:
        self.assertIn("VK_Entity_HasBloomRimDepthSampling", VK_MAIN)
        self.assertIn("bloom_rim_extract_render_pass", VK_MAIN)
        self.assertIn("bloom_depth_descriptor_set", VK_MAIN)
        self.assertIn("VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL", VK_MAIN)
        self.assertIn("pipeline_bloom_extract_additive_depth_sample", VK_ENTITY)
        self.assertIn("pipeline_gpu_md2_bloom_extract_additive_depth_sample", VK_ENTITY)
        self.assertIn("VK_ENTITY_BLOOM_EXTRACT_DEPTH_SAMPLE", ENTITY_SHADER)
        self.assertIn("texelFetch(bloom_depth_sampler", ENTITY_SHADER)

    def test_depth_hack_bloom_replays_only_direct_sources_and_gates_late_load_pass(self) -> None:
        self.assertIn("VK_ENTITY_VERTEX_BLOOM_DEPTHHACK", VK_ENTITY)
        self.assertIn("shell && depth_hack", VK_ENTITY)
        self.assertIn("VK_Entity_RecordDepthHackBloomEmissionPhase", VK_ENTITY)
        self.assertIn("VK_Entity_HasDepthHackBloomEmission", VK_ENTITY)
        self.assertIn("emission *= 0.10", ENTITY_SHADER)
        self.assertIn("bloom_overlay_extract_render_pass", VK_MAIN)
        self.assertIn("VK_BloomDepthHackEmission_Record", VK_MAIN)
        late_pass = VK_MAIN.split("if (liquid_refraction && bloom_emission", 1)[1]
        self.assertIn("VK_Entity_HasDepthHackBloomEmission()", late_pass)
        self.assertIn("VK_BloomDepthHackEmission_Record", late_pass)

    def test_bloom_records_between_scene_copy_and_final_composite(self) -> None:
        final_postprocess = VK_MAIN.index("if (final_postprocess) {")
        scene_copy = VK_MAIN.index("VK_SceneCopy_Record(cmd, image_index)",
                                   final_postprocess)
        bloom = VK_MAIN.index("VK_PostProcess_RecordBloom(cmd)",
                              final_postprocess)
        final = VK_MAIN.index("VK_PostProcess_RecordFinal(",
                              final_postprocess)
        self.assertLess(scene_copy, bloom)
        self.assertLess(bloom, final)

    def test_external_descriptors_supply_scene_lut_and_bloom_bindings(self) -> None:
        self.assertIn("VK_UI_CreateExternalImageTripleDescriptor", VK_UI)
        self.assertIn(".dstBinding = 2", VK_UI)
        self.assertIn("VkDescriptorSetLayoutBinding bindings[3]", VK_UI)
        self.assertIn("VK_UI_CreateExternalImageTripleDescriptor", VK_POST)


if __name__ == "__main__":
    unittest.main()
