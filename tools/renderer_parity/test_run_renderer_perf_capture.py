#!/usr/bin/env python3
"""Tests for the hidden paired renderer telemetry capture runner."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("run_renderer_perf_capture.py")
ROOT = SCRIPT.parents[2]
SPEC = importlib.util.spec_from_file_location("renderer_perf_capture", SCRIPT)
assert SPEC and SPEC.loader
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


class RendererPerfCaptureTests(unittest.TestCase):
    def test_config_hash_covers_transitive_exec_includes_and_capture_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            install = Path(temp)
            config_dir = install / "basew" / "renderer_parity"
            config_dir.mkdir(parents=True)
            primary = config_dir / "primary.cfg"
            included = config_dir / "common.cfg"
            primary.write_text("exec renderer_parity/common.cfg\n", encoding="utf-8")
            included.write_text("set r_fullbright 1\n", encoding="utf-8")
            first = CAPTURE.config_tree_sha256(install, "renderer_parity/primary.cfg")
            included.write_text("set r_fullbright 0\n", encoding="utf-8")
            second = CAPTURE.config_tree_sha256(install, "renderer_parity/primary.cfg")
            self.assertNotEqual(first, second)

    def test_command_uses_the_explicit_hidden_native_surface_for_each_renderer(self) -> None:
        command = CAPTURE.build_command(
            Path("C:/stage/worr_x86_64.exe"), Path("C:/stage"),
            Path("C:/run/home"), "vulkan",
            "renderer_parity/fr01_renderer_perf_bmodel.cfg",
        )
        self.assertIn("win_headless", command)
        self.assertEqual(command[command.index("win_headless") + 1], "1")
        self.assertEqual(command[command.index("in_enable") + 1], "0")
        self.assertEqual(command[command.index("in_grab") + 1], "0")
        self.assertIn("r_fullscreen", command)
        self.assertEqual(command[command.index("r_fullscreen") + 1], "0")
        self.assertIn("r_renderer", command)
        self.assertEqual(command[command.index("r_renderer") + 1], "vulkan")
        self.assertIn("r_dof", command)
        self.assertEqual(command[command.index("r_dof") + 1], "0")
        self.assertIn("r_dof=0", CAPTURE.capture_profile(False))
        for cvar in (
            "gl_bloom",
            "vk_bloom",
            "gl_color_correction",
            "vk_color_correction",
            "r_crtmode",
        ):
            self.assertIn(cvar, command)
            self.assertEqual(command[command.index(cvar) + 1], "0")
            self.assertIn(f"{cvar}=0", CAPTURE.capture_profile(False))
        self.assertIn("CREATE_NO_WINDOW", SCRIPT.read_text(encoding="utf-8"))
        self.assertIn("stdin=subprocess.DEVNULL", SCRIPT.read_text(encoding="utf-8"))

    def test_postprocess_launch_overrides_are_commanded_and_hashed(self) -> None:
        dof_launch = CAPTURE.parse_launch_cvars(["r_dof=1"])
        command = CAPTURE.build_command(
            Path("C:/stage/worr_x86_64.exe"), Path("C:/stage"),
            Path("C:/run/home"), "vulkan",
            "renderer_parity/fr01_renderer_perf_dof.cfg", False, dof_launch,
        )
        self.assertEqual(command[command.index("r_dof") + 1], "1")
        self.assertIn("r_dof=1", CAPTURE.capture_profile(False, dof_launch))
        self.assertNotEqual(
            CAPTURE.capture_profile(False),
            CAPTURE.capture_profile(False, dof_launch),
        )
        with self.assertRaisesRegex(ValueError, "duplicate"):
            CAPTURE.parse_launch_cvars(["r_dof=1", "r_dof=0"])
        with self.assertRaisesRegex(ValueError, "NAME=VALUE"):
            CAPTURE.parse_launch_cvars(["r_dof"])

    def test_rmlui_mode_is_hashed_and_requires_real_vulkan_ui_uploads(self) -> None:
        command = CAPTURE.build_command(
            Path("C:/stage/worr_x86_64.exe"), Path("C:/stage"),
            Path("C:/run/home"), "vulkan",
            "renderer_parity/fr01_renderer_perf_rmlui.cfg", True,
        )
        self.assertEqual(command[command.index("ui_rml_enable") + 1], "1")
        self.assertNotEqual(
            CAPTURE.capture_profile(False), CAPTURE.capture_profile(True),
        )
        self.assertIn("RMLUI_CAPTURE_MARKER", SCRIPT.read_text(encoding="utf-8"))
        self.assertIn(r"ui_uploads=[1-9][0-9]*", SCRIPT.read_text(encoding="utf-8"))

    def test_non_dof_scenarios_disable_the_latched_effect_before_workload_setup(self) -> None:
        for config in (
            "fr01_renderer_perf_bmodel.cfg",
            "fr01_renderer_perf_bmodel_instances.cfg",
            "fr01_renderer_perf_rmlui.cfg",
        ):
            lines = (ROOT / "assets" / "renderer_parity" / config).read_text(
                encoding="utf-8"
            ).splitlines()
            self.assertGreater(len(lines), 0)
            self.assertEqual("set r_dof 0", lines[0], config)

        common = (ROOT / "assets" / "renderer_parity" /
                  "fr01_bmodel_first_frame_common.cfg").read_text(
                      encoding="utf-8"
                  )
        self.assertIn("set gl_bloom 0", common)
        self.assertIn("set vk_bloom 0", common)
        self.assertIn("set gl_color_correction 0", common)
        self.assertIn("set vk_color_correction 0", common)

        dof_lines = (ROOT / "assets" / "renderer_parity" /
                     "fr01_renderer_perf_dof.cfg").read_text(
                         encoding="utf-8"
                     ).splitlines()
        self.assertEqual("set r_dof 1", dof_lines[0])
        self.assertIn("inven", dof_lines)
        self.assertIn("set vk_stats_log 2", dof_lines)

        bloom_lines = (ROOT / "assets" / "renderer_parity" /
                       "fr01_renderer_perf_bloom_shell.cfg").read_text(
                           encoding="utf-8"
                       ).splitlines()
        self.assertEqual("set r_dof 0", bloom_lines[0])
        self.assertIn("set gl_bloom 1", bloom_lines)
        self.assertIn("set vk_bloom 1", bloom_lines)
        self.assertIn("set vk_stats_log 2", bloom_lines)

        bloom_disabled_lines = (ROOT / "assets" / "renderer_parity" /
                                "fr01_renderer_perf_bloom_shell_disabled.cfg").read_text(
                                    encoding="utf-8"
                                ).splitlines()
        self.assertEqual("set r_dof 0", bloom_disabled_lines[0])
        self.assertIn("set gl_bloom 0", bloom_disabled_lines)
        self.assertIn("set vk_bloom 0", bloom_disabled_lines)

        bloom_no_emission_lines = (ROOT / "assets" / "renderer_parity" /
                                   "fr01_renderer_perf_bloom_no_emission.cfg").read_text(
                                       encoding="utf-8"
                                   ).splitlines()
        self.assertEqual("set r_dof 0", bloom_no_emission_lines[0])
        self.assertIn("set gl_bloom 1", bloom_no_emission_lines)
        self.assertIn("set vk_bloom 1", bloom_no_emission_lines)
        self.assertIn("map worr_fr01_bmodel_first_frame", bloom_no_emission_lines)

    def test_adapter_parser_requires_the_active_native_adapter_from_each_log(self) -> None:
        self.assertEqual(
            CAPTURE.renderer_adapter("Vulkan device: Intel Iris Xe\n", "vulkan"),
            "Intel Iris Xe",
        )
        self.assertEqual(
            CAPTURE.renderer_adapter("GL_RENDERER: Intel Iris Xe\n", "opengl"),
            "Intel Iris Xe",
        )
        self.assertIsNone(CAPTURE.renderer_adapter("no adapter\n", "vulkan"))

    def test_capture_counts_explicit_full_frame_gpu_samples(self) -> None:
        samples, gpu_valid, gpu_frame_valid = CAPTURE.count_stats(
            "VK_STATS frame=1 gpu_valid=1 gpu_frame_valid=1\n"
            "VK_STATS frame=2 gpu_valid=1 gpu_frame_valid=0\n",
            "VK_STATS",
        )
        self.assertEqual((2, 2, 1), (samples, gpu_valid, gpu_frame_valid))


if __name__ == "__main__":
    unittest.main()
