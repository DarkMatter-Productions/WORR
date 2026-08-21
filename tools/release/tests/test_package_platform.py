from __future__ import annotations

import base64
import hashlib
import json
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

from tools.release import package_platform
from tools.release.package_platform import (
    release_trust_command_args,
    resolve_installer_path,
    validate_package_output_location,
)
from tools.release.targets import get_target


class PackagePlatformContractTests(unittest.TestCase):
    def test_release_trust_policy_is_forwarded_to_each_role_package(self) -> None:
        self.assertEqual(
            ["--release-trust-policy", "policy.json"],
            release_trust_command_args(
                "policy.json", allow_unsigned_development_config=False
            ),
        )
        self.assertEqual(
            ["--allow-unsigned-development-config"],
            release_trust_command_args(
                None, allow_unsigned_development_config=True
            ),
        )

    def test_main_threads_one_canonical_policy_snapshot_to_all_role_packages(self) -> None:
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
        role = {
            "package_name": "package.zip",
            "manifest_name": "package.json",
            "update_package_name": "update.zip",
            "update_manifest_name": "update.json",
            "launch_exe": "worr",
            "engine_library": "worr_engine.so",
            "local_manifest_name": "worr_install_manifest.json",
            "include": [],
            "exclude": [],
        }
        target = {
            "platform_id": "test-platform",
            "platform_stub": "test",
            "os": "test",
            "arch": "test",
            "archive_format": "zip",
            "autoupdater": {},
            "release_layout": {},
            "client": dict(role),
            "server": dict(role),
        }
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            policy_path = root / "policy.json"
            policy_path.write_text(json.dumps(policy), encoding="utf-8")
            snapshots: list[tuple[pathlib.Path, dict[str, object]]] = []
            commands: list[list[str]] = []

            def capture_command(command: list[str]) -> None:
                commands.append(command)
                flag_index = command.index("--release-trust-policy")
                snapshot_path = pathlib.Path(command[flag_index + 1])
                snapshots.append(
                    (snapshot_path, json.loads(snapshot_path.read_text(encoding="utf-8")))
                )

            argv = [
                "package_platform.py",
                "--input-dir",
                str(input_dir),
                "--output-dir",
                str(root / "output"),
                "--platform-id",
                "test-platform",
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
                package_platform, "get_target", return_value=target
            ), mock.patch.object(
                package_platform, "stage_release_layout"
            ), mock.patch.object(
                package_platform, "run_command", side_effect=capture_command
            ), mock.patch.object(
                package_platform, "metadata_artifacts", return_value=[]
            ), mock.patch.object(
                package_platform, "metadata_roles", return_value={}
            ):
                self.assertEqual(0, package_platform.main())

            self.assertEqual(4, len(snapshots))
            self.assertTrue(all("--write-config" in command for command in commands))
            self.assertEqual(1, len({path for path, _ in snapshots}))
            self.assertTrue(all(document == policy for _, document in snapshots))

    def test_windows_installer_is_required_and_must_use_the_registry_name(self) -> None:
        target = get_target("windows-x86_64")
        with self.assertRaises(SystemExit):
            resolve_installer_path(target, None)

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            wrong_name = root / "renamed.msi"
            wrong_name.write_bytes(b"installer")
            with self.assertRaises(SystemExit):
                resolve_installer_path(target, str(wrong_name))

            expected = root / target["installer"]["name"]
            expected.write_bytes(b"installer")
            self.assertEqual(expected.resolve(), resolve_installer_path(target, str(expected)))

    def test_non_installer_target_rejects_an_untracked_installer(self) -> None:
        target = get_target("linux-x86_64")
        self.assertIsNone(resolve_installer_path(target, None))
        with self.assertRaises(SystemExit):
            resolve_installer_path(target, "unexpected.msi")

    def test_platform_output_cannot_be_inside_its_input(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()

            for output_dir in (input_dir, input_dir / "release"):
                with self.subTest(output_dir=output_dir), self.assertRaises(SystemExit):
                    validate_package_output_location(input_dir.resolve(), output_dir.resolve())

            validate_package_output_location(input_dir.resolve(), (root / "release").resolve())

            containing_output = root / "containing-output"
            nested_input = containing_output / "input"
            nested_input.mkdir(parents=True)
            with self.assertRaises(SystemExit):
                validate_package_output_location(
                    nested_input.resolve(),
                    containing_output.resolve(),
                )

    def test_metadata_replacement_is_atomic_on_publication_failure(self) -> None:
        role = {
            "role": "client",
            "package_name": "package.zip",
            "manifest_name": "package.json",
            "update_package_name": "update.zip",
            "update_manifest_name": "update.json",
            "launch_exe": "worr",
            "engine_library": "worr_engine.so",
            "local_manifest_name": "worr_install_manifest.json",
            "include": [],
            "exclude": [],
        }
        target = {
            "platform_id": "test-platform",
            "platform_stub": "test",
            "os": "test",
            "arch": "test",
            "archive_format": "zip",
            "autoupdater": {},
            "release_layout": {},
            "client": dict(role),
            "server": {**role, "role": "server"},
        }
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            output_dir = root / "output"
            output_dir.mkdir()
            metadata_path = output_dir / "metadata-test-platform.json"
            metadata_path.write_bytes(b"published metadata")
            real_replace = os.replace
            failed = False

            def fail_metadata_once(source: object, destination: object) -> None:
                nonlocal failed
                if not failed and pathlib.Path(destination) == metadata_path:
                    failed = True
                    raise OSError("injected metadata publication failure")
                real_replace(source, destination)

            argv = [
                "package_platform.py",
                "--input-dir",
                str(input_dir),
                "--output-dir",
                str(output_dir),
                "--platform-id",
                "test-platform",
                "--repo",
                "example/WORR",
                "--channel",
                "development",
                "--version",
                "1.2.3",
                "--allow-unsigned-development-config",
            ]
            with mock.patch.object(sys, "argv", argv), mock.patch.object(
                package_platform, "get_target", return_value=target
            ), mock.patch.object(
                package_platform, "stage_release_layout"
            ), mock.patch.object(
                package_platform, "run_command"
            ), mock.patch.object(
                package_platform, "metadata_artifacts", return_value=[]
            ), mock.patch.object(
                package_platform, "metadata_roles", return_value={}
            ), mock.patch(
                "tools.package_release.os.replace",
                side_effect=fail_metadata_once,
            ), self.assertRaisesRegex(OSError, "injected metadata publication failure"):
                package_platform.main()

            self.assertTrue(failed)
            self.assertEqual(b"published metadata", metadata_path.read_bytes())
            self.assertEqual(
                {"metadata-test-platform.json"},
                {path.name for path in output_dir.iterdir()},
            )


if __name__ == "__main__":
    unittest.main()
