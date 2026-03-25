from __future__ import annotations

import unittest

from tools.release.semver import compare_versions, parse_semver


class SemverTests(unittest.TestCase):
    def test_stable_versions_sort_by_core(self) -> None:
        self.assertLess(compare_versions("1.2.3", "1.2.4"), 0)
        self.assertGreater(compare_versions("2.0.0", "1.9.9"), 0)

    def test_stable_is_newer_than_prerelease(self) -> None:
        self.assertGreater(compare_versions("1.2.3", "1.2.3-rc.1"), 0)
        self.assertLess(compare_versions("1.2.3-beta.2", "1.2.3"), 0)

    def test_nightly_versions_sort_by_date_and_revision(self) -> None:
        self.assertLess(
            compare_versions("1.2.3-nightly.20260322.r00000001", "1.2.3-nightly.20260323.r00000001"),
            0,
        )
        self.assertLess(
            compare_versions("1.2.3-nightly.20260323.r00000001", "1.2.3-nightly.20260323.r00000002"),
            0,
        )

    def test_invalid_versions_raise(self) -> None:
        with self.assertRaises(ValueError):
            parse_semver("not-a-version")
        with self.assertRaises(ValueError):
            parse_semver("1.2")


if __name__ == "__main__":
    unittest.main()
