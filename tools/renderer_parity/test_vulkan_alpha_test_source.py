#!/usr/bin/env python3
"""Static guards for native Vulkan alpha-cutout behavior."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORLD_SHADER = (ROOT / "src/rend_vk/shaders/vk_world_shadow.frag").read_text(
    encoding="utf-8"
)
ENTITY_SHADER = (ROOT / "src/rend_vk/shaders/vk_entity.frag").read_text(
    encoding="utf-8"
)
ENTITY_SUBMISSION = (ROOT / "src/rend_vk/vk_entity.c").read_text(
    encoding="utf-8"
)
GL_SHADER = (ROOT / "src/rend_gl/shader.c").read_text(encoding="utf-8")


class VulkanAlphaTestSourceTests(unittest.TestCase):
    def test_static_world_cutout_threshold_matches_opengl(self) -> None:
        self.assertIn("if (diffuse.a <= 0.666) discard;", GL_SHADER)
        self.assertIn(
            "VK_WORLD_VERTEX_ALPHATEST) != 0u && base.a <= 0.666",
            WORLD_SHADER,
        )

    def test_inline_bsp_marks_cutouts_and_keeps_them_on_general_pipeline(self) -> None:
        self.assertIn("flags |= VK_ENTITY_VERTEX_ALPHATEST;", ENTITY_SUBMISSION)
        self.assertIn(
            "((surf_flags & SURF_ALPHATEST) == 0 && texture_transparent)",
            ENTITY_SUBMISSION,
        )
        self.assertIn(
            "VK_ENTITY_VERTEX_ALPHATEST) != 0u && base.a < 0.666",
            ENTITY_SHADER,
        )


if __name__ == "__main__":
    unittest.main()
