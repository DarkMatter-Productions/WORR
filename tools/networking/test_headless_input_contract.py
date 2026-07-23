#!/usr/bin/env python3
"""Regression contract for automation-safe client input initialization."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INPUT = (ROOT / "src/client/input.cpp").read_text(encoding="utf-8")
WINDOWS_CLIENT = (ROOT / "src/windows/client.c").read_text(encoding="utf-8")


class HeadlessInputContractTests(unittest.TestCase):
    def test_headless_mode_disables_input_before_platform_mouse_setup(self) -> None:
        self.assertIn('Cvar_VariableInteger("win_headless") != 0', INPUT)
        init = INPUT[INPUT.index("void IN_Init(void)"):]
        self.assertIn("!in_enable->integer || IN_HeadlessAutomation()", init)
        self.assertLess(
            init.index("!in_enable->integer || IN_HeadlessAutomation()"),
            init.index("vid->init_mouse"),
        )

    def test_activation_fails_closed_when_headless_input_skips_grab_cvar_setup(self) -> None:
        grab = INPUT[INPUT.index("static bool IN_GetCurrentGrab(void)"):]
        self.assertIn("!in_enable || !in_enable->integer || !in_grab", grab)
        self.assertIn("IN_HeadlessAutomation()", grab)
        self.assertLess(
            grab.index("IN_HeadlessAutomation()"),
            grab.index("if (cls.key_dest & KEY_CONSOLE)"),
        )

    def test_windows_mouse_backend_refuses_headless_initialization_and_capture(self) -> None:
        init = WINDOWS_CLIENT[WINDOWS_CLIENT.index("bool Win_InitMouse(void)"):]
        self.assertIn("if (win_is_headless())", init)
        self.assertLess(init.index("if (win_is_headless())"), init.index("register_raw_mouse(true)"))

        grab = WINDOWS_CLIENT[WINDOWS_CLIENT.index("void Win_GrabMouse(bool grab)"):]
        self.assertIn("if (win_is_headless())", grab)
        self.assertLess(grab.index("if (win_is_headless())"), grab.index("Win_AcquireMouse();"))

        clip = WINDOWS_CLIENT[WINDOWS_CLIENT.index("static void Win_ClipCursor(void)\n{"):]
        self.assertIn("if (win_is_headless())", clip)
        self.assertLess(clip.index("if (win_is_headless())"), clip.index("ClipCursor(&win.screen_rc)"))

        warp = WINDOWS_CLIENT[WINDOWS_CLIENT.index("void Win_WarpMouse(int x, int y)"):]
        self.assertIn("if (win_is_headless())", warp)
        self.assertLess(warp.index("if (win_is_headless())"), warp.index("SetCursorPos("))


if __name__ == "__main__":
    unittest.main()
