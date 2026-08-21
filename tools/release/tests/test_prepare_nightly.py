from __future__ import annotations

import argparse
import datetime as dt
import unittest

from tools.release.prepare_nightly import parse_utc_timestamp


class PrepareNightlyTests(unittest.TestCase):
    def test_run_creation_timestamp_is_normalized_to_utc(self) -> None:
        self.assertEqual(
            dt.datetime(2026, 8, 3, 23, 55, tzinfo=dt.timezone.utc),
            parse_utc_timestamp("2026-08-04T01:55:00+02:00"),
        )

    def test_timestamp_requires_timezone(self) -> None:
        with self.assertRaises(argparse.ArgumentTypeError):
            parse_utc_timestamp("2026-08-03T23:55:00")

    def test_timestamp_accepts_github_rfc3339_form(self) -> None:
        self.assertEqual(
            dt.datetime(2026, 8, 3, 23, 55, tzinfo=dt.timezone.utc),
            parse_utc_timestamp("2026-08-03T23:55:00Z"),
        )


if __name__ == "__main__":
    unittest.main()
