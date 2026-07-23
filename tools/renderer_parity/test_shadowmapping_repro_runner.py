#!/usr/bin/env python3
"""Headless contract checks for the shadow smoke launcher."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER = (ROOT / "tools/shadowmapping_repro_smoke.py").read_text(encoding="utf-8")


class ShadowmappingReproRunnerTests(unittest.TestCase):
    def test_shadow_smoke_keeps_the_native_surface_hidden(self) -> None:
        self.assertIn('"win_headless", "1"', RUNNER)
        self.assertIn('"vid_renderer", renderer', RUNNER)
        self.assertIn('"s_enable", "0"', RUNNER)
        self.assertIn('"r_shadowmap_cache_mode", str(args.cache_mode)', RUNNER)
        self.assertIn('"--cache-mode", type=int, choices=(0, 1, 2), default=1', RUNNER)
        self.assertIn('"vk_shadow_shrink_frames", str(args.vk_shadow_shrink_frames)', RUNNER)
        self.assertIn('"--vk-shadow-shrink-frames", type=int, default=180', RUNNER)
        self.assertIn('"r_shadowmap_size", str(args.shadow_size)', RUNNER)
        self.assertIn("if args.shadow_size_after_wait is not None:", RUNNER)
        self.assertIn('"r_shadowmap_size", str(args.shadow_size_after_wait)', RUNNER)
        self.assertIn('"r_shadow_sun_resolution", str(args.sun_shadow_size)', RUNNER)
        self.assertIn("args.sun_shadow_size_after_wait is not None", RUNNER)
        self.assertIn('str(args.sun_shadow_size_after_wait)', RUNNER)
        self.assertIn('"--shadow-size", type=int, default=512', RUNNER)
        self.assertIn('"--shadow-size-after-wait", type=int,', RUNNER)
        self.assertIn('"--shadow-size-transition-wait", type=int, default=0', RUNNER)
        self.assertIn('"--sun-shadow-size", type=int, default=1024', RUNNER)
        self.assertIn('"--sun-shadow-size-after-wait", type=int,', RUNNER)
        self.assertIn("if args.dump_before_wait:", RUNNER)
        self.assertIn('"--dump-before-wait", action="store_true"', RUNNER)
        self.assertIn("if args.pre_dump_wait > 0:", RUNNER)
        self.assertIn('"--pre-dump-wait", type=int, default=0', RUNNER)
        self.assertIn('if args.inject_shadow_recreate_failure and renderer == "vulkan":', RUNNER)
        self.assertIn('"vk_shadow_test_fail_recreate", "1"', RUNNER)
        self.assertIn('args.inject_shadow_recreate_failure and renderer == "vulkan"', RUNNER)
        self.assertIn('"--inject-shadow-recreate-failure", action="store_true"', RUNNER)
        self.assertIn("args.inject_sun_resolution_drop_after_frames is not None", RUNNER)
        self.assertIn('"vk_shadow_test_sun_resolution_drop_after_frames"', RUNNER)
        self.assertIn('"--inject-sun-resolution-drop-after-frames", type=int,', RUNNER)
        self.assertIn('"homedir", str(job_home(args, renderer, scene, filter_value))', RUNNER)
        self.assertIn('default=".tmp/shadowmapping-repro"', RUNNER)
        self.assertIn("CREATE_NO_WINDOW", RUNNER)
        self.assertIn('environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"', RUNNER)
        self.assertIn("capture_output=True", RUNNER)
        self.assertIn("def job_log", RUNNER)


if __name__ == "__main__":
    unittest.main()
