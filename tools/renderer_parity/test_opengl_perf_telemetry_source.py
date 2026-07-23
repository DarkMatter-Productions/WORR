#!/usr/bin/env python3
"""Headless structural checks for the OpenGL performance telemetry contract."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
GL_PROFILE = (ROOT / "src/rend_gl/profile.c").read_text(encoding="utf-8")
QGL = (ROOT / "src/rend_gl/qgl.c").read_text(encoding="utf-8")


class OpenGLPerformanceTelemetrySourceTests(unittest.TestCase):
    def test_machine_readable_stats_include_comparable_metrics(self) -> None:
        self.assertIn('Cmd_AddCommand("gl_stats", GL_Stats_f);', GL_MAIN)
        self.assertIn('Cmd_RemoveCommand("gl_stats");', GL_MAIN)
        self.assertIn('"GL_STATS frame=%u draws=%d vertices=%llu indices=0 uploads=%llu "', GL_MAIN)
        for metric in ("cpu_ms=%.3f", "cpu_render_ms=%.3f", "gpu_ms=%.3f", "gpu_frame_ms=%.3f",
                       "gpu_world_ms=%.3f", "gpu_effects_ms=%.3f",
                       "gpu_post_ms=%.3f", "gpu_frame_valid=%d", "gpu_valid=%d"):
            self.assertIn(metric, GL_MAIN)

    def test_full_render_frame_timer_does_not_replace_phase_telemetry(self) -> None:
        self.assertIn("QGL_FN(QueryCounter)", QGL)
        self.assertIn("GL_ProfileGpuFrameBegin();", GL_MAIN)
        self.assertIn("GL_ProfileGpuFrameEnd();", GL_MAIN)
        self.assertIn("qglQueryCounter", GL_PROFILE)
        self.assertIn("GL_TIMESTAMP", GL_PROFILE)
        self.assertIn("frame_queries", GL_PROFILE)
        self.assertIn("gpu_frame_valid", GL_PROFILE)


if __name__ == "__main__":
    unittest.main()
