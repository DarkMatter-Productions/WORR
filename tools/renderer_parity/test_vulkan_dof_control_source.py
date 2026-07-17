#!/usr/bin/env python3
"""Headless structural checks for native Vulkan depth-of-field parity."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_POST = (ROOT / "src/rend_vk/vk_postprocess.c").read_text(encoding="utf-8")
DOF_SHADER = (ROOT / "src/rend_vk/shaders/vk_dof.frag").read_text(
    encoding="utf-8"
)


class VulkanDofControlSourceTests(unittest.TestCase):
    def test_shared_controls_gate_native_depth_aware_dof(self) -> None:
        self.assertIn(
            'Cvar_Get("r_dof", "1", CVAR_ARCHIVE | CVAR_LATCH);',
            VK_POST,
        )
        self.assertIn(
            'Cvar_Get("r_dof_focus_distance", "16.0", CVAR_SERVERINFO);',
            VK_POST,
        )
        self.assertIn(
            'Cvar_Get("r_dof_blur_range", "0.0", CVAR_SERVERINFO);',
            VK_POST,
        )
        self.assertIn("vk_postprocess.dof_requested = fd", VK_POST)
        self.assertIn("!(fd->rdflags & RDF_NOWORLDMODEL)", VK_POST)
        self.assertIn("Q_clipf(fd->dof_strength, 0.0f, 1.0f)", VK_POST)

    def test_depth_is_sampled_and_restored_outside_render_passes(self) -> None:
        self.assertIn("VK_IMAGE_USAGE_SAMPLED_BIT", VK_MAIN)
        self.assertIn("depth_sample_view", VK_MAIN)
        self.assertIn("VK_DepthToShaderRead", VK_MAIN)
        self.assertIn("VK_DepthToAttachment", VK_MAIN)
        self.assertIn("VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL", VK_MAIN)

    def test_native_blur_and_composite_match_the_opengl_shape(self) -> None:
        self.assertIn("VK_PostProcess_RecordDof", VK_POST)
        self.assertIn("VK_BLOOM_MODE_COPY", VK_POST)
        self.assertIn("for (int i = 0; i < 4; i++)", VK_POST)
        self.assertIn("VK_PostProcess_RecordDofComposite", VK_POST)
        self.assertIn("const int blur_downscale = Cvar_ClampInteger(", VK_POST)
        self.assertIn("base_height / 2160.0f * 4.0f / (float)blur_downscale", VK_POST)
        self.assertIn("linearize_depth", DOF_SHADER)
        self.assertIn("focus_dist <= 0.0", DOF_SHADER)
        self.assertIn("blur_range <= 0.0", DOF_SHADER)

    def test_menu_rectangle_converts_top_origin_ui_pixels_for_vulkan(self) -> None:
        self.assertIn("cgame supplies top-origin pixels", VK_POST)
        self.assertIn("1.0f - bottom / output_height", VK_POST)
        self.assertIn("1.0f - top / output_height", VK_POST)
        self.assertIn("depth_sampler", DOF_SHADER)
        self.assertIn("vec4 rect;", DOF_SHADER)

    def test_dof_runs_after_scene_copy_and_before_final_composite(self) -> None:
        final_postprocess = VK_MAIN.index("if (final_postprocess) {")
        scene_copy = VK_MAIN.index("VK_SceneCopy_Record(cmd, image_index)", final_postprocess)
        depth_read = VK_MAIN.index("VK_DepthToShaderRead(cmd, frame)", final_postprocess)
        dof = VK_MAIN.index("VK_PostProcess_RecordDof(cmd)", final_postprocess)
        depth_restore = VK_MAIN.index("VK_DepthToAttachment(cmd, frame)", final_postprocess)
        final = VK_MAIN.index("VK_PostProcess_RecordFinal(", final_postprocess)
        self.assertLess(scene_copy, depth_read)
        self.assertLess(depth_read, dof)
        self.assertLess(dof, depth_restore)
        self.assertLess(depth_restore, final)

    def test_native_only(self) -> None:
        self.assertNotIn('#include "rend_gl', VK_MAIN)
        self.assertNotIn('#include "rend_gl', VK_POST)


if __name__ == "__main__":
    unittest.main()
