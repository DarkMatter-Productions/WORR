#!/usr/bin/env python3
"""Headless structural coverage for FR-01-T14 native GPU MD2 submission."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
GPU_MD2_VERT = (
    ROOT / "src/rend_vk/shaders/vk_entity_gpu_md2.vert"
).read_text(encoding="utf-8")
SPV_GENERATOR = (ROOT / "tools/gen_vk_world_spv.py").read_text(encoding="utf-8")


class VulkanGpuMd2SubmissionSourceTests(unittest.TestCase):
    def test_native_static_model_resources_stay_device_local(self) -> None:
        self.assertIn("static bool VK_Entity_CreateMd2GpuResources", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_ENTITY)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", VK_ENTITY)
        self.assertIn("VK_Entity_CopyStaticBuffers", VK_ENTITY)
        self.assertIn("uploads[i].destination = *destination_buffers[i];", VK_ENTITY)
        self.assertIn("VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT", VK_ENTITY)
        self.assertIn("VK_ACCESS_INDEX_READ_BIT", VK_ENTITY)
        self.assertIn("VK_Entity_DestroyMd2GpuResources", VK_ENTITY)

    def test_gpu_vertex_stage_interpolates_and_transforms_native_model_data(self) -> None:
        self.assertIn("mix(in_old_pos, in_new_pos, frontlerp)", GPU_MD2_VERT)
        self.assertIn("mix(in_old_normal, in_new_normal, frontlerp)", GPU_MD2_VERT)
        self.assertIn("local_normal * in_shell.x", GPU_MD2_VERT)
        self.assertIn("out_world_pos = world_pos", GPU_MD2_VERT)
        self.assertIn("out_normal = world_normal", GPU_MD2_VERT)
        self.assertIn("vk_entity_gpu_md2.vert", SPV_GENERATOR)

    def test_instance_stream_batches_same_model_frame_without_cpu_vertex_expansion(self) -> None:
        self.assertIn("vk_md2_gpu_instance_t *md2_instances;", VK_ENTITY)
        self.assertIn("static bool VK_Entity_AppendGpuMd2Batch", VK_ENTITY)
        self.assertIn("batch->gpu_md2_frame != frame", VK_ENTITY)
        self.assertIn("batch->vertex_flags != vertex_flags", VK_ENTITY)
        self.assertIn("batch->first_instance + batch->instance_count != instance_index", VK_ENTITY)
        self.assertIn("VK_Entity_AddGpuMD2", VK_ENTITY)
        self.assertIn("VK_Entity_ShouldUseGpuMD2", VK_ENTITY)
        self.assertIn("vkCmdDrawIndexed(cmd, batch->index_count, batch->instance_count,", VK_ENTITY)

    def test_bloom_replay_uses_resolved_batch_flags(self) -> None:
        self.assertIn("static bool VK_Entity_BatchHasDirectBloomEmission", VK_ENTITY)
        self.assertIn(
            "const uint32_t vertex_flags = VK_Entity_BatchVertexFlags(batch);",
            VK_ENTITY,
        )
        self.assertIn(
            "if ((vertex_flags & (VK_ENTITY_VERTEX_ITEM_COLORIZE |",
            VK_ENTITY,
        )

    def test_current_frame_instance_upload_is_device_local_and_synchronized(self) -> None:
        self.assertIn("static bool VK_Entity_EnsureMd2InstanceBuffer", VK_ENTITY)
        self.assertIn("frame->md2_instance_upload_bytes = instance_bytes;", VK_ENTITY)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->md2_instance_staging_buffer,", VK_ENTITY)
        self.assertIn(".buffer = frame->md2_instance_buffer,", VK_ENTITY)
        self.assertIn("VK_Entity_BindGpuMd2Batch", VK_ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_LAYOUT_GPU_MD2", VK_ENTITY)
        self.assertIn("vk_md2_gpu_lerp", VK_ENTITY)

    def test_cpu_fallback_keeps_special_model_passes_intact(self) -> None:
        self.assertIn("!(ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE))", VK_ENTITY)
        self.assertIn("using CPU interpolation", VK_ENTITY)
        self.assertIn("GPU MD2 residency unavailable for %s ", VK_ENTITY)
        self.assertIn('error && *error ? error : "unknown failure"', VK_ENTITY)
        self.assertNotIn('#include "rend_gl', VK_ENTITY)

    def test_model_lifetime_releases_static_gpu_resources_before_cpu_storage(self) -> None:
        free_model = VK_ENTITY.split("static void VK_Entity_FreeModel", 1)[1].split(
            "static void VK_Entity_FreeAllModels", 1
        )[0]
        self.assertLess(
            free_model.index("VK_Entity_DestroyMd2GpuResources(&model->md2);"),
            free_model.index("free(model->md2.positions);"),
        )


if __name__ == "__main__":
    unittest.main()
