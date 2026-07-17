#!/usr/bin/env python3
"""Headless structural coverage for FR-01-T14 native GPU MD5 skinning."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
GPU_MD5_VERT = (
    ROOT / "src/rend_vk/shaders/vk_entity_gpu_md5.vert"
).read_text(encoding="utf-8")
SPV_GENERATOR = (ROOT / "tools/gen_vk_world_spv.py").read_text(encoding="utf-8")


class VulkanGpuMd5SubmissionSourceTests(unittest.TestCase):
    def test_static_mesh_and_weight_data_are_native_device_local_resources(self) -> None:
        self.assertIn("static bool VK_Entity_CreateMd5GpuResources", VK_ENTITY)
        self.assertIn("gpu_md5_weight_buffer", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_STORAGE_BUFFER_BIT", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_ENTITY)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", VK_ENTITY)
        self.assertIn("VK_Entity_DestroyMd5GpuResources", VK_ENTITY)

    def test_vertex_stage_performs_native_weighted_skinning(self) -> None:
        self.assertIn("readonly buffer Md5Weights", GPU_MD5_VERT)
        self.assertIn("readonly buffer Md5Joints", GPU_MD5_VERT)
        self.assertIn("for (uint i = 0u; i < in_weight_count; i++)", GPU_MD5_VERT)
        self.assertIn("in_joint_palette_offset + weight.joint_index", GPU_MD5_VERT)
        self.assertIn("local_pos += weight.pos_bias.w", GPU_MD5_VERT)
        self.assertIn("local_normal += weight.pos_bias.w", GPU_MD5_VERT)
        self.assertIn("out_world_pos = world_pos", GPU_MD5_VERT)
        self.assertIn("out_normal = world_normal", GPU_MD5_VERT)
        self.assertIn("vk_entity_gpu_md5.vert", SPV_GENERATOR)

    def test_per_frame_instance_palette_streams_and_descriptor_set_are_bound(self) -> None:
        self.assertIn("static bool VK_Entity_EnsureMd5InstanceBuffer", VK_ENTITY)
        self.assertIn("static bool VK_Entity_EnsureMd5PaletteBuffer", VK_ENTITY)
        self.assertIn("frame->md5_instance_upload_bytes = instance_bytes", VK_ENTITY)
        self.assertIn("frame->md5_palette_upload_bytes = palette_bytes", VK_ENTITY)
        self.assertIn("VK_Entity_UpdateMd5DescriptorSets", VK_ENTITY)
        self.assertIn("const uint32_t frame_index = vk_entity.ctx->current_frame", VK_ENTITY)
        self.assertNotIn("for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {\n        vk_entity_frame_buffer_t *frame", VK_ENTITY)
        self.assertIn("Other frame-indexed descriptor sets can still be pending", VK_ENTITY)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->md5_instance_staging_buffer,", VK_ENTITY)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->md5_palette_staging_buffer,", VK_ENTITY)
        self.assertIn("VK_ACCESS_SHADER_READ_BIT", VK_ENTITY)
        self.assertIn("VK_PIPELINE_STAGE_VERTEX_SHADER_BIT", VK_ENTITY)
        self.assertIn("vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,\n                            vk_entity.pipeline_layout, 3", VK_ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_LAYOUT_GPU_MD5", VK_ENTITY)

    def test_gpu_submission_batches_static_mesh_instances_and_preserves_fallbacks(self) -> None:
        self.assertIn("static bool VK_Entity_AppendGpuMd5Batch", VK_ENTITY)
        self.assertIn("batch->gpu_md5_mesh != mesh", VK_ENTITY)
        self.assertIn("batch->vertex_flags != vertex_flags", VK_ENTITY)
        self.assertIn("batch->first_instance + batch->instance_count != instance_index", VK_ENTITY)
        self.assertIn("VK_Entity_AddGpuMD5", VK_ENTITY)
        self.assertIn("VK_Entity_ShouldUseGpuMD5", VK_ENTITY)
        self.assertIn("vk_md5_gpu_skinning", VK_ENTITY)
        self.assertIn("!(ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE))", VK_ENTITY)
        self.assertIn("GPU MD5 residency unavailable for %s ", VK_ENTITY)
        self.assertIn('error && *error ? error : "unknown failure"', VK_ENTITY)
        self.assertNotIn('#include "rend_gl', VK_ENTITY)

    def test_model_lifetime_releases_static_gpu_resources_before_cpu_storage(self) -> None:
        free_model = VK_ENTITY.split("static void VK_Entity_FreeModel", 1)[1].split(
            "static void VK_Entity_FreeAllModels", 1
        )[0]
        self.assertLess(
            free_model.index("VK_Entity_DestroyMd5GpuResources(&model->md5);"),
            free_model.index("VK_MD5_Free(&model->md5);"),
        )


if __name__ == "__main__":
    unittest.main()
