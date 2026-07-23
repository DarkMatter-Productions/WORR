#!/usr/bin/env python3
"""Headless structural checks for immutable Vulkan inline-BSP submission."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
SHADER = (
    ROOT / "src/rend_vk/shaders/vk_entity_gpu_bmodel.vert"
).read_text(encoding="utf-8")
TEXTURE_REPLACE_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_entity_gpu_bmodel_texture_replace.vert"
).read_text(encoding="utf-8")
TEXTURE_REPLACE_FRAGMENT = (
    ROOT / "src/rend_vk/shaders/vk_entity_gpu_bmodel_texture_replace.frag"
).read_text(encoding="utf-8")
ENTITY_FRAGMENT = (ROOT / "src/rend_vk/shaders/vk_entity.frag").read_text(
    encoding="utf-8"
)
SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
SPV_GENERATOR = (ROOT / "tools/gen_vk_world_spv.py").read_text(encoding="utf-8")


class VulkanGpuBmodelSubmissionSourceTests(unittest.TestCase):
    def test_static_geometry_and_frame_instance_stream_are_native_vulkan(self) -> None:
        self.assertIn("vk_bmodel_gpu_vertex_t", ENTITY)
        self.assertIn("vk_bmodel_gpu_instance_t", ENTITY)
        self.assertIn("VK_Entity_EnsureBspGpuGeometry", ENTITY)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", ENTITY)
        self.assertIn('"vkCreateBuffer(static BSP vertex)"', ENTITY)
        self.assertIn("VK_Entity_EnsureBmodelInstanceBuffer", ENTITY)
        self.assertIn("VK_Entity_BindGpuBmodelBatch", ENTITY)
        self.assertIn("VK_Entity_AppendGpuBmodelBatch", ENTITY)
        self.assertNotIn('#include "rend_gl', ENTITY)

    def test_static_mesh_keeps_per_entity_transform_and_normal_handling(self) -> None:
        self.assertIn("in_scaled_axis0", SHADER)
        self.assertIn("in_normal_axis0", SHADER)
        self.assertIn("world_pos = in_origin", SHADER)
        self.assertIn("world_normal = normalize", SHADER)
        self.assertIn("out_flags = in_face_flags | in_entity_flags", SHADER)
        self.assertIn("VK_Entity_TransformNormalWithTransform", ENTITY)
        self.assertIn("transform.inv_scale[axis]", ENTITY)

    def test_shader_is_embedded_and_gets_dedicated_pipelines(self) -> None:
        self.assertIn("vk_entity_gpu_bmodel.vert", SPV_GENERATOR)
        self.assertIn("vk_entity_gpu_bmodel_vert_spv", ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_LAYOUT_GPU_BMODEL", ENTITY)
        self.assertIn("pipeline_gpu_bmodel_opaque", ENTITY)
        self.assertIn("pipeline_gpu_bmodel_alpha", ENTITY)
        self.assertIn("gpu_bmodel_available", ENTITY)

    def test_special_bmodels_keep_cpu_expansion_fallback(self) -> None:
        self.assertIn("RF_SHELL_MASK | RF_OUTLINE | RF_RIMLIGHT | RF_ITEM_COLORIZE", ENTITY)
        self.assertIn("using CPU expansion", ENTITY)
        self.assertIn("VK_Entity_AddBspModel", ENTITY)

    def test_interval_stats_expose_the_entity_upload_domain(self) -> None:
        self.assertIn("entity_uploads=%llu", DEBUG)
        self.assertIn("VK_DEBUG_DOMAIN_ENTITY", DEBUG)

    def test_opaque_bmodel_instances_coalesce_without_reordering_blended_work(self) -> None:
        self.assertIn("VK_Entity_CoalesceGpuBmodelBatches", ENTITY)
        self.assertIn("VK_Entity_CanCoalesceGpuBmodelBatches", ENTITY)
        self.assertIn("Opaque, non-alpha-tested geometry is order-independent", ENTITY)
        self.assertIn("!(first->vertex_flags & VK_ENTITY_VERTEX_ALPHATEST)", ENTITY)
        self.assertIn("first->first_instance + first->instance_count == next->first_instance", ENTITY)
        self.assertIn("existing->instance_count++;", ENTITY)
        self.assertIn("vk_entity.bmodel_instance_count - batch->first_instance", ENTITY)

    def test_static_bmodel_bindings_are_cached_per_recording_pass(self) -> None:
        self.assertIn('Cvar_Get("vk_bmodel_binding_cache", "1",', ENTITY)
        self.assertIn("VK_Entity_BmodelBindingCacheEnabled", ENTITY)
        self.assertIn("bool bmodel_buffers_bound = false;", ENTITY)
        self.assertIn("!bmodel_buffers_bound ||", ENTITY)
        self.assertIn("VK_Debug_RecordEntityBmodelBinding()", ENTITY)
        self.assertIn("entity_bmodel_bindings=%u", DEBUG)

    def test_fast_lit_specialization_is_limited_to_unlit_opaque_receivers(self) -> None:
        self.assertIn("VK_ENTITY_VERTEX_GPU_BMODEL_FAST_LIT", ENTITY)
        self.assertIn('Cvar_Get("vk_bmodel_fast_lit", "1"', ENTITY)
        self.assertIn("vk_bmodel_fast_lit->integer", ENTITY)
        self.assertIn("fast_lit_receivers_inactive", ENTITY)
        self.assertIn("VK_Shadow_HasActiveReceiverLighting", ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_LIGHTMAP |", ENTITY)
        self.assertIn("pipeline_gpu_bmodel_fast_lit_opaque", ENTITY)
        self.assertIn("VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT", ENTITY)
        self.assertIn("VK_ENTITY_GPU_BMODEL_FAST_LIT", ENTITY_FRAGMENT)
        self.assertIn("apply_fog(out_color.rgb", ENTITY_FRAGMENT)
        self.assertIn("vk_entity_gpu_bmodel_fast_lit_frag_spv", SPV_GENERATOR)
        self.assertIn("vk_entity_gpu_bmodel_fast_lit_frag_spv", ENTITY)
        self.assertIn("VK_Debug_RecordFastLitDraw(VK_DEBUG_DOMAIN_ENTITY)", ENTITY)

    def test_fast_lit_no_fog_variant_is_gated_by_current_surface_fog(self) -> None:
        shadow_header = (ROOT / "src/rend_vk/vk_shadow.h").read_text(
            encoding="utf-8"
        )
        self.assertIn('Cvar_Get("vk_bmodel_fast_lit_no_fog", "1",', ENTITY)
        self.assertIn("VK_Shadow_HasActiveSurfaceFog", shadow_header)
        self.assertIn("bmodel_fast_lit_no_fog_enabled", ENTITY)
        self.assertIn("pipeline_gpu_bmodel_fast_lit_no_fog_opaque", ENTITY)
        self.assertIn("VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_FAST_LIT_NO_FOG", ENTITY)
        self.assertIn("VK_ENTITY_GPU_BMODEL_FAST_LIT_NO_FOG", ENTITY_FRAGMENT)
        self.assertIn("vk_entity_gpu_bmodel_fast_lit_no_fog_frag_spv", ENTITY)
        self.assertIn("vk_entity_gpu_bmodel_fast_lit_no_fog_frag_spv", SPV_GENERATOR)
        self.assertIn("VK_Debug_RecordEntityFastLitNoFogDraw()", ENTITY)

    def test_fast_lit_fragment_keeps_opaque_batches_discard_free(self) -> None:
        fast_lit = ENTITY_FRAGMENT.split(
            "#elif defined(VK_ENTITY_GPU_BMODEL_FAST_LIT)", 1
        )[1].split("#else", 1)[0]
        self.assertNotIn("discard;", fast_lit)
        self.assertIn("#ifndef VK_ENTITY_GPU_BMODEL_FAST_LIT_NO_FOG", fast_lit)
        self.assertIn("texture_transparent", ENTITY)
        self.assertIn("SURF_ALPHATEST", ENTITY)
        self.assertIn("vk_shadow.sun_active", SHADOW)
        self.assertIn("vk_shadow.uniform.dlight_count[0]", SHADOW)

    def test_unlightmapped_opaque_bmodels_preserve_gl_texture_replace(self) -> None:
        self.assertIn("VK_ENTITY_VERTEX_TEXTURE_REPLACE = BIT(16)", ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_GPU_BMODEL_TEXTURE_REPLACE", ENTITY)
        self.assertIn("!face_lightmapped && !(surf_flags & SURF_TRANS_MASK)", ENTITY)
        self.assertIn("texture_replace_face_flags", ENTITY)
        self.assertIn("texture_replace_ignored_entity_flags", ENTITY)
        self.assertIn(
            "VK_ENTITY_VERTEX_FULLBRIGHT | VK_ENTITY_VERTEX_NO_SHADOW", ENTITY
        )
        self.assertIn('Cvar_Get("vk_bmodel_texture_replace", "1",', ENTITY)
        self.assertIn("pipeline_gpu_bmodel_texture_replace_opaque", ENTITY)
        self.assertIn("pipeline_gpu_bmodel_texture_replace_no_fog_opaque", ENTITY)
        self.assertIn(
            "VK_ENTITY_BLEND_OPAQUE_GPU_BMODEL_TEXTURE_REPLACE", ENTITY
        )
        self.assertIn("VK_ENTITY_VERTEX_TEXTURE_REPLACE 65536u", ENTITY_FRAGMENT)
        self.assertIn("VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE", ENTITY_FRAGMENT)
        self.assertIn(
            "VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG", ENTITY_FRAGMENT
        )
        self.assertIn("GLS_TEXTURE_REPLACE", ENTITY_FRAGMENT)
        self.assertIn("out_color = base;", ENTITY_FRAGMENT)
        self.assertIn(
            "vk_entity_gpu_bmodel_texture_replace_frag_spv", SPV_GENERATOR
        )
        self.assertIn(
            "vk_entity_gpu_bmodel_texture_replace_no_fog_frag_spv",
            SPV_GENERATOR,
        )
        self.assertIn("vk_entity_gpu_bmodel_texture_replace.frag", SPV_GENERATOR)
        self.assertIn("VK_Debug_RecordEntityTextureReplaceDraw", ENTITY)

    def test_texture_replace_vertex_specialization_avoids_unused_receiver_work(
        self,
    ) -> None:
        self.assertIn("vk_entity_gpu_bmodel_texture_replace.vert", SPV_GENERATOR)
        self.assertIn("vk_entity_gpu_bmodel_texture_replace_vert_spv", ENTITY)
        self.assertIn(
            "vk_entity_gpu_bmodel_texture_replace_no_fog_vert_spv", ENTITY
        )
        self.assertIn("gpu_bmodel_texture_replace", ENTITY)
        self.assertIn("gpu_bmodel_texture_replace_specialized", ENTITY)
        self.assertIn("vk_bmodel_texture_replace_specialization", ENTITY)
        self.assertIn("in_pos", TEXTURE_REPLACE_SHADER)
        self.assertIn("in_uv", TEXTURE_REPLACE_SHADER)
        self.assertIn("in_face_flags", TEXTURE_REPLACE_SHADER)
        self.assertIn("in_origin", TEXTURE_REPLACE_SHADER)
        self.assertIn("in_scaled_axis0", TEXTURE_REPLACE_SHADER)
        self.assertIn("in_scaled_axis1", TEXTURE_REPLACE_SHADER)
        self.assertIn("in_scaled_axis2", TEXTURE_REPLACE_SHADER)
        self.assertIn("in_entity_flags", TEXTURE_REPLACE_SHADER)
        self.assertIn("out_flags = in_face_flags | in_entity_flags", TEXTURE_REPLACE_SHADER)
        self.assertNotIn("in_normal", TEXTURE_REPLACE_SHADER)
        self.assertNotIn("in_lm_uv", TEXTURE_REPLACE_SHADER)
        self.assertNotIn("in_face_alpha", TEXTURE_REPLACE_SHADER)
        self.assertNotIn("in_normal_axis", TEXTURE_REPLACE_SHADER)
        self.assertNotIn("in_color", TEXTURE_REPLACE_SHADER)
        self.assertIn("layout(location = 4) out vec3 out_world_pos", TEXTURE_REPLACE_SHADER)
        self.assertIn("VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG", TEXTURE_REPLACE_SHADER)
        self.assertIn("layout(location = 0) in vec2 in_uv", TEXTURE_REPLACE_FRAGMENT)
        self.assertIn("layout(location = 3) flat in uint in_flags", TEXTURE_REPLACE_FRAGMENT)
        self.assertIn("layout(location = 4) in vec3 in_world_pos", TEXTURE_REPLACE_FRAGMENT)
        self.assertIn("VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG", TEXTURE_REPLACE_FRAGMENT)


if __name__ == "__main__":
    unittest.main()
