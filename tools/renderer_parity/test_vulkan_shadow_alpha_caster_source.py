#!/usr/bin/env python3
"""Static guardrails for native Vulkan alpha-tested shadow casters."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
SHADOW_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_shadow_alpha.frag"
).read_text(encoding="utf-8")
WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
ENTITY_HEADER = (ROOT / "src/rend_vk/vk_entity.h").read_text(encoding="utf-8")
GENERATOR = (ROOT / "tools/gen_vk_world_spv.py").read_text(encoding="utf-8")
GL_SHADOW = (ROOT / "src/rend_gl/shadow.c").read_text(encoding="utf-8")


class VulkanShadowAlphaCasterSourceTests(unittest.TestCase):
    def test_cutout_fragment_uses_visible_material_threshold_for_depth_and_moments(self) -> None:
        self.assertIn("texture(tex_sampler, in_uv).a <= 0.666", SHADOW_SHADER)
        self.assertIn("VK_SHADOW_MOMENT", SHADOW_SHADER)
        self.assertIn("vk_shadow_alpha_frag_spv", GENERATOR)
        self.assertIn("vk_shadow_alpha_moment_frag_spv", GENERATOR)
        self.assertIn("texture(u_diffuse, v_tc).a <= 0.666", GL_SHADOW)
        self.assertIn("GL_Shadow_CreateAlphaProgram", GL_SHADOW)

    def test_world_and_inline_bsp_casters_keep_material_coordinates(self) -> None:
        self.assertIn("VK_World_GetFaceShadowMaterial", WORLD)
        self.assertIn("const mface_t *face", ENTITY)
        self.assertIn("const vec3_t texture_points[3]", ENTITY_HEADER)
        self.assertIn("VK_Shadow_AddWorldOpaqueDepth", SHADOW)
        self.assertIn("VK_Shadow_AddWorldAlphaDepth", SHADOW)
        self.assertIn("VK_SHADOW_CASTER_ALPHA", SHADOW)
        self.assertIn("GL_Shadow_DrawBspCaster", GL_SHADOW)
        self.assertIn("GL_Shadow_BeginAlphaFace", GL_SHADOW)

    def test_opaque_pages_stay_batched_and_cutouts_bind_only_their_materials(self) -> None:
        self.assertIn("VK_Shadow_AddDraw(job, job->first_vertex, opaque_vertex_count", SHADOW)
        self.assertIn("draw->alpha_test && !draw->descriptor_set", SHADOW)
        self.assertIn("vk_shadow.alpha_pipeline", SHADOW)
        self.assertIn("vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS", SHADOW)
        self.assertNotIn('#include "rend_gl', SHADOW)


if __name__ == "__main__":
    unittest.main()
