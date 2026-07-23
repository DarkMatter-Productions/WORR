#!/usr/bin/env python3
"""Regression checks for the deterministic binary-alpha fence fixture."""

from __future__ import annotations

import unittest
from pathlib import Path

try:
    from . import generate_fence_cutout_fixture as fixture
except ImportError:
    import generate_fence_cutout_fixture as fixture


ROOT = Path(__file__).resolve().parents[2]


class FenceCutoutFixtureTests(unittest.TestCase):
    def test_authored_outputs_are_current(self) -> None:
        for path, data in fixture.generated_outputs(ROOT / "assets").items():
            self.assertEqual(path.read_bytes(), data, path)

    def test_binary_fence_contains_both_coverage_states(self) -> None:
        alphas = fixture.fence_pixels()[3::4]
        self.assertIn(0, alphas)
        self.assertIn(255, alphas)


if __name__ == "__main__":
    unittest.main()
