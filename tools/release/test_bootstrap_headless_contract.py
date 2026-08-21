#!/usr/bin/env python3
"""Source contract for headless launcher UI propagation."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src" / "updater" / "bootstrap.cpp").read_text(encoding="utf-8")
QUIET_HEADLESS_ASSIGNMENT = re.compile(
    r"options\.quiet_status\s*=\s*options\.quiet_status\s*\|\|\s*"
    r"ForwardedArgsRequestHeadless\(options\.forwarded_args\);"
)


class BootstrapHeadlessContractTests(unittest.TestCase):
    def test_headless_engine_cvar_selects_silent_bootstrap_ui(self) -> None:
        self.assertIn("bool ForwardedArgsRequestHeadless", SOURCE)
        self.assertIn('ToLower(args[i + 1]) != "win_headless"', SOURCE)
        self.assertRegex(SOURCE, QUIET_HEADLESS_ASSIGNMENT)
        self.assertIn("if (quiet_status) {\n      ui_ = std::make_unique<SilentUi>();", SOURCE)

    def test_worker_and_public_launcher_both_propagate_headless(self) -> None:
        self.assertGreaterEqual(
            len(QUIET_HEADLESS_ASSIGNMENT.findall(SOURCE)),
            2,
        )
        self.assertIn("if (options.quiet_status)\n    args.push_back(kQuietStatusArg);", SOURCE)


if __name__ == "__main__":
    unittest.main()
