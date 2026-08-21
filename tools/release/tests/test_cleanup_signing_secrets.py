from __future__ import annotations

import os
import pathlib
import tempfile
import unittest
from unittest import mock

from tools.release import cleanup_signing_secrets


class CleanupSigningSecretsTests(unittest.TestCase):
    def test_removes_only_direct_prefixed_child_of_runner_temp(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            secret_dir = root / "worr-release-signing-test"
            secret_dir.mkdir()
            (secret_dir / "key.json").write_text("private", encoding="utf-8")
            unrelated = root / "unrelated"
            unrelated.mkdir()

            argv = [
                "cleanup_signing_secrets.py",
                "--secret-dir",
                str(secret_dir),
                "--temp-root",
                str(root),
            ]
            with mock.patch("sys.argv", argv):
                self.assertEqual(0, cleanup_signing_secrets.main())

            self.assertFalse(secret_dir.exists())
            self.assertTrue(unrelated.is_dir())

    def test_missing_export_is_a_safe_noop(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch(
            "sys.argv", ["cleanup_signing_secrets.py"]
        ):
            self.assertEqual(0, cleanup_signing_secrets.main())

    def test_rejects_unprefixed_outside_and_link_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            outside = root.parent / f"outside-{root.name}"
            outside.mkdir()
            self.addCleanup(lambda: outside.rmdir() if outside.exists() else None)

            cases = [root / "ordinary", outside / "worr-release-signing-outside"]
            cases[0].mkdir()
            cases[1].mkdir()
            self.addCleanup(lambda: cases[1].rmdir() if cases[1].exists() else None)
            for candidate in cases:
                with self.subTest(candidate=candidate), self.assertRaises(SystemExit):
                    cleanup_signing_secrets.validate_secret_directory(candidate, root)
                self.assertTrue(candidate.is_dir())

            link = root / "worr-release-signing-link"
            try:
                link.symlink_to(cases[0], target_is_directory=True)
            except OSError:
                return
            with self.assertRaises(SystemExit):
                cleanup_signing_secrets.validate_secret_directory(link, root)
            self.assertTrue(cases[0].is_dir())


if __name__ == "__main__":
    unittest.main()
