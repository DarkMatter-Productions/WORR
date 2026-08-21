from __future__ import annotations

import os
import pathlib
import stat
import tempfile
import unittest

from tools.release import validate_stage


class ValidateExactStageContractTests(unittest.TestCase):
    @staticmethod
    def target(*, required: list[str], forbidden: list[str]) -> dict[str, object]:
        return {
            "client": {
                "update_required_paths": required,
                "update_forbidden_paths": forbidden,
            }
        }

    @staticmethod
    def write(root: pathlib.Path, relative: str, payload: bytes = b"payload") -> pathlib.Path:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        return path

    def test_valid_stage_requires_exact_empty_runtime_lock(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.write(root, "worr.exe")
            lock = self.write(root, "worr_install.lock", b"")
            lock.chmod(0o644)

            validate_stage.validate_exact_stage_contract(
                root,
                self.target(
                    required=["worr.exe", "worr_install.lock", "worr_update.json"],
                    forbidden=["basew/config.cfg"],
                ),
            )

    def test_forbidden_runtime_user_data_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.write(root, "worr.exe")
            lock = self.write(root, "worr_install.lock", b"")
            lock.chmod(0o644)
            self.write(root, "basew/config.cfg", b"private cvars")

            with self.assertRaisesRegex(SystemExit, "forbidden runtime/user data"):
                validate_stage.validate_exact_stage_contract(
                    root,
                    self.target(
                        required=["worr.exe", "worr_install.lock"],
                        forbidden=["basew/config.cfg"],
                    ),
                )

    def test_missing_required_file_and_nonempty_lock_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            lock = self.write(root, "worr_install.lock", b"")
            lock.chmod(0o644)
            with self.assertRaisesRegex(SystemExit, "Missing required"):
                validate_stage.validate_exact_stage_contract(
                    root,
                    self.target(
                        required=["worr.exe", "worr_install.lock"],
                        forbidden=[],
                    ),
                )

            self.write(root, "worr.exe")
            lock.write_bytes(b"changed")
            with self.assertRaisesRegex(SystemExit, "must be an empty file"):
                validate_stage.validate_exact_stage_contract(
                    root,
                    self.target(
                        required=["worr.exe", "worr_install.lock"],
                        forbidden=[],
                    ),
                )

    @unittest.skipIf(os.name == "nt", "POSIX modes are not represented on Windows")
    def test_runtime_lock_mode_is_exact_on_posix(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.write(root, "worr.exe")
            lock = self.write(root, "worr_install.lock", b"")
            lock.chmod(0o600)
            self.assertEqual(0o600, stat.S_IMODE(lock.stat().st_mode))

            with self.assertRaisesRegex(SystemExit, "mode 0644"):
                validate_stage.validate_exact_stage_contract(
                    root,
                    self.target(
                        required=["worr.exe", "worr_install.lock"],
                        forbidden=[],
                    ),
                )

    @unittest.skipIf(os.name == "nt", "case aliases cannot be created on Windows")
    def test_portable_case_collisions_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.write(root, "Data.bin")
            self.write(root, "data.bin")
            with self.assertRaisesRegex(SystemExit, "colliding portable paths"):
                validate_stage.validate_exact_stage_contract(
                    root,
                    self.target(required=[], forbidden=[]),
                )


if __name__ == "__main__":
    unittest.main()
