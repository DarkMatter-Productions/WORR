from __future__ import annotations

import base64
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

from tools import atomic_directory
from tools.release import prepare_role_install


class PrepareRoleInstallTrustPolicyTests(unittest.TestCase):
    @staticmethod
    def target_fixture() -> dict[str, object]:
        return {
            "platform_id": "test-platform",
            "os": "test",
            "arch": "test",
            "release_layout": {},
            "client": {
                "launch_exe": "worr",
                "engine_library": "worr_engine.so",
                "local_manifest_name": "worr_install_manifest.json",
                "include": [],
                "exclude": [],
            },
        }

    @staticmethod
    def development_argv(input_dir: pathlib.Path, output_dir: pathlib.Path) -> list[str]:
        return [
            "prepare_role_install.py",
            "--input-dir",
            str(input_dir),
            "--output-dir",
            str(output_dir),
            "--platform-id",
            "test-platform",
            "--role",
            "client",
            "--repo",
            "example/WORR",
            "--channel",
            "development",
            "--version",
            "1.2.3",
            "--allow-unsigned-development-config",
        ]

    @staticmethod
    def write_trust_policy(path: pathlib.Path) -> dict[str, object]:
        public_key = bytes(range(32))
        policy = {
            "schema_version": 1,
            "threshold": 1,
            "keys": [
                {
                    "key_id": hashlib.sha256(public_key).hexdigest(),
                    "algorithm": "ed25519",
                    "public_key": base64.urlsafe_b64encode(public_key)
                    .rstrip(b"=")
                    .decode("ascii"),
                    "repos": ["example/WORR"],
                    "channels": ["stable"],
                }
            ],
        }
        path.write_text(json.dumps(policy), encoding="utf-8")
        return policy

    def test_role_install_embeds_the_validated_public_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            (input_dir / "basew").mkdir(parents=True)
            (input_dir / "basew" / "payload.pak").write_bytes(b"payload")
            output_dir = root / "output"
            policy_path = root / "trust-policy.json"
            expected_policy = self.write_trust_policy(policy_path)
            target = self.target_fixture()
            argv = [
                "prepare_role_install.py",
                "--input-dir",
                str(input_dir),
                "--output-dir",
                str(output_dir),
                "--platform-id",
                "test-platform",
                "--role",
                "client",
                "--repo",
                "example/WORR",
                "--channel",
                "stable",
                "--version",
                "1.2.3",
                "--release-trust-policy",
                str(policy_path),
            ]

            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                prepare_role_install, "get_target", return_value=target
            ):
                self.assertEqual(0, prepare_role_install.main())

            config = json.loads((output_dir / "worr_update.json").read_text(encoding="utf-8"))
            self.assertEqual(expected_policy, config["release_trust_policy"])
            self.assertNotIn("allow_unsigned_development_index", config)
            self.assertNotIn("private_key", json.dumps(config))

    def test_input_and_output_trees_must_be_disjoint(self) -> None:
        for layout in ("same", "output-inside-input", "input-inside-output"):
            with self.subTest(layout=layout), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                if layout == "same":
                    input_dir = root / "tree"
                    output_dir = input_dir
                elif layout == "output-inside-input":
                    input_dir = root / "tree"
                    output_dir = input_dir / "output"
                else:
                    output_dir = root / "tree"
                    input_dir = output_dir / "input"
                input_dir.mkdir(parents=True)
                sentinel = input_dir / "sentinel.bin"
                sentinel.write_bytes(b"preserve me")

                with mock.patch.object(
                    sys,
                    "argv",
                    self.development_argv(input_dir, output_dir),
                ), mock.patch.object(
                    prepare_role_install,
                    "get_target",
                    return_value=self.target_fixture(),
                ), self.assertRaisesRegex(SystemExit, "must be disjoint"):
                    prepare_role_install.main()

                self.assertEqual(b"preserve me", sentinel.read_bytes())

    def test_failed_directory_publication_restores_the_previous_complete_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "new.bin").write_bytes(b"new")
            (input_dir / "basew").mkdir()
            (input_dir / "basew" / "payload.pak").write_bytes(b"payload")
            output_dir = root / "output"
            output_dir.mkdir()
            (output_dir / "old.bin").write_bytes(b"old")
            real_replace = atomic_directory.os.replace
            failed = False

            def fail_new_tree_once(source: object, destination: object) -> None:
                nonlocal failed
                source_path = pathlib.Path(source)
                destination_path = pathlib.Path(destination)
                if (
                    not failed
                    and source_path.name == "prepared-install"
                    and destination_path == output_dir
                ):
                    failed = True
                    raise OSError("injected publication failure")
                real_replace(source, destination)

            with mock.patch.object(
                sys,
                "argv",
                self.development_argv(input_dir, output_dir),
            ), mock.patch.object(
                prepare_role_install,
                "get_target",
                return_value=self.target_fixture(),
            ), mock.patch.object(
                atomic_directory.os,
                "replace",
                side_effect=fail_new_tree_once,
            ), self.assertRaisesRegex(OSError, "injected publication failure"):
                prepare_role_install.main()

            self.assertTrue(failed)
            self.assertEqual(b"old", (output_dir / "old.bin").read_bytes())
            self.assertFalse((output_dir / "new.bin").exists())
            self.assertFalse((output_dir / "worr_update.json").exists())
            self.assertFalse(any(path.name.startswith(".output.") for path in root.iterdir()))

    def test_directory_publication_retries_transient_windows_share_failures(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            staged_dir = root / "staged"
            staged_dir.mkdir()
            (staged_dir / "payload.bin").write_bytes(b"complete")
            output_dir = root / "output"
            real_replace = atomic_directory.os.replace
            attempts = 0

            def fail_twice_then_replace(source: object, destination: object) -> None:
                nonlocal attempts
                attempts += 1
                if attempts <= 2:
                    error = OSError("injected Windows sharing violation")
                    error.winerror = 32
                    raise error
                real_replace(source, destination)

            with mock.patch.object(
                atomic_directory.os,
                "name",
                "nt",
            ), mock.patch.object(
                atomic_directory.os,
                "replace",
                side_effect=fail_twice_then_replace,
            ), mock.patch.object(
                atomic_directory.time,
                "sleep",
            ) as sleep:
                atomic_directory.publish_staged_directory_atomically(
                    staged_dir,
                    output_dir,
                )

            self.assertEqual(3, attempts)
            self.assertEqual(2, sleep.call_count)
            self.assertEqual(b"complete", (output_dir / "payload.bin").read_bytes())

    def test_successful_directory_publication_removes_stale_output_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "new.bin").write_bytes(b"new")
            (input_dir / "basew").mkdir()
            (input_dir / "basew" / "payload.pak").write_bytes(b"payload")
            output_dir = root / "output"
            output_dir.mkdir()
            (output_dir / "stale.bin").write_bytes(b"stale")

            with mock.patch.object(
                sys,
                "argv",
                self.development_argv(input_dir, output_dir),
            ), mock.patch.object(
                prepare_role_install,
                "get_target",
                return_value=self.target_fixture(),
            ):
                self.assertEqual(0, prepare_role_install.main())

            self.assertEqual(b"new", (output_dir / "new.bin").read_bytes())
            self.assertFalse((output_dir / "stale.bin").exists())
            self.assertTrue((output_dir / "worr_update.json").is_file())
            self.assertTrue((output_dir / "worr_install_manifest.json").is_file())

    def test_validation_failure_does_not_replace_the_previous_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            (input_dir / "basew").mkdir(parents=True)
            (input_dir / "basew" / "payload.pak").write_bytes(b"payload")
            output_dir = root / "output"
            output_dir.mkdir()
            (output_dir / "old.bin").write_bytes(b"old")

            with mock.patch.object(
                sys,
                "argv",
                self.development_argv(input_dir, output_dir),
            ), mock.patch.object(
                prepare_role_install,
                "get_target",
                return_value=self.target_fixture(),
            ), mock.patch.object(
                prepare_role_install,
                "MAX_LOCAL_MANIFEST_BYTES",
                1,
            ), self.assertRaisesRegex(SystemExit, "Local install manifest"):
                prepare_role_install.main()

            self.assertEqual(b"old", (output_dir / "old.bin").read_bytes())
            self.assertEqual({"old.bin"}, {path.name for path in output_dir.iterdir()})


if __name__ == "__main__":
    unittest.main()
