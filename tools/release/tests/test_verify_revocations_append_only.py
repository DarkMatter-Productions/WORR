from __future__ import annotations

import unittest

from tools.release.verify_revocations_append_only import require_append_only


class VerifyRevocationsAppendOnlyTests(unittest.TestCase):
    def test_additions_and_new_channels_are_allowed(self) -> None:
        require_append_only(
            {"stable": {"a" * 64}},
            {"stable": {"a" * 64, "b" * 64}, "nightly": set()},
        )

    def test_digest_removal_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "removed from stable"):
            require_append_only({"stable": {"a" * 64}}, {"stable": set()})

    def test_channel_removal_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "channels were removed"):
            require_append_only({"stable": set()}, {})


if __name__ == "__main__":
    unittest.main()
