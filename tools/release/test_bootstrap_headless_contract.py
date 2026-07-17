#!/usr/bin/env python3
"""Source contract for headless launcher UI propagation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src" / "updater" / "bootstrap.cpp").read_text(encoding="utf-8")


class BootstrapHeadlessContractTests(unittest.TestCase):
    def test_headless_engine_cvar_selects_silent_bootstrap_ui(self) -> None:
        self.assertIn("bool ForwardedArgsRequestHeadless", SOURCE)
        self.assertIn('ToLower(args[i + 1]) != "win_headless"', SOURCE)
        self.assertIn("options.quiet_status = options.quiet_status || ForwardedArgsRequestHeadless(options.forwarded_args);", SOURCE)
        self.assertIn("if (quiet_status) {\n      ui_ = std::make_unique<SilentUi>();", SOURCE)

    def test_worker_and_public_launcher_both_propagate_headless(self) -> None:
        self.assertGreaterEqual(
            SOURCE.count("options.quiet_status = options.quiet_status || ForwardedArgsRequestHeadless(options.forwarded_args);"),
            2,
        )
        self.assertIn("if (options.quiet_status)\n    args.push_back(kQuietStatusArg);", SOURCE)


if __name__ == "__main__":
    unittest.main()
