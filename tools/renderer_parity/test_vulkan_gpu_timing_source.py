#!/usr/bin/env python3
"""Headless structural checks for FR-01-T15 Vulkan GPU timing."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_POST = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")


class VulkanGpuTimingSourceTests(unittest.TestCase):
    def test_timestamp_capability_and_pool_are_native_vulkan(self) -> None:
        self.assertIn("timestampComputeAndGraphics", VK_DEBUG)
        self.assertIn("VK_QUERY_TYPE_TIMESTAMP", VK_DEBUG)
        self.assertIn("vkCreateQueryPool", VK_DEBUG)
        self.assertIn("VK_DEBUG_GPU_TIMESTAMP_COUNT", VK_DEBUG)
        self.assertIn(
            "ctx->frame_count * VK_DEBUG_GPU_TIMESTAMP_COUNT",
            VK_DEBUG,
        )
        self.assertIn("timestampPeriod", VK_DEBUG)
        self.assertNotIn('#include "rend_gl', VK_DEBUG)

    def test_command_buffer_brackets_full_gpu_submission(self) -> None:
        self.assertIn("vkCmdResetQueryPool", VK_DEBUG)
        self.assertIn("VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT", VK_DEBUG)
        self.assertIn("VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT", VK_DEBUG)
        self.assertIn("VK_Debug_BeginGpuFrame(cmd, ctx->current_frame)", VK_MAIN)
        self.assertIn("VK_Debug_EndGpuFrame(cmd)", VK_MAIN)
        begin = VK_MAIN.index("VK_Debug_BeginGpuFrame(cmd, ctx->current_frame)")
        end = VK_MAIN.index("VK_Debug_EndGpuFrame(cmd)")
        screenshot = VK_MAIN.index("VK_Screenshot_RecordCopy(cmd, image_index)")
        self.assertLess(begin, screenshot)
        self.assertLess(screenshot, end)

    def test_phases_bracket_native_upload_shadow_scene_and_composition(self) -> None:
        self.assertIn("VK_DEBUG_GPU_PHASE_UPLOAD", VK_MAIN)
        self.assertIn("VK_DEBUG_GPU_PHASE_SHADOW", VK_MAIN)
        self.assertIn("VK_DEBUG_GPU_PHASE_OPAQUE_WORLD", VK_MAIN)
        self.assertIn("VK_DEBUG_GPU_PHASE_OPAQUE_ENTITY", VK_MAIN)
        self.assertIn("VK_DEBUG_GPU_PHASE_SCENE", VK_MAIN)
        self.assertIn("VK_DEBUG_GPU_PHASE_POSTPROCESS", VK_MAIN)
        self.assertIn("VK_Debug_MarkGpuPhase", VK_DEBUG)
        self.assertIn("last_gpu_upload_ms", VK_DEBUG)
        self.assertIn("last_gpu_shadow_ms", VK_DEBUG)
        self.assertIn("last_gpu_opaque_world_ms", VK_DEBUG)
        self.assertIn("last_gpu_opaque_entity_ms", VK_DEBUG)
        self.assertIn("last_gpu_scene_ms", VK_DEBUG)
        self.assertIn("last_gpu_postprocess_ms", VK_DEBUG)

        upload = VK_MAIN.index("VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_UPLOAD)")
        shadow = VK_MAIN.index("VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_SHADOW)")
        world = VK_MAIN.index(
            "VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_OPAQUE_WORLD)"
        )
        entity = VK_MAIN.index(
            "VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_OPAQUE_ENTITY)"
        )
        scene = VK_MAIN.index("VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_SCENE)")
        post = VK_MAIN.index(
            "VK_Debug_MarkGpuPhase(cmd, VK_DEBUG_GPU_PHASE_POSTPROCESS)"
        )
        screenshot = VK_MAIN.index("VK_Screenshot_RecordCopy(cmd, image_index)")
        self.assertLess(upload, shadow)
        self.assertLess(shadow, world)
        self.assertLess(world, entity)
        self.assertLess(entity, scene)
        self.assertLess(shadow, scene)
        self.assertLess(scene, post)
        self.assertLess(post, screenshot)

    def test_results_are_resolved_after_submission_fence(self) -> None:
        self.assertIn("vkGetQueryPoolResults", VK_DEBUG)
        self.assertIn("VK_QUERY_RESULT_64_BIT", VK_DEBUG)
        self.assertIn("VK_Debug_ResolveGpuFrame(frame_index);", VK_MAIN)
        self.assertIn("VK_Debug_MarkGpuFrameSubmitted(frame_index);", VK_MAIN)
        wait = VK_MAIN.index("vkWaitForFences")
        resolve = VK_MAIN.index("VK_Debug_ResolveGpuFrame(frame_index);")
        submit = VK_MAIN.index("vkQueueSubmit")
        mark = VK_MAIN.index("VK_Debug_MarkGpuFrameSubmitted(frame_index);")
        self.assertLess(wait, resolve)
        self.assertLess(submit, mark)

    def test_stats_expose_gpu_timing_and_capability(self) -> None:
        self.assertIn('SCR_StatKeyValue("GPU frame ms"', VK_DEBUG)
        self.assertIn('SCR_StatKeyValue("GPU phases ms"', VK_DEBUG)
        self.assertIn("gpu_ms=%.3f", VK_DEBUG)
        self.assertIn("gpu_frame_ms=%.3f", VK_DEBUG)
        self.assertIn("gpu_upload_ms=%.3f", VK_DEBUG)
        self.assertIn("gpu_shadow_ms=%.3f", VK_DEBUG)
        self.assertIn("gpu_opaque_world_ms=%.3f", VK_DEBUG)
        self.assertIn("gpu_opaque_entity_ms=%.3f", VK_DEBUG)
        self.assertIn("gpu_scene_ms=%.3f", VK_DEBUG)
        self.assertIn("gpu_post_ms=%.3f", VK_DEBUG)
        self.assertIn("world_fast_lit_draws=%u", VK_DEBUG)
        self.assertIn("entity_fast_lit_draws=%u", VK_DEBUG)
        self.assertIn("VK_Debug_RecordFastLitDraw", VK_DEBUG)
        self.assertIn("gpu_frame_valid=%d", VK_DEBUG)
        self.assertIn("gpu_timing=%d", VK_DEBUG)
        self.assertIn("VK_DEBUG_MISSING_GPU_TIMING", VK_DEBUG)

    def test_cpu_submission_timing_preserves_submillisecond_precision(self) -> None:
        self.assertIn("VK_HighResMicroseconds", VK_MAIN)
        self.assertIn("QueryPerformanceCounter", VK_MAIN)
        self.assertIn("vk_frame_begin_us", VK_MAIN)
        self.assertIn("/\n                      1000.0f", VK_MAIN)

    def test_stats_count_native_fullscreen_postprocess_submission(self) -> None:
        self.assertIn("VK_DEBUG_DOMAIN_POSTPROCESS", VK_DEBUG)
        self.assertIn("post_draws=%llu", VK_DEBUG)
        self.assertIn("post_uploads=%llu", VK_DEBUG)
        self.assertIn('#include "vk_debug.h"', VK_POST)
        self.assertEqual(
            5,
            VK_POST.count(
                "VK_Debug_RecordDraw(VK_DEBUG_DOMAIN_POSTPROCESS, 3, 0);"
            ),
        )


if __name__ == "__main__":
    unittest.main()
