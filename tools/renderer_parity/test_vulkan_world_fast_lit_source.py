#!/usr/bin/env python3
"""Headless structural checks for the native Vulkan static-light fast path."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
SHADER = (
    ROOT / "src/rend_vk/shaders/vk_world_shadow.frag"
).read_text(encoding="utf-8")
SPV_GENERATOR = (ROOT / "tools/gen_vk_world_spv.py").read_text(encoding="utf-8")


class VulkanWorldFastLitSourceTests(unittest.TestCase):
    def test_static_batches_preserve_receiver_flag_boundaries(self) -> None:
        self.assertIn("uint8_t vertex_flags;", WORLD)
        self.assertIn(".vertex_flags = vertex_flags", WORLD)
        self.assertIn(".vertex_flags != vertex_flags", WORLD)

    def test_fast_pipeline_requires_unlit_opaque_lightmapped_receivers(self) -> None:
        self.assertIn("pipeline_fast_lit_opaque", WORLD)
        self.assertIn("VK_Shadow_HasActiveReceiverLighting", WORLD)
        self.assertIn("const bool world_fullbright = VK_World_Fullbright()", WORLD)
        self.assertIn("VK_WORLD_VERTEX_LIGHTMAPPED |", WORLD)
        self.assertIn("(batch->vertex_flags & ~fast_lit_vertex_flags) == 0", WORLD)
        self.assertIn("VK_World_CreatePipelineVariant", WORLD)
        self.assertIn("vk_world_fast_lit_frag_spv", WORLD)
        self.assertIn("VK_CULL_MODE_BACK_BIT, false, false,", WORLD)
        self.assertIn("VK_Debug_RecordFastLitDraw(VK_DEBUG_DOMAIN_WORLD)", WORLD)

    def test_coverage_telemetry_classifies_every_lightmapped_world_batch(self) -> None:
        self.assertIn("VK_Debug_SetWorldFastLitCoverage", WORLD)
        self.assertIn("world_fast_lit_candidates", (ROOT / "src/rend_vk/vk_debug.c").read_text(
            encoding="utf-8"
        ))
        self.assertIn("world_fast_lit_receiver_lighting", (ROOT / "src/rend_vk/vk_debug.c").read_text(
            encoding="utf-8"
        ))
        self.assertIn("world_fast_lit_material_ineligible", (ROOT / "src/rend_vk/vk_debug.c").read_text(
            encoding="utf-8"
        ))

    def test_fast_shader_keeps_lightmap_intensity_and_fog_contract(self) -> None:
        self.assertIn("VK_WORLD_STATIC_FAST_LIT", SHADER)
        self.assertIn("texture(lm_sampler, in_lm_uv)", SHADER)
        self.assertIn("shadow_dlight_count.z", SHADER)
        self.assertIn("VK_WORLD_VERTEX_INTENSITY", SHADER)
        self.assertIn("apply_fog(color.rgb, false)", SHADER)
        self.assertIn("vk_world_fast_lit_frag_spv", SPV_GENERATOR)

    def test_texture_replace_opaque_surfaces_use_a_native_fast_pipeline(self) -> None:
        self.assertIn('Cvar_Get("vk_world_fast_lit", "1"', WORLD)
        self.assertIn("pipeline_texture_replace_opaque", WORLD)
        self.assertIn("pipeline_texture_replace_no_fog_opaque", WORLD)
        self.assertIn("texture_replace_vertex_flags", WORLD)
        self.assertIn("vk_world_texture_replace_frag_spv", WORLD)
        self.assertIn("VK_WORLD_TEXTURE_REPLACE", SHADER)
        self.assertIn("texture(tex_sampler, in_uv)", SHADER)
        self.assertIn("apply_fog(color.rgb, false)", SHADER)
        self.assertIn("vk_world_texture_replace_frag_spv", SPV_GENERATOR)
        self.assertIn("vk_world_texture_replace_no_fog_frag_spv", SPV_GENERATOR)
        self.assertIn("VK_WORLD_TEXTURE_REPLACE_NO_FOG", SHADER)
        self.assertIn("VK_Debug_RecordWorldTextureReplaceDraw", WORLD)
        texture_replace_creation = WORLD.split(
            "// OpenGL compiles ordinary opaque faces with GLS_TEXTURE_REPLACE", 1
        )[1].split("if (!VK_World_CreatePipelineVariant(ctx, true", 1)[0]
        self.assertIn("VK_CULL_MODE_NONE", texture_replace_creation)
        self.assertNotIn("VK_CULL_MODE_BACK_BIT", texture_replace_creation)

    def test_static_light_glowmaps_have_their_own_receiver_specialization(self) -> None:
        self.assertIn("pipeline_fast_lit_glowmap_opaque", WORLD)
        self.assertIn("fast_lit_glowmap_vertex_flags", WORLD)
        self.assertIn("VK_WORLD_VERTEX_GLOWMAP", WORLD)
        self.assertIn("VK_WORLD_STATIC_FAST_LIT_GLOWMAP", SHADER)
        self.assertIn("texture(glow_sampler, in_uv).a * glow_scale", SHADER)
        self.assertIn("vk_world_fast_lit_glowmap_frag_spv", SPV_GENERATOR)

    def test_no_fog_static_receivers_are_runtime_gated_by_surface_fog(self) -> None:
        shadow = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
        shadow_header = (ROOT / "src/rend_vk/vk_shadow.h").read_text(encoding="utf-8")
        self.assertIn("VK_Shadow_HasActiveSurfaceFog", shadow_header)
        self.assertIn("VK_FOG_GLOBAL | VK_FOG_HEIGHT", shadow)
        self.assertIn("surface_fog_active = VK_Shadow_HasActiveSurfaceFog()", WORLD)
        self.assertIn('Cvar_Get("vk_world_fast_lit_no_fog", "1",', WORLD)
        self.assertIn("pipeline_fast_lit_no_fog_opaque", WORLD)
        self.assertIn("pipeline_fast_lit_glowmap_no_fog_opaque", WORLD)
        self.assertIn("fast_lit && fast_lit_no_fog_enabled", WORLD)
        self.assertIn("fast_lit_no_fog_enabled && !surface_fog_active", WORLD)
        self.assertIn("VK_Debug_RecordWorldFastLitNoFogDraw()", WORLD)
        self.assertIn("VK_WORLD_STATIC_FAST_LIT_NO_FOG", SHADER)
        self.assertIn("VK_WORLD_STATIC_FAST_LIT_GLOWMAP_NO_FOG", SHADER)
        self.assertIn("vk_world_fast_lit_no_fog_frag_spv", SPV_GENERATOR)
        self.assertIn("vk_world_fast_lit_glowmap_no_fog_frag_spv", SPV_GENERATOR)


if __name__ == "__main__":
    unittest.main()
