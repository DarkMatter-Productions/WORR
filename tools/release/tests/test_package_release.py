from __future__ import annotations

import base64
import hashlib
import json
import os
import pathlib
import sys
import tempfile
import tarfile
import unittest
import zipfile
from unittest import mock

from tools import package_release


class PackageReleasePathTests(unittest.TestCase):
    def run_package_release(
        self,
        input_dir: pathlib.Path,
        output_dir: pathlib.Path,
        *extra_args: str,
        trust_policy: pathlib.Path | None = None,
        channel: str | None = None,
        allow_unsigned_development: bool | None = None,
    ) -> int:
        if channel is None:
            channel = "stable" if trust_policy else "development"
        if allow_unsigned_development is None:
            allow_unsigned_development = trust_policy is None
        trust_args: list[str] = []
        if trust_policy is not None:
            trust_args.extend(["--release-trust-policy", str(trust_policy)])
        if allow_unsigned_development:
            trust_args.append("--allow-unsigned-development-config")
        argv = [
            "package_release.py",
            "--input-dir",
            str(input_dir),
            "--output-dir",
            str(output_dir),
            "--package-name",
            "payload.zip",
            "--manifest-name",
            "payload.json",
            "--version",
            "1.2.3",
            "--repo",
            "example/WORR",
            "--role",
            "client",
            "--channel",
            channel,
            "--launch-exe",
            "worr_x86_64.exe",
            "--engine-library",
            "worr_engine_x86_64.dll",
            "--release-index-asset",
            f"worr-release-index-{channel}.json",
            *trust_args,
            *extra_args,
        ]
        with mock.patch.object(sys, "argv", argv):
            return package_release.main()

    @staticmethod
    def write_trust_policy(
        path: pathlib.Path,
        *,
        repo: str = "example/WORR",
        channel: str = "stable",
        extra_key_fields: dict[str, object] | None = None,
    ) -> dict[str, object]:
        public_key = bytes(range(32))
        key = {
            "key_id": hashlib.sha256(public_key).hexdigest(),
            "algorithm": "ed25519",
            "public_key": base64.urlsafe_b64encode(public_key).rstrip(b"=").decode("ascii"),
            "repos": [repo],
            "channels": [channel],
        }
        key.update(extra_key_fields or {})
        document = {"schema_version": 1, "threshold": 1, "keys": [key]}
        path.write_text(json.dumps(document), encoding="utf-8")
        return document

    def test_production_config_embeds_only_the_validated_public_trust_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")
            policy_path = root / "trust-policy.json"
            expected_policy = self.write_trust_policy(policy_path)

            result = self.run_package_release(
                input_dir,
                root / "output",
                "--write-config",
                trust_policy=policy_path,
            )

            self.assertEqual(0, result)
            config = json.loads((input_dir / "worr_update.json").read_text(encoding="utf-8"))
            self.assertEqual(expected_policy, config["release_trust_policy"])
            self.assertNotIn("allow_unsigned_development_index", config)
            self.assertNotIn("private_key", json.dumps(config))

    def test_production_config_fails_closed_without_a_trust_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")

            with self.assertRaisesRegex(SystemExit, "requires --release-trust-policy"):
                self.run_package_release(
                    input_dir,
                    root / "output",
                    channel="stable",
                    allow_unsigned_development=False,
                )

            self.assertFalse((root / "output").exists())

    def test_production_package_cannot_skip_writing_the_validated_config(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")
            policy_path = root / "trust-policy.json"
            self.write_trust_policy(policy_path)

            with self.assertRaisesRegex(SystemExit, "requires --write-config"):
                self.run_package_release(
                    input_dir,
                    root / "output",
                    trust_policy=policy_path,
                )

            self.assertFalse((root / "output").exists())

    def test_unsigned_config_escape_hatch_is_development_only(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")

            with self.assertRaisesRegex(SystemExit, "restricted"):
                self.run_package_release(
                    input_dir,
                    root / "output",
                    channel="nightly",
                    allow_unsigned_development=True,
                )

            result = self.run_package_release(
                input_dir,
                root / "development-output",
                "--write-config",
            )
            self.assertEqual(0, result)
            config = json.loads((input_dir / "worr_update.json").read_text(encoding="utf-8"))
            self.assertTrue(config["allow_unsigned_development_index"])
            self.assertNotIn("release_trust_policy", config)

    def test_policy_with_private_or_unknown_key_fields_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")
            policy_path = root / "trust-policy.json"
            self.write_trust_policy(policy_path, extra_key_fields={"private_key": "forbidden"})

            with self.assertRaisesRegex(SystemExit, "unexpected private_key"):
                self.run_package_release(
                    input_dir,
                    root / "output",
                    trust_policy=policy_path,
                )

    def test_policy_must_authorize_the_packaged_repo_and_channel_threshold(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")
            policy_path = root / "trust-policy.json"
            self.write_trust_policy(policy_path, repo="different/WORR")

            with self.assertRaisesRegex(SystemExit, "threshold cannot be met"):
                self.run_package_release(
                    input_dir,
                    root / "output",
                    trust_policy=policy_path,
                )

    def test_mapped_paths_are_canonicalized_and_packaged(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            source = input_dir / "bin" / "engine.dll"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"engine")

            result = self.run_package_release(
                input_dir,
                output_dir,
                "--mapped-file",
                "bin\\engine.dll=runtime\\engine.dll",
            )

            self.assertEqual(0, result)
            with zipfile.ZipFile(output_dir / "payload.zip") as archive:
                self.assertEqual(
                    {"runtime/engine.dll", "worr_install_manifest.json"},
                    set(archive.namelist()),
                )
            manifest = json.loads((output_dir / "payload.json").read_text(encoding="utf-8"))
            self.assertEqual(["runtime/engine.dll"], [entry["path"] for entry in manifest["files"]])

    def test_mapped_source_does_not_suppress_a_distinct_hard_link_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            input_dir.mkdir()
            source = input_dir / "source.bin"
            alias = input_dir / "alias.bin"
            source.write_bytes(b"shared inode")
            try:
                os.link(source, alias)
            except OSError as exc:
                self.skipTest(f"hard links are unavailable: {exc}")

            result = self.run_package_release(
                input_dir,
                output_dir,
                "--mapped-file",
                "source.bin=runtime/source.bin",
            )

            self.assertEqual(0, result)
            with zipfile.ZipFile(output_dir / "payload.zip") as archive:
                self.assertEqual(
                    {"alias.bin", "runtime/source.bin", "worr_install_manifest.json"},
                    set(archive.namelist()),
                )

    def test_tar_materializes_distinct_hard_link_aliases_as_regular_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "source.bin"
            alias = root / "alias.bin"
            source.write_bytes(b"shared inode")
            try:
                os.link(source, alias)
            except OSError as exc:
                self.skipTest(f"hard links are unavailable: {exc}")

            package_path = root / "payload.tar.gz"
            package_release.create_archive(
                package_path,
                "tar.gz",
                [(source, "source.bin"), (alias, "alias.bin")],
            )

            with tarfile.open(package_path, "r:gz") as archive:
                members = {member.name: member for member in archive.getmembers()}
                self.assertTrue(members["source.bin"].isreg())
                self.assertTrue(members["alias.bin"].isreg())
                self.assertEqual(b"shared inode", archive.extractfile(members["alias.bin"]).read())

    def test_generated_local_manifest_atomically_breaks_a_payload_hard_link(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            input_dir.mkdir()
            payload = input_dir / "payload.bin"
            local_manifest = input_dir / "worr_install_manifest.json"
            payload.write_bytes(b"payload remains unchanged")
            try:
                os.link(payload, local_manifest)
            except OSError as exc:
                self.skipTest(f"hard links are unavailable: {exc}")

            result = self.run_package_release(input_dir, output_dir)

            self.assertEqual(0, result)
            self.assertEqual(b"payload remains unchanged", payload.read_bytes())
            with zipfile.ZipFile(output_dir / "payload.zip") as archive:
                self.assertEqual(b"payload remains unchanged", archive.read("payload.bin"))

    @unittest.skipUnless(os.name == "nt", "case-insensitive source lookup is Windows-specific")
    def test_mapped_source_case_variant_is_not_packaged_twice_on_windows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            source = input_dir / "bin" / "engine.dll"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"engine")

            result = self.run_package_release(
                input_dir,
                output_dir,
                "--mapped-file",
                "BIN/ENGINE.DLL=runtime/engine.dll",
            )

            self.assertEqual(0, result)
            with zipfile.ZipFile(output_dir / "payload.zip") as archive:
                self.assertEqual(
                    {"runtime/engine.dll", "worr_install_manifest.json"},
                    set(archive.namelist()),
                )

    def test_case_variant_of_generated_local_manifest_is_not_archived(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")
            (input_dir / "WORR_INSTALL_MANIFEST.JSON").write_text("stale", encoding="utf-8")

            result = self.run_package_release(input_dir, output_dir)

            self.assertEqual(0, result)
            with zipfile.ZipFile(output_dir / "payload.zip") as archive:
                names = archive.namelist()
            self.assertEqual(1, sum(name.casefold() == "worr_install_manifest.json" for name in names))
            self.assertNotIn("WORR_INSTALL_MANIFEST.JSON", names)

    def test_required_local_manifest_bypasses_positive_include_filters(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")

            result = self.run_package_release(
                input_dir,
                root / "output",
                "--include",
                "payload.bin",
            )

            self.assertEqual(0, result)
            with zipfile.ZipFile(root / "output" / "payload.zip") as archive:
                self.assertIn("worr_install_manifest.json", archive.namelist())

    def test_required_local_manifest_cannot_be_excluded(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")

            with self.assertRaises(SystemExit):
                self.run_package_release(
                    input_dir,
                    root / "output",
                    "--exclude",
                    "worr_install_manifest.json",
                )

    def test_mapped_source_and_destination_traversal_is_rejected(self) -> None:
        invalid_entries = (
            "../secret.txt=safe.txt",
            "/absolute.txt=safe.txt",
            "C:/secret.txt=safe.txt",
            "safe.txt=../escape.txt",
            "safe.txt=/absolute.txt",
            "safe.txt=C:/escape.txt",
            "safe.txt=dir/./escape.txt",
            "safe.txt=dir//escape.txt",
            "safe.txt=dir/NUL.txt",
            "safe.txt=dir/CONIN$",
            "safe.txt=dir/conout$.log",
            "safe.txt=dir/CLOCK$",
            "safe.txt=dir/COM¹.txt",
            "safe.txt=dir/LPT³.cfg",
        )
        for entry in invalid_entries:
            with self.subTest(entry=entry), self.assertRaises(SystemExit):
                package_release.parse_mapped_files([entry])

    def test_overlong_archive_paths_are_rejected(self) -> None:
        with self.assertRaises(SystemExit):
            package_release.validated_archive_path("a" * 256, "test path")
        with self.assertRaises(SystemExit):
            package_release.validated_archive_path("a/" * 2048 + "b", "test path")

    def test_non_ascii_archive_paths_are_rejected_for_cross_platform_identity(self) -> None:
        for path in ("café.dat", "cafe\u0301.dat", "basew/Δ.dat"):
            with self.subTest(path=path), self.assertRaises(SystemExit):
                package_release.validated_archive_path(path, "test path")

    def test_updater_limit_helpers_allow_exact_boundaries_and_reject_overflow(self) -> None:
        cases = (
            (package_release.validate_managed_file_count, package_release.MAX_MANAGED_FILES),
            (package_release.validate_update_manifest_size, package_release.MAX_UPDATE_MANIFEST_BYTES),
            (package_release.validate_update_package_size, package_release.MAX_UPDATE_PACKAGE_BYTES),
            (package_release.validate_install_journal_size, package_release.MAX_INSTALL_JOURNAL_BYTES),
        )
        for validator, limit in cases:
            with self.subTest(validator=validator.__name__):
                validator(limit)
                with self.assertRaises(SystemExit):
                    validator(limit + 1)

        self.assertEqual(
            128
            + 64
            + 2 * len("basew/café.dat".encode("utf-8"))
            + 64
            + 2 * len("state.json".encode("utf-8")),
            package_release.estimate_install_journal_size(
                ["basew/café.dat"],
                "state.json",
            ),
        )

    def test_count_and_journal_preflight_run_before_archive_creation(self) -> None:
        cases = (
            ("MAX_MANAGED_FILES", 0, "managed files"),
            ("MAX_INSTALL_JOURNAL_BYTES", 0, "install journal"),
        )
        for constant_name, limit, message in cases:
            with self.subTest(limit=constant_name), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                input_dir = root / "input"
                input_dir.mkdir()
                (input_dir / "payload.bin").write_bytes(b"payload")
                output_dir = root / "output"

                with (
                    mock.patch.object(package_release, constant_name, limit),
                    mock.patch.object(package_release, "create_archive") as create_archive,
                    self.assertRaisesRegex(SystemExit, message),
                ):
                    self.run_package_release(input_dir, output_dir)

                create_archive.assert_not_called()
                self.assertFalse((output_dir / "payload.zip").exists())
                self.assertFalse((output_dir / "payload.json").exists())

    def test_oversized_staged_outputs_do_not_replace_published_artifacts(self) -> None:
        cases = (
            ("MAX_UPDATE_PACKAGE_BYTES", 1, "Update package"),
            ("MAX_UPDATE_MANIFEST_BYTES", 1, "Update manifest"),
        )
        for constant_name, limit, message in cases:
            with self.subTest(limit=constant_name), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                input_dir = root / "input"
                input_dir.mkdir()
                (input_dir / "payload.bin").write_bytes(b"payload")
                output_dir = root / "output"
                output_dir.mkdir()
                package_path = output_dir / "payload.zip"
                manifest_path = output_dir / "payload.json"
                package_path.write_bytes(b"published package")
                manifest_path.write_bytes(b"published manifest")

                with (
                    mock.patch.object(package_release, constant_name, limit),
                    self.assertRaisesRegex(SystemExit, message),
                ):
                    self.run_package_release(input_dir, output_dir)

                self.assertEqual(b"published package", package_path.read_bytes())
                self.assertEqual(b"published manifest", manifest_path.read_bytes())
                self.assertEqual(
                    {"payload.zip", "payload.json"},
                    {path.name for path in output_dir.iterdir()},
                )

    def test_output_directory_cannot_overlap_the_package_input(self) -> None:
        for output_kind in ("same", "nested", "ancestor"):
            with self.subTest(output_kind=output_kind), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                if output_kind == "ancestor":
                    output_dir = root / "output"
                    input_dir = output_dir / "input"
                else:
                    input_dir = root / "input"
                    output_dir = input_dir if output_kind == "same" else input_dir / "release"
                input_dir.mkdir(parents=True)
                sentinel = input_dir / "sentinel.bin"
                sentinel.write_bytes(b"preserve me")

                with self.assertRaises(SystemExit):
                    self.run_package_release(input_dir, output_dir, "--write-config")

                self.assertEqual(b"preserve me", sentinel.read_bytes())
                self.assertFalse((input_dir / "worr_update.json").exists())

    def test_package_and_manifest_publication_rolls_back_as_one_set(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"new payload")
            output_dir = root / "output"
            output_dir.mkdir()
            package_path = output_dir / "payload.zip"
            manifest_path = output_dir / "payload.json"
            package_path.write_bytes(b"published package")
            manifest_path.write_bytes(b"published manifest")
            real_replace = package_release.os.replace
            failed = False

            def fail_manifest_once(source: object, destination: object) -> None:
                nonlocal failed
                source_path = pathlib.Path(source)
                destination_path = pathlib.Path(destination)
                if (
                    not failed
                    and destination_path == manifest_path
                    and source_path.name.startswith(".payload.json.")
                    and ".backup." not in source_path.name
                ):
                    failed = True
                    raise OSError("injected manifest publication failure")
                real_replace(source, destination)

            with mock.patch.object(
                package_release.os,
                "replace",
                side_effect=fail_manifest_once,
            ), self.assertRaisesRegex(OSError, "injected manifest publication failure"):
                self.run_package_release(input_dir, output_dir)

            self.assertTrue(failed)
            self.assertEqual(b"published package", package_path.read_bytes())
            self.assertEqual(b"published manifest", manifest_path.read_bytes())
            self.assertEqual(
                {"payload.zip", "payload.json"},
                {path.name for path in output_dir.iterdir()},
            )

    def test_failed_first_publication_does_not_leave_a_partial_new_set(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"new payload")
            output_dir = root / "output"
            real_replace = package_release.os.replace
            failed = False

            def fail_manifest_once(source: object, destination: object) -> None:
                nonlocal failed
                source_path = pathlib.Path(source)
                destination_path = pathlib.Path(destination)
                if (
                    not failed
                    and destination_path == output_dir / "payload.json"
                    and source_path.name.startswith(".payload.json.")
                ):
                    failed = True
                    raise OSError("injected manifest publication failure")
                real_replace(source, destination)

            with mock.patch.object(
                package_release.os,
                "replace",
                side_effect=fail_manifest_once,
            ), self.assertRaisesRegex(OSError, "injected manifest publication failure"):
                self.run_package_release(input_dir, output_dir)

            self.assertTrue(failed)
            self.assertEqual(set(), {path.name for path in output_dir.iterdir()})

    def test_generated_artifact_paths_must_be_plain_distinct_filenames(self) -> None:
        for value in ("../payload.zip", "/payload.zip", "C:/payload.zip", "dir/payload.zip"):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                package_release.validated_archive_filename(value, "--package-name")

        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "payload.bin").write_bytes(b"payload")
            argv = [
                "package_release.py",
                "--input-dir",
                str(input_dir),
                "--output-dir",
                str(root / "output"),
                "--package-name",
                "same.zip",
                "--manifest-name",
                "SAME.ZIP",
                "--version",
                "1.2.3",
                "--repo",
                "example/WORR",
                "--role",
                "client",
                "--channel",
                "development",
                "--launch-exe",
                "worr",
                "--engine-library",
                "worr_engine.so",
                "--release-index-asset",
                "index.json",
                "--allow-unsigned-development-config",
            ]
            with mock.patch.object(sys, "argv", argv), self.assertRaises(SystemExit):
                package_release.main()

    def test_generated_local_manifest_cannot_be_used_as_a_mapped_source(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            (input_dir / "worr_install_manifest.json").write_text("stale", encoding="utf-8")

            with self.assertRaises(SystemExit):
                self.run_package_release(
                    input_dir,
                    root / "output",
                    "--mapped-file",
                    "worr_install_manifest.json=runtime/old-manifest.json",
                )

    def test_symbolic_link_source_cannot_escape_the_input_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            input_dir.mkdir()
            outside = root / "secret.txt"
            outside.write_text("secret", encoding="utf-8")
            link = input_dir / "payload.txt"
            try:
                os.symlink(outside, link)
            except OSError as exc:
                self.skipTest(f"symbolic links are unavailable: {exc}")

            with self.assertRaises(SystemExit):
                package_release.require_packaged_source(input_dir.resolve(), link, "test source")


if __name__ == "__main__":
    unittest.main()
