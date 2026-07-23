#!/usr/bin/env python3
"""Headless structural checks for the native Vulkan CRT presentation pass."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_POST = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")
CRT_SHADER = (ROOT / "src/rend_vk/shaders/vk_crt.frag").read_text(
    encoding="utf-8"
)


class VulkanCrtSourceTests(unittest.TestCase):
    def test_uses_the_shared_crt_cvar_contract(self) -> None:
        for name in (
            "r_crtmode",
            "r_crt_brightboost",
            "r_crt_hard_pix",
            "r_crt_hard_scan",
            "r_crt_mask_dark",
            "r_crt_mask_light",
            "r_crt_scale_in_linear_gamma",
            "r_crt_shadow_mask",
        ):
            self.assertIn(f'Cvar_Get("{name}"', VK_POST)

    def test_native_crt_pipeline_has_its_own_small_push_block(self) -> None:
        self.assertIn("vk_postprocess_crt_push_t", VK_POST)
        self.assertIn("sizeof(vk_postprocess_crt_push_t) == 48", VK_POST)
        self.assertIn("vk_crt_frag_spv", VK_POST)
        self.assertIn("vk_postprocess.crt_pipeline", VK_POST)
        self.assertNotIn('#include "rend_gl', VK_POST)

    def test_shader_keeps_scanline_and_shadow_mask_contracts(self) -> None:
        self.assertIn("crt_horz5", CRT_SHADER)
        self.assertIn("crt_scanline_mod", CRT_SHADER)
        self.assertIn("crt_mask", CRT_SHADER)
        self.assertIn("crt_to_srgb", CRT_SHADER)
        self.assertIn("gl_FragCoord.xy", CRT_SHADER)
        self.assertIn("float phase = scale", CRT_SHADER)
        self.assertIn("floor((gl_FragCoord.y + phase) / scale)", CRT_SHADER)
        self.assertIn("layout(set = 0, binding = 0)", CRT_SHADER)

    def test_crt_runs_after_base_composite_without_filtering_ui(self) -> None:
        final_block = VK_MAIN.index("if (final_postprocess) {")
        composite = VK_MAIN.index("if (postprocess_composite) {", final_block)
        crt = VK_MAIN.index("if (crt_postprocess) {", final_block)
        self.assertLess(composite, crt)
        self.assertIn("VK_PostProcess_RecordCrt", VK_MAIN[crt:])
        self.assertIn("if (final_postprocess) {", VK_MAIN)

    def test_scaled_scene_uses_the_opengl_scanline_cadence(self) -> None:
        self.assertIn("vk_postprocess.ctx->scene_extent.height", VK_POST)
        self.assertIn("vk_postprocess.push.output_size[1] / max(scene_height, 1.0f)", VK_POST)
        self.assertIn("CRT samples its already-upscaled presentation image", VK_POST)

    def test_scaled_crt_lazily_owns_a_full_resolution_native_copy(self) -> None:
        self.assertIn("VK_CrtSceneCopyResourcesNeedUpdate", VK_MAIN)
        self.assertIn("VK_UpdateCrtSceneCopyResources", VK_MAIN)
        self.assertIn("VK_WaitForSubmittedFrames(ctx,", VK_MAIN)
        self.assertIn("frame->liquid_scene_descriptor_set", VK_MAIN)
        self.assertIn("VK_PostProcess_UsesCrtPass() &&", VK_MAIN)


if __name__ == "__main__":
    unittest.main()
