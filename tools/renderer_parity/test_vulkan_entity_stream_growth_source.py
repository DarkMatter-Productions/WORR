#!/usr/bin/env python3
"""Headless structural checks for FR-01-T14 entity-stream allocation."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_GPU_PARTICLE = (ROOT / "src/rend_vk/shaders/vk_entity_gpu_particle.vert").read_text(
    encoding="utf-8"
)
VK_GPU_BEAM = (ROOT / "src/rend_vk/shaders/vk_entity_gpu_beam.vert").read_text(
    encoding="utf-8"
)


class VulkanEntityStreamGrowthSourceTests(unittest.TestCase):
    def test_persistent_stream_uses_geometric_capacity(self) -> None:
        self.assertIn("VK_ENTITY_STREAM_BUFFER_MIN_BYTES", VK_ENTITY)
        self.assertIn("static bool VK_Entity_GrowStreamBuffer", VK_ENTITY)
        self.assertIn("size_t capacity = current ? current : VK_ENTITY_STREAM_BUFFER_MIN_BYTES;", VK_ENTITY)
        self.assertIn("while (capacity < needed)", VK_ENTITY)
        self.assertIn("capacity > SIZE_MAX / 2", VK_ENTITY)
        self.assertIn("capacity *= 2;", VK_ENTITY)

    def test_device_local_geometry_arena_uses_capacity_and_live_staging_ranges(self) -> None:
        self.assertIn("vk_entity_frame_buffer_t frame_buffers[VK_MAX_FRAMES_IN_FLIGHT]", VK_ENTITY)
        self.assertIn("VK_Entity_CurrentFrameBuffer", VK_ENTITY)
        self.assertIn("static bool VK_Entity_EnsureGeometryBuffer", VK_ENTITY)
        self.assertIn("VkBuffer geometry_buffer;", VK_ENTITY)
        self.assertIn("VkBuffer geometry_staging_buffer;", VK_ENTITY)
        self.assertIn("geometry_vertex_capacity", VK_ENTITY)
        self.assertIn("geometry_index_capacity", VK_ENTITY)
        self.assertIn("geometry_index_offset", VK_ENTITY)
        self.assertIn("VK_Entity_GrowStreamBuffer(vertex_capacity, vertex_bytes,", VK_ENTITY)
        self.assertIn("VK_Entity_GrowStreamBuffer(index_capacity, index_bytes,", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_INDEX_BUFFER_BIT |", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_ENTITY)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_SRC_BIT", VK_ENTITY)
        self.assertIn("frame->geometry_buffer_bytes = capacity;", VK_ENTITY)
        self.assertIn("memcpy(frame->geometry_mapped, vk_entity.vertices, vertex_bytes);", VK_ENTITY)
        self.assertIn("frame->geometry_mapped + frame->geometry_index_offset", VK_ENTITY)
        self.assertIn("frame->vertex_upload_bytes = vertex_bytes;", VK_ENTITY)
        self.assertIn("frame->index_upload_bytes = index_bytes;", VK_ENTITY)
        self.assertIn("void VK_Entity_RecordUploads(VkCommandBuffer cmd)", VK_ENTITY)
        self.assertIn("VkBufferCopy copies[2]", VK_ENTITY)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->geometry_staging_buffer,", VK_ENTITY)
        self.assertIn("frame->geometry_buffer, copy_count, copies);", VK_ENTITY)
        self.assertIn("VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT", VK_ENTITY)
        self.assertIn("VK_ACCESS_INDEX_READ_BIT", VK_ENTITY)
        self.assertIn("VK_PIPELINE_STAGE_TRANSFER_BIT", VK_ENTITY)
        self.assertIn("frame->geometry_index_offset,\n                             VK_INDEX_TYPE_UINT16", VK_ENTITY)
        self.assertIn("VK_Entity_RecordUploads(cmd);", VK_MAIN)

    def test_model_instance_arena_preserves_independent_capacity_and_visibility(self) -> None:
        self.assertIn("static bool VK_Entity_EnsureInstanceBuffer", VK_ENTITY)
        self.assertIn("VkBuffer instance_buffer;", VK_ENTITY)
        self.assertIn("VkBuffer instance_staging_buffer;", VK_ENTITY)
        self.assertIn("md2_instance_capacity", VK_ENTITY)
        self.assertIn("bmodel_instance_capacity", VK_ENTITY)
        self.assertIn("particle_instance_capacity", VK_ENTITY)
        self.assertIn("beam_instance_capacity", VK_ENTITY)
        self.assertIn("md5_instance_capacity", VK_ENTITY)
        self.assertIn("md5_palette_capacity", VK_ENTITY)
        self.assertIn("VK_Entity_InstanceArenaAlignment", VK_ENTITY)
        self.assertIn("minStorageBufferOffsetAlignment", VK_ENTITY)
        self.assertIn("VK_Entity_InstanceArenaAppendRegion", VK_ENTITY)
        self.assertIn("VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |", VK_ENTITY)
        self.assertIn("frame->md2_instance_offset", VK_ENTITY)
        self.assertIn("frame->bmodel_instance_offset", VK_ENTITY)
        self.assertIn("frame->particle_instance_offset", VK_ENTITY)
        self.assertIn("frame->beam_instance_offset", VK_ENTITY)
        self.assertIn("frame->md5_instance_offset", VK_ENTITY)
        self.assertIn("frame->md5_palette_offset", VK_ENTITY)
        self.assertIn("VkBufferCopy copies[6]", VK_ENTITY)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->instance_staging_buffer,", VK_ENTITY)
        self.assertIn("frame->instance_buffer, copy_count, copies);", VK_ENTITY)
        self.assertIn("VK_ACCESS_SHADER_READ_BIT", VK_ENTITY)
        self.assertIn("VK_PIPELINE_STAGE_VERTEX_SHADER_BIT", VK_ENTITY)

    def test_particles_use_compact_native_instances_with_cpu_vulkan_fallback(self) -> None:
        self.assertIn('Cvar_Get("vk_particle_gpu_instancing",', VK_ENTITY)
        self.assertIn('"1", CVAR_ARCHIVE', VK_ENTITY)
        self.assertIn("typedef struct {\n    float origin_scale[4];\n    uint32_t color;\n} vk_particle_gpu_instance_t;", VK_ENTITY)
        self.assertIn("sizeof(vk_particle_gpu_instance_t) == 20", VK_ENTITY)
        self.assertIn("static bool VK_Entity_AppendGpuParticleBatch", VK_ENTITY)
        self.assertIn("static bool VK_Entity_BindGpuParticleBatch", VK_ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_LAYOUT_GPU_PARTICLE", VK_ENTITY)
        self.assertIn("pipeline_gpu_particle_alpha", VK_ENTITY)
        self.assertIn("pipeline_gpu_particle_additive", VK_ENTITY)
        self.assertIn("batch->gpu_bmodel || batch->gpu_particle", VK_ENTITY)
        self.assertIn("vk_entity_gpu_particle_vert_spv", VK_ENTITY)
        self.assertIn("gl_VertexIndex", VK_GPU_PARTICLE)
        self.assertIn("VK_ENTITY_PARTICLE_SIZE = 1.70710678", VK_GPU_PARTICLE)
        self.assertIn("VK_ENTITY_VERTEX_FULLBRIGHT |", VK_GPU_PARTICLE)

    def test_simple_beams_use_compact_native_instances_with_cpu_vulkan_fallback(self) -> None:
        self.assertIn('Cvar_Get("vk_beam_gpu_instancing", "1",', VK_ENTITY)
        self.assertIn("sizeof(vk_beam_gpu_instance_t) == 40", VK_ENTITY)
        self.assertIn("static bool VK_Entity_AppendGpuBeamBatch", VK_ENTITY)
        self.assertIn("static bool VK_Entity_BindGpuBeamBatch", VK_ENTITY)
        self.assertIn("VK_ENTITY_VERTEX_LAYOUT_GPU_BEAM", VK_ENTITY)
        self.assertIn("pipeline_gpu_beam_alpha", VK_ENTITY)
        self.assertIn("pipeline_gpu_beam_depthhack_alpha", VK_ENTITY)
        self.assertIn("batch->gpu_bmodel || batch->gpu_particle ||\n            batch->gpu_beam", VK_ENTITY)
        self.assertIn("vk_entity_gpu_beam_vert_spv", VK_ENTITY)
        self.assertIn("gl_VertexIndex", VK_GPU_BEAM)
        self.assertIn("(0, 2, 3), then (0, 1, 2)", VK_GPU_BEAM)

    def test_standard_md2_and_md5_instances_use_a_compact_native_index_stream(self) -> None:
        self.assertIn("static bool VK_Entity_AppendIndexedBatch", VK_ENTITY)
        self.assertIn("uint16_t *indices;", VK_ENTITY)
        self.assertIn("vk_entity.indices[vk_entity.index_count++] = (uint16_t)i0;", VK_ENTITY)
        self.assertIn("vkCmdBindIndexBuffer(cmd, frame->geometry_buffer,", VK_ENTITY)
        self.assertIn("(int32_t)batch->first_vertex, 0);", VK_ENTITY)
        self.assertIn("!(ent->flags & (RF_ITEM_COLORIZE | RF_OUTLINE))", VK_ENTITY)
        self.assertIn("Vulkan entity: indexed MD5 stream count overflow", VK_ENTITY)
        self.assertIn("memcpy(&vk_entity.vertices[first_vertex], vk_entity.temp_md5_vertices,", VK_ENTITY)
        self.assertIn("uint16_t *index = &vk_entity.indices[first_index + i];", VK_ENTITY)
        self.assertIn("Vulkan entity: 16-bit indexed batch overflow", VK_ENTITY)

    def test_no_opengl_renderer_route(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_ENTITY)


if __name__ == "__main__":
    unittest.main()
