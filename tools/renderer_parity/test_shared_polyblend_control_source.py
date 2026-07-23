#!/usr/bin/env python3
"""Source checks for the shared GL/Vulkan screen-blend control."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")


class SharedPolyblendControlSourceTests(unittest.TestCase):
    def test_shared_cvar_synchronizes_the_legacy_gl_spelling(self) -> None:
        self.assertIn('Cvar_Get("gl_polyblend", "1", 0)', GL_MAIN)
        self.assertIn('Cvar_Get("r_polyblend", gl_polyblend->string,', GL_MAIN)
        self.assertIn("gl_polyblend_changed", GL_MAIN)
        self.assertIn("gl_sync_polyblend_defaults", GL_MAIN)
        self.assertIn("if (r_polyblend->integer)", GL_MAIN)

    def test_vulkan_gates_native_screen_blend_before_queueing_ui_geometry(self) -> None:
        self.assertIn('Cvar_Get("r_polyblend", "1", CVAR_ARCHIVE)', VK_MAIN)
        gate = VK_MAIN.split("if (!vk_r_polyblend || vk_r_polyblend->integer)", 1)[1]
        self.assertIn("VK_UI_DrawScreenBlend(fd", gate)
        self.assertNotIn('#include "rend_gl', VK_MAIN)


if __name__ == "__main__":
    unittest.main()
