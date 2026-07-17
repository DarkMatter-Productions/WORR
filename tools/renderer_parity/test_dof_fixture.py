#!/usr/bin/env python3
"""Regression checks for the depth-aware DOF parity receiver."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_dof.cfg"
CENTRE_FOCUS_CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_dof_centre_focus.cfg"
MENU_RECT_CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_dof_menu_rect.cfg"
WIDE_RANGE_CONFIG_PATH = ROOT / "assets/renderer_parity/fr01_dof_wide_range.cfg"
MANIFEST_PATH = ROOT / "assets/renderer_parity/fr01_dof_manifest.json"


class DofFixtureTests(unittest.TestCase):
    def test_config_enables_a_real_inventory_driven_dof_transition(self) -> None:
        config = CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set gl_bloom_sigma 8",
            "set vk_bloom_sigma 8",
            "set gl_bloom_downscale 4",
            "set vk_bloom_downscale 4",
            "set r_dof_focus_distance 250",
            "set r_dof_blur_range 32",
            "set cl_draw2d 0",
            "map worr_fr01_bmodel_first_frame",
            "inven",
            "pause",
        ):
            self.assertIn(line, config)

    def test_manifest_enables_the_latched_control_before_renderer_startup(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(1, manifest["schema_version"])
        self.assertEqual("FR-01-T12", manifest["task_id"])
        self.assertEqual(8, len(manifest["scenes"]))
        scene = manifest["scenes"][0]
        self.assertEqual("depth_aware_dof_bmodel_focus", scene["id"])
        self.assertEqual({"r_dof": "1"}, scene["launch_cvars"])
        self.assertEqual([160, 120, 640, 480], scene["crop"])
        self.assertEqual(1, scene["metrics"]["pixel_threshold"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])
        self.assertEqual({"r_dof": "0"}, manifest["scenes"][1]["launch_cvars"])

    def test_centre_depth_focus_and_automatic_range_have_a_paired_control(self) -> None:
        config = CENTRE_FOCUS_CONFIG_PATH.read_text(encoding="utf-8")
        self.assertIn("set r_dof_focus_distance 0", config)
        self.assertIn("set r_dof_blur_range 0", config)
        self.assertIn("inven", config)

        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        scene = manifest["scenes"][2]
        self.assertEqual("depth_aware_dof_centre_focus_auto_range", scene["id"])
        self.assertEqual({"r_dof": "1"}, scene["launch_cvars"])
        self.assertEqual({"r_dof": "0"}, manifest["scenes"][3]["launch_cvars"])

    def test_menu_owned_rectangle_state_has_a_strict_paired_capture(self) -> None:
        config = MENU_RECT_CONFIG_PATH.read_text(encoding="utf-8")
        for line in (
            "set ui_rml_enable 0",
            "set cl_menu_bokeh_blur 1",
            "pushmenu quit_confirm",
            "inven",
        ):
            self.assertIn(line, config)

        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        scene = manifest["scenes"][4]
        self.assertEqual("depth_aware_dof_menu_rect", scene["id"])
        self.assertEqual({"r_dof": "1"}, scene["launch_cvars"])
        self.assertEqual(1, scene["metrics"]["pixel_threshold"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])
        self.assertEqual({"r_dof": "0"}, manifest["scenes"][5]["launch_cvars"])

    def test_wide_explicit_range_has_a_paired_capture(self) -> None:
        config = WIDE_RANGE_CONFIG_PATH.read_text(encoding="utf-8")
        self.assertIn("set r_dof_focus_distance 250", config)
        self.assertIn("set r_dof_blur_range 512", config)
        self.assertIn("inven", config)

        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        scene = manifest["scenes"][6]
        self.assertEqual("depth_aware_dof_wide_explicit_range", scene["id"])
        self.assertEqual({"r_dof": "1"}, scene["launch_cvars"])
        self.assertEqual({"r_dof": "0"}, manifest["scenes"][7]["launch_cvars"])


if __name__ == "__main__":
    unittest.main()
