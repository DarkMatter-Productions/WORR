#!/usr/bin/env python3
"""Unit contracts for the shared input-free runtime process policy."""

from __future__ import annotations

import importlib.util
import subprocess
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/headless_process.py"
SPEC = importlib.util.spec_from_file_location("headless_process", SCRIPT)
assert SPEC and SPEC.loader
HEADLESS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HEADLESS)


class HeadlessProcessTests(unittest.TestCase):
    def test_every_network_runtime_launcher_uses_the_shared_process_policy(self) -> None:
        runners = (
            "run_canonical_rail_damage_runtime_gate.py",
            "run_native_shadow_runtime_smoke.py",
            "run_rewind_mover_runtime_gate.py",
            "run_rewind_rail_damage_runtime_gate.py",
            "run_staged_impairment_smoke.py",
        )
        for runner in runners:
            source = (ROOT / "tools/networking" / runner).read_text(encoding="utf-8")
            self.assertIn("_headless_creation_flags", source, runner)
            self.assertIn("start_headless_process", source, runner)
            self.assertIn("terminate_process_tree", source, runner)

    def test_windows_teardown_kills_and_waits_for_the_complete_process_tree(self) -> None:
        process = mock.Mock()
        process.pid = 4242
        process.poll.return_value = None

        with mock.patch.object(HEADLESS.os, "name", "nt"), mock.patch.object(
            HEADLESS.subprocess, "run"
        ) as run:
            self.assertTrue(HEADLESS.terminate_process_tree(process))

        run.assert_called_once_with(
            ["taskkill", "/PID", "4242", "/T", "/F"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=5,
            creationflags=HEADLESS.creation_flags(),
        )
        process.wait.assert_called_once_with(timeout=5)
        process.terminate.assert_not_called()
        process.kill.assert_not_called()

    def test_windows_teardown_closes_a_tracked_job_after_the_root_has_exited(self) -> None:
        process = mock.Mock()
        process.pid = 4343
        process.poll.return_value = 0
        HEADLESS._WINDOWS_JOBS[process.pid] = 99
        try:
            with mock.patch.object(HEADLESS.os, "name", "nt"), mock.patch.object(
                HEADLESS, "_close_windows_job", return_value=True
            ) as close_job:
                self.assertTrue(HEADLESS.terminate_process_tree(process))
        finally:
            HEADLESS._WINDOWS_JOBS.pop(process.pid, None)

        close_job.assert_called_once_with(4343)
        process.wait.assert_not_called()
        process.terminate.assert_not_called()

    def test_posix_teardown_uses_the_direct_process_handle(self) -> None:
        process = mock.Mock()
        process.poll.return_value = None

        with mock.patch.object(HEADLESS.os, "name", "posix"):
            self.assertTrue(HEADLESS.terminate_process_tree(process))

        process.terminate.assert_called_once_with()
        process.wait.assert_called_once_with(timeout=5)
        process.kill.assert_not_called()

    def test_exited_process_is_never_reterminated(self) -> None:
        process = mock.Mock()
        process.poll.return_value = 0

        self.assertFalse(HEADLESS.terminate_process_tree(process))
        process.terminate.assert_not_called()

    def test_headless_process_start_rejects_an_interactive_stdin(self) -> None:
        with self.assertRaisesRegex(ValueError, "stdin"):
            HEADLESS.start_headless_process(["ignored"])


if __name__ == "__main__":
    unittest.main()
