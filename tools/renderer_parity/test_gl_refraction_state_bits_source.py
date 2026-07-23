#!/usr/bin/env python3
"""Prevent OpenGL world-refraction state from losing high-order bits."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BSP_HEADER = (ROOT / "inc/common/bsp.h").read_text(encoding="utf-8")
GL_HEADER = (ROOT / "src/rend_gl/gl.h").read_text(encoding="utf-8")
GL_SURFACES = (ROOT / "src/rend_gl/surf.c").read_text(encoding="utf-8")


class GLRefractionStateBitsSourceTests(unittest.TestCase):
    def test_common_face_state_preserves_refraction_bit_38(self) -> None:
        self.assertRegex(
            GL_HEADER, r"#define\s+GLS_REFRACT_ENABLE\s+BIT_ULL\(38\)"
        )
        self.assertIn("uint64_t        statebits;", BSP_HEADER)

    def test_surface_batch_hash_uses_the_full_state_mask(self) -> None:
        self.assertIn("static void calc_surface_hash", GL_SURFACES)
        self.assertIn("uint64_t statebits;", GL_SURFACES)
        self.assertIn(".statebits = surf->statebits,", GL_SURFACES)
        self.assertIn("(const uint8_t *)&args", GL_SURFACES)


if __name__ == "__main__":
    unittest.main()
