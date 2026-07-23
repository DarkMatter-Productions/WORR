#!/usr/bin/env python3
"""Regression checks for OpenGL entity-origin diagnostic coordinates."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")


class OpenGLDebugOriginsSourceTests(unittest.TestCase):
    def test_nullmodel_axes_are_local_to_the_entity_matrix(self) -> None:
        start = MAIN.index("static void GL_DrawNullModel(void)")
        end = MAIN.index("static void make_flare_quad", start)
        draw = MAIN[start:end]
        for origin in (0, 8, 16):
            self.assertIn(f"VectorClear(tess.vertices + {origin});", draw)
        self.assertIn("VectorSet(tess.vertices + 4, 16, 0, 0);", draw)
        self.assertIn("VectorSet(tess.vertices + 12, 0, 16, 0);", draw)
        self.assertIn("VectorSet(tess.vertices + 20, 0, 0, 16);", draw)
        self.assertIn("GL_LoadMatrix(glr.entmatrix, glr.viewmatrix);", draw)
        self.assertNotIn("VectorCopy(e->origin, tess.vertices", draw)
        self.assertNotIn("VectorMA(e->origin, 16", draw)


if __name__ == "__main__":
    unittest.main()
