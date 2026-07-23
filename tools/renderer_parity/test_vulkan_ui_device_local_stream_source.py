#!/usr/bin/env python3
"""Structural coverage for native device-local Vulkan UI geometry streams."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_UI_HEADER = (ROOT / "src/rend_vk/vk_ui.h").read_text(encoding="utf-8")


class VulkanUiDeviceLocalStreamSourceTests(unittest.TestCase):
    def test_frame_local_ui_buffers_pair_device_local_draw_storage_with_staging(self) -> None:
        self.assertIn("VkBuffer vertex_staging_buffer", VK_UI)
        self.assertIn("VkBuffer index_staging_buffer", VK_UI)
        self.assertIn("VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |\n                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_UI)
        self.assertIn("VK_BUFFER_USAGE_INDEX_BUFFER_BIT |\n                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT", VK_UI)
        self.assertIn("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT", VK_UI)
        self.assertIn("VK_BUFFER_USAGE_TRANSFER_SRC_BIT", VK_UI)
        self.assertIn("vertex_staging_mapped", VK_UI)
        self.assertIn("index_staging_mapped", VK_UI)

    def test_upload_phase_copies_live_ranges_and_makes_them_visible_to_vertex_input(self) -> None:
        self.assertIn("void VK_UI_RecordUploads(VkCommandBuffer cmd)", VK_UI)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->vertex_staging_buffer, frame->vertex_buffer", VK_UI)
        self.assertIn("vkCmdCopyBuffer(cmd, frame->index_staging_buffer, frame->index_buffer", VK_UI)
        self.assertIn("VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT", VK_UI)
        self.assertIn("VK_ACCESS_INDEX_READ_BIT", VK_UI)
        self.assertIn("VK_PIPELINE_STAGE_TRANSFER_BIT", VK_UI)
        self.assertIn("VK_PIPELINE_STAGE_VERTEX_INPUT_BIT", VK_UI)
        self.assertIn("frame->vertex_upload_bytes", VK_UI)
        self.assertIn("frame->index_upload_bytes", VK_UI)

    def test_main_submission_records_ui_uploads_before_scene_passes(self) -> None:
        self.assertIn("void VK_UI_RecordUploads(VkCommandBuffer cmd);", VK_UI_HEADER)
        self.assertIn("VK_UI_RecordUploads(cmd);", VK_MAIN)
        self.assertLess(
            VK_MAIN.index("VK_UI_RecordUploads(cmd);"),
            VK_MAIN.index("VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_UPLOAD)"),
        )

    def test_ui_draws_bind_device_local_buffers_only_after_upload_recording(self) -> None:
        record = VK_UI[VK_UI.index("static void VK_UI_RecordWithPipelines"):]
        self.assertIn("!frame->vertex_upload_bytes || !frame->index_upload_bytes", record)
        self.assertIn("vkCmdBindVertexBuffers(cmd, 0, 1, &frame->vertex_buffer", record)
        self.assertIn("vkCmdBindIndexBuffer(cmd, frame->index_buffer", record)
        self.assertNotIn("memcpy(frame->vertex_mapped", record)
        self.assertNotIn("memcpy(frame->index_mapped", record)


if __name__ == "__main__":
    unittest.main()
