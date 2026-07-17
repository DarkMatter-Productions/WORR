#!/usr/bin/env python3
"""Structural contract for Vulkan's native tearing diagnostic."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")


class VulkanTearingSourceTests(unittest.TestCase):
    def test_native_tearing_clear_matches_gl_diagnostic_order(self) -> None:
        self.assertIn('Cvar_Get("vk_showtearing", "0", CVAR_CHEAT)', MAIN)
        self.assertIn("VK_RecordTearingDiagnostic", MAIN)
        self.assertIn("vk_showtearing_frame", MAIN)
        self.assertIn("++vk_showtearing_frame", MAIN)
        self.assertIn("ctx->presentation_render_pass", MAIN)
        self.assertIn("white ? 1.0f : 0.0f", MAIN)
        self.assertIn("vkCmdBeginRenderPass(cmd, &tearing_info", MAIN)
        self.assertIn("vkCmdEndRenderPass(cmd)", MAIN)
        self.assertLess(
            MAIN.index("if (entity_overlay && !linear_scene"),
            MAIN.index("VK_RecordTearingDiagnostic(cmd, image_index)"),
        )
        self.assertLess(
            MAIN.index("VK_RecordTearingDiagnostic(cmd, image_index)"),
            MAIN.index("if (vk_screenshot_armed)"),
        )
        self.assertNotIn('#include "rend_gl', MAIN)


if __name__ == "__main__":
    unittest.main()
