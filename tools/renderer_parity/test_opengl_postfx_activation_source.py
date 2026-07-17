#!/usr/bin/env python3
"""Regression coverage for independent OpenGL final-stage activation."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")


class OpenGLPostFxActivationSourceTests(unittest.TestCase):
    def test_split_toning_and_lut_activate_the_final_post_target_independently(self) -> None:
        self.assertIn(
            "if (gl_color_split_strength && gl_color_split_strength->value > 0.0f)",
            GL_MAIN,
        )
        self.assertIn("if (gl_color_lut_valid && gl_color_lut_intensity &&", GL_MAIN)
        self.assertIn("gl_color_lut_intensity->value > 0.0f)", GL_MAIN)
        self.assertIn("gl_color_split_strength_modified", GL_MAIN)
        self.assertIn("gl_color_lut_modified", GL_MAIN)
        self.assertIn("gl_color_lut_intensity_modified", GL_MAIN)


if __name__ == "__main__":
    unittest.main()
